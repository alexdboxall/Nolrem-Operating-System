#pragma once

#include <stddef.h>

#define GLUE(a, b) a##b
#define EVAL(a, b) GLUE(a, b)
#define MK_SYSCALL(name) \
    constexpr int SYS_##name = __COUNTER__ ; \
    size_t Sys##name(size_t, size_t, size_t, size_t);

MK_SYSCALL(Ref)
MK_SYSCALL(EmptyOrEveryRegion)
MK_SYSCALL(GetRegionCombination)
MK_SYSCALL(CreateRectRegion)
MK_SYSCALL(CreateEllipseRegion)
MK_SYSCALL(CreateRoundedRectRegion)
MK_SYSCALL(CreatePolyPolygonRegion)

size_t SystemCall(int call, size_t arg1, size_t arg2, size_t arg3, size_t arg4);