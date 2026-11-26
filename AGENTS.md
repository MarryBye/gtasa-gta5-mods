# GTA San Andreas Modular ASI Suite — Golden Rules & Architecture (`AGENTS.md`)

> **MANDATORY:** Every rule in **Section 1–8** below is a **Golden Rule** of this project. Violating any of them is strictly forbidden. Always verify compliance before editing code or adding features.

---

## 0. Project Vision & Mission
- Build a fun, creative, experimental suite of **C++20 32-bit ASI plugins** for **GTA San Andreas 1.0 US (Hoodlum)** — porting GTA 5 / ETS 2 mechanics and original gameplay ideas into classic GTA SA.
- Every feature and mod **MUST** remain 100% compatible with **SA-COOP (`Karimov-Akbar/sa-coop`)**, **WidescreenFix**, **Proper Shaders**, **Proper Vehicle Damage**, **ModelExtras**, and **MSKL Russificator**.
- **Long-term roadmap (Rule 7):** Currently developed as independent standalone ASI modules (`ModDev_*`), **planned to be merged into ONE unified all-in-one ASI super-mod later**. Every architectural choice must keep that merge effortless.

---

## 1. Golden Rule #1 — React-Style Dumb-Component UI Kit (UI Law)

**ALL UI work** — every new screen, window, HUD overlay, modal, menu, toast, widget, or UI update for ANY existing or future mod — **MUST** live inside the shared UI Kit library (`ModDev_Common/ui_kit.hpp` + `ModDev_UIKit/`). Feature mods are **STRICTLY FORBIDDEN** from drawing raw rectangles, raw `CFont::PrintString` UI blocks, or ad-hoc custom styling outside the UI Kit.

Follow a strict **React-inspired 3-tier component architecture**:

### 1a. Single Design System (`Tier 0 — ui::ThemeTokens`)
- `ui::Theme()` (`ThemeTokens` in `ModDev_Common/ui_kit.hpp`) is the **single source of truth** for the entire project's visual identity: color palette (`bgBody`, `bgHeader`, `bgFooter`, `bgInput`, `borderSubtle`, `divider`, `selectFill`, `textTitle`, `textCol`, `textNormal`, `textSelected`, `textDisabled`, `textFooter`, `barTrack`, `barFill`, `barGain`, `barLoss`, `accentWarn`), spacing, margins, typography scales, and WidescreenFix-matched scaling (`ui::GetScreenScales`).
- Global runtime tokens (`Language`, `SpeedUnit`, `UIScale`, `RestyleVanillaMenus`, `RestyleHelpBoxes`) are owned by `GTA5UIKit.asi` (`GTA5UIKit.ini`) and synchronized live across all modules via `mod_bus.hpp` (`ui::SyncFromSharedBus()`).
- **Never** hardcode rogue colors, arbitrary font sizes, or inconsistent layouts in feature mods. If a new visual token is needed, add it to `ThemeTokens`.

### 1b. Strict Atomic Composition (`Tier 1 Primitives` → `Tier 2 Composite Components`)
- **Tier 1 — Atomic Primitives (`ui::prim::*`)**:
  - **THE ONLY LAYER** permitted to touch low-level rendering (`sa::font::DrawRect`, `CFont::PrintString`, Im2D primitives).
  - Existing primitives: `Rect`, `Panel`, `Divider`, `AccentStrip`, `Label` (auto-fit width via `CFont::GetStringWidth`), `MeasureWrappedLines` / `WrappedText`, `Header`, `Row`, `Footer`, `ProgressBar` (with green delta gain & red wear loss), `TextField` (bordered input + blinking caret + char counter), `Banner`, `Swatch`.
  - When a new basic building block is needed (e.g., `Button`, `Checkbox`, `Toggle`, `Slider`, `Badge`, `TabStrip`, `ScrollBar`, `IconBox`), **create it in `ui::prim::*` first**.
