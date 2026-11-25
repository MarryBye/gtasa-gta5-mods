#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "bass_audio.hpp"
#include "config.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "sa_sdk.hpp"
#include "weapon_data.hpp"
#include "weapon_storage.hpp"

namespace waudio {

struct WeaponAudioState {
    bool initialized = false;
    bool attempted   = false;

    std::vector<uint8_t> wavSuppPistol;
    std::vector<uint8_t> wavSuppSmg;
    std::vector<uint8_t> wavSuppRifle;
    std::vector<uint8_t> wavSuppShotgun;
    std::vector<uint8_t> wavHeavyCrack;
    std::vector<uint8_t> wavAttachClick;

    bass::HSAMPLE smpSuppPistol  = 0;
    bass::HSAMPLE smpSuppSmg     = 0;
    bass::HSAMPLE smpSuppRifle   = 0;
    bass::HSAMPLE smpSuppShotgun = 0;
    bass::HSAMPLE smpHeavyCrack  = 0;
    bass::HSAMPLE smpAttachClick = 0;
};

inline WeaponAudioState& State() {
    static WeaponAudioState st;
    return st;
}

// Synthesize crisp, realistic tactical weapon audio in RAM via shared ModDev_Common/bass_audio.hpp
inline void SynthesizeWeaponSounds() {
    auto& st = State();
    const uint32_t sr = 44100;
    constexpr float PI2 = 6.28318530718f;

    auto makeSuppressed = [&](float durSec, float thumpHz, float boltHz, float lpAlpha, float decay, uint32_t seed) {
        uint32_t count = static_cast<uint32_t>(sr * durSec);
        std::vector<int16_t> pcm(count);
        uint32_t rng = seed;
        float lp1 = 0.0f, lp2 = 0.0f, hpPrev = 0.0f;

        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            rng = rng * 1664525u + 1013904223u;
            float white = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;

            // Band-shaped pressurized gas puff ("pfft")
            float hp = white - hpPrev * 0.80f;
            hpPrev = white;
            lp1 += lpAlpha * (hp - lp1);
            lp2 += lpAlpha * (lp1 - lp2);

            float gasEnv  = std::exp(-t * decay);
            if (t < 0.0025f) gasEnv *= (t / 0.0025f);

            // Low muted gas baffle thump
            float thumpEnv = std::exp(-t * (decay * 1.4f));
            float thump    = std::sin(PI2 * (thumpHz * std::exp(-t * 14.0f) + 55.0f) * t) * thumpEnv;

            // Crisp metallic bolt/slide clack ~16ms after gas release
            float boltT   = t - 0.016f;
            float bolt    = 0.0f;
            if (boltT > 0.0f && boltT < 0.045f) {
                float bEnv = std::exp(-boltT * 115.0f);
                bolt = (0.45f * std::sin(PI2 * boltHz * boltT) + 0.30f * white) * bEnv;
            }

            float s = std::tanh((lp2 * 1.55f * gasEnv + thump * 0.58f + bolt * 0.35f) * 1.6f);
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 27000.0f), -32000, 32000));
        }
        return bass::BuildWav16Mono(pcm, sr);
    };

    // 4 distinct suppressed signatures tailored by weapon class!
    st.wavSuppPistol  = makeSuppressed(0.18f, 210.0f, 1650.0f, 0.24f, 28.0f, 0x11223344u);
    st.wavSuppSmg     = makeSuppressed(0.14f, 240.0f, 1900.0f, 0.28f, 36.0f, 0x22334455u);
    st.wavSuppRifle   = makeSuppressed(0.22f, 165.0f, 1400.0f, 0.20f, 22.0f, 0x33445566u);
    st.wavSuppShotgun = makeSuppressed(0.28f, 110.0f,  980.0f, 0.14f, 15.0f, 0x44556677u);

    // Punchy muzzle-brake / heavy-barrel crack layered over vanilla gunshot
    {
        const uint32_t count = static_cast<uint32_t>(sr * 0.20f);
        std::vector<int16_t> pcm(count);
        uint32_t rng = 0x778899AAu;
        float lp = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            rng = rng * 1664525u + 1013904223u;
            float n = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
            lp += 0.32f * (n - lp);

            float punchEnv = std::exp(-t * 32.0f);
            float crackEnv = std::exp(-t * 58.0f);
            float subBoom  = std::sin(PI2 * (120.0f * std::exp(-t * 16.0f) + 42.0f) * t) * punchEnv;
            float s = std::tanh((subBoom * 0.85f + lp * 1.45f * crackEnv) * 1.8f);
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 24000.0f), -32000, 32000));
        }
        st.wavHeavyCrack = bass::BuildWav16Mono(pcm, sr);
    }

    // Crisp tactical mechanical rail-lock click when installing an attachment in Ammu-Nation
    {
        const uint32_t count = static_cast<uint32_t>(sr * 0.11f);
        std::vector<int16_t> pcm(count);
        uint32_t rng = 0xCAFEBABEu;
        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            rng = rng * 1664525u + 1013904223u;
            float n = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
            float c1 = std::exp(-t * 140.0f) * (std::sin(PI2 * 1800.0f * t) + 0.4f * n);
            float t2 = t - 0.038f;
            float c2 = (t2 > 0.0f) ? std::exp(-t2 * 160.0f) * (std::sin(PI2 * 2400.0f * t2) + 0.35f * n) : 0.0f;
            float s = std::clamp((c1 * 0.65f + c2 * 0.85f), -1.0f, 1.0f);
            pcm[i] = static_cast<int16_t>(s * 22000.0f);
        }
        st.wavAttachClick = bass::BuildWav16Mono(pcm, sr);
    }
}

