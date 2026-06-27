#include "game_rdr2.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <safetyhook.hpp>
#include <soup/DetourHook.hpp>
#include <soup/macros.hpp>
#include <soup/Module.hpp>
#include <soup/Pattern.hpp>

#include "common.hpp"

using namespace soup;

static DetourHook rage_ioMapper_Update_hook;
static SafetyHookMid rdr2_keyboard_action_hook;
static float keyboard_values[256];

static void _fastcall rage_ioMapper_Update_detour(void* _this, unsigned int timeMS, bool forceKeyboardMouse)
{
	std::memset(keyboard_values, 0, sizeof(keyboard_values));

	unsigned short code_buffer[16];
	float analog_buffer[16];
	const int num = wooting_analog_read_full_buffer(code_buffer, analog_buffer, COUNT(code_buffer));
	if (num >= 0)
	{
		for (int i = 0; i != num; ++i)
		{
			if (code_buffer[i] < COUNT(keyboard_values))
			{
				keyboard_values[code_buffer[i]] = analogsense_transform_value(analog_buffer[i]);
			}
		}
	}

	analogsense_on_input_tick();

	return reinterpret_cast<decltype(&rage_ioMapper_Update_detour)>(rage_ioMapper_Update_hook.original)(_this, timeMS, forceKeyboardMouse);
}


