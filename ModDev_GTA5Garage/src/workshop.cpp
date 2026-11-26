#include "workshop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "audio.hpp"
#include "config.hpp"
#include "garage.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "tuning_physics.hpp"
#include "ui_menu.hpp"
#include "veh_mods.hpp"

namespace workshop {

namespace {

// Verified 5 Mod Shop IPL bounds + exact built-in Rockstar Mod Shop Interior & Exterior Exit coordinates!
struct ModShopGarage {
    const char* name;
    const char* labelRus;
    const char* labelEng;
    float minX, maxX;
    float minY, maxY;
    float minZ, maxZ;
    float cx, cy, cz;
    int32_t interiorId; // 1 = TransFender, 2 = Loco Low Co., 3 = Wheel Arch Angels
    float   intX, intY, intZ, intHeading;
    float   exitX, exitY, exitZ, exitHeading;
};

constexpr ModShopGarage kModShops[5] = {
    {"BODLAWN", "TransFender", "TransFender",
     1037.0f, 1069.5f, -1027.0f, -994.0f, 30.0f, 40.0f, 1041.8f, -1018.0f, 32.1f,
     1, 617.536f, -1.990f, 999.980f, 90.0f,
     1041.8f, -1034.8f, 32.1f, 180.0f},
    {"MODLAST", "Loco Low Co.", "Loco Low Co.",
     2639.5f, 2650.5f, -2051.5f, -2038.0f, 11.5f, 17.5f, 2644.9f, -2044.8f, 13.5f,
     2, 616.783f, -74.815f, 997.014f, 90.0f,
     2644.9f, -2031.0f, 13.55f, 0.0f},
    {"MDSSFSE", "TransFender", "TransFender",
     -1942.5f, -1929.5f, 238.5f, 253.0f, 32.5f, 39.5f, -1935.8f, 245.6f, 34.4f,
     1, 617.536f, -1.990f, 999.980f, 90.0f,
     -1935.8f, 230.8f, 34.4f, 180.0f},
    {"MDS1SFS", "Wheel Arch Angels", "Wheel Arch Angels",
     -2730.0f, -2715.0f, 211.0f, 224.0f, 2.5f, 9.5f, -2722.4f, 217.5f, 4.5f,
     3, 615.286f, -124.239f, 996.995f, 90.0f,
     -2707.5f, 217.5f, 4.4f, 270.0f},
    {"VECMOD", "TransFender", "TransFender",
     2381.0f, 2392.5f, 1042.5f, 1061.0f, 8.8f, 15.0f, 2386.7f, 1051.9f, 10.8f,
     1, 617.536f, -1.990f, 999.980f, 90.0f,
     2386.7f, 1034.8f, 10.8f, 180.0f},
};

enum class CategoryId {
    VanillaItem = 0,
    // Standalone / Universal Visual Tuning
    Repair,
    Maintenance, // ETS 2-style Per-Part Mechanical Wear & Overhaul!
    Paintjob,
    PaintPrimary,
    PaintSecondary,
    Wheels,
    Spoilers,
    FrontBumper,
    RearBumper,
    SideSkirt,
    Exhaust,
    Roof,
    Hood,
    AuxLights,
    Hydraulics,
    Nitro,
    Stereo,
    // GTA 5 Performance & Utility Tuning
    Engine,
    Transmission,
    Suspension,
    Brakes,
    Tires,
    Turbo,
    Armor,
    Plate,
    Horn,
    Registration
};

struct MainEntry {
    CategoryId  cat;
    int         vanillaRow = -1;
    std::string labelGame;
    std::string rightGame;
};

struct SubOption {
    std::string labelGame;
    int         price     = 0;
    bool        installed = false;
    int         value     = 0; // Level, Color ID, Upgrade Model ID, or special action code
};

constexpr const char* kPlatePresets[7] = {
    "GROVE ST",
    "SAN ANDR",
    "LS CUSTM",
    "TURBO V8",
    "OUTLAW",
    "SPEED 01",
    "MY RIDE"
};

using FnMenuProcess = void(__cdecl*)(int8_t);
FnMenuProcess g_origMenuProcess = nullptr;

// Session state
bool          g_vanillaHookMode   = false;
bool          g_vanillaEverOpened = false;
bool          g_standaloneMode    = false;
int           g_standaloneShopIdx = -1;
sa::CVehicle* g_workshopVeh       = nullptr;

int           g_insideShopIdx     = -1;
uint32_t      g_insideStartMs     = 0;
uint32_t      g_standaloneEnteredMs = 0;
uint32_t      g_standaloneCooldownUntilMs = 0;
bool          g_waitLeaveBox      = false;
int           g_doorBlockedShopIdx = -1;
bool          g_doorOpened[5]     = {false, false, false, false, false};
bool          g_workshopFrozen    = false;
uint32_t      g_workshopSettleUntilMs = 0;

// Smart Camera Orbit + Zoom + Part Auto-Focus
float         g_camOrbitAngle     = 2.46f;
float         g_camDist           = 3.80f;
float         g_camHeight         = 1.20f;
float         g_camLookZ          = 0.28f;
float         g_camLookFwd        = 0.0f;
float         g_camTargetAngle    = 2.46f;
float         g_camTargetDist     = 3.80f;
float         g_camTargetHeight   = 1.20f;
float         g_camTargetLookZ    = 0.28f;
float         g_camTargetLookFwd  = 0.0f;
bool          g_camAutoLerp       = false;

// Menu navigation state
bool          g_inSubmenu         = false;
bool          g_typingPlate       = false;
std::string   g_plateInputBuf;
CategoryId    g_activeSubCat      = CategoryId::Engine;
int           g_mainCursor        = 0;
int           g_subCursor         = 0;
int           g_vanillaRowCount   = 0;
bool          g_forwardVanillaAccept = false;
bool          g_forwardVanillaExit   = false;
int           g_forwardVanillaRow    = 0;

// Preview snapshots (so scrolling previews live and Backspace reverts cleanly!)
mod::PerformanceTuning g_savedTuningBeforeSub{};
uint8_t       g_savedCol1         = 0;
uint8_t       g_savedCol2         = 0;
int8_t        g_savedPaintjob     = -1;
int16_t       g_savedVisualModId  = -1; // Previously installed mod in the active visual ModCat
int16_t       g_previewVisualModId = -1;
bool          g_previewModified   = false;

std::vector<MainEntry> g_mainEntries;
std::vector<SubOption> g_subOptions;

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

sa::tMenuPanel* GetVanillaCarmodMainPanel() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (p->m_nType != 0) return nullptr;
    if (p->m_nNumColumns != 1) return nullptr;
    if (std::strncmp(p->m_acTitle, "UPGRADE", 7) != 0) return nullptr;
    if (p->m_nNumRows == 0 || p->m_nNumRows > 12) return nullptr;
    return p;
}

bool IsAnyVanillaCarmodPanelOpen() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return false;
    return std::strncmp(p->m_acTitle, "UPGRADE", 7) == 0;
}

std::string FormatPrice(int price) {
    if (price <= 0) return ui::Loc("Беспл.", "Free");
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d", price);
    return std::string(buf);
}

mod::PerformanceTuning GetCurrentVehTuning(sa::CVehicle* v) {
    if (const auto* t = tuning::GetLiveTuning(v)) return *t;
    return mod::PerformanceTuning{};
}

bool IsVisualModCategory(CategoryId cat, vehmods::ModCat& outMc) {
    switch (cat) {
        case CategoryId::Wheels:      outMc = vehmods::ModCat::Wheels;      return true;
        case CategoryId::Spoilers:    outMc = vehmods::ModCat::Spoiler;     return true;
        case CategoryId::FrontBumper: outMc = vehmods::ModCat::FrontBumper; return true;
        case CategoryId::RearBumper:  outMc = vehmods::ModCat::RearBumper;  return true;
        case CategoryId::SideSkirt:   outMc = vehmods::ModCat::SideSkirt;   return true;
        case CategoryId::Exhaust:     outMc = vehmods::ModCat::Exhaust;     return true;
        case CategoryId::Roof:        outMc = vehmods::ModCat::Roof;        return true;
        case CategoryId::Hood:        outMc = vehmods::ModCat::Hood;        return true;
        case CategoryId::AuxLights:   outMc = vehmods::ModCat::Lights;      return true;
        case CategoryId::Nitro:       outMc = vehmods::ModCat::Nitro;       return true;
        case CategoryId::Hydraulics:  outMc = vehmods::ModCat::Hydraulics;  return true;
        case CategoryId::Stereo:      outMc = vehmods::ModCat::Stereo;      return true;
        default: return false;
    }
}

