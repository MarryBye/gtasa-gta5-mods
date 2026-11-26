#include "dealership.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "config.hpp"
#include "delivery.hpp"
#include "garage.hpp"
#include "log.hpp"
#include "mod_bus.hpp"
#include "sa_sdk.hpp"
#include "tuning_physics.hpp"
#include "ui_menu.hpp"
#include "workshop.hpp"

namespace dealership {

namespace {

enum class VehClass : uint8_t {
    Supercars = 0,
    SportsTuners,
    MuscleLowriders,
    SedansCoupes,
    SuvOffroad,
    BikesQuads,
    VansTrucks,
    SpecialTactical,
    Boats,
    Aircraft,
    Count
};

struct ClassInfo {
    VehClass    id;
    const char* rus;
    const char* eng;
};

constexpr ClassInfo kClasses[] = {
    {VehClass::Supercars,       "Суперкары",           "Supercars"},
    {VehClass::SportsTuners,    "Спорт и Тюнинг",      "Sports & Tuners"},
    {VehClass::MuscleLowriders, "Маслкары и Классика", "Muscle & Lowriders"},
    {VehClass::SedansCoupes,    "Седаны и Купе",       "Sedans & Coupes"},
    {VehClass::SuvOffroad,      "Внедорожники",        "SUVs & Off-Road"},
    {VehClass::BikesQuads,      "Мото и Вело",         "Bikes & Quads"},
    {VehClass::VansTrucks,      "Фургоны и Грузовики", "Vans & Trucks"},
    {VehClass::SpecialTactical, "Эксклюзив и Спец.",   "Special & Tactical"},
    {VehClass::Boats,           "Лодки и Яхты",        "Boats & Yachts"},
    {VehClass::Aircraft,        "Авиация",             "Aircraft & Helis"},
};

struct CatalogVehicle {
    int32_t  modelId;
    VehClass vclass;
    int      basePrice; // High official GTA 5-style dealership price!
    bool     isLand;
};

// Official San Andreas Dealership Catalog (verified model IDs 400..611 from data/vehicles.ide!)
constexpr CatalogVehicle kCatalog[] = {
    // --- Supercars ---
    {411, VehClass::Supercars, 580000, true}, // Infernus
    {451, VehClass::Supercars, 540000, true}, // Turismo
    {541, VehClass::Supercars, 490000, true}, // Bullet
    {415, VehClass::Supercars, 420000, true}, // Cheetah
    {429, VehClass::Supercars, 360000, true}, // Banshee
    {506, VehClass::Supercars, 340000, true}, // Super GT
    {480, VehClass::Supercars, 290000, true}, // Comet
    {477, VehClass::Supercars, 265000, true}, // ZR-350
    {494, VehClass::Supercars, 650000, true}, // Hotring Racer
    {502, VehClass::Supercars, 680000, true}, // Hotring Racer A
    {503, VehClass::Supercars, 680000, true}, // Hotring Racer B

    // --- Sports & Tuners ---
    {562, VehClass::SportsTuners, 215000, true}, // Elegy
    {560, VehClass::SportsTuners, 225000, true}, // Sultan
    {559, VehClass::SportsTuners, 240000, true}, // Jester
    {587, VehClass::SportsTuners, 195000, true}, // Euros
    {402, VehClass::SportsTuners, 210000, true}, // Buffalo
    {603, VehClass::SportsTuners, 185000, true}, // Phoenix
    {602, VehClass::SportsTuners, 170000, true}, // Alpha
    {565, VehClass::SportsTuners, 155000, true}, // Flash
    {558, VehClass::SportsTuners, 145000, true}, // Uranus
    {561, VehClass::SportsTuners, 130000, true}, // Stratum
    {589, VehClass::SportsTuners, 125000, true}, // Club
    {496, VehClass::SportsTuners, 115000, true}, // Blista Compact
    {555, VehClass::SportsTuners, 190000, true}, // Windsor
    {533, VehClass::SportsTuners, 165000, true}, // Feltzer

    // --- Muscle & Lowriders ---
    {475, VehClass::MuscleLowriders, 110000, true}, // Sabre
    {434, VehClass::MuscleLowriders, 245000, true}, // Hotknife
    {535, VehClass::MuscleLowriders, 140000, true}, // Slamvan
    {536, VehClass::MuscleLowriders, 135000, true}, // Blade
    {567, VehClass::MuscleLowriders, 145000, true}, // Savanna
    {534, VehClass::MuscleLowriders, 150000, true}, // Remington
    {575, VehClass::MuscleLowriders, 125000, true}, // Broadway
    {576, VehClass::MuscleLowriders, 115000, true}, // Tornado
    {412, VehClass::MuscleLowriders, 120000, true}, // Voodoo
    {566, VehClass::MuscleLowriders,  95000, true}, // Tahoma
    {439, VehClass::MuscleLowriders,  98000, true}, // Stallion
    {518, VehClass::MuscleLowriders,  88000, true}, // Buccaneer
    {542, VehClass::MuscleLowriders,  65000, true}, // Clover
    {549, VehClass::MuscleLowriders,  68000, true}, // Tampa
    {474, VehClass::MuscleLowriders,  82000, true}, // Hermes
    {545, VehClass::MuscleLowriders, 135000, true}, // Hustler
    {491, VehClass::MuscleLowriders,  72000, true}, // Virgo

    // --- Sedans & Coupes ---
    {409, VehClass::SedansCoupes, 280000, true}, // Stretch (Limousine)
    {580, VehClass::SedansCoupes, 180000, true}, // Stafford
    {405, VehClass::SedansCoupes, 105000, true}, // Sentinel
    {445, VehClass::SedansCoupes,  92000, true}, // Admiral
    {507, VehClass::SedansCoupes,  88000, true}, // Elegant
    {551, VehClass::SedansCoupes,  86000, true}, // Merit
    {426, VehClass::SedansCoupes,  82000, true}, // Premier
    {421, VehClass::SedansCoupes,  85000, true}, // Washington
    {550, VehClass::SedansCoupes,  78000, true}, // Sunrise
    {540, VehClass::SedansCoupes,  74000, true}, // Vincent
    {585, VehClass::SedansCoupes,  76000, true}, // Emperor
    {492, VehClass::SedansCoupes,  62000, true}, // Greenwood
    {517, VehClass::SedansCoupes,  58000, true}, // Majestic
    {516, VehClass::SedansCoupes,  54000, true}, // Nebula
    {436, VehClass::SedansCoupes,  52000, true}, // Previon
    {526, VehClass::SedansCoupes,  55000, true}, // Fortune
    {527, VehClass::SedansCoupes,  48000, true}, // Cadrona
    {401, VehClass::SedansCoupes,  46000, true}, // Bravura
    {529, VehClass::SedansCoupes,  45000, true}, // Willard
    {547, VehClass::SedansCoupes,  44000, true}, // Primo
    {546, VehClass::SedansCoupes,  49000, true}, // Intruder
    {467, VehClass::SedansCoupes,  56000, true}, // Oceanic
    {466, VehClass::SedansCoupes,  52000, true}, // Glendale
    {404, VehClass::SedansCoupes,  38000, true}, // Perennial
    {458, VehClass::SedansCoupes,  50000, true}, // Solair
    {479, VehClass::SedansCoupes,  42000, true}, // Regina
    {419, VehClass::SedansCoupes,  46000, true}, // Esperanto
    {410, VehClass::SedansCoupes,  35000, true}, // Manana

    // --- SUVs & Off-Road ---
    {579, VehClass::SuvOffroad, 165000, true}, // Huntley
    {495, VehClass::SuvOffroad, 240000, true}, // Sandking
    {470, VehClass::SuvOffroad, 260000, true}, // Patriot
    {400, VehClass::SuvOffroad, 115000, true}, // Landstalker
    {489, VehClass::SuvOffroad, 108000, true}, // Rancher
    {500, VehClass::SuvOffroad,  95000, true}, // Mesa
    {554, VehClass::SuvOffroad, 118000, true}, // Yosemite
    {422, VehClass::SuvOffroad,  78000, true}, // Bobcat
    {600, VehClass::SuvOffroad,  74000, true}, // Picador
    {543, VehClass::SuvOffroad,  58000, true}, // Sadler
    {478, VehClass::SuvOffroad,  48000, true}, // Walton
    {424, VehClass::SuvOffroad, 110000, true}, // BF Injection
    {568, VehClass::SuvOffroad, 125000, true}, // Bandito
    {444, VehClass::SuvOffroad, 380000, true}, // Monster
    {556, VehClass::SuvOffroad, 400000, true}, // Monster A
    {557, VehClass::SuvOffroad, 400000, true}, // Monster B
    {573, VehClass::SuvOffroad, 420000, true}, // Dune

    // --- Bikes & Quads ---
    {522, VehClass::BikesQuads, 145000, true}, // NRG-500
    {521, VehClass::BikesQuads, 105000, true}, // FCR-900
    {461, VehClass::BikesQuads,  85000, true}, // PCJ-600
    {581, VehClass::BikesQuads,  78000, true}, // BF-400
    {468, VehClass::BikesQuads,  65000, true}, // Sanchez
    {463, VehClass::BikesQuads,  72000, true}, // Freeway
    {586, VehClass::BikesQuads,  68000, true}, // Wayfarer
    {471, VehClass::BikesQuads,  58000, true}, // Quadbike
    {462, VehClass::BikesQuads,  18000, true}, // Faggio
    {510, VehClass::BikesQuads,  12000, true}, // Mountain Bike
    {481, VehClass::BikesQuads,   9000, true}, // BMX
    {509, VehClass::BikesQuads,   8000, true}, // Bike

    // --- Vans & Trucks ---
    {482, VehClass::VansTrucks,  72000, true}, // Burrito
    {418, VehClass::VansTrucks,  64000, true}, // Moonbeam
    {413, VehClass::VansTrucks,  58000, true}, // Pony
    {440, VehClass::VansTrucks,  62000, true}, // Rumpo
    {483, VehClass::VansTrucks,  85000, true}, // Camper
    {508, VehClass::VansTrucks, 115000, true}, // Journey
    {498, VehClass::VansTrucks,  75000, true}, // Boxville
    {499, VehClass::VansTrucks,  88000, true}, // Benson
    {414, VehClass::VansTrucks,  92000, true}, // Mule
    {456, VehClass::VansTrucks,  98000, true}, // Yankee
    {455, VehClass::VansTrucks, 125000, true}, // Flatbed
    {403, VehClass::VansTrucks, 175000, true}, // Linerunner
    {514, VehClass::VansTrucks, 185000, true}, // Petrol Tanker
    {515, VehClass::VansTrucks, 210000, true}, // Roadtrain
    {525, VehClass::VansTrucks, 110000, true}, // Towtruck
    {578, VehClass::VansTrucks, 145000, true}, // DFT-30
    {443, VehClass::VansTrucks, 165000, true}, // Packer
    {437, VehClass::VansTrucks, 195000, true}, // Coach
    {431, VehClass::VansTrucks, 170000, true}, // Bus
    {406, VehClass::VansTrucks, 360000, true}, // Dumper
    {486, VehClass::VansTrucks, 240000, true}, // Dozer
    {531, VehClass::VansTrucks,  65000, true}, // Tractor

    // --- Special & Tactical ---
    {420, VehClass::SpecialTactical,  75000, true}, // Taxi
    {438, VehClass::SpecialTactical,  72000, true}, // Cabbie
    {596, VehClass::SpecialTactical, 180000, true}, // Police LS
    {597, VehClass::SpecialTactical, 180000, true}, // Police SF
    {598, VehClass::SpecialTactical, 180000, true}, // Police LV
    {599, VehClass::SpecialTactical, 210000, true}, // Police Ranger
    {523, VehClass::SpecialTactical, 165000, true}, // HPV-1000
    {490, VehClass::SpecialTactical, 290000, true}, // FBI Rancher
    {528, VehClass::SpecialTactical, 450000, true}, // FBI Truck
    {427, VehClass::SpecialTactical, 380000, true}, // Enforcer
    {601, VehClass::SpecialTactical, 520000, true}, // S.W.A.T.
    {433, VehClass::SpecialTactical, 340000, true}, // Barracks
    {407, VehClass::SpecialTactical, 260000, true}, // Firetruck
    {416, VehClass::SpecialTactical, 195000, true}, // Ambulance
    {428, VehClass::SpecialTactical, 320000, true}, // Securicar
    {571, VehClass::SpecialTactical,  85000, true}, // Kart
    {457, VehClass::SpecialTactical,  45000, true}, // Caddy
    {539, VehClass::SpecialTactical, 275000, true}, // Vortex
    {432, VehClass::SpecialTactical, 1800000, true}, // Rhino (Tank)

    // --- Boats & Yachts ---
    {493, VehClass::Boats, 420000, false}, // Jetmax
    {452, VehClass::Boats, 320000, false}, // Speeder
    {446, VehClass::Boats, 350000, false}, // Squalo
    {484, VehClass::Boats, 480000, false}, // Marquis
    {454, VehClass::Boats, 390000, false}, // Tropic
    {430, VehClass::Boats, 340000, false}, // Predator
    {472, VehClass::Boats, 260000, false}, // Coastguard
    {595, VehClass::Boats, 240000, false}, // Launch
    {453, VehClass::Boats, 150000, false}, // Reefer
    {473, VehClass::Boats,  85000, false}, // Dinghy

    // --- Aircraft & Helis ---
    {487, VehClass::Aircraft,  780000, false}, // Maverick
    {469, VehClass::Aircraft,  520000, false}, // Sparrow
    {447, VehClass::Aircraft,  680000, false}, // Sea Sparrow
    {563, VehClass::Aircraft,  840000, false}, // Raindance
    {417, VehClass::Aircraft,  960000, false}, // Leviathan
    {548, VehClass::Aircraft, 1150000, false}, // Cargobob
    {425, VehClass::Aircraft, 2200000, false}, // Hunter
    {593, VehClass::Aircraft,  380000, false}, // Dodo
    {513, VehClass::Aircraft,  550000, false}, // Stuntplane
    {511, VehClass::Aircraft,  460000, false}, // Beagle
    {476, VehClass::Aircraft,  690000, false}, // Rustler
    {512, VehClass::Aircraft,  410000, false}, // Cropduster
    {460, VehClass::Aircraft,  490000, false}, // Skimmer
    {553, VehClass::Aircraft,  890000, false}, // Nevada
    {519, VehClass::Aircraft, 1250000, false}, // Shamal
    {577, VehClass::Aircraft, 1950000, false}, // AT-400
    {520, VehClass::Aircraft, 2500000, false}, // Hydra
};
constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

// Curated showroom factory colors (Primary color ID + localized name)
struct PaintPreset {
    uint8_t     col1;
    uint8_t     col2;
    const char* rus;
    const char* eng;
};
constexpr PaintPreset kShowroomColors[] = {
    {  1,   1, "Белый",           "White"},
    {  0,   0, "Чёрный",          "Black"},
    {  3,   3, "Красный",         "Crimson Red"},
    {  2,   2, "Тёмно-синий",     "Midnight Blue"},
    { 79,  79, "Ярко-синий",      "Racing Blue"},
    { 16,  16, "Зелёный",         "Emerald Green"},
    {  6,   6, "Жёлтый",          "Solar Yellow"},
    { 11,  11, "Серебристый",     "Silver"},
    { 24,  24, "Графитовый",      "Graphite"},
    { 65,  65, "Золотистый",      "Gold"},
    {  5,   5, "Бордовый",        "Burgundy"},
    {126, 126, "Розовый",         "Hot Pink"},
};
constexpr int kShowroomColorCount = sizeof(kShowroomColors) / sizeof(kShowroomColors[0]);

// 3 Official San Andreas Car Dealership Showrooms (verified IPL coords!)
struct DealershipSite {
    const char* nameRus;
    const char* nameEng;
    float mx, my, mz;            // Walk-in trigger marker
    float px, py, pz, pHeading;  // Live 3D showroom preview spot
    float sx, sy, sz, sHeading;  // Post-purchase pickup spot
};

constexpr DealershipSite kSites[3] = {
    {"Coutt & Schutz (Лос-Сантос)", "Coutt & Schutz (Los Santos)",
      2131.8f, -1150.2f, 24.1f,
      2128.0f, -1139.0f, 25.0f, 210.0f,
      2135.5f, -1142.0f, 24.8f, 180.0f},
    {"Wang Cars (Сан-Фиерро)", "Wang Cars (San Fierro)",
     -1974.5f,   288.0f, 35.2f,
     -1963.0f,   293.5f, 35.5f, 115.0f,
     -1980.0f,   284.0f, 35.1f, 180.0f},
    {"Auto Bahn (Лас-Вентурас)", "Auto Bahn (Las Venturas)",
      1949.0f,  2068.8f, 10.8f,
      1958.0f,  2065.0f, 11.0f, 140.0f,
      1942.0f,  2078.0f, 10.8f, 270.0f},
};

enum class Screen : uint8_t {
    Closed = 0,
    Categories,
    VehicleList,
    ConfirmBuy
};

Screen   g_screen          = Screen::Closed;
bool     g_remoteMode      = false;
bool     g_openedFromPhone = false;
int      g_siteIdx         = -1;
int      g_catCursor       = 0;
int      g_vehCursor       = 0;
int      g_colorIdx        = 0;
int      g_confirmCursor   = 0;
uint32_t g_reenterBlockMs  = 0;

int32_t  g_previewVehHandle     = -1;
int32_t  g_previewModelId       = 0;
bool     g_previewFrozen        = false;
uint32_t g_previewSettleUntilMs = 0;
float    g_camAngle             = 0.7f;
float    g_camDist              = 5.6f;

bool     g_blipsPlaced          = false;
int32_t  g_siteBlips[3]         = {0, 0, 0};
int32_t  g_siteMarkers[3]       = {0, 0, 0};
float    g_siteMarkerZ[3]       = {24.1f, 35.2f, 10.8f};
bool     g_waitLeaveMarker      = false;
int      g_waitLeaveSiteIdx     = -1;

std::vector<const CatalogVehicle*> g_catList;

int ScaledPrice(int basePrice) {
    int p = static_cast<int>(std::round((static_cast<float>(basePrice) * mod::Config().dealershipPriceMult) / 500.0f)) * 500;
    return std::max(1000, p);
}

void RebuildClassList() {
    g_catList.clear();
    VehClass target = static_cast<VehClass>(std::clamp(g_catCursor, 0, static_cast<int>(VehClass::Count) - 1));
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (kCatalog[i].vclass == target) {
            g_catList.push_back(&kCatalog[i]);
        }
    }
    if (g_vehCursor >= static_cast<int>(g_catList.size())) g_vehCursor = 0;
}

void DestroySiteMarkers() {
    for (int i = 0; i < 3; ++i) {
        if (g_siteMarkers[i] > 0) {
            sa::ScriptCmd(0x03BD).Int(g_siteMarkers[i]).Run(); // REMOVE_SPHERE
            g_siteMarkers[i] = 0;
        }
    }
}

void DestroyPreviewVehicle() {
    if (g_previewVehHandle > 0) {
        sa::ScriptCmd(0x00A6).Int(g_previewVehHandle).Run(); // DELETE_CAR
        g_previewVehHandle = -1;
    }
    if (g_previewModelId >= 400) {
        sa::ScriptCmd(0x0249).Int(g_previewModelId).Run(); // MARK_MODEL_AS_NO_LONGER_NEEDED
        g_previewModelId = 0;
    }
    g_previewFrozen        = false;
    g_previewSettleUntilMs = 0;
}

void SpawnShowroomPreview(int32_t modelId) {
    if (g_remoteMode || g_siteIdx < 0 || g_siteIdx >= 3) return;
    if (g_previewModelId == modelId && g_previewVehHandle > 0 && sa::GetVehicleFromHandle(g_previewVehHandle)) {
        // Just refresh color!
        const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];
        sa::ScriptCmd(0x0229).Int(g_previewVehHandle).Int(pc.col1).Int(pc.col2).Run();
        return;
    }

