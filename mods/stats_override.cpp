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

#include <eqlib/Offsets.h>

#include <cstdint>
#include <cstring>
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

// ---------------------------------------------------------------------------
// Stat types for the override map
// ---------------------------------------------------------------------------
enum class StatType : uint8_t
{
    MaxMana      = 0,
    CurMana      = 1,
    MaxEndurance = 2,
    CurEndurance = 3,
    MaxHP        = 4,
    CurHP        = 5,
};

// ---------------------------------------------------------------------------
// Server packet structure for opcode 0x1338
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct EdgeStatEntry
{
    uint8_t statType;
    int     value;
};

struct EdgeStat_Struct
{
    uint32_t       count;
    EdgeStatEntry  entries[1]; // variable-length array
};
#pragma pack(pop)

// Custom opcode for server-sent stat overrides
static constexpr uint32_t OP_EdgeStats = 0x1338;

// ---------------------------------------------------------------------------
// Stat override storage
// Server-sent values take highest priority in the 3-tier system.
// ---------------------------------------------------------------------------
static std::unordered_map<StatType, int> s_statOverrides;

// TEMPORARY test default — returned when original function returns 0 and no
// server data exists. Proves the hooks are installed and working.
// Replace with server-sent data once the server broadcasts via 0x1338.
static constexpr int TEST_DEFAULT_VALUE = 100;

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

// ---------------------------------------------------------------------------
// Helper: 3-tier stat resolution
// ---------------------------------------------------------------------------
static int ResolveStat(StatType type, int originalValue)
{
    // Tier 1: Server-sent override
    auto it = s_statOverrides.find(type);
    if (it != s_statOverrides.end())
        return it->second;

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
    return ResolveStat(StatType::MaxMana, original);
}

static int __fastcall CurMana_Detour(void* thisPtr, void* edx, bool bCapAtMax)
{
    int original = CurMana_Original(thisPtr, edx, bCapAtMax);
    return ResolveStat(StatType::CurMana, original);
}

static int __fastcall MaxEndurance_Detour(void* thisPtr, void* edx, bool bCapAtMax)
{
    int original = MaxEndurance_Original(thisPtr, edx, bCapAtMax);
    return ResolveStat(StatType::MaxEndurance, original);
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
        return ResolveStat(StatType::CurMana, original);
    case GAUGE_STAMINA:
        return ResolveStat(StatType::CurEndurance, original);
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

static bool __cdecl GetLabelFromEQ_Detour(int labelId, void* pStr, bool* pEnabled, unsigned long* pColor)
{
    bool result = GetLabelFromEQ_Original(labelId, pStr, pEnabled, pColor);

    // Only intervene on mana/endurance labels, and only if the label system
    // returned a value we can check. The label text is in a CXStr which we
    // don't modify here — the gauge hooks handle the numeric display.
    // This hook is reserved for future label text overrides if needed.

    return result;
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

    LogFramework("StatsOverride: Initialized — 5 hooks installed");
    return true;
}

void StatsOverride::Shutdown()
{
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
        return false;  // Suppress malformed packet
    }

    auto* pkt = reinterpret_cast<const EdgeStat_Struct*>(buffer);

    // Validate that the packet contains enough data for all entries
    size_t expectedSize = sizeof(uint32_t) + pkt->count * sizeof(EdgeStatEntry);
    if (size < expectedSize)
    {
        LogFramework("StatsOverride: Received 0x1338 with count=%u but size=%u (need %u)",
            pkt->count, size, static_cast<unsigned int>(expectedSize));
        return false;
    }

    LogFramework("StatsOverride: Received %u stat overrides from server", pkt->count);

    for (uint32_t i = 0; i < pkt->count; ++i)
    {
        auto type = static_cast<StatType>(pkt->entries[i].statType);
        int  val  = pkt->entries[i].value;

        s_statOverrides[type] = val;
        LogFramework("StatsOverride:   stat[%u] = %d", pkt->entries[i].statType, val);
    }

    return false;  // Suppress — don't pass unknown opcode to original handler
}
