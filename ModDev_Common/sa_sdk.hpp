#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <windows.h>
#include "integrations/limit_adjusters.hpp"
#include "integrations/proper_shaders.hpp"
#include "integrations/sacoop.hpp"

namespace sa {

// ============================================================================
// Basic GTA San Andreas 1.0 US (Hoodlum, 14,383,616 bytes) Types
// ============================================================================

struct CVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float Dist2D(const CVector& o) const {
        float dx = x - o.x, dy = y - o.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    float Dist3D(const CVector& o) const {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }
    CVector operator+(const CVector& o) const { return {x + o.x, y + o.y, z + o.z}; }
    CVector operator-(const CVector& o) const { return {x - o.x, y - o.y, z - o.z}; }
    CVector operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct CRect {
    float left;
    float bottom;
    float right;
    float top;
};

struct CRGBA {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};
static_assert(sizeof(CRGBA) == 4, "CRGBA must be 4 bytes");

struct CMatrix {
    CVector right;   uint32_t flags;
    CVector up;      uint32_t pad1; // forward vector for vehicles/peds; lateral for weapon clumps
    CVector at;      uint32_t pad2; // up vector
    CVector pos;     uint32_t pad3;

    CVector TransformPoint(const CVector& local) const {
        return {
            pos.x + right.x * local.x + up.x * local.y + at.x * local.z,
            pos.y + right.y * local.x + up.y * local.y + at.y * local.z,
            pos.z + right.z * local.x + up.z * local.y + at.z * local.z
        };
    }
    CVector TransformDir(const CVector& dir) const {
        return {
            right.x * dir.x + up.x * dir.y + at.x * dir.z,
            right.y * dir.x + up.y * dir.y + at.y * dir.z,
            right.z * dir.x + up.z * dir.y + at.z * dir.z
        };
    }
};
static_assert(sizeof(CMatrix) == 64, "CMatrix / RwMatrix must be 64 bytes");

// ============================================================================
// Version Check & Global State Accessors (SA 1.0 US)
// (SA-COOP integration lives in ModDev_Common/integrations/sacoop.hpp)
// ============================================================================
inline bool IsSA10US() {
    return *reinterpret_cast<const uint32_t*>(0x0082457C) == 0x000094BF;
}

constexpr uint32_t PEDSTATE_IDLE    = 1;
constexpr uint32_t PEDSTATE_DRIVING = 50;

// Verified at 0x004396D3 (CCheat::NotWantedCheat / AEZAKMI) & 0x00438576 (CCheat::m_bHasPlayerCheated)
inline bool IsNeverWantedCheatActive() {
    return *reinterpret_cast<const uint8_t*>(0x00969171) != 0;
}

inline bool HasPlayerCheatedFlag() {
    return *reinterpret_cast<const uint8_t*>(0x0096918C) != 0;
}

inline int32_t GetGameState() {
    return *reinterpret_cast<const int32_t*>(0x00C8D4C0);
}

inline bool IsPauseMenuActive() {
    return *reinterpret_cast<const uint8_t*>(0x00BA6748 + 0x5C) != 0;
}

inline bool IsCutsceneRunning() {
    return *reinterpret_cast<const uint8_t*>(0x00B5F851) != 0;
}

inline uint32_t GetFrameCounter() {
    return *reinterpret_cast<const uint32_t*>(0x00B7CB4C); // CTimer::m_FrameCounter
}

inline uint32_t GetTimeMs() {
    return *reinterpret_cast<const uint32_t*>(0x00B7CB84); // CTimer::m_snTimeInMilliseconds
}

inline float GetTimeStep() {
    return *reinterpret_cast<const float*>(0x00B7CB5C); // CTimer::ms_fTimeStep (50fps normalized)
}

inline int32_t GetScreenWidth() {
    return *reinterpret_cast<const int32_t*>(0x00C17044); // RsGlobal.maximumWidth
}

inline int32_t GetScreenHeight() {
    return *reinterpret_cast<const int32_t*>(0x00C17048); // RsGlobal.maximumHeight
}

inline int32_t GetActiveInterior() {
    return *reinterpret_cast<const int32_t*>(0x00B72914); // CGame::currArea
}

inline uint8_t GetClockHours() {
    return *reinterpret_cast<const uint8_t*>(0x00B70153); // CClock::ms_nGameClockHours
}

inline uint8_t GetClockMinutes() {
    return *reinterpret_cast<const uint8_t*>(0x00B70152); // CClock::ms_nGameClockMinutes
}

inline bool IsOnMission() {
    const uint8_t* scriptSpace = reinterpret_cast<const uint8_t*>(0x00A49960);
    return (*reinterpret_cast<const int32_t*>(scriptSpace + 0x4 * 4)) != 0;
}

// Local Player Money (CWorld::Players[0].m_nMoney — always local player, even in SA-COOP!)
inline int32_t GetPlayerMoney() {
    return *reinterpret_cast<const int32_t*>(0x00B7CE50);
}

inline void AddPlayerMoney(int32_t delta) {
    int32_t cur = GetPlayerMoney();
    int64_t next = static_cast<int64_t>(cur) + delta;
    if (next < 0) next = 0;
    if (next > 999999999) next = 999999999;
    *reinterpret_cast<int32_t*>(0x00B7CE50) = static_cast<int32_t>(next);
}

// Pad0 Input Helpers (CPad::Pads[0] at 0x00B73458)
inline uint8_t* GetPad0() {
    return reinterpret_cast<uint8_t*>(0x00B73458);
}

// Verified at 0x0053FB80 (CPad::GetPedWalkLeftRight: cmp WORD PTR [ecx+0x10e], 0) & 0x0044C31D (CGarage)
inline bool IsPlayerControlsDisabled() {
    return *reinterpret_cast<const uint16_t*>(GetPad0() + 0x10E) != 0;
}

inline void ClearPadMenuInputs() {
    uint8_t* pad = GetPad0();
    *reinterpret_cast<int16_t*>(pad + 0x02) = 0; // LeftStickY
    *reinterpret_cast<int16_t*>(pad + 0x10) = 0; // DPadUp
    *reinterpret_cast<int16_t*>(pad + 0x12) = 0; // DPadDown
    *reinterpret_cast<int16_t*>(pad + 0x14) = 0; // DPadLeft
    *reinterpret_cast<int16_t*>(pad + 0x16) = 0; // DPadRight
    *reinterpret_cast<int16_t*>(pad + 0x1C) = 0; // ButtonSquare
    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0; // ButtonTriangle
    *reinterpret_cast<int16_t*>(pad + 0x20) = 0; // ButtonCross
    *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle
}

// Verified CPad::NewMouseControllerState at 0x00B73418 (20 bytes)
#pragma pack(push, 1)
struct CMouseControllerState {
    uint8_t lmb;
    uint8_t rmb;
    uint8_t mmb;
    uint8_t wheelUp;
    uint8_t wheelDown;
    uint8_t bmx1;
    uint8_t bmx2;
    uint8_t _pad07;
    float   Z;
    float   X;
    float   Y;
};
static_assert(sizeof(CMouseControllerState) == 20, "CMouseControllerState must be 20 bytes");
#pragma pack(pop)

inline CMouseControllerState* GetNewMouseState() {
    return reinterpret_cast<CMouseControllerState*>(0x00B73418);
}

// Clears ALL player movement, combat, camera, and mouse inputs while Phone / Modal UI is active!
// Optionally preserves TAB (LeftShoulder1) when Priority 1 Story Phone Call is ringing!
inline void ClearPadModalInputs(bool preserveTabForStoryPhone = false) {
    uint8_t* pad = GetPad0();
    *reinterpret_cast<int16_t*>(pad + 0x00) = 0; // LeftStickX  (A/D movement)
    *reinterpret_cast<int16_t*>(pad + 0x02) = 0; // LeftStickY  (W/S movement)
    *reinterpret_cast<int16_t*>(pad + 0x04) = 0; // RightStickX (Camera X)
    *reinterpret_cast<int16_t*>(pad + 0x06) = 0; // RightStickY (Camera Y)
    if (!preserveTabForStoryPhone) {
        *reinterpret_cast<int16_t*>(pad + 0x08) = 0; // LeftShoulder1 (TAB)
    }
    *reinterpret_cast<int16_t*>(pad + 0x0A) = 0; // LeftShoulder2 (Q / Prev Weapon)
    *reinterpret_cast<int16_t*>(pad + 0x0C) = 0; // RightShoulder1 (Aim)
    *reinterpret_cast<int16_t*>(pad + 0x0E) = 0; // RightShoulder2 (E / Next Weapon)
    *reinterpret_cast<int16_t*>(pad + 0x10) = 0; // DPadUp
    *reinterpret_cast<int16_t*>(pad + 0x12) = 0; // DPadDown
    *reinterpret_cast<int16_t*>(pad + 0x14) = 0; // DPadLeft
    *reinterpret_cast<int16_t*>(pad + 0x16) = 0; // DPadRight
    *reinterpret_cast<int16_t*>(pad + 0x1A) = 0; // Select
    *reinterpret_cast<int16_t*>(pad + 0x1C) = 0; // ButtonSquare (Jump/Brake)
    *reinterpret_cast<int16_t*>(pad + 0x1E) = 0; // ButtonTriangle (Enter/Exit car / F)
    *reinterpret_cast<int16_t*>(pad + 0x20) = 0; // ButtonCross (Sprint/Gas)
    *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle (Fire/Attack)
    *reinterpret_cast<int16_t*>(pad + 0x24) = 0; // ShockButtonL (Crouch/Horn)
    *reinterpret_cast<int16_t*>(pad + 0x26) = 0; // ShockButtonR
    *reinterpret_cast<int16_t*>(pad + 0x4E) = 0; // OldState.ButtonTriangle
    auto* ms = GetNewMouseState();
    if (ms) {
        ms->X = 0.0f;
        ms->Y = 0.0f;
        ms->Z = 0.0f;
        ms->lmb = 0;
        ms->rmb = 0;
        ms->wheelUp = 0;
        ms->wheelDown = 0;
    }
}

// Suppresses firing, aiming, camera rotation, weapon cycling & TAB stats while Weapon Wheel is held
inline void ClearPadWheelInputs() {
    uint8_t* pad = GetPad0();
    *reinterpret_cast<int16_t*>(pad + 0x04) = 0; // RightStickX
    *reinterpret_cast<int16_t*>(pad + 0x06) = 0; // RightStickY
    *reinterpret_cast<int16_t*>(pad + 0x08) = 0; // LeftShoulder1 (TAB / Stats)
    *reinterpret_cast<int16_t*>(pad + 0x0A) = 0; // LeftShoulder2 (Q / Prev Weapon)
    *reinterpret_cast<int16_t*>(pad + 0x0C) = 0; // RightShoulder1 (Aim)
    *reinterpret_cast<int16_t*>(pad + 0x0E) = 0; // RightShoulder2 (E / Next Weapon)
    *reinterpret_cast<int16_t*>(pad + 0x10) = 0; // DPadUp
    *reinterpret_cast<int16_t*>(pad + 0x12) = 0; // DPadDown (Stats/Group)
    *reinterpret_cast<int16_t*>(pad + 0x14) = 0; // DPadLeft
    *reinterpret_cast<int16_t*>(pad + 0x16) = 0; // DPadRight
    *reinterpret_cast<int16_t*>(pad + 0x1A) = 0; // Select
    *reinterpret_cast<int16_t*>(pad + 0x20) = 0; // ButtonCross
    *reinterpret_cast<int16_t*>(pad + 0x22) = 0; // ButtonCircle (Fire)
    auto* ms = GetNewMouseState();
    if (ms) {
        ms->X = 0.0f;
        ms->Y = 0.0f;
        ms->Z = 0.0f;
        ms->lmb = 0;
        ms->rmb = 0;
        ms->wheelUp = 0;
        ms->wheelDown = 0;
    }
}

// ============================================================================
// Camera Helpers (TheCamera at 0x00B6F028 — verified against gta_sa.exe!)
// ============================================================================
namespace camera {

inline uint8_t* GetTheCamera() {
    return reinterpret_cast<uint8_t*>(0x00B6F028);
}

inline uint8_t GetActiveCamIndex() {
    uint8_t idx = GetTheCamera()[0x59];
    return (idx < 3) ? idx : 0;
}

inline uint8_t* GetActiveCam() {
    return GetTheCamera() + 0x174 + static_cast<size_t>(GetActiveCamIndex()) * 0x238;
}

inline uint16_t GetActiveCamMode() {
    // Verified at 0x0052B68A: CCam::m_nMode is at CCam + 0x0C (TheCamera + 0x180 for cam 0)
    return *reinterpret_cast<const uint16_t*>(GetActiveCam() + 0x0C);
}

// Verified at 0x0053BF53 (CGame::Process): TheCamera is a CPlaceable (m_matrix @ +0x14, pos @ +0x30 or +0x04)
inline CVector GetPosition() {
    const CMatrix* mat = *reinterpret_cast<const CMatrix* const*>(0x00B6F03C);
    if (mat) return mat->pos;
    return *reinterpret_cast<const CVector*>(0x00B6F02C);
}

inline bool IsAimingWeaponCam() {
    uint16_t m = GetActiveCamMode();
    return (m == 53 || m == 55 || m == 65); // MODE_AIMWEAPON / FROMCAR / ATTACHED
}

inline bool IsSniperOrRocketCam() {
    uint16_t m = GetActiveCamMode();
    return (m == 7 || m == 8 || m == 51); // MODE_SNIPER / ROCKETLAUNCHER / HS
}

inline float GetActiveCamFov() {
    return *reinterpret_cast<const float*>(GetActiveCam() + 0xA0);
}

// Verified at 0x0050D280: void __thiscall CCamera::LerpFOV(float from, float to, float timeMs, bool bKeepAfterFinish)
inline void LerpFOV(float fromFov, float toFov, float timeMs, bool keepAfterFinish) {
    using Fn = void(__thiscall*)(void*, float, float, float, bool);
    reinterpret_cast<Fn>(0x0050D280)(GetTheCamera(), fromFov, toFov, timeMs, keepAfterFinish);
}

inline void StopLerpFOV() {
    uint8_t* cam = GetTheCamera();
    *reinterpret_cast<float*>(cam + 0xCA4) = 0.0f;
    *reinterpret_cast<float*>(cam + 0xCA8) = 0.0f;
    cam[0xCB4] = 0;
}

// Verified at 0x00514970 (ret 0x18): exact 3rd-person crosshair aim ray used by CWeapon::FireInstantHit!
inline void Find3rdPersonCamTargetVector(float dist, CVector gunMuzzle, CVector* outSource, CVector* outTarget) {
    using Fn = void(__thiscall*)(void*, float, CVector, CVector*, CVector*);
    reinterpret_cast<Fn>(0x00514970)(GetTheCamera(), dist, gunMuzzle, outSource, outTarget);
}

} // namespace camera

// Verified at 0x0040EA10: CStreaming::LoadAllRequestedModels(bool onlyQuickRequests)
inline void LoadAllRequestedModels(bool onlyQuick = false) {
    reinterpret_cast<void(__cdecl*)(bool)>(0x0040EA10)(onlyQuick);
}

// ============================================================================
// Weapon Structures (Verified instruction-by-instruction against gta_sa.exe!)
// ============================================================================
#pragma pack(push, 1)
struct CWeapon {
    uint32_t m_nType;              // +0x00
    uint32_t m_nState;             // +0x04 (0=READY, 1=FIRING, 2=RELOADING, 3=OUT_OF_AMMO)
    uint32_t m_nAmmoInClip;        // +0x08
    uint32_t m_nTotalAmmo;         // +0x0C
    uint32_t m_nTimeForNextShot;   // +0x10
    uint8_t  m_bFirstPersonWeapon; // +0x14
    uint8_t  m_bNoModel;           // +0x15
    uint8_t  _pad16[2];            // +0x16
    void*    m_pFxSystem;          // +0x18
};
static_assert(sizeof(CWeapon) == 0x1C, "CWeapon must be 0x1C bytes");

struct CWeaponInfo {
    uint32_t m_nWeaponFire;        // +0x00 (0=MELEE, 1=INSTANT_HIT, 2=PROJECTILE, 3=AREA_EFFECT, 4=CAMERA)
    float    m_fTargetRange;       // +0x04
    float    m_fWeaponRange;       // +0x08
    int32_t  m_nModelId1;          // +0x0C
    int32_t  m_nModelId2;          // +0x10
    uint32_t m_nSlot;              // +0x14
    uint32_t m_nFlags;             // +0x18 (0x0001=CANAIM, 0x0010=MOVEAIM, 0x0020=MOVEFIRE, 0x0800=TWIN, 0x1000=RELOAD, 0x2000=CROUCHFIRE)
    uint32_t m_nAnimGroup;         // +0x1C
    uint16_t m_nAmmoClip;          // +0x20
    uint16_t m_nDamage;            // +0x22
    CVector  m_vecFireOffset;      // +0x24..+0x2F (Weapon local space: +X=forward barrel, +Y=lateral, +Z=up!)
    uint32_t m_nSkillLevel;        // +0x30
    uint32_t m_nReqStatLevel;      // +0x34
    float    m_fAccuracy;          // +0x38
    float    m_fMoveSpeed;         // +0x3C
    float    m_fAnimLoopStart;     // +0x40
    float    m_fAnimLoopEnd;       // +0x44
    float    m_fAnimLoopFire;      // +0x48
    float    m_fAnim2LoopStart;    // +0x4C
    float    m_fAnim2LoopEnd;      // +0x50
    float    m_fAnim2LoopFire;     // +0x54
    float    m_fAnimBreakoutTime;  // +0x58
    float    m_fSpeed;             // +0x5C
    float    m_fRadius;            // +0x60
    float    m_fLifespan;          // +0x64
    float    m_fSpread;            // +0x68
    uint16_t m_nAimOffsetIndex;    // +0x6C
    uint8_t  m_nDefaultCombo;      // +0x6E
    uint8_t  m_nCombosAvailable;   // +0x6F
};
static_assert(sizeof(CWeaponInfo) == 0x70, "CWeaponInfo must be 0x70 bytes");
#pragma pack(pop)

constexpr uint32_t WEAPON_FLAG_CANAIM     = 0x0001;
constexpr uint32_t WEAPON_FLAG_MOVEAIM    = 0x0010;
constexpr uint32_t WEAPON_FLAG_MOVEFIRE   = 0x0020;
constexpr uint32_t WEAPON_FLAG_TWIN       = 0x0800;
constexpr uint32_t WEAPON_FLAG_RELOAD     = 0x1000;
constexpr uint32_t WEAPON_FLAG_CROUCHFIRE = 0x2000;

constexpr int WEAPON_INFO_COUNT = 80; // 0..79 entries at 0x00C8AAB8

inline CWeaponInfo* GetWeaponInfoTable() {
    return reinterpret_cast<CWeaponInfo*>(0x00C8AAB8);
}

// Verified at 0x00743C60: CWeaponInfo* __cdecl CWeaponInfo::GetWeaponInfo(uint32_t weaponType, int8_t skill)
inline CWeaponInfo* GetWeaponInfo(uint32_t weaponType, int8_t skill = 1) {
    using Fn = CWeaponInfo* (__cdecl*)(uint32_t, int8_t);
    return reinterpret_cast<Fn>(0x00743C60)(weaponType, skill);
}

// ============================================================================
// Entity / Vehicle / Handling Structures (SA 1.0 US verified offsets)
// ============================================================================
#pragma pack(push, 1)
struct tTransmissionGear {
    float m_fMaxVelocity;
    float m_fChangeUpVelocity;
    float m_fChangeDownVelocity;
};

struct cTransmission {
    tTransmissionGear m_aGears[6];
    uint8_t  m_nDriveType;
    uint8_t  m_nEngineType;
    uint8_t  m_nNumberOfGears;
    uint8_t  _pad4B;
    uint32_t m_nHandlingFlags;
    float    m_fEngineAcceleration;
    float    m_fEngineInertia;
    float    m_fMaxGearVelocity;
    uint32_t _unk5C;
    float    m_fMinGearVelocity;
    float    m_fCurrentSpeed;
};
static_assert(sizeof(cTransmission) == 0x68, "cTransmission size mismatch");

struct tHandlingData {
    int32_t  m_nVehicleId;
    float    m_fMass;
    float    m_fInvMass;
    float    m_fTurnMass;
    float    m_fDragMult;
    CVector  m_vecCentreOfMass;
    uint8_t  m_nPercentSubmerged;
    uint8_t  _pad21[3];
    float    m_fBuoyancyConstant;
    float    m_fTractionMultiplier;
    cTransmission m_transmissionData;
    float    m_fBrakeDeceleration;
    float    m_fBrakeBias;
    uint8_t  m_bABS;
    uint8_t  _pad9D[3];
    float    m_fSteeringLock;
    float    m_fTractionLoss;
    float    m_fTractionBias;
    float    m_fSuspensionForceLevel;
    float    m_fSuspensionDampingLevel;
    float    m_fSuspensionHighSpdComDamp;
    float    m_fSuspensionUpperLimit;
    float    m_fSuspensionLowerLimit;
    float    m_fSuspensionBiasBetweenFrontAndRear;
    float    m_fSuspensionAntiDiveMultiplier;
    float    m_fCollisionDamageMultiplier;
    uint32_t m_nModelFlags;
    uint32_t m_nHandlingFlags;
    float    m_fSeatOffsetDistance;
    uint32_t m_nMonetaryValue;
    uint8_t  m_nFrontLights;
    uint8_t  m_nRearLights;
    uint8_t  m_nAnimGroup;
    uint8_t  _padDF;
};
static_assert(sizeof(tHandlingData) == 0xE0, "tHandlingData must be 0xE0 bytes");
#pragma pack(pop)

enum eVehicleType : uint32_t {
    VEHICLE_AUTOMOBILE = 0,
    VEHICLE_MTRUCK     = 1,
    VEHICLE_QUAD       = 2,
    VEHICLE_HELI       = 3,
    VEHICLE_PLANE      = 4,
    VEHICLE_BOAT       = 5,
    VEHICLE_TRAIN      = 6,
    VEHICLE_FHELI      = 7,
    VEHICLE_FPLANE     = 8,
    VEHICLE_BIKE       = 9,
    VEHICLE_BMX        = 10,
    VEHICLE_TRAILER    = 11
};

struct CEntity {
    uint8_t _raw[0x38];
    CMatrix* GetMatrix() const { return *reinterpret_cast<CMatrix* const*>(reinterpret_cast<const uint8_t*>(this) + 0x14); }
    int16_t  GetModelIndex() const { return *reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(this) + 0x22); }
    CVector  GetPosition() const {
        if (CMatrix* m = GetMatrix()) return m->pos;
        return *reinterpret_cast<const CVector*>(reinterpret_cast<const uint8_t*>(this) + 0x04);
    }
    CVector GetForward() const {
        if (CMatrix* m = GetMatrix()) return m->up;
        return {0.0f, 1.0f, 0.0f};
    }
    CVector GetRight() const {
        if (CMatrix* m = GetMatrix()) return m->right;
        return {1.0f, 0.0f, 0.0f};
    }
    CVector GetUp() const {
        if (CMatrix* m = GetMatrix()) return m->at;
        return {0.0f, 0.0f, 1.0f};
    }
    CVector TransformPoint(const CVector& local) const {
        if (CMatrix* m = GetMatrix()) return m->TransformPoint(local);
        return GetPosition();
    }
    // Verified at 0x004B5BBB & 0x005E8186: CEntity::m_nType @ +0x36 bits 0..2 (2=VEHICLE, 3=PED, 4=OBJECT)
    uint8_t GetEntityType() const {
        return reinterpret_cast<const uint8_t*>(this)[0x36] & 0x07u;
    }
};

constexpr uint8_t ENTITY_TYPE_BUILDING = 1;
constexpr uint8_t ENTITY_TYPE_VEHICLE  = 2;
constexpr uint8_t ENTITY_TYPE_PED      = 3;
constexpr uint8_t ENTITY_TYPE_OBJECT   = 4;

// Verified against CPed::IsPlayer (0x005DF8F0), data/ped.dat & data/pedgrp.dat
constexpr uint32_t PEDTYPE_PLAYER1    = 0;
constexpr uint32_t PEDTYPE_PLAYER2    = 1;
constexpr uint32_t PEDTYPE_PLAYER_NET = 2;
constexpr uint32_t PEDTYPE_CIVMALE    = 4;
constexpr uint32_t PEDTYPE_CIVFEMALE  = 5;
constexpr uint32_t PEDTYPE_COP        = 6;
constexpr uint32_t PEDTYPE_GANG1      = 7;  // Ballas
constexpr uint32_t PEDTYPE_GANG2      = 8;  // Grove Street Families (Player Gang)
constexpr uint32_t PEDTYPE_GANG3      = 9;  // Los Santos Vagos
constexpr uint32_t PEDTYPE_GANG4      = 10; // San Fierro Rifa
constexpr uint32_t PEDTYPE_GANG5      = 11; // Da Nang Boys
constexpr uint32_t PEDTYPE_GANG6      = 12; // Italian Mafia
constexpr uint32_t PEDTYPE_GANG7      = 13; // Triads
constexpr uint32_t PEDTYPE_GANG8      = 14; // Varrios Los Aztecas
constexpr uint32_t PEDTYPE_GANG9      = 15; // Unused / Enemy Gang
constexpr uint32_t PEDTYPE_GANG10     = 16; // Unused / Enemy Gang
constexpr uint32_t PEDTYPE_DEALER     = 17;
constexpr uint32_t PEDTYPE_MEDIC      = 18;
constexpr uint32_t PEDTYPE_FIREMAN    = 19;
constexpr uint32_t PEDTYPE_CRIMINAL   = 20;
constexpr uint32_t PEDTYPE_BUM        = 21;
constexpr uint32_t PEDTYPE_PROSTITUTE = 22;
constexpr uint32_t PEDTYPE_SPECIAL    = 23;
constexpr uint32_t PEDTYPE_MISSION1   = 24;
constexpr uint32_t PEDTYPE_MISSION8   = 31;

constexpr uint32_t PED_PIECE_HEAD     = 9;

struct CPed : CEntity {
    uint8_t* Raw() { return reinterpret_cast<uint8_t*>(this); }
    const uint8_t* Raw() const { return reinterpret_cast<const uint8_t*>(this); }

