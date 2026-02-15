/**
 * @file pet_window.h
 * @brief PetWindow mod — POC for multi-pet display in the Pet Info Window.
 *
 * Phase 1: Find the CPetInfoWnd instance by scanning CXWndManager's window list.
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

private:
    void* m_petInfoWnd = nullptr;
    void* m_newGauge1 = nullptr;
    void* m_newGauge2 = nullptr;
};
