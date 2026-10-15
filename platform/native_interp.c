#include "platform/native_interp.h"

#include <common.h>
#include <macros.h>
#include <platform.h>

#include "ctr_scratchpad.h"
#include "platform/native_gpu.h"
#include "platform/native_hooks.h"
#include "platform/native_memory.h"
#include "platform/native_settings.h"
#if defined(CTR_INTERNAL)
#include "platform/native_replay_scheduler.h"
#endif

#include <SDL3/SDL.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// NOTE: Frame interpolation. See native_interp.h for the overview.
//
// Timeline at 60 fps (V = emulated VBlank, ~59.8 Hz):
//   V0  real frame k-1 presented        -> NativeInterp_EndTick records its transforms
//       logic tick k runs
//   V1  in-between frame, alpha 0.5     -> NativeInterp_RenderBetweenTicks
//   V2  real frame k presented
// Higher rates split the V0..V2 span into more evenly spaced frames.

#define NATIVE_INTERP_MAX_INSTANCES 1024
// Power of two, at least twice NATIVE_INTERP_MAX_INSTANCES.
#define NATIVE_INTERP_HASH_SIZE     2048
#define NATIVE_INTERP_MAX_FRAMES    16
#define NATIVE_INTERP_ALPHA_ONE     0x1000
// Moves larger than this in one tick are cuts (respawn, warp, camera change, a
// reused pool slot): those objects are drawn at the current tick, not blended.
#define NATIVE_INTERP_SNAP_DISTANCE 0x400
// 0x1000 is a full turn, so this is 45 degrees.
#define NATIVE_INTERP_SNAP_ANGLE    0x200
#define NATIVE_INTERP_PLAYERS       4
#define NATIVE_INTERP_DRIVERS       NATIVE_MAX_DRIVERS

struct NativeInterpInstance
{
	struct Instance *inst;
	struct Model *model;
	MATRIX matrix;
	b32 used;
};

struct NativeInterpDriver
{
	struct Driver *driver;
	Vec3 posCurr;
	int quadBlockHeight;
};

struct NativeInterpCamera
{
	SVec3 pos;
	SVec3 rot;
};

// Transforms the last real frame was drawn with.
struct NativeInterpTick
{
	b32 valid;
	int frameCounter;
	struct Level *level;
	int numPlyr;
	// VBlank count when the frame was presented.
	int presentVBlank;
	struct NativeInterpCamera cameras[NATIVE_INTERP_PLAYERS];
	struct NativeInterpDriver drivers[NATIVE_INTERP_DRIVERS];
	int instanceCount;
	struct NativeInterpInstance instances[NATIVE_INTERP_MAX_INSTANCES];
	// Index + 1 into instances, 0 = empty.
	s16 hash[NATIVE_INTERP_HASH_SIZE];
};

struct NativeInterpRegion
{
	void *ptr;
	u32 size;
};

global_variable struct NativeInterpTick s_prevTick;
global_variable b32 s_interpRenderPass;
// Frames owed to the window, in thirtieths of a frame (see NativeInterp_RenderBetweenTicks).
global_variable int s_frameCredit;
global_variable u8 *s_interpSnapshot;
global_variable u32 s_interpSnapshotSize;

int NativeInterp_InRenderPass(void)
{
	return s_interpRenderPass;
}

//------------------------------------------------------------------------------------------------------------------------
// Game memory snapshot

// Everything the game mutates, as in the checkpoint system's region list, plus
// the host-side GTE registers and BIOS rand seed that game code also carries
// from one call to the next.
internal int NativeInterp_GetRegions(struct NativeInterpRegion *regions)
{
	int count = 0;

#define NATIVE_INTERP_REGION(p, s)          \
	do                                      \
	{                                       \
		regions[count].ptr = (void *)(p);   \
		regions[count].size = (u32)(s);     \
		count++;                            \
	} while (0)

	NATIVE_INTERP_REGION(&rdata, sizeof(rdata));
	NATIVE_INTERP_REGION(&data, sizeof(data));
	NATIVE_INTERP_REGION(&sdata_static, sizeof(sdata_static));
	NATIVE_INTERP_REGION(&D230, sizeof(D230));
	NATIVE_INTERP_REGION(&V230, sizeof(V230));
	NATIVE_INTERP_REGION(&D231, sizeof(D231));
	NATIVE_INTERP_REGION(&D232, sizeof(D232));
	NATIVE_INTERP_REGION(&D233, sizeof(D233));
	NATIVE_INTERP_REGION(&gGarage, sizeof(gGarage));
	NATIVE_INTERP_REGION(&creditsBSS, sizeof(creditsBSS));
	NATIVE_INTERP_REGION(Platform_GetMempackBacking(), Platform_GetMempackBackingSize());
	NATIVE_INTERP_REGION(CTR_SCRATCHPAD_BASE, CTR_SCRATCHPAD_SIZE);
	NATIVE_INTERP_REGION(&gteRegs, sizeof(gteRegs));

#undef NATIVE_INTERP_REGION

	return count;
}

