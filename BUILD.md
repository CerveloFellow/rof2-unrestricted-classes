# ROF2 Client dinput8.dll - Build Guide

## Prerequisites

- Visual Studio 2022 with C++ desktop development workload (v143 toolset)
- Git with submodule support
- Windows SDK (included with Visual Studio)
- Detours library (v4.0.1) — expected at `../macroquest-emu/contrib/vcpkg/installed/x86-windows-static/`

## Clone & Setup

```
git clone --recurse-submodules https://github.com/CerveloFellow/rof2client-dinput8.git
cd rof2client-dinput8
```

If already cloned without submodules:

```
git submodule update --init --recursive
```

## Build

### From Visual Studio

Open `dinput8.sln`, retarget to v143 if prompted, then build Debug|Win32 or Release|Win32.

### From Command Line (MSYS2/Git Bash)

```bash
MSYS_NO_PATHCONV=1 "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" \
    dinput8.sln /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v143
```

### From Command Line (CMD/PowerShell)

```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
    dinput8.sln /p:Configuration=Debug /p:Platform=Win32 /p:PlatformToolset=v143
```

Output: `build\bin\debug\dinput8.dll` (relative to solution directory)

## Deploy

Copy `dinput8.dll` to the ROF2 client directory (where `eqgame.exe` lives). No other files needed — eqlib is used headers-only, no eqlib.dll required.

Optionally copy `dinput8.pdb` alongside for debug symbols.

## Verify

1. Launch `eqgame.exe`
2. Check `dinput8_proxy.log` in the game directory:
   - Proxy layer: all 6 DirectInput exports resolved "OK"
   - Framework: "Framework initializing", base address + hook addresses logged, "hooks installed"
3. Game should behave identically to without the proxy (no mods registered yet, hooks are pass-through)

## Notes

- The vcxproj specifies PlatformToolset v145 which may not be installed. Override with `/p:PlatformToolset=v143` or retarget in Visual Studio.
- In MSYS2/Git Bash, prefix the MSBuild command with `MSYS_NO_PATHCONV=1` so `/p:` flags aren't interpreted as file paths.
- The `EQLIB_STATIC` preprocessor define is set in `dinput8.props` — this enables headers-only usage of eqlib without linking eqlib.lib.
