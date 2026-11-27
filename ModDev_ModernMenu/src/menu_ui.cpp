#include "menu_ui.hpp"
#include "menu_config.hpp"
#include "menu_sdk.hpp"
#include "ui_kit.hpp"
#include "mod_bus.hpp"
#include "log.hpp"
#include "integrations/sacoop.hpp"
#include "integrations/widescreen_fix.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace modern_menu {
namespace {

// ============================================================================
// Page Classification & Top-Tab Definitions
// ============================================================================
constexpr int8_t kPageMultiplayer = -10; // Virtual Main-Menu-only SA-COOP hub!

struct TopTabDef {
    int8_t      targetPage;
    const char* rus;
    const char* eng;
};

static constexpr TopTabDef kPauseTabs[] = {
    {41, "ПАУЗА",      "PAUSE"},
    {5,  "КАРТА",      "MAP"},
    {1,  "ИГРА",       "GAME"},
    {0,  "СТАТИСТИКА", "STATS"},
    {2,  "БРИФИНГ",    "BRIEF"},
    {33, "НАСТРОЙКИ",  "OPTIONS"},
    {35, "ВЫХОД",      "QUIT"}
};

static constexpr TopTabDef kMainTabsSolo[] = {
    {34, "ГЛАВНОЕ МЕНЮ", "MAIN MENU"},
    {1,  "НАЧАТЬ ИГРУ",  "START GAME"},
    {33, "НАСТРОЙКИ",    "OPTIONS"},
    {35, "ВЫХОД",        "QUIT"}
};

static constexpr TopTabDef kMainTabsCoop[] = {
    {34,               "ГЛАВНОЕ МЕНЮ", "MAIN MENU"},
    {1,                "НАЧАТЬ ИГРУ",  "START GAME"},
    {kPageMultiplayer, "МУЛЬТИПЛЕЕР",  "MULTIPLAYER"},
    {33,               "НАСТРОЙКИ",    "OPTIONS"},
    {35,               "ВЫХОД",        "QUIT"}
};

const TopTabDef* GetActiveTabs(bool inGame, int& outCount) {
    if (inGame) {
        outCount = static_cast<int>(std::size(kPauseTabs));
        return kPauseTabs;
    }
    if (integrations::sacoop::IsLoaded()) {
        outCount = static_cast<int>(std::size(kMainTabsCoop));
        return kMainTabsCoop;
    }
    outCount = static_cast<int>(std::size(kMainTabsSolo));
    return kMainTabsSolo;
}

bool IsModalPage(int8_t page) {
    switch (page) {
        case 6:  // NEW_GAME_ASK
        case 8:  // LOAD_MISSION_ASK
        case 11: // LOAD_GAME_ASK (verified @ 0x005772E1 -> YES switches to page 13)
        case 12: // DELETE_GAME_ASK (verified @ 0x005772BE -> YES switches to page 14)
        case 15: // DELETE_FINISHED
        case 17: // SAVE_WRITE_ASK (YES switches to page 18)
        case 19: // SAVE_DONE_1
        case 20: // SAVE_DONE_2
        case 21: // GAME_SAVED
        case 22: // CHEAT_WARNING
        case 23: // ASK_DISPLAY_DEFAULT_SETS
        case 24: // ASK_AUDIO_DEFAULT_SETS
        case 25: // ASK_CONTROLS_DEFAULT_SETS
        case 29: // SAVE_GAME_WARNING
        case 30: // SAVE_GAME_UNSUCCESSFUL
        case 31: // LOAD_GAME_WARNING
        case 32: // LOAD_GAME_UNSUCCESSFUL
        case 35: // QUIT_GAME_ASK
            return true;
        default:
            return false;
    }
}

bool IsDangerModalPage(int8_t page) {
    return page == 6 || page == 12 || page == 17 || page == 23 || page == 24 || page == 25 || page == 35;
}

bool ShouldShowTopTabs(int8_t page) {
    // Focused Safehouse Save Game flow hides pause tabs for a clean dedicated save screen
    if (page == 16 || page == 17 || page == 19 || page == 20 || page == 21 || page == 29 || page == 30) {
        return false;
    }
    return true;
}

int GetActiveTopTabIndex(bool inGame, int8_t effectivePage) {
    if (inGame) {
        if (effectivePage == 41) return 0;
        if (effectivePage == 5)  return 1;
        if (effectivePage == 1 || effectivePage == 6 || effectivePage == 8 || effectivePage == 9 ||
            effectivePage == 10 || effectivePage == 11 || effectivePage == 12 || effectivePage == 15 ||
            effectivePage == 22 || effectivePage == 31 || effectivePage == 32) {
            return 2;
        }
        if (effectivePage == 0)  return 3;
        if (effectivePage == 2)  return 4;
        if (effectivePage == 35) return 6;
        return 5; // Options & all settings subpages (including page 38 Redefine Controls)
    }

    const bool hasCoop = integrations::sacoop::IsLoaded();
    if (effectivePage == 34) return 0;
    if (effectivePage == 1 || effectivePage == 6 || effectivePage == 8 || effectivePage == 9 ||
        effectivePage == 10 || effectivePage == 11 || effectivePage == 12 || effectivePage == 15 ||
        effectivePage == 22 || effectivePage == 31 || effectivePage == 32) {
        return 1;
    }
    if (hasCoop && effectivePage == kPageMultiplayer) return 2;
    if (effectivePage == 35) return hasCoop ? 4 : 3;
    return hasCoop ? 3 : 2; // Options & subpages
}

std::string GetPageBreadcrumb(bool inGame, int8_t effectivePage, uint8_t* mm) {
    const std::string sep = " / ";
    switch (effectivePage) {
        case kPageMultiplayer: return ui::Loc("ГЛАВНОЕ МЕНЮ", "MAIN MENU") + sep + ui::Loc("МУЛЬТИПЛЕЕР", "MULTIPLAYER");
        case 9:  return ui::Loc("ИГРА", "GAME") + sep + ui::Loc("ЗАГРУЗИТЬ ИГРУ", "LOAD GAME");
        case 10: return ui::Loc("ИГРА", "GAME") + sep + ui::Loc("УДАЛИТЬ ИГРУ", "DELETE GAME");
        case 16: return ui::Loc("СОХРАНЕНИЕ ИГРЫ", "SAVE GAME");
        case 3:  return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("ЗВУК", "AUDIO");
        case 26: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("ЗВУК", "AUDIO") + sep + ui::Loc("СВОЯ МУЗЫКА", "USER TRACKS");
        case 4:  return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("ДИСПЛЕЙ", "DISPLAY");
        case 27: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("ДИСПЛЕЙ", "DISPLAY") + sep + ui::Loc("ДОПОЛНИТЕЛЬНО", "ADVANCED");
        case 28: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("ЯЗЫК", "LANGUAGE");
        case 36: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("УПРАВЛЕНИЕ", "CONTROLS");
        case 37: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("УПРАВЛЕНИЕ", "CONTROLS") + sep + ui::Loc("НАЗНАЧЕНИЕ КЛАВИШ", "REDEFINE CONTROLS");
        case 38: {
            const bool isVeh = (mm && sdk::ControlSubMode(mm) != 0);
            return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("УПРАВЛЕНИЕ", "CONTROLS") + sep +
                   (isVeh ? ui::Loc("В ТРАНСПОРТЕ", "VEHICLE CONTROLS") : ui::Loc("ПЕШКОМ", "FOOT CONTROLS"));
        }
        case 39: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("УПРАВЛЕНИЕ", "CONTROLS") + sep + ui::Loc("МЫШЬ", "MOUSE");
        case 40: return ui::Loc("НАСТРОЙКИ", "OPTIONS") + sep + ui::Loc("УПРАВЛЕНИЕ", "CONTROLS") + sep + ui::Loc("ГЕЙМПАД", "JOYPAD");
        default:
            (void)inGame;
            return "";
    }
}

std::string GetPageHeaderTitle(int8_t effectivePage, uint8_t* mm = nullptr) {
    switch (effectivePage) {
        case kPageMultiplayer: return ui::Loc("МУЛЬТИПЛЕЕР SA-COOP", "SA-COOP MULTIPLAYER");
        case 41: return ui::Loc("ПАУЗА", "PAUSE");
        case 34: return ui::Loc("ГЛАВНОЕ МЕНЮ", "MAIN MENU");
        case 1:  return ui::Loc("НАЧАТЬ ИГРУ", "START GAME");
        case 9:  return ui::Loc("ЗАГРУЗИТЬ ИГРУ", "LOAD GAME");
        case 10: return ui::Loc("УДАЛИТЬ ИГРУ", "DELETE GAME");
        case 16: return ui::Loc("СОХРАНИТЬ ИГРУ", "SAVE GAME");
        case 0:  return ui::Loc("КАТЕГОРИИ", "CATEGORIES");
        case 2:  return ui::Loc("БРИФИНГ", "BRIEF");
        case 33: return ui::Loc("НАСТРОЙКИ", "OPTIONS");
        case 3:  return ui::Loc("ЗВУК", "AUDIO");
        case 26: return ui::Loc("СВОЯ МУЗЫКА", "USER TRACKS");
        case 4:  return ui::Loc("ДИСПЛЕЙ", "DISPLAY");
        case 27: return ui::Loc("ДОПОЛНИТЕЛЬНО", "ADVANCED DISPLAY");
        case 28: return ui::Loc("ЯЗЫК", "LANGUAGE");
        case 36: return ui::Loc("УПРАВЛЕНИЕ", "CONTROLS");
        case 37: return ui::Loc("НАЗНАЧЕНИЕ КЛАВИШ", "REDEFINE CONTROLS");
        case 38:
            return (mm && sdk::ControlSubMode(mm) != 0)
                       ? ui::Loc("КЛАВИШИ В ТРАНСПОРТЕ", "VEHICLE CONTROLS")
                       : ui::Loc("КЛАВИШИ ПЕШКОМ", "FOOT CONTROLS");
        case 39: return ui::Loc("НАСТРОЙКИ МЫШИ", "MOUSE SETTINGS");
        case 40: return ui::Loc("НАСТРОЙКИ ГЕЙМПАДА", "JOYPAD SETTINGS");
        case 35: return ui::Loc("ВЫХОД ИЗ ИГРЫ", "QUIT GAME");
        case 6:  return ui::Loc("НОВАЯ ИГРА", "NEW GAME");
        case 8:  return ui::Loc("ЗАГРУЗКА МИССИИ", "LOAD MISSION");
        case 11: return ui::Loc("ЗАГРУЗКА СОХРАНЕНИЯ", "LOAD SAVE");
        case 12: return ui::Loc("УДАЛЕНИЕ СОХРАНЕНИЯ", "DELETE SAVE");
        case 15: return ui::Loc("УДАЛЕНИЕ ЗАВЕРШЕНО", "DELETE COMPLETE");
        case 17: return ui::Loc("ПЕРЕЗАПИСЬ", "OVERWRITE SAVE");
        case 23:
        case 24:
        case 25: return ui::Loc("СБРОС НАСТРОЕК", "RESTORE DEFAULTS");
        default: {
            if (const auto* scr = sdk::GetScreen(effectivePage)) {
                std::string k = sdk::ReadGxtKey8(scr->titleGxt);
                std::string t = sdk::GetGxtText(k.c_str());
                if (!t.empty()) return t;
            }
            return ui::Loc("МЕНЮ", "MENU");
        }
    }
}

// ============================================================================
// Bilingual Clean Copywriting Lookup (Rule 1f: Russian MSKL + English, zero |)
// ============================================================================
std::string GetCleanEntryLabel(uint8_t* mm, const std::string& gxtKey, uint8_t actionType) {
    if (actionType == 56) return ui::Loc("Разрешение экрана", "Resolution");
    if (actionType == 7) {
        return (sdk::ControlMethod(mm) == 0)
                   ? ui::Loc("Настройки мыши", "Mouse Settings")
                   : ui::Loc("Настройки геймпада", "Joypad Settings");
    }
    // WidescreenFix replaces FED_WIS (Widescreen) with Cutscene Borders!
    if (actionType == 26 && integrations::widescreen::IsCutsceneBordersActive()) {
        return ui::Loc("Полосы в роликах", "Cutscene Borders");
    }

    if (gxtKey == "FEP_RES") return ui::Loc("Продолжить игру", "Resume Game");
    if (gxtKey == "FEP_STG" || gxtKey == "FEH_SGA") return ui::Loc("Начать игру", "Start Game");
    if (gxtKey == "FEP_MAP" || gxtKey == "FEH_MAP") return ui::Loc("Карта", "Map");
    if (gxtKey == "FEP_BRI" || gxtKey == "FEH_BRI") return ui::Loc("Брифинг", "Brief");
    if (gxtKey == "FEP_STA") return ui::Loc("Статистика", "Stats");
    if (gxtKey == "FEP_OPT") return ui::Loc("Настройки", "Options");
    if (gxtKey == "FEP_QUI") return ui::Loc("Выйти из игры", "Quit Game");
    if (gxtKey == "FES_NGA") return ui::Loc("Новая игра", "New Game");
    if (gxtKey == "FES_LOA") return ui::Loc("Загрузить игру", "Load Game");
    if (gxtKey == "FES_DEL") return ui::Loc("Удалить игру", "Delete Game");
    if (gxtKey == "FEDS_TB") return ui::Loc("Назад", "Back");
    if (gxtKey == "FESZ_CA") return ui::Loc("Отмена", "Cancel");
    if (gxtKey == "FEM_YES") return ui::Loc("Да", "Yes");
    if (gxtKey == "FEM_NO")  return ui::Loc("Нет", "No");
    if (gxtKey == "FEM_OK" || gxtKey == "FEC_OKK") return ui::Loc("ОК", "OK");

    if (gxtKey == "FEO_CON" || gxtKey == "FEC_CON") return ui::Loc("Управление", "Controller Setup");
    if (gxtKey == "FEO_AUD" || gxtKey == "FEA_AUD") return ui::Loc("Звук", "Audio Setup");
    if (gxtKey == "FEO_DIS" || gxtKey == "FED_DIS") return ui::Loc("Дисплей", "Display Setup");
    if (gxtKey == "FEO_LAN" || gxtKey == "FEL_LAN" || gxtKey == "FEH_LAN") return ui::Loc("Язык", "Language");
    if (gxtKey == "FED_ADV") return ui::Loc("Дополнительно", "Advanced");
    if (gxtKey == "FET_DEF" || gxtKey == "FEA_RST" || gxtKey == "FEC_RST") return ui::Loc("Сбросить настройки", "Restore Defaults");
    if (gxtKey == "FEC_RED") return ui::Loc("Назначить клавиши", "Redefine Controls");
    if (gxtKey == "FEC_MOU") return ui::Loc("Настройки мыши", "Mouse Settings");
    if (gxtKey == "FEC_JOY") return ui::Loc("Настройки геймпада", "Joypad Settings");
    if (gxtKey == "FEC_CFG") return ui::Loc("Способ управления", "Configuration");
    if (gxtKey == "FET_CFT" || gxtKey == "FEC_FT")  return ui::Loc("Пешком", "Foot Controls");
    if (gxtKey == "FET_CCR" || gxtKey == "FEC_VEH") return ui::Loc("В транспорте", "Vehicle Controls");

    if (gxtKey == "FEA_MUS" || gxtKey == "FEA_RDO") return ui::Loc("Громкость радио", "Radio");
    if (gxtKey == "FEA_SFX") return ui::Loc("Громкость эффектов", "SFX");
    if (gxtKey == "FEA_BAS" || gxtKey == "FEA_EQ")  return ui::Loc("Эквалайзер радио", "Radio EQ");
    if (gxtKey == "FEA_ART") return ui::Loc("Автопоиск радио", "Radio Auto-Tune");
    if (gxtKey == "FEA_TIT" || gxtKey == "FEA_USS") return ui::Loc("Своя музыка", "User Track Options");
    if (gxtKey == "FEA_RSS") return ui::Loc("Радиостанция", "Radio Station");
    if (gxtKey == "FEA_MPM" || gxtKey == "FEA_PMD") return ui::Loc("Режим воспроизведения", "Play Mode");
    if (gxtKey == "FEA_AMS" || gxtKey == "FEA_ASC") return ui::Loc("Автосканирование", "Automatic Media Scan");
    if (gxtKey == "FEA_SCN") return ui::Loc("Сканировать треки", "Scan User Tracks");

    if (gxtKey == "FED_BRI") return ui::Loc("Яркость", "Brightness");
    if (gxtKey == "MAP_LEG" || gxtKey == "FED_LGD") return ui::Loc("Легенда карты", "Legend");
    if (gxtKey == "FED_RDR") return ui::Loc("Режим радара", "Radar Mode");
    if (gxtKey == "FED_HUD") return ui::Loc("Интерфейс HUD", "HUD Mode");
    if (gxtKey == "FED_SUB") return ui::Loc("Субтитры", "Subtitles");
    if (gxtKey == "FED_GLS") return ui::Loc("Сохранять фото", "Store Gallery Photos");
    if (gxtKey == "FEM_LOD" || gxtKey == "FED_DRW") return ui::Loc("Дальность прорисовки", "Draw Distance");
    if (gxtKey == "FEM_FRM" || gxtKey == "FED_FLM") return ui::Loc("Ограничитель кадров", "Frame Limiter");
    if (gxtKey == "FED_WIS") return ui::Loc("Широкоэкранный режим", "Widescreen");
    if (gxtKey == "FED_FXQ") return ui::Loc("Качество эффектов", "Visual FX Quality");
    if (gxtKey == "FED_MIP") return ui::Loc("Сглаживание текстур", "Mip Mapping");
    if (gxtKey == "FED_AAS") return ui::Loc("Сглаживание", "Anti Aliasing");
    if (gxtKey == "FED_RES" || gxtKey == "FED_RSL") return ui::Loc("Разрешение экрана", "Resolution");

    if (gxtKey == "FEC_MSH") return ui::Loc("Чувствительность мыши", "Mouse Sensitivity");
    if (gxtKey == "FEC_IVV") return ui::Loc("Инверсия мыши", "Invert Mouse Vertically");
    if (gxtKey == "FET_MST" || gxtKey == "FEC_SMW") return ui::Loc("Руление мышью", "Steer With Mouse");
    if (gxtKey == "FET_MFL" || gxtKey == "FEC_FMW") return ui::Loc("Полет мышью", "Fly With Mouse");
    if (gxtKey == "FEJ_INX" || gxtKey == "FEC_ILX") return ui::Loc("Инверсия левой оси X", "Invert Left Stick X");
    if (gxtKey == "FEJ_INY" || gxtKey == "FEC_ILY") return ui::Loc("Инверсия левой оси Y", "Invert Left Stick Y");
    if (gxtKey == "FEJ_INA" || gxtKey == "FEC_ILZ") return ui::Loc("Инверсия левой оси", "Invert Left Axis");
    if (gxtKey == "FEJ_RNX" || gxtKey == "FEC_IRX") return ui::Loc("Инверсия правой оси X", "Invert Right Stick X");
    if (gxtKey == "FEJ_RNY" || gxtKey == "FEC_IRY") return ui::Loc("Инверсия правой оси Y", "Invert Right Stick Y");
    if (gxtKey == "FEJ_RNA" || gxtKey == "FEC_IRZ") return ui::Loc("Инверсия правой оси", "Invert Right Axis");

    if (gxtKey == "FES_PLA") return ui::Loc("Игрок", "Player");
    if (gxtKey == "FES_MON") return ui::Loc("Деньги", "Money");
    if (gxtKey == "FES_WEA") return ui::Loc("Оружие", "Weapons");
    if (gxtKey == "FES_CRI") return ui::Loc("Преступления", "Crimes");
    if (gxtKey == "FES_GAN") return ui::Loc("Банды", "Gangs");
    if (gxtKey == "FES_ACH") return ui::Loc("Достижения", "Achievements");
    if (gxtKey == "FES_MIS") return ui::Loc("Миссии", "Mission");
    if (gxtKey == "FES_MSC") return ui::Loc("Прочее", "Misc");

    if (gxtKey == "FEL_ENG") return ui::Loc("Английский", "English");
    if (gxtKey == "FEL_FRE") return ui::Loc("Французский", "French");
    if (gxtKey == "FEL_GER") return ui::Loc("Немецкий", "German");
    if (gxtKey == "FEL_ITA") return ui::Loc("Итальянский", "Italian");
    if (gxtKey == "FEL_SPA") return ui::Loc("Испанский", "Spanish");

    if (actionType == 2) return ui::Loc("Назад", "Back");

    std::string gxt = sdk::GetGxtText(gxtKey.c_str());
    if (!gxt.empty()) return gxt;
    return gxtKey;
}

std::string GetEntryDescription(uint8_t actionType, const std::string& gxtKey, int8_t page, bool inGame) {
    if (actionType == 26 && integrations::widescreen::IsCutsceneBordersActive()) {
        return ui::Loc("Настройка черных полос во время сюжетных роликов от Widescreen Fix.",
                       "Configure black cutscene border mode provided by Widescreen Fix.");
    }
    if (actionType == 24 && integrations::widescreen::IsFrameLimiterExtended()) {
        return ui::Loc("Выбор лимита частоты кадров от Widescreen Fix. Используйте A/D или клик.",
                       "Choose gameplay FPS cap from Widescreen Fix. Use A/D or click to cycle.");
    }
    if (actionType == 43 && inGame) {
        return ui::Loc("Изменение сглаживания текстур доступно только в главном меню до начала игры.",
                       "Mip mapping can only be changed from the Main Menu before starting a game.");
    }
    if (inGame && integrations::sacoop::IsMultiplayerSessionActive() &&
        (gxtKey == "FES_NGA" || gxtKey == "FES_LOA" || gxtKey == "FES_DEL")) {
        return ui::Loc("В сетевой игре SA-COOP перезапуск и загрузка сохранений отключены для стабильной синхронизации.",
                       "Restarting or loading saves is disabled during an active SA-COOP session to protect sync.");
    }

    if (gxtKey == "FEP_RES") return ui::Loc("Закрыть меню паузы и вернуться в игру.", "Close the pause menu and return to the game.");
    if (gxtKey == "FEP_STG" || gxtKey == "FEH_SGA") return ui::Loc("Начать новую историю, загрузить сохранение или удалить ненужный слот.", "Start a new story, load a saved slot, or delete an old save.");
    if (gxtKey == "FEP_MAP" || gxtKey == "FEH_MAP") return ui::Loc("Интерактивная карта всего штата Сан-Андреас с метками и масштабом.", "Interactive map of San Andreas with waypoints, zoom, and legend.");
    if (gxtKey == "FEP_BRI" || gxtKey == "FEH_BRI") return ui::Loc("История последних заданий, подсказок и сообщений игры.", "History of recent mission briefs, tips, and game messages.");
    if (gxtKey == "FEP_STA") return ui::Loc("Подробная статистика персонажа, навыков, банд, миссий и оружия.", "Detailed statistics for player skills, gangs, missions, and weapons.");
    if (gxtKey == "FEP_OPT") return ui::Loc("Настройка звука, графики, управления, языка и конфигурации модов.", "Configure audio, display, controls, language, and mod settings.");
    if (gxtKey == "FEP_QUI") return ui::Loc("Завершить сеанс и выйти на рабочий стол.", "Exit the current session and return to desktop.");
    if (gxtKey == "FES_NGA") return ui::Loc("Начать прохождение с самого начала с чистым прогрессом.", "Begin a fresh story playthrough from the start.");
    if (gxtKey == "FES_LOA") return ui::Loc("Выбрать и загрузить один из восьми слотов сохранения.", "Choose and load any of the eight save slots.");
    if (gxtKey == "FES_DEL") return ui::Loc("Безвозвратно удалить выбранный файл сохранения.", "Permanently remove a selected save file.");

    if (gxtKey == "FEO_CON" || gxtKey == "FEC_CON") return ui::Loc("Назначение клавиш, чувствительность мыши и параметры геймпада.", "Key bindings, mouse sensitivity, and controller options.");
    if (gxtKey == "FEO_AUD" || gxtKey == "FEA_AUD") return ui::Loc("Громкость радио и эффектов, выбор радиостанции и своей музыки.", "Radio and sound effect levels, radio station, and user tracks.");
    if (gxtKey == "FEO_DIS" || gxtKey == "FED_DIS") return ui::Loc("Яркость, радар, субтитры, разрешение и качество графики.", "Brightness, radar, subtitles, resolution, and visual quality.");
    if (gxtKey == "FEO_LAN" || gxtKey == "FEL_LAN" || gxtKey == "FEH_LAN") return ui::Loc("Выбор языка игровых текстов и субтитров.", "Select the game text and subtitle language.");
    if (gxtKey == "FED_ADV") return ui::Loc("Дальность прорисовки, лимит кадров, полосы в роликах, сглаживание и разрешение.", "Draw distance, frame limiter, cutscene borders, anti-aliasing, and resolution.");
    if (gxtKey == "FEA_TIT" || gxtKey == "FEA_USS") return ui::Loc("Порядок воспроизведения и сканирование папки пользовательской музыки.", "Playback mode and scanning for custom user music tracks.");
    if (gxtKey == "FET_DEF") return ui::Loc("Сбросить параметры этого раздела к стандартным значениям.", "Restore all settings in this section to default values.");
    if (gxtKey == "FEC_RED") return ui::Loc("Назначить собственные клавиши для ходьбы и транспорта.", "Assign custom key bindings for on-foot and vehicle actions.");
    if (gxtKey == "FET_CFT") return ui::Loc("Назначить клавиши для передвижения и стрельбы пешком.", "Configure key bindings for on-foot movement and combat.");
    if (gxtKey == "FET_CCR") return ui::Loc("Назначить клавиши для вождения, полетов и управления транспортом.", "Configure key bindings for driving, flying, and vehicle controls.");
    if (gxtKey == "FEC_MOU" || actionType == 7) return ui::Loc("Настройка чувствительности, инверсии и управления транспортом.", "Adjust sensitivity, axis inversion, and vehicle controls.");
    if (gxtKey == "FEC_JOY") return ui::Loc("Инверсия осей стиков и триггеров подключенного геймпада.", "Invert stick and trigger axes for a connected controller.");
    if (gxtKey == "FEA_SCN") return ui::Loc("Просканировать пользовательскую папку на наличие новых треков.", "Scan your user tracks folder for custom music files.");

    switch (actionType) {
        case 27: return ui::Loc("Регулировка общей яркости изображения в игре.", "Adjust overall in-game display brightness.");
        case 61: return ui::Loc("Дальность отображения объектов и мира на горизонте.", "Controls how far world geometry and objects render.");
        case 28: return ui::Loc("Общая громкость музыки и радиостанций в транспорте.", "Master volume for vehicle radio stations and music.");
        case 29: return ui::Loc("Громкость звуковых эффектов, выстрелов, двигателей и речи.", "Master volume for sound effects, weapons, vehicles, and speech.");
        case 30: return ui::Loc("Выравнивание частот и громкости радиоэфира.", "Balances radio frequency response and audio clarity.");
        case 31: return ui::Loc("Автоматический выбор любимой радиостанции при посадке в авто.", "Automatically tunes to your preferred station when entering vehicles.");
        case 32: return ui::Loc("Прослушать и выбрать радиостанцию по умолчанию.", "Preview and select your default radio station.");
        case 33: return ui::Loc("Показывать список значков и обозначений на карте.", "Display icon markers and map legend overlay.");
        case 34: return ui::Loc("Отображать карту со значками, только значки или скрыть радар.", "Show full map and blips, blips only, or hide the radar.");
        case 35: return ui::Loc("Включить или скрыть основные элементы игрового интерфейса.", "Toggle main HUD indicators such as health, money, and weapon.");
        case 25: return ui::Loc("Показывать текст диалогов во время роликов и разговоров.", "Display dialogue subtitles during cutscenes and conversations.");
        case 65: return ui::Loc("Сохранять снимки с фотоаппарата в папку пользователя.", "Save photos taken with the in-game camera to user files.");
        case 24: return ui::Loc("Ограничение частоты кадров для стабильной физики движка.", "Caps frame rate for consistent classic engine physics.");
        case 26: return ui::Loc("Адаптация поля зрения под широкоформатные мониторы.", "Adapts field of view for widescreen monitors.");
        case 42: return ui::Loc("Детализация теней, частиц, отражений и растительности.", "Controls shadow, particle, reflection, and foliage detail.");
        case 43: return ui::Loc("Смягчение мерцания текстур на дальних дистанциях.", "Reduces texture shimmering at longer viewing distances.");
        case 44: {
            const auto& wsf = integrations::widescreen::GetIniSummary();
            if (wsf.loaded && wsf.smaa) {
                return ui::Loc("Активно сглаживание SMAA из Widescreen Fix. Для стабильности держите MSAA выключенным.",
                               "Widescreen Fix SMAA is active. Keep MSAA off for stability.");
            }
            return ui::Loc("Сглаживание ступенчатых краев. Нажмите Enter для применения.",
                           "Smooths jagged geometry edges. Press Enter to apply.");
        }
        case 56: return ui::Loc("Выбор разрешения экрана через A/D. Нажмите Enter для применения.",
                                "Cycle display resolution with A/D. Press Enter to apply.");
        case 58: return ui::Loc("Переключение между клавиатурой с мышью и геймпадом.", "Switch between keyboard with mouse and controller input.");
        case 62: return ui::Loc("Скорость поворота камеры и прицела при движении мыши.", "Adjusts camera and aiming turn speed for mouse movement.");
        case 46: return ui::Loc("Инвертировать вертикальное направление обзора мышью.", "Invert vertical mouse look direction.");
        case 59: return ui::Loc("Использовать движения мыши для управления автомобилем.", "Steer land vehicles using mouse movement.");
        case 60: return ui::Loc("Использовать мышь для управления самолетами и вертолетами.", "Control aircraft pitch and roll with the mouse.");
        case 63: return ui::Loc("Порядок воспроизведения треков из пользовательской папки.", "Choose radio, random, or sequential user track playback.");
        case 64: return ui::Loc("Автоматически проверять новые треки при запуске игры.", "Automatically scan for new custom audio files on startup.");
        case 2:  return ui::Loc("Вернуться на предыдущий экран меню.", "Return to the previous menu screen.");
        default: break;
    }

    if (page == 33 && gxtKey != "FEDS_TB") {
        return ui::Loc("Открыть настройки подключенных модификаций.", "Open settings for installed game modifications.");
    }
    if (page == 28) {
        return ui::Loc("Переключить язык игровых сообщений и субтитров.", "Switch the active game text and subtitle language.");
    }
    return "";
}

// ============================================================================
// Live Value & Slider Formatting / Adjustment
// ============================================================================
bool IsSliderAction(uint8_t actionType) {
    return actionType == 27 || actionType == 28 || actionType == 29 ||
           actionType == 61 || actionType == 62;
}

float ReadSliderFrac01(uint8_t* mm, uint8_t actionType) {
    switch (actionType) {
        case 27: return std::clamp(static_cast<float>(sdk::Brightness(mm)) / 384.0f, 0.0f, 1.0f);
        case 28: return std::clamp(static_cast<float>(sdk::RadioVolume(mm)) / 64.0f, 0.0f, 1.0f);
        case 29: return std::clamp(static_cast<float>(sdk::SfxVolume(mm)) / 64.0f, 0.0f, 1.0f);
        case 61: return std::clamp((sdk::DrawDistance(mm) - 0.925f) / (1.8f - 0.925f), 0.0f, 1.0f);
        case 62: return std::clamp((sdk::MouseSens() - 0.0003125f) / (0.005f - 0.0003125f), 0.0f, 1.0f);
        default: return 0.0f;
    }
}

void ApplySliderTargetFrac(uint8_t* mm, int rawItemIdx, uint8_t actionType, float targetFrac01) {
    if (!IsSliderAction(actionType)) return;
    targetFrac01 = std::clamp(targetFrac01, 0.0f, 1.0f);
    sdk::SelectedItem(mm) = rawItemIdx;

    for (int step = 0; step < 20; ++step) {
        float cur  = ReadSliderFrac01(mm, actionType);
        float diff = targetFrac01 - cur;
        if (std::fabs(diff) < 0.032f) break;
        int8_t dir = (diff > 0.0f) ? 1 : -1;
        sdk::ProcessUserInput(mm, 0, 0, 0, 0, dir);
        float next = ReadSliderFrac01(mm, actionType);
        if (std::fabs(next - cur) < 0.0005f) break;
        if ((dir > 0 && next >= targetFrac01) || (dir < 0 && next <= targetFrac01)) break;
    }
}

std::string FormatBoolValue(bool on, sa::CRGBA& outCol) {
    const auto& th = ui::Theme();
    outCol = on ? th.textSuccess : th.textCol;
    return on ? ui::Loc("ВКЛ", "ON") : ui::Loc("ВЫКЛ", "OFF");
}

void ReadOptionValueDisplay(uint8_t* mm, uint8_t actionType, std::string& outVal, sa::CRGBA& outCol) {
    const auto& th = ui::Theme();
    outVal.clear();
    outCol = th.textNormal;

    switch (actionType) {
        case 24: {
            uint8_t fl = sdk::FrameLimiter(mm);
            outVal = integrations::widescreen::FormatFrameLimiterValue(fl, ui::UseRussianFlag());
            outCol = (fl != 0) ? th.textSuccess : th.textCol;
            break;
        }
        case 25: outVal = FormatBoolValue(sdk::ShowSubtitles(mm) != 0, outCol); break;
        case 26: {
            uint8_t ws = sdk::Widescreen(mm);
            outVal = integrations::widescreen::FormatCutsceneBordersValue(ws, ui::UseRussianFlag());
            outCol = (ws != 0) ? th.textSuccess : th.textCol;
            break;
        }
        case 30: outVal = FormatBoolValue(sdk::RadioEq(mm) != 0, outCol); break;
        case 31: outVal = FormatBoolValue(sdk::RadioAutoSelect(mm) != 0, outCol); break;
        case 33: outVal = FormatBoolValue(sdk::ShowLegend(mm) != 0, outCol); break;
        case 35: outVal = FormatBoolValue(sdk::HudOn(mm) != 0, outCol); break;
        case 43: outVal = FormatBoolValue(sdk::MipMapping(mm) != 0, outCol); break;
        case 46: outVal = FormatBoolValue(sdk::InvertMouseY(), outCol); break;
        case 59: outVal = FormatBoolValue(sdk::SteerWithMouse(), outCol); break;
        case 60: outVal = FormatBoolValue(sdk::FlyWithMouse(), outCol); break;
        case 64: outVal = FormatBoolValue(sdk::RadioAutoScan(mm) != 0, outCol); break;
        case 65: outVal = FormatBoolValue(sdk::SavePhotos(mm) != 0, outCol); break;
        case 47: case 48: case 49: case 50: case 51: case 52:
            outVal = FormatBoolValue(sdk::JoypadFlag(mm, actionType - 47) != 0, outCol);
            break;
        case 32: {
            const char* st = sdk::GetRadioStationName(sdk::RadioStation(mm));
            outVal = (st && st[0]) ? std::string(st) : ui::Loc("Выкл", "Off");
            outCol = th.textSelected;
            break;
        }
        case 34: {
            int32_t m = sdk::RadarMode(mm);
            if (m == 0)      { outVal = ui::Loc("Карта и метки", "Map and Blips"); outCol = th.textSuccess; }
            else if (m == 1) { outVal = ui::Loc("Только метки", "Blips Only");     outCol = th.textWarning; }
            else             { outVal = ui::Loc("ВЫКЛ", "OFF");                    outCol = th.textCol; }
            break;
        }
        case 42: {
            int q = sdk::GetFxQuality();
            if (q <= 0)      outVal = ui::Loc("Низкое", "Low");
            else if (q == 1) outVal = ui::Loc("Среднее", "Medium");
            else if (q == 2) outVal = ui::Loc("Высокое", "High");
            else             outVal = ui::Loc("Очень высокое", "Very High");
            outCol = th.textSelected;
            break;
        }
        case 44: {
            int32_t aa = sdk::PrefsAntialiasing(mm);
            if (aa <= 1) { outVal = ui::Loc("ВЫКЛ", "OFF"); outCol = th.textCol; }
            else {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%dx", aa);
                outVal = buf;
                outCol = (aa != sdk::CurrentAntialiasing(mm)) ? th.textWarning : th.textSuccess;
            }
            break;
        }
        case 56: {
            outVal = sdk::GetVideoModeName(sdk::PrefsVideoMode(mm));
            if (outVal.empty()) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%dx%d", sa::GetScreenWidth(), sa::GetScreenHeight());
                outVal = buf;
            }
            outCol = (sdk::PrefsVideoMode(mm) != sdk::DisplayVideoMode(mm)) ? th.textWarning : th.textSelected;
            break;
        }
        case 58: {
            outVal = (sdk::ControlMethod(mm) == 0)
                         ? ui::Loc("Мышь и клавиши", "Mouse and Keys")
                         : ui::Loc("Геймпад", "Joypad");
            outCol = th.textSelected;
            break;
        }
        case 63: {
            int8_t rm = sdk::RadioMode(mm);
            if (rm == 0)      outVal = ui::Loc("Радио", "Radio");
            else if (rm == 1) outVal = ui::Loc("Случайно", "Random");
            else              outVal = ui::Loc("По порядку", "Sequential");
            outCol = th.textSelected;
            break;
        }
        case 37: case 38: case 39: case 40: case 41: {
            int langIdx = actionType - 37;
            if (sdk::Language(mm) == langIdx) {
                outVal = ui::Loc("АКТИВЕН", "ACTIVE");
                outCol = th.textSuccess;
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Page Entry Representation & UI Runtime State
// ============================================================================
struct ResolvedEntry {
    int                rawIndex   = 0;
    uint8_t            actionType = 0;
    uint8_t            entryType  = 0;
    int8_t             targetMenu = 0;
    std::string        gxtKey;
    ui::MenuOptionItem uiItem;
    int                saveSlot0  = -1;
};

enum class MpRowId : int {
    HostServer = 0,
    JoinServer,
    OpenCoopWindow,
    PlayerName,
    HostSlot,
    HostAddress,
    Port,
    Password,
    MaxPlayers,
    FriendlyFire,
    PartnerTeleport,
    SharedLife,
    PartnerCheats,
    VoiceEnabled,
    GpsNav,
    Count
};

struct MenuRuntimeState {
    bool     wasMenuActive    = false;
    bool     coopInitDone     = false;
    bool     inMultiplayerTab = false;
    int8_t   lastPage         = -1;
    uint32_t openedAtMs       = 0;
    uint32_t lastTickMs       = 0;
    uint32_t lastInputMs      = 0;
    int      hoveredTab       = -1;
    int      hoveredRow       = -1;
    int      hoveredModalBtn  = -1;
    int      statsScroll      = 0;
    int      lastStatsCat     = -1;
    int      briefScroll      = 0;
    int      controlsScroll   = 0;
    int      mpSelected       = 0;
    int      mpScroll         = 0;
    bool     mpCfgLoaded      = false;
    integrations::sacoop::CoopSettings mpCfg{};
    std::string mpStatusMsg;
    bool     mpStatusIsError  = false;

    // Inline text-input modal for SA-COOP nickname / host / port / password
    bool        textEditActive = false;
    MpRowId     textEditField  = MpRowId::PlayerName;
    std::string textEditBuf;
    int         textEditMaxLen = 24;
    bool        textEditNumeric = false;
    bool        prevKeyState[256] = {};

    bool     draggingSlider   = false;
    int      dragRawItem      = -1;
    uint8_t  dragActionType   = 0;
    bool     prevLmb          = false;
    bool     prevRmb          = false;
    bool     prevEnter        = false;
    bool     prevF            = false;
    bool     prevBack         = false;
    bool     prevEsc          = false;
    bool     prevTab          = false;
    bool     prevUp           = false;
    bool     prevDown         = false;
    bool     prevLeft         = false;
    bool     prevRight        = false;
    bool     prevQ            = false;
    bool     prevE            = false;
    bool     prevPgUp         = false;
    bool     prevPgDn         = false;
    uint32_t holdDirStartMs   = 0;
    uint32_t lastHoldStepMs   = 0;
    int      lastWinClientX   = -1;
    int      lastWinClientY   = -1;
};

MenuRuntimeState g_st{};

void EnsureCoopHookedOnce() {
    if (g_st.coopInitDone) return;
    if (!integrations::sacoop::IsLoaded()) {
        g_st.coopInitDone = true;
        return;
    }
    bool sup = integrations::sacoop::SuppressStartupConnectPopup();
    int  pat = integrations::sacoop::ApplyDarkUiKitTheme();
    g_st.coopInitDone = true;
    modlog::Info("SA-COOP runtime init (suppress=%d, themePatches=%d)", sup ? 1 : 0, pat);
}

// Guarantees we never linger on an in-game-only page while on the Title Screen (prevents 0056E301 crash!)
int8_t SanitizeAndGetEffectivePage(uint8_t* mm) {
    const bool inGame = sdk::IsInGame(mm);
    if (inGame) {
        g_st.inMultiplayerTab = false;
        g_st.textEditActive   = false;
        if (sdk::CurrentPage(mm) == 34) {
            sdk::CurrentPage(mm) = 41;
        }
        // In active SA-COOP session, block unsafe Load/Delete/NewGame pages
        if (integrations::sacoop::IsMultiplayerSessionActive()) {
            int8_t p = sdk::CurrentPage(mm);
            if (p == 6 || p == 9 || p == 10 || p == 11 || p == 12) {
                sdk::CurrentPage(mm) = 1;
            }
        }
        return sdk::CurrentPage(mm);
    }

    // Not in-game (Main / Title Menu)
    int8_t p = sdk::CurrentPage(mm);
    if (p == 0 || p == 2 || p == 5 || p == 41) {
        sdk::CurrentPage(mm) = 34;
        sdk::SelectedItem(mm) = 0;
        p = 34;
    }
    if (g_st.inMultiplayerTab && integrations::sacoop::IsLoaded()) {
        return kPageMultiplayer;
    }
    return p;
}

void SyncCursorPosition(uint8_t* mm, float sw, float sh, float& outMx, float& outMy) {
    float mx = static_cast<float>(sdk::MouseWinX(mm));
    float my = static_cast<float>(sdk::MouseWinY(mm));

    if (integrations::sacoop::IsGameWindowFocused()) {
        HWND fg = GetForegroundWindow();
        POINT pt{};
        RECT rc{};
        if (fg && GetCursorPos(&pt) && ScreenToClient(fg, &pt) && GetClientRect(fg, &rc)) {
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;
            if (cw > 64 && ch > 64 && pt.x >= 0 && pt.y >= 0 && pt.x <= cw && pt.y <= ch) {
                if (g_st.lastWinClientX >= 0 &&
                    (pt.x != g_st.lastWinClientX || pt.y != g_st.lastWinClientY)) {
                    mx = static_cast<float>(pt.x) * (sw / static_cast<float>(cw));
                    my = static_cast<float>(pt.y) * (sh / static_cast<float>(ch));
                    sdk::MouseWinX(mm) = static_cast<int32_t>(mx);
                    sdk::MouseWinY(mm) = static_cast<int32_t>(my);
                    sdk::ShowMouse(mm) = 1;
                }
                g_st.lastWinClientX = pt.x;
                g_st.lastWinClientY = pt.y;
            }
        }
    }

    outMx = std::clamp(mx, 0.0f, sw);
    outMy = std::clamp(my, 0.0f, sh);
}

bool IsSaCoopOverlayBlockingInput() {
    if (!integrations::sacoop::IsLoaded()) return false;
    if (integrations::sacoop::IsAnyCoopMenuVisible()) return true;
    // Fallback for any other fullscreen OS-cursor overlay
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) != 0) {
        HWND fg = GetForegroundWindow();
        if (fg) {
            LONG style = GetWindowLongA(fg, GWL_STYLE);
            if ((style & WS_CAPTION) == 0) return true;
        }
    }
    return false;
}

void BuildPageEntries(uint8_t* mm, int8_t page, std::vector<ResolvedEntry>& outEntries, std::string& outStaticPrompt) {
    outEntries.clear();
    outStaticPrompt.clear();
    const auto* scr = sdk::GetScreen(page);
    if (!scr) return;

    const auto& th     = ui::Theme();
    const bool  inGame = sdk::IsInGame(mm);
    const bool  inCoop = inGame && integrations::sacoop::IsMultiplayerSessionActive();

    for (int i = 0; i < 12; ++i) {
        const auto& raw = scr->items[i];
        if (raw.actionType == 0) continue;

        std::string key = sdk::ReadGxtKey8(raw.gxtKey);
        if (raw.actionType == 1) {
            if (outStaticPrompt.empty()) {
                outStaticPrompt = sdk::GetGxtText(key.c_str());
            }
            continue;
        }

        ResolvedEntry e{};
        e.rawIndex   = i;
        e.actionType = raw.actionType;
        e.entryType  = raw.entryType;
        e.targetMenu = raw.targetMenu;
        e.gxtKey     = key;
        e.uiItem.isBack  = (raw.actionType == 2 || key == "FEDS_TB");
        e.uiItem.enabled = true;

        // Disable Mip Mapping in-game (native GTA SA only allows changing it on Main Menu @ 0x57CEE1!)
        if (raw.actionType == 43 && inGame) {
            e.uiItem.enabled = false;
        }
        // In active SA-COOP multiplayer session, lock New Game / Load Game / Delete Game!
        if (inCoop && page == 1 && (key == "FES_NGA" || key == "FES_LOA" || key == "FES_DEL")) {
            e.uiItem.enabled = false;
        }

        if ((page == 9 || page == 10 || page == 16) && raw.entryType >= 1 && raw.entryType <= 8) {
            int slot0 = static_cast<int>(raw.entryType) - 1;
            e.saveSlot0 = slot0;
            auto info = sdk::GetSaveSlotInfo(slot0);
            char prefix[16];
            std::snprintf(prefix, sizeof(prefix), "%d. ", info.slot1to8);
            if (info.status == 0) {
                e.uiItem.labelGame  = std::string(prefix) + (info.nameGame.empty() ? ui::Loc("Сохранение", "Saved Game") : info.nameGame);
                e.uiItem.valueGame  = info.dateGame;
                e.uiItem.valueColor = th.textCol;
                e.uiItem.enabled    = true;
            } else if (info.status == 2) {
                e.uiItem.labelGame  = std::string(prefix) + ui::Loc("Поврежденный слот", "Corrupt Save");
                e.uiItem.valueGame  = "";
                e.uiItem.valueColor = th.textDanger;
                e.uiItem.enabled    = (page == 10 || page == 16);
            } else {
                e.uiItem.labelGame  = std::string(prefix) + ui::Loc("Пустой слот", "Empty Slot");
                e.uiItem.valueGame  = "";
                e.uiItem.valueColor = th.textDisabled;
                e.uiItem.enabled    = (page == 16);
            }
        } else {
            e.uiItem.labelGame = GetCleanEntryLabel(mm, key, raw.actionType);
            if (IsSliderAction(raw.actionType)) {
                e.uiItem.hasSlider    = true;
                e.uiItem.sliderFrac01 = ReadSliderFrac01(mm, raw.actionType);
            } else {
                ReadOptionValueDisplay(mm, raw.actionType, e.uiItem.valueGame, e.uiItem.valueColor);
                if (inCoop && page == 1 && !e.uiItem.enabled) {
                    e.uiItem.valueGame  = ui::Loc("SA-COOP", "SA-COOP");
                    e.uiItem.valueColor = th.textDisabled;
                }
            }
        }

        outEntries.push_back(std::move(e));
    }
}

int FindVisibleIndexForRaw(const std::vector<ResolvedEntry>& entries, int rawIdx) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].rawIndex == rawIdx) return static_cast<int>(i);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].uiItem.enabled) return static_cast<int>(i);
    }
    return entries.empty() ? -1 : 0;
}

