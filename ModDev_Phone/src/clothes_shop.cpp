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

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kPi2 = 6.28318530718530717959f;

enum class ShopMode : uint8_t {
    None = 0,
    Clothes,
    Barber
};

enum class Screen : uint8_t {
    Categories = 0,
    Items
};

using FnMenuProcess = void(__cdecl*)(int8_t);
FnMenuProcess g_origMenuProcess = nullptr;

bool     g_shopActive         = false;
ShopMode g_mode               = ShopMode::None;
bool     g_forwardExit        = false;
uint32_t g_exitCooldownUntil  = 0;
int      g_exitPulseCounter   = 0;
Screen   g_screen             = Screen::Categories;
int      g_catCursor          = 0;
int      g_itemCursor         = 0;

const wardrobe::ShopCatalog* g_activeShop = nullptr;
std::vector<int>             g_availSlots;  // Clothes: indices 0..6 that have items in this shop
std::vector<int>             g_slotItems;   // Clothes or Haircuts catalog indices

wardrobe::ClothSnapshot      g_savedSnap{};
bool                         g_camActive        = false;
float                        g_camAngle         = 0.0f;  // Open-aisle world angle from CJ -> camera
float                        g_camDist          = 2.0f;  // Safe unobstructed distance
float                        g_savedHeadingDeg  = 0.0f;  // CJ heading before entering clothes shop
bool                         g_hasSavedHeading  = false;
float                        g_pedYawDeg        = 0.0f;  // Interactive 360° CJ rotation angle (Clothes)
float                        g_barberBaseAngle  = 0.0f;  // Base front angle of barber chair
float                        g_barberOrbitOff   = 0.0f;  // Interactive camera orbit offset (Barber)

sa::tMenuPanel* GetInterceptedPanel(ShopMode* outMode = nullptr) {
    if (!pcfg::Get().enableClothesShopUI) return nullptr;
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;

    const char* loadedShop = sa::shopping::GetLoadedShopName();
    if (std::strncmp(p->m_acTitle, "CLOT", 4) == 0) {
        // Story Mission Safety: During missions (e.g. "Nines and AKs" at Binco), yield to vanilla cloth.sc
        // (GTA5UIKit still renders it in our modern style with unified controls!) so mission flags always trigger!
        if (!sa::IsOnMission() && wardrobe::FindShopCatalog(loadedShop)) {
            if (outMode) *outMode = ShopMode::Clothes;
            return p;
        }
    } else if (std::strncmp(p->m_acTitle, "HAIR", 4) == 0) {
        // Story Mission Safety: NEVER intercept Barber during missions (e.g., Mission 2 "Ryder" scripted cutscene!)
        if (!sa::IsOnMission() && wardrobe::FindBarberCatalog(loadedShop)) {
            if (outMode) *outMode = ShopMode::Barber;
            return p;
        }
    }
    return nullptr;
}

