/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include "platform/native_gpu.h"

#include <platform.h>

#include <SDL3/SDL.h>

#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"
#include "platform/native_settings.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Platform_PollHostEvents(void);
extern int g_cfg_bilinearFiltering;
extern int g_dbg_emulatorPaused;
extern int g_dbg_polygonSelected;

#define NATIVE_GPU_LOG(fmt, ...)   Platform_Log("[CTR GPU] " fmt, __VA_ARGS__)
#define NATIVE_GPU_ERROR(fmt, ...) Platform_LogError("[CTR GPU] [%s] - " fmt, __func__, __VA_ARGS__)

// NOTE(aalhendi): Little-endian tag `CTRG` = CTR native GPU snapshot.
#define NATIVE_GPU_STATE_MAGIC     0x47525443
#define NATIVE_GPU_STATE_VERSION   1

#define GET_TPAGE_BLEND(tpage)     ((BlendMode)(((tpage >> 5) & 3) + 1))

#define GET_TPAGE_DITHER(tpage)    ((tpage >> 9) & 0x1)

#define GET_CLUT_X(clut)           ((clut & 0x3F) << 4)
#define GET_CLUT_Y(clut)           (clut >> 6)

internal TexFormat GetTPageFormat(int tpage)
{
	const int mode = (tpage >> 7) & 0x3;

	// NOTE(aalhendi): ctr-native local divergence from upstream PsyCross. PS1
	// mode 3 is reserved; TF_32_BIT_RGBA is only for explicit native override
	// textures, not raw retail TPAGE mode bits.
	return mode == 3 ? TF_16_BIT : (TexFormat)mode;
}

internal short GetTPageBase(int tpage)
{
	const u16 page = (u16)tpage;

	// NOTE(aalhendi): ctr-native local divergence for CTR retail emitters. The
	// shader wants xPage + yPage * 16, not raw draw-mode bits.
	return (s16)((page & 0xf) | ((page & 0x10) ? 0x10 : 0));
}

internal s16 NativeGpu_SignExtend11(u32 value)
{
	value &= 0x7ff;
	return (s16)((value ^ 0x400) - 0x400);
}

DISPENV activeDispEnv;
DRAWENV activeDrawEnv;
int g_GPUDisabledState = 0;

typedef struct
{
	DRAWENV drawenv;
	DISPENV dispenv;

	BlendMode blendMode;

	TexFormat texFormat;
	TextureID textureId;

	int drawPrimMode;
	bool psxTexturedSemiTrans;
	bool psxTextureOutputSTP;
	bool psxDrawMaskSet;
	// Widescreen: this split's scissor (a menu sub-viewport) is compressed like its vertices.
	bool wideClip;

	u16 startVertex;
	u16 numVerts;

	const char *debugText;
} GPUDrawSplit;

#define MAX_DRAW_SPLITS 4096

typedef struct
{
	const char *currentSplitDebugText;
	TextureID overrideTexture;
	int overrideTextureWidth;
	int overrideTextureHeight;

	int drawPrimMode;
	bool psxDrawMaskSet;
	bool framebufferFeedbackRunActive;

	GrVertex vertexBuffer[MAX_VERTEX_BUFFER_SIZE];
	GPUDrawSplit splits[MAX_DRAW_SPLITS];
	int vertexIndex;
	int splitIndex;
} NativeGpuState;

global_variable NativeGpuState s_gpu;

struct NativeGpuSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	DRAWENV drawEnv;
	DISPENV dispEnv;
	s32 gpuDisabledState;
	s32 psxDrawMaskSet;
	u16 vram[VRAM_WIDTH * VRAM_HEIGHT];
};

int NativeGpu_HasPendingSplits(void)
{
	return s_gpu.splitIndex > 0;
}

void ClearSplits(void)
{
	s_gpu.currentSplitDebugText = NULL;
	s_gpu.vertexIndex = 0;
	s_gpu.splitIndex = 0;
	s_gpu.splits[0].texFormat = (TexFormat)0xFFFF;
	s_gpu.splits[0].psxTexturedSemiTrans = false;
	s_gpu.splits[0].psxTextureOutputSTP = false;
	s_gpu.splits[0].psxDrawMaskSet = false;
	s_gpu.framebufferFeedbackRunActive = false;
}

int NativeGpu_GetStateSize(void)
{
	return (int)sizeof(struct NativeGpuSnapshot);
}

int NativeGpu_CaptureState(void *dst, int dstSize)
{
	struct NativeGpuSnapshot *snapshot = (struct NativeGpuSnapshot *)dst;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_GPU_STATE_MAGIC;
	snapshot->version = NATIVE_GPU_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->drawEnv = activeDrawEnv;
	snapshot->dispEnv = activeDispEnv;
	snapshot->gpuDisabledState = g_GPUDisabledState;
	snapshot->psxDrawMaskSet = s_gpu.psxDrawMaskSet;

	return NativeRenderer_CaptureVRAMState(snapshot->vram, sizeof(snapshot->vram));
}

int NativeGpu_RestoreState(const void *src, int srcSize)
{
	const struct NativeGpuSnapshot *snapshot = (const struct NativeGpuSnapshot *)src;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_GPU_STATE_MAGIC) || (snapshot->version != NATIVE_GPU_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->gpuDisabledState < 0) || (snapshot->gpuDisabledState > 1) || (snapshot->psxDrawMaskSet < 0) || (snapshot->psxDrawMaskSet > 1))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}
	if (!NativeRenderer_RestoreVRAMState(snapshot->vram, sizeof(snapshot->vram)))
	{
		return 0;
	}

	activeDrawEnv = snapshot->drawEnv;
	activeDispEnv = snapshot->dispEnv;
	g_GPUDisabledState = snapshot->gpuDisabledState;
	s_gpu.psxDrawMaskSet = snapshot->psxDrawMaskSet;
	ClearSplits();
	return 1;
}

internal void DrawEnvDimensionsInt(int *width, int *height)
{
	if (activeDrawEnv.dfe)
	{
		*width = activeDispEnv.disp.w;
		*height = activeDispEnv.disp.h;
	}
	else
	{
		*width = activeDrawEnv.clip.w;
		*height = activeDrawEnv.clip.h;
	}
}

void DrawEnvOffset(float *ofsX, float *ofsY)
{
	if (activeDrawEnv.dfe)
	{
		int w, h;
		DrawEnvDimensionsInt(&w, &h);

		if (w <= 0)
		{
			w = 1;
		}

		// NOTE(aalhendi): Convert PS1 VRAM-page draw offsets into display-relative host-screen offsets.
		// CTR alternates draw pages at y=0 and y=0x128; using raw modulo VRAM offsets shifts every other native frame vertically.
		*ofsX = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
	}
	else
	{
		*ofsX = activeDrawEnv.ofs[0] - activeDrawEnv.clip.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDrawEnv.clip.y;
	}
}

