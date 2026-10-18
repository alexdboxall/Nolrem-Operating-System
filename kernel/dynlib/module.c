#include <obj.h>
#include <heap.h>
#include <module.h>
#include <common.h>
#include <string.h>
#include <arch.h>
#include <vfs.h>
#include <fcntl.h>
#include <spinlock.h>
#include <panic.h>
#include <log.h>
#include <unaligned.h>
#include <vmm.h>

struct symbol_list;

struct module {
    struct obj_header hdr;
    char* path;
    uint16_t major_ver;
    uint16_t minor_ver;
    struct module* next;
    struct symbol_list* sym_list;
    char name[];
};

static struct module* module_list_head;
static struct spinlock modlock;

static void CleanupModule(void* _mod) {
    struct module* mod = _mod;
    FreeHeap(mod->sym_list);
    FreeHeap(mod->name);
    FreeHeap(mod->path);
    FreeHeap(mod);
}

void InitModule(void) {
    RegisterObjectType(OBJTYPE_MODULE, CleanupModule);  
    InitSpinlock(&modlock); 
}

static struct symbol_list* CreateSymbolListFromSymFile(struct file* f) {
    void* initial = AllocFileMemory(f, 0, PAGE_SIZE, 0, 0);
    void* mem = initial;
    int total_symbols = ReadUnaligned16AndAdvance(&mem);
    size_t address = ReadUnalignedSizeT(mem);
    FreeMemory(initial, PAGE_SIZE);

    struct symbol_list* symlist = CreateSymbolList(total_symbols);
    size_t file_size = 2 + sizeof(size_t) + total_symbols * 10;
    
    void* og_mem = AllocFileMemory(f, 0, file_size, 0, 0);
    mem = AddVoidPtr(og_mem, 2 + sizeof(size_t));

    for (int i = 0; i < total_symbols; ++i) {
        uint64_t hash = ReadUnaligned64AndAdvance(&mem);
        uint16_t offset = ReadUnaligned16AndAdvance(&mem);

        address += offset;
        AddToSymbolListPreHashed(symlist, i, hash, address);

        LogPrintf("Added symbol: addr 0x%X, hash_low = 0x%X\n", address, (uint32_t) hash);
    }
    FreeMemory(og_mem, file_size);
    return symlist;
}

struct module* LoadKernelModule(void) {
    uint16_t maj_ver = 1;
    uint16_t min_ver = 0;

    struct module* mod = AllocPermanentHeap(sizeof(struct module) + strlen("KERNEL") + 1);
    InitObject(mod, OBJTYPE_MODULE);

    strcpy(mod->name, "KERNEL");
    mod->major_ver = maj_ver;
    mod->minor_ver = min_ver;
    mod->path = "";

    struct file* file;
    int res = OpenFile("drv0:/SYSTEM/KERNEL.SYM", O_RDONLY, 0, &file);
    if (res != 0) {
        Panic(PANIC_BAD_KERNEL);
    }
    mod->sym_list = CreateSymbolListFromSymFile(file);
    DerefObject(file);

    AcquireSpinlock(&modlock);
    mod->next = module_list_head;
    module_list_head = mod;
    ReleaseSpinlock(&modlock);

    return mod;
}

export struct module* CreateModule(const char* path) {
    // TODO: load the version
    // TODO: load the name
    struct file* file;
    int res = OpenFile(path, O_RDONLY, 0, &file);
    if (res != 0) {
        return NULL;
    }

    char* name = "KERNEL";
    uint16_t maj_ver = 1;
    uint16_t min_ver = 0;

    struct module* mod = AllocHeap(sizeof(struct module) + strlen(name) + 1);
    InitObject(mod, OBJTYPE_MODULE);

    strcpy(mod->name, name);
    mod->major_ver = maj_ver;
    mod->minor_ver = min_ver;
    mod->path = KeStrdup(path);

    mod->sym_list = ArchLoadSymbols(file, 0, false);

    AcquireSpinlock(&modlock);
    mod->next = module_list_head;
    module_list_head = mod;
    ReleaseSpinlock(&modlock);

    return mod;
}

export uint16_t GetModuleMajorVersion(struct module* mod) {
    return mod->major_ver;
}

export uint16_t GetModuleMinorVersion(struct module* mod) {
    return mod->minor_ver;
}

export size_t ResolveSymbol(const char* sym, const char* mod_name, int ver_mode, uint16_t major, uint16_t minor) {
    AcquireSpinlock(&modlock);
    struct module* curr = module_list_head;
    while (curr) {
        if (mod_name == NULL || !strcmp(curr->name, mod_name)) {
            if (IsMatchingModuleVersion(curr, ver_mode, major, minor)) {
                struct symbol* s = FindSymbol(curr->sym_list, sym);
                if (s != NULL) {
                    ReleaseSpinlock(&modlock);
                    return s->addr;
                }
            }
        }
        curr = curr->next;
    }

    ReleaseSpinlock(&modlock);
    return 0;
}

bool IsMatchingModuleVersion(struct module* mod, int ver_mode, uint16_t major, uint16_t minor) {
    uint16_t mod_maj = mod->major_ver;
    uint16_t mod_min = mod->minor_ver;
    if (ver_mode == MODVER_ANY) {
        return true;
    } else if (ver_mode == MODVER_EXACT) {
        return mod_maj == major && mod_min == minor;
    } else if (ver_mode == MODVER_THIS_OR_HIGHER) {
        return mod_maj >= major || (mod_maj == major && mod_min >= minor);
    } else if (ver_mode == MODVER_EXACT_MAJOR_OR_HIGHER_MINOR) {
        return mod_maj == major && mod_min >= minor;
    } else {
        return false;
    }
}