#include <common.h>
#include <syscall.h>
#include <errno.h>

struct syscall_range {
    const ke_syscall_handler_t* table;
    size_t count;
};

static struct syscall_range syscall_ranges[SYSCALL_NUM_RANGES];

export int KeRegisterSyscallRange(int range, const ke_syscall_handler_t* table, size_t count) {
    if (range < 0 || range >= SYSCALL_NUM_RANGES || table == NULL || count > SYSCALL_RANGE_SIZE) {
        return EINVAL;
    }
    if (syscall_ranges[range].table != NULL) {
        return EEXIST;
    }
    syscall_ranges[range].count = count;
    syscall_ranges[range].table = table;
    return 0;
}

#define KE_SYSCALL_ENTRY(name)  [SYS_##name - SYSCALL_BASE(SYSCALL_RANGE_KE)] = Sys##name,

static const ke_syscall_handler_t ke_syscall_table[] = {
    KE_SYSCALLS(KE_SYSCALL_ENTRY)
};

void KeInitSyscalls(void) {
    KeRegisterSyscallRange(SYSCALL_RANGE_KE, ke_syscall_table,
                           sizeof(ke_syscall_table) / sizeof(ke_syscall_table[0]));
}

size_t KePerformSystemCall(size_t call, size_t a, size_t b, size_t c, size_t d) {
    size_t range = call >> SYSCALL_RANGE_BITS;
    size_t index = call & (SYSCALL_RANGE_SIZE - 1);
    if (range >= SYSCALL_NUM_RANGES) {
        return ENOSYS;
    }

    const struct syscall_range* r = &syscall_ranges[range];
    if (r->table == NULL || index >= r->count || r->table[index] == NULL) {
        return ENOSYS;
    }
    return r->table[index](a, b, c, d);
}
