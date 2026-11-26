#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "log.hpp"

// ============================================================================
// Shared BASS.dll Dynamic Audio Engine (Reused by Vehicle & Weapon ASI Mods)
// ============================================================================
namespace bass {

using HSTREAM  = uint32_t;
using HSAMPLE  = uint32_t;
using HCHANNEL = uint32_t;

constexpr uint32_t SAMPLE_LOOP      = 4;
constexpr uint32_t SAMPLE_OVER_POS  = 0x20000;
constexpr uint32_t ATTRIB_FREQ      = 1;
constexpr uint32_t ATTRIB_VOL       = 2;
constexpr uint32_t ACTIVE_PLAYING   = 1;

using FnBASS_Init                = BOOL(WINAPI*)(int, uint32_t, uint32_t, HWND, const void*);
using FnBASS_ErrorGetCode        = int(WINAPI*)();
using FnBASS_StreamCreateFile    = HSTREAM(WINAPI*)(BOOL, const void*, uint64_t, uint64_t, uint32_t);
using FnBASS_SampleLoad          = HSAMPLE(WINAPI*)(BOOL, const void*, uint64_t, uint32_t, uint32_t, uint32_t);
using FnBASS_SampleGetChannel    = HCHANNEL(WINAPI*)(HSAMPLE, uint32_t);
using FnBASS_ChannelPlay         = BOOL(WINAPI*)(uint32_t, BOOL);
using FnBASS_ChannelStop         = BOOL(WINAPI*)(uint32_t);
using FnBASS_ChannelSetAttribute = BOOL(WINAPI*)(uint32_t, uint32_t, float);
using FnBASS_ChannelIsActive     = uint32_t(WINAPI*)(uint32_t);
using FnBASS_StreamFree          = BOOL(WINAPI*)(HSTREAM);

struct Core {
    bool    ready     = false;
    bool    attempted = false;
    HMODULE dll       = nullptr;