#define NATIVE_INTERP_MAX_REGIONS 16

internal b32 NativeInterp_SaveSnapshot(void)
{
	struct NativeInterpRegion regions[NATIVE_INTERP_MAX_REGIONS];
	const int count = NativeInterp_GetRegions(regions);

	if (s_interpSnapshot == NULL)
	{
		u32 total = 0;
		for (int i = 0; i < count; i++)
		{
			total += regions[i].size;
		}

		s_interpSnapshot = (u8 *)malloc(total);
		if (s_interpSnapshot == NULL)
		{
			return false;
		}
		s_interpSnapshotSize = total;
	}

	u8 *cursor = s_interpSnapshot;
	for (int i = 0; i < count; i++)
	{
		memcpy(cursor, regions[i].ptr, regions[i].size);
		cursor += regions[i].size;
	}

	return true;
}

internal void NativeInterp_RestoreSnapshot(void)
{
	struct NativeInterpRegion regions[NATIVE_INTERP_MAX_REGIONS];
	const int count = NativeInterp_GetRegions(regions);

	const u8 *cursor = s_interpSnapshot;
	for (int i = 0; i < count; i++)
	{
		memcpy(regions[i].ptr, cursor, regions[i].size);
		cursor += regions[i].size;
	}
}

//------------------------------------------------------------------------------------------------------------------------
// Transform capture

internal u32 NativeInterp_HashSlot(const struct Instance *inst)
{
	return ((u32)(uintptr_t)inst * 2654435761u) >> (32 - 11);
}

CTR_STATIC_ASSERT(NATIVE_INTERP_HASH_SIZE == (1 << 11));

internal struct NativeInterpInstance *NativeInterp_FindInstance(struct NativeInterpTick *tick, const struct Instance *inst)
{
	u32 slot = NativeInterp_HashSlot(inst);

	while (tick->hash[slot] != 0)
	{
		struct NativeInterpInstance *entry = &tick->instances[tick->hash[slot] - 1];
		if (entry->inst == inst)
		{
			return entry;
		}
		slot = (slot + 1) & (NATIVE_INTERP_HASH_SIZE - 1);
	}

	return NULL;
}

internal void NativeInterp_AddInstance(struct NativeInterpTick *tick, struct Instance *inst)
{
	if ((inst == NULL) || (tick->instanceCount >= NATIVE_INTERP_MAX_INSTANCES))
	{
		return;
	}

	u32 slot = NativeInterp_HashSlot(inst);
	while (tick->hash[slot] != 0)
	{
		if (tick->instances[tick->hash[slot] - 1].inst == inst)
		{
			return;
		}
		slot = (slot + 1) & (NATIVE_INTERP_HASH_SIZE - 1);
	}

	struct NativeInterpInstance *entry = &tick->instances[tick->instanceCount++];
	entry->inst = inst;
	entry->model = inst->model;
	entry->matrix = inst->matrix;
	entry->used = false;
	tick->hash[slot] = (s16)tick->instanceCount;
}

