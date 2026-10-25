#include "platform/native_big_field.h"
#include "platform/native_log.h"
#include "platform/native_settings.h"

#include <string.h>

// Host memory for a big-field race's pools and buffers (larger JitPools, prim
// memory, render-bucket list). Outside the mempack window, so retail allocations
// keep their addresses whatever this module does.
#define NATIVE_BIG_FIELD_ARENA_SIZE 0x200000

struct NativeBigField g_nativeBigField = {NATIVE_RETAIL_DRIVERS};

global_variable u8 s_bigFieldArena[NATIVE_BIG_FIELD_ARENA_SIZE];
global_variable int s_bigFieldArenaUsed;

internal int NativeBigField_IsEligible(struct GameTracker *gGT)
{
	u32 mode = gGT->gameMode1;

	if (!g_settings.game.bigField || (gGT->numPlyrCurrGame != 1) || gGT->boolDemoMode)
	{
		return 0;
	}

	// Races against bots only: no boss race (sign bit), time trial, relic, crystal
	// challenge, battle, hub, menu or cutscene.
	if ((mode & ADVENTURE_BOSS) != 0)
	{
		return 0;
	}

	if ((mode & (BATTLE_MODE | MAIN_MENU | TIME_TRIAL | ADVENTURE_ARENA | RELIC_RACE | CRYSTAL_CHALLENGE | GAME_CUTSCENE)) != 0)
	{
		return 0;
	}

	if ((mode & (ARCADE_MODE | ADVENTURE_MODE)) == 0)
	{
		return 0;
	}

	// The purple gem cup races the four bosses.
	if (((mode & ADVENTURE_CUP) != 0) && (gGT->cup.cupID == 4))
	{
		return 0;
	}

	return 1;
}

void NativeBigField_Latch(struct GameTracker *gGT)
{
	int cap = NativeBigField_IsEligible(gGT) ? NATIVE_MAX_DRIVERS : NATIVE_RETAIL_DRIVERS;

	if (cap != g_nativeBigField.driverCap)
	{
		Platform_Log("[CTR Native] Big field: %d driver slots\n", cap);
	}

	g_nativeBigField.driverCap = cap;
	s_bigFieldArenaUsed = 0;
	memset(g_nativeBigField.timesAttacking, 0, sizeof(g_nativeBigField.timesAttacking));
	memset(g_nativeBigField.timesAttackedBy, 0, sizeof(g_nativeBigField.timesAttackedBy));

	// Cup points carry over between a cup's tracks; none are awarded before the first
	// track ends, so a first track (or any non-cup race) starts from zero.
	if (gGT->cup.trackIndex == 0)
	{
		memset(g_nativeBigField.cupPoints, 0, sizeof(g_nativeBigField.cupPoints));
	}
}

void *NativeBigField_Alloc(int size)
{
	void *mem;

	size = (size + 7) & ~7;
	if ((size <= 0) || (s_bigFieldArenaUsed + size > NATIVE_BIG_FIELD_ARENA_SIZE))
	{
		Platform_LogError("[CTR Native] Big field: arena out of memory (%d of %d bytes used, %d asked)\n", s_bigFieldArenaUsed,
		                  NATIVE_BIG_FIELD_ARENA_SIZE, size);
		return NULL;
	}

	mem = &s_bigFieldArena[s_bigFieldArenaUsed];
	s_bigFieldArenaUsed += size;
	memset(mem, 0, size);
	return mem;
}

void NativeBigField_GetArena(void **ptr, u32 *size)
{
	*ptr = s_bigFieldArena;
	*size = NATIVE_BIG_FIELD_ON() ? (u32)s_bigFieldArenaUsed : 0;
}

// Driver in retail grid slot `slot`, or -1.
internal int NativeBigField_DriverInSlot(int slot)
{
	for (int i = 0; i < NATIVE_RETAIL_DRIVERS; i++)
	{
		if ((u8)sdata->kartSpawnOrderArray[i] == slot)
		{
			return i;
		}
	}

	return -1;
}

// Bots 8..23 reuse the seven opponents' characters, whose models are already in the
// race's pack: about three karts of each.
internal void NativeBigField_PickCharacters(void)
{
	for (int i = NATIVE_RETAIL_DRIVERS; i < NATIVE_MAX_DRIVERS; i++)
	{
		NATIVE_CHARACTER_ID(i) = data.characterIDs[1 + ((i - 1) % (NATIVE_RETAIL_DRIVERS - 1))];
	}
}

