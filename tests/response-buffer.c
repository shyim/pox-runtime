#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation = 0;
static void *test_realloc(void *pointer, size_t size) {
    return fail_allocation ? NULL : realloc(pointer, size);
}
#define realloc test_realloc
#include "../src/response_buffer.h"
#undef realloc

int main(void) {
    char *buffer = NULL;
    size_t capacity = 0;
    assert(pox_response_reserve(&buffer, &capacity, 0, 0, 0));
    assert(buffer == NULL && capacity == 0);
    assert(!pox_response_reserve(&buffer, &capacity, 0, 1, 0));
    assert(pox_response_reserve(&buffer, &capacity, 0, 3, 3));
    assert(capacity == 3);
    memcpy(buffer, "abc", 3);
    char *previous = buffer;
    assert(!pox_response_reserve(&buffer, &capacity, 3, 1, 3));
    assert(!pox_response_reserve(&buffer, &capacity, SIZE_MAX, 1, SIZE_MAX));
    assert(!pox_response_reserve(&buffer, &capacity, 1, SIZE_MAX, SIZE_MAX));
    assert(buffer == previous && capacity == 3 && memcmp(buffer, "abc", 3) == 0);
    fail_allocation = 1;
    assert(!pox_response_reserve(&buffer, &capacity, 3, 1, 8));
    assert(buffer == previous && capacity == 3 && memcmp(buffer, "abc", 3) == 0);
    fail_allocation = 0;
    assert(pox_response_reserve(&buffer, &capacity, 3, 5, 8));
    assert(capacity == 8 && memcmp(buffer, "abc", 3) == 0);
    free(buffer);
    return 0;
}