    float GetHealth() const    { return *reinterpret_cast<const float*>(Raw() + 0x540); }
    void  SetHealth(float v)   { *reinterpret_cast<float*>(Raw() + 0x540) = v; }
    float GetMaxHealth() const { return *reinterpret_cast<const float*>(Raw() + 0x544); }
    float GetArmor() const     { return *reinterpret_cast<const float*>(Raw() + 0x548); }
    void  SetArmor(float v)    { *reinterpret_cast<float*>(Raw() + 0x548) = v; }
    void* GetIntelligence() const { return *reinterpret_cast<void* const*>(Raw() + 0x47C); }
    void* GetPlayerData() const { return *reinterpret_cast<void* const*>(Raw() + 0x480); }
    // Verified at 0x005E8199 (CPed::CPed): m_nCreatedBy @ +0x484 (1=PED_GAME, 2=PED_MISSION)
    uint8_t GetCreatedBy() const { return Raw()[0x484]; }
    // Verified at 0x005E835A..0x005E837E (CPed::CPed): CAcquaintance @ +0x4E0 (+0x00 Respect, +0x10 Hate)
    uint32_t GetRespectMask() const { return *reinterpret_cast<const uint32_t*>(Raw() + 0x4E0); }
    uint32_t GetHateMask() const    { return *reinterpret_cast<const uint32_t*>(Raw() + 0x4F0); }
    // Verified at 0x005DF8F0 (CPed::IsPlayer): m_nPedType @ +0x598
    uint32_t GetPedType() const     { return *reinterpret_cast<const uint32_t*>(Raw() + 0x598); }
    bool IsPlayerPedType() const    { uint32_t t = GetPedType(); return t == PEDTYPE_PLAYER1 || t == PEDTYPE_PLAYER2 || t == PEDTYPE_PLAYER_NET; }
    void* GetVehiclePtr() const { return *reinterpret_cast<void* const*>(Raw() + 0x58C); }
    // Verified at 0x005E456D (CPed::SetPedState): m_ePedState @ +0x530 (1=IDLE, 24=ENTER_CAR, 25=EXIT_CAR, 50=DRIVING)
    uint32_t GetPedState() const { return *reinterpret_cast<const uint32_t*>(Raw() + 0x530); }
    // Verified at 0x00600EE6 / 0x00681750: CPedIntelligence::m_TaskMgr @ +0x04, m_aPrimaryTasks[0..3] (non-default active tasks: EnterCar/ExitCar/Jump/Climb/etc.)
    bool HasActivePrimaryTask() const {
        const uint8_t* intel = reinterpret_cast<const uint8_t*>(GetIntelligence());
        if (!intel) return false;
        void* const* primary = reinterpret_cast<void* const*>(intel + 0x04);
        return primary[0] != nullptr || primary[1] != nullptr || primary[2] != nullptr || primary[3] != nullptr;
    }
    bool  IsInVehicle() const { return (*reinterpret_cast<const uint32_t*>(Raw() + 0x46C) & 0x100) != 0 && GetVehiclePtr() != nullptr; }
    bool  IsInWater() const { return (*reinterpret_cast<const uint32_t*>(Raw() + 0x46C) & 0x40) != 0; }

