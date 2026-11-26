# ModDev_Common — Shared GTA SA 1.0 US SDK, Dumb-Component UI Kit & Inter-Mod Bus

Общая header-only библиотека для всей модульной линейки ASI-плагинов (`GTA5UIKit`, `GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`, `GTA5EarlyUnlock`), написанная на чистом C++20 под 32-битный MinGW-w64 без внешних SDK-зависимостей.

## Модули

| Файл | Назначение |
|---|---|
| `sa_sdk.hpp` | Верифицированные по `gta_sa.exe` (1.0 US Hoodlum) структуры (`CPed`, `CVehicle`, `CWeapon`, `CWeaponInfo`, `tHandlingData`, `tMenuPanel`, `CPedClothesDesc`), нативные вызовы (`CFont`, `CSprite2d::Draw`, `RenderWare` Im2D/Im3D, `CCamera`, `CWanted`, `CStats`, `CClothes`, `CShopping`, `CRunningScript` SCM-обёртка, `IsCoopModLoaded`, `IsGameWindowFocused`). |
| `ui_kit.hpp` | **Dumb-Component UI Kit**: единая корневая тема `ui::Theme()` (синхронизируется с `GTA5UIKit.asi`), автоподгонка текста `ui::PrintFittedText()` на базе `CFont::GetStringWidth`, независимые компоненты `ui::ListMenu`, `ui::StatsCard` (с красной полосой потерь эффективности от износа в стиле ETS 2), `ui::PhoneFrame`, `ui::RadialWheel`, `DrawTextInputBox`. |
| `mod_bus.hpp` | **Межмодовая шина сервисов и реестр аддонов** (pure C-ABI поверх process-local shared memory): независимая от порядка загрузки связь между `GTA5UIKit`, `GTA5Phone`, `GTA5GarageAndTuning`, `GTA5Inventory`, `GTA5AmmuNation` и сторонними аддонами (`RegisterPhoneAddon`). |
| `bass_audio.hpp` | Динамическая загрузка `bass.dll`, синтез 16-bit PCM WAV в памяти, зацикленные потоки и сэмплы. |
| `text_rus.hpp` | Кодировщик UTF-8 → SanLtd русификатор GTA SA с поддержкой цветовых тегов (`~g~`, `~r~`, `~y~`, `~w~`). |
| `memory.hpp` | Безопасные x86-хуки (`HookCall`, `WriteJmp`, `PatchBytes`). |
| `log.hpp` | Потокобезопасный файловый логгер рядом с `.asi`. |
