#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <spinlock.h>

#define OBJTYPE_SEM             0
#define OBJTYPE_VAS             1
#define OBJTYPE_PAGE_ORIGIN     2
#define OBJTYPE_PAGE_VIRT       3
#define OBJTYPE_THREAD          4
#define OBJTYPE_VNODE           5
#define OBJTYPE_FILE            6
#define OBJTYPE_MODULE          7
#define OBJTYPE_USEROBJ         8
#define OBJTYPE_MSGBOX          9

struct obj_header {
    _Atomic uint8_t objtype;
    _Atomic uint16_t ref_count;
};

struct user_obj_header {
    struct obj_header hdr;
    struct spinlock lock;
    uint8_t user_ref_count;
    uint8_t user_gone       : 1;
    uint8_t user_type       : 7;
};

void RegisterObjectType(uint8_t type, void(*cleanup_func)(void*));
void InitObject(void* obj, uint8_t type);
void RefObject(void* obj);
void DerefObject(void* obj);



void RegisterUserObjectType(uint8_t type, void(*cleanup_func)(void*));
void InitUserObjectType(void);
void InitUserObject(void* obj, uint8_t user_type);
void LockUserObject(void* obj);
void UnlockUserObject(void* obj);

void UserRef(void* obj);
void UserDeref(void* obj);