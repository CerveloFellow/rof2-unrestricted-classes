/**
 * @file combat_abilities.h
 * @brief CombatAbilities mod — patches out the class restriction that prevents
 *        pure casters from opening the Combat Abilities window.
 * @date 2026-02-11
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class CombatAbilities : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
};