// Instances are reached two ways: the instance pool's taken list (threads,
// pickups, HUD models) and the level's InstDefs (placed objects). Calls `visit`
// once per list entry; an instance can appear in both.
#define NATIVE_INTERP_FOR_EACH_INSTANCE(gGT, inst, body)                                                   \
	do                                                                                                     \
	{                                                                                                      \
		for (struct Item *item_ = (gGT)->JitPools.instance.taken.first; item_ != NULL; item_ = item_->next) \
		{                                                                                                  \
			struct Instance *inst = (struct Instance *)item_;                                              \
			body;                                                                                          \
		}                                                                                                  \
		struct Level *level_ = (gGT)->level1;                                                              \
		if ((level_ != NULL) && (level_->ptrInstDefs != NULL))                                             \
		{                                                                                                  \
			for (u32 i_ = 0; i_ < level_->numInstances; i_++)                                              \
			{                                                                                              \
				struct Instance *inst = level_->ptrInstDefs[i_].ptrInstance;                               \
				if (inst != NULL)                                                                          \
				{                                                                                          \
					body;                                                                                  \
				}                                                                                          \
			}                                                                                              \
		}                                                                                                  \
	} while (0)

void NativeInterp_EndTick(struct GameTracker *gGT)
{
	struct NativeInterpTick *tick = &s_prevTick;

	tick->valid = (Platform_GetTargetFrameRate() > NATIVE_FRAME_RATE_ORIGINAL) && (gGT->level1 != NULL);
	if (!tick->valid)
	{
		return;
	}

	tick->frameCounter = sdata->frameCounter;
	tick->level = gGT->level1;
	tick->numPlyr = gGT->numPlyrCurrGame;
	tick->presentVBlank = Platform_GetVBlankCount();

	for (int i = 0; i < NATIVE_INTERP_PLAYERS; i++)
	{
		tick->cameras[i].pos = gGT->pushBuffer[i].pos;
		tick->cameras[i].rot = gGT->pushBuffer[i].rot;
	}

	for (int i = 0; i < NATIVE_DRIVER_CAP; i++)
	{
		struct Driver *d = NATIVE_DRIVER(gGT, i);
		tick->drivers[i].driver = d;
		if (d != NULL)
		{
			tick->drivers[i].posCurr = d->posCurr;
			tick->drivers[i].quadBlockHeight = d->quadBlockHeight;
		}
	}

	tick->instanceCount = 0;
	memset(tick->hash, 0, sizeof(tick->hash));
	NATIVE_INTERP_FOR_EACH_INSTANCE(gGT, inst, NativeInterp_AddInstance(tick, inst));
}

//------------------------------------------------------------------------------------------------------------------------
// Blending

internal s32 NativeInterp_Lerp(s32 from, s32 to, int alpha)
{
	return from + (s32)(((s64)to - (s64)from) * alpha / NATIVE_INTERP_ALPHA_ONE);
}

internal s32 NativeInterp_AngleDelta(s16 from, s16 to)
{
	return (((s32)to - (s32)from + 0x800) & 0xfff) - 0x800;
}

internal s32 NativeInterp_Abs(s32 value)
{
	return (value < 0) ? -value : value;
}

internal b32 NativeInterp_IsCut(s32 dx, s32 dy, s32 dz, s32 limit)
{
	return (NativeInterp_Abs(dx) > limit) || (NativeInterp_Abs(dy) > limit) || (NativeInterp_Abs(dz) > limit);
}

internal void NativeInterp_BlendCamera(struct PushBuffer *pb, const struct NativeInterpCamera *prev, int alpha)
{
	const s32 drx = NativeInterp_AngleDelta(prev->rot.x, pb->rot.x);
	const s32 dry = NativeInterp_AngleDelta(prev->rot.y, pb->rot.y);
	const s32 drz = NativeInterp_AngleDelta(prev->rot.z, pb->rot.z);

	if (NativeInterp_IsCut(pb->pos.x - prev->pos.x, pb->pos.y - prev->pos.y, pb->pos.z - prev->pos.z, NATIVE_INTERP_SNAP_DISTANCE) ||
	    NativeInterp_IsCut(drx, dry, drz, NATIVE_INTERP_SNAP_ANGLE))
	{
		return;
	}

	pb->pos.x = (s16)NativeInterp_Lerp(prev->pos.x, pb->pos.x, alpha);
	pb->pos.y = (s16)NativeInterp_Lerp(prev->pos.y, pb->pos.y, alpha);
	pb->pos.z = (s16)NativeInterp_Lerp(prev->pos.z, pb->pos.z, alpha);
	pb->rot.x = (s16)(prev->rot.x + drx * alpha / NATIVE_INTERP_ALPHA_ONE);
	pb->rot.y = (s16)(prev->rot.y + dry * alpha / NATIVE_INTERP_ALPHA_ONE);
	pb->rot.z = (s16)(prev->rot.z + drz * alpha / NATIVE_INTERP_ALPHA_ONE);
}

