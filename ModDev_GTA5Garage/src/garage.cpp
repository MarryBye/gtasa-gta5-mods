#include "garage.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "common_utils.hpp"
#include "delivery.hpp"
#include "garage_config.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "mod_bus.hpp"
#include "storage.hpp"
#include "tuning_physics.hpp"
#include "ui_kit.hpp"
#include "workshop.hpp"

namespace garage {

namespace {

int g_activeSaveSlot = 0; // 1..8 or 0 (autosave / SA-COOP client)
std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES> g_slots{};
std::array<mod::SafehouseCarRecord, mod::MAX_SAFEHOUSE_CARS> g_safehouseCars{};

sa::CVehicle* g_activeVeh        = nullptr;
int32_t       g_activeVehHandle  = -1;
int           g_activeSlot       = -1;
int32_t       g_deliveryBlip     = 0;
uint32_t      g_lastSafehouseScanMs = 0;
bool          g_safehouseDirtySave  = false;
bool          g_safehouseHooksInstalled = false;

using FnIsPointInside = bool(__thiscall*)(const void*, float, float, float);
FnIsPointInside g_origIsPointInside1 = nullptr;
FnIsPointInside g_origIsPointInside2 = nullptr;
FnIsPointInside g_origIsPointInside3 = nullptr;

// Fix GTA SA's x87 FPU NaN bug in CGarage::IsPointInsideGarage while chaining any SilentPatch / external hook!
bool SafeIsPointInside(FnIsPointInside origFn, const void* garage, float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    if (std::fabs(x) > 5000.0f || std::fabs(y) > 5000.0f || std::fabs(z) > 2000.0f) return false;
    return origFn ? origFn(garage, x, y, z) : false;
}

bool __fastcall HookedGarageIsPointInside1(const void* garage, int, float x, float y, float z) {
    return SafeIsPointInside(g_origIsPointInside1, garage, x, y, z);
}
bool __fastcall HookedGarageIsPointInside2(const void* garage, int, float x, float y, float z) {
    return SafeIsPointInside(g_origIsPointInside2, garage, x, y, z);
}
bool __fastcall HookedGarageIsPointInside3(const void* garage, int, float x, float y, float z) {
    return SafeIsPointInside(g_origIsPointInside3, garage, x, y, z);
}

// Purge corrupted NaN / out-of-world ghost cars from all 20 safehouse garages (heals existing saves too!)
// and remove any orphaned driverless NaN vehicles from CVehiclePool so they can never block garage capacity.
void SanitizeSafehouseStoredCars() {
    for (int h = 0; h < sa::MAX_SAFEHOUSE_HIDEOUTS; ++h) {
        sa::CStoredCar* arr = sa::GetSafehouseStoredCars(h);
        if (!arr) continue;
        for (int s = 0; s < sa::MAX_CARS_PER_SAFEHOUSE; ++s) {
            auto& sc = arr[s];
            if (sc.m_nModelIndex == 0) continue;
            bool badPos = (!std::isfinite(sc.x) || !std::isfinite(sc.y) || !std::isfinite(sc.z) ||
                           std::fabs(sc.x) > 5000.0f || std::fabs(sc.y) > 5000.0f || std::fabs(sc.z) > 2000.0f);
            bool badModel = (sc.m_nModelIndex < 400 || sc.m_nModelIndex > 611);
            if (badPos || badModel) {
                uint16_t oldMid = sc.m_nModelIndex;
                sc.m_nModelIndex = 0;
                modlog::Info("[Garage] Purged corrupted NaN/OOB ghost car (model=%u) from safehouse hideout #%d slot #%d.",
                             static_cast<unsigned>(oldMid), h, s);
            }
        }
    }

    auto* pool = sa::GetVehiclePool();
    if (!pool || !pool->objects || !pool->byteMap || pool->size <= 0 || pool->size > 65536) return;
    sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
    for (int i = 0; i < pool->size; ++i) {
        if ((pool->byteMap[i] & 0x80) != 0) continue;
        auto* v = reinterpret_cast<sa::CVehicle*>(pool->objects + static_cast<size_t>(i) * sa::VEHICLE_POOL_STRIDE);
        if (!sa::IsVehicleValid(v) || v == playerVeh || v->GetDriver() != nullptr) continue;
        sa::CVector p = v->GetPosition();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            int32_t handle = sa::GetVehicleHandle(v);
            if (handle > 0) {
                modlog::Info("[Garage] Deleting orphaned live NaN ghost vehicle (model=%d, handle=%d).",
                             v->GetModelIndex(), handle);
                sa::ScriptCmd(0x00A6).Int(handle).Run(); // DELETE_CAR
            }
        }
    }
}

mod::VisualAppearance CaptureVisuals(sa::CVehicle* veh) {
    mod::VisualAppearance vis{};
    if (!sa::IsVehicleValid(veh)) return vis;
    int32_t handle = sa::GetVehicleHandle(veh);
    if (handle > 0) {
        int32_t c1 = veh->PrimaryColor();
        int32_t c2 = veh->SecondaryColor();
        sa::ScriptCmd(0x03F3).Int(handle).Out(c1).Out(c2).Run();
        vis.primaryColor   = static_cast<uint8_t>(c1);
        vis.secondaryColor = static_cast<uint8_t>(c2);
        int32_t pj = -1;
        sa::ScriptCmd(0x0988).Int(handle).Out(pj).Run();
        vis.paintjob = (pj >= 0 && pj <= 3) ? static_cast<int8_t>(pj) : -1;
    } else {
        vis.primaryColor   = veh->PrimaryColor();
        vis.secondaryColor = veh->SecondaryColor();
        vis.paintjob       = -1;
    }

    const int16_t* upgs = veh->Upgrades();
    int16_t lastNitro = -1;
    for (int i = 0; i < 15; ++i) {
        if (upgs[i] == 1008 || upgs[i] == 1009 || upgs[i] == 1010) lastNitro = upgs[i];
    }
    int writeIdx = 0;
    for (int i = 0; i < 15; ++i) vis.upgrades[i] = -1;
    bool wroteNitro = false;
    for (int i = 0; i < 15 && writeIdx < 15; ++i) {
        int16_t u = upgs[i];
        if (u < 1000) continue;
        if (u == 1008 || u == 1009 || u == 1010) {
            if (!wroteNitro && lastNitro >= 1000) {
                vis.upgrades[writeIdx++] = lastNitro;
                wroteNitro = true;
            }
            continue;
        }
        vis.upgrades[writeIdx++] = u;
    }
    return vis;
}

// Returns match quality: 0 = mismatch, 1 = model+color match, 2 = model+color+upgrade overlap match
int ScoreSafehouseMatch(sa::CVehicle* veh, int32_t modelId, const mod::VisualAppearance& vis) {
    if (!sa::IsVehicleValid(veh) || veh->GetModelIndex() != modelId) return 0;
    if (veh->PrimaryColor() != vis.primaryColor || veh->SecondaryColor() != vis.secondaryColor) return 0;

    int savedCount = 0;
    int hitCount   = 0;
    const int16_t* liveUpg = veh->Upgrades();
    for (int i = 0; i < 15; ++i) {
        int16_t su = vis.upgrades[i];
        if (su < 1000) continue;
        ++savedCount;
        for (int j = 0; j < 15; ++j) {
            if (liveUpg[j] == su) { ++hitCount; break; }
        }
    }
    if (savedCount == 0) return 2;
    if (hitCount > 0) return 2 + hitCount;
    return 1; // GTA SA CStoredCar may omit custom universal upgrades -> still match by garage+model+colors!
}

void UpsertSafehouseRecord(int hideoutGarageId, sa::CVehicle* veh, int personalSlotIdx,
                           const mod::PerformanceTuning& tun, const mod::VehicleWear& wear) {
    if (hideoutGarageId < 0 || !sa::IsVehicleValid(veh)) return;
    int32_t mid = veh->GetModelIndex();
    auto vis = CaptureVisuals(veh);

    int targetIdx = -1;
    int freeIdx   = -1;
    for (int i = 0; i < mod::MAX_SAFEHOUSE_CARS; ++i) {
        const auto& r = g_safehouseCars[i];
        if (!r.occupied) {
            if (freeIdx < 0) freeIdx = i;
            continue;
        }
        if (personalSlotIdx >= 0 && r.personalSlotIdx == personalSlotIdx) {
            targetIdx = i;
            break;
        }
        if (r.hideoutGarageId == hideoutGarageId && r.modelId == mid &&
            r.visuals.primaryColor == vis.primaryColor && r.visuals.secondaryColor == vis.secondaryColor) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0) targetIdx = (freeIdx >= 0) ? freeIdx : (mod::MAX_SAFEHOUSE_CARS - 1);

    auto& dst = g_safehouseCars[targetIdx];
    bool changed = (!dst.occupied || dst.hideoutGarageId != hideoutGarageId ||
                    dst.personalSlotIdx != personalSlotIdx || dst.modelId != mid ||
                    dst.visuals.primaryColor != vis.primaryColor ||
                    dst.visuals.secondaryColor != vis.secondaryColor ||
                    dst.tuning.engineLevel != tun.engineLevel ||
                    dst.tuning.turbo != tun.turbo ||
                    std::strcmp(dst.tuning.plateText, tun.plateText) != 0);
    dst.occupied        = true;
    dst.hideoutGarageId = hideoutGarageId;
    dst.personalSlotIdx = personalSlotIdx;
    dst.modelId         = mid;
    dst.visuals         = vis;
    dst.tuning          = tun;
    dst.wear            = wear;
    if (changed) g_safehouseDirtySave = true;
}

void RestoreTuningOntoSafehouseVehicle(sa::CVehicle* veh, const mod::VisualAppearance& vis,
                                      const mod::PerformanceTuning& tun, const mod::VehicleWear& wear,
                                      int personalSlotIdx) {
    if (!sa::IsVehicleValid(veh)) return;
    int32_t handle = sa::GetVehicleHandle(veh);
    if (handle <= 0) return;

    // Re-attach any missing custom/universal visual upgrades that GTA SA's CStoredCar didn't keep
    sa::EnsureUniversalUpgradeMounts(veh);
    const int16_t* liveUpg = veh->Upgrades();
    bool needReq = false;
    int16_t missing[15] = {};
    int missingCnt = 0;
    for (int i = 0; i < 15; ++i) {
        int16_t u = vis.upgrades[i];
        if (u < 1000 || u > 1193) continue;
        bool present = false;
        for (int j = 0; j < 15; ++j) {
            if (liveUpg[j] == u) { present = true; break; }
        }
        if (!present) {
            missing[missingCnt++] = u;
            sa::ScriptCmd(0x06E9).Int(u).Run();
            needReq = true;
        }
    }
    if (needReq) {
        sa::ScriptCmd(0x038B).Run();
        for (int i = 0; i < missingCnt; ++i) {
            int32_t dummyOut = 0;
            sa::ScriptCmd(0x06E7).Int(handle).Int(missing[i]).Out(dummyOut).Run();
            sa::ScriptCmd(0x06EB).Int(missing[i]).Run();
        }
    }

    if (tun.plateText[0] != '\0') {
        sa::ApplyLiveCustomPlate(veh, tun.plateText);
    }
    tuning::ApplyTuning(veh, tun, &wear);

    if (personalSlotIdx >= 0 && personalSlotIdx < mod::MAX_OWNED_VEHICLES &&
        g_slots[personalSlotIdx].occupied && !g_slots[personalSlotIdx].destroyed) {
        if (!GetActivePersonalVehicle()) {
            g_activeVeh       = veh;
            g_activeVehHandle = handle;
            g_activeSlot      = personalSlotIdx;
        }
    }
    // Keep CreatedBy == 1 (RANDOM_VEHICLE) while parked inside the safehouse garage so walking away
    // again cleanly stores & repairs it in GTA SA's CStoredCar table!
    modlog::Info("[Garage] Restored safehouse vehicle tuning/wear/plate (model=%d, personalSlot=%d, eng=%u, turbo=%d).",
                 veh->GetModelIndex(), personalSlotIdx, tun.engineLevel, tun.turbo ? 1 : 0);
}

void TickSafehousePersistence() {
    if (sa::GetActiveInterior() != 0 || workshop::IsWorkshopActive()) return;
    uint32_t nowMs = sa::GetTimeMs();
    sa::CPed* localPed = sa::FindPlayerPed(0);
    sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
    const bool delivering = delivery::IsDeliveryInProgress();

    // Fast per-frame (zero-delay!) release of our active personal vehicle the instant CJ exits it inside
    // a safehouse garage so StoreAndRemoveCarsForThisHideout ALWAYS accepts, stores, and repairs it!
    if (g_activeVeh && g_activeSlot >= 0 && !delivering &&
        sa::IsVehicleValid(g_activeVeh) && sa::GetVehicleHandle(g_activeVeh) == g_activeVehHandle &&
        g_activeVeh->GetDriver() == nullptr && g_activeVeh->CreatedBy() == 2) {
        int activeGarage = sa::FindHideoutGarageIndexAt(g_activeVeh->GetPosition());
        if (activeGarage >= 0) {
            int32_t h = sa::GetVehicleHandle(g_activeVeh);
            if (h > 0) {
                sa::ScriptCmd(0x01C3).Int(h).Run(); // MARK_CAR_AS_NO_LONGER_NEEDED
            }
            g_activeVeh->CreatedBy() = 1; // RANDOM_VEHICLE -> stored & repaired cleanly when door closes!
            mod::PerformanceTuning tun = tuning::GetLiveTuning(g_activeVeh)
                ? *tuning::GetLiveTuning(g_activeVeh)
                : g_slots[g_activeSlot].tuning;
            mod::VehicleWear wear = tuning::GetLiveWear(g_activeVeh);
            UpsertSafehouseRecord(activeGarage, g_activeVeh, g_activeSlot, tun, wear);
            SnapshotVehicleToSlot(g_activeVeh, g_activeSlot);
            g_safehouseDirtySave = true;
            modlog::Info("[Garage] Released personal vehicle (slot=%d, model=%d) to safehouse garage #%d for vanilla storage & repair.",
                         g_activeSlot, g_activeVeh->GetModelIndex(), activeGarage);
        }
    }

    bool urgentPlayerCheck = (playerVeh != nullptr && tuning::GetLiveTuning(playerVeh) == nullptr);
    // Keep scan interval tight (180ms) so parked/restored vehicles sync promptly!
    if (!urgentPlayerCheck && (nowMs - g_lastSafehouseScanMs < 180)) return;
    g_lastSafehouseScanMs = nowMs;

    // Continuously guard against NaN/OOB ghost vehicles corrupting safehouse garage slots
    SanitizeSafehouseStoredCars();

    auto* pool = sa::GetVehiclePool();
    if (!pool || !pool->objects || !pool->byteMap || pool->size <= 0 || pool->size > 65536) return;

    const bool coopActive = integrations::sacoop::IsInMultiplayer();

    for (int i = 0; i < pool->size; ++i) {
        if ((pool->byteMap[i] & 0x80) != 0) continue;
        auto* v = reinterpret_cast<sa::CVehicle*>(pool->objects + static_cast<size_t>(i) * sa::VEHICLE_POOL_STRIDE);
        if (!sa::IsVehicleValid(v) || v->Health() <= 0.0f) continue;

        // SA-COOP guard: never touch a vehicle currently driven by another player or NPC!
        sa::CPed* drv = v->GetDriver();
        if (drv != nullptr && drv != localPed) continue;

        int gIdx = sa::FindHideoutGarageIndexAt(v->GetPosition());
        if (gIdx < 0) continue;

        const auto* liveTun = tuning::GetLiveTuning(v);
        int liveSlot = FindSlotForLiveVehicle(v);

        if (liveTun != nullptr || liveSlot >= 0) {
            // 1. Live tuned / personal vehicle currently parked inside a safehouse garage -> remember it!
            mod::PerformanceTuning tun = liveTun ? *liveTun : g_slots[liveSlot].tuning;
            mod::VehicleWear wear      = tuning::GetLiveWear(v);
            if (liveSlot >= 0 || mod::HasAnyCustomTuning(tun) || wear.HasAnyWear()) {
                UpsertSafehouseRecord(gIdx, v, liveSlot, tun, wear);
                if (liveSlot >= 0 && liveSlot < mod::MAX_OWNED_VEHICLES) {
                    SnapshotVehicleToSlot(v, liveSlot);
                }
            }

            // CRITICAL FIX for safehouse storage & auto-repair:
            // CGarage::StoreAndRemoveCarsForThisHideout (0x00449995) skips any car where
            // veh->m_nCreatedBy == 2 (MISSION_VEHICLE, set by 0x00A5 CREATE_CAR from Mechanic/Dealership)!
            // As soon as our personal car is parked inside a hideout garage with no driver (and not mid-delivery),
            // release it to RANDOM_VEHICLE (1) so GTA SA stores it in GTASAsfN.b AND repairs it when the door closes!
            if (liveSlot >= 0 && v == g_activeVeh && drv == nullptr && !delivering && v->CreatedBy() == 2) {
                int32_t h = sa::GetVehicleHandle(v);
                if (h > 0) {
                    sa::ScriptCmd(0x01C3).Int(h).Run(); // MARK_CAR_AS_NO_LONGER_NEEDED
                }
                v->CreatedBy() = 1; // RANDOM_VEHICLE -> accepted by StoreAndRemoveCarsForThisHideout!
                g_safehouseDirtySave = true;
                modlog::Info("[Garage] Released personal vehicle (slot=%d, model=%d) to safehouse garage #%d for vanilla storage & repair.",
                             liveSlot, v->GetModelIndex(), gIdx);
            }
            continue;
        }

        // 2. Untuned vehicle sitting inside a safehouse garage -> check if GTA SA just respawned our tuned car!
        int bestRec   = -1;
        int bestScore = 0;
        for (int r = 0; r < mod::MAX_SAFEHOUSE_CARS; ++r) {
            const auto& rec = g_safehouseCars[r];
            if (!rec.occupied || rec.hideoutGarageId != gIdx) continue;
            int sc = ScoreSafehouseMatch(v, rec.modelId, rec.visuals);
            if (sc > bestScore) {
                bestScore = sc;
                bestRec   = r;
            }
        }
        if (bestRec >= 0) {
            auto& rec = g_safehouseCars[bestRec];
            if (rec.personalSlotIdx >= 0 && rec.personalSlotIdx < mod::MAX_OWNED_VEHICLES &&
                g_slots[rec.personalSlotIdx].occupied && !g_slots[rec.personalSlotIdx].destroyed) {
                const auto& sl = g_slots[rec.personalSlotIdx];
                rec.tuning  = sl.tuning;
                rec.wear    = sl.wear;
                rec.visuals = sl.visuals;
            }
            RestoreTuningOntoSafehouseVehicle(v, rec.visuals, rec.tuning, rec.wear, rec.personalSlotIdx);
            continue;
        }

        // 3. Fallback for personal cars parked in a safehouse garage BEFORE this update (match occupied slots!)
        // In SA-COOP, only run fallback when the car is unoccupied or driven by local CJ.
        if (!GetActivePersonalVehicle() && (!coopActive || drv == nullptr || drv == localPed)) {
            int bestSlot = -1;
            int bestSlSc = 0;
            for (int s = 0; s < mod::MAX_OWNED_VEHICLES; ++s) {
                const auto& sl = g_slots[s];
                if (!sl.occupied || sl.destroyed) continue;
                int sc = ScoreSafehouseMatch(v, sl.modelId, sl.visuals);
                if (sc > bestSlSc) {
                    bestSlSc = sc;
                    bestSlot = s;
                }
            }
            if (bestSlot >= 0) {
                const auto& sl = g_slots[bestSlot];
                RestoreTuningOntoSafehouseVehicle(v, sl.visuals, sl.tuning, sl.wear, bestSlot);
                UpsertSafehouseRecord(gIdx, v, bestSlot, sl.tuning, sl.wear);
            }
        }
    }

    if (g_safehouseDirtySave) {
        g_safehouseDirtySave = false;
        SaveNow();
    }
}

} // namespace

