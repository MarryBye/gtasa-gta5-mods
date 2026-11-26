#include "delivery.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include "config.hpp"
#include "dealership.hpp"
#include "garage.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "tuning_physics.hpp"
#include "ui_menu.hpp"
#include "workshop.hpp"

namespace delivery {

namespace {

constexpr int32_t MODEL_MECHANIC = 50; // WMYMECH — classic San Andreas mechanic skin!

enum class MenuMode {
    Closed = 0,
    VehicleList,
    InsuranceList,
    ConfirmRestore,
    ConfirmDelete
};

enum class DeliveryPhase {
    Idle = 0,
    DrivingToPlayer,
    MechanicExiting
};

// Menu state
MenuMode g_menuMode      = MenuMode::Closed;
bool     g_openedFromPhone = false;
int      g_page          = 0; // 0 or 1 (5 slots per page, 10 total)
int      g_rowInPage     = 0; // 0..4
int      g_insCursor     = 0;
int      g_confirmCursor = 0; // 0 = Yes, 1 = Cancel
int      g_pendingSlot   = -1;

// Active mechanic delivery state
DeliveryPhase g_phase          = DeliveryPhase::Idle;
int           g_delivSlot      = -1;
int32_t       g_delivVehHandle = -1;
int32_t       g_delivPedHandle = -1;
sa::CVector   g_targetRoadPos{};
sa::CVector   g_lastCheckPos{};
uint32_t      g_startMs        = 0;
uint32_t      g_lastRetargetMs = 0;
uint32_t      g_lastMoveMs     = 0;
uint32_t      g_exitStartMs    = 0;
int           g_stuckCount     = 0;

// Find closest road node to `pos` via 03D3 GET_CLOSEST_CAR_NODE_WITH_HEADING
bool FindClosestRoadNode(const sa::CVector& pos, sa::CVector& outNode, float& outHeading) {
    float nx = 0.0f, ny = 0.0f, nz = 0.0f, h = 0.0f;
    sa::ScriptCmd(0x03D3)
        .Float(pos.x).Float(pos.y).Float(pos.z)
        .Out(nx).Out(ny).Out(nz).Out(h)
        .Run();
    if (nx == 0.0f && ny == 0.0f && nz == 0.0f) return false;
    outNode = {nx, ny, nz};
    outHeading = h;
    return true;
}

// Validate player is near a drivable road (not in a field, forest, mountain, water, or interior!)
bool ValidatePlayerNearRoad(sa::CVector& outRoadPos, float& outRoadHeading) {
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!ped) return false;

    if (sa::GetActiveInterior() != 0) {
        ui::ShowHelp("~r~Нельзя вызвать транспорт в помещении!", "~r~Cannot request delivery indoors!");
        return false;
    }

    if (ped->IsInWater()) {
        ui::ShowHelp("~r~Механик не может доставить транспорт в воду!", "~r~Mechanic cannot deliver into water!");
        return false;
    }

    if (!mod::Config().allowOnMission && sa::IsOnMission()) {
        ui::ShowHelp("~r~Доставка недоступна во время миссии!", "~r~Delivery unavailable during a mission!");
        return false;
    }

    if (sa::CVehicle* curVeh = sa::FindPlayerVehicle(0, false)) {
        if (!curVeh->IsLandVehicle()) {
            ui::ShowHelp("~r~Нельзя вызвать транспорт, находясь в воздухе или на воде!", "~r~Cannot request delivery while airborne or on water!");
            return false;
        }
    }

    sa::CVector pPos = ped->GetPosition();
    if (!FindClosestRoadNode(pPos, outRoadPos, outRoadHeading)) {
        ui::ShowHelp("~r~Механик не может сюда доехать! Подойдите к дороге.", "~r~Mechanic cannot reach you here! Move closer to a road.");
        return false;
    }

    float dist2D = pPos.Dist2D(outRoadPos);
    float zDelta = std::fabs(pPos.z - outRoadPos.z);

    if (dist2D > mod::Config().maxRoadDistance || zDelta > 16.0f) {
        ui::ShowHelp(
            "~r~Вы слишком далеко от дороги!~w~ Механик не может доехать в поле.",
            "~r~Too far from a road!~w~ The mechanic cannot drive off-road here.");
        return false;
    }

