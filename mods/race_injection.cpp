/**
 * @file race_injection.cpp
 * @brief Implementation of RaceInjection mod.
 *
 * Reads race definitions from thj_races.ini in the game directory and injects
 * custom race models into the client when it enters the game world.
 *
 * The Pyrelight DLL strings tell us:
 *   "injecting race %s gender %d id %d"
 *   "loaded race %s gender %d as id %d"
 *   "CRaces_AddModel"
 *
 * This mod hooks the character model loading path to insert additional race
 * entries. The actual hook point (CRaces::AddModel) is resolved at runtime
 * via pattern scanning since no static offset is available in eqlib.
 *
 * Config file format (thj_races.ini):
 *   [Races]
 *   Count=2
 *   Race0=700,ELF,0
 *   Race1=701,DWF,1
 *   ; format: raceId,modelName,gender
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "race_injection.h"
#include "../core.h"
#include "../hooks.h"
#include "../memory.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// CRaces::AddModel hook
//
// We use pattern scanning to find this function since it doesn't have a
// static offset in eqlib. The function signature from MQ2 is:
//   void CRaces::AddModel(int raceId, int gender, const char* modelName)
// ---------------------------------------------------------------------------
using CRaces_AddModel_t = void(__fastcall*)(void* thisPtr, void* edx,
    int raceId, int gender, const char* modelName);
static CRaces_AddModel_t CRaces_AddModel_Original = nullptr;
static CRaces_AddModel_t CRaces_AddModel_Func     = nullptr;

// Pointer to the CRaces singleton
static void* s_pCRaces = nullptr;

// Forward declaration
static void InjectPendingRaces();

// Pending race definitions to inject
static std::vector<RaceInjection::RaceDefinition>* s_pendingRaces = nullptr;

static void __fastcall CRaces_AddModel_Detour(void* thisPtr, void* edx,
    int raceId, int gender, const char* modelName)
{
    // Call original first
    CRaces_AddModel_Original(thisPtr, edx, raceId, gender, modelName);

    // Capture the CRaces pointer on first call
    if (!s_pCRaces)
    {
        s_pCRaces = thisPtr;
        LogFramework("RaceInjection: CRaces instance at 0x%p", thisPtr);
    }
}

static void InjectPendingRaces()
{
    if (!s_pendingRaces || !CRaces_AddModel_Original || !s_pCRaces)
        return;

    for (const auto& race : *s_pendingRaces)
    {
        LogFramework("RaceInjection: injecting race %s gender %d id %d",
            race.modelName.c_str(), race.gender, race.raceId);

        // Call the original function directly (not through hook) to add the model
        CRaces_AddModel_Original(s_pCRaces, nullptr,
            race.raceId, race.gender, race.modelName.c_str());

        LogFramework("RaceInjection: loaded race %s gender %d as id %d",
            race.modelName.c_str(), race.gender, race.raceId);
    }
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* RaceInjection::GetName() const
{
    return "RaceInjection";
}

bool RaceInjection::Initialize()
{
    LogFramework("RaceInjection: Initializing...");

    // Load race definitions from config
    if (!LoadRaceConfig())
    {
        LogFramework("RaceInjection: No race config found or no races defined — skipping");
        LogFramework("RaceInjection: Initialized (no races to inject)");
        return true;
    }

    LogFramework("RaceInjection: Loaded %zu race definitions from config", m_races.size());
    s_pendingRaces = &m_races;

    // Try to find CRaces::AddModel via pattern scanning
    // Pattern: The function typically starts with a standard prologue and
    // references the race model string table.
    //
    // For now, we log the race config but defer injection until the address
    // is confirmed. If the pattern is found, the hook will capture the CRaces
    // pointer and we can inject during game state transitions.
    //
    // TODO: Add pattern scan for CRaces::AddModel when byte pattern is confirmed
    // from Pyrelight DLL analysis. The function can be found by searching for
    // xrefs to the format string "injecting race %s gender %d id %d".

    LogFramework("RaceInjection: Initialized (%zu races configured)", m_races.size());
    return true;
}

void RaceInjection::Shutdown()
{
    if (CRaces_AddModel_Original)
        Hooks::Remove("CRaces_AddModel");

    s_pendingRaces = nullptr;
    s_pCRaces = nullptr;
    m_races.clear();

    LogFramework("RaceInjection: Shutdown");
}

void RaceInjection::OnPulse()
{
    // No per-frame work needed
}

bool RaceInjection::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}

void RaceInjection::OnSetGameState(int gameState)
{
    // Game state 5 = GAMESTATE_INGAME
    // Inject races when entering the game world, after models are loaded
    if (gameState == 5 && !m_injected && !m_races.empty())
    {
        if (s_pCRaces && CRaces_AddModel_Original)
        {
            InjectPendingRaces();
            m_injected = true;
        }
    }

    // Reset on return to character select (state 1) so we re-inject on next entry
    if (gameState == 1)
        m_injected = false;
}

// ---------------------------------------------------------------------------
// Config file parsing
// ---------------------------------------------------------------------------

bool RaceInjection::LoadRaceConfig()
{
    // Read from thj_races.ini in the game directory
    char iniPath[MAX_PATH];
    GetModuleFileNameA(nullptr, iniPath, MAX_PATH);

    // Replace the exe name with the ini name
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash)
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - iniPath), "thj_races.ini");
    else
        strcpy_s(iniPath, "thj_races.ini");

    // Check if file exists
    DWORD attrs = GetFileAttributesA(iniPath);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        LogFramework("RaceInjection: Config file not found: %s", iniPath);
        return false;
    }

    LogFramework("RaceInjection: Reading config: %s", iniPath);

    int count = GetPrivateProfileIntA("Races", "Count", 0, iniPath);
    if (count <= 0)
        return false;

    for (int i = 0; i < count; ++i)
    {
        char key[32];
        sprintf_s(key, "Race%d", i);

        char value[256] = {};
        GetPrivateProfileStringA("Races", key, "", value, sizeof(value), iniPath);

        if (value[0] == '\0')
            continue;

        // Parse: raceId,modelName,gender
        RaceDefinition def = {};
        char modelName[64] = {};
        if (sscanf_s(value, "%d,%63[^,],%d", &def.raceId, modelName,
            static_cast<unsigned int>(sizeof(modelName)), &def.gender) >= 2)
        {
            def.modelName = modelName;
            m_races.push_back(def);
            LogFramework("RaceInjection: Config race %d: id=%d model=%s gender=%d",
                i, def.raceId, def.modelName.c_str(), def.gender);
        }
    }

    return !m_races.empty();
}
