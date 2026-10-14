#include <platform/native_input.h>
#include <platform/native_ui.h>
#include <platform/native_input_config.h>
#include <platform/native_settings.h>
#include <platform/native_log.h>
#include <platform/native_replay_scheduler.h>

#include <macros.h>
#include "psx/libpad.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_INPUT_MAX_CONTROLLERS       PLATFORM_INPUT_PAD_COUNT
#define NATIVE_INPUT_PHYSICAL_SLOT_COUNT   2
#define NATIVE_INPUT_PAD_PACKET_BYTES      8
#define NATIVE_INPUT_MULTITAP_HEADER       2
#define NATIVE_INPUT_PAD_DIGITAL           0x41
#define NATIVE_INPUT_PAD_ANALOG            0x73
#define NATIVE_INPUT_PAD_MULTITAP          0x80
#define NATIVE_INPUT_PAD_DISCONNECT        0xff
#define NATIVE_INPUT_AXIS_DEADZONE         500
#define NATIVE_INPUT_AXIS_MAX              32767
// NOTE(aalhendi): Little-endian tag `CTRI` = CTR native Input snapshot.
#define NATIVE_INPUT_STATE_MAGIC           0x49525443
#define NATIVE_INPUT_STATE_VERSION         2

// NOTE(aalhendi): Native input preserves behavior from PsyCross's
// MIT-licensed pad implementation while moving host ownership into ctr-native.
// See THIRD_PARTY_NOTICES.md.

struct NativeInputController
{
	SDL_JoystickID instanceId;
	SDL_Gamepad *controller;
	s32 analogEnabled;
	s32 switchingAnalog;
	struct PlatformInputPadSnapshot snapshot;
};

struct NativeInputControllerStateSnapshot
{
	struct PlatformInputPadSnapshot snapshot;
	s32 analogEnabled;
	s32 switchingAnalog;
};

struct NativeInputStateSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	s32 lastActiveControllerSlot;
	s32 installedSnapshotsActive;
	struct PlatformInputPadSnapshot installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
	struct NativeInputControllerStateSnapshot controllers[NATIVE_INPUT_MAX_CONTROLLERS];
};

