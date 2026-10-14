#include <module.h>
#include <common.h>
#include <string.h>
#include <heap.h>

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

static uint64_t HashSymbol(const char* id) {
    uint64_t hash = 0xcbf29ce484222325;     // FNV-1a 64-bit offset basis
    const uint64_t prime = 0x100000001b3;   // FNV-1a 64-bit prime
    for (int i = 0; id[i]; ++i) {
        hash ^= (uint8_t) id[i];
        hash *= prime;
    }
    return hash & 0x7FFF'FFFF'FFFF'FFFFULL;
}

static bool IsMatchingSymbol(const char* id, struct symbol* sym) {
    if (IS_HASH(sym->hash_or_str)) {
        return HashSymbol(id) == GET_HASH(sym->hash_or_str);
    } else {
        return !strcmp(id, GET_PTR(sym->hash_or_str));
    }
}

struct symbol_list* CreateSymbolList(int total_syms) {
    struct symbol_list* sl = AllocHeap(sizeof(struct symbol_list) + total_syms * sizeof(struct symbol));
    sl->num = total_syms;
    memset(sl->syms, 0x77, total_syms * sizeof(struct symbol));
    return sl;
}

static size_t MakeSymbol(const char* id, bool use_hash) {
    if (use_hash) {
        return HashSymbol(id) << 1;
    } else {
        return (size_t) KeStrdup(id);
    }
}

void AddToSymbolList(struct symbol_list* sl, int index, const char* id, bool use_hash, size_t addr) {
    sl->syms[index].addr = addr;
    sl->syms[index].hash_or_str = MakeSymbol(id, use_hash);
}

// ASSUMES SOME LOCK IS ALREADY HELD!
struct symbol* FindSymbol(struct symbol_list* sl, const char* id) {
    for (int i = 0; i < sl->num; ++i) {
        if (IsMatchingSymbol(id, &sl->syms[i])) {
            return &sl->syms[i];
        }
    }
    return NULL;
}
