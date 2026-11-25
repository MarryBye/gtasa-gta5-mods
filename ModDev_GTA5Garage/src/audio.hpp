#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "bass_audio.hpp"
#include "config.hpp"
#include "log.hpp"

namespace audio {

struct VehicleAudioState {
    bool initialized = false;
    bool attempted   = false;

    std::vector<uint8_t> wavEngine[4];
    std::vector<uint8_t> wavSpool;
    std::vector<uint8_t> wavBov;
    std::vector<uint8_t> wavExhPop;
    std::vector<uint8_t> wavHorns[4];

    bass::HSTREAM engineStreams[4] = {};
    int           activeEngineLv   = 0;
    bass::HSTREAM spoolStream      = 0;
    bass::HSAMPLE bovSample        = 0;
    bass::HSAMPLE exhPopSample     = 0;
    bass::HSTREAM hornStreams[4]   = {};
    int           activeHorn       = 0;
};

inline VehicleAudioState& State() {
    static VehicleAudioState s;
    return s;
}

inline void SynthesizeSounds() {
    auto& st = State();
    const uint32_t sr = 44100;
    constexpr float PI2 = 6.28318530718f;

    struct EngProfile {
        float baseHz;
        float h2, h3, h4, h5;
        float drive;
        float lpAlpha;
        float pulseHz;
        float pulseDepth;
    };
    const EngProfile profiles[4] = {
        {56.0f, 0.55f, 0.25f, 0.10f, 0.04f, 1.15f, 0.065f, 14.0f, 0.12f},
        {62.0f, 0.65f, 0.38f, 0.18f, 0.08f, 1.45f, 0.095f, 16.0f, 0.18f},
        {68.0f, 0.72f, 0.48f, 0.28f, 0.14f, 1.85f, 0.135f, 18.0f, 0.24f},
        {74.0f, 0.78f, 0.58f, 0.38f, 0.22f, 2.35f, 0.180f, 22.0f, 0.30f},
    };

    for (int lv = 0; lv < 4; ++lv) {
        const auto& pr = profiles[lv];
        std::vector<int16_t> pcm(sr);
        float lp1 = 0.0f, lp2 = 0.0f, nLp = 0.0f;
        uint32_t rng = 0x9E3779B9u + static_cast<uint32_t>(lv) * 7919u;

        for (uint32_t pass = 0; pass < 2; ++pass) {
            for (uint32_t i = 0; i < sr; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(sr);
                rng = rng * 1664525u + 1013904223u;
                float n = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
                nLp = nLp * 0.88f + n * 0.12f;

                float f = pr.baseHz;
                float wave = std::sin(PI2 * f * t)
                           + pr.h2 * std::sin(PI2 * f * 2.0f * t)
                           + pr.h3 * std::sin(PI2 * f * 3.0f * t)
                           + pr.h4 * std::sin(PI2 * f * 4.0f * t)
                           + pr.h5 * std::sin(PI2 * f * 5.0f * t)
                           + 0.30f * std::sin(PI2 * (f * 0.5f) * t)
                           + 0.18f * nLp;

                float pulse = 1.0f - pr.pulseDepth * (0.5f - 0.5f * std::sin(PI2 * pr.pulseHz * t));
                float sat = std::tanh(wave * pr.drive) * pulse;
                lp1 += pr.lpAlpha * (sat - lp1);
                lp2 += pr.lpAlpha * (lp1 - lp2);

                if (pass == 1) {
                    pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(lp2 * 22000.0f), -32000, 32000));
                }
            }
        }
        st.wavEngine[lv] = bass::BuildWav16Mono(pcm, sr);
    }

    // Soft warm turbo air-whoosh + subtle low-mid turbine
    {
        std::vector<int16_t> pcm(sr);
        uint32_t rng = 0x13579BDFu;
        float bp1 = 0.0f, bp2 = 0.0f, hpPrev = 0.0f;
        for (uint32_t pass = 0; pass < 2; ++pass) {
            for (uint32_t i = 0; i < sr; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(sr);
                rng = rng * 1664525u + 1013904223u;
                float white = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
                float hp = white - hpPrev * 0.90f;
                hpPrev = white;
                bp1 += 0.11f * (hp - bp1);
                bp2 += 0.11f * (bp1 - bp2);

                float turbine = 0.34f * std::sin(PI2 * 520.0f * t) + 0.12f * std::sin(PI2 * 1040.0f * t);
                float s = turbine * 0.60f + bp2 * 0.95f;
                if (pass == 1) {
                    pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 14500.0f), -32000, 32000));
                }
            }
        }
        st.wavSpool = bass::BuildWav16Mono(pcm, sr);
    }

    // Natural muffled BOV
    {
        const uint32_t count = static_cast<uint32_t>(sr * 0.42f);
        std::vector<int16_t> pcm(count);
        uint32_t rng = 0x2468ACE1u;
        float hpPrev = 0.0f, lp1 = 0.0f, lp2 = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            float env = std::exp(-t * 8.2f);
            if (t < 0.012f) env *= (t / 0.012f);
            float flutter = 0.50f + 0.50f * (0.5f + 0.5f * std::sin(PI2 * 19.0f * t));

            rng = rng * 1664525u + 1013904223u;
            float white = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
            float hp = white - hpPrev * 0.85f;
            hpPrev = white;
            lp1 += 0.22f * (hp - lp1);
            lp2 += 0.22f * (lp1 - lp2);

            float freq = 850.0f * std::exp(-t * 3.0f) + 320.0f;
            float body = 0.16f * std::sin(PI2 * freq * t);
            float s = (lp2 * 1.15f + body) * env * flutter;
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 22000.0f), -32000, 32000));
        }
        st.wavBov = bass::BuildWav16Mono(pcm, sr);
    }

    // Exhaust burble / pop on lift-off
    {
        const uint32_t count = static_cast<uint32_t>(sr * 0.24f);
        std::vector<int16_t> pcm(count);
        uint32_t rng = 0x55AA33CCu;
        float lp = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            rng = rng * 1664525u + 1013904223u;
            float n = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
            lp += 0.14f * (n - lp);

            float p1 = std::exp(-std::pow((t - 0.035f) / 0.022f, 2.0f));
            float p2 = 0.65f * std::exp(-std::pow((t - 0.125f) / 0.026f, 2.0f));
            float s = (std::sin(PI2 * 85.0f * t) * 0.45f + lp * 1.2f) * (p1 + p2);
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 20000.0f), -32000, 32000));
        }
        st.wavExhPop = bass::BuildWav16Mono(pcm, sr);
    }

    auto makeHorn = [&](int idx, float f1, float f2, float f3, bool vibrato, bool musical) {
        std::vector<int16_t> pcm(sr);
        for (uint32_t i = 0; i < sr; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            float s = 0.0f;
            if (musical) {
                static const float notes[4] = {523.0f, 659.0f, 784.0f, 1046.0f};
                float nf = notes[static_cast<int>(t * 8.0f) % 4];
                s = 0.55f * std::sin(PI2 * nf * t)
                  + 0.28f * std::sin(PI2 * nf * 2.0f * t)
                  + 0.12f * std::sin(PI2 * nf * 3.0f * t);
            } else {
                float mod = vibrato ? (1.0f + 0.025f * std::sin(PI2 * 14.0f * t)) : 1.0f;
                float raw = std::sin(PI2 * f1 * mod * t)
                          + 0.85f * std::sin(PI2 * f2 * mod * t)
                          + 0.40f * std::sin(PI2 * f3 * mod * t)
                          + 0.35f * std::sin(PI2 * f1 * 2.0f * t)
                          + 0.25f * std::sin(PI2 * f2 * 2.0f * t);
                s = std::tanh(raw * 0.75f);
            }
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 16000.0f), -32000, 32000));
        }
        st.wavHorns[idx] = bass::BuildWav16Mono(pcm, sr);
    };

    makeHorn(0, 185.0f, 233.0f, 370.0f, false, false);
    makeHorn(1, 440.0f, 554.0f, 880.0f, false, false);
    makeHorn(2, 310.0f, 390.0f, 620.0f, true,  false);
    makeHorn(3, 523.0f, 659.0f, 784.0f, false, true);
}

