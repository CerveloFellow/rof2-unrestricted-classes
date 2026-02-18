/**
 * @file multi_pet.cpp
 * @brief Implementation of MultiPet mod — tracks multiple simultaneous pets
 *        for THJ multiclass characters.
 *
 * Pet identification: Server sends OP_PetList (0x1339) with all pet SpawnIDs.
 * Spawn resolution: OnAddSpawn/OnRemoveSpawn maintain a SpawnID→pointer map.
 * Commands: /pets (list), /petcycle (rotate UI pet), /petdebug (diagnostics).
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "multi_pet.h"
#include "../core.h"
#include "../game_state.h"
#include "../commands.h"

#include <eqlib/Offsets.h>

#include <cstdint>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Spawn field offsets (verified from eqlib + /petscan)
// ---------------------------------------------------------------------------
static constexpr uint32_t OFF_SPAWN_NAME      = 0x0A4;  // PlayerBase::Name (char[64])
static constexpr uint32_t OFF_SPAWN_ID        = 0x148;  // PlayerBase::SpawnID (uint32)
static constexpr uint32_t OFF_SPAWN_PET_ID    = 0x2B4;  // PlayerZoneClient::PetID (int)
static constexpr uint32_t OFF_SPAWN_MASTER_ID = 0x38C;  // PlayerZoneClient::MasterID (uint32)
static constexpr uint32_t OFF_SPAWN_NEXT      = 0x04;   // TListNode<PlayerClient>::m_pNext

// PlayerManagerClient virtual method offsets
// GetSpawnByID: thiscall, takes int spawnID, returns PlayerClient* (or nullptr)
static constexpr uint32_t RAW_GET_SPAWN_BY_ID = 0x5996E0;
// PlayerManagerClient + 0x04 = NextID (next spawn ID that will be assigned)
static constexpr uint32_t OFF_MGR_NEXT_ID     = 0x04;

// GetSpawnByID function pointer (resolved at init)
using GetSpawnByID_t = void*(__fastcall*)(void* thisPtr, void* edx, int id);
static GetSpawnByID_t s_GetSpawnByID = nullptr;

// Opcodes
static constexpr uint32_t OP_PetList         = 0x1339;

// ---------------------------------------------------------------------------
// Static instance pointer for command callbacks and cross-mod access
// ---------------------------------------------------------------------------
static MultiPet* s_instance = nullptr;

MultiPet* MultiPet::GetInstance() { return s_instance; }

// ---------------------------------------------------------------------------
// Spawn field helpers
// ---------------------------------------------------------------------------
static inline uint32_t GetSpawnID(void* pSpawn)
{
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(pSpawn) + OFF_SPAWN_ID);
}

static inline const char* GetSpawnName(void* pSpawn)
{
    return reinterpret_cast<const char*>(
        reinterpret_cast<uintptr_t>(pSpawn) + OFF_SPAWN_NAME);
}

static inline int GetPetID(void* pSpawn)
{
    return *reinterpret_cast<int*>(
        reinterpret_cast<uintptr_t>(pSpawn) + OFF_SPAWN_PET_ID);
}

static inline void SetPetID(void* pSpawn, int petID)
{
    *reinterpret_cast<int*>(
        reinterpret_cast<uintptr_t>(pSpawn) + OFF_SPAWN_PET_ID) = petID;
}

static inline uint32_t GetMasterID(void* pSpawn)
{
    return *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(pSpawn) + OFF_SPAWN_MASTER_ID);
}

// ---------------------------------------------------------------------------
// Class name table for pet display
// ---------------------------------------------------------------------------
static const char* GetClassName(uint32_t classID)
{
    switch (classID)
    {
        case 1:  return "Warrior";
        case 2:  return "Cleric";
        case 3:  return "Paladin";
        case 4:  return "Ranger";
        case 5:  return "Shadow Knight";
        case 6:  return "Druid";
        case 7:  return "Monk";
        case 8:  return "Bard";
        case 9:  return "Rogue";
        case 10: return "Shaman";
        case 11: return "Necromancer";
        case 12: return "Wizard";
        case 13: return "Magician";
        case 14: return "Enchanter";
        case 15: return "Beastlord";
        case 16: return "Berserker";
        default: return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void CmdPets(eqlib::PlayerClient* /*pChar*/, const char* /*szLine*/)
{
    if (s_instance) s_instance->ListPets();
}

