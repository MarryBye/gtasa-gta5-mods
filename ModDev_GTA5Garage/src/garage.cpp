#include "garage.hpp"
#include <cstring>
#include "config.hpp"
#include "log.hpp"
#include "storage.hpp"
#include "tuning_physics.hpp"
#include "ui_menu.hpp"
#include "workshop.hpp"

namespace garage {

namespace {

int g_activeSaveSlot = 0; // 1..8 or 0 (autosave / SA-COOP client)
std::array<mod::OwnedVehicle, mod::MAX_OWNED_VEHICLES> g_slots{};

sa::CVehicle* g_activeVeh        = nullptr;
int32_t       g_activeVehHandle  = -1;
int           g_activeSlot       = -1;
int32_t       g_deliveryBlip     = 0;

} // namespace

int GetActiveSaveSlot() { return g_activeSaveSlot; }

void SaveNow() {
    storage::SaveGarageToSlot(g_activeSaveSlot, g_slots);
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

void OnGameLoad(int slot1to8) {
    ClearDeliveryBlip();
    g_activeVeh = nullptr;
    g_activeVehHandle = -1;
    g_activeSlot = -1;
    tuning::ResetAll();

    g_activeSaveSlot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
    storage::LoadGarageFromSlot(g_activeSaveSlot, g_slots);
}

void OnNewGame() {
    ClearDeliveryBlip();
    g_activeVeh = nullptr;
    g_activeVehHandle = -1;
    g_activeSlot = -1;
    tuning::ResetAll();

    g_activeSaveSlot = 0;
    for (auto& s : g_slots) s = mod::OwnedVehicle{};
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

    // Colors
    int32_t handle = sa::GetVehicleHandle(veh);
    if (handle > 0) {
        int32_t c1 = veh->PrimaryColor();
        int32_t c2 = veh->SecondaryColor();
        sa::ScriptCmd(0x03F3).Int(handle).Out(c1).Out(c2).Run();
        dst.visuals.primaryColor   = static_cast<uint8_t>(c1);
        dst.visuals.secondaryColor = static_cast<uint8_t>(c2);

        // Paintjob
        int32_t pj = -1;
        sa::ScriptCmd(0x0988).Int(handle).Out(pj).Run();
        dst.visuals.paintjob = (pj >= 0 && pj <= 3) ? static_cast<int8_t>(pj) : -1;
    } else {
        dst.visuals.primaryColor   = veh->PrimaryColor();
        dst.visuals.secondaryColor = veh->SecondaryColor();
        dst.visuals.paintjob       = -1;
    }

    // All 15 vanilla upgrade slots (deduplicate Nitro 1008/1009/1010 so only one clean Nitro is stored!)
    const int16_t* upgs = veh->Upgrades();
    int16_t lastNitro = -1;
    for (int i = 0; i < 15; ++i) {
        if (upgs[i] == 1008 || upgs[i] == 1009 || upgs[i] == 1010) lastNitro = upgs[i];
    }
    int writeIdx = 0;
    for (int i = 0; i < 15; ++i) dst.visuals.upgrades[i] = -1;
    bool wroteNitro = false;
    for (int i = 0; i < 15 && writeIdx < 15; ++i) {
        int16_t u = upgs[i];
        if (u < 1000) continue;
        if (u == 1008 || u == 1009 || u == 1010) {
            if (!wroteNitro && lastNitro >= 1000) {
                dst.visuals.upgrades[writeIdx++] = lastNitro;
                wroteNitro = true;
            }
            continue;
        }
        dst.visuals.upgrades[writeIdx++] = u;
    }

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
        // SA-COOP safety: never DELETE_CAR out from under a co-op teammate driving our personal vehicle!
        sa::CPed* driver = v->GetDriver();
        bool occupiedByOther = (driver != nullptr && driver != sa::FindPlayerPed(0));
        tuning::DetachVehicle(v);
        if (deleteEntity && !occupiedByOther && h > 0) {
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
            SaveNow();
            ui::ShowHelp(
                "~r~Ваш личный транспорт уничтожен!~w~ Вы можете восстановить его в меню ~y~F6~w~ (износ сильно вырос!).",
                "~r~Your personal vehicle was destroyed!~w~ Restore it via ~y~F6~w~ (heavy mechanical wear incurred!).",
                true);
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

    // Protect personal vehicle paint color from Pay 'n' Spray resprays!
    // (Pay 'n' Spray still repairs the car & clears wanted level, while keeping custom paint untouched!)
    if (!workshop::IsWorkshopActive() && g_activeVehHandle > 0) {
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
