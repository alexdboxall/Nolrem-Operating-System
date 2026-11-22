
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <msgbox.h>
#include <vfs.h>
#include <timer.h>
#include <errno.h>
#include <cpu.h>
#include <string.h>
#include <spinlock.h>
#include <arch.h>
#include <fcntl.h>
#include <interrupt.h>
#include <vmm.h>
#include <thread.h>
#include <scheduler.h>
#include <kgfx.h>
#include <phys.h>
#include <stdatomic.h>
#include "winmgr_internal.h"

int WmHardError(int herrno) {
    struct msgbox* sysbox = WmGetSystemMessageBox();
    struct graphics_driver* drv = KeGetKernelGraphicsDriver();
    int sw = WmGetDesktop()->local_client_bound.w;
    int sh = WmGetDesktop()->local_client_bound.h;
    CdStartVideoUpdate(0, 0, sw, sh);
    drv->fill_rect(drv, sw / 2 - 200 + 10, sh / 2 - 100 + 10, sw / 2 + 210, sh / 2 + 110, BlackColour());
    drv->fill_rect(drv, sw / 2 - 200, sh / 2 - 100, sw / 2 + 200, sh / 2 + 100,  SystemColour());
    drv->fill_rect(drv, sw / 2 - 200 + 3, sh / 2 - 100 + 3, sw / 2 + 200 - 3, sh / 2 + 100 - 3, WhiteColour());

    extern void VGADrawString(int x, int y, int pt, uint8_t col, const char* s, bool bold, bool italic);
    char* msg1 = "System Error";
    char* msg2 = "";
    char* msg3 = "";
    if (herrno == ENOMEM) {
        msg1 = "System Error - Out of Memory";
        msg2 = "For Nolrem to continue to operate, memory";
        msg3 = "must be freed by closing a program.";
    } else if (herrno == EIO) {
        msg1 = "System Error - Disk Error";
        msg2 = "A disk error occured while performing a critical";
        msg3 = "operation. Ensure the disk is inserted.";
    }
    VGADrawString(sw / 2 - 200 + 20, sh / 2 - 100 + 20, 8, 0, msg1, true, false);
    VGADrawString(sw / 2 - 200 + 20, sh / 2 - 100 + 50, 8, 0, msg2, false, false);
    VGADrawString(sw / 2 - 200 + 20, sh / 2 - 100 + 65, 8, 0, msg3, false, false);
    CdEndVideoUpdate(0, 0, sw, sh);

    while (true) {
        struct msg msg;
        int res = KeGetMessage(sysbox, &msg, TIMEOUT_INFINITE);
        if (res != 0) {
            continue;
        }

        switch (msg.type) {
        case SYSMSG_MOUSEEVENT: {
            int mx = msg.rect_arg.x;
            int my = msg.rect_arg.y;
            int btn = msg.i_arg;
            (void) btn;
            
            CdRemoveMouse();
            CdDrawMouse(mx, my);
            break;
        }
        }
    }
}