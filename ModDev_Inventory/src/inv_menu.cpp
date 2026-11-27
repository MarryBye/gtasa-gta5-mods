#include "inv_menu.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include "food_shop.hpp"
#include "integrations/bass_audio.hpp"
#include "integrations/sacoop.hpp"
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
bool     g_openedDirectTab = false;
int      g_catCursor       = 0;
int      g_snackCursor       = 0;
int      g_armorCursor       = 0;
uint32_t g_lastUseMs         = 0;
bool     g_pendingFatRebuild = false;

bass::HSAMPLE g_sndEat   = 0;
bass::HSAMPLE g_sndArmor = 0;
bool          g_audioInit = false;

void EnsureAudioLoaded() {
    if (g_audioInit) return;
    g_audioInit = true;
    if (!bass::EnsureLoaded()) return;

    std::vector<int16_t> eatPcm(22050 * 16 / 100);
    uint32_t seed = 0x13579BDFu;
    for (size_t i = 0; i < eatPcm.size(); ++i) {
        float t = static_cast<float>(i) / 22050.0f;
        float noise = bass::NextNoise(seed);
        float env = std::sin(t * 65.0f) * std::exp(-t * 14.0f);
        eatPcm[i] = bass::FloatToPcm16(noise * env * 0.40f);
    }
    auto eatWav = bass::BuildWav16Mono(eatPcm, 22050);
    g_sndEat = bass::LoadSample("", eatWav, 2);

    std::vector<int16_t> armPcm(22050 * 22 / 100);
    for (size_t i = 0; i < armPcm.size(); ++i) {
        float t = static_cast<float>(i) / 22050.0f;
        float noise = bass::NextNoise(seed);
        float thump = std::sin(2.0f * 3.14159265f * (150.0f - t * 280.0f) * t) * std::exp(-t * 18.0f);
        float zip   = noise * 0.25f * std::exp(-t * 11.0f);
        armPcm[i] = bass::FloatToPcm16((thump * 0.65f + zip) * 0.62f);
    }
    auto armWav = bass::BuildWav16Mono(armPcm, 22050);
    g_sndArmor = bass::LoadSample("", armWav, 2);
}

void CloseAndMaybeReturn() {
    bool backToPhone = g_openedFromPhone;
    g_screen          = Screen::Closed;
    g_openedFromPhone = false;
    g_openedDirectTab = false;
    if (backToPhone) {
        ui::LockMenuInputBriefly(180);
        if (auto* b = modbus::GetBus(); b && b->phoneInstalled && b->phoneReturnToMain) {
            b->phoneReturnToMain();
        }
    } else {
        ui::TriggerPostCloseShield(160);
    }
}

void ConsumeSnack(int idx) {
    if (idx < 0 || idx >= inv::FOOD_COUNT) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;

    auto& st = inv::Store();
    if (st.food[idx] <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Нет в наличии.~w~ Купите еду в ресторане.", "~r~Out of stock.~w~ Buy food at any restaurant.");
        return;
    }

    float maxHp = std::max(100.0f, ped->GetMaxHealth());
    float curHp = ped->GetHealth();
    if (curHp >= maxHp - 0.5f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Здоровье уже полное.", "Health is already full.");
        return;
    }

    uint32_t now = sa::GetTimeMs();
    if (now - g_lastUseMs < 250) return;
    g_lastUseMs = now;

    const auto& d = inv::kFoodDefs[idx];
    st.food[idx]--;
    ped->SetHealth(std::min(maxHp, curHp + static_cast<float>(d.healHp)));

    // Authentic vanilla GTA SA obesity & hunger integration:
    // Heavier/stronger healing fast food adds more STAT_FAT via CStats::IncrementStat (0x0055C180)
    // and schedules a safe CClothes::RebuildPlayer(ped, false) so CJ visibly gains weight!
    const float fatGain = d.fatGain * inv::Cfg().fatGainMultiplier;
    if (fatGain > 0.01f) {
        sa::stats::Increment(sa::stats::STAT_FAT, fatGain);
        auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
        const float curFat = sa::stats::Get(sa::stats::STAT_FAT);
        if (!desc || std::fabs(curFat - desc->m_fFatStat) >= 15.0f || fatGain >= 20.0f) {
            g_pendingFatRebuild = true;
        }
    }
    // Reset vanilla starvation timer cleanly without causing extra overflow fat gain
    if (sa::stats::Get(sa::stats::STAT_CALORIES) < 250.0f) {
        sa::stats::Set(sa::stats::STAT_CALORIES, 250.0f);
    }

    st.SaveNow();
    EnsureAudioLoaded();
    if (g_sndEat) bass::PlaySample(g_sndEat, 0.55f, 22050.0f);
    else          sa::PlayFrontendSound(sa::SFX_SELECT);
}

