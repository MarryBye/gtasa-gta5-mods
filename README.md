# GTA San Andreas — GTA 5 Style Expansion Mods (ASI Suite)

Набор из трёх модульных **C++20 ASI-плагинов** для **GTA San Andreas 1.0 US (Hoodlum)**, переносящих лучшие геймплейные механики GTA 5 (глубокий тюнинг и доставку личного авто механиком, кастомизацию оружия в Ammu-Nation + колесо выбора оружия, раннее открытие магазинов) с сохранением **аутентичного ванильного стиля GTA SA** и полной совместимостью с **SA-COOP**.

> **English summary:** A modular C++20 ASI plugin suite for **GTA San Andreas 1.0 US (Hoodlum)** bringing GTA 5-style vehicle tuning & mechanic delivery, Ammu-Nation weapon customization & multi-weapon radial wheel, and early shop unlocking — built on a shared vanilla-styled auto-fitting Dumb-Component UI Kit (`ModDev_Common`) with full **SA-COOP**, **WidescreenFix**, **Proper Shaders**, and **Proper Vehicle Damage** compatibility.

---

## Состав коллекции / Included Mods

| Мод | Папка | Описание |
|---|---|---|
| **[GTA5GarageAndTuning](ModDev_GTA5Garage/README.md)** | `ModDev_GTA5Garage/` | Тюнинг производительности (Двигатель 1–4, Трансмиссия, Подвеска, Тормоза, Шины/Пулестойкие, Турбо, Броня 20–100%), полный визуальный тюнинг и кастомные номера во всех 5 автомастерских, умная камера с зумом и автофокусом, регистрация до 10 личных машин, реальная доставка механиком по `F6` с карточкой характеристик и страховкой, синтезированные звуки выхлопа/турбо/клаксонов (`bass.dll`). |
| **[GTA5AmmuNation](ModDev_GTA5Weapons/README.md)** | `ModDev_GTA5Weapons/` | Новый интерфейс Ammu-Nation с панелью характеристик, модули оружия (глушитель, компенсатор, увеличенный/барабанный магазин, коллиматор/оптика с реальным зумом ПКМ, рукоять, усиленный ствол, 3D-ЛЦУ), опциональные 3D-обвесы на моделях оружия (вкл. две руки Hitman, по умолчанию выкл. в INI), кастомные звуки выстрелов (`bass.dll`), ношение нескольких стволов одного класса с общими патронами и ванильное 8-секторное **колесо выбора оружия** (`Колёсико мыши` / `TAB`, без слоу-мо). |
| **[GTA5EarlyUnlock](ModDev_EarlyUnlock/README.md)** | `ModDev_EarlyUnlock/` | Независимый плагин, открывающий все автомастерские, Ammu-Nation, магазины одежды, парикмахерские, тату-салоны, спортзалы и закусочные с самого начала игры с безопасным добавлением недостающих иконок на карту (без дубликатов и без конфликта с миссиями). |
| **[ModDev_Common](ModDev_Common/README.md)** | `ModDev_Common/` | Общая header-only библиотека: верифицированный SDK под GTA SA 1.0 US (`sa_sdk.hpp`), **Dumb-Component UI Kit** с корневой темой и автоподгонкой текста через `CFont::GetStringWidth` (`ui_kit.hpp`), BASS-аудио (`bass_audio.hpp`), кодировщик русификатора SanLtd (`text_rus.hpp`), утилиты хуков (`memory.hpp`). |

---

## Быстрая установка (из Releases) / Quick Install

