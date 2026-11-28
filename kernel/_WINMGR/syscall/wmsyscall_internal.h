#pragma once

#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include <errno.h>
#include "../api.h"
#include "../winmgr_internal.h"

/*
 * What the window manager's system calls have in common. All of these set the last error when they fail
 * (and say so below), so the calls themselves only have to return.
 */

/*
 * The application of the calling process. NULL (with EACCES) if it can't use windows. The pointer is
 * borrowed, as WmGetAppObjForProcess() is described in winmgr_internal.h.
 */
struct app* WmSysGetApp(void);

/*
 * A window or class from a handle that the user passed. They come back with a reference, which the caller
 * has to give back with KeDerefObject(). NULL (with EINVAL) if the handle isn't one for that kind of object.
 *
 * All of the ones that look up a handle take a place to put the error as well (it can be NULL), for the calls
 * that return the error rather than 0 or NULL.
 */
struct window* WmSysWindow(size_t handle, int* err);
struct window_class* WmSysClass(size_t handle, int* err);

/*
 * The same, and in addition the window has to be one that belongs to the calling process's application:
 * NULL with EACCES if it is anyone else's (that includes the desktop), and with EINVAL as above.
 */
struct window* WmSysOwnWindow(size_t handle, int* err);

/*
 * Give the user a handle for a window or class. The object must carry a reference for the handle, which is
 * taken over (and given back if the handle can't be made, which is ENOMEM). So when an object is only borrowed,
 * add a reference first.
 */
size_t WmSysWindowHandle(struct window* win);
size_t WmSysClassHandle(struct window_class* wc);

/* Adds a reference to a window that was found in the tree (with the window manager's lock held, or the desktop). NULL gives NULL. */
struct window* WmSysRefWindow(struct window* win);

/* Whether the rectangle is within the limits of WINDOW_MAX_COORD. Sets EINVAL if not. */
bool WmSysCheckRect(struct rect r);

/* Copy a rectangle in from or out to the user. In sets EINVAL if it is out of the limits. Both return 0 or the error. */
int WmSysReadRect(size_t user_ptr, struct rect* out);
int WmSysWriteRect(size_t user_ptr, struct rect rect);

/* Whether a class may be used by user mode to make windows. Sets EACCES if not. */
bool WmSysClassIsUsable(struct window_class* wc);

/* For the calls that give back an error: sets it as the last error, and returns it. */
size_t WmSysFail(int err);
