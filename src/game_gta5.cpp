#include "game_gta5.hpp"

#include <MinHook.h>

#include <iostream>

#include <soup/DetourHook.hpp>
#include <soup/macros.hpp>
#include <soup/Module.hpp>
#include <soup/Pattern.hpp>

#include "common.hpp"

using namespace soup;

extern "C"
{
	void rage_input_helper_init(float(*keyboard_array)[256], void* set_value_from_keyboard_ret, void* set_value_from_mkb_axis_ret);
	void gta5_set_value_from_keyboard_detour();
	void gta5_set_value_from_mkb_axis_detour();
	void gta5ee_set_value_from_keyboard_detour();
	void gta5ee_set_value_from_mkb_axis_detour();
}

static DetourHook rage_ioMapper_Update_hook;
static float arr[256];

static void _fastcall rage_ioMapper_Update_detour(void* _this, unsigned int timeMS, bool forceKeyboardMouse)
{
	memset(arr, 0, sizeof(arr));
	unsigned short code_buffer[16];
	float analog_buffer[16];
	const int num = wooting_analog_read_full_buffer(code_buffer, analog_buffer, 16);
	if (num >= 0)
	{
		for (int i = 0; i != num; ++i)
		{
			if (code_buffer[i] < COUNT(arr))
			{
				arr[code_buffer[i]] = analogsense_transform_value(analog_buffer[i]);
			}
		}
	}

	analogsense_on_input_tick();

	return reinterpret_cast<decltype(&rage_ioMapper_Update_detour)>(rage_ioMapper_Update_hook.original)(_this, timeMS, forceKeyboardMouse);
}

void gta5_init()
{
	MH_Initialize();

	auto rage_ioMapper_Update = Module(nullptr).range.scan(Pattern("48 8B C4 48 89 58 10 44 88 40 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC E0 00 00 00 0F 29 70 B8"));
	std::cout << "rage_ioMapper_Update = " << rage_ioMapper_Update.as<void*>() << std::endl;

	auto set_value_from_keyboard = Module(nullptr).range.scan(Pattern("0F 28 F7 83 FF 09"));
	std::cout << "set_value_from_keyboard = " << set_value_from_keyboard.as<void*>() << std::endl;

	auto set_value_from_keyboard_ret = set_value_from_keyboard.add(6);
	std::cout << "set_value_from_keyboard_ret = " << set_value_from_keyboard_ret.as<void*>() << std::endl;

	auto set_value_from_mkb_axis = Module(nullptr).range.scan(Pattern("8B C7 45 8A C2"));
	std::cout << "set_value_from_mkb_axis = " << set_value_from_mkb_axis.as<void*>() << std::endl;

	auto set_value_from_mkb_axis_ret = Module(nullptr).range.scan(Pattern("49 8B 9C DE B8 15 00 00"));
	std::cout << "set_value_from_mkb_axis_ret = " << set_value_from_mkb_axis_ret.as<void*>() << std::endl;

	rage_input_helper_init(&arr, set_value_from_keyboard_ret.as<void*>(), set_value_from_mkb_axis_ret.as<void*>());

	rage_ioMapper_Update_hook.detour = reinterpret_cast<void*>(rage_ioMapper_Update_detour);
	rage_ioMapper_Update_hook.target = rage_ioMapper_Update.as<void*>();
	rage_ioMapper_Update_hook.create();
	rage_ioMapper_Update_hook.enable();

	MH_CreateHook(set_value_from_keyboard.as<void*>(), reinterpret_cast<void*>(&gta5_set_value_from_keyboard_detour), nullptr);
	MH_EnableHook(set_value_from_keyboard.as<void*>());

	MH_CreateHook(set_value_from_mkb_axis.as<void*>(), reinterpret_cast<void*>(&gta5_set_value_from_mkb_axis_detour), nullptr);
	MH_EnableHook(set_value_from_mkb_axis.as<void*>());
}

void gta5_init_ee()
{
	MH_Initialize();

	auto rage_ioMapper_Update = Module(nullptr).range.scan(Pattern("41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 0F 29 A4 24 80 00 00 00 44 0F 29 5C 24 70 44 0F 29 54 24 60 44 0F 29 4C 24 50 44 0F 29 44 24 40 0F 29 7C 24 30 0F 29 74 24 20 45 89 C4"));
	std::cout << "rage_ioMapper_Update = " << rage_ioMapper_Update.as<void*>() << std::endl;

	auto set_value_from_keyboard = Module(nullptr).range.scan(Pattern("41 0F 28 C0 84 D2"));
	std::cout << "set_value_from_keyboard = " << set_value_from_keyboard.as<void*>() << std::endl;

	auto set_value_from_keyboard_ret = set_value_from_keyboard.add(6);
	std::cout << "set_value_from_keyboard_ret = " << set_value_from_keyboard_ret.as<void*>() << std::endl;

	auto set_value_from_mkb_axis = Module(nullptr).range.scan(Pattern("F7 C1 00 00 01 00 0F 85 ? ? ? ? 0F B6 D1"));
	std::cout << "set_value_from_mkb_axis = " << set_value_from_mkb_axis.as<void*>() << std::endl;

	auto set_value_from_mkb_axis_ret = Module(nullptr).range.scan(Pattern("49 8B 84 FE B8 15 00 00"));
	std::cout << "set_value_from_mkb_axis_ret = " << set_value_from_mkb_axis_ret.as<void*>() << std::endl;

	rage_input_helper_init(&arr, set_value_from_keyboard_ret.as<void*>(), set_value_from_mkb_axis_ret.as<void*>());

	rage_ioMapper_Update_hook.detour = reinterpret_cast<void*>(rage_ioMapper_Update_detour);
	rage_ioMapper_Update_hook.target = rage_ioMapper_Update.as<void*>();
	rage_ioMapper_Update_hook.create();
	rage_ioMapper_Update_hook.enable();

	MH_CreateHook(set_value_from_keyboard.as<void*>(), reinterpret_cast<void*>(&gta5ee_set_value_from_keyboard_detour), nullptr);
	MH_EnableHook(set_value_from_keyboard.as<void*>());

	MH_CreateHook(set_value_from_mkb_axis.as<void*>(), reinterpret_cast<void*>(&gta5ee_set_value_from_mkb_axis_detour), nullptr);
	MH_EnableHook(set_value_from_mkb_axis.as<void*>());
}

void gta5_deinit()
{
	if (rage_ioMapper_Update_hook.isCreated())
	{
		rage_ioMapper_Update_hook.disable();
		rage_ioMapper_Update_hook.destroy();
	}

	MH_Uninitialize();
}
