/* ==========================================================================
 *  outgame/autoload.c
 *
 *  The boot-time system-file probe (FF3 0x153d68..0x154720, FF2
 *  outgame/autoload.c). Phase 14 AutoLoad_Main (outgame/boot.c) runs
 *  AutoLoadMain every frame between Boot_Load and the logos: it looks for the
 *  game directory on memory card port 0, loads the system file if it is
 *  there, and keeps a message (outgame/autoload_scr.c draws it) up for every
 *  way that can go wrong. Whatever happens it ends in step 3, and the phase
 *  goes on to the logos.
 *
 *  From MioPan outgame/autoload.c (FF2), checked against FF3 function by
 *  function. FF3 differences:
 *    - an outer step (AUTO_LOAD_CTRL.step) that waits two frames before the
 *      card steps run, and reports "finished" to the phase (FF2 jumped to the
 *      publisher screen itself);
 *    - FF2's nine card steps are eleven: the error and the empty-card
 *      messages each got an init step (AutoLoadMcErrorConfInit /
 *      AutoLoadMcEmptyErrorInit) that arms a card watch and a minimum display
 *      time, and while they wait for OK the card is polled every frame
 *      (AutoLoadMcCardWatch): pulling the card restarts the check;
 *    - drawing moved to autoload_scr.c, which reads auto_load_ctrl;
 *    - the "loaded" message also ends after 150 frames without OK.
 * ======================================================================== */
#include "outgame/autoload.h"

#include <string.h>

#include "outgame/autoload_scr.h"           // AutoLoadScrMsgIsOpen
#include "system/mc/prg/mc.h"               // MemoryCardExeInit, MemoryCardEnd
#include "system/mc/prg/mc_check.h"         // MemoryCardCheckInit / Main
#include "system/mc/prg/mc_check_broken.h"  // dir / file broken, new file
#include "system/mc/prg/mc_check_card.h"    // MemoryCardSetAccessPort, the card watch
#include "system/mc/prg/mc_check_empty.h"   // MemoryCardCheckEmpty / EmptyBroken
#include "system/mc/prg/mc_load.h"          // MemoryCardFileLoadInit / Main
#include "system/mc/prg/mc_set_data.h"      // paths, sizes, data buffer, DevelopMemoryCardLoadData
#include "system/os/system.h"               // GetPALMode
#include "system/pad/pad_ctrl.h"            // CheckPadTrgPlaySe, pad_key
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// AUTO_LOAD_CTRL::step: the outer state AutoLoadMain reports on.
#define AL_STEP_START       0               // first frame
#define AL_STEP_WAIT        1               // second frame
#define AL_STEP_CARD        2               // the card steps (mc_step) run
#define AL_STEP_END         3               // finished: the phase moves on

// AUTO_LOAD_CTRL::mc_step (AL_STEP_ERROR_CONF, AL_STEP_DEF_START,
// AL_STEP_EMPTY_ERROR and AL_STEP_EMPTY_ASK are in autoload.h).
#define AL_STEP_CHECK_INIT  0
#define AL_STEP_CHECK_WAIT  1
#define AL_STEP_LOAD_INIT   2
#define AL_STEP_LOAD_WAIT   3
#define AL_STEP_LOAD_CONF   4               // "loaded" message
#define AL_STEP_ERROR_INIT  5
#define AL_STEP_EMPTY_INIT  8

// The ROM's message ids (kind 0x83, AUTO_LOAD_SCR_MSG_KIND), by where they
// are set; the texts are in the message table.
#define AL_MSG_NONE         0x00
#define AL_MSG_NO_CARD      0x02            // the check failed (no / unusable card)
#define AL_MSG_LOAD_ERROR   0x0f            // the load failed
#define AL_MSG_DIR_BROKEN   0x19            // saved data damaged, room to rewrite it
#define AL_MSG_LOADING      0x1d            // while the system file loads
#define AL_MSG_DEF_START    0x23            // "start without saving?" (yes / no)
#define AL_MSG_EMPTY_ASK    0x24            // no room: "start anyway?" (yes / no)
#define AL_MSG_FILE_BROKEN  0x33            // the system file is damaged
#define AL_MSG_NO_DATA      0x35            // card unformatted / no game directory (0x36 once shown)
#define AL_MSG_NO_DATA_WAIT 0x36
#define AL_MSG_NO_ROOM      0x37            // no saved data and no room to make it
// AL_MSG_NO_BACKDROP 0x38 (autoload.h): the card is not a PS2 card

