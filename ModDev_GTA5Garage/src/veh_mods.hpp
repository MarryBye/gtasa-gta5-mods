#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "log.hpp"
#include "sa_sdk.hpp"
#include "ui_menu.hpp"

namespace vehmods {

enum class ModCat : uint8_t {
    Spoiler = 0,
    FrontBumper,
    RearBumper,
    SideSkirt,
    Exhaust,
    Roof,
    Hood,
    Lights,
    Wheels,
    Nitro,
    Hydraulics,
    Stereo,
    Other
};

struct ModPartInfo {
    int16_t     modelId = 0;
    std::string rawName;
    std::string gxtKey;
    int         price = 500;
    ModCat      cat   = ModCat::Other;
};

struct ModDatabase {
    bool loaded = false;
    std::unordered_map<std::string, int16_t>     nameToModId;
    std::unordered_map<int16_t, ModPartInfo>     modById;
    std::unordered_map<int32_t, std::string>     vehIdToName;
    std::unordered_map<std::string, std::vector<int16_t>> vehNameToMods;
};

inline ModDatabase& DB() {
    static ModDatabase db;
    return db;
}

inline std::string ToLowerTrim(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline ModCat ClassifyPart(const std::string& n, int16_t id) {
    if (id == 1087) return ModCat::Hydraulics;
    if (id == 1086) return ModCat::Stereo;
    if (id == 1008 || id == 1009 || id == 1010) return ModCat::Nitro;
    if (n.rfind("wheel_", 0) == 0) return ModCat::Wheels;
    if (n.rfind("spl_", 0) == 0)   return ModCat::Spoiler;
    if (n.rfind("fbmp_", 0) == 0 || n.rfind("bbb_", 0) == 0 || n.rfind("fbl_", 0) == 0) return ModCat::FrontBumper;
    if (n.rfind("rbmp_", 0) == 0 || n.rfind("bbr_", 0) == 0 || n.rfind("rbl_", 0) == 0) return ModCat::RearBumper;
    if (n.rfind("wg_l_", 0) == 0)  return ModCat::SideSkirt;
    if (n.rfind("wg_r_", 0) == 0)  return ModCat::Other; // Paired right skirt added automatically with wg_l_
    if (n.rfind("exh_", 0) == 0)   return ModCat::Exhaust;
    if (n.rfind("rf_", 0) == 0)    return ModCat::Roof;
    if (n.rfind("bnt_", 0) == 0 || n.rfind("bntl_", 0) == 0) return ModCat::Hood;
    if (n.rfind("bntr_", 0) == 0)  return ModCat::Other; // Paired right vent
    if (n.rfind("lgt_", 0) == 0)   return ModCat::Lights;
    return ModCat::Other;
}

inline void EnsureLoaded() {
    auto& db = DB();
    if (db.loaded) return;
    db.loaded = true;

    const std::string& gDir = mod::Config().gameDir;

    // 1. Parse <gameDir>\data\maps\veh_mods\veh_mods.ide -> modelId <-> partName
    {
        std::ifstream f(gDir + "\\data\\maps\\veh_mods\\veh_mods.ide");
        std::string line;
        bool inObjs = false;
        while (std::getline(f, line)) {
            std::string t = ToLowerTrim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t == "objs") { inObjs = true; continue; }
            if (t == "end")  { inObjs = false; continue; }
            if (!inObjs) continue;
            std::stringstream ss(t);
            std::string sId, sName;
            if (std::getline(ss, sId, ',') && std::getline(ss, sName, ',')) {
                int id = std::atoi(sId.c_str());
                sName = ToLowerTrim(sName);
                if (id >= 1000 && id <= 1193 && !sName.empty()) {
                    ModPartInfo info;
                    info.modelId = static_cast<int16_t>(id);
                    info.rawName = sName;
                    info.cat     = ClassifyPart(sName, info.modelId);
                    info.price   = (info.cat == ModCat::Wheels) ? 1000 : 650;
                    db.nameToModId[sName] = info.modelId;
                    db.modById[info.modelId] = info;
                }
            }
        }
    }

    // Add Hydraulics (1087) & Stereo (1086) in case they weren't in veh_mods.ide
    if (db.modById.find(1087) == db.modById.end()) {
        db.modById[1087] = {1087, "hydralics", "HYDRAU", 1500, ModCat::Hydraulics};
        db.nameToModId["hydralics"] = 1087;
    }
    if (db.modById.find(1086) == db.modById.end()) {
        db.modById[1086] = {1086, "stereo", "STEREO", 500, ModCat::Stereo};
        db.nameToModId["stereo"] = 1086;
    }

    // 2. Parse <gameDir>\data\shopping.dat CarMods section -> exact GXT keys & vanilla prices!
    {
        std::ifstream f(gDir + "\\data\\shopping.dat");
        std::string line;
        bool inCarMods = false;
        while (std::getline(f, line)) {
            std::string trimmed = line;
            size_t hash = trimmed.find('#');
            if (hash != std::string::npos) trimmed = trimmed.substr(0, hash);
            std::string low = ToLowerTrim(trimmed);
            if (low.empty()) continue;
            if (low == "section carmods") { inCarMods = true; continue; }
            if (inCarMods && low == "end") { inCarMods = false; break; }
            if (!inCarMods) continue;

            std::stringstream ss(trimmed);
            std::string name, nametag, s1, v1, s2, v2;
            int price = 500;
            if (ss >> name >> nametag >> s1 >> v1 >> s2 >> v2 >> price) {
                std::string lname = ToLowerTrim(name);
                auto it = db.nameToModId.find(lname);
                if (it != db.nameToModId.end()) {
                    auto& info = db.modById[it->second];
                    info.gxtKey = nametag;
                    if (price > 0) info.price = price;
                }
            }
        }
    }

    // 3. Parse <gameDir>\data\vehicles.ide -> modelId -> internal vehicle name
    {
        std::ifstream f(gDir + "\\data\\vehicles.ide");
        std::string line;
        bool inCars = false;
        while (std::getline(f, line)) {
            std::string t = ToLowerTrim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t == "cars") { inCars = true; continue; }
            if (t == "end")  { inCars = false; continue; }
            if (!inCars) continue;
            std::stringstream ss(t);
            std::string sId, sName;
            if (std::getline(ss, sId, ',') && std::getline(ss, sName, ',')) {
                int id = std::atoi(sId.c_str());
                sName = ToLowerTrim(sName);
                if (id >= 400 && id <= 611 && !sName.empty()) {
                    db.vehIdToName[id] = sName;
                }
            }
        }
    }

