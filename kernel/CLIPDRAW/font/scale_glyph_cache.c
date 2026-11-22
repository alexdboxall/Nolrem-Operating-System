#include "font_structures.h"
#include <spinlock.h>
#include <string.h>
#include <heap.h>
#include <phys.h>

#define NUM_CACHED_GLYPHS 64

struct glyph_cache_entry {
    struct font* fnt;
    font_style_id style;
    _Atomic uint8_t in_use;
    uint8_t ascii;
    uint16_t lru;
    uint8_t* data;
};

static _Atomic uint32_t num_cached = 0;
static struct glyph_cache_entry* scaled_font_cache = NULL;
static struct spinlock lock;

export pageable uint8_t* CdGetCacheScaledGlyph(struct font* font, font_style_id style, uint32_t c, int* index_out) {
    if (c < 0x20 || c > 0x7F || index_out == NULL) {
        return NULL;
    }
    KeAcquireSpinlock(&lock);
    if (scaled_font_cache == NULL) {
        /* Nothing has been cached yet (the array is made on the first suggestion). */
        KeReleaseSpinlock(&lock);
        return NULL;
    }
    for (int i = 0; i < NUM_CACHED_GLYPHS; ++i) {
        if (scaled_font_cache[i].fnt == font 
            && scaled_font_cache[i].ascii == c 
            && scaled_font_cache[i].style == style
            && scaled_font_cache[i].data != NULL
        ) {
            scaled_font_cache[i].in_use++;
            scaled_font_cache[i].lru |= 0x8000;
            uint8_t* data = scaled_font_cache[i].data;
            KeReleaseSpinlock(&lock);
            *index_out = i;
            return data;
        }
    }
    KeReleaseSpinlock(&lock);
    return NULL;
}

export pageable void CdReleaseCachedScaledGlyph(int index) {
    KeAcquireSpinlock(&lock);
    if (scaled_font_cache != NULL && index >= 0 && index < NUM_CACHED_GLYPHS && scaled_font_cache[index].in_use > 0) {
        uint8_t remaining = --scaled_font_cache[index].in_use;
        if (remaining == 0 && KeIsMemoryLow()) {
            FreeHeap(scaled_font_cache[index].data);
            scaled_font_cache[index].data = NULL;
            --num_cached;
        }
    }
    if (scaled_font_cache != NULL && num_cached == 0) {
        FreeHeap(scaled_font_cache);
        scaled_font_cache = NULL;
    }
    KeReleaseSpinlock(&lock);
}

/* Not pageable, as we need to be able to purge while we're paging! */
void CdPurgeScaledGlyphCacheIfNeeded(void) {
    if (!KeIsMemoryRunningLow() || num_cached == 0) {
        return;
    }
    bool very_low = KeIsMemoryLow();
    KeAcquireSpinlock(&lock);
    for (int i = 0; i < NUM_CACHED_GLYPHS; ++i) {
        if (scaled_font_cache[i].in_use == 0 && scaled_font_cache[i].data != NULL) {
            if (very_low || scaled_font_cache[i].lru == 0) {
                FreeHeap(scaled_font_cache[i].data);
                scaled_font_cache[i].data = NULL;
                --num_cached;
            }
        }
    }
    if (num_cached == 0) {
        FreeHeap(scaled_font_cache);
        scaled_font_cache = NULL;
    }
    KeReleaseSpinlock(&lock);
}

export pageable void CdSuggestAddScaledGlyphToCache(struct font* font, font_style_id style, uint32_t c, uint8_t* bitmap, size_t size) {
    if (c < 0x20 || c > 0x7F || KeIsMemoryRunningLow()) {
        return;
    }
    uint16_t lowest_lru = 0xFFFF;
    int lowest_index = 0;
    bool found_exact_match = false;
    KeAcquireSpinlock(&lock);
    if (scaled_font_cache == NULL) {
        scaled_font_cache = AllocHeap(sizeof(struct glyph_cache_entry) * NUM_CACHED_GLYPHS);
        if (scaled_font_cache == NULL) {
            KeReleaseSpinlock(&lock);
            return;
        }
        memset(scaled_font_cache, 0, sizeof(struct glyph_cache_entry) * NUM_CACHED_GLYPHS);
    }
    for (int i = 0; i < NUM_CACHED_GLYPHS; ++i) {
        if (scaled_font_cache[i].data != NULL 
            && scaled_font_cache[i].lru < lowest_lru
            && scaled_font_cache[i].in_use == 0)
        {
            lowest_lru = scaled_font_cache[i].lru;
            lowest_index = i;
        }
        scaled_font_cache[i].lru >>= 1;

        if (scaled_font_cache[i].fnt == font 
            && scaled_font_cache[i].ascii == c 
            && scaled_font_cache[i].style == style
            && scaled_font_cache[i].data != NULL
        ) {
            /* Found exact match, but continue looping for LRU update. */
            found_exact_match = true;
            scaled_font_cache[i].lru |= 0x8000;
        }
    }
    if (scaled_font_cache[lowest_index].in_use == 0
        && !found_exact_match) 
    {
        if (scaled_font_cache[lowest_index].data != NULL) {
            FreeHeap(scaled_font_cache[lowest_index].data);
            scaled_font_cache[lowest_index].data = NULL;
            --num_cached;
        }
        uint8_t* copy = AllocHeap(size);
        if (copy == NULL) {
            /* The slot is empty and uncounted, which is what it now is. */
            KeReleaseSpinlock(&lock);
            return;
        }
        ++num_cached;
        scaled_font_cache[lowest_index].data = copy;
        memcpy(copy, bitmap, size);
        scaled_font_cache[lowest_index].lru = 0xFFFF;
        scaled_font_cache[lowest_index].ascii = c; 
        scaled_font_cache[lowest_index].style = style;
        scaled_font_cache[lowest_index].fnt = font;
        /* in_use stays zero, as you're expected to use the original copy */
    }
    KeReleaseSpinlock(&lock);
}

void CdInitScaledGlyphCache(void) {
    KeInitSpinlock(&lock);
}
