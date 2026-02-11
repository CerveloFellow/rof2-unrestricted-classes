/**
 * @file stats_override.h
 * @brief StatsOverride mod — hooks mana/endurance functions so non-caster classes
 *        display correct values from the server, and provides test defaults for
 *        verifying hooks before server-side implementation.
 * @date 2026-02-11
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class StatsOverride : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
};