static void CmdPetCycle(eqlib::PlayerClient* /*pChar*/, const char* /*szLine*/)
{
    if (s_instance) s_instance->CyclePet();
}

static void CmdPetDebug(eqlib::PlayerClient* /*pChar*/, const char* /*szLine*/)
{
    if (s_instance) s_instance->DebugSpawns();
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* MultiPet::GetName() const
{
    return "MultiPet";
}

bool MultiPet::Initialize()
{
    LogFramework("MultiPet: Initializing...");

    s_instance = this;

    // Resolve GetSpawnByID game function
    uintptr_t gsbiAddr = eqlib::FixEQGameOffset(RAW_GET_SPAWN_BY_ID);
    s_GetSpawnByID = reinterpret_cast<GetSpawnByID_t>(gsbiAddr);
    LogFramework("MultiPet: GetSpawnByID = 0x%08X", static_cast<unsigned int>(gsbiAddr));

    Commands::AddCommand("/pets", CmdPets);
    Commands::AddCommand("/petcycle", CmdPetCycle);
    Commands::AddCommand("/petdebug", CmdPetDebug);

    LogFramework("MultiPet: Listening for OP_PetList (0x%04X)", OP_PetList);
    LogFramework("MultiPet: Initialized");
    return true;
}

void MultiPet::Shutdown()
{
    ClearAllTracking();

    Commands::RemoveCommand("/pets");
    Commands::RemoveCommand("/petcycle");
    Commands::RemoveCommand("/petdebug");

    s_instance = nullptr;
    LogFramework("MultiPet: Shutdown");
}

void MultiPet::OnSetGameState(int gameState)
{
    if (gameState != 5)
    {
        LogFramework("MultiPet: Game state changed to %d — clearing all tracking", gameState);
        ClearAllTracking();
    }
}

// ---------------------------------------------------------------------------
// Spawn map — track ALL spawns by SpawnID for later lookup
// ---------------------------------------------------------------------------

void MultiPet::OnAddSpawn(void* pSpawn)
{
    if (!pSpawn) return;
    uint32_t spawnID = GetSpawnID(pSpawn);
    if (spawnID == 0) return;

    m_spawnMap[spawnID] = pSpawn;

    // Check if this spawn is one of our pets (MasterID-based detection)
    TryTrackPet(pSpawn, spawnID);

    // If we have unresolved pets waiting for spawn pointers, try now
    if (m_needsResolve)
        ResolvePetSpawns();
}

void MultiPet::OnRemoveSpawn(void* pSpawn)
{
    if (!pSpawn) return;
    uint32_t spawnID = GetSpawnID(pSpawn);

    m_spawnMap.erase(spawnID);

    // Remove from pet tracking if it was a pet
    auto it = std::find_if(m_pets.begin(), m_pets.end(),
        [spawnID](const TrackedPet& p) { return p.spawnID == spawnID; });

    if (it != m_pets.end())
    {
        LogFramework("MultiPet: Pet '%s' (ID %u) despawned", it->name, it->spawnID);
        m_pets.erase(it);
    }
}

// ---------------------------------------------------------------------------
// OnPulse — periodic pet scanning
// ---------------------------------------------------------------------------

void MultiPet::OnPulse()
{
    if (GameState::GetGameState() != 5) return;

    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal) return;

    uint32_t currentLocalID = GetSpawnID(pLocal);

    // Detect SpawnID change (zoned)
    if (m_localSpawnID != 0 && m_localSpawnID != currentLocalID)
    {
        LogFramework("MultiPet: Local SpawnID changed (%u -> %u) — clearing",
            m_localSpawnID, currentLocalID);
        ClearAllTracking();
    }
    m_localSpawnID = currentLocalID;

    // Rebuild spawn map if empty (e.g. after zone-in or DLL loaded mid-session)
    if (m_spawnMap.empty())
    {
        RebuildSpawnMap();
        ScanForPets();
    }

    // Periodic scan for pets we may have missed (MasterID set after spawn)
    if (++m_scanCounter >= 120)
    {
        m_scanCounter = 0;
        ScanForPets();
    }
}

