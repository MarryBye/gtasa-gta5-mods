#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "bass_audio.hpp"
#include "config.hpp"
#include "log.hpp"
#include "sa_sdk.hpp"

namespace audio {

#pragma pack(push, 1)
struct BankLookupEntry {
    uint8_t  pakIndex; // 1 = GENRL
    uint8_t  pad[3];
    uint32_t offset;   // byte offset in pak file
    uint32_t size;     // total byte size of bank
};
static_assert(sizeof(BankLookupEntry) == 12, "BankLookupEntry must be 12 bytes");

struct BankSoundEntry {
    uint32_t pcmOffset;  // relative to bankOffset + 4804
    int32_t  loopOffset; // 0 = full loop, >0 = loop start sample, -1 = one-shot
    uint16_t sampleRate; // e.g., 18000 Hz (cars/bikes), 11025 Hz (trucks)
    int16_t  headroom;
};
static_assert(sizeof(BankSoundEntry) == 12, "BankSoundEntry must be 12 bytes");
#pragma pack(pop)

constexpr size_t kBankHeaderBytes  = 4804; // 4-byte count + 400 * 12-byte entries
constexpr size_t kMaxCachedBanks   = 12;

struct CachedBankEngine {
    int                  bankId      = -1;
    uint32_t             sampleRate  = 18000;
    uint32_t             lastUsedSeq = 0;
    bool                 valid       = false;
    std::vector<uint8_t> wavLv[4];
    bass::HSTREAM        streams[4]  = {};
};

struct VehicleAudioState {
    bool initialized = false;
    bool attempted   = false;

    // Parsed GTA SA audio\CONFIG\BankLkup.dat & PakFiles.dat
    bool                         bankTableLoaded = false;
    std::vector<BankLookupEntry> bankLookup;
    std::vector<std::string>     pakNames;

    // Per-bank LRU cache of progressively aggressive GENRL engine streams (Lv.1..Lv.4)
    std::vector<CachedBankEngine> bankCache;
    uint32_t                      lruCounter = 0;

    // Currently playing engine stream handle (so StopAll / level switch / car switch is always clean)
    bass::HSTREAM activeEngineStream = 0;
    int           activeBankId       = -2;
    int           activeEngineLv     = 0;
    float         smoothPitchMult    = 1.0f;
    bool          hasSmoothPitch     = false;

    // Fallback generic engine synth (used ONLY if a vehicle has no GENRL bank or GENRL read fails)
    std::vector<uint8_t> wavEngineFallback[4];
    bass::HSTREAM        fallbackEngineStreams[4] = {};

    // Turbocharger spool, Blow-Off Valve (BOV), Exhaust Pop, Custom Horns (unchanged!)
    std::vector<uint8_t> wavSpool;
    std::vector<uint8_t> wavBov;
    std::vector<uint8_t> wavExhPop;
    std::vector<uint8_t> wavHorns[4];