// ============================================================================
// Build Main Workshop Menu Entries
// ============================================================================
void RebuildMainEntries(sa::tMenuPanel* vanillaPanel, sa::CVehicle* veh) {
    g_mainEntries.clear();
    g_vanillaRowCount = 0;

    if (vanillaPanel) {
        g_vanillaRowCount = std::min<int>(vanillaPanel->m_nNumRows, 12);
        for (int i = 0; i < g_vanillaRowCount; ++i) {
            char key[12] = {};
            std::memcpy(key, vanillaPanel->m_aaacRowItems[0][i], 10);
            key[10] = '\0';
            const char* txt = (key[0] != '\0') ? sa::TextGet(key) : "";
            std::string label = (txt && txt[0]) ? std::string(txt) : std::string(key);
            g_mainEntries.push_back({CategoryId::VanillaItem, i, label, ""});
        }
    }

    if (!veh) return;
    const bool isFourWheel = veh->IsFourWheelLike();
    const auto cur = GetCurrentVehTuning(veh);

    // ETS 2-style Mechanical Wear & Maintenance entry (shown in BOTH Mode A & Mode B!)
    if (mod::Config().enableWearSystem) {
        auto wear = tuning::GetLiveWear(veh);
        int avgW  = static_cast<int>(std::round(wear.Average()));
        char wbuf[32];
        if (avgW > 0) std::snprintf(wbuf, sizeof(wbuf), "%s %d%%", ui::Loc("Износ", "Wear").c_str(), avgW);
        else          std::snprintf(wbuf, sizeof(wbuf), "%s", ui::Loc("Идеал 0%", "Mint 0%").c_str());
        g_mainEntries.push_back({CategoryId::Maintenance, -1,
            ui::Loc("ТО и Состояние узлов", "Maintenance & Wear"), wbuf});
    }

    // In Standalone Mode B: expose ALL visual tuning (Wheels, Paintjobs, Colors, Spoilers, Bumpers, Skirts, Exhaust, Roof, Hood, Hydraulics, Nitro, Stereo!)
    if (!vanillaPanel) {
        if (veh->Health() < 995.0f) {
            g_mainEntries.push_back({CategoryId::Repair, -1,
                ui::Loc("Починить транспорт", "Repair Vehicle"),
                FormatPrice(mod::Config().priceRepair)});
        }

        // Paintjobs (Vinyls) if vehicle model has any
        int32_t h = sa::GetVehicleHandle(veh);
        int32_t numPj = 0;
        if (h > 0) sa::ScriptCmd(0x06EC).Int(h).Out(numPj).Run();
        if (numPj > 0) {
            g_mainEntries.push_back({CategoryId::Paintjob, -1,
                ui::Loc("Винилы (Покраска)", "Paintjobs"),
                std::to_string(numPj) + " " + ui::Loc("шт.", "avail")});
        }

        g_mainEntries.push_back({CategoryId::PaintPrimary, -1,
            ui::Loc("Основной цвет", "Primary Color"),
            std::to_string(veh->PrimaryColor())});
        g_mainEntries.push_back({CategoryId::PaintSecondary, -1,
            ui::Loc("Доп. цвет", "Secondary Color"),
            std::to_string(veh->SecondaryColor())});

        if (isFourWheel) {
            // Wheels (ALL 17 rims for EVERY 4-wheel car!)
            int16_t curW = vehmods::FindInstalledUpgradeInCat(veh, vehmods::ModCat::Wheels);
            g_mainEntries.push_back({CategoryId::Wheels, -1,
                ui::Loc("Диски (Колёса)", "Wheels"),
                curW >= 1000 ? ui::Loc("Уст.", "Custom") : ui::Loc("Сток", "Stock")});

            // Body parts (auto-detected from carmods.dat + 3D dummy nodes!)
            auto addBodyCat = [&](CategoryId cid, vehmods::ModCat mc, const char* rLbl, const char* eLbl) {
                auto parts = vehmods::GetPartsForVehicle(veh, mc);
                if (!parts.empty()) {
                    int16_t curM = vehmods::FindInstalledUpgradeInCat(veh, mc);
                    g_mainEntries.push_back({cid, -1, ui::Loc(rLbl, eLbl),
                        curM >= 1000 ? ui::Loc("Уст.", "On") : std::to_string(parts.size())});
                }
            };
            addBodyCat(CategoryId::Spoilers,    vehmods::ModCat::Spoiler,     "Спойлеры",        "Spoilers");
            addBodyCat(CategoryId::FrontBumper, vehmods::ModCat::FrontBumper, "Передний бампер", "Front Bumper");
            addBodyCat(CategoryId::RearBumper,  vehmods::ModCat::RearBumper,  "Задний бампер",   "Rear Bumper");
            addBodyCat(CategoryId::SideSkirt,   vehmods::ModCat::SideSkirt,   "Пороги",          "Side Skirts");
            addBodyCat(CategoryId::Exhaust,     vehmods::ModCat::Exhaust,     "Выхлопная труба", "Exhaust");
            addBodyCat(CategoryId::Roof,        vehmods::ModCat::Roof,        "Крыша",           "Roof");
            addBodyCat(CategoryId::Hood,        vehmods::ModCat::Hood,        "Капот",           "Hood");
            addBodyCat(CategoryId::AuxLights,   vehmods::ModCat::Lights,      "Доп. фары",       "Fog Lights");

            // Hydraulics, Nitro, Stereo on EVERY 4-wheel car!
            bool hasHyd = vehmods::VehicleHasUpgradeId(veh, 1087);
            g_mainEntries.push_back({CategoryId::Hydraulics, -1,
                ui::Loc("Гидравлика", "Hydraulics"),
                hasHyd ? ui::Loc("Уст.", "Owned") : FormatPrice(1500)});

            int16_t curN = vehmods::FindInstalledUpgradeInCat(veh, vehmods::ModCat::Nitro);
            g_mainEntries.push_back({CategoryId::Nitro, -1,
                ui::Loc("Нитро", "Nitro"),
                curN >= 1000 ? ui::Loc("Уст.", "Owned") : ui::Loc("Нет", "None")});

            bool hasStereo = vehmods::VehicleHasUpgradeId(veh, 1086);
            g_mainEntries.push_back({CategoryId::Stereo, -1,
                ui::Loc("Аудиосистема", "Car Stereo"),
                hasStereo ? ui::Loc("Уст.", "Owned") : FormatPrice(500)});
        }
    }

    auto lvlBadge = [](int lv, int maxLv) -> std::string {
        if (lv <= 0) return ui::Loc("Сток", "Stock");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d/%d", lv, maxLv);
        return std::string(buf);
    };

    // GTA 5 Performance & Utility Tuning (ALL vehicles, appropriately filtered!)
    g_mainEntries.push_back({CategoryId::Engine, -1,
        ui::Loc("Двигатель", "Engine"), lvlBadge(cur.engineLevel, 4)});

    g_mainEntries.push_back({CategoryId::Transmission, -1,
        ui::Loc("Трансмиссия", "Transmission"), lvlBadge(cur.transmissionLevel, 3)});

    if (isFourWheel) {
        g_mainEntries.push_back({CategoryId::Suspension, -1,
            ui::Loc("Подвеска", "Suspension"), lvlBadge(cur.suspensionLevel, 4)});
    }

    g_mainEntries.push_back({CategoryId::Brakes, -1,
        ui::Loc("Тормоза", "Brakes"), lvlBadge(cur.brakesLevel, 3)});

    {
        std::string st = cur.bulletproofTires
            ? ui::Loc("Пулестойк.", "Bulletproof")
            : lvlBadge(cur.tiresGripLevel, 3);
        g_mainEntries.push_back({CategoryId::Tires, -1,
            ui::Loc("Шины", "Tires"), st});
    }

    g_mainEntries.push_back({CategoryId::Turbo, -1,
        ui::Loc("Турбонаддув", "Turbo"),
        cur.turbo ? ui::Loc("Уст.", "On") : ui::Loc("Нет", "None")});

    g_mainEntries.push_back({CategoryId::Armor, -1,
        ui::Loc("Кузов (Броня)", "Armor"),
        cur.armorLevel > 0 ? (std::to_string(cur.armorLevel * 20) + "%") : ui::Loc("0%", "0%")});

    g_mainEntries.push_back({CategoryId::Plate, -1,
        ui::Loc("Номерной знак", "License Plate"),
        cur.plateText[0] ? std::string(cur.plateText) : ui::Loc("Сток", "Stock")});

    g_mainEntries.push_back({CategoryId::Horn, -1,
        ui::Loc("Клаксон", "Horn"),
        cur.hornType > 0 ? (ui::Loc("Тип ", "Type ") + std::to_string(cur.hornType)) : ui::Loc("Сток", "Stock")});

    // Personal Vehicle Registration
    int ownedSlot = garage::FindSlotForLiveVehicle(veh);
    if (ownedSlot >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%d/10", ownedSlot + 1);
        g_mainEntries.push_back({CategoryId::Registration, -1,
            ui::Loc("Личный транспорт", "Personal Vehicle"), std::string(buf)});
    } else {
        g_mainEntries.push_back({CategoryId::Registration, -1,
            ui::Loc("Зарегистрировать авто", "Register Vehicle"),
            FormatPrice(mod::Config().priceRegister)});
    }

    if (g_mainCursor >= static_cast<int>(g_mainEntries.size())) {
        g_mainCursor = std::max(0, static_cast<int>(g_mainEntries.size()) - 1);
    }
}

std::string GetSubmenuTitle(CategoryId cat) {
    switch (cat) {
        case CategoryId::Maintenance:    return ui::Loc("ТО И ИЗНОС УЗЛОВ", "MAINTENANCE & WEAR");
        case CategoryId::Paintjob:       return ui::Loc("ВИНИЛЫ (ПОКРАСКА)", "PAINTJOBS");
        case CategoryId::PaintPrimary:   return ui::Loc("ОСНОВНОЙ ЦВЕТ", "PRIMARY COLOR");
        case CategoryId::PaintSecondary: return ui::Loc("ДОП. ЦВЕТ", "SECONDARY COLOR");
        case CategoryId::Wheels:         return ui::Loc("ДИСКИ (КОЛЁСА)", "WHEELS");
        case CategoryId::Spoilers:       return ui::Loc("СПОЙЛЕРЫ", "SPOILERS");
        case CategoryId::FrontBumper:    return ui::Loc("ПЕРЕДНИЙ БАМПЕР", "FRONT BUMPER");
        case CategoryId::RearBumper:     return ui::Loc("ЗАДНИЙ БАМПЕР", "REAR BUMPER");
        case CategoryId::SideSkirt:      return ui::Loc("ПОРОГИ", "SIDE SKIRTS");
        case CategoryId::Exhaust:        return ui::Loc("ВЫХЛОПНАЯ ТРУБА", "EXHAUST");
        case CategoryId::Roof:           return ui::Loc("КРЫША", "ROOF");
        case CategoryId::Hood:           return ui::Loc("КАПОТ", "HOOD");
        case CategoryId::AuxLights:      return ui::Loc("ДОП. ФАРЫ", "LIGHTS");
        case CategoryId::Hydraulics:     return ui::Loc("ГИДРАВЛИКА", "HYDRAULICS");
        case CategoryId::Nitro:          return ui::Loc("НИТРО", "NITRO");
        case CategoryId::Stereo:         return ui::Loc("АУДИОСИСТЕМА", "CAR STEREO");
        case CategoryId::Engine:         return ui::Loc("ДВИГАТЕЛЬ", "ENGINE");
        case CategoryId::Transmission:   return ui::Loc("ТРАНСМИССИЯ", "TRANSMISSION");
        case CategoryId::Suspension:     return ui::Loc("ПОДВЕСКА", "SUSPENSION");
        case CategoryId::Brakes:         return ui::Loc("ТОРМОЗА", "BRAKES");
        case CategoryId::Tires:          return ui::Loc("ШИНЫ", "TIRES");
        case CategoryId::Turbo:          return ui::Loc("ТУРБОНАДДУВ", "TURBO");
        case CategoryId::Armor:          return ui::Loc("КУЗОВ И БРОНЯ", "ARMOR");
        case CategoryId::Plate:          return ui::Loc("НОМЕРНОЙ ЗНАК", "LICENSE PLATE");
        case CategoryId::Horn:           return ui::Loc("КЛАКСОН", "HORN");
        case CategoryId::Registration:   return ui::Loc("РЕГИСТРАЦИЯ АВТО", "REGISTRATION");
        default:                         return ui::Loc("ТЮНИНГ", "UPGRADES");
    }
}