void BuildTopBar(uint8_t* mm, int8_t effectivePage, bool showTabs, const std::string& breadcrumb, ui::MenuTopBar& outBar) {
    const bool inGame = sdk::IsInGame(mm);
    std::string mastLeft = ui::Loc("SAN ANDREAS", "SAN ANDREAS");
    std::string mastRight;
    sa::CRGBA   rightCol = ui::Theme().textCol;

    if (inGame) {
        char buf[160];
        int32_t money = sa::GetPlayerMoney();
        uint8_t hh    = sa::GetClockHours();
        uint8_t mmClk = sa::GetClockMinutes();
        auto* bus     = modbus::GetBus();
        std::string modePrefix;
        if (integrations::sacoop::IsMultiplayerSessionActive()) {
            modePrefix = integrations::sacoop::IsFollowerLive()
                             ? (ui::Loc("SA-COOP КЛИЕНТ", "SA-COOP CLIENT") + "  -  ")
                             : (ui::Loc("SA-COOP ХОСТ", "SA-COOP HOST") + "  -  ");
        }
        if (bus && bus->progressionInstalled) {
            std::snprintf(buf, sizeof(buf), "%s%s %d  -  $%d  -  %02u:%02u",
                          modePrefix.c_str(),
                          ui::Loc("УР.", "RANK").c_str(),
                          std::max(1, bus->playerRank),
                          money,
                          static_cast<unsigned>(hh),
                          static_cast<unsigned>(mmClk));
        } else {
            std::snprintf(buf, sizeof(buf), "%s$%d  -  %02u:%02u",
                          modePrefix.c_str(),
                          money,
                          static_cast<unsigned>(hh),
                          static_cast<unsigned>(mmClk));
        }
        mastRight = buf;
        rightCol  = ui::Theme().textNormal;
    } else {
        mastRight = (effectivePage == kPageMultiplayer)
                        ? ui::Loc("СЕТЕВАЯ ИГРА SA-COOP", "SA-COOP MULTIPLAYER")
                        : ui::Loc("ГЛАВНОЕ МЕНЮ", "MAIN MENU");
    }

    outBar.SetMasthead(mastLeft, mastRight, rightCol)
          .SetBreadcrumb(breadcrumb)
          .SetShowSwitchHints(showTabs)
          .ClearTabs();

    if (showTabs) {
        int tabCount = 0;
        const TopTabDef* tabs = GetActiveTabs(inGame, tabCount);
        const int activeIdx   = GetActiveTopTabIndex(inGame, effectivePage);
        for (int i = 0; i < tabCount; ++i) {
            outBar.AddTab(ui::Loc(tabs[i].rus, tabs[i].eng),
                          i == activeIdx, i == g_st.hoveredTab, true);
        }
    }
}

