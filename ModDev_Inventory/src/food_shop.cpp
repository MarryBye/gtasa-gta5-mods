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

bool       g_shopActive         = false;
bool       g_forwardExit        = false;
uint32_t   g_exitCooldownUntil  = 0;
int        g_exitPulseCounter   = 0;
ShopScreen g_screen             = ShopScreen::FoodItems;
int        g_cursor             = 0;
uint8_t    g_currentShopMask    = inv::SHOP_ALL;
std::string g_shopTitleRus      = "РЕСТОРАН (НА ВЫНОС)";
std::string g_shopTitleEng      = "RESTAURANT (TAKE-OUT)";

struct ShopRow {
    int type;  // 0=Food, 1=RefillAllFood, 2=OpenArmorSubmenu, 3=Armor, 4=RefillAllArmor
    int index; // food or armor index
};
std::vector<ShopRow> g_rows;

sa::tMenuPanel* GetFoodPanel() {
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (std::strncmp(p->m_acTitle, "FOOD", 4) != 0) return nullptr;
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
        // Diner / Donut / Other -> sell all signature items + snacks!
        g_currentShopMask = inv::SHOP_ALL | inv::SHOP_BURGER | inv::SHOP_PIZZA | inv::SHOP_CLUCK;
        g_shopTitleRus = "ЗАКУСОЧНАЯ (С СОБОЙ)";
        g_shopTitleEng = "DINER (TAKE-OUT)";
    }
}

void RebuildRows() {
    g_rows.clear();
    if (g_screen == ShopScreen::FoodItems) {
        // 1-click fill all available snacks/food to pocket limit!
        g_rows.push_back({1, -1});
        // Show signature restaurant item first, then universal snacks
        for (int i = 0; i < inv::FOOD_COUNT; ++i) {
            if ((inv::kFoodDefs[i].shopMask & g_currentShopMask) != 0 && inv::kFoodDefs[i].shopMask != inv::SHOP_ALL) {
                g_rows.push_back({0, i});
            }
        }
        for (int i = 0; i < inv::FOOD_COUNT; ++i) {
            if (inv::kFoodDefs[i].shopMask == inv::SHOP_ALL) {
                g_rows.push_back({0, i});
            }
        }
        if (inv::Cfg().allowArmorInFoodShops) {
            g_rows.push_back({2, -1});
        }
    } else {
        g_rows.push_back({4, -1});
        for (int i = 0; i < inv::ARMOR_COUNT; ++i) {
            g_rows.push_back({3, i});
        }
    }
    if (g_cursor >= static_cast<int>(g_rows.size())) g_cursor = 0;
}

int ComputeRefillAllFoodCost() {
    int total = 0;
    const auto& cfg = inv::Cfg();
    const auto& st  = inv::Store();
    for (const auto& r : g_rows) {
        if (r.type == 0) {
            int need = std::max(0, cfg.foodMax[r.index] - st.food[r.index]);
            total += need * cfg.foodPrice[r.index];
        }
    }
    return total;
}

int ComputeRefillAllArmorCost() {
    int total = 0;
    const auto& cfg = inv::Cfg();
    const auto& st  = inv::Store();
    for (int i = 0; i < inv::ARMOR_COUNT; ++i) {
        int need = std::max(0, cfg.armorMax[i] - st.armor[i]);
        total += need * cfg.armorPrice[i];
    }
    return total;
}

