# GTA5EarlyUnlock

**Standalone early-game shop & service unlocker for GTA San Andreas 1.0 US (Hoodlum).**

Лёгкий независимый ASI-плагин, открывающий все магазины, мастерские и сервисы штата с самого начала игры (без прохождения сюжетных миссий) и добавляющий недостающие иконки на радар без дубликатов.

---

## Что разблокируется (каждая категория отключается в INI)
- **Автомастерские** — TransFender (LS / SF / LV), Loco Low Co., Wheel Arch Angels + иконки ключа на карте (`UnlockModShops=1`)
- **Оружейные магазины** — все 11 филиалов Ammu-Nation (`AMMUN1`–`AMMUN5`, `UnlockAmmuNation=1`)
- **Магазины одежды** — Binco, ProLaps, Sub Urban, ZIP, Victim, Didier Sachs (`UnlockClothes=1`)
- **Парикмахерские и тату-салоны** — по всему штату (`UnlockBarbersAndTattoos=1`)
- **Спортзалы** — Los Santos, San Fierro, Las Venturas (`UnlockGyms=1`)
- **Закусочные, рестораны и сервисы** — Burger Shot, Cluckin' Bell, Well Stacked Pizza, рестораны, букмекерские конторы (`UnlockFoodAndMisc=1`)

## Безопасность для сюжета и лимитов
- Во время активной сюжетной миссии плагин **не вмешивается** в флаги интерьеров, чтобы не конфликтовать со скриптами миссий.
- Перед добавлением каждой иконки проверяется реальный массив `CRadar::ms_RadarTrace` (`0x00BA86F0`) — **дубликаты никогда не создаются**, а также сохраняется запас свободных слотов радара для сюжетных маркеров.
- Полностью совместим с **GTA5GarageAndTuning**, **GTA5AmmuNation** и **SA-COOP**.

## Конфигурация (`GTA5EarlyUnlock.ini`)
Все категории и флаг `AddMapIcons` настраиваются в [`config/GTA5EarlyUnlock.ini`](config/GTA5EarlyUnlock.ini).

## Сборка из исходников
```powershell
.\build.ps1            # Собрать и установить в ..\modloader\GTA5EarlyUnlock
.\build.ps1 -NoDeploy  # Только собрать в .\build\GTA5EarlyUnlock.asi
```