void RebuildSubOptions(CategoryId cat, sa::CVehicle* veh) {
    g_subOptions.clear();
    if (!veh) return;
    const auto cur = GetCurrentVehTuning(veh);
    const auto& cfg = mod::Config();

    vehmods::ModCat vmc{};
    if (IsVisualModCategory(cat, vmc)) {
        int16_t installedId = vehmods::FindInstalledUpgradeInCat(veh, vmc);
        // Stock / Remove option
        g_subOptions.push_back({ui::Loc("Сток (Убрать)", "Stock (Remove)"), 0, (installedId < 1000), -1});
        auto parts = vehmods::GetPartsForVehicle(veh, vmc);
        for (int16_t mid : parts) {
            g_subOptions.push_back({
                vehmods::GetPartDisplayName(mid),
                vehmods::GetPartPrice(mid),
                (installedId == mid),
                mid
            });
        }
        g_subCursor = 0;
        for (int i = 0; i < static_cast<int>(g_subOptions.size()); ++i) {
            if (g_subOptions[i].installed) { g_subCursor = i; break; }
        }
        return;
    }

    switch (cat) {
        case CategoryId::Maintenance: {
            auto w = tuning::GetLiveWear(veh);
            int cEng = mod::ComputePartServiceCost(w.engine,       cfg.maintBaseEngine,  cur, cfg.maintCostMult);
            int cTr  = mod::ComputePartServiceCost(w.transmission, cfg.maintBaseTrans,   cur, cfg.maintCostMult);
            int cBrk = mod::ComputePartServiceCost(w.brakes,       cfg.maintBaseBrakes,  cur, cfg.maintCostMult);
            int cTir = mod::ComputePartServiceCost(w.tires,        cfg.maintBaseTires,   cur, cfg.maintCostMult);
            int cSus = mod::ComputePartServiceCost(w.suspension,   cfg.maintBaseSusp,    cur, cfg.maintCostMult);
            int cCha = mod::ComputePartServiceCost(w.chassis,      cfg.maintBaseChassis, cur, cfg.maintCostMult);
            int cAllRaw = cEng + cTr + cBrk + cTir + cSus + cCha;
            int cAll = (cAllRaw > 0) ? std::max(200, static_cast<int>(std::round((cAllRaw * 0.90f) / 50.0f)) * 50) : 0;

            auto makePartLabel = [](const char* rus, const char* eng, float wp) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s (%d%%)", ui::Loc(rus, eng).c_str(), static_cast<int>(std::round(wp)));
                return std::string(buf);
            };

            g_subOptions.push_back({makePartLabel("Полное ТО (Всё)", "Full Overhaul (All)", w.Average()), cAll, cAll == 0, 100});
            g_subOptions.push_back({makePartLabel("Двигатель",       "Engine",       w.engine),       cEng, cEng == 0, 0});
            g_subOptions.push_back({makePartLabel("Трансмиссия",     "Transmission", w.transmission), cTr,  cTr  == 0, 1});
            g_subOptions.push_back({makePartLabel("Тормоза",         "Brakes",       w.brakes),       cBrk, cBrk == 0, 2});
            g_subOptions.push_back({makePartLabel("Шины",            "Tires",        w.tires),        cTir, cTir == 0, 3});
            g_subOptions.push_back({makePartLabel("Подвеска",        "Suspension",   w.suspension),   cSus, cSus == 0, 4});
            g_subOptions.push_back({makePartLabel("Кузов и Шасси",   "Chassis/Body", w.chassis),      cCha, cCha == 0, 5});
            g_subCursor = 0;
            return;
        }
        case CategoryId::Paintjob: {
            int32_t h = sa::GetVehicleHandle(veh);
            int32_t numPj = 0, curPj = -1;
            if (h > 0) {
                sa::ScriptCmd(0x06EC).Int(h).Out(numPj).Run();
                sa::ScriptCmd(0x0988).Int(h).Out(curPj).Run();
            }
            if (curPj > 2) curPj = -1;
            g_subOptions.push_back({ui::Loc("Без винила", "No Paintjob"), 0, curPj < 0, 3});
            for (int i = 0; i < numPj && i < 3; ++i) {
                char nm[48];
                std::snprintf(nm, sizeof(nm), "%s #%d", ui::Loc("Винил", "Paintjob").c_str(), i + 1);
                g_subOptions.push_back({nm, 500, curPj == i, i});
            }
            break;
        }
        case CategoryId::PaintPrimary:
        case CategoryId::PaintSecondary: {
            uint8_t curCol = (cat == CategoryId::PaintPrimary) ? veh->PrimaryColor() : veh->SecondaryColor();
            for (int c = 0; c <= 126; ++c) {
                char nm[32];
                std::snprintf(nm, sizeof(nm), "%s #%d", ui::Loc("Цвет", "Color").c_str(), c);
                g_subOptions.push_back({nm, cfg.pricePaint, (curCol == c), c});
            }
            g_subCursor = std::clamp<int>(curCol, 0, 126);
            return;
        }
        case CategoryId::Engine:
            g_subOptions.push_back({ui::Loc("Стоковый двигатель", "Stock Engine"), 0, cur.engineLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Улучшение EMS Ур. 1", "EMS Upgrade Lv. 1"), cfg.priceEngine[0], cur.engineLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Улучшение EMS Ур. 2", "EMS Upgrade Lv. 2"), cfg.priceEngine[1], cur.engineLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Улучшение EMS Ур. 3", "EMS Upgrade Lv. 3"), cfg.priceEngine[2], cur.engineLevel == 3, 3});
            g_subOptions.push_back({ui::Loc("Улучшение EMS Ур. 4", "EMS Upgrade Lv. 4"), cfg.priceEngine[3], cur.engineLevel == 4, 4});
            break;

        case CategoryId::Transmission:
            g_subOptions.push_back({ui::Loc("Стоковая коробка", "Stock Transmission"), 0, cur.transmissionLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Уличная трансмиссия", "Street Transmission"), cfg.priceTrans[0], cur.transmissionLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Спортивная трансмиссия", "Sport Transmission"), cfg.priceTrans[1], cur.transmissionLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Гоночная трансмиссия", "Race Transmission"), cfg.priceTrans[2], cur.transmissionLevel == 3, 3});
            break;

        case CategoryId::Suspension:
            g_subOptions.push_back({ui::Loc("Стоковая подвеска", "Stock Suspension"), 0, cur.suspensionLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Заниженная подвеска", "Lowered Suspension"), cfg.priceSusp[0], cur.suspensionLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Уличная подвеска", "Street Suspension"), cfg.priceSusp[1], cur.suspensionLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Спортивная подвеска", "Sport Suspension"), cfg.priceSusp[2], cur.suspensionLevel == 3, 3});
            g_subOptions.push_back({ui::Loc("Гоночная подвеска", "Competition Suspension"), cfg.priceSusp[3], cur.suspensionLevel == 4, 4});
            break;

        case CategoryId::Brakes:
            g_subOptions.push_back({ui::Loc("Стоковые тормоза", "Stock Brakes"), 0, cur.brakesLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Уличные тормоза", "Street Brakes"), cfg.priceBrakes[0], cur.brakesLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Спортивные тормоза", "Sport Brakes"), cfg.priceBrakes[1], cur.brakesLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Гоночные тормоза", "Race Brakes"), cfg.priceBrakes[2], cur.brakesLevel == 3, 3});
            break;

        case CategoryId::Tires:
            g_subOptions.push_back({ui::Loc("Сток (Сцепление)", "Stock Grip"), 0, cur.tiresGripLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Уличные шины (+Сцепл.)", "Street Tires (+Grip)"), cfg.priceTiresGrip[0], cur.tiresGripLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Спортивные шины (++Сцепл.)", "Sport Tires (++Grip)"), cfg.priceTiresGrip[1], cur.tiresGripLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Гоночные слики (Макс.)", "Race Slicks (Max Grip)"), cfg.priceTiresGrip[2], cur.tiresGripLevel == 3, 3});
            g_subOptions.push_back({ui::Loc("Пуленепробиваемые шины", "Bulletproof Tires"),
                cur.bulletproofTires ? 0 : cfg.priceBulletproof, cur.bulletproofTires, 100});
            break;

        case CategoryId::Turbo:
            g_subOptions.push_back({ui::Loc("Без турбонаддува", "None"), 0, !cur.turbo, 0});
            g_subOptions.push_back({ui::Loc("Турбо-тюнинг", "Turbo Tuning"), cfg.priceTurbo, cur.turbo, 1});
            break;

        case CategoryId::Armor:
            g_subOptions.push_back({ui::Loc("Без брони (0%)", "No Armor (0%)"), 0, cur.armorLevel == 0, 0});
            g_subOptions.push_back({ui::Loc("Усиление кузова 20%", "Armor Upgrade 20%"), cfg.priceArmor[0], cur.armorLevel == 1, 1});
            g_subOptions.push_back({ui::Loc("Усиление кузова 40%", "Armor Upgrade 40%"), cfg.priceArmor[1], cur.armorLevel == 2, 2});
            g_subOptions.push_back({ui::Loc("Усиление кузова 60%", "Armor Upgrade 60%"), cfg.priceArmor[2], cur.armorLevel == 3, 3});
            g_subOptions.push_back({ui::Loc("Усиление кузова 80%", "Armor Upgrade 80%"), cfg.priceArmor[3], cur.armorLevel == 4, 4});
            g_subOptions.push_back({ui::Loc("Усиление кузова 100%", "Armor Upgrade 100%"), cfg.priceArmor[4], cur.armorLevel == 5, 5});
            break;

        case CategoryId::Plate:
            // Interactive custom text entry RIGHT AT THE TOP!
            g_subOptions.push_back({ui::Loc(">> Ввести свой номер...", ">> Enter Custom Plate..."), cfg.pricePlate, false, -100});
            for (int i = 0; i < 7; ++i) {
                bool eq = (std::strcmp(cur.plateText, kPlatePresets[i]) == 0);
                g_subOptions.push_back({std::string(kPlatePresets[i]), cfg.pricePlate, eq, i});
            }
            break;

        case CategoryId::Horn:
            g_subOptions.push_back({ui::Loc("Стандартный клаксон", "Stock Horn"), 0, cur.hornType == 0, 0});
            g_subOptions.push_back({ui::Loc("Воздушный (Грузовой)", "Truck Airhorn"), cfg.priceHorn, cur.hornType == 1, 1});
            g_subOptions.push_back({ui::Loc("Спортивный сигнал", "Sport Dual-Tone"), cfg.priceHorn, cur.hornType == 2, 2});
            g_subOptions.push_back({ui::Loc("Классический ретро", "Classic Horn"), cfg.priceHorn, cur.hornType == 3, 3});
            g_subOptions.push_back({ui::Loc("Музыкальный сигнал", "Musical Horn"), cfg.priceHorn, cur.hornType == 4, 4});
            break;

        case CategoryId::Registration: {
            int ownedSlot = garage::FindSlotForLiveVehicle(veh);
            if (ownedSlot < 0) {
                int freeSlot = garage::FindFreeSlot();
                if (freeSlot >= 0) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s (#%d/10)",
                        ui::Loc("Зарегистрировать в слот", "Register in Slot").c_str(), freeSlot + 1);
                    g_subOptions.push_back({buf, cfg.priceRegister, false, 1});
                } else {
                    g_subOptions.push_back({ui::Loc("Гараж полон (10/10)", "Garage Full (10/10)"), 0, false, 0});
                }
            } else {
                g_subOptions.push_back({ui::Loc("Обновить конфиг в гараже", "Save Latest Tuning"), 0, true, 2});
                g_subOptions.push_back({ui::Loc("Снять с учёта (Удалить)", "Unregister Vehicle"), 0, false, 3});
            }
            break;
        }
        default:
            break;
    }

    g_subCursor = 0;
    for (int i = 0; i < static_cast<int>(g_subOptions.size()); ++i) {
        if (g_subOptions[i].installed) { g_subCursor = i; break; }
    }
}

// Compute hypothetical tuning for live Stats Panel preview when hovering a submenu item!
mod::PerformanceTuning BuildPreviewTuningForStats() {
    auto t = g_savedTuningBeforeSub;
    if (!g_inSubmenu || g_subOptions.empty()) {
        return g_workshopVeh ? GetCurrentVehTuning(g_workshopVeh) : t;
    }
    int val = g_subOptions[g_subCursor].value;
    switch (g_activeSubCat) {
        case CategoryId::Engine:       t.engineLevel       = static_cast<uint8_t>(val); break;
        case CategoryId::Transmission: t.transmissionLevel = static_cast<uint8_t>(val); break;
        case CategoryId::Suspension:   t.suspensionLevel   = static_cast<uint8_t>(val); break;
        case CategoryId::Brakes:       t.brakesLevel       = static_cast<uint8_t>(val); break;
        case CategoryId::Tires:
            if (val != 100) t.tiresGripLevel = static_cast<uint8_t>(val);
            break;
        case CategoryId::Turbo:        t.turbo             = (val != 0); break;
        case CategoryId::Armor:        t.armorLevel        = static_cast<uint8_t>(val); break;
        default: break;
    }
    return t;
}

mod::VehicleWear BuildPreviewWearForStats(sa::CVehicle* veh) {
    auto w = tuning::GetLiveWear(veh);
    if (!g_inSubmenu || g_activeSubCat != CategoryId::Maintenance || g_subOptions.empty()) return w;
    int val = g_subOptions[g_subCursor].value;
    switch (val) {
        case 100: w = mod::VehicleWear{}; break;
        case 0:   w.engine       = 0.0f; break;
        case 1:   w.transmission = 0.0f; break;
        case 2:   w.brakes       = 0.0f; break;
        case 3:   w.tires        = 0.0f; break;
        case 4:   w.suspension   = 0.0f; break;
        case 5:   w.chassis      = 0.0f; break;
        default: break;
    }
    return w;
}

void ApplySubPreview(sa::CVehicle* veh) {
    if (!veh || g_subOptions.empty()) return;
    int val = g_subOptions[g_subCursor].value;

    // Live visual mod preview (Wheels, Spoilers, Bumpers, Skirts, Exhaust, Roof, Hood, Nitro)
    vehmods::ModCat vmc{};
    if (IsVisualModCategory(g_activeSubCat, vmc)) {
        if (g_previewVisualModId >= 1000 && g_previewVisualModId != g_savedVisualModId) {
            vehmods::RemoveUpgradeLive(veh, g_previewVisualModId);
            g_previewVisualModId = -1;
        }
        if (val >= 1000) {
            vehmods::InstallUpgradeLive(veh, static_cast<int16_t>(val));
            g_previewVisualModId = static_cast<int16_t>(val);
        } else {
            int16_t curM = vehmods::FindInstalledUpgradeInCat(veh, vmc);
            if (curM >= 1000) vehmods::RemoveUpgradeLive(veh, curM);
            g_previewVisualModId = -1;
        }
        g_previewModified = true;
        return;
    }

    if (g_activeSubCat == CategoryId::Paintjob) {
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x06ED).Int(h).Int(val).Run();
        g_previewModified = true;
        return;
    }
    if (g_activeSubCat == CategoryId::PaintPrimary) {
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x0229).Int(h).Int(val).Int(g_savedCol2).Run();
        g_previewModified = true;
        return;
    }
    if (g_activeSubCat == CategoryId::PaintSecondary) {
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x0229).Int(h).Int(g_savedCol1).Int(val).Run();
        g_previewModified = true;
        return;
    }
    if (g_activeSubCat == CategoryId::Suspension) {
        auto preview = g_savedTuningBeforeSub;
        preview.suspensionLevel = static_cast<uint8_t>(val);
        tuning::ApplyTuning(veh, preview);
        if (g_standaloneMode) {
            int32_t h = sa::GetVehicleHandle(veh);
            if (h > 0) sa::ScriptCmd(0x0519).Int(h).Int(0).Run(); // Temporarily unfreeze so ride-height settles visibly!
            g_workshopFrozen = false;
            g_workshopSettleUntilMs = sa::GetTimeMs() + 260;
        }
        g_previewModified = true;
        return;
    }
    if (g_activeSubCat == CategoryId::Horn) {
        audio::UpdateCustomHorn(val, val > 0);
    }
}

