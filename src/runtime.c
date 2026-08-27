/*
 * pox - PHP CLI embedded in Rust
 *
 * This file provides the C interface to PHP's embed SAPI.
 * Inspired by FrankenPHP's approach to embedding PHP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

#include "pox_php_runtime.h"

#include <sapi/embed/php_embed.h>
#include <php.h>
#include <php_main.h>
#include <php_variables.h>
#include <php_output.h>
#include <SAPI.h>
#include <Zend/zend.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_modules.h>
#include <Zend/zend_compile.h>
#include <Zend/zend_extensions.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include <unicode/uvernum.h>
#include <libxml/xmlversion.h>
#include <openssl/opensslv.h>
#include <ext/pcre/pcre2lib/pcre2.h>
#include <zlib.h>
#include <curl/curlver.h>

/* ============================================================================
 * Common / CLI Mode
 * ============================================================================ */

/* Register CLI-specific variables in $_SERVER */
static char *pox_script_filename = NULL;
static char *pox_ini_entries = NULL;

static void pox_register_variables(zval *track_vars_array) {
    /* Import environment variables */
    php_import_environment_variables(track_vars_array);

    if (pox_script_filename != NULL) {
        size_t len = strlen(pox_script_filename);
        php_register_variable_safe("PHP_SELF", pox_script_filename, len, track_vars_array);
        php_register_variable_safe("SCRIPT_NAME", pox_script_filename, len, track_vars_array);
        php_register_variable_safe("SCRIPT_FILENAME", pox_script_filename, len, track_vars_array);
        php_register_variable_safe("PATH_TRANSLATED", pox_script_filename, len, track_vars_array);
    }

    php_register_variable_safe("DOCUMENT_ROOT", "", 0, track_vars_array);
}

/* Register STDIN, STDOUT, STDERR constants */
static void pox_register_file_handles(void) {
    php_stream *s_in, *s_out, *s_err;
    php_stream_context *sc_in = NULL, *sc_out = NULL, *sc_err = NULL;
    zend_constant ic, oc, ec;

    s_in = php_stream_open_wrapper_ex("php://stdin", "rb", 0, NULL, sc_in);
    s_out = php_stream_open_wrapper_ex("php://stdout", "wb", 0, NULL, sc_out);
    s_err = php_stream_open_wrapper_ex("php://stderr", "wb", 0, NULL, sc_err);

    if (s_in) s_in->flags |= PHP_STREAM_FLAG_NO_RSCR_DTOR_CLOSE;
    if (s_out) s_out->flags |= PHP_STREAM_FLAG_NO_RSCR_DTOR_CLOSE;
    if (s_err) s_err->flags |= PHP_STREAM_FLAG_NO_RSCR_DTOR_CLOSE;

    if (s_in == NULL || s_out == NULL || s_err == NULL) {
        if (s_in) php_stream_close(s_in);
        if (s_out) php_stream_close(s_out);
        if (s_err) php_stream_close(s_err);
        return;
    }

    php_stream_to_zval(s_in, &ic.value);
    php_stream_to_zval(s_out, &oc.value);
    php_stream_to_zval(s_err, &ec.value);

    ZEND_CONSTANT_SET_FLAGS(&ic, CONST_CS, 0);
    ic.name = zend_string_init_interned("STDIN", sizeof("STDIN") - 1, 0);
    zend_register_constant(&ic);

    ZEND_CONSTANT_SET_FLAGS(&oc, CONST_CS, 0);
    oc.name = zend_string_init_interned("STDOUT", sizeof("STDOUT") - 1, 0);
    zend_register_constant(&oc);

    ZEND_CONSTANT_SET_FLAGS(&ec, CONST_CS, 0);
    ec.name = zend_string_init_interned("STDERR", sizeof("STDERR") - 1, 0);
    zend_register_constant(&ec);
}

/* Set INI entries before initialization */
void pox_set_ini_entries(const char *entries) {
    if (pox_ini_entries != NULL) {
        free(pox_ini_entries);
    }
    if (entries != NULL) {
        pox_ini_entries = strdup(entries);
    } else {
        pox_ini_entries = NULL;
    }
}

/* Parse and apply ini entries after PHP startup */
static void pox_apply_ini_entries(void) {
    if (pox_ini_entries == NULL) {
        return;
    }

    char *entries = strdup(pox_ini_entries);
    char *line = strtok(entries, "\n");

    while (line != NULL) {
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;

            zend_string *key_str = zend_string_init(key, strlen(key), 0);
            zend_alter_ini_entry_chars(key_str, value, strlen(value),
                                       ZEND_INI_USER, ZEND_INI_STAGE_RUNTIME);
            zend_string_release(key_str);
        }
        line = strtok(NULL, "\n");
    }

    free(entries);
}

/* Internal initialization helper */
static int pox_init(int argc, char **argv) {
    php_embed_module.name = "cli";
    php_embed_module.pretty_name = "PHP CLI embedded";
    php_embed_module.register_server_variables = pox_register_variables;
    php_embed_module.phpinfo_as_text = 1;  /* Output phpinfo as plain text, not HTML */

    if (php_embed_init(argc, argv) != SUCCESS) {
        return 1;
    }

    pox_register_file_handles();

    /* Apply INI entries after startup */
    pox_apply_ini_entries();

    return 0;
}

/*
 * Execute a PHP script file.
 * Returns the exit status code.
 */
int pox_execute_script(const char *script_path, int argc, char **argv) {
    int exit_status = 0;

    pox_script_filename = (char *)script_path;

    if (pox_init(argc, argv) != 0) {
        return 1;
    }

    zend_first_try {
        zend_file_handle file_handle;
        zend_stream_init_filename(&file_handle, script_path);

        /* Skip shebang line if present */
        CG(skip_shebang) = 1;

        php_execute_script(&file_handle);
        exit_status = EG(exit_status);
    } zend_catch {
        exit_status = EG(exit_status);
    } zend_end_try();

    php_embed_shutdown();
    pox_script_filename = NULL;

    return exit_status;
}

/*
 * Execute PHP code passed as a string (like php -r).
 * Returns the exit status code.
 */
int pox_execute_code(const char *code, int argc, char **argv) {
    int exit_status = 0;

    pox_script_filename = "Command line code";

    if (pox_init(argc, argv) != 0) {
        return 1;
    }

    zend_first_try {
        zend_eval_string_ex((char *)code, NULL, "Command line code", 1);
        exit_status = EG(exit_status);
    } zend_catch {
        exit_status = EG(exit_status);
    } zend_end_try();

    php_embed_shutdown();
    pox_script_filename = NULL;

    return exit_status;
}

/*
 * Syntax check a PHP file (lint).
 * Returns 0 if syntax is valid, 1 otherwise.
 */
int pox_lint_file(const char *script_path, int argc, char **argv) {
    int result = 0;

    pox_script_filename = (char *)script_path;

    if (pox_init(argc, argv) != 0) {
        return 1;
    }

    zend_first_try {
        zend_file_handle file_handle;
        zend_stream_init_filename(&file_handle, script_path);

        CG(skip_shebang) = 1;

        zend_op_array *op_array = zend_compile_file(&file_handle, ZEND_REQUIRE);

        if (op_array != NULL) {
            destroy_op_array(op_array);
            efree(op_array);
            printf("No syntax errors detected in %s\n", script_path);
            result = 0;
        } else {
            result = 1;
        }

        zend_destroy_file_handle(&file_handle);
    } zend_catch {
        result = 1;
    } zend_end_try();

    php_embed_shutdown();
    pox_script_filename = NULL;

    return result;
}