//------------------------------------------------------------------------------------------------------------------------
// Widescreen layout. In widescreen the whole 4:3 frame is shown stretched to the wide ratio
// and full-screen 3D views are pre-compressed by the projection
// (PushBuffer_SetMatrixVP). Everything else -- the HUD slots, and in menus the small
// 3D preview windows -- is compressed here at draw time instead, which leaves game
// memory (and savestates/replays) untouched. With edge anchoring, race HUD elements
// then move back out towards the screen (or split-screen half) edges.

#define NATIVE_WIDE_OT_BUFFERS    2
#define NATIVE_WIDE_ANCHOR_RANGES 8
#define NATIVE_WIDE_ANCHOR_GROUPS 32
// Group whose centre has not been measured yet.
#define NATIVE_WIDE_GROUP_UNMEASURED INT_MIN

struct NativeWideFrame
{
	const u8 *otBase[NATIVE_WIDE_OT_BUFFERS];
	const u8 *otEnd[NATIVE_WIDE_OT_BUFFERS];
	const u8 *hudFirst[NATIVE_WIDE_OT_BUFFERS];
	const u8 *hudEnd[NATIVE_WIDE_OT_BUFFERS];
	b32 menuMode;
	s32 anchorRegions;
	const u8 *anchorStart[NATIVE_WIDE_ANCHOR_RANGES];
	const u8 *anchorEnd[NATIVE_WIDE_ANCHOR_RANGES];
	// Full-frame range (see NativeGpu_AddFullFrameRange) instead of race HUD.
	b32 anchorFullFrame[NATIVE_WIDE_ANCHOR_RANGES];
	s32 anchorCount;
	// HUD models (NativeGpu_AddHudAnchorGroup), each placed by its centre X (raw
	// packet coordinates, measured when the walk first reaches it).
	const u8 *groupStart[NATIVE_WIDE_ANCHOR_GROUPS];
	const u8 *groupEnd[NATIVE_WIDE_ANCHOR_GROUPS];
	s32 groupMidX[NATIVE_WIDE_ANCHOR_GROUPS];
	s32 groupCount;
};

global_variable struct NativeWideFrame s_wide;
// Set while the OT walk is inside one of the HUD slots' primitive chains.
global_variable b32 s_inHudOT;
// Set while parsing a packet that lies in an anchor range (race HUD).
global_variable b32 s_primAnchored;
// Set while parsing a packet of a full-frame image in a HUD slot, which keeps its full width.
global_variable b32 s_primFullFrame;
// Group of the packet being parsed (index into s_wide.group*), or -1.
global_variable s32 s_primGroup = -1;

void NativeGpu_SetWidescreenFrame(int bufferIndex, const void *otBase, int otBytes, const void *hudFirst, const void *hudEnd, int menuMode,
                                  int anchorRegions)
{
	if ((bufferIndex < 0) || (bufferIndex >= NATIVE_WIDE_OT_BUFFERS))
	{
		return;
	}
	s_wide.otBase[bufferIndex] = (const u8 *)otBase;
	s_wide.otEnd[bufferIndex] = (const u8 *)otBase + otBytes;
	s_wide.hudFirst[bufferIndex] = (const u8 *)hudFirst;
	s_wide.hudEnd[bufferIndex] = (const u8 *)hudEnd;
	s_wide.menuMode = menuMode != 0;
	s_wide.anchorRegions = (anchorRegions == 2) ? 2 : 1;
	s_wide.anchorCount = 0;
	s_wide.groupCount = 0;
}

internal void NativeGpu_AddRange(const void *start, const void *end, b32 fullFrame)
{
	if ((s_wide.anchorCount >= NATIVE_WIDE_ANCHOR_RANGES) || (start == NULL) || (end <= start))
	{
		return;
	}
	s_wide.anchorStart[s_wide.anchorCount] = (const u8 *)start;
	s_wide.anchorEnd[s_wide.anchorCount] = (const u8 *)end;
	s_wide.anchorFullFrame[s_wide.anchorCount] = fullFrame;
	s_wide.anchorCount++;
}

void NativeGpu_AddHudAnchorRange(const void *start, const void *end)
{
	NativeGpu_AddRange(start, end, false);
}

void NativeGpu_AddFullFrameRange(const void *start, const void *end)
{
	NativeGpu_AddRange(start, end, true);
}

void NativeGpu_AddHudAnchorGroup(const void *start, const void *end)
{
	if ((s_wide.groupCount >= NATIVE_WIDE_ANCHOR_GROUPS) || (start == NULL) || (end <= start))
	{
		return;
	}
	s_wide.groupStart[s_wide.groupCount] = (const u8 *)start;
	s_wide.groupEnd[s_wide.groupCount] = (const u8 *)end;
	s_wide.groupMidX[s_wide.groupCount] = NATIVE_WIDE_GROUP_UNMEASURED;
	s_wide.groupCount++;
}

// Mark: anchor count in the low 16 bits, group count above.
int NativeGpu_GetHudAnchorCount(void)
{
	return s_wide.anchorCount | (s_wide.groupCount << 16);
}

void NativeGpu_SetHudAnchorCount(int count)
{
	const int anchors = count & 0xffff;
	const int groups = (count >> 16) & 0xffff;
	if (anchors <= s_wide.anchorCount)
	{
		s_wide.anchorCount = anchors;
	}
	if (groups <= s_wide.groupCount)
	{
		s_wide.groupCount = groups;
	}
}

// Widens [*minX, *maxX] by the vertices of the GPU primitives in one packet's words
// [words, end). Polygons and sprites only (what models and HUD art are made of);
// stops at anything else.
internal void NativeGpu_PacketXRange(const u32 *words, const u32 *end, int *minX, int *maxX)
{
	while (words < end)
	{
		const u32 code = words[0] >> 24;
		int xs[4];
		int count = 0;
		int length;

		if ((code >= 0x20) && (code < 0x40))
		{
			const b32 gouraud = (code & 0x10) != 0;
			const b32 quad = (code & 0x08) != 0;
			const b32 textured = (code & 0x04) != 0;
			const int verts = quad ? 4 : 3;
			const u32 *w = words + 1;
			for (int v = 0; v < verts; v++)
			{
				if (gouraud && (v > 0))
				{
					w++;
				}
				xs[count++] = (s16)(w[0] & 0xffff);
				w += textured ? 2 : 1;
			}
			length = (int)(w - words);
		}
		else if ((code >= 0x60) && (code < 0x80))
		{
			const b32 textured = (code & 0x04) != 0;
			const u32 size = code & 0x18;
			const u32 *w = words + 2 + (textured ? 1 : 0);
			int width = (size == 0x08) ? 1 : (size == 0x10) ? 8 : 16;
			if (size == 0)
			{
				width = (s16)(w[0] & 0xffff);
				w++;
			}
			xs[count++] = (s16)(words[1] & 0xffff);
			xs[count++] = xs[0] + width;
			length = (int)(w - words);
		}
		else
		{
			return;
		}

		for (int i = 0; i < count; i++)
		{
			*minX = SDL_min(*minX, xs[i]);
			*maxX = SDL_max(*maxX, xs[i]);
		}
		words += length;
	}
}