    FnBASS_Init                Init = nullptr;
    FnBASS_ErrorGetCode        ErrorGetCode = nullptr;
    FnBASS_StreamCreateFile    StreamCreateFile = nullptr;
    FnBASS_SampleLoad          SampleLoad = nullptr;
    FnBASS_SampleGetChannel    SampleGetChannel = nullptr;
    FnBASS_ChannelPlay         ChannelPlay = nullptr;
    FnBASS_ChannelStop         ChannelStop = nullptr;
    FnBASS_ChannelSetAttribute ChannelSetAttribute = nullptr;
    FnBASS_ChannelIsActive     ChannelIsActive = nullptr;
    FnBASS_StreamFree          StreamFree = nullptr;
};

inline Core& Engine() {
    static Core c;
    return c;
}

inline bool EnsureLoaded() {
    auto& c = Engine();
    if (c.attempted) return c.ready;
    c.attempted = true;

    c.dll = GetModuleHandleA("bass.dll");
    if (!c.dll) c.dll = LoadLibraryA("bass.dll");
    if (!c.dll) {
        modlog::Info("[BASS] bass.dll not found, custom audio disabled.");
        return false;
    }

    auto getFn = [&](const char* name) -> void* {
        return reinterpret_cast<void*>(GetProcAddress(c.dll, name));
    };

    c.Init                = reinterpret_cast<FnBASS_Init>(getFn("BASS_Init"));
    c.ErrorGetCode        = reinterpret_cast<FnBASS_ErrorGetCode>(getFn("BASS_ErrorGetCode"));
    c.StreamCreateFile    = reinterpret_cast<FnBASS_StreamCreateFile>(getFn("BASS_StreamCreateFile"));
    c.SampleLoad          = reinterpret_cast<FnBASS_SampleLoad>(getFn("BASS_SampleLoad"));
    c.SampleGetChannel    = reinterpret_cast<FnBASS_SampleGetChannel>(getFn("BASS_SampleGetChannel"));
    c.ChannelPlay         = reinterpret_cast<FnBASS_ChannelPlay>(getFn("BASS_ChannelPlay"));
    c.ChannelStop         = reinterpret_cast<FnBASS_ChannelStop>(getFn("BASS_ChannelStop"));
    c.ChannelSetAttribute = reinterpret_cast<FnBASS_ChannelSetAttribute>(getFn("BASS_ChannelSetAttribute"));
    c.ChannelIsActive     = reinterpret_cast<FnBASS_ChannelIsActive>(getFn("BASS_ChannelIsActive"));
    c.StreamFree          = reinterpret_cast<FnBASS_StreamFree>(getFn("BASS_StreamFree"));

    if (!c.Init || !c.StreamCreateFile || !c.SampleLoad || !c.SampleGetChannel ||
        !c.ChannelPlay || !c.ChannelStop || !c.ChannelSetAttribute || !c.ChannelIsActive) {
        modlog::Info("[BASS] bass.dll missing expected exports.");
        return false;
    }

    BOOL ok = c.Init(-1, 44100, 0, nullptr, nullptr);
    if (!ok) {
        int err = c.ErrorGetCode ? c.ErrorGetCode() : -1;
        if (err != 14) { // 14 = BASS_ERROR_ALREADY (shared with CLEO/other ASI mods)
            modlog::Info("[BASS] BASS_Init failed (err=%d).", err);
            return false;
        }
    }

    c.ready = true;
    return true;
}

inline std::vector<uint8_t> BuildWav16Mono(const std::vector<int16_t>& samples, uint32_t sampleRate = 44100) {
    uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t riffSize = 36 + dataSize;
    std::vector<uint8_t> wav(44 + dataSize);
    uint8_t* p = wav.data();

    std::memcpy(p + 0,  "RIFF", 4);
    std::memcpy(p + 4,  &riffSize, 4);
    std::memcpy(p + 8,  "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    uint32_t fmtSz = 16;
    uint16_t audioFmt = 1, channels = 1, bitsPerSample = 16;
    uint32_t byteRate = sampleRate * 2;
    uint16_t blockAlign = 2;
    std::memcpy(p + 16, &fmtSz, 4);
    std::memcpy(p + 20, &audioFmt, 2);
    std::memcpy(p + 22, &channels, 2);
    std::memcpy(p + 24, &sampleRate, 4);
    std::memcpy(p + 28, &byteRate, 4);
    std::memcpy(p + 32, &blockAlign, 2);
    std::memcpy(p + 34, &bitsPerSample, 2);
    std::memcpy(p + 36, "data", 4);
    std::memcpy(p + 40, &dataSize, 4);
    if (dataSize > 0) {
        std::memcpy(p + 44, samples.data(), dataSize);
    }
    return wav;
}

inline HSTREAM LoadStream(const std::string& filePath, const std::vector<uint8_t>& fallbackWav, uint32_t flags = SAMPLE_LOOP) {
    auto& c = Engine();
    if (!c.ready) return 0;
    if (!filePath.empty() && GetFileAttributesA(filePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HSTREAM s = c.StreamCreateFile(FALSE, filePath.c_str(), 0, 0, flags);
        if (s) return s;
    }
    if (fallbackWav.empty()) return 0;
    return c.StreamCreateFile(TRUE, fallbackWav.data(), 0, fallbackWav.size(), flags);
}

inline HSAMPLE LoadSample(const std::string& filePath, const std::vector<uint8_t>& fallbackWav, uint32_t maxChannels = 6) {
    auto& c = Engine();
    if (!c.ready) return 0;
    if (!filePath.empty() && GetFileAttributesA(filePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        HSAMPLE s = c.SampleLoad(FALSE, filePath.c_str(), 0, 0, maxChannels, SAMPLE_OVER_POS);
        if (s) return s;
    }
    if (fallbackWav.empty()) return 0;
    return c.SampleLoad(TRUE, fallbackWav.data(), 0, static_cast<uint32_t>(fallbackWav.size()), maxChannels, SAMPLE_OVER_POS);
}

inline void PlaySample(HSAMPLE sample, float volume, float freqHz = 44100.0f) {
    auto& c = Engine();
    if (!c.ready || !sample) return;
    HCHANNEL ch = c.SampleGetChannel(sample, 0);
    if (!ch) return;
    c.ChannelSetAttribute(ch, ATTRIB_VOL, std::clamp(volume, 0.0f, 1.0f));
    c.ChannelSetAttribute(ch, ATTRIB_FREQ, freqHz);
    c.ChannelPlay(ch, TRUE);
}

inline void StopStream(HSTREAM stream) {
    auto& c = Engine();
    if (!c.ready || !stream) return;
    if (c.ChannelIsActive(stream) == ACTIVE_PLAYING) {
        c.ChannelStop(stream);
    }
}

inline void FreeStream(HSTREAM stream) {
    auto& c = Engine();
    if (!c.ready || !stream || !c.StreamFree) return;
    if (c.ChannelIsActive(stream) == ACTIVE_PLAYING) {
        c.ChannelStop(stream);
    }
    c.StreamFree(stream);
}

inline void PlayLoopStream(HSTREAM stream, float volume, float freqHz) {
    auto& c = Engine();
    if (!c.ready || !stream) return;
    c.ChannelSetAttribute(stream, ATTRIB_VOL, std::clamp(volume, 0.0f, 1.0f));
    c.ChannelSetAttribute(stream, ATTRIB_FREQ, freqHz);
    if (c.ChannelIsActive(stream) != ACTIVE_PLAYING) {
        c.ChannelPlay(stream, FALSE);
    }
}

} // namespace bass
