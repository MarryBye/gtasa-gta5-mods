#include "wardrobe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <windows.h>
#include "clothes_shop.hpp"
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "phone_config.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace wardrobe {

namespace {

bool                         g_catalogLoaded = false;
std::vector<ClothItemDef>    g_catalog;
std::vector<ShopCatalog>     g_shops;
std::unordered_set<uint32_t> g_ownedHashes;
std::array<OutfitPreset, MAX_OUTFIT_PRESETS> g_presets{};

int  g_saveSlot          = 0;
bool g_pendingRebuild    = false;
bool g_wasOnMission      = false;
bool g_wasWanted         = false;
bool g_autoStateInited   = false;
int  g_lastAutoPresetIdx = -1;

int BodyPartToSlotIdx(uint32_t bp) {
    for (int i = 0; i < CLOTH_SLOT_COUNT; ++i) {
        if (kSlotBodyParts[i] == bp) return i;
    }
    return -1;
}

bool IsDefaultStarterCloth(const std::string& tex) {
    return (_stricmp(tex.c_str(), "player_torso")   == 0) ||
           (_stricmp(tex.c_str(), "player_legs")    == 0) ||
           (_stricmp(tex.c_str(), "foot")           == 0) ||
           (_stricmp(tex.c_str(), "vest")           == 0) ||
           (_stricmp(tex.c_str(), "jeansdenim")     == 0) ||
           (_stricmp(tex.c_str(), "sneakerbincblk") == 0);
}

void SafeRebuildPlayer(sa::CPed* ped, bool forceNow) {
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        g_pendingRebuild = false;
        return;
    }
    if (!forceNow && sa::IsCutsceneRunning()) {
        g_pendingRebuild = true;
        return;
    }
    g_pendingRebuild = false;
    sa::clothes::RebuildPlayer(ped, false);
}

std::string GetSaveFilePath(int slot, bool forLoading = false) {
    const char* ov = integrations::sacoop::GetProfileOverride();
    if (forLoading) {
        return integrations::sacoop::ResolveExistingLoadPath("GTA5PhoneSaves", pcfg::Get().modDir, "wardrobe", slot, ov);
    }
    return integrations::sacoop::BuildSaveProfilePath("GTA5PhoneSaves", pcfg::Get().modDir, "wardrobe", slot, ov);
}

void ApplyPresetInternal(sa::CPed* ped, const OutfitPreset& pr, bool showNotify) {
    if (!sa::IsPedValid(ped) || !pr.used) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        if (showNotify) {
            ui::ShowHelp("~y~Гардероб доступен только для стандартного облика.",
                         "~y~Wardrobe requires the default character model.");
        }
        return;
    }
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;

    if (pr.tex[7].empty()) {
        sa::clothes::SetTextureAndModel(desc, nullptr, nullptr, 17);
    }
    for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
        uint32_t bp = kSlotBodyParts[s];
        if (!pr.tex[s].empty()) {
            const char* m = pr.mod[s].empty() ? nullptr : pr.mod[s].c_str();
            sa::clothes::SetTextureAndModel(desc, pr.tex[s].c_str(), m, bp);
        } else if (bp >= 13 && bp <= 17) {
            sa::clothes::SetTextureAndModel(desc, nullptr, nullptr, bp);
        }
    }

    if (sa::IsCutsceneRunning()) {
        g_pendingRebuild = true;
    } else {
        SafeRebuildPlayer(ped, true);
        if (showNotify) {
            ui::ShowHelp("~g~Набор одежды надет.", "~g~Outfit equipped.");
        }
    }
}

bool ParseCatalogEntry(const std::string& tex, std::istringstream& iss, ClothItemDef& outDef) {
    std::string tag, modName, s1, s2;
    int typeVal = -1, v1 = 0, v2 = 0, price = 0;
    if (!(iss >> tag >> modName >> typeVal >> s1 >> v1 >> s2 >> v2 >> price)) return false;
    if (BodyPartToSlotIdx(static_cast<uint32_t>(typeVal)) < 0) return false;
    outDef.texName   = tex;
    outDef.gxtTag    = tag;
    outDef.modelName = modName;
    outDef.bodyPart  = static_cast<uint32_t>(typeVal);
    outDef.texHash   = sa::GetUppercaseKey(tex.c_str());
    outDef.modHash   = sa::GetUppercaseKey(modName.c_str());
    if (_stricmp(s1.c_str(), "respect") == 0) outDef.respect = v1;
    if (_stricmp(s2.c_str(), "sexy")    == 0) outDef.sexy    = v2;
    outDef.price     = price;
    return true;
}

