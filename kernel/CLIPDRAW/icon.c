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
    KeInitUserObject(ico, UOBJ_ICON);

    int res = KeOpenAndMapFile(file, OMF_TRANSPARENT_DECOMPRESS, &ico->omf);
    if (res != 0) {
        FreeHeap(ico);
        return NULL;
    }

    /* so goto fail_after_open; valid before AllocHeap */
    ico->ico_offsets_mask = NULL;
    ico->ico_offsets_bitmap = NULL;
    ico->ico_types = NULL;
    
    uint8_t sig1 = KeOmfRead8AndAdvance(&ico->omf);
    uint8_t sig2 = KeOmfRead8AndAdvance(&ico->omf);
    uint8_t sig3 = KeOmfRead8AndAdvance(&ico->omf);
    uint8_t ver  = KeOmfRead8AndAdvance(&ico->omf);
    if (sig1 != 'I' || sig2 != 'C' || sig3 != 'O' || ver != 0x00) {
        goto fail_after_open;
    }

    ico->num_icons = KeOmfRead8AndAdvance(&ico->omf);
    ico->shift = KeOmfRead8AndAdvance(&ico->omf);
    if (ico->omf.error || ico->num_icons == 0) {
        goto fail_after_open;
    }
    
    ico->ico_offsets_mask = AllocHeap(sizeof(uint16_t) * ico->num_icons);
    ico->ico_offsets_bitmap = AllocHeap(sizeof(uint16_t) * ico->num_icons);
    ico->ico_types = AllocHeap(sizeof(uint8_t) * ico->num_icons);

    if (ico->ico_types == NULL || ico->ico_offsets_mask == NULL || ico->ico_offsets_bitmap == NULL) {
        goto fail_after_open;
    }
    KeOmfJump(&ico->omf, 8);
    for (int i = 0; i < ico->num_icons; ++i) {
        ico->ico_offsets_bitmap[i] = KeOmfRead16AndAdvanceLE(&ico->omf);
        ico->ico_offsets_mask[i] = KeOmfRead16AndAdvanceLE(&ico->omf);
        ico->ico_types[i] = KeOmfRead8AndAdvance(&ico->omf);
    }

    if (ico->omf.error || ico->num_icons == 0) {
        goto fail_after_open;
    }

    return ico;

fail_after_open:
    /* CleanupIcon closes the Omf and frees tf. */
    KeDerefObject(ico);
    return NULL;
}

static void CleanupIcon(void* _ico) {
    struct icon* ico = _ico;
    if (ico == NULL) {
        return;
    }
    KeOmfClose(&ico->omf);
    FreeHeap(ico->ico_offsets_bitmap);
    FreeHeap(ico->ico_offsets_mask);
    FreeHeap(ico->ico_types);
    FreeHeap(ico);
}

void CdInitIconSubsystem(void) {
    KeRegisterUserObjectType(UOBJ_ICON, CleanupIcon);
}
