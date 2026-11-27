#pragma once
#include <cstdint>
#include <windows.h>

// ============================================================================
// Standalone Integration Module: SilentPatch / ModelExtras / Limit Adjusters /
// Proper Vehicle Damage / Proper Shaders Runtime Compatibility
//
// Reads potentially relocated game tables (CModelInfo::ms_modelInfoPtrs and
// mod_HandlingManager) dynamically from gta_sa.exe code references so Open
// Limit Adjuster, Fastman92 Limit Adjuster, ModelExtras, and custom carpacks
// work seamlessly without hardcoded vanilla bounds.
// ============================================================================
namespace integrations::limits {

inline bool IsSilentPatchLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("SilentPatchSA.asi") != nullptr ||
                   GetModuleHandleA("SilentPatch.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

inline bool IsProperVehicleDamageLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("Proper Vehicle Damage.asi") != nullptr ||
                   GetModuleHandleA("ProperVehicleDamage.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

inline bool IsProperShadersLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("Proper Shaders.asi") != nullptr ||
                   GetModuleHandleA("ProperShaders.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

inline bool IsModelExtrasLoaded() {
    static int s_state = -1;
    if (s_state < 0) {
        s_state = (GetModuleHandleA("ModelExtras.asi") != nullptr ||
                   GetModuleHandleA("VehFuncs.asi") != nullptr) ? 1 : 0;
    }
    return s_state == 1;
}

// Reads CModelInfo::ms_modelInfoPtrs dynamically (supports OLA / FLA / ModelExtras relocation).
// Verified at 0x006B0B22: mov eax, ds:ms_modelInfoPtrs[eax*4] (pointer stored at 0x006B0B25).
inline void** GetModelInfoPtrs() {
    static void** s_table = nullptr;
    if (!s_table) {
        uintptr_t p = *reinterpret_cast<const uintptr_t*>(0x006B0B22 + 3);
        s_table = (p >= 0x00400000) ? reinterpret_cast<void**>(p)
                                    : reinterpret_cast<void**>(0x00A9B0C8);
    }
    return s_table;
}

inline int32_t GetModelInfoMaxId() {
    static int32_t s_maxId = 0;
    if (s_maxId == 0) {
        uintptr_t cur = reinterpret_cast<uintptr_t>(GetModelInfoPtrs());
        // If a Limit Adjuster relocated ms_modelInfoPtrs above vanilla 0x00A9B0C8, allow expanded IDs.
        s_maxId = (cur != 0x00A9B0C8) ? 65536 : 20000;
    }
    return s_maxId;
}

inline void* GetModelInfo(int32_t modelId) {
    if (modelId < 0 || modelId >= GetModelInfoMaxId()) return nullptr;
    return GetModelInfoPtrs()[modelId];
}

// Reads mod_HandlingManager base pointer dynamically (supports Limit Adjuster handling relocation).
// Verified at 0x006B0BA2: lea ecx, gHandlingDataMgr (pointer stored at 0x006B0BA4).
inline uint8_t* GetHandlingDataMgrBase() {
    static uint8_t* s_mgr = nullptr;
    if (!s_mgr) {
        uintptr_t p = *reinterpret_cast<const uintptr_t*>(0x006B0BA2 + 2);
        s_mgr = (p >= 0x00400000) ? reinterpret_cast<uint8_t*>(p)
                                  : reinterpret_cast<uint8_t*>(0x00C2B9DC);
    }
    return s_mgr;
}

} // namespace integrations::limits
