# ModDev_Common — Shared GTA SA 1.0 US SDK, Dumb-Component UI Kit, Bus & Reusable Integrations

Shared header-only library powering the entire modular ASI plugin suite (`GTA5UIKit`, `GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`, `GTA5EarlyUnlock`), written in pure C++20 for 32-bit MinGW-w64 with zero external SDK dependencies.

## Core Headers

| File | Purpose |
|---|---|
| `sa_sdk.hpp` | Verified GTA SA 1.0 US (Hoodlum) memory structures (`CPed`, `CVehicle`, `CWeapon`, `CWeaponInfo`, `tHandlingData`, `tMenuPanel`, `CPedClothesDesc`), native game calls (`CFont`, `CSprite2d::Draw`, `RenderWare` Im2D/Im3D, `CCamera`, `CWanted`, `CStats`, `CClothes`, `CShopping`, `CRadar`, `CGarages`, `CRunningScript` SCM wrapper). |
| `ui_kit.hpp` | **React-Style Dumb-Component UI Kit** with strict bottom-up composition: `Tier 0` `ui::Theme()` tokens (synced live from `GTA5UIKit.asi`), CSS-like `Relative`/`Absolute` child positioning (`RectBox`, `LayoutStyle`, `VStack`), `Tier 1` atomic primitives (`ui::prim::*` — `Rect`, 4-edge `Border`, `Panel`, `Divider`, `AccentStrip`, auto-fit `Label`, `WrappedText`, `Icon`, `Arc`, `Disc`), `Tier 1.5` micro-components (`ui::widget::*` — `Header`, `Row`, `Footer`, `ProgressBar`, `TextField`, `Banner`, `Swatch`, `Badge`, `Button`), `Tier 2` composite dumb containers (`ListMenu`, generic `Grid`, `ColorGrid`, `StatsCard`, `PhoneFrame`, titled `HelpToast`, `RadialWheel`, `TextInputModal`), and frame-deduplicated `KeyTracker` + `ListInputController`. |
| `mod_bus.hpp` | **Inter-Mod Service Bus, Input Priority & Addon Registry** (pure C-ABI over process-local shared memory): load-order-independent integration between all suite plugins, centralized `InputPriority` arbitration (`StoryPhone` > `ModalMenu` > `QuickOverlay` > `PassiveHud`), `RegisterPanelClaim`, and extensible `RegisterPhoneAddon`. |
| `common_utils.hpp` | Shared DRY helpers: executable/module directory resolution, INI read/write wrappers, localized price formatting, dynamic virtual-key labels, and GTA SA 1.0 US `SaveSlotTracker`. |
| `memory.hpp` | Safe x86 hooking utilities (`HookCall`, `HookJump`, `PatchBytes`) with opcode validation and original-target chaining. |
| `log.hpp` | Thread-safe (`CRITICAL_SECTION`), merge-safe per-plugin file logger written next to the `.asi` binary. |

## Standalone Reusable Integration Modules (`integrations/`)

Every third-party mod/library integration lives in its own standalone reusable header under `ModDev_Common/integrations/`:

| File | Integrated Mod / Library |
|---|---|
| `integrations/sacoop.hpp` | **GTA SA COOP (`Karimov-Akbar/sa-coop`)** — cached detection, window-focus & overlay/chat guards, cached per-player profile paths, `F4` custom-skin guards, teammate-safe `DELETE_CAR` guard, and scan-code `F2` player-list trigger. |
| `integrations/widescreen_fix.hpp` | **ThirteenAG Widescreen Fix (latest)** — cached detection, aspect-ratio-corrected 640x448 virtual scaling, and `ScopeFovGuard` preserving live dynamic FOV across ADS scope zoom. |
| `integrations/proper_shaders.hpp` | **Proper Shaders / RenderWare 3.6** — RAII `RwRenderStateGuard` saving/restoring all 12 RW states (including `SHADEMODE=7`, `TEXTUREFILTER=9`, `FOGENABLE=14`, `ALPHATEST`) around custom Im2D/Im3D draws. |
| `integrations/limit_adjusters.hpp` | **SilentPatch / ModelExtras / Limit Adjusters / Proper Vehicle Damage** — dynamic runtime lookup of potentially relocated `ms_modelInfoPtrs` and `mod_HandlingManager` tables + cached runtime mod detection. |
| `integrations/mskl_rus.hpp` | **MSKL / SanLtd Russificator** — UTF-8 to game byte-table encoder preserving GTA color tags (`~g~`, `~r~`, `~y~`, `~w~`, `~n~`), sanitizing reserved `|` glyphs, and normalizing UTF-8 punctuation. |
| `integrations/bass_audio.hpp` | **BASS (`bass.dll`)** — zero-static-dependency dynamic loader with graceful silent fallback, in-memory 16-bit PCM WAV builder, looped streams, one-shot samples, and shared DSP helpers. |
