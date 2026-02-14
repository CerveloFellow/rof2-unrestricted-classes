/**
 * @file version_check.h
 * @brief VersionCheck mod — validates the EQ client version at startup.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class VersionCheck : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
};
