
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

static void DrawInvFrame(struct dc* dc, struct rect pos) {
    const int BORDER = 3;
    ActualInvertRect(dc, pos.x, pos.y, pos.x + pos.w, pos.y + BORDER, true);
    ActualInvertRect(dc, pos.x, pos.y + pos.h - BORDER, pos.x + pos.w, pos.y + pos.h, true);

    ActualInvertRect(dc, pos.x, pos.y + BORDER, pos.x + BORDER, pos.y + pos.h - BORDER, true);
    ActualInvertRect(dc, pos.x + pos.w - BORDER, pos.y + BORDER, pos.x + pos.w, pos.y + pos.h - BORDER, true);
}

/* 
 * We want to be able to page out some of the very early bootstrap code.
 * So this is the part of the kernel that runs when discarding is permitted.
 */
export _Noreturn void InitKernelResidentPortion(void) {
    LogString("Ready!\n");

    extern void CdInit();
    extern void WmInit();

    CdInit();
    WmInit();

    struct rect w1pos = (struct rect) {
        .x = 75, .y = 75, .w = 300, .h = 350
    };
    struct rect w2pos = (struct rect) {
        .x = 175, .y = 175, .w = 450, .h = 150
    };

    struct window* win = WmCreateWindow(WmGetDesktop(), NULL, w1pos, true);
    struct window* win2 = WmCreateWindow(WmGetDesktop(), NULL, w2pos, true);
    
    WmCallWinProc(WmGetDesktop(), (struct msg) {
        .type = WM_PAINT
    });

    struct dc* dc = CdCreateDc();

    struct point oldm = {.x = 0, .y = 0};
    int ticks_since_mouse_moved = 9;
    bool has_inv_region = false;

    WmCallWinProc(WmGetDesktop(), (struct msg) {
                        .type = WM_PAINT
                    });
                    WmCallWinProc(win, (struct msg) {
                        .type = WM_PAINT
                    });
                    WmCallWinProc(win2, (struct msg) {
                        .type = WM_PAINT
                    });
                    
    while (true) {
        //w1pos.y = (((w1pos.y - 75) + 1) % 35) + 75;
        //w2pos.x = (((w2pos.x - 175) + 3) % 100) + 175;
        //
        //WmChangePosition(win2, w2pos, true);
        asm ("sti");
        asm ("hlt");
        struct point m = WmGetMousePositionGlobal();
        if (m.x == oldm.x && m.y == oldm.y) {
            if (ticks_since_mouse_moved != -1) {
                ticks_since_mouse_moved++;
                if (ticks_since_mouse_moved >= 10) {
                    WmCallWinProc(WmGetDesktop(), (struct msg) {
                        .type = WM_PAINT
                    });
                    WmCallWinProc(win, (struct msg) {
                        .type = WM_PAINT
                    });
                    WmCallWinProc(win2, (struct msg) {
                        .type = WM_PAINT
                    });
                    ticks_since_mouse_moved = -1;
                    has_inv_region = false;
                }
            }
            
        } else {
            if (has_inv_region) {
                DrawInvFrame(dc, w2pos);
            }
            oldm = m;
            w2pos.x = m.x;
            w2pos.y = m.y;
            ticks_since_mouse_moved = 0;
            WmChangePosition(win2, w2pos, true);
            has_inv_region = true;
            DrawInvFrame(dc, w2pos);
        }
        
    }
    
    (void) win;
    (void) win2;

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