global_variable struct NativeInputController s_controllers[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable struct PlatformInputPadSnapshot s_installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable u8 *s_padSlotData[NATIVE_INPUT_PHYSICAL_SLOT_COUNT];
global_variable const bool *s_keyboardState;
global_variable s32 s_inputInitialized;
global_variable s32 s_installedSnapshotsActive;
global_variable s32 s_lastActiveControllerSlot = -1;

extern s32 g_padCommEnable;

// PS1 pad button bits (active low), indexed by enum NativePadButton.
global_variable const u16 s_padButtonMasks[NATIVE_PAD_BUTTON_COUNT] = {
	0x4000, // cross
	0x2000, // circle
	0x8000, // square
	0x1000, // triangle
	0x0400, // L1
	0x0800, // R1
	0x0100, // L2
	0x0200, // R2
	0x0010, // up
	0x0040, // down
	0x0080, // left
	0x0020, // right
	0x0008, // start
	0x0001, // select
	0x0002, // L3
	0x0004, // R3
};

internal u16 NativeInput_GetSnapshotButtons(const struct PlatformInputPadSnapshot *snapshot)
{
	return (u16)(snapshot->buttons[0] | (snapshot->buttons[1] << 8));
}

internal void NativeInput_SetSnapshotButtons(struct PlatformInputPadSnapshot *snapshot, u16 buttons)
{
	snapshot->buttons[0] = (u8)(buttons & 0xff);
	snapshot->buttons[1] = (u8)(buttons >> 8);
}

#if defined(CTR_INTERNAL)
// --input-script: pad 1 holds scripted buttons, for recording test replays without
// playing. Each line is "<replay frame> <button> <button>..." ("-" for none) and holds
// those buttons from that frame on; '#' starts a comment.
struct NativeInputScriptStep
{
	u32 frame;
	u16 pressed;
};

#define NATIVE_INPUT_SCRIPT_STEPS_MAX 4096

global_variable struct NativeInputScriptStep s_scriptSteps[NATIVE_INPUT_SCRIPT_STEPS_MAX];
global_variable s32 s_scriptStepCount;

int Platform_InputLoadScript(const char *path)
{
	FILE *file = fopen(path, "r");
	char line[512];
	s32 lineNumber = 0;

	if (file == NULL)
	{
		Platform_Log("[CTR Input] cannot open input script: %s\n", path);
		return 0;
	}

	s_scriptStepCount = 0;
	while (fgets(line, sizeof(line), file) != NULL)
	{
		char *comment = strchr(line, '#');
		char *token;
		char *end = NULL;
		struct NativeInputScriptStep step;

		lineNumber++;
		if (comment != NULL)
		{
			*comment = '\0';
		}
		token = strtok(line, " \t\r\n,");
		if (token == NULL)
		{
			continue;
		}

		step.frame = (u32)strtoul(token, &end, 10);
		step.pressed = 0;
		if ((end == token) || (*end != '\0') || ((s_scriptStepCount > 0) && (step.frame < s_scriptSteps[s_scriptStepCount - 1].frame)))
		{
			Platform_Log("[CTR Input] input script line %d: expected a frame number in increasing order\n", lineNumber);
			fclose(file);
			return 0;
		}

		while ((token = strtok(NULL, " \t\r\n,")) != NULL)
		{
			s32 button = 0;
			if (strcmp(token, "-") == 0)
			{
				continue;
			}
			while ((button < NATIVE_PAD_BUTTON_COUNT) && (strcmp(token, NativeSettings_PadButtonKey(button)) != 0))
			{
				button++;
			}
			if (button == NATIVE_PAD_BUTTON_COUNT)
			{
				Platform_Log("[CTR Input] input script line %d: unknown button \"%s\"\n", lineNumber, token);
				fclose(file);
				return 0;
			}
			step.pressed |= s_padButtonMasks[button];
		}

		if (s_scriptStepCount >= NATIVE_INPUT_SCRIPT_STEPS_MAX)
		{
			Platform_Log("[CTR Input] input script has more than %d lines\n", NATIVE_INPUT_SCRIPT_STEPS_MAX);
			fclose(file);
			return 0;
		}
		s_scriptSteps[s_scriptStepCount++] = step;
	}

	fclose(file);
	Platform_Log("[CTR Input] input script: %s (%d steps)\n", path, s_scriptStepCount);
	return 1;
}

internal void NativeInput_ApplyScript(void)
{
	const u32 frame = NativeReplayScheduler_GetFrame();
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[0].snapshot;
	u16 pressed = 0;

	if (s_scriptStepCount == 0)
	{
		return;
	}

	for (s32 i = 0; (i < s_scriptStepCount) && (s_scriptSteps[i].frame <= frame); i++)
	{
		pressed = s_scriptSteps[i].pressed;
	}
	NativeInput_SetSnapshotButtons(snapshot, NativeInput_GetSnapshotButtons(snapshot) & (u16)~pressed);
}
#endif

internal void NativeInput_ResetSnapshot(s32 slot)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;

	snapshot->connected = slot == 0;
	snapshot->status = snapshot->connected ? 0 : NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = snapshot->connected ? NATIVE_INPUT_PAD_DIGITAL : NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal s32 NativeInput_IsValidControllerSlot(s32 slot)
{
	return (slot >= 0) && (slot < NATIVE_INPUT_MAX_CONTROLLERS);
}

internal s32 NativeInput_NextControllerSlot(s32 slot)
{
	slot++;
	if (slot >= NATIVE_INPUT_MAX_CONTROLLERS)
	{
		slot = 0;
	}

	return slot;
}

internal void NativeInput_MakeDisconnectedSnapshot(struct PlatformInputPadSnapshot *snapshot)
{
	if (snapshot == NULL)
	{
		return;
	}

	snapshot->connected = 0;
	snapshot->status = NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal void NativeInput_WritePadPacket(u8 *dst, const struct PlatformInputPadSnapshot *snapshot)
{
	if ((dst == NULL) || (snapshot == NULL))
	{
		return;
	}

	dst[0] = snapshot->status;
	dst[1] = snapshot->id;
	dst[2] = snapshot->buttons[0];
	dst[3] = snapshot->buttons[1];
	dst[4] = snapshot->analog[0];
	dst[5] = snapshot->analog[1];
	dst[6] = snapshot->analog[2];
	dst[7] = snapshot->analog[3];
}

internal s32 NativeInput_UseMultitapBus(void)
{
	for (s32 slot = NATIVE_INPUT_PHYSICAL_SLOT_COUNT; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].snapshot.connected != 0)
		{
			return 1;
		}
	}

	return 0;
}

