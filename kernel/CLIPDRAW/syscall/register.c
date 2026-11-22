#include <common.h>
#include <syscall.h>
#include "../clipdraw_internal.h"

#define CD_SYSCALL_ENTRY(name)  [SYS_##name - SYSCALL_BASE(SYSCALL_RANGE_CD)] = Sys##name,

static const ke_syscall_handler_t cd_syscall_table[] = {
    CD_SYSCALLS(CD_SYSCALL_ENTRY)
};

/*
 * Hands CLIPDRAW's system calls to the kernel. Everything else about them (the
 * numbers, the prototypes, this table) comes from CD_SYSCALLS in syscall.h.
 */
void CdInitSyscalls(void) {
    KeRegisterSyscallRange(SYSCALL_RANGE_CD, cd_syscall_table,
                           sizeof(cd_syscall_table) / sizeof(cd_syscall_table[0]));
}
