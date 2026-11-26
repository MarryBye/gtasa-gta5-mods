#include "inv_menu.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <windows.h>
#include "bass_audio.hpp"
#include "food_shop.hpp"
#include "inv_data.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace invmenu {

namespace {

enum class Screen : uint8_t {
    Closed = 0,
    Categories,
    Snacks,
    Armor
};

Screen   g_screen          = Screen::Closed;
bool     g_openedFromPhone = false;
int      g_catCursor       = 0;
int      g_snackCursor     = 0;
int      g_armorCursor     = 0;
uint32_t g_lastUseMs       = 0;

// Synthesized crisp eat-crunch & armor-velcro sounds via shared bass_audio.hpp
bass::HSAMPLE g_sndEat   = 0;
bass::HSAMPLE g_sndArmor = 0;

void EnsureAudioLoaded() {
    static bool s_inited = false;
    if (s_inited) return;
    s_inited = true;
    if (!bass::EnsureLoaded()) return;

    // Short crunchy snack sound (0.16s filtered burst)
    std::vector<int16_t> eatPcm(22050 * 16 / 100);
    uint32_t seed = 0x13579BDFu;
    for (size_t i = 0; i < eatPcm.size(); ++i) {
        float t = static_cast<float>(i) / 22050.0f;
        seed = seed * 1664525u + 1013904223u;
        float noise = (static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f);
        float env = std::sin(t * 65.0f) * std::exp(-t * 14.0f);
        eatPcm[i] = static_cast<int16_t>(std::clamp(noise * env * 0.45f, -1.0f, 1.0f) * 28000.0f);
    }
    auto eatWav = bass::BuildWav16Mono(eatPcm, 22050);
    g_sndEat = bass::LoadSample("", eatWav, 2);

    // Crisp vest zip/velcro click (0.22s mid-low thump + zip)
    std::vector<int16_t> armPcm(22050 * 22 / 100);
    for (size_t i = 0; i < armPcm.size(); ++i) {
        float t = static_cast<float>(i) / 22050.0f;
        seed = seed * 1664525u + 1013904223u;
        float noise = (static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f);
        float thump = std::sin(2.0f * 3.14159265f * (150.0f - t * 280.0f) * t) * std::exp(-t * 18.0f);
        float zip   = noise * 0.25f * std::exp(-t * 11.0f);
        armPcm[i] = static_cast<int16_t>(std::clamp((thump * 0.65f + zip) * 0.7f, -1.0f, 1.0f) * 28000.0f);
    }
    auto armWav = bass::BuildWav16Mono(armPcm, 22050);
    g_sndArmor = bass::LoadSample("", armWav, 2);
}

void CloseAndMaybeReturn() {
    bool backToPhone = g_openedFromPhone;
    g_screen          = Screen::Closed;
    g_openedFromPhone = false;
    if (backToPhone) {
        if (auto* b = modbus::GetBus(); b && b->phoneInstalled && b->phoneReturnToMain) {
            b->phoneReturnToMain();
        }
    }
}

void ConsumeSnack(int idx) {
    if (idx < 0 || idx >= inv::FOOD_COUNT) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;

    auto& st = inv::Store();
    if (st.food[idx] <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Нет в наличии!~w~ Купите в любом ресторане.", "~r~Out of stock!~w~ Buy more at any restaurant.");
        return;
    }

    float maxHp = std::max(100.0f, ped->GetMaxHealth());
    float curHp = ped->GetHealth();
    if (curHp >= maxHp - 0.5f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Здоровье уже полное!", "Health is already full!");
        return;
    }

    uint32_t now = sa::GetTimeMs();
    if (now - g_lastUseMs < 250) return;
    g_lastUseMs = now;

    const auto& d = inv::kFoodDefs[idx];
    st.food[idx]--;
    ped->SetHealth(std::min(maxHp, curHp + static_cast<float>(d.healHp)));

    if (d.fatGain > 0.01f) {
        float fat = sa::stats::Get(sa::stats::STAT_FAT);
        sa::stats::Set(sa::stats::STAT_FAT, std::min(1000.0f, fat + d.fatGain));
    }

    st.SaveNow();
    EnsureAudioLoaded();
    if (g_sndEat) bass::PlaySample(g_sndEat, 0.55f, 22050.0f);
    else          sa::PlayFrontendSound(sa::SFX_SELECT);
}

void EquipSpareArmor(int idx) {
    if (idx < 0 || idx >= inv::ARMOR_COUNT) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;

    auto& st = inv::Store();
    if (st.armor[idx] <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Нет запасных броников этого типа!~w~ Купите в Амму-Нации или ресторане.",
                     "~r~No spare armor of this type!~w~ Buy more at Ammu-Nation or a shop.");
        return;
    }