- **Tier 2 — Composite Components (`ui::*`)**:
  - **MUST be composed exclusively from `ui::prim::*` and/or other Tier 2 components** — NEVER call raw `sa::font::*` or bypass primitives!
  - Guarantee: **changing a single Tier 1 primitive automatically updates every window across every mod** with zero design drift.
  - Existing composites:
    - `ui::ListMenu` — scrollable list window (shops, menus, vanilla restyler)
    - `ui::StatsCard` — stat bars with green `+` / red `−` preview deltas and ETS 2 red wear bars
    - `ui::PhoneFrame` — 1990s keypad handset with 4 LCD themes (`ClassicSA`, `Nokia92`, `Amber90s`, `NightBlue`)
    - `ui::HelpToast` — top-left notification / prompt / story-call toast with semantic `~r~`/`~g~`/`~y~` accent strip
    - `ui::ColorGrid` — color swatch grid (paint & vanilla `DisplayGridMenu`)
    - `ui::RadialWheel` — 8-sector monochrome radial wheel
    - `ui::TextInputModal` (`DrawTextInputBox`) — monochrome text-entry dialog (license plates, names, etc.)
  - Unified Input Layer: `ui::ListInputController` (`ui::ListInput()`) drives consistent controls across all custom and vanilla menus (`Wheel`/`↑`/`↓`/`W`/`S` scroll; `←`/`→`/`A`/`D` tabs/grids; `F`/`Enter`/`LMB` accept; `Backspace`/`Esc`/`RMB` back without popping Pause Menu; `Q`/`E` 3D camera orbit; `Z`/`X` zoom; `C` reset; cross-DLL debounce via `LockMenuInputBriefly` & `TriggerPostCloseShield`).

### 1c. Strict "Dumb / Presentational" Component Discipline
- **EVERY UI component (Tier 1 AND Tier 2) MUST be 100% "dumb" (presentational):**
  - **ZERO business/domain logic** inside `ui_kit.hpp` or `ModDev_UIKit/` (no money checks, vehicle/weapon data, inventory rules, save logic, shop rules, or gameplay actions).
  - Components expose **only**:
    1. **Data / composition methods (props & children)**: e.g., `AddItem(...)`, `AddBar(...)`, `SetTitle(...)`, `SetSelected(...)`, `SetFooter(...)`, `SetBanner(...)`.
    2. **Typed outbound signals / events**: e.g., `MenuEvent` (`HoverChanged`, `Activated`, `Back`), button click, list selection, character typed, key pressed, scroll/tab change.
  - Feature mods act as **smart containers**: they assemble UI components, feed display data, receive emitted signals, and execute domain logic in response.

### 1d. Unified Vanilla GTA SA UI Restyling (`ModDev_UIKit`)
- All vanilla GTA SA UI surfaces — unclaimed `CMenuSystem` panels (barber shops, tattoo parlors, gyms, betting shops, mission menus, color grids) and `CHud::DrawHelpText` top-left notifications / Q&A prompts / story phone calls — are restyled by `ModDev_UIKit/src/vanilla_restyle.cpp` using the exact same UI Kit components (`ListMenu`, `ColorGrid`, `HelpToast`) without modifying vanilla gameplay logic.
- When a feature mod replaces a vanilla `CMenuSystem` menu with its own UI, it **MUST** register a claim via `modbus::RegisterPanelClaim()` so `GTA5UIKit` never overlaps it.
- Storyline phone calls (`PED_ANSWER_PHONE` / `bus->storyPhonePriority`) always keep **Priority 1** and must never be blocked or hijacked by custom menus.

### 1e. Mandatory Dual-Language Support (Russian MSKL + English)
- Every user-visible string (titles, rows, buttons, stats, tooltips, footers, toast notifications, phone apps) **MUST** support **both Russian (`rus`) and English (`eng`)** via `ui::Loc(rusUtf8, engAscii)` (respecting `GTA5UIKit.ini` / module INI `Language=rus|eng`).
- Keep all Russian strings as **UTF-8 in C++ source** and always encode for in-game rendering through `text::ToGameRussian()` (`ModDev_Common/text_rus.hpp`), which targets the **MSKL Russificator / SanLtd** font byte table while preserving GTA color tags (`~r~`, `~g~`, `~y~`, `~w~`, `~n~`).
- **CRITICAL MSKL GLYPH RULE:** **NEVER** use the pipe character `|` (`0x7C`) as a UI separator — MSKL Russificator remaps `0x7C` to a custom glyph! Always use `-`, `:`, `,`, or `/` (as in `ui::StandardFooter()`).

---

## 2. Golden Rules #2–#6 — Module Boundaries & Domain Ownership

Keep feature code strictly inside its owning module. Never mix unrelated gameplay features across module folders.

