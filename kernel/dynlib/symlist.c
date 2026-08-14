#include <module.h>
#include <common.h>
#include <string.h>
#include <heap.h>

struct symbol_list {
    struct symbol_list* next;
    struct symbol* sym;
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

static struct symbol* CreateSymbol(const char* id, size_t addr) {
    struct symbol* sym = AllocHeap(sizeof(struct symbol));
    sym->hash_or_str = HashSymbol(id) << 1;
    sym->addr = addr;
    return sym;
}

static void DestroySymbol(struct symbol* sym) {
    if (IS_PTR(sym->hash_or_str)) {
        FreeHeap(GET_PTR(sym->hash_or_str));
    }
    FreeHeap(sym);
}

void DestroySymbolList(struct symbol_list* sl) {
    while (sl) {
        struct symbol_list* next = sl->next;
        DestroySymbol(sl->sym);
        FreeHeap(sl);
        sl = next;
    }
}

// ASSUMES SOME LOCK IS ALREADY HELD!
struct symbol* FindSymbol(struct symbol_list* sl, const char* id) {
    while (sl) {
        if (IsMatchingSymbol(id, sl->sym)) {
            return sl->sym;
        }
        sl = sl->next;
    }
    return NULL;
}

// ASSUMES SOME LOCK IS ALREADY HELD
struct symbol_list* AddSymbol(struct symbol_list* sl, const char* id, size_t addr) {
    struct symbol_list* ent = AllocHeap(sizeof(struct symbol_list));
    ent->next = sl;
    ent->sym = CreateSymbol(id, addr);
    return ent;
}
