#include "food_shop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "common_utils.hpp"
#include "inv_data.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "progression_rules.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace foodshop {

namespace {

enum class ShopScreen : uint8_t {
    FoodItems = 0,
    ArmorItems
};

using FnMenuProcess = void(__cdecl*)(int8_t);
FnMenuProcess g_origMenuProcess = nullptr;

bool        g_shopActive         = false;
bool        g_forwardExit        = false;
uint32_t    g_exitCooldownUntil  = 0;
int         g_exitPulseCounter   = 0;
ShopScreen  g_screen             = ShopScreen::FoodItems;
int         g_cursor             = 0;
uint8_t     g_currentShopMask    = inv::SHOP_ALL;
std::string g_shopTitleRus       = "РЕСТОРАН";
std::string g_shopTitleEng       = "RESTAURANT";

struct ShopRow {
    int type;  // 0=Food, 1=FillPocketFood, 2=OpenArmorSubmenu, 3=Armor
    int index;
};
std::vector<ShopRow> g_rows;

sa::tMenuPanel* GetFoodPanel() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (std::strncmp(p->m_acTitle, "FOOD", 4) != 0) return nullptr;

    // Story Mission Safety: Mission 2 ("Ryder") requires buying vanilla pizza at Well Stacked Pizza (FDPIZA)
    if (sa::IsOnMission()) {
        const char* sname = sa::shopping::GetLoadedShopName();
        if (sname && _strnicmp(sname, "FDPIZA", 6) == 0) {
            return nullptr;
        }
    }
    return p;
}

void DetectRestaurantType() {
    const char* sname = sa::shopping::GetLoadedShopName();
    g_currentShopMask = inv::SHOP_ALL;
    if (sname && _strnicmp(sname, "FDBURG", 6) == 0) {
        g_currentShopMask |= inv::SHOP_BURGER;
        g_shopTitleRus = "BURGER SHOT";
        g_shopTitleEng = "BURGER SHOT";
    } else if (sname && _strnicmp(sname, "FDPIZA", 6) == 0) {
        g_currentShopMask |= inv::SHOP_PIZZA;
        g_shopTitleRus = "PIZZA STACK";
        g_shopTitleEng = "PIZZA STACK";
    } else if (sname && _strnicmp(sname, "FDCHICK", 7) == 0) {
        g_currentShopMask |= inv::SHOP_CLUCK;
        g_shopTitleRus = "CLUCKIN' BELL";
        g_shopTitleEng = "CLUCKIN' BELL";
    } else {
        g_currentShopMask = inv::SHOP_ALL | inv::SHOP_BURGER | inv::SHOP_PIZZA | inv::SHOP_CLUCK;
        g_shopTitleRus = "ЗАКУСОЧНАЯ";
        g_shopTitleEng = "DINER";
    }
}

void RebuildRows() {
    g_rows.clear();
    if (g_screen == ShopScreen::FoodItems) {
        g_rows.push_back({1, -1});
        for (int i = 0; i < inv::FOOD_COUNT; ++i) {
            if ((inv::kFoodDefs[i].shopMask & g_currentShopMask) != 0 && inv::kFoodDefs[i].shopMask != inv::SHOP_ALL) {
                g_rows.push_back({0, i});
            }
        }
        for (int i = inv::FOOD_COUNT - 1; i >= 0; --i) {
            if (inv::kFoodDefs[i].shopMask == inv::SHOP_ALL) {
                g_rows.push_back({0, i});
            }
        }
        if (inv::Cfg().allowArmorInFoodShops) {
            g_rows.push_back({2, -1});
        }
    } else {
        for (int i = 0; i < inv::ARMOR_COUNT; ++i) {
            g_rows.push_back({3, i});
        }
    }
    if (g_cursor >= static_cast<int>(g_rows.size())) g_cursor = 0;
}

struct FillPlan {
    int addCount[inv::FOOD_COUNT] = {};
    int totalCost  = 0;
    int totalItems = 0;
};