int GetActiveSaveSlot() { return g_activeSaveSlot; }

void SaveNow() {
    storage::SaveGarageToSlot(g_activeSaveSlot, g_slots, &g_safehouseCars);
}

void OnGameSave(int slot1to8) {
    if (slot1to8 >= 1 && slot1to8 <= 8) {
        g_activeSaveSlot = slot1to8;
    }
    // Refresh live personal vehicle snapshot before saving
    if (g_activeSlot >= 0 && g_activeSlot < mod::MAX_OWNED_VEHICLES &&
        sa::IsVehicleValid(g_activeVeh) &&
        sa::GetVehicleHandle(g_activeVeh) == g_activeVehHandle) {
        SnapshotVehicleToSlot(g_activeVeh, g_activeSlot);
    }
    SaveNow();
}

void InstallSafehouseHooks() {
    if (g_safehouseHooksInstalled) return;
    // Guard all 3 hideout garage vehicle-inside checks against GTA SA's x87 NaN comparison bug:
    //   0x0044998C (StoreAndRemoveCarsForThisHideout #1)
    //   0x00449ADC (StoreAndRemoveCarsForThisHideout #2)
    //   0x0156146F (CountCarsWithCenterPointWithinGarage)
    g_origIsPointInside1 = reinterpret_cast<FnIsPointInside>(
        mem::HookCall(0x0044998C, reinterpret_cast<uintptr_t>(&HookedGarageIsPointInside1)));
    g_origIsPointInside2 = reinterpret_cast<FnIsPointInside>(
        mem::HookCall(0x00449ADC, reinterpret_cast<uintptr_t>(&HookedGarageIsPointInside2)));
    g_origIsPointInside3 = reinterpret_cast<FnIsPointInside>(
        mem::HookCall(0x0156146F, reinterpret_cast<uintptr_t>(&HookedGarageIsPointInside3)));
    g_safehouseHooksInstalled = true;
    modlog::Info("[Garage] Installed safehouse NaN-ghost guard hooks (0x0044998C, 0x00449ADC, 0x0156146F).");
}

