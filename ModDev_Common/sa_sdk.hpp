#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <windows.h>
#include <shlobj.h>

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
// Version Check & Common Save Directory Helper
// ============================================================================
inline bool IsSA10US() {
    return *reinterpret_cast<const uint32_t*>(0x0082457C) == 0x000094BF;
}

inline bool IsCoopModLoaded() {
    return (GetModuleHandleA("sa-coop.asi") != nullptr) ||
           (GetModuleHandleA("SA-COOP.asi") != nullptr) ||
           (GetModuleHandleA("coop.asi")    != nullptr) ||
           (GetModuleHandleA("sa_coop.asi") != nullptr);
}

inline bool IsGameWindowFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

inline std::string GetUserFilesSubDir(const char* subFolder, const std::string& fallbackDir) {
    char docs[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)) && docs[0] != '\0') {
        std::string userFiles = std::string(docs) + "\\GTA San Andreas User Files";
        CreateDirectoryA(userFiles.c_str(), nullptr);
        std::string dir = userFiles + "\\" + subFolder;
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    std::string dir = fallbackDir + "\\" + subFolder;
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// Parse slot number 1..8 from GTA SA save filename buffer (e.g., "...\GTASAsf3.b")
inline int ParseSaveSlotFromPath(const char* path) {
    if (!path) return 0;
    const char* p = std::strstr(path, "GTASAsf");
    if (!p) p = std::strstr(path, "gtasasf");
    if (p && p[7] >= '1' && p[7] <= '8') {
        return p[7] - '0';
    }
    return 0;
}

// ============================================================================
// Global State Accessors (SA 1.0 US)
// ============================================================================
inline int32_t GetGameState() {
    return *reinterpret_cast<const int32_t*>(0x00C8D4C0);
}

inline bool IsPauseMenuActive() {
    return *reinterpret_cast<const uint8_t*>(0x00BA6748 + 0x5C) != 0;
}

inline bool IsCutsceneRunning() {
    return *reinterpret_cast<const uint8_t*>(0x00B5F851) != 0;
}

inline bool IsWidescreenOn() {
    return *reinterpret_cast<const uint8_t*>(0x00BA6748 + 0x58) != 0;
}

inline uint32_t GetTimeMs() {
    return *reinterpret_cast<const uint32_t*>(0x00B7CB84); // CTimer::m_snTimeInMilliseconds
}

inline float GetTimeStep() {
    return *reinterpret_cast<const float*>(0x00B7CB5C); // CTimer::ms_fTimeStep (50fps normalized)
}

inline float GetTimeScale() {
    return *reinterpret_cast<const float*>(0x00B7CB64); // CTimer::ms_fTimeScale
}

inline void SetTimeScale(float scale) {
    *reinterpret_cast<float*>(0x00B7CB64) = scale;
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

inline bool IsOnMission() {
    const uint8_t* scriptSpace = reinterpret_cast<const uint8_t*>(0x00A49960);
    return (*reinterpret_cast<const int32_t*>(scriptSpace + 0x4 * 4)) != 0;
}

// Local Player Money (CWorld::Players[0].m_nMoney — always local player, even in SA-COOP!)
inline int32_t GetPlayerMoney() {
    return *reinterpret_cast<const int32_t*>(0x00B7CE50);
}

inline void SetPlayerMoney(int32_t amount) {
    if (amount < 0) amount = 0;
    *reinterpret_cast<int32_t*>(0x00B7CE50) = amount;
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

// Suppresses firing, aiming, camera rotation, weapon cycling & TAB stats while Weapon Wheel is held
inline void ClearPadWheelInputs() {
    uint8_t* pad = GetPad0();
    // NewState starts at +0x00 (CControllerState, 0x30 bytes):
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
    ms->X = 0.0f;
    ms->Y = 0.0f;
    ms->Z = 0.0f;
    ms->lmb = 0;
    ms->rmb = 0;
    ms->wheelUp = 0;
    ms->wheelDown = 0;
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

inline CVector GetCamPos() {
    // CCamera::m_pRwCamera / CCamera::GetPosition (TheCamera + 0x974 = m_mCameraMatrix.pos)
    return *reinterpret_cast<const CVector*>(GetTheCamera() + 0x974);
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
};

struct CPed : CEntity {
    uint8_t* Raw() { return reinterpret_cast<uint8_t*>(this); }
    const uint8_t* Raw() const { return reinterpret_cast<const uint8_t*>(this); }

    float GetHealth() const { return *reinterpret_cast<const float*>(Raw() + 0x540); }
    float GetArmor() const  { return *reinterpret_cast<const float*>(Raw() + 0x548); }
    void  SetArmor(float v) { *reinterpret_cast<float*>(Raw() + 0x548) = v; }
    void* GetVehiclePtr() const { return *reinterpret_cast<void* const*>(Raw() + 0x58C); }
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

    CPed*   GetDriver() const { return *reinterpret_cast<CPed* const*>(Raw() + 0x460); }
    float&  GasPedal() { return *reinterpret_cast<float*>(Raw() + 0x49C); }
    float&  BrakePedal() { return *reinterpret_cast<float*>(Raw() + 0x4A0); }
    uint8_t GetCurrentGear() const { return *reinterpret_cast<const uint8_t*>(Raw() + 0x4B4); }
    float&  Health() { return *reinterpret_cast<float*>(Raw() + 0x4C0); }
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
    if (modelId < 0 || modelId >= 20000) return nullptr;
    return reinterpret_cast<void**>(0x00A9B0C8)[modelId];
}

inline std::string GetVehicleDisplayNameGameEncoded(int32_t modelId) {
    void* mi = GetModelInfo(modelId);
    if (!mi) return "Vehicle";
    char gxtKey[12] = {};
    std::memcpy(gxtKey, reinterpret_cast<const uint8_t*>(mi) + 0x32, 8);
    gxtKey[8] = '\0';
    if (gxtKey[0] != '\0') {
        const char* localized = TextGet(gxtKey);
        if (localized && localized[0] != '\0') return std::string(localized);
        return std::string(gxtKey);
    }
    return "Vehicle";
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

// RenderState enum constants (RenderWare 3.6)
constexpr int32_t STATE_TEXTURERASTER      = 1;
constexpr int32_t STATE_ZTESTENABLE        = 6;
constexpr int32_t STATE_ZWRITEENABLE       = 8;
constexpr int32_t STATE_SHADEMODE          = 9;  // 1=FLAT, 2=GOURAUD
constexpr int32_t STATE_SRCBLEND           = 10; // 5=SRCALPHA
constexpr int32_t STATE_DESTBLEND          = 11; // 2=ONE, 6=INVSRCALPHA
constexpr int32_t STATE_VERTEXALPHAENABLE  = 12;
constexpr int32_t STATE_CULLMODE           = 20; // 1=NONE, 2=BACK

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

inline void* GetWeaponIconRaster(int32_t modelId) {
    void* rwTex = GetWeaponIconTexture(modelId);
    return rwTex ? *reinterpret_cast<void**>(rwTex) : nullptr;
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
    inline void SetWrapx(float x) { reinterpret_cast<void(__cdecl*)(float)>(0x007194D0)(x); }
    inline void SetRightJustifyWrap(float x) { *reinterpret_cast<float*>(0x00C71A8C) = x; }
    inline void SetCentreSize(float s) { reinterpret_cast<void(__cdecl*)(float)>(0x007194F0)(s); }
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
    inline void DrawRect(const CRect& r, const CRGBA& c) { reinterpret_cast<void(__cdecl*)(const CRect&, const CRGBA&)>(0x00727B60)(r, c); }
}

// Verified at 0x00728350 (CSprite2d::Draw(CRect const&, CRGBA const&), __thiscall on RwTexture**):
inline void DrawSpriteTexture(void* rwTexture, const CRect& r, const CRGBA& c) {
    if (!rwTexture) return;
    void* spriteObj = rwTexture;
    using Fn = void(__thiscall*)(void*, const CRect&, const CRGBA&);
    reinterpret_cast<Fn>(0x00728350)(&spriteObj, r, c);
}

// Verified at 0x005E6580: CPed::GetWeaponSkill() -> 0=Poor, 1=Std, 2=Pro(Hitman), 3=Cop
inline int8_t GetPedWeaponSkill(const CPed* ped) {
    if (!IsPedValid(ped)) return 1;
    using Fn = int8_t(__thiscall*)(const CPed*);
    return reinterpret_cast<Fn>(0x005E6580)(ped);
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
// CMenuSystem Structures (Verified against gta_sa.exe 1.0 US)
// ============================================================================
#pragma pack(push, 1)
struct tMenuPanel {
    uint8_t  m_anUsedCarColors[64];
    uint8_t  m_nType;
    char     m_aaacRowItems[4][12][10];
    uint8_t  _unk221[0x1AB];
    char     m_acTitle[10];
    uint8_t  m_anRowEnabled[12];
    uint8_t  _unk3E2[0x14];
    uint8_t  m_nNumRows;
    uint8_t  m_nNumColumns;
    uint8_t  m_bInteractive;
    uint8_t  m_bColumnBackground;
    uint8_t  _pad3FA[2];
    float    m_fColumnWidth[4];
    float    m_fPosX;
    float    m_fPosY;
    uint8_t  m_bBackground;
    int8_t   m_nSelectedRow;
    int8_t   m_nAcceptedRow;
    uint8_t  _pad417;
};
static_assert(sizeof(tMenuPanel) == 0x418, "tMenuPanel must be 0x418 bytes");
#pragma pack(pop)

inline tMenuPanel* GetMenuPanel(int idx) {
    if (idx < 0 || idx > 1) return nullptr;
    auto* inUse = reinterpret_cast<uint8_t*>(0x00BA82E0);
    if (!inUse[idx]) return nullptr;
    return reinterpret_cast<tMenuPanel**>(0x00BA82D8)[idx];
}

inline uint8_t GetNumMenusInUse() {
    return *reinterpret_cast<const uint8_t*>(0x00BA82E3);
}

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
        m_buf[m_pos++] = 1;
        std::memcpy(m_buf + m_pos, &v, 4);
        m_pos += 4;
        return *this;
    }

    ScriptCmd& Float(float v) {
        m_buf[m_pos++] = 6;
        std::memcpy(m_buf + m_pos, &v, 4);
        m_pos += 4;
        return *this;
    }

    ScriptCmd& Str8(const char* s) {
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
        m_buf[m_pos++] = 14;
        m_buf[m_pos++] = static_cast<uint8_t>(len);
        if (len > 0) std::memcpy(m_buf + m_pos, s, len);
        m_pos += len;
        return *this;
    }

    ScriptCmd& Out(int32_t& outVar) {
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
    int32_t* m_outInt[16] = {};
    float*   m_outFloat[16] = {};
    bool     m_outIsFloat[16] = {};
};

} // namespace sa