float ProbeRayClearDist(const sa::CVector& origin, float angleRad, float maxDist) {
    sa::CVector target{
        origin.x + std::cos(angleRad) * maxDist,
        origin.y + std::sin(angleRad) * maxDist,
        origin.z
    };
    sa::CColPoint col{};
    sa::CEntity* hitEnt = nullptr;
    if (sa::ProcessLineOfSight(origin, target, col, hitEnt,
                               true, false, false, true, false, false, false, false)) {
        float dx = col.m_vecPoint.x - origin.x;
        float dy = col.m_vecPoint.y - origin.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    return maxDist;
}

void BeginClothesPreviewCam(sa::CPed* ped) {
    if (!sa::IsPedValid(ped)) return;
    int32_t pedH = sa::GetPedHandle(ped);
    if (pedH > 0) {
        sa::ScriptCmd(0x0172).Int(pedH).Out(g_savedHeadingDeg).Run(); // GET_CHAR_HEADING
        g_hasSavedHeading = true;
        sa::ScriptCmd(0x0337).Int(pedH).Int(1).Run();                 // SET_CHAR_VISIBLE(ped, TRUE)
    }

    sa::CVector p   = ped->GetPosition();
    sa::CVector fwd = ped->GetForward();
    float fwdAngle  = std::atan2(fwd.y, fwd.x);
    sa::CVector eye{p.x, p.y, p.z + 0.35f};

    // Probe 16 horizontal directions to find the widest unobstructed open store aisle (never behind fitting-room walls!)
    constexpr int kRays = 16;
    constexpr float kProbeMax = 2.75f;
    float clearD[kRays];
    for (int i = 0; i < kRays; ++i) {
        float a = kPi2 * static_cast<float>(i) / static_cast<float>(kRays);
        clearD[i] = ProbeRayClearDist(eye, a, kProbeMax);
    }

    int   bestIdx   = 0;
    float bestScore = -1.0f;
    for (int i = 0; i < kRays; ++i) {
        float a       = kPi2 * static_cast<float>(i) / static_cast<float>(kRays);
        float cSelf   = clearD[i];
        float cLeft   = clearD[(i + kRays - 1) % kRays];
        float cRight  = clearD[(i + 1) % kRays];
        float widthMin = std::min({cSelf, cLeft * 0.94f + 0.15f, cRight * 0.94f + 0.15f});
        float fwdAlign = std::cos(a - fwdAngle); // Mild tie-breaker preferring CJ's natural facing direction
        float score    = widthMin + 0.12f * fwdAlign;
        if (score > bestScore) {
            bestScore = score;
            bestIdx   = i;
        }
    }

    g_camAngle = kPi2 * static_cast<float>(bestIdx) / static_cast<float>(kRays);
    g_camDist  = std::clamp(clearD[bestIdx] - 0.32f, 1.35f, 2.20f);

    // Turn CJ directly toward our open-aisle camera!
    float toCamX = std::cos(g_camAngle);
    float toCamY = std::sin(g_camAngle);
    g_pedYawDeg  = std::atan2(-toCamX, toCamY) * (180.0f / kPi);
    if (pedH > 0) {
        sa::ScriptCmd(0x0173).Int(pedH).Float(g_pedYawDeg).Run(); // SET_CHAR_HEADING
    }
    g_camActive = true;
}

void BeginBarberPreviewCam(sa::CPed* ped) {
    if (!sa::IsPedValid(ped)) return;
    sa::CVector fwd   = ped->GetForward();
    g_barberBaseAngle = std::atan2(fwd.y, fwd.x);
    g_barberOrbitOff  = 0.0f;
    g_camActive       = true;
}

void EndPreviewCam() {
    if (g_camActive) {
        g_camActive = false;
        if (g_hasSavedHeading) {
            sa::CPed* ped = sa::FindPlayerPed(0);
            int32_t pedH  = sa::GetPedHandle(ped);
            if (pedH > 0) {
                sa::ScriptCmd(0x0173).Int(pedH).Float(g_savedHeadingDeg).Run();
            }
            g_hasSavedHeading = false;
        }
        sa::ScriptCmd(0x02EB).Run(); // RESTORE_CAMERA_JUMPCUT
    }
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

void PreviewHighlightedItem() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || g_itemCursor < 0 || g_itemCursor >= static_cast<int>(g_slotItems.size())) return;
    if (g_mode == ShopMode::Barber) {
        wardrobe::ApplyHaircutItem(ped, g_slotItems[g_itemCursor]);
    } else {
        wardrobe::ApplyClothItem(ped, g_slotItems[g_itemCursor], true);
    }
}

void CloseShopCleanly() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if ((g_mode == ShopMode::Barber || g_screen == Screen::Items) && g_savedSnap.valid) {
        wardrobe::RestoreSnapshot(ped, g_savedSnap);
    }
    EndPreviewCam();
    g_shopActive       = false;
    g_mode             = ShopMode::None;
    g_forwardExit      = true;
    g_exitPulseCounter = 0;
    ui::TriggerPostCloseShield(160);
    sa::PlayFrontendSound(sa::SFX_BACK);
}

