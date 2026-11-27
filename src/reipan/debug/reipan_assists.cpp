/* ==========================================================================
 *  reipan/debug/reipan_assists.cpp
 *
 *  The Assists menu's work (reipan_assists.h). Every write is one the game's
 *  own unlock paths make -- FINDER_STATE_WRK::Clear, the shop's purchase,
 *  phase 77's group opening -- only all of them at once.
 * ======================================================================== */
#include "reipan/debug/reipan_assists.h"

#include "ingame/ingame_wrk.h"                  // TitleMenuItemOpenSet, title_start_flg
#include "ingame/menu/start_menu.h"             // GetFinderStateWrk, GetFinderSlotWrk, StartMenuOptionApplyBought
#include "ingame/mission/mission_list.h"        // MissionGroupSetOpen
#include "ingame/photo/finder_photo.h"          // GetLensWrk, LENS_WRK
#include "main/gphase.h"                        // GetGPhaseCtrl, GPHASE_NODE
#include "reipan/debug/reipan_warp.h"           // ReiPan_WarpIdle, ReiPan_WarpInGame
#include "reipan/io/reipan_log.h"               // ReiPan_LogPrintf

#include <cstdlib>
#include <cstring>

namespace
{

constexpr int   kSlotBought      = 2;   // FINDER_SLOT_WRK: 0 locked, 1 for sale, 2 bought
constexpr int   kDifficulty      = 4;   // title_item_open 0..3: Easy, Normal, Hard, Nightmare
constexpr int   kNormal          = 1;   // the difficulty the clear is put on
constexpr short kPhaseStoryStart = 29;

bool g_unlock_all;
bool g_env_done;

// What StartMenuOptionApplyBought gives for the bought lens entries (slots
// 26..32): camera functions 5..8, and lenses 2, 3 and 5 for players 0 and 2.
// Calling it again switches the functions back on (AddFunc), so it only runs
// while something is missing.
bool LensComplete()
{
    static const u_char kLenses[] = {2, 3, 5};
    LENS_WRK           *lens      = GetLensWrk();

    for (int func = 5; func <= 8; func++)
    {
        if (lens->HaveFunc((u_char)func) == 0)
        {
            return false;
        }
    }
    for (u_char no : kLenses)
    {
        if (lens->IsHave(no, 0) == 0 || lens->IsHave(no, 2) == 0)
        {
            return false;
        }
    }
    return true;
}

void UnlockAll()
{
    for (int i = 0; i < kDifficulty; i++)
    {
        TitleMenuItemOpenSet(i, 1);
    }

    // The clear, unless a warp or event-browser job is starting a game: that
    // goes straight from Story_Start to Story_NowLoading, and a clear would
    // stop it in the start menu (the clear follows once the job is done).
    // On a fresh New Game in Story_Start the start menu's Continue has
    // nothing to continue, so it closes (_0c), as a clear save has it; a
    // loaded game keeps its own.
    FINDER_STATE_WRK *state = GetFinderStateWrk();
    if (FinderStateHasAny() == 0 && ReiPan_WarpIdle())
    {
        const GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;

        state->cnt[kNormal] = 1;
        if (leaf != nullptr && leaf->id == kPhaseStoryStart && title_start_flg.load == 0)
        {
            state->SetClearSaveFlag(1);
        }
    }
    state->_04 = 1;
    state->_08 = 1;
#ifdef REIPAN_SLPS_25544_CHS
    state->SetPromoMovieState(kSlotBought);
#endif

    FINDER_SLOT_WRK *slot = GetFinderSlotWrk();
    for (int i = 0; i < FINDER_SLOT_MAX; i++)
    {
        slot->Set(i, kSlotBought);
    }

    for (int i = 0; i < MISSION_GROUP_NUM; i++)
    {
        MissionGroupSetOpen(i);
    }

    // The lens work only holds a game's state once Story_Main is up (the
    // start menu included); IngameInit clears it before.
    if (ReiPan_WarpInGame() && !LensComplete())
    {
        StartMenuOptionApplyBought();
    }
}

} // namespace

void ReiPan_AssistsFrame(void)
{
    if (!g_env_done)
    {
        g_env_done = true;
        const char *env = std::getenv("REIPAN_UNLOCK_ALL");
        if (env != nullptr && *env != '\0' && std::strcmp(env, "0") != 0)
        {
            ReiPan_AssistsSetUnlockAll(1);
        }
    }
    if (g_unlock_all)
    {
        UnlockAll();
    }
}

int ReiPan_AssistsUnlockAll(void)
{
    return g_unlock_all;
}

void ReiPan_AssistsSetUnlockAll(int on)
{
    if (g_unlock_all == (on != 0))
    {
        return;
    }
    g_unlock_all = on != 0;
    ReiPan_LogPrintf("[assists] unlock everything %s\n", g_unlock_all ? "on" : "off");
}
