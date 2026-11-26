# GTA San Andreas — Modular GTA 5 Style Expansion Suite (6 ASI Plugins)

A modular suite of **6 independent C++20 ASI plugins** for **GTA San Andreas 1.0 US (Hoodlum)** that brings core **GTA 5** and **ETS 2** gameplay mechanics into classic GTA SA while staying authentic to the vanilla aesthetic — with **zero third-party mod dependencies** beyond **Ultimate ASI Loader**.

Install **only the modules you want** — every plugin automatically discovers and integrates with the others at runtime via the shared C-ABI service bus (`mod_bus.hpp`), with zero load-order dependencies.

---

## Requirements & Compatibility

### Requirements
- **GTA San Andreas 1.0 US (Hoodlum)**
- **Ultimate ASI Loader** (and optionally **ModLoader** for easy folder-based installation)
- **No other mod dependencies** (no CLEO, no MoonLoader, no external SDK runtimes required).

### Supported & Tested Mods
All plugins are designed to work cleanly alongside:
- **SilentPatch**
- **GTA SA COOP (`Karimov-Akbar/sa-coop`)** — both Singleplayer and Co-op supported
- **Widescreen Fix** (latest version)
- **Proper Shaders**
- **Proper Vehicle Damage**
- **ModelExtras**
- **MSKL Russificator** (and SanLtd Russian font tables, alongside full English support)

---

## Modular Structure

