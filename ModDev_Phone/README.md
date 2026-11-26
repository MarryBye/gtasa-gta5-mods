# GTA5Phone — Modular 90s Phone Hub (F6), Contacts, Wardrobe & Clothing Shops

Module **E** of the modular ASI suite — a GTA 5-inspired, expandable 1990s keypad **Phone (`F6`)** acting as the central hub connecting every mod in the suite:

- **Automatic Addon Integration via `mod_bus.hpp`:**
  - With `GTA5GarageAndTuning` installed, the phone unlocks **Mechanic**, **Online Dealership**, and **Insurance**.
  - With `GTA5Inventory` installed, the phone integrates **Food & Armor** and automatically disables the standalone `F7` key.
  - With `GTA5AmmuNation` installed, the phone displays your **Arsenal** status.
  - Other or third-party mods can register custom apps, contacts, and quick actions via `modbus::RegisterPhoneAddon()`.
- **Authentic Handset Experience:**
  - Native CJ handset animation (`0x0729`), automatic holstering when taking gunfire, full CJ movement/combat input lock while browsing, and unified Mouse Wheel / LMB / RMB / WASD / Arrow navigation.
  - **Phone Settings:** 4 retro LCD color themes (`Classic SA`, `Nokia '92`, `Amber '90s`, `Night Blue`), 4 synthesized ringtones (`bass.dll`), keypad tone toggle, silent mode, animation toggle, and auto-outfit rules.
  - Storyline phone calls (`PED_ANSWER_PHONE`) always keep Priority 1.
- **Contacts:**
  - **Old Friend:** Removes up to 3 wanted stars once every 10 minutes (with a shared cooldown under GTA SA COOP).
  - **Mechanic / Dealership / Insurance / EMS (Paramedic)**.
- **SA-COOP Players:** Quick launch shortcut for the GTA SA COOP player list (`F2`, configurable in INI).
- **CJ Wardrobe & Outfits:**
  - Piece-by-piece dressing from owned clothing (torso, legs, shoes, chains, watches, glasses, hats, special suits), safely guarded for custom `F4` SA-COOP skins.
  - Up to **8 saved outfits** with automatic outfit-switching rules (**On Mission**, **Freeplay**, **Wanted**, **Manual**).
  - Complete UI Kit restyling of all 6 clothing store chains (**Binco**, **ProLaps**, **Sub Urban**, **ZIP**, **Victim**, **Didier Sachs**) with **`Owned`** badges and interactive 3D camera orbit/zoom preview.

## Configuration (`GTA5Phone.ini`)
Configure language (`rus`/`eng`), `F6` hotkey, SA-COOP player list key, Old Friend cooldown/pricing, EMS cost, default LCD theme, and audio settings in [`config/GTA5Phone.ini`](config/GTA5Phone.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5Phone
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5Phone.asi
```