    // Verified against CPed::AddWeaponModel (0x5E5ED0) & CPed::SetCurrentWeapon (0x5E61F0):
    void*    GetWeaponClump() const    { return *reinterpret_cast<void* const*>(Raw() + 0x4F4); }
    void*    GetGunflashFrame() const  { return *reinterpret_cast<void* const*>(Raw() + 0x4F8); }
    CWeapon* Weapons()                 { return reinterpret_cast<CWeapon*>(Raw() + 0x5A0); }
    const CWeapon* Weapons() const     { return reinterpret_cast<const CWeapon*>(Raw() + 0x5A0); }
    int8_t   GetActiveWeaponSlot() const { return *reinterpret_cast<const int8_t*>(Raw() + 0x718); }

    CWeapon* GetActiveWeapon() {
        int8_t slot = GetActiveWeaponSlot();
        if (slot < 0 || slot >= 13) return nullptr;
        return &Weapons()[slot];
    }
    const CWeapon* GetActiveWeapon() const {
        int8_t slot = GetActiveWeaponSlot();
        if (slot < 0 || slot >= 13) return nullptr;
        return &Weapons()[slot];
    }

    // Verified at 0x005E3B60: int8_t __thiscall CPed::GetWeaponSkill(uint32_t weaponType)
    // Returns 0 = Poor, 1 = Gangster (Std), 2 = Hitman (Pro) using player's live CStats weapon skill!
    int8_t GetWeaponSkill(uint32_t weaponType) const {
        using Fn = int8_t(__thiscall*)(const CPed*, uint32_t);
        return reinterpret_cast<Fn>(0x005E3B60)(this, weaponType);
    }

    static constexpr uint32_t BONE_PELVIS = 1;
    static constexpr uint32_t BONE_SPINE1 = 3;
    static constexpr uint32_t BONE_NECK   = 4;
    static constexpr uint32_t BONE_HEAD   = 5;

    // Verified at 0x005E4280 (ret 0x0C): void __thiscall CPed::GetBonePosition(CVector* outPos, uint32_t boneId, bool updateSkin)
    // Reads live animated RpHAnimHierarchy bone world coordinates (accurate even when sitting in barber/tattoo chairs!)
    bool GetBonePosition(CVector& outPos, uint32_t boneId, bool updateSkin = true) const {
        CVector tmp{};
        using Fn = void(__thiscall*)(const CPed*, CVector*, uint32_t, bool);
        reinterpret_cast<Fn>(0x005E4280)(this, &tmp, boneId, updateSkin);
        if (!std::isfinite(tmp.x) || !std::isfinite(tmp.y) || !std::isfinite(tmp.z)) {
            outPos = GetPosition();
            return false;
        }
        outPos = tmp;
        return true;
    }
};

struct CVehicle : CEntity {
    uint8_t* Raw() { return reinterpret_cast<uint8_t*>(this); }
    const uint8_t* Raw() const { return reinterpret_cast<const uint8_t*>(this); }