void RemovePersonalSlotFromSafehouses(int slotIdx, bool alsoClearStoredCar) {
    if (slotIdx < 0 || slotIdx >= mod::MAX_OWNED_VEHICLES) return;
    const auto& sl = g_slots[slotIdx];
    bool dirty = false;

    for (int i = 0; i < mod::MAX_SAFEHOUSE_CARS; ++i) {
        auto& r = g_safehouseCars[i];
        if (!r.occupied || r.personalSlotIdx != slotIdx) continue;

        if (alsoClearStoredCar) {
            int hid = sa::GetHideoutIndexForGarageIndex(r.hideoutGarageId);
            sa::CStoredCar* arr = sa::GetSafehouseStoredCars(hid);
            if (arr) {
                for (int s = 0; s < sa::MAX_CARS_PER_SAFEHOUSE; ++s) {
                    auto& sc = arr[s];
                    if (sc.m_nModelIndex == 0) continue;
                    bool modelMatch = (static_cast<int32_t>(sc.m_nModelIndex) == r.modelId ||
                                       (sl.occupied && static_cast<int32_t>(sc.m_nModelIndex) == sl.modelId));
                    bool colorMatch = (sc.m_nPrimaryColor == r.visuals.primaryColor &&
                                       sc.m_nSecondaryColor == r.visuals.secondaryColor);
                    if (modelMatch && colorMatch) {
                        sc.m_nModelIndex = 0;
                        modlog::Info("[Garage] Cleared CStoredCar (model=%d) from hideout #%d slot #%d for personal slot %d.",
                                     r.modelId, hid, s, slotIdx);
                        break;
                    }
                }
            }
        }

        r = mod::SafehouseCarRecord{};
        dirty = true;
    }

    if (dirty) g_safehouseDirtySave = true;
}