void AppendShopItemByHash(const std::vector<ClothItemDef>& catalog, ShopCatalog& shop, const std::string& itemTex) {
    uint32_t h = sa::GetUppercaseKey(itemTex.c_str());
    for (size_t i = 0; i < catalog.size(); ++i) {
        if (catalog[i].texHash == h) {
            shop.itemIndices.push_back(static_cast<int>(i));
            return;
        }
    }
}

} // namespace

void EnsureCatalogLoaded() {
    if (g_catalogLoaded) return;
    g_catalogLoaded = true;
    g_catalog.clear();
    g_shops.clear();

    const std::string path = pcfg::Get().gameDir + "\\data\\shopping.dat";
    std::ifstream f(path);
    if (!f.is_open()) {
        modlog::Info("[Wardrobe] WARNING: Could not open %s", path.c_str());
        return;
    }

    struct ShopMeta { const char* key; const char* rus; const char* eng; };
    static constexpr ShopMeta kShopMetas[] = {
        {"CSchp",   "BINCO",        "BINCO"},
        {"CSsprt",  "PROLAPS",      "PROLAPS"},
        {"LACS1",   "SUB URBAN",    "SUB URBAN"},
        {"clothgp", "ZIP",          "ZIP"},
        {"Csdesgn", "VICTIM",       "VICTIM"},
        {"Csexl",   "DIDIER SACHS", "DIDIER SACHS"},
    };

    bool inClothesPrices = false;
    int  activeShopIdx   = -1;

    std::string line;
    while (std::getline(f, line)) {
        size_t hashPos = line.find('#');
        if (hashPos != std::string::npos) line.resize(hashPos);
        std::istringstream iss(line);
        std::string tok;
        if (!(iss >> tok)) continue;

        if (_stricmp(tok.c_str(), "section") == 0) {
            std::string secName;
            if (iss >> secName) {
                inClothesPrices = false;
                activeShopIdx   = -1;

                if (_stricmp(secName.c_str(), "Clothes") == 0) {
                    inClothesPrices = true;
                } else {
                    for (const auto& sm : kShopMetas) {
                        if (_stricmp(secName.c_str(), sm.key) == 0) {
                            activeShopIdx = static_cast<int>(g_shops.size());
                            g_shops.push_back({sm.key, sm.rus, sm.eng, {}});
                            break;
                        }
                    }
                }
            }
            continue;
        }

        if (_stricmp(tok.c_str(), "end") == 0) {
            inClothesPrices = false;
            activeShopIdx   = -1;
            continue;
        }

        if (inClothesPrices) {
            ClothItemDef def;
            if (ParseCatalogEntry(tok, iss, def)) g_catalog.push_back(std::move(def));
        } else if (activeShopIdx >= 0 && _stricmp(tok.c_str(), "item") == 0) {
            std::string itemTex;
            if (iss >> itemTex) AppendShopItemByHash(g_catalog, g_shops[activeShopIdx], itemTex);
        }
    }

    modlog::Info("[Wardrobe] Parsed %zu clothing items (%zu stores).", g_catalog.size(), g_shops.size());
}

const std::vector<ClothItemDef>& Catalog() {
    EnsureCatalogLoaded();
    return g_catalog;
}

const ShopCatalog* FindShopCatalog(const char* loadedShopName) {
    EnsureCatalogLoaded();
    if (!loadedShopName || !loadedShopName[0]) return nullptr;
    for (const auto& s : g_shops) {
        if (_stricmp(s.shopKey.c_str(), loadedShopName) == 0) return &s;
    }
    return nullptr;
}

std::string GetClothDisplayNameGame(const ClothItemDef& item) {
    if (!item.gxtTag.empty()) {
        const char* loc = sa::TextGet(item.gxtTag.c_str());
        if (loc && loc[0] != '\0') return std::string(loc);
    }
    return item.texName;
}

const char* SlotNameRus(int slotIdx) {
    static constexpr const char* kNames[CLOTH_SLOT_COUNT] = {
        "Верхняя одежда", "Штаны и шорты", "Обувь", "Цепи",
        "Часы", "Очки и маски", "Головные уборы", "Особые костюмы"
    };
    return (slotIdx >= 0 && slotIdx < CLOTH_SLOT_COUNT) ? kNames[slotIdx] : "Одежда";
}

