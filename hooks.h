#pragma once

#include <cstdint>

namespace Hooks
{

// Install a detour. target must point to a function pointer that holds the
// original address; it will be overwritten with the trampoline on success.
bool Install(const char* name, void** target, void* detour);

// Remove a previously installed detour by name.
bool Remove(const char* name);

// Remove all installed detours (called during shutdown).
void RemoveAll();

} // namespace Hooks