    // 4. Parse <gameDir>\data\carmods.dat -> vehicleName -> compatible mod model IDs
    {
        std::ifstream f(gDir + "\\data\\carmods.dat");
        std::string line;
        bool inMods = false;
        while (std::getline(f, line)) {
            std::string t = ToLowerTrim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t == "mods") { inMods = true; continue; }
            if (t == "end")  { inMods = false; continue; }
            if (!inMods) continue;
            std::stringstream ss(t);
            std::string vehName;
            if (!std::getline(ss, vehName, ',')) continue;
            vehName = ToLowerTrim(vehName);
            std::string part;
            auto& list = db.vehNameToMods[vehName];
            while (std::getline(ss, part, ',')) {
                part = ToLowerTrim(part);
                auto it = db.nameToModId.find(part);
                if (it != db.nameToModId.end()) {
                    list.push_back(it->second);
                }
            }
        }
    }

    modlog::Info("[VehMods] Loaded %zu upgrade parts, %zu vehicle mappings from %s\\data",
                 db.modById.size(), db.vehNameToMods.size(), gDir.c_str());
}

// Get localized Russian/English display name for an upgrade modelId (uses GXT directly!)
inline std::string GetPartDisplayName(int16_t modId) {
    EnsureLoaded();
    if (modId == 1009) return ui::Loc("Нитро 2x", "2x Nitro");
    if (modId == 1008) return ui::Loc("Нитро 5x", "5x Nitro");
    if (modId == 1010) return ui::Loc("Нитро 10x", "10x Nitro");
    if (modId == 1087) return ui::Loc("Гидравлика", "Hydraulics");
    if (modId == 1086) return ui::Loc("Аудиосистема (Сабвуфер)", "Stereo / Bass Boost");

    auto it = DB().modById.find(modId);
    if (it != DB().modById.end()) {
        if (!it->second.gxtKey.empty()) {
            const char* txt = sa::TextGet(it->second.gxtKey.c_str());
            if (txt && txt[0] != '\0') return std::string(txt);
        }
        return it->second.rawName;
    }
    return "Part #" + std::to_string(modId);
}

inline int GetPartPrice(int16_t modId) {
    EnsureLoaded();
    if (modId == 1009) return 200;
    if (modId == 1008) return 500;
    if (modId == 1010) return 1000;
    if (modId == 1087) return 1500;
    if (modId == 1086) return 500;
    auto it = DB().modById.find(modId);
    return (it != DB().modById.end()) ? it->second.price : 650;
}

// All 17 GTA SA wheels (compatible with EVERY 4-wheel car in the game!)
inline const std::vector<int16_t>& AllWheelIds() {
    static const std::vector<int16_t> kWheels = {
        1025, 1073, 1074, 1075, 1076, 1077, 1078, 1079,
        1080, 1081, 1082, 1083, 1084, 1085, 1096, 1097, 1098
    };
    return kWheels;
}