// ---------------------------------------------------------------------------
// OP_PetList handler — server tells us which spawns are our pets
// ---------------------------------------------------------------------------

bool MultiPet::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    if (opcode == OP_PetList)
    {
        // Packet: [uint32 count] per pet: [uint32 spawnID] [uint32 classID]
        if (size < 4)
            return false;

        const uint8_t* buf = static_cast<const uint8_t*>(buffer);
        uint32_t count;
        memcpy(&count, buf, 4);

        size_t expectedSize = 4 + count * 8;
        if (size < expectedSize)
        {
            LogFramework("MultiPet: OP_PetList size mismatch: count=%u, size=%u, need=%u",
                count, size, static_cast<unsigned int>(expectedSize));
            return false;
        }

        LogFramework("MultiPet: Received OP_PetList with %u pets", count);

        m_pets.clear();

        // Get UI pet ID so we can skip it
        auto* pLocal = GameState::GetLocalPlayer();
        int uiPetID = pLocal ? GetPetID(pLocal) : 0;

        uint32_t offset = 4;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t petSpawnID, petClassID;
            memcpy(&petSpawnID, buf + offset, 4);
            offset += 4;
            memcpy(&petClassID, buf + offset, 4);
            offset += 4;

            // Skip the UI pet — already shown in pet window
            if (static_cast<int>(petSpawnID) == uiPetID)
            {
                LogFramework("MultiPet:   Pet ID %u class %u (%s) — UI pet, skipping",
                    petSpawnID, petClassID, GetClassName(petClassID));
                continue;
            }

            TrackedPet pet;
            pet.spawnID = petSpawnID;
            pet.classID = petClassID;

            // Try to resolve spawn pointer immediately
            auto mapIt = m_spawnMap.find(petSpawnID);
            if (mapIt != m_spawnMap.end())
            {
                pet.pSpawn = mapIt->second;
                strncpy_s(pet.name, GetSpawnName(pet.pSpawn), _TRUNCATE);
                LogFramework("MultiPet:   Tracking pet '%s' (ID %u, class %u/%s) — resolved",
                    pet.name, pet.spawnID, pet.classID, GetClassName(pet.classID));
            }
            else
            {
                LogFramework("MultiPet:   Tracking pet ID %u (class %u/%s) — spawn not yet found",
                    pet.spawnID, pet.classID, GetClassName(pet.classID));
            }

            m_pets.push_back(pet);
        }

        m_needsResolve = true;
        ResolvePetSpawns();

        return false;  // Suppress — custom opcode, don't pass to client
    }

    return true;
}

// ---------------------------------------------------------------------------
// Resolve pet spawn pointers from the spawn map
// ---------------------------------------------------------------------------

void MultiPet::ResolvePetSpawns()
{
    bool allResolved = true;

    for (auto& pet : m_pets)
    {
        if (pet.pSpawn) continue;  // Already resolved

        auto it = m_spawnMap.find(pet.spawnID);
        if (it != m_spawnMap.end())
        {
            pet.pSpawn = it->second;
            strncpy_s(pet.name, GetSpawnName(pet.pSpawn), _TRUNCATE);
            LogFramework("MultiPet: Resolved pet '%s' (ID %u)", pet.name, pet.spawnID);
        }
        else
        {
            allResolved = false;
        }
    }

    if (allResolved)
        m_needsResolve = false;
}

