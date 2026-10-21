#include "clipdraw_internal.h"
#include "../_WINMGR/winmgr_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <panic.h>
#include <kgfx.h>
#include <heap.h>
#include <spinlock.h>
#include <string.h>
#include <unaligned.h>

static struct spinlock video_lock;

extern void VgaInvertRect(struct graphics_driver* drv, int x1, int y1, int x2, int y2);
extern colour_t VGAReadPixel(struct graphics_driver*, int x, int y);
extern void VgaPutSolidRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);

/* You could have your own ID types and use those, that's fine. 
 * But set upper bound so that negative input overflows to very big. */
#define MAX_CURSOR_IDS  32

/* The cursor shape that is currently active (what RealDrawMouse paints). */
static uint32_t decoded_current_cursor_black[32] = {0};
static uint32_t decoded_current_cursor_white[32] = {0};
static int current_hotspot_x = 0;
static int current_hotspot_y = 0;
static int current_mouse_w = 0;
static int current_mouse_h = 0;

#include "standard_cursor.h"

static uint8_t* prev_cursor_file_data = NULL;
static size_t prev_cursor_index = 0;

/* Logical pointer position, in *hotspot* coordinates, as last given to CdDrawMouse().
 * This is the one position that stays meaningful when the shape (and so the hotspot) changes. */
static bool pointer_valid = false;
static int pointer_x = 0;
static int pointer_y = 0;

/* What is physically painted on the screen right now: top-left corner and size *as drawn*.
 * Removal always uses these, never the current shape or a caller-supplied position, because
 * the restore buffer only matches what was drawn. */
static bool mouse_onscreen = false;
static int mouse_screen_x = 0;
static int mouse_screen_y = 0;
static int mouse_screen_w = 0;
static int mouse_screen_h = 0;

static struct graphics_driver* GetMouseDriver(void) {
    static struct graphics_driver* drv = NULL;
    if (drv == NULL) {
        drv = GetKernelGraphicsDriver();
        struct graphics_capabilities caps = drv->get_capabilities(drv);
        int bytes = 32 * 32 * 4;
        if (caps.desired_mouse_restore_buffer_mode == MOUSE_BUFFER_UINT8) bytes /= 4;
        if (caps.desired_mouse_restore_buffer_mode == MOUSE_BUFFER_UINT16) bytes /= 2;
        drv->mouse_restore_buffer = AllocPermanentHeap(bytes);
    }
    return drv;
}

/* All Real* functions must be called with video_lock held. */

static void RealRemoveMouse(void) {
    if (!mouse_onscreen) {
        return;
    }
    struct graphics_driver* drv = GetMouseDriver();
    drv->remove_mouse(drv, mouse_screen_x, mouse_screen_y, drv->mouse_restore_buffer, mouse_screen_w, mouse_screen_h);
    mouse_onscreen = false;
}

/* x, y are the pointer (hotspot) position; the hotspot of the *current* shape is applied here. */
static void RealDrawMouse(int x, int y) {
    /* Never draw over an already-painted cursor: that would overwrite the restore buffer with cursor pixels. */
    RealRemoveMouse();

    struct graphics_driver* drv = GetMouseDriver();
    int left = x - current_hotspot_x;
    int top  = y - current_hotspot_y;
    drv->draw_mouse(
        drv, left, top, decoded_current_cursor_black, decoded_current_cursor_white, drv->mouse_restore_buffer,
        current_mouse_w, current_mouse_h
    );
    mouse_onscreen = true;
    mouse_screen_x = left;
    mouse_screen_y = top;
    mouse_screen_w = current_mouse_w;
    mouse_screen_h = current_mouse_h;
}

/* Swap in a new shape, decoding straight from its (already validated) record in the cursor file:
 *   [0]=hotspot_x (int8) [1]=hotspot_y (int8) [2]=lines [3]=flags|width, then black rows, then white rows.
 * Pulls the old cursor off the screen using the geometry it was drawn with, then repaints at the same
 * pointer position using the new hotspot. */
static void ApplyCursorRecord(const uint8_t* rec, bool flip_vt) {
    bool was_onscreen = mouse_onscreen;
    RealRemoveMouse();

    int lines = rec[2];
    bool has_black = !!(rec[3] & 0x80);
    bool has_white = !!(rec[3] & 0x40);
    int width = rec[3] & 0x3F;
    bool use_32 = width >= 17;

    /* Rows past 'lines' (and absent layers) must not keep stale pixels. */
    memset(decoded_current_cursor_black, 0, sizeof(decoded_current_cursor_black));
    memset(decoded_current_cursor_white, 0, sizeof(decoded_current_cursor_white));

    void* ptr = (void*) (rec + 4);
    for (int layer = 0; layer < 2; ++layer) {
        if (!(layer == 0 ? has_black : has_white)) {
            continue;
        }
        uint32_t* out = (layer == 0) ? decoded_current_cursor_black : decoded_current_cursor_white;
        for (int i = 0; i < lines; ++i) {
            uint32_t data;
            if (use_32) {
                data = ReadUnaligned32AndAdvanceLE(&ptr);
            } else {
                data = ReadUnaligned16AndAdvanceLE(&ptr);
                data <<= 16;
            }
            if (flip_vt) {
                out[lines - 1 - i] = data;
            } else {
                out[i] = data;
            }
        }
    }
    current_hotspot_x = (int8_t) rec[0];   /* signed in the file format */
    current_hotspot_y = (int8_t) rec[1];
    current_mouse_w = width;
    current_mouse_h = lines;

    if (was_onscreen) {
        RealDrawMouse(pointer_x, pointer_y);
    }
    /* If it was hidden for a video update, CdEndVideoUpdate redraws it at pointer_x/y. */
}

