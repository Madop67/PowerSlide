#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_H

#include <macros.h>

#if defined(CTR_INTERNAL)
struct NativeReplaySchedulerFrameInfo
{
	s32 frameTimer;
	s32 frameCounter;
	s32 timer;
	s32 framesInThisLEV;
	s32 elapsedTimeMS;
	s32 msInThisLEV;
	s32 elapsedEventTime;
	s32 mainGameState;
	s32 loadingStage;
	s32 levelID;
	u32 mixRandomNumber;
	u32 audioRNG;
	u32 deadcoed0;
	u32 deadcoed1;
	u32 advRng0;
	u32 advRng1;
	// Hash of every driver's position, speed, angle, lap and items, so a physics
	// change shows up even when the RNG streams agree.
	u32 driverHash;
};

int NativeReplayScheduler_PrepareReportFromArgs(int argc, char **argv);
int NativeReplayScheduler_ConfigureFromArgs(int argc, char **argv);
void NativeReplayScheduler_Shutdown(void);
int NativeReplayScheduler_RequestStart(void);
int NativeReplayScheduler_RequestStop(void);
int NativeReplayScheduler_BeginFrame(const struct NativeReplaySchedulerFrameInfo *info);
int NativeReplayScheduler_ConsumeVSyncPacket(int requestedVBlanks, int *emittedVBlanks);
int NativeReplayScheduler_ConsumeFrameElapsedTimeMS(int *elapsedTimeMS);
int NativeReplayScheduler_EndFrame(const struct NativeReplaySchedulerFrameInfo *info);
void NativeReplayScheduler_RecordVSyncPacket(int emittedVBlanks);
// True while recording or playing back, when every VBlank must come from VSync().
int NativeReplayScheduler_IsActive(void);
// True when playback should emit VBlanks without waiting for wall-clock time (--replay-turbo).
int NativeReplayScheduler_IsTurbo(void);
// Creates a folder and its parents.
int NativeReplayScheduler_EnsureDir(const char *path);
// Frames recorded or played so far.
unsigned int NativeReplayScheduler_GetFrame(void);
// Process exit code for the run: 0, NATIVE_REPLAY_EXIT_ERROR or NATIVE_REPLAY_EXIT_DIVERGED.
int NativeReplayScheduler_GetExitCode(void);

#define NATIVE_REPLAY_EXIT_ERROR    2
#define NATIVE_REPLAY_EXIT_DIVERGED 3
#endif

#endif
