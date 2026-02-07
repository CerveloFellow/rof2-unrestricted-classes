/**
 * @file proxy.h
 * @brief Function pointer typedefs and externs for proxying the real dinput8.dll exports.
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

// Function pointer typedefs for the 6 dinput8.dll exports we proxy.
// These match the real signatures from the DirectInput8 API.

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

typedef HRESULT(WINAPI* DirectInput8CreateProc)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* DllCanUnloadNowProc)();
typedef HRESULT(WINAPI* DllGetClassObjectProc)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT(WINAPI* DllRegisterServerProc)();
typedef HRESULT(WINAPI* DllUnregisterServerProc)();
typedef LPCDIDATAFORMAT(WINAPI* GetdfDIJoystickProc)();

// Global function pointers to the real dinput8.dll exports.
// Loaded in DllMain(DLL_PROCESS_ATTACH).
extern DirectInput8CreateProc   g_pDirectInput8Create;
extern DllCanUnloadNowProc      g_pDllCanUnloadNow;
extern DllGetClassObjectProc    g_pDllGetClassObject;
extern DllRegisterServerProc    g_pDllRegisterServer;
extern DllUnregisterServerProc  g_pDllUnregisterServer;
extern GetdfDIJoystickProc      g_pGetdfDIJoystick;