    DestroyPreviewVehicle();
    if (modelId < 400 || modelId > 611) return;

    const auto& site = kSites[g_siteIdx];
    sa::ScriptCmd(0x04E4).Float(site.px).Float(site.py).Run(); // REQUEST_COLLISION
    sa::ScriptCmd(0x0247).Int(modelId).Run();                  // REQUEST_MODEL
    sa::LoadAllRequestedModels(false);

    float gz = site.pz;
    sa::ScriptCmd(0x02CE).Float(site.px).Float(site.py).Float(site.pz + 2.5f).Out(gz).Run();
    float z = (gz > site.pz - 6.0f && gz < site.pz + 6.0f) ? (gz + 0.14f) : (site.pz + 0.20f);

    int32_t vh = 0;
    sa::ScriptCmd(0x00A5).Int(modelId).Float(site.px).Float(site.py).Float(z).Out(vh).Run();
    if (vh > 0) {
        // Place cleanly via SET_CAR_COORDINATES & leave unfrozen briefly so suspension settles naturally onto pavement!
        sa::ScriptCmd(0x0519).Int(vh).Int(0).Run();
        sa::ScriptCmd(0x00AB).Int(vh).Float(site.px).Float(site.py).Float(z).Run();
        sa::ScriptCmd(0x0175).Int(vh).Float(site.pHeading).Run();
        sa::ScriptCmd(0x04BA).Int(vh).Float(0.0f).Run();
        const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];
        sa::ScriptCmd(0x0229).Int(vh).Int(pc.col1).Int(pc.col2).Run();
        sa::ScriptCmd(0x0878).Int(vh).Float(0.0f).Run(); // clean shine
        sa::ScriptCmd(0x020A).Int(vh).Int(2).Run();      // locked preview
        sa::ScriptCmd(0x02AC).Int(vh).Int(1).Int(1).Int(1).Int(1).Int(1).Run(); // proofs
        g_previewVehHandle     = vh;
        g_previewModelId       = modelId;
        g_previewFrozen        = false;
        g_previewSettleUntilMs = sa::GetTimeMs() + 450;
    } else {
        sa::ScriptCmd(0x0249).Int(modelId).Run();
    }
}

