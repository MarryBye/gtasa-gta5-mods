#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "config.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "weapon_data.hpp"

namespace wstore {

class ArsenalStore {
public:
    void SetSlotAndLoad(int slot) {
        m_slot = slot;
        LoadFromDisk();
    }

    // Starts a completely fresh empty arsenal (used on Singleplayer New Game so old slot-0 gear NEVER leaks!)
    void StartFreshNewGame() {
        m_slot = 0;
        m_loadouts.clear();
        for (int c = 0; c < wdata::kWheelCategoryCount; ++c) m_preferredWeapon[c] = 0;
        for (int s = 0; s < 13; ++s) m_lastLiveSlotType[s] = 0;
        modlog::Info("[WStore] Started fresh New Game -> cleared weapon arsenal.");
    }

    // Zeroes stored ammo for ALL weapons (e.g. Heavy/Thrown like RPG & Grenades) when CJ dies/gets busted
    // and GTA SA strips his inventory, while keeping L.owned = true so purchased weapons stay in the wheel!
    void ClearAllStoredAmmo() {
        for (auto& kv : m_loadouts) {
            kv.second.storedAmmo = 0;
        }
        for (int s = 0; s < 13; ++s) m_lastLiveSlotType[s] = 0;
    }

    void SaveToSlot(int slot) {
        if (slot >= 0 && slot <= 8) m_slot = slot;
        SaveToDisk();
    }

    int GetSlot() const { return m_slot; }

    wdata::WeaponLoadout& Get(uint32_t weaponId) {
        return m_loadouts[weaponId];
    }

    const wdata::WeaponLoadout* Find(uint32_t weaponId) const {
        auto it = m_loadouts.find(weaponId);
        return (it != m_loadouts.end()) ? &it->second : nullptr;
    }

    uint32_t GetPreferredWeapon(wdata::WeaponCategory cat) const {
        int idx = static_cast<int>(cat);
        if (idx >= 0 && idx < wdata::kWheelCategoryCount) return m_preferredWeapon[idx];
        return 0;
    }

    void SetPreferredWeapon(wdata::WeaponCategory cat, uint32_t weaponId) {
        int idx = static_cast<int>(cat);
        if (idx >= 0 && idx < wdata::kWheelCategoryCount) m_preferredWeapon[idx] = weaponId;
    }

    void SaveToDisk() const {
        std::string path = BuildFilePath(m_slot);
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) {
            modlog::Info("[WStore] Failed to write %s", path.c_str());
            return;
        }
        std::fprintf(f, "; GTA5AmmuNation Weapon Loadouts & Multi-Weapon Arsenal (Slot %d)\n", m_slot);
        std::fprintf(f, "[WheelPreferred]\n");
        for (int c = 0; c < wdata::kWheelCategoryCount; ++c) {
            std::fprintf(f, "Cat_%d=%u\n", c, m_preferredWeapon[c]);
        }
        std::fprintf(f, "\n");

        for (size_t i = 0; i < wdata::kWeaponCount; ++i) {
            uint32_t id = wdata::kWeapons[i].id;
            if (id == 0) continue;
            auto it = m_loadouts.find(id);
            if (it == m_loadouts.end()) continue;
            const auto& L = it->second;
            if (!L.owned && !L.HasAnyMod() && L.storedAmmo <= 0) continue;
            std::fprintf(f, "[Weapon_%u]\n", id);
            std::fprintf(f, "Owned=%d\n", L.owned ? 1 : 0);
            std::fprintf(f, "Muzzle=%u\n", L.muzzle);
            std::fprintf(f, "Mag=%u\n", L.mag);
            std::fprintf(f, "Scope=%u\n", L.scope);
            std::fprintf(f, "Grip=%u\n", L.grip);
            std::fprintf(f, "Barrel=%u\n", L.barrel);
            std::fprintf(f, "Laser=%u\n", L.laser);
            std::fprintf(f, "OwnedMuzzle=%u\n", L.ownedMuzzleMask);
            std::fprintf(f, "OwnedMag=%u\n", L.ownedMagMask);
            std::fprintf(f, "OwnedScope=%u\n", L.ownedScopeMask);
            std::fprintf(f, "OwnedGrip=%u\n", L.ownedGripMask);
            std::fprintf(f, "OwnedBarrel=%u\n", L.ownedBarrelMask);
            std::fprintf(f, "OwnedLaser=%u\n", L.ownedLaserMask);
            std::fprintf(f, "StoredAmmo=%d\n\n", L.storedAmmo);
        }
        std::fclose(f);
        modlog::Info("[WStore] Saved weapon arsenal to %s", path.c_str());
    }