// Retail's cup rule over all 24 karts (BOTS_Adv_AdjustDifficulty skips its 8-kart
// version): the bot leading on points swaps accelerate order with the kart that has 0.
internal void NativeBigField_SwapCupLeader(struct GameTracker *gGT)
{
	int bestPoints = -1;
	int bestDriver = 0;
	int topDriver = 0;
	u8 swap;

	for (int i = 0; i < NATIVE_MAX_DRIVERS; i++)
	{
		if ((gGT->numPlyrCurrGame <= i) && (bestPoints < NATIVE_CUP_POINTS(gGT, i)))
		{
			bestPoints = (s16)NATIVE_CUP_POINTS(gGT, i);
			bestDriver = i;
		}

		if (NATIVE_ACCEL_ORDER(i) == 0)
		{
			topDriver = i;
		}
	}

	swap = NATIVE_ACCEL_ORDER(bestDriver);
	NATIVE_ACCEL_ORDER(bestDriver) = NATIVE_ACCEL_ORDER(topDriver);
	NATIVE_ACCEL_ORDER(topDriver) = swap;
}

void NativeBigField_AfterAdjustDifficulty(struct GameTracker *gGT)
{
	int newGrid;
	int slotPath[NATIVE_RETAIL_DRIVERS];
	int slotAccel[NATIVE_RETAIL_DRIVERS];

	if (!NATIVE_BIG_FIELD_ON())
	{
		return;
	}

	NativeBigField_PickCharacters();

	// Retail resets the grid and the accelerate order on a cup's first track only;
	// later tracks keep the grid MainGameEnd wrote from the last results.
	newGrid = (((gGT->gameMode1 & ADVENTURE_CUP) == 0) && ((gGT->gameMode2 & CUP_ANY_KIND) == 0)) || (gGT->cup.trackIndex == 0);

	if (newGrid)
	{
		// Extra karts line up behind retail's eight. A human retail put last (as
		// adventure does) moves to the new last slot; a restart finds it there already.
		int nextSlot = NATIVE_RETAIL_DRIVERS;

		for (int i = 0; i < gGT->numPlyrCurrGame; i++)
		{
			int slot = (u8)sdata->kartSpawnOrderArray[i];
			if ((slot == NATIVE_RETAIL_DRIVERS - 1) || (slot == NATIVE_MAX_DRIVERS - 1))
			{
				nextSlot = NATIVE_RETAIL_DRIVERS - 1;
				sdata->kartSpawnOrderArray[i] = NATIVE_MAX_DRIVERS - 1;
			}
		}

		for (int i = NATIVE_RETAIL_DRIVERS; i < NATIVE_MAX_DRIVERS; i++)
		{
			NATIVE_KART_SPAWN(i) = (char)nextSlot++;
		}
	}

	// Paths and acceleration repeat retail's pattern every 8 grid slots (2 rows of
	// 4), so each new row behaves like the retail row it extends.
	for (int slot = 0; slot < NATIVE_RETAIL_DRIVERS; slot++)
	{
		int driver = NativeBigField_DriverInSlot(slot);
		slotPath[slot] = (driver >= 0) ? sdata->driver_pathIndexIDs[driver] : (slot & 3) % 3;
		slotAccel[slot] = (driver >= 0) ? sdata->accelerateOrder[driver] : slot;
	}

	for (int i = NATIVE_RETAIL_DRIVERS; i < NATIVE_MAX_DRIVERS; i++)
	{
		int slot = (u8)NATIVE_KART_SPAWN(i);
		NATIVE_DRIVER_PATH(i) = (char)slotPath[slot % NATIVE_RETAIL_DRIVERS];

		// Per driver, like retail's; cups keep it from the first track.
		if (newGrid)
		{
			NATIVE_ACCEL_ORDER(i) = (u8)(slotAccel[slot % NATIVE_RETAIL_DRIVERS] + (slot / NATIVE_RETAIL_DRIVERS) * NATIVE_RETAIL_DRIVERS);
		}
	}

	if (!newGrid)
	{
		NativeBigField_SwapCupLeader(gGT);
	}
}

// Slot index >= 8: the level's two rows of 4 (slots 0..3 and 4..7), extended
// backwards by the same spacing.
internal void NativeBigField_ExtrapolateSpawn(struct Level *level, int index)
{
	int column = index & 3;
	int rowsBack = (index / 4) - 1;
	const SVec3 *front = &level->DriverSpawn[column].pos;
	const SVec3 *rear = &level->DriverSpawn[4 + column].pos;
	int extIndex = index - NATIVE_RETAIL_DRIVERS;

	g_nativeBigField.spawn[extIndex].pos.x = (s16)(rear->x + rowsBack * (rear->x - front->x));
	g_nativeBigField.spawn[extIndex].pos.y = (s16)(rear->y + rowsBack * (rear->y - front->y));
	g_nativeBigField.spawn[extIndex].pos.z = (s16)(rear->z + rowsBack * (rear->z - front->z));
	g_nativeBigField.spawn[extIndex].rot = level->DriverSpawn[4 + column].rot;
}

