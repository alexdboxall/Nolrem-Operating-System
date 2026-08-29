
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
#include "_WINMGR/winmgr_internal.h"


void draw_test(struct graphics_driver* drv, uint8_t* pattern, int x, int y) {
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

    struct dc* dc = CdCreateDc();

    /*struct region r1 = CdCreateEllipseRegion(300, 100, 150, 150);
    struct region r2 = CdCreateRectRegion(330, 130, 200, 150);
    struct region r3 = CdIntersectRegion(r1, r2);

    CdSubtractRegionInPlace(&r1, r3);
    CdSubtractRegionInPlace(&r2, r3);

    CdTranslateRegion(&r1, -25, -25);
    CdTranslateRegion(&r2, +25, +25);
    
    struct brush* b1 = CdCreatePatternedBrush(0xFFFF0000, TransparentColour(), BRUSH_PATTERN_BIG_DIAG_CROSS);
    CdPaintRegionWithBrush(dc, r1, b1);
    struct brush* b2 = CdCreatePatternedBrush(0xFF00FF00, 0xFFFF0000, BRUSH_PATTERN_50_PERCENT);
    CdPaintRegionWithBrush(dc, r2, b2);
    struct brush* b3 = CdCreateSolidBrush(0xFF4ba67d);
    //CdCreatePatternedBrush(0xFFfcba03, 0xFF4ba67d, BRUSH_PATTERN_CROSS);
    CdPaintRegionWithBrush(dc, r3, b3);
    CdInvertRect(dc, 65, 175, 400, 200);

    LogStringAndHexLine("Region 1 size: 0x", r1.used_length);
    LogStringAndHexLine("Region 2 size: 0x", r2.used_length);
    LogStringAndHexLine("Region 3 size: 0x", r3.used_length);

    CdFreeRegion(r1);
    CdFreeRegion(r2);
    CdFreeRegion(r3);
*/

    /*struct graphics_driver* drv = GetOutputDriver(dc);
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
        asm("hlt");
        asm("hlt");
        draw_test(drv, pattern, x, 50);
        x += dx;
        pattern[0] ^= 3;
        pattern[1] ^= 3;
        if (x == 450 || x == 50) {
            dx *= -1;
        }
    }*/

    // Now let's try running the *USERMODE* versions!
    //region_t r = CreateRectRegion(50, 60, 300, 250);
    //LogStringAndHexLine("Have a user object at ", (size_t) r);

    /*
    CdPaintGradientRectHz(dc, 50, 400, 200, 20, 0xFF000080, 0xFF000080);
    CdPaintGradientRectHz(dc, 250, 400, 250, 20, 0xFF000080, 0xFF00C8FF);
    CdPaintGradientRectHz(dc, 500, 400, 100, 20, 0xFF00CAFF, 0xFF00CAFF);

    CdPaintGradientRectHz(dc, 50, 440, 200, 20, 0xFF404040, 0xFF404040);
    CdPaintGradientRectHz(dc, 250, 440, 250, 20, 0xFF404040, 0xFFC0C0C0);
    CdPaintGradientRectHz(dc, 500, 440, 100, 20, 0xFFC0C0C0, 0xFFC0C0C0);


    // 240 rows for the hue wheel (40 pixels per color transition)
for (int y = 0; y < 240; ++y) {
    // Map 240 rows across 6 RGB hue regions (6 * 255 = 1530 steps) using pure integer math
    int pos = (y * 1530) / 240;
    int region = pos / 255;
    int val = pos % 255;

    uint8_t r = 0, g = 0, b = 0;

    switch (region) {
        case 0: r = 255;       g = val;       b = 0;         break; // Red -> Yellow
        case 1: r = 255 - val; g = 255;       b = 0;         break; // Yellow -> Green
        case 2: r = 0;         g = 255;       b = val;       break; // Green -> Cyan
        case 3: r = 0;         g = 255 - val; b = 255;       break; // Cyan -> Blue
        case 4: r = val;       g = 0;         b = 255;       break; // Blue -> Magenta
        case 5: r = 255;       g = 0;         b = 255 - val; break; // Magenta -> Red
        default: break;
    }

    uint32_t hue = 0xFF000000 | (r << 16) | (g << 8) | b;

    // Left bar: Black to Hue | Right bar: Hue to White
    CdPaintGradientRectHz(dc,   0, y, 320, 1, 0xFF000000, hue);
    CdPaintGradientRectHz(dc, 320, y, 320, 1, hue,        0xFFFFFFFF);
}

// Final 10-pixel block for the neutral gray band
for (int y = 240; y < 250; ++y) {
    uint32_t gray = 0xFF808080;
    CdPaintGradientRectHz(dc,   0, y, 320, 1, 0xFF000000, gray);
    CdPaintGradientRectHz(dc, 320, y, 320, 1, gray,       0xFFFFFFFF);
}


    CdPaintGradientRectHz(dc, 0, 280, 640, 40, 0xFF30361B, 0xFF30361B);
    CdPaintGradientRectHz(dc, 0, 330, 640, 60, 0xFF000000, 0xFFFFFFFF);


*/
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            //CdPaintGradientRectHz(dc, x, y, 1, 1, 0xFFA0E0FF, 0xFFA0E0FF);
        }
    }

    /*
    CdPaintGradientRectHz(dc, 47 + 6, 97 + 6, 556, 326, 0xFF4095bf, 0xFF4095bf);
    CdPaintGradientRectHz(dc, 47 + 2, 97 + 2, 556, 326, BlackColour(), BlackColour());
    CdPaintGradientRectHz(dc, 47, 97, 556, 326, SystemColour(), SystemColour());
    CdPaintGradientRectHz(dc, 50, 100, 200, 20, 0xFF000080, 0xFF000080);
    CdPaintGradientRectHz(dc, 250, 100, 250, 20, 0xFF000080, 0xFF00C8FF);
    CdPaintGradientRectHz(dc, 500, 100, 100, 20, 0xFF00CAFF, 0xFF00CAFF);
    CdPaintGradientRectHz(dc, 50, 120, 550, 300, WhiteColour(), WhiteColour());
*/
    extern void WmInit(void);
    WmInit();

    struct window* win = WmCreateWindow(WmGetDesktop(), NULL, (struct rect) {
        .x = 75, .y = 75, .w = 300, .h = 350
    }, true);

    struct rect w2pos = (struct rect) {
        .x = 175, .y = 175, .w = 450, .h = 150
    };

    struct window* win2 = WmCreateWindow(WmGetDesktop(), NULL, w2pos, true);
    
    WmCallWinProc(WmGetDesktop(), (struct msg) {
        .type = WM_PAINT
    });

    while (true) {
        w2pos.x = (((w2pos.x - 175) + 1) % 100) + 175;
        WmChangePosition(win2, w2pos, true);
        
        WmInvalidateWindow(WmGetDesktop(), true);
        WmInvalidateWindow(win, true);
        WmInvalidateWindow(win2, true);

        WmCallWinProc(WmGetDesktop(), (struct msg) {
            .type = WM_PAINT
        });
        WmCallWinProc(win, (struct msg) {
            .type = WM_PAINT
        });
        WmCallWinProc(win2, (struct msg) {
            .type = WM_PAINT
        });
    }
    
    (void) win;
    (void) win2;

    DerefObject(dc);

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
    InitVga();
    InitBoostrapHeap();
    InitKernelVirtArena();
    InitTimer();
    ArchInit();
    ArchCallGlobalConstructors();

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
