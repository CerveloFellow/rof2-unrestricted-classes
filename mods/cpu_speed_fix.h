/**
 * @file cpu_speed_fix.h
 * @brief CpuSpeedFix mod — normalizes CPU timing for older games and sets
 *        process affinity to all cores.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class CpuSpeedFix : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;

private:
    void SetCpuAffinity();
    bool InstallQpcFix();

    bool m_qpcFixInstalled = false;
};