inline void EnsureInit() {
    auto& st = State();
    if (st.attempted) return;
    st.attempted = true;
    if (!bass::EnsureLoaded()) return;

    SynthesizeWeaponSounds();
    std::string sndDir = wcfg::Get().modDir + "\\sounds\\";
    st.smpSuppPistol  = bass::LoadSample(sndDir + "suppressed_pistol.wav",  st.wavSuppPistol,  8);
    st.smpSuppSmg     = bass::LoadSample(sndDir + "suppressed_smg.wav",     st.wavSuppSmg,     12);
    st.smpSuppRifle   = bass::LoadSample(sndDir + "suppressed_rifle.wav",   st.wavSuppRifle,   12);
    st.smpSuppShotgun = bass::LoadSample(sndDir + "suppressed_shotgun.wav", st.wavSuppShotgun, 6);
    st.smpHeavyCrack  = bass::LoadSample(sndDir + "heavy_crack.wav",        st.wavHeavyCrack,  10);
    st.smpAttachClick = bass::LoadSample(sndDir + "attach_click.wav",       st.wavAttachClick, 4);

    st.initialized = true;
    modlog::Info("[WAudio] Weapon BASS audio ready.");
}

inline void PlayAttachClick() {
    if (!wcfg::Get().enableWeaponAudio) return;
    EnsureInit();
    if (!State().initialized) return;
    bass::PlaySample(State().smpAttachClick, 0.45f * wcfg::Get().audioVolume, 44100.0f);
}

inline void PlaySuppressedShot(uint32_t weaponId) {
    if (!wcfg::Get().enableWeaponAudio) return;
    EnsureInit();
    auto& st = State();
    if (!st.initialized) return;

    const auto* def = wdata::FindDef(weaponId);
    bass::HSAMPLE smp = st.smpSuppRifle;
    float baseFreq = 44100.0f;
    if (def) {
        switch (def->audioClass) {
            case wdata::AudioClass::Pistol:  smp = st.smpSuppPistol;  baseFreq = (weaponId == 24) ? 37500.0f : 44100.0f; break;
            case wdata::AudioClass::SMG:     smp = st.smpSuppSmg;     baseFreq = 45000.0f; break;
            case wdata::AudioClass::Rifle:   smp = st.smpSuppRifle;   baseFreq = (weaponId == 30) ? 41000.0f : 44500.0f; break;
            case wdata::AudioClass::Shotgun: smp = st.smpSuppShotgun; baseFreq = 42000.0f; break;
            case wdata::AudioClass::Sniper:  smp = st.smpSuppRifle;   baseFreq = 35500.0f; break;
            default: break;
        }
    }
    // Slight natural pitch variation per shot (+/- 3%)
    static uint32_t jitter = 0x98765432u;
    jitter = jitter * 1664525u + 1013904223u;
    float var = 0.97f + (static_cast<float>((jitter >> 16) & 0xFF) / 255.0f) * 0.06f;
    bass::PlaySample(smp, 0.62f * wcfg::Get().audioVolume, baseFreq * var);
}