void ProcessPendingFatRebuild() {
    if (!g_pendingFatRebuild) return;
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 1.0f) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        g_pendingFatRebuild = false;
        return;
    }
    // Defer 3D body rebuild until CJ is safely on foot (avoids interrupting vehicle seat animations)
    if (ped->IsInVehicle() || sa::FindPlayerVehicle(0, true) != nullptr) return;
    g_pendingFatRebuild = false;
    sa::clothes::RebuildPlayer(ped, false);
}

void EquipSpareArmor(int idx) {
    if (idx < 0 || idx >= inv::ARMOR_COUNT) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;

    auto& st = inv::Store();
    if (st.armor[idx] <= 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Нет запасных бронежилетов этого типа.~w~ Купите их в Амму-Нации.",
                     "~r~No spare armor of this type.~w~ Buy more at Ammu-Nation.");
        return;
    }

    const auto& d = inv::kArmorDefs[idx];
    if (ped->GetArmor() >= d.armorValue - 0.5f) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("Текущая броня уже не ниже этого бронежилета.", "Current armor is already at or above this tier.");
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

    ui::ShowHelp("~g~Запасной бронежилет надет.", "~g~Spare body armor equipped.");
}

bool IsPlayerAlive() {
    if (sa::GetPlayerState(0) != 0) return false;
    sa::CPed* ped = sa::FindPlayerPed(0);
    return sa::IsPedValid(ped) && ped->GetHealth() > 0.0f;
}

} // namespace