void OnGameLoad(int slot1to8) {
    ClearDeliveryBlip();
    g_activeVeh = nullptr;
    g_activeVehHandle = -1;
    g_activeSlot = -1;
    tuning::ResetAll();

    g_activeSaveSlot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
    storage::LoadGarageFromSlot(g_activeSaveSlot, g_slots, &g_safehouseCars);
    g_safehouseDirtySave = false;
    // Immediately purge any NaN ghost cars loaded from an existing GTASAsfN.b save file!
    SanitizeSafehouseStoredCars();
}

void OnNewGame() {
    ClearDeliveryBlip();
    g_activeVeh = nullptr;
    g_activeVehHandle = -1;
    g_activeSlot = -1;
    tuning::ResetAll();

    g_activeSaveSlot = 0;
    for (auto& s : g_slots) s = mod::OwnedVehicle{};
    for (auto& r : g_safehouseCars) r = mod::SafehouseCarRecord{};
    g_safehouseDirtySave = false;
    modlog::Info("[Garage] New game started -> cleared runtime garage (slot 0).");
}

const std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& Slots() { return g_slots; }
std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES>& SlotsMutable() { return g_slots; }

int FindFreeSlot() {
    int limit = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);
    for (int i = 0; i < limit; ++i) {
        if (!g_slots[i].occupied) return i;
    }
    return -1;
}