internal void NativeInput_WritePadBus(void)
{
	u8 *slot0 = s_padSlotData[0];
	u8 *slot1 = s_padSlotData[1];
	s32 useMultitap = NativeInput_UseMultitapBus();
	s32 slot;

	if (slot0 != NULL)
	{
		if (useMultitap != 0)
		{
			slot0[0] = 0;
			slot0[1] = NATIVE_INPUT_PAD_MULTITAP;
			for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
			{
				NativeInput_WritePadPacket(&slot0[NATIVE_INPUT_MULTITAP_HEADER + (slot * NATIVE_INPUT_PAD_PACKET_BYTES)], &s_controllers[slot].snapshot);
			}
		}
		else
		{
			NativeInput_WritePadPacket(slot0, &s_controllers[0].snapshot);
		}
	}

	if (slot1 != NULL)
	{
		if (useMultitap != 0)
		{
			struct PlatformInputPadSnapshot disconnected;

			NativeInput_MakeDisconnectedSnapshot(&disconnected);
			NativeInput_WritePadPacket(slot1, &disconnected);
		}
		else
		{
			NativeInput_WritePadPacket(slot1, &s_controllers[1].snapshot);
		}
	}
}

internal void NativeInput_WriteInstalledSnapshots(void)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].snapshot = s_installedSnapshots[slot];
	}

	NativeInput_WritePadBus();
}

// Returns how far a gamepad binding is pressed, 0..32767.
internal s32 NativeInput_BindingValue(SDL_Gamepad *controller, s32 binding)
{
	if ((controller == NULL) || (binding == NATIVE_PAD_BIND_NONE))
	{
		return 0;
	}

	if ((binding & NATIVE_PAD_BIND_AXIS) != 0)
	{
		const SDL_GamepadAxis axis = (SDL_GamepadAxis)(binding & ~(NATIVE_PAD_BIND_AXIS | NATIVE_PAD_BIND_NEGATIVE));
		s32 value = SDL_GetGamepadAxis(controller, axis);
		if ((binding & NATIVE_PAD_BIND_NEGATIVE) != 0)
		{
			value = -value;
		}
		return (value > 0) ? value : 0;
	}

	return SDL_GetGamepadButton(controller, (SDL_GamepadButton)binding) ? NATIVE_INPUT_AXIS_MAX : 0;
}

internal u8 NativeInput_AxisToByte(s32 axis)
{
	s32 value = (axis / 256) + 128;

	if (value < 0)
	{
		return 0;
	}

	if (value > 0xff)
	{
		return 0xff;
	}

	return (u8)value;
}

internal s32 NativeInput_AxisIsActive(s32 axis)
{
	return abs(axis) > NATIVE_INPUT_AXIS_DEADZONE;
}

// Radial deadzone: inside it the stick reads centred; outside, the rest of the
// range is rescaled so full deflection still reaches full scale.
internal void NativeInput_ApplyStickDeadzone(s32 *x, s32 *y, s32 percent)
{
	if (percent <= 0)
	{
		return;
	}

	const f32 deadzone = (f32)NATIVE_INPUT_AXIS_MAX * (f32)percent / 100.0f;
	const f32 magnitude = SDL_sqrtf((f32)(*x) * (f32)(*x) + (f32)(*y) * (f32)(*y));
	if (magnitude <= deadzone)
	{
		*x = 0;
		*y = 0;
		return;
	}

	f32 scale = ((magnitude - deadzone) / ((f32)NATIVE_INPUT_AXIS_MAX - deadzone)) * (f32)NATIVE_INPUT_AXIS_MAX / magnitude;
	*x = (s32)SDL_clamp((f32)(*x) * scale, -32768.0f, (f32)NATIVE_INPUT_AXIS_MAX);
	*y = (s32)SDL_clamp((f32)(*y) * scale, -32768.0f, (f32)NATIVE_INPUT_AXIS_MAX);
}