#define AL_LOAD_CONF_TIME   0x95            // "loaded" ends after 150 frames
#define AL_MSG_WAIT_PAL     75              // error messages stay at least 3 s
#define AL_MSG_WAIT_NTSC    90

#define AL_SE_CSR           0x1a9b0000      // SE_BANK_DEFAULT: cursor
#define AL_SE_CANCEL        0x1a9b0001
#define AL_SE_OK            0x1a9b0003

#define AL_PAD_LEFT         0x8000          // the yes / no cursor
#define AL_PAD_RIGHT        0x2000

// ══════════════════════════════════════════════════════════════════════
// Data
// ══════════════════════════════════════════════════════════════════════

AUTO_LOAD_CTRL auto_load_ctrl;                  // data 0x2f6400 (read by autoload_scr.c)
static void *auto_load_data_buff;               // sdata 0x36dbe0 (gp-0x5910): the system file

static void AutoLoadMcMain(void);
static void AutoLoadMcCheckInit(void);
static void AutoLoadMcCheckWait(void);
static void AutoLoadMcLoadInit(void);
static void AutoLoadMcLoadWait(void);
static void AutoLoadMcLoadConf(void);
static void AutoLoadMcMsgWaitInit(void);
static void AutoLoadMcCardWatch(void);
static void AutoLoadMcErrorConfInit(void);
static void AutoLoadMcErrorConf(void);
static void AutoLoadMcDefStartConf(void);
static void AutoLoadMcEmptyErrorInit(void);
static void AutoLoadMcEmptyError(void);
static void AutoLoadMcEmptyWarning(void);

// ══════════════════════════════════════════════════════════════════════
// Phase 14 entry points
// ══════════════════════════════════════════════════════════════════════

// Takes the card (MemoryCardExeInit) and names port 0. The buffer pointer is
// only cleared: FF2 freed a buffer left over here, FF3 frees it in
// AutoLoadMcLoadWait.
// FF3 0x00153d68; FF2 AutoLoadInit 0x0012b728.
void AutoLoadInit(void)
{
    memset(&auto_load_ctrl, 0, sizeof(auto_load_ctrl));
    auto_load_ctrl.step = AL_STEP_START;
    auto_load_ctrl.csr = 1;
    auto_load_ctrl.wait_timer = 0;
    auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
    auto_load_ctrl.conf_timer = 0;
    auto_load_ctrl.msg_id = AL_MSG_NONE;
    auto_load_data_buff = nullptr;
    MemoryCardExeInit();
    MemoryCardSetAccessPort(0);
}

// 1 once the probe is over (step 3), else 0.
// FF3 0x00153dd0; FF2 AutoLoadMain 0x0012b778 (which held the step switch).
int AutoLoadMain(void)
{
    switch (auto_load_ctrl.step)
    {
    case AL_STEP_START:
        auto_load_ctrl.step = AL_STEP_WAIT;
        break;
    case AL_STEP_WAIT:
        auto_load_ctrl.step = AL_STEP_CARD;
        break;
    case AL_STEP_CARD:
        AutoLoadMcMain();
        return 0;
    case AL_STEP_END:
        return 1;
    }
    return 0;
}

// The card steps. 0, 2, 5 and 8 fall through into the step they set, so a
// check, a load or a message starts on the frame it is asked for.
// FF3 0x00153e40.
static void AutoLoadMcMain(void)
{
    switch (auto_load_ctrl.mc_step)
    {
    case AL_STEP_CHECK_INIT:
        AutoLoadMcCheckInit();
        // fall through
    case AL_STEP_CHECK_WAIT:
        AutoLoadMcCheckWait();
        return;
    case AL_STEP_LOAD_INIT:
        AutoLoadMcLoadInit();
        // fall through
    case AL_STEP_LOAD_WAIT:
        AutoLoadMcLoadWait();
        return;
    case AL_STEP_LOAD_CONF:
        AutoLoadMcLoadConf();
        return;
    case AL_STEP_ERROR_INIT:
        AutoLoadMcErrorConfInit();
        // fall through
    case AL_STEP_ERROR_CONF:
        AutoLoadMcErrorConf();
        return;
    case AL_STEP_DEF_START:
        AutoLoadMcDefStartConf();
        return;
    case AL_STEP_EMPTY_INIT:
        AutoLoadMcEmptyErrorInit();
        // fall through
    case AL_STEP_EMPTY_ERROR:
        AutoLoadMcEmptyError();
        return;
    case AL_STEP_EMPTY_ASK:
        AutoLoadMcEmptyWarning();
        return;
    }
}