// Centre X of a group's primitives, from the packets written in its memory.
internal s32 NativeGpu_MeasureGroup(int group)
{
	const u8 *packet = s_wide.groupStart[group];
	const u8 *end = s_wide.groupEnd[group];
	int minX = INT_MAX;
	int maxX = INT_MIN;

	while (packet + sizeof(u32) <= end)
	{
		const int length = getlen(packet);
		const u8 *next = packet + (length + P_LEN) * sizeof(u32);
		if ((length <= 0) || (length > 32) || (next > end))
		{
			break;
		}
		NativeGpu_PacketXRange((const u32 *)packet + P_LEN, (const u32 *)next, &minX, &maxX);
		packet = next;
	}

	if (minX > maxX)
	{
		return NATIVE_WIDE_GROUP_UNMEASURED;
	}
	return (minX + maxX) / 2;
}

// Called for every zero-length packet in the walk: OT slots are the only
// zero-length nodes, and each one starts the chain of a new slot.
internal void NativeGpu_UpdateHudRegion(const u8 *packet)
{
	for (int i = 0; i < NATIVE_WIDE_OT_BUFFERS; i++)
	{
		if ((packet >= s_wide.hudFirst[i]) && (packet < s_wide.hudEnd[i]))
		{
			s_inHudOT = true;
			return;
		}
		if ((packet >= s_wide.otBase[i]) && (packet < s_wide.otEnd[i]))
		{
			s_inHudOT = false;
			return;
		}
	}
}

// Finds the range holding packet: sets *anchored for race HUD, *fullFrame for a full-frame image.
internal void NativeGpu_ClassifyPacket(const u8 *packet, b32 *anchored, b32 *fullFrame)
{
	*anchored = false;
	*fullFrame = false;
	s_primGroup = -1;
	for (int i = 0; i < s_wide.anchorCount; i++)
	{
		if ((packet >= s_wide.anchorStart[i]) && (packet < s_wide.anchorEnd[i]))
		{
			*fullFrame = s_wide.anchorFullFrame[i];
			*anchored = !s_wide.anchorFullFrame[i];
			break;
		}
	}

	if (!*anchored)
	{
		return;
	}

	for (int i = 0; i < s_wide.groupCount; i++)
	{
		if ((packet >= s_wide.groupStart[i]) && (packet < s_wide.groupEnd[i]))
		{
			if (s_wide.groupMidX[i] == NATIVE_WIDE_GROUP_UNMEASURED)
			{
				s_wide.groupMidX[i] = NativeGpu_MeasureGroup(i);
			}
			if (s_wide.groupMidX[i] != NATIVE_WIDE_GROUP_UNMEASURED)
			{
				s_primGroup = i;
			}
			return;
		}
	}
}

internal b32 NativeGpu_ClipIsSubViewport(const RECT16 *clip)
{
	return (clip->w > 0) && (clip->w < activeDispEnv.disp.w);
}

internal b32 NativeGpu_HudSqueezeActive(void)
{
	if (!activeDrawEnv.dfe || !NativeSettings_WidescreenActive())
	{
		return false;
	}
	if (s_inHudOT)
	{
		return !s_primFullFrame;
	}
	// Menu 3D preview windows skip the projection change and are squeezed with their frames.
	return s_wide.menuMode && NativeGpu_ClipIsSubViewport(&activeDrawEnv.clip);
}

internal s16 NativeGpu_SqueezeHudX(int x, int centerX)
{
	return (s16)(centerX + (int)SDL_floorf((f32)(x - centerX) * NativeSettings_WideSqueeze() + 0.5f));
}

// Picks the squeeze centre and edge-anchor shift for a primitive spanning
// [minX, maxX]. Returns false for full-width primitives (fades, backgrounds),
// which keep covering the screen.
internal b32 NativeGpu_WideLayout(int minX, int maxX, int *centerX, int *shift)
{
	int width;
	int height;
	DrawEnvDimensionsInt(&width, &height);

	if ((minX <= 0) && (maxX >= width - 1))
	{
		return false;
	}

	*centerX = width / 2;
	*shift = 0;

	if (s_inHudOT && s_primAnchored && (g_settings.video.hudPosition == NATIVE_HUD_POSITION_EDGES))
	{
		// Left and right thirds of the screen (or of each split-screen half) move out by
		// half the width the squeeze took away (1/8 at 16:9); the middle stays centred.
		// A HUD model moves as a whole, by its centre.
		int mid = (minX + maxX) / 2;
		if (s_primGroup >= 0)
		{
			float ofsX, ofsY;
			DrawEnvOffset(&ofsX, &ofsY);
			mid = s_wide.groupMidX[s_primGroup] + (int)ofsX;
		}
		const int regionW = (s_wide.anchorRegions == 2) ? width / 2 : width;
		const int regionX = ((s_wide.anchorRegions == 2) && (mid >= width / 2)) ? width / 2 : 0;
		const int rel = mid - regionX;

		const int edgeShift = (regionW * (1000 - NativeSettings_WideSqueeze1000())) / 2000;

		*centerX = regionX + regionW / 2;
		if (rel * 10 < regionW * 4)
		{
			*shift = -edgeShift;
		}
		else if (rel * 10 > regionW * 6)
		{
			*shift = edgeShift;
		}
	}

	// A centred primitive sliding off a 4:3 edge (results, menus) would stop short of
	// the wide screen's edge once squeezed. Push it out by how far it has left the 4:3
	// frame, so it is off screen exactly when it is off the 4:3 frame.
	if ((*shift == 0) && (maxX > minX))
	{
		const int span = maxX - minX;
		const int edgeOffset = (width * (1000 - NativeSettings_WideSqueeze1000())) / 2000;
		if (minX < 0)
		{
			*shift = -(edgeOffset * SDL_min(-minX, span)) / span;
		}
		else if (maxX > width)
		{
			*shift = (edgeOffset * SDL_min(maxX - width, span)) / span;
		}
	}
	return true;
}

// Compresses a primitive's vertices by the wide squeeze (3/4 width at 16:9; see NativeGpu_WideLayout).
internal void NativeGpu_SqueezeHudVertices(GrVertex *vertex, int count)
{
	int minX = vertex[0].x;
	int maxX = vertex[0].x;
	for (int i = 1; i < count; i++)
	{
		minX = SDL_min(minX, vertex[i].x);
		maxX = SDL_max(maxX, vertex[i].x);
	}

	int centerX;
	int shift;
	if (!NativeGpu_WideLayout(minX, maxX, &centerX, &shift))
	{
		return;
	}

	for (int i = 0; i < count; i++)
	{
		vertex[i].x = (s16)(NativeGpu_SqueezeHudX(vertex[i].x, centerX) + shift);
	}
}

// Compresses a menu sub-viewport's scissor rect the same way as its vertices.
internal RECT16 NativeGpu_SqueezeClip(const RECT16 *clip, const DISPENV *dispenv)
{
	RECT16 result = *clip;
	const int centerX = dispenv->disp.w / 2;
	const int x0 = NativeGpu_SqueezeHudX(clip->x - dispenv->disp.x, centerX);
	const int x1 = NativeGpu_SqueezeHudX(clip->x - dispenv->disp.x + clip->w, centerX);
	result.x = (s16)(dispenv->disp.x + x0);
	result.w = (s16)(x1 - x0);
	return result;
}