void HandleSelect() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    if (g_mode == ShopMode::Barber) {
        if (g_itemCursor < 0 || g_itemCursor >= static_cast<int>(g_slotItems.size())) return;
        int hairIdx = g_slotItems[g_itemCursor];
        const auto& item = wardrobe::HairCatalog()[hairIdx];

        if (g_savedSnap.valid && g_savedSnap.texKeys[1] == item.texHash) {
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            ui::ShowHelp("Эта причёска уже на вас!", "You already have this haircut!");
            return;
        }
        if (item.price > 0 && sa::GetPlayerMoney() < item.price) {
            sa::PlayFrontendSound(sa::SFX_ERROR);
            ui::ShowHelp("~r~Недостаточно денег!", "~r~Not enough money!");
            return;
        }
        if (item.price > 0) {
            sa::AddPlayerMoney(-item.price);
        }
        // Apply haircut permanently AND STAY SEATED IN THE BARBER CHAIR (never kick CJ out!)
        wardrobe::ApplyHaircutItem(ped, hairIdx);
        g_savedSnap = wardrobe::CaptureSnapshot(ped);
        sa::PlayFrontendSound(sa::SFX_BUY);
        ui::ShowHelp("~g~Причёска сделана!~w~ Выберите ещё или ~y~ПКМ/Back~w~ для выхода.",
                     "~g~Haircut done!~w~ Pick another style or press ~y~RMB/Back~w~ to leave.");
        return;
    }

    if (g_screen == Screen::Categories) {
        if (g_catCursor < 0 || g_catCursor >= static_cast<int>(g_availSlots.size())) return;
        g_savedSnap = wardrobe::CaptureSnapshot(ped);
        RebuildSlotItems(g_availSlots[g_catCursor]);
        g_screen = Screen::Items;
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
    if (g_mode == ShopMode::Clothes && g_screen == Screen::Items) {
        if (g_savedSnap.valid) {
            wardrobe::RestoreSnapshot(ped, g_savedSnap);
        }
        g_screen = Screen::Categories;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    CloseShopCleanly();
}

void ProcessInput() {
    auto in = ui::PollMenuInput(true, true, true);
    int* cur = (g_mode == ShopMode::Clothes && g_screen == Screen::Categories) ? &g_catCursor : &g_itemCursor;
    int  cnt = (g_mode == ShopMode::Clothes && g_screen == Screen::Categories)
        ? static_cast<int>(g_availSlots.size())
        : static_cast<int>(g_slotItems.size());

    if (cnt > 0) {
        if (in.up) {
            *cur = (*cur - 1 + cnt) % cnt;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            if (g_mode == ShopMode::Barber || g_screen == Screen::Items) PreviewHighlightedItem();
        } else if (in.down) {
            *cur = (*cur + 1) % cnt;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
            if (g_mode == ShopMode::Barber || g_screen == Screen::Items) PreviewHighlightedItem();
        }
    }

    if (in.accept) {
        HandleSelect();
    } else if (in.back) {
        HandleBack();
    }
}

void __cdecl HookedMenuProcess(int8_t menuId) {
    if (g_shopActive || g_forwardExit) return;
    if (GetInterceptedPanel() != nullptr) return;
    if (g_origMenuProcess) g_origMenuProcess(menuId);
}

} // namespace

