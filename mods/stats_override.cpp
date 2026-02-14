/**
 * @file stats_override.cpp
 * @brief Implementation of StatsOverride mod — hooks Max_Mana, Cur_Mana,
 *        Max_Endurance, GetGaugeValueFromEQ, and GetLabelFromEQ to display
 *        correct stat values for non-caster classes.
 * @date 2026-02-11
 *
 * @copyright Copyright (c) 2026
 *
 * Three-tier priority system for all stat hooks:
 *   1. Server data (from opcode 0x1338) — highest priority
 *   2. Test default (100) — used when original returns 0, to verify hooks work
 *   3. Original function return value — used for casters who already have values
 *
 * The test default of 100 is TEMPORARY — it exists only to verify that hooks
 * are working before the server-side stat broadcast is implemented. Once the
 * server sends real data via opcode 0x1338, the test defaults are never reached.
 */

#include "pch.h"
#include "stats_override.h"
#include "../core.h"
#include "../hooks.h"
#include "../game_state.h"
#include "../commands.h"

#include <eqlib/Offsets.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// EQGameBaseAddress — defined in core.cpp (extern "C" linkage)
// ---------------------------------------------------------------------------
extern "C" uintptr_t EQGameBaseAddress;

// ---------------------------------------------------------------------------
// Raw offsets (from eqlib offsets file)
// ---------------------------------------------------------------------------
#define CharacterZoneClient__Max_Mana_x       0x581E60
#define CharacterZoneClient__Cur_Mana_x       0x4442E0
#define CharacterZoneClient__Max_Endurance_x  0x582020
#define __GetGaugeValueFromEQ_x               0x762410
#define __GetLabelFromEQ_x                    0x763640
#define EQ_Spell__GetSpellLevelNeeded_x        0x4AF700
#define __eq_new_x                            0x8DBB3B
#define __eq_delete_x                         0x8DB146
#define CXStr__gFreeLists_x                   0xB618A0

// ---------------------------------------------------------------------------
// Server stat keys (from classless-dll eStatEntry enum)
// ---------------------------------------------------------------------------
enum eStatEntry : uint32_t
{
    eStatClassesBitmask = 1,
    eStatCurHP          = 2,
    eStatCurMana        = 3,
    eStatCurEndur       = 4,
    eStatMaxHP          = 5,
    eStatMaxMana        = 6,
    eStatMaxEndur       = 7,
    eStatATK            = 8,
    eStatAC             = 9,
    eStatSTR            = 10,
    eStatSTA            = 11,
    eStatDEX            = 12,
    eStatAGI            = 13,
    eStatINT            = 14,
    eStatWIS            = 15,
    eStatCHA            = 16,
    eStatMR             = 17,
    eStatFR             = 18,
    eStatCR             = 19,
    eStatPR             = 20,
    eStatDR             = 21,
    eStatWalkspeed      = 22,
    eStatRunspeed       = 23,
    eStatWeight         = 24,
    eStatMaxWeight      = 25,
    eStatMeleePower     = 26,
    eStatSpellPower     = 27,
    eStatHealingPower   = 28,
    eStatMeleeHaste     = 29,
    eStatSpellHaste     = 30,
    eStatHealingHaste   = 31,
    eStatMeleeCrit      = 32,
    eStatSpellCrit      = 33,
    eStatHealingCrit    = 34,
    eStatAvoidance      = 35,
    eStatMitigation     = 36,
    eStatAAPoints       = 37,
};

// ---------------------------------------------------------------------------
// Server packet structure for opcode 0x1338 (matches classless-dll format)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct EdgeStatEntry
{
    uint32_t statKey;
    uint64_t statValue;
};

struct EdgeStat_Struct
{
    uint32_t       count;
    EdgeStatEntry  entries[1]; // variable-length array
};
#pragma pack(pop)

// Custom opcode for server-sent stat overrides
static constexpr uint32_t OP_EdgeStats = 0x1338;

// Spawn field offset for Level
static constexpr uint32_t OFF_SPAWN_LEVEL = 0x250;

