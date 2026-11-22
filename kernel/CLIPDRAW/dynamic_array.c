#include "api.h"
#include <string.h>
#include <heap.h>
#include <errno.h>
#include <common.h>

#include "dynamic_array.h"

struct dynamic_array CdCreateDynamicArray(int data_size, int initial_allocation) {
    struct dynamic_array da;
    if (initial_allocation < 1) {
        initial_allocation = 1;
    }
    da.allocated = (uint16_t) initial_allocation;
    da.used = 0;
    da.data_size = (uint8_t) data_size;
    da.array = (size_t) AllocHeap(data_size * initial_allocation);
    if (da.array == 0) {
        da.allocated = 0;
    }
    return da;
}

struct dynamic_array CdCopyDynamicArray(struct dynamic_array old) {
    struct dynamic_array da = old;
    da.array = old.array == 0 ? 0 : (size_t) AllocHeap(old.allocated * old.data_size);
    if (da.array == 0) {
        da.allocated = 0;
        da.used = 0;
        return da;
    }
    memcpy(DA_GET_ARRAY(da), DA_GET_ARRAY(old), old.allocated * DA_GET_DATA_SIZE(old));
    return da;
}

void CdFreeDynamicArray(struct dynamic_array da) {
    FreeHeap(DA_GET_ARRAY(da));
}

int CdInsertDynamicArray(struct dynamic_array* da, void* data) {
    if (da->used >= da->allocated) {
        /* The counts are 16 bit, so stop at the most they can say rather than wrapping round to 0. */
        uint32_t new_allocated = da->allocated == 0 ? 8 : (uint32_t) da->allocated * 2;
        if (new_allocated > 0xFFFF) {
            new_allocated = 0xFFFF;
        }
        if (new_allocated <= da->used) {
            return ENOMEM;
        }
        void* grown = ReallocHeap(DA_P_GET_ARRAY(da), new_allocated * da->data_size);
        if (grown == NULL) {
            return ENOMEM;      /* the old block is still there and still ours */
        }
        da->array = (size_t) grown;
        da->allocated = (uint16_t) new_allocated;
    }
    memcpy(AddVoidPtr(DA_P_GET_ARRAY(da), da->data_size * da->used), data, DA_P_GET_DATA_SIZE(da));
    da->used++;
    return 0;
}

void CdSortDynamicArray(struct dynamic_array* da, int (*comparator)(void* d1, void* d2, int n)) {
    if (!da || da->used <= 1) {
        return;
    }

    /* data_size is a uint8_t, so this is always big enough, and sorting can't fail for want of memory. */
    uint8_t temp[256];

    for (int i = 0; i < da->used - 1; ++i) {
        for (int j = 0; j < da->used - i - 1; ++j) {
            int data_size   = DA_P_GET_DATA_SIZE(da);
            void* elem_j    = AddVoidPtr(DA_P_GET_ARRAY(da), data_size * j);
            void* elem_next = AddVoidPtr(DA_P_GET_ARRAY(da), data_size * (j + 1));

            if (comparator(elem_j, elem_next, data_size) > 0) {
                memcpy(temp, elem_j, data_size);
                memcpy(elem_j, elem_next, data_size);
                memcpy(elem_next, temp, data_size);
            }
        }
    }
}

void* CdFindInDynamicArray(struct dynamic_array* da, void* data, int (*comparator)(void* d1, void* d2, int n)) {
    for (int i = 0; i < da->used; ++i) {
        if (!comparator(data, AddVoidPtr(da->array, da->data_size * i), da->data_size)) {
            return AddVoidPtr(da->array, da->data_size * i);
        }
    }
    return NULL;
}
