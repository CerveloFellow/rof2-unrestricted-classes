/**
 * @file version_check.cpp
 * @brief Implementation of VersionCheck mod — reads the client's compiled version
 *        date/time from eqgame.exe memory and compares against the expected
 *        ROF2 version ("May 10 2013" / "23:30:08").
 *
 * Ported from macroquest-emu/src/main/MacroQuest.cpp (CoreInitialize).
 * Logs a warning on mismatch but does not block — we want flexibility to
 * experiment with slightly different client builds.
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "version_check.h"
#include "../core.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>
#include <cstring>

const char* VersionCheck::GetName() const
{
    return "VersionCheck";
}

bool VersionCheck::Initialize()
{
    LogFramework("VersionCheck: Initializing...");

    // Read actual version strings from game memory
    uintptr_t dateAddr = eqlib::FixEQGameOffset(__ActualVersionDate_x);
    uintptr_t timeAddr = eqlib::FixEQGameOffset(__ActualVersionTime_x);

    const char* actualDate = reinterpret_cast<const char*>(dateAddr);
    const char* actualTime = reinterpret_cast<const char*>(timeAddr);

    LogFramework("VersionCheck: Expected client: %s %s", __ExpectedVersionDate, __ExpectedVersionTime);
    LogFramework("VersionCheck: Actual client:   %s %s", actualDate, actualTime);

    bool dateMatch = (strncmp(__ExpectedVersionDate, actualDate, strlen(__ExpectedVersionDate)) == 0);
    bool timeMatch = (strncmp(__ExpectedVersionTime, actualTime, strlen(__ExpectedVersionTime)) == 0);

    if (!dateMatch || !timeMatch)
    {
        LogFramework("VersionCheck: WARNING — Client version mismatch!");
        LogFramework("VersionCheck:   Expected: %s %s", __ExpectedVersionDate, __ExpectedVersionTime);
        LogFramework("VersionCheck:   Actual:   %s %s", actualDate, actualTime);
        LogFramework("VersionCheck:   Continuing anyway (non-blocking check)");
    }
    else
    {
        LogFramework("VersionCheck: Client version matches — OK");
    }

    LogFramework("VersionCheck: Initialized");
    return true;
}

void VersionCheck::Shutdown()
{
    LogFramework("VersionCheck: Shutdown");
}

void VersionCheck::OnPulse()
{
    // No per-frame work needed
}

bool VersionCheck::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}
