#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <common.h>

#include <uobj.h>

/*
 * The window manager's user mode calls (the implementations are in syscall/, and the numbers in syscall.h).
 *
 * Windows and window classes are objects, and usermode holds handles to them (see uobj.h), not the objects.
 * Like every handle, one is closed with Deref(). Each call that hands back a handle makes a new one, even if
 * it is for a window that the caller already has a handle to; the two are interchangeable, and both have to be
 * closed. Closing a handle does not destroy the window: there are no calls to destroy windows yet.
 *
 * There is no application argument anywhere. The calls work for the calling process's own application, which
 * the window manager finds from the process itself. What a process can do to a window depends on whether the
 * window is its own (it was made by this process) or not:
 *
 *      anyone's      finding a window and asking things about it: GetDesktopWindow(), GetWindowRect(), ...
 *      only your own changing anything about it: moving it, raising it, invalidating it, creating children
 *                    in it. Trying it on someone else's gives EACCES, and the desktop is nobody's.
 *
 * Calls that can fail set the thread's last error (see GetLastError()) and return 0 (or NULL, or false), unless
 * they are documented as returning the error themselves (the ones that return an int, where 0 is success).
 */

struct window_handle;
struct window_class_handle;

typedef struct window_handle* window_t;
typedef struct window_class_handle* window_class_t;

/* The objects themselves, for the kernel's side of this. */
struct window;
struct window_class;

/*
 * Window styles. These are for AdjustWindowRectForClass(); a window's style
 * can't be changed yet, and every window starts with none of them.
 */
#define WS_MAXIMISED                    (1 << 0)
#define WS_HIDDEN                       (1 << 1)

/* The modes of AdjustWindowRect() and AdjustWindowRectForClass(). */
#define ADJUST_CLIENT_TO_OUTER          (-1)    /* rect is a client area; makes the window that has it */
#define ADJUST_OUTER_TO_CLIENT          ( 1)    /* rect is a window; makes its client area */

/*
 * What GetRelatedWindow() finds. The children of a window are in order from the front to the back, and
 * the toplevel windows are the children of the desktop.
 */
#define WINDOW_PARENT                   0
#define WINDOW_FIRST_CHILD              1       /* the frontmost child */
#define WINDOW_NEXT_SIBLING             2       /* the next one back; the one behind it in the order */

/*
 * Every coordinate and size that the window manager is given has to be in this range: x and y from
 * -WINDOW_MAX_COORD to WINDOW_MAX_COORD, and the width and height from 0 to WINDOW_MAX_COORD. (Windows are
 * positioned relative to their parent, so the screen position is a sum of several of these; this keeps it
 * from overflowing.) Anything outside it is EINVAL.
 */
#define WINDOW_MAX_COORD                4000


/* Creating windows */

/*
 * Makes a window of the given class (or the default class, if `cls` is NULL), whose position and size is
 * `rect`, relative to `parent`. A NULL parent means the desktop, which makes a toplevel window (and puts it in the
 * foreground). `rect` is the whole window, borders and title bar included; use AdjustWindowRectForClass() to get
 * it from the size that is wanted for the client area.
 *
 * The parent has to be the desktop or one of the caller's own windows, and the window can't be nested more than
 * six deep. The window is sent a WM_CREATE message before this returns.
 *
 * System classes (the ones that have names starting with a '.', except for the default class) are for the
 * window manager's own windows, and give EACCES. Windows can't be destroyed yet, so every call that works leaves
 * one behind for good.
 *
 * Returns NULL on failure.
 */
window_t CreateWindow(window_t parent, window_class_t cls, struct rect rect);

window_t GetDesktopWindow(void);

/* The window that is in the foreground. NULL, with ENOENT, if there isn't one. */
window_t GetForegroundWindow(void);

/* Brings `win` in front of all of the windows that are children of the same parent as it. Returns 0 or an error. */
int RaiseWindowToTop(window_t win);


/* Position and size */

/* The position of the window in its parent (or on the screen, if it is a toplevel window). */
struct rect GetWindowRect(window_t win);

/*
 * The client area (the part of the window inside the borders and the title bar), in the window's own coordinates:
 * relative to the top left of the window, which is also what its children's positions are relative to.
 */
struct rect GetWindowClientRect(window_t win);

/* The position of the window on the screen. */
struct rect GetWindowGlobalRect(window_t win);

/*
 * Moves and resizes a window. `rect` is in the window's parent's coordinates, and is the whole window, not the
 * client area. Returns 0 or an error. Only for your own windows.
 */
int SetWindowRect(window_t win, struct rect rect);

/*
 * Changes `*rect` from a client area to the window that has it (ADJUST_CLIENT_TO_OUTER) or the other way
 * (ADJUST_OUTER_TO_CLIENT). The first form is for an existing window, and the second is for one that is yet to
 * be made, from the class that it will have and the style it will have (which is 0, to begin with). Both
 * return 0 or an error, and don't change `*rect` if they fail.
 */
int AdjustWindowRect(struct rect* rect, window_t win, int mode);
int AdjustWindowRectForClass(struct rect* rect, window_class_t cls, int style, int mode);


/* The window tree */

/*
 * Finds a window that is related to this one (the WINDOW_* values above). Any window can be asked, not just the
 * caller's own. Returns NULL with the error ENOENT if there is no such window (there is no parent of the desktop,
 * no child of a window that has none, and nothing behind the last child), which isn't a failure as such.
 */
window_t GetRelatedWindow(window_t win, int relation);

/* True if `ancestor` is a parent, grandparent, ... of `win`. A window is not its own ancestor. */
bool IsWindowAncestor(window_t ancestor, window_t win);

/*
 * The toplevel window that has the point (in screen coordinates), or the deepest one, which is the one at the
 * very front there, whatever it is nested in. The first is NULL (with ENOENT) if there is none, and the second
 * is the desktop if there is nothing else.
 */
window_t GetToplevelWindowAtPoint(int x, int y);
window_t GetDeepestWindowAtPoint(int x, int y);

/* Where the mouse is, in screen coordinates. */
struct point GetMousePosition(void);


/* Painting */

/*
 * Marks the whole window, or the given part of it, as needing to be drawn again. `rect` is in the window's own
 * coordinates (like GetWindowClientRect()). Only for your own windows. Returns 0 or an error.
 */
int InvalidateWindow(window_t win);
int InvalidateWindowRect(window_t win, struct rect rect);


/* Window classes */

/* NULL for the name means the default class. ENOENT if there is no class with that name. */
window_class_t OpenWindowClass(const char* name);

window_class_t GetWindowClass(window_t win);

/*
 * Copies the name of the class (and the null terminator) into `buffer`, which is `size` bytes long. If it doesn't
 * all fit then the start of the name is copied, and it is null terminated all the same, and ERANGE is
 * returned. Returns 0 or an error.
 */
int GetWindowClassName(window_class_t cls, char* buffer, size_t size);
