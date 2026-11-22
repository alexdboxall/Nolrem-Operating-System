#pragma once

#include <stddef.h>
#include <errno.h>
#include <phys.h>

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
MK_SYSCALL(GetObjectType)
MK_SYSCALL(IsRegionEqual)
MK_SYSCALL(IsRegionEmpty)
MK_SYSCALL(IsPointInRegion)
MK_SYSCALL(TranslateRegion)
MK_SYSCALL(CopyRegion)
MK_SYSCALL(GetRegionBounds)
MK_SYSCALL(CreatePatternedBrush)
MK_SYSCALL(CreateCustomBrush)
MK_SYSCALL(CopyBrush)
MK_SYSCALL(GetStockBrush)
MK_SYSCALL(BrushPattern)
MK_SYSCALL(BrushColour)
MK_SYSCALL(BrushOrigin)
MK_SYSCALL(CreatePatternedPen)
MK_SYSCALL(CreateCustomPen)
MK_SYSCALL(CopyPen)
MK_SYSCALL(GetStockPen)
MK_SYSCALL(PenColour)
MK_SYSCALL(PenPattern)
MK_SYSCALL(PenThickness)
MK_SYSCALL(PenOrigin)
MK_SYSCALL(SetGraphicsObject)
MK_SYSCALL(GetGraphicsObject)
MK_SYSCALL(PaintRect)
MK_SYSCALL(PaintRoundedRect)
MK_SYSCALL(PaintEllipse)
MK_SYSCALL(PaintPolygon)
MK_SYSCALL(PaintRegion)
MK_SYSCALL(InvertRect)
MK_SYSCALL(InvertRegion)

// User side - requests it
size_t SystemCall(int call, size_t arg1, size_t arg2, size_t arg3, size_t arg4);
size_t SystemCall1(size_t arg1);
size_t SystemCall2(size_t arg1, size_t arg2);
size_t SystemCall3(size_t arg1, size_t arg2, size_t arg3);
size_t SystemCall4(size_t arg1, size_t arg2, size_t arg3, size_t arg4);

// Kernel side - handles it
size_t KePerformSystemCall(size_t call, size_t a, size_t b, size_t c, size_t d);