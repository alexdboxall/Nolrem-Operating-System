#pragma once

#include <common.h>
#include <spinlock.h>

struct module;

#define MODVER_ANY                             0   // e.g. "1.0" allowed for request "3.5"
#define MODVER_THIS_OR_HIGHER                  1   // e.g. "4.x", "3.6", etc. allowed for request "3.5"
#define MODVER_EXACT_MAJOR_OR_HIGHER_MINOR     2   // e.g. "3.7" allowed for request "3.5" (but not "4.0")
#define MODVER_EXACT                           3   // e.g. "3.5" for request "3.5"


void InitModule(void);

struct module* CreateModule(const char* path);
struct module* LoadKernelModule(void);

uint16_t GetModuleMajorVersion(struct module* mod);
uint16_t GetModuleMinorVersion(struct module* mod);

bool IsMatchingModuleVersion(struct module* mod, int ver_mode, uint16_t major, uint16_t minor);
size_t ResolveSymbol(const char* sym, const char* mod_name, int ver_mode, uint16_t major, uint16_t minor);

struct symbol_list;
struct symbol {
    uint64_t hash_or_str;
    size_t addr;
} __attribute__((packed));

struct symbol_list* CreateSymbolList(int total_syms);
void AddToSymbolList(struct symbol_list* sl, int index, const char* id, bool use_hash, size_t addr);
void AddToSymbolListPreHashed(struct symbol_list* sl, int index, uint64_t hash, size_t addr);
struct symbol* FindSymbol(struct symbol_list* sl, const char* id);