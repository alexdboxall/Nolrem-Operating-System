#pragma once

#include <stdbool.h>
#include <objtypes.h>

/*
 * User objects: the handles that usermode holds onto (windows, brushes, ...).
 *
 * A handle is NOT a pointer to the object. It is a pointer to a small wrapper object (an OBJ_USER_REF, see
 * obj.h) that the kernel keeps for the purpose, which holds the real object and the user reference count. So
 * handles are opaque: only ever pass them back to the kernel. Two handles can be to the same object, and the
 * same object can be given out as a new handle more than once (GetGraphicsObject() does), so compare objects,
 * not handles.
 *
 * Ref() and Deref() work on any handle. The kernel keeps the object alive for as long as there is a handle with
 * a user reference to it, and for as long as it is using it for you.
 *
 * GetObjectType() says what kind of object a handle is to, as one of the OBJ_ types (objtypes.h): the type of
 * the object under it, not of the wrapper. A bad handle, or one that has been closed, is OBJ_INVALID.
 */

typedef void* any_t;

/* Object Functions (implemented by the kernel, in syscall/ref.c and syscall/gettype.c) */
void Ref(any_t);
void Deref(any_t);

int GetObjectType(any_t handle);
bool IsAnything(any_t handle);

#define IsSemaphore(handle)       (GetObjectType(handle) == OBJ_SEM)
#define IsMutex(handle)           IsSemaphore(handle)     /* a mutex is a semaphore (see usersync.h) */
#define IsRwLock(handle)          (GetObjectType(handle) == OBJ_RWLOCK)
#define IsThread(handle)          (GetObjectType(handle) == OBJ_THREAD)
#define IsProcess(handle)         (GetObjectType(handle) == OBJ_PROCESS)
#define IsFile(handle)            (GetObjectType(handle) == OBJ_FILE)
#define IsDirectory(handle)       (GetObjectType(handle) == OBJ_DIRECTORY)
#define IsTimer(handle)           (GetObjectType(handle) == OBJ_TIMER)
#define IsMemoryRegion(handle)    (GetObjectType(handle) == OBJ_MEMORY_REGION)
#define IsSound(handle)           (GetObjectType(handle) == OBJ_SOUND)
#define IsDynamicLibrary(handle)  (GetObjectType(handle) == OBJ_MODULE)
#define IsWindow(handle)          (GetObjectType(handle) == OBJ_WINDOW)
#define IsWindowClass(handle)     (GetObjectType(handle) == OBJ_WINDOW_CLASS)
#define IsBrush(handle)           (GetObjectType(handle) == OBJ_BRUSH)
#define IsPen(handle)             (GetObjectType(handle) == OBJ_PEN)
#define IsBitmap(handle)          (GetObjectType(handle) == OBJ_BITMAP)
#define IsTypeface(handle)        (GetObjectType(handle) == OBJ_TYPEFACE)
#define IsFont(handle)            (GetObjectType(handle) == OBJ_FONT)
#define IsMailbox(handle)         (GetObjectType(handle) == OBJ_MSGBOX)
#define IsRegion(handle)          (GetObjectType(handle) == OBJ_REGION)
#define IsDc(handle)              (GetObjectType(handle) == OBJ_DC)
#define IsCursor(handle)          (GetObjectType(handle) == OBJ_CURSOR)
#define IsApp(handle)             (GetObjectType(handle) == OBJ_APP)
#define IsIcon(handle)            (GetObjectType(handle) == OBJ_ICON)
