# ROF2 Unrestricted Classes — Build Guide

A dinput8.dll proxy for the ROF2 EverQuest client that removes class restrictions on spells, memorization, and item equipping. The **SpellbookUnlock** mod hooks the game client so that every class can cast any spell and equip any item.

## Prerequisites

- **Visual Studio 2026** with the "Desktop development with C++" workload (includes v145 toolset and Windows SDK)
- **Git** with submodule support

## Clone & Setup

```
git clone --recurse-submodules https://github.com/CerveloFellow/rof2-unrestricted-classes.git
cd rof2-unrestricted-classes
vcpkg/bootstrap-vcpkg.bat
```

The `--recurse-submodules` flag fetches the `eqlib` and `vcpkg` submodules. The bootstrap script downloads the vcpkg binary (one-time setup).

If you already cloned without `--recurse-submodules`:

```
git submodule update --init --recursive
vcpkg/bootstrap-vcpkg.bat
```

## Build

On the first build, vcpkg will automatically download and compile Microsoft Detours. Subsequent builds skip this step.

### From Visual Studio

Open `dinput8.sln` and build **Debug|Win32** or **Release|Win32**.

### From Developer Command Prompt

```
MSBuild.exe dinput8.sln /p:Configuration=Debug /p:Platform=Win32
```

### From MSYS2 / Git Bash

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
   - Framework: "Framework initializing", base address + hook addresses logged
   - SpellbookUnlock: 6 hooks installed (IsSpellcaster ×3, GetSpellLevelNeeded, CanStartMemming, CanUseItem)
3. In-game: any class should be able to scribe/memorize all spells and equip all items

## Project Structure

```
├── dllmain.cpp              # DLL entry point, proxy exports, mod registration
├── core.{h,cpp}             # Framework core — mod registry, hooks, logging
├── hooks.{h,cpp}            # Detour management (MS Detours)
├── mods/
│   ├── mod_interface.h      # IMod abstract base class
│   ├── spellbook_unlock.h   # SpellbookUnlock mod header
│   └── spellbook_unlock.cpp # Hook implementations for class restriction bypass
├── game_state.{h,cpp}       # Game global pointer resolution
├── commands.{h,cpp}         # Slash command registry
├── memory.h                 # Memory read/write helpers
├── proxy.h, framework.h     # DLL proxy infrastructure
├── pch.{h,cpp}              # Precompiled header
├── eqlib/                   # Submodule — EQ struct/offset definitions (headers-only)
└── vcpkg/                   # Submodule — package manager (provides MS Detours)
```

The framework provides 10 hook points (ProcessGameEvents, HandleWorldMessage, CreatePlayer, etc.) that are pass-through when unused. New mods can be added by implementing the `IMod` interface and registering in `dllmain.cpp`.

## Notes

- The `MSYS_NO_PATHCONV=1` prefix is required in Git Bash to prevent `/p:` flags from being interpreted as Unix paths.
- The `EQLIB_STATIC` preprocessor define is set in `dinput8.props` — this enables headers-only usage of eqlib without linking eqlib.lib.
- The `vcpkg_installed/` directory is created locally by vcpkg manifest mode and is git-ignored.
- The vcpkg submodule pins a version that recognizes VS 2026 (the copy bundled with VS 2026 predates it and cannot detect it).
