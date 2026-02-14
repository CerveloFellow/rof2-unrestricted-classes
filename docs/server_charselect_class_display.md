# Server Change Request: Show Class Abbreviations on Character Select

## Problem

On the **character select screen**, multiclass characters display as **"100 Hero"** (patched by our DLL from "Unknown Class"). We want it to show **"100 SHM/NEC/MAG"** (abbreviated class names).

## Current Behavior

The character select screen calls `CEverQuest::GetClassDesc(classId)` to render each character's class. The server currently sends `classId = 255` for multiclass characters. Our DLL intercepts this and shows "Hero" as a fallback.

## Required Server Change

In the character select packet, change the `class` field for multiclass characters from `255` to:

```
class = classBitmask + 1000
```

One-line server change. No new opcodes. The client DLL already handles decoding.

### What the Server Sends Today

```
Character: Morsal
  Level: 100
  Class: 255        <-- sentinel, DLL shows "Hero"
```

### What the Server Should Send

```
Character: Morsal
  Level: 100
  Class: 6632       <-- 5632 (bitmask) + 1000 (offset)
```

## How to Build the Bitmask

The bitmask is the same value the server already computes for `eStatClassesBitmask` (key 1) in `OP_EdgeStats`. Each class sets one bit:

```
bitmask |= (1 << (classId - 1))
```

| Class | ID | Bit to Set | Abbreviation |
|-------|----|------------|--------------|
| Warrior | 1 | `1 << 0` = 0x0001 | WAR |
| Cleric | 2 | `1 << 1` = 0x0002 | CLR |
| Paladin | 3 | `1 << 2` = 0x0004 | PAL |
| Ranger | 4 | `1 << 3` = 0x0008 | RNG |
| Shadow Knight | 5 | `1 << 4` = 0x0010 | SHD |
| Druid | 6 | `1 << 5` = 0x0020 | DRU |
| Monk | 7 | `1 << 6` = 0x0040 | MNK |
| Bard | 8 | `1 << 7` = 0x0080 | BRD |
| Rogue | 9 | `1 << 8` = 0x0100 | ROG |
| Shaman | 10 | `1 << 9` = 0x0200 | SHM |
| Necromancer | 11 | `1 << 10` = 0x0400 | NEC |
| Wizard | 12 | `1 << 11` = 0x0800 | WIZ |
| Magician | 13 | `1 << 12` = 0x1000 | MAG |
| Enchanter | 14 | `1 << 13` = 0x2000 | ENC |
| Beastlord | 15 | `1 << 14` = 0x4000 | BST |
| Berserker | 16 | `1 << 15` = 0x8000 | BER |

## Examples

| Character's Classes | Bitmask | Value to Send (bitmask + 1000) |
|---|---|---|
| Shaman + Necromancer + Magician | 0x1600 (5632) | **6632** |
| Warrior + Cleric | 0x0003 (3) | **1003** |
| Warrior only | 0x0001 (1) | **1001** |
| All 16 classes | 0xFFFF (65535) | **66535** |

## Why Offset by 1000?

The raw bitmask can't be used directly because bitmask values collide with standard class IDs:

- Standard classes use IDs **1-16**
- GM classes use IDs **20-35**
- Server currently sends **255** as a multiclass sentinel
- A raw bitmask for WAR+CLR = 3, which collides with Paladin (classId 3)

The +1000 offset puts all encoded values into the **1001-66535** range, which is unambiguous.

## Pseudocode for Server

```cpp
// When building the character select list:
if (IsMulticlass(charId)) {
    uint32_t classBitmask = GetCharacterClassBitmask(charId);  // same as OP_EdgeStats key 1
    charSelectEntry.class = classBitmask + 1000;
} else {
    charSelectEntry.class = GetCharacterClass(charId);  // standard 1-16
}
```

## Client-Side Handling (already implemented)

The DLL's `GetClassDesc` hook handles this with the following priority:

```
classId 1-16       -> standard class name (pass through to original)
OP_EdgeStats data  -> decode bitmask to "SHM/NEC/MAG" (after zoning in)
classId >= 1000    -> subtract 1000, decode bitmask to "SHM/NEC/MAG" (character select)
classId 17-999     -> "Hero" fallback (includes current sentinel 255)
```

No further client changes needed. Once the server sends `bitmask + 1000`, the character select screen will automatically display abbreviated class names.

## Field Size Note

The maximum encoded value is 66535 (all 16 classes). This fits in a `uint32` (DWORD) with no issue. If the field is `uint16` (max 65535), the value 66535 overflows — but no character will realistically have all 16 classes, so the practical max is well under 65535.