/*
 * Print phpinfo() output.
 * flag: -1 for all, or specific PHP_INFO_* constant
 */
int pox_info(int flag, int argc, char **argv) {
    pox_script_filename = "phpinfo";

    if (pox_init(argc, argv) != 0) {
        return 1;
    }

    zend_first_try {
        php_print_info(flag == -1 ? PHP_INFO_ALL : (unsigned int)flag);
    } zend_catch {
    } zend_end_try();

    php_embed_shutdown();
    pox_script_filename = NULL;

    return 0;
}

/*
 * Print loaded modules.
 */
int pox_print_modules(int argc, char **argv) {
    pox_script_filename = "modules";

    if (pox_init(argc, argv) != 0) {
        return 1;
    }

    zend_first_try {
        zend_module_entry *module;

        printf("[PHP Modules]\n");
        ZEND_HASH_MAP_FOREACH_PTR(&module_registry, module) {
            printf("%s\n", module->name);
        } ZEND_HASH_FOREACH_END();

        printf("\n[Zend Modules]\n");
        zend_llist_position pos;
        zend_extension *ext = (zend_extension *)zend_llist_get_first_ex(&zend_extensions, &pos);
        while (ext) {
            printf("%s\n", ext->name);
            ext = (zend_extension *)zend_llist_get_next_ex(&zend_extensions, &pos);
        }
    } zend_catch {
    } zend_end_try();

    php_embed_shutdown();
    pox_script_filename = NULL;

    return 0;
}

/*
 * Get PHP version string.
 */
const char *pox_get_version(void) {
    return PHP_VERSION;
}

/*
 * Get PHP version ID (e.g., 80300 for PHP 8.3.0).
 */
int pox_get_version_id(void) {
    return PHP_VERSION_ID;
}

/*
 * Get Zend Engine version string.
 */
const char *pox_get_zend_version(void) {
    return ZEND_VERSION;
}

/*
 * Check if PHP is built with debug mode.
 */
int pox_is_debug(void) {
#ifdef ZEND_DEBUG
    return ZEND_DEBUG;
#else
    return 0;
#endif
}

/*
 * Check if PHP is built with ZTS (thread safety).
 */
int pox_is_zts(void) {
#ifdef ZTS
    return 1;
#else
    return 0;
#endif
}

/*
 * Get ICU version (from intl extension).
 * Returns NULL if not available.
 */
const char *pox_get_icu_version(void) {
#ifdef U_ICU_VERSION
    return U_ICU_VERSION;
#else
    return NULL;
#endif
}

/*
 * Get libxml version.
 */
const char *pox_get_libxml_version(void) {
#ifdef LIBXML_DOTTED_VERSION
    return LIBXML_DOTTED_VERSION;
#else
    return NULL;
#endif
}

/*
 * Get OpenSSL version text.
 */
const char *pox_get_openssl_version(void) {
#ifdef OPENSSL_VERSION_TEXT
    return OPENSSL_VERSION_TEXT;
#else
    return NULL;
#endif
}

/*
 * Get PCRE version.
 */
const char *pox_get_pcre_version(void) {
#ifdef PCRE2_MAJOR
    /* PCRE2 - construct version string */
    static char pcre_version[32];
    snprintf(pcre_version, sizeof(pcre_version), "%d.%d", PCRE2_MAJOR, PCRE2_MINOR);
    return pcre_version;
#elif defined(PCRE_MAJOR)
    /* PCRE1 */
    static char pcre_version[32];
    snprintf(pcre_version, sizeof(pcre_version), "%d.%d", PCRE_MAJOR, PCRE_MINOR);
    return pcre_version;
#else
    return NULL;
#endif
}

/*
 * Get zlib version.
 */
const char *pox_get_zlib_version(void) {
#ifdef ZLIB_VERSION
    return ZLIB_VERSION;
#else
    return NULL;
#endif
}

/*
 * Get curl version.
 */
const char *pox_get_curl_version(void) {
#ifdef LIBCURL_VERSION
    return LIBCURL_VERSION;
#else
    return NULL;
#endif
}

/*
 * Get loaded extension names as a newline-separated string.
 * Caller must free the returned string.
 */
char *pox_get_loaded_extensions(int argc, char **argv) {
    pox_script_filename = "extensions";

    if (pox_init(argc, argv) != 0) {
        return NULL;
    }

    /* Calculate total buffer size needed */
    size_t total_len = 0;
    zend_module_entry *module;

    ZEND_HASH_MAP_FOREACH_PTR(&module_registry, module) {
        total_len += strlen(module->name) + 1; /* +1 for newline */
    } ZEND_HASH_FOREACH_END();

    /* Allocate buffer */
    char *result = malloc(total_len + 1);
    if (result == NULL) {
        php_embed_shutdown();
        pox_script_filename = NULL;
        return NULL;
    }

    /* Build the string */
    char *ptr = result;
    ZEND_HASH_MAP_FOREACH_PTR(&module_registry, module) {
        size_t len = strlen(module->name);
        memcpy(ptr, module->name, len);
        ptr += len;
        *ptr++ = '\n';
    } ZEND_HASH_FOREACH_END();
    *ptr = '\0';

    php_embed_shutdown();
    pox_script_filename = NULL;

    return result;
}

/*
 * Free a string allocated by pox functions.
 */
void pox_free_string(char *str) {
    if (str != NULL) {
        free(str);
    }
}

/* ============================================================================
 * Web/Server Mode - Custom SAPI for handling HTTP requests
 * ============================================================================ */

/* Request context passed from Rust */
typedef struct {
    /* Request info */
    const char *method;
    const char *uri;
    const char *query_string;
    const char *content_type;
    size_t content_length;
    const char *request_body;
    size_t request_body_len;
    size_t request_body_read;

    /* Headers (key=value pairs, newline separated) */
    const char *headers;

    /* Document root and script */
    const char *document_root;
    const char *script_filename;

    /* Server info */
    const char *server_name;
    int server_port;
    const char *remote_addr;
    int remote_port;

    /* Response output buffer */
    char *response_body;
    size_t response_body_len;
    size_t response_body_cap;

    /* Response headers */
    char *response_headers;
    size_t response_headers_len;
    size_t response_headers_cap;

    /* Response status */
    int response_status;
} pox_request_context;

void pox_free_response(pox_request_context *ctx);

typedef struct {
    pox_request_context context;
    char *method;
    char *uri;
    char *query_string;
    char *content_type;
    char *headers;
    char *document_root;
    char *script_filename;
    char *server_name;
    char *remote_addr;
    uint8_t *body;
} pox_owned_request;

