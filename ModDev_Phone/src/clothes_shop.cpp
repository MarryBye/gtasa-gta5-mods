#include "clothes_shop.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "phone_config.hpp"
#include "progression_rules.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"
#include "wardrobe.hpp"

namespace clothesshop {

namespace {

constexpr float kPi  = 3.14159265358979323846f;
constexpr float kPi2 = 6.28318530718530717959f;

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
std::vector<int>             g_availSlots;
std::vector<int>             g_slotItems;

wardrobe::ClothSnapshot      g_savedSnap{};
ui::OrbitCameraRig           g_camRig{};
sa::CVector                  g_savedPos{};
bool                         g_hasSavedPos      = false;
sa::CVector                  g_showroomPos{};
float                        g_savedHeadingDeg  = 0.0f;
bool                         g_hasSavedHeading  = false;
float                        g_pedYawDeg        = 0.0f;

struct ShowroomSpot {
    const char* shopKey;
    float       x, y, z;
    float       camDirAngle;
};

constexpr ShowroomSpot kShowroomSpots[6] = {
    {"LACS1",   203.78f,  -45.30f, 1001.80f, -kPi * 0.5f},
    {"CSSPRT",  207.05f, -135.60f, 1003.51f, -kPi * 0.5f},
    {"CSCHP",   207.74f, -105.80f, 1005.26f, -kPi * 0.5f},
    {"CLOTHGP", 161.39f,  -92.10f, 1001.80f, -kPi * 0.5f},
    {"CSEXL",   204.33f, -163.50f, 1000.52f, -kPi * 0.5f},
    {"CSDESGN", 223.10f,   -7.43f, 1002.21f,  0.0f},
};

const ShowroomSpot* FindShowroomSpot(const char* shopKey) {
    if (!shopKey) return nullptr;
    for (const auto& s : kShowroomSpots) {
        if (_stricmp(s.shopKey, shopKey) == 0) return &s;
    }
    return nullptr;
}

// Intercepts only the 6 Clothing Store chains (CLOT*) during Free Roam;
// Barber shops (HAIR*) stay unclaimed so GTA5UIKit restyles them natively with full barber animations!
sa::tMenuPanel* GetInterceptedPanel() {
    if (!pcfg::Get().enableClothesShopUI) return nullptr;
    sa::tMenuPanel* p = sa::GetMenuPanel(0);
    if (!p) return nullptr;

    const char* loadedShop = sa::shopping::GetLoadedShopName();
    if (std::strncmp(p->m_acTitle, "CLOT", 4) == 0) {
        if (!sa::IsOnMission() && wardrobe::FindShopCatalog(loadedShop)) {
            return p;
        }
    }
    return nullptr;
}

void BeginClothesPreviewCam(sa::CPed* ped) {
    if (!sa::IsPedValid(ped)) return;
    int32_t pedH = sa::GetPedHandle(ped);

    g_savedPos    = ped->GetPosition();
    g_hasSavedPos = true;

    if (pedH > 0) {
        sa::ScriptCmd(0x0172).Int(pedH).Out(g_savedHeadingDeg).Run();
        g_hasSavedHeading = true;
    }

    const ShowroomSpot* spot = g_activeShop ? FindShowroomSpot(g_activeShop->shopKey.c_str()) : nullptr;
    float preferredCamAngle  = 0.0f;
    bool  hasPreferredAngle  = false;
    if (spot && pedH > 0) {
        float gz = spot->z;
        sa::ScriptCmd(0x02CE).Float(spot->x).Float(spot->y).Float(spot->z + 1.5f).Out(gz).Run();
        float placeZ = (gz > spot->z - 3.0f && gz < spot->z + 2.0f) ? gz : (spot->z - 0.95f);
        g_showroomPos = {spot->x, spot->y, placeZ};
        sa::ScriptCmd(0x00A1).Int(pedH).Float(g_showroomPos.x).Float(g_showroomPos.y).Float(g_showroomPos.z).Run();
        sa::ScriptCmd(0x04D7).Int(pedH).Int(1).Run();
        sa::ScriptCmd(0x0337).Int(pedH).Int(1).Run();
        preferredCamAngle = spot->camDirAngle;
        hasPreferredAngle = true;
    } else {
        g_showroomPos = g_savedPos;
        if (pedH > 0) sa::ScriptCmd(0x0337).Int(pedH).Int(1).Run();
    }

    sa::CVector p   = ped->GetPosition();
    sa::CVector fwd = ped->GetForward();
    float fwdAngle  = hasPreferredAngle ? preferredCamAngle : std::atan2(fwd.y, fwd.x);
    sa::CVector eye{p.x, p.y, p.z + 0.35f};

    float initAngle = preferredCamAngle;
    float initDist  = 2.0f;
    if (hasPreferredAngle) {
        float clearPref = sa::ProbeLineOfSightClearDist(eye, preferredCamAngle, 2.75f);
        if (clearPref >= 1.85f) {
            initAngle = preferredCamAngle;
            initDist  = std::clamp(clearPref - 0.30f, 1.65f, 2.20f);
        } else {
            hasPreferredAngle = false;
        }
    }

    if (!hasPreferredAngle) {
        constexpr int kRays = 16;
        constexpr float kProbeMax = 2.75f;
        float clearD[kRays];
        for (int i = 0; i < kRays; ++i) {
            float a = kPi2 * static_cast<float>(i) / static_cast<float>(kRays);
            clearD[i] = sa::ProbeLineOfSightClearDist(eye, a, kProbeMax);
        }
        int   bestIdx   = 0;
        float bestScore = -1.0f;
        for (int i = 0; i < kRays; ++i) {
            float a        = kPi2 * static_cast<float>(i) / static_cast<float>(kRays);
            float cSelf    = clearD[i];
            float cLeft    = clearD[(i + kRays - 1) % kRays];
            float cRight   = clearD[(i + 1) % kRays];
            float widthMin = std::min({cSelf, cLeft * 0.94f + 0.15f, cRight * 0.94f + 0.15f});
            float score    = widthMin + 0.12f * std::cos(a - fwdAngle);
            if (score > bestScore) {
                bestScore = score;
                bestIdx   = i;
            }
        }
        initAngle = kPi2 * static_cast<float>(bestIdx) / static_cast<float>(kRays);
        initDist  = std::clamp(clearD[bestIdx] - 0.32f, 1.45f, 2.20f);
    }

    g_camRig.Begin(initAngle, initDist, 0.30f, 0.88f, 2.75f, 0.21f);
    g_camRig.minClearDist = 0.72f;
    g_camRig.wallMargin   = 0.22f;

    float toCamX = std::cos(initAngle);
    float toCamY = std::sin(initAngle);
    g_pedYawDeg  = std::atan2(-toCamX, toCamY) * (180.0f / kPi);
    if (pedH > 0) {
        sa::ScriptCmd(0x0173).Int(pedH).Float(g_pedYawDeg).Run();
    }
}

void EndPreviewCam() {
    if (g_camRig.active) {
        g_camRig.Reset();
        sa::CPed* ped = sa::FindPlayerPed(0);
        int32_t pedH  = sa::GetPedHandle(ped);
        if (pedH > 0) {
            if (g_hasSavedPos) {
                sa::ScriptCmd(0x04D7).Int(pedH).Int(0).Run();
                sa::ScriptCmd(0x00A1).Int(pedH).Float(g_savedPos.x).Float(g_savedPos.y).Float(g_savedPos.z - 0.95f).Run();
                g_hasSavedPos = false;
            }
            if (g_hasSavedHeading) {
                sa::ScriptCmd(0x0173).Int(pedH).Float(g_savedHeadingDeg).Run();
                g_hasSavedHeading = false;
            }
        }
        sa::ScriptCmd(0x02EB).Run();
    }
}

void RebuildAvailableSlots() {
    g_availSlots.clear();
    if (!g_activeShop) return;
    const auto& cat = wardrobe::Catalog();
    for (int s = 0; s < 7; ++s) {
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
    ui::TriggerPostCloseShield(160);
    sa::PlayFrontendSound(sa::SFX_BACK);
}

int ClothReqRank(const wardrobe::ClothItemDef& item) {
    return g_activeShop ? prog_rules::ClothItemRank(g_activeShop->shopKey.c_str(), item.price) : 1;
}

void HandleSelect() {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    // SA-COOP F4 Custom Skin Safety: NEVER deduct money when CClothes cannot apply to non-CJ models!
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~y~Доступно только для стандартного облика.",
                     "~y~Requires the default character model.");
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

        if (!owned) {
            int reqRank = ClothReqRank(item);
            if (!modbus::IsRankUnlocked(reqRank)) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowRankLockedHelp(reqRank);
                return;
            }
        }