int FindSlotForLiveVehicle(const sa::CVehicle* veh) {
    if (!veh || !sa::IsVehicleValid(veh)) return -1;
    if (veh == g_activeVeh && g_activeSlot >= 0 && g_activeSlot < mod::MAX_OWNED_VEHICLES &&
        g_slots[g_activeSlot].occupied && sa::GetVehicleHandle(veh) == g_activeVehHandle) {
        return g_activeSlot;
    }
    return -1;
}

int CountOccupied() {
    int n = 0;
    int limit = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);
    for (int i = 0; i < limit; ++i) {
        if (g_slots[i].occupied) ++n;
    }
    return n;
}

void SnapshotVehicleToSlot(sa::CVehicle* veh, int slotIdx) {
    if (!sa::IsVehicleValid(veh) || slotIdx < 0 || slotIdx >= mod::MAX_OWNED_VEHICLES) return;

    auto& dst = g_slots[slotIdx];
    dst.occupied        = true;
    dst.destroyed       = false;
    dst.modelId         = veh->GetModelIndex();
    dst.displayNameGame = sa::GetVehicleDisplayNameGameEncoded(dst.modelId);
    dst.visuals         = CaptureVisuals(veh);

    // GTA 5 performance & visual tuning + ETS 2 wear state
    if (const auto* live = tuning::GetLiveTuning(veh)) {
        dst.tuning = *live;
        dst.wear   = tuning::GetLiveWear(veh);
    } else {
        dst.tuning = mod::PerformanceTuning{};
        dst.wear   = mod::VehicleWear{};
    }
}