void RevertSubPreview(sa::CVehicle* veh) {
    audio::UpdateCustomHorn(0, false);
    if (!g_previewModified || !sa::IsVehicleValid(veh)) {
        g_previewModified = false;
        g_previewVisualModId = -1;
        return;
    }

    vehmods::ModCat vmc{};
    if (IsVisualModCategory(g_activeSubCat, vmc)) {
        int16_t curM = vehmods::FindInstalledUpgradeInCat(veh, vmc);
        if (curM >= 1000 && curM != g_savedVisualModId) {
            vehmods::RemoveUpgradeLive(veh, curM);
        }
        if (g_savedVisualModId >= 1000 && !vehmods::VehicleHasUpgradeId(veh, g_savedVisualModId)) {
            vehmods::InstallUpgradeLive(veh, g_savedVisualModId);
        }
        g_previewVisualModId = -1;
    } else if (g_activeSubCat == CategoryId::Paintjob) {
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x06ED).Int(h).Int(g_savedPaintjob < 0 ? 3 : g_savedPaintjob).Run();
    } else if (g_activeSubCat == CategoryId::PaintPrimary || g_activeSubCat == CategoryId::PaintSecondary) {
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x0229).Int(h).Int(g_savedCol1).Int(g_savedCol2).Run();
    } else {
        tuning::ApplyTuning(veh, g_savedTuningBeforeSub);
    }
    g_previewModified = false;
}

void CommitCustomPlate(sa::CVehicle* veh, const std::string& plateStr) {
    if (!veh || plateStr.empty()) return;
    int cost = mod::Config().pricePlate;
    if (sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
        return;
    }

    sa::AddPlayerMoney(-cost);
    auto next = g_savedTuningBeforeSub;
    size_t len = std::min<size_t>(plateStr.size(), 8);
    std::memcpy(next.plateText, plateStr.c_str(), len);
    next.plateText[len] = '\0';

    // Update live plate texture IMMEDIATELY on the vehicle in the workshop!
    sa::ApplyLiveCustomPlate(veh, next.plateText);
    sa::ScriptCmd(0x0674).Int(veh->GetModelIndex()).Str8(next.plateText).Run();

    g_savedTuningBeforeSub = next;
    tuning::ApplyTuning(veh, next);
    sa::PlayFrontendSound(sa::SFX_BUY);

    int slot = garage::FindSlotForLiveVehicle(veh);
    if (slot >= 0) {
        garage::SnapshotVehicleToSlot(veh, slot);
        garage::SaveNow();
    }

    ui::ShowHelp("~g~Новый номерной знак установлен!", "~g~Custom license plate installed!");
    RebuildSubOptions(CategoryId::Plate, veh);
}