    CVector& MoveSpeed() { return *reinterpret_cast<CVector*>(Raw() + 0x44); }
    const CVector& MoveSpeed() const { return *reinterpret_cast<const CVector*>(Raw() + 0x44); }
    float Speed() const {
        const auto& v = MoveSpeed();
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    tHandlingData*& HandlingData() { return *reinterpret_cast<tHandlingData**>(Raw() + 0x384); }
    tHandlingData*  HandlingData() const { return *reinterpret_cast<tHandlingData* const*>(Raw() + 0x384); }

    uint8_t& PrimaryColor() { return *reinterpret_cast<uint8_t*>(Raw() + 0x434); }
    uint8_t& SecondaryColor() { return *reinterpret_cast<uint8_t*>(Raw() + 0x435); }
    int16_t* Upgrades() { return reinterpret_cast<int16_t*>(Raw() + 0x43A); }
    const int16_t* Upgrades() const { return reinterpret_cast<const int16_t*>(Raw() + 0x43A); }

    CPed*    GetDriver() const { return *reinterpret_cast<CPed* const*>(Raw() + 0x460); }
    float&   GasPedal() { return *reinterpret_cast<float*>(Raw() + 0x49C); }
    float&   BrakePedal() { return *reinterpret_cast<float*>(Raw() + 0x4A0); }
    // Verified at 0x00449995 (CGarage::StoreAndRemoveCarsForThisHideout: cmp BYTE PTR [esi+0x4a4], 0x2)
    // 1 = RANDOM_VEHICLE (safehouse garages store & repair), 2 = MISSION_VEHICLE (never despawns on street)
    uint8_t& CreatedBy() { return *reinterpret_cast<uint8_t*>(Raw() + 0x4A4); }
    uint8_t  CreatedBy() const { return *reinterpret_cast<const uint8_t*>(Raw() + 0x4A4); }
    uint8_t  GetCurrentGear() const { return *reinterpret_cast<const uint8_t*>(Raw() + 0x4B4); }
    float&   Health() { return *reinterpret_cast<float*>(Raw() + 0x4C0); }
    float    Health() const { return *reinterpret_cast<const float*>(Raw() + 0x4C0); }

    // Verified in CVehicle::CVehicle (0x6D5F36: m_vehicleAudio @ +0x138),
    // CAEVehicleAudioEntity::RequestNewSound (0x4F7AFC: m_aEngineSounds[slot].m_pSound @ +0xE8 + slot*8),
    // CAESound::Initialise (0x4EFE99: m_fSpeed @ +0x1C, 0x4EFF2A: m_nIsUsed @ +0x58).
    // Returns live native engine playback pitch ratio (e.g. 0.55..2.10), or 0.0f if unavailable.
    float GetLiveEnginePitchRatio() const {
        const uint8_t* ae = Raw() + 0x138;
        static constexpr int kPrioritySlots[4] = {1, 2, 3, 4};
        for (int slot : kPrioritySlots) {
            const uint8_t* snd = *reinterpret_cast<const uint8_t* const*>(ae + 0xE8 + slot * 8);
            if (!snd) continue;
            int16_t isUsed = *reinterpret_cast<const int16_t*>(snd + 0x58);
            if (isUsed != 1) continue;
            float spd = *reinterpret_cast<const float*>(snd + 0x1C);
            if (spd >= 0.30f && spd <= 3.00f) return spd;
        }
        return 0.0f;
    }
    uint8_t GetHornState() const { return *reinterpret_cast<const uint8_t*>(Raw() + 0x514); }
    uint8_t GetOverrideLights() const { return *reinterpret_cast<const uint8_t*>(Raw() + 0x584); }
    void*&  CustomCarPlateTexture() { return *reinterpret_cast<void**>(Raw() + 0x588); }
    uint32_t GetSubClass() const { return *reinterpret_cast<const uint32_t*>(Raw() + 0x594); }
    void*   GetRwClump() const { return *reinterpret_cast<void* const*>(Raw() + 0x18); }

    bool IsEngineOn() const { return (Raw()[0x428] & 0x10) != 0; }
    bool AreLightsOn() const { return (Raw()[0x428] & 0x40) != 0; }
    bool IsSirenOrAlarmOn() const { return (Raw()[0x42D] & 0x80) != 0; }
    void SetTyresDontBurst(bool v) {
        if (v) Raw()[0x42B] |= 0x80;
        else   Raw()[0x42B] &= ~0x80;
    }

    uint8_t* AutoWheelStatus() { return Raw() + 0x5A5; }
    uint32_t AutoLightsStatus() const { return *reinterpret_cast<const uint32_t*>(Raw() + 0x5AC); }
    uint8_t* BikeWheelStatus() { return Raw() + 0x65C; }

    bool IsFourWheelLike() const {
        uint32_t sc = GetSubClass();
        return sc == VEHICLE_AUTOMOBILE || sc == VEHICLE_MTRUCK || sc == VEHICLE_QUAD;
    }
    bool IsBikeLike() const {
        uint32_t sc = GetSubClass();
        return sc == VEHICLE_BIKE || sc == VEHICLE_BMX;
    }
    bool IsLandVehicle() const {
        return IsFourWheelLike() || IsBikeLike();
    }
};

// ============================================================================
// CPool Helpers (SA 1.0 US verified strides: Vehicle=0xA18, Ped=0x7C4)
// ============================================================================
struct CPoolRaw {
    uint8_t* objects;
    uint8_t* byteMap;
    int32_t  size;
    int32_t  firstFree;
};

constexpr size_t VEHICLE_POOL_STRIDE = 0xA18;
constexpr size_t PED_POOL_STRIDE     = 0x7C4;

inline CPoolRaw* GetVehiclePool() { return *reinterpret_cast<CPoolRaw**>(0x00B74494); }
inline CPoolRaw* GetPedPool()     { return *reinterpret_cast<CPoolRaw**>(0x00B74490); }

inline bool IsVehicleValid(const CVehicle* v);

// Verified at 0x006D0460: void __thiscall cTransmission::InitGearRatios(cTransmission* this)
// Recomputes all forward gear curves (1..m_nNumberOfGears) + reverse gear cleanly!
inline void InitTransmissionGearRatios(cTransmission* tr) {
    if (!tr) return;
    if (tr->m_nNumberOfGears < 1) tr->m_nNumberOfGears = 1;
    if (tr->m_nNumberOfGears > 5) tr->m_nNumberOfGears = 5;
    if (tr->m_fMinGearVelocity >= -0.02f || !std::isfinite(tr->m_fMinGearVelocity)) {
        tr->m_fMinGearVelocity = -0.25f;
    }
    using Fn = void(__thiscall*)(cTransmission*);
    reinterpret_cast<Fn>(0x006D0460)(tr);
}

// Verified against CDamageManager::ResetDamageStatus (0x006C20E0) at CAutomobile + 0x5A0:
// +0x04 engineStatus (u8), +0x05 wheelStatus[4] (u32), +0x09 doorStatus[0..5] (u32+u16),
// +0x10 lightsStatus (u32), +0x14 panelsStatus (u32).
inline bool IsVehicleVisuallyDamaged(const CVehicle* v) {
    if (!IsVehicleValid(v)) return false;
    if (v->Health() < 999.0f) return true;
    const uint8_t* raw = v->Raw();
    if (v->IsFourWheelLike()) {
        const uint8_t* dm = raw + 0x5A0;
        if (dm[0x04] != 0) return true; // engine smoke/damage
        uint32_t wheels = 0, doors0 = 0, lights = 0, panels = 0;
        uint16_t doors1 = 0;
        std::memcpy(&wheels, dm + 0x05, 4);
        std::memcpy(&doors0, dm + 0x09, 4);
        std::memcpy(&doors1, dm + 0x0D, 2);
        std::memcpy(&lights, dm + 0x10, 4);
        std::memcpy(&panels, dm + 0x14, 4);
        return (wheels != 0 || doors0 != 0 || doors1 != 0 || lights != 0 || panels != 0);
    }
    if (v->IsBikeLike()) {
        const uint8_t* bw = raw + 0x65C;
        return (bw[0] != 0 || bw[1] != 0);
    }
    return false;
}

inline bool IsVehicleValid(const CVehicle* v) {
    if (!v) return false;
    CPoolRaw* pool = GetVehiclePool();
    if (!pool || !pool->objects || !pool->byteMap) return false;
    uintptr_t base = reinterpret_cast<uintptr_t>(pool->objects);
    uintptr_t ptr  = reinterpret_cast<uintptr_t>(v);
    if (ptr < base) return false;
    uintptr_t diff = ptr - base;
    if (diff % VEHICLE_POOL_STRIDE != 0) return false;
    int32_t idx = static_cast<int32_t>(diff / VEHICLE_POOL_STRIDE);
    if (idx < 0 || idx >= pool->size) return false;
    return (pool->byteMap[idx] & 0x80) == 0;
}

inline int32_t GetVehicleHandle(const CVehicle* v) {
    if (!IsVehicleValid(v)) return -1;
    CPoolRaw* pool = GetVehiclePool();
    int32_t idx = static_cast<int32_t>((reinterpret_cast<uintptr_t>(v) - reinterpret_cast<uintptr_t>(pool->objects)) / VEHICLE_POOL_STRIDE);
    return (idx << 8) | pool->byteMap[idx];
}

inline CVehicle* GetVehicleFromHandle(int32_t handle) {
    if (handle <= 0) return nullptr;
    CPoolRaw* pool = GetVehiclePool();
    if (!pool || !pool->objects || !pool->byteMap) return nullptr;
    int32_t idx = handle >> 8;
    if (idx < 0 || idx >= pool->size) return nullptr;
    if (pool->byteMap[idx] != (handle & 0xFF)) return nullptr;
    if ((pool->byteMap[idx] & 0x80) != 0) return nullptr;
    return reinterpret_cast<CVehicle*>(pool->objects + static_cast<size_t>(idx) * VEHICLE_POOL_STRIDE);
}

inline bool IsPedValid(const CPed* p) {
    if (!p) return false;
    CPoolRaw* pool = GetPedPool();
    if (!pool || !pool->objects || !pool->byteMap) return false;
    uintptr_t base = reinterpret_cast<uintptr_t>(pool->objects);
    uintptr_t ptr  = reinterpret_cast<uintptr_t>(p);
    if (ptr < base) return false;
    uintptr_t diff = ptr - base;
    if (diff % PED_POOL_STRIDE != 0) return false;
    int32_t idx = static_cast<int32_t>(diff / PED_POOL_STRIDE);
    if (idx < 0 || idx >= pool->size) return false;
    return (pool->byteMap[idx] & 0x80) == 0;
}

inline int32_t GetPedHandle(const CPed* p) {
    if (!IsPedValid(p)) return -1;
    CPoolRaw* pool = GetPedPool();
    int32_t idx = static_cast<int32_t>((reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(pool->objects)) / PED_POOL_STRIDE);
    return (idx << 8) | pool->byteMap[idx];
}

inline CPed* GetPedFromHandle(int32_t handle) {
    if (handle <= 0) return nullptr;
    CPoolRaw* pool = GetPedPool();
    if (!pool || !pool->objects || !pool->byteMap) return nullptr;
    int32_t idx = handle >> 8;
    if (idx < 0 || idx >= pool->size) return nullptr;
    if (pool->byteMap[idx] != (handle & 0xFF)) return nullptr;
    if ((pool->byteMap[idx] & 0x80) != 0) return nullptr;
    return reinterpret_cast<CPed*>(pool->objects + static_cast<size_t>(idx) * PED_POOL_STRIDE);
}

// ============================================================================
// Verified Native Function Bindings (GTA SA 1.0 US)
// ============================================================================
inline CPed* FindPlayerPed(int32_t playerId = 0) {
    return reinterpret_cast<CPed*(__cdecl*)(int32_t)>(0x0056E210)(playerId);
}

inline CVehicle* FindPlayerVehicle(int32_t playerId = 0, bool includeRemote = false) {
    return reinterpret_cast<CVehicle*(__cdecl*)(int32_t, bool)>(0x0056E0D0)(playerId, includeRemote);
}

inline const char* TextGet(const char* key) {
    using Fn = const char* (__thiscall*)(void*, const char*);
    return reinterpret_cast<Fn>(0x006A0050)(reinterpret_cast<void*>(0x00C1B340), key);
}

inline void* GetModelInfo(int32_t modelId) {
    return integrations::limits::GetModelInfo(modelId);
}

inline std::string GetVehicleDisplayNameGameEncoded(int32_t modelId, const char* fallbackEncoded = "Vehicle") {
    void* mi = GetModelInfo(modelId);
    if (!mi) return fallbackEncoded ? fallbackEncoded : "Vehicle";
    char gxtKey[12] = {};
    std::memcpy(gxtKey, reinterpret_cast<const uint8_t*>(mi) + 0x32, 8);
    gxtKey[8] = '\0';
    if (gxtKey[0] != '\0') {
        const char* localized = TextGet(gxtKey);
        if (localized && localized[0] != '\0') return std::string(localized);
        return std::string(gxtKey);
    }
    return fallbackEncoded ? fallbackEncoded : "Vehicle";
}

inline bool GetVehicleBounds(int32_t modelId, CVector& outMin, CVector& outMax) {
    void* mi = GetModelInfo(modelId);
    if (!mi) return false;
    uint8_t* col = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(mi) + 0x14);
    if (!col) return false;
    outMin = *reinterpret_cast<const CVector*>(col + 0x00);
    outMax = *reinterpret_cast<const CVector*>(col + 0x0C);
    return true;
}

#pragma pack(push, 1)
struct CUpgradePosn {
    CVector pos;
    float   qx, qy, qz, qw;
    int32_t parentComponentId;
};
static_assert(sizeof(CUpgradePosn) == 0x20, "CUpgradePosn must be 32 bytes");
#pragma pack(pop)

inline void EnsureUniversalUpgradeMounts(const CVehicle* veh) {
    if (!IsVehicleValid(veh) || !veh->IsFourWheelLike()) return;
    int32_t modelId = veh->GetModelIndex();
    void* mi = GetModelInfo(modelId);
    if (!mi) return;

    uint8_t* vstruct = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(mi) + 0x5C);
    if (!vstruct) return;

    CVector bMin{-0.95f, -2.1f, -0.5f}, bMax{0.95f, 2.1f, 0.85f};
    if (!GetVehicleBounds(modelId, bMin, bMax)) return;

    auto* upgs = reinterpret_cast<CUpgradePosn*>(vstruct + 0xB4);
    auto setIfMissing = [&](int slot, float x, float y, float z) {
        if (upgs[slot].parentComponentId < 0) {
            upgs[slot].pos = {x, y, z};
            upgs[slot].qx  = 0.0f;
            upgs[slot].qy  = 0.0f;
            upgs[slot].qz  = 0.0f;
            upgs[slot].qw  = 1.0f;
            upgs[slot].parentComponentId = 1; // CAR_CHASSIS
        }
    };

    setIfMissing(6,  0.0f, bMin.y + 0.36f, bMax.z * 0.42f);
    setIfMissing(14, 0.0f, (bMin.y + bMax.y) * 0.5f - 0.08f, bMax.z - 0.05f);
    setIfMissing(0,  0.0f, bMax.y * 0.52f, bMax.z * 0.32f);
    setIfMissing(12, 0.0f, bMax.y - 0.12f, bMin.z + 0.40f);
}

inline void SetModelCustomPlateText(int32_t modelId, const char* text) {
    void* mi = GetModelInfo(modelId);
    if (!mi) return;
    using Fn = void(__thiscall*)(void*, const char*);
    reinterpret_cast<Fn>(0x004C8980)(mi, text);
}

