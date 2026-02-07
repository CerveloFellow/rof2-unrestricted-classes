#pragma once

#include <cstdint>

// Abstract base class for all mods. Each mod implements this interface
// and registers itself with Core during startup.
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
