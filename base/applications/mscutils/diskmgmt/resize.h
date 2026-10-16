/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shrink Volume dialog and Extend Volume Wizard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef DISKMGMT_RESIZE_H
#define DISKMGMT_RESIZE_H

BOOL
DmResizeCanShrink(
    _In_ const DM_ACTION_CONTEXT *Context);

BOOL
DmResizeCanExtend(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_opt_ const DM_REGION *FreeRegion);

BOOL
DmResizeShrinkVolume(
    _In_ const DM_ACTION_CONTEXT *Context);

BOOL
DmResizeExtendVolume(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ const DM_REGION *FreeRegion);

#endif /* DISKMGMT_RESIZE_H */
