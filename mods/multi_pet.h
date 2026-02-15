/**
 * @file multi_pet.h
 * @brief MultiPet mod — tracks multiple simultaneous pets for THJ multiclass
 *        characters and populates XTarget slots so secondary pets show HP bars.
 *
 * Pet identification uses two methods:
 *   1. MasterID scanning — OnAddSpawn checks MasterID (offset 0x38C) on each
 *      spawn. If it matches the local player and isn't the UI pet, it's tracked.
 *      A periodic scan catches pets whose MasterID was set after spawn.
 *   2. OP_PetList (0x1339) — if the server sends this custom opcode, it
 *      overrides MasterID detection with authoritative pet data + class IDs.
 *
 * Spawn resolution: OnAddSpawn/OnRemoveSpawn maintain a SpawnID→pointer map.
 * XTarget: Writes secondary pets into AutoHater slots that have no target.
 * Commands: /pets (list), /petcycle (rotate UI pet), /petdebug (diagnostics).
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct TrackedPet
{
    uint32_t    spawnID   = 0;
    uint32_t    classID   = 0;      // owner class (mage=12, necro=10, etc.)
    void*       pSpawn    = nullptr;
    char        name[64]  = {};
    int         xtSlot    = -1;     // XTarget slot index, -1 = unassigned
};

class MultiPet : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
    void        OnAddSpawn(void* pSpawn) override;
    void        OnRemoveSpawn(void* pSpawn) override;
    void        OnSetGameState(int gameState) override;

    // Public so static command handlers can access
    void ListPets();
    void CyclePet();
    void DebugSpawns();

    // Accessor for other mods (e.g. PetWindow)
    static MultiPet* GetInstance();
    const std::vector<TrackedPet>& GetTrackedPets() const { return m_pets; }

    void ClearAllTracking();
    void PopulateXTargetSlots();
    void ClearXTargetSlot(int slotIndex);
    void ResolvePetSpawns();
    void TryTrackPet(void* pSpawn, uint32_t spawnID);
    void ScanForPets();
    void RebuildSpawnMap();

    // Tracked secondary pets (from MasterID detection or OP_PetList)
    std::vector<TrackedPet>  m_pets;

    // SpawnID → spawn pointer map (populated via OnAddSpawn/OnRemoveSpawn)
    std::unordered_map<uint32_t, void*>  m_spawnMap;

    uint32_t m_localSpawnID  = 0;   // cached local player SpawnID (detect zone)
    bool     m_needsResolve  = false; // set when pet list arrives, cleared after resolving
    int      m_scanCounter   = 0;    // pulse counter for periodic pet scan
};