void LineSwapSourceVerts(VERTTYPE **p0, VERTTYPE **p1, u8 **c0, u8 **c1)
{
	// swap line coordinates for left-to-right and up-to-bottom direction
	if (((*p0)[0] > (*p1)[0]) || ((*p0)[1] > (*p1)[1] && (*p0)[0] == (*p1)[0]))
	{
		VERTTYPE *tmp = *p0;
		*p0 = *p1;
		*p1 = tmp;

		u8 *tmpCol = *c0;
		*c0 = *c1;
		*c1 = tmpCol;
	}
}

void MakeLineArray(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1)
{
	const VERTTYPE dx = p1[0] - p0[0];
	const VERTTYPE dy = p1[1] - p0[1];

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	// Widescreen HUD squeezes the endpoints but keeps the 1-pixel thickness.
	int x0 = (int)(p0[0] + ofsX);
	int x1 = (int)(p1[0] + ofsX);
	int centerX;
	int shift;
	if (NativeGpu_HudSqueezeActive() && NativeGpu_WideLayout(SDL_min(x0, x1), SDL_max(x0, x1), &centerX, &shift))
	{
		x0 = NativeGpu_SqueezeHudX(x0, centerX) + shift;
		x1 = NativeGpu_SqueezeHudX(x1, centerX) + shift;
	}

	if (dx > abs((s16)dy))
	{ // horizontal
		vertex[0].x = x0;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = x1 + 1;
		vertex[1].y = p1[1] + ofsY;

		vertex[2].x = vertex[1].x;
		vertex[2].y = vertex[1].y + 1;

		vertex[3].x = vertex[0].x;
		vertex[3].y = vertex[0].y + 1;
	}
	else
	{ // vertical
		vertex[0].x = x0;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = x1;
		vertex[1].y = p1[1] + ofsY + 1;

		vertex[2].x = vertex[1].x + 1;
		vertex[2].y = vertex[1].y;

		vertex[3].x = vertex[0].x + 1;
		vertex[3].y = vertex[0].y;
	} // TODO diagonal line alignment
}

void MakeVertexTriangle(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2)
{
	assert(p0);
	assert(p1);
	assert(p2);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 3);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = p1[0] + ofsX;
	vertex[1].y = p1[1] + ofsY;

	vertex[2].x = p2[0] + ofsX;
	vertex[2].y = p2[1] + ofsY;

	if (NativeGpu_HudSqueezeActive())
	{
		NativeGpu_SqueezeHudVertices(vertex, 3);
	}
}

void MakeVertexQuad(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2, VERTTYPE *p3)
{
	assert(p0);
	assert(p1);
	assert(p2);
	assert(p3);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = p1[0] + ofsX;
	vertex[1].y = p1[1] + ofsY;

	vertex[2].x = p2[0] + ofsX;
	vertex[2].y = p2[1] + ofsY;

	vertex[3].x = p3[0] + ofsX;
	vertex[3].y = p3[1] + ofsY;

	if (NativeGpu_HudSqueezeActive())
	{
		NativeGpu_SqueezeHudVertices(vertex, 4);
	}
}

void MakeVertexRect(GrVertex *vertex, VERTTYPE *p0, s16 w, s16 h)
{
	assert(p0);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = vertex[0].x;
	vertex[1].y = vertex[0].y + h;

	vertex[2].x = vertex[0].x + w;
	vertex[2].y = vertex[0].y + h;

	vertex[3].x = vertex[0].x + w;
	vertex[3].y = vertex[0].y;

	if (NativeGpu_HudSqueezeActive())
	{
		NativeGpu_SqueezeHudVertices(vertex, 4);
	}
}

void MakeTexcoordQuad(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, u8 *uv3, s16 page, s16 clut, u8 dither)
{
	assert(uv0);
	assert(uv1);
	assert(uv2);
	assert(uv3);

	const u8 bright = 2;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv3[0];
	vertex[3].v = uv3[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;
	/*
	if (g_cfg_bilinearFiltering)
	{
	    vertex[0].tcx = -1;
	    vertex[0].tcy = -1;

	    vertex[1].tcx = -1;
	    vertex[1].tcy = -1;

	    vertex[2].tcx = -1;
	    vertex[2].tcy = -1;

	    vertex[3].tcx = -1;
	    vertex[3].tcy = -1;
	}*/
}

void MakeTexcoordTriangle(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, s16 page, s16 clut, u8 dither)
{
	assert(uv0);
	assert(uv1);
	assert(uv2);

	const u8 bright = 2;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;
	/*
	if (g_cfg_bilinearFiltering)
	{
	    vertex[0].tcx = -1;
	    vertex[0].tcy = -1;

	    vertex[1].tcx = -1;
	    vertex[1].tcy = -1;

	    vertex[2].tcx = -1;
	    vertex[2].tcy = -1;

	    vertex[3].tcx = -1;
	    vertex[3].tcy = -1;
	}*/
}

void MakeTexcoordRect(GrVertex *vertex, u8 *uv, s16 page, s16 clut, s16 w, s16 h)
{
	assert(uv);

	// sim overflow
	if ((int)uv[0] + w > 255)
	{
		w = 255 - uv[0];
	}
	if ((int)uv[1] + h > 255)
	{
		h = 255 - uv[1];
	}

	const u8 bright = 2;
	const u8 dither = 0;
	const short texPage = GetTPageBase(page);

	vertex[0].u = uv[0];
	vertex[0].v = uv[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv[0];
	vertex[1].v = uv[1] + h;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv[0] + w;
	vertex[2].v = uv[1] + h;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv[0] + w;
	vertex[3].v = uv[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;

	if (g_cfg_bilinearFiltering)
	{
		vertex[0].tcx = -1;
		vertex[0].tcy = -1;

		vertex[1].tcx = -1;
		vertex[1].tcy = -1;

		vertex[2].tcx = -1;
		vertex[2].tcy = -1;

		vertex[3].tcx = -1;
		vertex[3].tcy = -1;
	}
}

void MakeTexcoordLineZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeTexcoordTriangleZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;
}

void MakeTexcoordQuadZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeColourNoShade(GrVertex *vertex, int n)
{
	--n;
	while (n >= 0)
	{
		vertex[n].r = 128;
		vertex[n].g = 128;
		vertex[n].b = 128;
		vertex[n].a = 255;
		--n;
	}
}

void MakeColourLine(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}
	assert(col0);
	assert(col1);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col1[0];
	vertex[2].g = col1[1];
	vertex[2].b = col1[2];
	vertex[2].a = 255;

	vertex[3].r = col0[0];
	vertex[3].g = col0[1];
	vertex[3].b = col0[2];
	vertex[3].a = 255;
}

void MakeColourTriangle(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 3);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;
}

void MakeColourQuad(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2, u8 *col3)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);
	assert(col3);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;

	vertex[3].r = col3[0];
	vertex[3].g = col3[1];
	vertex[3].b = col3[2];
	vertex[3].a = 255;
}

