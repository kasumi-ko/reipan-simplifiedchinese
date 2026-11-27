/* ==========================================================================
 *  ingame/event/prg/ev_main.c
 *
 *  The event system's top level (FF3 0x19b768..0x19baf8; FF2 ev_main.c).
 *  EventInit / EventRelease reset every event subsystem, EventDataLoad
 *  starts the loads the events need (the event file, the ladder list, the
 *  event camera's VCI pack), EventMain is the per-frame driver and
 *  EventEnd / EventAfterLoad / EventAfterSave turn the saved event
 *  addresses into offsets and back around a memory-card copy.
 *
 *  FF3 differs from FF2 here: ev_wrk and the sister gaze moved out (the
 *  event states are ev_change.c's), the event file is EVENT_OBJ (or its
 *  debug / mission-mode variants) at EE 0xf82000 instead of FF2's PAL / NTSC
 *  macro paks, and EventMain does not run while the player is hurt or dying.
 * ======================================================================== */
#include "ingame/event/prg/ev_main.h"

#include <stddef.h>                             // offsetof
#include <string.h>                             // memset

#include "ingame/camera/camVci.h"              // camVci, GetEvCamVci
#include "ingame/event/prg/ev_change.h"         // EvWrkInit, EvChangeCtrlInit, EvDreamPlayerInit, CompulsionSetEventStateMain
#include "ingame/event/prg/ev_command.h"        // EvMsgCtrlInit / Release
#include "ingame/event/prg/ev_chapter.h"        // EvChapterDispInit / Release
#include "ingame/event/prg/ev_ladder.h"          // GetLadderList, LadderList
#include "ingame/event/prg/ev_exe.h"            // EventExeInit, EventExe, EvExeCtrlToSave / FromSave
#include "ingame/event/prg/ev_open.h"           // EvCtrlCenterInit, EventCtrlCenterMain, EventSetOpenCondition, ...
#include "ingame/event/prg/ev_se.h"             // ev_seInit / Release
#include "ingame/event/prg/ev_timer.h"          // EvTimerCtrlInit / Main
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_MODE, PlyrDeathIsRevived
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer
#include "system/eeiop/cddat.h"                 // file IDs and GetFileSize
#include "system/os/eecdvd.h"                   // LoadReqGetAddr, IsLoadEnd
#include "system/os/system.h"                   // GetPALMode

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// data 0x325188 (0x0c bytes, zero in the ELF): EventRelease clears it whole.
struct EV_MAIN_WRK
{
    /* 0x00 */ int mode;                // EventSetMode: 1 / 2 load EVENT_DB_OBJ
    /* 0x04 */ int stop;                // EventSetStop: nonzero skips EventMain
    /* 0x08 */ int load_id;             // the event file's load request (EventDataLoad)
};
static_assert(sizeof(EV_MAIN_WRK) == 0x0c, "EV_MAIN_WRK is 0x0c bytes");
static_assert(offsetof(EV_MAIN_WRK, stop) == 0x04, "EV_MAIN_WRK.stop");
static_assert(offsetof(EV_MAIN_WRK, load_id) == 0x08, "EV_MAIN_WRK.load_id");

static EV_MAIN_WRK ev_main_wrk;         // data 0x325188

// bss 0x3e3ff8: a camVci nothing in FF3 uses (the event camera's is
// GetEvCamVci's). Built by EvMain_static_init 0x19ba40 (_GLOBAL__I_ 0x19bab8,
// _GLOBAL__D_ 0x19bad8); kept for the constructor / destructor calls.
static camVci ev_main_cam_vci;

// ──────────────────────────────────────────────────────────────────────
// The event mode (title_menu.c sets 0 when the title starts the game).
// FF3 0x0019b768.

void EventSetMode(int mode)
{
    ev_main_wrk.mode = mode;
}

// FF3 0x0019b778 (IngameNowLoadingMain: mode 2 keeps the start area).
int EventGetMode(void)
{
    return ev_main_wrk.mode;
}

// FF3 0x0019b788 (no callers).
void EventSetStop(int stop)
{
    ev_main_wrk.stop = stop;
}

// ──────────────────────────────────────────────────────────────────────
// Every event subsystem back to its start (IngameInit, a new game's reset).
// FF3 0x0019b798; FF2 EventInit (FF2 cleared ev_wrk here and ran
// EvCondCtrlInit / EvTalkInit / EvDispInit and the sister gaze instead).