void FocusCameraForCategory(CategoryId cat, sa::CVehicle* veh, bool snapImmediate) {
    if (!sa::IsVehicleValid(veh)) return;
    const bool bike = veh->IsBikeLike();
    sa::CVector fwd = veh->GetForward();
    float fwdAng = std::atan2(fwd.y, fwd.x);

    float relAng  = -0.68f;
    float dist    = bike ? 2.95f : 3.85f;
    float height  = 1.20f;
    float lookZ   = 0.28f;
    float lookFwd = 0.0f;

    switch (cat) {
        case CategoryId::FrontBumper:
        case CategoryId::Hood:
        case CategoryId::AuxLights:
        case CategoryId::Engine:
        case CategoryId::Turbo:
        case CategoryId::Transmission:
            relAng  = -0.36f;
            dist    = bike ? 2.55f : 3.25f;
            height  = 0.96f;
            lookZ   = 0.22f;
            lookFwd = bike ? 0.35f : 0.72f;
            break;

        case CategoryId::RearBumper:
        case CategoryId::Exhaust:
        case CategoryId::Plate:
        case CategoryId::Nitro:
            relAng  = 3.14159265f + 0.34f;
            dist    = bike ? 2.45f : 3.10f;
            height  = 0.86f;
            lookZ   = 0.24f;
            lookFwd = bike ? -0.35f : -0.82f;
            break;

        case CategoryId::Spoilers:
            relAng  = 3.14159265f + 0.54f;
            dist    = bike ? 2.65f : 3.45f;
            height  = 1.24f;
            lookZ   = 0.44f;
            lookFwd = -0.72f;
            break;

        case CategoryId::Wheels:
        case CategoryId::SideSkirt:
        case CategoryId::Suspension:
        case CategoryId::Brakes:
        case CategoryId::Tires:
        case CategoryId::Hydraulics:
            relAng  = -1.52f;
            dist    = bike ? 2.55f : 3.30f;
            height  = 0.72f;
            lookZ   = 0.10f;
            lookFwd = 0.0f;
            break;

        case CategoryId::Roof:
            relAng  = -0.78f;
            dist    = bike ? 2.80f : 3.55f;
            height  = 1.72f;
            lookZ   = 0.52f;
            lookFwd = 0.0f;
            break;

        default:
            break;
    }

    g_camTargetAngle   = fwdAng + relAng;
    g_camTargetDist    = dist;
    g_camTargetHeight  = height;
    g_camTargetLookZ   = lookZ;
    g_camTargetLookFwd = lookFwd;

    if (snapImmediate) {
        g_camOrbitAngle = g_camTargetAngle;
        g_camDist       = g_camTargetDist;
        g_camHeight     = g_camTargetHeight;
        g_camLookZ      = g_camTargetLookZ;
        g_camLookFwd    = g_camTargetLookFwd;
        g_camAutoLerp   = false;
    } else {
        g_camAutoLerp   = true;
    }
}

void EnterSubmenu(CategoryId cat, sa::CVehicle* veh) {
    if (!veh) return;

    // Direct 1-click Repair action
    if (cat == CategoryId::Repair) {
        g_standaloneEnteredMs = 0;
        int cost = mod::Config().priceRepair;
        if (sa::GetPlayerMoney() < cost) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
            return;
        }
        sa::AddPlayerMoney(-cost);
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) {
            sa::ScriptCmd(0x0A30).Int(h).Run();
            sa::ScriptCmd(0x0878).Int(h).Float(0.0f).Run();
        }
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Транспорт отремонтирован!", "~g~Vehicle repaired!");
        return;
    }

    g_activeSubCat         = cat;
    g_savedTuningBeforeSub = GetCurrentVehTuning(veh);
    g_savedCol1            = veh->PrimaryColor();
    g_savedCol2            = veh->SecondaryColor();
    {
        int32_t h = sa::GetVehicleHandle(veh), pj = -1;
        if (h > 0) sa::ScriptCmd(0x0988).Int(h).Out(pj).Run();
        g_savedPaintjob = (pj >= 0 && pj <= 2) ? static_cast<int8_t>(pj) : -1;
    }
    vehmods::ModCat vmc{};
    g_savedVisualModId   = IsVisualModCategory(cat, vmc) ? vehmods::FindInstalledUpgradeInCat(veh, vmc) : -1;
    g_previewVisualModId = -1;
    g_previewModified    = false;

    RebuildSubOptions(cat, veh);
    g_inSubmenu = true;
    if (g_standaloneMode) {
        FocusCameraForCategory(cat, veh, false);
    }
    sa::PlayFrontendSound(sa::SFX_SELECT);
    ApplySubPreview(veh);
}

void PurchaseSubOption(sa::CVehicle* veh) {
    if (!veh || g_subOptions.empty()) return;
    g_standaloneEnteredMs = 0;
    const auto& opt = g_subOptions[g_subCursor];

    // Special interactive plate typing entry
    if (g_activeSubCat == CategoryId::Plate && opt.value == -100) {
        g_plateInputBuf = g_savedTuningBeforeSub.plateText;
        g_typingPlate   = true;
        sa::PlayFrontendSound(sa::SFX_SELECT);
        return;
    }

    // Special ETS 2 Maintenance & Part Overhaul purchase!
    if (g_activeSubCat == CategoryId::Maintenance) {
        if (opt.installed || opt.price <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Узел уже в идеальном состоянии (0% износа).", "Part is already in mint condition (0% wear).");
            return;
        }
        if (sa::GetPlayerMoney() < opt.price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег для обслуживания!", "~r~Not enough money for maintenance!");
            return;
        }
        sa::AddPlayerMoney(-opt.price);
        auto w = tuning::GetLiveWear(veh);
        switch (opt.value) {
            case 100: w = mod::VehicleWear{}; break;
            case 0:   w.engine       = 0.0f; break;
            case 1:   w.transmission = 0.0f; break;
            case 2:   w.brakes       = 0.0f; break;
            case 3:   w.tires        = 0.0f; break;
            case 4:   w.suspension   = 0.0f; break;
            case 5:   w.chassis      = 0.0f; break;
            default: break;
        }
        tuning::SetLiveWear(veh, w);
        if (opt.value == 100 || opt.value == 5) {
            int32_t h = sa::GetVehicleHandle(veh);
            if (h > 0) {
                sa::ScriptCmd(0x0A30).Int(h).Run(); // FIX_CAR
                sa::ScriptCmd(0x0878).Int(h).Float(0.0f).Run();
            }
        }
        int ownedSlot = garage::FindSlotForLiveVehicle(veh);
        if (ownedSlot >= 0) {
            garage::SnapshotVehicleToSlot(veh, ownedSlot);
            garage::SaveNow();
        }
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Обслуживание выполнено!~w~ Характеристики восстановлены.",
                     "~g~Maintenance complete!~w~ Performance restored.");
        RebuildSubOptions(CategoryId::Maintenance, veh);
        return;
    }

    // Special Registration actions
    if (g_activeSubCat == CategoryId::Registration) {
        if (opt.value == 0) { sa::PlayFrontendSound(sa::SFX_ERROR); return; }
        if (opt.value == 1) {
            if (sa::GetPlayerMoney() < opt.price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег для регистрации!", "~r~Not enough money to register!");
                return;
            }
            int slot = garage::RegisterVehicle(veh);
            if (slot >= 0) {
                sa::AddPlayerMoney(-opt.price);
                sa::PlayFrontendSound(sa::SFX_BUY);
                char msgRus[160], msgEng[160];
                std::snprintf(msgRus, sizeof(msgRus), "~g~Транспорт зарегистрирован!~w~ Слот #%d. Вызов по ~y~F6~w~.", slot + 1);
                std::snprintf(msgEng, sizeof(msgEng), "~g~Vehicle registered!~w~ Slot #%d. Call via ~y~F6~w~.", slot + 1);
                ui::ShowHelp(msgRus, msgEng);
                g_inSubmenu = false;
            } else {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Гараж заполнен (макс. 10 авто)!", "~r~Garage is full (max 10 vehicles)!");
            }
            return;
        }
        if (opt.value == 2) {
            int slot = garage::FindSlotForLiveVehicle(veh);
            if (slot >= 0) {
                garage::SnapshotVehicleToSlot(veh, slot);
                garage::SaveNow();
                sa::PlayFrontendSound(sa::SFX_BUY);
                ui::ShowHelp("~g~Конфигурация личного авто сохранена!", "~g~Personal vehicle config saved!");
            }
            g_inSubmenu = false;
            return;
        }
        if (opt.value == 3) {
            int slot = garage::FindSlotForLiveVehicle(veh);
            if (slot >= 0) {
                garage::UnregisterSlot(slot);
                sa::PlayFrontendSound(sa::SFX_BACK);
                ui::ShowHelp("Транспорт снят с учёта.", "Vehicle unregistered.");
            }
            g_inSubmenu = false;
            return;
        }
        return;
    }

    if (opt.installed && g_activeSubCat != CategoryId::Tires) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Уже установлено.", "Already installed.");
        return;
    }

    int cost = opt.installed ? 0 : opt.price;
    if (cost > 0 && sa::GetPlayerMoney() < cost) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
        return;
    }

    // Visual Mod Purchase (Wheels, Spoilers, Bumpers, Skirts, Exhaust, Roof, Hood, Lights, Nitro)
    vehmods::ModCat vmc{};
    if (IsVisualModCategory(g_activeSubCat, vmc)) {
        if (cost > 0) sa::AddPlayerMoney(-cost);
        g_savedVisualModId   = static_cast<int16_t>(opt.value);
        g_previewVisualModId = -1;
        g_previewModified    = false;
        sa::PlayFrontendSound(sa::SFX_BUY);
        int slot = garage::FindSlotForLiveVehicle(veh);
        if (slot >= 0) { garage::SnapshotVehicleToSlot(veh, slot); garage::SaveNow(); }
        RebuildSubOptions(g_activeSubCat, veh);
        return;
    }

    // Paintjob purchase
    if (g_activeSubCat == CategoryId::Paintjob) {
        if (cost > 0) sa::AddPlayerMoney(-cost);
        g_savedPaintjob   = (opt.value >= 0 && opt.value <= 2) ? static_cast<int8_t>(opt.value) : -1;
        g_previewModified = false;
        sa::PlayFrontendSound(sa::SFX_BUY);
        int slot = garage::FindSlotForLiveVehicle(veh);
        if (slot >= 0) { garage::SnapshotVehicleToSlot(veh, slot); garage::SaveNow(); }
        RebuildSubOptions(g_activeSubCat, veh);
        return;
    }

    // Color purchase
    if (g_activeSubCat == CategoryId::PaintPrimary || g_activeSubCat == CategoryId::PaintSecondary) {
        if (cost > 0) sa::AddPlayerMoney(-cost);
        if (g_activeSubCat == CategoryId::PaintPrimary) g_savedCol1 = static_cast<uint8_t>(opt.value);
        else g_savedCol2 = static_cast<uint8_t>(opt.value);
        int32_t h = sa::GetVehicleHandle(veh);
        if (h > 0) sa::ScriptCmd(0x0229).Int(h).Int(g_savedCol1).Int(g_savedCol2).Run();
        g_previewModified = false;
        sa::PlayFrontendSound(sa::SFX_BUY);
        int slot = garage::FindSlotForLiveVehicle(veh);
        if (slot >= 0) { garage::SnapshotVehicleToSlot(veh, slot); garage::SaveNow(); }
        RebuildSubOptions(g_activeSubCat, veh);
        return;
    }

    // Preset License Plate purchase (updates live IMMEDIATELY!)
    if (g_activeSubCat == CategoryId::Plate) {
        CommitCustomPlate(veh, kPlatePresets[opt.value]);
        return;
    }

    // Performance GTA 5 Upgrade purchase
    auto next = g_savedTuningBeforeSub;
    switch (g_activeSubCat) {
        case CategoryId::Engine:       next.engineLevel       = static_cast<uint8_t>(opt.value); break;
        case CategoryId::Transmission: next.transmissionLevel = static_cast<uint8_t>(opt.value); break;
        case CategoryId::Suspension:   next.suspensionLevel   = static_cast<uint8_t>(opt.value); break;
        case CategoryId::Brakes:       next.brakesLevel       = static_cast<uint8_t>(opt.value); break;
        case CategoryId::Tires:
            if (opt.value == 100) next.bulletproofTires = !next.bulletproofTires;
            else                  next.tiresGripLevel   = static_cast<uint8_t>(opt.value);
            break;
        case CategoryId::Turbo:
            next.turbo = (opt.value != 0);
            break;
        case CategoryId::Armor:        next.armorLevel        = static_cast<uint8_t>(opt.value); break;
        case CategoryId::Horn:
            next.hornType = static_cast<uint8_t>(opt.value);
            audio::UpdateCustomHorn(0, false);
            break;
        default: break;
    }

    if (cost > 0) sa::AddPlayerMoney(-cost);
    g_savedTuningBeforeSub = next;
    g_previewModified      = false;
    tuning::ApplyTuning(veh, next);
    sa::PlayFrontendSound(sa::SFX_BUY);

    int ownedSlot = garage::FindSlotForLiveVehicle(veh);
    if (ownedSlot >= 0) {
        garage::SnapshotVehicleToSlot(veh, ownedSlot);
        garage::SaveNow();
    }

    RebuildSubOptions(g_activeSubCat, veh);
}