// ---------------------------------------------------------------------------
// Minimal CStrRep layout for direct buffer access (matches eqlib::CStrRep)
// CXStr is a single CStrRep* pointer (4 bytes on x86).
// We use this to modify label text in-place without allocating or linking
// eqlib's CXStr implementation.
// ---------------------------------------------------------------------------
struct CStrRep_Raw
{
    int32_t  refCount;   // 0x00
    uint32_t alloc;      // 0x04 — total bytes allocated for string data
    uint32_t length;     // 0x08 — current string length
    uint32_t encoding;   // 0x0C — 0 = UTF8, 1 = UTF16
    void*    freeList;   // 0x10
    char     utf8[4];    // 0x14 — variable-length string data starts here
};

// ---------------------------------------------------------------------------
// Game allocator function pointers (resolved in Initialize)
// ---------------------------------------------------------------------------
using EqAllocFn = void*(*)(size_t);
using EqFreeFn  = void(*)(void*);

static EqAllocFn s_eqAlloc   = nullptr;
static EqFreeFn  s_eqFree    = nullptr;
static void*     s_gFreeLists = nullptr;  // CXFreeList array in eqgame.exe

// Set a CXStr's text, allocating a new CStrRep if the buffer is too small.
// Uses the game's own allocator so the game can later free our CStrRep normally.
static void SetCXStrText(void* pCXStr, const char* text)
{
    if (!pCXStr || !text || !s_eqAlloc || !s_eqFree) return;

    size_t len = strlen(text);
    CStrRep_Raw** ppRep = reinterpret_cast<CStrRep_Raw**>(pCXStr);
    CStrRep_Raw* oldRep = *ppRep;

    // Fast path: buffer is big enough — modify in-place
    if (oldRep && oldRep->encoding == 0 && oldRep->alloc > len)
    {
        memcpy(oldRep->utf8, text, len + 1);
        oldRep->length = static_cast<uint32_t>(len);
        return;
    }

    // Slow path: allocate a new CStrRep via the game's allocator
    size_t dataSize = len + 1;
    if (dataSize < 64) dataSize = 64;  // match a likely free-list bucket

    // CStrRep header is 0x14 bytes, then string data follows
    size_t totalSize = 0x14 + dataSize;
    CStrRep_Raw* newRep = reinterpret_cast<CStrRep_Raw*>(s_eqAlloc(totalSize));
    if (!newRep) return;

    memset(newRep, 0, totalSize);
    newRep->refCount = 1;
    newRep->alloc    = static_cast<uint32_t>(dataSize);
    newRep->length   = static_cast<uint32_t>(len);
    newRep->encoding = 0;  // UTF8
    newRep->freeList = s_gFreeLists;  // so game's FreeRep can handle it
    memcpy(newRep->utf8, text, len + 1);

    // Swap into the CXStr
    *ppRep = newRep;

    // Free the old rep (refcount should be 1, so this frees it)
    if (oldRep)
    {
        if (--oldRep->refCount <= 0)
            s_eqFree(oldRep);
    }
}

// ---------------------------------------------------------------------------
// Stat override storage — keyed by server stat key, values are uint64
// ---------------------------------------------------------------------------
static std::unordered_map<uint32_t, uint64_t> s_statOverrides;

// TEMPORARY test default — returned when original function returns 0 and no
// server data exists. Proves the hooks are installed and working.
static constexpr int TEST_DEFAULT_VALUE = 100;