const char* SlotNameEng(int slotIdx) {
    static constexpr const char* kNames[CLOTH_SLOT_COUNT] = {
        "Torso", "Legs", "Shoes", "Chains",
        "Watches", "Glasses & Masks", "Hats", "Special Outfits"
    };
    return (slotIdx >= 0 && slotIdx < CLOTH_SLOT_COUNT) ? kNames[slotIdx] : "Clothes";
}

const char* TriggerNameRus(AutoTrigger t) {
    switch (t) {
        case AutoTrigger::OnMission:  return "При миссии";
        case AutoTrigger::FreeRoam:   return "Свободная игра";
        case AutoTrigger::WhenWanted: return "При розыске";
        default:                      return "Вручную";
    }
}

const char* TriggerNameEng(AutoTrigger t) {
    switch (t) {
        case AutoTrigger::OnMission:  return "On Mission";
        case AutoTrigger::FreeRoam:   return "Free Roam";
        case AutoTrigger::WhenWanted: return "When Wanted";
        default:                      return "Manual";
    }
}

bool IsClothOwned(int itemIdx) {
    EnsureCatalogLoaded();
    if (itemIdx < 0 || itemIdx >= static_cast<int>(g_catalog.size())) return false;
    const auto& it = g_catalog[itemIdx];
    if (IsDefaultStarterCloth(it.texName)) return true;
    if (g_ownedHashes.count(it.texHash) > 0) return true;
    if (sa::shopping::HasBoughtHash(it.texHash)) return true;
    return false;
}

void MarkClothOwned(int itemIdx) {
    EnsureCatalogLoaded();
    if (itemIdx < 0 || itemIdx >= static_cast<int>(g_catalog.size())) return;
    const auto& it = g_catalog[itemIdx];
    g_ownedHashes.insert(it.texHash);
    sa::shopping::SetBoughtHash(it.texHash, true);
}

bool IsClothWorn(const sa::CPed* ped, int itemIdx) {
    EnsureCatalogLoaded();
    if (itemIdx < 0 || itemIdx >= static_cast<int>(g_catalog.size())) return false;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return false;
    const auto& it = g_catalog[itemIdx];
    if (it.bodyPart >= 18) return false;
    return desc->m_anTextureKeys[it.bodyPart] == it.texHash;
}

void SyncWornClothesToOwned(const sa::CPed* ped) {
    EnsureCatalogLoaded();
    if (!sa::IsPedValid(ped) || integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) return;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;
    for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
        uint32_t bp = kSlotBodyParts[s];
        uint32_t h  = desc->m_anTextureKeys[bp];
        if (h != 0) g_ownedHashes.insert(h);
    }
}

void ApplyClothItem(sa::CPed* ped, int itemIdx, bool previewOnly) {
    EnsureCatalogLoaded();
    if (!sa::IsPedValid(ped) || itemIdx < 0 || itemIdx >= static_cast<int>(g_catalog.size())) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        if (!previewOnly) {
            ui::ShowHelp("~y~Гардероб доступен только для стандартного облика.",
                         "~y~Wardrobe requires the default character model.");
        }
        return;
    }
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;

    const auto& it = g_catalog[itemIdx];
    if (it.bodyPart != 17) {
        sa::clothes::SetTextureAndModel(desc, nullptr, nullptr, 17);
    }
    sa::clothes::SetTextureAndModel(desc, it.texName.c_str(), it.modelName.c_str(), it.bodyPart);
    SafeRebuildPlayer(ped, previewOnly);
}

void ClearClothSlot(sa::CPed* ped, int slotIdx, bool previewOnly) {
    if (!sa::IsPedValid(ped) || slotIdx < 0 || slotIdx >= CLOTH_SLOT_COUNT) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) return;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;
    uint32_t bp = kSlotBodyParts[slotIdx];
    if (bp == 0)      sa::clothes::SetTextureAndModel(desc, "player_torso", "torso", 0);
    else if (bp == 2) sa::clothes::SetTextureAndModel(desc, "player_legs",  "legs",  2);
    else if (bp == 3) sa::clothes::SetTextureAndModel(desc, "foot",         "feet",  3);
    else              sa::clothes::SetTextureAndModel(desc, nullptr,        nullptr, bp);
    SafeRebuildPlayer(ped, previewOnly);
}

