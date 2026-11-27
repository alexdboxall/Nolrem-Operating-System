#include <unaligned.h>
#include <common.h>

#define STRTAB_OFF16    (0 << 1)
#define STRTAB_OFF32    (1 << 1)

struct string_table_header {
    char sig[3];
    uint8_t ver;
    uint16_t num_strings;
    uint16_t flags;
    uint8_t data[];
};

struct string_table {
    size_t length;
    struct string_table_header* mmap_ptr;
};

export pageableuserexec const char* GetString(struct string_table* strtab, int index) {
    if (index < 0 || strtab == NULL || index >= strtab->num_strings) {
        SetLastError(EINVAL);
        return NULL;
    }
    uint32_t offset;
    if (strtab->flags & STRTAB_OFF32) {
        offset = ReadUnaligned32LE(strtab->data + index * 4);
    } else {
        offset = ReadUnaligned16LE(strtab->data + index * 2);
    }
    return strtab->data + offset;
}