// ══════════════════════════════════════════════════════════════════════
// Check and load
// ══════════════════════════════════════════════════════════════════════

// Lists the game directory ("/BESLES-53825ZERO-BD/*") of port 0.
// FF3 0x00153ef8; FF2 AutoLoadMcCheckInit 0x0012b870.
static void AutoLoadMcCheckInit(void)
{
    char path_name[64];

    memset(path_name, 0, 55);
    MemoryCardMakeSearchDirPath(path_name, 0);
    MemoryCardCheckInit(0, 0, path_name);
    auto_load_ctrl.mc_step = AL_STEP_CHECK_WAIT;
}

// A whole directory goes on to the load; a damaged one (file count or size
// wrong) or none at all asks whether there is room to write it again.
// FF3 0x00153f48; FF2 AutoLoadMcCheckWait 0x0012b8b8.
static void AutoLoadMcCheckWait(void)
{
    int res = MemoryCardCheckMain();

    if (res == 1)
    {
        if (MemoryCardCheckDirBroken(0) != 0)
        {
            auto_load_ctrl.mc_step = AL_STEP_LOAD_INIT;
            auto_load_ctrl.msg_id = AL_MSG_NO_DATA;
            return;
        }
        if (MemoryCardCheckEmptyBroken(0) == 0)
        {
            auto_load_ctrl.mc_step = AL_STEP_EMPTY_INIT;
            auto_load_ctrl.msg_id = AL_MSG_NO_ROOM;
            return;
        }
        auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
        auto_load_ctrl.msg_id = AL_MSG_DIR_BROKEN;
        return;
    }
    if (res >= 0)
        return;

    switch (res)
    {
    case -0x14:                             // not a PS2 card
        auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
        auto_load_ctrl.msg_id = AL_MSG_NO_BACKDROP;
        break;
    case -6:                                // the directory is not empty but unreadable
        if (MemoryCardCheckEmptyBroken(0) == 0)
        {
            auto_load_ctrl.mc_step = AL_STEP_EMPTY_INIT;
            auto_load_ctrl.msg_id = AL_MSG_NO_ROOM;
            return;
        }
        auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
        auto_load_ctrl.msg_id = AL_MSG_DIR_BROKEN;
        return;
    case -4:                                // no game directory
        if (MemoryCardCheckEmpty(0) == 0)
        {
            auto_load_ctrl.mc_step = AL_STEP_EMPTY_INIT;
            auto_load_ctrl.msg_id = AL_MSG_NO_ROOM;
            return;
        }
#ifdef REIPAN_SLPS_25544_CHS
        // The host's empty virtual card is the normal first launch.
        auto_load_ctrl.step = AL_STEP_END;
        return;
#endif
        // fall through
    case -2:                                // unformatted
        auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
        auto_load_ctrl.msg_id = AL_MSG_NO_DATA;
        break;
    case -1:                                // the card was swapped: load anyway
        auto_load_ctrl.mc_step = AL_STEP_LOAD_INIT;
        auto_load_ctrl.msg_id = AL_MSG_NO_DATA;
        break;
    default:
        auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
        auto_load_ctrl.msg_id = AL_MSG_NO_CARD;
        break;
    }
}

// Reads the system file ("Zero3System") into a system-heap buffer.
// FF3 0x00154060; FF2 AutoLoadMcLoadInit 0x0012b9c0.
static void AutoLoadMcLoadInit(void)
{
    char path_name[64];
    int size;

    memset(path_name, 0, 55);
    MemoryCardSetFilePath(path_name, 0, 0);
    size = GetMemoryCardDataSize(0, 0);
    auto_load_data_buff = GetDataMemoryArea(size);
    MemoryCardFileLoadInit(0, 0, path_name, auto_load_data_buff, size);
    auto_load_ctrl.mc_step = AL_STEP_LOAD_WAIT;
    auto_load_ctrl.msg_id = AL_MSG_LOADING;
}