static char *pox_slice_string(pox_slice_v1 value) {
    if (value.len > 0 && value.data == NULL) return NULL;
    if (value.len > 0 && memchr(value.data, '\0', value.len) != NULL) return NULL;
    char *result = malloc(value.len + 1);
    if (result == NULL) return NULL;
    if (value.len > 0 && value.data != NULL) memcpy(result, value.data, value.len);
    result[value.len] = '\0';
    return result;
}

static char *pox_content_type(const char *headers) {
    if (headers == NULL) return strdup("");
    const char *line = headers;
    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        static const char prefix[] = "Content-Type:";
        if (len >= sizeof(prefix) - 1 && strncasecmp(line, prefix, sizeof(prefix) - 1) == 0) {
            const char *value = line + sizeof(prefix) - 1;
            while (value < line + len && (*value == ' ' || *value == '\t')) value++;
            size_t value_len = (size_t)((line + len) - value);
            char *result = malloc(value_len + 1);
            if (result == NULL) return NULL;
            memcpy(result, value, value_len);
            result[value_len] = '\0';
            return result;
        }
        if (end == NULL) break;
        line = end + 1;
    }
    return strdup("");
}

static void pox_owned_request_free(pox_owned_request *request, int free_response) {
    if (request == NULL) return;
    if (free_response) pox_free_response(&request->context);
    free(request->method);
    free(request->uri);
    free(request->query_string);
    free(request->content_type);
    free(request->headers);
    free(request->document_root);
    free(request->script_filename);
    free(request->server_name);
    free(request->remote_addr);
    free(request->body);
    memset(request, 0, sizeof(*request));
}

static int pox_owned_request_init(pox_owned_request *owned, const pox_http_request_v1 *request) {
    if (owned == NULL || request == NULL || request->struct_size < sizeof(*request)) return 0;
    if ((request->method.len > 0 && request->method.data == NULL) ||
        (request->uri.len > 0 && request->uri.data == NULL) ||
        (request->query_string.len > 0 && request->query_string.data == NULL) ||
        (request->headers.len > 0 && request->headers.data == NULL) ||
        (request->body.len > 0 && request->body.data == NULL) ||
        (request->document_root.len > 0 && request->document_root.data == NULL) ||
        (request->script_filename.len > 0 && request->script_filename.data == NULL) ||
        (request->server_name.len > 0 && request->server_name.data == NULL) ||
        (request->remote_addr.len > 0 && request->remote_addr.data == NULL)) return 0;
    memset(owned, 0, sizeof(*owned));
    owned->method = pox_slice_string(request->method);
    owned->uri = pox_slice_string(request->uri);
    owned->query_string = pox_slice_string(request->query_string);
    owned->headers = pox_slice_string(request->headers);
    owned->document_root = pox_slice_string(request->document_root);
    owned->script_filename = pox_slice_string(request->script_filename);
    owned->server_name = pox_slice_string(request->server_name);
    owned->remote_addr = pox_slice_string(request->remote_addr);
    owned->content_type = pox_content_type(owned->headers);
    if (request->body.len > 0) {
        owned->body = malloc(request->body.len);
        if (owned->body != NULL && request->body.data != NULL) {
            memcpy(owned->body, request->body.data, request->body.len);
        }
    }
    if (owned->method == NULL || owned->uri == NULL || owned->query_string == NULL ||
        owned->headers == NULL || owned->document_root == NULL ||
        owned->script_filename == NULL || owned->server_name == NULL ||
        owned->remote_addr == NULL || owned->content_type == NULL ||
        (request->body.len > 0 && owned->body == NULL)) {
        pox_owned_request_free(owned, 0);
        return 0;
    }

    owned->context.method = owned->method;
    owned->context.uri = owned->uri;
    owned->context.query_string = owned->query_string;
    owned->context.content_type = owned->content_type;
    owned->context.content_length = request->body.len;
    owned->context.request_body = (const char *)owned->body;
    owned->context.request_body_len = request->body.len;
    owned->context.headers = owned->headers;
    owned->context.document_root = owned->document_root;
    owned->context.script_filename = owned->script_filename;
    owned->context.server_name = owned->server_name;
    owned->context.server_port = request->server_port;
    owned->context.remote_addr = owned->remote_addr;
    owned->context.remote_port = request->remote_port;
    owned->context.response_status = 200;
    return 1;
}

static void pox_response_view(const pox_request_context *context, pox_http_response_v1 *response) {
    memset(response, 0, sizeof(*response));
    response->struct_size = sizeof(*response);
    response->status = (uint16_t)context->response_status;
    response->headers.data = (uint8_t *)context->response_headers;
    response->headers.len = context->response_headers_len;
    response->body.data = (uint8_t *)context->response_body;
    response->body.len = context->response_body_len;
}

/* Thread-local request context for the web SAPI */
static __thread pox_request_context *current_request = NULL;

/* Append to response body buffer */
static void append_response_body(const char *data, size_t len) {
    if (current_request == NULL) return;

    /* Grow buffer if needed */
    while (current_request->response_body_len + len >= current_request->response_body_cap) {
        size_t new_cap = current_request->response_body_cap * 2;
        if (new_cap == 0) new_cap = 8192;
        char *new_buf = realloc(current_request->response_body, new_cap);
        if (new_buf == NULL) return;
        current_request->response_body = new_buf;
        current_request->response_body_cap = new_cap;
    }

    memcpy(current_request->response_body + current_request->response_body_len, data, len);
    current_request->response_body_len += len;
}

/* Append to response headers buffer */
static void append_response_header(const char *header, size_t len) {
    if (current_request == NULL) return;

    /* Add newline after header */
    size_t total_len = len + 1;

    /* Grow buffer if needed */
    while (current_request->response_headers_len + total_len >= current_request->response_headers_cap) {
        size_t new_cap = current_request->response_headers_cap * 2;
        if (new_cap == 0) new_cap = 4096;
        char *new_buf = realloc(current_request->response_headers, new_cap);
        if (new_buf == NULL) return;
        current_request->response_headers = new_buf;
        current_request->response_headers_cap = new_cap;
    }

    memcpy(current_request->response_headers + current_request->response_headers_len, header, len);
    current_request->response_headers_len += len;
    current_request->response_headers[current_request->response_headers_len++] = '\n';
}

/* SAPI: Unbuffered write - captures PHP output */
static size_t pox_web_ub_write(const char *str, size_t str_length) {
    append_response_body(str, str_length);
    return str_length;
}

/* SAPI: Flush output */
static void pox_web_sapi_flush(void *server_context) {
    /* We buffer everything, so flush is a no-op */
    (void)server_context;
}

