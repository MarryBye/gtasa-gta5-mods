#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "garage_config.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "sa_sdk.hpp"
#include "vehicle_data.hpp"

namespace storage {

inline std::string GetSlotFilePath(int slotIndex, bool forLoading = false) {
    const char* ov = integrations::sacoop::GetProfileOverride();
    if (forLoading) {
        return integrations::sacoop::ResolveExistingLoadPath("GTA5GarageSaves", mod::Config().gameDir, "garage", slotIndex, ov);
    }
    return integrations::sacoop::BuildSaveProfilePath("GTA5GarageSaves", mod::Config().gameDir, "garage", slotIndex, ov);
}

inline std::string HexEncode(const std::string& raw) {
    static const char HEX[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(HEX[c >> 4]);
        out.push_back(HEX[c & 0xF]);
    }
    return out;
}

inline std::string HexDecode(const char* hex) {
    std::string out;
    if (!hex) return out;
    size_t n = std::strlen(hex);
    out.reserve(n / 2);
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return 0;
    };
    for (size_t i = 0; i + 1 < n; i += 2) {
        out.push_back(static_cast<char>((val(hex[i]) << 4) | val(hex[i + 1])));
    }
    return out;
}

inline void WriteVehicleDataBlock(FILE* f, const mod::VisualAppearance& vis,
                                  const mod::PerformanceTuning& tun, const mod::VehicleWear& wear)
{
    std::fprintf(f, "Color1=%u\n", vis.primaryColor);
    std::fprintf(f, "Color2=%u\n", vis.secondaryColor);
    std::fprintf(f, "Paintjob=%d\n", static_cast<int>(vis.paintjob));

    std::string upgStr;
    for (int u = 0; u < 15; ++u) {
        if (u > 0) upgStr += ",";
        upgStr += std::to_string(vis.upgrades[u]);
    }
    std::fprintf(f, "Upgrades=%s\n", upgStr.c_str());

    std::fprintf(f, "Engine=%u\n", tun.engineLevel);
    std::fprintf(f, "Transmission=%u\n", tun.transmissionLevel);
    std::fprintf(f, "Suspension=%u\n", tun.suspensionLevel);
    std::fprintf(f, "Brakes=%u\n", tun.brakesLevel);
    std::fprintf(f, "TiresGrip=%u\n", tun.tiresGripLevel);
    std::fprintf(f, "Armor=%u\n", tun.armorLevel);
    std::fprintf(f, "Bulletproof=%d\n", tun.bulletproofTires ? 1 : 0);
    std::fprintf(f, "Turbo=%d\n", tun.turbo ? 1 : 0);
    std::fprintf(f, "Horn=%u\n", tun.hornType);
    std::fprintf(f, "Plate=%s\n", tun.plateText);
    std::fprintf(f, "WearEngine=%.2f\n", wear.engine);
    std::fprintf(f, "WearTrans=%.2f\n", wear.transmission);
    std::fprintf(f, "WearBrakes=%.2f\n", wear.brakes);
    std::fprintf(f, "WearTires=%.2f\n", wear.tires);
    std::fprintf(f, "WearSusp=%.2f\n", wear.suspension);
    std::fprintf(f, "WearChassis=%.2f\n\n", wear.chassis);
}

inline void ReadVehicleDataBlock(const char* p, const char* sec, mod::VisualAppearance& vis,
                                 mod::PerformanceTuning& tun, mod::VehicleWear& wear)
{
    vis.primaryColor   = static_cast<uint8_t>(common::IniReadInt(p, sec, "Color1", 0));
    vis.secondaryColor = static_cast<uint8_t>(common::IniReadInt(p, sec, "Color2", 0));
    vis.paintjob       = static_cast<int8_t>(common::IniReadInt(p, sec, "Paintjob", -1));

    std::string upg = common::IniReadString(p, sec, "Upgrades", "");
    if (!upg.empty()) {
        char buf[256] = {};
        std::strncpy(buf, upg.c_str(), sizeof(buf) - 1);
        char* cur = buf;
        for (int u = 0; u < 15 && cur && *cur; ++u) {
            char* comma = std::strchr(cur, ',');
            if (comma) *comma = '\0';
            vis.upgrades[u] = static_cast<int16_t>(std::atoi(cur));
            cur = comma ? (comma + 1) : nullptr;
        }
    }

    tun.engineLevel       = static_cast<uint8_t>(common::IniReadInt(p, sec, "Engine", 0));
    tun.transmissionLevel = static_cast<uint8_t>(common::IniReadInt(p, sec, "Transmission", 0));
    tun.suspensionLevel   = static_cast<uint8_t>(common::IniReadInt(p, sec, "Suspension", 0));
    tun.brakesLevel       = static_cast<uint8_t>(common::IniReadInt(p, sec, "Brakes", 0));
    tun.tiresGripLevel    = static_cast<uint8_t>(common::IniReadInt(p, sec, "TiresGrip", 0));
    tun.armorLevel        = static_cast<uint8_t>(common::IniReadInt(p, sec, "Armor", 0));
    tun.bulletproofTires  = common::IniReadBool(p, sec, "Bulletproof", false);
    tun.turbo             = common::IniReadBool(p, sec, "Turbo", false);
    tun.hornType          = static_cast<uint8_t>(common::IniReadInt(p, sec, "Horn", 0));

    std::string plate = common::IniReadString(p, sec, "Plate", "");
    size_t len = std::min<size_t>(plate.size(), 8);
    std::memcpy(tun.plateText, plate.c_str(), len);
    tun.plateText[len] = '\0';

    wear.engine       = common::IniReadFloat(p, sec, "WearEngine",  0.0f);
    wear.transmission = common::IniReadFloat(p, sec, "WearTrans",   0.0f);
    wear.brakes       = common::IniReadFloat(p, sec, "WearBrakes",  0.0f);
    wear.tires        = common::IniReadFloat(p, sec, "WearTires",   0.0f);
    wear.suspension   = common::IniReadFloat(p, sec, "WearSusp",    0.0f);
    wear.chassis      = common::IniReadFloat(p, sec, "WearChassis", 0.0f);
    wear.ClampAll();
}