const SVec3 *NativeBigField_SpawnPos(struct Level *level, int index)
{
	if (index < NATIVE_RETAIL_DRIVERS)
	{
		return &level->DriverSpawn[index].pos;
	}

	NativeBigField_ExtrapolateSpawn(level, index);
	return &g_nativeBigField.spawn[index - NATIVE_RETAIL_DRIVERS].pos;
}

const SVec3 *NativeBigField_SpawnRot(struct Level *level, int index)
{
	if (index < NATIVE_RETAIL_DRIVERS)
	{
		return &level->DriverSpawn[index].rot;
	}

	NativeBigField_ExtrapolateSpawn(level, index);
	return &g_nativeBigField.spawn[index - NATIVE_RETAIL_DRIVERS].rot;
}

// Ground shadows for a big field: VehGroundShadow_DrawBatch (game/Vehicle/VehGroundShadow.c,
// same unity build) draws 8 drivers from s_groundShadowDriverBase, the most its scratchpad
// table holds. Without a big field NATIVE_DRIVER_CAP is 8 and this is one retail pass.
void VehGroundShadow_Main(void)
{
	for (int base = 0; base < NATIVE_DRIVER_CAP; base += VEH_GROUND_SHADOW_MAX_DRIVERS)
	{
		s_groundShadowDriverBase = base;
		VehGroundShadow_DrawBatch();
	}

	s_groundShadowDriverBase = 0;
}

// Cup ranks for up to 24 drivers. Retail's versions (game/UI/UI_CupRanks.c, PS1 only)
// keep an 8-bit mask of ranked drivers and read gGT->cup.points directly; these read
// cup points and positions through the GAME_* accessors.
void UI_CupStandings_FinalizeCupRanks(void)
{
	struct GameTracker *gGT;
	int numDrivers;
	int tiedTopCount;
	int topScore;
	int bestRank;
	int selectedRankSlot;

	bestRank = 99;
	gGT = sdata->gGT;
	selectedRankSlot = -1;

	numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	if (numDrivers >= 5)
	{
		numDrivers = 4;
	}

	tiedTopCount = 0;
	if (1 < numDrivers)
	{
		topScore = GAME_CUP_POINTS(gGT, data.cupPositionPerPlayer[0]);

		for (int i = 1; i < numDrivers; i++)
		{
			if (GAME_CUP_POINTS(gGT, data.cupPositionPerPlayer[i]) != topScore)
			{
				break;
			}

			tiedTopCount++;
		}
	}

	for (int rankSlot = 0; rankSlot < tiedTopCount + 1; rankSlot++)
	{
		for (int i = rankSlot; i < tiedTopCount + 1; i++)
		{
			struct Driver *driver;
			driver = GAME_DRIVER(gGT, data.cupPositionPerPlayer[i]);

			if (driver->driverRank < (s16)bestRank)
			{
				bestRank = (u16)driver->driverRank;
				selectedRankSlot = i;
			}
		}

		int previousRankValue = (u16)data.cupPositionPerPlayer[rankSlot];
		data.cupPositionPerPlayer[rankSlot] = (s16)selectedRankSlot;
		data.cupPositionPerPlayer[selectedRankSlot] = (s16)previousRankValue;

		bestRank = 99;
	}
}

void UI_CupStandings_UpdateCupRanks(void)
{
	struct GameTracker *gGT;
	int bestScore;
	int bestIndex;
	int assignedMask;
	int numDrivers;

	bestScore = 0;
	bestIndex = -1;
	gGT = sdata->gGT;
	assignedMask = 0;

	numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	if (numDrivers == 0)
	{
		return;
	}

	for (int rankSlot = 0; rankSlot < numDrivers; rankSlot++)
	{
		for (int driverIndex = numDrivers - 1; driverIndex >= 0; driverIndex--)
		{
			if ((GAME_CUP_POINTS(gGT, driverIndex) >= (s16)bestScore) && (((assignedMask >> driverIndex) & 1) == 0))
			{
				bestScore = (u16)GAME_CUP_POINTS(gGT, driverIndex);

				if ((s16)bestIndex != -1)
				{
					assignedMask &= ~(1 << bestIndex);
				}

				bestIndex = driverIndex;
				assignedMask |= 1 << driverIndex;
			}
		}

		GAME_CUP_POSITION(rankSlot) = (s16)bestIndex;
		bestScore = 0;
		bestIndex = -1;
	}
}