    return true;
}

// Sample road nodes around player to find one ~100m away along the road network
bool FindDeliverySpawnNode(const sa::CVector& playerPos, float desiredDist, sa::CVector& outNode, float& outHeading) {
    bool found = false;
    float bestScore = 1e9f;

    const float radii[3] = {desiredDist, desiredDist * 0.80f, desiredDist * 1.25f};
    constexpr float PI2 = 6.28318530718f;

    for (float r : radii) {
        for (int i = 0; i < 16; ++i) {
            float ang = (PI2 * static_cast<float>(i)) / 16.0f;
            sa::CVector probe{
                playerPos.x + std::cos(ang) * r,
                playerPos.y + std::sin(ang) * r,
                playerPos.z
            };
            sa::CVector cand{};
            float candH = 0.0f;
            if (!FindClosestRoadNode(probe, cand, candH)) continue;

            float d = playerPos.Dist2D(cand);
            float dz = std::fabs(playerPos.z - cand.z);
            if (d < 55.0f || d > 165.0f || dz > 28.0f) continue;

            float score = std::fabs(d - desiredDist) + dz * 1.5f;
            if (score < bestScore) {
                bestScore  = score;
                outNode    = cand;
                outHeading = candH;
                found      = true;
            }
        }
        if (found && bestScore < 22.0f) break;
    }

    return found;
}

bool StartMechanicDelivery(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= mod::MAX_OWNED_VEHICLES) return false;
    auto& slot = garage::SlotsMutable()[slotIdx];
    if (!slot.occupied || slot.modelId < 400 || slot.modelId > 611) return false;

    sa::CVector targetRoad{};
    float targetHeading = 0.0f;
    if (!ValidatePlayerNearRoad(targetRoad, targetHeading)) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        return false;
    }

    sa::CPed* playerPed = sa::FindPlayerPed(0);
    if (!playerPed) return false;
    sa::CVector playerPos = playerPed->GetPosition();

    sa::CVector spawnPos{};
    float spawnHeading = targetHeading;
    if (!FindDeliverySpawnNode(playerPos, mod::Config().spawnDistance, spawnPos, spawnHeading)) {
        ui::ShowHelp("~r~Не удалось найти дорогу для доставки поблизости.", "~r~Could not find a nearby road for delivery.");
        sa::PlayFrontendSound(sa::SFX_ERROR);
        return false;
    }

    // Point vehicle roughly toward player along the road
    float toPlayerDeg = std::atan2(-(playerPos.x - spawnPos.x), (playerPos.y - spawnPos.y)) * (180.0f / 3.14159265f);
    if (toPlayerDeg < 0.0f) toPlayerDeg += 360.0f;
    // Choose whichever road lane heading (spawnHeading or spawnHeading+180) faces closer to player
    auto angDiff = [](float a, float b) {
        float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
        return std::fabs(d);
    };
    if (angDiff(spawnHeading + 180.0f, toPlayerDeg) < angDiff(spawnHeading, toPlayerDeg)) {
        spawnHeading = std::fmod(spawnHeading + 180.0f, 360.0f);
    }

    // Clean up any previous delivery or old personal vehicle
    CancelDelivery(false);
    sa::CVehicle* playerCurVeh = sa::FindPlayerVehicle(0, false);
    bool deleteOld = (garage::GetActivePersonalVehicle() != playerCurVeh);
    garage::DismissCurrentPersonalVehicle(deleteOld);

    // Stream vehicle & mechanic ped models safely
    sa::ScriptCmd(0x0247).Int(slot.modelId).Run();    // REQUEST_MODEL
    sa::ScriptCmd(0x0247).Int(MODEL_MECHANIC).Run();  // REQUEST_MODEL
    sa::ScriptCmd(0x038B).Run();                      // LOAD_ALL_MODELS_NOW

    // Apply custom plate before creation so GTA SA bakes the plate texture natively!
    if (slot.tuning.plateText[0] != '\0') {
        sa::ScriptCmd(0x0674).Int(slot.modelId).Str8(slot.tuning.plateText).Run(); // CUSTOM_PLATE_FOR_NEXT_CAR
    }

    // Spawn vehicle at road node ~100m away
    int32_t vehHandle = 0;
    sa::ScriptCmd(0x00A5)
        .Int(slot.modelId)
        .Float(spawnPos.x).Float(spawnPos.y).Float(spawnPos.z + 0.35f)
        .Out(vehHandle)
        .Run();

    sa::CVehicle* veh = sa::GetVehicleFromHandle(vehHandle);
    if (!veh) {
        sa::ScriptCmd(0x0249).Int(slot.modelId).Run();
        sa::ScriptCmd(0x0249).Int(MODEL_MECHANIC).Run();
        ui::ShowHelp("~r~Ошибка спавна транспорта!", "~r~Failed to spawn vehicle!");
        return false;
    }

    sa::ScriptCmd(0x0175).Int(vehHandle).Float(spawnHeading).Run(); // SET_CAR_HEADING

    // Apply ALL saved visuals (colors, paintjob, 15 vanilla mods) + GTA 5 tuning!
    slot.destroyed = false;
    garage::ApplySavedVehicleConfig(veh, slot);

    // Spawn mechanic ped inside driver seat
    int32_t mechHandle = 0;
    sa::ScriptCmd(0x0129)
        .Int(vehHandle)
        .Int(4) // PEDTYPE_CIVMALE
        .Int(MODEL_MECHANIC)
        .Out(mechHandle)
        .Run();

    if (mechHandle > 0) {
        // Give mechanic mild crash resilience during delivery
        sa::ScriptCmd(0x02AB).Int(mechHandle).Int(0).Int(1).Int(0).Int(1).Int(0).Run(); // SET_CHAR_PROOFS
    }

    // Release model stream locks (entities stay alive in world)
    sa::ScriptCmd(0x0249).Int(slot.modelId).Run();
    sa::ScriptCmd(0x0249).Int(MODEL_MECHANIC).Run();

    // Add radar blip (stays on map until player sits in vehicle!)
    int32_t blipHandle = 0;
    sa::ScriptCmd(0x0186).Int(vehHandle).Out(blipHandle).Run(); // ADD_BLIP_FOR_CAR
    if (blipHandle > 0) {
        sa::ScriptCmd(0x0165).Int(blipHandle).Int(5).Run(); // CHANGE_BLIP_COLOUR (light blue / cyan)
        sa::ScriptCmd(0x0168).Int(blipHandle).Int(2).Run(); // CHANGE_BLIP_SCALE
    }

    garage::SetActivePersonalVehicle(veh, slotIdx, blipHandle);

    // Order mechanic to drive along roads to player's nearest road point!
    sa::ScriptCmd(0x00AE).Int(vehHandle).Int(2).Run();     // SET_CAR_DRIVING_STYLE (2 = overtake/avoid cars smoothly)
    sa::ScriptCmd(0x00AD).Int(vehHandle).Float(23.0f).Run(); // SET_CAR_CRUISE_SPEED
    sa::ScriptCmd(0x00A7)
        .Int(vehHandle)
        .Float(targetRoad.x).Float(targetRoad.y).Float(targetRoad.z)
        .Run(); // CAR_GOTO_COORDINATES

    g_phase          = DeliveryPhase::DrivingToPlayer;
    g_delivSlot      = slotIdx;
    g_delivVehHandle = vehHandle;
    g_delivPedHandle = mechHandle;
    g_targetRoadPos  = targetRoad;
    g_lastCheckPos   = spawnPos;
    g_startMs        = sa::GetTimeMs();
    g_lastRetargetMs = g_startMs;
    g_lastMoveMs     = g_startMs;
    g_stuckCount     = 0;

    garage::SaveNow();

    ui::ShowHelp(
        "~g~Доставка в процессе!~w~ Механик уже едет к вам (отмечен на карте).",
        "~g~Delivery in progress!~w~ Your mechanic is on the way (marked on radar).");
    sa::PlayFrontendSound(sa::SFX_SELECT);
    modlog::Info("[Delivery] Started mechanic delivery of slot %d (model %d) from (%.1f, %.1f) -> (%.1f, %.1f)",
                 slotIdx, slot.modelId, spawnPos.x, spawnPos.y, targetRoad.x, targetRoad.y);
    return true;
}

