#include "ammu_shop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "common_utils.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "progression_rules.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "weapon_audio.hpp"
#include "weapon_config.hpp"
#include "weapon_data.hpp"
#include "weapon_mods.hpp"
#include "weapon_storage.hpp"
#include "weapon_ui.hpp"
#include "weapon_visuals.hpp"

namespace ammu {

namespace {

enum class Screen : uint8_t {
    Categories = 0,
    WeaponList,
    WeaponMods,
    ModSlotOptions,
    ArmorShop
};

enum class ModSlot : uint8_t {
    AmmoPack = 0,
    AmmoFull,
    Muzzle,
    Magazine,
    Scope,
    Grip,
    Barrel,
    Laser
};

struct SlotOption {
    std::string labelGame;
    std::string noteGame;
    uint8_t     value    = 0;
    int         price    = 0;
    int         reqRank  = 1;
    bool        owned    = false;
    bool        equipped = false;
};

using FnMenuProcess = void(__cdecl*)(int8_t);
FnMenuProcess g_origMenuProcess = nullptr;

bool     g_shopActive         = false;
bool     g_forwardExit        = false;
uint32_t g_exitCooldownUntilMs= 0;
int      g_exitPulseCounter   = 0;

Screen   g_screen             = Screen::Categories;
int      g_catCursor          = 0;
int      g_weapCursor         = 0;
int      g_modCursor          = 0;
int      g_optCursor          = 0;
int      g_armorCursor        = 0;

wdata::WeaponCategory g_activeCat      = wdata::WeaponCategory::Pistols;
uint32_t              g_activeWeaponId = 0;
ModSlot               g_activeModSlot  = ModSlot::Muzzle;

bool     g_inspectCamActive   = false;
float    g_camOrbitAngle      = 0.0f;
float    g_camDefaultOrbit    = 0.0f;
float    g_inspectDist        = 1.25f;
bool     g_camOrbitInited     = false;

std::vector<const wdata::WeaponDef*> g_catWeapons;
std::vector<ModSlot>                 g_modSlots;
std::vector<SlotOption>              g_slotOptions;

struct CategoryInfo {
    wdata::WeaponCategory cat;
    const char*           rus;
    const char*           eng;
};

constexpr CategoryInfo kCategories[] = {
    {wdata::WeaponCategory::Pistols,  "Пистолеты",          "Pistols"},
    {wdata::WeaponCategory::Shotguns, "Дробовики",          "Shotguns"},
    {wdata::WeaponCategory::SMGs,     "ПП",                 "SMGs"},
    {wdata::WeaponCategory::Rifles,   "Автоматы",           "Assault Rifles"},
    {wdata::WeaponCategory::Snipers,  "Винтовки",           "Sniper Rifles"},
    {wdata::WeaponCategory::Heavy,    "Тяжёлое",            "Heavy"},
    {wdata::WeaponCategory::Thrown,   "Метательное",        "Thrown"},
    {wdata::WeaponCategory::Melee,    "Ближний бой",        "Melee"},
    {wdata::WeaponCategory::Armor,    "Бронежилет",         "Body Armor"},
};
constexpr int kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

sa::tMenuPanel* GetAmmuPanel() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (std::strncmp(p->m_acTitle, "AMMU", 4) != 0) return nullptr;
    return p;
}

void RebuildCategoryWeapons() {
    g_catWeapons.clear();
    for (size_t i = 0; i < wdata::kWeaponCount; ++i) {
        if (wdata::kWeapons[i].soldInShop && wdata::kWeapons[i].category == g_activeCat) {
            g_catWeapons.push_back(&wdata::kWeapons[i]);
        }
    }
    if (g_weapCursor >= static_cast<int>(g_catWeapons.size())) g_weapCursor = 0;
}

void RebuildModSlots(const wdata::WeaponDef* def) {
    g_modSlots.clear();
    if (!def) return;
    if (def->category != wdata::WeaponCategory::Melee && def->ammoPackCount > 0) {
        g_modSlots.push_back(ModSlot::AmmoPack);
        g_modSlots.push_back(ModSlot::AmmoFull);
    }
    if (def->supportMask & (wdata::SUP_SUPPRESSOR | wdata::SUP_COMPENSATOR)) g_modSlots.push_back(ModSlot::Muzzle);
    if (def->supportMask & (wdata::SUP_EXT_MAG    | wdata::SUP_DRUM_MAG))    g_modSlots.push_back(ModSlot::Magazine);
    if (def->supportMask & (wdata::SUP_REFLEX     | wdata::SUP_SCOPE))       g_modSlots.push_back(ModSlot::Scope);
    if (def->supportMask & wdata::SUP_GRIP)                                  g_modSlots.push_back(ModSlot::Grip);
    if (def->supportMask & wdata::SUP_HEAVY_BRL)                             g_modSlots.push_back(ModSlot::Barrel);
    if (def->supportMask & wdata::SUP_LASER)                                 g_modSlots.push_back(ModSlot::Laser);
    if (g_modCursor >= static_cast<int>(g_modSlots.size())) g_modCursor = 0;
}

int ModSlotToRuleType(ModSlot slot) {
    switch (slot) {
        case ModSlot::Muzzle:   return 0;
        case ModSlot::Magazine: return 1;
        case ModSlot::Scope:    return 2;
        case ModSlot::Grip:     return 3;
        case ModSlot::Barrel:   return 4;
        case ModSlot::Laser:    return 5;
        default:                return -1;
    }
}

void RebuildSlotOptions(const wdata::WeaponDef* def, ModSlot slot) {
    g_slotOptions.clear();
    if (!def) return;
    const auto& cfg = wcfg::Get();
    const auto& L   = wstore::Store().Get(def->id);
    float m = def->modPriceMult;
    const int ruleSlot = ModSlotToRuleType(slot);

    auto addOpt = [&](const char* rus, const char* eng, const char* noteRus, const char* noteEng,
                      uint8_t val, int price, uint8_t ownedMask, uint8_t curVal) {
        SlotOption o;
        o.labelGame = ui::Loc(rus, eng);
        o.noteGame  = (noteRus && noteRus[0]) ? ui::Loc(noteRus, noteEng) : "";
        o.value     = val;
        o.price     = (val == 0) ? 0 : wdata::ScalePrice(price, m);
        o.reqRank   = prog_rules::WeaponAttachmentUnlockRank(ruleSlot, val);
        o.owned     = ((ownedMask & (1u << val)) != 0) || (val == 0);
        o.equipped  = (curVal == val);
        g_slotOptions.push_back(std::move(o));
    };

    switch (slot) {
        case ModSlot::Muzzle:
            addOpt("Стандарт", "Standard", "", "", 0, 0, L.ownedMuzzleMask, L.muzzle);
            if (def->supportMask & wdata::SUP_SUPPRESSOR) {
                addOpt("Глушитель", "Suppressor", "Тихий выстрел", "Silenced shot",
                       1, cfg.priceSuppressor, L.ownedMuzzleMask, L.muzzle);
            }
            if (def->supportMask & wdata::SUP_COMPENSATOR) {
                addOpt("Компенсатор", "Compensator", "Точность и звук", "Stability and report",
                       2, cfg.priceCompensator, L.ownedMuzzleMask, L.muzzle);
            }
            break;

        case ModSlot::Magazine:
            addOpt("Стандарт", "Standard", "", "", 0, 0, L.ownedMagMask, L.mag);
            if (def->supportMask & wdata::SUP_EXT_MAG) {
                addOpt("Увеличенный", "Extended Mag", "+60% обойма", "+60% clip",
                       1, cfg.priceExtendedMag, L.ownedMagMask, L.mag);
            }
            if (def->supportMask & wdata::SUP_DRUM_MAG) {
                addOpt("Барабанный", "Drum Mag", "+100% обойма", "2x clip",
                       2, cfg.priceDrumMag, L.ownedMagMask, L.mag);
            }
            break;

        case ModSlot::Scope:
            addOpt("Мушка", "Iron Sights", "", "", 0, 0, L.ownedScopeMask, L.scope);
            if (def->supportMask & wdata::SUP_REFLEX) {
                addOpt("Коллиматор", "Reflex", "Приближение ПКМ и точность", "ADS zoom and accuracy",
                       1, cfg.priceReflexSight, L.ownedScopeMask, L.scope);
            }
            if (def->supportMask & wdata::SUP_SCOPE) {
                addOpt("Оптика", "Tactical Scope", "Приближение 1.8x при ПКМ", "1.8x ADS zoom",
                       2, cfg.priceTacticalScope, L.ownedScopeMask, L.scope);
            }
            break;

        case ModSlot::Grip:
            addOpt("Нет", "None", "", "", 0, 0, L.ownedGripMask, L.grip);
            if (def->supportMask & wdata::SUP_GRIP) {
                addOpt("Тактич. рукоять", "Foregrip", "Стрельба в движении", "Move while firing",
                       1, cfg.priceVerticalGrip, L.ownedGripMask, L.grip);
            }
            break;

        case ModSlot::Barrel:
            addOpt("Стандарт", "Standard", "", "", 0, 0, L.ownedBarrelMask, L.barrel);
            if (def->supportMask & wdata::SUP_HEAVY_BRL) {
                addOpt("Усиленный ствол", "Heavy Barrel", "+Урон, дальность, темп", "+Damage, range, fire rate",
                       1, cfg.priceHeavyBarrel, L.ownedBarrelMask, L.barrel);
            }
            break;

        case ModSlot::Laser:
            addOpt("Нет", "None", "", "", 0, 0, L.ownedLaserMask, L.laser);
            if (def->supportMask & wdata::SUP_LASER) {
                addOpt("Красный ЛЦУ", "Red Laser", "Луч и точка при ПКМ", "Red laser on ADS",
                       1, cfg.priceLaserRed, L.ownedLaserMask, L.laser);
                addOpt("Зелёный ЛЦУ", "Green Laser", "Яркий луч при ПКМ", "Green laser on ADS",
                       2, cfg.priceLaserGreen, L.ownedLaserMask, L.laser);
            }
            break;

        default:
            break;
    }

    g_optCursor = 0;
    for (size_t i = 0; i < g_slotOptions.size(); ++i) {
        if (g_slotOptions[i].equipped) {
            g_optCursor = static_cast<int>(i);
            break;
        }
    }
}

wdata::WeaponLoadout BuildPreviewLoadout(uint32_t weaponId) {
    wdata::WeaponLoadout L = wstore::Store().Get(weaponId);
    if (g_screen == Screen::ModSlotOptions && g_optCursor >= 0 && g_optCursor < static_cast<int>(g_slotOptions.size())) {
        uint8_t v = g_slotOptions[g_optCursor].value;
        switch (g_activeModSlot) {
            case ModSlot::Muzzle:   L.muzzle = v; break;
            case ModSlot::Magazine: L.mag    = v; break;
            case ModSlot::Scope:    L.scope  = v; break;
            case ModSlot::Grip:     L.grip   = v; break;
            case ModSlot::Barrel:   L.barrel = v; break;
            case ModSlot::Laser:    L.laser  = v; break;
            default: break;
        }
    }
    return L;
}

void UpdateVisualPreview() {
    if (g_shopActive && (g_screen == Screen::WeaponMods || g_screen == Screen::ModSlotOptions) && g_activeWeaponId != 0) {
        wvis::SetPreviewOverride(true, g_activeWeaponId, BuildPreviewLoadout(g_activeWeaponId));
    } else {
        wdata::WeaponLoadout empty{};
        wvis::SetPreviewOverride(false, 0, empty);
    }
}

void BeginInspectCam(const sa::CPed* ped) {
    if (!sa::IsPedValid(ped)) return;
    if (!g_camOrbitInited) {
        sa::CVector fwd   = ped->GetForward();
        g_camDefaultOrbit = std::atan2(fwd.y, fwd.x) - 0.35f;
        g_camOrbitAngle   = g_camDefaultOrbit;
        g_inspectDist     = 1.25f;
        g_camOrbitInited  = true;
    }
    g_inspectCamActive = true;
}

void EndInspectCam() {
    if (g_inspectCamActive) {
        g_inspectCamActive = false;
        g_camOrbitInited   = false;
        g_inspectDist      = 1.25f;
        sa::ScriptCmd(0x02EB).Run();
    }
}

void CloseShopCleanly() {
    g_shopActive       = false;
    g_forwardExit      = true;
    g_exitPulseCounter = 0;
    EndInspectCam();
    wdata::WeaponLoadout empty{};
    wvis::SetPreviewOverride(false, 0, empty);
    ui::TriggerPostCloseShield(160);
    sa::PlayFrontendSound(sa::SFX_BACK);
}

int GetBusArmorUsedCap(modbus::SharedBus* bus) {
    if (bus && bus->inventoryGetArmorUsedCap) return bus->inventoryGetArmorUsedCap();
    return 0;
}
int GetBusArmorMaxCap(modbus::SharedBus* bus) {
    if (bus && bus->inventoryGetArmorMaxCap) return bus->inventoryGetArmorMaxCap();
    return 10;
}
int GetBusArmorWeight(modbus::SharedBus* bus, int tier) {
    static constexpr int kFallbackW[4] = {2, 4, 7, 10};
    if (bus && bus->inventoryGetArmorWeight) return bus->inventoryGetArmorWeight(tier);
    return (tier >= 0 && tier < 4) ? kFallbackW[tier] : 2;
}
int GetBusArmorPrice(modbus::SharedBus* bus, int tier) {
    static constexpr int kFallbackP[4] = {350, 700, 1200, 1800};
    if (bus && bus->inventoryGetArmorPrice) return bus->inventoryGetArmorPrice(tier);
    return (tier >= 0 && tier < 4) ? kFallbackP[tier] : 350;
}

void HandleSelect() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    if (g_screen == Screen::Categories) {
        if (g_catCursor < 0 || g_catCursor >= kCategoryCount) return;
        g_activeCat = kCategories[g_catCursor].cat;
        sa::PlayFrontendSound(sa::SFX_SELECT);
        if (g_activeCat == wdata::WeaponCategory::Armor) {
            g_armorCursor = 0;
            g_screen = Screen::ArmorShop;
        } else {
            g_weapCursor = 0;
            RebuildCategoryWeapons();
            g_screen = Screen::WeaponList;
        }
        return;
    }

