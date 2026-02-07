#pragma once

#include "mods/mod_interface.h"
#include <memory>

// Logging function used by core and hooks modules.
// Writes timestamped lines to dinput8_proxy.log.
void LogFramework(const char* fmt, ...);

namespace Core
{

// Register a mod to be managed by the framework.
// Call before Initialize().
void RegisterMod(std::unique_ptr<IMod> mod);

// Called from the init thread once the game window is ready.
// Initializes all mods, then installs hooks.
void Initialize();

// Called from DLL_PROCESS_DETACH.
// Removes all hooks, then shuts down all mods.
void Shutdown();

} // namespace Core

// Init thread entry point — polls for game window, then calls Core::Initialize().
DWORD WINAPI InitThread(LPVOID lpParam);
