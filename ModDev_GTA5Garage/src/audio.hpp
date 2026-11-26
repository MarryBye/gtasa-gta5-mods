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

    // Turbocharger spool, Blow-Off Valve (BOV), and Custom Horns
    // (Engine pitch deepening is handled natively via CAEVehicleAudioEntity::GetFreqForPlayerEngineSound hook!)
    std::vector<uint8_t> wavSpool;
    std::vector<uint8_t> wavBov;
    std::vector<uint8_t> wavHorns[4];

    bass::HSTREAM spoolStream    = 0;
    bass::HSAMPLE bovSample      = 0;
    bass::HSTREAM hornStreams[4] = {};
    int           activeHorn     = 0;
};

inline VehicleAudioState& State() {
    static VehicleAudioState s;
    return s;
}

inline void SynthesizeSounds() {
    auto& st = State();
    const uint32_t sr = 44100;
    constexpr float PI2 = 6.28318530718f;

    // Soft subtle turbo air-whoosh + gentle turbine whistle
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

                float turbine = 0.36f * std::sin(PI2 * 560.0f * t) + 0.14f * std::sin(PI2 * 1120.0f * t);
                float s = turbine * 0.62f + bp2 * 0.95f;
                if (pass == 1) {
                    pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 20500.0f), -32000, 32000));
                }
            }
        }
        st.wavSpool = bass::BuildWav16Mono(pcm, sr);
    }

    // Natural muffled Blow-Off Valve (gentle pshh on gear shift / throttle lift)
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
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 28500.0f), -32000, 32000));
        }
        st.wavBov = bass::BuildWav16Mono(pcm, sr);
    }

    // 4 Custom Horns
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

    static const char* hornFiles[4] = {"horn_air.wav", "horn_sport.wav", "horn_classic.wav", "horn_chime.wav"};
    for (int i = 0; i < 4; ++i) {
        st.hornStreams[i] = bass::LoadStream(sndDir + hornFiles[i], st.wavHorns[i]);
    }
    st.spoolStream = bass::LoadStream(sndDir + "turbo_spool.wav", st.wavSpool);
    st.bovSample   = bass::LoadSample(sndDir + "turbo_bov.wav", st.wavBov, 4);

    st.initialized = true;
    modlog::Info("[Audio] Vehicle BASS audio initialized (Subtle Turbo Spool/BOV + Custom Horns).");
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
    // Clean audible turbine whistle under boost (balanced alongside custom Vehicle Sounds mods)
    bass::PlayLoopStream(st.spoolStream, 0.028f + boost01 * 0.112f, 25000.0f + boost01 * 19500.0f);
}

inline void PlayTurboBov(float intensity01) {
    auto& st = State();
    if (!mod::Config().enableTurboAudio) return;
    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;
    intensity01 = std::clamp(intensity01, 0.25f, 1.0f);
    // Crisp fluttery Blow-Off Valve pshh on gear upshift / throttle lift
    bass::PlaySample(st.bovSample, 0.14f + intensity01 * 0.17f, 34500.0f + intensity01 * 9500.0f);
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
    UpdateTurboSpool(false, 0.0f);
    UpdateCustomHorn(0, false);
}

} // namespace audio