inline void* CreatePlateTexture(const char* text, int8_t plateType = -1) {
    using Fn = void* (__cdecl*)(const char*, int8_t);
    return reinterpret_cast<Fn>(0x006FDEA0)(text, plateType);
}

inline void RwTextureDestroy(void* tex) {
    if (!tex) return;
    reinterpret_cast<void(__cdecl*)(void*)>(0x007F3820)(tex);
}

inline bool ApplyLiveCustomPlate(CVehicle* veh, const char* plateText) {
    if (!IsVehicleValid(veh) || !plateText || !plateText[0]) return false;
    char padded[9];
    std::memset(padded, ' ', 8);
    padded[8] = '\0';
    size_t len = std::strlen(plateText);
    if (len > 8) len = 8;
    size_t leftPad = (8 - len) / 2;
    std::memcpy(padded + leftPad, plateText, len);

    SetModelCustomPlateText(veh->GetModelIndex(), padded);
    void* newTex = CreatePlateTexture(padded, -1);
    if (!newTex) return false;

    void*& curTex = veh->CustomCarPlateTexture();
    if (curTex && curTex != newTex) {
        RwTextureDestroy(curTex);
    }
    curTex = newTex;
    return true;
}

inline bool VehicleHasDummy(const CVehicle* veh, const char* frameName) {
    if (!IsVehicleValid(veh) || !frameName) return false;
    void* clump = veh->GetRwClump();
    if (!clump) return false;
    using Fn = void* (__cdecl*)(void*, const char*);
    return reinterpret_cast<Fn>(0x004C5400)(clump, frameName) != nullptr;
}

// ============================================================================
// World Collision & RenderWare 3D Immediate Mode (Verified SA 1.0 US)
// ============================================================================
struct CColPoint {
    CVector m_vecPoint;      // +0x00
    float   _pad0C;
    CVector m_vecNormal;     // +0x10
    float   _pad1C;
    uint8_t m_nSurfaceTypeA; // +0x20
    uint8_t m_nPieceTypeA;
    uint8_t m_nLightingA;
    uint8_t m_nSurfaceTypeB;
    uint8_t m_nPieceTypeB;
    uint8_t m_nLightingB;
    uint8_t _pad26[2];
    float   m_fDepth;        // +0x28
};
static_assert(sizeof(CColPoint) == 0x2C, "CColPoint must be 0x2C bytes");

inline bool ProcessLineOfSight(
    const CVector& origin, const CVector& target,
    CColPoint& outColPoint, CEntity*& outEntity,
    bool buildings = true, bool vehicles = true, bool peds = true,
    bool objects = true, bool dummies = false,
    bool seeThrough = false, bool cameraObjects = false, bool shootThrough = false)
{
    using Fn = bool(__cdecl*)(
        const CVector&, const CVector&, CColPoint&, CEntity*&,
        bool, bool, bool, bool, bool, bool, bool, bool);
    return reinterpret_cast<Fn>(0x0056BA00)(
        origin, target, outColPoint, outEntity,
        buildings, vehicles, peds, objects, dummies,
        seeThrough, cameraObjects, shootThrough);
}

// Shared horizontal raycast helper for unobstructed 3D preview cameras (Clothes, Barber, Tattoo, Workshop)
// Set includeObjects=false for seated interiors (Barber/Tattoo) so chair/stool CObjects never trap camera zoom!
inline float ProbeLineOfSightClearDist(const CVector& origin, float angleRad, float maxDist,
                                       float zOffset = 0.0f, bool includeObjects = true)
{
    CVector target{
        origin.x + std::cos(angleRad) * maxDist,
        origin.y + std::sin(angleRad) * maxDist,
        origin.z + zOffset
    };
    CColPoint col{};
    CEntity* hitEnt = nullptr;
    if (ProcessLineOfSight(origin, target, col, hitEnt,
                           true, false, false, includeObjects, false, false, false, false)) {
        float dx = col.m_vecPoint.x - origin.x;
        float dy = col.m_vecPoint.y - origin.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    return maxDist;
}

namespace rw {

// RenderWare 3.6 D3D9 RwIm2DVertex (28 bytes — verified against CSprite2d::SetVertices 0x00727420)
struct RwIm2DVertex {
    float    x, y, z, rhw;
    uint32_t color; // D3DCOLOR (BGRA in memory: (a<<24)|(r<<16)|(g<<8)|b)
    float    u, v;
};
static_assert(sizeof(RwIm2DVertex) == 28, "RwIm2DVertex must be 28 bytes");

// RenderWare 3.6 D3D9 RwIm3DVertex (36 bytes — verified against RwIm3DTransform 0x007EF450)
struct RwIm3DVertex {
    float    x, y, z;
    float    nx, ny, nz;
    uint32_t color; // D3DCOLOR (BGRA in memory: (a<<24)|(r<<16)|(g<<8)|b)
    float    u, v;
};
static_assert(sizeof(RwIm3DVertex) == 36, "RwIm3DVertex must be 36 bytes");

inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(b);
}

inline float GetNearScreenZ()   { return *reinterpret_cast<const float*>(0x00C80460); }
inline float GetRecipNearClip() { return *reinterpret_cast<const float*>(0x00C80464); }

// RenderState enum constants (RenderWare 3.6 — synced with integrations/proper_shaders.hpp)
constexpr int32_t STATE_TEXTURERASTER      = integrations::proper_shaders::RW_TEXTURERASTER;
constexpr int32_t STATE_ZTESTENABLE        = integrations::proper_shaders::RW_ZTESTENABLE;
constexpr int32_t STATE_SHADEMODE          = integrations::proper_shaders::RW_SHADEMODE;
constexpr int32_t STATE_ZWRITEENABLE       = integrations::proper_shaders::RW_ZWRITEENABLE;
constexpr int32_t STATE_TEXTUREFILTER      = integrations::proper_shaders::RW_TEXTUREFILTER;
constexpr int32_t STATE_SRCBLEND           = integrations::proper_shaders::RW_SRCBLEND;
constexpr int32_t STATE_DESTBLEND          = integrations::proper_shaders::RW_DESTBLEND;
constexpr int32_t STATE_VERTEXALPHAENABLE  = integrations::proper_shaders::RW_VERTEXALPHAENABLE;
constexpr int32_t STATE_FOGENABLE          = integrations::proper_shaders::RW_FOGENABLE;
constexpr int32_t STATE_CULLMODE           = integrations::proper_shaders::RW_CULLMODE;
constexpr int32_t STATE_ALPHATESTFUNCTION  = integrations::proper_shaders::RW_ALPHATESTFUNCTION;
constexpr int32_t STATE_ALPHATESTREF       = integrations::proper_shaders::RW_ALPHATESTREF;

constexpr uint32_t PRIM_LINELIST = 1;
constexpr uint32_t PRIM_TRILIST  = 3;
constexpr uint32_t PRIM_TRISTRIP = 4;
constexpr uint32_t PRIM_TRIFAN   = 5;

constexpr uint32_t IM3D_VERTEXUV   = 0x01;
constexpr uint32_t IM3D_ALLOPAQUE  = 0x02;
constexpr uint32_t IM3D_NOCLIP     = 0x04;
constexpr uint32_t IM3D_VERTEXXYZ  = 0x08;
constexpr uint32_t IM3D_VERTEXRGBA = 0x10;

inline void* GetClumpFrame(void* rpClump) {
    if (!rpClump) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(rpClump) + 0x04);
}

inline CMatrix* FrameGetLTM(void* rwFrame) {
    if (!rwFrame) return nullptr;
    return reinterpret_cast<CMatrix*(__cdecl*)(void*)>(0x007F0990)(rwFrame);
}

inline void* Im3DTransform(RwIm3DVertex* verts, uint32_t numVerts, const CMatrix* ltm, uint32_t flags) {
    using Fn = void* (__cdecl*)(RwIm3DVertex*, uint32_t, const CMatrix*, uint32_t);
    return reinterpret_cast<Fn>(0x007EF450)(verts, numVerts, ltm, flags);
}

inline int32_t Im3DRenderIndexedPrimitive(uint32_t primType, const uint16_t* indices, int32_t numIndices) {
    using Fn = int32_t(__cdecl*)(uint32_t, const uint16_t*, int32_t);
    return reinterpret_cast<Fn>(0x007EF550)(primType, indices, numIndices);
}

inline int32_t Im3DRenderLine(uint16_t vert1, uint16_t vert2) {
    uint16_t idx[2] = {vert1, vert2};
    return Im3DRenderIndexedPrimitive(PRIM_LINELIST, idx, 2);
}

inline int32_t Im3DEnd() {
    return reinterpret_cast<int32_t(__cdecl*)()>(0x007EF520)();
}

// Verified at 0x00734E90 (called by CSprite2d::DrawRect at 0x00727BB1)
inline int32_t Im2DRenderPrimitive(uint32_t primType, RwIm2DVertex* verts, int32_t numVerts) {
    using Fn = int32_t(__cdecl*)(uint32_t, RwIm2DVertex*, int32_t);
    return reinterpret_cast<Fn>(0x00734E90)(primType, verts, numVerts);
}

// Verified at 0x0058D90C & 0x00732F47 & 0x00727B6A:
// Dispatches straight through RwEngineInstance (0x00C97B24) -> dOpenDevice.fpRenderStateSet (+0x20) / Get (+0x24)!
inline int32_t RenderStateSet(int32_t state, uintptr_t value) {
    uint8_t* inst = *reinterpret_cast<uint8_t**>(0x00C97B24);
    if (!inst) return 0;
    using Fn = int32_t(__cdecl*)(int32_t, uintptr_t);
    auto fp = *reinterpret_cast<Fn*>(inst + 0x20);
    return fp ? fp(state, value) : 0;
}

inline int32_t RenderStateGet(int32_t state, uintptr_t* outValue) {
    uint8_t* inst = *reinterpret_cast<uint8_t**>(0x00C97B24);
    if (!inst || !outValue) return 0;
    using Fn = int32_t(__cdecl*)(int32_t, uintptr_t*);
    auto fp = *reinterpret_cast<Fn*>(inst + 0x24);
    return fp ? fp(state, outValue) : 0;
}

// Verified against CHud::DrawWeaponIcon (0x0058D80B..0x0058D869 & 0x0058D983):
// Note: 0x00866C54 is UPPERCASE "ICON" (CKeyGen::AppendStringToKey is case-sensitive!).
inline void* GetWeaponIconTexture(int32_t modelId) {
    if (modelId <= 0) {
        return *reinterpret_cast<void**>(0x00BAB1FC); // CHud::Sprites[0].m_pTexture (Fist!)
    }
    void* mi = GetModelInfo(modelId);
    if (!mi) return nullptr;
    int16_t txdIdx = *reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(mi) + 0x0A);
    if (txdIdx < 0) return nullptr;

    CPoolRaw* txdPool = *reinterpret_cast<CPoolRaw**>(0x00C8800C); // CTxdStore::ms_pTxdPool
    if (!txdPool || !txdPool->objects || !txdPool->byteMap || txdIdx >= txdPool->size) return nullptr;
    if ((txdPool->byteMap[txdIdx] & 0x80) != 0) return nullptr;

    void* txd = *reinterpret_cast<void**>(txdPool->objects + static_cast<size_t>(txdIdx) * 12);
    if (!txd) return nullptr;

    uint32_t modelHash = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(mi) + 0x04);
    using AppendKeyFn  = uint32_t(__cdecl*)(uint32_t, const char*);
    using FindTexFn    = void* (__cdecl*)(void*, uint32_t);
    const char* suffix = reinterpret_cast<const char*>(0x00866C54); // "ICON"
    uint32_t iconHash  = reinterpret_cast<AppendKeyFn>(0x0053CF70)(modelHash, suffix);
    return reinterpret_cast<FindTexFn>(0x00734E50)(txd, iconHash);
}

} // namespace rw

// ============================================================================
// Native HUD, Frontend Audio, Fonts & Coronas
// ============================================================================
inline void SetHelpMessage(const char* gameText, bool quickMessage = true, bool permanent = false, bool addToBrief = false) {
    reinterpret_cast<void(__cdecl*)(const char*, bool, bool, bool)>(0x00588BE0)(gameText, quickMessage, permanent, addToBrief);
}

