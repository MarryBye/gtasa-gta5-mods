# GTA5UIKit — Shared Runtime UI Kit, Addon Bus & Vanilla Menu Restyler

Модуль **A** из линейки модов:
- Единая глобальная тема (`Language`, `SpeedUnit`, `UIScale`) для всех модулей (`GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`).
- Межмодовая шина сервисов (`mod_bus.hpp`), позволяющая плагинам подключаться друг к другу в любом порядке загрузки без статических DLL-зависимостей.
- **Обновление всех ванильных меню GTA SA** (`RestyleVanillaMenus=1`): перехватывает рендер `CMenuSystem::DisplayStandardMenu` (`0x00580E00`) и перерисовывает парикмахерские, тату-салоны, спортзалы, тотализатор и миссионные меню в едином стиле нашего UI Kit без изменения их ванильного функционала.