void CloseDealership(bool returnToPhone) {
    bool wasShowroom = (!g_remoteMode && g_screen != Screen::Closed);
    bool goPhone     = returnToPhone && g_openedFromPhone;
    int  prevSite    = g_siteIdx;

    DestroyPreviewVehicle();
    g_screen          = Screen::Closed;
    g_remoteMode      = false;
    g_openedFromPhone = false;
    g_siteIdx         = -1;
    g_reenterBlockMs  = sa::GetTimeMs() + 1800;
    if (wasShowroom && prevSite >= 0 && prevSite < 3) {
        g_waitLeaveMarker  = true;
        g_waitLeaveSiteIdx = prevSite;
    }

    if (wasShowroom) {
        sa::ScriptCmd(0x02EB).Run();             // RESTORE_CAMERA_JUMPCUT
        sa::ScriptCmd(0x01B4).Int(0).Int(1).Run(); // SET_PLAYER_CONTROL true
    }

    if (goPhone) {
        if (auto* b = modbus::GetBus(); b && b->phoneInstalled && b->phoneReturnToMain) {
            b->phoneReturnToMain();
        }
    }
}

void OpenShowroomAt(int siteIdx) {
    if (siteIdx < 0 || siteIdx >= 3) return;
    DestroySiteMarkers();
    g_siteIdx         = siteIdx;
    g_remoteMode      = false;
    g_openedFromPhone = false;
    g_screen          = Screen::Categories;
    g_catCursor       = 0;
    g_vehCursor       = 0;
    g_colorIdx        = 0;
    g_camAngle        = 0.75f;
    g_camDist         = 5.8f;

    sa::ScriptCmd(0x01B4).Int(0).Int(0).Run(); // Freeze player during showroom browse
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void CompletePurchase(const CatalogVehicle* cv) {
    if (!cv) return;
    int price = ScaledPrice(cv->basePrice);
    if (sa::GetPlayerMoney() < price) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Недостаточно денег для покупки!", "~r~Not enough money to purchase!");
        return;
    }

    int freeSlot = garage::FindFreeSlot();
    if (freeSlot < 0) {
        sa::PlayFrontendSound(sa::SFX_ERROR);
        ui::ShowHelp("~r~Личный гараж полон (10/10)! Освободите слот.", "~r~Personal garage is full (10/10)!");
        return;
    }

    const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];

    // Charge money & register factory-fresh vehicle (0% wear!) into Personal Garage slot!
    sa::AddPlayerMoney(-price);
    auto& slot = garage::SlotsMutable()[freeSlot];
    slot                 = mod::OwnedVehicle{};
    slot.occupied        = true;
    slot.destroyed       = false;
    slot.modelId         = cv->modelId;
    slot.displayNameGame = sa::GetVehicleDisplayNameGameEncoded(cv->modelId);
    slot.visuals.primaryColor   = pc.col1;
    slot.visuals.secondaryColor = pc.col2;
    slot.visuals.paintjob       = -1;
    slot.wear            = mod::VehicleWear{}; // Brand new, 0% wear!
    garage::SaveNow();

    sa::PlayFrontendSound(sa::SFX_BUY);

    int purchasedSite = g_siteIdx;
    bool wasRemote    = g_remoteMode;
    CloseDealership(false);

    if (!wasRemote && purchasedSite >= 0 && purchasedSite < 3 && cv->isLand) {
        // Spawn the freshly purchased land vehicle right outside the dealership showroom!
        const auto& site = kSites[purchasedSite];
        sa::CVehicle* playerCurVeh = sa::FindPlayerVehicle(0, false);
        garage::DismissCurrentPersonalVehicle(garage::GetActivePersonalVehicle() != playerCurVeh);

        sa::ScriptCmd(0x0247).Int(cv->modelId).Run();
        sa::LoadAllRequestedModels(false);

        float gz = site.sz;
        sa::ScriptCmd(0x02CE).Float(site.sx).Float(site.sy).Float(site.sz + 2.5f).Out(gz).Run();
        float z = (gz > site.sz - 6.0f && gz < site.sz + 6.0f) ? (gz + 0.12f) : (site.sz + 0.18f);

        int32_t vh = 0;
        sa::ScriptCmd(0x00A5).Int(cv->modelId).Float(site.sx).Float(site.sy).Float(z).Out(vh).Run();
        sa::ScriptCmd(0x0249).Int(cv->modelId).Run();

        if (sa::CVehicle* v = sa::GetVehicleFromHandle(vh)) {
            sa::ScriptCmd(0x00AB).Int(vh).Float(site.sx).Float(site.sy).Float(z).Run();
            sa::ScriptCmd(0x0175).Int(vh).Float(site.sHeading).Run();
            v->MoveSpeed() = {0.0f, 0.0f, 0.0f};
            garage::ApplySavedVehicleConfig(v, slot);
            int32_t blip = 0;
            sa::ScriptCmd(0x0186).Int(vh).Out(blip).Run();
            if (blip > 0) {
                sa::ScriptCmd(0x0165).Int(blip).Int(5).Run();
                sa::ScriptCmd(0x0168).Int(blip).Int(2).Run();
            }
            garage::SetActivePersonalVehicle(v, freeSlot, blip);
            ui::ShowHelp("~g~Поздравляем с покупкой!~w~ Ваш новый транспорт ждёт на стоянке (отмечен на карте).",
                         "~g~Congratulations!~w~ Your new vehicle is waiting outside (marked on radar).");
            return;
        }
    }

    // Remote purchase (or non-land craft): immediately deliver land vehicles or confirm garage registration!
    if (wasRemote && cv->isLand && sa::GetActiveInterior() == 0) {
        if (delivery::StartDeliveryForSlot(freeSlot)) {
            return;
        }
    }

    char rusBuf[160], engBuf[160];
    std::snprintf(rusBuf, sizeof(rusBuf),
                  "~g~Куплено и оформлено в гараж (слот #%d)!~w~ Вызовите через Механика.",
                  freeSlot + 1);
    std::snprintf(engBuf, sizeof(engBuf),
                  "~g~Purchased & registered to Garage Slot #%d!~w~ Call Mechanic anytime.",
                  freeSlot + 1);
    ui::ShowHelp(rusBuf, engBuf);
}

