#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <obj.h>
#include <spinlock.h>
#include <sys/types.h>
#include <allocvirt.h>
#include <mutex.h>

#define PF_PRESENT  1   /* protection violation, rather than a missing page */
#define PF_WRITE    2   /* the access was a write */
#define PF_USER     4   /* the access came from user mode */
#define PF_FETCH    8   /* the access was an instruction fetch */
struct file;
struct mutex;
struct page_origin;

/*
 * Custom loader for a file-backed page. A copy is stored by value in every
 * page_origin created by AllocFileMemory(), so the caller keeps no ownership
 * obligations over the struct itself. Callbacks should treat it as read-only:
 * they can run concurrently for the same origin on different frames.
 *
 * reload_func: Populate 'frame' with the page's contents. REPLACES the default
 *              file read; call VmmDefaultReload() first if you just want to
 *              post-process (e.g. relocate) what the file holds. The frame is
 *              NOT pre-zeroed - the whole page must be initialised. Runs under
 *              po->mtx and may block. Return 0 on success, errno otherwise.
 * unmap_func:  Called once the frame has been detached from its origin and is
 *              about to be freed, with the frame contents still intact. Runs
 *              with no VMM locks held when the page is released via
 *              FreeMemory(), but under the phys_page spinlock when reclaimed
 *              by DiscardPage() - it must not block in that case. Its return
 *              value is currently ignored.
 *
 * Either may be NULL (default read / nothing). Only meaningful for file pages.
 */
struct virt_loader {
    int (*reload_func)(size_t frame, struct virt_loader* vr, struct page_origin* po);
    int (*unmap_func)(size_t frame, struct virt_loader* vr, struct page_origin* po);
    size_t context_s;
    void* context_p;
};

/* TODO: could look into having this only allocate the first half for non-file
 * origins, and it allocating the whole lot on file pages - metadata reduction!*/
struct page_origin {
    struct obj_header hdr;
    uint8_t fixed       : 1;
    uint8_t is_file     : 1;
    struct mutex mtx;
    size_t phys;

    /* Only stored if it's a file-backed page. */
    struct file* file;
    size_t file_offset;
    struct virt_loader loader;
};

#define SIZEOF_PAGE_ORIGIN_ANON  (offsetof(struct page_origin, file))
#define SIZEOF_PAGE_ORIGIN_FILE  (sizeof(struct page_origin))

struct vas_chain {
    struct vas* vas;
    struct virt_page* vp;
    struct vas_chain* next;
};

struct virt_page {
    /* 
    IF PRESENT = 1 AND BUSY = 0
        Then it is present, and at PHYS
    IF PRESENT = 1 AND BUSY = 1
        Then it is TEMPORARILY non-present, as it's being exclusively held.
        It is still at 'phys' technically, but unreachable by us. Need to 
        yield and try again later.
    IF PRESENT = 0
        Then it's not here at all.
        See 'ORIGIN' for how to load it back in.
    */
    struct obj_header hdr;
    struct vas* vas;
    size_t virt;
    union {
        size_t phys;
        struct page_origin* origin;
    };
    uint8_t present  : 1;    /* nonwithstanding 'busy' */
    uint8_t write    : 1;
    uint8_t user     : 1;
    uint8_t accessed : 1;
    uint8_t dirty    : 1;
    uint8_t executable : 1;
    uint8_t fixed    : 1;
    uint8_t busy;
} __attribute__((packed));

struct phys_page {
    struct page_origin* origin;
    struct vas* vas;
    struct virt_page* vp;
    struct vas_chain* chain;
    uint16_t lru;
    /*
     * Wire COUNT: nonzero prevents the page being discarded (and being freed
     * via FreeMemory - that asserts). A count rather than a flag so
     * independent users can wire the same frame. Protected by 'lock'.
     * Doesn't fault anything in; see WireVirt() for that.
     */
    uint16_t wired;
    struct spinlock lock;
    uint8_t allocated : 1;    
    uint8_t exists : 1;     /* there's actually RAM here */
    uint8_t dirty  : 1;     /* synced in critical section on eviction */
};


#define MAPPINGS_PER_LEVEL  128

struct vas {
    struct obj_header hdr;
    void* arch_data;
    struct virt_page**** mappings;
    struct mutex* lock;
    struct virt_arena* va;
};

#define VP_WRITE    1
#define VP_USER     2
#define VP_EXEC     4

void InitVmm(void);

struct vas* CreateVas(void);
void CreateInitialVas(void);

void* AllocAnonMemory(size_t bytes, int flags);
void* AllocFixedMemory(size_t phys, size_t bytes, int flags);
/* 'loader' may be NULL (plain demand-load from the file); it is copied. */
void* AllocFileMemory(struct file* file, size_t file_offset, size_t bytes,
                      int flags, const struct virt_loader* loader);
void FreeMemory(void* ptr, size_t bytes);

/*
 * Faults in every page overlapping [ptr, ptr + bytes) in the current VAS and
 * pins them in physical memory, so DiscardPage() can't take them, until the
 * matching UnwireVirt(). Calls nest (it's a count). Returns 0, or an errno
 * (and leaves nothing wired) if part of the range isn't mapped.
 *
 * May block and take page faults, so call it with no spinlocks held. Wire
 * only what you need and unwire promptly: wired pages are unreclaimable.
 * Pass UnwireVirt() exactly the range given to WireVirt().
 */
int  WireVirt(void* ptr, size_t bytes);
void UnwireVirt(void* ptr, size_t bytes);

/* Helpers for use inside custom loaders. */
int   VmmDefaultReload(size_t frame, struct virt_loader* vr, struct page_origin* po);
void* VmmMapFrame(size_t frame);        /* temporary kernel mapping, NULL on failure */
void  VmmUnmapFrame(void* virt);

void HandlePageFault(size_t virt, int reason);
struct vas* GetKernelVas(void);
struct vas* GetCurrentVas(void); 

void ZeroPhysPage(size_t phys);

struct phys_page* DiscardPage(void);

void MarkPageableSegmentsDiscardable(void);


void* AllocVmmHeap(size_t bytes);
void FreeVmmHeap(void* ptr);