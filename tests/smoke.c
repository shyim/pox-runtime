#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "pox_php_runtime.h"

typedef const pox_php_api_v1 *(*get_api_fn)(uint32_t, uint32_t);

static int buffer_contains(const uint8_t *haystack, size_t haystack_len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len > haystack_len) return 0;
    for (size_t offset = 0; offset <= haystack_len - needle_len; offset++) {
        if (memcmp(haystack + offset, needle, needle_len) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    get_api_fn get_api = (get_api_fn)dlsym(library, "pox_php_get_api");
    if (get_api == NULL) return 1;
    const pox_php_api_v1 *api = get_api(1, 0);
    if (api == NULL || api->abi_major != 1 || api->abi_minor < 0) return 1;

    pox_buffer_v1 metadata = {0};
    if (api->metadata_json(&metadata) != POX_STATUS_OK) return 1;
    fwrite(metadata.data, 1, metadata.len, stdout);
    fputc('\n', stdout);
    if (!buffer_contains(metadata.data, metadata.len, "\"zts\":true")) return 1;
    api->free_buffer(&metadata);

    static const char code[] = "echo 'runtime-ok';";
    pox_cli_request_v1 request = {0};
    request.struct_size = sizeof(request);
    request.operation = POX_CLI_EXECUTE_CODE;
    request.source.data = (const uint8_t *)code;
    request.source.len = strlen(code);
    int32_t exit_code = -1;
    if (api->execute_cli(&request, &exit_code) != POX_STATUS_OK || exit_code != 0) return 1;
    dlclose(library);
    return 0;
}
