#include "food_shop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "inv_data.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
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
std::string g_shopTitleRus       = "РЕСТОРАН (НА ВЫНОС)";
std::string g_shopTitleEng       = "RESTAURANT (TAKE-OUT)";

struct ShopRow {
    int type;  // 0=Food, 1=FillPocketFood, 2=OpenArmorSubmenu, 3=Armor
    int index; // food or armor index
};
std::vector<ShopRow> g_rows;

sa::tMenuPanel* GetFoodPanel() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (std::strncmp(p->m_acTitle, "FOOD", 4) != 0) return nullptr;

    // Story Mission Safety: Mission 2 ("Ryder") requires buying a vanilla pizza at Well Stacked Pizza (FDPIZA)
    // to trigger Ryder's scripted robbery cutscene -> yield to vanilla during missions inside FDPIZA!
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
        g_shopTitleRus = "BURGER SHOT (С СОБОЙ)";
        g_shopTitleEng = "BURGER SHOT (TAKE-OUT)";
    } else if (sname && _strnicmp(sname, "FDPIZA", 6) == 0) {
        g_currentShopMask |= inv::SHOP_PIZZA;
        g_shopTitleRus = "PIZZA STACK (С СОБОЙ)";
        g_shopTitleEng = "PIZZA STACK (TAKE-OUT)";
    } else if (sname && _strnicmp(sname, "FDCHICK", 7) == 0) {
        g_currentShopMask |= inv::SHOP_CLUCK;
        g_shopTitleRus = "CLUCKIN' BELL (С СОБОЙ)";
        g_shopTitleEng = "CLUCKIN' BELL (TAKE-OUT)";
    } else {
        g_currentShopMask = inv::SHOP_ALL | inv::SHOP_BURGER | inv::SHOP_PIZZA | inv::SHOP_CLUCK;
        g_shopTitleRus = "ЗАКУСОЧНАЯ (С СОБОЙ)";
        g_shopTitleEng = "DINER (TAKE-OUT)";
    }
}

