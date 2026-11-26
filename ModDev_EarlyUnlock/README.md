# GTA5EarlyUnlock

**Standalone early-game shop, workshop & service unlocker for GTA San Andreas 1.0 US (Hoodlum).**

A lightweight independent ASI plugin that opens all vehicle workshops, dealerships, gun stores, clothing stores, barber shops, tattoo parlors, gyms, and diners across San Andreas right from the start of the game (without waiting for story mission unlocks) while safely adding missing radar icons with zero duplicates.

---

## Unlocked Locations (Each Category Toggleable in INI)
- **Vehicle Mod Shops:** TransFender (LS / SF / LV), Loco Low Co., and Wheel Arch Angels + wrench map blips (`UnlockModShops=1`).
- **Weapon Stores:** All 11 Ammu-Nation branches (`AMMUN1`–`AMMUN5`, `UnlockAmmuNation=1`).
- **Clothing Stores:** Binco, ProLaps, Sub Urban, ZIP, Victim, and Didier Sachs (`UnlockClothes=1`).
- **Barber Shops & Tattoo Parlors:** Across the entire state (`UnlockBarbersAndTattoos=1`).
- **Gyms:** Los Santos, San Fierro, and Las Venturas (`UnlockGyms=1`).
- **Diners, Restaurants & Services:** Burger Shot, Cluckin' Bell, Well Stacked Pizza, restaurants, and betting shops (`UnlockFoodAndMisc=1`).

## Storyline & Radar Limit Safety
- While an active story mission is running, the plugin **does not interfere** with interior unlock flags so mission scripts never conflict.
- Before creating each map icon, the plugin inspects the live `CRadar::ms_RadarTrace` array (`0x00BA86F0`) — **no duplicate blips are ever created**, and radar trace slots are reserved safely for mission markers.
- 100% compatible with **GTA5GarageAndTuning**, **GTA5AmmuNation**, **GTA5Phone**, and **SA-COOP**.

## Configuration (`GTA5EarlyUnlock.ini`)
All unlock categories and the `AddMapIcons` toggle are configurable in [`config/GTA5EarlyUnlock.ini`](config/GTA5EarlyUnlock.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5EarlyUnlock
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5EarlyUnlock.asi
```