// Handle direct keyboard typing for Custom License Plate (A-Z, 0-9, Space, '-', Backspace, Enter, Esc)
void ProcessPlateTypingInput(sa::CVehicle* veh) {
    const auto& k = ui::Keys();

    if (k.JustPressed(VK_ESCAPE)) {
        g_typingPlate = false;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    if (k.JustPressed(VK_RETURN)) {
        g_typingPlate = false;
        if (!g_plateInputBuf.empty()) {
            CommitCustomPlate(veh, g_plateInputBuf);
        } else {
            sa::PlayFrontendSound(sa::SFX_BACK);
        }
        return;
    }
    if (k.Repeated(VK_BACK)) {
        if (!g_plateInputBuf.empty()) {
            g_plateInputBuf.pop_back();
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }
        return;
    }

    if (g_plateInputBuf.size() < 8) {
        for (int vk = 'A'; vk <= 'Z'; ++vk) {
            if (k.JustPressed(vk)) {
                g_plateInputBuf.push_back(static_cast<char>(vk));
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                return;
            }
        }
        for (int vk = '0'; vk <= '9'; ++vk) {
            if (k.JustPressed(vk)) {
                g_plateInputBuf.push_back(static_cast<char>(vk));
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                return;
            }
        }
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) {
            if (k.JustPressed(vk)) {
                g_plateInputBuf.push_back(static_cast<char>('0' + (vk - VK_NUMPAD0)));
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                return;
            }
        }
        if (k.JustPressed(VK_SPACE)) {
            g_plateInputBuf.push_back(' ');
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            return;
        }
        if (k.JustPressed(VK_OEM_MINUS) || k.JustPressed(VK_SUBTRACT)) {
            g_plateInputBuf.push_back('-');
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            return;
        }
    }
}

// ============================================================================
// Standalone Mode B Enter / Exit (Real GTA SA Mod Shop Interiors!)
// ============================================================================
void EnterStandaloneWorkshop(int shopIdx, sa::CVehicle* veh) {
    if (!sa::IsVehicleValid(veh) || shopIdx < 0 || shopIdx >= 5) return;
    int32_t h = sa::GetVehicleHandle(veh);
    sa::CPed* ped = sa::FindPlayerPed(0);
    int32_t pedH = sa::GetPedHandle(ped);
    if (h <= 0 || pedH <= 0) return;

    vehmods::EnsureLoaded();
    const auto& shop = kModShops[shopIdx];

    g_standaloneMode      = true;
    g_standaloneShopIdx   = shopIdx;
    g_standaloneEnteredMs = sa::GetTimeMs();
    g_workshopVeh         = veh;
    g_inSubmenu           = false;
    g_typingPlate         = false;
    g_mainCursor          = 0;

    sa::ScriptCmd(0x03E6).Run();                    // CLEAR_HELP (kill vanilla "cannot modify" text immediately!)
    sa::ScriptCmd(0x0361).Str8(shop.name).Run();    // CLOSE_GARAGE
    g_doorOpened[shopIdx] = false;
    sa::ScriptCmd(0x01B4).Int(0).Int(0).Run();      // SET_PLAYER_CONTROL false
    sa::ScriptCmd(0x04BA).Int(h).Float(0.0f).Run(); // SET_CAR_FORWARD_SPEED 0

    sa::ScriptCmd(0x04E4).Float(shop.intX).Float(shop.intY).Run(); // REQUEST_COLLISION
    sa::ScriptCmd(0x04BB).Int(shop.interiorId).Run();              // SET_AREA_VISIBLE
    sa::ScriptCmd(0x0860).Int(pedH).Int(shop.interiorId).Run();    // SET_CHAR_AREA_VISIBLE
    sa::ScriptCmd(0x0840).Int(h).Int(shop.interiorId).Run();       // SET_VEHICLE_AREA_VISIBLE

    // Find exact interior floor collision height and let GTA SA's suspension settle naturally during fade-in!
    float floorZ = shop.intZ;
    sa::ScriptCmd(0x02CE).Float(shop.intX).Float(shop.intY).Float(shop.intZ + 1.5f).Out(floorZ).Run();
    float placeZ = (floorZ > shop.intZ - 2.5f && floorZ < shop.intZ + 1.0f) ? (floorZ + 0.04f) : (shop.intZ - 0.12f);

    sa::ScriptCmd(0x0519).Int(h).Int(0).Run(); // Keep unfrozen briefly so wheels drop flush onto the garage floor!
    sa::ScriptCmd(0x00AB).Int(h).Float(shop.intX).Float(shop.intY).Float(placeZ).Run();
    sa::ScriptCmd(0x0175).Int(h).Float(shop.intHeading).Run();
    sa::ScriptCmd(0x04BA).Int(h).Float(0.0f).Run();
    sa::ScriptCmd(0x016A).Int(260).Int(1).Run();    // Smooth fade-in

    g_workshopFrozen        = false;
    g_workshopSettleUntilMs = sa::GetTimeMs() + 420;

    FocusCameraForCategory(CategoryId::VanillaItem, veh, true);
    RebuildMainEntries(nullptr, veh);
    sa::PlayFrontendSound(sa::SFX_SELECT);
    modlog::Info("[Workshop] Entered interior %d (%s) for model %d",
                 shop.interiorId, shop.name, veh->GetModelIndex());
}

void ExitStandaloneWorkshop() {
    if (!g_standaloneMode) return;
    audio::UpdateCustomHorn(0, false);
    g_typingPlate = false;

    if (g_inSubmenu && g_workshopVeh) {
        RevertSubPreview(g_workshopVeh);
    }

    int shopIdx = g_standaloneShopIdx;
    sa::CPed* ped = sa::FindPlayerPed(0);
    int32_t pedH = sa::GetPedHandle(ped);

    if (sa::IsVehicleValid(g_workshopVeh)) {
        int32_t h = sa::GetVehicleHandle(g_workshopVeh);
        int ownedSlot = garage::FindSlotForLiveVehicle(g_workshopVeh);
        if (ownedSlot >= 0) {
            garage::SnapshotVehicleToSlot(g_workshopVeh, ownedSlot);
            garage::SaveNow();
        }

        if (shopIdx >= 0 && shopIdx < 5 && h > 0) {
            const auto& shop = kModShops[shopIdx];
            sa::ScriptCmd(0x04E4).Float(shop.exitX).Float(shop.exitY).Run(); // REQUEST_COLLISION
            sa::ScriptCmd(0x04BB).Int(0).Run();
            if (pedH > 0) sa::ScriptCmd(0x0860).Int(pedH).Int(0).Run();
            sa::ScriptCmd(0x0840).Int(h).Int(0).Run();
            // Synchronously stream exterior pavement collision BEFORE querying ground Z so the car NEVER falls!
            sa::ScriptCmd(0x03CB).Float(shop.exitX).Float(shop.exitY).Float(shop.exitZ).Run(); // LOAD_SCENE

            float groundZ = shop.exitZ;
            sa::ScriptCmd(0x02CE).Float(shop.exitX).Float(shop.exitY).Float(shop.exitZ + 2.5f).Out(groundZ).Run();
            float placeZ = (groundZ > shop.exitZ - 3.0f && groundZ < shop.exitZ + 3.0f) ? (groundZ + 0.04f) : shop.exitZ;

            g_workshopVeh->MoveSpeed() = {0.0f, 0.0f, 0.0f};
            sa::ScriptCmd(0x00AB).Int(h).Float(shop.exitX).Float(shop.exitY).Float(placeZ).Run();
            sa::ScriptCmd(0x0175).Int(h).Float(shop.exitHeading).Run();
            sa::ScriptCmd(0x04BA).Int(h).Float(0.0f).Run();
            sa::ScriptCmd(0x0519).Int(h).Int(0).Run(); // FREEZE_CAR_POSITION false
            sa::ScriptCmd(0x0361).Str8(shop.name).Run(); // Keep garage door closed until player drives away!
            g_doorBlockedShopIdx  = shopIdx;
            g_doorOpened[shopIdx] = false;
        } else if (h > 0) {
            sa::ScriptCmd(0x0519).Int(h).Int(0).Run();
        }
    } else {
        sa::ScriptCmd(0x04BB).Int(0).Run();
        if (pedH > 0) sa::ScriptCmd(0x0860).Int(pedH).Int(0).Run();
    }

    sa::ScriptCmd(0x03E6).Run(); // CLEAR_HELP
    sa::ScriptCmd(0x02EB).Run();
    sa::ScriptCmd(0x01B4).Int(0).Int(1).Run();
    sa::ScriptCmd(0x016A).Int(260).Int(1).Run();

    g_standaloneMode = false;
    g_inSubmenu      = false;
    g_workshopVeh    = nullptr;
    g_waitLeaveBox   = true;
    g_standaloneCooldownUntilMs = sa::GetTimeMs() + 4000;
    sa::PlayFrontendSound(sa::SFX_BACK);
}

