/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef _VBCCINLINE_DEVICETREE_H
#define _VBCCINLINE_DEVICETREE_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#include "lvocall_compat.h"

MGLV3D_LP1(APTR, __DT_OpenKey, 6, CONST_STRPTR, a0)
#define DT_OpenKey(name) __DT_OpenKey(DeviceTreeBase, (name))

MGLV3D_LP1NR(__DT_CloseKey, 12, APTR, a0)
#define DT_CloseKey(key) __DT_CloseKey(DeviceTreeBase, (key))

MGLV3D_LP2(APTR, __DT_GetChild, 18, APTR, a0, APTR, a1)
#define DT_GetChild(key, prev) __DT_GetChild(DeviceTreeBase, (key), (prev))

MGLV3D_LP2(APTR, __DT_FindProperty, 24, APTR, a0, CONST_STRPTR, a1)
#define DT_FindProperty(key, property) __DT_FindProperty(DeviceTreeBase, (key), (property))

MGLV3D_LP2(APTR, __DT_GetProperty, 30, APTR, a0, APTR, a1)
#define DT_GetProperty(key, prev) __DT_GetProperty(DeviceTreeBase, (key), (prev))

MGLV3D_LP1(ULONG, __DT_GetPropLen, 36, APTR, a0)
#define DT_GetPropLen(property) __DT_GetPropLen(DeviceTreeBase, (property))

MGLV3D_LP1(CONST_STRPTR, __DT_GetPropName, 42, APTR, a0)
#define DT_GetPropName(property) __DT_GetPropName(DeviceTreeBase, (property))

/* rt is plain APTR, not the original's "CONST APTR" -- MGLV3D_LP1's
 * GCC branch needs to write the result into a register variable, and
 * a const-qualified local can't be an asm output. Const on a by-value
 * return type has no observable effect in C either way (the returned
 * rvalue was never assignable), so this is a no-op behavior change. */
MGLV3D_LP1(APTR, __DT_GetPropValue, 48, APTR, a0)
#define DT_GetPropValue(property) __DT_GetPropValue(DeviceTreeBase, (property))

MGLV3D_LP1(APTR, __DT_GetParent, 54, APTR, a0)
#define DT_GetParent(key) __DT_GetParent(DeviceTreeBase, (key))

MGLV3D_LP1(CONST_STRPTR, __DT_GetKeyName, 60, APTR, a0)
/* NB: bug in this macro, carried over unchanged from the original --
 * it references `key`, which is not one of its parameters, instead of
 * its own `property` parameter. Nothing in this repository calls
 * DT_GetKeyName. */
#define DT_GetKeyName(property) __DT_GetKeyName(DeviceTreeBase, (key))

MGLV3D_LP2(APTR, __DT_FindPropertyRecursive, 66, APTR, a0, CONST_STRPTR, a1)
#define DT_FindPropertyRecursive(key, property) __DT_FindPropertyRecursive(DeviceTreeBase, (key), (property))

#endif /* !_VBCCINLINE_DEVICETREE_H */