internal void NativeInput_ApplyController(s32 slot)
{
	struct NativeInputController *nativeController = &s_controllers[slot];
	struct PlatformInputPadSnapshot *snapshot = &nativeController->snapshot;
	const struct NativePlayerInputSettings *settings = &g_settings.input.players[slot];
	SDL_Gamepad *controller = nativeController->controller;
	u16 buttons = 0xffff;

	if ((controller == NULL) || (SDL_GamepadConnected(controller) == 0))
	{
		return;
	}

	snapshot->connected = 1;
	snapshot->status = 0;
	snapshot->id = nativeController->analogEnabled ? NATIVE_INPUT_PAD_ANALOG : NATIVE_INPUT_PAD_DIGITAL;

	// Buttons press at any value; triggers and stick directions at the press point.
	const s32 analogPressValue = (NATIVE_INPUT_AXIS_MAX * settings->analogPressPoint) / 100;
	for (s32 button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
	{
		const s32 binding = settings->gamepad[button];
		const s32 threshold = ((binding != NATIVE_PAD_BIND_NONE) && ((binding & NATIVE_PAD_BIND_AXIS) != 0)) ? analogPressValue : 0;
		if (NativeInput_BindingValue(controller, binding) > threshold)
		{
			buttons &= ~s_padButtonMasks[button];
		}
	}

	s32 rightX = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTX);
	s32 rightY = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTY);
	s32 leftX = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTX);
	s32 leftY = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTY);
	NativeInput_ApplyStickDeadzone(&rightX, &rightY, settings->stickDeadzone);
	NativeInput_ApplyStickDeadzone(&leftX, &leftY, settings->stickDeadzone);

	if ((buttons != 0xffff) || NativeInput_AxisIsActive(rightX) || NativeInput_AxisIsActive(rightY) || NativeInput_AxisIsActive(leftX) ||
	    NativeInput_AxisIsActive(leftY))
	{
		s_lastActiveControllerSlot = slot;
	}

	// Select + Start toggles analog mode, like the DualShock's ANALOG button.
	if (((buttons & 0x1) == 0) && ((buttons & 0x8) == 0))
	{
		buttons = 0xffff;
		if (nativeController->switchingAnalog == 0)
		{
			nativeController->analogEnabled = nativeController->analogEnabled == 0;
		}
		nativeController->switchingAnalog = 1;
	}
	else
	{
		nativeController->switchingAnalog = 0;
	}

	NativeInput_SetSnapshotButtons(snapshot, buttons);
	snapshot->analog[0] = NativeInput_AxisToByte(rightX);
	snapshot->analog[1] = NativeInput_AxisToByte(rightY);
	snapshot->analog[2] = NativeInput_AxisToByte(leftX);
	snapshot->analog[3] = NativeInput_AxisToByte(leftY);
}

