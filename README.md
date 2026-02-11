# ROF2 Unrestricted Classes

A dinput8.dll proxy for the ROF2 EverQuest client that removes class restrictions on spells, combat abilities, and item equipping. Copy the built DLL into the same folder as your eqgame.exe and it unlocks UI components that are normally restricted by class — pure casters (Enchanter, Magician, Necromancer, Wizard) gain access to Combat Abilities, and pure melee classes gain access to the Spell Book. Designed for custom servers running classless or multiclass rulesets.

## What It Does

The proxy DLL loads transparently alongside eqgame.exe and applies two mods at runtime:

### SpellbookUnlock

Hooks 6 game functions so that every class can scribe and memorize any spell, and equip any item:

| Hook | Effect |
|------|--------|
| `IsSpellcaster` (x3 variants) | Always returns true — enables spell gems for all classes |
| `GetSpellLevelNeeded` | Returns 1 — all spells usable at level 1 |
| `CanStartMemming` | Always allows spell memorization |
| `CanUseItem` | Bypasses class/race restrictions on item equipping |

### CombatAbilities

Patches a conditional jump (`JE` → `NOP NOP`) in eqgame.exe that normally prevents pure caster classes from opening the Combat Abilities window. This is a one-shot memory patch — no hooks or per-frame work.

## How It Works

The project builds a 32-bit `dinput8.dll` that the game loads from its own directory instead of the system copy. All six DirectInput exports are forwarded to the real DLL, so input works normally.

On load, a background thread waits for the game window to appear, then:

1. Resolves function addresses using [eqlib](https://github.com/macroquest/eqlib) offset definitions (with ASLR adjustment)
2. Initializes each registered mod
3. Installs 10 framework hooks via [Microsoft Detours](https://github.com/microsoft/Detours) (game loop, world messages, spawn tracking, slash commands, UI lifecycle)

Mods implement the `IMod` interface and are registered in `dllmain.cpp`. The framework dispatches events (pulse, incoming messages, spawn add/remove, game state changes, UI reload) to all registered mods each frame.

## Quick Start

```
git clone --recurse-submodules https://github.com/CerveloFellow/rof2-unrestricted-classes.git
cd rof2-unrestricted-classes
vcpkg\bootstrap-vcpkg.bat
MSBuild.exe dinput8.sln /p:Configuration=Release /p:Platform=Win32
```

Copy `build\bin\release\dinput8.dll` to your ROF2 client directory (next to `eqgame.exe`).

See [BUILD.md](BUILD.md) for detailed build instructions, prerequisites, and troubleshooting.

## Verifying

1. Launch `eqgame.exe`
2. Check `dinput8_proxy.log` in the game directory for:
   - All 6 DirectInput exports resolved
   - SpellbookUnlock: 6 hooks installed
   - CombatAbilities: patch applied
3. In-game: any class can scribe/memorize all spells, equip all items, and open the Combat Abilities window

## Project Structure

```
├── dllmain.cpp              # DLL entry point, proxy exports, mod registration
├── core.{h,cpp}             # Framework — mod registry, hook dispatch, logging
├── hooks.{h,cpp}            # Detour management (MS Detours)
├── memory.h                 # Memory read/write/patch helpers
├── mods/
│   ├── mod_interface.h      # IMod abstract base class
│   ├── spellbook_unlock.*   # Spell/item class restriction bypass (hooks)
│   └── combat_abilities.*   # Combat Abilities window unlock (memory patch)
├── game_state.{h,cpp}       # Game global pointer resolution
├── commands.{h,cpp}         # Slash command registry
├── proxy.h, framework.h     # DLL proxy infrastructure
├── pch.{h,cpp}              # Precompiled header
├── eqlib/                   # Submodule — EQ struct/offset definitions
└── vcpkg/                   # Submodule — package manager (provides MS Detours)
```

## Adding a New Mod

1. Create `mods/your_mod.h` and `mods/your_mod.cpp` implementing `IMod`
2. Add `#include "mods/your_mod.h"` and `Core::RegisterMod(std::make_unique<YourMod>())` in `dllmain.cpp`
3. Add both files to `dinput8.vcxproj` (`<ClInclude>` and `<ClCompile>` groups)

The `IMod` interface provides these hooks:

- `Initialize()` / `Shutdown()` — one-time setup and teardown
- `OnPulse()` — called every game frame
- `OnIncomingMessage()` — intercept/suppress world messages
- `OnAddSpawn()` / `OnRemoveSpawn()` — spawn tracking
- `OnAddGroundItem()` / `OnRemoveGroundItem()` — ground item tracking
- `OnSetGameState()` — game state transitions (zoning, char select)
- `OnCleanUI()` / `OnReloadUI()` — UI lifecycle

## Known Issues

- **Combat Abilities button in Window Selector is still disabled for pure casters.** The CombatAbilities mod patches the code path that opens the window, but the Window Selector button remains grayed out. Use the keyboard shortcut to open it instead (Alt+C by default).
- **#mystats page does not show mana/endurance for classes that do not natively have them.** Pure melee classes (Warrior, Rogue, Monk, etc.) will not display the mana/endurance labels at all on the #mystats inventory page because the client's stat functions have internal class checks that return 0 for non-casters, and the UI hides the labels entirely when the values are 0.

## License

This project is for use with private/custom EverQuest servers only.
