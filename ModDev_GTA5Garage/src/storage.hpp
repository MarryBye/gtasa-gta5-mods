#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include <shlobj.h>
#include "config.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "vehicle_data.hpp"

namespace storage {

inline const char* GetCoopOverride() {
    if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) return b->coopProfileName;
    return nullptr;
}

// slotIndex: 1..8 for GTASAsf1..8.b (SP or SA-COOP Host), or 0 for SA-COOP Client per-player profile!
inline std::string GetSlotFilePath(int slotIndex, bool forLoading = false) {
    if (forLoading) {
        return sa::ResolveExistingLoadPath("GTA5GarageSaves", mod::Config().gameDir, "garage", slotIndex, GetCoopOverride());
    }
    return sa::BuildSaveProfilePath("GTA5GarageSaves", mod::Config().gameDir, "garage", slotIndex, GetCoopOverride());
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

inline bool SaveGarageToSlot(int slotIndex,
                             const std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& slots,
                             const std::array<mod::SafehouseCarRecord, mod::MAX_SAFEHOUSE_CARS>* safehouseCars = nullptr) {
    std::string path = GetSlotFilePath(slotIndex, false);
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
        std::fprintf(f, "Color1=%u\n", v.visuals.primaryColor);
        std::fprintf(f, "Color2=%u\n", v.visuals.secondaryColor);
        std::fprintf(f, "Paintjob=%d\n", static_cast<int>(v.visuals.paintjob));

        std::string upgStr;
        for (int u = 0; u < 15; ++u) {
            if (u > 0) upgStr += ",";
            upgStr += std::to_string(v.visuals.upgrades[u]);
        }
        std::fprintf(f, "Upgrades=%s\n", upgStr.c_str());

        std::fprintf(f, "Engine=%u\n", v.tuning.engineLevel);
        std::fprintf(f, "Transmission=%u\n", v.tuning.transmissionLevel);
        std::fprintf(f, "Suspension=%u\n", v.tuning.suspensionLevel);
        std::fprintf(f, "Brakes=%u\n", v.tuning.brakesLevel);
        std::fprintf(f, "TiresGrip=%u\n", v.tuning.tiresGripLevel);
        std::fprintf(f, "Armor=%u\n", v.tuning.armorLevel);
        std::fprintf(f, "Bulletproof=%d\n", v.tuning.bulletproofTires ? 1 : 0);
        std::fprintf(f, "Turbo=%d\n", v.tuning.turbo ? 1 : 0);
        std::fprintf(f, "Xenon=%d\n", v.tuning.xenonLights ? 1 : 0);
        std::fprintf(f, "Neon=%u\n", static_cast<unsigned>(v.tuning.neonColor));
        std::fprintf(f, "Horn=%u\n", v.tuning.hornType);
        std::fprintf(f, "Plate=%s\n", v.tuning.plateText);
        std::fprintf(f, "WearEngine=%.2f\n", v.wear.engine);
        std::fprintf(f, "WearTrans=%.2f\n", v.wear.transmission);
        std::fprintf(f, "WearBrakes=%.2f\n", v.wear.brakes);
        std::fprintf(f, "WearTires=%.2f\n", v.wear.tires);
        std::fprintf(f, "WearSusp=%.2f\n", v.wear.suspension);
        std::fprintf(f, "WearChassis=%.2f\n\n", v.wear.chassis);
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
            std::fprintf(f, "Color1=%u\n", r.visuals.primaryColor);
            std::fprintf(f, "Color2=%u\n", r.visuals.secondaryColor);
            std::fprintf(f, "Paintjob=%d\n", static_cast<int>(r.visuals.paintjob));
            std::string upgStr;
            for (int u = 0; u < 15; ++u) {
                if (u > 0) upgStr += ",";
                upgStr += std::to_string(r.visuals.upgrades[u]);
            }
            std::fprintf(f, "Upgrades=%s\n", upgStr.c_str());
            std::fprintf(f, "Engine=%u\n", r.tuning.engineLevel);
            std::fprintf(f, "Transmission=%u\n", r.tuning.transmissionLevel);
            std::fprintf(f, "Suspension=%u\n", r.tuning.suspensionLevel);
            std::fprintf(f, "Brakes=%u\n", r.tuning.brakesLevel);
            std::fprintf(f, "TiresGrip=%u\n", r.tuning.tiresGripLevel);
            std::fprintf(f, "Armor=%u\n", r.tuning.armorLevel);
            std::fprintf(f, "Bulletproof=%d\n", r.tuning.bulletproofTires ? 1 : 0);
            std::fprintf(f, "Turbo=%d\n", r.tuning.turbo ? 1 : 0);
            std::fprintf(f, "Horn=%u\n", r.tuning.hornType);
            std::fprintf(f, "Plate=%s\n", r.tuning.plateText);
            std::fprintf(f, "WearEngine=%.2f\n", r.wear.engine);
            std::fprintf(f, "WearTrans=%.2f\n", r.wear.transmission);
            std::fprintf(f, "WearBrakes=%.2f\n", r.wear.brakes);
            std::fprintf(f, "WearTires=%.2f\n", r.wear.tires);
            std::fprintf(f, "WearSusp=%.2f\n", r.wear.suspension);
            std::fprintf(f, "WearChassis=%.2f\n\n", r.wear.chassis);
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
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        modlog::Info("[Storage] No saved garage file for slot %d (%s) -> starting fresh", slotIndex, path.c_str());
        return false;
    }

    const char* p = path.c_str();
    for (int i = 0; i < mod::MAX_OWNED_VEHICLES; ++i) {
        char sec[32];
        std::snprintf(sec, sizeof(sec), "Slot%d", i);
        if (GetPrivateProfileIntA(sec, "Occupied", 0, p) == 0) continue;

        auto& v = slots[i];
        v.occupied  = true;
        v.destroyed = GetPrivateProfileIntA(sec, "Destroyed", 0, p) != 0;
        v.modelId   = GetPrivateProfileIntA(sec, "ModelId", 0, p);

        char buf[256] = {};
        GetPrivateProfileStringA(sec, "NameHex", "", buf, sizeof(buf), p);
        v.displayNameGame = HexDecode(buf);

        v.visuals.primaryColor   = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Color1", 0, p));
        v.visuals.secondaryColor = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Color2", 0, p));
        v.visuals.paintjob       = static_cast<int8_t>(GetPrivateProfileIntA(sec, "Paintjob", -1, p));

        GetPrivateProfileStringA(sec, "Upgrades", "", buf, sizeof(buf), p);
        {
            char* cur = buf;
            for (int u = 0; u < 15 && cur && *cur; ++u) {
                char* comma = std::strchr(cur, ',');
                if (comma) *comma = '\0';
                v.visuals.upgrades[u] = static_cast<int16_t>(std::atoi(cur));
                cur = comma ? (comma + 1) : nullptr;
            }
        }

        v.tuning.engineLevel       = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Engine", 0, p));
        v.tuning.transmissionLevel = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Transmission", 0, p));
        v.tuning.suspensionLevel   = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Suspension", 0, p));
        v.tuning.brakesLevel       = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Brakes", 0, p));
        v.tuning.tiresGripLevel    = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "TiresGrip", 0, p));
        v.tuning.armorLevel        = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Armor", 0, p));
        v.tuning.bulletproofTires  = GetPrivateProfileIntA(sec, "Bulletproof", 0, p) != 0;
        v.tuning.turbo             = GetPrivateProfileIntA(sec, "Turbo", 0, p) != 0;
        v.tuning.xenonLights       = GetPrivateProfileIntA(sec, "Xenon", 0, p) != 0;
        v.tuning.neonColor         = static_cast<mod::NeonColor>(GetPrivateProfileIntA(sec, "Neon", 0, p));
        v.tuning.hornType          = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Horn", 0, p));

        GetPrivateProfileStringA(sec, "Plate", "", buf, sizeof(buf), p);
        {
            size_t len = std::strlen(buf);
            if (len > 8) len = 8;
            std::memcpy(v.tuning.plateText, buf, len);
            v.tuning.plateText[len] = '\0';
        }

        auto readFloat = [&](const char* key) -> float {
            char fb[32] = {};
            GetPrivateProfileStringA(sec, key, "0.0", fb, sizeof(fb), p);
            return static_cast<float>(std::atof(fb));
        };
        v.wear.engine       = readFloat("WearEngine");
        v.wear.transmission = readFloat("WearTrans");
        v.wear.brakes       = readFloat("WearBrakes");
        v.wear.tires        = readFloat("WearTires");
        v.wear.suspension   = readFloat("WearSusp");
        v.wear.chassis      = readFloat("WearChassis");
        v.wear.ClampAll();
    }

    if (safehouseCars) {
        int shCount = std::clamp(static_cast<int>(GetPrivateProfileIntA("SafehouseMeta", "Count", 0, p)),
                                 0, mod::MAX_SAFEHOUSE_CARS);
        for (int i = 0; i < shCount; ++i) {
            char sec[32];
            std::snprintf(sec, sizeof(sec), "Safehouse%d", i);
            int mid = GetPrivateProfileIntA(sec, "ModelId", 0, p);
            int gid = GetPrivateProfileIntA(sec, "GarageId", -1, p);
            if (mid < 400 || gid < 0) continue;

            auto& r = (*safehouseCars)[i];
            r.occupied        = true;
            r.hideoutGarageId = gid;
            r.personalSlotIdx = GetPrivateProfileIntA(sec, "PersonalSlot", -1, p);
            r.modelId         = mid;
            r.visuals.primaryColor   = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Color1", 0, p));
            r.visuals.secondaryColor = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Color2", 0, p));
            r.visuals.paintjob       = static_cast<int8_t>(GetPrivateProfileIntA(sec, "Paintjob", -1, p));

            char buf[256] = {};
            GetPrivateProfileStringA(sec, "Upgrades", "", buf, sizeof(buf), p);
            {
                char* cur = buf;
                for (int u = 0; u < 15 && cur && *cur; ++u) {
                    char* comma = std::strchr(cur, ',');
                    if (comma) *comma = '\0';
                    r.visuals.upgrades[u] = static_cast<int16_t>(std::atoi(cur));
                    cur = comma ? (comma + 1) : nullptr;
                }
            }
            r.tuning.engineLevel       = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Engine", 0, p));
            r.tuning.transmissionLevel = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Transmission", 0, p));
            r.tuning.suspensionLevel   = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Suspension", 0, p));
            r.tuning.brakesLevel       = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Brakes", 0, p));
            r.tuning.tiresGripLevel    = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "TiresGrip", 0, p));
            r.tuning.armorLevel        = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Armor", 0, p));
            r.tuning.bulletproofTires  = GetPrivateProfileIntA(sec, "Bulletproof", 0, p) != 0;
            r.tuning.turbo             = GetPrivateProfileIntA(sec, "Turbo", 0, p) != 0;
            r.tuning.hornType          = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Horn", 0, p));

            GetPrivateProfileStringA(sec, "Plate", "", buf, sizeof(buf), p);
            {
                size_t len = std::strlen(buf);
                if (len > 8) len = 8;
                std::memcpy(r.tuning.plateText, buf, len);
                r.tuning.plateText[len] = '\0';
            }

            auto readFloat = [&](const char* key) -> float {
                char fb[32] = {};
                GetPrivateProfileStringA(sec, key, "0.0", fb, sizeof(fb), p);
                return static_cast<float>(std::atof(fb));
            };
            r.wear.engine       = readFloat("WearEngine");
            r.wear.transmission = readFloat("WearTrans");
            r.wear.brakes       = readFloat("WearBrakes");
            r.wear.tires        = readFloat("WearTires");
            r.wear.suspension   = readFloat("WearSusp");
            r.wear.chassis      = readFloat("WearChassis");
            r.wear.ClampAll();
        }
    }

    modlog::Info("[Storage] Loaded garage from %s", path.c_str());
    return true;
}

} // namespace storage
