#include <iostream>
#include <mutex>
#include <vector>

#include <Windows.h>

#include <DelayedCtor.hpp>
#include <DetourHook.hpp>
#include <Module.hpp>
#include <Pattern.hpp>
#include <SharedLibrary.hpp>

#define LOGGING false
#define UNLOADING false

using namespace soup;

using WootingAnalogResult = int;

using wooting_analog_initialise_t = WootingAnalogResult(*)();
using wooting_analog_uninitialise_t = WootingAnalogResult(*)();
using wooting_analog_set_keycode_mode_t = WootingAnalogResult(*)(int mode);
using wooting_analog_read_analog_t = float(*)(unsigned short code);

static HMODULE this_mod;
static DelayedCtor<SharedLibrary> sdk;
static wooting_analog_read_analog_t wooting_analog_read_analog;

static DetourHook UserMappingAxis_OnButtonChange_hook;
static DetourHook UserMapping_AddMapping_hook;
static DetourHook ContextManager_ProcessInput_hook;

static std::mutex map_mtx;
static std::vector<std::pair<int, float*>> map;

#if UNLOADING
static void unload()
{
	CreateThread(nullptr, 0, [](PVOID) -> DWORD
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
		if (sdk->isLoaded())
		{
			((wooting_analog_uninitialise_t)sdk->getAddress("wooting_analog_uninitialise"))();
		}
		sdk.destroy();
#if LOGGING
		FreeConsole();
#endif
		FreeLibraryAndExitThread(this_mod, 0);
	}, 0, 0, nullptr);
}
#endif

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

#if UNLOADING
	if (GetAsyncKeyState(VK_F1) & 0x8000)
	{
		std::cout << "F1 pressed, unloading" << std::endl;
		unload();
	}
#endif
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		this_mod = hmod;
		DisableThreadLibraryCalls(hmod);
#if LOGGING
		AllocConsole();
		{
			FILE* f;
			freopen_s(&f, "CONIN$", "r", stdin);
			freopen_s(&f, "CONOUT$", "w", stderr);
			freopen_s(&f, "CONOUT$", "w", stdout);
		}
#endif
		sdk.construct("wooting_analog_sdk");
		if (!sdk->isLoaded()
			|| ((wooting_analog_initialise_t)sdk->getAddress("wooting_analog_initialise"))() < 0
			)
		{
			return FALSE;
		}

		// Switch Wooting Analog SDK to VirtualKey mode
		((wooting_analog_set_keycode_mode_t)sdk->getAddress("wooting_analog_set_keycode_mode"))(2);

		wooting_analog_read_analog = (wooting_analog_read_analog_t)sdk->getAddress("wooting_analog_read_analog");

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
	return TRUE;
}
