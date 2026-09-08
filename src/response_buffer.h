#ifndef POX_RESPONSE_BUFFER_H
#define POX_RESPONSE_BUFFER_H
#include <stddef.h>
#include <stdlib.h>

/* Checked, capped geometric growth shared by body and header buffers. */
static int pox_response_reserve(char **buffer, size_t *capacity, size_t used,
                                size_t additional, size_t limit) {
    if (used > limit || additional > limit - used) return 0;
    size_t needed = used + additional;
    if (needed <= *capacity) return 1;
    size_t new_capacity = *capacity == 0 ? 4096 : *capacity;
    if (new_capacity > limit) new_capacity = limit;
    while (new_capacity < needed) {
        new_capacity = new_capacity > limit / 2 ? limit : new_capacity * 2;
    }
    char *next = realloc(*buffer, new_capacity);
    if (next == NULL) return 0;
    *buffer = next;
    *capacity = new_capacity;
    return 1;
}

#endif
