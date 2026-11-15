#pragma once

#include <spinlock.h>

struct locale;
struct uapp;

struct uobj_node {
    void* user_obj;
    struct uobj_node* left;
    struct uobj_node* right;
};

struct userside_process_block {
    void* krnl_process;
    int process_id;
    struct locale* locale;
    struct uapp* app;
    struct uobj_node* open_uobjs;
    struct spinlock lock;
    
};
