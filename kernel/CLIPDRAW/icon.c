#include <heap.h>
#include <common.h>
#include <errno.h>
#include <omf.h>
#include "api.h"
#include "clipdraw_internal.h"

struct icon {
    struct user_obj_header hdr;
    uint8_t num_icons;
    uint8_t shift;
    uint16_t* ico_offsets_bitmap;
    uint16_t* ico_offsets_mask;
    uint8_t* ico_types;
    struct open_mapped_file omf;
};

export pageable struct icon* CdOpenIcon(const char* file) {
    struct icon* ico = AllocHeap(sizeof(struct icon));
    if (ico == NULL) {
        return NULL;
    }
    InitUserObject(ico, UOBJ_ICON);

    int res = OpenAndMapFile(file, &ico->omf);
    if (res != 0) {
        FreeHeap(ico);
        return NULL;
    }

    /* so goto fail_after_open; valid before AllocHeap */
    ico->ico_offsets_mask = NULL;
    ico->ico_offsets_bitmap = NULL;
    ico->ico_types = NULL;
    
    uint8_t sig1 = OmfRead8AndAdvance(&ico->omf);
    uint8_t sig2 = OmfRead8AndAdvance(&ico->omf);
    uint8_t sig3 = OmfRead8AndAdvance(&ico->omf);
    uint8_t ver  = OmfRead8AndAdvance(&ico->omf);
    if (sig1 != 'I' || sig2 != 'C' || sig3 != 'O' || ver != 0x00) {
        goto fail_after_open;
    }

    ico->num_icons = OmfRead8AndAdvance(&ico->omf);
    ico->shift = OmfRead8AndAdvance(&ico->omf);
    if (ico->omf.error || ico->num_icons == 0) {
        goto fail_after_open;
    }
    
    ico->ico_offsets_mask = AllocHeap(sizeof(uint16_t) * ico->num_icons);
    ico->ico_offsets_bitmap = AllocHeap(sizeof(uint16_t) * ico->num_icons);
    ico->ico_types = AllocHeap(sizeof(uint8_t) * ico->num_icons);

    if (ico->ico_types == NULL || ico->ico_offsets_mask == NULL || ico->ico_offsets_bitmap == NULL) {
        goto fail_after_open;
    }
    OmfJump(&ico->omf, 8);
    for (int i = 0; i < ico->num_icons; ++i) {
        ico->ico_offsets_bitmap[i] = OmfRead16AndAdvanceLE(&ico->omf);
        ico->ico_offsets_mask[i] = OmfRead16AndAdvanceLE(&ico->omf);
        ico->ico_types[i] = OmfRead8AndAdvance(&ico->omf);
    }

    if (ico->omf.error || ico->num_icons == 0) {
        goto fail_after_open;
    }

    return ico;

fail_after_open:
    /* CleanupIcon closes the Omf and frees tf. */
    DerefObject(ico);
    return NULL;
}

static void CleanupIcon(void* _ico) {
    struct icon* ico = _ico;
    if (ico == NULL) {
        return;
    }
    OmfClose(&ico->omf);
    FreeHeap(ico->ico_offsets_bitmap);
    FreeHeap(ico->ico_offsets_mask);
    FreeHeap(ico->ico_types);
    FreeHeap(ico);
}

void CdInitIconSubsystem(void) {
    RegisterUserObjectType(UOBJ_ICON, CleanupIcon);
}