void ReleaseMechanicToWander() {
    if (g_delivPedHandle > 0) {
        sa::CPed* mech = sa::GetPedFromHandle(g_delivPedHandle);
        if (mech && !sa::ScriptCmd(0x0118).Int(g_delivPedHandle).Run()) { // !IS_CHAR_DEAD
            sa::ScriptCmd(0x02AB).Int(g_delivPedHandle).Int(0).Int(0).Int(0).Int(0).Int(0).Run(); // clear proofs
            sa::ScriptCmd(0x05DE).Int(g_delivPedHandle).Run(); // TASK_WANDER_STANDARD
        }
        sa::ScriptCmd(0x01C2).Int(g_delivPedHandle).Run(); // MARK_CHAR_AS_NO_LONGER_NEEDED
        g_delivPedHandle = -1;
    }
    g_phase = DeliveryPhase::Idle;
    g_delivVehHandle = -1;
    g_delivSlot = -1;
}

void UpdateDeliveryStateMachine() {
    if (g_phase == DeliveryPhase::Idle) return;

    sa::CVehicle* veh = sa::GetVehicleFromHandle(g_delivVehHandle);
    sa::CPed* playerPed = sa::FindPlayerPed(0);

    // If vehicle was destroyed or despawned, stop tracking delivery cleanly
    if (!veh || !playerPed || sa::ScriptCmd(0x0119).Int(g_delivVehHandle).Run()) {
        ReleaseMechanicToWander();
        return;
    }

    // If player already entered the vehicle themselves, finish immediately!
    if (sa::FindPlayerVehicle(0, false) == veh) {
        sa::ScriptCmd(0x00A9).Int(g_delivVehHandle).Run(); // CAR_SET_IDLE
        ReleaseMechanicToWander();
        return;
    }

    const uint32_t now = sa::GetTimeMs();
    sa::CVector vPos = veh->GetPosition();
    sa::CVector pPos = playerPed->GetPosition();

    if (g_phase == DeliveryPhase::DrivingToPlayer) {
        // Check if mechanic died or left the car prematurely
        bool mechDead = (g_delivPedHandle <= 0) || sa::ScriptCmd(0x0118).Int(g_delivPedHandle).Run();
        bool mechInCar = (!mechDead) && sa::ScriptCmd(0x00DB).Int(g_delivPedHandle).Int(g_delivVehHandle).Run();
        if (mechDead || !mechInCar) {
            sa::ScriptCmd(0x00A9).Int(g_delivVehHandle).Run();
            ReleaseMechanicToWander();
            return;
        }

        float distToPlayer = vPos.Dist2D(pPos);
        float distToTarget = vPos.Dist2D(g_targetRoadPos);

        // Arrived!
        if (distToPlayer <= 15.0f || distToTarget <= 9.5f) {
            sa::ScriptCmd(0x00A9).Int(g_delivVehHandle).Run(); // CAR_SET_IDLE
            sa::ScriptCmd(0x00AD).Int(g_delivVehHandle).Float(0.0f).Run();
            // Gentle stop so mechanic exits smoothly
            if (veh->Speed() < 0.25f) {
                sa::ScriptCmd(0x04BA).Int(g_delivVehHandle).Float(0.0f).Run();
            }
            sa::ScriptCmd(0x05CD).Int(g_delivPedHandle).Int(g_delivVehHandle).Run(); // TASK_LEAVE_CAR
            g_phase = DeliveryPhase::MechanicExiting;
            g_exitStartMs = now;
            ui::ShowHelp("~g~Механик доставил ваш транспорт!", "~g~Your mechanic has delivered your vehicle!");
            modlog::Info("[Delivery] Mechanic arrived at dist %.1fm, exiting vehicle.", distToPlayer);
            return;
        }

        // Dynamic retarget if player moved >14m along roads
        if (now - g_lastRetargetMs >= 1600) {
            g_lastRetargetMs = now;
            if (pPos.Dist2D(g_targetRoadPos) > 14.0f) {
                sa::CVector newRoad{};
                float dummyH = 0.0f;
                if (FindClosestRoadNode(pPos, newRoad, dummyH) && pPos.Dist2D(newRoad) <= mod::Config().maxRoadDistance + 15.0f) {
                    g_targetRoadPos = newRoad;
                    sa::ScriptCmd(0x00A7)
                        .Int(g_delivVehHandle)
                        .Float(g_targetRoadPos.x).Float(g_targetRoadPos.y).Float(g_targetRoadPos.z)
                        .Run();
                }
            }
            // Slow down gently when close (<32m) for clean braking
            float cruise = (distToPlayer < 32.0f) ? 11.0f : 24.0f;
            sa::ScriptCmd(0x00AD).Int(g_delivVehHandle).Float(cruise).Run();
        }

        // Anti-stuck safeguard (handles GTA SA traffic jams gracefully)
        if (vPos.Dist2D(g_lastCheckPos) > 2.5f) {
            g_lastCheckPos = vPos;
            g_lastMoveMs   = now;
        } else if (now - g_lastMoveMs >= 6500) {
            g_lastMoveMs = now;
            ++g_stuckCount;
            if (g_stuckCount >= 4) {
                // Hard traffic gridlock fallback: relocate onto a clear road node ~35m away and finish approach
                sa::CVector rescuePos{};
                float rescueH = 0.0f;
                if (FindDeliverySpawnNode(pPos, 40.0f, rescuePos, rescueH)) {
                    sa::ScriptCmd(0x00AB).Int(g_delivVehHandle)
                        .Float(rescuePos.x).Float(rescuePos.y).Float(rescuePos.z + 0.4f).Run();
                    sa::ScriptCmd(0x0175).Int(g_delivVehHandle).Float(rescueH).Run();
                    g_stuckCount = 0;
                    modlog::Info("[Delivery] Relocated stuck mechanic onto clear road node.");
                }
            }
            sa::ScriptCmd(0x00AE).Int(g_delivVehHandle).Int(2).Run();
            sa::ScriptCmd(0x00A7)
                .Int(g_delivVehHandle)
                .Float(g_targetRoadPos.x).Float(g_targetRoadPos.y).Float(g_targetRoadPos.z)
                .Run();
            sa::ScriptCmd(0x04BA).Int(g_delivVehHandle).Float(7.0f).Run();
        }
    } else if (g_phase == DeliveryPhase::MechanicExiting) {
        // Keep vehicle stopped while mechanic steps out
        if (veh->Speed() > 0.02f) {
            sa::ScriptCmd(0x04BA).Int(g_delivVehHandle).Float(0.0f).Run();
        }
        bool stillInCar = (g_delivPedHandle > 0) &&
                          sa::ScriptCmd(0x00DB).Int(g_delivPedHandle).Int(g_delivVehHandle).Run();
        if (!stillInCar || (now - g_exitStartMs > 4500)) {
            ReleaseMechanicToWander();
        }
    }
}

