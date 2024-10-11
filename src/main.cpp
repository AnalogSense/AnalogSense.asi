#include <iostream>

#include <soup/Process.hpp>

#include "common.hpp"
#include "game_cyberpunk2077.hpp"
#include "game_gta5.hpp"

static HMODULE this_lib;
static HMODULE wooting_lib;

using game_integration_init_t = void(*)();
using game_integration_deinit_t = void(*)();

static game_integration_deinit_t game_integration_deinit = nullptr;

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		this_lib = hmod;
		DisableThreadLibraryCalls(hmod);

		game_integration_init_t game_integration_init = nullptr;
		{
			const auto proc_name = soup::Process::current()->name;
			if (proc_name == "Cyberpunk2077.exe")
			{
				game_integration_init = &cyberpunk2077_init;
				game_integration_deinit = &cyberpunk2077_deinit;
			}
			else if (proc_name == "GTA5.exe")
			{
				game_integration_init = &gta5_init;
				game_integration_deinit = &gta5_deinit;
			}
		}
		if (!game_integration_init)
		{
			MessageBoxA(0, "This game is not supported by AnalogSense. Your keyboard input remains digital.", "AnalogSense", MB_OK | MB_ICONERROR);
			return FALSE;
		}

#ifdef AS_DEBUG
		AllocConsole();
		{
			FILE* f;
			freopen_s(&f, "CONIN$", "r", stdin);
			freopen_s(&f, "CONOUT$", "w", stderr);
			freopen_s(&f, "CONOUT$", "w", stdout);
		}
#endif

		wooting_lib = LoadLibraryA("wooting_analog_sdk");
		if (!wooting_lib)
		{
			MessageBoxA(0, "Failed to load Wooting Analog SDK.", "AnalogSense", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		if (((wooting_analog_initialise_t)GetProcAddress(wooting_lib, "wooting_analog_initialise"))() < 0)
		{
			MessageBoxA(0, "Failed to initialize Wooting Analog SDK.", "AnalogSense", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		((wooting_analog_set_keycode_mode_t)GetProcAddress(wooting_lib, "wooting_analog_set_keycode_mode"))(2); // VirtualKey
		wooting_analog_read_analog = (wooting_analog_read_analog_t)GetProcAddress(wooting_lib, "wooting_analog_read_analog");
		wooting_analog_read_full_buffer = (wooting_analog_read_full_buffer_t)GetProcAddress(wooting_lib, "wooting_analog_read_full_buffer");

		game_integration_init();
	}
	return TRUE;
}

static int unload_counter = 0;

void analogsense_on_input_tick()
{
	SOUP_IF_UNLIKELY (unload_counter == -1)
	{
		return;
	}

	if (GetAsyncKeyState(VK_DELETE) & 0x8000)
	{
		if (++unload_counter == 200)
		{
			std::cout << "unloading" << std::endl;
			unload_counter = -1;
			CreateThread(nullptr, 0, [](PVOID) -> DWORD
			{
#ifndef AS_DEBUG
				MessageBoxA(0, "AnalogSense detected [Delete] being held and will now unload.", "AnalogSense", MB_OK | MB_ICONINFORMATION);
#endif

				if (game_integration_deinit)
				{
					game_integration_deinit();
				}

				((wooting_analog_uninitialise_t)GetProcAddress(wooting_lib, "wooting_analog_uninitialise"))();
				FreeLibrary(wooting_lib);

#ifdef AS_DEBUG
				FreeConsole();
#endif

				FreeLibraryAndExitThread(this_lib, 0);
			}, 0, 0, nullptr);
		}
	}
	else
	{
		unload_counter = 0;
	}
}
