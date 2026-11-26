# GTA5Inventory — Restaurant Take-Out, Pocket Snacks & Spare Body Armor

Module **D** of the modular ASI suite — player pocket inventory for GTA San Andreas 1.0 US (Hoodlum):

- **Takeaway Restaurants:** Inside Burger Shot, Cluckin' Bell, Well Stacked Pizza, and diners, players open a take-out menu to buy snacks and meals **directly into their pocket** (P's & Q's candy, EgoChaser chocolate, Meteorite bar, Sprunk, signature meals, and healthy salad with zero fat gain) up to carry limits, including a "Refill All" action.
- **Spare Body Armor:** 4 tiers of spare body armor (25%, 50%, 75%, 100%) managed by a weight-based carry capacity — purchasable inside Ammu-Nation (via `mod_bus.hpp` integration with `GTA5AmmuNation`) and equippable anytime on the go.
- **Standalone Mode (`F7`) & Phone Integration:** When `GTA5Phone.asi` is not installed, opens standalone on `F7`. When `GTA5Phone.asi` is present, `F7` is automatically disabled and Food & Armor seamlessly embeds into the Phone (`F6`).
- **SA-COOP & Per-Save Persistence:** Stores pocket snacks and spare armor per save slot (`GTASAsf1..8`) or per SA-COOP player profile (`*_coop_<Nickname>.ini`) inside `Documents\GTA San Andreas User Files\GTA5InventorySaves\`.

## Configuration (`GTA5Inventory.ini`)
Configure language (`rus`/`eng`), standalone hotkey (`F7`), carry limits, armor capacity/weights, healing values, and prices in [`config/GTA5Inventory.ini`](config/GTA5Inventory.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5Inventory
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5Inventory.asi
```