void EnsureDealershipBlips() {
    if (g_blipsPlaced || !mod::Config().enableDealership) return;
    if (sa::GetTimeMs() < 1500) return;

    // Count active radar blips first to stay safely under 175 limit
    const uint8_t* base = reinterpret_cast<const uint8_t*>(0x00BA86F0);
    int used = 0;
    for (int i = 0; i < 175; ++i) {
        if ((base[i * 0x28 + 0x25] & 0x02) != 0) ++used;
    }
    if (used >= 164) return;

    for (int i = 0; i < 3; ++i) {
        if (g_siteBlips[i] > 0) continue;
        // Check for existing sprite 55 blip nearby
        bool exists = false;
        for (int b = 0; b < 175; ++b) {
            const uint8_t* e = base + b * 0x28;
            if ((e[0x25] & 0x02) == 0 || e[0x24] != 55) continue;
            float bx = *reinterpret_cast<const float*>(e + 0x08);
            float by = *reinterpret_cast<const float*>(e + 0x0C);
            float dx = bx - kSites[i].mx, dy = by - kSites[i].my;
            if (dx * dx + dy * dy < 1600.0f) { exists = true; break; }
        }
        if (!exists) {
            int32_t bh = 0;
            sa::ScriptCmd(0x04CE)
                .Float(kSites[i].mx).Float(kSites[i].my).Float(kSites[i].mz)
                .Int(55) // RADAR_SPRITE_IMPOUND (Car Dealership icon!)
                .Out(bh).Run();
            g_siteBlips[i] = bh;
        }
    }
    g_blipsPlaced = true;
}