void InstallHooks() {
    g_origMenuProcess = reinterpret_cast<FnMenuProcess>(
        mem::HookCall(0x0058FCDA, reinterpret_cast<uintptr_t>(&HookedMenuProcess)));
    modlog::Info("[ClothesShop] Hooked CMenuSystem::Process at 0x0058FCDA (Clothes Stores + Stay-In-Chair Barber).");
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    ShopMode detectedMode = ShopMode::None;
    sa::tMenuPanel* panel = GetInterceptedPanel(&detectedMode);

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

        const char* shopKey = sa::shopping::GetLoadedShopName();
        sa::CPed* ped       = sa::FindPlayerPed(0);

        if (detectedMode == ShopMode::Barber) {
            g_activeShop = wardrobe::FindBarberCatalog(shopKey);
            if (!g_activeShop || g_activeShop->itemIndices.empty()) return;

            g_shopActive = true;
            g_mode       = ShopMode::Barber;
            g_screen     = Screen::Items;
            g_slotItems  = g_activeShop->itemIndices;
            g_savedSnap  = wardrobe::CaptureSnapshot(ped);
            g_itemCursor = 0;
            for (size_t i = 0; i < g_slotItems.size(); ++i) {
                if (wardrobe::IsHaircutWorn(ped, g_slotItems[i])) {
                    g_itemCursor = static_cast<int>(i);
                    break;
                }
            }
            BeginBarberPreviewCam(ped);
            ui::LockMenuInputBriefly(180);
            ui::SyncIdleMouseEdges();
            sa::PlayFrontendSound(sa::SFX_SELECT);
            modlog::Info("[BarberShop] Opened Stay-In-Chair Barber UI '%s' (%zu styles).",
                         g_activeShop->shopKey.c_str(), g_slotItems.size());
        } else {
            g_activeShop = wardrobe::FindShopCatalog(shopKey);
            if (!g_activeShop) return;

            g_shopActive = true;
            g_mode       = ShopMode::Clothes;
            g_screen     = Screen::Categories;
            g_catCursor  = 0;
            g_itemCursor = 0;
            RebuildAvailableSlots();
            wardrobe::SyncWornClothesToOwned(ped);
            BeginClothesPreviewCam(ped);
            ui::LockMenuInputBriefly(180);
            ui::SyncIdleMouseEdges();
            sa::PlayFrontendSound(sa::SFX_SELECT);
            modlog::Info("[ClothesShop] Opened UI Kit Clothes Shop '%s' (open-aisle cam dist=%.2fm).",
                         g_activeShop->shopKey.c_str(), g_camDist);
        }
    } else if (!panel && g_shopActive) {
        EndPreviewCam();
        g_shopActive = false;
        g_mode       = ShopMode::None;
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
    int32_t pedH  = sa::GetPedHandle(ped);

    if (g_mode == ShopMode::Clothes) {
        // Guarantee CJ stays visible in every fitting room and rotates 360° smoothly on Q/E or Left/Right!
        if (pedH > 0) {
            sa::ScriptCmd(0x0337).Int(pedH).Int(1).Run(); // SET_CHAR_VISIBLE(ped, TRUE)
            if (k.IsDown('Q') || k.IsDown(VK_LEFT))        g_pedYawDeg -= dt * 125.0f;
            else if (k.IsDown('E') || k.IsDown(VK_RIGHT))  g_pedYawDeg += dt * 125.0f;
            sa::ScriptCmd(0x0173).Int(pedH).Float(g_pedYawDeg).Run(); // SET_CHAR_HEADING
        }

        sa::CVector p = ped->GetPosition();
        float dist  = g_camDist;
        float camZ  = p.z + 0.30f;
        float lookZ = p.z + 0.06f;

        if (g_screen == Screen::Items && g_catCursor >= 0 && g_catCursor < static_cast<int>(g_availSlots.size())) {
            int slotIdx = g_availSlots[g_catCursor];
            if (slotIdx == 2) { // Shoes -> frame feet cleanly
                dist  = std::min(g_camDist, 1.65f);
                camZ  = p.z - 0.12f;
                lookZ = p.z - 0.45f;
            } else if (slotIdx == 5 || slotIdx == 6) { // Glasses / Hats -> close-up on head
                dist  = std::min(g_camDist, 1.32f);
                camZ  = p.z + 0.62f;
                lookZ = p.z + 0.56f;
            } else if (slotIdx == 3 || slotIdx == 4) { // Chains / Watches -> upper torso
                dist  = std::min(g_camDist, 1.55f);
                camZ  = p.z + 0.42f;
                lookZ = p.z + 0.28f;
            }
        }

        float cosA = std::cos(g_camAngle);
        float sinA = std::sin(g_camAngle);
        float camX = p.x + cosA * dist;
        float camY = p.y + sinA * dist;

        // Shift look-at slightly toward camera-LEFT so CJ stands prominently in the right 60% of the screen,
        // 100% clear of the left ListMenu! (camRight = (-sinA, cosA), so -camRight = (+sinA, -cosA))
        float sideShift = dist * 0.21f;
        float lookX = p.x + sinA * sideShift;
        float lookY = p.y - cosA * sideShift;

        sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
        sa::ScriptCmd(0x0160).Float(lookX).Float(lookY).Float(lookZ).Int(2).Run();
    } else if (g_mode == ShopMode::Barber) {
        if (k.IsDown('Q') || k.IsDown(VK_LEFT))       g_barberOrbitOff -= dt * 1.65f;
        else if (k.IsDown('E') || k.IsDown(VK_RIGHT)) g_barberOrbitOff += dt * 1.65f;
        g_barberOrbitOff = std::clamp(g_barberOrbitOff, -2.0f, 2.0f);

        sa::CVector p = ped->GetPosition();
        float ang     = g_barberBaseAngle + g_barberOrbitOff;
        sa::CVector headPos{p.x, p.y, p.z + 0.58f};
        float clearD  = ProbeRayClearDist(headPos, ang, 1.38f);
        float dist    = std::clamp(clearD - 0.22f, 0.72f, 1.15f);

        float cosA = std::cos(ang);
        float sinA = std::sin(ang);
        float camX = headPos.x + cosA * dist;
        float camY = headPos.y + sinA * dist;
        float camZ = headPos.z + 0.08f;

        float sideShift = dist * 0.19f;
        float lookX = headPos.x + sinA * sideShift;
        float lookY = headPos.y - cosA * sideShift;

        sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
        sa::ScriptCmd(0x0160).Float(lookX).Float(lookY).Float(headPos.z).Int(2).Run();
    }
}