/* SAPI: Send headers */
static int pox_web_send_headers(sapi_headers_struct *sapi_headers) {
    if (current_request == NULL) {
        return SAPI_HEADER_SENT_SUCCESSFULLY;
    }

    /* Get status code */
    if (SG(sapi_headers).http_status_line) {
        current_request->response_status = atoi((SG(sapi_headers).http_status_line) + 9);
    } else {
        current_request->response_status = SG(sapi_headers).http_response_code;
        if (current_request->response_status == 0) {
            current_request->response_status = 200;
        }
    }

    /* Collect headers */
    zend_llist_element *element = sapi_headers->headers.head;
    while (element) {
        sapi_header_struct *header = (sapi_header_struct *)element->data;
        append_response_header(header->header, header->header_len);
        element = element->next;
    }

    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

/* SAPI: Read POST data */
static size_t pox_web_read_post(char *buffer, size_t count_bytes) {
    if (current_request == NULL || current_request->request_body == NULL) {
        return 0;
    }

    size_t remaining = current_request->request_body_len - current_request->request_body_read;
    if (remaining == 0) {
        return 0;
    }

    size_t to_read = (count_bytes < remaining) ? count_bytes : remaining;
    memcpy(buffer, current_request->request_body + current_request->request_body_read, to_read);
    current_request->request_body_read += to_read;

    return to_read;
}

/* SAPI: Read cookies */
static char *pox_web_read_cookies(void) {
    if (current_request == NULL || current_request->headers == NULL) {
        return NULL;
    }

    /* Search for Cookie: header in the headers string */
    const char *headers = current_request->headers;
    const char *cookie_header = "Cookie:";
    size_t cookie_header_len = 7;

    const char *line = headers;
    while (*line) {
        /* Find end of line */
        const char *eol = strchr(line, '\n');
        size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

        /* Check if this is the Cookie header */
        if (line_len > cookie_header_len &&
            strncasecmp(line, cookie_header, cookie_header_len) == 0) {
            /* Skip "Cookie:" and any whitespace */
            const char *value = line + cookie_header_len;
            while (*value == ' ' || *value == '\t') value++;

            /* Calculate value length (exclude newline) */
            size_t value_len = line_len - (value - line);

            /* Return a copy (PHP will free this) */
            char *cookies = estrndup(value, value_len);
            return cookies;
        }

        if (eol == NULL) break;
        line = eol + 1;
    }

    return NULL;
}

/* SAPI: Register server variables ($_SERVER) */
static void pox_web_register_variables(zval *track_vars_array) {
    if (current_request == NULL) return;

    /* Import environment variables */
    php_import_environment_variables(track_vars_array);

    /* Register standard CGI variables */
    php_register_variable_safe("REQUEST_METHOD",
        (char *)(current_request->method ? current_request->method : "GET"),
        current_request->method ? strlen(current_request->method) : 3, track_vars_array);

    php_register_variable_safe("REQUEST_URI",
        (char *)(current_request->uri ? current_request->uri : "/"),
        current_request->uri ? strlen(current_request->uri) : 1, track_vars_array);

    php_register_variable_safe("QUERY_STRING",
        (char *)(current_request->query_string ? current_request->query_string : ""),
        current_request->query_string ? strlen(current_request->query_string) : 0, track_vars_array);

    php_register_variable_safe("SCRIPT_FILENAME",
        (char *)(current_request->script_filename ? current_request->script_filename : ""),
        current_request->script_filename ? strlen(current_request->script_filename) : 0, track_vars_array);

    php_register_variable_safe("SCRIPT_NAME",
        (char *)(current_request->uri ? current_request->uri : "/"),
        current_request->uri ? strlen(current_request->uri) : 1, track_vars_array);

    php_register_variable_safe("PHP_SELF",
        (char *)(current_request->uri ? current_request->uri : "/"),
        current_request->uri ? strlen(current_request->uri) : 1, track_vars_array);

    php_register_variable_safe("DOCUMENT_ROOT",
        (char *)(current_request->document_root ? current_request->document_root : ""),
        current_request->document_root ? strlen(current_request->document_root) : 0, track_vars_array);

    php_register_variable_safe("SERVER_NAME",
        (char *)(current_request->server_name ? current_request->server_name : "localhost"),
        current_request->server_name ? strlen(current_request->server_name) : 9, track_vars_array);

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", current_request->server_port > 0 ? current_request->server_port : 80);
    php_register_variable_safe("SERVER_PORT", port_str, strlen(port_str), track_vars_array);

    php_register_variable_safe("REMOTE_ADDR",
        (char *)(current_request->remote_addr ? current_request->remote_addr : "127.0.0.1"),
        current_request->remote_addr ? strlen(current_request->remote_addr) : 9, track_vars_array);

    char remote_port_str[16];
    snprintf(remote_port_str, sizeof(remote_port_str), "%d", current_request->remote_port);
    php_register_variable_safe("REMOTE_PORT", remote_port_str, strlen(remote_port_str), track_vars_array);

    php_register_variable_safe("SERVER_SOFTWARE", "pox", 4, track_vars_array);
    php_register_variable_safe("SERVER_PROTOCOL", "HTTP/1.1", 8, track_vars_array);
    php_register_variable_safe("GATEWAY_INTERFACE", "CGI/1.1", 7, track_vars_array);

    if (current_request->content_type) {
        php_register_variable_safe("CONTENT_TYPE",
            (char *)current_request->content_type,
            strlen(current_request->content_type), track_vars_array);
    }

    if (current_request->content_length > 0) {
        char cl_str[32];
        snprintf(cl_str, sizeof(cl_str), "%zu", current_request->content_length);
        php_register_variable_safe("CONTENT_LENGTH", cl_str, strlen(cl_str), track_vars_array);
    }

    /* Register HTTP headers as HTTP_* variables */
    if (current_request->headers) {
        const char *line = current_request->headers;
        while (*line) {
            const char *eol = strchr(line, '\n');
            size_t line_len = eol ? (size_t)(eol - line) : strlen(line);

            const char *colon = memchr(line, ':', line_len);
            if (colon) {
                /* Build HTTP_* variable name */
                size_t name_len = colon - line;
                char *var_name = malloc(5 + name_len + 1); /* "HTTP_" + name + null */
                if (var_name) {
                    strcpy(var_name, "HTTP_");
                    for (size_t i = 0; i < name_len; i++) {
                        char c = line[i];
                        if (c == '-') {
                            var_name[5 + i] = '_';
                        } else if (c >= 'a' && c <= 'z') {
                            var_name[5 + i] = c - 32; /* uppercase */
                        } else {
                            var_name[5 + i] = c;
                        }
                    }
                    var_name[5 + name_len] = '\0';

                    /* Get value (skip colon and whitespace) */
                    const char *value = colon + 1;
                    while (*value == ' ' || *value == '\t') value++;
                    size_t value_len = line_len - (value - line);

                    /* Skip Content-Type and Content-Length (already handled) */
                    if (strcmp(var_name, "HTTP_CONTENT_TYPE") != 0 &&
                        strcmp(var_name, "HTTP_CONTENT_LENGTH") != 0) {
                        php_register_variable_safe(var_name, (char *)value, value_len, track_vars_array);
                    }

                    free(var_name);
                }
            }

            if (eol == NULL) break;
            line = eol + 1;
        }
    }
}

/* SAPI startup handler */
static int pox_web_startup(sapi_module_struct *sapi_module) {
    return php_module_startup(sapi_module, NULL);
}

/* Custom SAPI module for web requests */
static sapi_module_struct pox_web_sapi_module = {
    "pox",                         /* name */
    "pox Web Server",              /* pretty name */

    pox_web_startup,               /* startup */
    php_module_shutdown_wrapper,    /* shutdown */

    NULL,                           /* activate */
    NULL,                           /* deactivate */

    pox_web_ub_write,              /* unbuffered write */
    pox_web_sapi_flush,            /* flush */
    NULL,                           /* get uid */
    NULL,                           /* getenv */

    php_error,                      /* error handler */

    NULL,                           /* header handler */
    pox_web_send_headers,          /* send headers handler */
    NULL,                           /* send header handler */

    pox_web_read_post,             /* read POST data */
    pox_web_read_cookies,          /* read Cookies */

    pox_web_register_variables,    /* register server variables */
    NULL,                           /* Log message */
    NULL,                           /* Get request time */
    NULL,                           /* Child terminate */

    STANDARD_SAPI_MODULE_PROPERTIES
};

static int pox_web_initialized = 0;

/*
 * Initialize the web SAPI (call once at server startup).
 */
int pox_web_init(void) {
    if (pox_web_initialized) {
        return 0;
    }

#ifdef ZTS
    php_tsrm_startup();
#endif

    zend_signal_startup();

    sapi_startup(&pox_web_sapi_module);

    pox_web_sapi_module.ini_entries = pox_ini_entries;

    if (pox_web_sapi_module.startup(&pox_web_sapi_module) == FAILURE) {
        return 1;
    }

    pox_web_initialized = 1;
    return 0;
}

/*
 * Shutdown the web SAPI (call once at server shutdown).
 */
void pox_web_shutdown(void) {
    if (!pox_web_initialized) {
        return;
    }

    php_module_shutdown();
    sapi_shutdown();
    pox_web_initialized = 0;
}

/*
 * Execute a web request.
 * Takes a request context and populates response fields.
 */
int pox_web_execute(pox_request_context *ctx) {
    if (!pox_web_initialized) {
        if (pox_web_init() != 0) {
            return 1;
        }
    }

    current_request = ctx;

    /* Initialize response buffers */
    ctx->response_body = NULL;
    ctx->response_body_len = 0;
    ctx->response_body_cap = 0;
    ctx->response_headers = NULL;
    ctx->response_headers_len = 0;
    ctx->response_headers_cap = 0;
    ctx->response_status = 200;
    ctx->request_body_read = 0;

    /* Setup request info */
    SG(request_info).request_method = ctx->method;
    SG(request_info).query_string = (char *)ctx->query_string;
    SG(request_info).request_uri = (char *)ctx->uri;
    SG(request_info).content_type = ctx->content_type;
    SG(request_info).content_length = ctx->content_length;
    SG(request_info).path_translated = (char *)ctx->script_filename;

    SG(server_context) = (void *)ctx;
    SG(sapi_headers).http_response_code = 200;

    int result = 0;

    zend_first_try {
        if (php_request_startup() == FAILURE) {
            result = 1;
        } else {
            /* Apply INI entries */
            pox_apply_ini_entries();

            /* Execute the script */
            zend_file_handle file_handle;
            zend_stream_init_filename(&file_handle, ctx->script_filename);

            php_execute_script(&file_handle);
            result = EG(exit_status);
        }
    } zend_catch {
        result = EG(exit_status);
    } zend_end_try();

    zend_try {
        php_request_shutdown(NULL);
    } zend_end_try();

    current_request = NULL;

    return result;
}

/*
 * Get the request context struct size (for FFI allocation).
 */
size_t pox_request_context_size(void) {
    return sizeof(pox_request_context);
}

/*
 * Free response buffers in the request context.
 */
void pox_free_response(pox_request_context *ctx) {
    if (ctx->response_body) {
        free(ctx->response_body);
        ctx->response_body = NULL;
    }
    if (ctx->response_headers) {
        free(ctx->response_headers);
        ctx->response_headers = NULL;
    }
}

/* ============================================================================
 * Worker Mode - Long-running PHP processes like FrankenPHP
 * ============================================================================ */

/* Worker state */
typedef struct {
    int is_worker_mode;           /* Are we in worker mode? */
    int waiting_for_request;      /* Is worker waiting for a request? */
    pox_request_context *pending_request;  /* The pending request to handle */
} pox_worker_state;

static __thread pox_worker_state worker_state = {0};
static __thread const pox_worker_callbacks_v1 *pox_worker_callbacks = NULL;
static __thread pox_owned_request pox_worker_request = {0};

/*
 * PHP function: pox_handle_request(callable $callback): bool
 *
 * This function is called from the worker script in a loop.
 * It waits for an incoming HTTP request, sets up the request context,
 * calls the callback function, and then signals completion.
 */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_pox_handle_request, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, callback, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(pox_handle_request) {
    zend_fcall_info fci;
    zend_fcall_info_cache fcc;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_FUNC(fci, fcc)
    ZEND_PARSE_PARAMETERS_END();

    if (!worker_state.is_worker_mode) {
        zend_throw_exception(
            spl_ce_RuntimeException,
            "pox_handle_request() called while not in worker mode", 0);
        RETURN_THROWS();
    }

    /* Signal we're waiting and wait for a request from Rust */
    worker_state.waiting_for_request = 1;

    /* This call blocks until a request is available or shutdown is requested. */
    pox_http_request_v1 request = {0};
    request.struct_size = sizeof(request);
    int got_request = pox_worker_callbacks != NULL &&
        pox_worker_callbacks->wait_request != NULL
        ? pox_worker_callbacks->wait_request(pox_worker_callbacks->userdata, &request)
        : 0;

    worker_state.waiting_for_request = 0;

    if (!got_request) {
        /* Shutdown requested */
        RETURN_FALSE;
    }

    if (!pox_owned_request_init(&pox_worker_request, &request)) {
        static uint8_t error_body[] = "Invalid worker request";
        pox_http_response_v1 response = {0};
        response.struct_size = sizeof(response);
        response.status = 500;
        response.body.data = error_body;
        response.body.len = sizeof(error_body) - 1;
        if (pox_worker_callbacks != NULL && pox_worker_callbacks->complete_response != NULL) {
            pox_worker_callbacks->complete_response(pox_worker_callbacks->userdata, &response);
        }
        RETURN_TRUE;
    }

    worker_state.pending_request = &pox_worker_request.context;

    current_request = worker_state.pending_request;

    /* Reset response buffers */
    current_request->response_body = NULL;
    current_request->response_body_len = 0;
    current_request->response_body_cap = 0;
    current_request->response_headers = NULL;
    current_request->response_headers_len = 0;
    current_request->response_headers_cap = 0;
    current_request->response_status = 200;
    current_request->request_body_read = 0;

    /* Re-initialize request info from the new request */
    SG(request_info).request_method = current_request->method;
    SG(request_info).query_string = (char *)current_request->query_string;
    SG(request_info).request_uri = (char *)current_request->uri;
    SG(request_info).content_type = current_request->content_type;
    SG(request_info).content_length = current_request->content_length;
    SG(request_info).path_translated = (char *)current_request->script_filename;

    SG(server_context) = (void *)current_request;
    SG(sapi_headers).http_response_code = 200;
    SG(headers_sent) = 0;
    SG(read_post_bytes) = 0;  /* Reset POST read counter */

    /* Activate SAPI for the new request - this populates $_POST, $_COOKIE, etc. */
    sapi_activate();

    /* Reset auto globals to reimport $_SERVER, $_GET, $_POST, $_COOKIE, $_FILES
     * This is the proper way to refresh superglobals in worker mode.
     * See FrankenPHP's frankenphp_reset_super_globals() for reference. */
    zend_auto_global *auto_global;
    ZEND_HASH_MAP_FOREACH_PTR(CG(auto_globals), auto_global) {
        /* Skip $_ENV - we don't want to reset environment variables */
        if (zend_string_equals_literal(auto_global->name, "_ENV")) {
            continue;
        }

        /* For $_SERVER, always reimport */
        if (zend_string_equals_literal(auto_global->name, "_SERVER")) {
            if (auto_global->auto_global_callback) {
                auto_global->armed = auto_global->auto_global_callback(auto_global->name);
            }
            continue;
        }

        /* Skip JIT globals except when they have a callback
         * JIT globals (like $_REQUEST, $GLOBALS) are only populated on script parse */
        if (auto_global->jit) {
            continue;
        }

        /* Reimport $_GET, $_POST, $_COOKIE, $_FILES via their callbacks */
        if (auto_global->auto_global_callback) {
            auto_global->armed = auto_global->auto_global_callback(auto_global->name);
        }
    }
    ZEND_HASH_FOREACH_END();

    /* Clear output buffers */
    if (OG(handlers).elements) {
        php_output_end_all();
    }
    php_output_activate();

    /* Disable timeout in worker mode (we're in a Rust-managed thread) */
#ifdef ZEND_MAX_EXECUTION_TIMERS
    zend_unset_timeout();
#endif

    /* Call the callback function */
    zval retval = {0};
    fci.size = sizeof(fci);
    fci.retval = &retval;
    fci.params = NULL;
    fci.param_count = 0;

    if (zend_call_function(&fci, &fcc) == SUCCESS) {
        /* Handle any exception */
        if (EG(exception)) {
            if (!zend_is_unwind_exit(EG(exception)) &&
                !zend_is_graceful_exit(EG(exception))) {
                zend_exception_error(EG(exception), E_ERROR);
            }
            zend_clear_exception();
        }
    }

    zval_ptr_dtor(&retval);

    /* Flush output */
    php_output_end_all();

    /* Send headers if not already sent */
    if (!SG(headers_sent)) {
        sapi_send_headers();
    }

    /* Copy the response through the versioned callback before releasing it. */
    pox_http_response_v1 response;
    pox_response_view(current_request, &response);
    if (pox_worker_callbacks != NULL && pox_worker_callbacks->complete_response != NULL) {
        pox_worker_callbacks->complete_response(pox_worker_callbacks->userdata, &response);
    }

    current_request = NULL;
    worker_state.pending_request = NULL;
    pox_owned_request_free(&pox_worker_request, 1);

    RETURN_TRUE;
}

/* Module entry for the pox extension */
static const zend_function_entry pox_functions[] = {
    PHP_FE(pox_handle_request, arginfo_pox_handle_request)
    PHP_FE_END
};

static zend_module_entry pox_module_entry = {
    STANDARD_MODULE_HEADER,
    "pox",
    pox_functions,
    NULL, /* MINIT */
    NULL, /* MSHUTDOWN */
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    NULL, /* MINFO */
    "1.0.0",
    STANDARD_MODULE_PROPERTIES
};

/* Modified startup to register our extension */
static int pox_worker_startup(sapi_module_struct *sapi_module) {
    if (php_module_startup(sapi_module, &pox_module_entry) == FAILURE) {
        return FAILURE;
    }
    return SUCCESS;
}

/* Worker SAPI module - similar to web SAPI but for workers */
static sapi_module_struct pox_worker_sapi_module = {
    "pox-worker",                  /* name */
    "pox Worker Mode",             /* pretty name */

    pox_worker_startup,            /* startup - register our extension */
    php_module_shutdown_wrapper,    /* shutdown */

    NULL,                           /* activate */
    NULL,                           /* deactivate */

    pox_web_ub_write,              /* unbuffered write */
    pox_web_sapi_flush,            /* flush */
    NULL,                           /* get uid */
    NULL,                           /* getenv */

    php_error,                      /* error handler */

    NULL,                           /* header handler */
    pox_web_send_headers,          /* send headers handler */
    NULL,                           /* send header handler */

    pox_web_read_post,             /* read POST data */
    pox_web_read_cookies,          /* read Cookies */

    pox_web_register_variables,    /* register server variables */
    NULL,                           /* Log message */
    NULL,                           /* Get request time */
    NULL,                           /* Child terminate */

    STANDARD_SAPI_MODULE_PROPERTIES
};

static int pox_worker_global_initialized = 0;

/*
 * Global initialization for worker mode (call once from main thread before spawning workers).
 */
int pox_worker_global_init(void) {
    if (pox_worker_global_initialized) {
        return 0;
    }

#ifdef ZTS
    php_tsrm_startup();
#endif

    zend_signal_startup();

    sapi_startup(&pox_worker_sapi_module);

    pox_worker_sapi_module.ini_entries = pox_ini_entries;

    if (pox_worker_sapi_module.startup(&pox_worker_sapi_module) == FAILURE) {
        return 1;
    }

    pox_worker_global_initialized = 1;
    return 0;
}

/*
 * Initialize a worker thread (call once per worker thread).
 * Must be called from each worker thread after pox_worker_global_init()
 * has been called from the main thread.
 */
int pox_worker_init(const char *script_filename, const char *document_root) {
    (void)script_filename;
    (void)document_root;

#ifdef ZTS
    /* Allocate TSRM resources for this thread - required before accessing any ZTS globals */
    (void)ts_resource(0);
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    worker_state.is_worker_mode = 1;
    worker_state.waiting_for_request = 0;
    worker_state.pending_request = NULL;

    return 0;
}

/*
 * Set the pending request for the worker to handle.
 */
void pox_worker_set_request(pox_request_context *ctx) {
    worker_state.pending_request = ctx;
}

/*
 * Check if the worker is waiting for a request.
 */
int pox_worker_is_waiting(void) {
    return worker_state.waiting_for_request;
}

/*
 * Check if there's a response ready (called from Rust).
 */
int pox_worker_has_response(void) {
    return 0; /* Response state is tracked in Rust */
}

/*
 * Execute the worker script. This runs the worker script which should
 * contain a loop calling pox_handle_request().
 *
 * IMPORTANT: pox_worker_global_init() must be called from the main thread
 * before calling this function from worker threads.
 */
int pox_worker_run(const char *script_filename, const char *document_root) {
    /* Per-thread initialization */
    pox_worker_init(script_filename, document_root);

    /* Create a dummy request context for the initial script execution */
    pox_request_context dummy_ctx = {0};
    dummy_ctx.method = "GET";
    dummy_ctx.uri = "/";
    dummy_ctx.query_string = "";
    dummy_ctx.document_root = document_root;
    dummy_ctx.script_filename = script_filename;
    dummy_ctx.server_name = "localhost";
    dummy_ctx.server_port = 0;
    dummy_ctx.remote_addr = "127.0.0.1";
    dummy_ctx.remote_port = 0;

    current_request = &dummy_ctx;

    /* Setup request info */
    SG(request_info).request_method = dummy_ctx.method;
    SG(request_info).query_string = (char *)dummy_ctx.query_string;
    SG(request_info).request_uri = (char *)dummy_ctx.uri;
    SG(request_info).content_type = NULL;
    SG(request_info).content_length = 0;
    SG(request_info).path_translated = (char *)dummy_ctx.script_filename;

    SG(server_context) = (void *)&dummy_ctx;
    SG(sapi_headers).http_response_code = 200;

    int result = 0;

    zend_first_try {
        if (php_request_startup() == FAILURE) {
            result = 1;
        } else {
            pox_apply_ini_entries();

            /* Execute the worker script */
            zend_file_handle file_handle;
            zend_stream_init_filename(&file_handle, script_filename);

            php_execute_script(&file_handle);
            result = EG(exit_status);
        }
    } zend_catch {
        result = EG(exit_status);
    } zend_end_try();

    zend_try {
        php_request_shutdown(NULL);
    } zend_end_try();

    current_request = NULL;
    worker_state.is_worker_mode = 0;

    return result;
}

/*
 * Shutdown a worker thread.
 */
void pox_worker_shutdown(void) {
    worker_state.is_worker_mode = 0;
}

static void pox_worker_global_shutdown(void) {
    if (!pox_worker_global_initialized) return;
    php_module_shutdown();
    sapi_shutdown();
    pox_worker_global_initialized = 0;
}

/* ============================================================================
 * Stable Pox runtime ABI
 * ============================================================================ */

#ifndef POX_RUNTIME_REVISION
#define POX_RUNTIME_REVISION "dev"
#endif

#ifndef POX_RUNTIME_TARGET
#define POX_RUNTIME_TARGET "unknown"
#endif

static __thread char pox_last_error_message[512] = {0};

static void pox_set_error(const char *message) {
    if (message == NULL) message = "unknown runtime error";
    snprintf(pox_last_error_message, sizeof(pox_last_error_message), "%s", message);
}

static int32_t pox_copy_buffer(const void *data, size_t len, pox_buffer_v1 *output) {
    if (output == NULL) return POX_STATUS_INVALID_ARGUMENT;
    output->data = NULL;
    output->len = 0;
    if (len == 0) return POX_STATUS_OK;
    output->data = malloc(len);
    if (output->data == NULL) {
        pox_set_error("runtime buffer allocation failed");
        return POX_STATUS_OUT_OF_MEMORY;
    }
    memcpy(output->data, data, len);
    output->len = len;
    return POX_STATUS_OK;
}

static void pox_abi_free_buffer(pox_buffer_v1 *buffer) {
    if (buffer == NULL) return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} pox_json_buffer;

static int pox_json_reserve(pox_json_buffer *json, size_t additional) {
    if (json->len + additional + 1 <= json->cap) return 1;
    size_t cap = json->cap == 0 ? 1024 : json->cap;
    while (cap < json->len + additional + 1) cap *= 2;
    char *data = realloc(json->data, cap);
    if (data == NULL) return 0;
    json->data = data;
    json->cap = cap;
    return 1;
}

static int pox_json_append(pox_json_buffer *json, const char *value) {
    size_t len = strlen(value);
    if (!pox_json_reserve(json, len)) return 0;
    memcpy(json->data + json->len, value, len);
    json->len += len;
    json->data[json->len] = '\0';
    return 1;
}

static int pox_json_string(pox_json_buffer *json, const char *value) {
    if (!pox_json_append(json, "\"")) return 0;
    if (value != NULL) {
        for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
            char escaped[7] = {0};
            if (*p == '\"' || *p == '\\') {
                escaped[0] = '\\';
                escaped[1] = (char)*p;
            } else if (*p < 0x20) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            } else {
                escaped[0] = (char)*p;
            }
            if (!pox_json_append(json, escaped)) return 0;
        }
    }
    return pox_json_append(json, "\"");
}

