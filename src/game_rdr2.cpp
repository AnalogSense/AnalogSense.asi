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

static DetourHook rdr2_update_pending_input_hook;
static SafetyHookMid rdr2_keyboard_action_hook;
static float keyboard_values[256];
static float rdr2_injected_keyboard_values[256];

struct Rdr2PendingSource
{
	uint32_t packed;
	uint16_t flags;
	uint16_t pad;
	uint32_t mode;
	float value;
};

struct Rdr2PendingSourceOverride
{
	void* mapper;
	Rdr2PendingSource* original_sources;
	uint16_t original_count;
	bool active;
};

static Rdr2PendingSource rdr2_pending_source_buffer[512];

using Rdr2SetInputValue = void(__fastcall*)(void* io_value, float value, uint32_t* source_param);

struct Rdr2AnalogBinding
{
	uintptr_t io_value;
	uint32_t packed;
	uint32_t vk;
	uint32_t source_kind;
	uint32_t action_id;
	float sign;
	float last_value;
	bool valid;
};

static Rdr2SetInputValue rdr2_set_input_value;
static Rdr2AnalogBinding rdr2_analog_bindings[256];

static bool rdr2_is_foreground_process()
{
	const HWND foreground_window = GetForegroundWindow();
	if (!foreground_window)
		return false;

	DWORD foreground_process_id = 0;
	GetWindowThreadProcessId(foreground_window, &foreground_process_id);
	return foreground_process_id == GetCurrentProcessId();
}

static void read_wooting_keyboard_values()
{
	std::memset(keyboard_values, 0, sizeof(keyboard_values));

	if (!rdr2_is_foreground_process())
		return;

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
}

static void rdr2_apply_cached_analog_bindings()
{
	if (!rdr2_set_input_value)
		return;

	for (auto& binding : rdr2_analog_bindings)
	{
		if (!binding.valid || binding.vk >= COUNT(keyboard_values))
			continue;

		const float value = keyboard_values[binding.vk] * binding.sign;
		if (value == 0.0f && binding.last_value == 0.0f)
			continue;

		uint32_t packed = binding.packed;
		rdr2_set_input_value(reinterpret_cast<void*>(binding.io_value), value, &packed);
		binding.last_value = value;
	}
}

static Rdr2PendingSourceOverride rdr2_begin_pending_source_override(void* mapper)
{
	Rdr2PendingSourceOverride state{};
	state.mapper = mapper;

	if (!mapper)
		return state;

	auto* mapper_base = static_cast<uint8_t*>(mapper);
	auto** sources_ptr = reinterpret_cast<Rdr2PendingSource**>(mapper_base + 0xB18);
	auto* count_ptr = reinterpret_cast<uint16_t*>(mapper_base + 0xB20);

	state.original_sources = *sources_ptr;
	state.original_count = *count_ptr;

	if (state.original_count > COUNT(rdr2_pending_source_buffer))
		return state;

	size_t out_count = 0;
	if (state.original_count != 0)
	{
		if (!state.original_sources)
			return state;

		std::memcpy(rdr2_pending_source_buffer, state.original_sources, state.original_count * sizeof(Rdr2PendingSource));
		out_count = state.original_count;
	}

	for (uint32_t vk = 1; vk != COUNT(keyboard_values); ++vk)
	{
		const float value = keyboard_values[vk];
		const float previous_value = rdr2_injected_keyboard_values[vk];
		if (std::fabs(value - previous_value) < 0.0001f)
			continue;

		if (out_count == COUNT(rdr2_pending_source_buffer))
			break;

		rdr2_pending_source_buffer[out_count++] =
		{
			vk << 8,
			0,
			0,
			0,
			value
		};

		rdr2_injected_keyboard_values[vk] = value;
	}

	if (out_count == state.original_count)
		return state;

	*sources_ptr = rdr2_pending_source_buffer;
	*count_ptr = static_cast<uint16_t>(out_count);
	state.active = true;
	return state;
}

static void rdr2_end_pending_source_override(const Rdr2PendingSourceOverride& state)
{
	if (!state.active || !state.mapper)
		return;

	auto* mapper_base = static_cast<uint8_t*>(state.mapper);
	*reinterpret_cast<Rdr2PendingSource**>(mapper_base + 0xB18) = state.original_sources;
	*reinterpret_cast<uint16_t*>(mapper_base + 0xB20) = state.original_count;
}

static void __fastcall rdr2_update_pending_input_detour(void* mapper)
{
	read_wooting_keyboard_values();
	analogsense_on_input_tick();

	const auto pending_sources = rdr2_begin_pending_source_override(mapper);
	reinterpret_cast<decltype(&rdr2_update_pending_input_detour)>(rdr2_update_pending_input_hook.original)(mapper);
	rdr2_end_pending_source_override(pending_sources);
}