    bass::HSTREAM spoolStream    = 0;
    bass::HSAMPLE bovSample      = 0;
    bass::HSAMPLE exhPopSample   = 0;
    bass::HSTREAM hornStreams[4] = {};
    int           activeHorn     = 0;
};

inline VehicleAudioState& State() {
    static VehicleAudioState s;
    return s;
}

// Opens a GTA SA audio asset using relative path FIRST (so ModLoader virtual overrides work!),
// falling back to absolute gameDir path if needed.
inline HANDLE OpenGameAudioFile(const std::string& relPath) {
    HANDLE h = CreateFileA(relPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) return h;

    std::string absPath = mod::Config().gameDir + "\\" + relPath;
    return CreateFileA(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

inline void LoadBankLookupTable() {
    auto& st = State();
    if (st.bankTableLoaded) return;
    st.bankTableLoaded = true;

    // 1. Read PakFiles.dat (52-byte null-terminated pak names, index 1 = "GENRL")
    HANDLE hPak = OpenGameAudioFile("audio\\CONFIG\\PakFiles.dat");
    if (hPak != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hPak, nullptr);
        if (sz >= 52 && sz <= 65536) {
            std::vector<char> buf(sz);
            DWORD rd = 0;
            if (ReadFile(hPak, buf.data(), sz, &rd, nullptr) && rd >= 52) {
                size_t count = rd / 52;
                st.pakNames.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    char tmp[53] = {};
                    std::memcpy(tmp, buf.data() + i * 52, 52);
                    st.pakNames.emplace_back(tmp);
                }
            }
        }
        CloseHandle(hPak);
    }
    if (st.pakNames.size() < 2) {
        st.pakNames = {"FEET", "GENRL", "PAIN_A", "SCRIPT", "SPC_EA", "SPC_FA", "SPC_GA", "SPC_NA", "SPC_PA"};
    }

    // 2. Read BankLkup.dat (12 bytes per bank)
    HANDLE hLk = OpenGameAudioFile("audio\\CONFIG\\BankLkup.dat");
    if (hLk != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(hLk, nullptr);
        if (sz >= sizeof(BankLookupEntry) && sz <= 262144) {
            size_t count = sz / sizeof(BankLookupEntry);
            st.bankLookup.resize(count);
            DWORD rd = 0;
            if (!ReadFile(hLk, st.bankLookup.data(), static_cast<DWORD>(count * sizeof(BankLookupEntry)), &rd, nullptr) ||
                rd < sizeof(BankLookupEntry)) {
                st.bankLookup.clear();
            } else {
                st.bankLookup.resize(rd / sizeof(BankLookupEntry));
            }
        }
        CloseHandle(hLk);
    }

    modlog::Info("[Audio] Loaded BankLkup.dat (%u banks, %u pak names) for per-vehicle GENRL engine audio.",
                 static_cast<unsigned>(st.bankLookup.size()), static_cast<unsigned>(st.pakNames.size()));
}

inline bool ExtractBankPrimaryPcm(int bankId, std::vector<int16_t>& outPcm, uint32_t& outSampleRate) {
    auto& st = State();
    if (bankId < 0 || static_cast<size_t>(bankId) >= st.bankLookup.size()) return false;

    const auto& entry = st.bankLookup[static_cast<size_t>(bankId)];
    if (entry.size <= kBankHeaderBytes + 512) return false;

    std::string pakName = (entry.pakIndex < st.pakNames.size() && !st.pakNames[entry.pakIndex].empty())
                              ? st.pakNames[entry.pakIndex]
                              : std::string("GENRL");

    HANDLE hFile = OpenGameAudioFile("audio\\SFX\\" + pakName);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER seekPos{};
    seekPos.QuadPart = static_cast<LONGLONG>(entry.offset);
    if (!SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<uint8_t> hdr(kBankHeaderBytes);
    DWORD rd = 0;
    if (!ReadFile(hFile, hdr.data(), static_cast<DWORD>(kBankHeaderBytes), &rd, nullptr) || rd != kBankHeaderBytes) {
        CloseHandle(hFile);
        return false;
    }

    uint16_t numSounds = 0;
    std::memcpy(&numSounds, hdr.data(), 2);
    if (numSounds == 0 || numSounds > 400) {
        CloseHandle(hFile);
        return false;
    }

    BankSoundEntry snd0{};
    std::memcpy(&snd0, hdr.data() + 4, sizeof(BankSoundEntry));
    if (snd0.sampleRate < 4000 || snd0.sampleRate > 48000) {
        CloseHandle(hFile);
        return false;
    }

    uint32_t totalPcmBytes = entry.size - static_cast<uint32_t>(kBankHeaderBytes);
    uint32_t endOff = totalPcmBytes;
    if (numSounds > 1) {
        BankSoundEntry snd1{};
        std::memcpy(&snd1, hdr.data() + 4 + sizeof(BankSoundEntry), sizeof(BankSoundEntry));
        if (snd1.pcmOffset > snd0.pcmOffset && snd1.pcmOffset <= totalPcmBytes) {
            endOff = snd1.pcmOffset;
        }
    }
    if (endOff <= snd0.pcmOffset) {
        CloseHandle(hFile);
        return false;
    }

    uint32_t byteLen = endOff - snd0.pcmOffset;
    uint32_t totalSamples = byteLen / sizeof(int16_t);
    if (totalSamples < 1024 || totalSamples > 600000) {
        CloseHandle(hFile);
        return false;
    }

    // If bank sound defines an internal loop start point, skip any initial transient before the loop region!
    uint32_t startSample = 0;
    if (snd0.loopOffset > 0 && static_cast<uint32_t>(snd0.loopOffset) + 1024 < totalSamples) {
        startSample = static_cast<uint32_t>(snd0.loopOffset);
    }
    uint32_t readSamples = totalSamples - startSample;

    seekPos.QuadPart = static_cast<LONGLONG>(entry.offset) + static_cast<LONGLONG>(kBankHeaderBytes) +
                       static_cast<LONGLONG>(snd0.pcmOffset) + static_cast<LONGLONG>(startSample * sizeof(int16_t));
    if (!SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    outPcm.resize(readSamples);
    DWORD readBytes = static_cast<DWORD>(readSamples * sizeof(int16_t));
    BOOL ok = ReadFile(hFile, outPcm.data(), readBytes, &rd, nullptr);
    CloseHandle(hFile);

    if (!ok || rd < 2048) {
        outPcm.clear();
        return false;
    }
    outPcm.resize(rd / sizeof(int16_t));
    outSampleRate = snd0.sampleRate;
    return true;
}

// Transforms a vehicle's REAL GTA SA GENRL engine PCM into 4 progressively meaner/angrier exhaust grades
// (Lv.1 = Sport Intake/Exhaust -> Lv.2 = Street Headers -> Lv.3 = Titanium Straight-Pipe -> Lv.4 = Full Race Beast)
// while preserving 100% of that exact vehicle's unique firing cadence and sonic character!
inline std::vector<uint8_t> BuildAggressiveBankWav(const std::vector<int16_t>& srcPcm, uint32_t sampleRate, int lv0to3) {
    struct DspGrade {
        float drivePos;   // asymmetric positive overdrive (even + odd exhaust harmonics)
        float driveNeg;   // asymmetric negative overdrive
        float subMix;     // chest-thumping bank-locked sub-bass exhaust body
        float biteMix;    // resonant header mid-high rasp/snarl
        float gritMix;    // race-cam nonlinear harmonic edge (Lv.3..4)
        float outScale;   // target RMS output loudness
    };
    static constexpr DspGrade kGrades[4] = {
        {1.75f, 1.40f, 0.34f, 0.18f, 0.00f, 0.34f}, // Lv.1: Deep warm sport exhaust
        {2.85f, 2.10f, 0.48f, 0.40f, 0.06f, 0.38f}, // Lv.2: Punchy street-performance bark
        {4.40f, 3.05f, 0.62f, 0.68f, 0.16f, 0.41f}, // Lv.3: Aggressive titanium straight-pipe snarl
        {6.40f, 4.25f, 0.78f, 0.96f, 0.28f, 0.44f}, // Lv.4: Visceral full-race roar
    };

    const auto& g = kGrades[std::clamp(lv0to3, 0, 3)];
    const size_t n = srcPcm.size();
    if (n < 1024) return {};

    // 1. Convert to normalized float so quiet & loud GTA SA vehicle banks hit overdrive consistently!
    std::vector<float> norm(n);
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float v = static_cast<float>(srcPcm[i]) / 32768.0f;
        norm[i] = v;
        sumSq += static_cast<double>(v) * static_cast<double>(v);
    }
    float srcRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(n)));
    float preGain = (srcRms > 0.005f) ? std::clamp(0.36f / srcRms, 0.5f, 4.5f) : 1.0f;
    for (size_t i = 0; i < n; ++i) norm[i] *= preGain;