internal s32 NativeInput_PlayerHasKeys(s32 player)
{
	const struct NativePlayerInputSettings *settings = &g_settings.input.players[player];

	for (s32 button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
	{
		for (s32 i = 0; i < NATIVE_KEY_BINDINGS; i++)
		{
			if (settings->keyboard[button][i] != SDL_SCANCODE_UNKNOWN)
			{
				return 1;
			}
		}
	}

	return 0;
}

internal u16 NativeInput_ReadKeyboard(s32 player)
{
	const struct NativePlayerInputSettings *settings = &g_settings.input.players[player];
	u16 buttons = 0xffff;

	if (s_keyboardState == NULL)
	{
		return buttons;
	}

	for (s32 button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
	{
		for (s32 i = 0; i < NATIVE_KEY_BINDINGS; i++)
		{
			const s32 scancode = settings->keyboard[button][i];
			if ((scancode > SDL_SCANCODE_UNKNOWN) && (scancode < SDL_SCANCODE_COUNT) && s_keyboardState[scancode])
			{
				buttons &= ~s_padButtonMasks[button];
			}
		}
	}

	return buttons;
}

internal s32 NativeInput_KeyboardSuppressed(void)
{
	if (s_keyboardState == NULL)
	{
		return 0;
	}

	return s_keyboardState[SDL_SCANCODE_RALT] || s_keyboardState[SDL_SCANCODE_LALT];
}

// A player with any key bound counts as connected, alongside any controller in the slot.
internal void NativeInput_ApplyKeyboard(s32 slot, s32 readKeys)
{
	struct PlatformInputPadSnapshot *snapshot = &s_controllers[slot].snapshot;

	if (!NativeInput_PlayerHasKeys(slot))
	{
		return;
	}

	if (snapshot->connected == 0)
	{
		snapshot->connected = 1;
		snapshot->status = 0;
		snapshot->id = NATIVE_INPUT_PAD_DIGITAL;
	}

	if (readKeys)
	{
		u16 buttons = NativeInput_GetSnapshotButtons(snapshot);
		NativeInput_SetSnapshotButtons(snapshot, buttons & NativeInput_ReadKeyboard(slot));
	}
}

internal s32 NativeInput_FindActiveControllerSlot(void)
{
	if (NativeInput_IsValidControllerSlot(s_lastActiveControllerSlot) && (s_controllers[s_lastActiveControllerSlot].controller != NULL))
	{
		return s_lastActiveControllerSlot;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller != NULL)
		{
			return slot;
		}
	}

	return -1;
}

// Identifies a controller model across sessions: "vvvv:pppp" USB vendor and product ids.
internal void NativeInput_FormatControllerId(SDL_JoystickID instanceId, char *out, size_t outSize)
{
	SDL_snprintf(out, outSize, "%04x:%04x", SDL_GetGamepadVendorForID(instanceId), SDL_GetGamepadProductForID(instanceId));
}

// Remembers the controller now in `slot` as that player's controller.
internal void NativeInput_RememberController(s32 slot)
{
	struct NativePlayerInputSettings *player = &g_settings.input.players[slot];
	SDL_Gamepad *controller = s_controllers[slot].controller;

	if (controller == NULL)
	{
		return;
	}

	char id[NATIVE_CONTROLLER_ID_MAX];
	NativeInput_FormatControllerId(s_controllers[slot].instanceId, id, sizeof(id));
	const char *name = SDL_GetGamepadName(controller);
	char cleanName[NATIVE_CONTROLLER_NAME_MAX];
	SDL_snprintf(cleanName, sizeof(cleanName), "%s", (name != NULL) ? name : "Controller");
	for (char *c = cleanName; *c != '\0'; c++)
	{
		// The settings file quotes names and has no escapes.
		if (*c == '"')
		{
			*c = '\'';
		}
	}

	if ((strcmp(player->controllerId, id) == 0) && (strcmp(player->controllerName, cleanName) == 0))
	{
		return;
	}

	SDL_snprintf(player->controllerId, sizeof(player->controllerId), "%s", id);
	SDL_snprintf(player->controllerName, sizeof(player->controllerName), "%s", cleanName);
	NativeSettings_Save();
}

internal void NativeInput_SwapControllerSlots(s32 slotA, s32 slotB)
{
	if (!NativeInput_IsValidControllerSlot(slotA) || !NativeInput_IsValidControllerSlot(slotB) || (slotA == slotB))
	{
		return;
	}

	struct NativeInputController controller = s_controllers[slotA];
	s_controllers[slotA] = s_controllers[slotB];
	s_controllers[slotB] = controller;

	// The remembered controllers move with them; each player keeps its own bindings.
	struct NativePlayerInputSettings *playerA = &g_settings.input.players[slotA];
	struct NativePlayerInputSettings *playerB = &g_settings.input.players[slotB];
	char id[NATIVE_CONTROLLER_ID_MAX];
	char name[NATIVE_CONTROLLER_NAME_MAX];
	memcpy(id, playerA->controllerId, sizeof(id));
	memcpy(name, playerA->controllerName, sizeof(name));
	memcpy(playerA->controllerId, playerB->controllerId, sizeof(id));
	memcpy(playerA->controllerName, playerB->controllerName, sizeof(name));
	memcpy(playerB->controllerId, id, sizeof(id));
	memcpy(playerB->controllerName, name, sizeof(name));
	NativeSettings_Save();

	if (s_lastActiveControllerSlot == slotA)
	{
		s_lastActiveControllerSlot = slotB;
	}
	else if (s_lastActiveControllerSlot == slotB)
	{
		s_lastActiveControllerSlot = slotA;
	}
}

// Picks the player a newly connected controller drives: the free player that remembers
// it, else a free player that remembers no controller, else any free player. With
// `rememberedOnly`, only the first choice is tried.
internal s32 NativeInput_FindSlotForController(SDL_JoystickID instanceId, s32 rememberedOnly)
{
	char id[NATIVE_CONTROLLER_ID_MAX];
	s32 slot;

	NativeInput_FormatControllerId(instanceId, id, sizeof(id));

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].instanceId == instanceId)
		{
			return -1;
		}
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((s_controllers[slot].controller == NULL) && (strcmp(g_settings.input.players[slot].controllerId, id) == 0))
		{
			return slot;
		}
	}

	if (rememberedOnly)
	{
		return -1;
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((s_controllers[slot].controller == NULL) && (g_settings.input.players[slot].controllerId[0] == '\0'))
		{
			return slot;
		}
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].controller == NULL)
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_CloseController(s32 slot)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	struct NativeInputController *controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		SDL_CloseGamepad(controller->controller);
	}

	controller->controller = NULL;
	controller->instanceId = -1;
	controller->analogEnabled = 0;
	controller->switchingAnalog = 0;

	if (s_lastActiveControllerSlot == slot)
	{
		s_lastActiveControllerSlot = -1;
	}
}

