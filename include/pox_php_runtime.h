#ifndef POX_PHP_RUNTIME_H
#define POX_PHP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define POX_PHP_ABI_MAJOR 1u
#define POX_PHP_ABI_MINOR 0u

#if defined(_WIN32)
#define POX_PHP_EXPORT __declspec(dllexport)
#else
#define POX_PHP_EXPORT __attribute__((visibility("default")))
#endif

typedef struct pox_slice_v1 {
    const uint8_t *data;
    size_t len;
} pox_slice_v1;

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

    void *reserved[16];
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