static int pox_json_library(pox_json_buffer *json, const char *name, const char *value, int *first) {
    if (value == NULL || *value == '\0') return 1;
    if (!*first && !pox_json_append(json, ",")) return 0;
    *first = 0;
    return pox_json_string(json, name) && pox_json_append(json, ":") &&
        pox_json_string(json, value);
}

static int32_t pox_abi_metadata_json(pox_buffer_v1 *output) {
    char *argv[] = {"pox-metadata", NULL};
    char *extensions = pox_get_loaded_extensions(1, argv);
    if (extensions == NULL) {
        pox_set_error("failed to initialize PHP while reading runtime metadata");
        return POX_STATUS_INITIALIZATION_FAILED;
    }

    pox_json_buffer json = {0};
    char number[64];
    int ok = pox_json_append(&json, "{\"php_version\":") &&
        pox_json_string(&json, pox_get_version()) &&
        pox_json_append(&json, ",\"php_version_id\":");
    snprintf(number, sizeof(number), "%d", pox_get_version_id());
    ok = ok && pox_json_append(&json, number) &&
        pox_json_append(&json, ",\"zend_version\":") &&
        pox_json_string(&json, pox_get_zend_version()) &&
        pox_json_append(&json, ",\"zts\":") &&
        pox_json_append(&json, pox_is_zts() ? "true" : "false") &&
        pox_json_append(&json, ",\"debug\":") &&
        pox_json_append(&json, pox_is_debug() ? "true" : "false") &&
        pox_json_append(&json, ",\"runtime_revision\":") &&
        pox_json_string(&json, POX_RUNTIME_REVISION) &&
        pox_json_append(&json, ",\"target\":") &&
        pox_json_string(&json, POX_RUNTIME_TARGET) &&
        pox_json_append(&json, ",\"abi_major\":1,\"abi_minor\":0,\"extensions\":[");

    int first = 1;
    char *line = extensions;
    while (ok && line != NULL && *line != '\0') {
        char *end = strchr(line, '\n');
        if (end != NULL) *end = '\0';
        if (*line != '\0') {
            if (!first) ok = pox_json_append(&json, ",");
            first = 0;
            ok = ok && pox_json_string(&json, line);
        }
        line = end == NULL ? NULL : end + 1;
    }
    free(extensions);

    ok = ok && pox_json_append(&json, "],\"libraries\":{");
    first = 1;
    ok = ok && pox_json_library(&json, "icu", pox_get_icu_version(), &first);
    ok = ok && pox_json_library(&json, "libxml", pox_get_libxml_version(), &first);
    ok = ok && pox_json_library(&json, "openssl", pox_get_openssl_version(), &first);
    ok = ok && pox_json_library(&json, "pcre", pox_get_pcre_version(), &first);
    ok = ok && pox_json_library(&json, "zlib", pox_get_zlib_version(), &first);
    ok = ok && pox_json_library(&json, "curl", pox_get_curl_version(), &first);
    ok = ok && pox_json_append(&json, "}}");

    if (!ok) {
        free(json.data);
        pox_set_error("runtime metadata allocation failed");
        return POX_STATUS_OUT_OF_MEMORY;
    }
    output->data = (uint8_t *)json.data;
    output->len = json.len;
    return POX_STATUS_OK;
}

