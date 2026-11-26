#include "clothes_shop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "log.hpp"
#include "memory.hpp"
#include "phone_config.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "wardrobe.hpp"

namespace clothesshop {

namespace {

enum class Screen : uint8_t {
    Categories = 0,
    Items
};

using FnMenuProcess = void(__cdecl*)(int8_t);
FnMenuProcess g_origMenuProcess = nullptr;

bool     g_shopActive         = false;
bool     g_forwardExit        = false;
uint32_t g_exitCooldownUntil  = 0;
int      g_exitPulseCounter   = 0;
Screen   g_screen             = Screen::Categories;
int      g_catCursor          = 0;
int      g_itemCursor         = 0;

const wardrobe::ShopCatalog* g_activeShop = nullptr;
std::vector<int>             g_availSlots;  // indices 0..6 that have items in this shop
std::vector<int>             g_slotItems;   // catalog indices in selected slot

wardrobe::ClothSnapshot      g_savedSnap{};
bool                         g_camActive   = false;
float                        g_camAngle    = 0.0f;

sa::tMenuPanel* GetClothesPanel() {
    if (!pcfg::Get().enableClothesShopUI) return nullptr;
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;
    if (std::strncmp(p->m_acTitle, "CLOT", 4) != 0) return nullptr;
    // Only intercept actual clothing stores (Binco, ProLaps, Sub Urban, ZIP, Victim, Didier Sachs)
    if (!wardrobe::FindShopCatalog(sa::shopping::GetLoadedShopName())) return nullptr;
    return p;
}

void RebuildAvailableSlots() {
    g_availSlots.clear();
    if (!g_activeShop) return;
    const auto& cat = wardrobe::Catalog();
    for (int s = 0; s < 7; ++s) { // 0..6 = Torso, Legs, Shoes, Chains, Watches, Glasses, Hats
        uint32_t bp = wardrobe::kSlotBodyParts[s];
        bool hasAny = false;
        for (int idx : g_activeShop->itemIndices) {
            if (idx >= 0 && idx < static_cast<int>(cat.size()) && cat[idx].bodyPart == bp) {
                hasAny = true;
                break;
            }
        }
        if (hasAny) g_availSlots.push_back(s);
    }
    if (g_catCursor >= static_cast<int>(g_availSlots.size())) g_catCursor = 0;
}

void RebuildSlotItems(int slotIdx) {
    g_slotItems.clear();
    if (!g_activeShop) return;
    const auto& cat = wardrobe::Catalog();
    uint32_t bp = wardrobe::kSlotBodyParts[slotIdx];
    for (int idx : g_activeShop->itemIndices) {
        if (idx >= 0 && idx < static_cast<int>(cat.size()) && cat[idx].bodyPart == bp) {
            g_slotItems.push_back(idx);
        }
    }
    g_itemCursor = 0;
}

void BeginPreviewCam(const sa::CPed* ped) {
    if (!sa::IsPedValid(ped)) return;
    sa::CVector fwd = ped->GetForward();
    g_camAngle  = std::atan2(fwd.y, fwd.x);
    g_camActive = true;
}

void EndPreviewCam() {
    if (g_camActive) {
        g_camActive = false;
        sa::ScriptCmd(0x02EB).Run(); // RESTORE_CAMERA_JUMPCUT
    }
}

void PreviewHighlightedItem() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || g_itemCursor < 0 || g_itemCursor >= static_cast<int>(g_slotItems.size())) return;
    wardrobe::ApplyClothItem(ped, g_slotItems[g_itemCursor], true);
}

void CloseShopCleanly() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (g_screen == Screen::Items && g_savedSnap.valid) {
        wardrobe::RestoreSnapshot(ped, g_savedSnap);
    }
    EndPreviewCam();
    g_shopActive       = false;
    g_forwardExit      = true;
    g_exitPulseCounter = 0;
    sa::PlayFrontendSound(sa::SFX_BACK);
}