internal void NativeInput_OpenController(SDL_JoystickID instanceId, s32 slot)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	if (SDL_IsGamepad(instanceId) == 0)
	{
		return;
	}

	struct NativeInputController *controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		return;
	}

	controller->controller = SDL_OpenGamepad(instanceId);
	if (controller->controller == NULL)
	{
		return;
	}

	SDL_Joystick *joystick = SDL_GetGamepadJoystick(controller->controller);
	controller->instanceId = joystick != NULL ? SDL_GetJoystickID(joystick) : instanceId;
	controller->analogEnabled = 1;
	controller->switchingAnalog = 0;
	NativeInput_RememberController(slot);
}

internal void NativeInput_OpenKnownControllers(void)
{
	s32 count = 0;

	SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
	// Remembered controllers first, so an unknown one can't take their player.
	for (s32 pass = 0; pass < 2; pass++)
	{
		for (s32 i = 0; i < count; i++)
		{
			s32 slot = NativeInput_FindSlotForController(gamepads[i], pass == 0);

			if (slot >= 0)
			{
				NativeInput_OpenController(gamepads[i], slot);
			}
		}
	}
	SDL_free(gamepads);
}

int Platform_InputInit(void)
{
	if (s_inputInitialized != 0)
	{
		return 1;
	}

	memset(s_controllers, 0, sizeof(s_controllers));
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].instanceId = -1;
		NativeInput_ResetSnapshot(slot);
		s_installedSnapshots[slot] = s_controllers[slot].snapshot;
	}

	s_lastActiveControllerSlot = -1;
	s_installedSnapshotsActive = 0;
	s_keyboardState = SDL_GetKeyboardState(NULL);

	if (SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC) == 0)
	{
		fprintf(stderr, "[CTR Native] Failed to initialise SDL input subsystem: %s\n", SDL_GetError());
		return 0;
	}

	SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
	NativeInput_OpenKnownControllers();

	s_inputInitialized = 1;
	return 1;
}

void Platform_InputShutdown(void)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_CloseController(slot);
	}

	if (s_inputInitialized != 0)
	{
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
	}

	s_inputInitialized = 0;
	s_installedSnapshotsActive = 0;
	s_lastActiveControllerSlot = -1;
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	s_keyboardState = NULL;
}

void Platform_InputUpdate(void)
{
	if (s_inputInitialized == 0)
	{
		return;
	}

	if (s_installedSnapshotsActive != 0)
	{
		// NOTE(aalhendi): replay/state installs PSX-shaped pad bytes here;
		// SDL host state is not serialized.
		NativeInput_WriteInstalledSnapshots();
		return;
	}

	if (g_padCommEnable == 0)
	{
		return;
	}

	SDL_PumpEvents();
	// The game keeps running under the settings menu. Only the device driving the menu
	// is held: the keyboard, or the controller that opened it (every controller while a
	// controller binding is being captured). A held pad reads released buttons and
	// centred sticks but stays connected, so the game raises no disconnect pause.
	const s32 readKeys = !NativeUI_MenuOwnsKeyboard() && !NativeUI_IsCapturingBinding() && !NativeInput_KeyboardSuppressed();
	const s32 holdAllControllers = NativeUI_IsCapturingBinding();
	const SDL_JoystickID menuController = (SDL_JoystickID)NativeUI_GetMenuController();

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		struct NativeInputController *controller = &s_controllers[slot];
		const s32 holdController = (controller->controller != NULL) && (holdAllControllers || ((menuController != 0) && (controller->instanceId == menuController)));

		NativeInput_ResetSnapshot(slot);
		if (!holdController)
		{
			NativeInput_ApplyController(slot);
		}
		else if (controller->controller != NULL)
		{
			controller->snapshot.connected = 1;
			controller->snapshot.status = 0;
			controller->snapshot.id = controller->analogEnabled ? NATIVE_INPUT_PAD_ANALOG : NATIVE_INPUT_PAD_DIGITAL;
		}
		NativeInput_ApplyKeyboard(slot, readKeys);
	}
