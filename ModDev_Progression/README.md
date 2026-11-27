# GTA5Progression (`GTA5Progression.asi`)

GTA Online-style **Rank & XP Progression System** for **GTA San Andreas 1.0 US (Hoodlum)**, tightly integrated across the entire modular ASI suite (`GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`, `GTA5UIKit`).

## Features
- **Dynamic GTA Online Top XP Bar (`ui::RankBarHUD`):** Smoothly slides in at the top-center whenever you earn XP (or press `F8`), displaying your current rank circle on the left, next rank circle on the right, animated green fill bar with exact XP count, rising green `+X XP` popups, and a Rank-Up highlight with unlock notifications.
- **Hostile-Only Combat XP:** Awards XP for killing enemy gangsters, Mafia, Police / SWAT / FBI / Army, drug dealers, criminals, and mission targets (+10 XP headshot bonus). Civilians and friendly allies never award XP.
- **Scaling Mission & World Activity XP:** Story & side missions grant progressively more XP as you advance through the game, alongside gang territory captures, wanted-star escapes (cheat-safe), stunt jumps, and collectibles.
- **Non-Grindy Rank 1–25 Unlock Curve:** Every gameplay unlock across Workshops (engines, transmissions, suspension, brakes, tires, bulletproof tires, turbo, armor, nitro), Dealerships (sports, supercars, aircraft, Rhino/Hydra), Ammu-Nation (weapons, attachments, heavy armor), Restaurants (premium meals & Healthy Salad), and Clothing Chains (Sub Urban -> ProLaps -> ZIP -> Victim -> Didier Sachs) unlocks smoothly by Rank 25.
- **Phone Integration:** Registers a live **Rank & Unlocks** app inside `GTA5Phone` (`F6`) showing your progress and upcoming unlock milestones.
- **Singleplayer + SA-COOP Safe:** Per-save-slot (`GTASAsf1..8`) and SA-COOP per-player persistence under `Documents\GTA San Andreas User Files\GTA5ProgressionSaves\`, with automatic retroactive XP calculation when loading an existing mid-game save for the first time.