    void LoadFromDisk() {
        m_loadouts.clear();
        for (int c = 0; c < wdata::kWheelCategoryCount; ++c) m_preferredWeapon[c] = 0;
        for (int s = 0; s < 13; ++s) m_lastLiveSlotType[s] = 0;

        std::string path = BuildFilePath(m_slot, true);
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            modlog::Info("[WStore] No saved weapons yet at %s (fresh slot %d).", path.c_str(), m_slot);
            return;
        }
        for (int c = 0; c < wdata::kWheelCategoryCount; ++c) {
            char key[24];
            std::snprintf(key, sizeof(key), "Cat_%d", c);
            m_preferredWeapon[c] = static_cast<uint32_t>(GetPrivateProfileIntA("WheelPreferred", key, 0, path.c_str()));
        }

        int loaded = 0;
        for (size_t i = 0; i < wdata::kWeaponCount; ++i) {
            uint32_t id = wdata::kWeapons[i].id;
            if (id == 0) continue;
            char sec[32];
            std::snprintf(sec, sizeof(sec), "Weapon_%u", id);
            int owned = GetPrivateProfileIntA(sec, "Owned", -1, path.c_str());
            if (owned < 0) continue;

            wdata::WeaponLoadout L;
            L.owned           = (owned != 0);
            L.muzzle          = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Muzzle", 0, path.c_str()));
            L.mag             = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Mag",    0, path.c_str()));
            L.scope           = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Scope",  0, path.c_str()));
            L.grip            = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Grip",   0, path.c_str()));
            L.barrel          = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Barrel", 0, path.c_str()));
            L.laser           = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "Laser",  0, path.c_str()));
            L.ownedMuzzleMask = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedMuzzle", 1, path.c_str()) | 1 | (1u << L.muzzle));
            L.ownedMagMask    = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedMag",    1, path.c_str()) | 1 | (1u << L.mag));
            L.ownedScopeMask  = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedScope",  1, path.c_str()) | 1 | (1u << L.scope));
            L.ownedGripMask   = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedGrip",   1, path.c_str()) | 1 | (1u << L.grip));
            L.ownedBarrelMask = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedBarrel", 1, path.c_str()) | 1 | (1u << L.barrel));
            L.ownedLaserMask  = static_cast<uint8_t>(GetPrivateProfileIntA(sec, "OwnedLaser",  1, path.c_str()) | 1 | (1u << L.laser));
            L.storedAmmo      = GetPrivateProfileIntA(sec, "StoredAmmo", 0, path.c_str());
            m_loadouts[id] = L;
            ++loaded;
        }
        modlog::Info("[WStore] Loaded %d weapons from %s", loaded, path.c_str());
    }

    // Continuously tracks any weapons CJ picks up or holds in vanilla slots!
    void SyncFromPlayerInventory(const sa::CPed* ped) {
        if (!sa::IsPedValid(ped)) return;
        const sa::CWeapon* w = ped->Weapons();
        for (int s = 0; s < 13; ++s) {
            uint32_t wid = w[s].m_nType;
            uint32_t prevWid = m_lastLiveSlotType[s];

            // If CJ just fired the last rocket/grenade in an individual-ammo slot (Heavy/Thrown),
            // GTA SA clears w[s].m_nType to 0 -> zero that weapon's storedAmmo immediately!
            if (wid == 0 && prevWid != 0) {
                const auto* prevDef = wdata::FindDef(prevWid);
                if (prevDef && !prevDef->sharesSlotAmmo && prevDef->category != wdata::WeaponCategory::Melee) {
                    auto it = m_loadouts.find(prevWid);
                    if (it != m_loadouts.end()) it->second.storedAmmo = 0;
                }
            }
            m_lastLiveSlotType[s] = wid;

            if (wid == 0) continue;
            const auto* def = wdata::FindDef(wid);
            if (!def) continue;

            bool hasIt = (def->category == wdata::WeaponCategory::Melee) || (w[s].m_nTotalAmmo > 0);
            if (hasIt) {
                auto& L = m_loadouts[wid];
                L.owned = true;
                if (!def->sharesSlotAmmo && def->category != wdata::WeaponCategory::Melee) {
                    L.storedAmmo = static_cast<int>(w[s].m_nTotalAmmo);
                }
            } else if (!def->sharesSlotAmmo && def->category != wdata::WeaponCategory::Melee) {
                auto it = m_loadouts.find(wid);
                if (it != m_loadouts.end()) {
                    it->second.storedAmmo = 0;
                }
            }
        }
    }

    bool PlayerHasWeaponOrOwned(const sa::CPed* ped, uint32_t weaponId) const {
        if (weaponId == 0) return true; // Fists always owned!
        if (sa::IsPedValid(ped)) {
            const sa::CWeapon* w = ped->Weapons();
            for (int s = 0; s < 13; ++s) {
                if (w[s].m_nType == weaponId) {
                    const auto* def = wdata::FindDef(weaponId);
                    if ((def && def->category == wdata::WeaponCategory::Melee) || w[s].m_nTotalAmmo > 0) return true;
                }
            }
        }
        auto it = m_loadouts.find(weaponId);
        return (it != m_loadouts.end()) && it->second.owned;
    }

    // Returns effective ammo for a weapon (SHARED category ammo for slots 2..6, individual stored/live ammo for Heavy/Thrown!)
    int GetWeaponAmmo(const sa::CPed* ped, const wdata::WeaponDef* def) const {
        if (!def || def->category == wdata::WeaponCategory::Melee) return 0;
        if (!sa::IsPedValid(ped)) return 0;

        const sa::CWeapon& slotW = ped->Weapons()[def->gtaSlot];
        if (def->sharesSlotAmmo) {
            // Shared across ALL weapons of this category (e.g. M4 + AK-47 share slot 5 ammo!)
            if (slotW.m_nType != 0) {
                return static_cast<int>(slotW.m_nTotalAmmo);
            }
            return 0;
        }
        // Individual weapon ammo (Heavy / Thrown)
        if (slotW.m_nType == def->id) {
            return static_cast<int>(slotW.m_nTotalAmmo);
        }
        auto it = m_loadouts.find(def->id);
        return (it != m_loadouts.end()) ? std::max(0, it->second.storedAmmo) : 0;
    }

    // Returns all owned weapons in a wheel category (Unarmed always first in Melee)
    std::vector<const wdata::WeaponDef*> GetOwnedInCategory(const sa::CPed* ped, wdata::WeaponCategory cat) const {
        std::vector<const wdata::WeaponDef*> list;
        for (size_t i = 0; i < wdata::kWeaponCount; ++i) {
            const auto& d = wdata::kWeapons[i];
            if (d.category != cat) continue;
            if (PlayerHasWeaponOrOwned(ped, d.id)) {
                list.push_back(&d);
            }
        }
        return list;
    }

    // Swaps or gives weapon in CJ's hands while PRESERVING shared category ammo (slots 2..6)
    // OR saving/restoring individual Heavy/Thrown ammo!
    void EquipWeaponPreservingAmmo(sa::CPed* ped, const wdata::WeaponDef* def, int extraAmmo = 0, bool setCurrent = true) {
        if (!sa::IsPedValid(ped) || !def) return;
        int32_t pedH = sa::GetPedHandle(ped);
        if (pedH <= 0) return;

        // Unarmed (Fists): just switch active weapon to 0 without overwriting knuckles/melee!
        if (def->id == 0) {
            if (setCurrent) {
                sa::ScriptCmd(0x01B9).Int(pedH).Int(0).Run();
            }
            SetPreferredWeapon(wdata::WeaponCategory::Melee, 0);
            return;
        }

        // Snapshot current occupant of def->gtaSlot BEFORE replacing it!
        sa::CWeapon& slotW = ped->Weapons()[def->gtaSlot];
        uint32_t prevId   = slotW.m_nType;
        int      prevAmmo = static_cast<int>(slotW.m_nTotalAmmo);

        if (prevId != 0 && prevId != def->id) {
            const auto* prevDef = wdata::FindDef(prevId);
            if (prevDef) {
                m_loadouts[prevId].owned = true;
                if (!prevDef->sharesSlotAmmo && prevDef->category != wdata::WeaponCategory::Melee) {
                    m_loadouts[prevId].storedAmmo = prevAmmo;
                }
            }
        }

        // Compute target total ammo for def->id
        int targetAmmo = 1;
        if (def->category == wdata::WeaponCategory::Melee) {
            targetAmmo = 1;
        } else if (def->sharesSlotAmmo) {
            // SHARED category ammo: keep current slot ammo + add any newly purchased ammo!
            int baseAmmo = (prevId != 0) ? prevAmmo : 0;
            targetAmmo = std::clamp(baseAmmo + extraAmmo, 0, def->maxAmmo);
            if (targetAmmo == 0 && extraAmmo == 0) {
                // Keep 1 reserve or 0 if genuinely out of ammo (GTA SA removes gun at 0 ammo -> keep at 1 if swapping? No, respect 0 if truly empty!)
                // Wait: if player owns the gun and has 0 ammo, giving 0 won't spawn it in GTA SA slot.
                // Let's keep at least 0 if truly empty, otherwise exact shared ammo!
            }
        } else {
            // Non-shared (Heavy / Thrown): restore this weapon's own stored ammo + extraAmmo
            int baseAmmo = (prevId == def->id) ? prevAmmo : m_loadouts[def->id].storedAmmo;
            targetAmmo = std::clamp(baseAmmo + extraAmmo, 0, def->maxAmmo);
            m_loadouts[def->id].storedAmmo = targetAmmo;
        }

        // Stream weapon model cleanly before giving it
        if (def->modelId > 0) {
            sa::ScriptCmd(0x0247).Int(def->modelId).Run(); // REQUEST_MODEL
            sa::ScriptCmd(0x038B).Run();                   // LOAD_ALL_MODELS_NOW
        }

        if (prevId != def->id) {
            // Give the new weapon model in this slot, then force exact preserved ammo!
            int giveAmt = std::max(1, targetAmmo);
            sa::ScriptCmd(0x01B2).Int(pedH).Int(static_cast<int32_t>(def->id)).Int(giveAmt).Run();
            if (def->category != wdata::WeaponCategory::Melee) {
                sa::ScriptCmd(0x017B).Int(pedH).Int(static_cast<int32_t>(def->id)).Int(targetAmmo).Run();
            }
        } else if (extraAmmo > 0) {
            sa::ScriptCmd(0x017B).Int(pedH).Int(static_cast<int32_t>(def->id)).Int(targetAmmo).Run();
        }

        if (setCurrent) {
            sa::ScriptCmd(0x01B9).Int(pedH).Int(static_cast<int32_t>(def->id)).Run();
        }

        if (def->modelId > 0) {
            sa::ScriptCmd(0x0249).Int(def->modelId).Run(); // MARK_MODEL_AS_NO_LONGER_NEEDED
        }

        SetPreferredWeapon(def->category, def->id);
    }

private:
    static std::string BuildFilePath(int slot, bool forLoading = false) {
        const char* ov = nullptr;
        if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) ov = b->coopProfileName;
        if (forLoading) {
            return sa::ResolveExistingLoadPath("GTA5WeaponSaves", wcfg::Get().modDir, "weapons", slot, ov);
        }
        return sa::BuildSaveProfilePath("GTA5WeaponSaves", wcfg::Get().modDir, "weapons", slot, ov);
    }

    int      m_slot = 0;
    uint32_t m_preferredWeapon[wdata::kWheelCategoryCount] = {};
    uint32_t m_lastLiveSlotType[13] = {};
    std::unordered_map<uint32_t, wdata::WeaponLoadout> m_loadouts;
};

inline ArsenalStore& Store() {
    static ArsenalStore st;
    return st;
}

} // namespace wstore
