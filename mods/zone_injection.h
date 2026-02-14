/**
 * @file zone_injection.h
 * @brief ZoneInjection mod — injects custom zone entries into the client's
 *        world data, replicating the Pyrelight DLL's zone injection capability.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"
#include <vector>
#include <string>

class ZoneInjection : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
    void        OnSetGameState(int gameState) override;

private:
    struct ZoneDefinition
    {
        int         zoneId;
        std::string shortName;
        std::string longName;
        int         expansion;
    };

    bool LoadZoneConfig();
    void InjectZones();

    std::vector<ZoneDefinition> m_zones;
    bool m_injected = false;
};