// ---------------------------------------------------------------------------
// Class title resolver (from classless-dll, level-appropriate titles)
// ---------------------------------------------------------------------------
static const char* GetClassTitle(int class_id, int level)
{
    switch (class_id)
    {
    case 1: // Warrior
        if (level >= 75) return "Imperator";
        if (level >= 70) return "Vanquisher";
        if (level >= 65) return "Overlord";
        if (level >= 60) return "Warlord";
        if (level >= 55) return "Myrmidon";
        if (level >= 51) return "Champion";
        return "Warrior";
    case 2: // Cleric
        if (level >= 75) return "Exemplar";
        if (level >= 70) return "Prelate";
        if (level >= 65) return "Archon";
        if (level >= 60) return "High Priest";
        if (level >= 55) return "Templar";
        if (level >= 51) return "Vicar";
        return "Cleric";
    case 3: // Paladin
        if (level >= 75) return "Holy Defender";
        if (level >= 70) return "Lord";
        if (level >= 65) return "Lord Protector";
        if (level >= 60) return "Crusader";
        if (level >= 55) return "Knight";
        if (level >= 51) return "Cavalier";
        return "Paladin";
    case 4: // Ranger
        if (level >= 75) return "Huntmaster";
        if (level >= 70) return "Plainswalker";
        if (level >= 65) return "Forest Stalker";
        if (level >= 60) return "Warder";
        if (level >= 55) return "Outrider";
        if (level >= 51) return "Pathfinder";
        return "Ranger";
    case 5: // Shadow Knight
        if (level >= 75) return "Bloodreaver";
        if (level >= 70) return "Scourge Knight";
        if (level >= 65) return "Dread Lord";
        if (level >= 60) return "Grave Lord";
        if (level >= 55) return "Revenant";
        if (level >= 51) return "Reaver";
        return "Shadow Knight";
    case 6: // Druid
        if (level >= 75) return "Storm Caller";
        if (level >= 70) return "Natureguard";
        if (level >= 65) return "Storm Warden";
        if (level >= 60) return "Hierophant";
        if (level >= 55) return "Preserver";
        if (level >= 51) return "Wanderer";
        return "Druid";
    case 7: // Monk
        if (level >= 75) return "Ashenhand";
        if (level >= 70) return "Stone Fist";
        if (level >= 65) return "Transcendent";
        if (level >= 60) return "Grandmaster";
        if (level >= 55) return "Master";
        if (level >= 51) return "Disciple";
        return "Monk";
    case 8: // Bard
        if (level >= 75) return "Lyricist";
        if (level >= 70) return "Performer";
        if (level >= 65) return "Maestro";
        if (level >= 60) return "Virtuoso";
        if (level >= 55) return "Troubadour";
        if (level >= 51) return "Minstrel";
        return "Bard";
    case 9: // Rogue
        if (level >= 75) return "Shadowblade";
        if (level >= 70) return "Nemesis";
        if (level >= 65) return "Deceiver";
        if (level >= 60) return "Assassin";
        if (level >= 55) return "Blackguard";
        if (level >= 51) return "Rake";
        return "Rogue";
    case 10: // Shaman
        if (level >= 75) return "Spiritwatcher";
        if (level >= 70) return "Soothsayer";
        if (level >= 65) return "Prophet";
        if (level >= 60) return "Oracle";
        if (level >= 55) return "Luminary";
        if (level >= 51) return "Mystic";
        return "Shaman";
    case 11: // Necromancer
        if (level >= 75) return "Deathcaller";
        if (level >= 70) return "Wraith";
        if (level >= 65) return "Arch Lich";
        if (level >= 60) return "Warlock";
        if (level >= 55) return "Defiler";
        if (level >= 51) return "Heretic";
        return "Necromancer";
    case 12: // Wizard
        if (level >= 75) return "Pyromancer";
        if (level >= 70) return "Grand Arcanist";
        if (level >= 65) return "Arcanist";
        if (level >= 60) return "Sorcerer";
        if (level >= 55) return "Evoker";
        if (level >= 51) return "Channeler";
        return "Wizard";
    case 13: // Magician
        if (level >= 75) return "Grand Summoner";
        if (level >= 70) return "Arch Magus";
        if (level >= 65) return "Arch Convoker";
        if (level >= 60) return "Arch Mage";
        if (level >= 55) return "Conjurer";
        if (level >= 51) return "Elementalist";
        return "Magician";
    case 14: // Enchanter
        if (level >= 75) return "Entrancer";
        if (level >= 70) return "Bedazzler";
        if (level >= 65) return "Coercer";
        if (level >= 60) return "Phantasmist";
        if (level >= 55) return "Beguiler";
        if (level >= 51) return "Illusionist";
        return "Enchanter";
    case 15: // Beastlord
        if (level >= 75) return "Frostblood";
        if (level >= 70) return "Wildblood";
        if (level >= 65) return "Feral Lord";
        if (level >= 60) return "Savage Lord";
        if (level >= 55) return "Animist";
        if (level >= 51) return "Primalist";
        return "Beastlord";
    case 16: // Berserker
        if (level >= 75) return "Juggernaut";
        if (level >= 70) return "Ravager";
        if (level >= 65) return "Fury";
        if (level >= 60) return "Rager";
        if (level >= 55) return "Vehement";
        if (level >= 51) return "Brawler";
        return "Berserker";
    }
    return "Unknown";
}

