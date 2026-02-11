/**
 * @file dllmain.cpp
 * @brief DLL entry point and exported proxy functions for the dinput8.dll proxy.
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 *
 * This DLL acts as a transparent proxy: the game loads our dinput8.dll from its
 * directory, and we forward every DirectInput call to the real system DLL.
 * Mod registration and framework init thread launch happen in DLL_PROCESS_ATTACH.
 */

#include "pch.h"
#include "core.h"
#include "mods/spellbook_unlock.h"
#include "mods/combat_abilities.h"
#include "mods/stats_override.h"
#include <memory>

// ---------------------------------------------------------------------------
// Global function pointers to the real dinput8.dll (defined in proxy.h as extern)
// ---------------------------------------------------------------------------
DirectInput8CreateProc   g_pDirectInput8Create   = nullptr;
DllCanUnloadNowProc      g_pDllCanUnloadNow      = nullptr;
DllGetClassObjectProc    g_pDllGetClassObject     = nullptr;
DllRegisterServerProc    g_pDllRegisterServer     = nullptr;
DllUnregisterServerProc  g_pDllUnregisterServer   = nullptr;
GetdfDIJoystickProc      g_pGetdfDIJoystick       = nullptr;

static HMODULE g_hRealDInput8 = nullptr;

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);

        LogFramework("=== dinput8 proxy DLL loaded ===");
        LogFramework("DLL_PROCESS_ATTACH: hModule=0x%p", hModule);

        // Load the real dinput8.dll from the system directory.
        // GetSystemDirectoryA returns SysWOW64 for 32-bit processes on 64-bit Windows,
        // which is exactly where the real 32-bit dinput8.dll lives.
        char systemPath[MAX_PATH];
        GetSystemDirectoryA(systemPath, MAX_PATH);
        strcat_s(systemPath, "\\dinput8.dll");

        LogFramework("Loading real DLL: %s", systemPath);
        g_hRealDInput8 = LoadLibraryA(systemPath);

        if (!g_hRealDInput8)
        {
            LogFramework("FATAL: Failed to load real dinput8.dll! Error=%lu", GetLastError());
            return FALSE;
        }
        LogFramework("Real DLL loaded at 0x%p", g_hRealDInput8);

        // Resolve all 6 export addresses
        g_pDirectInput8Create = (DirectInput8CreateProc)GetProcAddress(g_hRealDInput8, "DirectInput8Create");
        g_pDllCanUnloadNow    = (DllCanUnloadNowProc)GetProcAddress(g_hRealDInput8, "DllCanUnloadNow");
        g_pDllGetClassObject  = (DllGetClassObjectProc)GetProcAddress(g_hRealDInput8, "DllGetClassObject");
        g_pDllRegisterServer  = (DllRegisterServerProc)GetProcAddress(g_hRealDInput8, "DllRegisterServer");
        g_pDllUnregisterServer = (DllUnregisterServerProc)GetProcAddress(g_hRealDInput8, "DllUnregisterServer");
        g_pGetdfDIJoystick    = (GetdfDIJoystickProc)GetProcAddress(g_hRealDInput8, "GetdfDIJoystick");

        LogFramework("Resolved exports:");
        LogFramework("  DirectInput8Create  = 0x%p %s", g_pDirectInput8Create, g_pDirectInput8Create ? "OK" : "MISSING");
        LogFramework("  DllCanUnloadNow     = 0x%p %s", g_pDllCanUnloadNow,    g_pDllCanUnloadNow    ? "OK" : "MISSING");
        LogFramework("  DllGetClassObject   = 0x%p %s", g_pDllGetClassObject,  g_pDllGetClassObject  ? "OK" : "MISSING");
        LogFramework("  DllRegisterServer   = 0x%p %s", g_pDllRegisterServer,  g_pDllRegisterServer  ? "OK" : "MISSING");
        LogFramework("  DllUnregisterServer = 0x%p %s", g_pDllUnregisterServer,g_pDllUnregisterServer? "OK" : "MISSING");
        LogFramework("  GetdfDIJoystick     = 0x%p %s", g_pGetdfDIJoystick,    g_pGetdfDIJoystick    ? "OK" : "MISSING");
        LogFramework("Proxy initialization complete.");

        // Register mods before launching init thread
        Core::RegisterMod(std::make_unique<SpellbookUnlock>());
        Core::RegisterMod(std::make_unique<CombatAbilities>());
        Core::RegisterMod(std::make_unique<StatsOverride>());

        // Launch framework init thread — waits for game window, then hooks
        CreateThread(NULL, 0, &InitThread, NULL, 0, NULL);
        LogFramework("Framework init thread launched.");
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        LogFramework("DLL_PROCESS_DETACH: Shutting down proxy.");

        // Shutdown framework before freeing the real DLL
        Core::Shutdown();

        if (g_hRealDInput8)
        {
            FreeLibrary(g_hRealDInput8);
            g_hRealDInput8 = nullptr;
            LogFramework("Real DLL freed.");
        }

        LogFramework("=== dinput8 proxy DLL unloaded ===");
        break;
    }
    }

    return TRUE;
}

// ---------------------------------------------------------------------------
// Exported proxy functions — pure pass-through to the real DLL
// ---------------------------------------------------------------------------

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD     dwVersion,
    REFIID    riidltf,
    LPVOID*   ppvOut,
    LPUNKNOWN punkOuter)
{
    LogFramework("DirectInput8Create called: hinst=0x%p, dwVersion=0x%08X", hinst, dwVersion);

    if (!g_pDirectInput8Create)
    {
        LogFramework("  ERROR: real DirectInput8Create is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    LogFramework("  Result: 0x%08X, ppvOut=0x%p", hr, ppvOut ? *ppvOut : nullptr);
    return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    LogFramework("DllCanUnloadNow called");

    if (!g_pDllCanUnloadNow)
    {
        LogFramework("  ERROR: real DllCanUnloadNow is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllCanUnloadNow();
    LogFramework("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    LogFramework("DllGetClassObject called");

    if (!g_pDllGetClassObject)
    {
        LogFramework("  ERROR: real DllGetClassObject is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllGetClassObject(rclsid, riid, ppv);
    LogFramework("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllRegisterServer()
{
    LogFramework("DllRegisterServer called");

    if (!g_pDllRegisterServer)
    {
        LogFramework("  ERROR: real DllRegisterServer is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllRegisterServer();
    LogFramework("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllUnregisterServer()
{
    LogFramework("DllUnregisterServer called");

    if (!g_pDllUnregisterServer)
    {
        LogFramework("  ERROR: real DllUnregisterServer is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllUnregisterServer();
    LogFramework("  Result: 0x%08X", hr);
    return hr;
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick()
{
    LogFramework("GetdfDIJoystick called");

    if (!g_pGetdfDIJoystick)
    {
        LogFramework("  ERROR: real GetdfDIJoystick is NULL!");
        return nullptr;
    }

    LPCDIDATAFORMAT result = g_pGetdfDIJoystick();
    LogFramework("  Result: 0x%p", result);
    return result;
}