static int32_t pox_abi_last_error(pox_buffer_v1 *output) {
    return pox_copy_buffer(pox_last_error_message, strlen(pox_last_error_message), output);
}

static int32_t pox_abi_set_ini_entries(pox_slice_v1 entries) {
    if (entries.len == 0) {
        pox_set_ini_entries(NULL);
        return POX_STATUS_OK;
    }
    char *value = pox_slice_string(entries);
    if (value == NULL) return POX_STATUS_OUT_OF_MEMORY;
    pox_set_ini_entries(value);
    free(value);
    return POX_STATUS_OK;
}

static void pox_free_arguments(char **argv, size_t count) {
    if (argv == NULL) return;
    for (size_t i = 0; i < count; i++) free(argv[i]);
    free(argv);
}

static int32_t pox_abi_execute_cli(const pox_cli_request_v1 *request, int32_t *exit_code) {
    if (request == NULL || exit_code == NULL || request->struct_size < sizeof(*request)) {
        pox_set_error("invalid CLI request");
        return POX_STATUS_INVALID_ARGUMENT;
    }
    if ((request->source.len > 0 && request->source.data == NULL) ||
        (request->argument_count > 0 && request->arguments == NULL)) {
        pox_set_error("CLI request contains invalid slices");
        return POX_STATUS_INVALID_ARGUMENT;
    }
    char *source = pox_slice_string(request->source);
    if (source == NULL) return POX_STATUS_OUT_OF_MEMORY;
    size_t argc = request->argument_count + 1;
    char **argv = calloc(argc + 1, sizeof(char *));
    if (argv == NULL) {
        free(source);
        return POX_STATUS_OUT_OF_MEMORY;
    }
    argv[0] = strdup(*source != '\0' ? source : "pox");
    for (size_t i = 0; i < request->argument_count; i++) {
        argv[i + 1] = pox_slice_string(request->arguments[i]);
        if (argv[i + 1] == NULL) {
            pox_free_arguments(argv, i + 1);
            free(source);
            return POX_STATUS_OUT_OF_MEMORY;
        }
    }

    switch (request->operation) {
        case POX_CLI_EXECUTE_SCRIPT:
            *exit_code = pox_execute_script(source, (int)argc, argv);
            break;
        case POX_CLI_EXECUTE_CODE:
            *exit_code = pox_execute_code(source, (int)argc, argv);
            break;
        case POX_CLI_LINT:
            *exit_code = pox_lint_file(source, (int)argc, argv);
            break;
        case POX_CLI_INFO:
            *exit_code = pox_info(request->info_flags, (int)argc, argv);
            break;
        case POX_CLI_MODULES:
            *exit_code = pox_print_modules((int)argc, argv);
            break;
        default:
            pox_free_arguments(argv, argc);
            free(source);
            pox_set_error("unsupported CLI operation");
            return POX_STATUS_INVALID_ARGUMENT;
    }

    pox_free_arguments(argv, argc);
    free(source);
    return POX_STATUS_OK;
}

