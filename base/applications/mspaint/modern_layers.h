/* LiberNT Paint experimental raster layers (v4/v5 combined).
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

HWND ModernLayersCreate(HWND parent);
void ModernLayersLayout(HWND parent, RECT *available);
void ModernLayersToggle(HWND parent);
void ModernLayersAttachCanvas(void);
HBITMAP ModernLayersFlatten(HBITMAP original);
void ModernLayersPaintComposite(HDC target);
void ModernLayersImageResized(void);
void ModernLayersShutdown(void);
bool ModernLayersHasActiveOverlay(void);
bool ModernLayersUndo(void);
bool ModernLayersRedo(void);

bool ModernLayersWandMode(void);
void ModernLayersSetWandMode(bool enabled);
