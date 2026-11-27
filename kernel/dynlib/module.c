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

static void KeCleanupModule(void* _mod) {
    struct module* mod = _mod;
    KeDestroySymbolList(mod->sym_list);
    /* name[] is inside the module's own allocation, so there is nothing separate to free for it. */
    FreeHeap(mod->path);
    FreeHeap(mod);
}

void KeInitModule(void) {
    KeRegisterObjectType(OBJ_MODULE, KeCleanupModule);  
    KeInitSpinlock(&modlock); 
}

static struct symbol_list* KeCreateSymbolListFromSymFile(struct file* f) {
    void* initial = KeAllocFileMemory(f, 0, PAGE_SIZE, 0, 0);
    if (initial == NULL) {
        return NULL;
    }
    void* mem = initial;
    int total_symbols = ReadUnaligned16AndAdvance(&mem);
    size_t address = ReadUnalignedSizeT(mem);
    KeFreeMemory(initial, PAGE_SIZE);

    struct symbol_list* symlist = KeCreateSymbolList(total_symbols);
    if (symlist == NULL) {
        return NULL;
    }
    size_t file_size = 2 + sizeof(size_t) + total_symbols * 10;
    
    void* og_mem = KeAllocFileMemory(f, 0, file_size, 0, 0);
    if (og_mem == NULL) {
        KeDestroySymbolList(symlist);
        return NULL;
    }
    mem = AddVoidPtr(og_mem, 2 + sizeof(size_t));

    for (int i = 0; i < total_symbols; ++i) {
        uint64_t hash = ReadUnaligned64AndAdvance(&mem);
        uint16_t offset = ReadUnaligned16AndAdvance(&mem);

        address += offset;
        KeAddToSymbolListPreHashed(symlist, i, hash, address);

        KeLogPrintf("Added symbol: addr 0x%X, hash_low = 0x%X\n", address, (uint32_t) hash);
    }
    KeFreeMemory(og_mem, file_size);
    return symlist;
}

struct module* KeLoadKernelModule(void) {
    uint16_t maj_ver = 1;
    uint16_t min_ver = 0;

    struct module* mod = KeAllocPermanentHeap(sizeof(struct module) + strlen("KERNEL") + 1);
    if (mod == NULL) {
        KePanic(PANIC_OUT_OF_MEMORY);       /* boot time: nothing can be linked without the kernel's symbols */
    }
    KeInitObject(mod, OBJ_MODULE);

    strcpy(mod->name, "KERNEL");
    mod->major_ver = maj_ver;
    mod->minor_ver = min_ver;
    mod->path = "";

    struct file* file;
    int res = KeOpenFile("drv0:/SYSTEM/KERNEL.SYM", O_RDONLY, 0, &file);
    if (res != 0) {
        KePanic(PANIC_BAD_KERNEL);
    }
    mod->sym_list = KeCreateSymbolListFromSymFile(file);
    KeDerefObject(file);
    if (mod->sym_list == NULL) {
        KePanic(PANIC_BAD_KERNEL);
    }

    KeAcquireSpinlock(&modlock);
    mod->next = module_list_head;
    module_list_head = mod;
    KeReleaseSpinlock(&modlock);

    return mod;
}

export struct module* KeCreateModule(const char* path) {
    // TODO: load the version
    // TODO: load the name
    struct file* file;
    int res = KeOpenFile(path, O_RDONLY, 0, &file);
    if (res != 0) {
        return NULL;
    }

    char* name = "KERNEL";
    uint16_t maj_ver = 1;
    uint16_t min_ver = 0;

    struct module* mod = AllocHeap(sizeof(struct module) + strlen(name) + 1);
    if (mod == NULL) {
        KeDerefObject(file);
        return NULL;
    }
    char* path_copy = KeStrdup(path);
    if (path_copy == NULL) {
        FreeHeap(mod);
        KeDerefObject(file);
        return NULL;
    }

    /* The mapping ArchLoadSymbols makes keeps the file alive, so we can let go of ours. */
    struct symbol_list* sym_list = ArchLoadSymbols(file, 0, false);
    KeDerefObject(file);
    if (sym_list == NULL) {
        /* Not a usable symbol table. Don't register a module nothing can resolve against. */
        FreeHeap(path_copy);
        FreeHeap(mod);
        return NULL;
    }

    KeInitObject(mod, OBJ_MODULE);

    strcpy(mod->name, name);
    mod->major_ver = maj_ver;
    mod->minor_ver = min_ver;
    mod->path = path_copy;
    mod->sym_list = sym_list;

    /*
     * The list holds a reference of its own: modules are never unloaded, and without
     * it the creator dropping its reference would free a module still in the list.
     */
    KeRefObject(mod);
    KeAcquireSpinlock(&modlock);
    mod->next = module_list_head;
    module_list_head = mod;
    KeReleaseSpinlock(&modlock);

    return mod;
}

export uint16_t KeGetModuleMajorVersion(struct module* mod) {
    return mod->major_ver;
}

export uint16_t KeGetModuleMinorVersion(struct module* mod) {
    return mod->minor_ver;
}

export size_t KeResolveSymbol(const char* sym, const char* mod_name, int ver_mode, uint16_t major, uint16_t minor) {
    KeAcquireSpinlock(&modlock);
    struct module* curr = module_list_head;
    while (curr) {
        if (mod_name == NULL || !strcmp(curr->name, mod_name)) {
            if (curr->sym_list != NULL && KeIsMatchingModuleVersion(curr, ver_mode, major, minor)) {
                struct symbol* s = KeFindSymbol(curr->sym_list, sym);
                if (s != NULL) {
                    KeReleaseSpinlock(&modlock);
                    return s->addr;
                }
            }
        }
        curr = curr->next;
    }

    KeReleaseSpinlock(&modlock);
    return 0;
}

bool KeIsMatchingModuleVersion(struct module* mod, int ver_mode, uint16_t major, uint16_t minor) {
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