static int32_t pox_abi_web_create(void **runtime) {
    if (runtime == NULL) return POX_STATUS_INVALID_ARGUMENT;
    if (pox_web_init() != 0) {
        pox_set_error("PHP web SAPI initialization failed");
        return POX_STATUS_INITIALIZATION_FAILED;
    }
    *runtime = malloc(1);
    if (*runtime == NULL) {
        pox_web_shutdown();
        return POX_STATUS_OUT_OF_MEMORY;
    }
    return POX_STATUS_OK;
}

static int32_t pox_abi_web_execute(void *runtime, const pox_http_request_v1 *request,
                                   pox_http_response_v1 *response, int32_t *exit_code) {
    if (runtime == NULL || response == NULL || exit_code == NULL) return POX_STATUS_INVALID_ARGUMENT;
    pox_owned_request owned;
    if (!pox_owned_request_init(&owned, request)) {
        pox_set_error("invalid or unallocatable HTTP request");
        return POX_STATUS_INVALID_ARGUMENT;
    }
    *exit_code = pox_web_execute(&owned.context);
    pox_response_view(&owned.context, response);
    owned.context.response_headers = NULL;
    owned.context.response_body = NULL;
    pox_owned_request_free(&owned, 0);
    return POX_STATUS_OK;
}

static void pox_abi_web_destroy(void *runtime) {
    if (runtime == NULL) return;
    pox_web_shutdown();
    free(runtime);
}