        if (!owned && item.price > 0) {
            if (sa::GetPlayerMoney() < item.price) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег.", "~r~Not enough money.");
                return;
            }
            sa::AddPlayerMoney(-item.price);
            wardrobe::MarkClothOwned(idx);
            wardrobe::SaveNow();
            sa::PlayFrontendSound(sa::SFX_BUY);
            ui::ShowHelp("~g~Одежда добавлена в гардероб.", "~g~Clothing added to Wardrobe.");
        } else {
            sa::PlayFrontendSound(sa::SFX_SELECT);
            ui::ShowHelp("~g~Одежда надета.", "~g~Clothing equipped.");
        }

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
        g_screen = Screen::Categories;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    CloseShopCleanly();
}

void ProcessInput() {
    auto in = ui::ListInput().Poll(true);
    int* cur = (g_screen == Screen::Categories) ? &g_catCursor : &g_itemCursor;
    int  cnt = (g_screen == Screen::Categories)
        ? static_cast<int>(g_availSlots.size())
        : static_cast<int>(g_slotItems.size());

    if (cnt > 0 && ui::ListInput().ApplyToCursor(*cur, cnt, in)) {
        sa::PlayFrontendSound(sa::SFX_SCROLL);
        if (g_screen == Screen::Items) PreviewHighlightedItem();
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
    modlog::Info("[ClothesShop] Hooked CMenuSystem::Process at 0x0058FCDA.");
}

bool ShouldClaimShopPanel() {
    return g_shopActive || g_forwardExit || (GetInterceptedPanel() != nullptr);
}

void PreScriptsTick() {
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;

    sa::tMenuPanel* panel = GetInterceptedPanel();

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

        g_activeShop = wardrobe::FindShopCatalog(shopKey);
        if (!g_activeShop) return;

        g_shopActive = true;
        g_screen     = Screen::Categories;
        g_catCursor  = 0;
        g_itemCursor = 0;
        RebuildAvailableSlots();
        wardrobe::SyncWornClothesToOwned(ped);
        BeginClothesPreviewCam(ped);
        ui::LockMenuInputBriefly(180);
        sa::PlayFrontendSound(sa::SFX_SELECT);
        modlog::Info("[ClothesShop] Opened Clothes Shop '%s' (cam dist=%.2fm).",
                     g_activeShop->shopKey.c_str(), g_camRig.baseDist);
    } else if (!panel && g_shopActive) {
        EndPreviewCam();
        g_shopActive = false;
        return;
    }

    if (g_shopActive) {
        ProcessInput();
    }
}

