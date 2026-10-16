#ifndef NATIVE_BIG_FIELD_H
#define NATIVE_BIG_FIELD_H

// Big field (game.big_field): 1-player races with 24 karts, 23 of them bots.
//
// Retail sizes everything for 8 drivers. The per-driver tables in struct Data and
// sData can't grow (their offsets are asserted), so they keep slots 0..7 and the
// extra slots 8..23 live here; the NATIVE_* accessors below pick the right one by
// index. With the field off (or in a mode it doesn't apply to) the cap is 8, no
// index reaches the extension and every accessor reads the retail table.
//
// Included from common.h on native only; game code uses it inside CTR_NATIVE.

#define NATIVE_RETAIL_DRIVERS 8
#define NATIVE_MAX_DRIVERS    24
#define NATIVE_EXTRA_DRIVERS  (NATIVE_MAX_DRIVERS - NATIVE_RETAIL_DRIVERS)


struct GameTracker;
struct Level;
struct Driver;

struct NativeBigField
{
	// Number of driver slots this race uses: 8, or 24 in a big-field race. Latched
	// when the level's pools are set up, so changing the setting waits for a load.
	int driverCap;

	// Extension of retail's 8-entry tables, for slots 8..23. GameTracker's driver
	// arrays can't grow either: sData's later fields are asserted at retail offsets.
	struct Driver *drivers[NATIVE_EXTRA_DRIVERS];
	struct Driver *driversInRaceOrder[NATIVE_EXTRA_DRIVERS];
	s16 characterIDs[NATIVE_EXTRA_DRIVERS];
	s16 characterIDsBackup[NATIVE_EXTRA_DRIVERS];
	char kartSpawnOrder[NATIVE_EXTRA_DRIVERS];
	u8 accelerateOrder[NATIVE_EXTRA_DRIVERS];
	char pathIndex[NATIVE_EXTRA_DRIVERS];
	s16 rankIconsDesired[NATIVE_EXTRA_DRIVERS];
	s16 rankIconsCurr[NATIVE_EXTRA_DRIVERS];
	s16 rankIconsTransitionTimer[NATIVE_EXTRA_DRIVERS];
	int cupPositionPerPlayer[NATIVE_EXTRA_DRIVERS];
	// GameTracker.cup.points and humanPlayerPositions, which sit mid-struct.
	int cupPoints[NATIVE_EXTRA_DRIVERS];
	u8 humanPlayerPositions[NATIVE_EXTRA_DRIVERS];

	// Start grid slots 8..23, extrapolated from the level's two rows.
	struct
	{
		SVec3 pos;
		SVec3 rot;
	} spawn[NATIVE_EXTRA_DRIVERS];

	// Driver.numTimesAttackingPlayer / numTimesAttackedByPlayer for the pairs the
	// Driver struct's 8-entry arrays can't hold: [driverID][otherDriverID].
	u8 timesAttacking[NATIVE_MAX_DRIVERS][NATIVE_MAX_DRIVERS];
	u8 timesAttackedBy[NATIVE_MAX_DRIVERS][NATIVE_MAX_DRIVERS];
};

extern struct NativeBigField g_nativeBigField;

#define NATIVE_DRIVER_CAP     (g_nativeBigField.driverCap)
#define NATIVE_BIG_FIELD_ON() (g_nativeBigField.driverCap > NATIVE_RETAIL_DRIVERS)

#define NATIVE_BF_SLOT(retail, ext, i) (*(((i) < NATIVE_RETAIL_DRIVERS) ? &(retail)[(i)] : &(ext)[(i) - NATIVE_RETAIL_DRIVERS]))

