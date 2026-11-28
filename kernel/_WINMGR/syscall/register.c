#include <common.h>
#include <syscall.h>
#include "../winmgr_internal.h"

#define WM_SYSCALL_ENTRY(name)  [SYS_##name - SYSCALL_BASE(SYSCALL_RANGE_WM)] = Sys##name,

static const ke_syscall_handler_t wm_syscall_table[] = {
    WM_SYSCALLS(WM_SYSCALL_ENTRY)
};

/*
 * Hands the window manager's system calls to the kernel. Everything else about them (the numbers, the
 * prototypes, this table) comes from WM_SYSCALLS in syscall.h.
 */
void WmInitSyscalls(void) {
    KeRegisterSyscallRange(SYSCALL_RANGE_WM, wm_syscall_table,
                           sizeof(wm_syscall_table) / sizeof(wm_syscall_table[0]));
}
