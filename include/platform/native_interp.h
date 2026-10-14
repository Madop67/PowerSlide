#ifndef NATIVE_INTERP_H
#define NATIVE_INTERP_H

// Frame interpolation (video.frame_rate above 30).
//
// Game logic stays at its retail 30 Hz tick. Between two ticks, extra frames are
// drawn with the camera, instance matrices and kart positions blended between the
// previous tick and the current one. Each extra frame runs the normal render path
// inside a snapshot of all game memory that is restored afterwards, so the
// simulation (physics, ghosts, replays, RNG) never sees it.

struct GameTracker;
struct GamepadSystem;

// Draws and presents the in-between frames due before this tick's real frame.
// Runs on the BeforeDraw hook: after MainFrame_GameLogic, before MainFrame_RenderFrame.
void NativeInterp_RenderBetweenTicks(struct GameTracker *gGT, struct GamepadSystem *gGamepads);

// Records the transforms the real frame was drawn with. Runs on the AfterPresent hook.
void NativeInterp_EndTick(struct GameTracker *gGT);

// True while an in-between frame is being drawn. MainFrame_RenderFrame skips its
// logic-side work and its submit, and host side effects (audio, XA, STR video,
// memory card, CD) become no-ops.
int NativeInterp_InRenderPass(void);

// Subscribes to the BeforeDraw and AfterPresent hooks.
void NativeInterp_RegisterHooks(void);

#endif