void TriangulateQuad()
{
	/*
	Triangulate like this:

	v0--v1
	|  / |
	| /  |
	v2--v3

	NOTE: v2 swapped with v3 during primitive parsing but it not shown here
	*/

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 4] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 5] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 2];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 2] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 3] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 1];
}

//------------------------------------------------------------------------------------------------------------------------

internal bool NativeGpu_RectOverlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	return (aw > 0) && (ah > 0) && (bw > 0) && (bh > 0) && (ax < bx + bw) && (bx < ax + aw) && (ay < by + bh) && (by < ay + ah);
}

internal bool NativeGpu_TPageOverlapsActiveDrawPage(int tpage)
{
	const int pageX = (tpage & 0xf) << 6;
	const int pageY = (tpage & 0x10) ? 0x100 : 0;
	const int pageW = 0x100;
	const int pageH = 0x100;

	if (!activeDrawEnv.dfe)
	{
		return false;
	}

	if (GetTPageFormat(tpage) != TF_16_BIT)
	{
		return false;
	}

	return NativeGpu_RectOverlaps(pageX, pageY, pageW, pageH, activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h);
}

internal void NativeGpu_PrepareFramebufferFeedback(int tpage)
{
	if (!NativeGpu_TPageOverlapsActiveDrawPage(tpage))
	{
		return;
	}

	if (s_gpu.framebufferFeedbackRunActive)
	{
		return;
	}

	// NOTE(aalhendi): PS1 can draw into VRAM and immediately texture from that
	// same draw page. Native batches primitives, so screen-feedback effects
	// like heat warp need an explicit barrier before their framebuffer-sampling
	// polygons consume the VRAM texture.
	if (NativeGpu_HasPendingSplits())
	{
		DrawAllSplits();
	}

	NativeRenderer_StoreFrameBuffer(activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h);
	s_gpu.framebufferFeedbackRunActive = true;
}

internal void AddSplit(bool semiTrans, bool textured, bool framebufferFeedback)
{
	int tpage = activeDrawEnv.tpage;

	if (framebufferFeedback)
	{
		NativeGpu_PrepareFramebufferFeedback(tpage);
	}
	else
	{
		s_gpu.framebufferFeedbackRunActive = false;
	}

	GPUDrawSplit *curSplit = &s_gpu.splits[s_gpu.splitIndex];

	BlendMode blendMode = semiTrans ? GET_TPAGE_BLEND(tpage) : BM_NONE;
	TexFormat texFormat = GetTPageFormat(tpage);
	TextureID textureId = textured ? NativeRenderer_GetVRAMTexture() : NativeRenderer_GetWhiteTexture();
	bool psxTexturedSemiTrans = semiTrans && textured && s_gpu.overrideTexture == 0;
	// NOTE(aalhendi): PS1 framebuffer bit 15 follows sampled texture STP for
	// textured draws unless E6 forces it. Recursive screen-copy effects depend
	// on this bit surviving after the blended textured pass.
	bool psxTextureOutputSTP = textured && s_gpu.overrideTexture == 0;

	if (textured && s_gpu.overrideTexture != 0)
	{
		// override texture format, zero tpage
		texFormat = TF_32_BIT_RGBA;
		textureId = s_gpu.overrideTexture;
		psxTexturedSemiTrans = false;
	}

	const bool wideClip = !s_inHudOT && NativeGpu_HudSqueezeActive();

	// FIXME: compare drawing environment too?
	if (!psxTexturedSemiTrans && curSplit->blendMode == blendMode && curSplit->texFormat == texFormat && curSplit->textureId == textureId &&
	    curSplit->drawPrimMode == s_gpu.drawPrimMode && curSplit->psxTexturedSemiTrans == psxTexturedSemiTrans &&
	    curSplit->psxTextureOutputSTP == psxTextureOutputSTP && curSplit->psxDrawMaskSet == s_gpu.psxDrawMaskSet && curSplit->wideClip == wideClip &&
	    curSplit->drawenv.clip.x == activeDrawEnv.clip.x && curSplit->drawenv.clip.y == activeDrawEnv.clip.y &&
	    curSplit->drawenv.clip.w == activeDrawEnv.clip.w && curSplit->drawenv.clip.h == activeDrawEnv.clip.h && curSplit->drawenv.dfe == activeDrawEnv.dfe &&
	    curSplit->debugText == s_gpu.currentSplitDebugText)
	{
		return;
	}

	curSplit->numVerts = s_gpu.vertexIndex - curSplit->startVertex;

	if (s_gpu.splitIndex + 1 >= MAX_DRAW_SPLITS)
	{
		NATIVE_GPU_ERROR("%s\n", "MAX_DRAW_SPLITS reached (too many blend modes, texture formats, drawEnv clip rects, dfe switches), expect rendering errors");
		return;
	}

	GPUDrawSplit *split = &s_gpu.splits[++s_gpu.splitIndex];
	split->blendMode = blendMode;
	split->texFormat = texFormat;
	split->textureId = textureId;
	split->drawPrimMode = s_gpu.drawPrimMode;
	split->psxTexturedSemiTrans = psxTexturedSemiTrans;
	split->psxTextureOutputSTP = psxTextureOutputSTP;
	split->psxDrawMaskSet = s_gpu.psxDrawMaskSet;
	split->wideClip = wideClip;
	split->drawenv = activeDrawEnv;
	split->dispenv = activeDispEnv;
	split->debugText = s_gpu.currentSplitDebugText;

	split->drawenv.tw.w = s_gpu.overrideTextureWidth;
	split->drawenv.tw.h = s_gpu.overrideTextureHeight;

	split->startVertex = s_gpu.vertexIndex;
	split->numVerts = 0;
}

