/**
 * @file hooks.cpp
 * @brief Implementation of detour install/remove using Microsoft Detours.
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 */

#include "pch.h"
#include "hooks.h"
#include "core.h"

#include <detours/detours.h>
#include <vector>
#include <string>

namespace Hooks
{

struct HookRecord
{
    std::string name;
    void**      target;   // pointer to the original-function pointer
    void*       detour;   // our replacement function
};

static std::vector<HookRecord> s_hooks;

bool Install(const char* name, void** target, void* detour)
{
    LogFramework("Hooks::Install '%s' target=0x%p detour=0x%p", name, *target, detour);

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        LogFramework("  DetourTransactionBegin failed: %ld", error);
        return false;
    }

    DetourUpdateThread(GetCurrentThread());

    error = DetourAttach(target, detour);
    if (error != NO_ERROR)
    {
        LogFramework("  DetourAttach failed: %ld", error);
        DetourTransactionAbort();
        return false;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR)
    {
        LogFramework("  DetourTransactionCommit failed: %ld", error);
        return false;
    }

    s_hooks.push_back({ name, target, detour });
    LogFramework("  Hook '%s' installed successfully", name);
    return true;
}

bool Remove(const char* name)
{
    for (auto it = s_hooks.begin(); it != s_hooks.end(); ++it)
    {
        if (it->name == name)
        {
            LogFramework("Hooks::Remove '%s'", name);

            LONG error = DetourTransactionBegin();
            if (error != NO_ERROR) return false;

            DetourUpdateThread(GetCurrentThread());

            error = DetourDetach(it->target, it->detour);
            if (error != NO_ERROR)
            {
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) return false;

            s_hooks.erase(it);
            LogFramework("  Hook '%s' removed", name);
            return true;
        }
    }

    LogFramework("Hooks::Remove '%s' - not found", name);
    return false;
}

void RemoveAll()
{
    LogFramework("Hooks::RemoveAll — %zu hooks to remove", s_hooks.size());

    if (s_hooks.empty())
        return;

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR)
    {
        LogFramework("  DetourTransactionBegin failed: %ld", error);
        return;
    }

    DetourUpdateThread(GetCurrentThread());

    for (auto& hook : s_hooks)
    {
        error = DetourDetach(hook.target, hook.detour);
        if (error != NO_ERROR)
            LogFramework("  DetourDetach '%s' failed: %ld", hook.name.c_str(), error);
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR)
    {
        LogFramework("  DetourTransactionCommit failed: %ld", error);
        return;
    }

    LogFramework("  All hooks removed");
    s_hooks.clear();
}

} // namespace Hooks