// ---------------------------------------------------------------------------
// MasterID-based pet detection
// ---------------------------------------------------------------------------

void MultiPet::TryTrackPet(void* pSpawn, uint32_t spawnID)
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal) return;

    uint32_t localID = GetSpawnID(pLocal);
    uint32_t masterID = GetMasterID(pSpawn);

    if (masterID != localID || masterID == 0) return;  // Not our pet

    int uiPetID = GetPetID(pLocal);
    if (static_cast<int>(spawnID) == uiPetID) return;  // UI pet — already shown

    // Already tracked?
    for (const auto& pet : m_pets)
        if (pet.spawnID == spawnID) return;

    TrackedPet pet;
    pet.spawnID = spawnID;
    pet.pSpawn = pSpawn;
    strncpy_s(pet.name, GetSpawnName(pSpawn), _TRUNCATE);

    m_pets.push_back(pet);
    LogFramework("MultiPet: Detected pet '%s' (ID %u) via MasterID", pet.name, spawnID);
}

void MultiPet::RebuildSpawnMap()
{
    if (!s_GetSpawnByID) return;

    auto* mgr = reinterpret_cast<void*>(GameState::GetSpawnManager());
    if (!mgr) return;

    // Read NextID from PlayerManagerClient (+0x04) — upper bound for spawn IDs
    uint32_t nextID = *reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(mgr) + OFF_MGR_NEXT_ID);
    if (nextID == 0 || nextID > 10000) nextID = 1000;  // safety cap

    int count = 0;
    for (uint32_t id = 1; id < nextID; ++id)
    {
        void* pSpawn = s_GetSpawnByID(mgr, nullptr, static_cast<int>(id));
        if (pSpawn)
        {
            m_spawnMap[id] = pSpawn;
            count++;
        }
    }

    LogFramework("MultiPet: Rebuilt spawn map via GetSpawnByID — %d spawns (scanned 1-%u)",
        count, nextID - 1);
}

void MultiPet::ScanForPets()
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal) return;

    uint32_t localID = GetSpawnID(pLocal);
    int uiPetID = GetPetID(pLocal);

    // Scan the spawn map (populated by OnAddSpawn + RebuildSpawnMap)
    for (const auto& [sid, pSpawn] : m_spawnMap)
    {
        if (!pSpawn) continue;

        uint32_t masterID = GetMasterID(pSpawn);
        if (masterID != localID || masterID == 0) continue;
        if (static_cast<int>(sid) == uiPetID) continue;

        // Already tracked?
        bool found = false;
        for (const auto& pet : m_pets)
            if (pet.spawnID == sid) { found = true; break; }
        if (found) continue;

        TrackedPet pet;
        pet.spawnID = sid;
        pet.pSpawn = pSpawn;
        strncpy_s(pet.name, GetSpawnName(pSpawn), _TRUNCATE);

        m_pets.push_back(pet);
        LogFramework("MultiPet: Detected pet '%s' (ID %u) via scan", pet.name, sid);
    }
}

// ---------------------------------------------------------------------------
// Clear all tracking state
// ---------------------------------------------------------------------------

void MultiPet::ClearAllTracking()
{
    m_pets.clear();
    m_spawnMap.clear();
    m_localSpawnID = 0;
    m_needsResolve = false;

    LogFramework("MultiPet: All tracking cleared");
}

// ---------------------------------------------------------------------------
// /pets command
// ---------------------------------------------------------------------------