// A file with the right checksum that is not a blank one is spread into the
// game (DevelopMemoryCardLoadData) and the probe is over; a damaged file asks
// for room like a damaged directory. The buffer is freed once the load ends,
// either way.
// FF3 0x001540e8; FF2 AutoLoadMcLoadWait 0x0012ba48.
static void AutoLoadMcLoadWait(void)
{
    int res = MemoryCardFileLoadMain();
    int size;
    int ok;

    if (res == 1)
    {
        size = GetMemoryCardDataSize(0, 0);
        if (MemoryCardCheckFileBroken(auto_load_data_buff, size) != 0)
        {
            if (MemoryCardCheckNewFileLoad(auto_load_data_buff, size) == 0)
            {
                DevelopMemoryCardLoadData(auto_load_data_buff, 0, 0);
                auto_load_ctrl.step = AL_STEP_END;
            }
            else
            {
                auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
                auto_load_ctrl.msg_id = AL_MSG_FILE_BROKEN;
            }
        }
        else
        {
            ok = MemoryCardCheckEmptyBroken(0);
            if (ok == 0)
            {
                auto_load_ctrl.mc_step = AL_STEP_EMPTY_INIT;
                auto_load_ctrl.msg_id = AL_MSG_NO_ROOM;
            }
            else
            {
                auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
                auto_load_ctrl.msg_id = AL_MSG_DIR_BROKEN;
            }
        }
    }
    else if (res < 0)
    {
        switch (res)
        {
        case -3:                            // short read
            ok = MemoryCardCheckEmptyBroken(0);
            if (ok == 0)
            {
                auto_load_ctrl.mc_step = AL_STEP_EMPTY_INIT;
                auto_load_ctrl.msg_id = AL_MSG_NO_ROOM;
            }
            else
            {
                auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
                auto_load_ctrl.msg_id = AL_MSG_DIR_BROKEN;
            }
            break;
        case -2:
            auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
            auto_load_ctrl.msg_id = AL_MSG_NO_DATA;
            break;
        default:
            auto_load_ctrl.mc_step = AL_STEP_ERROR_INIT;
            auto_load_ctrl.msg_id = AL_MSG_LOAD_ERROR;
            break;
        }
    }

    if (res != 0)
        LiberateDataMemoryArea(&auto_load_data_buff);
}

// FF2 AutoLoadMcLoadConf 0x0012bae0 waited for OK only.
// FF3 0x00154240.
static void AutoLoadMcLoadConf(void)
{
    if (AutoLoadScrMsgIsOpen() == 0)
        return;

    ReiPan_MenuMouseConfirm();          // PC: a click acknowledges
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], AL_SE_OK, 0) != 0)
        auto_load_ctrl.step = AL_STEP_END;
    if (++auto_load_ctrl.conf_timer > AL_LOAD_CONF_TIME)
        auto_load_ctrl.step = AL_STEP_END;
}

// ══════════════════════════════════════════════════════════════════════
// Messages
// ══════════════════════════════════════════════════════════════════════

// Arms the card watch and the minimum display time before OK is read: none
// for "no saved data" (shown as 0x36 from here on) and "not a PS2 card",
// 3 s for the rest.
// FF3 0x001542c0.
static void AutoLoadMcMsgWaitInit(void)
{
    MemoryCardCheckEveryFrameInit(0, 0);

    switch (auto_load_ctrl.msg_id)
    {
    case AL_MSG_NONE:
    case AL_MSG_NO_DATA_WAIT:
    case AL_MSG_NO_BACKDROP:
        auto_load_ctrl.wait_timer = 0;
        break;
    case AL_MSG_NO_DATA:
        auto_load_ctrl.msg_id = AL_MSG_NO_DATA_WAIT;
        auto_load_ctrl.wait_timer = 0;
        break;
    default:
        auto_load_ctrl.wait_timer = AL_MSG_WAIT_PAL;
        if (GetPALMode() == 0)
            auto_load_ctrl.wait_timer = AL_MSG_WAIT_NTSC;
        break;
    }
}

// Polls the card while a message waits for OK. A swapped card restarts the
// check; an unformatted one turns the message into 0x36, an unusable one
// into 0x38 once the minimum display time is over -- both in the error step.
// FF3 0x00154358.
static void AutoLoadMcCardWatch(void)
{
    int res;

    if (auto_load_ctrl.wait_timer > 0)
        auto_load_ctrl.wait_timer--;

    res = MemoryCardCheckEveryFrameMain();
    if (res == -1)
    {
        auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
        auto_load_ctrl.msg_id = AL_MSG_NONE;
        return;
    }
    if (res != 1)
    {
        if (res == -2)
        {
            auto_load_ctrl.msg_id = AL_MSG_NO_DATA_WAIT;
            auto_load_ctrl.mc_step = AL_STEP_ERROR_CONF;
        }
        else if (res == -0x14)
        {
            if (auto_load_ctrl.wait_timer != 0)
                return;
            auto_load_ctrl.msg_id = AL_MSG_NO_BACKDROP;
            auto_load_ctrl.mc_step = AL_STEP_ERROR_CONF;
        }
        else if (res >= 0)
        {
            return;
        }
    }
    MemoryCardCheckEveryFrameInit(0, 0);
}