void DrawSplit(const GPUDrawSplit *split)
{
	if (split->debugText)
	{
		NativeRenderer_PushDebugLabel(split->debugText);
	}

	const bool drawOnScreen = split->drawenv.dfe;
	if ((split->drawenv.clip.w <= 0) || (split->drawenv.clip.h <= 0))
	{
		// NOTE(aalhendi): VS/Battle end previews can shrink a losing viewport
		// into an empty retail draw area. Empty clips should consume no pixels,
		// and must not leak stale native offscreen/scissor state.
		NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
		NativeRenderer_SetOffscreenState(&split->drawenv.clip, 0);
		if (split->debugText)
		{
			NativeRenderer_PopDebugLabel();
		}
		return;
	}

	NativeRenderer_SetStencilMode(split->drawPrimMode); // draw with mask 0x16

	NativeRenderer_SetTexture(split->textureId, split->texFormat);

	if (split->texFormat == TF_32_BIT_RGBA)
	{
		NativeRenderer_SetOverrideTextureSize(split->drawenv.tw.w, split->drawenv.tw.h);
	}

	NativeRenderer_SetPSXDrawMaskSet(split->psxDrawMaskSet);
	NativeRenderer_SetPSXTextureOutputSTP(split->psxTextureOutputSTP);

	if (split->wideClip)
	{
		const RECT16 wideClip = NativeGpu_SqueezeClip(&split->drawenv.clip, &split->dispenv);
		NativeRenderer_SetupClipMode(&wideClip, &split->dispenv, drawOnScreen);
	}
	else
	{
		NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
	}
	NativeRenderer_SetOffscreenState(&split->drawenv.clip, !drawOnScreen);
	NativeRenderer_SetProjection(&split->drawenv.clip, &split->dispenv, !drawOnScreen);

	if (split->psxTexturedSemiTrans)
	{
		// NOTE(aalhendi): CTR native renderer divergence from upstream PsyCross.
		// PS1 textured ABE only blends texels whose sampled 16-bit color has STP
		// set; non-STP texels remain opaque. Native split state is per draw,
		// so draw this primitive-sized split twice with shader-side STP masks.
		NativeRenderer_SetBlendMode(BM_NONE);
		NativeRenderer_SetPSXTextureSemiTransPass(1);
		NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);

		NativeRenderer_SetBlendMode(split->blendMode);
		NativeRenderer_SetPSXTextureSemiTransPass(2);
		NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);

		NativeRenderer_SetPSXTextureSemiTransPass(0);
	}
	else
	{
		NativeRenderer_SetBlendMode(split->blendMode);
		NativeRenderer_SetPSXTextureSemiTransPass(0);
		NativeRenderer_DrawTriangles(split->startVertex, split->numVerts / 3);
	}

	if (split->debugText)
	{
		NativeRenderer_PopDebugLabel();
	}
}

internal void SetPSXMaskState(u32 code)
{
	s_gpu.psxDrawMaskSet = (code & 1) != 0;
}

//
// Draws all polygons after AggregatePTAG
//
void DrawAllSplits()
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
	// CPU-originated LoadImage, MoveImage, and fill commands are GPU-visible
	// before the next draw batch, matching PS1 command ordering.
	NativeRenderer_UpdateVRAM();
#ifdef CTR_INTERNAL
	if (g_dbg_emulatorPaused)
	{
		for (int i = 0; i < 3; i++)
		{
			GrVertex *vert = &s_gpu.vertexBuffer[g_dbg_polygonSelected + i];
			vert->r = 255;
			vert->g = 0;
			vert->b = 0;

			NATIVE_GPU_LOG("%s\n", "==========================================");
			NATIVE_GPU_LOG("POLYGON: %d\n", g_dbg_polygonSelected);
			NATIVE_GPU_LOG("X: %d Y: %d\n", vert->x, vert->y);
			NATIVE_GPU_LOG("U: %d V: %d\n", vert->u, vert->v);
			NATIVE_GPU_LOG("TP: %d CLT: %d\n", vert->page, vert->clut);

			NATIVE_GPU_LOG("%s\n", "==========================================");
		}

		Platform_PollHostEvents();
	}
#endif

	// next code ideally should be called before EndScene
	NativeRenderer_UpdateVertexBuffer(s_gpu.vertexBuffer, s_gpu.vertexIndex);

	for (int i = 1; i <= s_gpu.splitIndex; i++)
	{
		DrawSplit(&s_gpu.splits[i]);
	}

	ClearSplits();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
}

// forward declarations
int ParsePrimitive(P_TAG *polyTag);
int ParseTaglessPrimitive(u32 *command);

internal bool NativeGpu_IsValidOTLink(uintptr_t link)
{
	if (NativeGpuLinks_IsRegisteredHostPointer((const void *)link))
	{
		return (link & (sizeof(u32) - 1)) == 0;
	}

	return false;
}

internal u32 NativeGpu_ReadPacketWordForLog(uintptr_t packet, int wordIndex)
{
	const struct PlatformMempackArena *arena = Platform_GetMempackArena();
	const uintptr_t word = packet + (uintptr_t)wordIndex * sizeof(u32);
	const uintptr_t end = word + sizeof(u32);

	if ((NativeGpuLinks_IsRegisteredHostRange((const void *)word, sizeof(u32))) && ((word & (sizeof(u32) - 1)) == 0))
	{
		return *(const u32 *)word;
	}

	if ((word < (uintptr_t)arena->base) || (end > (uintptr_t)arena->endOfMemory) || ((word & (sizeof(u32) - 1)) != 0))
	{
		return 0xffffffffu;
	}

	return *(const u32 *)word;
}

internal void NativeGpu_FormatPointerRegion(char *dst, size_t dstSize, uintptr_t ptr)
{
	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		snprintf(dst, dstSize, "no-gGT");
		return;
	}

	struct GameTracker *gGT = sdata->gGT;

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[playerIndex];
		const uintptr_t start = (uintptr_t)pb->ptrOT;
		const uintptr_t end = (uintptr_t)pb->renderBucketOTRangeEnd;
		if ((start != 0) && (end != 0) && (ptr >= start) && (ptr <= end))
		{
			snprintf(dst, dstSize, "pb%d.ot+0x%zx", playerIndex, (size_t)(ptr - start));
			return;
		}
	}

	struct PushBuffer *uiPB = &gGT->pushBuffer_UI;
	const uintptr_t uiStart = (uintptr_t)uiPB->ptrOT;
	const uintptr_t uiEnd = (uintptr_t)uiPB->renderBucketOTRangeEnd;
	if ((uiStart != 0) && (uiEnd != 0) && (ptr >= uiStart) && (ptr <= uiEnd))
	{
		snprintf(dst, dstSize, "ui.ot+0x%zx", (size_t)(ptr - uiStart));
		return;
	}

	for (int dbIndex = 0; dbIndex < 2; dbIndex++)
	{
		struct DB *db = &gGT->db[dbIndex];
		const uintptr_t primStart = (uintptr_t)db->primMem.start;
		const uintptr_t primEnd = (uintptr_t)db->primMem.end;
		if ((primStart != 0) && (ptr >= primStart) && (ptr < primEnd))
		{
			snprintf(dst, dstSize, "db%d.prim+0x%zx", dbIndex, (size_t)(ptr - primStart));
			return;
		}

		const uintptr_t otStart = (uintptr_t)db->otMem.start;
		const uintptr_t otEnd = (uintptr_t)db->otMem.end;
		if ((otStart != 0) && (ptr >= otStart) && (ptr < otEnd))
		{
			snprintf(dst, dstSize, "db%d.ot+0x%zx", dbIndex, (size_t)(ptr - otStart));
			return;
		}
	}

	snprintf(dst, dstSize, "unknown");
}

void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive)
{
	if (!p)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);

#ifdef CTR_NATIVE
	if (!singlePrimitive && !NativeGpuLinks_IsRegisteredHostPointer(p) && !isendprim(p))
	{
		char packetRegion[64];
		NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)p);
		NATIVE_GPU_ERROR("unregistered linked DrawOTag packet: packet=%p region=%s addr=%06x len=%d code=%02x words=%08x %08x %08x %08x\n", (void *)p,
		                 packetRegion, getaddr(p), getlen(p), getcode(p), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 0),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 2),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 3));
		NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
		return;
	}
