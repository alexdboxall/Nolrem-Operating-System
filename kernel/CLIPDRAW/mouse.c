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

struct spinlock video_lock;

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

/* A cursor object: one whole cursor file. A file can hold several cursor types, picked by index. */
struct cursor {
    struct user_obj_header hdr;
    void* data;
    size_t len;
};

static void CleanupCursor(void* _cur) {
    struct cursor* cur = (void*) _cur;
    if (cur == NULL) {
        return;
    }
    if (cur->data != standard_mouse_file) {
        FreeHeap(cur->data);
    }
    FreeHeap(cur);
}

struct cursor* CdCreateCursor(void* file_data, size_t length) {
    struct cursor* cur = AllocHeap(sizeof(struct cursor));
    if (cur == NULL) {
        return NULL;
    }
    InitUserObject(cur, UOBJ_CURSOR);
    cur->len = length;
    if (file_data == standard_mouse_file) {
        cur->data = standard_mouse_file;     /* shared, never freed */
    } else {
        cur->data = AllocHeap(length);
        if (cur->data == NULL) {
            FreeHeap(cur);
            return NULL;
        }
        memcpy(cur->data, file_data, length);
    }
    return cur;
}

/* The standard cursor file, created in CdInitMouseSubsystem and kept for the life of the system. */
static struct cursor* standard_cursor = NULL;

/* The most recently requested cursor and index. Holds a reference, so the pointer comparison in
 * CdSetCursor can never match a freed-and-reallocated object. */
static struct cursor* prev_cursor = NULL;
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
/* A cursor change that arrived while the lock was busy. Latest request wins. pending_cursor owns a
 * reference to the cursor object, so the file data that pending_cursor_record points into cannot be
 * freed before the change is applied. NULL = nothing pending. */
static struct cursor* volatile pending_cursor = NULL;
static const uint8_t* volatile pending_cursor_record = NULL;
static volatile bool pending_cursor_flip_vt = false;

/* Call with video_lock held. */
static void ApplyPendingCursor(void) {
    /* Atomic take, so the pending reference is released exactly once. */
    struct cursor* cur = __atomic_exchange_n(&pending_cursor, NULL, __ATOMIC_ACQ_REL);
    if (cur == NULL) {
        return;
    }
    ApplyCursorRecord(pending_cursor_record, pending_cursor_flip_vt);
    /* The shape is decoded into the live arrays now, so the pending reference is no longer needed.
     * This is never the last reference: a pending cursor is always the latest request, which
     * prev_cursor also references, so nothing is freed here (this can run from the mouse IRQ). */
    DerefObject(cur);
}

static void DoBufferedMouse(void) {
    ApplyPendingCursor();
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
void CdRemoveMouse(void) {
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

    ApplyPendingCursor();

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


/* Validate cursor_index against the cursor file and find its record. No state is touched. */
static int FindCursorRecord(const struct cursor* cur, size_t cursor_index, const uint8_t** rec_out, bool* flip_vt_out) {
    const uint8_t* cursor_file_data = cur->data;
    size_t file_length = cur->len;

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

    *rec_out = cursor_file_data + offset;
    *flip_vt_out = flip_vt;
    return 0;
}

/* Make cursor number cursor_index of the cursor object cur the active cursor.
 * The caller keeps its own reference to cur; this function takes whatever references it needs. */
export int CdSetCursor(struct cursor* cur, size_t cursor_index) {
    if (cur == NULL) {
        return EINVAL;
    }
    if (cur == prev_cursor && cursor_index == prev_cursor_index) {
        /* No change - common case. */
        return 0;
    }

    RefObject(cur);

    const uint8_t* rec;
    bool flip_vt;
    int err = FindCursorRecord(cur, cursor_index, &rec, &flip_vt);
    if (err != 0) {
        DerefObject(cur);
        return err;
    }

    /* prev_cursor takes over the reference taken above; drop the one it held before. */
    struct cursor* old_prev = __atomic_exchange_n(&prev_cursor, cur, __ATOMIC_ACQ_REL);
    prev_cursor_index = cursor_index;

    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        ApplyCursorRecord(rec, flip_vt);
        ReleaseSpinlock(&video_lock);
    } else {
        /* A video update is in progress; applied in CdEndVideoUpdate. The pending change holds its own
         * reference until then, so cur's file data stays valid even if every other reference goes away. */
        RefObject(cur);
        pending_cursor_record = rec;
        pending_cursor_flip_vt = flip_vt;
        struct cursor* old_pending = __atomic_exchange_n(&pending_cursor, cur, __ATOMIC_ACQ_REL);
        if (old_pending != NULL) {
            DerefObject(old_pending);    /* superseded before it was ever applied */
        }
    }

    if (old_prev != NULL) {
        DerefObject(old_prev);           /* may free the old cursor; we are not holding the video lock here */
    }
    return 0;
}

export int CdSetStandardCursor(int index) {
    if (index < 0 || standard_cursor == NULL) {
        return EINVAL;
    }
    return CdSetCursor(standard_cursor, (size_t) index);
}

void CdInitMouseSubsystem(void) {
    InitSpinlock(&video_lock);
    RegisterUserObjectType(UOBJ_CURSOR, CleanupCursor);
    standard_cursor = CdCreateCursor(standard_mouse_file, sizeof(standard_mouse_file));
    CdSetStandardCursor(CURSOR_WAIT);
}