void CloseMenuAndMaybeReturn() {
    bool backToPhone = g_openedFromPhone;
    g_menuMode = MenuMode::Closed;
    g_openedFromPhone = false;
    if (backToPhone) {
        if (auto* b = modbus::GetBus(); b && b->phoneInstalled && b->phoneReturnToMain) {
            b->phoneReturnToMain();
        }
    }
}

void HandleMenuInput() {
    const auto& k = ui::Keys();
    auto* bus = modbus::GetBus();
    bool phoneOwnsF6 = (bus && bus->phoneInstalled);

    // Toggle F6 menu ONLY when GTA5Phone.asi is NOT installed and no SA-COOP/ImGui overlay is active!
    if (!phoneOwnsF6 && !sa::IsExternalOverlayActive() && k.JustPressed(mod::Config().callMenuKey)) {
        if (g_menuMode != MenuMode::Closed) {
            g_menuMode = MenuMode::Closed;
            g_openedFromPhone = false;
            sa::PlayFrontendSound(sa::SFX_BACK);
            return;
        }
        if (!workshop::IsWorkshopActive() && !dealership::IsActive() &&
            !modbus::IsAnyModalMenuOpen("garage") &&
            !sa::IsPauseMenuActive() && !sa::IsCutsceneRunning()) {
            g_menuMode        = MenuMode::VehicleList;
            g_openedFromPhone = false;
            g_rowInPage       = std::clamp(g_rowInPage, 0, mod::VEHICLES_PER_PAGE - 1);
            sa::PlayFrontendSound(sa::SFX_SELECT);
            return;
        }
    }

    if (g_menuMode == MenuMode::Closed) return;

    // Close menu automatically if workshop/dealership/pause/cutscene opens
    if (workshop::IsWorkshopActive() || dealership::IsActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) {
        g_menuMode = MenuMode::Closed;
        g_openedFromPhone = false;
        return;
    }

    const bool up     = k.Repeated(VK_UP) || k.Repeated('W');
    const bool down   = k.Repeated(VK_DOWN) || k.Repeated('S');
    const bool left   = k.JustPressed(VK_LEFT) || k.JustPressed('A');
    const bool right  = k.JustPressed(VK_RIGHT) || k.JustPressed('D');
    const bool accept = k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE);
    const bool back   = k.JustPressed(VK_BACK)   || k.JustPressed(VK_ESCAPE) || (phoneOwnsF6 && k.JustPressed(VK_F6));
    const bool delKey = k.JustPressed(VK_DELETE);

    const int maxVeh   = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);
    const int numPages = (maxVeh + mod::VEHICLES_PER_PAGE - 1) / mod::VEHICLES_PER_PAGE;

    if (g_menuMode == MenuMode::InsuranceList) {
        std::vector<int> wreckedSlots;
        for (int i = 0; i < maxVeh; ++i) {
            if (garage::Slots()[i].occupied && garage::Slots()[i].destroyed) {
                wreckedSlots.push_back(i);
            }
        }
        if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            CloseMenuAndMaybeReturn();
            return;
        }
        if (wreckedSlots.empty()) return;
        g_insCursor = std::clamp(g_insCursor, 0, static_cast<int>(wreckedSlots.size()) - 1);
        if (up) {
            g_insCursor = (g_insCursor - 1 + static_cast<int>(wreckedSlots.size())) % static_cast<int>(wreckedSlots.size());
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (down) {
            g_insCursor = (g_insCursor + 1) % static_cast<int>(wreckedSlots.size());
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (accept) {
            g_pendingSlot   = wreckedSlots[g_insCursor];
            g_confirmCursor = 0;
            g_menuMode      = MenuMode::ConfirmRestore;
            sa::PlayFrontendSound(sa::SFX_SELECT);
        }
        return;
    }

    if (g_menuMode == MenuMode::VehicleList) {
        if (up) {
            g_rowInPage = (g_rowInPage - 1 + mod::VEHICLES_PER_PAGE) % mod::VEHICLES_PER_PAGE;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (down) {
            g_rowInPage = (g_rowInPage + 1) % mod::VEHICLES_PER_PAGE;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if ((left || right) && numPages > 1) {
            g_page = (g_page + (right ? 1 : -1) + numPages) % numPages;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            CloseMenuAndMaybeReturn();
        } else if (delKey) {
            int slotIdx = g_page * mod::VEHICLES_PER_PAGE + g_rowInPage;
            if (slotIdx < maxVeh && garage::Slots()[slotIdx].occupied) {
                g_pendingSlot   = slotIdx;
                g_confirmCursor = 1; // Default to Cancel for safety!
                g_menuMode      = MenuMode::ConfirmDelete;
                sa::PlayFrontendSound(sa::SFX_SELECT);
            }
        } else if (accept) {
            int slotIdx = g_page * mod::VEHICLES_PER_PAGE + g_rowInPage;
            if (slotIdx >= maxVeh || !garage::Slots()[slotIdx].occupied) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp(
                    "Слот пуст. Купите авто в автосалоне или зарегистрируйте в мастерской.",
                    "Slot empty. Buy a vehicle at a dealership or register one in a mod shop.");
                return;
            }
            if (g_phase != DeliveryPhase::Idle) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("Механик уже везёт ваш транспорт!", "Your mechanic is already on the way!");
                return;
            }

            const auto& slot = garage::Slots()[slotIdx];
            if (slot.destroyed) {
                g_pendingSlot   = slotIdx;
                g_confirmCursor = 0;
                g_menuMode      = MenuMode::ConfirmRestore;
                sa::PlayFrontendSound(sa::SFX_SELECT);
            } else {
                // Free delivery!
                if (StartMechanicDelivery(slotIdx)) {
                    g_menuMode = MenuMode::Closed;
                    g_openedFromPhone = false;
                }
            }
        }
        return;
    }

    if (g_menuMode == MenuMode::ConfirmRestore || g_menuMode == MenuMode::ConfirmDelete) {
        if (up || down) {
            g_confirmCursor = 1 - g_confirmCursor;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (back) {
            g_menuMode = MenuMode::VehicleList;
            sa::PlayFrontendSound(sa::SFX_BACK);
        } else if (accept) {
            if (g_confirmCursor == 1) {
                g_menuMode = MenuMode::VehicleList;
                sa::PlayFrontendSound(sa::SFX_BACK);
                return;
            }

            if (g_menuMode == MenuMode::ConfirmDelete) {
                garage::UnregisterSlot(g_pendingSlot);
                sa::PlayFrontendSound(sa::SFX_BACK);
                ui::ShowHelp("Транспорт удалён из личного гаража.", "Vehicle removed from personal garage.");
                g_menuMode = MenuMode::VehicleList;
                return;
            }

            // ConfirmRestore: validate road first so player doesn't lose money if standing in a field!
            int cost = mod::ComputeRestoreCost(sa::GetPlayerMoney());
            if (sa::GetPlayerMoney() < cost) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                ui::ShowHelp("~r~Недостаточно денег для восстановления!", "~r~Not enough money to restore!");
                g_menuMode = MenuMode::VehicleList;
                return;
            }

            sa::CVector dummyRoad{};
            float dummyH = 0.0f;
            if (!ValidatePlayerNearRoad(dummyRoad, dummyH)) {
                sa::PlayFrontendSound(sa::SFX_ERROR);
                g_menuMode = MenuMode::VehicleList;
                return;
            }

            sa::AddPlayerMoney(-cost);
            sa::PlayFrontendSound(sa::SFX_BUY);
            if (StartMechanicDelivery(g_pendingSlot)) {
                g_menuMode = MenuMode::Closed;
                g_openedFromPhone = false;
            } else {
                // Refund if spawn somehow failed
                sa::AddPlayerMoney(cost);
                g_menuMode = MenuMode::VehicleList;
            }
        }
    }
}

} // namespace