void ProcessMenuInput(sa::tMenuPanel* vanillaPanel, sa::CVehicle* veh) {
    if (g_typingPlate) {
        ProcessPlateTypingInput(veh);
        return;
    }

    const auto& k = ui::Keys();
    bool up     = k.Repeated(VK_UP)    || k.Repeated('W');
    bool down   = k.Repeated(VK_DOWN)  || k.Repeated('S');
    bool accept = k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE);
    bool back   = k.JustPressed(VK_BACK)   || k.JustPressed('F') || k.JustPressed(VK_ESCAPE);

    if (g_inSubmenu) {
        int n = static_cast<int>(g_subOptions.size());
        if (n > 0) {
            if (up) {
                g_subCursor = (g_subCursor - 1 + n) % n;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                ApplySubPreview(veh);
            } else if (down) {
                g_subCursor = (g_subCursor + 1) % n;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                ApplySubPreview(veh);
            } else if (accept) {
                PurchaseSubOption(veh);
            }
        }
        if (back) {
            RevertSubPreview(veh);
            g_inSubmenu = false;
            if (g_standaloneMode) {
                FocusCameraForCategory(CategoryId::VanillaItem, veh, false);
            }
            sa::PlayFrontendSound(sa::SFX_BACK);
        }
        return;
    }

    int n = static_cast<int>(g_mainEntries.size());
    if (n <= 0) return;

    if (up) {
        g_mainCursor = (g_mainCursor - 1 + n) % n;
        sa::PlayFrontendSound(sa::SFX_SCROLL);
    } else if (down) {
        g_mainCursor = (g_mainCursor + 1) % n;
        sa::PlayFrontendSound(sa::SFX_SCROLL);
    } else if (accept) {
        const auto& e = g_mainEntries[g_mainCursor];
        if (e.cat == CategoryId::VanillaItem && vanillaPanel) {
            g_forwardVanillaAccept = true;
            g_forwardVanillaRow    = e.vanillaRow;
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else {
            EnterSubmenu(e.cat, veh);
        }
    } else if (back) {
        if (g_standaloneMode) {
            ExitStandaloneWorkshop();
        } else if (vanillaPanel) {
            g_forwardVanillaExit = true;
        }
    }
}

void __cdecl HookedMenuSystemProcess(int8_t panelId) {
    if (g_vanillaHookMode || g_forwardVanillaExit) return;
    if (g_origMenuProcess) g_origMenuProcess(panelId);
}

} // namespace

void InstallHooks() {
    uintptr_t orig = mem::HookCall(0x0058FCDA, reinterpret_cast<uintptr_t>(&HookedMenuSystemProcess));
    g_origMenuProcess = reinterpret_cast<FnMenuProcess>(orig);
    modlog::Info("[Workshop] Hooked CMenuSystem::Process at 0x0058FCDA (orig=0x%08X)", orig);
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
    sa::tMenuPanel* topPanel = GetVanillaCarmodMainPanel();

    bool anyCarmodOpen = IsAnyVanillaCarmodPanelOpen();
    if (anyCarmodOpen) {
        g_vanillaEverOpened = true;
        if (playerVeh) g_workshopVeh = playerVeh;
    } else if (g_vanillaEverOpened) {
        g_vanillaEverOpened = false;
        g_vanillaHookMode   = false;
        g_inSubmenu         = false;
        g_typingPlate       = false;
        audio::UpdateCustomHorn(0, false);
        if (sa::IsVehicleValid(g_workshopVeh)) {
            int slot = garage::FindSlotForLiveVehicle(g_workshopVeh);
            if (slot >= 0) {
                garage::SnapshotVehicleToSlot(g_workshopVeh, slot);
                garage::SaveNow();
            }
        }
        g_workshopVeh = nullptr;
    }

    if (topPanel && playerVeh && !g_standaloneMode) {
        if (!g_vanillaHookMode) {
            g_vanillaHookMode = true;
            g_inSubmenu       = false;
            g_typingPlate     = false;
            g_mainCursor      = std::clamp<int>(topPanel->m_nSelectedRow, 0, std::max<int>(0, topPanel->m_nNumRows - 1));
            g_workshopVeh     = playerVeh;
        }
        RebuildMainEntries(topPanel, playerVeh);
        ProcessMenuInput(topPanel, playerVeh);

        if (g_forwardVanillaAccept) {
            topPanel->m_nSelectedRow = static_cast<int8_t>(g_forwardVanillaRow);
            topPanel->m_nAcceptedRow = static_cast<int8_t>(g_forwardVanillaRow);
            uint8_t* pad = GetPad0();
            *reinterpret_cast<int16_t*>(pad + 0x20) = 255;
            *reinterpret_cast<int16_t*>(pad + 0x50) = 0;
            g_forwardVanillaAccept = false;
            g_vanillaHookMode = false;
        } else if (g_forwardVanillaExit) {
            uint8_t* pad = GetPad0();
            ClearPadMenuInputs();
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 255;
            *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
        } else {
            topPanel->m_nSelectedRow = 0;
            topPanel->m_nAcceptedRow = static_cast<int8_t>(-99);
            ClearPadMenuInputs();
        }
        return;
    }

    g_vanillaHookMode    = false;
    g_forwardVanillaExit = false;

    if (g_standaloneMode) {
        if (!sa::IsVehicleValid(g_workshopVeh) || sa::FindPlayerVehicle(0, false) != g_workshopVeh) {
            ExitStandaloneWorkshop();
            return;
        }
        RebuildMainEntries(nullptr, g_workshopVeh);
        ProcessMenuInput(nullptr, g_workshopVeh);
        ClearPadMenuInputs();
        return;
    }
}

void PostScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    vehmods::EnsureLoaded();

    // Smart Part Auto-Focus + Manual Orbit (Q/E, Arrows) + Zoom (Mouse Wheel / Z,X / +,-) inside Mod Shop Interior!
    if (g_standaloneMode && sa::IsVehicleValid(g_workshopVeh)) {
        // Let wheels settle naturally onto the physical floor mesh during fade-in, then freeze cleanly!
        if (!g_workshopFrozen) {
            g_workshopVeh->MoveSpeed().x = 0.0f;
            g_workshopVeh->MoveSpeed().y = 0.0f;
            if (g_workshopVeh->MoveSpeed().z > 0.0f) g_workshopVeh->MoveSpeed().z = 0.0f;
            if (sa::GetTimeMs() >= g_workshopSettleUntilMs) {
                int32_t h = sa::GetVehicleHandle(g_workshopVeh);
                if (h > 0) {
                    sa::ScriptCmd(0x04BA).Int(h).Float(0.0f).Run();
                    sa::ScriptCmd(0x0519).Int(h).Int(1).Run(); // FREEZE_CAR_POSITION true once tires sit flush!
                }
                g_workshopFrozen = true;
            }
        }

        // Clear any leftover vanilla "cannot modify this vehicle" help popup right after entering!
        if (g_standaloneEnteredMs != 0 && (sa::GetTimeMs() - g_standaloneEnteredMs) < 1500) {
            sa::ScriptCmd(0x03E6).Run();
        }

        float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
        if (!g_typingPlate) {
            const auto& k = ui::Keys();
            bool rotLeft  = k.IsDown('Q') || k.IsDown(VK_LEFT);
            bool rotRight = k.IsDown('E') || k.IsDown(VK_RIGHT);
            if (rotLeft) {
                g_camOrbitAngle -= dt * 1.75f;
                g_camTargetAngle = g_camOrbitAngle;
                g_camAutoLerp    = false;
            } else if (rotRight) {
                g_camOrbitAngle += dt * 1.75f;
                g_camTargetAngle = g_camOrbitAngle;
                g_camAutoLerp    = false;
            }

            // Zoom in / out via Mouse Wheel OR Z/X OR +/- keys (clamped inside room walls!)
            auto* ms = sa::GetNewMouseState();
            if (ms && ms->wheelUp) {
                g_camTargetDist = std::clamp(g_camTargetDist - 0.34f, 2.05f, 5.45f);
                g_camDist       = std::clamp(g_camDist - 0.34f, 2.05f, 5.45f);
                ms->wheelUp     = 0;
            } else if (ms && ms->wheelDown) {
                g_camTargetDist = std::clamp(g_camTargetDist + 0.34f, 2.05f, 5.45f);
                g_camDist       = std::clamp(g_camDist + 0.34f, 2.05f, 5.45f);
                ms->wheelDown   = 0;
            }
            if (k.IsDown('Z') || k.IsDown(VK_OEM_PLUS) || k.IsDown(VK_ADD)) {
                g_camTargetDist = std::clamp(g_camTargetDist - dt * 2.2f, 2.05f, 5.45f);
                g_camDist       = g_camTargetDist;
            } else if (k.IsDown('X') || k.IsDown(VK_OEM_MINUS) || k.IsDown(VK_SUBTRACT)) {
                g_camTargetDist = std::clamp(g_camTargetDist + dt * 2.2f, 2.05f, 5.45f);
                g_camDist       = g_camTargetDist;
            }

            // 'C' snaps/focuses smoothly back onto the active category's ideal camera angle!
            if (k.JustPressed('C')) {
                FocusCameraForCategory(g_inSubmenu ? g_activeSubCat : CategoryId::VanillaItem, g_workshopVeh, false);
            }
        }

        // Smooth shortest-arc lerp when auto-focusing a tuning category
        if (g_camAutoLerp) {
            constexpr float kPi  = 3.14159265f;
            constexpr float kPi2 = 6.28318530f;
            float diff = std::fmod(g_camTargetAngle - g_camOrbitAngle + kPi, kPi2);
            if (diff < 0.0f) diff += kPi2;
            diff -= kPi;

            float alpha = std::min(1.0f, dt * 6.5f);
            g_camOrbitAngle += diff * alpha;
            g_camDist       += (g_camTargetDist    - g_camDist)    * alpha;
            g_camHeight     += (g_camTargetHeight  - g_camHeight)  * alpha;
            g_camLookZ      += (g_camTargetLookZ   - g_camLookZ)   * alpha;
            g_camLookFwd    += (g_camTargetLookFwd - g_camLookFwd) * alpha;
        }

        sa::CVector pos = g_workshopVeh->GetPosition();
        sa::CVector fwd = g_workshopVeh->GetForward();
        float lookX = pos.x + fwd.x * g_camLookFwd;
        float lookY = pos.y + fwd.y * g_camLookFwd;
        float lookZ = pos.z + g_camLookZ;

        float camX = lookX + std::cos(g_camOrbitAngle) * g_camDist;
        float camY = lookY + std::sin(g_camOrbitAngle) * g_camDist;
        float camZ = pos.z + g_camHeight;

        sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
        sa::ScriptCmd(0x0160).Float(lookX).Float(lookY).Float(lookZ).Int(2).Run();
        return;
    }

    if (IsAnyVanillaCarmodPanelOpen()) {
        g_insideShopIdx = -1;
        return;
    }

    sa::CVehicle* veh = sa::FindPlayerVehicle(0, false);
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!veh || !ped || veh->GetDriver() != ped || !veh->IsLandVehicle()) {
        for (int i = 0; i < 5; ++i) {
            if (g_doorOpened[i]) {
                sa::ScriptCmd(0x0361).Str8(kModShops[i].name).Run();
                g_doorOpened[i] = false;
            }
        }
        g_insideShopIdx      = -1;
        g_waitLeaveBox       = false;
        g_doorBlockedShopIdx = -1;
        return;
    }

    sa::CVector p = veh->GetPosition();
    int foundInside = -1;

    // After exiting a workshop, keep its garage door CLOSED until the player drives > 22m away!
    if (g_doorBlockedShopIdx >= 0 && g_doorBlockedShopIdx < 5) {
        const auto& bs = kModShops[g_doorBlockedShopIdx];
        float dBlk = std::sqrt((p.x - bs.cx) * (p.x - bs.cx) + (p.y - bs.cy) * (p.y - bs.cy));
        if (dBlk > 22.0f) {
            g_doorBlockedShopIdx = -1;
        }
    }

    for (int i = 0; i < 5; ++i) {
        const auto& s = kModShops[i];
        float dCenter = std::sqrt((p.x - s.cx) * (p.x - s.cx) + (p.y - s.cy) * (p.y - s.cy));
        float dz      = std::fabs(p.z - s.cz);

        // Clean single-trigger hysteresis: open ONCE when pulling right up to the door, close ONCE after leaving!
        if (i != g_doorBlockedShopIdx && !g_doorOpened[i] && dCenter < 13.0f && dz < 5.0f) {
            sa::ScriptCmd(0x0360).Str8(s.name).Run(); // OPEN_GARAGE
            g_doorOpened[i] = true;
        } else if (g_doorOpened[i] && (dCenter > 17.5f || dz > 6.5f)) {
            sa::ScriptCmd(0x0361).Str8(s.name).Run(); // CLOSE_GARAGE
            g_doorOpened[i] = false;
        }

        if (p.x >= s.minX && p.x <= s.maxX &&
            p.y >= s.minY && p.y <= s.maxY &&
            p.z >= s.minZ && p.z <= s.maxZ) {
            foundInside = i;
        }
    }

    if (foundInside < 0) {
        g_insideShopIdx = -1;
        g_waitLeaveBox  = false;
        return;
    }

    if (g_waitLeaveBox || sa::GetTimeMs() < g_standaloneCooldownUntilMs) {
        return;
    }

    // Suppress vanilla "You cannot modify this vehicle" popup while entering any mod shop!
    sa::ScriptCmd(0x03E6).Run();

    if (g_insideShopIdx != foundInside) {
        g_insideShopIdx = foundInside;
        g_insideStartMs = sa::GetTimeMs();
        return;
    }

    if (sa::GetTimeMs() - g_insideStartMs >= 420 && veh->Speed() < 0.12f) {
        EnterStandaloneWorkshop(foundInside, veh);
    }
}

void Render2D() {
    if (sa::IsPauseMenuActive()) return;

    // Show live vehicle stats panel with RED ETS 2 wear-loss bars whenever inside ANY mod shop!
    if ((g_vanillaHookMode || g_standaloneMode || IsAnyVanillaCarmodPanelOpen()) && sa::IsVehicleValid(g_workshopVeh)) {
        auto curCfg     = g_inSubmenu ? g_savedTuningBeforeSub : GetCurrentVehTuning(g_workshopVeh);
        auto previewCfg = g_inSubmenu ? BuildPreviewTuningForStats() : curCfg;
        auto curWear    = tuning::GetLiveWear(g_workshopVeh);
        auto prevWear   = BuildPreviewWearForStats(g_workshopVeh);

        auto curStats      = tuning::ComputeStats(g_workshopVeh, curCfg, curWear);
        auto prevStats     = tuning::ComputeStats(g_workshopVeh, previewCfg, prevWear);
        auto pristineStats = tuning::ComputeStats(g_workshopVeh, curCfg, mod::VehicleWear{});
        std::string vName  = sa::GetVehicleDisplayNameGameEncoded(g_workshopVeh->GetModelIndex());

        std::string note;
        if (mod::Config().enableWearSystem && curWear.HasAnyWear()) {
            char wb[64];
            std::snprintf(wb, sizeof(wb), "%s: %d%%",
                          ui::Loc("Общий износ", "Total Wear").c_str(),
                          static_cast<int>(std::round(curWear.Average())));
            note = wb;
        }
        const auto* prisPtr = (mod::Config().enableWearSystem && curWear.HasAnyWear()) ? &pristineStats : nullptr;
        ui::DrawVehicleStatsPanel(vName, curStats, prevStats, false, note, prisPtr);
    }

    if (!g_vanillaHookMode && !g_standaloneMode) return;

    ui::MenuDrawSpec spec;
    spec.x640 = 24.0f;
    spec.y448 = 72.0f;
    spec.w640 = 192.0f;
    spec.maxVisibleRows = 11;

    if (!g_inSubmenu) {
        spec.titleGame = (g_standaloneMode && g_standaloneShopIdx >= 0)
            ? ui::Loc(kModShops[g_standaloneShopIdx].labelRus, kModShops[g_standaloneShopIdx].labelEng)
            : ui::Loc("АВТОМАСТЕРСКАЯ", "MOD SHOP");
        spec.colLeftGame  = ui::Loc("КАТЕГОРИЯ", "CATEGORY");
        spec.colRightGame = ui::Loc("СТАТУС", "STATUS");
        spec.footerGame   = g_standaloneMode
            ? ui::Loc("Enter-Выбр. | Q/E-Кам. | Колесо-Зум | F-Вых.", "Enter-Sel | Q/E-Cam | Wheel-Zoom | F-Exit")
            : ui::Loc("Enter-Выбр. | F-Выход", "Enter-Select | F-Exit");
        spec.selectedIndex = g_mainCursor;

        for (const auto& e : g_mainEntries) {
            ui::MenuRow row;
            row.leftGame  = e.labelGame;
            row.rightGame = e.rightGame;
            if (e.cat == CategoryId::Maintenance && sa::IsVehicleValid(g_workshopVeh)) {
                row.rightColor = ui::GetWearColor(tuning::GetLiveWear(g_workshopVeh).Average());
            } else if (e.cat == CategoryId::Registration) {
                row.rightColor = {135, 225, 135, 255};
            } else {
                row.rightColor = {200, 200, 200, 255};
            }
            spec.rows.push_back(std::move(row));
        }
    } else {
        spec.titleGame    = GetSubmenuTitle(g_activeSubCat);
        spec.colLeftGame  = ui::Loc("УЛУЧШЕНИЕ", "UPGRADE");
        spec.colRightGame = ui::Loc("ЦЕНА", "PRICE");
        spec.footerGame   = g_standaloneMode
            ? ui::Loc("Enter-Куп. | Q/E-Кам. | Колесо-Зум | F-Наз.", "Enter-Buy | Q/E-Cam | Wheel-Zoom | F-Back")
            : ui::Loc("Enter-Купить | F-Назад", "Enter-Buy | F-Back");
        spec.selectedIndex = g_subCursor;

        for (const auto& opt : g_subOptions) {
            ui::MenuRow row;
            row.leftGame = opt.labelGame;
            if (opt.installed) {
                row.rightGame  = ui::Loc("Уст.", "Owned");
                row.rightColor = {115, 235, 130, 255};
            } else {
                row.rightGame  = FormatPrice(opt.price);
                row.rightColor = (sa::GetPlayerMoney() >= opt.price)
                    ? sa::CRGBA{220, 220, 220, 255}
                    : sa::CRGBA{235, 100, 100, 255};
            }
            spec.rows.push_back(std::move(row));
        }
    }

    ui::DrawVanillaMenu(spec);

    if (g_typingPlate) {
        ui::DrawPlateInputBox(g_plateInputBuf, mod::Config().pricePlate);
    }
}

bool IsWorkshopActive() {
    return g_vanillaHookMode || g_standaloneMode || IsAnyVanillaCarmodPanelOpen();
}

bool IsCustomWorkshopMenuDrawing() {
    return g_vanillaHookMode || g_standaloneMode;
}

sa::CVehicle* GetWorkshopVehicle() {
    return (g_vanillaHookMode || g_standaloneMode) ? g_workshopVeh : nullptr;
}

void Reset() {
    g_vanillaHookMode    = false;
    g_vanillaEverOpened  = false;
    g_standaloneMode     = false;
    g_inSubmenu          = false;
    g_typingPlate        = false;
    g_workshopVeh        = nullptr;
    g_insideShopIdx      = -1;
    g_waitLeaveBox       = false;
    g_doorBlockedShopIdx = -1;
    for (int i = 0; i < 5; ++i) g_doorOpened[i] = false;
}

} // namespace workshop