#endif

	// setup single primitive flag (needed for AddSplits)
	s_gpu.drawPrimMode = singlePrimitive;
	s_inHudOT = false;
	s_primAnchored = false;
	s_primGroup = -1;
	s_primFullFrame = false;

	if (singlePrimitive)
	{
		P_TAG *polyTag = (P_TAG *)p;
		ParsePrimitive(polyTag);

		GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
		lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;
	}
	else
	{
		// walk OT_TAG linked list
		u8 *basePacket = (u8 *)p;
		while (true)
		{
			const int tagLength = getlen(basePacket);
			if (tagLength == 0)
			{
				NativeGpu_UpdateHudRegion(basePacket);
			}
			if (tagLength > 0)
			{
				if (tagLength > 32)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("got invalid tag length %d, code %d packet=%p region=%s words=%08x %08x %08x %08x\n", tagLength,
					                 ((P_TAG *)basePacket)->code, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
					break;
				}

				u8 *currentPacket = basePacket;
				u8 *endPacket = basePacket + (tagLength + P_LEN) * sizeof(u32);
				s_primAnchored = false;
				s_primGroup = -1;
				s_primFullFrame = false;
				if (s_inHudOT)
				{
					NativeGpu_ClassifyPacket(basePacket, &s_primAnchored, &s_primFullFrame);
				}
				int primLength = 0;
				if (currentPacket < endPacket)
				{
					primLength = ParsePrimitive((P_TAG *)currentPacket);
					currentPacket += (primLength + P_LEN) * sizeof(u32);
				}

				while (currentPacket < endPacket)
				{
					primLength = ParseTaglessPrimitive((u32 *)currentPacket);
					currentPacket += primLength * sizeof(u32);
				}

				if (currentPacket != endPacket)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("did not output valid primitive or ptag length is not valid (diff=%d packet=%p region=%s words=%08x %08x %08x %08x)\n",
					                 endPacket - currentPacket, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				}
			}

			GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
			lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;

			if (isendprim(basePacket))
			{
				break;
			}

			u8 *nextPacket = nextPrim(basePacket);
			if (!NativeGpu_IsValidOTLink((uintptr_t)nextPacket))
			{
				char packetRegion[64];
				char nextRegion[64];
				NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
				NativeGpu_FormatPointerRegion(nextRegion, sizeof(nextRegion), (uintptr_t)nextPacket);
				NATIVE_GPU_ERROR("invalid OT link: packet=%p region=%s addr=%06x next=%p nextRegion=%s len=%d code=%02x words=%08x %08x %08x %08x\n",
				                 (void *)basePacket, packetRegion, getaddr(basePacket), (void *)nextPacket, nextRegion, getlen(basePacket), getcode(basePacket),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				break;
			}

			basePacket = nextPacket;
		}
	}

	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
}

internal inline int IsNull(POLY_FT3 *poly)
{
	return poly->x0 == -1 && poly->y0 == -1 && poly->x1 == -1 && poly->y1 == -1 && poly->x2 == -1 && poly->y2 == -1;
}

internal int ProcessFlatLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_F2 *poly = (LINE_F2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = c0;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x8: // TODO (unused)
	{
		LINE_F3 *poly = (LINE_F3 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 5;
	}
	case 0xc:
	{
		LINE_F4 *poly = (LINE_F4 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x2;
			VERTTYPE *p1 = &poly->x3;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 6;
	}
	}
	return 0;
}

internal int ProcessGouraudLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_G2 *poly = (LINE_G2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = &poly->r1;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x8:
	{
		// TODO: LINE_G3
		return 7;
	}
	case 0xC:
	{
		// TODO: LINE_G4
		return 9;
	}
	}
	return 0;
}

internal int ProcessFlatPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_F3 *poly = (POLY_F3 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 0);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);

		s_gpu.vertexIndex += 3;

		return 4;
	}
	case 0x4:
	{
		POLY_FT3 *poly = (POLY_FT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		// It is an official hack from SCE devs to not use DR_TPAGE and instead use null polygon
		if (!IsNull(poly))
		{
			AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
			MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut,
			                     GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
			MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);

			s_gpu.vertexIndex += 3;
		}
		return 7;
	}
	case 0x8:
	{
		POLY_F4 *poly = (POLY_F4 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;
		return 5;
	}
	case 0xC:
	{
		POLY_FT4 *poly = (POLY_FT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
		                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 9;
	}
	}
	return 0;
}

internal int ProcessGouraudPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_G3 *poly = (POLY_G3 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 1);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);

		s_gpu.vertexIndex += 3;

		return 6;
	}
	case 0x4:
	{
		POLY_GT3 *poly = (POLY_GT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut, GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);

		s_gpu.vertexIndex += 3;

		return 9;
	}
	case 0x8:
	{
		POLY_G4 *poly = (POLY_G4 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 1);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 8;
	}
	case 0xC:
	{
		POLY_GT4 *poly = (POLY_GT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
		                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 12;
	}
	}
	return 0;
}

