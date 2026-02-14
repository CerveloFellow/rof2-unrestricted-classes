/**
 * @file cpu_speed_fix.cpp
 * @brief Implementation of CpuSpeedFix mod.
 *
 * Two fixes:
 *
 *   1. CPU Affinity (ported from macroquest-emu EmuExtensions.cpp)
 *      Sets the process affinity mask to all available cores, preventing
 *      Windows from migrating the game between cores which causes RDTSC
 *      discontinuities on older CPUs.
 *
 *   2. QueryPerformanceCounter normalization
 *      Hooks QPC to detect and correct discontinuities caused by CPU
 *      frequency scaling (SpeedStep/Turbo Boost). When EQ was built (2013),
 *      RDTSC was not invariant on all CPUs, so the game's tick-rate math
 *      would break when the CPU changed speed.
 *
 *      The Pyrelight DLL strings tell us:
 *        "cpu has CMPXCHG8 enabled"
 *        "cpu is not candidate for speed fix"
 *        "cpu speed fix needed, applying trampoline"
 *        "MQ2CpuSpeedFix adjusting CPU ticks per ms from %llu to %llu"
 *
 *      Modern Windows provides invariant QPC (uses HPET/TSC_INVARIANT),
 *      so this fix is primarily for older hardware. We hook QPC and smooth
 *      out any backward jumps.
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "cpu_speed_fix.h"
#include "../core.h"
#include "../hooks.h"

#include <cstdint>
#include <cmath>
#include <intrin.h>

// ---------------------------------------------------------------------------
// QPC hook — smooth out backward time jumps
// ---------------------------------------------------------------------------

using QueryPerformanceCounter_t = BOOL(WINAPI*)(LARGE_INTEGER* lpPerformanceCount);
static QueryPerformanceCounter_t QPC_Original = nullptr;

static LARGE_INTEGER s_lastQpcValue = {};
static LARGE_INTEGER s_qpcFrequency = {};
static bool s_qpcInitialized = false;

static BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER* lpPerformanceCount)
{
    BOOL result = QPC_Original(lpPerformanceCount);

    if (result && lpPerformanceCount)
    {
        // Detect backward jumps (clock went backwards due to CPU frequency change)
        if (s_qpcInitialized && lpPerformanceCount->QuadPart < s_lastQpcValue.QuadPart)
        {
            // Clamp to the last known good value + 1 tick to keep time moving forward
            lpPerformanceCount->QuadPart = s_lastQpcValue.QuadPart + 1;
        }

        s_lastQpcValue = *lpPerformanceCount;
        s_qpcInitialized = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// CPUID feature detection (matches Pyrelight's "cpu has CMPXCHG8 enabled")
// ---------------------------------------------------------------------------

static bool HasInvariantTSC()
{
    int cpuInfo[4] = {};

    // Check for invariant TSC support (CPUID 0x80000007, bit 8 of EDX)
    __cpuid(cpuInfo, 0x80000000);
    if (static_cast<unsigned int>(cpuInfo[0]) >= 0x80000007)
    {
        __cpuid(cpuInfo, 0x80000007);
        return (cpuInfo[3] & (1 << 8)) != 0;  // Invariant TSC bit
    }

    return false;
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* CpuSpeedFix::GetName() const
{
    return "CpuSpeedFix";
}

bool CpuSpeedFix::Initialize()
{
    LogFramework("CpuSpeedFix: Initializing...");

    // 1. Set CPU affinity to all cores
    SetCpuAffinity();

    // 2. Check if CPU speed fix is needed
    if (HasInvariantTSC())
    {
        LogFramework("CpuSpeedFix: CPU has invariant TSC — speed fix not needed");
    }
    else
    {
        LogFramework("CpuSpeedFix: CPU speed fix needed, applying trampoline");
        InstallQpcFix();
    }

    // Log CMPXCHG8B support (matches Pyrelight string)
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 1);
    bool hasCmpxchg8 = (cpuInfo[3] & (1 << 8)) != 0;
    LogFramework("CpuSpeedFix: cpu has CMPXCHG8 %s", hasCmpxchg8 ? "enabled" : "disabled");

    LogFramework("CpuSpeedFix: Initialized");
    return true;
}

void CpuSpeedFix::Shutdown()
{
    if (m_qpcFixInstalled)
        Hooks::Remove("QueryPerformanceCounter");

    LogFramework("CpuSpeedFix: Shutdown");
}

void CpuSpeedFix::OnPulse()
{
    // No per-frame work needed
}

bool CpuSpeedFix::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}

// ---------------------------------------------------------------------------
// CPU affinity fix (ported from EmuExtensions.cpp)
// ---------------------------------------------------------------------------

void CpuSpeedFix::SetCpuAffinity()
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    int cores = sysInfo.dwNumberOfProcessors;
    DWORD_PTR mask = 0;

    if (cores >= static_cast<int>(sizeof(DWORD_PTR) * 8))
    {
        mask = static_cast<DWORD_PTR>(-1);
    }
    else
    {
        mask = (static_cast<DWORD_PTR>(1) << cores) - 1;
    }

    LogFramework("CpuSpeedFix: Setting processor affinity to 0x%llX (%d logical cores)",
        static_cast<unsigned long long>(mask), cores);

    HANDLE processHandle = GetCurrentProcess();
    if (!SetProcessAffinityMask(processHandle, mask))
    {
        LogFramework("CpuSpeedFix: WARNING — Failed to set processor affinity: %lu", GetLastError());
    }
}

// ---------------------------------------------------------------------------
// QPC hook installation
// ---------------------------------------------------------------------------

bool CpuSpeedFix::InstallQpcFix()
{
    // Get the frequency for reference
    QueryPerformanceFrequency(&s_qpcFrequency);
    LogFramework("CpuSpeedFix: QPC frequency = %llu ticks/sec", s_qpcFrequency.QuadPart);

    // Get the real QPC function from kernel32
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32)
    {
        LogFramework("CpuSpeedFix: WARNING — Could not get kernel32.dll handle");
        return false;
    }

    QPC_Original = reinterpret_cast<QueryPerformanceCounter_t>(
        GetProcAddress(hKernel32, "QueryPerformanceCounter"));

    if (!QPC_Original)
    {
        LogFramework("CpuSpeedFix: WARNING — Could not resolve QueryPerformanceCounter");
        return false;
    }

    if (Hooks::Install("QueryPerformanceCounter",
        reinterpret_cast<void**>(&QPC_Original),
        reinterpret_cast<void*>(&QueryPerformanceCounter_Detour)))
    {
        m_qpcFixInstalled = true;
        LogFramework("CpuSpeedFix: QPC smoothing hook installed");
        return true;
    }

    LogFramework("CpuSpeedFix: WARNING — Failed to install QPC hook");
    return false;
}