void CloseShopCleanly() {
    g_shopActive       = false;
    g_forwardExit      = true;
    g_exitPulseCounter = 0;
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
        if (st.food[idx] >= cfg.foodMax[idx]) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Инвентарь полон для этого продукта!", "You carry the maximum amount of this item!");
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
        int cost = ComputeRefillAllFoodCost();
        if (cost <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Вся еда уже заполнена до максимума!", "All snacks are already full!");
            return;
        }
        if (money < cost) {
            // Buy as many items as the player can afford, prioritizing snacks cleanly!
            int spent = 0;
            for (const auto& r : g_rows) {
                if (r.type != 0) continue;
                while (st.food[r.index] < cfg.foodMax[r.index] && sa::GetPlayerMoney() >= cfg.foodPrice[r.index]) {
                    sa::AddPlayerMoney(-cfg.foodPrice[r.index]);
                    st.food[r.index]++;
                    spent += cfg.foodPrice[r.index];
                }
            }
            if (spent > 0) {
                st.SaveNow();
                sa::PlayFrontendSound(sa::SFX_BUY);
                ui::ShowHelp("~g~Куплено еды на все доступные средства!", "~g~Bought snacks with available cash!");
            } else {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
            }
            return;
        }
        sa::AddPlayerMoney(-cost);
        for (const auto& r : g_rows) {
            if (r.type == 0) st.food[r.index] = cfg.foodMax[r.index];
        }
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Запас еды в кармане заполнен до максимума!", "~g~Pocket snacks refilled to maximum!");
    } else if (row.type == 2) {
        g_screen = ShopScreen::ArmorItems;
        g_cursor = 0;
        RebuildRows();
        sa::PlayFrontendSound(sa::SFX_SELECT);
    } else if (row.type == 3) {
        int idx = row.index;
        if (st.armor[idx] >= cfg.armorMax[idx]) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Достигнут лимит этих бронежилетов в запасе!", "Max spare armor of this type reached!");
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
    } else if (row.type == 4) {
        int cost = ComputeRefillAllArmorCost();
        if (cost <= 0) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("Запас бронежилетов уже полон!", "All spare armor slots are full!");
            return;
        }
        if (money < cost) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег для полной закупки!", "~r~Not enough money to fill all armor!");
            return;
        }
        sa::AddPlayerMoney(-cost);
        for (int i = 0; i < inv::ARMOR_COUNT; ++i) st.armor[i] = cfg.armorMax[i];
        st.SaveNow();
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Запасные бронежилеты пополнены до лимита!", "~g~Spare body armor refilled to limit!");
    }
}