ClothSnapshot CaptureSnapshot(const sa::CPed* ped) {
    ClothSnapshot snap;
    if (!sa::IsPedValid(ped) || integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) return snap;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return snap;
    for (int i = 0; i < 10; ++i) snap.modelKeys[i] = desc->m_anModelKeys[i];
    for (int i = 0; i < 18; ++i) snap.texKeys[i]   = desc->m_anTextureKeys[i];
    snap.valid = true;
    return snap;
}

void RestoreSnapshot(sa::CPed* ped, const ClothSnapshot& snap) {
    if (!snap.valid || !sa::IsPedValid(ped)) return;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) return;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;
    for (int i = 0; i < 10; ++i) desc->m_anModelKeys[i]   = snap.modelKeys[i];
    for (int i = 0; i < 18; ++i) desc->m_anTextureKeys[i] = snap.texKeys[i];
    SafeRebuildPlayer(ped, true);
}

std::array<OutfitPreset, MAX_OUTFIT_PRESETS>& Presets() { return g_presets; }

bool SaveCurrentToPreset(sa::CPed* ped, int presetIdx) {
    EnsureCatalogLoaded();
    if (!sa::IsPedValid(ped) || presetIdx < 0 || presetIdx >= MAX_OUTFIT_PRESETS) return false;
    if (integrations::sacoop::IsCustomSkinModel(ped->GetModelIndex())) {
        ui::ShowHelp("~y~Гардероб доступен только для стандартного облика.",
                     "~y~Wardrobe requires the default character model.");
        return false;
    }
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return false;

    auto& pr = g_presets[presetIdx];
    pr.used = true;
    for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
        pr.tex[s].clear();
        pr.mod[s].clear();
        uint32_t bp = kSlotBodyParts[s];
        uint32_t th = desc->m_anTextureKeys[bp];
        if (th == 0) continue;
        for (const auto& def : g_catalog) {
            if (def.bodyPart == bp && def.texHash == th) {
                pr.tex[s] = def.texName;
                pr.mod[s] = def.modelName;
                break;
            }
        }
    }

    SaveNow();
    return true;
}

bool EquipPreset(sa::CPed* ped, int presetIdx, bool showNotify) {
    if (presetIdx < 0 || presetIdx >= MAX_OUTFIT_PRESETS || !g_presets[presetIdx].used) return false;
    ApplyPresetInternal(ped, g_presets[presetIdx], showNotify);
    return true;
}

void CyclePresetTrigger(int presetIdx) {
    if (presetIdx < 0 || presetIdx >= MAX_OUTFIT_PRESETS || !g_presets[presetIdx].used) return;
    uint8_t next = (static_cast<uint8_t>(g_presets[presetIdx].trigger) + 1) % static_cast<uint8_t>(AutoTrigger::Count);
    g_presets[presetIdx].trigger = static_cast<AutoTrigger>(next);
    SaveNow();
}

void DeletePreset(int presetIdx) {
    if (presetIdx < 0 || presetIdx >= MAX_OUTFIT_PRESETS) return;
    g_presets[presetIdx] = OutfitPreset{};
    SaveNow();
}

void SaveNow() {
    std::string path = GetSaveFilePath(g_saveSlot);
    if (path.empty()) return; // Unsaved Singleplayer/Host New Game: keep in RAM until saved to slot 1..8
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;

    std::fprintf(f, "; GTA5Phone Wardrobe & Outfit Presets (Slot %d)\n\n[Owned]\n", g_saveSlot);
    int oi = 0;
    for (uint32_t h : g_ownedHashes) {
        std::fprintf(f, "H%d=%08X\n", oi++, h);
    }
    std::fprintf(f, "Count=%d\n\n", oi);

    for (int i = 0; i < MAX_OUTFIT_PRESETS; ++i) {
        const auto& pr = g_presets[i];
        std::fprintf(f, "[Preset%d]\nUsed=%d\n", i, pr.used ? 1 : 0);
        if (!pr.used) { std::fprintf(f, "\n"); continue; }
        std::fprintf(f, "Trigger=%u\n", static_cast<unsigned>(pr.trigger));
        for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
            std::fprintf(f, "T%d=%s\nM%d=%s\n", s, pr.tex[s].c_str(), s, pr.mod[s].c_str());
        }
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}

void OnGameSave(int slot1to8) {
    if (slot1to8 >= 1 && slot1to8 <= 8) g_saveSlot = slot1to8;
    SyncWornClothesToOwned(sa::FindPlayerPed(0));
    SaveNow();
}