void Open(bool fromPhone, int tab) {
    if (foodshop::IsShopActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning() || !IsPlayerAlive()) return;
    g_openedFromPhone = fromPhone;
    g_openedDirectTab = (tab == 1 || tab == 2);
    if (tab == 1)      g_screen = Screen::Snacks;
    else if (tab == 2) g_screen = Screen::Armor;
    else               g_screen = Screen::Categories;
    ui::LockMenuInputBriefly(200);
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void PreScriptsTick() {
    if (sa::GetGameState() != 9) return;
    ProcessPendingFatRebuild();
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning() || foodshop::IsShopActive() || !IsPlayerAlive()) {
        if (g_screen != Screen::Closed) {
            g_screen          = Screen::Closed;
            g_openedFromPhone = false;
            g_openedDirectTab = false;
        }
        return;
    }

    // Priority 1 Story Phone Call: yield immediately so player can answer with TAB!
    const bool storyPhoneActive = modbus::IsStoryPhoneBlocking() || sa::IsStoryPhoneRinging();
    if (storyPhoneActive && g_screen != Screen::Closed && !g_openedFromPhone) {
        g_screen          = Screen::Closed;
        g_openedDirectTab = false;
        return;
    }

    const auto& k = ui::Keys();
    auto* bus = modbus::GetBus();
    bool phoneInstalled = (bus && bus->phoneInstalled);

    // Standalone F7 hotkey — suppressed when GTA5Phone, SA-COOP overlay, or Story Phone is active
    if (!phoneInstalled && !storyPhoneActive && !integrations::sacoop::IsOverlayActive() && k.JustPressed(inv::Cfg().openKey)) {
        if (g_screen != Screen::Closed) {
            g_screen          = Screen::Closed;
            g_openedFromPhone = false;
            g_openedDirectTab = false;
            ui::TriggerPostCloseShield(160);
            sa::PlayFrontendSound(sa::SFX_BACK);
            return;
        }
        if (!modbus::IsAnyModalMenuOpen("inventory") && sa::GetNumMenusInUse() == 0) {
            g_screen          = Screen::Categories;
            g_openedFromPhone = false;
            g_openedDirectTab = false;
            ui::LockMenuInputBriefly(180);
            sa::PlayFrontendSound(sa::SFX_SELECT);
            return;
        }
    }

    if (g_screen == Screen::Closed) {
        ui::UpdatePostCloseShield();
        return;
    }

    auto in = ui::ListInput().Poll(true, true);
    bool phoneClose = (phoneInstalled && !integrations::sacoop::IsOverlayActive() && k.JustPressed(modbus::GetPhoneKey()));

    if (g_screen == Screen::Categories) {
        if (in.up || in.down) {
            g_catCursor = 1 - g_catCursor;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.accept) {
            g_screen = (g_catCursor == 0) ? Screen::Snacks : Screen::Armor;
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (in.back || phoneClose) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            CloseAndMaybeReturn();
        }
    } else if (g_screen == Screen::Snacks) {
        if (ui::ListInput().ApplyToCursor(g_snackCursor, inv::FOOD_COUNT, in)) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.left || in.right) {
            g_screen    = Screen::Armor;
            g_catCursor = 1;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.accept) {
            ConsumeSnack(g_snackCursor);
        } else if (in.back || phoneClose) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            if (phoneClose || g_openedDirectTab) CloseAndMaybeReturn();
            else g_screen = Screen::Categories;
        }
    } else if (g_screen == Screen::Armor) {
        if (ui::ListInput().ApplyToCursor(g_armorCursor, inv::ARMOR_COUNT, in)) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.left || in.right) {
            g_screen    = Screen::Snacks;
            g_catCursor = 0;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.accept) {
            EquipSpareArmor(g_armorCursor);
        } else if (in.back || phoneClose) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            if (phoneClose || g_openedDirectTab) CloseAndMaybeReturn();
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
    menu.SetPosition(24.0f, 106.0f, 194.0f).SetMaxVisible(10);

    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc("ЕДА И БРОНЯ", "FOOD & ARMOR"))
            .SetColumns(ui::Loc("РАЗДЕЛ", "SECTION"), ui::Loc("ЗАПАС", "CAPACITY"))
            .SetFooter(g_openedFromPhone
                ? ui::StandardFooter("Открыть", "Open", "Телефон", "Phone")
                : ui::StandardFooter("Открыть", "Open", "Закрыть", "Close"));

        char fb[32], ab[32];
        std::snprintf(fb, sizeof(fb), "%d/%d", st.GetUsedFoodCapacity(),  cfg.maxFoodCapacity);
        std::snprintf(ab, sizeof(ab), "%d/%d", st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
        menu.AddItem(ui::Loc("Еда и закуски", "Snacks & Food"), fb, ui::ColorSuccess(), true);
        menu.AddItem(ui::Loc("Запасные бронежилеты", "Spare Body Armor"), ab, ui::ColorWarning(), true);
        menu.SetSelected(g_catCursor);
    } else if (g_screen == Screen::Snacks) {
        char colR[32];
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("КАРМАН", "POCKET").c_str(),
                      st.GetUsedFoodCapacity(), cfg.maxFoodCapacity);
        menu.SetTitle(ui::Loc("ЕДА И ЗАКУСКИ", "SNACKS & FOOD"))
            .SetColumns(ui::Loc("ПРОДУКТ", "SNACK"), colR)
            .SetFooter(ui::StandardFooter("Съесть", "Eat", "Назад", "Back", "A/D - Броня", "A/D - Armor"));
        for (int i = 0; i < inv::FOOD_COUNT; ++i) {
            const auto& d = inv::kFoodDefs[i];
            int qty = st.food[i];
            char qb[32];
            std::snprintf(qb, sizeof(qb), "x%d +%d %s", qty, d.healHp, ui::Loc("ЗД", "HP").c_str());
            sa::CRGBA col = (qty > 0) ? ui::ColorSuccess() : ui::ColorDisabled();
            menu.AddItem(ui::Loc(d.rus, d.eng), qb, col, qty > 0);
        }
        menu.SetSelected(g_snackCursor);
    } else if (g_screen == Screen::Armor) {
        char colR[32];
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("ЗАПАС", "SPACE").c_str(),
                      st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
        menu.SetTitle(ui::Loc("ЗАПАСНЫЕ БРОНЕЖИЛЕТЫ", "SPARE BODY ARMOR"))
            .SetColumns(ui::Loc("ТИП БРОНИ", "ARMOR TIER"), colR)
            .SetFooter(ui::StandardFooter("Надеть", "Equip", "Назад", "Back", "A/D - Еда", "A/D - Food"));
        for (int i = 0; i < inv::ARMOR_COUNT; ++i) {
            const auto& d = inv::kArmorDefs[i];
            int qty = st.armor[i];
            char qb[24];
            std::snprintf(qb, sizeof(qb), "x%d", qty);
            sa::CRGBA col = (qty > 0) ? ui::ColorSuccess() : ui::ColorDisabled();
            menu.AddItem(ui::Loc(d.rus, d.eng), qb, col, qty > 0);
        }
        menu.SetSelected(g_armorCursor);
    }
    menu.Render();

    int hp      = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetHealth())) : 100;
    int maxHp   = sa::IsPedValid(ped) ? std::max(100, static_cast<int>(std::round(ped->GetMaxHealth()))) : 100;
    int arm     = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
    int fat     = std::clamp(static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f)), 0, 100);
    int prevHp  = hp;
    int prevArm = arm;
    int prevFat = fat;

    std::string note;
    if (g_screen == Screen::Snacks) {
        const auto& fd = inv::kFoodDefs[g_snackCursor];
        const float gainPct = fd.fatGain * cfg.fatGainMultiplier * 0.1f;
        if (st.food[g_snackCursor] > 0) {
            prevHp  = std::min(maxHp, hp + fd.healHp);
            prevFat = std::min(100, static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f + gainPct)));
        }
        char nb[80];
        if (gainPct >= 0.05f) {
            std::snprintf(nb, sizeof(nb), "+%d %s, +%.1f%% %s",
                          fd.healHp, ui::Loc("ЗД", "HP").c_str(),
                          gainPct, ui::Loc("вес", "fat").c_str());
        } else {
            std::snprintf(nb, sizeof(nb), "+%d %s, %s",
                          fd.healHp, ui::Loc("ЗД", "HP").c_str(),
                          ui::Loc("без набора веса", "no fat gain").c_str());
        }
        note = nb;
    } else if (g_screen == Screen::Armor) {
        if (st.armor[g_armorCursor] > 0) {
            prevArm = std::max(arm, static_cast<int>(std::round(inv::kArmorDefs[g_armorCursor].armorValue)));
        }
        char nb[64];
        std::snprintf(nb, sizeof(nb), "%s: %d/%d",
                      ui::Loc("Запас брони", "Armor Space").c_str(), st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
        note = nb;
    } else {
        char nb[64];
        std::snprintf(nb, sizeof(nb), "%s: %d/%d, %s: %d/%d",
                      ui::Loc("Еда", "Food").c_str(), st.GetUsedFoodCapacity(), cfg.maxFoodCapacity,
                      ui::Loc("Броня", "Armor").c_str(), st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
        note = nb;
    }

    ui::StatsCard card;
    card.SetTitle(ui::Loc("СОСТОЯНИЕ", "VITALS"))
        .SetAnchor(true, 0.0f, 16.0f, 18.0f)
        .SetNote(note)
        .AddBar(ui::Loc("Здоровье", "Health"), hp,  prevHp,  maxHp, "")
        .AddBar(ui::Loc("Броня",    "Armor"),  arm, prevArm, 100,   "%");
    if (g_screen == Screen::Snacks) {
        card.AddBar(ui::Loc("Вес", "Fat"), fat, prevFat, 100, "%");
    }
    card.Render();
}

bool IsOpen() { return g_screen != Screen::Closed; }

void Reset() {
    g_screen            = Screen::Closed;
    g_openedFromPhone   = false;
    g_openedDirectTab   = false;
    g_pendingFatRebuild = false;
}

void Shutdown() {
    Reset();
    bass::FreeSample(g_sndEat);
    bass::FreeSample(g_sndArmor);
}

} // namespace invmenu