// ============================================================================
// Virtual Page: SA-COOP MULTIPLAYER HUB (Main Menu ONLY!)
// ============================================================================
void EnsureMpConfigLoaded() {
    if (!g_st.mpCfgLoaded) {
        g_st.mpCfg       = integrations::sacoop::LoadCoopSettings();
        g_st.mpCfgLoaded = true;
    }
}

std::vector<ui::MenuOptionItem> BuildMultiplayerItems() {
    EnsureMpConfigLoaded();
    const auto& th = ui::Theme();
    const auto& c  = g_st.mpCfg;
    std::vector<ui::MenuOptionItem> out;
    out.reserve(static_cast<size_t>(MpRowId::Count));

    auto add = [&](const std::string& lbl, const std::string& val, sa::CRGBA col, bool enabled = true) {
        ui::MenuOptionItem it{};
        it.labelGame  = lbl;
        it.valueGame  = val;
        it.valueColor = col;
        it.enabled    = enabled;
        out.push_back(std::move(it));
    };

    char slotBuf[32];
    if (c.loadSlot == 0) {
        std::snprintf(slotBuf, sizeof(slotBuf), "%s", ui::Loc("Новая игра", "New Game").c_str());
    } else {
        std::snprintf(slotBuf, sizeof(slotBuf), "%s %d", ui::Loc("Слот", "Slot").c_str(), c.loadSlot);
    }
    char portBuf[16], maxPBuf[16];
    std::snprintf(portBuf, sizeof(portBuf), "%d", c.port);
    std::snprintf(maxPBuf, sizeof(maxPBuf), "%d", c.maxPlayers);

    auto boolStr = [&](bool v, sa::CRGBA& col) -> std::string {
        col = v ? th.textSuccess : th.textCol;
        return v ? ui::Loc("ВКЛ", "ON") : ui::Loc("ВЫКЛ", "OFF");
    };

    add(ui::Loc("Создать сервер", "Host Server"),
        ui::Loc("ЗАПУСК", "START"), th.textSuccess);
    add(ui::Loc("Подключиться к серверу", "Join Server"),
        ui::Loc("ВХОД", "CONNECT"), th.textSelected);
    add(ui::Loc("Открыть окно SA-COOP", "Open SA-COOP Window"),
        "F9", th.textCol);
    add(ui::Loc("Имя игрока", "Player Name"),
        c.playerName.empty() ? "Player" : c.playerName, th.textSelected);
    add(ui::Loc("Сохранение хоста", "Host Save Slot"),
        slotBuf, th.textNormal);
    add(ui::Loc("Адрес или код", "Address or Code"),
        c.host.empty() ? "127.0.0.1" : c.host, th.textNormal);
    add(ui::Loc("Порт сервера", "Server Port"),
        portBuf, th.textNormal);
    add(ui::Loc("Пароль", "Password"),
        c.password.empty() ? ui::Loc("Нет", "None") : std::string(c.password.size(), '*'), th.textCol);
    add(ui::Loc("Максимум игроков", "Max Players"),
        maxPBuf, th.textNormal);

    sa::CRGBA col{};
    std::string v;
    v = boolStr(c.friendlyFire, col);    add(ui::Loc("Огонь по своим", "Friendly Fire"), v, col);
    v = boolStr(c.partnerTeleport, col); add(ui::Loc("Телепорт напарника", "Partner Teleport"), v, col);
    v = boolStr(c.sharedLife, col);      add(ui::Loc("Общая жизнь в миссиях", "Shared Mission Life"), v, col);
    v = boolStr(c.partnerCheats, col);   add(ui::Loc("Чит-коды напарников", "Partner Cheats"), v, col);
    v = boolStr(c.voiceEnabled, col);    add(ui::Loc("Голосовой чат", "Voice Chat"), v, col);
    v = boolStr(c.gps, col);             add(ui::Loc("Навигатор GPS", "GPS Navigation"), v, col);
    return out;
}

