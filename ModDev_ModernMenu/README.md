# GTA5ModernMenu (`GTA5ModernMenu.asi`)

Modernized frontend Main Menu & Pause Menu overhaul for **GTA San Andreas 1.0 US (Hoodlum)** — blending the suite's clean dark UI Kit aesthetic with **Definitive Edition** two-column layouts and **GTA V**-style horizontal top navigation tabs.

## Features
- **GTA V Horizontal Top Navigation Bar:** Switch effortlessly between `Pause`, `Map`, `Game`, `Stats`, `Brief`, `Options`, and `Quit` using `Q` / `E` or direct mouse click, with a sleek masthead displaying live player rank, cash, and in-game clock.
- **Definitive Edition Two-Column Layout:** Compact, non-bulky option lists on the left paired with a live contextual detail card on the right:
  - **Pause Overview:** Live Rank & XP bar (`GTA5Progression`), Cash, Health, Armor, Personal Vehicles (`GTA5GarageAndTuning`), Owned Arsenal (`GTA5AmmuNation`), and current region.
  - **Save / Load / Delete:** Instant slot preview with mission title and save timestamp.
  - **Stats:** Scrollable native GTA SA statistics with clean progress bars and counters per category.
  - **Brief:** Clean wrapped history of recent mission & gameplay notifications.
  - **Settings & Sliders:** Interactive inline sliders (mouse drag/click or `A`/`D` / `Left`/`Right`) and live contextual descriptions for every Audio, Display, and Controller option.
- **Framed Native Interactive Map:** Full native GTA SA map with pan, zoom, waypoint placement, and legend framed cleanly between the modern top tab bar and minimalist bottom footer.
- **Centered Confirmation Modals:** Crisp Definitive Edition dialogs for Quit, Overwrite Save, Delete Save, Load, New Game, and Restore Defaults.
- **Full Third-Party Mod Compatibility:** Preserves **ModLoader (`Mod Configuration`)**, **Redefine Controls**, **SA-COOP**, **SilentPatch**, **Widescreen Fix**, **Proper Shaders**, and **MSKL Russificator** with zero external mod dependencies beyond Ultimate ASI Loader.

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
