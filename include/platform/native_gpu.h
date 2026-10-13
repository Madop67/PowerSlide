/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_GPU_H
#define NATIVE_GPU_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

extern DISPENV activeDispEnv;
extern DRAWENV activeDrawEnv;
extern int g_GPUDisabledState;

int NativeGpu_HasPendingSplits(void);
void ClearSplits(void);
void DrawAllSplits(void);
void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive);
int NativeGpu_GetStateSize(void);
int NativeGpu_CaptureState(void *dst, int dstSize);
int NativeGpu_RestoreState(const void *src, int srcSize);

// Widescreen layout, set once per frame from MainFrame_ResetDB. Registers the
// ordering table of swapchain buffer `bufferIndex` (otBytes long) and its HUD
// slots [hudFirst, hudEnd). Primitives reached through those slots -- and, when
// menuMode is set, primitives in narrower-than-screen 3D viewports -- are drawn
// squeezed by the wide ratio (3/4 width at 16:9). anchorRegions is 2 when the race HUD is laid out per
// left/right screen half (3-4 players), else 1. Clears the anchor ranges.
void NativeGpu_SetWidescreenFrame(int bufferIndex, const void *otBase, int otBytes, const void *hudFirst, const void *hudEnd, int menuMode,
                                  int anchorRegions);
// Marks primitive memory [start, end) as race HUD, which edge anchoring may move
// to the screen (or split-screen half) edges.
void NativeGpu_AddHudAnchorRange(const void *start, const void *end);
// Marks primitive memory [start, end) as a full-frame image drawn in a HUD slot
// (the pause screenshot): it is tiled from the already-wide frame, so it is not squeezed.
void NativeGpu_AddFullFrameRange(const void *start, const void *end);
// Frame interpolation draws a frame between two real ones; it puts the anchor
// list back afterwards so the real frame's ranges are not crowded out.
int NativeGpu_GetHudAnchorCount(void);
void NativeGpu_SetHudAnchorCount(int count);

#endif
