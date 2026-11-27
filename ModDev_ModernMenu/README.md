# GTA5ModernMenu (`GTA5ModernMenu.asi`)

Modernized frontend Main Menu & Pause Menu overhaul for **GTA San Andreas 1.0 US (Hoodlum)** — blending the suite's clean dark UI Kit aesthetic with **Definitive Edition** two-column layouts and **GTA V**-style horizontal top navigation tabs.

## Features
- **GTA V Horizontal Top Navigation Bar:**
  - **Main Menu (Title Screen):** `Main Menu`, `Start Game` (Singleplayer), `Multiplayer` (SA-COOP hub when `coop.asi` is installed), `Mods` (ModLoader hub when `modloader.asi` is installed), `Options`, and `Quit` — strictly hiding in-game-only tabs (`Map`, `Stats`, `Brief`, `Pause`) before gameplay starts.
  - **In-Game Pause Menu:** `Pause`, `Map`, `Game`, `Stats`, `Brief`, `Mods`, `Options`, and `Quit` (switchable via `Q` / `E` or mouse click), displaying live rank, cash, clock, and active SA-COOP role in the masthead — with the `Multiplayer` tab removed in-game so Singleplayer and Co-op remain cleanly separated.
- **Native ModLoader Integration (`Mods` Tab & Modernized Pages):** Dynamically reads ModLoader's relocated `aScreens[]` (`Mod Configuration`, `Modifications`, and per-mod `Enabled` / `Priority` pages) and renders them natively inside our modernized two-column UI with live `modloader.ini` / `config.ini` status & priorities, plus `A`/`D` page switching.
- **Modernized Redefine Controls (`page 38`):** Scrollable two-column Foot & Vehicle keybinding list (`TAB` / `A` / `D` switches Foot <-> Vehicle inline), **clickable right-hand binding slots** (`Primary Key`, `Secondary Key`, `Mouse Button`, `Clear All Keys`) plus `Space` (secondary key) & `Delete` (clear all) shortcuts, backed by native `CControllerConfigManager` capture & `gta_sa.set` persistence.
- **Full ThirteenAG Widescreen Fix Integration:** Properly integrates with Widescreen Fix's extended **Frame Limiter** (`OFF / 30..240 FPS`) and **Cutscene Borders** (`OFF / Letterbox / Pillarbox / Letterbox + Pillarbox`) in `Advanced Display` with full MSKL Russian & English localization, fullscreen `FrameLimiter=0` load-safety protection, automatic 32-bit resolution preference, and live SMAA / Menu FPS Cap status on the detail card.
- **Deep GTA SA COOP (`coop.asi`) Integration:**
  - Suppresses SA-COOP's automatic startup popup on boot and moves multiplayer configuration & Host/Join launching into the dedicated Main-Menu **`Multiplayer`** tab (Player Name, Host Save Slot, Address/Code, Port, Password, Max Players, Friendly Fire, Partner Teleport, Shared Mission Life, Partner Cheats, Voice Chat, GPS).
  - Restyles SA-COOP's native UI windows (`F9` Connect, `F2` Players, `F4` Skins, Animations) to match our dark UI Kit aesthetic (signature-verified for safety).
  - Locks unsafe `New Game`, `Load Game`, and `Delete Game` options inside the Pause Menu during active SA-COOP multiplayer sessions.
- **Definitive Edition Two-Column Layout:** Compact, non-bulky option lists on the left paired with a live contextual detail card on the right:
  - **Pause Overview:** Live Rank & XP bar (`GTA5Progression`), Cash, Health, Armor, Personal Vehicles (`GTA5GarageAndTuning`), Owned Arsenal (`GTA5AmmuNation`), and current region.
  - **Save / Load / Delete:** Instant slot preview with mission title and save timestamp.
  - **Stats:** Scrollable native GTA SA statistics with clean progress bars and counters per category.
  - **Brief:** Clean wrapped history of recent mission & gameplay notifications.
  - **Settings & Sliders:** Interactive inline sliders (mouse drag/click or `A`/`D` / `Left`/`Right`) and live contextual descriptions for every Audio, Display, and Controller option.
- **Framed Native Interactive Map:** Full native GTA SA map with pan, zoom, waypoint placement, and legend framed cleanly between the modern top tab bar and minimalist bottom footer.
- **Centered Confirmation Modals:** Crisp Definitive Edition dialogs for Quit, Overwrite Save, Delete Save, Load, New Game, and Restore Defaults.
- **Full Third-Party Mod Compatibility:** Preserves **ModLoader (`Mod Configuration`)**, **SA-COOP 0.3.4+**, **SilentPatch**, **Widescreen Fix (latest)**, **Proper Shaders**, and **MSKL Russificator** with zero external mod dependencies beyond Ultimate ASI Loader.

## Controls
- **Navigate List:** `Up` / `Down`, `W` / `S`, `Mouse Wheel`, or mouse hover
- **Switch Top Tabs:** `Q` / `E` or click any tab
- **Adjust Options & Sliders:** `Left` / `Right`, `A` / `D`, or click/drag slider bar
- **Accept / Confirm:** `LMB`, `Enter`, or `F`
- **Back / Resume:** `RMB`, `Backspace`, or `Esc`

## Configuration (`GTA5ModernMenu.ini`)
- `EnableModernMenu=1` — Toggle modernized menu rendering (`0` restores vanilla GTA SA menu)
- `BackdropOpacity=178` — Scrim darkness behind menu surfaces (`0..255`)
- `ShowPlayerCard=1` — Show live player overview card on the Pause screen
- `ShowClassicTitleArt=1` — Keep classic GTA SA background artwork subtly visible behind the Main Menu scrim
- `EnableLog=1` — Enable diagnostic logging to `GTA5ModernMenu.log`
