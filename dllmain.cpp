// dllmain.cpp : dinput8.dll proxy for ROF2 EverQuest client
//
// This DLL acts as a transparent proxy: the game loads our dinput8.dll from its
// directory, and we forward every DirectInput call to the real system DLL.
// Logging is written to dinput8_proxy.log in the game directory for verification.

#include "pch.h"
#include "core.h"
#include <cstdio>
#include <ctime>

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
static FILE*   g_logFile      = nullptr;

// ---------------------------------------------------------------------------
// Logging helper — writes timestamped lines to dinput8_proxy.log
// ---------------------------------------------------------------------------
static void LogMessage(const char* fmt, ...)
{
    if (!g_logFile)
        return;

    // Timestamp
    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);
    fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);

    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

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

        // Open log file in the game directory (current working directory)
        fopen_s(&g_logFile, "dinput8_proxy.log", "w");
        LogMessage("=== dinput8 proxy DLL loaded ===");
        LogMessage("DLL_PROCESS_ATTACH: hModule=0x%p", hModule);

        // Load the real dinput8.dll from the system directory.
        // GetSystemDirectoryA returns SysWOW64 for 32-bit processes on 64-bit Windows,
        // which is exactly where the real 32-bit dinput8.dll lives.
        char systemPath[MAX_PATH];
        GetSystemDirectoryA(systemPath, MAX_PATH);
        strcat_s(systemPath, "\\dinput8.dll");

        LogMessage("Loading real DLL: %s", systemPath);
        g_hRealDInput8 = LoadLibraryA(systemPath);

        if (!g_hRealDInput8)
        {
            LogMessage("FATAL: Failed to load real dinput8.dll! Error=%lu", GetLastError());
            return FALSE;
        }
        LogMessage("Real DLL loaded at 0x%p", g_hRealDInput8);

        // Resolve all 6 export addresses
        g_pDirectInput8Create = (DirectInput8CreateProc)GetProcAddress(g_hRealDInput8, "DirectInput8Create");
        g_pDllCanUnloadNow    = (DllCanUnloadNowProc)GetProcAddress(g_hRealDInput8, "DllCanUnloadNow");
        g_pDllGetClassObject  = (DllGetClassObjectProc)GetProcAddress(g_hRealDInput8, "DllGetClassObject");
        g_pDllRegisterServer  = (DllRegisterServerProc)GetProcAddress(g_hRealDInput8, "DllRegisterServer");
        g_pDllUnregisterServer = (DllUnregisterServerProc)GetProcAddress(g_hRealDInput8, "DllUnregisterServer");
        g_pGetdfDIJoystick    = (GetdfDIJoystickProc)GetProcAddress(g_hRealDInput8, "GetdfDIJoystick");

        LogMessage("Resolved exports:");
        LogMessage("  DirectInput8Create  = 0x%p %s", g_pDirectInput8Create, g_pDirectInput8Create ? "OK" : "MISSING");
        LogMessage("  DllCanUnloadNow     = 0x%p %s", g_pDllCanUnloadNow,    g_pDllCanUnloadNow    ? "OK" : "MISSING");
        LogMessage("  DllGetClassObject   = 0x%p %s", g_pDllGetClassObject,  g_pDllGetClassObject  ? "OK" : "MISSING");
        LogMessage("  DllRegisterServer   = 0x%p %s", g_pDllRegisterServer,  g_pDllRegisterServer  ? "OK" : "MISSING");
        LogMessage("  DllUnregisterServer = 0x%p %s", g_pDllUnregisterServer,g_pDllUnregisterServer? "OK" : "MISSING");
        LogMessage("  GetdfDIJoystick     = 0x%p %s", g_pGetdfDIJoystick,    g_pGetdfDIJoystick    ? "OK" : "MISSING");
        LogMessage("Proxy initialization complete.");

        // Launch framework init thread — waits for game window, then hooks
        CreateThread(NULL, 0, &InitThread, NULL, 0, NULL);
        LogMessage("Framework init thread launched.");
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        LogMessage("DLL_PROCESS_DETACH: Shutting down proxy.");

        // Shutdown framework before freeing the real DLL
        Core::Shutdown();

        if (g_hRealDInput8)
        {
            FreeLibrary(g_hRealDInput8);
            g_hRealDInput8 = nullptr;
            LogMessage("Real DLL freed.");
        }

        LogMessage("=== dinput8 proxy DLL unloaded ===");
        if (g_logFile)
        {
            fclose(g_logFile);
            g_logFile = nullptr;
        }
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
    LogMessage("DirectInput8Create called: hinst=0x%p, dwVersion=0x%08X", hinst, dwVersion);

    if (!g_pDirectInput8Create)
    {
        LogMessage("  ERROR: real DirectInput8Create is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    LogMessage("  Result: 0x%08X, ppvOut=0x%p", hr, ppvOut ? *ppvOut : nullptr);
    return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    LogMessage("DllCanUnloadNow called");

    if (!g_pDllCanUnloadNow)
    {
        LogMessage("  ERROR: real DllCanUnloadNow is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllCanUnloadNow();
    LogMessage("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    LogMessage("DllGetClassObject called");

    if (!g_pDllGetClassObject)
    {
        LogMessage("  ERROR: real DllGetClassObject is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllGetClassObject(rclsid, riid, ppv);
    LogMessage("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllRegisterServer()
{
    LogMessage("DllRegisterServer called");

    if (!g_pDllRegisterServer)
    {
        LogMessage("  ERROR: real DllRegisterServer is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllRegisterServer();
    LogMessage("  Result: 0x%08X", hr);
    return hr;
}

extern "C" HRESULT WINAPI DllUnregisterServer()
{
    LogMessage("DllUnregisterServer called");

    if (!g_pDllUnregisterServer)
    {
        LogMessage("  ERROR: real DllUnregisterServer is NULL!");
        return E_FAIL;
    }

    HRESULT hr = g_pDllUnregisterServer();
    LogMessage("  Result: 0x%08X", hr);
    return hr;
}

extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick()
{
    LogMessage("GetdfDIJoystick called");

    if (!g_pGetdfDIJoystick)
    {
        LogMessage("  ERROR: real GetdfDIJoystick is NULL!");
        return nullptr;
    }

    LPCDIDATAFORMAT result = g_pGetdfDIJoystick();
    LogMessage("  Result: 0x%p", result);
    return result;
}