void HandleSelect() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    if (g_screen == Screen::Categories) {
        if (g_catCursor < 0 || g_catCursor >= static_cast<int>(g_availSlots.size())) return;
        g_savedSnap = wardrobe::CaptureSnapshot(ped);
        RebuildSlotItems(g_availSlots[g_catCursor]);
        g_screen = Screen::Items;
        BeginPreviewCam(ped);
        PreviewHighlightedItem();
        sa::PlayFrontendSound(sa::SFX_SELECT);
        return;
    }

    if (g_screen == Screen::Items) {
        if (g_itemCursor < 0 || g_itemCursor >= static_cast<int>(g_slotItems.size())) return;
        int idx = g_slotItems[g_itemCursor];
        const auto& item = wardrobe::Catalog()[idx];
        bool owned = wardrobe::IsClothOwned(idx);

        if (!owned && item.price > 0) {
            if (sa::GetPlayerMoney() < item.price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
                return;
            }
            sa::AddPlayerMoney(-item.price);
            wardrobe::MarkClothOwned(idx);
            wardrobe::SaveNow();
            sa::PlayFrontendSound(sa::SFX_BUY);
            ui::ShowHelp("~g~Одежда куплена и добавлена в гардероб телефона!",
                         "~g~Clothing purchased & added to your Phone Wardrobe!");
        } else {
            sa::PlayFrontendSound(sa::SFX_SELECT);
            ui::ShowHelp("~g~Одежда надета!", "~g~Clothing equipped!");
        }

        // Commit current preview as new saved snapshot!
        wardrobe::ApplyClothItem(ped, idx, true);
        g_savedSnap = wardrobe::CaptureSnapshot(ped);
    }
}

void HandleBack() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (g_screen == Screen::Items) {
        if (g_savedSnap.valid) {
            wardrobe::RestoreSnapshot(ped, g_savedSnap);
        }
        EndPreviewCam();
        g_screen = Screen::Categories;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    CloseShopCleanly();
}

void ProcessInput() {
    const auto& k = ui::Keys();
    int* cur = (g_screen == Screen::Categories) ? &g_catCursor : &g_itemCursor;
    int  cnt = (g_screen == Screen::Categories) ? static_cast<int>(g_availSlots.size()) : static_cast<int>(g_slotItems.size());

    if (cnt > 0) {
        if (k.Repeated(VK_UP) || k.Repeated('W')) {
            *cur = (*cur - 1 + cnt) % cnt;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            if (g_screen == Screen::Items) PreviewHighlightedItem();
        } else if (k.Repeated(VK_DOWN) || k.Repeated('S')) {
            *cur = (*cur + 1) % cnt;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            if (g_screen == Screen::Items) PreviewHighlightedItem();
        }
    }

    if (k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE)) {
        HandleSelect();
    } else if (k.JustPressed(VK_BACK) || k.JustPressed(VK_ESCAPE) || k.JustPressed('F')) {
        HandleBack();
    }
}

void __cdecl HookedMenuProcess(int8_t menuId) {
    if (g_shopActive || g_forwardExit) return;
    if (GetClothesPanel() != nullptr) return;
    if (g_origMenuProcess) g_origMenuProcess(menuId);
}

} // namespace