inline void AddMessageJumpQ(const char* gameText, uint32_t timeMs, uint16_t flag = 0, bool addToPrevBriefs = false) {
    reinterpret_cast<void(__cdecl*)(const char*, uint32_t, uint16_t, bool)>(0x0069F1E0)(gameText, timeMs, flag, addToPrevBriefs);
}

inline void PlayFrontendSound(int32_t eventId, float volumeChange = 0.0f, float speed = 1.0f) {
    using Fn = void(__thiscall*)(void*, int32_t, float, float);
    reinterpret_cast<Fn>(0x00506EA0)(reinterpret_cast<void*>(0x00B6BC90), eventId, volumeChange, speed);
}

constexpr int32_t SFX_SELECT = 1;
constexpr int32_t SFX_BACK   = 2;
constexpr int32_t SFX_SCROLL = 3;
constexpr int32_t SFX_ERROR  = 4;
constexpr int32_t SFX_BUY    = 40;

namespace font {
    inline void SetScale(float w, float h) { reinterpret_cast<void(__cdecl*)(float, float)>(0x00719380)(w, h); }
    inline void SetColor(CRGBA c) { reinterpret_cast<void(__cdecl*)(CRGBA)>(0x00719430)(c); }
    inline void SetFontStyle(uint8_t style) { reinterpret_cast<void(__cdecl*)(uint8_t)>(0x00719490)(style); }
    inline void SetWrapx(float x) { reinterpret_cast<void(__cdecl*)(float)>(0x007194D0)(x); }            // m_fWrapx @ 0x00C71A88
    inline void SetCentreSize(float s) { reinterpret_cast<void(__cdecl*)(float)>(0x007194E0)(s); }       // m_fCentreSize @ 0x00C71A8C
    inline void SetRightJustifyWrap(float x) { reinterpret_cast<void(__cdecl*)(float)>(0x007194F0)(x); } // m_fRightJustifyWrap @ 0x00C71A90
    inline void SetDropColor(CRGBA c) { reinterpret_cast<void(__cdecl*)(CRGBA)>(0x00719510)(c); }
    inline void SetDropShadowPosition(int8_t pos) { reinterpret_cast<void(__cdecl*)(int8_t)>(0x00719570)(pos); }
    inline void SetEdge(int8_t edge) { reinterpret_cast<void(__cdecl*)(int8_t)>(0x00719590)(edge); }
    inline void SetProportional(bool on) { reinterpret_cast<void(__cdecl*)(bool)>(0x007195B0)(on); }
    inline void SetBackground(bool bg, bool includeWrap = false) { reinterpret_cast<void(__cdecl*)(bool, bool)>(0x007195C0)(bg, includeWrap); }
    inline void SetBackgroundColor(CRGBA c) { reinterpret_cast<void(__cdecl*)(CRGBA)>(0x007195E0)(c); }
    inline void SetJustify(bool on) { reinterpret_cast<void(__cdecl*)(bool)>(0x00719600)(on); }
    inline void SetOrientation(uint8_t align) { reinterpret_cast<void(__cdecl*)(uint8_t)>(0x00719610)(align); }
    inline void PrintString(float x, float y, const char* text) { reinterpret_cast<void(__cdecl*)(float, float, const char*)>(0x0071A700)(x, y, text); }
    // Verified at 0x0071A0E0: returns exact pixel width of string at current CFont scale/style!
    inline float GetStringWidth(const char* text, bool unk1 = true, bool spaces = false) {
        return reinterpret_cast<float(__cdecl*)(const char*, bool, bool)>(0x0071A0E0)(text, unk1, spaces);
    }
    // Verified at 0x0071A5E0 (called by CHud::DrawHelpText @ 0x0058B7E5): exact wrapped line count!
    inline int16_t GetNumberLines(float x, float y, const char* text) {
        return reinterpret_cast<int16_t(__cdecl*)(float, float, const char*)>(0x0071A5E0)(x, y, text);
    }
    inline void DrawRect(const CRect& r, const CRGBA& c) { reinterpret_cast<void(__cdecl*)(const CRect&, const CRGBA&)>(0x00727B60)(r, c); }
}

// Verified at 0x005817C9 (CVehicleModelInfo::ms_vehicleColourTable @ 0x00B4E480, 128 CRGBA entries)
inline CRGBA GetVehicleColorRGBA(uint8_t colorIdx) {
    const auto* tbl = reinterpret_cast<const CRGBA*>(0x00B4E480);
    CRGBA c = tbl[colorIdx & 0x7F];
    c.a = 255;
    return c;
}

// Verified at 0x00728350 (CSprite2d::Draw(CRect const&, CRGBA const&), __thiscall on RwTexture**):
inline void DrawSpriteTexture(void* rwTexture, const CRect& r, const CRGBA& c) {
    if (!rwTexture) return;
    void* spriteObj = rwTexture;
    using Fn = void(__thiscall*)(void*, const CRect&, const CRGBA&);
    reinterpret_cast<Fn>(0x00728350)(&spriteObj, r, c);
}

inline void RegisterCorona(
    uint32_t id, CEntity* attachTo,
    uint8_t r, uint8_t g, uint8_t b, uint8_t alpha,
    const CVector& pos, float radius, float farClip,
    uint8_t coronaType, uint8_t flareType,
    bool enableReflection, bool checkObstacles,
    int32_t unused, float angle, bool longDistance,
    float nearClip, uint8_t fadeState, float fadeSpeed,
    bool onlyFromBelow, bool reflectionDelay)
{
    using Fn = void(__cdecl*)(
        uint32_t, CEntity*, uint8_t, uint8_t, uint8_t, uint8_t,
        const CVector&, float, float, uint8_t, uint8_t,
        bool, bool, int32_t, float, bool, float, uint8_t, float, bool, bool);
    reinterpret_cast<Fn>(0x006FC580)(
        id, attachTo, r, g, b, alpha, pos, radius, farClip,
        coronaType, flareType, enableReflection, checkObstacles,
        unused, angle, longDistance, nearClip, fadeState, fadeSpeed,
        onlyFromBelow, reflectionDelay);
}

// ============================================================================
// CMenuSystem Structures (Fully Verified against gta_sa.exe 1.0 US @ 0x00580E00)
// ============================================================================
#pragma pack(push, 1)
struct tMenuPanel {
    uint8_t  m_anUsedCarColors[64];           // +0x000
    uint8_t  m_nType;                         // +0x040 (0=Standard, 1=Grid)
    char     m_aaacRowItems[4][12][10];       // +0x041
    uint8_t  _pad221[3];                      // +0x221
    int32_t  m_aadwNumberInRowTitle[4][12];   // +0x224
    int32_t  m_aadw2ndNumberInRowTitle[4][12];// +0x2E4
    char     m_aacColumnHeaders[4][10];       // +0x3A4
    char     m_acTitle[10];                   // +0x3CC
    uint8_t  m_anRowEnabled[12];              // +0x3D6
    uint8_t  m_anRowHighlighted[12];          // +0x3E2
    uint8_t  m_anColumnAlignment[4];          // +0x3EE
    int8_t   m_anColumnHeaderAlignment[4];    // +0x3F2
    uint8_t  m_nNumRows;                      // +0x3F6
    uint8_t  m_nNumColumns;                   // +0x3F7
    uint8_t  m_bInteractive;                  // +0x3F8
    uint8_t  m_bColumnBackground;             // +0x3F9
    uint8_t  _pad3FA[2];                      // +0x3FA
    float    m_fColumnWidth[4];               // +0x3FC
    float    m_fPosX;                         // +0x40C
    float    m_fPosY;                         // +0x410
    uint8_t  m_bBackground;                   // +0x414
    int8_t   m_nSelectedRow;                  // +0x415
    int8_t   m_nAcceptedRow;                  // +0x416
    uint8_t  _pad417;                         // +0x417
};
static_assert(sizeof(tMenuPanel) == 0x418, "tMenuPanel must be 0x418 bytes");
#pragma pack(pop)

inline tMenuPanel* GetMenuPanel(int idx) {
    if (idx < 0 || idx > 1) return nullptr;
    auto* inUse = reinterpret_cast<uint8_t*>(0x00BA82E0);
    if (!inUse[idx]) return nullptr;
    return reinterpret_cast<tMenuPanel**>(0x00BA82D8)[idx];
}

inline int8_t GetCurrentMenuInFocus() {
    return *reinterpret_cast<const int8_t*>(0x00BA82E2);
}

inline uint8_t GetNumMenusInUse() {
    return *reinterpret_cast<const uint8_t*>(0x00BA82E3);
}

// ============================================================================
// Story Phone Call Priority Detection & CPickups Pool (Verified SA 1.0 US)
// ============================================================================
inline const char* GetActiveHelpTextRaw() {
    if (*reinterpret_cast<const int32_t*>(0x00BAA474) == 0) return nullptr; // CHud::m_nHelpMessageState == 0
    const char* s = reinterpret_cast<const char*>(0x00BAA480); // CHud::m_pHelpMessageToPrint
    return (s && s[0]) ? s : nullptr;
}

// Detects whether GTA SA's story phone is ringing (help prompt CELL_1/CELL_2) or being held in a scripted call.
// Gives native story phone calls Priority 1 over Weapon Wheel (TAB) & Custom Phone (F6)!
inline bool IsStoryPhoneRinging() {
    const char* help = GetActiveHelpTextRaw();
    if (!help) return false;
    // Compare against localized GXT strings used by main.scm for incoming phone calls (works in RUS & ENG!)
    static constexpr const char* kPhoneKeys[] = {"CELL_1", "CELL_2", "PHON_1", "PHON_2", "MOB_1"};
    for (const char* key : kPhoneKeys) {
        const char* gxt = TextGet(key);
        if (gxt && gxt[0] && std::strlen(gxt) >= 8) {
            if (std::strncmp(help, gxt, std::min<size_t>(std::strlen(gxt), 16)) == 0) {
                return true;
            }
        }
    }
    // Also detect raw control-key token ~k~~PED_ANSWER_PHONE~ before/after substitution
    if (std::strstr(help, "PED_ANSWER_PHONE") != nullptr) return true;
    return false;
}

// Identifies vanilla top-left menu control hint boxes (e.g. "Press Space to select, F to exit")
// so GTA5UIKit hides redundant top-left hints while our unified bottom footer bar is visible!
inline bool IsVanillaMenuControlHint(const char* help) {
    if (!help || !help[0]) return false;
    if (std::strstr(help, "PED_SPRINT") != nullptr ||
        std::strstr(help, "VEHICLE_ENTER_EXIT") != nullptr ||
        std::strstr(help, "GO_FORWARD") != nullptr ||
        std::strstr(help, "GO_BACK") != nullptr) {
        return true;
    }
    // Match localized shop control GXT headers (SHOP_1..SHOP_9, GYM_, TATT_, BARB_, CLOT_, FOOD_)
    static constexpr const char* kHintKeys[] = {
        "SHOPNO", "CLO_HLP", "BARB_H", "TAT_HLP", "GYM_HLP", "FOOD_H", "AMM_HLP", "MOD_HLP",
        "FE_HLP1", "FE_HLP2", "FE_HLP3", "BUY_HLP", "S_HLP1", "S_HLP2"
    };
    for (const char* k : kHintKeys) {
        const char* gxt = TextGet(k);
        if (gxt && gxt[0] && std::strlen(gxt) >= 10 &&
            std::strncmp(help, gxt, std::min<size_t>(std::strlen(gxt), 18)) == 0) {
            return true;
        }
    }
    return false;
}