    // Frequency-normalized filter coefficients (scales cleanly between 11025 Hz trucks & 18000 Hz sports/bikes!)
    const float sr = static_cast<float>(sampleRate);
    constexpr float PI2 = 6.28318530718f;
    const float alphaSubLp   = std::clamp(1.0f - std::exp(-PI2 * 165.0f / sr), 0.01f, 0.45f);
    const float alphaBiteHp  = std::clamp(std::exp(-PI2 * 520.0f / sr), 0.50f, 0.96f);
    const float alphaBiteLp  = std::clamp(1.0f - std::exp(-PI2 * (2300.0f + lv0to3 * 350.0f) / sr), 0.08f, 0.85f);
    const float alphaDc      = std::clamp(std::exp(-PI2 * 26.0f / sr), 0.92f, 0.996f);
    const float alphaSubEnv  = std::clamp(1.0f - std::exp(-PI2 * 95.0f / sr), 0.01f, 0.35f);

    std::vector<float> proc(n);
    float subLp1 = 0.0f, subLp2 = 0.0f;
    float subRectLp = 0.0f, subSignFlip = 1.0f, prevSub = 0.0f;
    float biteHpPrevIn = 0.0f, biteHpPrevOut = 0.0f, biteLp = 0.0f;
    float dcPrevIn = 0.0f, dcPrevOut = 0.0f;