#if defined(CTR_INTERNAL)
	NativeInput_ApplyScript();
#endif
	NativeInput_WritePadBus();
}

void Platform_InputControllerAdded(int deviceIndex)
{
	if (s_inputInitialized == 0)
	{
		return;
	}

	s32 slot = NativeInput_FindSlotForController((SDL_JoystickID)deviceIndex, 0);
	if (slot >= 0)
	{
		NativeInput_OpenController((SDL_JoystickID)deviceIndex, slot);
	}
}

void Platform_InputControllerRemoved(int instanceId)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].instanceId == (SDL_JoystickID)instanceId)
		{
			NativeInput_CloseController(slot);
			return;
		}
	}
}

// Debug hotkey: rotates every player's keys to the next player (P4's go to P1). With the
// default layout this moves the keyboard to the next player; returns the first player with keys.
int Platform_InputCycleKeyboardController(void)
{
	struct NativePlayerInputSettings *players = g_settings.input.players;
	s32 last[NATIVE_PAD_BUTTON_COUNT][NATIVE_KEY_BINDINGS];

	memcpy(last, players[NATIVE_INPUT_PLAYERS - 1].keyboard, sizeof(last));
	for (s32 player = NATIVE_INPUT_PLAYERS - 1; player > 0; player--)
	{
		memcpy(players[player].keyboard, players[player - 1].keyboard, sizeof(last));
	}
	memcpy(players[0].keyboard, last, sizeof(last));
	NativeSettings_Save();

	for (s32 player = 0; player < NATIVE_INPUT_PLAYERS; player++)
	{
		if (NativeInput_PlayerHasKeys(player))
		{
			return player + 1;
		}
	}
	return 0;
}

void Platform_ApplyInputSettings(void)
{
	// Bindings are read from g_settings every frame; nothing is cached.
}

//------------------------------------------------------------------------------------------------------------------------
// Settings-menu queries (native_input_config.h).

int NativeInput_GetPlayerController(int player, char *name, int nameSize, int *gamepadType)
{
	if (!NativeInput_IsValidControllerSlot(player) || (s_controllers[player].controller == NULL))
	{
		return 0;
	}

	SDL_Gamepad *controller = s_controllers[player].controller;
	if ((name != NULL) && (nameSize > 0))
	{
		const char *controllerName = SDL_GetGamepadName(controller);
		SDL_snprintf(name, (size_t)nameSize, "%s", (controllerName != NULL) ? controllerName : "Controller");
	}
	if (gamepadType != NULL)
	{
		*gamepadType = (int)SDL_GetGamepadType(controller);
	}
	return 1;
}

void NativeInput_SwapPlayers(int playerA, int playerB)
{
	NativeInput_SwapControllerSlots(playerA, playerB);
	NativeInput_WritePadBus();
}

int NativeInput_GetPlayerControllerId(int player)
{
	if (!NativeInput_IsValidControllerSlot(player) || (s_controllers[player].controller == NULL))
	{
		return 0;
	}
	return (int)s_controllers[player].instanceId;
}

int NativeInput_IsGamepadEventForPlayer(unsigned int instanceId)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((s_controllers[slot].controller != NULL) && (s_controllers[slot].instanceId == (SDL_JoystickID)instanceId))
		{
			return slot;
		}
	}
	return -1;
}

int Platform_InputCycleGamepadController(void)
{
	s32 slot = NativeInput_FindActiveControllerSlot();

	if (slot < 0)
	{
		return 0;
	}

	s32 nextSlot = NativeInput_NextControllerSlot(slot);
	NativeInput_SwapControllerSlots(slot, nextSlot);
	NativeInput_WritePadBus();
	return nextSlot + 1;
}

