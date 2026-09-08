#ifndef POX_PHP_RUNTIME_H
#define POX_PHP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define POX_PHP_ABI_MAJOR 1u
#define POX_PHP_ABI_MINOR 1u

/* ABI 1.1: the existing request layout is unchanged. When reserved0 has
 * POX_HTTP_RESPONSE_LIMITS, reserved[0] is the maximum buffered body size and
 * reserved[1] the maximum serialized response-header size (bytes, uint32_t).
 * Without the flag the runtime applies 32 MiB / 32 KiB defaults.
 * A buffering failure yields status 502, empty buffers, and reserved0 bit 0 in
 * pox_http_response_v1. Hosts must check the feature before sending the flag. */
#define POX_FEATURE_RESPONSE_LIMITS UINT64_C(1)
#define POX_FEATURE_PARALLEL_WEB UINT64_C(2)
/* POX_HTTP_PROTOCOL selects reserved[2]: 1000 = HTTP/1.0, 1001 = HTTP/1.1.
 * Other values are rejected; absent flag retains legacy HTTP/1.1 metadata. */
#define POX_FEATURE_HTTP_PROTOCOL UINT64_C(4)
#define POX_HTTP_PROTOCOL 2u
/* POX_HTTP_SECURE marks a host-validated HTTPS request. */
#define POX_FEATURE_REQUEST_SCHEME UINT64_C(8)
#define POX_FEATURE_WEB_THREADS UINT64_C(16)
/* Single-use cancellation handle encoded as low/high uint32 words in
 * request.reserved[3:5]. Host retains it until web_execute returns or worker
 * complete_response returns. The runtime retains its own reference through
 * cleanup. Cancellation is cooperative at Zend opcode boundaries, not a
 * promise to interrupt arbitrary native extension calls. */
#define POX_FEATURE_CANCELLATION UINT64_C(32)
#define POX_HTTP_CANCELLATION 8u
#define POX_RESPONSE_CANCELLED 2u
/* reserved[5]/[6] encode the low/high address words of output callbacks. */
#define POX_FEATURE_RESPONSE_OUTPUT UINT64_C(64)
#define POX_HTTP_RESPONSE_OUTPUT 16u
#define POX_RESPONSE_OUTPUT_FAILED 4u
#define POX_HTTP_SECURE 4u
#define POX_HTTP_RESPONSE_LIMITS 1u
#define POX_RESPONSE_BUFFER_FAILED 1u

#if defined(_WIN32)
#define POX_PHP_EXPORT __declspec(dllexport)
#else
#define POX_PHP_EXPORT __attribute__((visibility("default")))
#endif

typedef struct pox_slice_v1 {
    const uint8_t *data;
    size_t len;
} pox_slice_v1;

/* Called synchronously on the PHP thread, never after request completion.
 * Return nonzero to accept an event, zero to abort output. Headers precede
 * writes/flushes; body chunks are at most 16 KiB. The host must keep this table
 * and userdata alive through execution and check the final execution result:
 * a failure after headers were delivered cannot change their status. */
typedef struct pox_output_callbacks_v1 {
    uint32_t struct_size;
    uint32_t reserved0;
    void *userdata;
    int32_t (*start)(void *userdata, uint16_t status, pox_slice_v1 headers);
    int32_t (*write)(void *userdata, pox_slice_v1 data);
    int32_t (*flush)(void *userdata);
} pox_output_callbacks_v1;

typedef struct pox_buffer_v1 {
    uint8_t *data;
    size_t len;
} pox_buffer_v1;

typedef enum pox_status_v1 {
    POX_STATUS_OK = 0,
    POX_STATUS_INVALID_ARGUMENT = 1,
    POX_STATUS_INITIALIZATION_FAILED = 2,
    POX_STATUS_EXECUTION_FAILED = 3,
    POX_STATUS_OUT_OF_MEMORY = 4,
    POX_STATUS_UNSUPPORTED = 5,
    POX_STATUS_INTERNAL_ERROR = 6
} pox_status_v1;

typedef enum pox_cli_operation_v1 {
    POX_CLI_EXECUTE_SCRIPT = 1,
    POX_CLI_EXECUTE_CODE = 2,
    POX_CLI_LINT = 3,
    POX_CLI_INFO = 4,
    POX_CLI_MODULES = 5
} pox_cli_operation_v1;

typedef struct pox_cli_request_v1 {
    uint32_t struct_size;
    uint32_t operation;
    pox_slice_v1 source;
    const pox_slice_v1 *arguments;
    size_t argument_count;
    int32_t info_flags;
    uint32_t reserved[8];
} pox_cli_request_v1;

typedef struct pox_http_request_v1 {
    uint32_t struct_size;
    uint32_t reserved0;
    pox_slice_v1 method;
    pox_slice_v1 uri;
    pox_slice_v1 query_string;
    pox_slice_v1 headers;
    pox_slice_v1 body;
    pox_slice_v1 document_root;
    pox_slice_v1 script_filename;
    pox_slice_v1 server_name;
    pox_slice_v1 remote_addr;
    uint16_t server_port;
    uint16_t remote_port;
    uint32_t reserved[8];
} pox_http_request_v1;

typedef struct pox_http_response_v1 {
    uint32_t struct_size;
    uint16_t status;
    uint16_t reserved0;
    pox_buffer_v1 headers;
    pox_buffer_v1 body;
    uint32_t reserved[8];
} pox_http_response_v1;

/*
 * The host owns request memory until complete_response returns. The runtime
 * owns response buffers; the host copies them during complete_response and
 * the runtime releases them afterwards.
 */
typedef struct pox_worker_callbacks_v1 {
    uint32_t struct_size;
    uint32_t reserved0;
    void *userdata;
    int32_t (*wait_request)(void *userdata, pox_http_request_v1 *request);
    void (*complete_response)(void *userdata, const pox_http_response_v1 *response);
    uint32_t reserved[8];
} pox_worker_callbacks_v1;

typedef struct pox_php_api_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint64_t feature_flags;

    int32_t (*metadata_json)(pox_buffer_v1 *output);
    int32_t (*last_error)(pox_buffer_v1 *output);
    void (*free_buffer)(pox_buffer_v1 *buffer);
    int32_t (*set_ini_entries)(pox_slice_v1 entries);
    int32_t (*execute_cli)(const pox_cli_request_v1 *request, int32_t *exit_code);

    int32_t (*web_create)(void **runtime);
    int32_t (*web_execute)(void *runtime, const pox_http_request_v1 *request,
                           pox_http_response_v1 *response, int32_t *exit_code);
    void (*web_destroy)(void *runtime);

    int32_t (*worker_create)(void **runtime);
    int32_t (*worker_run)(void *runtime, pox_slice_v1 script_filename,
                          pox_slice_v1 document_root,
                          const pox_worker_callbacks_v1 *callbacks,
                          int32_t *exit_code);
    void (*worker_destroy)(void *runtime);

    /* Consume two reserved pointer slots without changing the table size. */
    int32_t (*web_thread_enter)(void *runtime);
    void (*web_thread_leave)(void *runtime);
    int32_t (*cancellation_create)(void **control);
    void (*cancellation_request)(void *control);
    void (*cancellation_release)(void *control);
    void *reserved[11];
} pox_php_api_v1;

/* The only public symbol exported by a Pox PHP runtime. */
POX_PHP_EXPORT const pox_php_api_v1 *pox_php_get_api(
    uint32_t requested_major,
    uint32_t requested_minor
);

#if defined(__cplusplus)
}
#endif

#endif