internal int ProcessTileAndSprt(P_TAG *polyTag)
{
	// NOTE: TILE does not support switching shadeTex on real PSX
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);

	switch (polyTag->code & 0xFD)
	{
	case 0x60:
	{
		TILE *poly = (TILE *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x64:
	{
		SPRT *poly = (SPRT *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, poly->w, poly->h);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x68:
	{
		TILE_1 *poly = (TILE_1 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 1, 1);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x70:
	{
		TILE_8 *poly = (TILE_8 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x74:
	{
		SPRT_8 *poly = (SPRT_8 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 8, 8);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x78:
	{
		TILE_16 *poly = (TILE_16 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x7C:
	{
		SPRT_16 *poly = (SPRT_16 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 16, 16);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	}
	return 0;
}

internal int ProcessDrawEnv(P_TAG *polyTag)
{
	const u32 *codePtr = (u32 *)&polyTag->pad0;
	int processedLongs = 0;
	bool fullDrawEnvPacket = false;
	for (int i = 0; i < polyTag->len; ++i)
	{
		const u32 code = codePtr[i];
		const int primType = code >> 24 & 0xF0;
		const int primSubType = code >> 24 & 0x0F;

		// NOTE(aalhendi): CTR can pack draw-env commands, tagless geometry,
		// and more draw-env commands into one OT entry. Stop at the first
		// non-E command so ParseTaglessPrimitive owns the geometry payload.
		if (primType != 0xE0)
		{
			return processedLongs;
		}

		switch (primSubType)
		{
		case 0x1:
		{
			// DR_TPAGE
			activeDrawEnv.tpage = (code & 0x1FF);
			activeDrawEnv.dtd = (code >> 9) & 1;
			// NOTE(aalhendi): Standalone DR_TPAGE packets use the same E1 word
			// for blend changes; only full DRAWENV packets retarget native
			// on-screen/offscreen rendering.
			if (fullDrawEnvPacket)
			{
				activeDrawEnv.dfe = (code >> 10) & 1;
			}
			break;
		}
		case 0x2:
		{
			// DR_TWIN
			activeDrawEnv.tw.w = (code & 0x1F);
			activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
			activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
			activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
			break;
		}
		case 0x3:
		{
			// DR_AREA
			activeDrawEnv.clip.x = code & 1023;
			activeDrawEnv.clip.y = (code >> 10) & 1023;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x4:
		{
			// DR_AREA (second part)
			activeDrawEnv.clip.w = code & 1023;
			activeDrawEnv.clip.h = (code >> 10) & 1023;

			activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
			activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x5:
		{
			// DR_OFFSET
			activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
			activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
			fullDrawEnvPacket = true;
			break;
		}
		case 0x6:
		{
			SetPSXMaskState(code);
			break;
		}
		case 0:
			// NOTE(aalhendi): ctr-native local divergence for CTR OTs. A zero
			// word can be terminal draw-env padding, or the tag word for the
			// next primitive packed into the same OT entry.
			// return processedLongs;
			if (i + 1 != polyTag->len)
			{
				return processedLongs;
			}
			break;
		}
		++processedLongs;
	}

	return processedLongs;
}

internal void ProcessDrawEnvCommand(u32 code)
{
	const int primSubType = code >> 24 & 0x0F;

	switch (primSubType)
	{
	case 0x1:
		activeDrawEnv.tpage = (code & 0x1FF);
		activeDrawEnv.dtd = (code >> 9) & 1;
		break;
	case 0x2:
		activeDrawEnv.tw.w = (code & 0x1F);
		activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
		activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
		activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
		break;
	case 0x3:
		activeDrawEnv.clip.x = code & 1023;
		activeDrawEnv.clip.y = (code >> 10) & 1023;
		break;
	case 0x4:
		activeDrawEnv.clip.w = code & 1023;
		activeDrawEnv.clip.h = (code >> 10) & 1023;
		activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
		activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
		break;
	case 0x5:
		activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
		activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
		break;
	case 0x6:
		SetPSXMaskState(code);
		break;
	}
}

internal int ProcessPsyXPrims(P_TAG *polyTag)
{
	const int primSubType = polyTag->code & 0x0F;

	switch (primSubType)
	{
	case 0x01:
	{
		DR_PSYX_TEX *psytex = (DR_PSYX_TEX *)polyTag;
		s_gpu.overrideTexture = psytex->code[0] & 0xFFFFFF;
		s_gpu.overrideTextureWidth = psytex->code[1] & 0xFFF;
		s_gpu.overrideTextureHeight = psytex->code[1] >> 16 & 0xFFF;
		return 2;
	}
	case 0x02:
	{
		// [A] Psy-X custom debug marker packet
		DR_PSYX_DBGMARKER *psydbg = (DR_PSYX_DBGMARKER *)polyTag;
		s_gpu.currentSplitDebugText = psydbg->text;
		return 2;
	}
	}

	return 0;
}

// Processes primitive
// returns processed primitive primLength in longs
int ParsePrimitive(P_TAG *polyTag)
{
	const int primType = polyTag->code & 0xF0;

	int primLength = 0;
	bool handledZeroLength = false;

	switch (primType)
	{
	case 0x00:
	{
		const int primSubType = polyTag->code & 0x0F;
		const u32 *codePtr = (u32 *)&polyTag->pad0;
		// NOTE(aalhendi): ctr-native local divergence. CTR RenderWeather can
		// emit a retail length-2 zero packet when weather is enabled but the
		// level has no fill-mode payload. The PSX consumes it by tag length;
		// the native parser must advance past it too.
		if (polyTag->len == 2 && codePtr[0] == 0 && codePtr[1] == 0)
		{
			primLength = 2;
		}
		else if (polyTag->len == 0 && *(u32 *)polyTag == 0)
		{
			// CTR ghost transparency packets include raw GPU NOP words between
			// draw-mode changes and triangle commands. They consume exactly one
			// command word; ParsePrimitivesLinkedList adds P_LEN to the return.
			handledZeroLength = true;
		}
		else if (primSubType == 0x0)
		{
			primLength = 3;
		}
		else if (primSubType == 0x1)
		{
			DR_MOVE *drmove = (DR_MOVE *)polyTag;
			const u32 rectPos = drmove->code[2];
			const u32 rectSize = drmove->code[4];

			const int y = drmove->code[3] >> 0x10 & 0xFFFF;
			const int x = drmove->code[3] & 0xFFFF;

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			MoveImage(&rect, x, y);
			primLength = 5;
		}
		else if (primSubType == 0x2)
		{
			// NOTE(aalhendi): ctr-native local divergence. CTR emits retail
			// FILL packets in OTs; the old PsyCross parser did not consume them, which caused
			// zero-length primitive spam.
			TILE *fill = (TILE *)polyTag;
			RECT16 rect;

			rect.x = fill->x0;
			rect.y = fill->y0;
			rect.w = fill->w;
			rect.h = fill->h;

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			ClearImage(&rect, fill->r0, fill->g0, fill->b0);
			primLength = 3;
		}
		break;
	}
	case 0x20:
		// Flat polygons
		primLength = ProcessFlatPoly(polyTag);
		break;
	case 0x30:
		// Gouraud shaded polygons
		primLength = ProcessGouraudPoly(polyTag);
		break;
	case 0x40:
		// Flat (single colour) Lines
		primLength = ProcessFlatLines(polyTag);
		break;
	case 0x50:
		// Gouraud lines
		primLength = ProcessGouraudLines(polyTag);
		break;
	case 0x60:
	case 0x70:
		// TILE and SPRT
		primLength = ProcessTileAndSprt(polyTag);
		break;
	case 0xA0:
		// DR_LOAD
		{
			DR_LOAD *drload = (DR_LOAD *)polyTag;
			const u32 rectPos = drload->code[1];
			const u32 rectSize = drload->code[2];

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			LoadImage(&rect, (uint32_t *)drload->p);

			// TODO(aalhendi): Audit whether CTR ever appends additional GPU
			// commands after a DR_LOAD payload in the same packet.
		}
		primLength = getlen(polyTag);
		break;
	case 0xB0:
		// [A] Psy-X custom primitives
		primLength = ProcessPsyXPrims(polyTag);
		break;
	case 0xE0:
		// Draw Env setup
		primLength = ProcessDrawEnv(polyTag);
		break;
		// default:
		//	NATIVE_GPU_ERROR("got %0x primitive\n", primType);
	}

	if (primLength == 0 && !handledZeroLength)
	{
		NATIVE_GPU_ERROR("Unhandled zero length %0x primitive\n", primType);
	}

	return primLength;
}

int ParseTaglessPrimitive(u32 *command)
{
	const u32 code = *command;
	const int primType = (code >> 24) & 0xF0;

	if (code == 0)
	{
		return 1;
	}

	if (primType == 0xE0)
	{
		ProcessDrawEnvCommand(code);
		return 1;
	}

	P_TAG *polyTag = (P_TAG *)(command - P_LEN);
	int primLength = ParsePrimitive(polyTag);

	if (primLength == 0)
	{
		NATIVE_GPU_ERROR("Unhandled tagless primitive %08x\n", code);
		return 1;
	}

	return primLength;
}