    // 2 passes: pass 0 warms all IIR filter states across the loop boundary for zero seam transient!
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < n; ++i) {
            float x = norm[i];

            // A. Bank-native low-end fundamental & octave-down firing cadence sub-harmonic
            subLp1 += alphaSubLp * (x - subLp1);
            subLp2 += alphaSubLp * (subLp1 - subLp2);
            if (prevSub <= 0.0f && subLp2 > 0.0f) {
                subSignFlip = -subSignFlip; // zero-crossing half-frequency divider locked to engine RPM!
            }
            prevSub = subLp2;
            float subOctaveRaw = std::fabs(subLp2) * subSignFlip;
            subRectLp += alphaSubEnv * (subOctaveRaw - subRectLp);
            float subBody = std::tanh((subLp2 * 1.35f + subRectLp * 0.70f) * (1.25f + lv0to3 * 0.25f));

            // B. Asymmetric exhaust tube/header saturation (generates natural 2nd/3rd/4th/5th engine harmonics)
            float driven = (x >= 0.0f) ? std::tanh(x * g.drivePos) : std::tanh(x * g.driveNeg);

            // C. Resonant straight-pipe header midrange bite (bandpass 520..3350 Hz)
            float hp = alphaBiteHp * (biteHpPrevOut + driven - biteHpPrevIn);
            biteHpPrevIn  = driven;
            biteHpPrevOut = hp;
            biteLp += alphaBiteLp * (hp - biteLp);

            // D. Optional nonlinear race-cam grit on the header band (Lv.3..4)
            float biteOut = biteLp;
            if (g.gritMix > 0.001f) {
                float edge = std::tanh(biteLp * 2.4f) - 0.35f * biteLp;
                biteOut += edge * g.gritMix;
            }

            // E. Combine body + saturated core + header bite + DC blocker
            float mixed = driven * 0.72f + subBody * g.subMix + biteOut * g.biteMix;
            float clean = alphaDc * (dcPrevOut + mixed - dcPrevIn);
            dcPrevIn  = mixed;
            dcPrevOut = clean;

            if (pass == 1) {
                proc[i] = clean;
            }
        }
    }

    // 3. Seamless 45ms equal-power overlap crossfade at the loop seam (guarantees zero clicks!)
    size_t fadeLen = std::min<size_t>(static_cast<size_t>(sr * 0.045f), n / 6);
    size_t outLen  = (fadeLen >= 64) ? (n - fadeLen) : n;
    if (fadeLen >= 64) {
        constexpr float HALF_PI = 1.57079632679f;
        for (size_t i = 0; i < fadeLen; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(fadeLen);
            float wHead = std::sin(t * HALF_PI);
            float wTail = std::cos(t * HALF_PI);
            proc[i] = proc[i] * wHead + proc[outLen + i] * wTail;
        }
    }

    // 4. RMS normalize + gentle soft limiter to 16-bit mono PCM
    double outSq = 0.0;
    for (size_t i = 0; i < outLen; ++i) {
        outSq += static_cast<double>(proc[i]) * static_cast<double>(proc[i]);
    }
    float outRms = static_cast<float>(std::sqrt(outSq / static_cast<double>(outLen)));
    float gain = (outRms > 0.005f) ? std::clamp(g.outScale / outRms, 0.25f, 3.0f) : 1.0f;

    std::vector<int16_t> pcm(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        float s = std::tanh(proc[i] * gain * 1.12f);
        pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 31500.0f), -32000, 32000));
    }
    return bass::BuildWav16Mono(pcm, sampleRate);
}

