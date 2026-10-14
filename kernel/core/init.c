
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
#include <errno.h>
#include <aio.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <_test.h>

export _Noreturn void InitKernelResidentPortion(void) {
    extern void CdInit();
    extern void WmInit();
    extern _Noreturn void WmMainloop();

    MarkPageableSegmentsDiscardable();
    while (DiscardPage() != NULL) {
        ;
    }

    CdInit();
#ifndef NDEBUG
    RunTestSuite();
#endif

    struct file* f;
    int res = OpenFile("raw-fd0:", O_RDONLY, 0, &f);
    LogPrintf("OpenFile: res = %d\n", res);

    // int CreateIO(struct aio* aio, void* node_or_file, struct transfer tr, void(*callback)(void*), void* callback_ctxt);

    uint8_t buffer[512];
    struct transfer tr = CreateKernelTransfer(buffer, 512, 0, TRANSFER_READ);
    struct aio aio;
    LogPrintf("Creating IO...\n");
    CreateIO(&aio, f, tr, NULL, NULL);
    LogPrintf("About to read file...\n");
    res = ReadFile(&aio);
    LogPrintf("ReadFile: res = %d\n", res);
    res = WaitIO(&aio, TIMEOUT_INFINITE);
    LogPrintf("WaitIO: res = %d\n", res);

    LogPrintf("sizeof(struct mutex      ):    0x%X\n", sizeof(struct mutex));
    LogPrintf("sizeof(struct sem        ):    0x%X\n", sizeof(struct sem));
    LogPrintf("sizeof(struct aio        ):    0x%X\n", sizeof(struct aio));
    LogPrintf("sizeof(struct page_origin):    0x%X\n", sizeof(struct page_origin));
    LogPrintf("sizeof(struct vas_chain  ):    0x%X\n", sizeof(struct vas_chain));
    LogPrintf("sizeof(struct virt_page  ):    0x%X\n", sizeof(struct virt_page));
    LogPrintf("sizeof(struct virt_arena ):    0x%X\n", sizeof(struct virt_arena));

    LogPrintf("\nCheck out the floppy bootsector (if it worked) at 0x%X\n", buffer);
    
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
    ArchInit2();
    InitKernelResidentPortion();
}