int RegisterVehicle(sa::CVehicle* veh) {
    if (!sa::IsVehicleValid(veh)) return -1;

    int existing = FindSlotForLiveVehicle(veh);
    if (existing >= 0) {
        SnapshotVehicleToSlot(veh, existing);
        SaveNow();
        return existing;
    }

    int freeSlot = FindFreeSlot();
    if (freeSlot < 0) return -1;

    SnapshotVehicleToSlot(veh, freeSlot);
    // Ensure live ETS 2 wear tracking begins immediately even on stock-tuned personal vehicles!
    tuning::ApplyTuning(veh, g_slots[freeSlot].tuning, &g_slots[freeSlot].wear);

    // Make this vehicle the active tracked personal vehicle
    ClearDeliveryBlip();
    g_activeVeh       = veh;
    g_activeVehHandle = sa::GetVehicleHandle(veh);
    g_activeSlot      = freeSlot;

    SaveNow();
    modlog::Info("[Garage] Registered model %d into slot %d (save slot %d)", g_slots[freeSlot].modelId, freeSlot, g_activeSaveSlot);
    return freeSlot;
}

bool UnregisterSlot(int slotIdx) {
    if (slotIdx < 0 || slotIdx >= mod::MAX_OWNED_VEHICLES) return false;
    if (!g_slots[slotIdx].occupied) return false;

    if (g_activeSlot == slotIdx) {
        ClearDeliveryBlip();
        g_activeVeh       = nullptr;
        g_activeVehHandle = -1;
        g_activeSlot      = -1;
    }

    RemovePersonalSlotFromSafehouses(slotIdx, true);
    g_slots[slotIdx] = mod::OwnedVehicle{};
    SaveNow();
    modlog::Info("[Garage] Unregistered slot %d", slotIdx);
    return true;
}