void PopulateMultiplayerRightCard(int selIdx, ui::MenuInfoCard& card) {
    EnsureMpConfigLoaded();
    const auto& th = ui::Theme();
    const auto& c  = g_st.mpCfg;
    const MpRowId row = static_cast<MpRowId>(std::clamp(selIdx, 0, static_cast<int>(MpRowId::Count) - 1));

    card.SetHeader(ui::Loc("СОВМЕСТНАЯ ИГРА", "CO-OP SESSION"), "SA-COOP");

    std::string desc;
    switch (row) {
        case MpRowId::HostServer:
            desc = ui::Loc("Запустить собственный сервер SA-COOP и войти в мир в роли хоста с выбранным сохранением.",
                           "Launch your own SA-COOP server and enter the world as host with the chosen save slot.");
            break;
        case MpRowId::JoinServer:
            desc = ui::Loc("Подключиться к серверу друга по указанному IP-адресу или коду приглашения.",
                           "Connect to a friend's server using the configured IP address or invite code.");
            break;
        case MpRowId::OpenCoopWindow:
            desc = ui::Loc("Открыть полное окно SA-COOP для копирования кода приглашения и настройки клавиш чата.",
                           "Open the native SA-COOP window for invite codes and chat key bindings.");
            break;
        case MpRowId::PlayerName:
            desc = ui::Loc("Ваш псевдоним в сетевой игре и имя профиля сохранений SA-COOP.",
                           "Your display name in multiplayer and SA-COOP client save profile.");
            break;
        case MpRowId::HostSlot:
            desc = ui::Loc("Выберите новую игру или слот 1-8, который загрузится при создании сервера.",
                           "Choose New Game or slot 1-8 to load when hosting a server.");
            break;
        case MpRowId::HostAddress:
            desc = ui::Loc("IP-адрес хоста или код приглашения для подключения к серверу.",
                           "Host IP address or invite code used when joining a server.");
            break;
        case MpRowId::Port:
            desc = ui::Loc("Сетевой порт подключения. Стандартное значение 7788.",
                           "Network port for hosting or joining. Default is 7788.");
            break;
        case MpRowId::Password:
            desc = ui::Loc("Необязательный пароль доступа к серверу.",
                           "Optional password required to join the server.");
            break;
        case MpRowId::MaxPlayers:
            desc = ui::Loc("Максимальное количество участников на вашем сервере.",
                           "Maximum number of players allowed on your hosted server.");
            break;
        case MpRowId::FriendlyFire:
            desc = ui::Loc("Разрешить игрокам наносить урон друг другу.",
                           "Allow players to damage each other.");
            break;
        case MpRowId::PartnerTeleport:
            desc = ui::Loc("Разрешить напарникам перемещаться к вам командами чата.",
                           "Allow teammates to teleport via chat commands.");
            break;
        case MpRowId::SharedLife:
            desc = ui::Loc("Гибель напарника проваливает активную сюжетную миссию хоста.",
                           "A teammate's death fails the host's active story mission.");
            break;
        case MpRowId::PartnerCheats:
            desc = ui::Loc("Разрешить напарникам использовать игровые чит-коды.",
                           "Allow teammates to activate cheat codes.");
            break;
        case MpRowId::VoiceEnabled:
            desc = ui::Loc("Включить встроенную голосовую связь SA-COOP.",
                           "Enable built-in SA-COOP voice chat.");
            break;
        case MpRowId::GpsNav:
            desc = ui::Loc("Отображать маршрут GPS на радаре во время вождения.",
                           "Display GPS route guidance on the radar while driving.");
            break;
        default:
            break;
    }
    if (!desc.empty()) card.SetDescription(desc);

    card.AddRow(ui::Loc("Профиль", "Player"), c.playerName.empty() ? "Player" : c.playerName, th.textSelected);
    char epBuf[96];
    std::snprintf(epBuf, sizeof(epBuf), "%s:%d", c.host.empty() ? "127.0.0.1" : c.host.c_str(), c.port);
    card.AddRow(ui::Loc("Адрес", "Endpoint"), epBuf, th.textNormal);
    if (c.loadSlot >= 1 && c.loadSlot <= 8) {
        auto sInfo = sdk::GetSaveSlotInfo(c.loadSlot - 1);
        std::string sTxt = (sInfo.status == 0 && !sInfo.nameGame.empty())
                               ? sInfo.nameGame
                               : (ui::Loc("Слот ", "Slot ") + std::to_string(c.loadSlot));
        card.AddRow(ui::Loc("Мир хоста", "Host World"), sTxt, th.textSuccess);
    } else {
        card.AddRow(ui::Loc("Мир хоста", "Host World"), ui::Loc("Новая игра", "New Game"), th.textCol);
    }

    std::string liveErr = integrations::sacoop::GetLastFlowError();
    if (!liveErr.empty()) {
        card.AddRow(ui::Loc("Статус", "Status"), liveErr, th.textDanger);
    } else if (!g_st.mpStatusMsg.empty()) {
        card.AddRow(ui::Loc("Статус", "Status"), g_st.mpStatusMsg,
                    g_st.mpStatusIsError ? th.textDanger : th.textSuccess);
    } else {
        card.AddRow(ui::Loc("Режим", "Mode"), ui::Loc("Готов к запуску", "Ready"), th.textSuccess);
    }

    card.SetFooterNote(ui::Loc("После входа в мир сменить режим можно только перезапуском игры",
                               "Mode is locked once in-game; restart to switch Singleplayer or Co-op"));
}