#define NATIVE_DRIVER(gGT, i)               NATIVE_BF_SLOT((gGT)->drivers, g_nativeBigField.drivers, (i))
#define NATIVE_DRIVER_IN_RACE_ORDER(gGT, i) NATIVE_BF_SLOT((gGT)->driversInRaceOrder, g_nativeBigField.driversInRaceOrder, (i))
#define NATIVE_CHARACTER_ID(i)        NATIVE_BF_SLOT(data.characterIDs, g_nativeBigField.characterIDs, (i))
#define NATIVE_CHARACTER_ID_BACKUP(i) NATIVE_BF_SLOT(sdata->characterIDs_backup, g_nativeBigField.characterIDsBackup, (i))
#define NATIVE_KART_SPAWN(i)          NATIVE_BF_SLOT(sdata->kartSpawnOrderArray, g_nativeBigField.kartSpawnOrder, (i))
#define NATIVE_ACCEL_ORDER(i)         NATIVE_BF_SLOT(sdata->accelerateOrder, g_nativeBigField.accelerateOrder, (i))
#define NATIVE_DRIVER_PATH(i)         NATIVE_BF_SLOT(sdata->driver_pathIndexIDs, g_nativeBigField.pathIndex, (i))
#define NATIVE_RANK_ICON_DESIRED(i)   NATIVE_BF_SLOT(data.rankIconsDesired, g_nativeBigField.rankIconsDesired, (i))
#define NATIVE_RANK_ICON_CURR(i)      NATIVE_BF_SLOT(data.rankIconsCurr, g_nativeBigField.rankIconsCurr, (i))
#define NATIVE_RANK_ICON_TIMER(i)     NATIVE_BF_SLOT(data.rankIconsTransitionTimer, g_nativeBigField.rankIconsTransitionTimer, (i))
#define NATIVE_CUP_POSITION(i)        NATIVE_BF_SLOT(data.cupPositionPerPlayer, g_nativeBigField.cupPositionPerPlayer, (i))
#define NATIVE_CUP_POINTS(gGT, i)     NATIVE_BF_SLOT((gGT)->cup.points, g_nativeBigField.cupPoints, (i))
#define NATIVE_HUMAN_POSITION(gGT, i) NATIVE_BF_SLOT((gGT)->humanPlayerPositions, g_nativeBigField.humanPlayerPositions, (i))

// Driver d's attack counter against driver other, through the Driver struct below 8.
#define NATIVE_TIMES_ATTACKING(d, other)                                                                                   \
	(*(((other) < NATIVE_RETAIL_DRIVERS && (d)->driverID < NATIVE_RETAIL_DRIVERS) ? &(d)->numTimesAttackingPlayer[(other)] \
	                                                                               : &g_nativeBigField.timesAttacking[(d)->driverID][(other)]))
#define NATIVE_TIMES_ATTACKED_BY(d, other)                                                                                  \
	(*(((other) < NATIVE_RETAIL_DRIVERS && (d)->driverID < NATIVE_RETAIL_DRIVERS) ? &(d)->numTimesAttackedByPlayer[(other)] \
	                                                                               : &g_nativeBigField.timesAttackedBy[(d)->driverID][(other)]))

// Maps a rank among bots (0 = best) onto retail's 8 per-rank table entries. Identity
// with a retail field, so retail's out-of-range reads stay as they were.
static inline int NativeBigField_Rank8(int rank)
{
	if (!NATIVE_BIG_FIELD_ON() || (rank <= 0))
	{
		return rank;
	}

	rank = (rank * NATIVE_RETAIL_DRIVERS) / (NATIVE_DRIVER_CAP - 1);
	return (rank > NATIVE_RETAIL_DRIVERS - 1) ? NATIVE_RETAIL_DRIVERS - 1 : rank;
}

// Decides the driver cap for the level gGT is loading and resets the arena. Called
// from MainInit_PrimMem, the first allocation of a level load.
void NativeBigField_Latch(struct GameTracker *gGT);

// Memory for the pools and buffers a big-field race needs beyond retail's, from a
// host arena outside the mempack (reset by every latch). NULL when it runs out.
void *NativeBigField_Alloc(int size);

// The arena's used part (pools, drivers, prim memory): game state that snapshots
// such as frame interpolation must save with the mempack. Size 0 when unused.
void NativeBigField_GetArena(void **ptr, u32 *size);

// End of BOTS_Adv_AdjustDifficulty: picks characters for bots 8..23 and fills slots
// 8..23 of the spawn, path and accelerate orders from the ones retail computed for 0..7.
void NativeBigField_AfterAdjustDifficulty(struct GameTracker *gGT);

// Position and rotation of start grid slot index (0..23).
const SVec3 *NativeBigField_SpawnPos(struct Level *level, int index);
const SVec3 *NativeBigField_SpawnRot(struct Level *level, int index);

#endif
