
#include <bootloader.h>
#include <common.h>
#include <obj.h>
#include <log.h>
#include <panic.h>
#include <heap.h>
#include <phys.h>
#include <timer.h>
#include <allocvirt.h>
#include <dc.h>
#include <vnode.h>
#include <vfs.h>
#include <cpu.h>
#include <file.h>
#include <module.h>
#include <fcntl.h>
#include <vmm.h>
#include <sem.h>
#include <scheduler.h>
#include <thread.h>
#include <diskutil.h>
#include <dev.h>
#include <msgbox.h>
#include <arch.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

#define EINVAL 1

export _Noreturn void InitKernelResidentPortion(void) {
    extern void CdInit();
    extern void WmInit();
    extern _Noreturn void WmMainloop();

    MarkPageableSegmentsDiscardable();
    while (DiscardPage() != NULL) {
        ;
    }
    CdInit();
    WmInit();
    WmMainloop();
}


/* 
 * This stuff will be discardable, so we'd better finish up this before we turn
 * on the discarder!
 */
export _Noreturn pageable void InitKernel(struct kernel_boot_info* boot_info) {
    InitLog();
    InitBootstrapHeap();
    InitCpuTable();
    InitKernelVirtArena();
    InitTimer();
    ArchInit();
    ArchCallGlobalConstructors();

    InitPhys(
        (void*)((size_t) boot_info->ram_table + ARCH_KRNL_MAPPING_BASE), 
        boot_info->num_ram_table_entries
    );
    InitVmm();
    InitSem();
    InitThread();
    CreateInitialVas();
    InitScheduler();
    ArchEnableInterrupts();
    InitMessageBox();
    InitModule();
    InitVnode();
    InitDiskUtil();
    InitFile();
    InitUserObjectType();
    InitVfs();
    InitNullDevice();
    InitKernelResidentPortion();
}