void BeginMpTextEdit(MpRowId field) {
    EnsureMpConfigLoaded();
    g_st.textEditActive  = true;
    g_st.textEditField   = field;
    g_st.textEditNumeric = (field == MpRowId::Port);
    if (field == MpRowId::PlayerName) {
        g_st.textEditBuf    = g_st.mpCfg.playerName;
        g_st.textEditMaxLen = 20;
    } else if (field == MpRowId::HostAddress) {
        g_st.textEditBuf    = g_st.mpCfg.host;
        g_st.textEditMaxLen = 40;
    } else if (field == MpRowId::Port) {
        g_st.textEditBuf    = std::to_string(g_st.mpCfg.port);
        g_st.textEditMaxLen = 5;
    } else if (field == MpRowId::Password) {
        g_st.textEditBuf    = g_st.mpCfg.password;
        g_st.textEditMaxLen = 24;
    }
    for (int vk = 0; vk < 256; ++vk) {
        g_st.prevKeyState[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

void CommitMpTextEdit() {
    if (g_st.textEditField == MpRowId::PlayerName) {
        if (!g_st.textEditBuf.empty()) g_st.mpCfg.playerName = g_st.textEditBuf;
    } else if (g_st.textEditField == MpRowId::HostAddress) {
        if (!g_st.textEditBuf.empty()) g_st.mpCfg.host = g_st.textEditBuf;
    } else if (g_st.textEditField == MpRowId::Port) {
        int p = std::atoi(g_st.textEditBuf.c_str());
        if (p >= 1 && p <= 65535) g_st.mpCfg.port = p;
    } else if (g_st.textEditField == MpRowId::Password) {
        g_st.mpCfg.password = g_st.textEditBuf;
    }
    integrations::sacoop::SaveCoopSettings(g_st.mpCfg);
    g_st.textEditActive = false;
    sa::PlayFrontendSound(sa::SFX_SELECT);
}

void HandleMpTextEditInput() {
    auto justPressed = [&](int vk) -> bool {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool edge = down && !g_st.prevKeyState[vk];
        g_st.prevKeyState[vk] = down;
        return edge;
    };

    if (justPressed(VK_ESCAPE)) {
        g_st.textEditActive = false;
        sa::PlayFrontendSound(sa::SFX_BACK);
        return;
    }
    if (justPressed(VK_RETURN)) {
        CommitMpTextEdit();
        return;
    }
    if (justPressed(VK_BACK)) {
        if (!g_st.textEditBuf.empty()) {
            g_st.textEditBuf.pop_back();
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }
        return;
    }

    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    auto pushChar = [&](char c) {
        if (static_cast<int>(g_st.textEditBuf.size()) >= g_st.textEditMaxLen) return;
        g_st.textEditBuf.push_back(c);
        sa::PlayFrontendSound(sa::SFX_SCROLL);
    };

    for (int vk = '0'; vk <= '9'; ++vk) {
        if (justPressed(vk)) pushChar(static_cast<char>(vk));
    }
    if (!g_st.textEditNumeric) {
        for (int vk = 'A'; vk <= 'Z'; ++vk) {
            if (justPressed(vk)) {
                char ch = static_cast<char>(shift ? vk : (vk - 'A' + 'a'));
                pushChar(ch);
            }
        }
        if (justPressed(VK_OEM_PERIOD)) pushChar('.');
        if (justPressed(VK_OEM_MINUS))  pushChar(shift ? '_' : '-');
        if (justPressed(VK_OEM_4))      pushChar('[');
        if (justPressed(VK_OEM_6))      pushChar(']');
    }
}

void RenderMpTextEditModal() {
    std::string title;
    if (g_st.textEditField == MpRowId::PlayerName)       title = ui::Loc("ИМЯ ИГРОКА", "PLAYER NAME");
    else if (g_st.textEditField == MpRowId::HostAddress) title = ui::Loc("АДРЕС ИЛИ КОД", "ADDRESS OR CODE");
    else if (g_st.textEditField == MpRowId::Port)        title = ui::Loc("ПОРТ СЕРВЕРА", "SERVER PORT");
    else                                                 title = ui::Loc("ПАРОЛЬ СЕРВЕРА", "SERVER PASSWORD");

    ui::TextInputModal modal;
    modal.SetTitle(title)
         .SetText(g_st.textEditBuf)
         .SetPlaceholder(ui::Loc("Введите значение", "Enter value"))
         .SetMaxLength(g_st.textEditMaxLen)
         .SetFooter(ui::Loc("ENTER - СОХРАНИТЬ   ESC - ОТМЕНА", "ENTER - SAVE   ESC - CANCEL"));
    modal.Render();
    ui::prim::FlushText();
}

void AdjustMultiplayerRow(int idx, int dir, bool activate) {
    EnsureMpConfigLoaded();
    MpRowId row = static_cast<MpRowId>(std::clamp(idx, 0, static_cast<int>(MpRowId::Count) - 1));
    auto& c = g_st.mpCfg;
    bool changed = false;

    switch (row) {
        case MpRowId::HostServer:
            if (activate) {
                c.mode = "host";
                integrations::sacoop::SaveCoopSettings(c);
                g_st.inMultiplayerTab = false;
                if (integrations::sacoop::LaunchHost(c.loadSlot, static_cast<uint16_t>(c.port), c.password)) {
                    g_st.mpStatusMsg     = ui::Loc("Запуск сервера...", "Starting server...");
                    g_st.mpStatusIsError = false;
                    sa::PlayFrontendSound(sa::SFX_SELECT);
                } else {
                    g_st.mpStatusMsg     = ui::Loc("Откройте окно SA-COOP", "Open SA-COOP window");
                    g_st.mpStatusIsError = true;
                    integrations::sacoop::SetConnectMenuVisible(true);
                }
            }
            return;
        case MpRowId::JoinServer:
            if (activate) {
                c.mode = "join";
                integrations::sacoop::SaveCoopSettings(c);
                g_st.inMultiplayerTab = false;
                if (integrations::sacoop::LaunchJoin(c.host, static_cast<uint16_t>(c.port), c.password)) {
                    g_st.mpStatusMsg     = ui::Loc("Подключение...", "Connecting...");
                    g_st.mpStatusIsError = false;
                    sa::PlayFrontendSound(sa::SFX_SELECT);
                } else {
                    g_st.mpStatusMsg     = ui::Loc("Откройте окно SA-COOP", "Open SA-COOP window");
                    g_st.mpStatusIsError = true;
                    integrations::sacoop::SetConnectMenuVisible(true);
                }
            }
            return;
        case MpRowId::OpenCoopWindow:
            if (activate) {
                integrations::sacoop::SaveCoopSettings(c);
                integrations::sacoop::ApplyDarkUiKitTheme();
                integrations::sacoop::SetConnectMenuVisible(true);
                sa::PlayFrontendSound(sa::SFX_SELECT);
            }
            return;
        case MpRowId::PlayerName:
        case MpRowId::HostAddress:
        case MpRowId::Port:
        case MpRowId::Password:
            if (activate) {
                BeginMpTextEdit(row);
                sa::PlayFrontendSound(sa::SFX_SELECT);
            }
            return;
        case MpRowId::HostSlot:
            c.loadSlot = (c.loadSlot + (dir != 0 ? dir : 1) + 9) % 9;
            changed = true;
            break;
        case MpRowId::MaxPlayers:
            c.maxPlayers = 2 + ((c.maxPlayers - 2 + (dir != 0 ? dir : 1) + 7) % 7);
            changed = true;
            break;
        case MpRowId::FriendlyFire:    c.friendlyFire    = !c.friendlyFire;    changed = true; break;
        case MpRowId::PartnerTeleport: c.partnerTeleport = !c.partnerTeleport; changed = true; break;
        case MpRowId::SharedLife:      c.sharedLife      = !c.sharedLife;      changed = true; break;
        case MpRowId::PartnerCheats:   c.partnerCheats   = !c.partnerCheats;   changed = true; break;
        case MpRowId::VoiceEnabled:    c.voiceEnabled    = !c.voiceEnabled;    changed = true; break;
        case MpRowId::GpsNav:          c.gps             = !c.gps;             changed = true; break;
        default: break;
    }

    if (changed) {
        integrations::sacoop::SaveCoopSettings(c);
        sa::PlayFrontendSound(sa::SFX_SCROLL);
    }
}

// ============================================================================
// Modernized Redefine Controls (page == 38) Helpers
// ============================================================================
std::string FormatControlRowKeysSummary(int32_t actionId) {
    std::string k1 = sdk::GetBoundKeyName(actionId, 1);
    std::string k2 = sdk::GetBoundKeyName(actionId, 2);
    if (!k1.empty() && !k2.empty() && k1 != k2) {
        return k1 + " / " + k2;
    }
    if (!k1.empty()) return k1;
    if (!k2.empty()) return k2;
    std::string k3 = sdk::GetBoundKeyName(actionId, 3);
    if (!k3.empty()) return k3;
    std::string k4 = sdk::GetBoundKeyName(actionId, 4);
    if (!k4.empty()) return k4;
    return ui::Loc("Не назначено", "Unbound");
}

void PopulateControlsRightCard(uint8_t* mm, const std::vector<sdk::ControlRowDef>& rows, int selIdx,
                               ui::MenuInfoCard& card)
{
    const auto& th        = ui::Theme();
    const bool  isVeh     = (sdk::ControlSubMode(mm) != 0);
    const bool  capturing = sdk::IsWaitingForKeybind(mm);

    if (selIdx < 0 || selIdx >= static_cast<int>(rows.size())) {
        card.SetHeader(GetPageHeaderTitle(38, mm));
        return;
    }
    const auto& cur = rows[selIdx];
    card.SetHeader(ui::Loc(cur.rusLabel, cur.engLabel),
                   isVeh ? ui::Loc("В ТРАНСПОРТЕ", "VEHICLE") : ui::Loc("ПЕШКОМ", "ON FOOT"));

    if (capturing) {
        card.SetDescription(ui::Loc("Ожидание ввода... Нажмите новую клавишу или кнопку мыши. Backspace - очистить, Esc - отмена.",
                                    "Waiting for input... Press a new key or mouse button. Backspace - clear, Esc - cancel."));
    } else {
        card.SetDescription(ui::Loc("Нажмите Enter или ЛКМ на действии, чтобы назначить новую клавишу.",
                                    "Press Enter or LMB on an action to assign a new key binding."));
    }

    int shownBindings = 0;
    for (int order = 1; order <= 4; ++order) {
        std::string s = sdk::GetBoundKeyName(cur.actionId, order);
        if (s.empty() && order > 2) continue;
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%s %d", ui::Loc("Привязка", "Binding").c_str(), order);
        card.AddRow(lbl,
                    s.empty() ? ui::Loc("Нет", "None") : s,
                    s.empty() ? th.textDisabled : (order == 1 ? th.textSelected : th.textNormal));
        ++shownBindings;
    }
    (void)shownBindings;

    card.SetFooterNote(ui::Loc("TAB или A/D - переключить Пешком / В транспорте",
                               "TAB or A/D - toggle Foot / Vehicle controls"));
}

// ============================================================================
// Standard Right Info Card & Modal Dialog Builders
// ============================================================================
void PopulateRightInfoCard(uint8_t* mm, int8_t page,
                           const std::vector<ResolvedEntry>& entries, int selVisIdx,
                           ui::MenuInfoCard& card)
{
    const auto& th    = ui::Theme();
    const bool inGame = sdk::IsInGame(mm);

    // 1. STATS PAGE (page == 0): Live scrollable GTA SA statistics
    if (page == 0 && inGame) {
        int cat = 0;
        if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size()) && !entries[selVisIdx].uiItem.isBack) {
            cat = std::clamp(entries[selVisIdx].rawIndex, 0, 7);
        }
        if (cat != g_st.lastStatsCat) {
            g_st.lastStatsCat = cat;
            g_st.statsScroll  = 0;
        }

        int total = sdk::GetStatCategoryCount(static_cast<uint8_t>(cat));
        constexpr int kPerPage = 10;
        int maxScroll = std::max(0, total - kPerPage);
        g_st.statsScroll = std::clamp(g_st.statsScroll, 0, maxScroll);

        std::string catTitle = (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size()))
                                   ? entries[selVisIdx].uiItem.labelGame
                                   : ui::Loc("СТАТИСТИКА", "STATS");
        char badge[32] = {};
        if (total > 0) {
            int first = g_st.statsScroll + 1;
            int last  = std::min(total, g_st.statsScroll + kPerPage);
            std::snprintf(badge, sizeof(badge), "%d-%d / %d", first, last, total);
        }
        card.SetHeader(catTitle, badge);

        if (total <= 0) {
            card.SetDescription(ui::Loc("В этой категории пока нет записей.", "No recorded statistics in this category yet."));
            return;
        }

        int shown = 0;
        for (int idx = g_st.statsScroll; idx < total && shown < kPerPage; ++idx) {
            sdk::StatRowData row{};
            if (!sdk::ReadStatLine(idx, static_cast<uint8_t>(cat), row)) continue;
            if (row.isBar) {
                card.AddBar(row.labelGame, row.valueGame, row.frac01, th.textSelected);
            } else {
                card.AddRow(row.labelGame, row.valueGame, th.textNormal);
            }
            ++shown;
        }
        if (total > kPerPage) {
            card.SetFooterNote(ui::Loc("A/D или PgUp/PgDn - прокрутка списка", "A/D or PgUp/PgDn - scroll stats"));
        }
        return;
    }

    // 2. SAVE / LOAD / DELETE SLOT PREVIEW (pages 9, 10, 16)
    if (page == 9 || page == 10 || page == 16) {
        if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size()) && entries[selVisIdx].saveSlot0 >= 0) {
            auto info = sdk::GetSaveSlotInfo(entries[selVisIdx].saveSlot0);
            char hdr[32];
            std::snprintf(hdr, sizeof(hdr), "%s %d", ui::Loc("СЛОТ", "SLOT").c_str(), info.slot1to8);
            card.SetHeader(hdr, info.status == 0 ? ui::Loc("ЗАНЯТ", "USED") : ui::Loc("ПУСТО", "EMPTY"));

            if (info.status == 0) {
                card.AddRow(ui::Loc("Миссия", "Mission"),
                            info.nameGame.empty() ? ui::Loc("Сохранение", "Saved Game") : info.nameGame,
                            th.textSelected);
                if (!info.dateGame.empty()) {
                    card.AddRow(ui::Loc("Дата", "Date"), info.dateGame, th.textNormal);
                }
                if (page == 9) {
                    card.SetDescription(ui::Loc("Загрузить выбранное сохранение и продолжить с этого момента.",
                                                "Load this save slot and resume from this point."));
                } else if (page == 10) {
                    card.SetDescription(ui::Loc("Удалить выбранный слот. Восстановление после удаления невозможно.",
                                                "Delete this save slot permanently."));
                } else {
                    card.SetDescription(ui::Loc("Записать текущий прогресс в этот слот с заменой старого сохранения.",
                                                "Overwrite this slot with your current game progress."));
                }
            } else if (info.status == 2) {
                card.SetDescription(ui::Loc("Файл сохранения поврежден и не может быть загружен.",
                                            "This save file is corrupted and cannot be loaded."));
            } else {
                card.SetDescription(page == 16
                                        ? ui::Loc("Свободный слот готов для записи нового сохранения.",
                                                  "Empty slot ready for a new saved game.")
                                        : ui::Loc("В этом слоте нет сохраненной игры.",
                                                  "There is no saved game in this slot."));
            }
            return;
        }
    }

    // 3. PAUSE ROOT (page == 41): Live Player Overview Card!
    if (page == 41 && inGame && g_cfg.showPlayerCard) {
        const auto* ped = sa::FindPlayerPed(0);
        const auto* bus = modbus::GetBus();
        card.SetHeader(ui::Loc("ПРОФИЛЬ ПЕРСОНАЖА", "PLAYER OVERVIEW"),
                       integrations::sacoop::IsMultiplayerSessionActive() ? "SA-COOP" : ui::Loc("СЮЖЕТ", "STORY"));

        if (bus && bus->progressionInstalled) {
            int rank  = std::max(1, bus->playerRank);
            int curXp = std::max(0, bus->playerLevelXp);
            int nxtXp = std::max(1, bus->playerNextLevelXp);
            float xp01 = std::clamp(static_cast<float>(curXp) / static_cast<float>(nxtXp), 0.0f, 1.0f);
            char rankLbl[48], xpVal[48];
            std::snprintf(rankLbl, sizeof(rankLbl), "%s %d", ui::Loc("Уровень", "Rank").c_str(), rank);
            std::snprintf(xpVal, sizeof(xpVal), "%d / %d %s", curXp, nxtXp, ui::Loc("ОП", "XP").c_str());
            card.AddBar(rankLbl, xpVal, xp01, th.textSuccess);
        }

        if (sa::IsPedValid(ped)) {
            float hp    = std::max(0.0f, ped->GetHealth());
            float maxHp = std::max(100.0f, ped->GetMaxHealth());
            float arm   = std::clamp(ped->GetArmor(), 0.0f, 100.0f);
            char hpStr[24], armStr[24];
            std::snprintf(hpStr, sizeof(hpStr), "%d%%", static_cast<int>(std::round(std::clamp(hp / maxHp, 0.0f, 1.0f) * 100.0f)));
            std::snprintf(armStr, sizeof(armStr), "%d%%", static_cast<int>(std::round(arm)));
            card.AddBar(ui::Loc("Здоровье", "Health"), hpStr, std::clamp(hp / maxHp, 0.0f, 1.0f), th.barGain);
            card.AddBar(ui::Loc("Броня", "Armor"), armStr, arm * 0.01f, {165, 195, 225, 240});
        }

        char cashBuf[32];
        std::snprintf(cashBuf, sizeof(cashBuf), "$%d", sa::GetPlayerMoney());
        card.AddRow(ui::Loc("Наличные", "Cash"), cashBuf, th.textSuccess);

        const char* regRus = nullptr;
        const char* regEng = nullptr;
        sdk::GetPlayerRegionNames(ped, regRus, regEng);
        card.AddRow(ui::Loc("Район", "Region"), ui::Loc(regRus, regEng), th.textSelected);

        int32_t stars = sa::wanted::GetLevel(0);
        char starBuf[24];
        std::snprintf(starBuf, sizeof(starBuf), "%d / 6", stars);
        card.AddRow(ui::Loc("Розыск", "Wanted Level"),
                    stars > 0 ? std::string(starBuf) : ui::Loc("Нет", "Clear"),
                    stars > 0 ? th.textWarning : th.textCol);

        if (bus && bus->garageInstalled && bus->garageGetOwnedCount) {
            char gBuf[24];
            std::snprintf(gBuf, sizeof(gBuf), "%d / 10", bus->garageGetOwnedCount());
            card.AddRow(ui::Loc("Личный гараж", "Personal Garage"), gBuf, th.textNormal);
        }
        if (bus && bus->weaponsInstalled && bus->weaponsGetOwnedCount) {
            char wBuf[24];
            std::snprintf(wBuf, sizeof(wBuf), "%d", bus->weaponsGetOwnedCount());
            card.AddRow(ui::Loc("Арсенал", "Owned Weapons"), wBuf, th.textNormal);
        }

        if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size())) {
            std::string desc = GetEntryDescription(entries[selVisIdx].actionType, entries[selVisIdx].gxtKey, page, inGame);
            if (!desc.empty()) card.SetFooterNote(desc);
        }
        return;
    }

    // 4. ALL OTHER PAGES (Title Menu, Start Game, Options, Audio, Display, Advanced, Controls, Language)
    if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size())) {
        const auto& cur = entries[selVisIdx];
        card.SetHeader(cur.uiItem.labelGame, GetPageHeaderTitle(page, mm));
        std::string desc = GetEntryDescription(cur.actionType, cur.gxtKey, page, inGame);
        if (!desc.empty()) {
            card.SetDescription(desc);
        }
        if (cur.uiItem.hasSlider) {
            char pct[16];
            std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::round(cur.uiItem.sliderFrac01 * 100.0f)));
            card.AddBar(ui::Loc("Текущее значение", "Current Value"), pct, cur.uiItem.sliderFrac01, th.textSelected);
            card.SetFooterNote(ui::Loc("A/D, стрелки или клик по шкале для настройки",
                                       "Use A/D, arrows, or click slider to adjust"));
        } else if (!cur.uiItem.valueGame.empty()) {
            card.AddRow(ui::Loc("Текущее значение", "Current Value"), cur.uiItem.valueGame, cur.uiItem.valueColor);
        }

        // Show Widescreen Fix live status block on Advanced Display (page 27)
        if (page == 27 && integrations::widescreen::IsLoaded()) {
            const auto& wsf = integrations::widescreen::GetIniSummary();
            if (wsf.loaded) {
                card.AddRow("Widescreen Fix", ui::Loc("Активен", "Active"), th.textSuccess);
                card.AddRow("SMAA",
                            wsf.smaa ? ui::Loc("ВКЛ", "ON") : ui::Loc("ВЫКЛ", "OFF"),
                            wsf.smaa ? th.textSuccess : th.textCol);
                if (wsf.menuFrameLimit > 0) {
                    char mfl[24];
                    std::snprintf(mfl, sizeof(mfl), "%d FPS", wsf.menuFrameLimit);
                    card.AddRow(ui::Loc("Лимит FPS меню", "Menu FPS Cap"), mfl, th.textNormal);
                }
            }
        }

        if (page == 34 && !inGame) {
            card.AddRow(ui::Loc("Одиночная игра", "Singleplayer"),
                        ui::Loc("Вкладка Начать игру", "Start Game tab"), th.textNormal);
            if (integrations::sacoop::IsLoaded()) {
                card.AddRow(ui::Loc("Сетевая игра", "Multiplayer"),
                            ui::Loc("Вкладка Мультиплеер", "Multiplayer tab"), th.textSuccess);
            }
            card.AddRow(ui::Loc("Язык интерфейса", "UI Language"),
                        ui::UseRussianFlag() ? ui::Loc("Русский", "Russian") : std::string("English"),
                        th.textCol);
        }
    } else {
        card.SetHeader(GetPageHeaderTitle(page, mm));
    }
}

