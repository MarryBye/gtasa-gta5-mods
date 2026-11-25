# ModDev_Common — Shared GTA SA 1.0 US SDK & Vanilla UI Kit

Общая header-only библиотека для всей линейки ASI-плагинов (`GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5EarlyUnlock`), написанная на чистом C++20 под 32-битный MinGW-w64 без внешних SDK-зависимостей.

## Модули

| Файл | Назначение |
|---|---|
| `sa_sdk.hpp` | Верифицированные по `gta_sa.exe` (1.0 US Hoodlum) структуры (`CPed`, `CVehicle`, `CWeapon`, `CWeaponInfo`, `tHandlingData`, `CRect`, `CMatrix`), нативные вызовы (`CFont`, `CSprite2d::Draw`, `RenderWare` Im2D/Im3D, `CCamera`, `CRunningScript` SCM-обёртка, `LoadAllRequestedModels`, `IsCoopModLoaded`, `IsGameWindowFocused`). |
| `ui_kit.hpp` | **Dumb-Component UI Kit в стиле веб-разработки**: единая корневая тема `ui::Theme()`, автоматическая подгонка текста `ui::PrintFittedText()` на базе `CFont::GetStringWidth` (`0x0071A0E0`), защита от выхода за границы экрана под WidescreenFix и независимые компоненты `ui::ListMenu`, `ui::StatsCard`, `ui::RadialWheel`, `DrawTextInputBox`. |
| `bass_audio.hpp` | Динамическая загрузка `bass.dll` (из корня GTA SA / CLEO / рядом с плагином), генератор 16-bit PCM WAV в памяти, зацикленные потоки и сэмплы с плавным управлением частотой и громкостью. |
| `text_rus.hpp` | Кодировщик UTF-8 → SanLtd / стандартный русификатор GTA SA с поддержкой управляющих тегов (`~g~`, `~r~`, `~y~`, `~w~`). |
| `memory.hpp` | Утилиты безопасного патчинга x86 (`HookCall`, `WriteBytes`, `Nop`) с автоматическим `VirtualProtect`. |
| `log.hpp` | Потокобезопасный файловый логгер с таймстемпами рядом с `.asi`. |