void ProcessInput() {
    const auto& k = ui::Keys();
    int n = static_cast<int>(g_rows.size());
    if (n > 0) {
        if (k.Repeated(VK_UP) || k.Repeated('W')) {
            g_cursor = (g_cursor - 1 + n) % n;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (k.Repeated(VK_DOWN) || k.Repeated('S')) {
            g_cursor = (g_cursor + 1) % n;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE)) {
            HandleSelect();
        }
    }
    if (k.JustPressed(VK_BACK) || k.JustPressed(VK_ESCAPE) || k.JustPressed('F')) {
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
            *reinterpret_cast<int16_t*>(pad + 0x1E) = 255; // ButtonTriangle exit
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

    const auto& st  = inv::Store();
    const auto& cfg = inv::Cfg();
    const int32_t money = sa::GetPlayerMoney();

    ui::ListMenu menu;
    menu.SetTitle(g_screen == ShopScreen::FoodItems
            ? ui::Loc(g_shopTitleRus.c_str(), g_shopTitleEng.c_str())
            : ui::Loc("ЗАПАСНЫЕ БРОНИКИ", "SPARE BODY ARMOR"))
        .SetColumns(ui::Loc("ТОВАР (В КАРМАН)", "ITEM (TO POCKET)"), ui::Loc("ЗАПАС / ЦЕНА", "STOCK / PRICE"))
        .SetFooter(ui::Loc("Enter - Купить в карман | F - Выход", "Enter - Buy to Pocket | F - Exit"))
        .SetPosition(24.0f, 72.0f, 206.0f)
        .SetMaxVisible(10);

    for (const auto& r : g_rows) {
        if (r.type == 1) {
            int cost = ComputeRefillAllFoodCost();
            std::string right = (cost > 0) ? ui::FormatPrice(cost) : ui::Loc("Полно", "Full");
            sa::CRGBA col = (cost <= 0) ? sa::CRGBA{115, 225, 130, 255}
                                        : ((money >= cost) ? sa::CRGBA{135, 235, 145, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(">> Заполнить всю еду", ">> Refill All Snacks"), right, col, true);
        } else if (r.type == 2) {
            char ab[32];
            std::snprintf(ab, sizeof(ab), "%d %s", st.GetTotalArmor(), ui::Loc("шт.", "pcs").c_str());
            menu.AddItem(ui::Loc("Бронежилеты (в запас) ->", "Spare Body Armor ->"), ab, {140, 200, 255, 255}, true);
        } else if (r.type == 4) {
            int cost = ComputeRefillAllArmorCost();
            std::string right = (cost > 0) ? ui::FormatPrice(cost) : ui::Loc("Полно", "Full");
            sa::CRGBA col = (cost <= 0) ? sa::CRGBA{115, 225, 130, 255}
                                        : ((money >= cost) ? sa::CRGBA{135, 235, 145, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(">> Заполнить все броники", ">> Refill All Armor"), right, col, true);
        } else if (r.type == 0) {
            const auto& d = inv::kFoodDefs[r.index];
            int cur = st.food[r.index], mx = cfg.foodMax[r.index], pr = cfg.foodPrice[r.index];
            char rb[48];
            if (cur >= mx) std::snprintf(rb, sizeof(rb), "%d/%d", cur, mx);
            else           std::snprintf(rb, sizeof(rb), "%d/%d  $%d", cur, mx, pr);
            sa::CRGBA col = (cur >= mx) ? sa::CRGBA{115, 225, 130, 255}
                                        : ((money >= pr) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
        } else if (r.type == 3) {
            const auto& d = inv::kArmorDefs[r.index];
            int cur = st.armor[r.index], mx = cfg.armorMax[r.index], pr = cfg.armorPrice[r.index];
            char rb[48];
            if (cur >= mx) std::snprintf(rb, sizeof(rb), "%d/%d", cur, mx);
            else           std::snprintf(rb, sizeof(rb), "%d/%d  $%d", cur, mx, pr);
            sa::CRGBA col = (cur >= mx) ? sa::CRGBA{115, 225, 130, 255}
                                        : ((money >= pr) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(ui::Loc(d.rus, d.eng), rb, col, true);
        }
    }
    menu.SetSelected(g_cursor);
    menu.Render();

    // Right StatsCard showing CJ's Vitals & highlighted item effect!
    sa::CPed* ped = sa::FindPlayerPed(0);
    int hp    = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetHealth())) : 100;
    int maxHp = sa::IsPedValid(ped) ? std::max(100, static_cast<int>(std::round(ped->GetMaxHealth()))) : 100;
    int arm   = sa::IsPedValid(ped) ? static_cast<int>(std::round(ped->GetArmor())) : 0;
    int fat   = static_cast<int>(std::round(sa::stats::Get(sa::stats::STAT_FAT) * 0.1f));

    int prevHp  = hp;
    int prevArm = arm;
    std::string note;
    if (g_cursor >= 0 && g_cursor < static_cast<int>(g_rows.size())) {
        const auto& r = g_rows[g_cursor];
        if (r.type == 0) {
            const auto& d = inv::kFoodDefs[r.index];
            prevHp = std::min(maxHp, hp + d.healHp);
            char nb[64];
            std::snprintf(nb, sizeof(nb), "+%d HP | %s: %s", d.healHp,
                          ui::Loc("Жир", "Fat").c_str(),
                          (d.fatGain <= 0.01f) ? ui::Loc("Нет", "None").c_str() : "+");
            note = nb;
        } else if (r.type == 3) {
            const auto& d = inv::kArmorDefs[r.index];
            prevArm = std::max(arm, static_cast<int>(std::round(d.armorValue)));
            note = ui::Loc("Надевается в любой момент", "Equip anytime from Pocket/Phone");
        } else {
            auto* bus = modbus::GetBus();
            bool hasPhone = (bus && bus->phoneInstalled);
            note = hasPhone
                ? ui::Loc("Еда сохраняется в Телефон (F6)", "Items stored in Phone (F6)")
                : ui::Loc("Еда сохраняется в Инвентарь (F7)", "Items stored in Inventory (F7)");
        }
    }

    ui::StatsCard card;
    card.SetTitle(ui::Loc("ИНВЕНТАРЬ", "POCKET STOCK"), ui::Loc("С СОБОЙ", "TAKE-OUT"))
        .SetAnchor(true, 152.0f, 16.0f, 18.0f)
        .SetNote(note)
        .AddBar(ui::Loc("Здоровье", "Health"), hp,  prevHp,  maxHp, "")
        .AddBar(ui::Loc("Броня",    "Armor"),  arm, prevArm, 100,   "%")
        .AddBar(ui::Loc("Вес",      "Fat"),    fat, fat,     100,   "%");
    card.Render();
}

bool IsShopActive() { return g_shopActive || g_forwardExit; }

void Reset() {
    g_shopActive  = false;
    g_forwardExit = false;
    g_cursor      = 0;
}

} // namespace foodshop