void UpdateShowroomCamera() {
    if (g_remoteMode || g_siteIdx < 0 || g_siteIdx >= 3) return;
    const auto& site = kSites[g_siteIdx];

    float dt = std::clamp(sa::GetTimeStep() / 50.0f, 0.001f, 0.05f);
    const auto& k = ui::Keys();
    if (k.IsDown('Q'))      g_camAngle -= dt * 1.4f;
    else if (k.IsDown('E')) g_camAngle += dt * 1.4f;
    else                    g_camAngle += dt * 0.22f; // gentle showroom turntable rotation!

    auto* ms = sa::GetNewMouseState();
    if (ms && ms->wheelUp)   { g_camDist = std::clamp(g_camDist - 0.4f, 3.2f, 9.5f); ms->wheelUp = 0; }
    if (ms && ms->wheelDown) { g_camDist = std::clamp(g_camDist + 0.4f, 3.2f, 9.5f); ms->wheelDown = 0; }

    sa::CVector focus{site.px, site.py, site.pz + 0.45f};
    if (sa::CVehicle* pv = sa::GetVehicleFromHandle(g_previewVehHandle)) {
        focus = pv->GetPosition();
        focus.z += 0.35f;
    }

    float camX = focus.x + std::cos(g_camAngle) * g_camDist;
    float camY = focus.y + std::sin(g_camAngle) * g_camDist;
    float camZ = focus.z + 1.45f;

    sa::ScriptCmd(0x015F).Float(camX).Float(camY).Float(camZ).Float(0.0f).Float(0.0f).Float(0.0f).Run();
    sa::ScriptCmd(0x0160).Float(focus.x).Float(focus.y).Float(focus.z).Int(2).Run();
}

