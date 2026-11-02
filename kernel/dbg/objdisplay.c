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

static const char* uobjtype_strings[] = {

};

void DbgPrintObject(void* _o) {
    struct obj_header* hdr = _o;
    uint8_t type = KeGetObjectType(hdr);
    uint32_t ref = KeGetRefCount(hdr);
    bool valid_sig = true;
    if (type == OBJTYPE_USEROBJ) {
        struct user_obj_header* uo = _o;
        uint32_t userref = uo->user_ref_count;
        bool gone = uo->user_gone;
        uint8_t utype = uo->user_type;
        const char* utype_str = utype < sizeof(uobjtype_strings) / sizeof(uobjtype_strings[0]) ? uobjtype_strings[type] : "INVALID USERTYPE";
        LogPrintf("%s %s@ 0x%X - %s (0x%X)\n    Refcnt: %d\n    Usrref: %d\n", valid_sig ? "User object" : "User object (bad sig.)", gone ? "(GONE) " : "", hdr, utype_str, utype, ref, userref);

    } else {
        const char* type_str = type < sizeof(objtype_strings) / sizeof(objtype_strings[0]) ? objtype_strings[type] : "INVALID TYPE";
        LogPrintf("%s @ 0x%X - %s (0x%X)\n    Refcnt: %d\n", valid_sig ? "Object" : "Object (bad sig.)", hdr, type_str, type, ref);
    }
}
