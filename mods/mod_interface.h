/**
 * @file mod_interface.h
 * @brief Abstract base class (IMod) that all framework mods must implement.
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
class IMod
{
public:
    virtual ~IMod() = default;

    // Display name for logging
    virtual const char* GetName() const = 0;

    // Called once after game window is ready, before hooks are installed
    virtual bool Initialize() = 0;

    // Called once during teardown, after hooks are removed
    virtual void Shutdown() = 0;

    // Called every game frame (from ProcessGameEvents detour)
    virtual void OnPulse() = 0;

    // Called when a world message arrives (from HandleWorldMessage detour).
    // Return true to allow the message through to the original handler,
    // return false to suppress it.
    virtual bool OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) = 0;
};