void ApplySavedVehicleConfig(sa::CVehicle* veh, const mod::OwnedVehicle& data) {
    if (!sa::IsVehicleValid(veh)) return;
    int32_t handle = sa::GetVehicleHandle(veh);
    if (handle <= 0) return;

    // 1. Colors
    sa::ScriptCmd(0x0229)
        .Int(handle)
        .Int(data.visuals.primaryColor)
        .Int(data.visuals.secondaryColor)
        .Run();

    // 2. Paintjob
    if (data.visuals.paintjob >= 0 && data.visuals.paintjob <= 2) {
        sa::ScriptCmd(0x06ED).Int(handle).Int(data.visuals.paintjob).Run();
    }

    // 3. Ensure universal mount points exist for cars like Bullet/Infernus/Tahoma before attaching saved mods!
    sa::EnsureUniversalUpgradeMounts(veh);

    // All 15 vanilla upgrades (request -> load -> install -> release; deduplicate legacy Nitro slots cleanly!)
    int16_t savedNitro = -1;
    bool requestedAny = false;
    for (int i = 0; i < 15; ++i) {
        int16_t modId = data.visuals.upgrades[i];
        if (modId == 1008 || modId == 1009 || modId == 1010) {
            savedNitro = modId;
            continue;
        }
        if (modId >= 1000 && modId <= 1193) {
            sa::ScriptCmd(0x06E9).Int(modId).Run(); // REQUEST_VEHICLE_MOD
            requestedAny = true;
        }
    }
    if (savedNitro >= 1000) {
        sa::ScriptCmd(0x06E9).Int(savedNitro).Run();
        requestedAny = true;
    }
    if (requestedAny) {
        sa::ScriptCmd(0x038B).Run(); // LOAD_ALL_MODELS_NOW
        for (int i = 0; i < 15; ++i) {
            int16_t modId = data.visuals.upgrades[i];
            if (modId == 1008 || modId == 1009 || modId == 1010) continue;
            if (modId >= 1000 && modId <= 1193) {
                int32_t dummyOut = 0;
                sa::ScriptCmd(0x06E7).Int(handle).Int(modId).Out(dummyOut).Run(); // ADD_VEHICLE_MOD
                sa::ScriptCmd(0x06EB).Int(modId).Run(); // MARK_VEHICLE_MOD_AS_NO_LONGER_NEEDED
            }
        }
        if (savedNitro >= 1000) {
            int32_t dummyOut = 0;
            sa::ScriptCmd(0x06E7).Int(handle).Int(savedNitro).Out(dummyOut).Run();
            sa::ScriptCmd(0x06EB).Int(savedNitro).Run();
        }
    }

    // 4. Clean dirt & repair cleanly
    sa::ScriptCmd(0x0A30).Int(handle).Run(); // FIX_CAR
    sa::ScriptCmd(0x0878).Int(handle).Float(0.0f).Run(); // SET_VEHICLE_DIRT_LEVEL

    // 5. Apply custom license plate texture immediately if set
    if (data.tuning.plateText[0] != '\0') {
        sa::ApplyLiveCustomPlate(veh, data.tuning.plateText);
    }

    // 6. Apply all GTA 5 performance tuning + persisted mechanical wear!
    tuning::ApplyTuning(veh, data.tuning, &data.wear);
}

sa::CVehicle* GetActivePersonalVehicle() {
    if (!g_activeVeh) return nullptr;
    if (!sa::IsVehicleValid(g_activeVeh) || sa::GetVehicleHandle(g_activeVeh) != g_activeVehHandle) {
        return nullptr;
    }
    return g_activeVeh;
}

int GetActivePersonalSlot() { return g_activeSlot; }
int32_t GetDeliveryBlip() { return g_deliveryBlip; }

void SetActivePersonalVehicle(sa::CVehicle* veh, int slotIdx, int32_t blipHandle) {
    ClearDeliveryBlip();
    g_activeVeh       = veh;
    g_activeVehHandle = sa::IsVehicleValid(veh) ? sa::GetVehicleHandle(veh) : -1;
    g_activeSlot      = slotIdx;
    g_deliveryBlip    = blipHandle;
}

void ClearDeliveryBlip() {
    if (g_deliveryBlip > 0) {
        sa::ScriptCmd(0x0164).Int(g_deliveryBlip).Run(); // REMOVE_BLIP
        g_deliveryBlip = 0;
    }
}

void DismissCurrentPersonalVehicle(bool deleteEntity) {
    ClearDeliveryBlip();
    if (sa::CVehicle* v = GetActivePersonalVehicle()) {
        // Save any latest tuning/colors before dismissing
        if (g_activeSlot >= 0 && g_activeSlot < mod::MAX_OWNED_VEHICLES &&
            g_slots[g_activeSlot].occupied && !g_slots[g_activeSlot].destroyed) {
            SnapshotVehicleToSlot(v, g_activeSlot);
            SaveNow();
        }
        int32_t h = g_activeVehHandle;
        bool safeToDelete = integrations::sacoop::IsSafeToDeleteVehicle(v, sa::FindPlayerPed(0));
        tuning::DetachVehicle(v);
        if (deleteEntity && safeToDelete && h > 0) {
            sa::ScriptCmd(0x00A6).Int(h).Run(); // DELETE_CAR
        } else if (h > 0) {
            sa::ScriptCmd(0x01C3).Int(h).Run(); // MARK_CAR_AS_NO_LONGER_NEEDED
        }
    }
    g_activeVeh       = nullptr;
    g_activeVehHandle = -1;
    g_activeSlot      = -1;
}

