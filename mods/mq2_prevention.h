/**
 * @file mq2_prevention.h
 * @brief Mq2Prevention mod — prevents other MQ2 instances from loading
 *        alongside our DLL to avoid conflicts and instability.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class Mq2Prevention : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;

private:
    bool CheckForMQ2();
    void CreateMutex();

    HANDLE m_hMutex = nullptr;
    int    m_pulseCount = 0;
};