static int32_t pox_abi_worker_create(void **runtime) {
    if (runtime == NULL) return POX_STATUS_INVALID_ARGUMENT;
    if (pox_worker_global_init() != 0) {
        pox_set_error("PHP worker SAPI initialization failed");
        return POX_STATUS_INITIALIZATION_FAILED;
    }
    *runtime = malloc(1);
    if (*runtime == NULL) {
        pox_worker_global_shutdown();
        return POX_STATUS_OUT_OF_MEMORY;
    }
    return POX_STATUS_OK;
}

static int32_t pox_abi_worker_run(void *runtime, pox_slice_v1 script_filename,
                                  pox_slice_v1 document_root,
                                  const pox_worker_callbacks_v1 *callbacks,
                                  int32_t *exit_code) {
    if (runtime == NULL || callbacks == NULL || exit_code == NULL ||
        callbacks->struct_size < sizeof(*callbacks) || callbacks->wait_request == NULL ||
        callbacks->complete_response == NULL) {
        return POX_STATUS_INVALID_ARGUMENT;
    }
    char *script = pox_slice_string(script_filename);
    char *root = pox_slice_string(document_root);
    if (script == NULL || root == NULL) {
        free(script);
        free(root);
        return POX_STATUS_OUT_OF_MEMORY;
    }
    pox_worker_callbacks = callbacks;
    *exit_code = pox_worker_run(script, root);
    pox_worker_callbacks = NULL;
    free(script);
    free(root);
    return POX_STATUS_OK;
}

static void pox_abi_worker_destroy(void *runtime) {
    if (runtime == NULL) return;
    pox_worker_global_shutdown();
    free(runtime);
}

static const pox_php_api_v1 POX_PHP_API = {
    .struct_size = sizeof(pox_php_api_v1),
    .abi_major = POX_PHP_ABI_MAJOR,
    .abi_minor = POX_PHP_ABI_MINOR,
    .feature_flags = 0,
    .metadata_json = pox_abi_metadata_json,
    .last_error = pox_abi_last_error,
    .free_buffer = pox_abi_free_buffer,
    .set_ini_entries = pox_abi_set_ini_entries,
    .execute_cli = pox_abi_execute_cli,
    .web_create = pox_abi_web_create,
    .web_execute = pox_abi_web_execute,
    .web_destroy = pox_abi_web_destroy,
    .worker_create = pox_abi_worker_create,
    .worker_run = pox_abi_worker_run,
    .worker_destroy = pox_abi_worker_destroy,
    .reserved = {0}
};

POX_PHP_EXPORT const pox_php_api_v1 *pox_php_get_api(uint32_t requested_major,
                                                      uint32_t requested_minor) {
    if (requested_major != POX_PHP_ABI_MAJOR || requested_minor > POX_PHP_ABI_MINOR) {
        return NULL;
    }
    return &POX_PHP_API;
}
