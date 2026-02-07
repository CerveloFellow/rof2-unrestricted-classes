#include "pch.h"
#include "core.h"
#include "hooks.h"
#include "memory.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// eqlib extern definitions
//
// eqlib headers declare these via EQLIB_VAR (extern "C") and EQLIB_OBJECT
// but we don't link eqlib.lib, so we provide the storage here.
//
// EQGameBaseAddress: declared as `extern "C" uintptr_t` in namespace eqlib
//   — extern "C" means the symbol has C linkage regardless of namespace
// InitBaseAddress:   declared as plain `void InitBaseAddress()` in namespace eqlib
//   — EQLIB_OBJECT is empty with EQLIB_STATIC, so it's C++ linkage in namespace eqlib
// ---------------------------------------------------------------------------
extern "C" uintptr_t EQGameBaseAddress = 0;

namespace eqlib
{
    void InitBaseAddress()
    {
        EQGameBaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    }
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_frameworkLog = nullptr;

static void OpenLog()
{
    if (!g_frameworkLog)
        fopen_s(&g_frameworkLog, "dinput8_proxy.log", "a");
}

void LogFramework(const char* fmt, ...)
{
    OpenLog();
    if (!g_frameworkLog)
        return;

    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);
    fprintf(g_frameworkLog, "[%04d-%02d-%02d %02d:%02d:%02d] [framework] ",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_frameworkLog, fmt, args);
    va_end(args);

    fprintf(g_frameworkLog, "\n");
    fflush(g_frameworkLog);
}

// ---------------------------------------------------------------------------
// Mod registry
// ---------------------------------------------------------------------------
static std::vector<std::unique_ptr<IMod>> s_mods;
static bool s_initialized = false;

// ---------------------------------------------------------------------------
// Hook addresses and originals
// ---------------------------------------------------------------------------

// ProcessGameEvents — free function: int __cdecl ProcessGameEvents()
using ProcessGameEvents_t = int(__cdecl*)();
static ProcessGameEvents_t ProcessGameEvents_Original = nullptr;

// HandleWorldMessage — member of CEverQuest (thiscall on x86)
// Signature: unsigned char CEverQuest::HandleWorldMessage(UdpConnection*, uint32_t opcode, char* buffer, uint32_t size)
// We use the __fastcall trick: first param = this (ecx), second = edx (unused)
using HandleWorldMessage_t = unsigned char(__fastcall*)(void* thisPtr, void* edx,
    void* connection, uint32_t opcode, char* buffer, uint32_t size);
static HandleWorldMessage_t HandleWorldMessage_Original = nullptr;

// ---------------------------------------------------------------------------
// Detour implementations
// ---------------------------------------------------------------------------

static int __cdecl ProcessGameEvents_Detour()
{
    int result = ProcessGameEvents_Original();

    for (auto& mod : s_mods)
        mod->OnPulse();

    return result;
}

static unsigned char __fastcall HandleWorldMessage_Detour(
    void* thisPtr, void* edx,
    void* connection, uint32_t opcode, char* buffer, uint32_t size)
{
    for (auto& mod : s_mods)
    {
        if (!mod->OnIncomingMessage(opcode, buffer, size))
            return 0;
    }

    return HandleWorldMessage_Original(thisPtr, edx, connection, opcode, buffer, size);
}

// ---------------------------------------------------------------------------
// Address resolution
// ---------------------------------------------------------------------------

// HandleWorldMessage raw offset (not in eqlib offsets file for ROF2)
#define CEverQuest__HandleWorldMessage_x  0x004C3250

// ---------------------------------------------------------------------------
// Core implementation
// ---------------------------------------------------------------------------

namespace Core
{

void RegisterMod(std::unique_ptr<IMod> mod)
{
    LogFramework("Registered mod: %s", mod->GetName());
    s_mods.push_back(std::move(mod));
}

void Initialize()
{
    if (s_initialized)
        return;
    s_initialized = true;

    LogFramework("=== Framework initializing ===");

    // Resolve base address (initializes EQGameBaseAddress via eqlib)
    eqlib::InitBaseAddress();
    LogFramework("EQGameBaseAddress = 0x%08X", static_cast<unsigned int>(EQGameBaseAddress));

    // Resolve ProcessGameEvents address using eqlib's FixEQGameOffset
    uintptr_t pgeAddr = eqlib::FixEQGameOffset(__ProcessGameEvents_x);
    ProcessGameEvents_Original = reinterpret_cast<ProcessGameEvents_t>(pgeAddr);
    LogFramework("ProcessGameEvents = 0x%08X", static_cast<unsigned int>(pgeAddr));

    // Resolve HandleWorldMessage address (manual calculation — not in eqlib offsets)
    uintptr_t hwmAddr = static_cast<uintptr_t>(CEverQuest__HandleWorldMessage_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    HandleWorldMessage_Original = reinterpret_cast<HandleWorldMessage_t>(hwmAddr);
    LogFramework("HandleWorldMessage = 0x%08X", static_cast<unsigned int>(hwmAddr));

    // Initialize all mods before installing hooks
    for (auto& mod : s_mods)
    {
        LogFramework("Initializing mod: %s", mod->GetName());
        if (!mod->Initialize())
            LogFramework("  WARNING: mod '%s' failed to initialize", mod->GetName());
    }

    // Install hooks
    Hooks::Install("ProcessGameEvents",
        reinterpret_cast<void**>(&ProcessGameEvents_Original),
        reinterpret_cast<void*>(&ProcessGameEvents_Detour));

    Hooks::Install("HandleWorldMessage",
        reinterpret_cast<void**>(&HandleWorldMessage_Original),
        reinterpret_cast<void*>(&HandleWorldMessage_Detour));

    LogFramework("=== Framework initialized — hooks installed ===");
}

void Shutdown()
{
    if (!s_initialized)
        return;
    s_initialized = false;

    LogFramework("=== Framework shutting down ===");

    // Remove hooks before shutting down mods
    Hooks::RemoveAll();

    // Shutdown all mods
    for (auto& mod : s_mods)
    {
        LogFramework("Shutting down mod: %s", mod->GetName());
        mod->Shutdown();
    }
    s_mods.clear();

    LogFramework("=== Framework shutdown complete ===");

    if (g_frameworkLog)
    {
        fclose(g_frameworkLog);
        g_frameworkLog = nullptr;
    }
}

} // namespace Core

// ---------------------------------------------------------------------------
// Init thread — waits for game window, then initializes the framework
// ---------------------------------------------------------------------------
DWORD WINAPI InitThread(LPVOID lpParam)
{
    // Poll until the game window handle is valid.
    // __HWnd_x (0xE678A0) is a fixed offset holding the HWND.
    // We resolve it with ASLR and read the pointer from game memory.
    uintptr_t hwndAddr = eqlib::FixEQGameOffset(__HWnd_x);

    LogFramework("Init thread started — waiting for game window (HWnd @ 0x%08X)",
        static_cast<unsigned int>(hwndAddr));

    while (Memory::ReadMemory<uintptr_t>(hwndAddr) == 0)
    {
        Sleep(100);
    }

    LogFramework("Game window detected — beginning initialization");
    Core::Initialize();

    return 0;
}