// Collect compatible parts for a given vehicle & category:
// Combines exact carmods.dat parts + universal TransFender parts where the car's 3D clump has the attachment dummy!
inline std::vector<int16_t> GetPartsForVehicle(sa::CVehicle* veh, ModCat cat) {
    EnsureLoaded();
    std::vector<int16_t> out;
    if (!veh || !veh->IsFourWheelLike()) return out;

    if (cat == ModCat::Wheels)     return AllWheelIds();
    if (cat == ModCat::Nitro)      return {1009, 1008, 1010};
    if (cat == ModCat::Hydraulics) return {1087};
    if (cat == ModCat::Stereo)     return {1086};

    auto addUnique = [&](int16_t id) {
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };

    // 1. Exact parts listed in carmods.dat for this vehicle model (works in ANY mod shop!)
    const auto& db = DB();
    auto vIt = db.vehIdToName.find(veh->GetModelIndex());
    if (vIt != db.vehIdToName.end()) {
        auto mIt = db.vehNameToMods.find(vIt->second);
        if (mIt != db.vehNameToMods.end()) {
            for (int16_t modId : mIt->second) {
                auto pIt = db.modById.find(modId);
                if (pIt != db.modById.end() && pIt->second.cat == cat) {
                    addUnique(modId);
                }
            }
        }
    }

    // 2. Universal TransFender parts for EVERY 4-wheel car (including Bullet, Infernus, Turismo, Banshee, Tahoma)!
    // EnsureUniversalUpgradeMounts populates clean chassis-fitted mount points if the car's .dff lacked ug_* dummies!
    if (out.empty()) {
        sa::EnsureUniversalUpgradeMounts(veh);
        if (cat == ModCat::Spoiler) {
            for (int16_t id : {1000, 1001, 1002, 1003, 1014, 1015, 1016, 1023}) addUnique(id);
        } else if (cat == ModCat::Roof) {
            addUnique(1006);
        } else if (cat == ModCat::Hood) {
            for (int16_t id : {1004, 1005, 1011, 1012}) addUnique(id);
        } else if (cat == ModCat::Lights) {
            for (int16_t id : {1013, 1024}) addUnique(id);
        } else if (cat == ModCat::SideSkirt && sa::VehicleHasDummy(veh, "ug_wing_right")) {
            addUnique(1007);
        } else if (cat == ModCat::Exhaust && (sa::VehicleHasDummy(veh, "exhaust") || sa::VehicleHasDummy(veh, "exhaust_ok"))) {
            for (int16_t id : {1018, 1019, 1020, 1021, 1022}) addUnique(id);
        }
    }

    return out;
}

// Check if a vehicle currently has a given upgrade ID mounted in any of its 15 upgrade slots
inline bool VehicleHasUpgradeId(const sa::CVehicle* veh, int16_t modId) {
    if (!veh || modId < 1000) return false;
    const int16_t* u = veh->Upgrades();
    for (int i = 0; i < 15; ++i) {
        if (u[i] == modId) return true;
    }
    return false;
}

// Find currently installed upgrade ID belonging to a specific ModCat (or -1 if none)
inline int16_t FindInstalledUpgradeInCat(const sa::CVehicle* veh, ModCat cat) {
    if (!veh) return -1;
    EnsureLoaded();
    const int16_t* u = veh->Upgrades();
    for (int i = 0; i < 15; ++i) {
        if (u[i] >= 1000) {
            auto it = DB().modById.find(u[i]);
            if (it != DB().modById.end() && it->second.cat == cat) {
                return u[i];
            }
        }
    }
    return -1;
}

inline void InstallUpgradeLive(sa::CVehicle* veh, int16_t modId) {
    if (!sa::IsVehicleValid(veh) || modId < 1000) return;
    sa::EnsureUniversalUpgradeMounts(veh);
    int32_t h = sa::GetVehicleHandle(veh);
    if (h <= 0) return;
    sa::ScriptCmd(0x06E9).Int(modId).Run(); // REQUEST_VEHICLE_MOD
    sa::ScriptCmd(0x038B).Run();            // LOAD_ALL_MODELS_NOW
    int32_t outDummy = 0;
    sa::ScriptCmd(0x06E7).Int(h).Int(modId).Out(outDummy).Run(); // ADD_VEHICLE_MOD
    sa::ScriptCmd(0x06EB).Int(modId).Run(); // MARK_VEHICLE_MOD_AS_NO_LONGER_NEEDED
}

inline void RemoveUpgradeLive(sa::CVehicle* veh, int16_t modId) {
    if (!sa::IsVehicleValid(veh) || modId < 1000) return;
    int32_t h = sa::GetVehicleHandle(veh);
    if (h <= 0) return;
    sa::ScriptCmd(0x06E8).Int(h).Int(modId).Run(); // REMOVE_VEHICLE_MOD
}

} // namespace vehmods