// Build multiline class title string from bitmask
static std::string GetClassString()
{
    auto it = s_statOverrides.find(eStatClassesBitmask);
    if (it == s_statOverrides.end())
        return "";

    uint32_t mask = static_cast<uint32_t>(it->second);
    if (!mask) return "";

    // Read player level
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal) return "";

    int level = *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(pLocal) + OFF_SPAWN_LEVEL);

    std::string result;
    for (int classId = 1; classId <= 16; ++classId)
    {
        if (mask & (1u << (classId - 1)))
        {
            if (!result.empty())
                result += "\n";
            result += GetClassTitle(classId, level);
        }
    }

    return result;
}

// Class abbreviation array (from classless-dll)
static const char* ClassAbbr[17] =
{
    "",     // 0 unused
    "WAR",  // 1 - Warrior
    "CLR",  // 2 - Cleric
    "PAL",  // 3 - Paladin
    "RNG",  // 4 - Ranger
    "SHD",  // 5 - Shadow Knight
    "DRU",  // 6 - Druid
    "MNK",  // 7 - Monk
    "BRD",  // 8 - Bard
    "ROG",  // 9 - Rogue
    "SHM",  // 10 - Shaman
    "NEC",  // 11 - Necromancer
    "WIZ",  // 12 - Wizard
    "MAG",  // 13 - Magician
    "ENC",  // 14 - Enchanter
    "BST",  // 15 - Beastlord
    "BER",  // 16 - Berserker
};