void EventInit(void)
{
    ev_seInit();
    EvWrkInit();
    EvCtrlCenterInit();
    EvCondCtrlInit();
    EventExeInit();
    EvTimerCtrlInit();
    EvChangeCtrlInit();
    EvChapterDispInit();
    EvMsgCtrlInit();
    EvDreamPlayerInit();
    ev_main_wrk.stop = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Drops what the events hold: the SE groups, the chapter title, the message
// window, the work above and the two packs EventDataLoad loaded (IngameEnd,
// a new game).
// FF3 0x0019b808.

void EventRelease(void)
{
    ev_seRelease();
    EvChapterDispRelease();
    EvMsgCtrlRelease();
    memset(&ev_main_wrk, 0, sizeof(ev_main_wrk));
    ev_main_wrk.mode = 0;               // FF3 stores it again after the memset
    GetLadderList()->Release();
    GetEvCamVci()->Release();
}

// ──────────────────────────────────────────────────────────────────────
// 1 once the event file, the ladder list and the VCI pack have arrived.
// FF3 0x0019b868 (no callers).

int EventDataIsLoadEnd(void)
{
    if (IsLoadEnd(ev_main_wrk.load_id) == 0) {
        return 0;
    }
    if (GetLadderList()->IsLoadEnd() == 0) {
        return 0;
    }
    return GetEvCamVci()->IsLoadEnd() != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Starts the loads of the event data: the event file (mission mode: the
// mission events, by PAL mode; event modes 1 and 2: the debug build of the
// story events; else the story events) to EE 0xf82000, the ladder list and
// the event camera's VCI pack. IngameInit and the difficulty select pass 0,
// IngameStartNewGame (mission mode) 1.
// FF3 0x0019b8c0.

void EventDataLoad(int mission)
{
    int file = EVENT_OBJ;

    if (mission == 1) {
        file = GetPALMode() != 0 && GetFileSize(EV_MISSION_50_OBJ) != (u_int)-1
            ? EV_MISSION_50_OBJ : EV_MISSION_OBJ;
    } else if (ev_main_wrk.mode == 1 || ev_main_wrk.mode == 2) {
        file = EVENT_DB_OBJ;
    }

    LoadReqGetAddr(file, ReiPan_GetHostPointer(EVENT_DATA_ADDR), &ev_main_wrk.load_id);
    GetLadderList()->Load();
    GetEvCamVci()->Load();
}

// ──────────────────────────────────────────────────────────────────────
// The story's root event waits for its open condition (IngameNowLoadingMain,
// a new game or a mission start). FF3 0x0019b950; FF2 EventRootStart.

void EventOpenConditionClear(void)
{
    EventSetOpenCondition(0);
}

// ──────────────────────────────────────────────────────────────────────
// One frame of the events (IngameMain): forced state changes, timers, the
// open / close conditions, then every running event program. Nothing runs
// while EventSetStop holds it, while the player takes damage (modes 1..4) or
// in the death scene until the player is revived.
// FF3 0x0019b968; FF2 EventMain (which also drew the debug event rectangle
// and stepped the sister gaze).

void EventMain(void)
{
    u_char mode;

    if (ev_main_wrk.stop != 0) {
        return;
    }

    mode = ingame_wrk.plyr_wrk->mode;
    if (mode >= PLYR_MODE_DMG1 && mode <= PLYR_MODE_DMG4) {
        return;
    }
    if (mode == PLYR_MODE_7 && PlyrDeathIsRevived() == 0) {
        return;
    }

    CompulsionSetEventStateMain();
    EvTimerMain();
    EventCtrlCenterMain();
    EventExe();
}

// ──────────────────────────────────────────────────────────────────────
// After a game file was loaded from the card (McLoadGameMcLoadWait): the
// saved offsets in the control center and the execution slots become
// addresses again. FF3 0x0019b9e8.

void EventAfterLoad(void)
{
    EvCtrlCenterFromSave();
    EvExeCtrlFromSave();
}

// ──────────────────────────────────────────────────────────────────────
// Before a game file is copied for the card: the addresses become offsets
// into the event file. FF3 0x0019ba08; FF2 EventEnd released the event
// streams instead.

void EventEnd(void)
{
    EvCtrlCenterToSave();
    EvExeCtrlToSave();
}

// FF3 0x0019ba28: after the copy, the game goes on with addresses.
void EventAfterSave(void)
{
    EventAfterLoad();
}
