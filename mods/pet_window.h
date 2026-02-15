/**
 * @file pet_window.h
 * @brief PetWindow mod — multi-pet display in the Pet Info Window.
 *
 * Phases 1-4: Find pet window, walk children, layout via XML, cache gauges.
 * Phase 5: Update gauge values (pet name + HP fill) from MultiPet data.
 *
 * @date 2026-02-14
 */

#pragma once

#include "mod_interface.h"

#include <cstdint>

class PetWindow : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
    void        OnSetGameState(int gameState) override;

    // Phase 1: Find and cache pet window pointer
    void* FindPetInfoWnd();
    void  DebugPetWindow();

    // Phase 2: Walk child widget tree
    void  DebugChildren();

    // Phase 4: Find gauge widgets for secondary pets
    void  CreateGauge();

    // Phase 5 debug: dump HP values
    void  DebugHP();

private:
    // Phase 5: Update a gauge with pet name and HP percentage
    void  UpdateGauge(void* gauge, const char* petName, int hpPercent);

    void* m_petInfoWnd = nullptr;
    void* m_newGauge1 = nullptr;
    void* m_newGauge2 = nullptr;
    bool  m_initialized = false;   // true after auto-init succeeds
    int   m_initCounter = 0;       // pulse counter for delayed init
};