/* Work that arrived while a video update held the lock. Positions are not stored here:
 * the draw uses pointer_x/y (always the latest) and the remove uses what is on screen. */
static bool need_mouse_remove = false;
static bool need_mouse_draw = false;
/* A cursor change that arrived while the lock was busy: just a pointer to its record in the cursor file
 * (NULL = none). The file data must stay valid until the video update ends. Latest request wins. */
static const uint8_t* volatile pending_cursor_record = NULL;
static bool pending_cursor_flip_vt = false;

static void DoBufferedMouse(void) {
    if (pending_cursor_record != NULL) {
        const uint8_t* rec = pending_cursor_record;
        pending_cursor_record = NULL;
        ApplyCursorRecord(rec, pending_cursor_flip_vt);
    }
    if (need_mouse_remove) {
        need_mouse_remove = false;
        RealRemoveMouse();
    }
    if (need_mouse_draw) {
        need_mouse_draw = false;
        RealDrawMouse(pointer_x, pointer_y);
    }
}

/* x, y are accepted for API compatibility; the cursor is removed from wherever it was drawn. */
void CdRemoveMouse(int x, int y) {
    (void) x; (void) y;
    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        RealRemoveMouse();
        ReleaseSpinlock(&video_lock);
    } else {
        need_mouse_remove = true;
    }
}

/* x, y are the pointer (hotspot) position. */
void CdDrawMouse(int x, int y) {
    pointer_x = x;
    pointer_y = y;
    pointer_valid = true;
    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        RealDrawMouse(x, y);
        ReleaseSpinlock(&video_lock);
    } else {
        need_mouse_draw = true;
    }
}

static bool hid_mouse = false;

void CdStartVideoUpdate(int x1, int y1, int x2, int y2) {
    AcquireSpinlock(&video_lock);

    if (mouse_onscreen &&
        mouse_screen_x < x2 && mouse_screen_x + mouse_screen_w > x1 &&
        mouse_screen_y < y2 && mouse_screen_y + mouse_screen_h > y1) {
        RealRemoveMouse();
        hid_mouse = true;
    }
}

void CdEndVideoUpdate(int x1, int y1, int x2, int y2) {
    (void) x1; (void) y1; (void) x2; (void) y2;

    if (pending_cursor_record != NULL) {
        const uint8_t* rec = pending_cursor_record;
        pending_cursor_record = NULL;
        ApplyCursorRecord(rec, pending_cursor_flip_vt);
    }

    bool remove_requested = need_mouse_remove;
    if (need_mouse_remove) {
        need_mouse_remove = false;
        RealRemoveMouse();
    }

    if (need_mouse_draw) {
        need_mouse_draw = false;
        RealDrawMouse(pointer_x, pointer_y);
    } else if (hid_mouse && !remove_requested) {
        /* No move happened during the paint: put the cursor back where it was hidden from. */
        RealDrawMouse(pointer_x, pointer_y);
    }

    hid_mouse = false;
    ReleaseSpinlock(&video_lock);
}


export int CdSetCursor(uint8_t* cursor_file_data, size_t file_length, size_t cursor_index) {
    if (cursor_file_data == prev_cursor_file_data && cursor_index == prev_cursor_index) {
        /* No change - common case. */
        return 0;
    }

    if (file_length < 8 || cursor_index >= MAX_CURSOR_IDS) {
        return EINVAL;
    }
    if (memcmp(cursor_file_data, "CUR\x00", 4)) {
        return EINVAL;
    }
    size_t num_curs = cursor_file_data[4];
    if (num_curs == 0 || num_curs > MAX_CURSOR_IDS) {
        return EINVAL;
    }
    if (file_length < 8 + 2 * num_curs) {
        return EINVAL;
    }
    if (cursor_index >= num_curs) {
        return EINVAL;
    }
    size_t offset = ReadUnaligned16((void*) (cursor_file_data + 8 + cursor_index * 2));
    bool flip_vt = !!(offset & 0x8000);
    offset &= 0x7FFF;
    if (offset == 0) {
        /* This means the file doesn't have this cursor type. */
        return ENOENT;
    }
    if (offset + 4 > file_length) {
        return EINVAL;
    }

    int lines = cursor_file_data[offset + 2];
    uint8_t flags_and_width = cursor_file_data[offset + 3];
    if (lines > 32) {
        return EINVAL;
    }
    bool has_black = !!(flags_and_width & 0x80);
    bool has_white = !!(flags_and_width & 0x40);
    int width = (flags_and_width & 0x3F);
    if (width > 32) {
        return EINVAL;
    }
    bool use_32 = width >= 17;

    int data_bytes_needed = lines * (use_32 ? 4 : 2) * (((int) has_black) + ((int) has_white));
    if (offset + 4 + data_bytes_needed > file_length) {
        return EINVAL;
    }

    /* Validated. From here on the record is decoded straight from the file by ApplyCursorRecord. */
    const uint8_t* rec = cursor_file_data + offset;
    prev_cursor_index = cursor_index;
    prev_cursor_file_data = cursor_file_data;

    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        ApplyCursorRecord(rec, flip_vt);
        ReleaseSpinlock(&video_lock);
    } else {
        /* A video update is in progress; applied in CdEndVideoUpdate. */
        pending_cursor_record = rec;
        pending_cursor_flip_vt = flip_vt;
    }
    return 0;
}

export int CdSetStandardCursor(int index) {
    if (index < 0) {
        return EINVAL;
    }
    return CdSetCursor(standard_mouse_file, sizeof(standard_mouse_file), (size_t) index);
}

void CdInitMouseSubsystem(void) {
    InitSpinlock(&video_lock);
    //CdSetStandardCursor(CURSOR_WAIT);
}