| # | Module Folder | Output Plugin | Owned Domain & Core Features |
|---|---|---|---|
| **A** | `ModDev_UIKit/` + `ModDev_Common/ui_kit.hpp` | `GTA5UIKit.asi` | **Shared UI Kit & Bus Host:** Tier 0–2 React-style dumb UI components, `ThemeTokens`, global `Language`/`SpeedUnit`/`UIScale`/`Coop.ProfileName` host, `mod_bus.hpp` host, unified `ListInputController`, vanilla `CMenuSystem` & `CHud::DrawHelpText` restyler (barbers, tattoos, gyms, betting, grids, toasts). |
| **2** | `ModDev_GTA5Garage/` | `GTA5GarageAndTuning.asi` | **Vehicle Customization, Dealerships & Mechanic:** Deep performance & visual tuning across all 5 workshops (TransFender LS/SF/LV, Loco Low Co., Wheel Arch Angels) for cars & bikes with per-vehicle isolated `tHandlingData` (EMS Engine 1–4, Transmission adding gears up to 5-speed + Gears stat, Suspension lowering, Brakes, Tires + Bulletproof, Turbo + subtle BOV, Armor 20–100% compatible with Proper Vehicle Damage, 17 wheel sets, bodykits, custom 8-char plates, 4 synthesized horns, smart 3D workshop camera); **ETS 2-style component wear & servicing** (engine, transmission, brakes, tires, suspension, chassis degradation + red wear bars on `StatsCard`); **10 personal vehicles** with safehouse persistence & Pay 'n' Spray paint protection; **official dealerships in 3 cities** (Coutt & Schutz LS, Wang Cars SF, Auto Bahn LV); insurance recovery; road-network **Mechanic delivery** (`F6` standalone or via Phone). |
| **3** | `ModDev_GTA5Weapons/` | `GTA5AmmuNation.asi` | **Weapon Customization, Multi-Weapon Arsenal & Weapon Wheel:** Revamped Ammu-Nation UI & weapon attachments (silencer, compensator, extended/drum mags, collimator/tactical optics with smooth RMB zoom, tactical grip, heavy barrel, red/green 3D laser sight, optional 3D attachments on weapon models including dual-wield); custom synthesized weapon audio via `bass.dll`; **multi-weapon storage per category** (carry & swap multiple weapons of the same slot anytime with shared category ammo); 8-sector monochrome GTA SA **Weapon Wheel** (`TAB` / Mouse Wheel, **zero slow-mo** for SA-COOP). Integrates with `GTA5Inventory` to sell spare armor in Ammu-Nation. |
| **4** | `ModDev_Inventory/` | `GTA5Inventory.asi` | **Player Pocket Inventory:** Takeaway restaurants (Burger Shot, Cluckin' Bell, Well Stacked Pizza, diners) letting players buy snacks & meals **into their pocket** (P's & Q's, EgoChaser, Meteorite, Sprunk, signature meals, healthy salad) with carry limits & refill-all; **spare body armor** (4 classes: 25/50/75/100%, weight-based carry capacity); expandable for medkits and future pocket consumables. Standalone `F7` menu (automatically disabled and embedded into `GTA5Phone` when `GTA5Phone.asi` is installed). |
| **5** | `ModDev_Phone/` | `GTA5Phone.asi` | **GTA 5-Inspired 90s Phone Hub (`F6`):** Central interaction hub connecting **all mods** via `mod_bus.hpp` & `modbus::RegisterPhoneAddon()`. Native handset animation (`0x0729`), auto-holster under gunfire, full CJ movement/combat lock while open, 4 LCD themes, 4 synthesized ringtones, silent mode & settings. Integrates: **Contacts** (Mechanic, Old Friend wanted-level removal up to 3 stars every 10 min with shared SA-COOP cooldown, Insurance, EMS), **Food & Armor / Inventory**, **CJ Wardrobe** (part-by-part dressing + 8 auto-equip outfits for Mission/Freeplay/Wanted/Manual + restyled 6 clothing shop chains with 3D preview), **Online Dealership**, **Arsenal status**, and **SA-COOP Player List** (`F2`). |
| **6** | `ModDev_EarlyUnlock/` | `GTA5EarlyUnlock.asi` | **Early-Game Access Unlocker:** Opens all dealerships, mod shops/workshops (TransFender, Loco Low Co., Wheel Arch Angels), all 11 Ammu-Nation branches, all 6 clothing store chains, barber shops, tattoo parlors, gyms, and diners/restaurants from the very beginning of the game — with mission-safety guard (idle during active story missions) and duplicate-free `CRadar::ms_RadarTrace` map icon injection. |

---

## 3. Golden Rule #7 — Modular Today, Mergeable Into One Super-Mod Tomorrow

Although each module currently builds as a standalone `.asi`, the architecture **MUST** stay merge-ready so all modules can be combined into a single unified `.asi` without refactoring:

1. **Strict Module Namespaces:** Every module wraps all internal symbols, globals, and functions inside its own unique C++ namespace (e.g., `uikit_restyle::`, `garage::`, `weapons::`, `inventory::`, `phone::`, `early_unlock::`) and anonymous namespaces for file-local state. Never leak generic global symbols across translation units.
2. **Clean Lifecycle Entrypoints:** Keep module initialization, frame tick, HUD render, and shutdown cleanly separated from `DllMain` so a future unified `DllMain` can simply invoke each module's `Init()` / `Shutdown()` in sequence.
3. **Shared Code Only in `ModDev_Common/`:** Never copy-paste SDK structs, hooks, UI widgets, audio synthesis, logging, or Russian encoding between `ModDev_*` folders. Shared utilities belong exclusively in `ModDev_Common/`:
   - `sa_sdk.hpp` — Verified GTA SA 1.0 US structures, native calls, SCM script wrapper, SA-COOP detection
   - `ui_kit.hpp` — 3-tier dumb-component UI Kit & unified `ListInputController`
   - `mod_bus.hpp` — Load-order-independent pure C-ABI inter-mod bus & phone addon registry
   - `bass_audio.hpp` — Dynamic `bass.dll` loader & in-memory PCM synthesis
   - `text_rus.hpp` — UTF-8 → MSKL/SanLtd Russian encoder
   - `memory.hpp` — Safe x86 hooking (`HookCall`, `HookJump`, `PatchBytes`)
   - `log.hpp` — Thread-safe per-plugin file logger
4. **Cross-Module Integration Strictly via `mod_bus.hpp`:** Modules communicate **only** through `modbus::SharedBus`, `RegisterPhoneAddon()`, `RegisterPanelClaim()`, and `IsAnyModalMenuOpen()` — never through tight source coupling or fragile file polling. That same C-ABI bus will keep working identically inside a single merged DLL.
5. **Hook Address Discipline:** Never install conflicting raw patches on the same GTA SA address across modules; always chain original function pointers returned by `mem::HookCall` / `mem::HookJump`, and guard shared hooks so they remain composable when merged.
6. **Non-Colliding Config & Persistence:** Keep INI sections, keys, and external save folders cleanly namespaced per module so merging into a single config/save layout requires zero breaking changes.

---

## 4. Golden Rule #8 — SA-COOP & Stability Compliance

- **Never** use slow-motion / time-scale manipulation (`CTimer::ms_fTimeScale`) in wheels, phones, or menus.
- **Always** guard custom hotkeys and overlays against unfocused windows (`sa::IsGameWindowFocused()`), active pause menu/cutscenes, and SA-COOP chat/overlays (`F2`, `F4`, `F9`, `T`).
- **Isolate per-player persistence under SA-COOP:** detect via `sa::IsCoopModLoaded()`, bind singleplayer/host saves to `GTASAsf1..8`, and bind SA-COOP clients to `*_coop_<Nickname>.ini` (or `[Coop] ProfileName=` in `GTA5UIKit.ini`) under `Documents\GTA San Andreas User Files\GTA5*Saves\` — never mutate `.b` save files.
- **Guard destructive world operations:** never `DELETE_CAR` while another player/ped is driving or occupying the vehicle; guard `CClothes::RebuildPlayer` when non-CJ skins (`F4` skin selector) are active.
- **Respect shared cooldowns & sync-sensitive loops** (e.g., Old Friend wanted-level removal cooldown in SA-COOP).
- **Verify binaries:** every build must target GTA SA 1.0 US (32-bit `pei-i386`, static `libgcc`/`libstdc++`, minimal standard Windows DLL imports verified via `objdump`).

---

## 5. Toolchain, Build, Deploy & Release Workflow

- **Compiler:** 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, `-std=c++20 -m32 -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at`).
- **Local Game Path:** Copy `local.config.example.ps1` to `local.config.ps1` (gitignored) and set `$GameDir`.
  - Resolution order: `-GameDir` parameter → `$env:GTASA_DIR` → `local.config.ps1` → legacy `..\modloader`.
- **Commands:**
  - Single plugin (compile + auto-deploy to `<GameDir>\modloader\<ModName>\`):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\ModDev_UIKit\build.ps1
    ```
  - All plugins (fast dev build + deploy to `modloader`, skip ZIPs):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -SkipZip
    ```
  - All plugins + release ZIPs in `dist\` + deploy to `modloader`:
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -Version vX.Y.Z
    ```
  - Selective build: `-Only GTA5Phone,GTA5UIKit` | Debug build: `-Debug` | Refresh INI defaults: `-OverwriteIni` | Skip deploy: `-NoDeploy`.
  - Full release (build + deploy to `modloader` + commit/push + GitHub Release):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\publish-release.ps1 -Version vX.Y.Z -Title "..." -Notes "..." -CommitMessage "..."
    ```
- **Adding a New Mod Module:**
  1. Create `ModDev_<YourMod>/` with `src/`, `config/<ModName>.ini`, `build.ps1`, `build.bat`, and English `README.md`.
  2. Put all UI components in `ModDev_Common/ui_kit.hpp` (Golden Rule #1) and connect to `GTA5Phone` via `modbus::RegisterPhoneAddon()` if applicable.
  3. Register the module in `build-all.ps1` (`$AllMods`), root `README.md`, and this `AGENTS.md`.
