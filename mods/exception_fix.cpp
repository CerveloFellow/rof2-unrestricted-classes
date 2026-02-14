/**
 * @file exception_fix.cpp
 * @brief Implementation of ExceptionFix mod.
 *
 * Ported from macroquest-emu/src/main/emu/EmuExtensions.cpp (lines 254-313).
 *
 * The EMU (ROF2) client wraps its main game loop in a global __try/__catch
 * block with an exception filter at __ExceptionFilter_x (0x8E3338). This
 * swallows all exceptions, preventing us from catching crashes in our own
 * unhandled exception filter.
 *
 * This mod walks the SEH (Structured Exception Handling) chain via FS:[0]
 * and removes the registration record whose handler matches __ExceptionFilter.
 * This allows crashes to propagate to our crash handler (and the OS) so we
 * get proper crash dumps.
 *
 * A /sehchain command is also provided for debugging the SEH chain.
 *
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "exception_fix.h"
#include "../core.h"
#include "../commands.h"

#include <eqlib/Offsets.h>
#include <eqlib/offsets/eqgame.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// SEH chain structures
// ---------------------------------------------------------------------------

struct EXCEPTION_REGISTRATION_RECORD_
{
    EXCEPTION_REGISTRATION_RECORD_* Next;
    PVOID                           ExceptionHandler;
};

// ---------------------------------------------------------------------------
// SEH chain access (x86 only — reads FS:[0])
// ---------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable: 4733)  // Inline asm modifying FS:[0]

static EXCEPTION_REGISTRATION_RECORD_* GetExceptionRegistrationRecords()
{
    EXCEPTION_REGISTRATION_RECORD_* result;

    __asm {
        mov eax, dword ptr fs:[0]
        mov result, eax
    }

    return result;
}

static void SetExceptionRegistrationHead(EXCEPTION_REGISTRATION_RECORD_* pRecord)
{
    __asm {
        mov eax, pRecord
        mov dword ptr fs:[0], eax
    }
}

#pragma warning(pop)

// ---------------------------------------------------------------------------
// /sehchain debug command
// ---------------------------------------------------------------------------

static uintptr_t s_exceptionFilterAddr = 0;

static void Cmd_SEHChain(eqlib::PlayerClient* pChar, const char* szLine)
{
    EXCEPTION_REGISTRATION_RECORD_* pRecord = GetExceptionRegistrationRecords();

    WriteChatf("SEH Chain:");
    while (reinterpret_cast<uintptr_t>(pRecord) != 0xFFFFFFFF)
    {
        const char* color = "\ag";  // green
        if (reinterpret_cast<uintptr_t>(pRecord->ExceptionHandler) == s_exceptionFilterAddr)
            color = "\ar";  // red — this is the one we want to remove

        WriteChatf("%s%p: Next=%p Handler=%p",
            color, pRecord, pRecord->Next, pRecord->ExceptionHandler);

        pRecord = pRecord->Next;
    }
}

// ---------------------------------------------------------------------------
// IMod interface
// ---------------------------------------------------------------------------

const char* ExceptionFix::GetName() const
{
    return "ExceptionFix";
}

bool ExceptionFix::Initialize()
{
    LogFramework("ExceptionFix: Initializing...");

    // Resolve __ExceptionFilter address
    s_exceptionFilterAddr = eqlib::FixEQGameOffset(__ExceptionFilter_x);
    LogFramework("ExceptionFix: ExceptionFilter = 0x%08X",
        static_cast<unsigned int>(s_exceptionFilterAddr));

    // Remove the exception handler from the SEH chain
    m_fixed = RemoveExceptionHandler();

    // Register /sehchain debug command
    Commands::AddCommand("/sehchain", Cmd_SEHChain);

    LogFramework("ExceptionFix: Initialized");
    return true;
}

void ExceptionFix::Shutdown()
{
    Commands::RemoveCommand("/sehchain");
    LogFramework("ExceptionFix: Shutdown");
}

void ExceptionFix::OnPulse()
{
    // The exception handler might be re-installed after zoning or other events.
    // Check periodically and remove it again if needed.
    // We check every ~60 frames (~1 second at 60fps) to avoid overhead.
    static int pulseCount = 0;
    if (++pulseCount >= 60)
    {
        pulseCount = 0;
        if (!m_fixed)
            m_fixed = RemoveExceptionHandler();
    }
}

bool ExceptionFix::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size)
{
    return true;
}

// ---------------------------------------------------------------------------
// Exception handler removal
// ---------------------------------------------------------------------------

bool ExceptionFix::RemoveExceptionHandler()
{
    if (!s_exceptionFilterAddr)
        return false;

    EXCEPTION_REGISTRATION_RECORD_* pRecord = GetExceptionRegistrationRecords();
    EXCEPTION_REGISTRATION_RECORD_* pPrevRecord = nullptr;

    while (reinterpret_cast<uintptr_t>(pRecord) != 0xFFFFFFFF)
    {
        if (reinterpret_cast<uintptr_t>(pRecord->ExceptionHandler) == s_exceptionFilterAddr)
        {
            LogFramework("ExceptionFix: Removing exception handler at 0x%p (handler=0x%p)",
                pRecord, pRecord->ExceptionHandler);

            if (pPrevRecord)
            {
                // Unlink from middle/end of chain
                pPrevRecord->Next = pRecord->Next;
            }
            else
            {
                // Unlink from head of chain — update FS:[0]
                SetExceptionRegistrationHead(pRecord->Next);
            }

            LogFramework("ExceptionFix: Exception handler removed successfully");
            return true;
        }

        pPrevRecord = pRecord;
        pRecord = pRecord->Next;
    }

    LogFramework("ExceptionFix: ExceptionFilter handler not found in SEH chain (may not be installed yet)");
    return false;
}
