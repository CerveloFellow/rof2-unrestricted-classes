/**
 * @file combat_abilities.cpp
 * @brief Implementation of CombatAbilities mod — NOPs the JE that skips the
 *        combat abilities window open for non-melee classes.
 * @date 2026-02-11
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "combat_abilities.h"
#include "../core.h"
#include "../memory.h"

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* CombatAbilities::GetName() const
{
    return "CombatAbilities";
}

bool CombatAbilities::Initialize()
{
    LogFramework("CombatAbilities: Initializing...");

    uintptr_t base   = reinterpret_cast<uintptr_t>(GetModuleHandle("eqgame.exe"));
    uintptr_t target = base + 0x25A087;

    const uint8_t expected[] = { 0x74, 0x09 };  // JE +9
    const uint8_t nops[]     = { 0x90, 0x90 };  // NOP NOP

    if (memcmp(reinterpret_cast<const void*>(target), expected, sizeof(expected)) == 0)
    {
        if (Memory::PatchMemory(target, nops, sizeof(nops)))
        {
            LogFramework("CombatAbilities: Patched JE at 0x%08X -> NOP NOP", static_cast<unsigned int>(target));
        }
        else
        {
            LogFramework("CombatAbilities: WARNING — PatchMemory failed at 0x%08X", static_cast<unsigned int>(target));
        }
    }
    else
    {
        LogFramework("CombatAbilities: WARNING — Expected bytes not found at 0x%08X (already patched or unexpected)", static_cast<unsigned int>(target));
    }

    LogFramework("CombatAbilities: Initialized");
    return true;
}

void CombatAbilities::Shutdown()
{
    LogFramework("CombatAbilities: Shutdown");
}

void CombatAbilities::OnPulse()
{
    // No per-frame work needed
}

bool CombatAbilities::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    // No packet interception needed
    return true;
}