void HandleMenuInput() {
    const auto& k = ui::Keys();
    const bool up     = k.Repeated(VK_UP) || k.Repeated('W');
    const bool down   = k.Repeated(VK_DOWN) || k.Repeated('S');
    const bool left   = k.JustPressed(VK_LEFT) || k.JustPressed('A');
    const bool right  = k.JustPressed(VK_RIGHT) || k.JustPressed('D');
    const bool accept = k.JustPressed(VK_RETURN) || k.JustPressed(VK_SPACE);
    const bool back   = k.JustPressed(VK_BACK) || k.JustPressed(VK_ESCAPE) || k.JustPressed('F');

    sa::ClearPadMenuInputs();

    if (g_screen == Screen::Categories) {
        const int n = static_cast<int>(VehClass::Count);
        if (up) {
            g_catCursor = (g_catCursor - 1 + n) % n;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (down) {
            g_catCursor = (g_catCursor + 1) % n;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (accept) {
            g_vehCursor = 0;
            RebuildClassList();
            g_screen = Screen::VehicleList;
            if (!g_catList.empty()) {
                SpawnShowroomPreview(g_catList[0]->modelId);
            }
            sa::PlayFrontendSound(sa::SFX_SELECT);
        } else if (back) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            CloseDealership(true);
        }
        return;
    }

    if (g_screen == Screen::VehicleList) {
        const int n = static_cast<int>(g_catList.size());
        if (n > 0) {
            if (up) {
                g_vehCursor = (g_vehCursor - 1 + n) % n;
                SpawnShowroomPreview(g_catList[g_vehCursor]->modelId);
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            } else if (down) {
                g_vehCursor = (g_vehCursor + 1) % n;
                SpawnShowroomPreview(g_catList[g_vehCursor]->modelId);
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            } else if (left || right) {
                g_colorIdx = (g_colorIdx + (right ? 1 : -1) + kShowroomColorCount) % kShowroomColorCount;
                SpawnShowroomPreview(g_catList[g_vehCursor]->modelId);
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            } else if (accept) {
                g_confirmCursor = 0;
                g_screen = Screen::ConfirmBuy;
                sa::PlayFrontendSound(sa::SFX_SELECT);
            }
        }
        if (back) {
            DestroyPreviewVehicle();
            g_screen = Screen::Categories;
            sa::PlayFrontendSound(sa::SFX_BACK);
        }
        return;
    }

    if (g_screen == Screen::ConfirmBuy) {
        if (up || down) {
            g_confirmCursor = 1 - g_confirmCursor;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (back) {
            g_screen = Screen::VehicleList;
            sa::PlayFrontendSound(sa::SFX_BACK);
        } else if (accept) {
            if (g_confirmCursor == 0 && g_vehCursor >= 0 && g_vehCursor < static_cast<int>(g_catList.size())) {
                CompletePurchase(g_catList[g_vehCursor]);
            } else {
                g_screen = Screen::VehicleList;
                sa::PlayFrontendSound(sa::SFX_BACK);
            }
        }
    }
}

} // namespace

void Tick() {
    if (!mod::Config().enableDealership || sa::GetGameState() != 9) return;
    if (sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) {
        if (g_screen != Screen::Closed) CloseDealership(false);
        return;
    }

    EnsureDealershipBlips();

    if (g_screen != Screen::Closed) {
        // Settle showroom preview car onto its natural suspension before freezing!
        if (!g_remoteMode && !g_previewFrozen && g_previewVehHandle > 0) {
            if (sa::CVehicle* pv = sa::GetVehicleFromHandle(g_previewVehHandle)) {
                pv->MoveSpeed().x = 0.0f;
                pv->MoveSpeed().y = 0.0f;
                if (pv->MoveSpeed().z > 0.0f) pv->MoveSpeed().z = 0.0f;
                if (sa::GetTimeMs() >= g_previewSettleUntilMs) {
                    sa::ScriptCmd(0x04BA).Int(g_previewVehHandle).Float(0.0f).Run();
                    sa::ScriptCmd(0x0519).Int(g_previewVehHandle).Int(1).Run(); // FREEZE_CAR_POSITION true once tires sit flush!
                    g_previewFrozen = true;
                }
            }
        }
        UpdateShowroomCamera();
        HandleMenuInput();
        return;
    }

    // Classic GTA SA Red Cylinder Walk-In Markers at the 3 San Andreas Car Dealerships
    if (sa::GetActiveInterior() != 0 || workshop::IsWorkshopActive() || delivery::IsMenuOpen() ||
        modbus::IsAnyModalMenuOpen("garage")) {
        DestroySiteMarkers();
        return;
    }

    sa::CPed* ped = sa::FindPlayerPed(0);
    if (!sa::IsPedValid(ped) || ped->GetHealth() <= 0.0f) {
        DestroySiteMarkers();
        return;
    }

    sa::CVector pPos = ped->GetPosition();

    // Clear exit-marker guard once player steps away from the marker they just left
    if (g_waitLeaveMarker) {
        if (g_waitLeaveSiteIdx < 0 || g_waitLeaveSiteIdx >= 3) {
            g_waitLeaveMarker = false;
        } else {
            const auto& ws = kSites[g_waitLeaveSiteIdx];
            if (pPos.Dist2D({ws.mx, ws.my, ws.mz}) > 2.6f) {
                g_waitLeaveMarker  = false;
                g_waitLeaveSiteIdx = -1;
            }
        }
    }

    for (int i = 0; i < 3; ++i) {
        const auto& s = kSites[i];
        float d = pPos.Dist2D({s.mx, s.my, s.mz});

        // Manage classic native GTA SA red marker cylinder (ADD_SPHERE 0x03BC / REMOVE_SPHERE 0x03BD)
        if (d < 120.0f) {
            if (g_siteMarkers[i] <= 0) {
                float gz = s.mz;
                sa::ScriptCmd(0x02CE).Float(s.mx).Float(s.my).Float(s.mz + 2.5f).Out(gz).Run();
                float mz = (gz > s.mz - 5.0f && gz < s.mz + 5.0f) ? (gz + 0.05f) : s.mz;
                g_siteMarkerZ[i] = mz;
                int32_t sh = 0;
                sa::ScriptCmd(0x03BC).Float(s.mx).Float(s.my).Float(mz).Float(1.15f).Out(sh).Run();
                g_siteMarkers[i] = sh;
            }
        } else if (d > 140.0f && g_siteMarkers[i] > 0) {
            sa::ScriptCmd(0x03BD).Int(g_siteMarkers[i]).Run();
            g_siteMarkers[i] = 0;
        }

        // Automatically enter dealership showroom when walking into the red marker on foot!
        if (!ped->IsInVehicle() && !g_waitLeaveMarker &&
            d < 1.35f && std::fabs(pPos.z - g_siteMarkerZ[i]) < 2.2f &&
            sa::GetTimeMs() >= g_reenterBlockMs) {
            OpenShowroomAt(i);
            return;
        }
    }
}

void Render2D() {
    if (g_screen == Screen::Closed || sa::IsPauseMenuActive()) return;

    const int32_t money    = sa::GetPlayerMoney();
    const int     occupied = garage::CountOccupied();
    const int     maxVeh   = std::min(mod::Config().maxVehicles, mod::MAX_OWNED_VEHICLES);

    ui::MenuDrawSpec spec;
    spec.x640           = 24.0f;
    spec.y448           = 72.0f;
    spec.w640           = 206.0f;
    spec.maxVisibleRows = 10;

    if (g_screen == Screen::Categories) {
        spec.titleGame = (!g_remoteMode && g_siteIdx >= 0)
            ? ui::Loc(kSites[g_siteIdx].nameRus, kSites[g_siteIdx].nameEng)
            : ui::Loc("АВТОСАЛОН ОНЛАЙН", "ONLINE DEALERSHIP");
        spec.colLeftGame = ui::Loc("КЛАСС ТРАНСПОРТА", "VEHICLE CLASS");
        char gbuf[32];
        std::snprintf(gbuf, sizeof(gbuf), "%s %d/%d", ui::Loc("Гараж", "Garage").c_str(), occupied, maxVeh);
        spec.colRightGame  = gbuf;
        spec.footerGame    = ui::Loc("Enter - Открыть | Backspace - Выход", "Enter - Open | Backspace - Exit");
        spec.selectedIndex = g_catCursor;

        for (int i = 0; i < static_cast<int>(VehClass::Count); ++i) {
            int cnt = 0;
            for (size_t k = 0; k < kCatalogCount; ++k) {
                if (kCatalog[k].vclass == kClasses[i].id) ++cnt;
            }
            ui::MenuRow r;
            r.leftGame   = ui::Loc(kClasses[i].rus, kClasses[i].eng);
            r.rightGame  = std::to_string(cnt);
            r.rightColor = {195, 195, 195, 255};
            spec.rows.push_back(std::move(r));
        }
    } else if (g_screen == Screen::VehicleList) {
        const auto& ci = kClasses[std::clamp(g_catCursor, 0, static_cast<int>(VehClass::Count) - 1)];
        const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];
        spec.titleGame     = ui::Loc(ci.rus, ci.eng);
        spec.colLeftGame   = ui::Loc("МОДЕЛЬ", "MODEL");
        spec.colRightGame  = ui::Loc(pc.rus, pc.eng);
        spec.footerGame    = g_remoteMode
            ? ui::Loc("Enter - Купить | <-/-> Цвет | Backspace - Назад", "Enter - Buy | <-/-> Color | Backspace - Back")
            : ui::Loc("Enter - Купить | <-/-> Цвет | Q/E Кам. | F - Назад", "Enter - Buy | <-/-> Color | Q/E Cam | F - Back");
        spec.selectedIndex = g_vehCursor;

        for (const auto* cv : g_catList) {
            int price = ScaledPrice(cv->basePrice);
            ui::MenuRow r;
            r.leftGame   = sa::GetVehicleDisplayNameGameEncoded(cv->modelId);
            r.rightGame  = ui::FormatPrice(price);
            r.rightColor = (money >= price) ? sa::CRGBA{125, 235, 135, 255} : sa::CRGBA{235, 95, 95, 255};
            spec.rows.push_back(std::move(r));
        }
    } else if (g_screen == Screen::ConfirmBuy && g_vehCursor >= 0 && g_vehCursor < static_cast<int>(g_catList.size())) {
        const auto* cv = g_catList[g_vehCursor];
        int price = ScaledPrice(cv->basePrice);
        const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];
        spec.titleGame     = ui::Loc("ОФОРМЛЕНИЕ ПОКУПКИ", "CONFIRM PURCHASE");
        spec.colLeftGame   = sa::GetVehicleDisplayNameGameEncoded(cv->modelId);
        spec.colRightGame  = ui::Loc(pc.rus, pc.eng);
        spec.footerGame    = ui::Loc("Enter - Подтвердить | Backspace - Назад", "Enter - Confirm | Backspace - Back");
        spec.selectedIndex = g_confirmCursor;

        bool canBuy = (money >= price) && (garage::FindFreeSlot() >= 0);
        spec.rows.push_back({ui::Loc("Купить официально", "Buy Officially"), ui::FormatPrice(price), canBuy, false,
                             canBuy ? sa::CRGBA{125, 235, 135, 255} : sa::CRGBA{235, 95, 95, 255}});
        spec.rows.push_back({ui::Loc("Отмена", "Cancel"), "", true, false, {200, 200, 200, 255}});
    }

    ui::DrawVanillaMenu(spec);

    // Live Vehicle StatsCard on the right whenever browsing vehicles!
    if ((g_screen == Screen::VehicleList || g_screen == Screen::ConfirmBuy) &&
        g_vehCursor >= 0 && g_vehCursor < static_cast<int>(g_catList.size())) {
        const auto* cv = g_catList[g_vehCursor];
        auto stats = tuning::ComputeStatsForModel(cv->modelId, mod::PerformanceTuning{}, mod::VehicleWear{});
        const auto& pc = kShowroomColors[g_colorIdx % kShowroomColorCount];
        std::string note = ui::Loc("Новый (Износ 0%) | ", "Factory New (0% Wear) | ") + ui::Loc(pc.rus, pc.eng);
        ui::DrawVehicleStatsPanel(sa::GetVehicleDisplayNameGameEncoded(cv->modelId), stats, stats, true, note, nullptr);
    }
}

void OpenRemote(bool fromPhone) {
    if (!mod::Config().enableDealership) {
        ui::ShowHelp("~r~Автосалон отключён в настройках.", "~r~Dealership is disabled in config.");
        return;
    }
    if (workshop::IsWorkshopActive() || sa::IsPauseMenuActive() || sa::IsCutsceneRunning()) return;
    g_siteIdx         = -1;
    g_remoteMode      = true;
    g_openedFromPhone = fromPhone;
    g_screen          = Screen::Categories;
    g_catCursor       = 0;
    g_vehCursor       = 0;
    g_colorIdx        = 0;
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

bool IsActive() { return g_screen != Screen::Closed; }

void Reset() {
    DestroyPreviewVehicle();
    DestroySiteMarkers();
    g_screen           = Screen::Closed;
    g_remoteMode       = false;
    g_openedFromPhone  = false;
    g_siteIdx          = -1;
    g_blipsPlaced      = false;
    g_waitLeaveMarker  = false;
    g_waitLeaveSiteIdx = -1;
    for (int i = 0; i < 3; ++i) {
        g_siteBlips[i]   = 0;
        g_siteMarkers[i] = 0;
    }
}

} // namespace dealership
