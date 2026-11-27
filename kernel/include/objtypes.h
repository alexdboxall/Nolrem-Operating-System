#pragma once

/*
 * Object type IDs.
 *
 * One namespace for every kind of object: the ones the kernel itself uses (semaphores, VASes, ...), the ones
 * that only usermode and a subsystem care about (brushes, windows, ...), and the OBJ_USER_REF that usermode
 * actually holds a handle to (see obj.h). It is what KeGetObjectType() returns, and what GetObjectType()
 * returns to usermode, which sees the type of the object that its handle wraps.
 *
 * This header is plain macros so that usermode can include it too. Add new types to the end of their group,
 * and update OBJ_LAST_TYPE. The type is stored in OBJ_TYPE_BITS bits of the object header.
 *
 * Nothing is zero on purpose: a header that was never initialised, or was zeroed on free, is OBJ_INVALID and
 * can't be mistaken for a real object.
 */

#define OBJ_ANYTYPE             (-1)    /* only for asking for 'any type': never a real type */

#define OBJ_INVALID             0

/* The kernel's own */
#define OBJ_SEM                 1
#define OBJ_VAS                 2
#define OBJ_PAGE_ORIGIN         3
#define OBJ_PAGE_VIRT           4
#define OBJ_THREAD              5
#define OBJ_VNODE               6
#define OBJ_FILE                7
#define OBJ_MODULE              8       /* a dynamic library */
#define OBJ_MSGBOX              9       /* a mailbox */
#define OBJ_WAITCLOT            10
#define OBJ_AIO                 11
#define OBJ_CPZ_INST            12
#define OBJ_RWLOCK              13

/* What usermode holds instead of a pointer to the object itself. Wraps any other type. */
#define OBJ_USER_REF            14

/* Things usermode will be able to hold. Not implemented yet: the IDs are only reserved. */
#define OBJ_MUTEX               15
#define OBJ_PROCESS             16
#define OBJ_DIRECTORY           17
#define OBJ_TIMER               18
#define OBJ_MEMORY_REGION       19
#define OBJ_SOUND               20
#define OBJ_BITMAP              21

/*
 * CLIPDRAW. The kernel doesn't operate on these, only CLIPDRAW does. OBJ_REGION is the reference counted
 * container that usermode holds; the `struct region` inside it is a plain value that is not an object, and
 * is what all the other kernel code works with.
 */
#define OBJ_REGION              22
#define OBJ_DC                  23
#define OBJ_BRUSH               24
#define OBJ_PEN                 25
#define OBJ_CURSOR              26
#define OBJ_ICON                27
#define OBJ_TYPEFACE            28
#define OBJ_FONT                29

/* The window manager */
#define OBJ_WINDOW              30
#define OBJ_WINDOW_CLASS        31
#define OBJ_APP                 32

#define OBJ_LAST_TYPE           OBJ_APP

/* The type is the top bits of the header's word; the reference count is what is left. */
#define OBJ_TYPE_BITS           6
#define OBJ_MAX_TYPES           (1 << OBJ_TYPE_BITS)