inline bool SaveGarageToSlot(int slotIndex,
                             const std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& slots,
                             const std::array<mod::SafehouseCarRecord, mod::MAX_SAFEHOUSE_CARS>* safehouseCars = nullptr) {
    std::string path = GetSlotFilePath(slotIndex, false);
    if (path.empty()) return true; // Unsaved Singleplayer/Host New Game: keep in RAM until saved to a slot
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        modlog::Info("[Storage] Failed to open %s for writing", path.c_str());
        return false;
    }

    std::fprintf(f, "; GTA5GarageAndTuning Per-Save Garage Data (Slot %d)\n\n", slotIndex);
    for (int i = 0; i < mod::MAX_OWNED_VEHICLES; ++i) {
        const auto& v = slots[i];
        std::fprintf(f, "[Slot%d]\n", i);
        std::fprintf(f, "Occupied=%d\n", v.occupied ? 1 : 0);
        if (!v.occupied) {
            std::fprintf(f, "\n");
            continue;
        }
        std::fprintf(f, "Destroyed=%d\n", v.destroyed ? 1 : 0);
        std::fprintf(f, "ModelId=%d\n", v.modelId);
        std::fprintf(f, "NameHex=%s\n", HexEncode(v.displayNameGame).c_str());
        WriteVehicleDataBlock(f, v.visuals, v.tuning, v.wear);
    }

    if (safehouseCars) {
        int outIdx = 0;
        for (int i = 0; i < mod::MAX_SAFEHOUSE_CARS; ++i) {
            const auto& r = (*safehouseCars)[i];
            if (!r.occupied || r.modelId < 400 || r.hideoutGarageId < 0) continue;
            std::fprintf(f, "[Safehouse%d]\n", outIdx++);
            std::fprintf(f, "GarageId=%d\n", r.hideoutGarageId);
            std::fprintf(f, "PersonalSlot=%d\n", r.personalSlotIdx);
            std::fprintf(f, "ModelId=%d\n", r.modelId);
            WriteVehicleDataBlock(f, r.visuals, r.tuning, r.wear);
        }
        std::fprintf(f, "[SafehouseMeta]\nCount=%d\n", outIdx);
    }

    std::fclose(f);
    modlog::Info("[Storage] Saved garage to %s", path.c_str());
    return true;
}

inline bool LoadGarageFromSlot(int slotIndex,
                               std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& slots,
                               std::array<mod::SafehouseCarRecord, mod::MAX_SAFEHOUSE_CARS>* safehouseCars = nullptr) {
    for (auto& v : slots) v = mod::OwnedVehicle{};
    if (safehouseCars) {
        for (auto& r : *safehouseCars) r = mod::SafehouseCarRecord{};
    }

    std::string path = GetSlotFilePath(slotIndex, true);
    if (path.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        modlog::Info("[Storage] No saved garage file for slot %d -> starting fresh", slotIndex);
        return false;
    }

    const char* p = path.c_str();
    for (int i = 0; i < mod::MAX_OWNED_VEHICLES; ++i) {
        char sec[32];
        std::snprintf(sec, sizeof(sec), "Slot%d", i);
        if (!common::IniReadBool(p, sec, "Occupied", false)) continue;

        auto& v = slots[i];
        v.occupied        = true;
        v.destroyed       = common::IniReadBool(p, sec, "Destroyed", false);
        v.modelId         = common::IniReadInt(p, sec, "ModelId", 0);
        v.displayNameGame = HexDecode(common::IniReadString(p, sec, "NameHex", "").c_str());
        ReadVehicleDataBlock(p, sec, v.visuals, v.tuning, v.wear);
    }

    if (safehouseCars) {
        int shCount = std::clamp(common::IniReadInt(p, "SafehouseMeta", "Count", 0), 0, mod::MAX_SAFEHOUSE_CARS);
        for (int i = 0; i < shCount; ++i) {
            char sec[32];
            std::snprintf(sec, sizeof(sec), "Safehouse%d", i);
            int mid = common::IniReadInt(p, sec, "ModelId", 0);
            int gid = common::IniReadInt(p, sec, "GarageId", -1);
            if (mid < 400 || gid < 0) continue;

            auto& r = (*safehouseCars)[i];
            r.occupied        = true;
            r.hideoutGarageId = gid;
            r.personalSlotIdx = common::IniReadInt(p, sec, "PersonalSlot", -1);
            r.modelId         = mid;
            ReadVehicleDataBlock(p, sec, r.visuals, r.tuning, r.wear);
        }
    }

    modlog::Info("[Storage] Loaded garage from %s", path.c_str());
    return true;
}

} // namespace storage