internal void NativeInterp_BlendInstance(struct Instance *inst, int alpha)
{
	struct NativeInterpInstance *prev = NativeInterp_FindInstance(&s_prevTick, inst);

	if ((prev == NULL) || prev->used || (prev->model != inst->model))
	{
		return;
	}
	prev->used = true;

	MATRIX *m = &inst->matrix;
	if (NativeInterp_IsCut(m->t[0] - prev->matrix.t[0], m->t[1] - prev->matrix.t[1], m->t[2] - prev->matrix.t[2], NATIVE_INTERP_SNAP_DISTANCE))
	{
		return;
	}

	// Element-wise blend. Between two 30 Hz ticks the rotation changes little, so
	// the slight shrink this gives a blended rotation is not visible.
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
		{
			m->m[r][c] = (s16)NativeInterp_Lerp(prev->matrix.m[r][c], m->m[r][c], alpha);
		}
		m->t[r] = NativeInterp_Lerp(prev->matrix.t[r], m->t[r], alpha);
	}
}

internal void NativeInterp_BlendDriver(struct Driver *d, const struct NativeInterpDriver *prev, int alpha)
{
	// posCurr is in 1/256 world units.
	if (NativeInterp_IsCut(d->posCurr.x - prev->posCurr.x, d->posCurr.y - prev->posCurr.y, d->posCurr.z - prev->posCurr.z,
	                       NATIVE_INTERP_SNAP_DISTANCE << 8))
	{
		return;
	}

	d->posCurr.x = NativeInterp_Lerp(prev->posCurr.x, d->posCurr.x, alpha);
	d->posCurr.y = NativeInterp_Lerp(prev->posCurr.y, d->posCurr.y, alpha);
	d->posCurr.z = NativeInterp_Lerp(prev->posCurr.z, d->posCurr.z, alpha);
	d->quadBlockHeight = NativeInterp_Lerp(prev->quadBlockHeight, d->quadBlockHeight, alpha);
}

// Writes blended transforms into live game memory. Only called inside a snapshot.
internal void NativeInterp_ApplyBlend(struct GameTracker *gGT, int alpha)
{
	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		NativeInterp_BlendCamera(&gGT->pushBuffer[i], &s_prevTick.cameras[i], alpha);
	}

	for (int i = 0; i < NATIVE_DRIVER_CAP; i++)
	{
		struct Driver *d = NATIVE_DRIVER(gGT, i);
		if ((d != NULL) && (s_prevTick.drivers[i].driver == d))
		{
			NativeInterp_BlendDriver(d, &s_prevTick.drivers[i], alpha);
		}
	}

	for (int i = 0; i < s_prevTick.instanceCount; i++)
	{
		s_prevTick.instances[i].used = false;
	}
	NATIVE_INTERP_FOR_EACH_INSTANCE(gGT, inst, NativeInterp_BlendInstance(inst, alpha));
}

//------------------------------------------------------------------------------------------------------------------------
// In-between frames

// The previous tick must be the one just before this, in the same scene, and the
// frame must be plain gameplay: menus mix input handling into their drawing, and
// loading screens drive the CD, so both stay at 30 fps.
internal b32 NativeInterp_CanBlend(struct GameTracker *gGT)
{
	if (!s_prevTick.valid || (s_prevTick.frameCounter + 1 != sdata->frameCounter) || (s_prevTick.level != gGT->level1) ||
	    (s_prevTick.numPlyr != gGT->numPlyrCurrGame))
	{
		return false;
	}

	if ((gGT->gameMode1 & (LOADING | PAUSE_ALL)) != 0)
	{
		return false;
	}

	if ((sdata->Loading.stage != LOAD_IDLE) || (sdata->ptrActiveMenu != NULL) || (sdata->ptrDesiredMenu != NULL))
	{
		return false;
	}

	return !Platform_IsVRAMDisplayPinned();
}