void Platform_InputPadInit(int slot, unsigned char *padData)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_PHYSICAL_SLOT_COUNT))
	{
		return;
	}

	s_padSlotData[slot] = padData;
	NativeInput_WritePadBus();
}

int Platform_InputPadGetState(int port)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return PadStateDiscon;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return PadStateDiscon;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return PadStateDiscon;
	}

	return s_controllers[slot].snapshot.connected ? PadStateStable : PadStateDiscon;
}

int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count)
{
	if ((dst == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		dst[slot] = s_controllers[slot].snapshot;
	}

	return NATIVE_INPUT_MAX_CONTROLLERS;
}

int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count)
{
	if ((src == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = src[slot];
	}

	s_installedSnapshotsActive = 1;
	NativeInput_WriteInstalledSnapshots();
	return NATIVE_INPUT_MAX_CONTROLLERS;
}

void Platform_InputClearInstalledPadSnapshots(void)
{
	s_installedSnapshotsActive = 0;
}

int Platform_InputGetStateSize(void)
{
	return (int)sizeof(struct NativeInputStateSnapshot);
}

int Platform_InputCaptureState(void *dst, int dstSize)
{
	struct NativeInputStateSnapshot *snapshot = (struct NativeInputStateSnapshot *)dst;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_INPUT_STATE_MAGIC;
	snapshot->version = NATIVE_INPUT_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->lastActiveControllerSlot = s_lastActiveControllerSlot;
	snapshot->installedSnapshotsActive = s_installedSnapshotsActive;

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		snapshot->installedSnapshots[slot] = s_installedSnapshots[slot];
		snapshot->controllers[slot].snapshot = s_controllers[slot].snapshot;
		snapshot->controllers[slot].analogEnabled = s_controllers[slot].analogEnabled;
		snapshot->controllers[slot].switchingAnalog = s_controllers[slot].switchingAnalog;
	}

	return 1;
}

int Platform_InputRestoreState(const void *src, int srcSize)
{
	const struct NativeInputStateSnapshot *snapshot = (const struct NativeInputStateSnapshot *)src;
	s32 slot;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_INPUT_STATE_MAGIC) || (snapshot->version != NATIVE_INPUT_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->lastActiveControllerSlot < -1) || (snapshot->lastActiveControllerSlot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}
	if ((snapshot->installedSnapshotsActive < 0) || (snapshot->installedSnapshotsActive > 1))
	{
		return 0;
	}
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((snapshot->controllers[slot].analogEnabled < 0) || (snapshot->controllers[slot].analogEnabled > 1))
		{
			return 0;
		}
		if ((snapshot->controllers[slot].switchingAnalog < 0) || (snapshot->controllers[slot].switchingAnalog > 1))
		{
			return 0;
		}
	}

	s_lastActiveControllerSlot = snapshot->lastActiveControllerSlot;
	s_installedSnapshotsActive = snapshot->installedSnapshotsActive != 0;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = snapshot->installedSnapshots[slot];
		s_controllers[slot].snapshot = snapshot->controllers[slot].snapshot;
		s_controllers[slot].analogEnabled = snapshot->controllers[slot].analogEnabled;
		s_controllers[slot].switchingAnalog = snapshot->controllers[slot].switchingAnalog;
	}
	NativeInput_WritePadBus();

	return 1;
}

void Platform_InputPadVibrate(int port, unsigned char *table, int len)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS) || (table == NULL) || (len <= 0))
	{
		return;
	}

	struct NativeInputController *controller = &s_controllers[slot];
	const struct NativePlayerInputSettings *settings = &g_settings.input.players[slot];
	if ((controller->controller == NULL) || !settings->rumble)
	{
		return;
	}

	const u32 strength = (u32)SDL_clamp(settings->rumbleStrength, 0, 100);
	u16 freqHigh = (u16)((table[0] * 255u * strength) / 100u);
	u16 freqLow = len > 1 ? (u16)((table[1] * 255u * strength) / 100u) : 0;

	if ((freqLow != 0) && (freqLow < 4096))
	{
		freqLow = 4096;
	}

	if ((freqHigh != 0) && (freqHigh < 4096))
	{
		freqHigh = 4096;
	}

	SDL_RumbleGamepad(controller->controller, freqLow, freqHigh, 200);
}