void Tick() {
    // Always track & restore tuned/personal vehicles stored in vanilla safehouse garages (even after door closes!)
    TickSafehousePersistence();

    if (g_activeSlot < 0 || g_activeSlot >= mod::MAX_OWNED_VEHICLES) return;
    if (!g_activeVeh) return;

    auto& slot = g_slots[g_activeSlot];
    if (!slot.occupied) {
        ClearDeliveryBlip();
        g_activeVeh = nullptr;
        g_activeSlot = -1;
        return;
    }

    // Check if vehicle handle vanished (despawned/removed by mission — NOT destroyed!)
    if (!sa::IsVehicleValid(g_activeVeh) || sa::GetVehicleHandle(g_activeVeh) != g_activeVehHandle) {
        ClearDeliveryBlip();
        g_activeVeh = nullptr;
        g_activeVehHandle = -1;
        g_activeSlot = -1;
        return;
    }

    // Check destruction (exploded, burnt out, or drowned in water!)
    bool dead = sa::ScriptCmd(0x0119).Int(g_activeVehHandle).Run(); // IS_CAR_DEAD
    bool inWater = sa::ScriptCmd(0x02BF).Int(g_activeVehHandle).Run(); // IS_CAR_IN_WATER
    float hp = g_activeVeh->Health();

    if (dead || hp <= 0.0f || (inWater && hp < 260.0f)) {
        if (!slot.destroyed) {
            slot.destroyed = true;
            // Sync latest pre-explosion wear AND apply heavy explosion wear penalty!
            slot.wear = tuning::GetLiveWear(g_activeVeh);
            if (mod::Config().enableWearSystem) {
                slot.wear.ApplyExplosionPenalty();
            }
            ClearDeliveryBlip();
            RemovePersonalSlotFromSafehouses(g_activeSlot, true);
            SaveNow();
            auto* bus = modbus::GetBus();
            int keyVk = (bus && bus->phoneInstalled) ? modbus::GetPhoneKey() : mod::Config().callMenuKey;
            std::string keyLbl = common::GetVirtualKeyLabel(keyVk);
            char rmsg[160], emsg[160];
            std::snprintf(rmsg, sizeof(rmsg),
                          "~r~Ваш личный транспорт уничтожен.~w~ Восстановите его через ~y~%s~w~.",
                          keyLbl.c_str());
            std::snprintf(emsg, sizeof(emsg),
                          "~r~Your personal vehicle was destroyed.~w~ Restore it via ~y~%s~w~.",
                          keyLbl.c_str());
            ui::ShowHelp(rmsg, emsg, true);
            modlog::Info("[Garage] Slot %d vehicle destroyed! Avg wear now %.1f%%.", g_activeSlot, slot.wear.Average());
        }
        tuning::DetachVehicle(g_activeVeh);
        g_activeVeh = nullptr;
        g_activeVehHandle = -1;
        g_activeSlot = -1;
        return;
    }

    // Periodically sync live mechanical wear back into the personal garage slot!
    static uint32_t s_lastWearSyncMs = 0;
    uint32_t nowMs = sa::GetTimeMs();
    if (mod::Config().enableWearSystem && (nowMs - s_lastWearSyncMs >= 2000)) {
        s_lastWearSyncMs = nowMs;
        slot.wear = tuning::GetLiveWear(g_activeVeh);
    }

    // Clear delivery radar blip once the local player enters the vehicle!
    if (g_deliveryBlip > 0) {
        sa::CVehicle* playerVeh = sa::FindPlayerVehicle(0, false);
        if (playerVeh == g_activeVeh) {
            ClearDeliveryBlip();
        }
    }

    // Outside safehouse garages, keep our active personal vehicle marked as MISSION_VEHICLE (2)
    // so GTA SA never despawns it when parked outside shops/interiors, and clear any stale safehouse record
    // once the player drives the car out of the safehouse garage!
    if (!workshop::IsWorkshopActive() && !delivery::IsDeliveryInProgress() && sa::GetActiveInterior() == 0) {
        sa::CPed* drv = g_activeVeh->GetDriver();
        if (drv == sa::FindPlayerPed(0) &&
            sa::FindHideoutGarageIndexAt(g_activeVeh->GetPosition()) < 0) {
            if (g_activeVeh->CreatedBy() != 2) {
                g_activeVeh->CreatedBy() = 2;
            }
            RemovePersonalSlotFromSafehouses(g_activeSlot, true);
        }
    }

    // Protect personal vehicle paint color from Pay 'n' Spray resprays (only while local player is driving!)
    if (!workshop::IsWorkshopActive() && g_activeVehHandle > 0 &&
        g_activeVeh->GetDriver() == sa::FindPlayerPed(0)) {
        if (g_activeVeh->PrimaryColor() != slot.visuals.primaryColor ||
            g_activeVeh->SecondaryColor() != slot.visuals.secondaryColor) {
            sa::ScriptCmd(0x0229)
                .Int(g_activeVehHandle)
                .Int(slot.visuals.primaryColor)
                .Int(slot.visuals.secondaryColor)
                .Run();
            if (slot.visuals.paintjob >= 0 && slot.visuals.paintjob <= 2) {
                sa::ScriptCmd(0x06ED).Int(g_activeVehHandle).Int(slot.visuals.paintjob).Run();
            }
        }
    }
}

} // namespace garage
