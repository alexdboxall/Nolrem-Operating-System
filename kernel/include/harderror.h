#pragma once

/*
 * Hard errors: conditions the kernel cannot continue past without the user's
 * help (e.g. out of memory in the middle of a page fault).
 *
 * The kernel itself has no user interface, so it only owns the hook. Whichever
 * component can put a message in front of the user (the window manager)
 * registers a handler with KeSetHardErrorHandler(). With no handler registered,
 * KeHardError() fails straight away and the caller does whatever it does when
 * recovery is impossible (normally a panic).
 */

/*
 * Called with the errno describing the problem (ENOMEM, EIO, ...). Returns 0 if
 * the problem has been dealt with and the caller should retry the operation,
 * or a non-zero errno if the caller should give up. May block, and may never
 * return at all if the user has to intervene.
 */
typedef int (*ke_hard_error_handler_t)(int herrno);

/* Passing NULL removes the handler. */
void KeSetHardErrorHandler(ke_hard_error_handler_t handler);

/* Returns 0 if the caller should retry, non-zero if there is nothing to be done. */
int KeHardError(int herrno);