void BuildModalDialog(int8_t page, const std::vector<ResolvedEntry>& entries,
                      const std::string& staticPrompt, int selVisIdx, ui::MenuConfirmModal& modal)
{
    modal.SetTitle(GetPageHeaderTitle(page)).ClearButtons();

    std::string prompt;
    switch (page) {
        case 35: prompt = ui::Loc("Вы действительно хотите выйти из игры? Несохраненный прогресс будет потерян.",
                                  "Are you sure you want to quit? Any unsaved progress will be lost."); break;
        case 6:  prompt = ui::Loc("Начать новую игру? Текущий несохраненный прогресс будет потерян.",
                                  "Start a new game? Any unsaved progress will be lost."); break;
        case 8:  prompt = ui::Loc("Загрузить выбранную миссию? Несохраненный прогресс будет потерян.",
                                  "Load the selected mission? Unsaved progress will be lost."); break;
        case 11: prompt = ui::Loc("Загрузить выбранное сохранение? Несохраненный прогресс будет потерян.",
                                  "Load the selected save game? Unsaved progress will be lost."); break;
        case 12: prompt = ui::Loc("Удалить выбранное сохранение без возможности восстановления?",
                                  "Permanently delete this saved game?"); break;
        case 15: prompt = ui::Loc("Выбранное сохранение успешно удалено.",
                                  "The selected save file has been deleted."); break;
        case 17: prompt = ui::Loc("Перезаписать существующее сохранение в этом слоте?",
                                  "Overwrite the existing saved game in this slot?"); break;
        case 19:
        case 20:
        case 21: prompt = ui::Loc("Игра успешно сохранена.", "Game saved successfully."); break;
        case 23:
        case 24:
        case 25: prompt = ui::Loc("Сбросить параметры этого раздела к стандартным значениям?",
                                  "Restore settings in this section to defaults?"); break;
        default:
            prompt = !staticPrompt.empty()
                         ? staticPrompt
                         : ui::Loc("Подтвердите выбранное действие.", "Confirm the selected action.");
            break;
    }
    modal.SetPrompt(prompt);

    const bool danger = IsDangerModalPage(page);
    for (const auto& e : entries) {
        bool isYes = (e.gxtKey == "FEM_YES" || e.actionType == 3 || e.actionType == 12 ||
                      e.actionType == 14 || e.actionType == 53 || e.actionType == 57);
        modal.AddButton(e.uiItem.labelGame, danger && isYes);
    }
    modal.SetSelection(selVisIdx, g_st.hoveredModalBtn);
}

void ActivateTopTab(uint8_t* mm, int tabIdx) {
    const bool inGame = sdk::IsInGame(mm);
    int tabCount = 0;
    const TopTabDef* tabs = GetActiveTabs(inGame, tabCount);
    if (tabIdx < 0 || tabIdx >= tabCount) return;

    const int8_t target = tabs[tabIdx].targetPage;
    if (target == kPageMultiplayer) {
        if (inGame || !integrations::sacoop::IsLoaded()) return;
        if (!g_st.inMultiplayerTab) {
            g_st.inMultiplayerTab = true;
            g_st.textEditActive   = false;
            g_st.mpCfgLoaded      = false;
            g_st.mpStatusMsg.clear();
            sa::PlayFrontendSound(sa::SFX_SELECT);
        }
        return;
    }

    const bool wasMp = g_st.inMultiplayerTab;
    g_st.inMultiplayerTab = false;
    g_st.textEditActive   = false;

    if (!wasMp && target == sdk::CurrentPage(mm)) return;
    if (sdk::CurrentPage(mm) == 38) {
        sdk::CancelKeybindCapture(mm);
    }
    sa::PlayFrontendSound(sa::SFX_SELECT);
    sdk::NavigateToPage(mm, target);
}

void CycleTopTab(uint8_t* mm, int delta) {
    const bool inGame = sdk::IsInGame(mm);
    int tabCount = 0;
    GetActiveTabs(inGame, tabCount);
    if (tabCount <= 1) return;
    int8_t eff = SanitizeAndGetEffectivePage(mm);
    int cur = GetActiveTopTabIndex(inGame, eff);
    int nxt = (cur + delta + tabCount) % tabCount;
    ActivateTopTab(mm, nxt);
}

} // anonymous namespace

// ============================================================================
// Public Interface
// ============================================================================
bool ShouldPassThrough(uint8_t* mm) {
    if (!mm || !g_cfg.enableModernMenu) return true;
    if (sdk::LoadingScreenFlag(mm) != 0) return true;
    int8_t page = sdk::CurrentPage(mm);
    // CRITICAL: Pages 13 (LOAD_FIRST_SAVE), 14 (DELETE_FIRST_SAVE), and 18 (SAVE_WRITE)
    // execute the actual save/load/delete engine routines inside CMenuManager::DrawStandardMenus!
    // Page 43 is ModLoader's Mod Configuration menu.
    if (page == 13 || page == 14 || page == 18 || page == 43 ||
        page < 0 || page >= sdk::MAX_MENU_PAGES) {
        return true;
    }
    return false;
}

void ResetMenuUiState() {
    g_st = {};
}

