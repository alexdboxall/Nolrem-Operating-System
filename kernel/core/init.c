
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
#include <file.h>
#include <module.h>
#include <fcntl.h>
#include <vmm.h>
#include <sem.h>
#include <scheduler.h>
#include <thread.h>
#include <diskutil.h>
#include <dev.h>

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


static void draw_test(struct graphics_driver* drv, uint8_t* pattern, int x, int y) {
    drv->pen_line(drv, x, y, x, y + 400, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x, y + 400, x + 400, y + 400, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x + 400, y + 400, x + 400, y, 0xFFFF0000, 2, pattern, 2, 2, true);
    drv->pen_line(drv, x + 400, y, x, y, 0xFFFF0000, 2, pattern, 2, 2, true);
}

/* 
 * We want to be able to page out some of the very early bootstrap code.
 * So this is the part of the kernel that runs when discarding is permitted.
 */
export _Noreturn void InitKernelResidentPortion(void) {
    LogString("Ready!\n");

    extern void CdInit();
    CdInit();

    struct region r1 = CdCreateEllipseRegion(300, 100, 150, 150);
    struct region r2 = CdCreateRectRegion(330, 130, 200, 150);
    struct region r3 = CdIntersectRegion(r1, r2);

    CdSubtractRegionInPlace(&r1, r3);
    CdSubtractRegionInPlace(&r2, r3);

    struct dc* dc = CdCreateDc();
    CdTranslateRegion(&r1, -25, -25);
    CdTranslateRegion(&r2, +25, +25);
    
    struct brush* b1 = CdCreatePatternedBrush(0xFFFF0000, TransparentColour(), BRUSH_PATTERN_BIG_DIAG_CROSS);
    CdPaintRegionWithBrush(dc, r1, b1);
    struct brush* b2 = CdCreatePatternedBrush(0xFF00FF00, 0xFFFF0000, BRUSH_PATTERN_50_PERCENT);
    CdPaintRegionWithBrush(dc, r2, b2);
    struct brush* b3 = CdCreatePatternedBrush(0xFFfcba03, 0xFF4ba67d, BRUSH_PATTERN_CROSS);
    CdPaintRegionWithBrush(dc, r3, b3);
    CdInvertRect(dc, 65, 175, 400, 200);
    DerefObject(dc);

    LogStringAndHexLine("Region 1 size: 0x", r1.used_length);
    LogStringAndHexLine("Region 2 size: 0x", r2.used_length);
    LogStringAndHexLine("Region 3 size: 0x", r3.used_length);

    CdFreeRegion(r1);
    CdFreeRegion(r2);
    CdFreeRegion(r3);

    struct graphics_driver* drv = GetOutputDriver(dc);
    //drv->thin_line(drv, 50, 400, 600, 450, 0xFFFF0000);
    drv->solid_line(drv, 50, 400, 600, 450, 0xFFFF4000, 3);
    uint8_t pattern[] = {
        0b10,
        0b01,
    };
    int x = 50;
    int dx = 1;
    while (true) {
        draw_test(drv, pattern, x, 50);
        draw_test(drv, pattern, x, 50);
        x += dx;
        pattern[0] ^= 3;
        pattern[1] ^= 3;
        if (x == 450 || x == 50) {
            dx *= -1;
        }
    }

    // Now let's try running the *USERMODE* versions!
    region_t r = CreateRectRegion(50, 60, 300, 250);
    LogStringAndHexLine("Have a user object at ", (size_t) r);

    while (true) {
        ArchIdle();
    }
} 

/* 
 * This stuff will be discardable, so we'd better finish up this before we turn
 * on the discarder!
 */
export _Noreturn pageable void InitKernel(struct kernel_boot_info* boot_info) {
    InitLog();
    InitBoostrapHeap();
    InitKernelVirtArena();
    InitTimer();
    ArchInit();
    ArchCallGlobalConstructors();
    InitVga();

    InitPhys(
        (void*)((size_t) boot_info->ram_table + 0xC0000000), 
        boot_info->num_ram_table_entries
    );
    InitVmm();
    InitSem();
    InitModule();
    InitVnode();
    InitDiskUtil();
    InitFile();
    InitUserObjectType();
    InitThread();
    InitScheduler();
    InitVfs();
    InitNullDevice();

    struct file* f;
    int res = OpenFile("null:", O_WRONLY, 0, &f);
    char* buffer = "This is some text.";
    struct transfer tr = CreateKernelTransfer(buffer, 12, 0, TRANSFER_WRITE);
    res = WriteFile(f, &tr);
    LogStringAndHexLine("The write call returned: ", res);
    InitKernelResidentPortion();
}