static bool source_action_is_analog(uint32_t kind, uint32_t action_id)
{
	if (kind != 7 && kind != 9)
		return false;

	// RDR2 action ids are INPUT_* enum indices from the name table which start with the 
	// "UNDEFINED_INPUT". We have to keep this restricted to real axes, otherwise any actions for pressed and such a like don't register that well
	// im not sure if the cases I have listed here is enough but should do -clippy95
	switch (action_id)
	{
	case INPUT_LOOK_LR:
	case INPUT_LOOK_UD:
	case INPUT_LOOK_UP_ONLY:
	case INPUT_LOOK_DOWN_ONLY:
	case INPUT_LOOK_LEFT_ONLY:
	case INPUT_LOOK_RIGHT_ONLY:
	case INPUT_MOVE_LR:
	case INPUT_MOVE_UD:
	case INPUT_MOVE_UP_ONLY:
	case INPUT_MOVE_DOWN_ONLY:
	case INPUT_MOVE_LEFT_ONLY:
	case INPUT_MOVE_RIGHT_ONLY:
	case INPUT_VEH_MOVE_LR:
	case INPUT_VEH_MOVE_UD:
	case INPUT_VEH_MOVE_UP_ONLY:
	case INPUT_VEH_MOVE_DOWN_ONLY:
	case INPUT_VEH_MOVE_LEFT_ONLY:
	case INPUT_VEH_MOVE_RIGHT_ONLY:
	case INPUT_VEH_ACCELERATE:
	case INPUT_VEH_BRAKE:
	case INPUT_VEH_FLY_THROTTLE_UP:
	case INPUT_VEH_FLY_THROTTLE_DOWN:
	case INPUT_VEH_FLY_YAW_LEFT:
	case INPUT_VEH_FLY_YAW_RIGHT:
	case INPUT_VEH_FLY_ROLL_LR:
	case INPUT_VEH_FLY_ROLL_LEFT_ONLY:
	case INPUT_VEH_FLY_ROLL_RIGHT_ONLY:
	case INPUT_VEH_FLY_PITCH_UD:
	case INPUT_VEH_FLY_PITCH_UP_ONLY:
	case INPUT_VEH_FLY_PITCH_DOWN_ONLY:
	case INPUT_VEH_SUB_TURN_LR:
	case INPUT_VEH_SUB_TURN_LEFT_ONLY:
	case INPUT_VEH_SUB_TURN_RIGHT_ONLY:
	case INPUT_VEH_SUB_PITCH_UD:
	case INPUT_VEH_SUB_PITCH_UP_ONLY:
	case INPUT_VEH_SUB_PITCH_DOWN_ONLY:
	case INPUT_VEH_SUB_THROTTLE_UP:
	case INPUT_VEH_SUB_THROTTLE_DOWN:
	case INPUT_VEH_PUSHBIKE_PEDAL:
	case INPUT_VEH_PUSHBIKE_FRONT_BRAKE:
	case INPUT_VEH_PUSHBIKE_REAR_BRAKE:
	case INPUT_VEH_DRAFT_MOVE_UD:
	case INPUT_VEH_DRAFT_TURN_LR:
	case INPUT_VEH_DRAFT_MOVE_UP_ONLY:
	case INPUT_VEH_DRAFT_MOVE_DOWN_ONLY:
	case INPUT_VEH_DRAFT_TURN_LEFT_ONLY:
	case INPUT_VEH_DRAFT_TURN_RIGHT_ONLY:
	case INPUT_VEH_DRAFT_ACCELERATE:
	case INPUT_VEH_DRAFT_BRAKE:
	case INPUT_VEH_BOAT_TURN_LR:
	case INPUT_VEH_BOAT_TURN_LEFT_ONLY:
	case INPUT_VEH_BOAT_TURN_RIGHT_ONLY:
	case INPUT_VEH_BOAT_ACCELERATE:
	case INPUT_VEH_BOAT_BRAKE:
	case INPUT_VEH_CAR_TURN_LR:
	case INPUT_VEH_CAR_TURN_LEFT_ONLY:
	case INPUT_VEH_CAR_TURN_RIGHT_ONLY:
	case INPUT_VEH_CAR_ACCELERATE:
	case INPUT_VEH_CAR_BRAKE:
	case INPUT_VEH_HANDCART_ACCELERATE:
	case INPUT_VEH_HANDCART_BRAKE:
	case INPUT_HORSE_MOVE_LR:
	case INPUT_HORSE_MOVE_UD:
	case INPUT_HORSE_MOVE_UP_ONLY:
	case INPUT_HORSE_MOVE_DOWN_ONLY:
	case INPUT_HORSE_MOVE_LEFT_ONLY:
	case INPUT_HORSE_MOVE_RIGHT_ONLY:
	case INPUT_PARACHUTE_TURN_LR:
	case INPUT_PARACHUTE_TURN_LEFT_ONLY:
	case INPUT_PARACHUTE_TURN_RIGHT_ONLY:
	case INPUT_PARACHUTE_PITCH_UD:
	case INPUT_PARACHUTE_PITCH_UP_ONLY:
	case INPUT_PARACHUTE_PITCH_DOWN_ONLY:
	case INPUT_PARACHUTE_BRAKE_LEFT:
	case INPUT_PARACHUTE_BRAKE_RIGHT:
	case INPUT_CREATOR_ZOOM_IN:
	case INPUT_CREATOR_ZOOM_OUT:
	case INPUT_CREATOR_RAISE:
	case INPUT_CREATOR_LOWER:
	case INPUT_CREATOR_MOVE_UD:
	case INPUT_CREATOR_MOVE_LR:
	case INPUT_CREATOR_LOOK_UD:
	case INPUT_CREATOR_LOOK_LR:
	case INPUT_MINIGAME_FISHING_LEFT_AXIS_X:
	case INPUT_MINIGAME_FISHING_LEFT_AXIS_Y:
	case INPUT_MINIGAME_FISHING_RIGHT_AXIS_X:
	case INPUT_MINIGAME_FISHING_RIGHT_AXIS_Y:
	case INPUT_MINIGAME_FISHING_LEAN_LEFT:
	case INPUT_MINIGAME_FISHING_LEAN_RIGHT:
	case INPUT_MINIGAME_FISHING_REEL_SPEED_UP:
	case INPUT_MINIGAME_FISHING_REEL_SPEED_DOWN:
	case INPUT_MINIGAME_FISHING_REEL_SPEED_AXIS:
	case INPUT_MINIGAME_FISHING_MANUAL_REEL_IN:
	case INPUT_MINIGAME_FISHING_MANUAL_REEL_OUT_MODIFER:
	case INPUT_CAMERA_ZOOM:
	case INPUT_CAMERA_ADVANCED_ZOOM_IN:
	case INPUT_CAMERA_ADVANCED_ZOOM_OUT:
	case INPUT_PHOTO_MODE_MOVE_LR:
	case INPUT_PHOTO_MODE_MOVE_LEFT_ONLY:
	case INPUT_PHOTO_MODE_MOVE_RIGHT_ONLY:
	case INPUT_PHOTO_MODE_MOVE_UD:
	case INPUT_PHOTO_MODE_MOVE_UP_ONLY:
	case INPUT_PHOTO_MODE_MOVE_DOWN_ONLY:
	case INPUT_PHOTO_MODE_ROTATE_LEFT:
	case INPUT_PHOTO_MODE_ROTATE_RIGHT:
	case INPUT_PHOTO_MODE_FILTER_INTENSITY:
	case INPUT_PHOTO_MODE_FILTER_INTENSITY_UP:
	case INPUT_PHOTO_MODE_FILTER_INTENSITY_DOWN:
	case INPUT_PHOTO_MODE_FOCAL_LENGTH:
	case INPUT_PHOTO_MODE_FOCAL_LENGTH_UP_ONLY:
	case INPUT_PHOTO_MODE_FOCAL_LENGTH_DOWN_ONLY:
	case INPUT_PHOTO_MODE_ZOOM_IN:
	case INPUT_PHOTO_MODE_ZOOM_OUT:
	case INPUT_PHOTO_MODE_DOF:
	case INPUT_PHOTO_MODE_DOF_UP_ONLY:
	case INPUT_PHOTO_MODE_DOF_DOWN_ONLY:
	case INPUT_PHOTO_MODE_EXPOSURE_UP:
	case INPUT_PHOTO_MODE_EXPOSURE_DOWN:
	case INPUT_PHOTO_MODE_CONTRAST:
	case INPUT_PHOTO_MODE_CONTRAST_UP_ONLY:
	case INPUT_PHOTO_MODE_CONTRAST_DOWN_ONLY:
	case INPUT_SCRIPT_LEFT_AXIS_X:
	case INPUT_SCRIPT_LEFT_AXIS_Y:
	case INPUT_SCRIPT_RIGHT_AXIS_X:
	case INPUT_SCRIPT_RIGHT_AXIS_Y:
		return true;
	default:
		return false;
	}
}

