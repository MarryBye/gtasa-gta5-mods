#include "wardrobe.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <windows.h>
#include "log.hpp"
#include "mod_bus.hpp"
#include "phone_config.hpp"
#include "sa_sdk.hpp"
#include "ui_kit.hpp"

namespace wardrobe {

namespace {

bool                        g_catalogLoaded = false;
std::vector<ClothItemDef>   g_catalog;
std::vector<ShopCatalog>    g_shops;
std::vector<ClothItemDef>   g_hairCatalog;
std::vector<ShopCatalog>    g_barberShops;
std::unordered_set<uint32_t> g_ownedHashes;
std::array<OutfitPreset, MAX_OUTFIT_PRESETS> g_presets{};

int  g_saveSlot             = 0;
bool g_pendingRebuild       = false;
bool g_wasOnMission         = false;
bool g_wasWanted            = false;
bool g_autoStateInited      = false;
int  g_lastAutoPresetIdx    = -1;

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
    // SA-COOP F4 Custom Skin safety: CClothes::RebuildPlayer ONLY supports CJ's player model (ID 0)!
    if (ped->GetModelIndex() != 0) {
        g_pendingRebuild = false;
        return;
    }
    // Verified at 0x005A82C0: CClothes::RebuildPlayer preserves seated vehicle anim associations cleanly!
    // Only defer during cutscenes so CJ can change clothes IMMEDIATELY even while driving/sitting in a car!
    if (!forceNow && sa::IsCutsceneRunning()) {
        g_pendingRebuild = true;
        return;
    }
    g_pendingRebuild = false;
    sa::clothes::RebuildPlayer(ped, false);
}

std::string GetSaveFilePath(int slot, bool forLoading = false) {
    const char* ov = nullptr;
    if (auto* b = modbus::GetBus(); b && b->coopProfileName[0]) ov = b->coopProfileName;
    if (forLoading) {
        return sa::ResolveExistingLoadPath("GTA5PhoneSaves", pcfg::Get().modDir, "wardrobe", slot, ov);
    }
    return sa::BuildSaveProfilePath("GTA5PhoneSaves", pcfg::Get().modDir, "wardrobe", slot, ov);
}

void ApplyPresetInternal(sa::CPed* ped, const OutfitPreset& pr, bool showNotify) {
    if (!sa::IsPedValid(ped) || !pr.used) return;
    if (ped->GetModelIndex() != 0) {
        if (showNotify) {
            ui::ShowHelp("~y~Гардероб CJ недоступен при нестандартном скине (F4 в SA-COOP).",
                         "~y~CJ Wardrobe requires the default CJ skin (not a custom F4 skin).");
        }
        return;
    }
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;

    // Clear Special slot first unless preset explicitly sets one
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
            ui::ShowHelp("~g~Набор одежды надет!", "~g~Outfit equipped!");
        }
    }
}

} // namespace

