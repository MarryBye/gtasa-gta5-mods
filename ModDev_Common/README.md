# ModDev_Common — Shared GTA SA 1.0 US SDK, Dumb-Component UI Kit & Inter-Mod Bus

Shared header-only library powering the entire modular ASI plugin suite (`GTA5UIKit`, `GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`, `GTA5EarlyUnlock`), written in pure C++20 for 32-bit MinGW-w64 with zero external SDK dependencies.

## Headers

| File | Purpose |
|---|---|
| `sa_sdk.hpp` | Verified GTA SA 1.0 US (Hoodlum) memory structures (`CPed`, `CVehicle`, `CWeapon`, `CWeaponInfo`, `tHandlingData`, `tMenuPanel`, `CPedClothesDesc`), native game calls (`CFont`, `CSprite2d::Draw`, `RenderWare` Im2D/Im3D, `CCamera`, `CWanted`, `CStats`, `CClothes`, `CShopping`, `CRunningScript` SCM wrapper, `IsCoopModLoaded`, `IsGameWindowFocused`). |
| `ui_kit.hpp` | **3-Tier React-Style Dumb-Component UI Kit**: single root theme `ui::Theme()` (synced live from `GTA5UIKit.asi`), atomic primitives (`ui::prim::*`) with auto-fitting text via `CFont::GetStringWidth`, composite dumb components (`ui::ListMenu`, `ui::StatsCard` with ETS 2 red wear-loss bars, `ui::PhoneFrame`, `ui::HelpToast`, `ui::ColorGrid`, `ui::RadialWheel`, `ui::TextInputModal`), and unified `ui::ListInputController`. |
| `mod_bus.hpp` | **Inter-Mod Service Bus & Addon Registry** (pure C-ABI over process-local shared memory): load-order-independent integration between `GTA5UIKit`, `GTA5Phone`, `GTA5GarageAndTuning`, `GTA5Inventory`, `GTA5AmmuNation`, and third-party phone addons (`RegisterPhoneAddon`). |
| `bass_audio.hpp` | Dynamic `bass.dll` loader, in-memory 16-bit PCM WAV synthesis, looped streams, and one-shot samples. |
| `text_rus.hpp` | UTF-8 → MSKL / SanLtd Russian font byte encoder for GTA SA with full preservation of color/formatting tags (`~g~`, `~r~`, `~y~`, `~w~`, `~n~`). |
| `memory.hpp` | Safe x86 hooking utilities (`HookCall`, `HookJump`, `PatchBytes`). |
| `log.hpp` | Thread-safe per-plugin file logger written next to the `.asi` binary. |