void RenderModernMenu(uint8_t* mm, FnDrawBackground origDrawBg) {
    ui::SyncFromSharedBus();
    EnsureCoopHookedOnce();

    if (ShouldPassThrough(mm)) {
        if (origDrawBg) origDrawBg(mm);
        return;
    }

    const int8_t page    = SanitizeAndGetEffectivePage(mm);
    const bool   inGame  = sdk::IsInGame(mm);
    const bool   hasTabs = ShouldShowTopTabs(page);
    const std::string crumb = GetPageBreadcrumb(inGame, page, mm);
    const auto m = ui::ComputeMenuShellMetrics(hasTabs, !crumb.empty());
    if (m.sw < 100.0f || m.sh < 100.0f) {
        if (origDrawBg) origDrawBg(mm);
        return;
    }

    float mx = 0.0f, my = 0.0f;
    SyncCursorPosition(mm, m.sw, m.sh, mx, my);
    const bool coopOverlay = IsSaCoopOverlayBlockingInput();

    // 1. INTERACTIVE MAP PAGE (page == 5, strictly in-game only!)
    if (page == 5 && inGame) {
        sdk::DrawMapNative(mm);
        ui::RenderMenuBackdrop(0, true);

        ui::MenuTopBar topBar;
        BuildTopBar(mm, page, true, "", topBar);
        topBar.Render();

        ui::MenuFooterBar footer;
        footer.SetHints(
            ui::Loc("ЛКМ - ПЕРЕМЕЩЕНИЕ   ПКМ/T - МЕТКА   КОЛЕСО - МАСШТАБ   L - ЛЕГЕНДА",
                    "LMB - PAN   RMB/T - WAYPOINT   WHEEL - ZOOM   L - LEGEND"),
            ui::Loc("Q/E - ВКЛАДКИ   BACKSPACE - НАЗАД", "Q/E - TABS   BACKSPACE - BACK"));
        footer.Render();

        if (!coopOverlay) ui::RenderMenuCursor(mx, my);
        return;
    }

    // 2. BACKDROP
    if (!inGame && g_cfg.showClassicTitleArt) {
        sdk::DrawTitleBackgroundArt(mm, m.sw, m.sh);
    }
    ui::RenderMenuBackdrop(inGame ? g_cfg.backdropOpacity
                                  : static_cast<uint8_t>(std::min<int>(225, g_cfg.backdropOpacity + 18)),
                           true);

    // 3. TOP NAVIGATION BAR + MASTHEAD + BREADCRUMB
    ui::MenuTopBar topBar;
    BuildTopBar(mm, page, hasTabs, crumb, topBar);
    topBar.Render();

    // 4. DEDICATED SA-COOP MULTIPLAYER TAB (Main Menu only!)
    if (page == kPageMultiplayer && !inGame) {
        auto mpItems = BuildMultiplayerItems();
        int count = static_cast<int>(mpItems.size());
        g_st.mpSelected = std::clamp(g_st.mpSelected, 0, std::max(0, count - 1));

        ui::MenuOptionList leftList;
        leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH);
        int maxVis = leftList.GetMaxVisibleRows();
        if (g_st.mpSelected < g_st.mpScroll) g_st.mpScroll = g_st.mpSelected;
        if (g_st.mpSelected >= g_st.mpScroll + maxVis) g_st.mpScroll = g_st.mpSelected - maxVis + 1;
        g_st.mpScroll = std::clamp(g_st.mpScroll, 0, std::max(0, count - maxVis));

        char badge[24];
        std::snprintf(badge, sizeof(badge), "%d / %d", g_st.mpSelected + 1, count);
        leftList.SetHeader(GetPageHeaderTitle(kPageMultiplayer), badge)
                .SetSelection(g_st.mpSelected, g_st.hoveredRow)
                .SetScrollOffset(g_st.mpScroll)
                .ClearItems();
        for (const auto& it : mpItems) leftList.AddItem(it);
        leftList.Render();

        ui::MenuInfoCard rightCard;
        rightCard.SetBounds(m.rightColX, m.contentY, m.rightColW, m.contentH);
        PopulateMultiplayerRightCard(g_st.mpSelected, rightCard);
        rightCard.Render();

        ui::MenuFooterBar footer;
        footer.SetHints(
            ui::Loc("W/S/КОЛЕСО - СПИСОК   A/D - ИЗМЕНИТЬ   ЛКМ/F/ENTER - ВЫБРАТЬ",
                    "W/S/WHEEL - LIST   A/D - ADJUST   LMB/F/ENTER - SELECT"),
            ui::Loc("Q/E - ВКЛАДКИ   ПКМ/BACKSPACE - НАЗАД", "Q/E - TABS   RMB/BACKSPACE - BACK"));
        footer.Render();

        if (g_st.textEditActive) {
            RenderMpTextEditModal();
        }
        if (!coopOverlay) ui::RenderMenuCursor(mx, my);
        return;
    }

    // 5. MODERNIZED REDEFINE CONTROLS (page == 38)
    if (page == 38) {
        const bool isVeh     = (sdk::ControlSubMode(mm) != 0);
        const bool isJoy     = (sdk::ControlMethod(mm) != 0);
        const bool capturing = sdk::IsWaitingForKeybind(mm);
        auto rows = sdk::BuildControlRows(isVeh, isJoy);
        int count = static_cast<int>(rows.size());
        int sel   = std::clamp(sdk::SelectedControlRow(mm), 0, std::max(0, count - 1));
        sdk::SelectedControlRow(mm) = sel;
        if (sel >= 0 && sel < count) {
            sdk::KeybindActionId(mm)   = rows[sel].actionId;
            sdk::KeybindActiveFlag(mm) = capturing ? 1 : 0;
        }

        ui::MenuOptionList leftList;
        leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH);
        int maxVis = leftList.GetMaxVisibleRows();
        if (sel < g_st.controlsScroll) g_st.controlsScroll = sel;
        if (sel >= g_st.controlsScroll + maxVis) g_st.controlsScroll = sel - maxVis + 1;
        g_st.controlsScroll = std::clamp(g_st.controlsScroll, 0, std::max(0, count - maxVis));

        char badge[32];
        std::snprintf(badge, sizeof(badge), "%d / %d", sel + 1, count);
        leftList.SetHeader(GetPageHeaderTitle(38, mm), badge)
                .SetSelection(sel, g_st.hoveredRow)
                .SetScrollOffset(g_st.controlsScroll)
                .ClearItems();

        const auto& th = ui::Theme();
        for (int i = 0; i < count; ++i) {
            ui::MenuOptionItem it{};
            it.labelGame   = ui::Loc(rows[i].rusLabel, rows[i].engLabel);
            it.isCapturing = (capturing && i == sel);
            it.valueGame   = it.isCapturing
                                 ? ui::Loc("НАЖМИТЕ КЛАВИШУ...", "PRESS A KEY...")
                                 : FormatControlRowKeysSummary(rows[i].actionId);
            it.valueColor  = it.isCapturing ? th.textWarning : th.textCol;
            it.enabled     = true;
            leftList.AddItem(it);
        }
        leftList.Render();

        ui::MenuInfoCard rightCard;
        rightCard.SetBounds(m.rightColX, m.contentY, m.rightColW, m.contentH);
        PopulateControlsRightCard(mm, rows, sel, rightCard);
        rightCard.Render();

        ui::MenuFooterBar footer;
        if (capturing) {
            footer.SetHints(
                ui::Loc("НАЖМИТЕ НОВУЮ КЛАВИШУ ИЛИ КНОПКУ МЫШИ", "PRESS NEW KEY OR MOUSE BUTTON"),
                ui::Loc("BACKSPACE - ОЧИСТИТЬ   ESC - ОТМЕНА", "BACKSPACE - CLEAR   ESC - CANCEL"));
        } else {
            footer.SetHints(
                ui::Loc("W/S/КОЛЕСО - СПИСОК   ЛКМ/ENTER - НАЗНАЧИТЬ   TAB - РЕЖИМ",
                        "W/S/WHEEL - LIST   LMB/ENTER - BIND   TAB - MODE"),
                ui::Loc("ПКМ/BACKSPACE - НАЗАД", "RMB/BACKSPACE - BACK"));
        }
        footer.Render();

        if (!coopOverlay) ui::RenderMenuCursor(mx, my);
        return;
    }

    // 6. BRIEF PAGE (page == 2, in-game only!)
    if (page == 2 && inGame) {
        auto briefs = sdk::ReadPreviousBriefs(20);
        constexpr int kPerView = 6;
        int maxScr = std::max(0, static_cast<int>(briefs.size()) - kPerView);
        g_st.briefScroll = std::clamp(g_st.briefScroll, 0, maxScr);

        char badge[32] = {};
        if (!briefs.empty()) {
            std::snprintf(badge, sizeof(badge), "%d / %d",
                          g_st.briefScroll + 1, static_cast<int>(briefs.size()));
        }

        ui::MenuInfoCard briefCard;
        briefCard.SetBounds(m.frameX, m.contentY, m.frameW, m.contentH)
                 .SetHeader(ui::Loc("ЖУРНАЛ СООБЩЕНИЙ", "BRIEF HISTORY"), badge);
        if (briefs.empty()) {
            briefCard.SetDescription(ui::Loc("Недавних сообщений и заданий пока нет.",
                                             "No recent mission briefs or messages recorded yet."));
        } else {
            for (int i = g_st.briefScroll;
                 i < static_cast<int>(briefs.size()) && i < g_st.briefScroll + kPerView;
                 ++i) {
                briefCard.AddParagraph(briefs[i]);
            }
        }
        briefCard.Render();

        ui::MenuFooterBar footer;
        footer.SetHints(
            ui::Loc("W/S ИЛИ КОЛЕСО - ПРОКРУТКА   Q/E - ВКЛАДКИ", "W/S OR WHEEL - SCROLL   Q/E - TABS"),
            ui::Loc("BACKSPACE/ПКМ - НАЗАД", "BACKSPACE/RMB - BACK"));
        footer.Render();

        if (!coopOverlay) ui::RenderMenuCursor(mx, my);
        return;
    }

    // 7. EXTRACT DYNAMIC PAGE ENTRIES FROM aScreens[page]
    std::vector<ResolvedEntry> entries;
    std::string staticPrompt;
    BuildPageEntries(mm, page, entries, staticPrompt);
    int selVisIdx = FindVisibleIndexForRaw(entries, sdk::SelectedItem(mm));

    // 8. CENTERED MODAL CONFIRMATION / NOTICE PAGES
    if (IsModalPage(page)) {
        ui::MenuConfirmModal modal;
        BuildModalDialog(page, entries, staticPrompt, selVisIdx, modal);
        modal.Render();

        ui::MenuFooterBar footer;
        footer.SetHints(
            ui::Loc("A/D - ВЫБОР   ЛКМ/F/ENTER - ПОДТВЕРДИТЬ", "A/D - SELECT   LMB/F/ENTER - CONFIRM"),
            ui::Loc("ПКМ/BACKSPACE - ОТМЕНА", "RMB/BACKSPACE - CANCEL"));
        footer.Render();

        if (!coopOverlay) ui::RenderMenuCursor(mx, my);
        return;
    }

    // 9. STANDARD DEFINITIVE-EDITION TWO-COLUMN CONTENT LAYOUT
    ui::MenuOptionList leftList;
    leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH)
            .SetHeader(GetPageHeaderTitle(page, mm))
            .SetSelection(selVisIdx, g_st.hoveredRow)
            .ClearItems();
    for (const auto& e : entries) {
        leftList.AddItem(e.uiItem);
    }
    leftList.Render();

    ui::MenuInfoCard rightCard;
    rightCard.SetBounds(m.rightColX, m.contentY, m.rightColW, m.contentH);
    PopulateRightInfoCard(mm, page, entries, selVisIdx, rightCard);
    rightCard.Render();

    // 10. MINIMALIST BOTTOM FOOTER BAR
    ui::MenuFooterBar footer;
    std::string leftHint;
    if (page == 3 || page == 4 || page == 27 || page == 39 || page == 40 || page == 26) {
        leftHint = ui::Loc("W/S - СПИСОК   A/D - ИЗМЕНИТЬ   ЛКМ/F/ENTER - ВЫБРАТЬ",
                           "W/S - LIST   A/D - ADJUST   LMB/F/ENTER - SELECT");
    } else {
        leftHint = ui::Loc("W/S/КОЛЕСО - СПИСОК   ЛКМ/F/ENTER - ВЫБРАТЬ",
                           "W/S/WHEEL - LIST   LMB/F/ENTER - SELECT");
    }
    std::string rightHint = hasTabs
                                ? ui::Loc("Q/E - ВКЛАДКИ   ПКМ/BACKSPACE - НАЗАД",
                                          "Q/E - TABS   RMB/BACKSPACE - BACK")
                                : ui::Loc("ПКМ/BACKSPACE - НАЗАД", "RMB/BACKSPACE - BACK");
    footer.SetHints(leftHint, rightHint);
    footer.Render();

    if (!coopOverlay) ui::RenderMenuCursor(mx, my);
}

