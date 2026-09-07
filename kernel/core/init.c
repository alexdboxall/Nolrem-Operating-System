
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

typedef int rhandle_t;
#define USED __attribute__((used))

#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>

#define EINVAL 1


static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

#include "CLIPDRAW/api.h"

int vga_rect_callback(struct rect r, void* ctxt, int colour, bool* cancel) {
    extern void VGAPutRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);
    VGAPutRect(NULL, r.x, r.y, r.x + r.w, r.y + r.h, colour);
    *cancel = false;
    (void) ctxt;
    return colour;
}

extern void InitVga();

#include "CLIPDRAW/clipdraw_internal.h"
#include "CLIPDRAW/gfx_driver.h"
#include "_WINMGR/winmgr_internal.h"


void draw_test(struct graphics_driver* drv, uint8_t* pattern, int x, int y) {
    drv->pen_line(drv, x, y, x, y + 400, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x, y + 400, x + 400, y + 400, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x + 400, y + 400, x + 400, y, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x + 400, y, x, y, 0xFFFF0000, 2, pattern, 2, 2, true);
}

export _Noreturn void InitKernelResidentPortion(void) {
    extern void CdInit();
    extern void WmInit();

    CdInit();
    WmInit();
  
    WmMainloop();
}

void TestTask(void*) {
    BeginNewThread();
    
    LogPrintf("Running test task...!\n");
    Schedule();
    LogPrintf("Running test task... again!\n");
    Schedule();
    while (true) {
        LogPrintf("Ok, that's enough of the test task!\n");
        Schedule();
    }
}

/* 
 * We want to be able to page out some of the very early bootstrap code.
 * So this is the part of the kernel that runs when discarding is permitted.
 */
export _Noreturn pageable void KernelTask(void*) {
    asm ("sti");
        LogString("Ready\n");
        Schedule();
        LogString("Ready3!\n");
        (void) TestTask;
        //CreateThread(GetKernelVas(), TestTask, NULL);
        LogString("Added thread!\n");
        Schedule();
        LogString("Scheduled!\n");
    InitMessageBox();
    InitModule();
    InitVnode();
        LogString("Ready4!\n");
        Schedule();
    InitDiskUtil();
    InitFile();
    InitUserObjectType();
        LogString("Ready5!\n");
        Schedule();
    InitVfs();
        LogString("Ready6!\n");

    //InitNullDevice();
        LogString("Ready7!\n");

    InitVga();
    LogString("Ready!\n");

    InitKernelResidentPortion();
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
    InitScheduler(NULL);
    KernelTask(NULL);
    while (true) {
        ;
    }
}