void MultiPet::ListPets()
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal)
    {
        WriteChatf("No pets found.");
        return;
    }

    int uiPetID = GetPetID(pLocal);
    bool hasPets = false;

    WriteChatf("--- Your Pets ---");

    // Show the UI pet first
    if (uiPetID > 0)
    {
        auto it = m_spawnMap.find(static_cast<uint32_t>(uiPetID));
        if (it != m_spawnMap.end())
            WriteChatf("  [UI Pet] %s - ID %d", GetSpawnName(it->second), uiPetID);
        else
            WriteChatf("  [UI Pet] (not resolved) - ID %d", uiPetID);
        hasPets = true;
    }

    // Show tracked secondary pets
    for (const auto& pet : m_pets)
    {
        const char* className = GetClassName(pet.classID);
        if (pet.pSpawn)
            WriteChatf("  %s - ID %u (%s)", pet.name, pet.spawnID, className);
        else
            WriteChatf("  (unresolved) - ID %u (%s)", pet.spawnID, className);
        hasPets = true;
    }

    if (!hasPets)
        WriteChatf("  No pets found.");

    WriteChatf("-----------------");
}

// ---------------------------------------------------------------------------
// /petcycle command
// ---------------------------------------------------------------------------

void MultiPet::CyclePet()
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal)
    {
        WriteChatf("MultiPet: Not in game.");
        return;
    }

    int uiPetID = GetPetID(pLocal);

    // Build list of all pet SpawnIDs
    std::vector<uint32_t> allPetIDs;

    if (uiPetID > 0)
        allPetIDs.push_back(static_cast<uint32_t>(uiPetID));

    for (const auto& pet : m_pets)
        allPetIDs.push_back(pet.spawnID);

    if (allPetIDs.size() <= 1)
    {
        WriteChatf("MultiPet: No other pets to cycle to.");
        return;
    }

    // Find current UI pet and advance
    uint32_t currentUI = (uiPetID > 0) ? static_cast<uint32_t>(uiPetID) : 0;
    int currentIndex = -1;
    for (int i = 0; i < static_cast<int>(allPetIDs.size()); ++i)
    {
        if (allPetIDs[i] == currentUI) { currentIndex = i; break; }
    }

    int nextIndex = (currentIndex + 1) % static_cast<int>(allPetIDs.size());
    uint32_t newUIPetID = allPetIDs[nextIndex];

    // Move old UI pet into secondary tracking
    if (uiPetID > 0)
    {
        bool alreadyTracked = false;
        for (const auto& pet : m_pets)
        {
            if (pet.spawnID == static_cast<uint32_t>(uiPetID)) { alreadyTracked = true; break; }
        }

        if (!alreadyTracked)
        {
            TrackedPet tracked;
            tracked.spawnID = static_cast<uint32_t>(uiPetID);

            auto mapIt = m_spawnMap.find(tracked.spawnID);
            if (mapIt != m_spawnMap.end())
            {
                tracked.pSpawn = mapIt->second;
                strncpy_s(tracked.name, GetSpawnName(tracked.pSpawn), _TRUNCATE);
            }

            m_pets.push_back(tracked);
        }
    }

    // Remove new UI pet from secondary tracking
    auto it = std::find_if(m_pets.begin(), m_pets.end(),
        [newUIPetID](const TrackedPet& p) { return p.spawnID == newUIPetID; });

    if (it != m_pets.end())
        m_pets.erase(it);

    // Write new PetID
    SetPetID(pLocal, static_cast<int>(newUIPetID));

    // Get name for chat message
    const char* newPetName = "Unknown";
    auto mapIt = m_spawnMap.find(newUIPetID);
    if (mapIt != m_spawnMap.end())
        newPetName = GetSpawnName(mapIt->second);

    WriteChatf("MultiPet: Pet window now showing '%s' (ID %u)", newPetName, newUIPetID);
}

// ---------------------------------------------------------------------------
// SwapToPet — promote a specific secondary pet (by SpawnID) to the UI slot
// ---------------------------------------------------------------------------