void PostScriptsTick() {
    if (!g_shopActive || !g_camRig.active || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped)) return;

    float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
    auto cam = ui::ListInputController::PollCamera(dt, 1.85f, 1.40f);
    int32_t pedH  = sa::GetPedHandle(ped);

    if (pedH > 0) {
        sa::CVector curP = ped->GetPosition();
        if (g_hasSavedPos && curP.Dist2D(g_showroomPos) > 0.45f) {
            sa::ScriptCmd(0x00A1).Int(pedH).Float(g_showroomPos.x).Float(g_showroomPos.y).Float(g_showroomPos.z).Run();
            sa::ScriptCmd(0x04D7).Int(pedH).Int(1).Run();
        }
        sa::ScriptCmd(0x0337).Int(pedH).Int(1).Run();
        sa::ScriptCmd(0x0173).Int(pedH).Float(g_pedYawDeg).Run();
    }

    sa::CVector p = ped->GetPosition();
    float slotBaseDist = g_camRig.baseDist;
    float camHeightOff = 0.30f;
    float lookZOff     = 0.06f;

    if (g_screen == Screen::Items && g_catCursor >= 0 && g_catCursor < static_cast<int>(g_availSlots.size())) {
        int slotIdx = g_availSlots[g_catCursor];
        if (slotIdx == 2) {
            slotBaseDist = std::min(g_camRig.baseDist, 1.65f);
            camHeightOff = -0.12f;
            lookZOff     = -0.45f;
        } else if (slotIdx == 5 || slotIdx == 6) {
            slotBaseDist = std::min(g_camRig.baseDist, 1.32f);
            camHeightOff = 0.62f;
            lookZOff     = 0.56f;
        } else if (slotIdx == 3 || slotIdx == 4) {
            slotBaseDist = std::min(g_camRig.baseDist, 1.55f);
            camHeightOff = 0.42f;
            lookZOff     = 0.28f;
        }
    }

    g_camRig.Update(cam);
    g_camRig.dist         = std::clamp(slotBaseDist + g_camRig.zoomOffset, g_camRig.minDist, g_camRig.maxDist);
    g_camRig.heightOffset = camHeightOff;
    g_camRig.Apply(p, lookZOff, lookZOff + 0.15f);
}