// Verified against CPickup::Update (0x00457410) & CPickup::GetRidOfObjects (0x00454CF0):
// CPickups::aPickUps @ 0x009788C0, 620 entries, stride 0x20 (32 bytes)
#pragma pack(push, 1)
struct CPickup {
    float    m_fRevenueValue;     // +0x00
    void*    m_pObject;           // +0x04
    uint32_t m_nAmmo;             // +0x08
    uint32_t m_nRegenerationTime; // +0x0C
    int16_t  m_nPosX;             // +0x10 (world X = m_nPosX * 0.125f)
    int16_t  m_nPosY;             // +0x12 (world Y = m_nPosY * 0.125f)
    int16_t  m_nPosZ;             // +0x14 (world Z = m_nPosZ * 0.125f)
    uint16_t m_nMoneyPerDay;      // +0x16
    int16_t  m_nModelIndex;       // +0x18
    int16_t  m_nReferenceIndex;   // +0x1A
    uint8_t  m_nPickupType;       // +0x1C (3=ON_STREET, 4=ONCE, 5=ONCE_TIMEOUT, 8=ONCE_FOR_MISSION, 15=ON_STREET_SLOW)
    uint8_t  m_nFlags;            // +0x1D (bit 0 = disabled/hidden)
    uint8_t  _pad1E[2];           // +0x1E

    CVector GetPos() const {
        return {m_nPosX * 0.125f, m_nPosY * 0.125f, m_nPosZ * 0.125f};
    }

    void GetRidOfObjects() {
        using Fn = void(__thiscall*)(CPickup*);
        reinterpret_cast<Fn>(0x00454CF0)(this);
    }

    // Verified at 0x004556C0 (CPickup::Remove): destroys object & blip, sets m_nPickupType = 0 & m_nFlags |= 1!
    void Remove() {
        using Fn = void(__thiscall*)(CPickup*);
        reinterpret_cast<Fn>(0x004556C0)(this);
    }
};
static_assert(sizeof(CPickup) == 0x20, "CPickup must be 32 bytes");
#pragma pack(pop)

// Verified in CPlayerInfo::Process (0x0056F909): CWorld::Players[playerId].m_nPlayerState (+0xD4)
// 0 = PLAYING, 1 = WASTED, 2 = BUSTED, 3 = FAILED_CRITICAL_MISSION, 4 = LEFT_GAME
inline uint8_t GetPlayerState(int32_t playerId = 0) {
    if (playerId < 0 || playerId > 1) playerId = 0;
    return *reinterpret_cast<const uint8_t*>(0x00B7CD98 + static_cast<uintptr_t>(playerId) * 0x190 + 0xD4);
}

// Verified against gta_sa.exe 1.0 US:
//   CGarages::aCarsInSafeHouse @ 0x0096ABD8 (20 hideouts x 4 CStoredCar slots, sizeof(CStoredCar) == 0x40)
//   CStoredCar::StoreCar @ 0x00449760, CGarages::Load @ 0x005D3270
//   CGarages::NumGarages @ 0x0096C024, CGarages::aGarages @ 0x0096C048 (stride 0xD8, m_nType @ +0x4C)
//   CGarages::GetSafeHouseIndexForType @ 0x004489F0
//   CGarage::IsPointInsideGarage(CVector) @ 0x00448740
#pragma pack(push, 1)
struct CStoredCar {
    float    x;                  // +0x00
    float    y;                  // +0x04
    float    z;                  // +0x08
    uint32_t m_nHandlingFlags;   // +0x0C
    uint16_t m_nFlags;           // +0x10
    uint16_t m_nModelIndex;      // +0x12 (0 = empty slot)
    int16_t  m_anUpgrades[15];   // +0x14
    uint8_t  m_nPrimaryColor;    // +0x32
    uint8_t  m_nSecondaryColor;  // +0x33
    uint8_t  m_nTertiaryColor;   // +0x34
    uint8_t  m_nQuaternaryColor; // +0x35
    uint8_t  m_nRadioStation;    // +0x36
    uint8_t  m_nVariationA;      // +0x37
    uint8_t  m_nVariationB;      // +0x38
    uint8_t  m_nBombType;        // +0x39
    uint8_t  m_nPaintjob;        // +0x3A
    uint8_t  m_nNitroCount;      // +0x3B
    int8_t   m_nAngleX;          // +0x3C
    int8_t   m_nAngleY;          // +0x3D
    int8_t   m_nAngleZ;          // +0x3E
    uint8_t  _pad3F;             // +0x3F
};
static_assert(sizeof(CStoredCar) == 0x40, "CStoredCar must be 64 bytes");
#pragma pack(pop)

constexpr int MAX_SAFEHOUSE_HIDEOUTS = 20;
constexpr int MAX_CARS_PER_SAFEHOUSE = 4;

inline CStoredCar* GetSafehouseStoredCars(int hideoutIndex) {
    if (hideoutIndex < 0 || hideoutIndex >= MAX_SAFEHOUSE_HIDEOUTS) return nullptr;
    return reinterpret_cast<CStoredCar*>(0x0096ABD8 + static_cast<uintptr_t>(hideoutIndex) * 0x100);
}

inline bool IsHideoutGarageType(uint8_t garageType) {
    if (garageType == 16) return true; // GARAGE_HIDEOUT_ONE (index 0)
    return reinterpret_cast<int32_t(__cdecl*)(int32_t)>(0x004489F0)(static_cast<int32_t>(garageType)) > 0;
}

inline int GetHideoutIndexForGarageIndex(int garageIdx) {
    int32_t count = *reinterpret_cast<const int32_t*>(0x0096C024);
    if (count > 50) count = 50;
    if (garageIdx < 0 || garageIdx >= count) return -1;
    const uint8_t* g = reinterpret_cast<const uint8_t*>(0x0096C048) + static_cast<size_t>(garageIdx) * 0xD8;
    uint8_t gType = g[0x4C];
    if (!IsHideoutGarageType(gType)) return -1;
    int32_t hid = reinterpret_cast<int32_t(__cdecl*)(int32_t)>(0x004489F0)(static_cast<int32_t>(gType));
    if (hid < 0 || hid >= MAX_SAFEHOUSE_HIDEOUTS) return -1;
    return static_cast<int>(hid);
}

inline int FindHideoutGarageIndexAt(const CVector& pos) {
    // Guard against x87 NaN comparison bug in CGarage::IsPointInsideGarage (0x00448740)!
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) return -1;
    if (std::fabs(pos.x) > 5000.0f || std::fabs(pos.y) > 5000.0f || std::fabs(pos.z) > 2000.0f) return -1;
    int32_t count = *reinterpret_cast<const int32_t*>(0x0096C024);
    if (count <= 0) return -1;
    if (count > 50) count = 50;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(0x0096C048);
    using IsInsideFn = bool(__thiscall*)(const uint8_t*, CVector);
    auto isInside = reinterpret_cast<IsInsideFn>(0x00448740);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* g = base + static_cast<size_t>(i) * 0xD8;
        if (!IsHideoutGarageType(g[0x4C])) continue;
        if (isInside(g, pos)) return static_cast<int>(i);
    }
    return -1;
}

constexpr int MAX_PICKUPS = 620;
inline CPickup* GetPickupsArray() {
    return reinterpret_cast<CPickup*>(0x009788C0);
}

// Verified at 0x0069DE90 & 0x0069E160 (called by CMenuSystem::DisplayStandardMenu @ 0x00581444)
inline void InsertNumberInString(const char* src, int32_t n1, int32_t n2, int32_t n3,
                                 int32_t n4, int32_t n5, int32_t n6, char* dst) {
    using Fn = void(__cdecl*)(const char*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, char*);
    reinterpret_cast<Fn>(0x0069DE90)(src, n1, n2, n3, n4, n5, n6, dst);
}

inline void InsertPlayerControlKeysInString(char* str) {
    reinterpret_cast<void(__cdecl*)(char*)>(0x0069E160)(str);
}

// ============================================================================
// CKeyGen, CStats, CWanted, CClothes, CShopping (Verified against gta_sa.exe!)
// ============================================================================
inline uint32_t GetUppercaseKey(const char* str) {
    if (!str) return 0;
    return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(0x0053CF30)(str);
}

namespace stats {
    constexpr uint16_t STAT_FAT                  = 21;
    constexpr uint16_t STAT_STAMINA              = 22;
    constexpr uint16_t STAT_MUSCLE               = 23;
    constexpr uint16_t STAT_MAX_HEALTH           = 24;
    constexpr uint16_t STAT_SEX_APPEAL           = 25;
    constexpr uint16_t STAT_RESPECT              = 64;

    // Weapon skill stats (0.0 .. 1000.0, verified via CWeaponInfo::FindWeaponStat @ 0x00743CD0)
    constexpr uint16_t STAT_WEAPON_PISTOL        = 69;
    constexpr uint16_t STAT_WEAPON_SILENCED      = 70;
    constexpr uint16_t STAT_WEAPON_DESERT_EAGLE  = 71;
    constexpr uint16_t STAT_WEAPON_SHOTGUN       = 72;
    constexpr uint16_t STAT_WEAPON_SAWNOFF       = 73;
    constexpr uint16_t STAT_WEAPON_SPAS12        = 74;
    constexpr uint16_t STAT_WEAPON_MICRO_UZI     = 75; // Shared with Tec-9 (weaponId 32)
    constexpr uint16_t STAT_WEAPON_MP5           = 76;
    constexpr uint16_t STAT_WEAPON_AK47          = 77;
    constexpr uint16_t STAT_WEAPON_M4            = 78;
    constexpr uint16_t STAT_WEAPON_SNIPER        = 79; // Country Rifle (33) & Sniper Rifle (34)

    // Vehicle skill stats (0 .. 1000, verified via CStats::GetStatValue @ 0x00558E40 & GetFatAndMuscleModifier @ 0x00559AF0)
    constexpr uint16_t STAT_DRIVING_SKILL        = 160;
    constexpr uint16_t STAT_FLYING_SKILL         = 223;
    constexpr uint16_t STAT_BIKE_SKILL           = 229;
    constexpr uint16_t STAT_CYCLING_SKILL        = 230;

    // Misc & Progression stats (verified directly against text/american.gxt STATxxx & gta_sa.exe!)
    constexpr uint16_t STAT_HEADSHOTS                = 130;
    constexpr uint16_t STAT_WANTED_STARS_EVADED      = 132;
    constexpr uint16_t STAT_TIMES_CHEATED            = 137;
    constexpr uint16_t STAT_UNIQUE_JUMPS_DONE        = 145;
    constexpr uint16_t STAT_MISSIONS_PASSED          = 147;
    constexpr uint16_t STAT_CRIMINALS_VIGILANTE      = 152;
    constexpr uint16_t STAT_FIRES_EXTINGUISHED       = 153;
    constexpr uint16_t STAT_TRUCK_MISSIONS_PASSED    = 161;
    constexpr uint16_t STAT_CALORIES                 = 165;
    constexpr uint16_t STAT_GIRLS_PIMPED             = 171;
    constexpr uint16_t STAT_HOUSES_BURGLED           = 191;
    constexpr uint16_t STAT_SNAPSHOTS_TAKEN          = 231;
    constexpr uint16_t STAT_TERRITORIES_TAKEN_OVER   = 234;
    constexpr uint16_t STAT_HORSESHOES_COLLECTED     = 241;
    constexpr uint16_t STAT_OYSTERS_COLLECTED        = 243;

    // Verified at 0x0049CEE6..0x0049CF14: CTagManager::ms_numTags @ 0x00A9AD70, ms_tagDesc @ 0x00A9A8C0 (stride 8, +4 alpha > 0xE4)
    inline int GetSprayedTagsCount() {
        int32_t numTags = *reinterpret_cast<const int32_t*>(0x00A9AD70);
        if (numTags <= 0 || numTags > 150) return 0;
        const uint8_t* desc = reinterpret_cast<const uint8_t*>(0x00A9A8C0);
        int sprayed = 0;
        for (int32_t i = 0; i < numTags; ++i) {
            if (desc[static_cast<size_t>(i) * 8 + 4] > 0xE4u) ++sprayed;
        }
        return sprayed;
    }

    // Verified at 0x00558E40 (GetStatValue), 0x0055A070 (SetStatValue) & 0x0055C180 (IncrementStat)
    inline float Get(uint16_t statId) {
        return reinterpret_cast<float(__cdecl*)(uint16_t)>(0x00558E40)(statId);
    }
    inline void Set(uint16_t statId, float value) {
        reinterpret_cast<void(__cdecl*)(uint16_t, float)>(0x0055A070)(statId, value);
    }
    inline void Increment(uint16_t statId, float delta) {
        if (delta <= 0.0f) return;
        reinterpret_cast<void(__cdecl*)(uint16_t, float)>(0x0055C180)(statId, delta);
    }
    inline float GetNormalized01(uint16_t statId) {
        return std::clamp(Get(statId) * 0.001f, 0.0f, 1.0f);
    }
} // namespace stats