inline CachedBankEngine* GetOrBuildBankEngine(int bankId) {
    auto& st = State();
    if (bankId < 0) return nullptr;

    for (auto& entry : st.bankCache) {
        if (entry.bankId == bankId) {
            entry.lastUsedSeq = ++st.lruCounter;
            return entry.valid ? &entry : nullptr;
        }
    }

    // Evict least-recently-used bank if cache is full (making sure active stream isn't evicted)
    if (st.bankCache.size() >= kMaxCachedBanks) {
        size_t victimIdx = 0;
        uint32_t oldest = UINT32_MAX;
        for (size_t i = 0; i < st.bankCache.size(); ++i) {
            if (st.bankCache[i].bankId == st.activeBankId) continue;
            if (st.bankCache[i].lastUsedSeq < oldest) {
                oldest = st.bankCache[i].lastUsedSeq;
                victimIdx = i;
            }
        }
        auto& victim = st.bankCache[victimIdx];
        for (int lv = 0; lv < 4; ++lv) {
            if (victim.streams[lv]) {
                if (st.activeEngineStream == victim.streams[lv]) st.activeEngineStream = 0;
                bass::FreeStream(victim.streams[lv]);
                victim.streams[lv] = 0;
            }
            victim.wavLv[lv].clear();
        }
        st.bankCache.erase(st.bankCache.begin() + static_cast<ptrdiff_t>(victimIdx));
    }

    CachedBankEngine rec{};
    rec.bankId      = bankId;
    rec.lastUsedSeq = ++st.lruCounter;

    std::vector<int16_t> rawPcm;
    uint32_t sr = 18000;
    if (ExtractBankPrimaryPcm(bankId, rawPcm, sr)) {
        rec.sampleRate = sr;
        bool allOk = true;
        for (int lv = 0; lv < 4; ++lv) {
            rec.wavLv[lv] = BuildAggressiveBankWav(rawPcm, sr, lv);
            if (rec.wavLv[lv].empty()) {
                allOk = false;
                break;
            }
        }
        if (allOk) {
            // Must store in vector FIRST so wavLv[lv].data() pointers remain stable when BASS opens them!
            st.bankCache.push_back(std::move(rec));
            auto& stored = st.bankCache.back();
            bool streamsOk = true;
            for (int lv = 0; lv < 4; ++lv) {
                stored.streams[lv] = bass::LoadStream("", stored.wavLv[lv]);
                if (!stored.streams[lv]) streamsOk = false;
            }
            stored.valid = streamsOk;
            modlog::Info("[Audio] Built 4 aggressive engine grades from GENRL bank #%d (%u Hz, %u samples, ok=%d).",
                         bankId, sr, static_cast<unsigned>(rawPcm.size()), streamsOk ? 1 : 0);
            return stored.valid ? &stored : nullptr;
        }
    }

    // Cache negative result so we don't hit disk repeatedly for unsupported banks
    rec.valid = false;
    st.bankCache.push_back(std::move(rec));
    return nullptr;
}