inline void EnsureInit() {
    auto& st = State();
    if (st.attempted) return;
    st.attempted = true;

    if (!bass::EnsureLoaded()) return;

    SynthesizeSounds();
    std::string sndDir = mod::Config().modDir + "\\sounds\\";

    static const char* engFiles[4]  = {"engine_lv1.wav", "engine_lv2.wav", "engine_lv3.wav", "engine_lv4.wav"};
    static const char* hornFiles[4] = {"horn_air.wav", "horn_sport.wav", "horn_classic.wav", "horn_chime.wav"};
    for (int i = 0; i < 4; ++i) {
        st.engineStreams[i] = bass::LoadStream(sndDir + engFiles[i], st.wavEngine[i]);
        st.hornStreams[i]   = bass::LoadStream(sndDir + hornFiles[i], st.wavHorns[i]);
    }
    st.spoolStream  = bass::LoadStream(sndDir + "turbo_spool.wav", st.wavSpool);
    st.bovSample    = bass::LoadSample(sndDir + "turbo_bov.wav", st.wavBov, 4);
    st.exhPopSample = bass::LoadSample("", st.wavExhPop, 4);

    st.initialized = true;
    modlog::Info("[Audio] Vehicle BASS audio initialized via shared ModDev_Common.");
}

inline void UpdateEngineTone(int engineLevel, bool active, float rpm01, float load01) {
    auto& st = State();
    if (!mod::Config().enableEngineTone || engineLevel < 1 || engineLevel > 4) active = false;

    if (!active) {
        if (st.activeEngineLv >= 1 && st.activeEngineLv <= 4 && st.initialized) {
            bass::StopStream(st.engineStreams[st.activeEngineLv - 1]);
        }
        st.activeEngineLv = 0;
        return;
    }

    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;

    if (st.activeEngineLv != engineLevel && st.activeEngineLv >= 1 && st.activeEngineLv <= 4) {
        bass::StopStream(st.engineStreams[st.activeEngineLv - 1]);
    }

    rpm01  = std::clamp(rpm01, 0.0f, 1.0f);
    load01 = std::clamp(load01, 0.0f, 1.0f);
    float freq = 26000.0f + rpm01 * 42000.0f;
    float baseVol = 0.055f + static_cast<float>(engineLevel) * 0.018f;
    float vol = std::clamp(baseVol * (0.45f + 0.55f * load01 + 0.35f * rpm01), 0.02f, 0.21f);
    bass::PlayLoopStream(st.engineStreams[engineLevel - 1], vol, freq);
    st.activeEngineLv = engineLevel;
}

