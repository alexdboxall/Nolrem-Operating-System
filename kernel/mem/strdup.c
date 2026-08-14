#include <common.h>
#include <heap.h>
#include <string.h>

export char* KeStrdup(const char* str) {
    char* out = AllocHeap(strlen(str) + 1);
    strcpy(out, str);
    return out;
}