// Build multiline class abbreviation string from bitmask
static std::string GetClassAbbrString()
{
    auto it = s_statOverrides.find(eStatClassesBitmask);
    if (it == s_statOverrides.end())
        return "";

    uint32_t mask = static_cast<uint32_t>(it->second);
    if (!mask) return "";

    std::string result;
    for (int classId = 1; classId <= 16; ++classId)
    {
        if (mask & (1u << (classId - 1)))
        {
            if (!result.empty())
                result += "\n";
            result += ClassAbbr[classId];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Original function typedefs and pointers
// ---------------------------------------------------------------------------

// Max_Mana / Cur_Mana / Max_Endurance: int __thiscall(bool bCapAtMax)
using StatFunc_t = int (__fastcall*)(void* thisPtr, void* edx, bool bCapAtMax);
static StatFunc_t MaxMana_Original      = nullptr;
static StatFunc_t CurMana_Original      = nullptr;
static StatFunc_t MaxEndurance_Original  = nullptr;

// GetGaugeValueFromEQ: int __cdecl(int gaugeType, CXStr*, bool*, unsigned long*)
using GetGaugeValueFromEQ_t = int (__cdecl*)(int gaugeType, void* pStr, bool* pEnabled, unsigned long* pColor);
static GetGaugeValueFromEQ_t GetGaugeValueFromEQ_Original = nullptr;

// GetLabelFromEQ: bool __cdecl(int labelId, CXStr*, bool*, COLORREF*)
using GetLabelFromEQ_t = bool (__cdecl*)(int labelId, void* pStr, bool* pEnabled, unsigned long* pColor);
static GetLabelFromEQ_t GetLabelFromEQ_Original = nullptr;

// EQ_Spell::GetSpellLevelNeeded: unsigned char __thiscall(unsigned int classIndex)
// Returns the level a given class needs to cast this spell (255 = can't use)
using GetSpellLevelNeeded_t = unsigned char (__fastcall*)(void* thisPtr, void* edx, unsigned int classIndex);
static GetSpellLevelNeeded_t GetSpellLevelNeeded_Original = nullptr;

// Offset of ClassLevel[MAX_CLASSES+1] array within EQ_Spell struct (uint8_t per class, 1-based index)
static constexpr uint32_t OFF_SPELL_CLASS_LEVEL = 0x246;

// ---------------------------------------------------------------------------
// Helper: 3-tier stat resolution (uses server stat keys)
// ---------------------------------------------------------------------------
static int ResolveStat(uint32_t statKey, int originalValue)
{
    // Tier 1: Server-sent override
    auto it = s_statOverrides.find(statKey);
    if (it != s_statOverrides.end())
        return static_cast<int>(it->second);

    // Tier 2: Test default when original is 0 (non-caster class)
    if (originalValue == 0)
        return TEST_DEFAULT_VALUE;

    // Tier 3: Original value (caster class — already has real data)
    return originalValue;
}

// ---------------------------------------------------------------------------
// Detours
// ---------------------------------------------------------------------------

static int __fastcall MaxMana_Detour(void* thisPtr, void* edx, bool bCapAtMax)
{
    int original = MaxMana_Original(thisPtr, edx, bCapAtMax);
    return ResolveStat(eStatMaxMana, original);
}

static int __fastcall CurMana_Detour(void* thisPtr, void* edx, bool bCapAtMax)
{
    int original = CurMana_Original(thisPtr, edx, bCapAtMax);
    return ResolveStat(eStatCurMana, original);
}

static int __fastcall MaxEndurance_Detour(void* thisPtr, void* edx, bool bCapAtMax)
{
    int original = MaxEndurance_Original(thisPtr, edx, bCapAtMax);
    return ResolveStat(eStatMaxEndur, original);
}

// Gauge types — discovered empirically from EQ client UI.
// If mana/endurance gauges don't appear, add temp logging here to find correct IDs.
static constexpr int GAUGE_HP        = 0;
static constexpr int GAUGE_MANA      = 1;
static constexpr int GAUGE_STAMINA   = 2;  // endurance

static int __cdecl GetGaugeValueFromEQ_Detour(int gaugeType, void* pStr, bool* pEnabled, unsigned long* pColor)
{
    int original = GetGaugeValueFromEQ_Original(gaugeType, pStr, pEnabled, pColor);

    switch (gaugeType)
    {
    case GAUGE_MANA:
        return ResolveStat(eStatCurMana, original);
    case GAUGE_STAMINA:
        return ResolveStat(eStatCurEndur, original);
    default:
        return original;
    }
}

// Label IDs — from EQ client UI label system.
// Mana-related labels that show "0" for non-casters.
static constexpr int LABEL_MANA_VALUE   = 78;
static constexpr int LABEL_MANA_MAX     = 79;
static constexpr int LABEL_MANA_PERCENT = 80;
static constexpr int LABEL_ENDUR_VALUE  = 81;
static constexpr int LABEL_ENDUR_MAX    = 82;
static constexpr int LABEL_ENDUR_PCT    = 83;

static constexpr int LABEL_CLASS = 3;

// Custom label IDs from classless-dll SIDL XML
static constexpr int LABEL_CLASS_ABBR  = 6666;  // class abbreviations (SHM/NEC/MAG)
static constexpr int LABEL_AVOIDANCE   = 6667;  // avoidance stat
static constexpr int LABEL_MITIGATION  = 6668;  // mitigation stat

// One-shot debug flag — set by /classdebug, logs next label type 3 call
static bool s_debugNextClassLabel = false;

// Track all label IDs the game requests
static std::unordered_map<int, int> s_seenLabelIds;  // labelId → call count

static void CmdClassDebug(eqlib::PlayerClient* /*pChar*/, const char* /*szLine*/)
{
    WriteChatf("--- Class Label Debug ---");

    // Show all stat overrides
    WriteChatf("  Stat overrides (%d entries):", static_cast<int>(s_statOverrides.size()));
    for (const auto& [key, val] : s_statOverrides)
    {
        if (key == eStatClassesBitmask)
            WriteChatf("    key[%u] = 0x%08X (class bitmask)", key, static_cast<unsigned int>(val));
        else
            WriteChatf("    key[%u] = %lld", key, static_cast<long long>(val));
    }

    // Show class string result
    std::string classStr = GetClassString();
    if (classStr.empty())
        WriteChatf("  GetClassString() = (empty) — no class bitmask from server");
    else
        WriteChatf("  GetClassString() = '%s'", classStr.c_str());

    // Dump all seen label IDs
    WriteChatf("  --- Seen label IDs (%d unique) ---", static_cast<int>(s_seenLabelIds.size()));
    for (const auto& [id, count] : s_seenLabelIds)
        WriteChatf("    labelId=%d  (called %d times)", id, count);

    // Arm one-shot logging for next label type 3
    s_debugNextClassLabel = true;
    WriteChatf("  Armed: will log next label type 3 call");
    WriteChatf("-------------------------");
}

static bool __cdecl GetLabelFromEQ_Detour(int labelId, void* pStr, bool* pEnabled, unsigned long* pColor)
{
    bool result = GetLabelFromEQ_Original(labelId, pStr, pEnabled, pColor);

    // Track all label IDs for diagnostics
    s_seenLabelIds[labelId]++;

    // Override class label with multiclass titles from server bitmask
    if (labelId == LABEL_CLASS)
    {
        std::string classStr = GetClassString();

        // One-shot debug logging
        if (s_debugNextClassLabel)
        {
            s_debugNextClassLabel = false;

            CStrRep_Raw* rep = *reinterpret_cast<CStrRep_Raw**>(pStr);
            if (rep)
            {
                WriteChatf("ClassLabel: pStr=%p rep=%p encoding=%u alloc=%u len=%u text='%s'",
                    pStr, rep, rep->encoding, rep->alloc, rep->length,
                    (rep->encoding == 0 && rep->length > 0) ? rep->utf8 : "(n/a)");
            }
            else
            {
                WriteChatf("ClassLabel: pStr=%p rep=NULL", pStr);
            }
            WriteChatf("ClassLabel: classStr='%s' (len=%d)",
                classStr.empty() ? "(empty)" : classStr.c_str(),
                static_cast<int>(classStr.size()));
        }

        if (!classStr.empty())
        {
            SetCXStrText(pStr, classStr.c_str());
        }
    }

    // Override class abbreviation label (custom SIDL label 6666)
    if (labelId == LABEL_CLASS_ABBR)
    {
        std::string abbrStr = GetClassAbbrString();
        if (!abbrStr.empty())
            SetCXStrText(pStr, abbrStr.c_str());
    }

    // Override avoidance label (custom SIDL label 6667)
    if (labelId == LABEL_AVOIDANCE)
    {
        auto it = s_statOverrides.find(eStatAvoidance);
        if (it != s_statOverrides.end())
        {
            std::string val = std::to_string(static_cast<int>(it->second));
            SetCXStrText(pStr, val.c_str());
        }
    }

    // Override mitigation label (custom SIDL label 6668)
    if (labelId == LABEL_MITIGATION)
    {
        auto it = s_statOverrides.find(eStatMitigation);
        if (it != s_statOverrides.end())
        {
            std::string val = std::to_string(static_cast<int>(it->second));
            SetCXStrText(pStr, val.c_str());
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// GetSpellLevelNeeded detour — multiclass spell level bypass
//
// The client calls GetSpellLevelNeeded(playerClass) to check if the player
// can cast a spell. For multiclass characters, the player's base class may
// not be able to use the spell (returns 255). This detour checks ALL classes
// in the server-sent bitmask and returns the minimum level needed.
// ---------------------------------------------------------------------------
static unsigned char __fastcall GetSpellLevelNeeded_Detour(
    void* thisPtr, void* edx, unsigned int classIndex)
{
    // If no class bitmask from server, fall through to original
    auto it = s_statOverrides.find(eStatClassesBitmask);
    if (it == s_statOverrides.end())
        return GetSpellLevelNeeded_Original(thisPtr, edx, classIndex);

    uint32_t bitmask = static_cast<uint32_t>(it->second);
    if (!bitmask)
        return GetSpellLevelNeeded_Original(thisPtr, edx, classIndex);

    // thisPtr is the EQ_Spell object. ClassLevel at offset 0x246, 1-based class index.
    uint8_t* classLevels = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(thisPtr) + OFF_SPELL_CLASS_LEVEL);

    int minLevel = 255;
    for (int classId = 1; classId <= 16; ++classId)
    {
        if (bitmask & (1u << (classId - 1)))
        {
            int lvl = classLevels[classId];
            if (lvl > 0 && lvl < 255 && lvl < minLevel)
                minLevel = lvl;
        }
    }

    return (minLevel != 255)
        ? static_cast<unsigned char>(minLevel)
        : GetSpellLevelNeeded_Original(thisPtr, edx, classIndex);
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* StatsOverride::GetName() const
{
    return "StatsOverride";
}

bool StatsOverride::Initialize()
{
    LogFramework("StatsOverride: Initializing...");

    // --- Resolve addresses with ASLR: (raw - 0x400000 + base) ---

    uintptr_t maxManaAddr = static_cast<uintptr_t>(CharacterZoneClient__Max_Mana_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    MaxMana_Original = reinterpret_cast<StatFunc_t>(maxManaAddr);
    LogFramework("StatsOverride: Max_Mana = 0x%08X", static_cast<unsigned int>(maxManaAddr));

    uintptr_t curManaAddr = static_cast<uintptr_t>(CharacterZoneClient__Cur_Mana_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    CurMana_Original = reinterpret_cast<StatFunc_t>(curManaAddr);
    LogFramework("StatsOverride: Cur_Mana = 0x%08X", static_cast<unsigned int>(curManaAddr));

    uintptr_t maxEndurAddr = static_cast<uintptr_t>(CharacterZoneClient__Max_Endurance_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    MaxEndurance_Original = reinterpret_cast<StatFunc_t>(maxEndurAddr);
    LogFramework("StatsOverride: Max_Endurance = 0x%08X", static_cast<unsigned int>(maxEndurAddr));

    uintptr_t gaugeAddr = static_cast<uintptr_t>(__GetGaugeValueFromEQ_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    GetGaugeValueFromEQ_Original = reinterpret_cast<GetGaugeValueFromEQ_t>(gaugeAddr);
    LogFramework("StatsOverride: GetGaugeValueFromEQ = 0x%08X", static_cast<unsigned int>(gaugeAddr));

    uintptr_t labelAddr = static_cast<uintptr_t>(__GetLabelFromEQ_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    GetLabelFromEQ_Original = reinterpret_cast<GetLabelFromEQ_t>(labelAddr);
    LogFramework("StatsOverride: GetLabelFromEQ = 0x%08X", static_cast<unsigned int>(labelAddr));

    uintptr_t spellLevelAddr = static_cast<uintptr_t>(EQ_Spell__GetSpellLevelNeeded_x)
        - eqlib::EQGamePreferredAddress + EQGameBaseAddress;
    GetSpellLevelNeeded_Original = reinterpret_cast<GetSpellLevelNeeded_t>(spellLevelAddr);
    LogFramework("StatsOverride: GetSpellLevelNeeded = 0x%08X", static_cast<unsigned int>(spellLevelAddr));

    // --- Resolve game allocator for CXStr text replacement ---
    s_eqAlloc = reinterpret_cast<EqAllocFn>(
        static_cast<uintptr_t>(__eq_new_x) - eqlib::EQGamePreferredAddress + EQGameBaseAddress);
    s_eqFree = reinterpret_cast<EqFreeFn>(
        static_cast<uintptr_t>(__eq_delete_x) - eqlib::EQGamePreferredAddress + EQGameBaseAddress);
    s_gFreeLists = reinterpret_cast<void*>(
        static_cast<uintptr_t>(CXStr__gFreeLists_x) - eqlib::EQGamePreferredAddress + EQGameBaseAddress);
    LogFramework("StatsOverride: eqAlloc=0x%08X eqFree=0x%08X gFreeLists=0x%08X",
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(s_eqAlloc)),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(s_eqFree)),
        static_cast<unsigned int>(reinterpret_cast<uintptr_t>(s_gFreeLists)));

    // --- Install hooks ---
    Hooks::Install("Max_Mana",
        reinterpret_cast<void**>(&MaxMana_Original),
        reinterpret_cast<void*>(&MaxMana_Detour));

    Hooks::Install("Cur_Mana",
        reinterpret_cast<void**>(&CurMana_Original),
        reinterpret_cast<void*>(&CurMana_Detour));

    Hooks::Install("Max_Endurance",
        reinterpret_cast<void**>(&MaxEndurance_Original),
        reinterpret_cast<void*>(&MaxEndurance_Detour));

    Hooks::Install("GetGaugeValueFromEQ",
        reinterpret_cast<void**>(&GetGaugeValueFromEQ_Original),
        reinterpret_cast<void*>(&GetGaugeValueFromEQ_Detour));

    Hooks::Install("GetLabelFromEQ",
        reinterpret_cast<void**>(&GetLabelFromEQ_Original),
        reinterpret_cast<void*>(&GetLabelFromEQ_Detour));

    Hooks::Install("GetSpellLevelNeeded",
        reinterpret_cast<void**>(&GetSpellLevelNeeded_Original),
        reinterpret_cast<void*>(&GetSpellLevelNeeded_Detour));

    Commands::AddCommand("/classdebug", CmdClassDebug);

    LogFramework("StatsOverride: Initialized — 6 hooks installed");
    return true;
}

void StatsOverride::Shutdown()
{
    Commands::RemoveCommand("/classdebug");
    s_statOverrides.clear();
    LogFramework("StatsOverride: Shutdown");
}

void StatsOverride::OnPulse()
{
    // No per-frame work needed
}

bool StatsOverride::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    if (opcode != OP_EdgeStats)
        return true;  // Not our opcode — pass through to original handler

    // Validate minimum packet size: at least the count field
    if (size < sizeof(uint32_t))
    {
        LogFramework("StatsOverride: Received 0x1338 but size too small (%u)", size);
        return false;
    }

    auto* pkt = reinterpret_cast<const EdgeStat_Struct*>(buffer);

    // Validate: count * sizeof(EdgeStatEntry) should equal (size - 4)
    size_t expectedSize = sizeof(uint32_t) + pkt->count * sizeof(EdgeStatEntry);
    if (size < expectedSize)
    {
        LogFramework("StatsOverride: Received 0x1338 with count=%u but size=%u (need %u)",
            pkt->count, size, static_cast<unsigned int>(expectedSize));
        return false;
    }

    LogFramework("StatsOverride: Received %u stat entries from server", pkt->count);

    for (uint32_t i = 0; i < pkt->count; ++i)
    {
        uint32_t key = pkt->entries[i].statKey;
        uint64_t val = pkt->entries[i].statValue;

        s_statOverrides[key] = val;

        if (key == eStatClassesBitmask)
            LogFramework("StatsOverride:   key[%u] = 0x%08X (class bitmask)", key, static_cast<unsigned int>(val));
        else
            LogFramework("StatsOverride:   key[%u] = %lld", key, static_cast<long long>(val));
    }

    return false;  // Suppress — custom opcode
}
