/**
 * @file game_state.h
 * @brief Centralized game global pointer resolution with typed getters.
 * @date 2026-02-08
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

// Forward declarations — avoids pulling in heavy eqlib headers
class CEverQuest;
struct EQGroundItem;
struct MapViewLabel;

namespace eqlib {
    class PlayerClient;
    class PlayerManagerClient;
    class PcClient;
    class CDisplay;
    class CXWndManager;
    struct ZONEINFO;
}

namespace GameState
{

// Resolve all global addresses. Call once after InitBaseAddress().
void ResolveGlobals();

// Typed getters — return nullptr/null if the game pointer is not yet set.
eqlib::PlayerClient*        GetLocalPlayer();
eqlib::PlayerClient*        GetTarget();
void                        SetTarget(eqlib::PlayerClient* pSpawn);
eqlib::PlayerClient*        GetControlledPlayer();
eqlib::PlayerManagerClient* GetSpawnManager();
eqlib::PcClient*            GetLocalPC();
eqlib::CDisplay*            GetDisplay();
eqlib::CXWndManager*        GetWndManager();
eqlib::ZONEINFO*            GetZoneInfo();
eqlib::PlayerClient*        GetSpawnList();
CEverQuest*                 GetEverQuest();

// Read CEverQuest::GameState member (offset 0x5c8).
// Returns -1 if CEverQuest instance is not yet available.
int GetGameState();

// Ground item list — calls EQGroundItemListManager::Instance() then reads Top.
EQGroundItem*  GetGroundItemListTop();

// Currently hovered map label (game global at __CurrentMapLabel_x).
MapViewLabel*  GetCurrentMapLabel();

} // namespace GameState
