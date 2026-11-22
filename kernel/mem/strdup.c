#include <common.h>
#include <heap.h>
#include <string.h>

/* Returns NULL if 'str' is NULL or there is no memory for the copy. */
export char* KeStrdup(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t length = strlen(str) + 1;
    char* out = AllocHeap(length);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, str, length);
    return out;
}