    const auto& d = inv::kArmorDefs[idx];
    if (ped->GetArmor() >= d.armorValue - 0.5f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Текущая броня уже не ниже этого бронежилета!", "Current armor is already at or above this tier!");
        return;
    }

    uint32_t now = sa::GetTimeMs();
    if (now - g_lastUseMs < 250) return;
    g_lastUseMs = now;

    st.armor[idx]--;
    ped->SetArmor(d.armorValue);
    st.SaveNow();

    EnsureAudioLoaded();
    if (g_sndArmor) bass::PlaySample(g_sndArmor, 0.65f, 22050.0f);
    else            sa::PlayFrontendSound(sa::SFX_BUY);

    ui::ShowHelp("~g~Запасной бронежилет надет!", "~g~Spare body armor equipped!");
}

} // namespace

void Open(bool fromPhone, int tab) {
    if (foodshop::IsShopActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    g_openedFromPhone = fromPhone;
    if (tab == 1)      g_screen = Screen::Snacks;
    else if (tab == 2) g_screen = Screen::Armor;
    else               g_screen = Screen::Categories;
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void Tick() {
    if (sa::GetGameState() != 9) return;
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning() || foodshop::IsShopActive()) {
        if (g_screen != Screen::Closed) { g_screen = Screen::Closed; g_openedFromPhone = false; }
        return;
    }

    const auto& k = ui::Keys();
    auto* bus = modbus::GetBus();
    bool phoneInstalled = (bus && bus->phoneInstalled);

    // Standalone F7 hotkey — AUTOMATICALLY SUPPRESSED when GTA5Phone.asi or SA-COOP overlay is active!
    if (!phoneInstalled && !sa::IsExternalOverlayActive() && k.JustPressed(inv::Cfg().openKey)) {
        if (g_screen != Screen::Closed) {
            g_screen = Screen::Closed;
            g_openedFromPhone = false;
            sa::PlayFrontendSound(sa::SFX_BACK);
            return;
        }
        if (!modbus::IsAnyModalMenuOpen("inventory") && sa::GetNumMenusInUse() == 0) {
            g_screen          = Screen::Categories;
            g_openedFromPhone = false;
            sa::PlayFrontendSound(sa::SFX_SELECT);
            return;
        }
    }

    if (g_screen == Screen::Closed) return;

    const bool up     = k.Repeated(VK_UP) || k.Repeated('W');
    const bool down   = k.Repeated(VK_DOWN) || k.Repeated('S');
    const bool accept = k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE);
    const bool back   = k.JustPressed(VK_BACK) || k.JustPressed(VK_ESCAPE) || (phoneInstalled && k.JustPressed(VK_F6));

    if (g_screen == Screen::Categories) {
        if (up || down) {
            g_catCursor = 1 - g_catCursor;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (accept) {
            g_screen = (g_catCursor == 0) ? Screen::Snacks : Screen::Armor;
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            CloseAndMaybeReturn();
        }
    } else if (g_screen == Screen::Snacks) {
        if (up) {
            g_snackCursor = (g_snackCursor - 1 + inv::FOOD_COUNT) % inv::FOOD_COUNT;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (down) {
            g_snackCursor = (g_snackCursor + 1) % inv::FOOD_COUNT;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (accept) {
            ConsumeSnack(g_snackCursor);
        } else if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            if (g_openedFromPhone) CloseAndMaybeReturn();
            else g_screen = Screen::Categories;
        }
    } else if (g_screen == Screen::Armor) {
        if (up) {
            g_armorCursor = (g_armorCursor - 1 + inv::ARMOR_COUNT) % inv::ARMOR_COUNT;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (down) {
            g_armorCursor = (g_armorCursor + 1) % inv::ARMOR_COUNT;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (accept) {
            EquipSpareArmor(g_armorCursor);
        } else if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            if (g_openedFromPhone) CloseAndMaybeReturn();
            else g_screen = Screen::Categories;
        }
    }
}

void Render2D() {
    if (g_screen == Screen::Closed || sa::IsPauseMenuActive()) return;

    const auto& st  = inv::Store();
    const auto& cfg = inv::Cfg();
    sa::CPed* ped   = sa::FindPlayerPed(0);

    ui::ListMenu menu;
    menu.SetPosition(24.0f, 106.0f, 202.0f).SetMaxVisible(10);

    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc("ИНВЕНТАРЬ (КАРМАН)", "POCKET INVENTORY"))
            .SetColumns(ui::Loc("РАЗДЕЛ", "SECTION"), ui::Loc("ЗАПАС", "STOCK"))
            .SetFooter(g_openedFromPhone
                ? ui::Loc("Enter - Открыть | Backspace - Телефон", "Enter - Open | Backspace - Phone")
                : ui::Loc("Enter - Открыть | F7 / Esc - Закрыть", "Enter - Open | F7 / Esc - Close"));

        char fb[32], ab[32];
        std::snprintf(fb, sizeof(fb), "%d %s", st.GetTotalFood(),  ui::Loc("шт.", "pcs").c_str());
        std::snprintf(ab, sizeof(ab), "%d %s", st.GetTotalArmor(), ui::Loc("шт.", "pcs").c_str());
        menu.AddItem(ui::Loc("Еда и Закуски", "Snacks & Food"), fb, {135, 230, 145, 255}, true);
        menu.AddItem(ui::Loc("Запасные бронежилеты", "Spare Body Armor"), ab, {140, 200, 255, 255}, true);
        menu.SetSelected(g_catCursor);
    } else if (g_screen == Screen::Snacks) {
        menu.SetTitle(ui::Loc("ЕДА И ЗАКУСКИ", "SNACKS & FOOD"))
            .SetColumns(ui::Loc("ПРОДУКТ", "SNACK"), ui::Loc("ОСТАТОК", "QTY"))
            .SetFooter(ui::Loc("Enter - Съесть (+HP) | Backspace - Назад", "Enter - Eat (+HP) | Backspace - Back"));
        for (int i = 0; i < inv::FOOD_COUNT; ++i) {
            const auto& d = inv::kFoodDefs[i];
            int qty = st.food[i], mx = cfg.foodMax[i];
            char qb[32];
            std::snprintf(qb, sizeof(qb), "%d/%d (+%dHP)", qty, mx, d.healHp);
            sa::CRGBA col = (qty > 0) ? sa::CRGBA{135, 230, 145, 255} : sa::CRGBA{115, 115, 115, 255};
            menu.AddItem(ui::Loc(d.rus, d.eng), qb, col, qty > 0);
        }
        menu.SetSelected(g_snackCursor);
    } else if (g_screen == Screen::Armor) {
        menu.SetTitle(ui::Loc("БРОНЕЖИЛЕТЫ", "SPARE ARMOR"))
            .SetColumns(ui::Loc("ТИП БРОНИ", "ARMOR TIER"), ui::Loc("ОСТАТОК", "QTY"))
            .SetFooter(ui::Loc("Enter - Надеть | Backspace - Назад", "Enter - Equip | Backspace - Back"));
        for (int i = 0; i < inv::ARMOR_COUNT; ++i) {
            const auto& d = inv::kArmorDefs[i];
            int qty = st.armor[i], mx = cfg.armorMax[i];
            char qb[24];
            std::snprintf(qb, sizeof(qb), "%d/%d", qty, mx);
            sa::CRGBA col = (qty > 0) ? sa::CRGBA{140, 200, 255, 255} : sa::CRGBA{115, 115, 115, 255};
            menu.AddItem(ui::Loc(d.rus, d.eng), qb, col, qty > 0);
        }
        menu.SetSelected(g_armorCursor);
    }
    menu.Render();

    // Live Vitals Card (bottom-right) previewing exact HP / Armor recovery!
    int hp    = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetHealth())) : 100;
    int maxHp = sa::IsPedValid(ped) ? std::max(100, static_cast<int>(std::round(ped->GetMaxHealth()))) : 100;
    int arm   = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
    int prevHp  = hp;
    int prevArm = arm;

    if (g_screen == Screen::Snacks && st.food[g_snackCursor] > 0) {
        prevHp = std::min(maxHp, hp + inv::kFoodDefs[g_snackCursor].healHp);
    } else if (g_screen == Screen::Armor && st.armor[g_armorCursor] > 0) {
        prevArm = std::max(arm, static_cast<int>(std::round(inv::kArmorDefs[g_armorCursor].armorValue)));
    }

    ui::StatsCard card;
    card.SetTitle(ui::Loc("СОСТОЯНИЕ CJ", "CJ VITALS"))
        .SetAnchor(true, 148.0f, 16.0f, 18.0f)
        .AddBar(ui::Loc("Здоровье", "Health"), hp,  prevHp,  maxHp, "")
        .AddBar(ui::Loc("Броня",    "Armor"),  arm, prevArm, 100,   "%");
    card.Render();
}

bool IsOpen() { return g_screen != Screen::Closed; }

void Reset() {
    g_screen          = Screen::Closed;
    g_openedFromPhone = false;
}

} // namespace invmenu