inline void PlayExhaustPop(int engineLevel) {
    auto& st = State();
    if (!mod::Config().enableEngineTone || engineLevel < 3) return;
    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;
    bass::PlaySample(st.exhPopSample, (engineLevel >= 4) ? 0.16f : 0.10f, (engineLevel >= 4) ? 44100.0f : 38000.0f);
}

inline void UpdateTurboSpool(bool active, float boost01) {
    auto& st = State();
    if (!mod::Config().enableTurboAudio) active = false;
    if (!st.initialized) {
        if (active) EnsureInit();
        if (!st.initialized) return;
    }
    if (!active || boost01 < 0.08f) {
        bass::StopStream(st.spoolStream);
        return;
    }
    boost01 = std::clamp(boost01, 0.0f, 1.0f);
    bass::PlayLoopStream(st.spoolStream, 0.015f + boost01 * 0.085f, 24000.0f + boost01 * 20000.0f);
}

inline void PlayTurboBov(float intensity01) {
    auto& st = State();
    if (!mod::Config().enableTurboAudio) return;
    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;
    intensity01 = std::clamp(intensity01, 0.25f, 1.0f);
    bass::PlaySample(st.bovSample, 0.09f + intensity01 * 0.14f, 34000.0f + intensity01 * 10000.0f);
}

inline void UpdateCustomHorn(int hornType, bool pressed) {
    auto& st = State();
    if (!pressed || hornType < 1 || hornType > 4) {
        if (st.activeHorn >= 1 && st.activeHorn <= 4 && st.initialized) {
            bass::StopStream(st.hornStreams[st.activeHorn - 1]);
        }
        st.activeHorn = 0;
        return;
    }
    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;
    if (st.activeHorn != hornType && st.activeHorn >= 1 && st.activeHorn <= 4) {
        bass::StopStream(st.hornStreams[st.activeHorn - 1]);
    }
    bass::PlayLoopStream(st.hornStreams[hornType - 1], 0.30f, 44100.0f);
    st.activeHorn = hornType;
}

inline void StopAll() {
    UpdateEngineTone(0, false, 0.0f, 0.0f);
    UpdateTurboSpool(false, 0.0f);
    UpdateCustomHorn(0, false);
}

} // namespace audio