void EnsureCatalogLoaded() {
    if (g_catalogLoaded) return;
    g_catalogLoaded = true;
    g_catalog.clear();
    g_shops.clear();
    g_hairCatalog.clear();
    g_barberShops.clear();

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
    static constexpr ShopMeta kBarberMetas[] = {
        {"barbers", "ПАРИКМАХЕРСКАЯ (LS)", "BARBER SHOP (LS)"},
        {"barber2", "ПАРИКМАХЕРСКАЯ (SF)", "BARBER SHOP (SF)"},
        {"barber3", "ПАРИКМАХЕРСКАЯ (LV)", "BARBER SHOP (LV)"},
    };

    bool inClothesPrices = false;
    bool inHairPrices    = false;
    int  activeShopIdx   = -1;
    int  activeBarberIdx = -1;

    std::string line;
    while (std::getline(f, line)) {
        // Strip inline comments
        size_t hashPos = line.find('#');
        if (hashPos != std::string::npos) line.resize(hashPos);
        std::istringstream iss(line);
        std::string tok;
        if (!(iss >> tok)) continue;

        if (_stricmp(tok.c_str(), "section") == 0) {
            std::string secName;
            if (iss >> secName) {
                inClothesPrices = false;
                inHairPrices    = false;
                activeShopIdx   = -1;
                activeBarberIdx = -1;

                if (_stricmp(secName.c_str(), "Clothes") == 0) {
                    inClothesPrices = true;
                } else if (_stricmp(secName.c_str(), "Haircuts") == 0) {
                    inHairPrices = true;
                } else {
                    for (const auto& sm : kShopMetas) {
                        if (_stricmp(secName.c_str(), sm.key) == 0) {
                            ShopCatalog sc;
                            sc.shopKey = sm.key;
                            sc.nameRus = sm.rus;
                            sc.nameEng = sm.eng;
                            activeShopIdx = static_cast<int>(g_shops.size());
                            g_shops.push_back(std::move(sc));
                            break;
                        }
                    }
                    if (activeShopIdx < 0) {
                        for (const auto& bm : kBarberMetas) {
                            if (_stricmp(secName.c_str(), bm.key) == 0) {
                                ShopCatalog sc;
                                sc.shopKey = bm.key;
                                sc.nameRus = bm.rus;
                                sc.nameEng = bm.eng;
                                activeBarberIdx = static_cast<int>(g_barberShops.size());
                                g_barberShops.push_back(std::move(sc));
                                break;
                            }
                        }
                    }
                }
            }
            continue;
        }

        if (_stricmp(tok.c_str(), "end") == 0) {
            inClothesPrices = false;
            inHairPrices    = false;
            activeShopIdx   = -1;
            activeBarberIdx = -1;
            continue;
        }

        if (inClothesPrices) {
            std::string tex = tok;
            std::string tag, modName, s1, s2;
            int typeVal = -1, v1 = 0, v2 = 0, price = 0;
            if (iss >> tag >> modName >> typeVal >> s1 >> v1 >> s2 >> v2 >> price) {
                if (BodyPartToSlotIdx(static_cast<uint32_t>(typeVal)) >= 0) {
                    ClothItemDef def;
                    def.texName   = tex;
                    def.gxtTag    = tag;
                    def.modelName = modName;
                    def.bodyPart  = static_cast<uint32_t>(typeVal);
                    def.texHash   = sa::GetUppercaseKey(tex.c_str());
                    def.modHash   = sa::GetUppercaseKey(modName.c_str());
                    if (_stricmp(s1.c_str(), "respect") == 0) def.respect = v1;
                    if (_stricmp(s2.c_str(), "sexy")    == 0) def.sexy    = v2;
                    def.price     = price;
                    g_catalog.push_back(std::move(def));
                }
            }
        } else if (inHairPrices) {
            std::string tex = tok;
            std::string tag, modName, s1, s2;
            int typeVal = -1, v1 = 0, v2 = 0, price = 0;
            if (iss >> tag >> modName >> typeVal >> s1 >> v1 >> s2 >> v2 >> price) {
                if (typeVal == 1) {
                    ClothItemDef def;
                    def.texName   = tex;
                    def.gxtTag    = tag;
                    def.modelName = modName;
                    def.bodyPart  = 1;
                    def.texHash   = sa::GetUppercaseKey(tex.c_str());
                    def.modHash   = sa::GetUppercaseKey(modName.c_str());
                    if (_stricmp(s1.c_str(), "respect") == 0) def.respect = v1;
                    if (_stricmp(s2.c_str(), "sexy")    == 0) def.sexy    = v2;
                    def.price     = price;
                    g_hairCatalog.push_back(std::move(def));
                }
            }
        } else if (activeShopIdx >= 0 && _stricmp(tok.c_str(), "item") == 0) {
            std::string itemTex;
            if (iss >> itemTex) {
                uint32_t h = sa::GetUppercaseKey(itemTex.c_str());
                for (size_t i = 0; i < g_catalog.size(); ++i) {
                    if (g_catalog[i].texHash == h) {
                        g_shops[activeShopIdx].itemIndices.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
        } else if (activeBarberIdx >= 0 && _stricmp(tok.c_str(), "item") == 0) {
            std::string itemTex;
            if (iss >> itemTex) {
                uint32_t h = sa::GetUppercaseKey(itemTex.c_str());
                for (size_t i = 0; i < g_hairCatalog.size(); ++i) {
                    if (g_hairCatalog[i].texHash == h) {
                        g_barberShops[activeBarberIdx].itemIndices.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
        }
    }

    modlog::Info("[Wardrobe] Parsed %zu clothing items (%zu stores) & %zu haircuts (%zu barbers) from shopping.dat.",
                 g_catalog.size(), g_shops.size(), g_hairCatalog.size(), g_barberShops.size());
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

const std::vector<ClothItemDef>& HairCatalog() {
    EnsureCatalogLoaded();
    return g_hairCatalog;
}

const ShopCatalog* FindBarberCatalog(const char* loadedShopName) {
    EnsureCatalogLoaded();
    if (!loadedShopName || !loadedShopName[0]) return nullptr;
    for (const auto& s : g_barberShops) {
        if (_stricmp(s.shopKey.c_str(), loadedShopName) == 0) return &s;
    }
    return nullptr;
}

bool IsHaircutWorn(const sa::CPed* ped, int hairIdx) {
    EnsureCatalogLoaded();
    if (hairIdx < 0 || hairIdx >= static_cast<int>(g_hairCatalog.size())) return false;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return false;
    return desc->m_anTextureKeys[1] == g_hairCatalog[hairIdx].texHash;
}

void ApplyHaircutItem(sa::CPed* ped, int hairIdx) {
    EnsureCatalogLoaded();
    if (!sa::IsPedValid(ped) || hairIdx < 0 || hairIdx >= static_cast<int>(g_hairCatalog.size())) return;
    if (ped->GetModelIndex() != 0) return;
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;
    const auto& it = g_hairCatalog[hairIdx];
    sa::clothes::SetTextureAndModel(desc, it.texName.c_str(), it.modelName.c_str(), 1);
    SafeRebuildPlayer(ped, true);
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
        "Верх (Торс)", "Штаны и Шорты", "Обувь", "Цепи",
        "Часы", "Очки и Маски", "Головные уборы", "Особые костюмы"
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
        case AutoTrigger::FreeRoam:   return "Простая игра";
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
    if (!sa::IsPedValid(ped) || ped->GetModelIndex() != 0) return;
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
    if (ped->GetModelIndex() != 0) {
        if (!previewOnly) {
            ui::ShowHelp("~y~Гардероб CJ доступен только для стандартного скина CJ.",
                         "~y~CJ Wardrobe only works on the default CJ player model.");
        }
        return;
    }
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return;

    const auto& it = g_catalog[itemIdx];
    if (it.bodyPart != 17) {
        sa::clothes::SetTextureAndModel(desc, nullptr, nullptr, 17); // Clear special uniform
    }
    sa::clothes::SetTextureAndModel(desc, it.texName.c_str(), it.modelName.c_str(), it.bodyPart);
    SafeRebuildPlayer(ped, previewOnly);
}

void ClearClothSlot(sa::CPed* ped, int slotIdx, bool previewOnly) {
    if (!sa::IsPedValid(ped) || slotIdx < 0 || slotIdx >= CLOTH_SLOT_COUNT) return;
    if (ped->GetModelIndex() != 0) return;
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
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return snap;
    for (int i = 0; i < 10; ++i) snap.modelKeys[i] = desc->m_anModelKeys[i];
    for (int i = 0; i < 18; ++i) snap.texKeys[i]   = desc->m_anTextureKeys[i];
    snap.valid = true;
    return snap;
}

void RestoreSnapshot(sa::CPed* ped, const ClothSnapshot& snap) {
    if (!snap.valid || !sa::IsPedValid(ped)) return;
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
    auto* desc = sa::clothes::GetPlayerClothesDesc(ped);
    if (!desc) return false;

    auto& pr = g_presets[presetIdx];
    pr.used = true;
    if (pr.name.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Outfit #%d", presetIdx + 1);
        pr.name = buf;
    }

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
        std::fprintf(f, "Name=%s\nTrigger=%u\n", pr.name.c_str(), static_cast<unsigned>(pr.trigger));
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
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const char* p = path.c_str();

    int cnt = GetPrivateProfileIntA("Owned", "Count", 0, p);
    for (int i = 0; i < cnt && i < 1024; ++i) {
        char key[24], val[32] = {};
        std::snprintf(key, sizeof(key), "H%d", i);
        GetPrivateProfileStringA("Owned", key, "", val, sizeof(val), p);
        if (val[0]) {
            uint32_t h = static_cast<uint32_t>(std::strtoul(val, nullptr, 16));
            if (h != 0) g_ownedHashes.insert(h);
        }
    }

    for (int i = 0; i < MAX_OUTFIT_PRESETS; ++i) {
        char sec[24];
        std::snprintf(sec, sizeof(sec), "Preset%d", i);
        if (GetPrivateProfileIntA(sec, "Used", 0, p) == 0) continue;
        auto& pr = g_presets[i];
        pr.used = true;
        char buf[128] = {};
        GetPrivateProfileStringA(sec, "Name", "", buf, sizeof(buf), p);
        pr.name = buf;
        int tr = GetPrivateProfileIntA(sec, "Trigger", 0, p);
        pr.trigger = (tr >= 0 && tr < static_cast<int>(AutoTrigger::Count))
            ? static_cast<AutoTrigger>(tr) : AutoTrigger::Manual;
        for (int s = 0; s < CLOTH_SLOT_COUNT; ++s) {
            char tk[12], mk[12], tv[64] = {}, mv[64] = {};
            std::snprintf(tk, sizeof(tk), "T%d", s);
            std::snprintf(mk, sizeof(mk), "M%d", s);
            GetPrivateProfileStringA(sec, tk, "", tv, sizeof(tv), p);
            GetPrivateProfileStringA(sec, mk, "", mv, sizeof(mv), p);
            pr.tex[s] = tv;
            pr.mod[s] = mv;
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

    // 1. Complete any queued outfit rebuild as soon as cutscene finishes!
    if (g_pendingRebuild && !sa::IsCutsceneRunning()) {
        SafeRebuildPlayer(ped, true);
    }

    // 2. Periodically sync CJ's currently worn clothes (e.g., mission uniforms) into owned set
    static uint32_t s_lastSyncMs = 0;
    uint32_t now = sa::GetTimeMs();
    if (now - s_lastSyncMs >= 3000) {
        s_lastSyncMs = now;
        SyncWornClothesToOwned(ped);
    }

    // 3. Auto-Equip Outfit Presets on Mission / Free Roam / Wanted transitions!
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
        modlog::Info("[Wardrobe] Auto-equipped outfit preset #%d (mission=%d, wanted=%d).",
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
