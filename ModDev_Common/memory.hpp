#pragma once
#include <cstdint>
#include <cstring>
#include <windows.h>

namespace mem {

inline bool PatchBytes(uintptr_t addr, const void* data, size_t size) {
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

template <typename T>
inline void Write(uintptr_t addr, T value) {
    PatchBytes(addr, &value, sizeof(T));
}

// Replaces a 5-byte relative CALL (0xE8) and returns the original callee target.
// Chainable across multiple ASI plugins safely!
inline uintptr_t HookCall(uintptr_t callAddr, uintptr_t newTarget) {
    int32_t oldRel = Read<int32_t>(callAddr + 1);
    uintptr_t original = callAddr + 5 + static_cast<uintptr_t>(oldRel);

    uint8_t patch[5];
    patch[0] = 0xE8;
    int32_t newRel = static_cast<int32_t>(newTarget - (callAddr + 5));
    std::memcpy(patch + 1, &newRel, 4);
    PatchBytes(callAddr, patch, 5);
    return original;
}

// Replaces a 5-byte relative JMP (0xE9) and returns the original jump target.
inline uintptr_t HookJump(uintptr_t jmpAddr, uintptr_t newTarget) {
    int32_t oldRel = Read<int32_t>(jmpAddr + 1);
    uintptr_t original = jmpAddr + 5 + static_cast<uintptr_t>(oldRel);

    uint8_t patch[5];
    patch[0] = 0xE9;
    int32_t newRel = static_cast<int32_t>(newTarget - (jmpAddr + 5));
    std::memcpy(patch + 1, &newRel, 4);
    PatchBytes(jmpAddr, patch, 5);
    return original;
}

// Replaces a 5-byte relative JMP (0xE9).
inline void WriteJmp(uintptr_t jmpAddr, uintptr_t target) {
    HookJump(jmpAddr, target);
}

} // namespace mem
