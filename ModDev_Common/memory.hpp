#pragma once
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace mem {

inline bool PatchBytes(uintptr_t addr, const void* data, size_t size) {
    if (!addr || !data || size == 0) return false;
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(addr), data, size);
    VirtualProtect(reinterpret_cast<void*>(addr), size, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), size);
    return true;
}

template <typename T>
inline T Read(uintptr_t addr) {
    return *reinterpret_cast<const T*>(addr);
}

// Safely replaces a 5-byte relative CALL (0xE8) and returns the previous callee target.
// Verifies opcode 0xE8 before patching so unexpected patches from other mods never corrupt code.
inline uintptr_t HookCall(uintptr_t callAddr, uintptr_t newTarget) {
    if (!callAddr || !newTarget) return 0;
    if (Read<uint8_t>(callAddr) != 0xE8) return 0;

    int32_t oldRel = Read<int32_t>(callAddr + 1);
    uintptr_t original = callAddr + 5 + static_cast<uintptr_t>(oldRel);

    uint8_t patch[5];
    patch[0] = 0xE8;
    int32_t newRel = static_cast<int32_t>(newTarget - (callAddr + 5));
    std::memcpy(patch + 1, &newRel, 4);
    if (!PatchBytes(callAddr, patch, 5)) return 0;
    return original;
}

// Safely replaces a 5-byte relative JMP (0xE9) and returns the previous jump target.
// Verifies opcode 0xE9 before patching so unexpected patches from other mods never corrupt code.
inline uintptr_t HookJump(uintptr_t jmpAddr, uintptr_t newTarget) {
    if (!jmpAddr || !newTarget) return 0;
    if (Read<uint8_t>(jmpAddr) != 0xE9) return 0;

    int32_t oldRel = Read<int32_t>(jmpAddr + 1);
    uintptr_t original = jmpAddr + 5 + static_cast<uintptr_t>(oldRel);

    uint8_t patch[5];
    patch[0] = 0xE9;
    int32_t newRel = static_cast<int32_t>(newTarget - (jmpAddr + 5));
    std::memcpy(patch + 1, &newRel, 4);
    if (!PatchBytes(jmpAddr, patch, 5)) return 0;
    return original;
}

} // namespace mem