inline void SynthesizeSounds() {
    auto& st = State();
    const uint32_t sr = 44100;
    constexpr float PI2 = 6.28318530718f;

    // Fallback engine synth (ONLY used if a vehicle has no GENRL bank or GENRL is inaccessible)
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
        st.wavEngineFallback[lv] = bass::BuildWav16Mono(pcm, sr);
    }

    // Soft warm turbo air-whoosh + subtle low-mid turbine (kept intact!)
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

    // Natural muffled BOV (kept intact!)
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

    // Exhaust burble / pop on lift-off (pitch-scaled per vehicle class in PlayExhaustPop!)
    {
        const uint32_t count = static_cast<uint32_t>(sr * 0.26f);
        std::vector<int16_t> pcm(count);
        uint32_t rng = 0x55AA33CCu;
        float lp = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            rng = rng * 1664525u + 1013904223u;
            float n = (static_cast<float>((rng >> 16) & 0xFFFF) / 32768.0f) - 1.0f;
            lp += 0.16f * (n - lp);

            float p1 = std::exp(-std::pow((t - 0.032f) / 0.020f, 2.0f));
            float p2 = 0.72f * std::exp(-std::pow((t - 0.115f) / 0.024f, 2.0f));
            float p3 = 0.42f * std::exp(-std::pow((t - 0.195f) / 0.022f, 2.0f));
            float s = std::tanh((std::sin(PI2 * 88.0f * t) * 0.48f + lp * 1.45f) * 1.35f) * (p1 + p2 + p3);
            pcm[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s * 23000.0f), -32000, 32000));
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

    LoadBankLookupTable();
    st.bankCache.reserve(kMaxCachedBanks);
    SynthesizeSounds();
    std::string sndDir = mod::Config().modDir + "\\sounds\\";

    static const char* hornFiles[4] = {"horn_air.wav", "horn_sport.wav", "horn_classic.wav", "horn_chime.wav"};
    for (int i = 0; i < 4; ++i) {
        st.fallbackEngineStreams[i] = bass::LoadStream("", st.wavEngineFallback[i]);
        st.hornStreams[i]           = bass::LoadStream(sndDir + hornFiles[i], st.wavHorns[i]);
    }
    st.spoolStream  = bass::LoadStream(sndDir + "turbo_spool.wav", st.wavSpool);
    st.bovSample    = bass::LoadSample(sndDir + "turbo_bov.wav", st.wavBov, 4);
    st.exhPopSample = bass::LoadSample("", st.wavExhPop, 4);

    st.initialized = true;
    modlog::Info("[Audio] Vehicle BASS audio initialized (GENRL per-vehicle bank DSP + Turbo/BOV + Horns).");
}

inline void StopEngineTone() {
    auto& st = State();
    if (st.activeEngineStream && st.initialized) {
        bass::StopStream(st.activeEngineStream);
    }
    st.activeEngineStream = 0;
    st.activeBankId       = -2;
    st.activeEngineLv     = 0;
    st.hasSmoothPitch     = false;
}

