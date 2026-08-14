#include "api.h"
#include <string.h>
#include <heap.h>
#include <common.h>

#include "dynamic_array.h"

struct dynamic_array CreateDynamicArray(int data_size, int initial_allocation) {
    struct dynamic_array da;
    if (initial_allocation < 1) {
        initial_allocation = 1;
    }
    da.allocated = (uint16_t) initial_allocation;
    da.used = 0;
    da.data_size = (uint8_t) data_size;
    da.array = (size_t) AllocHeap(data_size * initial_allocation);
    return da;
}

struct dynamic_array CopyDynamicArray(struct dynamic_array old) {
    struct dynamic_array da = old;
    da.array = (size_t) AllocHeap(old.allocated);
    memcpy(DA_GET_ARRAY(da), DA_GET_ARRAY(old), old.allocated * DA_GET_DATA_SIZE(old));
    return da;
}

void FreeDynamicArray(struct dynamic_array da) {
    FreeHeap(DA_GET_ARRAY(da));
}

void InsertDynamicArray(struct dynamic_array* da, void* data) {
    while (da->used >= da->allocated) {
        da->allocated = da->allocated * 2;
        da->array = (size_t) ReallocHeap(DA_P_GET_ARRAY(da), da->allocated * da->data_size);
    }
    memcpy(AddVoidPtr(DA_P_GET_ARRAY(da), da->data_size * da->used), data, DA_P_GET_DATA_SIZE(da));
    da->used++;
}

void SortDynamicArray(struct dynamic_array* da, int (*comparator)(void* d1, void* d2, int n)) {
    if (!da || da->used <= 1) {
        return;
    }

    uint8_t* temp = AllocHeap(da->data_size);

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

    FreeHeap(temp);
}

void* FindInDynamicArray(struct dynamic_array* da, void* data, int (*comparator)(void* d1, void* d2, int n)) {
    for (int i = 0; i < da->used; ++i) {
        if (!comparator(data, AddVoidPtr(da->array, da->data_size * i), da->data_size)) {
            return AddVoidPtr(da->array, da->data_size * i);
        }
    }
    return NULL;
}
