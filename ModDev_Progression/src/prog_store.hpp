#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <windows.h>
#include "common_utils.hpp"
#include "integrations/sacoop.hpp"
#include "log.hpp"
#include "prog_config.hpp"
#include "progression_rules.hpp"
#include "sa_sdk.hpp"

namespace progression {

struct StatSnapshot {
    int      missionsPassed = 0;
    int      territories    = 0;
    int      stunts         = 0;
    int      tags           = 0;
    int      snapshots      = 0;
    int      horseshoes     = 0;
    int      oysters        = 0;
    int      evadedStars    = 0;
    int      vigilanteKills = 0;
    int      firesOut       = 0;
    int      housesBurgled  = 0;
    int      truckMissions  = 0;
    int      girlsPimped    = 0;
    int      timesCheated   = 0;
    uint32_t paroleTime     = 0;
    bool     valid          = false;
};

inline StatSnapshot CaptureWorldStats() {
    StatSnapshot s{};
    s.missionsPassed = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_MISSIONS_PASSED)));
    s.territories    = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_TERRITORIES_TAKEN_OVER)));
    s.stunts         = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_UNIQUE_JUMPS_DONE)));
    s.tags           = std::max(0, sa::stats::GetSprayedTagsCount());
    s.snapshots      = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_SNAPSHOTS_TAKEN)));
    s.horseshoes     = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_HORSESHOES_COLLECTED)));
    s.oysters        = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_OYSTERS_COLLECTED)));
    s.evadedStars    = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_WANTED_STARS_EVADED)));
    s.vigilanteKills = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_CRIMINALS_VIGILANTE)));
    s.firesOut       = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_FIRES_EXTINGUISHED)));
    s.housesBurgled  = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_HOUSES_BURGLED)));
    s.truckMissions  = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_TRUCK_MISSIONS_PASSED)));
    s.girlsPimped    = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_GIRLS_PIMPED)));
    s.timesCheated   = std::max(0, static_cast<int>(sa::stats::Get(sa::stats::STAT_TIMES_CHEATED)));
    s.paroleTime     = sa::wanted::GetTimeOfParole(0);
    s.valid          = true;
    return s;
}

inline int ComputeMissionXpReward(int missionNumber1Based) {
    int m = std::clamp(missionNumber1Based, 1, 95);
    return 220 + m * 12;
}

class ProgressionStore {
public:
    int totalXp       = 0;
    int hostileKills  = 0;
    int headshots     = 0;
    int missionsCount = 0;
    int evadedCount   = 0;
    int territories   = 0;

    StatSnapshot baseline{};

    prog_rules::RankProgress GetProgress() const {
        return prog_rules::ComputeProgress(totalXp);
    }

    int GetSlot() const { return m_slot; }

    void ResetMemory() {
        totalXp       = 0;
        hostileKills  = 0;
        headshots     = 0;
        missionsCount = 0;
        evadedCount   = 0;
        territories   = 0;
        baseline      = StatSnapshot{};
        m_loadedFromFile = false;
    }

    void StartFreshNewGame() {
        m_slot = 0;
        ResetMemory();
        baseline = CaptureWorldStats();
        modlog::Info("[Prog] Started fresh new game -> Rank 1, 0 XP.");
    }

    // If loading an existing mid-game GTA SA save for the very first time (no progression file yet),
    // retroactively compute fair starting XP from completed missions, territories, stunts & collectibles!
    void BootstrapRetroactiveIfNeeded() {
        if (m_loadedFromFile) return;
        StatSnapshot cur = CaptureWorldStats();
        int retroXp = 0;
        for (int m = 1; m <= std::min(cur.missionsPassed, 120); ++m) {
            retroXp += ComputeMissionXpReward(m);
        }
        retroXp += std::min(cur.territories, 60) * 180;
        retroXp += std::min(cur.stunts, 70) * 120;
        int col = std::min(cur.tags, 100) + std::min(cur.snapshots, 50) +
                  std::min(cur.horseshoes, 50) + std::min(cur.oysters, 50);
        retroXp += col * 75;

        baseline = cur;
        if (retroXp > 0) {
            totalXp       = retroXp;
            missionsCount = cur.missionsPassed;
            territories   = cur.territories;
            m_loadedFromFile = true;
            SaveNow();
            auto p = GetProgress();
            modlog::Info("[Prog] Retroactive save sync -> missions=%d, territories=%d, collectibles=%d => %d XP (Rank %d).",
                         cur.missionsPassed, cur.territories, col, totalXp, p.rank);
        }
    }

    void LoadFromSlot(int slot1to8) {
        m_slot = (slot1to8 >= 1 && slot1to8 <= 8) ? slot1to8 : 0;
        ResetMemory();

        std::string path = GetFilePath(m_slot, true);
        if (!path.empty() && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const char* p = path.c_str();
            totalXp       = std::max(0, common::IniReadInt(p, "Progression", "TotalXp",       0));
            hostileKills  = std::max(0, common::IniReadInt(p, "Progression", "HostileKills",  0));
            headshots     = std::max(0, common::IniReadInt(p, "Progression", "Headshots",     0));
            missionsCount = std::max(0, common::IniReadInt(p, "Progression", "MissionsCount", 0));
            evadedCount   = std::max(0, common::IniReadInt(p, "Progression", "EvadedCount",   0));
            territories   = std::max(0, common::IniReadInt(p, "Progression", "Territories",   0));
            m_loadedFromFile = true;
            baseline = CaptureWorldStats();
            auto pr = GetProgress();
            modlog::Info("[Prog] Loaded slot %d (%s): Rank %d, %d XP.", m_slot, path.c_str(), pr.rank, totalXp);
        } else {
            baseline = CaptureWorldStats();
            if (m_slot > 0 || integrations::sacoop::IsCoopClient()) {
                BootstrapRetroactiveIfNeeded();
            }
        }
    }

    void SaveToSlot(int slot1to8) {
        if (slot1to8 >= 1 && slot1to8 <= 8) m_slot = slot1to8;
        SaveNow();
    }

    void SaveNow() {
        std::string path = GetFilePath(m_slot, false);
        if (path.empty()) return; // Unsaved Singleplayer/Host New Game: keep in RAM until saved to slot 1..8
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        auto pr = GetProgress();
        std::fprintf(f, "[Progression]\n");
        std::fprintf(f, "Rank=%d\n",          pr.rank);
        std::fprintf(f, "TotalXp=%d\n",       totalXp);
        std::fprintf(f, "HostileKills=%d\n",  hostileKills);
        std::fprintf(f, "Headshots=%d\n",     headshots);
        std::fprintf(f, "MissionsCount=%d\n", missionsCount);
        std::fprintf(f, "EvadedCount=%d\n",   evadedCount);
        std::fprintf(f, "Territories=%d\n",   territories);
        std::fclose(f);
        m_loadedFromFile = true;
    }

private:
    int  m_slot           = 0;
    bool m_loadedFromFile = false;

    std::string GetFilePath(int slot, bool forLoading) const {
        const char* ov = integrations::sacoop::GetProfileOverride();
        if (forLoading) {
            return integrations::sacoop::ResolveExistingLoadPath("GTA5ProgressionSaves", Cfg().modDir, "prog", slot, ov);
        }
        return integrations::sacoop::BuildSaveProfilePath("GTA5ProgressionSaves", Cfg().modDir, "prog", slot, ov);
    }
};

inline ProgressionStore& Store() {
    static ProgressionStore s;
    return s;
}

} // namespace progression
