/**
 * @file game_state.cpp
 * @brief Resolves ASLR-corrected addresses for game globals and provides typed access.
 * @date 2026-02-08
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "game_state.h"
#include "core.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>

// Resolved addresses (set once by ResolveGlobals)
static uintptr_t s_pLocalPlayer      = 0;
static uintptr_t s_pTarget           = 0;
static uintptr_t s_pControlledPlayer = 0;
static uintptr_t s_pSpawnManager     = 0;
static uintptr_t s_pLocalPC          = 0;
static uintptr_t s_pDisplay          = 0;
static uintptr_t s_pWndMgr           = 0;
static uintptr_t s_pZoneInfo         = 0;
static uintptr_t s_pEverQuest        = 0;
static uintptr_t s_groundItemListMgrInstance = 0;
static uintptr_t s_currentMapLabel   = 0;

namespace GameState
{

void ResolveGlobals()
{
    s_pLocalPlayer      = eqlib::FixEQGameOffset(pinstLocalPlayer_x);
    s_pTarget           = eqlib::FixEQGameOffset(pinstTarget_x);
    s_pControlledPlayer = eqlib::FixEQGameOffset(pinstControlledPlayer_x);
    s_pSpawnManager     = eqlib::FixEQGameOffset(pinstSpawnManager_x);
    s_pLocalPC          = eqlib::FixEQGameOffset(pinstLocalPC_x);
    s_pDisplay          = eqlib::FixEQGameOffset(pinstCDisplay_x);
    s_pWndMgr           = eqlib::FixEQGameOffset(pinstCXWndManager_x);
    s_pZoneInfo         = eqlib::FixEQGameOffset(instEQZoneInfo_x);
    s_pEverQuest        = eqlib::FixEQGameOffset(pinstCEverQuest_x);
    s_groundItemListMgrInstance = eqlib::FixEQGameOffset(EQGroundItemListManager__Instance_x);
    s_currentMapLabel   = eqlib::FixEQGameOffset(__CurrentMapLabel_x);

    LogFramework("GameState globals resolved:");
    LogFramework("  pLocalPlayer      = 0x%08X", static_cast<unsigned int>(s_pLocalPlayer));
    LogFramework("  pTarget           = 0x%08X", static_cast<unsigned int>(s_pTarget));
    LogFramework("  pControlledPlayer = 0x%08X", static_cast<unsigned int>(s_pControlledPlayer));
    LogFramework("  pSpawnManager     = 0x%08X", static_cast<unsigned int>(s_pSpawnManager));
    LogFramework("  pLocalPC          = 0x%08X", static_cast<unsigned int>(s_pLocalPC));
    LogFramework("  pDisplay          = 0x%08X", static_cast<unsigned int>(s_pDisplay));
    LogFramework("  pWndMgr           = 0x%08X", static_cast<unsigned int>(s_pWndMgr));
    LogFramework("  pZoneInfo         = 0x%08X", static_cast<unsigned int>(s_pZoneInfo));
    LogFramework("  pEverQuest        = 0x%08X", static_cast<unsigned int>(s_pEverQuest));
    LogFramework("  GroundItemMgr::Instance = 0x%08X", static_cast<unsigned int>(s_groundItemListMgrInstance));
    LogFramework("  CurrentMapLabel   = 0x%08X", static_cast<unsigned int>(s_currentMapLabel));
}

// Double-pointer dereference: the offset points to a pointer-to-pointer in game memory.
// First deref gives the game's global pointer, second gives the object.
eqlib::PlayerClient* GetLocalPlayer()
{
    if (!s_pLocalPlayer) return nullptr;
    auto ptr = *reinterpret_cast<eqlib::PlayerClient**>(s_pLocalPlayer);
    return ptr;
}

eqlib::PlayerClient* GetTarget()
{
    if (!s_pTarget) return nullptr;
    return *reinterpret_cast<eqlib::PlayerClient**>(s_pTarget);
}

void SetTarget(eqlib::PlayerClient* pTarget) {
    if (!s_pTarget)
        return;
    *reinterpret_cast<eqlib::PlayerClient**>(s_pTarget) = pTarget;
}

eqlib::PlayerClient* GetControlledPlayer()
{
    if (!s_pControlledPlayer) return nullptr;
    return *reinterpret_cast<eqlib::PlayerClient**>(s_pControlledPlayer);
}

eqlib::PlayerManagerClient* GetSpawnManager()
{
    if (!s_pSpawnManager) return nullptr;
    return *reinterpret_cast<eqlib::PlayerManagerClient**>(s_pSpawnManager);
}

eqlib::PcClient* GetLocalPC()
{
    if (!s_pLocalPC) return nullptr;
    return *reinterpret_cast<eqlib::PcClient**>(s_pLocalPC);
}

eqlib::CDisplay* GetDisplay()
{
    if (!s_pDisplay) return nullptr;
    return *reinterpret_cast<eqlib::CDisplay**>(s_pDisplay);
}

eqlib::CXWndManager* GetWndManager()
{
    if (!s_pWndMgr) return nullptr;
    return *reinterpret_cast<eqlib::CXWndManager**>(s_pWndMgr);
}

// ZoneInfo is a direct instance in game memory (not a pointer-to-pointer)
eqlib::ZONEINFO* GetZoneInfo()
{
    if (!s_pZoneInfo) return nullptr;
    return reinterpret_cast<eqlib::ZONEINFO*>(s_pZoneInfo);
}

eqlib::PlayerClient* GetSpawnList()
{
    eqlib::PlayerManagerClient* mgr = GetSpawnManager();
    if (!mgr) return nullptr;
    // PlayerManagerClient has a virtual destructor → vtable pointer at +0x00
    // Then PlayerManagerBase members: NextID(+0x04), TList<PlayerClient>(+0x08)
    // TList<T>::m_pFirstNode is at offset 0x00 within TList
    // So FirstSpawn is at mgr + 0x08
    return *reinterpret_cast<eqlib::PlayerClient**>(
        reinterpret_cast<uintptr_t>(mgr) + 0x08);
}

CEverQuest* GetEverQuest()
{
    if (!s_pEverQuest) return nullptr;
    return *reinterpret_cast<CEverQuest**>(s_pEverQuest);
}

int GetGameState()
{
    CEverQuest* pEQ = GetEverQuest();
    if (!pEQ) return -1;
    // CEverQuest::GameState is at offset 0x5c8 (from eqlib EverQuest.h)
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEQ) + 0x5c8);
}

EQGroundItem* GetGroundItemListTop()
{
    if (!s_groundItemListMgrInstance) return nullptr;

    // EQGroundItemListManager::Instance() is a static __cdecl function
    using InstanceFn = void*(__cdecl*)();
    auto fn = reinterpret_cast<InstanceFn>(s_groundItemListMgrInstance);
    void* mgr = fn();
    if (!mgr) return nullptr;

    // Top is the first member (offset 0x00) of the manager
    return *reinterpret_cast<EQGroundItem**>(mgr);
}

MapViewLabel* GetCurrentMapLabel()
{
    if (!s_currentMapLabel) return nullptr;
    return *reinterpret_cast<MapViewLabel**>(s_currentMapLabel);
}

} // namespace GameState