    if (g_screen == Screen::ArmorShop) {
        if (g_armorCursor < 2) {
            int reqRank = prog_rules::AmmuEquipArmorRank(g_armorCursor);
            if (!modbus::IsRankUnlocked(reqRank)) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowRankLockedHelp(reqRank);
                return;
            }
            int price = (g_armorCursor == 0) ? 400 : 800;
            float tgt = (g_armorCursor == 0) ? 50.0f : 100.0f;
            if (ped->GetArmor() >= tgt - 0.5f) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Броня уже максимальная.", "Armor already sufficient.");
                return;
            }
            if (sa::GetPlayerMoney() < price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Недостаточно денег!", "Not enough money!");
                return;
            }
            sa::AddPlayerMoney(-price);
            ped->SetArmor(tgt);
            sa::PlayFrontendSound(sa::SFX_BUY);
            return;
        }
        // Spare Body Armor purchase straight into GTA5Inventory pocket (capacity-budget system)!
        auto* bus = modbus::GetBus();
        int tier = g_armorCursor - 2; // 0..3
        if (bus && bus->inventoryInstalled && bus->inventoryAddArmor && tier >= 0 && tier < 4) {
            int reqRank = prog_rules::SpareArmorRank(tier);
            if (!modbus::IsRankUnlocked(reqRank)) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowRankLockedHelp(reqRank);
                return;
            }
            int usedCap = GetBusArmorUsedCap(bus);
            int maxCap  = GetBusArmorMaxCap(bus);
            int weight  = GetBusArmorWeight(bus, tier);
            int price   = GetBusArmorPrice(bus, tier);
            int freeCap = std::max(0, maxCap - usedCap);

            if (weight > freeCap) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно места для запасной брони!",
                             "~r~Not enough space for spare armor!");
                return;
            }
            if (sa::GetPlayerMoney() < price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
                return;
            }
            if (bus->inventoryAddArmor(tier, +1)) {
                sa::AddPlayerMoney(-price);
                sa::PlayFrontendSound(sa::SFX_BUY);
                ui::ShowHelp("~g~Запасной бронежилет добавлен в инвентарь!", "~g~Spare body armor added to inventory!");
            }
        }
        return;
    }

    if (g_screen == Screen::WeaponList) {
        if (g_weapCursor < 0 || g_weapCursor >= static_cast<int>(g_catWeapons.size())) return;
        const wdata::WeaponDef* def = g_catWeapons[g_weapCursor];
        bool owned = wstore::Store().PlayerHasWeaponOrOwned(ped, def->id);

        if (!owned) {
            int reqRank = prog_rules::WeaponUnlockRank(def->id);
            if (!modbus::IsRankUnlocked(reqRank)) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowRankLockedHelp(reqRank);
                return;
            }
            if (sa::GetPlayerMoney() < def->buyPrice) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Недостаточно денег!", "Not enough money!");
                return;
            }
            sa::AddPlayerMoney(-def->buyPrice);
            wstore::Store().Get(def->id).owned = true;
            wstore::Store().EquipWeaponPreservingAmmo(ped, def, def->initAmmo, true);
            wstore::Store().SaveToDisk();
            wmods::ForceRefresh(true);
            sa::PlayFrontendSound(sa::SFX_BUY);
            ui::ShowHelp("Куплено. Нажмите ~g~ЛКМ/F/Enter~w~ снова для модификации.",
                         "Purchased. Press ~g~LMB/F/Enter~w~ again to customize.");
            return;
        }

        if (def->category == wdata::WeaponCategory::Melee) {
            wstore::Store().EquipWeaponPreservingAmmo(ped, def, 0, true);
            sa::PlayFrontendSound(sa::SFX_SELECT);
            return;
        }

        g_activeWeaponId = def->id;
        g_modCursor = 0;
        RebuildModSlots(def);
        wstore::Store().EquipWeaponPreservingAmmo(ped, def, 0, true);
        wmods::ForceRefresh(false);
        BeginInspectCam(ped);
        g_screen = Screen::WeaponMods;
        UpdateVisualPreview();
        sa::PlayFrontendSound(sa::SFX_SELECT);
        return;
    }

    if (g_screen == Screen::WeaponMods) {
        const wdata::WeaponDef* def = wdata::FindDef(g_activeWeaponId);
        if (!def || g_modCursor < 0 || g_modCursor >= static_cast<int>(g_modSlots.size())) return;
        ModSlot slot = g_modSlots[g_modCursor];

        if (slot == ModSlot::AmmoPack || slot == ModSlot::AmmoFull) {
            int curAmmo = wstore::Store().GetWeaponAmmo(ped, def);
            if (curAmmo >= def->maxAmmo) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Боезапас полон!", "Ammo is full!");
                return;
            }
            int addAmmo = 0;
            int price   = 0;
            if (slot == ModSlot::AmmoPack) {
                addAmmo = std::min(def->ammoPackCount, def->maxAmmo - curAmmo);
                price   = def->ammoPackPrice;
            } else {
                addAmmo = def->maxAmmo - curAmmo;
                int packs = std::max(1, (addAmmo + def->ammoPackCount - 1) / def->ammoPackCount);
                price   = packs * def->ammoPackPrice;
            }
            if (sa::GetPlayerMoney() < price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Недостаточно денег!", "Not enough money!");
                return;
            }
            sa::AddPlayerMoney(-price);
            wstore::Store().EquipWeaponPreservingAmmo(ped, def, addAmmo, true);
            wstore::Store().SaveToDisk();
            wmods::ForceRefresh(true);
            sa::PlayFrontendSound(sa::SFX_BUY);
            return;
        }

        g_activeModSlot = slot;
        RebuildSlotOptions(def, slot);
        g_screen = Screen::ModSlotOptions;
        UpdateVisualPreview();
        sa::PlayFrontendSound(sa::SFX_SELECT);
        return;
    }

    if (g_screen == Screen::ModSlotOptions) {
        const wdata::WeaponDef* def = wdata::FindDef(g_activeWeaponId);
        if (!def || g_optCursor < 0 || g_optCursor >= static_cast<int>(g_slotOptions.size())) return;
        const auto& opt = g_slotOptions[g_optCursor];

        if (opt.equipped) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            return;
        }

        if (!opt.owned && !modbus::IsRankUnlocked(opt.reqRank)) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowRankLockedHelp(opt.reqRank);
            return;
        }

        if (!opt.owned && opt.price > 0) {
            if (sa::GetPlayerMoney() < opt.price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Недостаточно денег!", "Not enough money!");
                return;
            }
            sa::AddPlayerMoney(-opt.price);
        }

        auto& L = wstore::Store().Get(def->id);
        L.owned = true;
        uint8_t bit = static_cast<uint8_t>(1u << opt.value);
        switch (g_activeModSlot) {
            case ModSlot::Muzzle:   L.muzzle = opt.value; L.ownedMuzzleMask |= bit; break;
            case ModSlot::Magazine: L.mag    = opt.value; L.ownedMagMask    |= bit; break;
            case ModSlot::Scope:    L.scope  = opt.value; L.ownedScopeMask  |= bit; break;
            case ModSlot::Grip:     L.grip   = opt.value; L.ownedGripMask   |= bit; break;
            case ModSlot::Barrel:   L.barrel = opt.value; L.ownedBarrelMask |= bit; break;
            case ModSlot::Laser:    L.laser  = opt.value; L.ownedLaserMask  |= bit; break;
            default: break;
        }

        wstore::Store().SaveToDisk();
        wmods::ForceRefresh(g_activeModSlot == ModSlot::Magazine);
        RebuildSlotOptions(def, g_activeModSlot);
        UpdateVisualPreview();

        waudio::PlayAttachClick();
        if (!opt.owned && opt.price > 0) sa::PlayFrontendSound(sa::SFX_BUY);
        else sa::PlayFrontendSound(sa::SFX_SELECT);
    }
}

