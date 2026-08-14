#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <obj.h>
#include <spinlock.h>

struct file;
struct mutex;

struct page_origin {
    struct obj_header hdr;
    struct file* file;
    size_t rebase_page;   
    size_t file_offset;
    struct mutex* mtx;
    size_t phys;
};

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
    uint8_t busy     : 2;
    uint8_t executable : 1;
};

struct phys_page {
    struct page_origin* origin;
    struct vas* vas;
    struct virt_page* vp;
    struct vas_chain* chain;
    uint16_t lru;
    struct spinlock lock;
    uint8_t allocated : 1;    
    uint8_t exists : 1;     /* there's actually RAM here */
    uint8_t dirty  : 1;     /* synced in critical section on eviction */
    uint8_t excl   : 1;     /* exclusively held in critical section */
    uint8_t wired  : 1;     /* set to prevent swapping (doesn't load in an unload page on 0->1 though)*/
};


void InitVmm(void);