namespace wanted {
    // Verified at 0x0056E230 (FindPlayerWanted), +0x2C (m_nWantedLevel), 0x00562470 (SetWantedLevel)
    inline uint8_t* GetPlayerWanted(int32_t playerId = 0) {
        return reinterpret_cast<uint8_t*(__cdecl*)(int32_t)>(0x0056E230)(playerId);
    }
    inline int32_t GetLevel(int32_t playerId = 0) {
        uint8_t* w = GetPlayerWanted(playerId);
        return w ? *reinterpret_cast<const int32_t*>(w + 0x2C) : 0;
    }
    inline void SetLevel(int32_t stars, int32_t playerId = 0) {
        uint8_t* w = GetPlayerWanted(playerId);
        if (!w) return;
        if (stars < 0) stars = 0;
        if (stars > 6) stars = 6;
        using Fn = void(__thiscall*)(void*, int32_t);
        reinterpret_cast<Fn>(0x00562470)(w, stars);
    }
    // Verified at 0x005625CA & 0x005625D6 (CWanted::ClearWantedLevelAndGoOnParole — Pay 'n' Spray!)
    inline uint32_t GetTimeOfParole(int32_t playerId = 0) {
        uint8_t* w = GetPlayerWanted(playerId);
        return w ? *reinterpret_cast<const uint32_t*>(w + 0x10) : 0;
    }
    inline int32_t GetStarsBeforeParole(int32_t playerId = 0) {
        uint8_t* w = GetPlayerWanted(playerId);
        return w ? *reinterpret_cast<const int32_t*>(w + 0x30) : 0;
    }
} // namespace wanted

namespace clothes {
    struct CPedClothesDesc {
        uint32_t m_anModelKeys[10];   // +0x00
        uint32_t m_anTextureKeys[18]; // +0x28
        float    m_fFatStat;          // +0x70
        float    m_fMuscleStat;       // +0x74
    };

    inline CPedClothesDesc* GetPlayerClothesDesc(const CPed* ped) {
        if (!IsPedValid(ped)) return nullptr;
        uint8_t* pdata = reinterpret_cast<uint8_t*>(ped->GetPlayerData());
        if (!pdata) return nullptr;
        return *reinterpret_cast<CPedClothesDesc**>(pdata + 0x04);
    }

    // Verified at 0x005A8080: void __thiscall CPedClothesDesc::SetTextureAndModel(const char* tex, const char* model, uint32_t bodyPart)
    inline void SetTextureAndModel(CPedClothesDesc* desc, const char* texName, const char* modelName, uint32_t bodyPart) {
        if (!desc || bodyPart >= 18) return;
        using Fn = void(__thiscall*)(void*, const char*, const char*, uint32_t);
        reinterpret_cast<Fn>(0x005A8080)(desc, texName, modelName, bodyPart);
    }

    // Verified at 0x005A82C0: void __cdecl CClothes::RebuildPlayer(CPlayerPed* player, bool bIgnoreFatAndMuscle)
    // Always guarded against SA-COOP F4 custom skins (modelId != 0 crashes CClothes::RebuildPlayer)!
    inline void RebuildPlayer(CPed* ped, bool ignoreFatAndMuscle = false) {
        if (!IsPedValid(ped)) return;
        if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) return;
        reinterpret_cast<void(__cdecl*)(CPed*, bool)>(0x005A82C0)(ped, ignoreFatAndMuscle);
    }
} // namespace clothes

namespace shopping {
    constexpr int MAX_SHOPPING_ITEMS = 0x230; // 560 verified at 0x0049BC5E

    // Verified at 0x0049BBE8
    inline const char* GetLoadedShopName() {
        return reinterpret_cast<const char*>(0x00A9A7D8);
    }

    // Verified at 0x0049BC54 (keys @ 0x00A97D90) & 0x0049BC68 (bought flags @ 0x00A972A0)
    inline int FindItemIndexByHash(uint32_t hash) {
        if (hash == 0) return -1;
        const uint32_t* keys = reinterpret_cast<const uint32_t*>(0x00A97D90);
        for (int i = 0; i < MAX_SHOPPING_ITEMS; ++i) {
            if (keys[i] == hash) return i;
        }
        return -1;
    }

    inline bool HasBoughtHash(uint32_t hash) {
        int idx = FindItemIndexByHash(hash);
        if (idx < 0) return false;
        return reinterpret_cast<const uint8_t*>(0x00A972A0)[idx] != 0;
    }

    inline void SetBoughtHash(uint32_t hash, bool bought) {
        int idx = FindItemIndexByHash(hash);
        if (idx < 0) return;
        reinterpret_cast<uint8_t*>(0x00A972A0)[idx] = bought ? 1 : 0;
    }
} // namespace shopping

// ============================================================================
// Shared CRadar Trace Pool & CGarages Table Helpers (Verified SA 1.0 US)
// Replaces raw 0x00BA86F0 / 0x0096C024 scans duplicated across EarlyUnlock & Dealership!
// ============================================================================
namespace radar {
    constexpr int    MAX_RADAR_TRACES   = 175;
    constexpr size_t RADAR_TRACE_STRIDE = 0x28;

    inline const uint8_t* GetTraceTable() {
        return reinterpret_cast<const uint8_t*>(0x00BA86F0); // CRadar::ms_RadarTrace
    }

    inline int CountUsedBlips() {
        const uint8_t* base = GetTraceTable();
        int used = 0;
        for (int i = 0; i < MAX_RADAR_TRACES; ++i) {
            if ((base[i * RADAR_TRACE_STRIDE + 0x25] & 0x02) != 0) ++used; // m_bInUse
        }
        return used;
    }

    // Checks if any active radar blip (optionally matching spriteId > 0) already exists near (x, y)
    inline bool HasBlipNear(float x, float y, float maxDist, uint8_t spriteId = 0) {
        const uint8_t* base = GetTraceTable();
        const float maxDistSq = maxDist * maxDist;
        for (int i = 0; i < MAX_RADAR_TRACES; ++i) {
            const uint8_t* t = base + static_cast<size_t>(i) * RADAR_TRACE_STRIDE;
            if ((t[0x25] & 0x02) == 0) continue; // m_bInUse
            if (spriteId != 0 && t[0x24] != spriteId) continue; // m_nBlipSprite
            float bx = *reinterpret_cast<const float*>(t + 0x08);
            float by = *reinterpret_cast<const float*>(t + 0x0C);
            float dx = bx - x, dy = by - y;
            if (dx * dx + dy * dy <= maxDistSq) return true;
        }
        return false;
    }
} // namespace radar

namespace garages {
    inline int32_t GetCount() {
        int32_t n = *reinterpret_cast<const int32_t*>(0x0096C024); // CGarages::NumGarages
        return std::clamp<int32_t>(n, 0, 50);
    }

    inline uint8_t* GetGarageAt(int32_t idx) {
        if (idx < 0 || idx >= GetCount()) return nullptr;
        return reinterpret_cast<uint8_t*>(0x0096C048) + static_cast<size_t>(idx) * 0xD8;
    }

    // Verified at 0x0044AEDE / 0x0044C311: type 5 = GARAGE_RESPRAY (Pay 'n' Spray), +0x4D = state (0 = closed/idle)
    inline bool IsAnyResprayGarageBusy() {
        const int32_t n = GetCount();
        for (int32_t i = 0; i < n; ++i) {
            const uint8_t* g = GetGarageAt(i);
            if (g && g[0x4C] == 5 && g[0x4D] != 0) return true;
        }
        return false;
    }
} // namespace garages

// ============================================================================
// Native SCM Opcode Runner (Calls CRunningScript::ProcessOneCommand 0x00469EB0)
// ============================================================================
class ScriptCmd {
public:
    static constexpr size_t SCRIPT_STRUCT_SIZE = 0xE0;

    ScriptCmd(uint16_t opcode) {
        std::memset(m_buf, 0, sizeof(m_buf));
        m_buf[0] = static_cast<uint8_t>(opcode & 0xFF);
        m_buf[1] = static_cast<uint8_t>((opcode >> 8) & 0xFF);
        m_pos = 2;
    }

    ScriptCmd& Int(int32_t v) {
        if (m_pos + 5 > sizeof(m_buf)) { m_overflow = true; return *this; }
        m_buf[m_pos++] = 1;
        std::memcpy(m_buf + m_pos, &v, 4);
        m_pos += 4;
        return *this;
    }

    ScriptCmd& Float(float v) {
        if (m_pos + 5 > sizeof(m_buf)) { m_overflow = true; return *this; }
        m_buf[m_pos++] = 6;
        std::memcpy(m_buf + m_pos, &v, 4);
        m_pos += 4;
        return *this;
    }

    ScriptCmd& Str8(const char* s) {
        if (m_pos + 9 > sizeof(m_buf)) { m_overflow = true; return *this; }
        m_buf[m_pos++] = 9;
        char tmp[8] = {};
        if (s) {
            size_t len = std::strlen(s);
            if (len > 8) len = 8;
            std::memcpy(tmp, s, len);
        }
        std::memcpy(m_buf + m_pos, tmp, 8);
        m_pos += 8;
        return *this;
    }

    ScriptCmd& VarStr(const char* s) {
        size_t len = s ? std::strlen(s) : 0;
        if (len > 120) len = 120;
        if (m_pos + 2 + len > sizeof(m_buf)) { m_overflow = true; return *this; }
        m_buf[m_pos++] = 14;
        m_buf[m_pos++] = static_cast<uint8_t>(len);
        if (len > 0) std::memcpy(m_buf + m_pos, s, len);
        m_pos += len;
        return *this;
    }

    ScriptCmd& Out(int32_t& outVar) {
        if (m_numOutputs >= 16 || m_pos + 3 > sizeof(m_buf)) { m_overflow = true; return *this; }
        uint16_t slot = m_numOutputs;
        m_outInt[slot] = &outVar;
        m_outIsFloat[slot] = false;
        m_buf[m_pos++] = 3;
        std::memcpy(m_buf + m_pos, &slot, 2);
        m_pos += 2;
        ++m_numOutputs;
        return *this;
    }

    ScriptCmd& Out(float& outVar) {
        if (m_numOutputs >= 16 || m_pos + 3 > sizeof(m_buf)) { m_overflow = true; return *this; }
        uint16_t slot = m_numOutputs;
        m_outFloat[slot] = &outVar;
        m_outIsFloat[slot] = true;
        m_buf[m_pos++] = 3;
        std::memcpy(m_buf + m_pos, &slot, 2);
        m_pos += 2;
        ++m_numOutputs;
        return *this;
    }

    bool Run() {
        if (m_overflow) return false;
        alignas(16) uint8_t script[SCRIPT_STRUCT_SIZE];
        std::memset(script, 0, sizeof(script));
        *reinterpret_cast<uint8_t**>(script + 0x10) = m_buf;
        *reinterpret_cast<uint8_t**>(script + 0x14) = m_buf;
        script[0xC4] = 1; // m_bIsActive

        using ProcessOneFn = int8_t(__thiscall*)(void*);
        reinterpret_cast<ProcessOneFn>(0x00469EB0)(script);

        int32_t* lvars = reinterpret_cast<int32_t*>(script + 0x3C);
        for (uint16_t i = 0; i < m_numOutputs; ++i) {
            if (m_outIsFloat[i]) {
                float fv = 0.0f;
                std::memcpy(&fv, &lvars[i], 4);
                *m_outFloat[i] = fv;
            } else {
                *m_outInt[i] = lvars[i];
            }
        }
        return script[0xC5] != 0;
    }

private:
    uint8_t  m_buf[256] = {};
    size_t   m_pos = 0;
    uint16_t m_numOutputs = 0;
    bool     m_overflow = false;
    int32_t* m_outInt[16] = {};
    float*   m_outFloat[16] = {};
    bool     m_outIsFloat[16] = {};
};

} // namespace sa
