/**
 * @file mq2_prevention.cpp
 * @brief Implementation of Mq2Prevention mod.
 *
 * Prevents other MacroQuest2 instances from loading alongside our DLL.
 * Uses three detection methods:
 *
 *   1. Module detection — checks for MQ2Main.dll and other known MQ2 modules
 *   2. Window detection — checks for the MQ2 injector window class
 *   3. Mutex — creates a named mutex to signal our presence
 *
 * If MQ2 is detected, we log a warning and optionally notify the user via
 * the game chat window. We do NOT forcefully unload foreign DLLs — that
 * could crash the client.
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "mq2_prevention.h"
#include "../core.h"

#include <cstdint>

// Known MQ2 module names
static const char* s_mq2Modules[] = {
    "MQ2Main.dll",
    "MQ2AutoLogin.dll",
    "MQ2Map.dll",
    "MQ2ChatWnd.dll",
    "MQ2HUD.dll",
    "MQ2ItemDisplay.dll",
};
static constexpr int s_mq2ModuleCount = sizeof(s_mq2Modules) / sizeof(s_mq2Modules[0]);

// MQ2 injector window class name (from MQ2 source)
static const char* s_mq2WndClassName = "MacroQuest2";

// Our mutex name — signals that our framework is loaded
static const char* s_mutexName = "THJ_DInput8_Proxy_Active";

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* Mq2Prevention::GetName() const
{
    return "Mq2Prevention";
}

bool Mq2Prevention::Initialize()
{
    LogFramework("Mq2Prevention: Initializing...");

    // Create our mutex to signal presence
    CreateMutex();

    // Initial check for MQ2
    if (CheckForMQ2())
    {
        LogFramework("Mq2Prevention: WARNING — MQ2 detected at startup!");
        LogFramework("Mq2Prevention: This may cause conflicts and instability.");
    }
    else
    {
        LogFramework("Mq2Prevention: No MQ2 detected — OK");
    }

    LogFramework("Mq2Prevention: Initialized");
    return true;
}

void Mq2Prevention::Shutdown()
{
    if (m_hMutex)
    {
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }

    LogFramework("Mq2Prevention: Shutdown");
}

void Mq2Prevention::OnPulse()
{
    // Periodic check every ~5 seconds (assuming ~60fps = 300 frames)
    if (++m_pulseCount >= 300)
    {
        m_pulseCount = 0;

        if (CheckForMQ2())
        {
            WriteChatf("\arWARNING: MacroQuest2 detected! This may cause conflicts with THJ.");
            LogFramework("Mq2Prevention: WARNING — MQ2 detected during runtime!");
        }
    }
}

bool Mq2Prevention::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}

// ---------------------------------------------------------------------------
// Detection methods
// ---------------------------------------------------------------------------

bool Mq2Prevention::CheckForMQ2()
{
    // Method 1: Check for known MQ2 modules
    for (int i = 0; i < s_mq2ModuleCount; ++i)
    {
        HMODULE hMod = GetModuleHandleA(s_mq2Modules[i]);
        if (hMod)
        {
            LogFramework("Mq2Prevention: Found loaded module: %s at 0x%p",
                s_mq2Modules[i], hMod);
            return true;
        }
    }

    // Method 2: Check for MQ2 injector window
    HWND hWnd = FindWindowA(s_mq2WndClassName, nullptr);
    if (hWnd)
    {
        LogFramework("Mq2Prevention: Found MQ2 injector window (class '%s')", s_mq2WndClassName);
        return true;
    }

    return false;
}

void Mq2Prevention::CreateMutex()
{
    m_hMutex = ::CreateMutexA(nullptr, FALSE, s_mutexName);
    if (m_hMutex)
    {
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            LogFramework("Mq2Prevention: WARNING — Our mutex already exists (another instance?)");
        }
        else
        {
            LogFramework("Mq2Prevention: Mutex created: %s", s_mutexName);
        }
    }
    else
    {
        LogFramework("Mq2Prevention: WARNING — Failed to create mutex: %lu", GetLastError());
    }
}
