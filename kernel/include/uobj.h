#pragma once

#include <stdbool.h>

/*
 * User objects: the handles that usermode holds onto (windows, brushes, ...).
 *
 * The kernel owns this header because it owns the machinery behind it: the
 * validation of handles that come in from usermode, the user reference counts,
 * and the Ref()/Deref()/GetObjectType() calls that work on any handle. See
 * obj.h for the object header, and KeRegisterUserObjectType() for how a
 * subsystem (CLIPDRAW, the window manager, ...) attaches its own cleanup code
 * to a type.
 *
 * The type IDs below are a single namespace shared by everything that creates
 * user objects, so a new type has to be added here. There is room for 128
 * (the id is stored in 7 bits), and KeRegisterUserObjectType() currently
 * handles the first 32.
 */

#define UOBJ_ANYTYPE            (-1)

#define UOBJ_INVALID             0
#define UOBJ_MUTEX               1
#define UOBJ_THREAD              2
#define UOBJ_PROCESS             3
#define UOBJ_FILE                4
#define UOBJ_DIRECTORY           5
#define UOBJ_TIMER               6
#define UOBJ_MEMORY_REGION       7
#define UOBJ_SOUND               8
#define UOBJ_DYNAMIC_LIBRARY     9
#define UOBJ_WINDOW              10
#define UOBJ_WINDOW_CLASS        11
#define UOBJ_BRUSH               12
#define UOBJ_PEN                 13
#define UOBJ_BITMAP              14
#define UOBJ_TYPEFACE            15
#define UOBJ_FONT                16
#define UOBJ_MAILBOX             17
#define UOBJ_REGION              18
#define UOBJ_DC                  19
#define UOBJ_CURSOR              20
#define UOBJ_APP                 21
#define UOBJ_ICON                22

typedef void* any_t;

/* Object Functions (implemented by the kernel, in syscall/ref.c and syscall/gettype.c) */
void Ref(any_t);
void Deref(any_t);

int GetObjectType(any_t handle);
bool IsAnything(any_t handle);

#define IsMutex(handle)           (GetObjectType(handle) == UOBJ_MUTEX)
#define IsThread(handle)          (GetObjectType(handle) == UOBJ_THREAD)
#define IsProcess(handle)         (GetObjectType(handle) == UOBJ_PROCESS)
#define IsFile(handle)            (GetObjectType(handle) == UOBJ_FILE)
#define IsDirectory(handle)       (GetObjectType(handle) == UOBJ_DIRECTORY)
#define IsTimer(handle)           (GetObjectType(handle) == UOBJ_TIMER)
#define IsMemoryRegion(handle)    (GetObjectType(handle) == UOBJ_MEMORY_REGION)
#define IsSound(handle)           (GetObjectType(handle) == UOBJ_SOUND)
#define IsDynamicLibrary(handle)  (GetObjectType(handle) == UOBJ_DYNAMIC_LIBRARY)
#define IsWindow(handle)          (GetObjectType(handle) == UOBJ_WINDOW)
#define IsWindowClass(handle)     (GetObjectType(handle) == UOBJ_WINDOW_CLASS)
#define IsBrush(handle)           (GetObjectType(handle) == UOBJ_BRUSH)
#define IsPen(handle)             (GetObjectType(handle) == UOBJ_PEN)
#define IsBitmap(handle)          (GetObjectType(handle) == UOBJ_BITMAP)
#define IsTypeface(handle)        (GetObjectType(handle) == UOBJ_TYPEFACE)
#define IsFont(handle)            (GetObjectType(handle) == UOBJ_FONT)
#define IsMailbox(handle)         (GetObjectType(handle) == UOBJ_MAILBOX)
#define IsRegion(handle)          (GetObjectType(handle) == UOBJ_REGION)
#define IsDc(handle)              (GetObjectType(handle) == UOBJ_DC)
#define IsCursor(handle)          (GetObjectType(handle) == UOBJ_CURSOR)
#define IsApp(handle)             (GetObjectType(handle) == UOBJ_APP)
#define IsIcon(handle)            (GetObjectType(handle) == UOBJ_ICON)

/*
 * Checks that `obj` is a live user object of the given type (or of any type, if
 * UOBJ_ANYTYPE), and takes a kernel reference on it if so. Returns false if it
 * isn't. On success the caller must KeDerefObject() it when done.
 *
 * This is the first thing every syscall that takes a handle does.
 */
bool KeValidateUserObjectAndAtomicallyRef(void* obj, int type);