void RebuildRows() {
    g_rows.clear();
    if (g_screen == ShopScreen::FoodItems) {
        g_rows.push_back({1, -1}); // Smart fill remaining pocket space
        // Signature restaurant hot meals first, then universal snacks
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

// Plans how to fill remaining food capacity with the best available food in this shop (hot meal -> salad -> bar -> snack)
struct FillPlan {
    int addCount[inv::FOOD_COUNT] = {};
    int totalCost = 0;
    int totalItems = 0;
};

FillPlan ComputeSmartFillFoodPlan(int maxBudget = -1) {
    FillPlan plan{};
    const auto& cfg = inv::Cfg();
    int freeCap = inv::Store().GetFreeFoodCapacity();
    if (freeCap <= 0) return plan;

    // Collect food indices available in this shop ordered by highest healHp first
    std::vector<int> avail;
    for (const auto& r : g_rows) {
        if (r.type == 0) avail.push_back(r.index);
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

void HandleSelect() {
    if (g_cursor < 0 || g_cursor >= static_cast<int>(g_rows.size())) return;
    const auto& row = g_rows[g_cursor];
    auto& st        = inv::Store();
    const auto& cfg = inv::Cfg();
    int32_t money   = sa::GetPlayerMoney();

    if (row.type == 0) {
        int idx = row.index;
        int w   = cfg.foodWeight[idx];
        int freeCap = st.GetFreeFoodCapacity();
        if (w > freeCap) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            char rb[128], eb[128];
            std::snprintf(rb, sizeof(rb), "~r~Не хватает места в кармане (нужно %d, свободно %d)!", w, freeCap);
            std::snprintf(eb, sizeof(eb), "~r~Not enough pocket space (needs %d, free %d)!", w, freeCap);
            ui::ShowHelp(rb, eb);
            return;
        }
        int price = cfg.foodPrice[idx];
        if (money < price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
            return;
        }
        sa::AddPlayerMoney(-price);
        st.food[idx]++;
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
    } else if (row.type == 1) {
        if (st.GetFreeFoodCapacity() <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Карман для еды уже заполнен!", "Food pocket is already full!");
            return;
        }
        FillPlan plan = ComputeSmartFillFoodPlan(money);
        if (plan.totalItems <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
            return;
        }
        sa::AddPlayerMoney(-plan.totalCost);
        for (int i = 0; i < inv::FOOD_COUNT; ++i) st.food[i] += plan.addCount[i];
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Запас еды в кармане пополнен!", "~g~Pocket food stocked up!");
    } else if (row.type == 2) {
        g_screen = ShopScreen::ArmorItems;
        g_cursor = 0;
        RebuildRows();
        sa::PlayFrontendSound(sa::SFX_SELECT);
    } else if (row.type == 3) {
        int idx = row.index;
        int w   = cfg.armorWeight[idx];
        int freeCap = st.GetFreeArmorCapacity();
        if (w > freeCap) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            char rb[128], eb[128];
            std::snprintf(rb, sizeof(rb), "~r~Не хватает места в запасе брони (нужно %d, свободно %d)!", w, freeCap);
            std::snprintf(eb, sizeof(eb), "~r~Not enough armor space (needs %d, free %d)!", w, freeCap);
            ui::ShowHelp(rb, eb);
            return;
        }
        int price = cfg.armorPrice[idx];
        if (money < price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
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
        if (in.up) {
            g_cursor = (g_cursor - 1 + n) % n;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (in.down) {
            g_cursor = (g_cursor + 1) % n;
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
        ui::SyncIdleMouseEdges();
        sa::PlayFrontendSound(sa::SFX_SELECT);
        modlog::Info("[FoodShop] Opened GTA 5 Take-Out Menu (shop='%s').", sa::shopping::GetLoadedShopName());
    } else if (!foodPanel && g_shopActive) {
        g_shopActive = false;
        return;
    }

    if (g_shopActive) {
        ProcessInput();
        sa::ClearPadMenuInputs();
    }
}

void Render2D() {
    if (!g_shopActive || sa::IsPauseMenuActive()) return;

    const auto& st      = inv::Store();
    const auto& cfg     = inv::Cfg();
    const int32_t money = sa::GetPlayerMoney();
    const int usedFood  = st.GetUsedFoodCapacity();
    const int freeFood  = st.GetFreeFoodCapacity();

    char colR[36];
    if (g_screen == ShopScreen::FoodItems) {
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("КАРМАН", "POCKET").c_str(), usedFood, cfg.maxFoodCapacity);
    } else {
        std::snprintf(colR, sizeof(colR), "%s %d/%d", ui::Loc("ЗАПАС", "SPACE").c_str(), st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
    }

    ui::ListMenu menu;
    menu.SetTitle(g_screen == ShopScreen::FoodItems
            ? ui::Loc(g_shopTitleRus.c_str(), g_shopTitleEng.c_str())
            : ui::Loc("ЗАПАСНЫЕ БРОНИКИ", "SPARE BODY ARMOR"))
        .SetColumns(ui::Loc("ТОВАР (С СОБОЙ)", "ITEM (TAKE-OUT)"), colR)
        .SetFooter(ui::StandardFooter("Купить", "Buy", "Выход", "Exit"))
        .SetPosition(24.0f, 72.0f, 208.0f)
        .SetMaxVisible(10);

    for (const auto& r : g_rows) {
        if (r.type == 1) {
            FillPlan fullPlan = ComputeSmartFillFoodPlan(-1);
            std::string right = (fullPlan.totalItems > 0) ? ui::FormatPrice(fullPlan.totalCost) : ui::Loc("Полно", "Full");
            sa::CRGBA col = (fullPlan.totalItems <= 0)
                ? sa::CRGBA{115, 225, 130, 255}
                : ((money >= fullPlan.totalCost) ? sa::CRGBA{135, 235, 145, 255} : sa::CRGBA{235, 185, 95, 255});
            menu.AddItem(ui::Loc(">> Заполнить карман", ">> Fill Food Pocket"), right, col, true);
        } else if (r.type == 2) {
            char ab[32];
            std::snprintf(ab, sizeof(ab), "%d/%d", st.GetUsedArmorCapacity(), cfg.maxArmorCapacity);
            menu.AddItem(ui::Loc("Бронежилеты (в запас) ->", "Spare Body Armor ->"), ab, {140, 200, 255, 255}, true);
        } else if (r.type == 0) {
            const auto& d = inv::kFoodDefs[r.index];
            int cur  = st.food[r.index];
            int w    = cfg.foodWeight[r.index];
            int pr   = cfg.foodPrice[r.index];
            bool fit = (w <= freeFood);
            char rb[48];
            if (!fit) {
                if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d (%s)", cur, ui::Loc("Полно", "Full").c_str());
                else         std::snprintf(rb, sizeof(rb), "%s", ui::Loc("Нет места", "Full").c_str());
            } else {
                if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d  $%d", cur, pr);
                else         std::snprintf(rb, sizeof(rb), "$%d", pr);
            }
            sa::CRGBA col = !fit
                ? ((cur > 0) ? sa::CRGBA{115, 225, 130, 255} : sa::CRGBA{135, 135, 135, 255})
                : ((money >= pr) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
        } else if (r.type == 3) {
            const auto& d = inv::kArmorDefs[r.index];
            int cur  = st.armor[r.index];
            int w    = cfg.armorWeight[r.index];
            int pr   = cfg.armorPrice[r.index];
            bool fit = (w <= st.GetFreeArmorCapacity());
            char rb[48];
            if (!fit) {
                if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d (%s)", cur, ui::Loc("Полно", "Full").c_str());
                else         std::snprintf(rb, sizeof(rb), "%s", ui::Loc("Нет места", "Full").c_str());
            } else {
                if (cur > 0) std::snprintf(rb, sizeof(rb), "x%d  $%d", cur, pr);
                else         std::snprintf(rb, sizeof(rb), "$%d", pr);
            }
            sa::CRGBA col = !fit
                ? ((cur > 0) ? sa::CRGBA{115, 225, 130, 255} : sa::CRGBA{135, 135, 135, 255})
                : ((money >= pr) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
        }
    }
    menu.SetSelected(g_cursor);
    menu.Render();

    // Right StatsCard showing CJ's Vitals, Pocket Space & highlighted item effect!
    sa::CPed* ped = sa::FindPlayerPed(0);
    int hp    = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetHealth())) : 100;
    int maxHp = sa::IsPedValid(ped) ? std::max(100, static_cast<int>(std::round(ped->GetMaxHealth()))) : 100;
    int fat   = static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f));

    int prevHp   = hp;
    int prevCap  = usedFood;
    std::string note;
    if (g_cursor >= 0 && g_cursor < static_cast<int>(g_rows.size())) {
        const auto& r = g_rows[g_cursor];
        if (r.type == 0) {
            const auto& d = inv::kFoodDefs[r.index];
            int w = cfg.foodWeight[r.index];
            prevHp  = std::min(maxHp, hp + d.healHp);
            prevCap = std::min(cfg.maxFoodCapacity, usedFood + w);
            char nb[80];
            std::snprintf(nb, sizeof(nb), "+%d HP | %s: %d | %s: %d",
                          d.healHp, ui::Loc("Объём", "Size").c_str(), w,
                          ui::Loc("Свободно", "Free").c_str(), freeFood);
            note = nb;
        } else if (r.type == 1) {
            prevCap = cfg.maxFoodCapacity;
            note = ui::Loc("Лучшие блюда + закуски до лимита", "Best meals + snacks up to limit");
        } else {
            auto* bus = modbus::GetBus();
            bool hasPhone = (bus && bus->phoneInstalled);
            note = hasPhone
                ? ui::Loc("Еда хранится в Телефоне (F6)", "Items stored in Phone (F6)")
                : ui::Loc("Еда хранится в Инвентаре (F7)", "Items stored in Inventory (F7)");
        }
    }

    ui::StatsCard card;
    card.SetTitle(ui::Loc("ИНВЕНТАРЬ", "POCKET STOCK"), ui::Loc("С СОБОЙ", "TAKE-OUT"))
        .SetAnchor(true, 156.0f, 16.0f, 18.0f)
        .SetNote(note)
        .AddBar(ui::Loc("Карман",   "Pocket"), usedFood, prevCap, cfg.maxFoodCapacity, "")
        .AddBar(ui::Loc("Здоровье", "Health"), hp,       prevHp,  maxHp,               "")
        .AddBar(ui::Loc("Вес",      "Fat"),    fat,      fat,     100,                 "%");
    card.Render();
}

bool IsShopActive() { return g_shopActive || g_forwardExit; }

void Reset() {
    g_shopActive  = false;
    g_forwardExit = false;
    g_cursor      = 0;
}

} // namespace foodshop
