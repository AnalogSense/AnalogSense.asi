#include "game_cyberpunk2077.hpp"

#include <iostream>
#include <mutex>
#include <vector>

#include <Windows.h>

#include <soup/DelayedCtor.hpp>
#include <soup/DetourHook.hpp>
#include <soup/Module.hpp>
#include <soup/Pattern.hpp>
#include <soup/SharedLibrary.hpp>

#include "common.hpp"

using namespace soup;

static DetourHook UserMappingAxis_OnButtonChange_hook;
static DetourHook UserMapping_AddMapping_hook;
static DetourHook ContextManager_ProcessInput_hook;

static std::mutex map_mtx;
static std::vector<std::pair<int, float*>> map;

struct FakeAxisInput
{
	uint16_t key;
	float value;
	bool pressed;
};
static_assert(offsetof(FakeAxisInput, pressed) == 8);

static FakeAxisInput* __fastcall UserMappingAxis_OnButtonChange_detour(void* _this, void* action, uint16_t key, bool pressed)
{
	FakeAxisInput* input = reinterpret_cast<decltype(&UserMappingAxis_OnButtonChange_detour)>(UserMappingAxis_OnButtonChange_hook.original)(_this, action, key, pressed);
	std::lock_guard lock(map_mtx);
	for (const auto& e : map)
	{
		if (e.first == key && e.second == &input->value)
		{
			return input;
		}
	}
	map.emplace_back(key, &input->value);
	return input;
}

static bool __fastcall UserMapping_AddMapping_detour(void* _this, uint16_t rawKey, void* mappedKey, int type, float extraData, void* presetName, float thresholdPress, float thresholdRelease, unsigned int overridableID, unsigned int joinID)
{
	{
		std::lock_guard lock(map_mtx);
		if (!map.empty())
		{
			std::cout << "flushing map" << std::endl;
			map.clear();
		}
	}

	return reinterpret_cast<decltype(&UserMapping_AddMapping_detour)>(UserMapping_AddMapping_hook.original)(_this, rawKey, mappedKey, type, extraData, presetName, thresholdPress, thresholdRelease, overridableID, joinID);
}

static void __fastcall ContextManager_ProcessInput_detour(void* _this, float dt, void* cache, uint64_t time)
{
	reinterpret_cast<decltype(&ContextManager_ProcessInput_detour)>(ContextManager_ProcessInput_hook.original)(_this, dt, cache, time);

	{
		std::lock_guard lock(map_mtx);
		for (const auto& e : map)
		{
			*e.second = std::copysign(wooting_analog_read_analog(e.first), *e.second);
		}
	}

	analogsense_on_input_tick();
}

void cyberpunk2077_init()
{
	{
		auto UserMappingAxis_OnButtonChange = Module(nullptr).range.scan(Pattern("48 8B C4 48 89 58 08 48 89 50 10 57 48 83 EC 40 41 0F B7 F8")).as<void*>();
		std::cout << "UserMappingAxis_OnButtonChange = " << UserMappingAxis_OnButtonChange << std::endl;

		UserMappingAxis_OnButtonChange_hook.detour = reinterpret_cast<void*>(&UserMappingAxis_OnButtonChange_detour);
		UserMappingAxis_OnButtonChange_hook.target = UserMappingAxis_OnButtonChange;
		UserMappingAxis_OnButtonChange_hook.create();
		UserMappingAxis_OnButtonChange_hook.enable();
	}

	{
		auto UserMapping_AddMapping = Module(nullptr).range.scan(Pattern("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 55 41 56 41 57 48 8B EC 48 83 EC 60 41 0F B6 F9")).as<void*>();
		std::cout << "UserMapping_AddMapping = " << UserMapping_AddMapping << std::endl;

		UserMapping_AddMapping_hook.detour = reinterpret_cast<void*>(&UserMapping_AddMapping_detour);
		UserMapping_AddMapping_hook.target = UserMapping_AddMapping;
		UserMapping_AddMapping_hook.create();
		UserMapping_AddMapping_hook.enable();
	}

	{
		auto ContextManager_ProcessInput = Module(nullptr).range.scan(Pattern("48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 50 4C 8D 61 28")).as<void*>();
		std::cout << "ContextManager_ProcessInput = " << ContextManager_ProcessInput << std::endl;

		ContextManager_ProcessInput_hook.detour = reinterpret_cast<void*>(&ContextManager_ProcessInput_detour);
		ContextManager_ProcessInput_hook.target = ContextManager_ProcessInput;
		ContextManager_ProcessInput_hook.create();
		ContextManager_ProcessInput_hook.enable();
	}
}

void cyberpunk2077_deinit()
{
	if (UserMapping_AddMapping_hook.isCreated())
	{
		UserMapping_AddMapping_hook.disable();
		UserMapping_AddMapping_hook.destroy();
	}

	if (UserMappingAxis_OnButtonChange_hook.isCreated())
	{
		UserMappingAxis_OnButtonChange_hook.disable();
		UserMappingAxis_OnButtonChange_hook.destroy();
	}

	if (ContextManager_ProcessInput_hook.isCreated())
	{
		ContextManager_ProcessInput_hook.disable();
		ContextManager_ProcessInput_hook.destroy();
	}

	{
		std::lock_guard lock(map_mtx);
		for (const auto& e : map)
		{
			*e.second = std::copysign(1.0f, *e.second);
		}
	}
}
