#include <unaligned.h>
#include <common.h>
#include <errno.h>
#include <usersys.h>

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
    if (index < 0 || strtab == NULL || strtab->mmap_ptr == NULL || index >= strtab->mmap_ptr->num_strings) {
        SetLastError(EINVAL);
        return NULL;
    }
    uint32_t offset;
    if (strtab->mmap_ptr->flags & STRTAB_OFF32) {
        offset = ReadUnaligned32LE(strtab->mmap_ptr->data + index * 4);
    } else {
        offset = ReadUnaligned16LE(strtab->mmap_ptr->data + index * 2);
    }
    return (const char*) strtab->mmap_ptr->data + offset;
}
