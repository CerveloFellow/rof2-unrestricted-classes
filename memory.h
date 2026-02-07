/**
 * @file memory.h
 * @brief Memory read/write helpers for patching and reading game process memory.
 * @date 2026-02-07
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <windows.h>

namespace Memory
{

// Write arbitrary bytes to a memory address, temporarily removing write protection.
inline bool PatchMemory(uintptr_t address, const void* bytes, size_t len)
{
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(address), len, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(reinterpret_cast<void*>(address), bytes, len);

    VirtualProtect(reinterpret_cast<void*>(address), len, oldProtect, &oldProtect);
    return true;
}

// Typed read from a game memory address.
template <typename T>
inline T ReadMemory(uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

} // namespace Memory