void HandleModernMenuInput(uint8_t* mm, FnUserInput origUserInput) {
    ui::SyncFromSharedBus();
    EnsureCoopHookedOnce();

    if (ShouldPassThrough(mm)) {
        g_st.draggingSlider = false;
        if (origUserInput) origUserInput(mm);
        return;
    }

    const uint32_t now    = GetTickCount();
    const int8_t   page   = SanitizeAndGetEffectivePage(mm);
    const bool     inGame = sdk::IsInGame(mm);
    const bool     hasTabs = ShouldShowTopTabs(page);

    // While waiting for a native keybind on page 38, hand full control to GTA SA's CheckRedefineControlInput!
    if (page == 38 && sdk::IsWaitingForKeybind(mm)) {
        if (origUserInput) origUserInput(mm);
        return;
    }

    // Helper: sample edge-triggered keys cleanly
    auto keyDown = [](int vk) -> bool { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    const bool lmb   = keyDown(VK_LBUTTON);
    const bool rmb   = keyDown(VK_RBUTTON);
    const bool enter = keyDown(VK_RETURN);
    const bool keyF  = keyDown('F');
    const bool back  = keyDown(VK_BACK);
    const bool esc   = keyDown(VK_ESCAPE);
    const bool tab   = keyDown(VK_TAB);
    const bool up    = keyDown(VK_UP)    || keyDown('W');
    const bool down  = keyDown(VK_DOWN)  || keyDown('S');
    const bool left  = keyDown(VK_LEFT)  || keyDown('A');
    const bool right = keyDown(VK_RIGHT) || keyDown('D');
    const bool keyQ  = keyDown('Q');
    const bool keyE  = keyDown('E');
    const bool pgUp  = keyDown(VK_PRIOR);
    const bool pgDn  = keyDown(VK_NEXT);

    const bool justLmb   = lmb   && !g_st.prevLmb;
    const bool justRmb   = rmb   && !g_st.prevRmb;
    const bool justEnter = enter && !g_st.prevEnter;
    const bool justF     = keyF  && !g_st.prevF;
    const bool justBack  = back  && !g_st.prevBack;
    const bool justEsc   = esc   && !g_st.prevEsc;
    const bool justTab   = tab   && !g_st.prevTab;
    const bool justUp    = up    && !g_st.prevUp;
    const bool justDown  = down  && !g_st.prevDown;
    const bool justLeft  = left  && !g_st.prevLeft;
    const bool justRight = right && !g_st.prevRight;
    const bool justQ     = keyQ  && !g_st.prevQ;
    const bool justE     = keyE  && !g_st.prevE;
    const bool justPgUp  = pgUp  && !g_st.prevPgUp;
    const bool justPgDn  = pgDn  && !g_st.prevPgDn;

    bool repUp = justUp, repDown = justDown, repLeft = justLeft, repRight = justRight;
    if (up || down || left || right) {
        if (justUp || justDown || justLeft || justRight) {
            g_st.holdDirStartMs = now;
            g_st.lastHoldStepMs = now;
        } else if ((now - g_st.holdDirStartMs) > 340u && (now - g_st.lastHoldStepMs) > 95u) {
            g_st.lastHoldStepMs = now;
            if (up)    repUp    = true;
            if (down)  repDown  = true;
            if (left)  repLeft  = true;
            if (right) repRight = true;
        }
    }

    auto commitPrevKeys = [&]() {
        g_st.prevLmb   = lmb;
        g_st.prevRmb   = rmb;
        g_st.prevEnter = enter;
        g_st.prevF     = keyF;
        g_st.prevBack  = back;
        g_st.prevEsc   = esc;
        g_st.prevTab   = tab;
        g_st.prevUp    = up;
        g_st.prevDown  = down;
        g_st.prevLeft  = left;
        g_st.prevRight = right;
        g_st.prevQ     = keyQ;
        g_st.prevE     = keyE;
        g_st.prevPgUp  = pgUp;
        g_st.prevPgDn  = pgDn;
    };

    const bool justReopened = (!g_st.wasMenuActive || (now - g_st.lastTickMs) > 250u);
    g_st.lastTickMs = now;
    if (justReopened) {
        g_st.wasMenuActive    = true;
        g_st.openedAtMs       = now;
        g_st.lastPage         = page;
        g_st.draggingSlider   = false;
        g_st.hoveredTab       = -1;
        g_st.hoveredRow       = -1;
        g_st.hoveredModalBtn  = -1;
        g_st.textEditActive   = false;
        commitPrevKeys();
        return;
    }
    if (page != g_st.lastPage) {
        g_st.lastPage        = page;
        g_st.draggingSlider  = false;
        g_st.hoveredRow      = -1;
        g_st.hoveredModalBtn = -1;
        g_st.statsScroll     = 0;
        g_st.briefScroll     = 0;
        g_st.controlsScroll  = 0;
        g_st.lastInputMs     = now;
        commitPrevKeys();
        return;
    }

    if (!lmb) {
        g_st.draggingSlider = false;
    }

    if (!integrations::sacoop::IsGameWindowFocused() || IsSaCoopOverlayBlockingInput()) {
        g_st.draggingSlider = false;
        commitPrevKeys();
        return;
    }
    if ((now - g_st.openedAtMs) < 120u) {
        commitPrevKeys();
        return;
    }

    // Inline text-input modal inside Multiplayer tab captures keyboard exclusively!
    if (page == kPageMultiplayer && g_st.textEditActive) {
        HandleMpTextEditInput();
        commitPrevKeys();
        return;
    }

    const std::string crumb = GetPageBreadcrumb(inGame, page, mm);
    const auto m = ui::ComputeMenuShellMetrics(hasTabs, !crumb.empty());
    float mx = 0.0f, my = 0.0f;
    SyncCursorPosition(mm, m.sw, m.sh, mx, my);

    auto* ms = sa::GetNewMouseState();
    const bool wheelUp   = ms && (ms->wheelUp != 0);
    const bool wheelDown = ms && (ms->wheelDown != 0);

    // 1. TOP BAR TAB HOVER & CLICK + Q/E TAB SWITCHING
    ui::MenuTopBar topBar;
    BuildTopBar(mm, page, hasTabs, crumb, topBar);
    g_st.hoveredTab = hasTabs ? topBar.HitTestTab(mx, my) : -1;

    if (hasTabs) {
        if (justLmb && g_st.hoveredTab >= 0) {
            ActivateTopTab(mm, g_st.hoveredTab);
            commitPrevKeys();
            return;
        }
        if (justQ) {
            CycleTopTab(mm, -1);
            commitPrevKeys();
            return;
        }
        if (justE) {
            CycleTopTab(mm, +1);
            commitPrevKeys();
            return;
        }
    }

    // 2. DEDICATED SA-COOP MULTIPLAYER TAB INPUT (Main Menu only!)
    if (page == kPageMultiplayer && !inGame) {
        auto mpItems = BuildMultiplayerItems();
        int count = static_cast<int>(mpItems.size());

        ui::MenuOptionList leftList;
        leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH)
                .SetHeader(GetPageHeaderTitle(kPageMultiplayer))
                .SetScrollOffset(g_st.mpScroll)
                .ClearItems();
        for (const auto& it : mpItems) leftList.AddItem(it);

        const int prevHover = g_st.hoveredRow;
        g_st.hoveredRow = leftList.HitTestRow(mx, my);
        if (g_st.hoveredRow >= 0 && g_st.hoveredRow != prevHover && g_st.hoveredRow < count) {
            if (g_st.hoveredRow != g_st.mpSelected) {
                g_st.mpSelected = g_st.hoveredRow;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        }

        if (repUp || wheelUp) {
            g_st.mpSelected = (g_st.mpSelected - 1 + count) % count;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (repDown || wheelDown) {
            g_st.mpSelected = (g_st.mpSelected + 1) % count;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }

        if (repLeft || repRight) {
            AdjustMultiplayerRow(g_st.mpSelected, repRight ? 1 : -1, false);
        } else if (justEnter || justF || (justLmb && g_st.hoveredRow >= 0)) {
            int tgt = (justLmb && g_st.hoveredRow >= 0) ? g_st.hoveredRow : g_st.mpSelected;
            g_st.mpSelected = tgt;
            AdjustMultiplayerRow(tgt, 1, true);
        } else if (justBack || justRmb) {
            g_st.inMultiplayerTab = false;
            sa::PlayFrontendSound(sa::SFX_BACK);
            sdk::NavigateToPage(mm, 34);
        }
        commitPrevKeys();
        return;
    }

    // 3. MODERNIZED REDEFINE CONTROLS INPUT (page == 38)
    if (page == 38) {
        const bool isVeh = (sdk::ControlSubMode(mm) != 0);
        const bool isJoy = (sdk::ControlMethod(mm) != 0);
        auto rows = sdk::BuildControlRows(isVeh, isJoy);
        int count = static_cast<int>(rows.size());
        int sel   = std::clamp(sdk::SelectedControlRow(mm), 0, std::max(0, count - 1));

        // Quick toggle Foot <-> Vehicle right inside Redefine Controls!
        if (justTab || repLeft || repRight) {
            sdk::ControlSubMode(mm) = isVeh ? 0 : 1;
            sdk::SelectedControlRow(mm) = 0;
            g_st.controlsScroll = 0;
            sa::PlayFrontendSound(sa::SFX_SELECT);
            commitPrevKeys();
            return;
        }

        ui::MenuOptionList leftList;
        leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH)
                .SetHeader(GetPageHeaderTitle(38, mm))
                .SetScrollOffset(g_st.controlsScroll)
                .ClearItems();
        for (int i = 0; i < count; ++i) {
            ui::MenuOptionItem it{};
            it.labelGame = ui::Loc(rows[i].rusLabel, rows[i].engLabel);
            it.enabled   = true;
            leftList.AddItem(it);
        }

        const int prevHover = g_st.hoveredRow;
        g_st.hoveredRow = leftList.HitTestRow(mx, my);
        if (g_st.hoveredRow >= 0 && g_st.hoveredRow != prevHover && g_st.hoveredRow < count) {
            if (g_st.hoveredRow != sel) {
                sel = g_st.hoveredRow;
                sdk::SelectedControlRow(mm) = sel;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        }

        if (repUp || wheelUp) {
            sel = (sel - 1 + count) % count;
            sdk::SelectedControlRow(mm) = sel;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (repDown || wheelDown) {
            sel = (sel + 1) % count;
            sdk::SelectedControlRow(mm) = sel;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (justPgUp) {
            sel = std::max(0, sel - 6);
            sdk::SelectedControlRow(mm) = sel;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        } else if (justPgDn) {
            sel = std::min(count - 1, sel + 6);
            sdk::SelectedControlRow(mm) = sel;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }

        if ((justEnter || justF || (justLmb && g_st.hoveredRow >= 0)) && count > 0) {
            int tgt = (justLmb && g_st.hoveredRow >= 0) ? g_st.hoveredRow : sel;
            if (tgt >= 0 && tgt < count) {
                sdk::BeginKeybindCapture(mm, tgt, rows[tgt].actionId);
                sa::PlayFrontendSound(sa::SFX_SELECT);
            }
            commitPrevKeys();
            return;
        }
        if (justBack || justRmb) {
            sdk::CancelKeybindCapture(mm);
            sa::PlayFrontendSound(sa::SFX_BACK);
            sdk::NavigateToPage(mm, 37);
            commitPrevKeys();
            return;
        }
        (void)justEsc;
        commitPrevKeys();
        return;
    }

    // 4. INTERACTIVE MAP PAGE (page == 5, in-game only!)
    if (page == 5 && inGame) {
        if (justBack) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
            commitPrevKeys();
            return;
        }
        commitPrevKeys();
        if (origUserInput) origUserInput(mm);
        return;
    }

    // 5. BRIEF PAGE (page == 2, in-game only!)
    if (page == 2 && inGame) {
        auto briefs = sdk::ReadPreviousBriefs(20);
        int maxScr = std::max(0, static_cast<int>(briefs.size()) - 6);
        if (repUp || wheelUp) {
            if (g_st.briefScroll > 0) {
                --g_st.briefScroll;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        } else if (repDown || wheelDown) {
            if (g_st.briefScroll < maxScr) {
                ++g_st.briefScroll;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        }
        if (justBack || justRmb) {
            sa::PlayFrontendSound(sa::SFX_BACK);
            sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
        }
        commitPrevKeys();
        return;
    }

    // 6. EXTRACT CURRENT PAGE ENTRIES
    std::vector<ResolvedEntry> entries;
    std::string staticPrompt;
    BuildPageEntries(mm, page, entries, staticPrompt);
    int selVisIdx = FindVisibleIndexForRaw(entries, sdk::SelectedItem(mm));
    if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size())) {
        sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
    }

    // 7. MODAL CONFIRMATION / NOTICE PAGES
    if (IsModalPage(page)) {
        ui::MenuConfirmModal modal;
        BuildModalDialog(page, entries, staticPrompt, selVisIdx, modal);
        g_st.hoveredModalBtn = modal.HitTestButton(mx, my);

        const int btnCount = static_cast<int>(entries.size());
        if (btnCount > 0) {
            if (g_st.hoveredModalBtn >= 0 && g_st.hoveredModalBtn < btnCount && g_st.hoveredModalBtn != selVisIdx) {
                selVisIdx = g_st.hoveredModalBtn;
                sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
            if (repLeft || repUp) {
                selVisIdx = (selVisIdx - 1 + btnCount) % btnCount;
                sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            } else if (repRight || repDown) {
                selVisIdx = (selVisIdx + 1) % btnCount;
                sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }

            if (justEnter || justF || (justLmb && g_st.hoveredModalBtn >= 0)) {
                if (justLmb && g_st.hoveredModalBtn >= 0) {
                    sdk::SelectedItem(mm) = entries[g_st.hoveredModalBtn].rawIndex;
                }
                sdk::ProcessUserInput(mm, 0, 0, 1, 0, 0);
                commitPrevKeys();
                return;
            }
        }
        if (justBack || justRmb) {
            sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
        }
        commitPrevKeys();
        return;
    }

    // 8. STANDARD TWO-COLUMN OPTION / STATS / SAVE-LOAD PAGES
    ui::MenuOptionList leftList;
    leftList.SetBounds(m.frameX, m.contentY, m.leftColW, m.contentH)
            .SetHeader(GetPageHeaderTitle(page, mm))
            .SetSelection(selVisIdx, g_st.hoveredRow)
            .ClearItems();
    for (const auto& e : entries) {
        leftList.AddItem(e.uiItem);
    }

    if (g_st.draggingSlider && lmb) {
        int dragVis = FindVisibleIndexForRaw(entries, g_st.dragRawItem);
        float frac = 0.0f;
        if (dragVis >= 0 && leftList.HitTestSlider(dragVis, mx, my, frac)) {
            ApplySliderTargetFrac(mm, g_st.dragRawItem, g_st.dragActionType, frac);
        } else if (dragVis >= 0) {
            const float padX = 8.5f * m.xScale;
            const float barW = std::min(84.0f * m.xScale, m.leftColW * 0.36f);
            const float barX = m.frameX + m.leftColW - padX - barW;
            float f = std::clamp((mx - barX) / std::max(1.0f, barW), 0.0f, 1.0f);
            ApplySliderTargetFrac(mm, g_st.dragRawItem, g_st.dragActionType, f);
        }
        commitPrevKeys();
        return;
    }

    const int prevHover = g_st.hoveredRow;
    g_st.hoveredRow = leftList.HitTestRow(mx, my);
    if (g_st.hoveredRow >= 0 && g_st.hoveredRow != prevHover && g_st.hoveredRow < static_cast<int>(entries.size())) {
        if (g_st.hoveredRow != selVisIdx && entries[g_st.hoveredRow].uiItem.enabled) {
            selVisIdx = g_st.hoveredRow;
            sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }
    }

    auto stepSelection = [&](int dir) {
        const int n = static_cast<int>(entries.size());
        if (n <= 1) return;
        for (int tries = 0; tries < n; ++tries) {
            selVisIdx = (selVisIdx + dir + n) % n;
            if (entries[selVisIdx].uiItem.enabled) {
                sdk::SelectedItem(mm) = entries[selVisIdx].rawIndex;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                break;
            }
        }
    };

    const bool overRightCard = (mx >= m.rightColX && mx <= m.rightColX + m.rightColW &&
                                my >= m.contentY && my <= m.contentY + m.contentH);
    if (page == 0 && inGame && (overRightCard && (wheelUp || wheelDown))) {
        int cat   = (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size()) && !entries[selVisIdx].uiItem.isBack)
                        ? std::clamp(entries[selVisIdx].rawIndex, 0, 7) : 0;
        int total = sdk::GetStatCategoryCount(static_cast<uint8_t>(cat));
        int maxS  = std::max(0, total - 10);
        if (wheelUp && g_st.statsScroll > 0)          { g_st.statsScroll = std::max(0, g_st.statsScroll - 2); sa::PlayFrontendSound(sa::SFX_SCROLL); }
        else if (wheelDown && g_st.statsScroll < maxS) { g_st.statsScroll = std::min(maxS, g_st.statsScroll + 2); sa::PlayFrontendSound(sa::SFX_SCROLL); }
    } else {
        if (repUp || wheelUp)          stepSelection(-1);
        else if (repDown || wheelDown) stepSelection(+1);
    }

    if (page == 0 && inGame) {
        int cat   = (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size()) && !entries[selVisIdx].uiItem.isBack)
                        ? std::clamp(entries[selVisIdx].rawIndex, 0, 7) : 0;
        int total = sdk::GetStatCategoryCount(static_cast<uint8_t>(cat));
        int maxS  = std::max(0, total - 10);
        if (repLeft || justPgUp) {
            int step = justPgUp ? 6 : 2;
            if (g_st.statsScroll > 0) {
                g_st.statsScroll = std::max(0, g_st.statsScroll - step);
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        } else if (repRight || justPgDn) {
            int step = justPgDn ? 6 : 2;
            if (g_st.statsScroll < maxS) {
                g_st.statsScroll = std::min(maxS, g_st.statsScroll + step);
                sa::PlayFrontendSound(sa::SFX_SCROLL);
            }
        }
    } else if (selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size())) {
        const auto& cur = entries[selVisIdx];
        if (cur.entryType == 12 && cur.uiItem.enabled && (repLeft || repRight)) {
            sdk::SelectedItem(mm) = cur.rawIndex;
            sdk::ProcessUserInput(mm, 0, 0, 0, 0, repRight ? 1 : -1);
            sa::PlayFrontendSound(sa::SFX_SCROLL);
        }
    }

    // LMB click on left list
    if (justLmb && g_st.hoveredRow >= 0 && g_st.hoveredRow < static_cast<int>(entries.size())) {
        const auto& clicked = entries[g_st.hoveredRow];
        if (clicked.uiItem.enabled) {
            sdk::SelectedItem(mm) = clicked.rawIndex;
            float frac = 0.0f;
            if (clicked.uiItem.hasSlider && leftList.HitTestSlider(g_st.hoveredRow, mx, my, frac)) {
                ApplySliderTargetFrac(mm, clicked.rawIndex, clicked.actionType, frac);
                g_st.draggingSlider = true;
                g_st.dragRawItem    = clicked.rawIndex;
                g_st.dragActionType = clicked.actionType;
                sa::PlayFrontendSound(sa::SFX_SCROLL);
                commitPrevKeys();
                return;
            }
            if (clicked.uiItem.isBack) {
                sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
            } else if (clicked.entryType == 12 && clicked.actionType != 56 && clicked.actionType != 44 && !clicked.uiItem.hasSlider) {
                sdk::ProcessUserInput(mm, 0, 0, 0, 0, 1);
                sa::PlayFrontendSound(sa::SFX_SELECT);
            } else {
                sdk::ProcessUserInput(mm, 0, 0, 1, 0, 0);
                if (sdk::MenuActive(mm) == 0) {
                    g_st.wasMenuActive = false;
                    ui::TriggerPostCloseShield(220);
                }
            }
            commitPrevKeys();
            return;
        }
    }

    // Enter / F activation on highlighted row
    if ((justEnter || justF) && selVisIdx >= 0 && selVisIdx < static_cast<int>(entries.size())) {
        const auto& cur = entries[selVisIdx];
        if (cur.uiItem.enabled) {
            sdk::SelectedItem(mm) = cur.rawIndex;
            if (cur.uiItem.isBack) {
                sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
            } else if (cur.entryType == 12 && cur.actionType != 56 && cur.actionType != 44 && !cur.uiItem.hasSlider) {
                sdk::ProcessUserInput(mm, 0, 0, 0, 0, 1);
                sa::PlayFrontendSound(sa::SFX_SELECT);
            } else {
                sdk::ProcessUserInput(mm, 0, 0, 1, 0, 0);
                if (sdk::MenuActive(mm) == 0) {
                    g_st.wasMenuActive = false;
                    ui::TriggerPostCloseShield(220);
                }
            }
            commitPrevKeys();
            return;
        }
    }

    // Backspace / RMB cancels/goes back cleanly
    if (justBack || justRmb) {
        if (inGame && page == 41) {
            sdk::MenuActive(mm) = 0;
            g_st.wasMenuActive  = false;
            ui::TriggerPostCloseShield(220);
            sa::PlayFrontendSound(sa::SFX_BACK);
        } else if (page != 34) {
            sdk::ProcessUserInput(mm, 0, 0, 0, 1, 0);
        }
        commitPrevKeys();
        return;
    }

    commitPrevKeys();
}

} // namespace modern_menu