static bool source_action_is_analog(uint32_t kind, uint32_t action_id)
{
	if (kind == 8)
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

static float source_action_sign(uint32_t action_id, float game_value)
{
	if (game_value != 0.0f)
		return std::copysign(1.0f, game_value);

	switch (action_id)
	{
	case INPUT_MOVE_UP_ONLY:
	case INPUT_LOOK_UP_ONLY:
	case INPUT_VEH_MOVE_UP_ONLY:
	case INPUT_VEH_FLY_PITCH_UP_ONLY:
	case INPUT_VEH_SUB_PITCH_UP_ONLY:
	case INPUT_VEH_DRAFT_MOVE_UP_ONLY:
	case INPUT_HORSE_MOVE_UP_ONLY:
	case INPUT_PARACHUTE_PITCH_UP_ONLY:
	case INPUT_PHOTO_MODE_MOVE_UP_ONLY:
	case INPUT_PHOTO_MODE_FOCAL_LENGTH_UP_ONLY:
	case INPUT_PHOTO_MODE_DOF_UP_ONLY:
	case INPUT_PHOTO_MODE_FILTER_INTENSITY_UP:
	case INPUT_PHOTO_MODE_EXPOSURE_UP:
	case INPUT_PHOTO_MODE_CONTRAST_UP_ONLY:
		return -1.0f;
	case INPUT_MOVE_DOWN_ONLY:
	case INPUT_LOOK_DOWN_ONLY:
	case INPUT_VEH_MOVE_DOWN_ONLY:
	case INPUT_VEH_FLY_PITCH_DOWN_ONLY:
	case INPUT_VEH_SUB_PITCH_DOWN_ONLY:
	case INPUT_VEH_DRAFT_MOVE_DOWN_ONLY:
	case INPUT_HORSE_MOVE_DOWN_ONLY:
	case INPUT_PARACHUTE_PITCH_DOWN_ONLY:
	case INPUT_PHOTO_MODE_MOVE_DOWN_ONLY:
	case INPUT_PHOTO_MODE_FOCAL_LENGTH_DOWN_ONLY:
	case INPUT_PHOTO_MODE_DOF_DOWN_ONLY:
	case INPUT_PHOTO_MODE_FILTER_INTENSITY_DOWN:
	case INPUT_PHOTO_MODE_EXPOSURE_DOWN:
	case INPUT_PHOTO_MODE_CONTRAST_DOWN_ONLY:
		return 1.0f;
	case INPUT_MOVE_LEFT_ONLY:
	case INPUT_LOOK_LEFT_ONLY:
	case INPUT_VEH_MOVE_LEFT_ONLY:
	case INPUT_VEH_FLY_YAW_LEFT:
	case INPUT_VEH_FLY_ROLL_LEFT_ONLY:
	case INPUT_VEH_SUB_TURN_LEFT_ONLY:
	case INPUT_VEH_SUB_TURN_HARD_LEFT:
	case INPUT_VEH_DRAFT_TURN_LEFT_ONLY:
	case INPUT_VEH_BOAT_TURN_LEFT_ONLY:
	case INPUT_VEH_CAR_TURN_LEFT_ONLY:
	case INPUT_HORSE_MOVE_LEFT_ONLY:
	case INPUT_PARACHUTE_TURN_LEFT_ONLY:
	case INPUT_PARACHUTE_BRAKE_LEFT:
	case INPUT_MINIGAME_FISHING_LEAN_LEFT:
	case INPUT_PHOTO_MODE_MOVE_LEFT_ONLY:
	case INPUT_PHOTO_MODE_ROTATE_LEFT:
		return -1.0f;
	case INPUT_MOVE_RIGHT_ONLY:
	case INPUT_LOOK_RIGHT_ONLY:
	case INPUT_VEH_MOVE_RIGHT_ONLY:
	case INPUT_VEH_FLY_YAW_RIGHT:
	case INPUT_VEH_FLY_ROLL_RIGHT_ONLY:
	case INPUT_VEH_SUB_TURN_RIGHT_ONLY:
	case INPUT_VEH_SUB_TURN_HARD_RIGHT:
	case INPUT_VEH_DRAFT_TURN_RIGHT_ONLY:
	case INPUT_VEH_BOAT_TURN_RIGHT_ONLY:
	case INPUT_VEH_CAR_TURN_RIGHT_ONLY:
	case INPUT_HORSE_MOVE_RIGHT_ONLY:
	case INPUT_PARACHUTE_TURN_RIGHT_ONLY:
	case INPUT_PARACHUTE_BRAKE_RIGHT:
	case INPUT_MINIGAME_FISHING_LEAN_RIGHT:
	case INPUT_PHOTO_MODE_MOVE_RIGHT_ONLY:
	case INPUT_PHOTO_MODE_ROTATE_RIGHT:
		return 1.0f;
	case INPUT_VEH_ACCELERATE:
	case INPUT_VEH_FLY_THROTTLE_UP:
	case INPUT_VEH_SUB_THROTTLE_UP:
	case INPUT_VEH_PUSHBIKE_PEDAL:
	case INPUT_VEH_DRAFT_ACCELERATE:
	case INPUT_VEH_BOAT_ACCELERATE:
	case INPUT_VEH_CAR_ACCELERATE:
	case INPUT_VEH_HANDCART_ACCELERATE:
	case INPUT_CREATOR_ZOOM_IN:
	case INPUT_MINIGAME_FISHING_REEL_SPEED_UP:
	case INPUT_MINIGAME_FISHING_MANUAL_REEL_IN:
	case INPUT_CAMERA_ADVANCED_ZOOM_IN:
	case INPUT_PHOTO_MODE_ZOOM_IN:
		return -1.0f;
	case INPUT_VEH_BRAKE:
	case INPUT_VEH_FLY_THROTTLE_DOWN:
	case INPUT_VEH_SUB_THROTTLE_DOWN:
	case INPUT_VEH_PUSHBIKE_FRONT_BRAKE:
	case INPUT_VEH_PUSHBIKE_REAR_BRAKE:
	case INPUT_VEH_DRAFT_BRAKE:
	case INPUT_VEH_BOAT_BRAKE:
	case INPUT_VEH_CAR_BRAKE:
	case INPUT_VEH_HANDCART_BRAKE:
	case INPUT_CREATOR_ZOOM_OUT:
	case INPUT_MINIGAME_FISHING_REEL_SPEED_DOWN:
	case INPUT_MINIGAME_FISHING_MANUAL_REEL_OUT_MODIFER:
	case INPUT_CAMERA_ADVANCED_ZOOM_OUT:
	case INPUT_PHOTO_MODE_ZOOM_OUT:
		return 1.0f;
	}

	return 0.0f;
}

static void cache_analog_binding(uint32_t vk, uint32_t packed, uint32_t source_kind, uint32_t action_id, uintptr_t io_value, float sign)
{
	if (!io_value || sign == 0.0f)
		return;

	for (auto& binding : rdr2_analog_bindings)
	{
		if (binding.valid
			&& binding.packed == packed
			&& binding.action_id == action_id
			&& binding.io_value == io_value)
		{
			binding.vk = vk;
			binding.source_kind = source_kind;
			binding.sign = sign;
			return;
		}
	}

	for (auto& binding : rdr2_analog_bindings)
	{
		if (!binding.valid)
		{
			binding =
			{
				io_value,
				packed,
				vk,
				source_kind,
				action_id,
				sign,
				0.0f,
				true
			};
			return;
		}
	}
}

static void* resolve_call_target(void* call_instruction)
{
	if (!call_instruction)
		return nullptr;

	auto* call = static_cast<uint8_t*>(call_instruction);
	const auto rel = *reinterpret_cast<int32_t*>(call + 1);
	return call + 5 + rel;
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

	const uint32_t source_kind = *reinterpret_cast<uint32_t*>(ctx.rbx + 0x20);
	const uint32_t action_id = static_cast<uint32_t>(ctx.r15);

	if (source_action_is_analog(source_kind, action_id))
		return;

	if (keyboard_values[vk] > 0.0f)
	{
		const float value = ctx.xmm1.f32[0];
		ctx.xmm1.f32[0] = value == 0.0f ? 1.0f : std::copysign(1.0f, value);
	}
}

void rdr2_init()
{
	auto rdr2_update_pending_input = Module(nullptr).range.scan(Pattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 48 83 C1 ? E8 ? ? ? ? 33 DB 38 9F ? ? ? ? 74 ? 48 8B CF"));
	std::cout << "rdr2_update_pending_input = " << rdr2_update_pending_input.as<void*>() << std::endl;

	if (auto rdr2_update_pending_input_ptr = rdr2_update_pending_input.as<void*>())
	{
		rdr2_update_pending_input_hook.detour = reinterpret_cast<void*>(rdr2_update_pending_input_detour);
		rdr2_update_pending_input_hook.target = rdr2_update_pending_input_ptr;
		rdr2_update_pending_input_hook.create();
		rdr2_update_pending_input_hook.enable();
	}

#ifdef AS_DEBUG
	// from RageOpenRDR2, game has anti opening console technique
	auto console_protection = Module(nullptr).range.scan(Pattern("ff 15 ? ? ? ? 33 c9 ff 15 ? ? ? ? 45 33 c9"));
	auto ptr = console_protection.as<void*>();
	if (ptr)
		nop_bytes(ptr, 6);
#endif

	auto keyboard_action_call = Module(nullptr).range.scan(Pattern("E8 ? ? ? ? 40 08 7E ? F0 01 7B"));
	std::cout << "keyboard_action_call = " << keyboard_action_call.as<void*>() << std::endl;
	if (auto keyboard_action_call_ptr = keyboard_action_call.as<void*>())
	{
		rdr2_keyboard_action_hook = safetyhook::create_mid(keyboard_action_call_ptr, keyboard_action_mid);
	}
}

void rdr2_deinit()
{
	if (rdr2_keyboard_action_hook)
	{
		rdr2_keyboard_action_hook.reset();
	}

	if (rdr2_update_pending_input_hook.isCreated())
	{
		rdr2_update_pending_input_hook.disable();
		rdr2_update_pending_input_hook.destroy();
	}

	rdr2_set_input_value = nullptr;
	std::memset(rdr2_analog_bindings, 0, sizeof(rdr2_analog_bindings));
	std::memset(rdr2_injected_keyboard_values, 0, sizeof(rdr2_injected_keyboard_values));
}
