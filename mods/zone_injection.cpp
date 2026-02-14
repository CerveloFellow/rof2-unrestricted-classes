/**
 * @file zone_injection.cpp
 * @brief Implementation of ZoneInjection mod.
 *
 * Reads zone definitions from thj_zones.ini in the game directory and injects
 * custom zone entries into EQWorldData::ZoneArray when the client enters the
 * game world.
 *
 * The Pyrelight DLL strings tell us:
 *   "injecting zone %s id %d"
 *   "loaded zone %s id %d"
 *
 * We write directly to EQWorldData::ZoneArray (offset 0x020 from the WorldData
 * pointer) since we know the full structure layout from eqlib.
 *
 * Config file format (thj_zones.ini):
 *   [Zones]
 *   Count=2
 *   Zone0=700,customzone,Custom Zone Name,0
 *   Zone1=701,anotherzone,Another Zone,0
 *   ; format: zoneId,shortName,longName,expansion
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "zone_injection.h"
#include "../core.h"
#include "../memory.h"
#include "../game_state.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// EQZoneInfo layout (from eqlib/game/EQClasses.h)
//
// We allocate our own EQZoneInfo-sized blocks and populate the key fields,
// then insert them into the WorldData ZoneArray.
// ---------------------------------------------------------------------------
static constexpr int ZONE_INFO_SIZE   = 0x1F8;
static constexpr int ZONE_SHORT_NAME_OFFSET = 0x010;
static constexpr int ZONE_LONG_NAME_OFFSET  = 0x091;
static constexpr int ZONE_ID_OFFSET         = 0x00C;
static constexpr int ZONE_EXPANSION_OFFSET  = 0x008;
static constexpr int ZONE_FLAGS_OFFSET      = 0x198;

// Allocated zone info blocks (we own these and must free them on shutdown)
static std::vector<void*> s_allocatedZones;

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* ZoneInjection::GetName() const
{
    return "ZoneInjection";
}

bool ZoneInjection::Initialize()
{
    LogFramework("ZoneInjection: Initializing...");

    if (!LoadZoneConfig())
    {
        LogFramework("ZoneInjection: No zone config found or no zones defined — skipping");
        LogFramework("ZoneInjection: Initialized (no zones to inject)");
        return true;
    }

    LogFramework("ZoneInjection: Loaded %zu zone definitions from config", m_zones.size());
    LogFramework("ZoneInjection: Initialized");
    return true;
}

void ZoneInjection::Shutdown()
{
    // Free allocated zone info blocks
    for (void* p : s_allocatedZones)
    {
        if (p) free(p);
    }
    s_allocatedZones.clear();
    m_zones.clear();

    LogFramework("ZoneInjection: Shutdown");
}

void ZoneInjection::OnPulse()
{
    // No per-frame work needed
}

bool ZoneInjection::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}

void ZoneInjection::OnSetGameState(int gameState)
{
    // Game state 5 = GAMESTATE_INGAME
    // Inject zones when entering the game world
    if (gameState == 5 && !m_injected && !m_zones.empty())
    {
        InjectZones();
        m_injected = true;
    }

    // Reset on return to character select (state 1) so we re-inject on next entry
    if (gameState == 1)
        m_injected = false;
}

// ---------------------------------------------------------------------------
// Zone injection
// ---------------------------------------------------------------------------

void ZoneInjection::InjectZones()
{
    // Get the WorldData pointer
    uintptr_t worldDataAddr = eqlib::FixEQGameOffset(pinstWorldData_x);
    void* pWorldData = *reinterpret_cast<void**>(worldDataAddr);

    if (!pWorldData)
    {
        LogFramework("ZoneInjection: WARNING — WorldData pointer is null, cannot inject zones");
        return;
    }

    // ZoneArray is at offset 0x020 within EQWorldData
    // It's an array of EQZoneInfo* pointers
    uintptr_t zoneArrayBase = reinterpret_cast<uintptr_t>(pWorldData) + 0x020;

    for (const auto& zone : m_zones)
    {
        if (zone.zoneId < 0 || zone.zoneId >= 1000)
        {
            LogFramework("ZoneInjection: WARNING — Zone id %d out of range [0..999], skipping",
                zone.zoneId);
            continue;
        }

        // Check if slot is already occupied
        uintptr_t slotAddr = zoneArrayBase + (zone.zoneId * sizeof(void*));
        void* existingZone = *reinterpret_cast<void**>(slotAddr);

        if (existingZone)
        {
            LogFramework("ZoneInjection: Zone slot %d already occupied, skipping %s",
                zone.zoneId, zone.shortName.c_str());
            continue;
        }

        LogFramework("ZoneInjection: injecting zone %s id %d", zone.shortName.c_str(), zone.zoneId);

        // Allocate and zero-fill a new EQZoneInfo
        void* zoneInfo = calloc(1, ZONE_INFO_SIZE);
        if (!zoneInfo)
        {
            LogFramework("ZoneInjection: WARNING — Failed to allocate EQZoneInfo for zone %d",
                zone.zoneId);
            continue;
        }
        s_allocatedZones.push_back(zoneInfo);

        // Note: We skip the vtable pointer at offset 0x000 (left as null).
        // Virtual functions on injected zones should not be called by the client
        // unless the zone is actually loaded by the server.

        // Set expansion (offset 0x008)
        *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(zoneInfo) + ZONE_EXPANSION_OFFSET) =
            zone.expansion;

        // Set zone ID (offset 0x00C)
        *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(zoneInfo) + ZONE_ID_OFFSET) =
            zone.zoneId;

        // Set short name (offset 0x010, max 128 chars)
        strncpy_s(
            reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(zoneInfo) + ZONE_SHORT_NAME_OFFSET),
            129, zone.shortName.c_str(), _TRUNCATE);

        // Set long name (offset 0x091, max 256 chars)
        strncpy_s(
            reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(zoneInfo) + ZONE_LONG_NAME_OFFSET),
            257, zone.longName.c_str(), _TRUNCATE);

        // Insert into ZoneArray
        *reinterpret_cast<void**>(slotAddr) = zoneInfo;

        LogFramework("ZoneInjection: loaded zone %s id %d", zone.shortName.c_str(), zone.zoneId);
    }
}

// ---------------------------------------------------------------------------
// Config file parsing
// ---------------------------------------------------------------------------

bool ZoneInjection::LoadZoneConfig()
{
    char iniPath[MAX_PATH];
    GetModuleFileNameA(nullptr, iniPath, MAX_PATH);

    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash)
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - iniPath), "thj_zones.ini");
    else
        strcpy_s(iniPath, "thj_zones.ini");

    DWORD attrs = GetFileAttributesA(iniPath);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        LogFramework("ZoneInjection: Config file not found: %s", iniPath);
        return false;
    }

    LogFramework("ZoneInjection: Reading config: %s", iniPath);

    int count = GetPrivateProfileIntA("Zones", "Count", 0, iniPath);
    if (count <= 0)
        return false;

    for (int i = 0; i < count; ++i)
    {
        char key[32];
        sprintf_s(key, "Zone%d", i);

        char value[512] = {};
        GetPrivateProfileStringA("Zones", key, "", value, sizeof(value), iniPath);

        if (value[0] == '\0')
            continue;

        // Parse: zoneId,shortName,longName,expansion
        ZoneDefinition def = {};
        char shortName[129] = {};
        char longName[257] = {};
        int expansion = 0;

        // Split by comma
        char* ctx = nullptr;
        char* tok = strtok_s(value, ",", &ctx);
        if (!tok) continue;
        def.zoneId = atoi(tok);

        tok = strtok_s(nullptr, ",", &ctx);
        if (!tok) continue;
        def.shortName = tok;

        tok = strtok_s(nullptr, ",", &ctx);
        if (!tok) continue;
        def.longName = tok;

        tok = strtok_s(nullptr, ",", &ctx);
        if (tok) def.expansion = atoi(tok);

        m_zones.push_back(def);
        LogFramework("ZoneInjection: Config zone %d: id=%d short=%s long=%s exp=%d",
            i, def.zoneId, def.shortName.c_str(), def.longName.c_str(), def.expansion);
    }

    return !m_zones.empty();
}
