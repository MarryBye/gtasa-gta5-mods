#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "sa_sdk.hpp"

namespace wardrobe {

constexpr int CLOTH_SLOT_COUNT = 8;
constexpr int MAX_OUTFIT_PRESETS = 8;

// Maps slot index 0..7 -> GTA SA CPedClothesDesc bodyPart IDs
constexpr uint32_t kSlotBodyParts[CLOTH_SLOT_COUNT] = {
    0,  // 0: Torso
    2,  // 1: Legs
    3,  // 2: Shoes
    13, // 3: Chains (Necklace)
    14, // 4: Watches
    15, // 5: Glasses
    16, // 6: Hats
    17  // 7: Special / Uniforms
};

struct ClothItemDef {
    std::string texName;
    std::string gxtTag;
    std::string modelName;
    uint32_t    bodyPart = 0; // 0, 2, 3, 13, 14, 15, 16, 17
    uint32_t    texHash  = 0;
    uint32_t    modHash  = 0;
    int         respect  = 0;
    int         sexy     = 0;
    int         price    = 0;
};

struct ShopCatalog {
    std::string shopKey;   // e.g. "CSchp", "CSsprt", "LACS1", "clothgp", "Csdesgn", "Csexl"
    std::string nameRus;
    std::string nameEng;
    std::vector<int> itemIndices; // Indices into Catalog()
};

enum class AutoTrigger : uint8_t {
    Manual = 0,
    OnMission,
    FreeRoam,
    WhenWanted,
    Count
};

struct OutfitPreset {
    bool        used    = false;
    std::string name;
    AutoTrigger trigger = AutoTrigger::Manual;
    std::array<std::string, CLOTH_SLOT_COUNT> tex;
    std::array<std::string, CLOTH_SLOT_COUNT> mod;
};

void EnsureCatalogLoaded();
const std::vector<ClothItemDef>& Catalog();
const ShopCatalog* FindShopCatalog(const char* loadedShopName);

std::string GetClothDisplayNameGame(const ClothItemDef& item);
const char* SlotNameRus(int slotIdx);
const char* SlotNameEng(int slotIdx);
const char* TriggerNameRus(AutoTrigger t);
const char* TriggerNameEng(AutoTrigger t);

// Ownership tracking (synced seamlessly with vanilla GTA SA CShopping + our per-save file!)
bool IsClothOwned(int itemIdx);
void MarkClothOwned(int itemIdx);
bool IsClothWorn(const sa::CPed* ped, int itemIdx);
void SyncWornClothesToOwned(const sa::CPed* ped);

// Safe live cloth/outfit application (rebuilds immediately on foot, or queues safely if in vehicle!)
void ApplyClothItem(sa::CPed* ped, int itemIdx, bool previewOnly = false);
void ClearClothSlot(sa::CPed* ped, int slotIdx, bool previewOnly = false);

// Snapshot & restore current CJ clothes (for shop preview revert!)
struct ClothSnapshot {
    std::array<uint32_t, 10> modelKeys{};
    std::array<uint32_t, 18> texKeys{};
    bool valid = false;
};
ClothSnapshot CaptureSnapshot(const sa::CPed* ped);
void RestoreSnapshot(sa::CPed* ped, const ClothSnapshot& snap);

// Outfit Presets (8 slots + Auto-Equip Triggers)
std::array<OutfitPreset, MAX_OUTFIT_PRESETS>& Presets();
bool SaveCurrentToPreset(sa::CPed* ped, int presetIdx);
bool EquipPreset(sa::CPed* ped, int presetIdx, bool showNotify = true);
void CyclePresetTrigger(int presetIdx);
void DeletePreset(int presetIdx);

// Per-save persistence & per-frame auto-equip + deferred vehicle-exit rebuild tick
void OnGameSave(int slot1to8);
void OnGameLoad(int slot1to8);
void OnNewGame();
void SaveNow();
void Tick();
void Reset();

} // namespace wardrobe