void Render2D() {
    if (!g_shopActive || !g_activeShop || sa::IsPauseMenuActive()) return;

    const int32_t money = sa::GetPlayerMoney();

    ui::ListMenu menu;
    menu.SetPosition(24.0f, 72.0f, 206.0f).SetMaxVisible(11);

    if (g_mode == ShopMode::Barber) {
        const auto& hairs = wardrobe::HairCatalog();
        menu.SetTitle(ui::Loc(g_activeShop->nameRus.c_str(), g_activeShop->nameEng.c_str()))
            .SetColumns(ui::Loc("ПРИЧЁСКА", "HAIRCUT"), ui::Loc("ЦЕНА", "PRICE"))
            .SetFooter(ui::StandardFooter("Подстричься", "Buy Cut", "Встать", "Leave", "Q/E - Осм.", "Q/E - Cam"));

        for (int hairIdx : g_slotItems) {
            const auto& item = hairs[hairIdx];
            bool worn = g_savedSnap.valid && (g_savedSnap.texKeys[1] == item.texHash);
            std::string right = worn ? ui::Loc("Сейчас", "Current") : ui::FormatPrice(item.price);
            sa::CRGBA col     = worn
                ? sa::CRGBA{115, 225, 130, 255}
                : ((money >= item.price) ? sa::CRGBA{220, 220, 220, 255} : sa::CRGBA{235, 95, 95, 255});
            menu.AddItem(wardrobe::GetClothDisplayNameGame(item), right, col, true);
        }
        menu.SetSelected(g_itemCursor);
        menu.Render();

        if (g_itemCursor >= 0 && g_itemCursor < static_cast<int>(g_slotItems.size())) {
            const auto& item = hairs[g_slotItems[g_itemCursor]];
            ui::StatsCard card;
            card.SetTitle(ui::Loc("СТИЛЬ", "STYLE"), wardrobe::GetClothDisplayNameGame(item))
                .SetAnchor(true, 148.0f, 16.0f, 18.0f)
                .SetNote(ui::Loc("CJ остаётся в кресле после стрижки", "CJ stays in the chair after haircut"))
                .AddBar(ui::Loc("Авторитет", "Respect"),    item.respect, item.respect, 30, "")
                .AddBar(ui::Loc("Привлекат.", "Sex Appeal"), item.sexy,    item.sexy,    30, "");
            card.Render();
        }
        return;
    }

    const auto& cat = wardrobe::Catalog();
    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc(g_activeShop->nameRus.c_str(), g_activeShop->nameEng.c_str()))
            .SetColumns(ui::Loc("КАТЕГОРИЯ ОДЕЖДЫ", "CLOTHING CATEGORY"), ui::Loc("КУПЛЕНО", "OWNED"))
            .SetFooter(ui::StandardFooter("Выбр.", "Select", "Выход", "Exit", "Q/E - Поворот", "Q/E - Spin"));

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
            .SetFooter(ui::StandardFooter("Куп./Над.", "Buy/Wear", "Назад", "Back", "Q/E - Поворот", "Q/E - Spin"));

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
    g_mode        = ShopMode::None;
    g_forwardExit = false;
    g_activeShop  = nullptr;
}

} // namespace clothesshop