void Tick() {
    if (sa::GetGameState() != 9) return;
    HandleMenuInput();
    if (!sa::IsPauseMenuActive() && !sa::IsCutsceneRunning()) {
        UpdateDeliveryStateMachine();
    }
}

void Render2D() {
    if (g_menuMode == MenuMode::Closed) return;
    if (sa::IsPauseMenuActive()) return;

    const int maxVeh   = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);
    const int numPages = std::max(1, (maxVeh + mod::VEHICLES_PER_PAGE - 1) / mod::VEHICLES_PER_PAGE);

    ui::MenuDrawSpec spec;
    spec.x640 = 24.0f;
    spec.y448 = 110.0f;
    spec.w640 = 212.0f;
    spec.maxVisibleRows = 6;

    if (g_menuMode == MenuMode::VehicleList) {
        spec.titleGame   = ui::Loc("МЕХАНИК / ГАРАЖ", "MECHANIC / GARAGE");
        spec.colLeftGame = ui::Loc("ЛИЧНЫЙ ТРАНСПОРТ", "PERSONAL VEHICLES");
        char pageBuf[32];
        std::snprintf(pageBuf, sizeof(pageBuf), "%s %d/%d",
                      ui::Loc("СТР.", "PAGE").c_str(), g_page + 1, numPages);
        spec.colRightGame = pageBuf;
        spec.footerGame   = ui::Loc("Enter - Вызвать | <-/-> Стр. | Del - Удал. | F6 - Закр.",
                                    "Enter - Request | <-/-> Page | Del - Remove | F6 - Close");
        spec.selectedIndex = g_rowInPage;

        const auto& slots = garage::Slots();
        const int restoreCost = mod::ComputeRestoreCost(sa::GetPlayerMoney());
        const int activeSlot  = garage::GetActivePersonalSlot();
        const bool activeAlive = (garage::GetActivePersonalVehicle() != nullptr);

        for (int r = 0; r < mod::VEHICLES_PER_PAGE; ++r) {
            int idx = g_page * mod::VEHICLES_PER_PAGE + r;
            ui::MenuRow row;
            if (idx >= maxVeh || !slots[idx].occupied) {
                char emptyLbl[48];
                std::snprintf(emptyLbl, sizeof(emptyLbl), "%d. %s", idx + 1, ui::Loc("Пустой слот", "Empty Slot").c_str());
                row.leftGame   = emptyLbl;
                row.rightGame  = "---";
                row.enabled    = false;
                row.rightColor = {115, 115, 120, 255};
            } else {
                const auto& v = slots[idx];
                char nm[96];
                std::snprintf(nm, sizeof(nm), "%d. %s", idx + 1, v.displayNameGame.c_str());
                row.leftGame = nm;
                row.enabled  = true;

                if (v.destroyed) {
                    char dst[48];
                    std::snprintf(dst, sizeof(dst), "%s ($%d)", ui::Loc("Уничтож.", "Wrecked").c_str(), restoreCost);
                    row.rightGame  = dst;
                    row.rightColor = {240, 105, 95, 255};
                } else if (activeAlive && activeSlot == idx) {
                    row.rightGame  = (g_phase != DeliveryPhase::Idle)
                        ? ui::Loc("В пути...", "On the way")
                        : ui::Loc("На карте", "Active");
                    row.rightColor = {130, 195, 255, 255};
                } else {
                    row.rightGame  = ui::Loc("Бесплатно", "Free");
                    row.rightColor = {125, 235, 135, 255};
                }
            }
            spec.rows.push_back(std::move(row));
        }
    } else if (g_menuMode == MenuMode::InsuranceList) {
        spec.titleGame    = ui::Loc("СТРАХОВАЯ КОМПАНИЯ", "MORS MUTUAL INSURANCE");
        spec.colLeftGame  = ui::Loc("УНИЧТОЖЕННЫЕ АВТО", "WRECKED VEHICLES");
        spec.colRightGame = ui::Loc("ВЫПЛАТА", "CLAIM");
        spec.footerGame   = ui::Loc("Enter - Восст. | Backspace - Назад", "Enter - Claim | Backspace - Back");
        const int restoreCost = mod::ComputeRestoreCost(sa::GetPlayerMoney());
        const auto& slots = garage::Slots();
        std::vector<int> wreckedSlots;
        for (int i = 0; i < maxVeh; ++i) {
            if (slots[i].occupied && slots[i].destroyed) {
                wreckedSlots.push_back(i);
                ui::MenuRow row;
                row.leftGame   = slots[i].displayNameGame;
                row.rightGame  = ui::FormatPrice(restoreCost);
                row.rightColor = (sa::GetPlayerMoney() >= restoreCost)
                    ? sa::CRGBA{125, 235, 135, 255}
                    : sa::CRGBA{240, 100, 95, 255};
                spec.rows.push_back(std::move(row));
            }
        }
        if (spec.rows.empty()) {
            spec.rows.push_back({ui::Loc("Нет уничтоженного транспорта", "No wrecked vehicles"), "", false, false, {140, 140, 140, 255}});
            spec.selectedIndex = 0;
        } else {
            spec.selectedIndex = std::clamp(g_insCursor, 0, static_cast<int>(spec.rows.size()) - 1);
        }
    } else if (g_menuMode == MenuMode::ConfirmRestore && g_pendingSlot >= 0) {
        int cost = mod::ComputeRestoreCost(sa::GetPlayerMoney());
        spec.titleGame    = ui::Loc("СТРАХОВАЯ КОМПАНИЯ", "INSURANCE CLAIM");
        spec.colLeftGame  = garage::Slots()[g_pendingSlot].displayNameGame;
        char pctBuf[32];
        std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", mod::Config().restorePercent);
        spec.colRightGame = pctBuf;
        spec.footerGame   = ui::Loc("Enter - Да | Backspace - Назад", "Enter - Confirm | Backspace - Back");
        spec.selectedIndex = g_confirmCursor;

        char costBuf[32];
        std::snprintf(costBuf, sizeof(costBuf), "$%d", cost);
        bool canAfford = (sa::GetPlayerMoney() >= cost);
        spec.rows.push_back({ui::Loc("Восстановить и доставить", "Restore & Deliver"), costBuf, true, false,
                             canAfford ? sa::CRGBA{125, 235, 135, 255} : sa::CRGBA{240, 100, 95, 255}});
        spec.rows.push_back({ui::Loc("Отмена", "Cancel"), "", true, false, {200, 200, 200, 255}});
    } else if (g_menuMode == MenuMode::ConfirmDelete && g_pendingSlot >= 0) {
        spec.titleGame    = ui::Loc("УДАЛИТЬ ТРАНСПОРТ?", "REMOVE VEHICLE?");
        spec.colLeftGame  = garage::Slots()[g_pendingSlot].displayNameGame;
        spec.colRightGame = "";
        spec.footerGame   = ui::Loc("Enter - Да | Backspace - Назад", "Enter - Confirm | Backspace - Back");
        spec.selectedIndex = g_confirmCursor;

        spec.rows.push_back({ui::Loc("Да, снять с учёта", "Yes, Unregister"), "", true, false, {240, 110, 100, 255}});
        spec.rows.push_back({ui::Loc("Отмена", "Cancel"), "", true, false, {200, 200, 200, 255}});
    }

    ui::DrawVanillaMenu(spec);

    // Show live Vehicle Stats Card at bottom-right (with RED ETS 2 wear-loss bars!)
    int previewIdx = -1;
    if (g_menuMode == MenuMode::VehicleList) {
        previewIdx = g_page * mod::VEHICLES_PER_PAGE + g_rowInPage;
    } else if (g_menuMode == MenuMode::InsuranceList) {
        int seen = 0;
        for (int i = 0; i < maxVeh; ++i) {
            if (garage::Slots()[i].occupied && garage::Slots()[i].destroyed) {
                if (seen == g_insCursor) { previewIdx = i; break; }
                ++seen;
            }
        }
    } else {
        previewIdx = g_pendingSlot;
    }

    if (previewIdx >= 0 && previewIdx < maxVeh) {
        const auto& slot = garage::Slots()[previewIdx];
        if (slot.occupied) {
            auto actualStats   = tuning::ComputeStatsForModel(slot.modelId, slot.tuning, slot.wear);
            auto pristineStats = tuning::ComputeStatsForModel(slot.modelId, slot.tuning, mod::VehicleWear{});
            std::string note;
            if (slot.destroyed) {
                note = ui::Loc("Уничтожен (нужна страховка)", "Wrecked (insurance req.)");
            } else {
                if (mod::Config().enableWearSystem && slot.wear.HasAnyWear()) {
                    char wbuf[48];
                    std::snprintf(wbuf, sizeof(wbuf), "%s %d%%",
                                  ui::Loc("Износ", "Wear").c_str(),
                                  static_cast<int>(std::round(slot.wear.Average())));
                    note += wbuf;
                }
                if (slot.tuning.turbo) {
                    if (!note.empty()) note += " | ";
                    note += ui::Loc("Турбо", "Turbo");
                }
                if (slot.tuning.bulletproofTires) {
                    if (!note.empty()) note += " | ";
                    note += ui::Loc("Пулест.", "BP");
                }
                if (slot.tuning.plateText[0] != '\0') {
                    if (!note.empty()) note += " | ";
                    note += slot.tuning.plateText;
                }
            }
            const auto* prisPtr = (mod::Config().enableWearSystem && slot.wear.HasAnyWear()) ? &pristineStats : nullptr;
            ui::DrawVehicleStatsPanel(slot.displayNameGame, actualStats, actualStats, true, note, prisPtr);
        }
    }
}

