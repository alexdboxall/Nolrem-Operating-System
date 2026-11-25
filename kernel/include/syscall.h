#pragma once

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <phys.h>

/*
 * System call numbers.
 *
 * The number space is split into ranges of SYSCALL_RANGE_SIZE calls, one per
 * subsystem that provides system calls. The kernel's dispatcher knows nothing
 * about the subsystems' handlers: each one registers a table for its own range
 * when it initialises (KeRegisterSyscallRange). That is what lets CLIPDRAW or
 * the window manager live in a separate module and bring their system calls
 * with them.
 *
 * Call number 0 of every range is never valid.
 */
#define SYSCALL_RANGE_BITS      8
#define SYSCALL_RANGE_SIZE      (1 << SYSCALL_RANGE_BITS)
#define SYSCALL_NUM_RANGES      16
#define SYSCALL_BASE(range)     ((range) << SYSCALL_RANGE_BITS)

#define SYSCALL_RANGE_KE        0   /* The kernel itself (object handling, ...) */
#define SYSCALL_RANGE_CD        1   /* CLIPDRAW */
#define SYSCALL_RANGE_WM        2   /* The window manager (nothing registered yet) */

/*
 * One list per subsystem, in the order the numbers are handed out. Each entry
 * defines the number SYS_<name>, declares the handler Sys<name>, and (in the
 * subsystem's own registration code) fills in that subsystem's table. Add new
 * calls to the end of the list so the existing numbers don't move.
 */
#define KE_SYSCALLS(X) \
    X(Ref) X(GetObjectType) \
    X(Yield) X(Sleep) X(SleepUntil) X(GetTimeSinceBoot) \
    X(GetUtcTime) X(QuerySystemInformation) X(LastError)

#define CD_SYSCALLS(X) \
    X(EmptyOrEveryRegion) X(GetRegionCombination) X(CreateRectRegion) X(CreateEllipseRegion) \
    X(CreateRoundedRectRegion) X(CreatePolyPolygonRegion) X(IsRegionEqual) X(IsRegionEmpty) \
    X(IsPointInRegion) X(TranslateRegion) X(CopyRegion) X(GetRegionBounds) \
    X(CreatePatternedBrush) X(CreateCustomBrush) X(CopyBrush) X(GetStockBrush) \
    X(BrushPattern) X(BrushColour) X(BrushOrigin) X(CreatePatternedPen) \
    X(CreateCustomPen) X(CopyPen) X(GetStockPen) X(PenColour) \
    X(PenPattern) X(PenThickness) X(PenOrigin) X(SetGraphicsObject) \
    X(GetGraphicsObject) X(PaintRect) X(PaintRoundedRect) X(PaintEllipse) \
    X(PaintPolygon) X(PaintRegion) X(InvertRect) X(InvertRegion)

#define SYSCALL_ENUMERATOR(name)    SYS_##name,
#define SYSCALL_PROTOTYPE(name)     size_t Sys##name(size_t, size_t, size_t, size_t);

enum {
    SYS_KE_RESERVED = SYSCALL_BASE(SYSCALL_RANGE_KE),
    KE_SYSCALLS(SYSCALL_ENUMERATOR)
    SYS_KE_END,

    SYS_CD_RESERVED = SYSCALL_BASE(SYSCALL_RANGE_CD),
    CD_SYSCALLS(SYSCALL_ENUMERATOR)
    SYS_CD_END,
};

static_assert(SYS_KE_END <= SYSCALL_BASE(SYSCALL_RANGE_KE + 1), "too many kernel system calls for one range");
static_assert(SYS_CD_END <= SYSCALL_BASE(SYSCALL_RANGE_CD + 1), "too many CLIPDRAW system calls for one range");

KE_SYSCALLS(SYSCALL_PROTOTYPE)
CD_SYSCALLS(SYSCALL_PROTOTYPE)

/*
 * Arguments are one machine word, so 64-bit values (times, mostly) are passed as
 * two: the low half first, then the high half. For results that are 64 bits
 * wide the user passes a pointer, which the kernel writes through.
 */
#define SYSCALL_U64_LO(v)       ((size_t) (uint32_t) (v))
#define SYSCALL_U64_HI(v)       ((size_t) (uint32_t) ((uint64_t) (v) >> 32))
#define SYSCALL_U64(lo, hi)     (((uint64_t) (uint32_t) (hi) << 32) | (uint32_t) (lo))

// User side - requests it
size_t SystemCall(int call, size_t arg1, size_t arg2, size_t arg3, size_t arg4);
size_t SystemCall1(size_t arg1);
size_t SystemCall2(size_t arg1, size_t arg2);
size_t SystemCall3(size_t arg1, size_t arg2, size_t arg3);
size_t SystemCall4(size_t arg1, size_t arg2, size_t arg3, size_t arg4);

// Kernel side - handles it
typedef size_t (*ke_syscall_handler_t)(size_t, size_t, size_t, size_t);

/*
 * Installs the handlers for one range. `table` is indexed by the call number
 * minus SYSCALL_BASE(range), and must stay valid forever. Entries may be NULL
 * (they return ENOSYS). Returns EINVAL for a bad range or table, and EEXIST if
 * the range is already taken.
 */
int KeRegisterSyscallRange(int range, const ke_syscall_handler_t* table, size_t count);

/* Registers the kernel's own range. */
void KeInitSyscalls(void);

size_t KePerformSystemCall(size_t call, size_t a, size_t b, size_t c, size_t d);

/*
 * Sets the error that the calling thread can read back with GetLastError().
 * Handlers use it to say why they failed, when the return value has no room.
 */
void KeSetLastUserError(int err);
