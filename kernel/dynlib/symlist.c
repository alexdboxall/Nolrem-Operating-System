#include <module.h>
#include <common.h>
#include <string.h>
#include <heap.h>
#include <errno.h>

/* TODO: I think you make it so the kernel itself gets to hash, but other
 * external drivers, etc. use strings. that way we don't get clashes.
 * because if hash is used, we don't have a backup string to compare against, we
 * just assume there can only be one.
 */
struct symbol_list {
    int num;
    struct symbol syms[];
};

#define IS_HASH(sv) (((sv) & 1) == 0)
#define IS_PTR(sv) (((sv) & 1) == 1)
#define GET_PTR(sv) ((char*) (size_t)((sv) & ~1))
#define GET_HASH(sv) (sv >> 1)

static uint64_t KeHashSymbol(const char* id) {
    uint64_t hash = 0xcbf29ce484222325;     // FNV-1a 64-bit offset basis
    const uint64_t prime = 0x100000001b3;   // FNV-1a 64-bit prime
    for (int i = 0; id[i]; ++i) {
        hash ^= (uint8_t) id[i];
        hash *= prime;
    }
    return hash & 0x7FFF'FFFF'FFFF'FFFFULL;
}

static bool KeIsMatchingSymbol(const char* id, struct symbol* sym) {
    if (IS_HASH(sym->hash_or_str)) {
        return KeHashSymbol(id) == GET_HASH(sym->hash_or_str);
    } else {
        return !strcmp(id, GET_PTR(sym->hash_or_str));
    }
}

struct symbol_list* KeCreateSymbolList(int total_syms) {
    struct symbol_list* sl = AllocHeap(sizeof(struct symbol_list) + total_syms * sizeof(struct symbol));
    if (sl == NULL) {
        return NULL;
    }
    sl->num = total_syms;
    /*
     * Zero, not a debug pattern: an unfilled slot must read as a hash (even), never as a
     * string pointer (odd), or KeDestroySymbolList() would free garbage.
     */
    memset(sl->syms, 0, total_syms * sizeof(struct symbol));
    return sl;
}

/* Frees the list and any names it copied (those are the entries tagged as pointers). */
void KeDestroySymbolList(struct symbol_list* sl) {
    if (sl == NULL) {
        return;
    }
    for (int i = 0; i < sl->num; ++i) {
        if (IS_PTR(sl->syms[i].hash_or_str)) {
            FreeHeap(GET_PTR(sl->syms[i].hash_or_str));
        }
    }
    FreeHeap(sl);
}

/*
 * Must be 64 bits wide: a size_t is only 32 on x86, which would cut the hash
 * short so that it never matches in KeIsMatchingSymbol. A pointer is tagged
 * with bit 0 set (heap strings are never odd-aligned), which is how
 * IS_PTR/IS_HASH tell the two apart; untagged, it would be read as a hash.
 */
static int KeMakeSymbol(const char* id, bool use_hash, uint64_t* out) {
    if (use_hash) {
        *out = KeHashSymbol(id) << 1;
        return 0;
    }
    char* copy = KeStrdup(id);
    if (copy == NULL) {
        return ENOMEM;
    }
    *out = ((uint64_t) (size_t) copy) | 1;
    return 0;
}

void KeAddToSymbolListPreHashed(struct symbol_list* sl, int index, uint64_t hash, size_t addr) {
    sl->syms[index].addr = addr;
    sl->syms[index].hash_or_str = hash << 1;
}

/* ENOMEM if the name couldn't be copied (then the slot is left empty). */
int KeAddToSymbolList(struct symbol_list* sl, int index, const char* id, bool use_hash, size_t addr) {
    uint64_t value;
    int res = KeMakeSymbol(id, use_hash, &value);
    if (res != 0) {
        return res;
    }
    sl->syms[index].addr = addr;
    sl->syms[index].hash_or_str = value;
    return 0;
}

// ASSUMES SOME LOCK IS ALREADY HELD!
struct symbol* KeFindSymbol(struct symbol_list* sl, const char* id) {
    for (int i = 0; i < sl->num; ++i) {
        if (KeIsMatchingSymbol(id, &sl->syms[i])) {
            return &sl->syms[i];
        }
    }
    return NULL;
}