FillPlan ComputeSmartFillFoodPlan(int maxBudget = -1) {
    FillPlan plan{};
    const auto& cfg = inv::Cfg();
    int freeCap = inv::Store().GetFreeFoodCapacity();
    if (freeCap <= 0) return plan;

    std::vector<int> avail;
    for (const auto& r : g_rows) {
        if (r.type == 0 && modbus::IsRankUnlocked(prog_rules::FoodItemRank(r.index))) {
            avail.push_back(r.index);
        }
    }
    std::sort(avail.begin(), avail.end(), [](int a, int b) {
        return inv::kFoodDefs[a].healHp > inv::kFoodDefs[b].healHp;
    });

    int cashLeft = (maxBudget >= 0) ? maxBudget : 0x3FFFFFFF;
    bool progress = true;
    while (freeCap > 0 && progress) {
        progress = false;
        for (int idx : avail) {
            int w = cfg.foodWeight[idx];
            int p = cfg.foodPrice[idx];
            if (w <= freeCap && p <= cashLeft) {
                plan.addCount[idx]++;
                plan.totalCost += p;
                plan.totalItems++;
                freeCap  -= w;
                cashLeft -= p;
                progress = true;
                break;
            }
        }
    }
    return plan;
}

void CloseShopCleanly() {
    g_shopActive       = false;
    g_forwardExit      = true;
    g_exitPulseCounter = 0;
    ui::TriggerPostCloseShield(160);
    sa::PlayFrontendSound(sa::SFX_BACK);
}

void FormatStockRow(int cur, int pr, bool fit, int32_t money, std::string& outRight, sa::CRGBA& outCol) {
    char rb[48];
    if (!fit) {
        if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d %s", cur, ui::Loc("макс.", "max").c_str());
        else         std::snprintf(rb, sizeof(rb), "%s", ui::Loc("Нет места", "Full").c_str());
        outCol = (cur > 0) ? ui::ColorSuccess() : ui::ColorDisabled();
    } else {
        if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d  $%d", cur, pr);
        else         std::snprintf(rb, sizeof(rb), "$%d", pr);
        outCol = (money >= pr) ? ui::ColorNormal() : ui::ColorDanger();
    }
    outRight = rb;
}

void HandleSelect() {
    if (g_cursor < 0 || g_cursor >= static_cast<int>(g_rows.size())) return;
    const auto& row = g_rows[g_cursor];
    auto& st        = inv::Store();
    const auto& cfg = inv::Cfg();
    int32_t money   = sa::GetPlayerMoney();

    if (row.type == 0) {
        int idx = row.index;
        int reqRank = prog_rules::FoodItemRank(idx);
        if (!modbus::IsRankUnlocked(reqRank)) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowRankLockedHelp(reqRank);
            return;
        }
        int w   = cfg.foodWeight[idx];
        int freeCap = st.GetFreeFoodCapacity();
        if (w > freeCap) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            char rb[128], eb[128];
            std::snprintf(rb, sizeof(rb), "~r~Не хватает места в кармане: нужно %d, свободно %d.", w, freeCap);
            std::snprintf(eb, sizeof(eb), "~r~Not enough pocket space: needs %d, free %d.", w, freeCap);
            ui::ShowHelp(rb, eb);
            return;
        }
        int price = cfg.foodPrice[idx];
        if (money < price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег.", "~r~Not enough money.");
            return;
        }
        sa::AddPlayerMoney(-price);
        st.food[idx]++;
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
    } else if (row.type == 1) {
        if (st.GetFreeFoodCapacity() <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Карман для еды уже заполнен.", "Food pocket is already full.");
            return;
        }
        FillPlan plan = ComputeSmartFillFoodPlan(money);
        if (plan.totalItems <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег.", "~r~Not enough money.");
            return;
        }
        sa::AddPlayerMoney(-plan.totalCost);
        for (int i = 0; i < inv::FOOD_COUNT; ++i) st.food[i] += plan.addCount[i];
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Запас еды пополнен.", "~g~Pocket food stocked up.");
    } else if (row.type == 2) {
        g_screen = ShopScreen::ArmorItems;
        g_cursor = 0;
        RebuildRows();
        sa::PlayFrontendSound(sa::SFX_SELECT);
    } else if (row.type == 3) {
        int idx = row.index;
        int reqRank = prog_rules::SpareArmorRank(idx);
        if (!modbus::IsRankUnlocked(reqRank)) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowRankLockedHelp(reqRank);
            return;
        }
        int w   = cfg.armorWeight[idx];
        int freeCap = st.GetFreeArmorCapacity();
        if (w > freeCap) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            char rb[128], eb[128];
            std::snprintf(rb, sizeof(rb), "~r~Не хватает места для брони: нужно %d, свободно %d.", w, freeCap);
            std::snprintf(eb, sizeof(eb), "~r~Not enough armor space: needs %d, free %d.", w, freeCap);
            ui::ShowHelp(rb, eb);
            return;
        }
        int price = cfg.armorPrice[idx];
        if (money < price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег.", "~r~Not enough money.");
            return;
        }
        sa::AddPlayerMoney(-price);
        st.armor[idx]++;
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
    }
}

