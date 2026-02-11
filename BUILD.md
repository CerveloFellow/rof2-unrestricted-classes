# ROF2 Client dinput8.dll - Build Guide

## Prerequisites

- **Visual Studio 2026** with C++ desktop development workload (v145 toolset)
- **Git** with submodule support
- **Windows SDK** (included with Visual Studio)

## Clone & Setup

```
git clone --recurse-submodules https://github.com/CerveloFellow/rof2client-dinput8.git
cd rof2client-dinput8
```

If already cloned without submodules:

```
git submodule update --init --recursive
```

### Bootstrap vcpkg (one-time)

```
vcpkg\bootstrap-vcpkg.bat
```

This downloads the vcpkg binary. Detours is then automatically installed on first build via vcpkg manifest mode.

## Build

### From Visual Studio

Open `dinput8.sln` and build Debug|Win32 or Release|Win32. vcpkg will fetch and build Detours automatically on first build.

### From Command Line (Developer Command Prompt)

```
MSBuild.exe dinput8.sln /p:Configuration=Debug /p:Platform=Win32
```

### From Command Line (MSYS2/Git Bash)

```bash
MSYS_NO_PATHCONV=1 "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" \
    dinput8.sln /p:Configuration=Debug /p:Platform=Win32
```

Output: `build\bin\debug\dinput8.dll` (Debug) or `build\bin\release\dinput8.dll` (Release)

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

- In MSYS2/Git Bash, prefix the MSBuild command with `MSYS_NO_PATHCONV=1` so `/p:` flags aren't interpreted as file paths.
- The `EQLIB_STATIC` preprocessor define is set in `dinput8.props` — this enables headers-only usage of eqlib without linking eqlib.lib.
- The `vcpkg_installed/` directory is created locally by vcpkg manifest mode and is git-ignored.
- The vcpkg submodule is used instead of a system-wide vcpkg installation to ensure a version that recognizes VS 2026.