void OpenMechanicMenu(bool fromPhone) {
    if (workshop::IsWorkshopActive() || dealership::IsActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    g_menuMode        = MenuMode::VehicleList;
    g_openedFromPhone = fromPhone;
    g_rowInPage       = std::clamp(g_rowInPage, 0, mod::VEHICLES_PER_PAGE - 1);
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void OpenInsuranceMenu(bool fromPhone) {
    if (workshop::IsWorkshopActive() || dealership::IsActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    g_menuMode        = MenuMode::InsuranceList;
    g_openedFromPhone = fromPhone;
    g_insCursor       = 0;
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

bool StartDeliveryForSlot(int slotIdx) {
    return StartMechanicDelivery(slotIdx);
}

bool IsMenuOpen() { return g_menuMode != MenuMode::Closed; }
bool IsDeliveryInProgress() { return g_phase != DeliveryPhase::Idle; }

void CancelDelivery(bool deleteVehicle) {
    if (g_delivPedHandle > 0) {
        sa::ScriptCmd(0x009B).Int(g_delivPedHandle).Run(); // DELETE_CHAR
        g_delivPedHandle = -1;
    }
    if (deleteVehicle && g_delivVehHandle > 0) {
        sa::ScriptCmd(0x00A6).Int(g_delivVehHandle).Run(); // DELETE_CAR
    }
    g_delivVehHandle = -1;
    g_delivSlot = -1;
    g_phase = DeliveryPhase::Idle;
}

void Reset() {
    g_menuMode        = MenuMode::Closed;
    g_openedFromPhone = false;
    g_phase           = DeliveryPhase::Idle;
    g_delivVehHandle  = -1;
    g_delivPedHandle  = -1;
    g_delivSlot       = -1;
}

} // namespace delivery