internal void NativeInterp_DrawFrame(struct GameTracker *gGT, struct GamepadSystem *gGamepads, int alpha)
{
	const u32 randSeed = PSX_BIOS_GetRandSeed();
	const int anchorCount = NativeGpu_GetHudAnchorCount();

	s_interpRenderPass = 1;
	NativeInterp_ApplyBlend(gGT, alpha);

	Platform_BeginFrame();
	MainFrame_RenderFrame(gGT, gGamepads);

	// As RenderSubmit: draw into the back buffer, display it.
	PutDrawEnv(&gGT->backBuffer->drawEnv);
	PutDispEnv(&gGT->db[1 - gGT->swapchainIndex].dispEnv);
	DrawOTag(&gGT->pushBuffer[0].ptrOT[0x3ff]);
	s_interpRenderPass = 0;

	NativeGpu_SetHudAnchorCount(anchorCount);
	PSX_BIOS_SetRandSeed(randSeed);
}

void NativeInterp_RenderBetweenTicks(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	const int frameRate = Platform_GetTargetFrameRate();

	if ((frameRate <= NATIVE_FRAME_RATE_ORIGINAL) || !NativeInterp_CanBlend(gGT))
	{
		s_frameCredit = 0;
		return;
	}

	// The real frame flips once vsyncTillFlip VBlanks have passed.
	const int vblank = Platform_GetVBlankCount();
	const int flipVBlank = vblank + sdata->vsyncTillFlip;
	if ((sdata->vsyncTillFlip <= 0) || (flipVBlank <= s_prevTick.presentVBlank))
	{
		return;
	}

	const u64 spanStart = Platform_GetVBlankTime(s_prevTick.presentVBlank);
	const u64 spanEnd = Platform_GetVBlankTime(flipVBlank);
	const u64 span = spanEnd - spanStart;

	// Frames this tick, the real one included, evenly spaced within it. Rates that
	// aren't a multiple of 30 carry the remainder to later ticks: 144 fps shows
	// 5, 5, 5, 5, 4 frames per tick. Ticks longer than one 30 Hz period (lag,
	// vsyncTillFlip > 2) get proportionally more.
	const u64 tickPeriod = SDL_GetPerformanceFrequency() / NATIVE_FRAME_RATE_ORIGINAL;
	const int ticks = SDL_max(1, (int)((span + tickPeriod / 2) / tickPeriod));
	s_frameCredit += frameRate * ticks;
	int frames = s_frameCredit / NATIVE_FRAME_RATE_ORIGINAL;
	s_frameCredit -= frames * NATIVE_FRAME_RATE_ORIGINAL;
	if (frames > NATIVE_INTERP_MAX_FRAMES)
	{
		frames = NATIVE_INTERP_MAX_FRAMES;
	}

	b32 paced = true;
#if defined(CTR_INTERNAL)
	// Replays count every VBlank, so draw the in-between frames without waiting
	// for (and emitting) VBlanks. This keeps playback a test that interpolation
	// leaves the simulation untouched.
	paced = !NativeReplayScheduler_IsActive();
#endif

	const DRAWENV savedDrawEnv = activeDrawEnv;
	const DISPENV savedDispEnv = activeDispEnv;

	for (int i = 1; i < frames; i++)
	{
		const u64 presentAt = spanStart + (span * (u64)i) / (u64)frames;

		if (paced && (SDL_GetPerformanceCounter() >= presentAt))
		{
			// Too late for this one (slow host); skip to the next.
			continue;
		}

		if (!NativeInterp_SaveSnapshot())
		{
			return;
		}
		NativeInterp_DrawFrame(gGT, gGamepads, (i * NATIVE_INTERP_ALPHA_ONE) / frames);
		NativeInterp_RestoreSnapshot();

		if (paced)
		{
			Platform_WaitUntilTime(presentAt);
		}
		Platform_EndFrame();
	}

	activeDrawEnv = savedDrawEnv;
	activeDispEnv = savedDispEnv;
}

void NativeInterp_RegisterHooks(void)
{
	NativeHook_RegisterBeforeDraw(NativeInterp_RenderBetweenTicks);
	NativeHook_RegisterAfterPresent(NativeInterp_EndTick);
}