#ifdef AS_DEBUG
static void nop_bytes(void* address, size_t size)
{
	auto* p = static_cast<uint8_t*>(address);
	auto unprotect = safetyhook::unprotect(p, size);
	if (!unprotect)
		return;

	std::memset(p, 0x90, size);
	FlushInstructionCache(GetCurrentProcess(), p, size);
}
#endif

static void keyboard_action_mid(SafetyHookContext& ctx)
{
	if (!ctx.rbx || !ctx.r8)
		return;

	const uint32_t packed = *reinterpret_cast<uint32_t*>(ctx.r8);

	// Only accept simple VK-packed keyboard params: 0x0000VV00.
	if ((packed & 0xFFFF00FF) != 0)
		return;

	const uint32_t vk = (packed >> 8) & 0xFF;
	if (vk == 0 || vk >= COUNT(keyboard_values))
		return;

	const float analog = keyboard_values[vk];
	if (analog <= 0.0f || ctx.xmm1.f32[0] == 0.0f)
		return;

	const uint32_t source_kind = *reinterpret_cast<uint32_t*>(ctx.rbx + 0x20);
	const uint32_t action_id = static_cast<uint32_t>(ctx.r15);
	const bool analogized = source_action_is_analog(source_kind, action_id);

	if (!analogized)
		return;

	ctx.xmm1.f32[0] = std::copysign(analog, ctx.xmm1.f32[0]);
}

void rdr2_init()
{
	auto rage_ioMapper_Update = Module(nullptr).range.scan(Pattern("48 8B C4 48 89 58 ? 44 88 40 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 0F 29 70 ? 8B F2"));
	std::cout << "rage_ioMapper_Update = " << rage_ioMapper_Update.as<void*>() << std::endl;

	rage_ioMapper_Update_hook.detour = reinterpret_cast<void*>(rage_ioMapper_Update_detour);
	rage_ioMapper_Update_hook.target = rage_ioMapper_Update.as<void*>();
	rage_ioMapper_Update_hook.create();
	rage_ioMapper_Update_hook.enable();

#ifdef AS_DEBUG
	// from RageOpenRDR2, game has anti opening console technique
	auto console_protection = Module(nullptr).range.scan(Pattern("ff 15 ? ? ? ? 33 c9 ff 15 ? ? ? ? 45 33 c9"));
	auto ptr = console_protection.as<void*>();
	if (ptr)
		nop_bytes(ptr, 6);
#endif

	auto keyboard_action_call = Module(nullptr).range.scan(Pattern("E8 ? ? ? ? 40 08 7E ? F0 01 7B"));
	std::cout << "keyboard_action_call = " << keyboard_action_call.as<void*>() << std::endl;
	rdr2_keyboard_action_hook = safetyhook::create_mid(keyboard_action_call.as<void*>(), keyboard_action_mid);
}

void rdr2_deinit()
{
	if (rdr2_keyboard_action_hook)
	{
		rdr2_keyboard_action_hook.reset();
	}

	if (rage_ioMapper_Update_hook.isCreated())
	{
		rage_ioMapper_Update_hook.disable();
		rage_ioMapper_Update_hook.destroy();
	}
}