1. Убедитесь, что у вас установлена **GTA San Andreas 1.0 US (Hoodlum, `14,383,616` байт)** с **ASI Loader** и **ModLoader** (а для кастомных звуков — `bass.dll` в корне игры, обычно уже идёт в комплекте с CLEO / ModLoader).
2. Скачайте нужные архивы (или общий бандл `GTASA-GTA5-Mods-Bundle-v*.zip`) со страницы **[Releases](../../releases)**.
3. Распакуйте папки `GTA5GarageAndTuning`, `GTA5AmmuNation` и/или `GTA5EarlyUnlock` напрямую в папку **`modloader\`** вашей игры:
   ```text
   Grand Theft Auto San Andreas/
   └── modloader/
       ├── GTA5GarageAndTuning/
       │   ├── GTA5GarageAndTuning.asi
       │   └── GTA5GarageAndTuning.ini
       ├── GTA5AmmuNation/
       │   ├── GTA5AmmuNation.asi
       │   └── GTA5AmmuNation.ini
       └── GTA5EarlyUnlock/
           ├── GTA5EarlyUnlock.asi
           └── GTA5EarlyUnlock.ini
   ```
4. При желании настройте параметры (язык `rus`/`eng`, цены, клавиши, единицы скорости `mph`/`kmh`, включение 3D-обвесов `Enable3DAttachments=1` и т.д.) в соответствующих `.ini` файлах.

> **Совет по сохранениям:** оба основных мода хранят данные отдельно от ванильных `.b`-файлов в `Документы\GTA San Andreas User Files\GTA5GarageSaves\` и `GTA5WeaponSaves\` (отдельно для каждого слота сейва `1–8`). При старте **Новой игры** в одиночке гараж и арсенал начинаются с чистого листа.

---

## Совместимость / Compatibility

| Мод / Среда | Статус | Примечания |
|---|---|---|
| **GTA SA 1.0 US (Hoodlum)** | Полная | Все адреса верифицированы напрямую по дизассемблированию `gta_sa.exe`. |
| **SA-COOP** | Полная | Нет замедления времени, работа строго с локальным игроком `Players[0]`, внешние слоты автосохранений. |
| **WidescreenFix** | Полная | Единый виртуальный масштаб `640×448` (4:3 aspect-corrected) + автоподгонка текста и защита от выхода за края экрана. |
| **Proper Shaders** | Полная | Чистое сохранение/восстановление RenderWare-состояний, отсутствие вмешательства в фары/освещение. |
| **Proper Vehicle Damage v0.3.7** | Полная | Броня кузова (20–100%) масштабирует и `m_fCollisionDamageMultiplier`, и жёсткость/массу шасси для реального снижения деформации. |
| **ModelExtras / Open Limit Adjuster** | Полная | Автоматическое следование за релоцированными таблицами `CModelInfo` / `gHandlingDataMgr` + `handling.cfg` fallback. |
| **Izzy's Lively Vehicle Physics** | Полная | Изолированные копии `tHandlingData` на каждый затюнингованный автомобиль. |
| **SA Styled Speedometer** | Полная | Панель характеристик по умолчанию калибрована под `MPH` (переключается на `kmh` в INI). |
| **Russificator (SanLtd)** | Полная | Встроенный перекодировщик UTF-8 → SanLtd + переключение на английский (`Language=eng`). |

---

## Сборка из исходников / Building from Source

Все плагины собираются в 32-битные DLL (`.asi`) со статической линковкой (без сторонних рантайм-DLL — только системные `KERNEL32`, `USER32`, `SHELL32`, `msvcrt`) компилятором **MinGW-w64 i686 (MSYS2)** с поддержкой C++20.

1. Установите [MSYS2](https://www.msys2.org/) и 32-битный тулчейн:
   ```bash
   pacman -S mingw-w64-i686-gcc
   ```
2. Клонируйте репозиторий и запустите общую сборку:
   ```powershell
   git clone https://github.com/MarryBye/gtasa-gta5-mods.git
   cd gtasa-gta5-mods
   .\build-all.ps1
   ```
   Скрипт соберёт все 3 плагина, проверит заголовки `pei-i386` и зависимости, и упакует готовые к установке архивы в `dist\`.

   Для сборки отдельного плагина используйте `.\ModDev_<Name>\build.ps1 -NoDeploy`.

---

## Архитектура

```text
gtasa-gta5-mods/
├── ModDev_Common/          # Общий header-only SDK, Dumb-Component UI Kit, BASS Audio, SanLtd encoder
├── ModDev_GTA5Garage/      # GTA5GarageAndTuning — автомастерские, физика, гараж, доставка механиком
├── ModDev_GTA5Weapons/     # GTA5AmmuNation — магазин, обвесы, звук, мульти-инвентарь, колесо оружия
├── ModDev_EarlyUnlock/     # GTA5EarlyUnlock — разблокировка всех магазинов и сервисов с начала игры
├── build-all.ps1           # Единый скрипт сборки и упаковки релизных архивов
├── LICENSE                 # MIT License
└── README.md
```

## Лицензия / License

Распространяется под лицензией [MIT](LICENSE).