void ProcessInput() {
    auto in = ui::ListInput().Poll(true);
    int n = static_cast<int>(g_rows.size());
    if (n > 0) {
        if (ui::ListInput().ApplyToCursor(g_cursor, n, in)) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.accept) {
            HandleSelect();
        }
    }
    if (in.back) {
        if (g_screen == ShopScreen::ArmorItems) {
            g_screen = ShopScreen::FoodItems;
            g_cursor = 0;
            RebuildRows();
            sa::PlayFrontendSound(sa::SFX_BACK);
        } else {
            CloseShopCleanly();
        }
    }
}

void __cdecl HookedMenuProcess(int8_t menuId) {
    if (g_shopActive || g_forwardExit) return;
    if (GetFoodPanel() != nullptr) return;
    if (g_origMenuProcess) g_origMenuProcess(menuId);
}

} // namespace

void InstallHooks() {
    g_origMenuProcess = reinterpret_cast<FnMenuProcess>(
        mem::HookCall(0x0058FCDA, reinterpret_cast<uintptr_t>(&HookedMenuProcess)));
    modlog::Info("[FoodShop] Hooked CMenuSystem::Process at 0x0058FCDA.");
}

bool ShouldClaimFoodPanel() {
    return g_shopActive || g_forwardExit || (GetFoodPanel() != nullptr);
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    sa::tMenuPanel* foodPanel = GetFoodPanel();

    if (g_forwardExit) {
        if (!foodPanel) {
            g_forwardExit       = false;
            g_exitCooldownUntil = sa::GetTimeMs() + 900;
            return;
        }
        sa::ClearPadMenuInputs();
        uint8_t* pad = sa::GetPad0();
        if ((g_exitPulseCounter++ % 2) == 0) {
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 255;
            *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
        } else {
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 0;
            *reinterpret_cast<int16_t*>(pad + 0x4E) = 0;
        }
        return;
    }

    if (foodPanel && !g_shopActive) {
        if (sa::GetTimeMs() < g_exitCooldownUntil) {
            g_forwardExit      = true;
            g_exitPulseCounter = 0;
            return;
        }
        g_shopActive = true;
        g_screen     = ShopScreen::FoodItems;
        g_cursor     = 0;
        DetectRestaurantType();
        RebuildRows();
        ui::LockMenuInputBriefly(180);
        sa::PlayFrontendSound(sa::SFX_SELECT);
        modlog::Info("[FoodShop] Opened Take-Out Menu (shop='%s').", sa::shopping::GetLoadedShopName());
    } else if (!foodPanel && g_shopActive) {
        g_shopActive = false;
        return;
    }

    if (g_shopActive) {
        ProcessInput();
    }
}

