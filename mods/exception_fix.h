/**
 * @file exception_fix.h
 * @brief ExceptionFix mod — removes the EMU client's global exception handler
 *        from the SEH chain to prevent crash-swallowing.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class ExceptionFix : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;

private:
    bool RemoveExceptionHandler();
    bool m_fixed = false;
};
