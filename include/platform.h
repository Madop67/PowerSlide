#ifndef PLATFORM_H
#define PLATFORM_H

struct PlatformMempackArena
{
	void *base;
	void *start;
	void *endOfMemory;
	int size;
	int backingSize;
};

void Platform_Init(const char *title, int width, int height);
void Platform_Shutdown(void);
void Platform_InitScratchpad(void);
const struct PlatformMempackArena *Platform_InitMempackArena(void);
const struct PlatformMempackArena *Platform_GetMempackArena(void);
void Platform_BeginFrame(void);
int Platform_BeginScene(void);
void Platform_EndScene(void);
void Platform_EndFrame(void);
void Platform_PresentVRAMDisplay(void);
void Platform_PinVRAMDisplayFrames(int frameCount);
void Platform_PinVRAMDisplayRect(int x, int y, int w, int h, int frameCount);
int Platform_IsVRAMDisplayPinned(void);
int Platform_GetVBlankCount(void);
void Platform_WaitUntilVBlank(int targetVBlank);
// Host performance-counter time at which VBlank number `vblankIndex` is (or was)
// due on the paced VBlank clock.
unsigned long long Platform_GetVBlankTime(int vblankIndex);
// Sleeps until the performance counter reaches `target`, emitting VBlanks as they fall due.
void Platform_WaitUntilTime(unsigned long long target);
void Platform_PollHostEvents(void);
int Platform_PollInput(void);

#if defined(CTR_NATIVE)
int NikoGetEnterKey(void);
#endif

#endif