void MultiPet::SwapToPet(uint32_t spawnID)
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal)
    {
        WriteChatf("MultiPet: Not in game.");
        return;
    }

    // Verify the target pet is actually tracked
    auto it = std::find_if(m_pets.begin(), m_pets.end(),
        [spawnID](const TrackedPet& p) { return p.spawnID == spawnID; });

    if (it == m_pets.end())
    {
        WriteChatf("MultiPet: Pet ID %u not found in tracked pets.", spawnID);
        return;
    }

    int uiPetID = GetPetID(pLocal);

    // Move current UI pet into secondary tracking
    if (uiPetID > 0)
    {
        bool alreadyTracked = false;
        for (const auto& pet : m_pets)
        {
            if (pet.spawnID == static_cast<uint32_t>(uiPetID)) { alreadyTracked = true; break; }
        }

        if (!alreadyTracked)
        {
            TrackedPet tracked;
            tracked.spawnID = static_cast<uint32_t>(uiPetID);

            auto mapIt = m_spawnMap.find(tracked.spawnID);
            if (mapIt != m_spawnMap.end())
            {
                tracked.pSpawn = mapIt->second;
                strncpy_s(tracked.name, GetSpawnName(tracked.pSpawn), _TRUNCATE);
            }

            m_pets.push_back(tracked);
        }
    }

    // Re-find iterator after push_back (may have invalidated)
    it = std::find_if(m_pets.begin(), m_pets.end(),
        [spawnID](const TrackedPet& p) { return p.spawnID == spawnID; });

    if (it == m_pets.end()) return;  // shouldn't happen

    m_pets.erase(it);

    // Set new UI pet
    SetPetID(pLocal, static_cast<int>(spawnID));

    const char* newPetName = "Unknown";
    auto mapIt = m_spawnMap.find(spawnID);
    if (mapIt != m_spawnMap.end())
        newPetName = GetSpawnName(mapIt->second);

    WriteChatf("MultiPet: Pet window now showing '%s' (ID %u)", newPetName, spawnID);
}

// ---------------------------------------------------------------------------
// /petdebug command
// ---------------------------------------------------------------------------

void MultiPet::DebugSpawns()
{
    auto* pLocal = GameState::GetLocalPlayer();
    if (!pLocal)
    {
        WriteChatf("MultiPet Debug: Not in game.");
        return;
    }

    uint32_t localID = GetSpawnID(pLocal);
    int uiPetID = GetPetID(pLocal);

    WriteChatf("--- MultiPet Debug ---");
    WriteChatf("  Local player: ID %u, PetID %d", localID, uiPetID);
    WriteChatf("  Spawn map size: %d", static_cast<int>(m_spawnMap.size()));
    WriteChatf("  Tracked secondary pets: %d", static_cast<int>(m_pets.size()));
    WriteChatf("  Needs resolve: %s", m_needsResolve ? "yes" : "no");

    for (const auto& pet : m_pets)
    {
        WriteChatf("    Pet '%s' ID %u class %u/%s spawn=%s",
            pet.name[0] ? pet.name : "(unresolved)",
            pet.spawnID, pet.classID, GetClassName(pet.classID),
            pet.pSpawn ? "yes" : "no");
    }

    // Scan spawns via GetSpawnByID for diagnostics
    WriteChatf("  --- Spawn Scan (GetSpawnByID) ---");
    if (s_GetSpawnByID)
    {
        auto* mgr = reinterpret_cast<void*>(GameState::GetSpawnManager());
        uint32_t nextID = mgr ? *reinterpret_cast<uint32_t*>(
            reinterpret_cast<uintptr_t>(mgr) + OFF_MGR_NEXT_ID) : 0;
        WriteChatf("    NextID=%u, SpawnMap size=%d", nextID, (int)m_spawnMap.size());

        // Show all spawns with MasterID matching local player
        for (const auto& [sid, sp] : m_spawnMap)
        {
            if (!sp) continue;
            uint32_t master = GetMasterID(sp);
            if (master == localID)
            {
                WriteChatf("    ID=%u '%s' master=%u %s",
                    sid, GetSpawnName(sp), master,
                    (static_cast<int>(sid) == uiPetID) ? "(UI pet)" : "(secondary)");
            }
        }
    }

    WriteChatf("----------------------");
}
