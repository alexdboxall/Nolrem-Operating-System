#pragma once

#define DA_GET_ARRAY(da) ((void*) ((da).array))
#define DA_P_GET_ARRAY(da) ((void*) ((da)->array))

#define DA_GET_DATA_SIZE(da) ((int)((da).data_size))
#define DA_P_GET_DATA_SIZE(da) ((int)((da)->data_size))

struct __attribute__((packed)) dynamic_array {
    size_t array; 
    
    // these are in 'number of items' not 'number of bytes'
    uint16_t allocated;
    uint16_t used;
    
    // number of bytes per element
    uint8_t data_size;
};

struct dynamic_array CdCreateDynamicArray(int data_size, int initial_allocation);
struct dynamic_array CdCopyDynamicArray(struct dynamic_array old);
void CdFreeDynamicArray(struct dynamic_array da);
void CdInsertDynamicArray(struct dynamic_array* da, void* data);
void CdSortDynamicArray(struct dynamic_array* da, int (*comparator)(void* d1, void* d2, int n));
void* CdFindInDynamicArray(struct dynamic_array* da, void* data, int (*comparator)(void* d1, void* d2, int n));