// FF3 0x00154418.
static void AutoLoadMcErrorConfInit(void)
{
    AutoLoadMcMsgWaitInit();
    auto_load_ctrl.mc_step = AL_STEP_ERROR_CONF;
}

// OK on the open window asks whether to start without saving (cursor on No).
// FF3 0x00154440; FF2 AutoLoadMcErrorConf 0x0012bbf8.
static void AutoLoadMcErrorConf(void)
{
    if (AutoLoadScrMsgIsOpen() != 0)
        ReiPan_MenuMouseConfirm();      // PC: a click acknowledges
    if (AutoLoadScrMsgIsOpen() != 0
        && CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], AL_SE_OK, 0) != 0)
    {
        auto_load_ctrl.csr = 1;
        auto_load_ctrl.mc_step = AL_STEP_DEF_START;
        auto_load_ctrl.msg_id = AL_MSG_DEF_START;
        return;
    }
    AutoLoadMcCardWatch();
}

// "Start without saving?": left or right flips the cursor; OK on Yes ends
// the probe (the game starts without the system file), OK on No or cancel
// checks the card again.
// FF3 0x001544a8; FF2 AutoLoadMcDefStartConf 0x0012bc40.
static void AutoLoadMcDefStartConf(void)
{
    if (AutoLoadScrMsgIsOpen() == 0)
        return;

    if (CheckPadRptPlaySe(AL_PAD_LEFT, AL_SE_CSR, 0) != 0
        || CheckPadRptPlaySe(AL_PAD_RIGHT, AL_SE_CSR, 0) != 0)
    {
        auto_load_ctrl.csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], AL_SE_OK, 0) != 0)
    {
        if (auto_load_ctrl.csr == 0)
        {
            auto_load_ctrl.step = AL_STEP_END;
        }
        else
        {
            auto_load_ctrl.msg_id = AL_MSG_NONE;
            auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], AL_SE_CANCEL, 0) != 0)
    {
        auto_load_ctrl.msg_id = AL_MSG_NONE;
        auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
    }
}

// FF3 0x00154588.
static void AutoLoadMcEmptyErrorInit(void)
{
    AutoLoadMcMsgWaitInit();
    auto_load_ctrl.mc_step = AL_STEP_EMPTY_ERROR;
}

// OK on the open window asks whether to start anyway (cursor on No).
// FF3 0x001545b0; FF2 AutoLoadMcEmptyError 0x0012bdb0.
static void AutoLoadMcEmptyError(void)
{
    if (AutoLoadScrMsgIsOpen() != 0)
        ReiPan_MenuMouseConfirm();      // PC: a click acknowledges
    if (AutoLoadScrMsgIsOpen() != 0
        && CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], AL_SE_OK, 0) != 0)
    {
        auto_load_ctrl.csr = 1;
        auto_load_ctrl.mc_step = AL_STEP_EMPTY_ASK;
        auto_load_ctrl.msg_id = AL_MSG_EMPTY_ASK;
        return;
    }
    AutoLoadMcCardWatch();
}

// No room for the save directory, "start anyway?": the same yes / no as
// AutoLoadMcDefStartConf (FF3 compiled the code twice).
// FF3 0x00154618; FF2 AutoLoadMcEmptyWarning 0x0012bdf8.
static void AutoLoadMcEmptyWarning(void)
{
    if (AutoLoadScrMsgIsOpen() == 0)
        return;

    if (CheckPadRptPlaySe(AL_PAD_LEFT, AL_SE_CSR, 0) != 0
        || CheckPadRptPlaySe(AL_PAD_RIGHT, AL_SE_CSR, 0) != 0)
    {
        auto_load_ctrl.csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], AL_SE_OK, 0) != 0)
    {
        if (auto_load_ctrl.csr == 0)
        {
            auto_load_ctrl.step = AL_STEP_END;
        }
        else
        {
            auto_load_ctrl.msg_id = AL_MSG_NONE;
            auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], AL_SE_CANCEL, 0) != 0)
    {
        auto_load_ctrl.msg_id = AL_MSG_NONE;
        auto_load_ctrl.mc_step = AL_STEP_CHECK_INIT;
    }
}

// ══════════════════════════════════════════════════════════════════════
// end_AutoLoad_Main
// ══════════════════════════════════════════════════════════════════════

// Gives the card back.
// FF3 0x001546f8; FF2 end_AutoLoad_Main called MemoryCardEnd itself.
void AutoLoadMcEnd(void)
{
    MemoryCardEnd();
}