void OnGameLoad(int slot1to8) {
    EnsureCatalogLoaded();
    g_saveSlot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
    g_ownedHashes.clear();
    for (auto& pr : g_presets) pr = OutfitPreset{};
    g_pendingRebuild  = false;
    g_autoStateInited = false;

    std::string path = GetSaveFilePath(g_saveSlot, true);
    if (path.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const char* p = path.c_str();

    int cnt = common::IniReadInt(p, "Owned", "Count", 0);
    for (int i = 0; i < cnt && i < 1024; ++i) {
        char key[24];
        std::snprintf(key, sizeof(key), "H%d", i);
        std::string val = common::IniReadString(p, "Owned", key, "");
        if (!val.empty()) {
            uint32_t h = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 16));
            if (h != 0) g_ownedHashes.insert(h);
        }
    }

    for (int i = 0; i < MAX_OUTFIT_PRESETS; ++i) {
        char sec[24];
        std::snprintf(sec, sizeof(sec), "Preset%d", i);
        if (common::IniReadInt(p, sec, "Used", 0) == 0) continue;
        auto& pr = g_presets[i];
        pr.used = true;
        int tr = common::IniReadInt(p, sec, "Trigger", 0);
        pr.trigger = (tr >= 0 && tr < static_cast<int>(AutoTrigger::Count))
            ? static_cast<AutoTrigger>(tr) : AutoTrigger::Manual;
        for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
            char tk[12], mk[12];
            std::snprintf(tk, sizeof(tk), "T%d", s);
            std::snprintf(mk, sizeof(mk), "M%d", s);
            pr.tex[s] = common::IniReadString(p, sec, tk, "");
            pr.mod[s] = common::IniReadString(p, sec, mk, "");
        }
    }
    modlog::Info("[Wardrobe] Loaded wardrobe slot %d (%zu owned clothes).", g_saveSlot, g_ownedHashes.size());
}

void OnNewGame() {
    g_saveSlot = 0;
    g_ownedHashes.clear();
    for (auto& pr : g_presets) pr = OutfitPreset{};
    g_pendingRebuild  = false;
    g_autoStateInited = false;
}

void Tick() {
    if (sa::GetGameState() != 9 || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) return;

    if (g_pendingRebuild && !sa::IsCutsceneRunning()) {
        SafeRebuildPlayer(ped, true);
    }

    static uint32_t s_lastSyncMs = 0;
    uint32_t now = sa::GetTimeMs();
    if (!clothesshop::IsShopActive() && (now - s_lastSyncMs >= 3000)) {
        s_lastSyncMs = now;
        SyncWornClothesToOwned(ped);
    }

    if (!pcfg::Get().enableAutoOutfits) return;
    bool onMission = sa::IsOnMission();
    bool isWanted  = (sa::wanted::GetLevel(0) > 0);

    if (!g_autoStateInited) {
        g_wasOnMission    = onMission;
        g_wasWanted       = isWanted;
        g_autoStateInited = true;
        return;
    }

    auto findPresetWithTrigger = [](AutoTrigger trig) -> int {
        for (int i = 0; i < MAX_OUTFIT_PRESETS; ++i) {
            if (g_presets[i].used && g_presets[i].trigger == trig) return i;
        }
        return -1;
    };

    int targetPreset = -1;
    if (!g_wasWanted && isWanted) {
        targetPreset = findPresetWithTrigger(AutoTrigger::WhenWanted);
    } else if (!g_wasOnMission && onMission) {
        targetPreset = findPresetWithTrigger(AutoTrigger::OnMission);
    } else if ((g_wasOnMission && !onMission) || (g_wasWanted && !isWanted && !onMission)) {
        targetPreset = findPresetWithTrigger(AutoTrigger::FreeRoam);
    }

    if (targetPreset >= 0 && targetPreset != g_lastAutoPresetIdx) {
        g_lastAutoPresetIdx = targetPreset;
        ApplyPresetInternal(ped, g_presets[targetPreset], true);
        modlog::Info("[Wardrobe] Auto-equipped outfit #%d (mission=%d, wanted=%d).",
                     targetPreset + 1, onMission ? 1 : 0, isWanted ? 1 : 0);
    } else if (targetPreset < 0 && !onMission && !isWanted) {
        g_lastAutoPresetIdx = -1;
    }

    g_wasOnMission = onMission;
    g_wasWanted    = isWanted;
}

void Reset() {
    g_pendingRebuild    = false;
    g_autoStateInited   = false;
    g_lastAutoPresetIdx = -1;
}

} // namespace wardrobe
