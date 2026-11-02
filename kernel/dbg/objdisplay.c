#include <common.h>
#include <obj.h>
#include <log.h>

static const char* objtype_strings[] = {
    [OBJTYPE_SEM] = "Sem",
    [OBJTYPE_VAS] = "VAS",
    [OBJTYPE_PAGE_ORIGIN] = "Page Origin",
    [OBJTYPE_PAGE_VIRT] = "Virt Page",
    [OBJTYPE_THREAD] = "Thread",
    [OBJTYPE_VNODE] = "vnode",
    [OBJTYPE_FILE] = "File",
    [OBJTYPE_MODULE] = "Module",
    [OBJTYPE_USEROBJ] = "USEROBJ",
    [OBJTYPE_MSGBOX] = "Msgbox",
    [OBJTYPE_WAITCLOT] = "Wait Clot",
    [OBJTYPE_AIO] = "Async IO"
};

void DbgPrintObject(void* _o) {
    struct obj_header* hdr = _o;
    uint8_t type = KeGetObjectType(hdr);
    uint32_t ref = KeGetRefCount(hdr);
    bool valid_sig = true;
    if (type == OBJTYPE_USEROBJ) {

    } else {
        const char* type_str = type < sizeof(objtype_strings) / sizeof(objtype_strings[0]) ? objtype_strings[type] : "INVALID TYPE";
        LogPrintf("%s @ 0x%X - %s (0x%X)\n    Refcnt: %d\n", valid_sig ? "Object" : "Object (bad sig.)", hdr, type_str, type, ref);
    }
}