inline void UpdateEngineTone(int32_t modelId, int engineLevel, bool active, float rpm01, float load01, float livePitchRatio = 0.0f) {
    auto& st = State();
    if (!mod::Config().enableEngineTone || engineLevel < 1 || engineLevel > 4) active = false;

    if (!active) {
        StopEngineTone();
        return;
    }

    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;

    const sa::tVehicleAudioSettings* vs = sa::GetVehicleAudioSettings(modelId);
    // Bicycles (sound type 2) never play engine sounds!
    if (vs && vs->m_nVehicleSoundType == 2) {
        StopEngineTone();
        return;
    }

    int bankId = (vs && vs->m_nEngineOnSoundBankId >= 0) ? static_cast<int>(vs->m_nEngineOnSoundBankId) : -1;
    float basePitch = (vs && vs->m_fField14 > 0.30f && vs->m_fField14 < 2.50f) ? vs->m_fField14 : 1.0f;

    CachedBankEngine* bank = GetOrBuildBankEngine(bankId);
    bass::HSTREAM targetStream = bank ? bank->streams[engineLevel - 1] : st.fallbackEngineStreams[engineLevel - 1];
    if (!targetStream) {
        StopEngineTone();
        return;
    }

    // Cleanly stop previous stream if switching vehicle bank or engine upgrade level
    if (st.activeEngineStream && st.activeEngineStream != targetStream) {
        bass::StopStream(st.activeEngineStream);
        st.hasSmoothPitch = false;
    }

    rpm01  = std::clamp(rpm01, 0.0f, 1.0f);
    load01 = std::clamp(load01, 0.0f, 1.0f);

    float freq = 44100.0f;
    float vol  = 0.0f;

    if (bank) {
        // Lock 100% to GTA SA's live CAESound::m_fSpeed when available so our aggressive GENRL
        // exhaust fuses seamlessly with the car's native engine sound with zero detuning!
        float targetPitch = (livePitchRatio >= 0.30f && livePitchRatio <= 2.80f)
                                ? livePitchRatio
                                : (basePitch * (0.72f + rpm01 * 0.88f));
        if (!st.hasSmoothPitch) {
            st.smoothPitchMult = targetPitch;
            st.hasSmoothPitch  = true;
        } else {
            st.smoothPitchMult += (targetPitch - st.smoothPitchMult) * 0.42f;
        }
        freq = std::clamp(static_cast<float>(bank->sampleRate) * st.smoothPitchMult,
                          static_cast<float>(bank->sampleRate) * 0.42f,
                          static_cast<float>(bank->sampleRate) * 2.45f);

        static constexpr float kLvGain[4] = {0.115f, 0.175f, 0.245f, 0.325f};
        float dyn = 0.30f + 0.56f * load01 + 0.26f * rpm01;
        vol = std::clamp(kLvGain[engineLevel - 1] * dyn, 0.025f, 0.36f);
    } else {
        // Fallback synth tuning
        freq = 26000.0f + rpm01 * 42000.0f;
        float baseVol = 0.055f + static_cast<float>(engineLevel) * 0.018f;
        vol = std::clamp(baseVol * (0.45f + 0.55f * load01 + 0.35f * rpm01), 0.02f, 0.21f);
    }

    bass::PlayLoopStream(targetStream, vol, freq);
    st.activeEngineStream = targetStream;
    st.activeBankId       = bankId;
    st.activeEngineLv     = engineLevel;
}

inline void PlayExhaustPop(int32_t modelId, int engineLevel) {
    auto& st = State();
    if (!mod::Config().enableEngineTone || engineLevel < 3) return;
    if (!st.initialized) EnsureInit();
    if (!st.initialized) return;

    const sa::tVehicleAudioSettings* vs = sa::GetVehicleAudioSettings(modelId);
    if (vs && vs->m_nVehicleSoundType == 2) return;

    float pitchScale = 1.0f;
    if (vs) {
        if (vs->m_nVehicleSoundType == 1) {
            pitchScale = 1.24f; // Sportbikes / motorcycles -> crisp sharp crackle
        } else if (vs->m_fField14 > 0.35f && vs->m_fField14 < 2.20f) {
            pitchScale = std::clamp(vs->m_fField14, 0.72f, 1.25f);
        }
        if (vs->m_nBassSetting > 0 && vs->m_nVehicleSoundType == 0) {
            pitchScale *= 0.88f; // Muscle / V8 / heavy bass cars -> deeper booming backfire
        }
    }

    float baseFreq = (engineLevel >= 4) ? 43000.0f : 37500.0f;
    float vol      = (engineLevel >= 4) ? 0.19f : 0.12f;
    bass::PlaySample(st.exhPopSample, vol, std::clamp(baseFreq * pitchScale, 25000.0f, 56000.0f));
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
    StopEngineTone();
    UpdateTurboSpool(false, 0.0f);
    UpdateCustomHorn(0, false);
}

} // namespace audio
