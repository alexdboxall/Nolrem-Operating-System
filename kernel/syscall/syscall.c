#include <common.h>
#include <syscall.h>
#include <errno.h>

static size_t (* const syscall_table[])(size_t, size_t, size_t, size_t) = {
    [SYS_CreateRectRegion] = SysCreateRectRegion,
    [SYS_Ref] = SysRef,
    [SYS_GetRegionCombination] = SysGetRegionCombination,
    [SYS_CreateEllipseRegion] = SysCreateEllipseRegion,
    [SYS_EmptyOrEveryRegion] = SysEmptyOrEveryRegion,
    [SYS_CreateRoundedRectRegion] = SysCreateRoundedRectRegion,
    [SYS_CreatePolyPolygonRegion] = SysCreatePolyPolygonRegion,
    [SYS_GetObjectType] = SysGetObjectType,
    [SYS_IsRegionEqual] = SysIsRegionEqual,
    [SYS_IsRegionEmpty] = SysIsRegionEmpty,
    [SYS_IsPointInRegion] = SysIsPointInRegion,
    [SYS_TranslateRegion] = SysTranslateRegion,
    [SYS_CopyRegion] = SysCopyRegion,
    [SYS_GetRegionBounds] = SysGetRegionBounds,
    [SYS_CreatePatternedBrush] = SysCreatePatternedBrush,
    [SYS_CreateCustomBrush] = SysCreateCustomBrush,
    [SYS_CopyBrush] = SysCopyBrush,
    [SYS_GetStockBrush] = SysGetStockBrush,
    [SYS_BrushPattern] = SysBrushPattern,
    [SYS_BrushColour] = SysBrushColour,
    [SYS_BrushOrigin] = SysBrushOrigin,
    [SYS_CreatePatternedPen] = SysCreatePatternedPen,
    [SYS_CreateCustomPen] = SysCreateCustomPen,
    [SYS_CopyPen] = SysCopyPen,
    [SYS_GetStockPen] = SysGetStockPen,
    [SYS_PenColour] = SysPenColour,
    [SYS_PenPattern] = SysPenPattern,
    [SYS_PenThickness] = SysPenThickness,
    [SYS_PenOrigin] = SysPenOrigin,
    [SYS_SetGraphicsObject] = SysSetGraphicsObject,
    [SYS_GetGraphicsObject] = SysGetGraphicsObject,
    [SYS_PaintRect] = SysPaintRect,
    [SYS_PaintRoundedRect] = SysPaintRoundedRect,
    [SYS_PaintEllipse] = SysPaintEllipse,
    [SYS_PaintPolygon] = SysPaintPolygon,
    [SYS_PaintRegion] = SysPaintRegion,
    [SYS_InvertRect] = SysInvertRect,
    [SYS_InvertRegion] = SysInvertRegion,
};

size_t KePerformSystemCall(size_t call, size_t a, size_t b, size_t c, size_t d) {
    if (call >= sizeof(syscall_table) / sizeof(syscall_table[0])) {
        return ENOSYS;
    }
    if (syscall_table[call] == NULL) {
        return ENOSYS;
    }
    return syscall_table[call](a, b, c, d);
}