inline void PlayHeavyAccent(uint32_t weaponId, bool compensator, bool heavyBarrel) {
    if (!wcfg::Get().enableWeaponAudio) return;
    EnsureInit();
    auto& st = State();
    if (!st.initialized) return;

    float vol = (compensator && heavyBarrel) ? 0.42f : 0.30f;
    float freq = (weaponId == 25 || weaponId == 26 || weaponId == 27 || weaponId == 34) ? 36000.0f : 43000.0f;
    bass::PlaySample(st.smpHeavyCrack, vol * wcfg::Get().audioVolume, freq);
}

// ============================================================================
// Hook on BOTH verified callers of CAEWeaponAudioEntity::WeaponFire (0x00504F80):
//   0x004DFAC6 and 0x004E6A3D
// Signature (verified from 0x00504F80):
//   void __thiscall WeaponFire(void* audioEnt, uint32_t weaponType, sa::CEntity* entity, int32_t audioEventId)
// ============================================================================
using FnWeaponFire = void(__thiscall*)(void*, uint32_t, sa::CEntity*, int32_t);
inline FnWeaponFire g_origWeaponFire1 = nullptr;
inline FnWeaponFire g_origWeaponFire2 = nullptr;

inline void HandleWeaponFire(FnWeaponFire origFn, void* self, uint32_t weaponType, sa::CEntity* entity, int32_t eventId) {
    if (wcfg::Get().enableWeaponAudio && entity && entity == static_cast<sa::CEntity*>(sa::FindPlayerPed(0))) {
        if (const auto* loadout = wstore::Store().Find(weaponType)) {
            if (loadout->muzzle == 1 && weaponType != 23) {
                // Suppressed! Replace loud gunshot with crisp suppressed audio!
                PlaySuppressedShot(weaponType);
                return;
            }
            if (loadout->muzzle == 2 || loadout->barrel == 1) {
                // Compensator / Heavy Barrel: play vanilla + punchy heavy accent!
                if (origFn) origFn(self, weaponType, entity, eventId);
                PlayHeavyAccent(weaponType, loadout->muzzle == 2, loadout->barrel == 1);
                return;
            }
        }
    }
    if (origFn) origFn(self, weaponType, entity, eventId);
}

inline void __fastcall HookedWeaponFire1(void* self, void* /*edx*/, uint32_t weaponType, sa::CEntity* entity, int32_t eventId) {
    HandleWeaponFire(g_origWeaponFire1, self, weaponType, entity, eventId);
}

inline void __fastcall HookedWeaponFire2(void* self, void* /*edx*/, uint32_t weaponType, sa::CEntity* entity, int32_t eventId) {
    HandleWeaponFire(g_origWeaponFire2, self, weaponType, entity, eventId);
}

inline void InstallHooks() {
    g_origWeaponFire1 = reinterpret_cast<FnWeaponFire>(
        mem::HookCall(0x004DFAC6, reinterpret_cast<uintptr_t>(&HookedWeaponFire1)));
    g_origWeaponFire2 = reinterpret_cast<FnWeaponFire>(
        mem::HookCall(0x004E6A3D, reinterpret_cast<uintptr_t>(&HookedWeaponFire2)));
    modlog::Info("[WAudio] Hooked WeaponFire callers (0x004DFAC6 -> 0x%08X, 0x004E6A3D -> 0x%08X).",
                 reinterpret_cast<uintptr_t>(g_origWeaponFire1),
                 reinterpret_cast<uintptr_t>(g_origWeaponFire2));
}

} // namespace waudio