void InstallHooks() {
    g_origMenuProcess = reinterpret_cast<FnMenuProcess>(
        mem::HookCall(0x0058FCDA, reinterpret_cast<uintptr_t>(&HookedMenuProcess)));
    modlog::Info("[ClothesShop] Hooked CMenuSystem::Process at 0x0058FCDA.");
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    sa::tMenuPanel* panel = GetClothesPanel();

    if (g_forwardExit) {
        if (!panel) {
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

    if (panel && !g_shopActive) {
        if (sa::GetTimeMs() < g_exitCooldownUntil) {
            g_forwardExit      = true;
            g_exitPulseCounter = 0;
            return;
        }
        g_activeShop = wardrobe::FindShopCatalog(sa::shopping::GetLoadedShopName());
        if (!g_activeShop) return;

        g_shopActive = true;
        g_screen     = Screen::Categories;
        g_catCursor  = 0;
        g_itemCursor = 0;
        RebuildAvailableSlots();
        wardrobe::SyncWornClothesToOwned(sa::FindPlayerPed(0));
        sa::PlayFrontendSound(sa::SFX_SELECT);
        modlog::Info("[ClothesShop] Opened UI Kit Clothes Shop '%s'.", g_activeShop->shopKey.c_str());
    } else if (!panel && g_shopActive) {
        EndPreviewCam();
        g_shopActive = false;
        return;
    }

    if (g_shopActive) {
        ProcessInput();
        sa::ClearPadMenuInputs();
    }
}

void PostScriptsTick() {
    if (!g_shopActive || !g_camActive || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
    const auto& k = ui::Keys();
    if (k.IsDown('Q') || k.IsDown(VK_LEFT))       g_camAngle -= dt * 1.9f;
    else if (k.IsDown('E') || k.IsDown(VK_RIGHT)) g_camAngle += dt * 1.9f;

    sa::CVector p = ped->GetPosition();
    float dist = 2.05f;
    float camX = p.x + std::cos(g_camAngle) * dist;
    float camY = p.y + std::sin(g_camAngle) * dist;
    float camZ = p.z + 0.35f;

    sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
    sa::ScriptCmd(0x0160).Float(p.x).Float(p.y).Float(p.z + 0.10f).Int(2).Run();
}

void Render2D() {
    if (!g_shopActive || !g_activeShop || sa::IsPauseMenuActive()) return;

    const auto& cat   = wardrobe::Catalog();
    const int32_t money = sa::GetPlayerMoney();

    ui::ListMenu menu;
    menu.SetPosition(24.0f, 72.0f, 206.0f).SetMaxVisible(11);

    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc(g_activeShop->nameRus.c_str(), g_activeShop->nameEng.c_str()))
            .SetColumns(ui::Loc("КАТЕГОРИЯ ОДЕЖДЫ", "CLOTHING CATEGORY"), ui::Loc("КУПЛЕНО", "OWNED"))
            .SetFooter(ui::Loc("Enter - Выбрать | F - Выход", "Enter - Select | F - Exit"));

        for (int slotIdx : g_availSlots) {
            uint32_t bp = wardrobe::kSlotBodyParts[slotIdx];
            int tot = 0, own = 0;
            for (int idx : g_activeShop->itemIndices) {
                if (cat[idx].bodyPart == bp) {
                    ++tot;
                    if (wardrobe::IsClothOwned(idx)) ++own;
                }
            }
            char cb[24];
            std::snprintf(cb, sizeof(cb), "%d/%d", own, tot);
            menu.AddItem(ui::Loc(wardrobe::SlotNameRus(slotIdx), wardrobe::SlotNameEng(slotIdx)),
                         cb, (own > 0) ? sa::CRGBA{120, 225, 130, 255} : sa::CRGBA{190, 190, 190, 255}, true);
        }
        menu.SetSelected(g_catCursor);
    } else {
        int slotIdx = g_availSlots[std::clamp(g_catCursor, 0, static_cast<int>(g_availSlots.size()) - 1)];
        menu.SetTitle(ui::Loc(wardrobe::SlotNameRus(slotIdx), wardrobe::SlotNameEng(slotIdx)))
            .SetColumns(ui::Loc("ОДЕЖДА", "ITEM"), ui::Loc("ЦЕНА / СТАТУС", "PRICE / STATUS"))
            .SetFooter(ui::Loc("Enter - Купить/Надеть | Q/E - Осмотр | F - Назад",
                               "Enter - Buy/Wear | Q/E - Orbit | F - Back"));

        for (int idx : g_slotItems) {
            const auto& item = cat[idx];
            bool owned = wardrobe::IsClothOwned(idx);
            std::string right;
            sa::CRGBA col;
            if (owned) {
                right = ui::Loc("Куплено", "Owned");
                col   = {115, 225, 130, 255};
            } else {
                right = ui::FormatPrice(item.price);
                col   = (money >= item.price) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255};
            }
            menu.AddItem(wardrobe::GetClothDisplayNameGame(item), right, col, true);
        }
        menu.SetSelected(g_itemCursor);
    }
    menu.Render();

    // Right StatsCard when previewing items (shows Respect & Sex Appeal bonus!)
    if (g_screen == Screen::Items && g_itemCursor >= 0 && g_itemCursor < static_cast<int>(g_slotItems.size())) {
        const auto& item = cat[g_slotItems[g_itemCursor]];
        ui::StatsCard card;
        card.SetTitle(ui::Loc("СТИЛЬ", "STYLE"), wardrobe::GetClothDisplayNameGame(item))
            .SetAnchor(true, 148.0f, 16.0f, 18.0f)
            .SetNote(wardrobe::IsClothOwned(g_slotItems[g_itemCursor])
                ? ui::Loc("Уже в гардеробе (надеть беспл.)", "In Wardrobe (equip free)")
                : ui::Loc("После покупки доступно в Телефоне", "Available in Phone after purchase"))
            .AddBar(ui::Loc("Авторитет", "Respect"),    item.respect, item.respect, 30, "")
            .AddBar(ui::Loc("Привлекат.", "Sex Appeal"), item.sexy,    item.sexy,    30, "");
        card.Render();
    }
}

bool IsShopActive() { return g_shopActive || g_forwardExit; }

void Reset() {
    EndPreviewCam();
    g_shopActive  = false;
    g_forwardExit = false;
    g_activeShop  = nullptr;
}

} // namespace clothesshop