void Render2D() {
    if (!g_shopActive || !g_activeShop || sa::IsPauseMenuActive()) return;

    const int32_t money = sa::GetPlayerMoney();

    ui::ListMenu menu;
    menu.SetPosition(24.0f, 72.0f, 194.0f).SetMaxVisible(11);

    const auto& cat = wardrobe::Catalog();
    if (g_screen == Screen::Categories) {
        menu.SetTitle(ui::Loc(g_activeShop->nameRus.c_str(), g_activeShop->nameEng.c_str()))
            .SetColumns(ui::Loc("КАТЕГОРИЯ", "CATEGORY"), ui::Loc("КУПЛЕНО", "OWNED"))
            .SetFooter(ui::StandardFooter("Выбрать", "Select", "Выход", "Exit", "Q/E Кам. Z/X Прибл.", "Q/E Cam Z/X Zoom"));

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
                         cb, (own > 0) ? ui::ColorSuccess() : ui::ColorNormal(), true);
        }
        menu.SetSelected(g_catCursor);
    } else {
        int slotIdx = g_availSlots[std::clamp(g_catCursor, 0, static_cast<int>(g_availSlots.size()) - 1)];
        menu.SetTitle(ui::Loc(wardrobe::SlotNameRus(slotIdx), wardrobe::SlotNameEng(slotIdx)))
            .SetColumns(ui::Loc("ОДЕЖДА", "ITEM"), ui::Loc("ЦЕНА", "PRICE"))
            .SetFooter(ui::StandardFooter("Купить", "Buy", "Назад", "Back", "Q/E Кам. Z/X Прибл.", "Q/E Cam Z/X Zoom"));

        for (int idx : g_slotItems) {
            const auto& item = cat[idx];
            bool owned   = wardrobe::IsClothOwned(idx);
            int  reqRank = ClothReqRank(item);
            if (!owned && !modbus::IsRankUnlocked(reqRank)) {
                menu.AddItem(wardrobe::GetClothDisplayNameGame(item), ui::FormatRankLock(reqRank), ui::ColorDisabled(), false);
            } else {
                std::string right = owned ? ui::Loc("Куплено", "Owned")
                                          : common::FormatPrice(item.price, ui::UseRussianFlag());
                sa::CRGBA col     = owned
                    ? ui::ColorSuccess()
                    : ((money >= item.price) ? ui::ColorNormal() : ui::ColorDanger());
                menu.AddItem(wardrobe::GetClothDisplayNameGame(item), right, col, true);
            }
        }
        menu.SetSelected(g_itemCursor);
    }
    menu.Render();

    if (g_screen == Screen::Items && g_itemCursor >= 0 && g_itemCursor < static_cast<int>(g_slotItems.size())) {
        const auto& item = cat[g_slotItems[g_itemCursor]];
        bool owned   = wardrobe::IsClothOwned(g_slotItems[g_itemCursor]);
        int  reqRank = ClothReqRank(item);
        std::string note;
        if (owned) {
            note = ui::Loc("Уже в гардеробе", "In Wardrobe");
        } else if (!modbus::IsRankUnlocked(reqRank)) {
            char lb[64];
            std::snprintf(lb, sizeof(lb), "%s: %d", ui::Loc("Нужен уровень", "Requires Rank").c_str(), reqRank);
            note = lb;
        } else {
            note = ui::Loc("Будет доступно в Телефоне", "Available in Phone after purchase");
        }
        ui::StatsCard card;
        card.SetTitle(ui::Loc("СТИЛЬ", "STYLE"), wardrobe::GetClothDisplayNameGame(item))
            .SetAnchor(true, 0.0f, 16.0f, 18.0f)
            .SetNote(note)
            .AddBar(ui::Loc("Авторитет",  "Respect"),    item.respect, item.respect, 30, "")
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