void HandleBack() {
    if (g_screen == Screen::ModSlotOptions) {
        g_screen = Screen::WeaponMods;
        UpdateVisualPreview();
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    if (g_screen == Screen::WeaponMods) {
        EndInspectCam();
        g_screen = Screen::WeaponList;
        UpdateVisualPreview();
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    if (g_screen == Screen::WeaponList || g_screen == Screen::ArmorShop) {
        g_screen = Screen::Categories;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    CloseShopCleanly();
}

void ProcessMenuInput() {
    auto in = ui::ListInput().Poll(true);

    int* curPtr = nullptr;
    int  count  = 0;
    switch (g_screen) {
        case Screen::Categories:     curPtr = &g_catCursor;   count = kCategoryCount; break;
        case Screen::WeaponList:     curPtr = &g_weapCursor;  count = static_cast<int>(g_catWeapons.size()); break;
        case Screen::WeaponMods:     curPtr = &g_modCursor;   count = static_cast<int>(g_modSlots.size()); break;
        case Screen::ModSlotOptions: curPtr = &g_optCursor;   count = static_cast<int>(g_slotOptions.size()); break;
        case Screen::ArmorShop: {
            auto* bus = modbus::GetBus();
            bool hasInv = (bus && bus->inventoryInstalled && bus->inventoryAddArmor);
            curPtr = &g_armorCursor;
            count  = hasInv ? 6 : 2;
            break;
        }
    }

    if (curPtr && count > 0) {
        if (ui::ListInput().ApplyToCursor(*curPtr, count, in)) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            if (g_screen == Screen::ModSlotOptions) UpdateVisualPreview();
        }
    }

    if (in.accept) {
        HandleSelect();
    } else if (in.back) {
        HandleBack();
    }
}

std::string SlotActiveSummary(const wdata::WeaponLoadout& L, ModSlot slot) {
    switch (slot) {
        case ModSlot::Muzzle:
            if (L.muzzle == 1) return ui::Loc("Глуш.", "Supp.");
            if (L.muzzle == 2) return ui::Loc("Комп.", "Comp.");
            return ui::Loc("Стандарт", "Standard");
        case ModSlot::Magazine:
            if (L.mag == 1) return ui::Loc("Увел.", "Ext.");
            if (L.mag == 2) return ui::Loc("Бараб.", "Drum");
            return ui::Loc("Стандарт", "Standard");
        case ModSlot::Scope:
            if (L.scope == 1) return ui::Loc("Коллим.", "Reflex");
            if (L.scope == 2) return ui::Loc("Оптика", "Scope");
            return ui::Loc("Мушка", "Iron");
        case ModSlot::Grip:
            return (L.grip == 1) ? ui::Loc("Да", "Yes") : ui::Loc("Нет", "No");
        case ModSlot::Barrel:
            return (L.barrel == 1) ? ui::Loc("Усил.", "Heavy") : ui::Loc("Стандарт", "Standard");
        case ModSlot::Laser:
            if (L.laser == 1) return ui::Loc("Красн.", "Red");
            if (L.laser == 2) return ui::Loc("Зел.", "Green");
            return ui::Loc("Нет", "No");
        default:
            return "";
    }
}

std::string ModSlotTitle(ModSlot slot) {
    switch (slot) {
        case ModSlot::Muzzle:   return ui::Loc("НАДУЛЬНИК", "MUZZLE");
        case ModSlot::Magazine: return ui::Loc("МАГАЗИН", "MAGAZINE");
        case ModSlot::Scope:    return ui::Loc("ПРИЦЕЛ", "OPTICS");
        case ModSlot::Grip:     return ui::Loc("РУКОЯТЬ", "FOREGRIP");
        case ModSlot::Barrel:   return ui::Loc("СТВОЛ", "BARREL");
        case ModSlot::Laser:    return ui::Loc("ЛЦУ", "LASER");
        default:                return ui::Loc("МОДУЛИ", "MODS");
    }
}

void DrawBottomRightWeaponStats(const sa::CPed* ped) {
    if (g_screen == Screen::ArmorShop) {
        int curArm  = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
        static constexpr int kPreviewVals[6] = {50, 100, 25, 50, 75, 100};
        int idx     = std::clamp(g_armorCursor, 0, 5);
        int prevArm = std::max(curArm, kPreviewVals[idx]);

        ui::StatsCard card;
        card.SetTitle(ui::Loc("БРОНЯ", "ARMOR"),
                      (idx >= 2) ? ui::Loc("В ЗАПАС", "SPARE") : ui::Loc("НА СЕБЯ", "EQUIP"))
            .SetAnchor(true, 156.0f, 18.0f, 18.0f)
            .AddBar(ui::Loc("Защита", "Armor"), curArm, prevArm, 100, "%");

        if (auto* bus = modbus::GetBus(); bus && bus->inventoryInstalled && bus->inventoryAddArmor) {
            int usedCap = GetBusArmorUsedCap(bus);
            int maxCap  = GetBusArmorMaxCap(bus);
            int prevCap = usedCap;
            if (idx >= 2) {
                int w = GetBusArmorWeight(bus, idx - 2);
                prevCap = std::min(maxCap, usedCap + w);
                char nb[64];
                std::snprintf(nb, sizeof(nb), "%s: %d, %s: %d",
                              ui::Loc("Объём", "Size").c_str(), w,
                              ui::Loc("Свободно", "Free").c_str(), std::max(0, maxCap - usedCap));
                card.SetNote(nb);
            }
            card.AddBar(ui::Loc("Запас", "Carry"), usedCap, prevCap, maxCap, "");
        }
        card.Render();
        return;
    }

    const wdata::WeaponDef* def = nullptr;
    if (g_screen == Screen::WeaponList && g_weapCursor >= 0 && g_weapCursor < static_cast<int>(g_catWeapons.size())) {
        def = g_catWeapons[g_weapCursor];
    } else if (g_screen == Screen::WeaponMods || g_screen == Screen::ModSlotOptions) {
        def = wdata::FindDef(g_activeWeaponId);
    }
    if (!def || def->category == wdata::WeaponCategory::Melee) return;

    wdata::WeaponLoadout curL = wstore::Store().Get(def->id);
    wdata::WeaponLoadout prvL = (g_screen == Screen::ModSlotOptions) ? BuildPreviewLoadout(def->id) : curL;

    auto curSt = wmods::ComputeStats(def->id, curL);
    auto prvSt = wmods::ComputeStats(def->id, prvL);

    std::string note;
    if (g_screen == Screen::ModSlotOptions && g_optCursor >= 0 && g_optCursor < static_cast<int>(g_slotOptions.size())) {
        note = g_slotOptions[g_optCursor].noteGame;
    } else if (g_screen == Screen::WeaponMods) {
        int curAmmo = wstore::Store().GetWeaponAmmo(ped, def);
        char abuf[48];
        std::snprintf(abuf, sizeof(abuf), "%s: %d/%d", ui::Loc("Патроны", "Ammo").c_str(), curAmmo, def->maxAmmo);
        note = abuf;
    }

    wui::RenderWeaponStatsCard(def, curSt, prvSt, note, 162.0f, 18.0f, 18.0f);
}

void __cdecl HookedMenuProcess(int8_t menuId) {
    // CRITICAL: Keep suppressing ALL vanilla Ammu-Nation drawing during BOTH active shop AND exit handoff!
    // Zero flash of vanilla menu when closing!
    if (g_shopActive || g_forwardExit) {
        return;
    }
    if (GetAmmuPanel() != nullptr) {
        return;
    }
    if (g_origMenuProcess) {
        g_origMenuProcess(menuId);
    }
}

} // namespace

void InstallHooks() {
    g_origMenuProcess = reinterpret_cast<FnMenuProcess>(
        mem::HookCall(0x0058FCDA, reinterpret_cast<uintptr_t>(&HookedMenuProcess)));
    modlog::Info("[Ammu] Hooked CMenuSystem::Process (0x0058FCDA -> 0x%08X).",
                 reinterpret_cast<uintptr_t>(g_origMenuProcess));
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    sa::tMenuPanel* ammuPanel = GetAmmuPanel();

    if (g_forwardExit) {
        if (!ammuPanel) {
            g_forwardExit         = false;
            g_exitCooldownUntilMs = sa::GetTimeMs() + 900;
            return;
        }
        // Pulse clean rising-edge ButtonTriangle (NewState=255, OldState=0) while vanilla panel stays hidden!
        // ammu.scm closes its own menu invisibly and restores CJ + camera cleanly!
        sa::ClearPadMenuInputs();
        uint8_t* pad = sa::GetPad0();
        if ((g_exitPulseCounter++ % 2) == 0) {
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 255; // NewState.ButtonTriangle
            *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;   // OldState.ButtonTriangle (force fresh edge!)
        } else {
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 0;
            *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
        }
        return;
    }

    if (ammuPanel && !g_shopActive) {
        if (sa::GetTimeMs() < g_exitCooldownUntilMs) {
            // Still stepping off the marker right after exit -> keep auto-closing invisibly!
            g_forwardExit      = true;
            g_exitPulseCounter = 0;
            return;
        }
        g_shopActive = true;
        g_screen     = Screen::Categories;
        g_catCursor  = 0;
        g_weapCursor = 0;
        g_modCursor  = 0;
        g_optCursor  = 0;
        EndInspectCam();
        wstore::Store().SyncFromPlayerInventory(sa::FindPlayerPed(0));
        wmods::EnsureStockCaptured();
        ui::LockMenuInputBriefly(180);
        sa::PlayFrontendSound(sa::SFX_SELECT);
        modlog::Info("[Ammu] Opened GTA 5 Ammu-Nation UI (panel title='%s').", ammuPanel->m_acTitle);
    } else if (!ammuPanel && g_shopActive) {
        g_shopActive = false;
        EndInspectCam();
        wdata::WeaponLoadout empty{};
        wvis::SetPreviewOverride(false, 0, empty);
        return;
    }

    if (g_shopActive) {
        ProcessMenuInput();
        sa::ClearPadMenuInputs();
    } else {
        ui::UpdatePostCloseShield();
    }
}

void PostScriptsTick() {
    if (!g_shopActive || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    if (g_inspectCamActive) {
        sa::CPed* ped = sa::FindPlayerPed(0);
        if (sa::IsPedValid(ped)) {
            float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
            auto cam = ui::ListInputController::PollCamera(dt, 1.75f, 1.35f);
            g_camOrbitAngle += cam.orbitDelta;
            g_inspectDist = std::clamp(g_inspectDist + cam.zoomDelta, 0.85f, 2.00f);
            if (cam.resetCam) {
                g_inspectDist   = 1.25f;
                g_camOrbitAngle = g_camDefaultOrbit;
            }

            sa::CVector p = ped->GetPosition();
            float camX = p.x + std::cos(g_camOrbitAngle) * g_inspectDist;
            float camY = p.y + std::sin(g_camOrbitAngle) * g_inspectDist;
            float camZ = p.z + 0.32f;

            sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
            sa::ScriptCmd(0x0160).Float(p.x).Float(p.y).Float(p.z + 0.14f).Int(2).Run();
        }
    }
}

void Render2D() {
    if (!g_shopActive || sa::IsPauseMenuActive()) return;

    sa::CPed* ped = sa::FindPlayerPed(0);
    const int32_t money = sa::GetPlayerMoney();
    const bool useRus   = ui::UseRussianFlag();

    ui::ListMenu menu;
    menu.SetPosition(24.0f, 72.0f, 186.0f).SetMaxVisible(10);

    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc("АММУ-НАЦИЯ", "AMMU-NATION"))
            .SetColumns(ui::Loc("КАТЕГОРИЯ", "CATEGORY"), ui::Loc("АРСЕНАЛ", "OWNED"))
            .SetFooter(ui::StandardFooter("Выбрать", "Select", "Выход", "Exit"))
            .SetSelected(g_catCursor);

        for (int i = 0; i < kCategoryCount; ++i) {
            std::string left = ui::Loc(kCategories[i].rus, kCategories[i].eng);
            if (kCategories[i].cat == wdata::WeaponCategory::Armor) {
                int arm = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
                char buf[20];
                std::snprintf(buf, sizeof(buf), "%d%%", arm);
                menu.AddItem(left, buf, (arm > 0) ? ui::ColorSuccess() : ui::ColorNormal());
            } else {
                int tot = 0, own = 0;
                for (size_t w = 0; w < wdata::kWeaponCount; ++w) {
                    if (wdata::kWeapons[w].soldInShop && wdata::kWeapons[w].category == kCategories[i].cat) {
                        ++tot;
                        if (wstore::Store().PlayerHasWeaponOrOwned(ped, wdata::kWeapons[w].id)) ++own;
                    }
                }
                char buf[20];
                std::snprintf(buf, sizeof(buf), "%d/%d", own, tot);
                menu.AddItem(left, buf, (own > 0) ? ui::ColorSuccess() : ui::ColorMuted());
            }
        }
    } else if (g_screen == Screen::ArmorShop) {
        auto* bus   = modbus::GetBus();
        bool hasInv = (bus && bus->inventoryInstalled && bus->inventoryAddArmor);
        std::string colRight = ui::Loc("ЦЕНА", "PRICE");
        if (hasInv) {
            char ch[32];
            std::snprintf(ch, sizeof(ch), "%s %d/%d",
                          ui::Loc("ЗАПАС", "SPACE").c_str(),
                          GetBusArmorUsedCap(bus), GetBusArmorMaxCap(bus));
            colRight = ch;
        }
        menu.SetTitle(ui::Loc("БРОНЕЖИЛЕТ", "ARMOR"))
            .SetColumns(ui::Loc("ТИП", "TYPE"), colRight)
            .SetFooter(ui::StandardFooter("Купить", "Buy", "Назад", "Back"))
            .SetSelected(g_armorCursor);

        float curArm = sa::IsPedValid(ped) ? ped->GetArmor() : 0.0f;
        struct ArmOpt { const char* rus; const char* eng; float val; int price; };
        const ArmOpt opts[2] = {
            {"Надеть: Лёгкий 50%",   "Equip: Light 50%",   50.0f,  400},
            {"Надеть: Тяжёлый 100%", "Equip: Heavy 100%",  100.0f, 800},
        };
        for (int i = 0; i < 2; ++i) {
            std::string left = ui::Loc(opts[i].rus, opts[i].eng);
            int reqRank = prog_rules::AmmuEquipArmorRank(i);
            if (curArm >= opts[i].val - 0.5f) {
                menu.AddItem(left, ui::Loc("Надето", "Owned"), ui::ColorSuccess(), true);
            } else if (!modbus::IsRankUnlocked(reqRank)) {
                menu.AddItem(left, ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
            } else {
                menu.AddItem(left, common::FormatPrice(opts[i].price, useRus),
                             (money >= opts[i].price) ? ui::ColorNormal() : ui::ColorDanger(), true);
            }
        }

        if (hasInv && bus->inventoryGetArmorCount) {
            struct SpareOpt { const char* rus; const char* eng; };
            static constexpr SpareOpt kSpares[4] = {
                {"Запас: Лёгкий 25%",     "Spare: Light 25%"},
                {"Запас: Средний 50%",    "Spare: Standard 50%"},
                {"Запас: Тяжёлый 75%",    "Spare: Heavy 75%"},
                {"Запас: Сверхтяж. 100%", "Spare: Super Heavy 100%"},
            };
            int usedCap = GetBusArmorUsedCap(bus);
            int maxCap  = GetBusArmorMaxCap(bus);
            int freeCap = std::max(0, maxCap - usedCap);
            for (int t = 0; t < 4; ++t) {
                int reqRank = prog_rules::SpareArmorRank(t);
                if (!modbus::IsRankUnlocked(reqRank)) {
                    menu.AddItem(ui::Loc(kSpares[t].rus, kSpares[t].eng), ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
                    continue;
                }
                int cur    = bus->inventoryGetArmorCount(t);
                int weight = GetBusArmorWeight(bus, t);
                int price  = GetBusArmorPrice(bus, t);
                bool fits  = (weight <= freeCap);
                char rb[40];
                sa::CRGBA rcol = ui::ColorNormal();
                if (!fits) {
                    if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d %s", cur, ui::Loc("Полно", "Full").c_str());
                    else         std::snprintf(rb, sizeof(rb), "%s", ui::Loc("Нет места", "Full").c_str());
                    rcol = (cur > 0) ? ui::ColorSuccess() : ui::ColorDisabled();
                } else {
                    if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d  $%d", cur, price);
                    else         std::snprintf(rb, sizeof(rb), "$%d", price);
                    rcol = (money >= price) ? ui::ColorNormal() : ui::ColorDanger();
                }
                menu.AddItem(ui::Loc(kSpares[t].rus, kSpares[t].eng), rb, rcol, true);
            }
        }
    } else if (g_screen == Screen::WeaponList) {
        const auto& cinfo = kCategories[std::clamp(g_catCursor, 0, kCategoryCount - 1)];
        menu.SetTitle(ui::Loc(cinfo.rus, cinfo.eng))
            .SetColumns(ui::Loc("ОРУЖИЕ", "WEAPON"), ui::Loc("ЦЕНА", "STATUS"))
            .SetFooter(ui::StandardFooter("Выбрать", "Select", "Назад", "Back"))
            .SetSelected(g_weapCursor);

        for (const auto* def : g_catWeapons) {
            std::string left = ui::Loc(def->nameRus, def->nameEng);
            int reqRank = prog_rules::WeaponUnlockRank(def->id);
            if (wstore::Store().PlayerHasWeaponOrOwned(ped, def->id)) {
                menu.AddItem(left, ui::Loc("Куплено", "Owned"), ui::ColorSuccess(), true);
            } else if (!modbus::IsRankUnlocked(reqRank)) {
                menu.AddItem(left, ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
            } else {
                menu.AddItem(left, common::FormatPrice(def->buyPrice, useRus),
                             (money >= def->buyPrice) ? ui::ColorNormal() : ui::ColorDanger(), true);
            }
        }
    } else if (g_screen == Screen::WeaponMods) {
        const wdata::WeaponDef* def = wdata::FindDef(g_activeWeaponId);
        const auto& L = wstore::Store().Get(g_activeWeaponId);
        menu.SetTitle(def ? ui::Loc(def->nameRus, def->nameEng) : ui::Loc("ОРУЖИЕ", "WEAPON"))
            .SetColumns(ui::Loc("МОДУЛИ И ПАТРОНЫ", "MODS & AMMO"), ui::Loc("СТАТУС", "STATUS"))
            .SetFooter(ui::StandardFooter("Выбрать", "Select", "Назад", "Back", "Q/E Кам., Z/X Прибл.", "Q/E Cam, Z/X Zoom"))
            .SetSelected(g_modCursor);

        int curAmmo = def ? wstore::Store().GetWeaponAmmo(ped, def) : 0;
        for (ModSlot slot : g_modSlots) {
            if (slot == ModSlot::AmmoPack && def) {
                char lbuf[48];
                std::snprintf(lbuf, sizeof(lbuf), "%s +%d", ui::Loc("Патроны", "Ammo").c_str(), def->ammoPackCount);
                if (curAmmo >= def->maxAmmo) {
                    menu.AddItem(lbuf, ui::Loc("Макс.", "Full"), ui::ColorSuccess());
                } else {
                    menu.AddItem(lbuf, common::FormatPrice(def->ammoPackPrice, useRus),
                                 (money >= def->ammoPackPrice) ? ui::ColorNormal() : ui::ColorDanger());
                }
            } else if (slot == ModSlot::AmmoFull && def) {
                std::string left = ui::Loc("Макс. патронов", "Fill Ammo");
                if (curAmmo >= def->maxAmmo) {
                    menu.AddItem(left, ui::Loc("Макс.", "Full"), ui::ColorSuccess());
                } else {
                    int need  = def->maxAmmo - curAmmo;
                    int packs = std::max(1, (need + def->ammoPackCount - 1) / def->ammoPackCount);
                    int price = packs * def->ammoPackPrice;
                    menu.AddItem(left, common::FormatPrice(price, useRus),
                                 (money >= price) ? ui::ColorNormal() : ui::ColorDanger());
                }
            } else {
                menu.AddItem(ModSlotTitle(slot), SlotActiveSummary(L, slot), ui::ColorMuted());
            }
        }
    } else if (g_screen == Screen::ModSlotOptions) {
        menu.SetTitle(ModSlotTitle(g_activeModSlot))
            .SetColumns(ui::Loc("ВАРИАНТ", "PART"), ui::Loc("ЦЕНА", "PRICE"))
            .SetFooter(ui::StandardFooter("Установить", "Equip", "Назад", "Back", "Q/E Кам., Z/X Прибл.", "Q/E Cam, Z/X Zoom"))
            .SetSelected(g_optCursor);

        for (const auto& opt : g_slotOptions) {
            if (opt.equipped) {
                menu.AddItem(opt.labelGame, ui::Loc("Уст.", "Equipped"), ui::ColorSuccess(), true);
            } else if (opt.owned) {
                menu.AddItem(opt.labelGame, ui::Loc("Есть", "Owned"), ui::ColorNormal(), true);
            } else if (!modbus::IsRankUnlocked(opt.reqRank)) {
                menu.AddItem(opt.labelGame, ui::FormatRankLock(opt.reqRank), ui::ColorDisabled(), false);
            } else {
                menu.AddItem(opt.labelGame, common::FormatPrice(opt.price, useRus),
                             (money >= opt.price) ? ui::ColorNormal() : ui::ColorDanger(), true);
            }
        }
    }

    menu.Render();
    DrawBottomRightWeaponStats(ped);
}

bool IsShopActive() {
    return g_shopActive || g_forwardExit;
}

bool ShouldClaimAmmuPanel() {
    return g_shopActive || g_forwardExit || (GetAmmuPanel() != nullptr);
}

void Reset() {
    g_shopActive          = false;
    g_forwardExit         = false;
    g_exitCooldownUntilMs = 0;
    g_screen              = Screen::Categories;
    g_activeWeaponId      = 0;
    EndInspectCam();
    wdata::WeaponLoadout empty{};
    wvis::SetPreviewOverride(false, 0, empty);
}

} // namespace ammu
