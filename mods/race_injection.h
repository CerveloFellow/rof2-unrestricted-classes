/**
 * @file race_injection.h
 * @brief RaceInjection mod — injects custom race models into the client,
 *        replicating the Pyrelight DLL's race injection capability.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"
#include <vector>
#include <string>

class RaceInjection : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
    void        OnSetGameState(int gameState) override;

    struct RaceDefinition
    {
        int         raceId;
        std::string modelName;
        int         gender;   // 0=male, 1=female, 2=neutral
    };

private:
    bool LoadRaceConfig();
    void InjectRaces();

    std::vector<RaceDefinition> m_races;
    bool m_injected = false;
};
