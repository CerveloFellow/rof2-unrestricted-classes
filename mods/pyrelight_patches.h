/**
 * @file pyrelight_patches.h
 * @brief PyrelightPatches mod — replicates memory patches from the Pyrelight DLL:
 *        MemChecker bypass, food/drink chat filter, and gamma restore on crash.
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "mod_interface.h"

class PyrelightPatches : public IMod
{
public:
    const char* GetName() const override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        OnPulse() override;
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;
    void        RestoreGammaRamp();

private:
    bool PatchMemCheckers();
    void SaveGammaRamp();

    bool m_gammaRampSaved = false;
    WORD m_savedGammaRamp[3][256] = {};
};
