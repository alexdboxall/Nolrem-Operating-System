#include <obj.h>
#include <heap.h>
#include <module.h>
#include <common.h>
#include <string.h>
#include <spinlock.h>

struct symbol_list;

struct module {
    struct obj_header hdr;
    char* name;
    char* path;
    uint16_t major_ver;
    uint16_t minor_ver;
    struct module* next;
    struct symbol_list* sym_list;
    struct spinlock sym_list_lock;
};

static struct module* module_list_head;
static struct spinlock modlock;

static void CleanupModule(void* _mod) {
    struct module* mod = _mod;
    DestroySymbolList(mod->sym_list);
    FreeHeap(mod->name);
    FreeHeap(mod->path);
    FreeHeap(mod);
}

void InitModule(void) {
    RegisterObjectType(OBJTYPE_MODULE, CleanupModule);  
    InitSpinlock(&modlock); 
}

export struct module* CreateModule(const char* path) {
    // TODO: load the version
    // TODO: load the name
    char* name = "KERNEL";
    uint16_t maj_ver = 1;
    uint16_t min_ver = 0;

    struct module* mod = AllocHeap(sizeof(struct module));
    InitObject(mod, OBJTYPE_MODULE);

    mod->name = KeStrdup(name);
    mod->major_ver = maj_ver;
    mod->minor_ver = min_ver;
    mod->path = KeStrdup(path);
    mod->sym_list = NULL;
    InitSpinlock(&mod->sym_list_lock);
    
    AcquireSpinlock(&modlock);
    mod->next = module_list_head;
    module_list_head = mod;
    ReleaseSpinlock(&modlock);

    return mod;
}

void AddModuleSymbol(struct module* mod, const char* symbol, size_t addr) {
    AcquireSpinlock(&mod->sym_list_lock);
    mod->sym_list = AddSymbol(mod->sym_list, symbol, addr);
    ReleaseSpinlock(&mod->sym_list_lock);
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
                AcquireSpinlock(&curr->sym_list_lock);
                struct symbol* s = FindSymbol(curr->sym_list, sym);
                ReleaseSpinlock(&curr->sym_list_lock);
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