| Module | Folder in `modloader/` | Description |
|---|---|---|
| **A. [GTA5UIKit](ModDev_UIKit/README.md)** | `GTA5UIKit/` | Shared 3-tier React-style dumb-component UI Kit (`ThemeTokens` → `ui::prim::*` → `ListMenu`, `StatsCard`, `PhoneFrame`, `HelpToast`, `ColorGrid`, `TextInputModal`, `RadialWheel`) + inter-mod addon bus (`mod_bus.hpp`) + **complete visual restyling of vanilla GTA SA menus & notifications**: barber shops, tattoo parlors, gyms, betting shops, color grids, and **all top-left notifications, prompts & story phone calls** (`CHud::DrawHelpText`) in one unified style without altering vanilla gameplay logic. |
| **B. [GTA5AmmuNation](ModDev_GTA5Weapons/README.md)** | `GTA5AmmuNation/` | GTA 5-style weapon customization in Ammu-Nation (silencer, compensator, extended/drum magazines, optics with smooth RMB zoom, tactical grip, heavy barrel, 3D laser sight, optional 3D weapon attachments), custom synthesized audio (`bass.dll`), **multi-weapon storage per category**, and an **8-sector Weapon Wheel** (`Mouse Wheel` / `TAB`, **no slow-motion** for 100% SA-COOP safety). When `GTA5Inventory` is installed, you can also buy **spare body armor** directly inside Ammu-Nation. |
| **C. [GTA5GarageAndTuning](ModDev_GTA5Garage/README.md)** | `GTA5GarageAndTuning/` | Deep performance & visual tuning across all 5 workshops, **10 personal vehicles**, road-network **Mechanic delivery** (`F6` standalone or via Phone), an **ETS 2-style component wear & servicing system** (engine, transmission, brakes, tires, suspension, chassis degradation with red loss bars on the stats card, expensive workshop servicing, heavy wear from crashes & explosions), and **official dealerships in all 3 cities** with classic red entry markers (Coutt & Schutz LS, Wang Cars SF, Auto Bahn LV) + online car purchasing via Phone. |
| **D. [GTA5Inventory](ModDev_Inventory/README.md)** | `GTA5Inventory/` | **Takeaway restaurants** (Burger Shot, Cluckin' Bell, Well Stacked Pizza, diners): buy snacks & meals **into your pocket** up to carry limits (P's & Q's, EgoChaser, Meteorite, Sprunk, signature meals, healthy salad) + **spare body armor** (4 classes). Standalone `F7` menu (automatically hidden and embedded inside `GTA5Phone` when installed). |
| **E. [GTA5Phone](ModDev_Phone/README.md)** | `GTA5Phone/` | Authentic 1990s keypad **Phone (`F6`)** with a modular addon hub architecture, unified **Mouse Wheel / LMB / RMB / WASD / Arrow** controls (with full CJ movement & combat lock), native handset animation (`0x0729`), automatic holstering under gunfire, and **Phone Settings** (4 retro LCD themes, 4 synthesized ringtones, keypad beeps, silent mode, animation toggle, auto-outfit rules). Unifies **Contacts** (Mechanic, "Old Friend" removing up to 3 wanted stars every 10 min with shared SA-COOP cooldown, Insurance, EMS), **Food & Armor**, **CJ Wardrobe** (part-by-part dressing + 8 auto-equip outfits + restyled clothing stores), **Online Dealership**, and quick launch of the **SA-COOP Player List** (`F2`). |
| **F. [GTA5EarlyUnlock](ModDev_EarlyUnlock/README.md)** | `GTA5EarlyUnlock/` | Standalone early-game unlocker opening all workshops, dealerships, Ammu-Nation stores, clothing stores, barber shops, tattoo parlors, gyms, and diners from the very start of the game, with duplicate-free radar icons. |

---

## GTA SA COOP Compatibility (`Karimov-Akbar/sa-coop`)

- **Isolated per-player profiles:** In singleplayer (and for the Host on save slots `1..8`), progress is tied to `GTASAsf1..8.b`. Joining SA-COOP players automatically get an isolated profile `*_coop_<NicknameFromCoopIni>.ini` (or `[Coop] ProfileName=` in `GTA5UIKit.ini`) inside `Documents\GTA San Andreas User Files\GTA5*Saves\`. Every player has their own 10 personal vehicles, weapon attachments, pocket snacks/armor, wardrobe items, and 8 outfits.
- **Co-op safety guards:** Input protection when SA-COOP overlays/chat (`F9`, `F2`, `F4`, `T`) are open, `DELETE_CAR` guard while another player is driving, `CClothes::RebuildPlayer` guard for custom `F4` skins, zero slow-motion in the weapon wheel, and a shared 10-minute cooldown for the "Old Friend" wanted-level contact.

---

## Quick Install

Extract the desired module folders directly into `Grand Theft Auto San Andreas\modloader\`:
```text
Grand Theft Auto San Andreas/
└── modloader/
    ├── GTA5UIKit/
    ├── GTA5AmmuNation/
    ├── GTA5GarageAndTuning/
    ├── GTA5Inventory/
    ├── GTA5Phone/
    └── GTA5EarlyUnlock/
```

All settings (language `rus`/`eng` with full **MSKL Russificator** & English support, UI scale, speed units `mph`/`kmh`, keybinds, prices, wear & servicing multipliers, SA-COOP player-list hotkey, vanilla menu restyling toggles) are configurable in each module's `.ini` file.

---

## Building from Source

Requires 32-bit MinGW-w64 (`pacman -S mingw-w64-i686-gcc` in MSYS2).

1. *(Optional, for automatic `modloader` deployment)* Copy `local.config.example.ps1` to `local.config.ps1` and set `$GameDir` to your GTA San Andreas directory. You can also set the `GTASA_DIR` environment variable or pass `-GameDir`.
2. Build all 6 plugins:
```powershell
# Compile all 6 .asi plugins, auto-deploy to modloader, and package release ZIPs in dist\
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1

# Fast compile & deploy to modloader without zipping
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -SkipZip

# Compile & package ZIPs only (skip modloader deployment)
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -NoDeploy
```

For full releases, `publish-release.ps1` automates the entire pipeline in one command: build → auto-deploy to `modloader` → `git push` → publish GitHub Release.

---

## License

[MIT](LICENSE)