void Render2D() {
    if (!g_shopActive || sa::IsPauseMenuActive()) return;

    const auto& st      = inv::Store();
    const auto& cfg     = inv::Cfg();
    const int32_t money = sa::GetPlayerMoney();
    const int usedFood  = st.GetUsedFoodCapacity();
    const int freeFood  = st.GetFreeFoodCapacity();
    const int usedArmor = st.GetUsedArmorCapacity();
    const int freeArmor = st.GetFreeArmorCapacity();

    char colR[36];
    if (g_screen == ShopScreen::FoodItems) {
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("КАРМАН", "POCKET").c_str(), usedFood, cfg.maxFoodCapacity);
    } else {
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("ЗАПАС", "SPACE").c_str(), usedArmor, cfg.maxArmorCapacity);
    }

    ui::ListMenu menu;
    menu.SetTitle(g_screen == ShopScreen::FoodItems
            ? ui::Loc(g_shopTitleRus.c_str(), g_shopTitleEng.c_str())
            : ui::Loc("ЗАПАСНЫЕ БРОНЕЖИЛЕТЫ", "SPARE BODY ARMOR"))
        .SetColumns(ui::Loc("ТОВАР", "ITEM"), colR)
        .SetFooter(ui::StandardFooter("Купить", "Buy", "Выход", "Exit"))
        .SetPosition(24.0f, 72.0f, 194.0f)
        .SetMaxVisible(10);

    for (const auto& r : g_rows) {
        if (r.type == 1) {
            FillPlan fullPlan = ComputeSmartFillFoodPlan(-1);
            std::string right = (fullPlan.totalItems > 0)
                ? common::FormatPrice(fullPlan.totalCost, ui::UseRussianFlag())
                : ui::Loc("Полно", "Full");
            sa::CRGBA col = (fullPlan.totalItems <= 0 || money >= fullPlan.totalCost)
                ? ui::ColorSuccess()
                : ui::ColorWarning();
            menu.AddItem(ui::Loc("Заполнить карман", "Fill Food Pocket"), right, col, true);
        } else if (r.type == 2) {
            char ab[32];
            std::snprintf(ab, sizeof(ab), "%d/%d", usedArmor, cfg.maxArmorCapacity);
            menu.AddItem(ui::Loc("Запасные бронежилеты", "Spare Body Armor"), ab, ui::ColorWarning(), true);
        } else if (r.type == 0) {
            const auto& d = inv::kFoodDefs[r.index];
            int reqRank = prog_rules::FoodItemRank(r.index);
            if (!modbus::IsRankUnlocked(reqRank)) {
                menu.AddItem(ui::Loc(d.rus, d.eng), ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
            } else {
                std::string rb;
                sa::CRGBA col = ui::ColorNormal();
                FormatStockRow(st.food[r.index], cfg.foodPrice[r.index], cfg.foodWeight[r.index] <= freeFood, money, rb, col);
                menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
            }
        } else if (r.type == 3) {
            const auto& d = inv::kArmorDefs[r.index];
            int reqRank = prog_rules::SpareArmorRank(r.index);
            if (!modbus::IsRankUnlocked(reqRank)) {
                menu.AddItem(ui::Loc(d.rus, d.eng), ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
            } else {
                std::string rb;
                sa::CRGBA col = ui::ColorNormal();
                FormatStockRow(st.armor[r.index], cfg.armorPrice[r.index], cfg.armorWeight[r.index] <= freeArmor, money, rb, col);
                menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
            }
        }
    }
    menu.SetSelected(g_cursor);
    menu.Render();

    sa::CPed* ped = sa::FindPlayerPed(0);
    int hp    = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetHealth())) : 100;
    int maxHp = sa::IsPedValid(ped) ? std::max(100, static_cast<int>(std::round(ped->GetMaxHealth()))) : 100;
    int arm   = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
    int fat   = static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f));

    ui::StatsCard card;
    if (g_screen == ShopScreen::ArmorItems) {
        int prevArmCap = usedArmor;
        int prevArm    = arm;
        std::string note;
        if (g_cursor >= 0 && g_cursor < static_cast<int>(g_rows.size()) && g_rows[g_cursor].type == 3) {
            int idx = g_rows[g_cursor].index;
            int w   = cfg.armorWeight[idx];
            prevArmCap = std::min(cfg.maxArmorCapacity, usedArmor + w);
            prevArm    = std::max(arm, static_cast<int>(std::round(inv::kArmorDefs[idx].armorValue)));
            char nb[80];
            std::snprintf(nb, sizeof(nb), "%s: %d, %s: %d",
                          ui::Loc("Объём", "Size").c_str(), w,
                          ui::Loc("Свободно", "Free").c_str(), freeArmor);
            note = nb;
        }
        card.SetTitle(ui::Loc("ЗАПАС БРОНИ", "ARMOR STOCK"))
            .SetAnchor(true, 0.0f, 16.0f, 18.0f)
            .SetNote(note)
            .AddBar(ui::Loc("Запас", "Space"), usedArmor, prevArmCap, cfg.maxArmorCapacity, "")
            .AddBar(ui::Loc("Броня", "Armor"), arm,       prevArm,    100,                  "%");
    } else {
        int prevHp  = hp;
        int prevCap = usedFood;
        int prevFat = fat;
        std::string note;
        if (g_cursor >= 0 && g_cursor < static_cast<int>(g_rows.size())) {
            const auto& r = g_rows[g_cursor];
            if (r.type == 0) {
                const auto& d = inv::kFoodDefs[r.index];
                const float gainPct = d.fatGain * cfg.fatGainMultiplier * 0.1f;
                int w = cfg.foodWeight[r.index];
                prevHp  = std::min(maxHp, hp + d.healHp);
                prevCap = std::min(cfg.maxFoodCapacity, usedFood + w);
                prevFat = std::min(100, static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f + gainPct)));
                char nb[96];
                if (gainPct >= 0.05f) {
                    std::snprintf(nb, sizeof(nb), "+%d %s, +%.1f%% %s, %s: %d",
                                  d.healHp, ui::Loc("ЗД", "HP").c_str(),
                                  gainPct, ui::Loc("вес", "fat").c_str(),
                                  ui::Loc("Объём", "Size").c_str(), w);
                } else {
                    std::snprintf(nb, sizeof(nb), "+%d %s, %s, %s: %d",
                                  d.healHp, ui::Loc("ЗД", "HP").c_str(),
                                  ui::Loc("0% веса", "0% fat").c_str(),
                                  ui::Loc("Объём", "Size").c_str(), w);
                }
                note = nb;
            } else if (r.type == 1) {
                prevCap = cfg.maxFoodCapacity;
                note = ui::Loc("Пополнение кармана до предела", "Fills pocket to capacity");
            } else {
                auto* bus = modbus::GetBus();
                bool hasPhone = (bus && bus->phoneInstalled);
                std::string keyLbl = common::GetVirtualKeyLabel(hasPhone ? modbus::GetPhoneKey() : cfg.openKey);
                char nb[80];
                if (hasPhone) {
                    std::snprintf(nb, sizeof(nb), "%s - %s", ui::Loc("Телефон", "Phone").c_str(), keyLbl.c_str());
                } else {
                    std::snprintf(nb, sizeof(nb), "%s - %s", ui::Loc("Инвентарь", "Inventory").c_str(), keyLbl.c_str());
                }
                note = nb;
            }
        }

        card.SetTitle(ui::Loc("ИНВЕНТАРЬ", "POCKET STOCK"))
            .SetAnchor(true, 0.0f, 16.0f, 18.0f)
            .SetNote(note)
            .AddBar(ui::Loc("Карман",   "Pocket"), usedFood, prevCap, cfg.maxFoodCapacity, "")
            .AddBar(ui::Loc("Здоровье", "Health"), hp,       prevHp,  maxHp,               "")
            .AddBar(ui::Loc("Вес",      "Fat"),    fat,      prevFat, 100,                 "%");
    }
    card.Render();
}

bool IsShopActive() { return g_shopActive || g_forwardExit; }

void Reset() {
    g_shopActive  = false;
    g_forwardExit = false;
    g_cursor      = 0;
}

} // namespace foodshop
