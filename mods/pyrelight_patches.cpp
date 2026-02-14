/**
 * @file pyrelight_patches.cpp
 * @brief Implementation of PyrelightPatches mod — replicates Pyrelight DLL memory patches.
 *
 * Patches applied:
 *   1. MemChecker bypass  — patches __MemChecker0..3 to return 0 immediately,
 *      disabling the client's memory-integrity ("patchme") checks so our hooks
 *      are not detected.
 *   2. Gamma restore      — saves the display gamma ramp at startup and installs
 *      a crash handler to restore it, preventing a stuck-dark screen on crash.
 *   3. Food/drink filter  — filters out food and drink spam messages from chat
 *      by intercepting dsp_chat output.
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "pyrelight_patches.h"
#include "../core.h"
#include "../hooks.h"
#include "../memory.h"
#include "../commands.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Food/drink filter state
// ---------------------------------------------------------------------------
static bool s_filterFood = true;

static const char* s_foodDrinkPatterns[] = {
    "You are low on drink",
    "You are low on food",
    "You are out of drink",
    "You are out of food",
    "You and your mount are thirsty.",
    "You and your mount are hungry.",
    "You are hungry",
    "You are thirsty",
    "You take a bite out of",
    "You take a bite of",
    "You take a drink from",
    "Ahhh. That was tasty.",
    "Ahhh. That was refreshing.",
    "Chomp, chomp, chomp...",
    "Glug, glug, glug...",
    "You could not possibly eat any more, you would explode",
    "You could not possibly drink any more, you would explode",
    "You could not possibly consume more alcohol",
};
static constexpr int s_foodDrinkPatternCount = sizeof(s_foodDrinkPatterns) / sizeof(s_foodDrinkPatterns[0]);

// ---------------------------------------------------------------------------
// dsp_chat hook for food/drink filtering
// ---------------------------------------------------------------------------
using DspChat_t = void(__fastcall*)(void* thisPtr, void* edx,
    const char* message, int color, bool allowLog, bool doPercentConversion);
static DspChat_t DspChat_Original = nullptr;

static bool IsFoodDrinkMessage(const char* message)
{
    if (!message || !s_filterFood)
        return false;

    for (int i = 0; i < s_foodDrinkPatternCount; ++i)
    {
        if (strstr(message, s_foodDrinkPatterns[i]) != nullptr)
            return true;
    }
    return false;
}

static void __fastcall DspChat_Detour(void* thisPtr, void* edx,
    const char* message, int color, bool allowLog, bool doPercentConversion)
{
    if (IsFoodDrinkMessage(message))
        return;  // Suppress the message

    DspChat_Original(thisPtr, edx, message, color, allowLog, doPercentConversion);
}

// ---------------------------------------------------------------------------
// Gamma restore crash handler
// ---------------------------------------------------------------------------
static PyrelightPatches* s_patchInstance = nullptr;

static LONG WINAPI GammaCrashHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
    if (s_patchInstance)
        s_patchInstance->RestoreGammaRamp();

    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------
// /filterfood command
// ---------------------------------------------------------------------------
static void Cmd_FilterFood(eqlib::PlayerClient* pChar, const char* szLine)
{
    if (szLine && szLine[0] != '\0')
    {
        if (_stricmp(szLine, "on") == 0)
            s_filterFood = true;
        else if (_stricmp(szLine, "off") == 0)
            s_filterFood = false;
    }
    else
    {
        s_filterFood = !s_filterFood;
    }

    WriteChatf("Food/drink message filter: %s", s_filterFood ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* PyrelightPatches::GetName() const
{
    return "PyrelightPatches";
}

bool PyrelightPatches::Initialize()
{
    LogFramework("PyrelightPatches: Initializing...");

    // 1. MemChecker bypass — DISABLED
    // The raw xor eax,eax; ret patch causes zone-in failure. The 3-mod DLL
    // works fine without it, so MemCheckers aren't enforcing against our hooks.
    // TODO: If MemChecker detection becomes an issue, implement proper byte
    // restoration (temporarily unhook during MemChecker calls) instead of
    // permanently patching the functions.
    // PatchMemCheckers();

    // 2. Save gamma ramp and install crash handler
    SaveGammaRamp();
    if (m_gammaRampSaved)
    {
        s_patchInstance = this;
        SetUnhandledExceptionFilter(GammaCrashHandler);
        LogFramework("PyrelightPatches: Gamma crash handler installed");
    }

    // 3. Hook dsp_chat for food/drink filtering
    uintptr_t dspChatAddr = eqlib::FixEQGameOffset(CEverQuest__dsp_chat_x);
    DspChat_Original = reinterpret_cast<DspChat_t>(dspChatAddr);

    if (DspChat_Original)
    {
        Hooks::Install("DspChat_FoodFilter",
            reinterpret_cast<void**>(&DspChat_Original),
            reinterpret_cast<void*>(&DspChat_Detour));
        LogFramework("PyrelightPatches: Food/drink chat filter installed");
    }

    // 4. Register /filterfood command
    Commands::AddCommand("/filterfood", Cmd_FilterFood);

    LogFramework("PyrelightPatches: Initialized");
    return true;
}

void PyrelightPatches::Shutdown()
{
    Hooks::Remove("DspChat_FoodFilter");
    Commands::RemoveCommand("/filterfood");
    s_patchInstance = nullptr;

    if (m_gammaRampSaved)
        RestoreGammaRamp();

    LogFramework("PyrelightPatches: Shutdown");
}

void PyrelightPatches::OnPulse()
{
    // No per-frame work needed
}

bool PyrelightPatches::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;  // Pass all messages through
}

// ---------------------------------------------------------------------------
// MemChecker bypass
// ---------------------------------------------------------------------------

bool PyrelightPatches::PatchMemCheckers()
{
    // The MemChecker functions are the client's memory integrity checks.
    // Pyrelight's "disable patchme" patches these to return 0 immediately
    // so the client doesn't detect our Detours hooks in code sections.
    //
    // We patch each MemChecker function entry point with:
    //   XOR EAX, EAX   (31 C0)    — set return value to 0
    //   RET             (C3)       — return immediately
    //
    // This is a standard 3-byte patch that fits within any function prologue.

    const uint8_t retZero[] = { 0x31, 0xC0, 0xC3 };  // xor eax, eax; ret

    struct MemCheckerPatch {
        const char* name;
        uintptr_t   offset;
    };

    MemCheckerPatch patches[] = {
        { "MemChecker0", __MemChecker0_x },
        { "MemChecker1", __MemChecker1_x },
        { "MemChecker2", __MemChecker2_x },
        { "MemChecker3", __MemChecker3_x },
    };

    bool allOk = true;
    for (auto& p : patches)
    {
        uintptr_t addr = eqlib::FixEQGameOffset(p.offset);
        if (Memory::PatchMemory(addr, retZero, sizeof(retZero)))
        {
            LogFramework("PyrelightPatches: Patched %s at 0x%08X (patchme disabled)",
                p.name, static_cast<unsigned int>(addr));
        }
        else
        {
            LogFramework("PyrelightPatches: WARNING — Failed to patch %s at 0x%08X",
                p.name, static_cast<unsigned int>(addr));
            allOk = false;
        }
    }

    return allOk;
}

// ---------------------------------------------------------------------------
// Gamma ramp save/restore
// ---------------------------------------------------------------------------

void PyrelightPatches::SaveGammaRamp()
{
    HDC hDC = GetDC(nullptr);
    if (hDC)
    {
        if (GetDeviceGammaRamp(hDC, m_savedGammaRamp))
        {
            m_gammaRampSaved = true;
            LogFramework("PyrelightPatches: Gamma ramp saved");
        }
        else
        {
            LogFramework("PyrelightPatches: WARNING — Failed to save gamma ramp");
        }
        ReleaseDC(nullptr, hDC);
    }
}

void PyrelightPatches::RestoreGammaRamp()
{
    if (!m_gammaRampSaved)
        return;

    HDC hDC = GetDC(nullptr);
    if (hDC)
    {
        SetDeviceGammaRamp(hDC, m_savedGammaRamp);
        ReleaseDC(nullptr, hDC);
        LogFramework("PyrelightPatches: Gamma ramp restored");
    }
}
