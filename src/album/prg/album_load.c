/* ==========================================================================
 *  album/prg/album_load.c
 *
 *  The album's memory-card load screen (see album_load.h). FF3 0x125318..
 *  0x126738, ported from MioPan's FF2 album_load.c and checked function by
 *  function against FF3; the save twin album_save.c has the same shape:
 *
 *   - 17 card steps: FF2's 15 plus steps 4/5, which load the first file of
 *     each listed card album and keep its slot info (AlbumSlotInfoSetCur)
 *     for the list the page shows. The load itself reads the second file
 *     (label 1) and makes its slot the current album (AlbumCurInfoSetSlot).
 *   - The album cursor starts on, and UP / DOWN skip to, albums the card
 *     has (album_flg != 0); OK on a broken album buzzes and reports it.
 *   - No drawing and no texture wait here: album/prg/album_load_scr.c shows
 *     the page from album_load_ctrl; the pads wait on its windows and list
 *     fade (AlbumLoadScr*IsOpen / ListIsIn).
 *   - The message id lives in the work (+0x8), FF2's ALBUM_LOAD_DISP is gone;
 *     the page closes in two places, both passing line 236 to AlbumMemFree.
 *   - An unformatted card is not offered a format: it reports "no album data"
 *     (0x2d) like an empty card. The error page holds a missing-card report
 *     for 75 (PAL) / 90 frames (+0x18).
 * ======================================================================== */
#include "album/prg/album_load.h"

#include <string.h>                         // memset

#include "album/prg/album.h"                // AlbumSlotInfoClearAll / SetCur, AlbumCurInfoSetSlot
#include "album/prg/album_load_scr.h"       // AlbumLoadScrSlotSelIsOpen, AlbumLoadScrListIsIn, AlbumLoadScrMsgIsOpen
#include "album/prg/album_mem.h"            // AlbumMemInit / Main / Free, GetAlbumMemAddr
#include "graphics/motion/anicode.h"         // SlotSelMouse
#include "ingame/sound/snd_ctrl.h"          // SndSePlay
#include "system/mc/prg/mc.h"               // MemoryCardExeInit, MemoryCardEnd
#include "system/mc/prg/mc_check.h"
#include "system/mc/prg/mc_check_broken.h"
#include "system/mc/prg/mc_check_card.h"
#include "system/mc/prg/mc_check_dir.h"
#include "system/mc/prg/mc_load.h"
#include "system/mc/prg/mc_set_data.h"
#include "system/os/system.h"               // GetPALMode
#include "system/pad/pad_ctrl.h"            // CheckPadTrgPlaySe, CheckPadRptPlaySe, pad_key
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────

#define ALBUM_LOAD_SLOT_MAX         2           // MEMORY CARD ports
#define ALBUM_LOAD_MEM_SIZE         0x100000    // staging buffer (AlbumMemInit)
#define ALBUM_LOAD_PATH_NAME_LEN    55          // every caller memsets 0x37 bytes of a 64-byte buffer
#define ALBUM_LOAD_LIST_FILE        0           // the file steps 4/5 read for the list
#define ALBUM_LOAD_ALBUM_FILE       1           // the file a load reads

#define ALBUM_LOAD_SE_NONE          (-1)        // the album select plays its own cursor SE
#define ALBUM_LOAD_SE_CSR           0x1a9b0000  // SE_BANK_DEFAULT cursor
#define ALBUM_LOAD_SE_CANCEL        0x1a9b0001
#define ALBUM_LOAD_SE_BUZZER        0x1a9b0002
#define ALBUM_LOAD_SE_OK            0x1a9b0003

#define ALBUM_LOAD_PAD_UP           0x1000
#define ALBUM_LOAD_PAD_RIGHT        0x2000
#define ALBUM_LOAD_PAD_DOWN         0x4000
#define ALBUM_LOAD_PAD_LEFT         0x8000

// album_load_ctrl.msg_id. The ids are FF2's (bank 0x50 there, 0x83 in FF3);
// the texts are FF2's English ones, not checked against FF3's message file.
enum ALBUM_LOAD_MSG
{
    ALBUM_LOAD_MSG_NONE          = 0x00,        // "Checking memory card..."
    ALBUM_LOAD_MSG_NO_CARD       = 0x01,        // "No memory card in slot n"
    ALBUM_LOAD_MSG_READ_FAILED   = 0x02,        // "Failed to read memory card"
    ALBUM_LOAD_MSG_WHICH_SLOT    = 0x0c,        // "Which MEMORY CARD slot..."
    ALBUM_LOAD_MSG_LOAD_NOW      = 0x0e,        // "Load now?"
    ALBUM_LOAD_MSG_LOAD_FAILED   = 0x0f,        // "Load failed!"
    ALBUM_LOAD_MSG_LOAD_OK       = 0x10,        // "Load successful."
    ALBUM_LOAD_MSG_SEL_ALBUM     = 0x2a,        // "Select Album to be loaded."
    ALBUM_LOAD_MSG_CARD_ERROR    = 0x2b,        // "...error! Data may be corrupt."
    ALBUM_LOAD_MSG_LOADING       = 0x2c,        // "Loading data."
    ALBUM_LOAD_MSG_NO_ALBUM_DATA = 0x2d,        // "No ... album data present"
};

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

ALBUM_LOAD_CTRL album_load_ctrl;                // bss 0x2f1570

static void *album_load_buff_addr;              // sdata 0x36b924

static void AlbumLoadModeMain(void);
static void AlbumLoadSlotSelPad(void);
static void AlbumLoadMcLoad(void);
static void AlbumLoadMcCardCheckInit(void);
static void AlbumLoadMcCardCheckWait(void);
static void AlbumLoadMcGetDirInfoInit(void);
static void AlbumLoadMcGetDirInfoWait(void);
static void AlbumLoadMcAlbumLoadInit(void);
static void AlbumLoadMcAlbumLoadWait(void);
static void AlbumLoadMcLoadAlbumSelInit(void);
static void AlbumLoadMcLoadAlbumSelWait(void);
static void AlbumLoadMcLoadAlbumSelPad(void);
static void AlbumLoadMcCheckAgainInit(void);
static void AlbumLoadMcCheckAgainWait(void);
static void AlbumLoadMcLoadConfInit(void);
static void AlbumLoadMcLoadConfWait(void);
static void AlbumLoadMcLoadConfPad(void);
static void AlbumLoadMcLoadInit(void);
static void AlbumLoadMcLoadWait(void);
static void AlbumLoadMcErrorConfInit(void);
static void AlbumLoadMcErrorConfWait(void);
static int  AlbumLoadMcErrorConfPad(void);
static void AlbumLoadMcEndConf(void);
static void AlbumLoadMcEveryFrameCheck(void);
static void AlbumLoadDispInit(void);

// ──────────────────────────────────────────────────────────────────────
// Set-up
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00125318: FF2 also pointed the card buffer at the current album here.
void AlbumLoadCtrlInit(void)
{
    int i;

    memset(&album_load_ctrl, 0, sizeof(album_load_ctrl));

    album_load_ctrl.step          = ALBUM_LOAD_DISP_INIT;
    album_load_ctrl.conf_csr      = 1;              // default "no"
    album_load_ctrl.mc_step       = ALBUM_LOAD_MC_CARD_CHECK_INIT;
    album_load_ctrl.mode          = ALBUM_LOAD_MODE_SLOT_SEL;
    album_load_ctrl.dir_check_cnt = 0;
    album_load_ctrl.slot_csr      = 0;
    album_load_ctrl.album_sel_csr = 0;
    album_load_ctrl.load_file_cnt = 0;
    album_load_ctrl.err_wait      = 0;

    for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++)
    {
        album_load_ctrl.album_flg[i] = 0;
    }

    album_load_ctrl.load_done = 0;

    MemoryCardExeInit();
}

// ──────────────────────────────────────────────────────────────────────
// The screen
// ──────────────────────────────────────────────────────────────────────

// FF2's AlbumLoadOutReq, inlined by FF3 into its two callers (both pass
// line 236): close the page and give the staging buffer back.
static inline void AlbumLoadOutReq(void)
{
    album_load_ctrl.step = ALBUM_LOAD_OUT;

    AlbumMemFree("album_load.c", 236);
}

// FF3 0x001253b0
int AlbumLoadMain(void)
{
    switch (album_load_ctrl.step)
    {
    case ALBUM_LOAD_DISP_INIT:
        AlbumLoadDispInit();
        AlbumMemInit(ALBUM_LOAD_MEM_SIZE, "album_load.c", 256);
        album_load_ctrl.step = ALBUM_LOAD_LOAD_WAIT;
        break;

    case ALBUM_LOAD_LOAD_WAIT:
        if (AlbumMemMain() != 0)
        {
            album_load_ctrl.step = ALBUM_LOAD_MC_EXE;
        }
        break;

    case ALBUM_LOAD_MC_EXE:
        AlbumLoadModeMain();
        break;

    case ALBUM_LOAD_OUT:
        // AlbumMemFree cleared the size, so AlbumMemMain allocates nothing
        // and answers 1 from the second frame on.
        if (AlbumMemMain() != 0)
        {
            return 1;
        }
        break;
    }

    return 0;
}

// FF3 0x00125480
static void AlbumLoadModeMain(void)
{
    switch (album_load_ctrl.mode)
    {
    case ALBUM_LOAD_MODE_SLOT_SEL:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_WHICH_SLOT;
        AlbumLoadSlotSelPad();
        break;

    case ALBUM_LOAD_MODE_MC_LOAD:
        AlbumLoadMcLoad();
        break;
    }
}

// FF3 0x001254c8: UP / DOWN toggle the slot, OK goes to the card machine,
// CANCEL leaves the page.
static void AlbumLoadSlotSelPad(void)
{
    if (AlbumLoadScrSlotSelIsOpen() == 0)
    {
        return;
    }

    SlotSelMouse(album_load_ctrl.slot_csr);

    if (CheckPadRptPlaySe(ALBUM_LOAD_PAD_UP, ALBUM_LOAD_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_LOAD_PAD_DOWN, ALBUM_LOAD_SE_CSR, 0))
    {
        album_load_ctrl.slot_csr = (s_char)((album_load_ctrl.slot_csr + 1) % ALBUM_LOAD_SLOT_MAX);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_LOAD_SE_OK, 0))
    {
        album_load_ctrl.mode    = ALBUM_LOAD_MODE_MC_LOAD;
        album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_INIT;

        MemoryCardSetAccessPort(album_load_ctrl.slot_csr);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_LOAD_SE_CANCEL, 0))
    {
        AlbumLoadOutReq();
    }
}

// ──────────────────────────────────────────────────────────────────────
// The card machine. Every Init case falls through into its Wait case.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001255e0
static void AlbumLoadMcLoad(void)
{
    switch (album_load_ctrl.mc_step)
    {
    case ALBUM_LOAD_MC_CARD_CHECK_INIT:
        AlbumLoadMcCardCheckInit();
        // fall through
    case ALBUM_LOAD_MC_CARD_CHECK_WAIT:
        AlbumLoadMcCardCheckWait();
        break;

    case ALBUM_LOAD_MC_GET_DIR_INFO_INIT:
        AlbumLoadMcGetDirInfoInit();
        // fall through
    case ALBUM_LOAD_MC_GET_DIR_INFO_WAIT:
        AlbumLoadMcGetDirInfoWait();
        break;

    case ALBUM_LOAD_MC_ALBUM_LOAD_INIT:
        AlbumLoadMcAlbumLoadInit();
        // fall through
    case ALBUM_LOAD_MC_ALBUM_LOAD_WAIT:
        AlbumLoadMcAlbumLoadWait();
        break;

    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT:
        AlbumLoadMcLoadAlbumSelInit();
        // fall through
    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT:
        AlbumLoadMcLoadAlbumSelWait();
        break;

    case ALBUM_LOAD_MC_CHECK_AGAIN_INIT:
        AlbumLoadMcCheckAgainInit();
        // fall through
    case ALBUM_LOAD_MC_CHECK_AGAIN_WAIT:
        AlbumLoadMcCheckAgainWait();
        break;

    case ALBUM_LOAD_MC_LOAD_CONF_INIT:
        AlbumLoadMcLoadConfInit();
        // fall through
    case ALBUM_LOAD_MC_LOAD_CONF_WAIT:
        AlbumLoadMcLoadConfWait();
        break;

    case ALBUM_LOAD_MC_LOAD_INIT:
        AlbumLoadMcLoadInit();
        // fall through
    case ALBUM_LOAD_MC_LOAD_WAIT:
        AlbumLoadMcLoadWait();
        break;

    case ALBUM_LOAD_MC_ERROR_CONF_INIT:
        AlbumLoadMcErrorConfInit();
        // fall through
    case ALBUM_LOAD_MC_ERROR_CONF_WAIT:
        AlbumLoadMcErrorConfWait();
        break;

    case ALBUM_LOAD_MC_END_CONF:
        AlbumLoadMcEndConf();
        break;
    }
}

// The first album the card has (album_flg != 0), or ALBUM_LOAD_ALBUM_MAX.
// Steps 3 and 5 inline this loop.
static int AlbumLoadFirstCardAlbum(void)
{
    int i;

    for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++)
    {
        if (album_load_ctrl.album_flg[i] != 0)
        {
            break;
        }
    }

    return i;
}

// ---- Steps 0/1: is there a card, and is it formatted ----

// FF3 0x001256d0
static void AlbumLoadMcCardCheckInit(void)
{
    MemoryCardGetCardInfoInit(album_load_ctrl.slot_csr, 0);

    album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_WAIT;
}

// FF3 0x00125710: an unformatted card has no albums to load.
static void AlbumLoadMcCardCheckWait(void)
{
    int mc_res = MemoryCardGetCardInfoMain();

    if (mc_res == 1)
    {
        if (GetAccessMemoryCardFormat() != 1)
        {
            album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            return;
        }
    }
    else
    {
        if (mc_res >= 0)
        {
            return;
        }

        switch (mc_res)
        {
        case -1:
            break;                              // read on, as for a good card

        case -2:
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            return;

        case -0x14:
            album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NO_CARD;
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            return;

        default:
            album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_READ_FAILED;
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            return;
        }
    }

    album_load_ctrl.mc_step       = ALBUM_LOAD_MC_GET_DIR_INFO_INIT;
    album_load_ctrl.dir_check_cnt = 0;
    album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NONE;
}

// ---- Steps 2/3: list the five album directories ----

// FF3 0x001257e8
static void AlbumLoadMcGetDirInfoInit(void)
{
    char path_name[64];

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);

    MemoryCardMakeSearchDirPath(path_name, album_load_ctrl.dir_check_cnt + 1);

    MemoryCardGetDirInfoInit(GetAccessMemoryCardPort(), GetAccessMemoryCardSlot(), path_name);

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_WAIT;
    album_load_ctrl.album_flg[album_load_ctrl.dir_check_cnt] = 0;
}

// FF3 0x00125868: after the fifth, the album cursor goes to the first album
// the card has; a card with none reports "no album data".
static void AlbumLoadMcGetDirInfoWait(void)
{
    int mc_res = MemoryCardGetDirInfoMain();
    s_char *flg = &album_load_ctrl.album_flg[album_load_ctrl.dir_check_cnt];

    if (mc_res == 1)
    {
        *flg = (MemoryCardCheckDirBroken(album_load_ctrl.dir_check_cnt + 1) != 0) ? 1 : -1;
    }
    else if (mc_res < 0)
    {
        switch (mc_res)
        {
        case -6:
            *flg = -1;
            break;

        case -4:
            *flg = 0;
            break;

        case -2:
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.dir_check_cnt = 0;
            break;

        case -0x14:
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NO_CARD;
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.dir_check_cnt = 0;
            break;

        default:
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_READ_FAILED;
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.dir_check_cnt = 0;
            break;
        }
    }

    if (mc_res == 1 || mc_res == -4 || mc_res == -6)
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_INIT;
        album_load_ctrl.dir_check_cnt++;
        album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;

        if (album_load_ctrl.dir_check_cnt >= ALBUM_LOAD_ALBUM_MAX)
        {
            int first = AlbumLoadFirstCardAlbum();

            if (first < ALBUM_LOAD_ALBUM_MAX)
            {
                album_load_ctrl.album_sel_csr = (s_char)first;
                album_load_ctrl.dir_check_cnt = 0;
                album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ALBUM_LOAD_INIT;
                album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NONE;

                AlbumSlotInfoClearAll();
                return;
            }

            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
        }
    }
}

// ---- Steps 4/5 (FF3-only): read each card album's first file for the list ----

// FF3 0x00125a50
static void AlbumLoadMcAlbumLoadInit(void)
{
    char path_name[64];
    int  dir_label;
    int  size;

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);

    dir_label = album_load_ctrl.dir_check_cnt + 1;

    MemoryCardSetFilePath(path_name, dir_label, ALBUM_LOAD_LIST_FILE);
    size = GetMemoryCardDataSize(dir_label, ALBUM_LOAD_LIST_FILE);

    album_load_buff_addr = GetAlbumMemAddr();

    MemoryCardFileLoadInit(album_load_ctrl.slot_csr, 0, path_name, album_load_buff_addr, size);

    album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ALBUM_LOAD_WAIT;
}

// FF3 0x00125ae0: a good file gives its album's slot info; a broken one, or
// none, marks a directory that listed as valid broken.
static void AlbumLoadMcAlbumLoadWait(void)
{
    int mc_res = MemoryCardFileLoadMain();
    s_char *flg = &album_load_ctrl.album_flg[album_load_ctrl.dir_check_cnt];

    if (mc_res == 1)
    {
        int dir_label = album_load_ctrl.dir_check_cnt + 1;
        int size      = GetMemoryCardDataSize(dir_label, ALBUM_LOAD_LIST_FILE);

        if (MemoryCardCheckFileBroken(album_load_buff_addr, size) == 0 ||
            MemoryCardCheckNewFileLoad(album_load_buff_addr, size) != 0)
        {
            if (*flg == 1)
            {
                *flg = -1;
            }
        }
        else if (*flg == 1)
        {
            DevelopMemoryCardLoadData(album_load_buff_addr, dir_label, ALBUM_LOAD_LIST_FILE);
            AlbumSlotInfoSetCur(album_load_ctrl.dir_check_cnt);
        }
    }
    else if (mc_res < 0)
    {
        switch (mc_res)
        {
        case -5:
        case -4:
        case -3:
            if (*flg == 1)
            {
                *flg = -1;
            }
            break;

        case -2:
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.dir_check_cnt = 0;
            break;

        default:
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_READ_FAILED;
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.dir_check_cnt = 0;
            break;
        }
    }

    if (mc_res == 1 || mc_res == -3 || mc_res == -4 || mc_res == -5)
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_ALBUM_LOAD_INIT;
        album_load_ctrl.dir_check_cnt++;
        album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;

        if (album_load_ctrl.dir_check_cnt >= ALBUM_LOAD_ALBUM_MAX)
        {
            int first = AlbumLoadFirstCardAlbum();

            // dir_check_cnt stays at 5: the page layer reads it as "list read".
            if (first < ALBUM_LOAD_ALBUM_MAX)
            {
                album_load_ctrl.album_sel_csr = (s_char)first;
                album_load_ctrl.mc_step       = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT;
                album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_SEL_ALBUM;
                return;
            }

            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_ERROR_CONF_INIT;
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            album_load_ctrl.dir_check_cnt = 0;
        }
    }
}

// ---- Steps 6/7: pick the card album to load ----

// FF3 0x00125d08
static void AlbumLoadMcLoadAlbumSelInit(void)
{
    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT;
    album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;
}

// FF3 0x00125d48
static void AlbumLoadMcLoadAlbumSelWait(void)
{
    AlbumLoadMcLoadAlbumSelPad();
    AlbumLoadMcEveryFrameCheck();
}

// FF3 0x00125d68: UP / DOWN walk to the next album the card has (the cursor
// SE only when it moved); OK on a valid album checks it again, on a broken
// one buzzes and reports it.
// PC: the mouse: the rows that hold an album.
static int AlbumLoadMouseCan(int row)
{
    return album_load_ctrl.album_flg[row] != 0;
}

static void AlbumLoadMcLoadAlbumSelPad(void)
{
    int old_csr = album_load_ctrl.album_sel_csr;
    int i;

    if (AlbumLoadScrListIsIn() == 0)
    {
        return;
    }

    // PC: the mouse: the rows that hold an album.
    {
        ReiPan_MenuBox box;

        ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_VERTICAL, nullptr);
        if (AlbumLoadScrMouseBox(&box.x, &box.y, &box.w, &box.h))
        {
            album_load_ctrl.album_sel_csr =
                (s_char)MenuMouseStepList(old_csr, &box, 0.0f, AlbumLoadScrMouseStep(), ALBUM_LOAD_ALBUM_MAX,
                                          AlbumLoadMouseCan);
        }
    }

    if (CheckPadRptPlaySe(ALBUM_LOAD_PAD_UP, ALBUM_LOAD_SE_NONE, 0))
    {
        for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++)
        {
            album_load_ctrl.album_sel_csr = (s_char)((album_load_ctrl.album_sel_csr + ALBUM_LOAD_ALBUM_MAX - 1) % ALBUM_LOAD_ALBUM_MAX);

            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] != 0)
            {
                break;
            }
        }
    }
    else if (CheckPadRptPlaySe(ALBUM_LOAD_PAD_DOWN, ALBUM_LOAD_SE_NONE, 0))
    {
        for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++)
        {
            album_load_ctrl.album_sel_csr = (s_char)((album_load_ctrl.album_sel_csr + 1) % ALBUM_LOAD_ALBUM_MAX);

            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] != 0)
            {
                break;
            }
        }
    }
    else
    {
        if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_LOAD_SE_NONE, 0))
        {
            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] == 1)
            {
                SndSePlay(ALBUM_LOAD_SE_OK, 100, 0x1000, 0, 0, 0);

                album_load_ctrl.mc_step = ALBUM_LOAD_MC_CHECK_AGAIN_INIT;
                album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
                return;
            }

            SndSePlay(ALBUM_LOAD_SE_BUZZER, 100, 0x1000, 0, 0, 0);

            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] == -1)
            {
                album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
                album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_CARD_ERROR;
            }
        }
        else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_LOAD_SE_CANCEL, 0))
        {
            album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_WHICH_SLOT;
        }
        return;
    }

    if (album_load_ctrl.album_sel_csr != old_csr)
    {
        SndSePlay(ALBUM_LOAD_SE_CSR, 100, 0x1000, 0, 0, 0);
    }
}

// ---- Steps 8/9: check the chosen directory ----

// FF3 0x00125f60
static void AlbumLoadMcCheckAgainInit(void)
{
    char path_name[64];

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);

    MemoryCardMakeSearchDirPath(path_name, album_load_ctrl.album_sel_csr + 1);

    MemoryCardCheckInit(album_load_ctrl.slot_csr, 0, path_name);

    album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_CHECK_AGAIN_WAIT;
}

// FF3 0x00125fc0
static void AlbumLoadMcCheckAgainWait(void)
{
    int mc_res = MemoryCardCheckMain();

    if (mc_res == 1)
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;

        if (MemoryCardCheckDirBroken(album_load_ctrl.album_sel_csr + 1) == 0)
        {
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;
        }
        else
        {
            album_load_ctrl.mc_step  = ALBUM_LOAD_MC_LOAD_CONF_INIT;
            album_load_ctrl.conf_csr = 1;
            album_load_ctrl.msg_id   = ALBUM_LOAD_MSG_LOAD_NOW;
        }
        return;
    }

    if (mc_res >= 0)
    {
        return;
    }

    switch (mc_res)
    {
    case -6:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;
        break;

    case -4:
    case -2:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
        break;

    case -0x14:
    case -1:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_CARD;
        break;

    default:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_READ_FAILED;
        break;
    }

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
}

// ---- Steps 10/11: "Load now?" ----

// FF3 0x001260a8
static void AlbumLoadMcLoadConfInit(void)
{
    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);

    album_load_ctrl.conf_csr = 1;
    album_load_ctrl.mc_step  = ALBUM_LOAD_MC_LOAD_CONF_WAIT;
    album_load_ctrl.msg_id   = ALBUM_LOAD_MSG_LOAD_NOW;
}

// FF3 0x001260f0
static void AlbumLoadMcLoadConfWait(void)
{
    AlbumLoadMcLoadConfPad();
    AlbumLoadMcEveryFrameCheck();
}

// FF3 0x00126110: LEFT / RIGHT toggle yes / no; "no" and CANCEL go back to
// the album list.
static void AlbumLoadMcLoadConfPad(void)
{
    if (AlbumLoadScrMsgIsOpen() == 0)
    {
        return;
    }

    if (CheckPadRptPlaySe(ALBUM_LOAD_PAD_LEFT, ALBUM_LOAD_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_LOAD_PAD_RIGHT, ALBUM_LOAD_SE_CSR, 0))
    {
        album_load_ctrl.conf_csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_LOAD_SE_OK, 0))
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT;

        if (album_load_ctrl.conf_csr == 0)
        {
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_LOAD_INIT;
            album_load_ctrl.load_file_cnt = 0;
            album_load_ctrl.msg_id        = ALBUM_LOAD_MSG_LOADING;
        }
        else
        {
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_SEL_ALBUM;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_LOAD_SE_CANCEL, 0))
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT;
        album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;
    }
}

// ---- Steps 12/13: read the album ----

// FF3 0x00126200
static void AlbumLoadMcLoadInit(void)
{
    char path_name[64];
    int  dir_label;
    int  size;

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);

    dir_label = album_load_ctrl.album_sel_csr + 1;

    MemoryCardSetFilePath(path_name, dir_label, ALBUM_LOAD_ALBUM_FILE);
    size = GetMemoryCardDataSize(dir_label, ALBUM_LOAD_ALBUM_FILE);

    album_load_buff_addr = GetAlbumMemAddr();

    MemoryCardFileLoadInit(album_load_ctrl.slot_csr, 0, path_name, album_load_buff_addr, size);

    album_load_ctrl.load_done = 0;
    album_load_ctrl.msg_id    = ALBUM_LOAD_MSG_LOADING;
    album_load_ctrl.mc_step   = ALBUM_LOAD_MC_LOAD_WAIT;
}

// FF3 0x00126298: a good file becomes the current album.
static void AlbumLoadMcLoadWait(void)
{
    int mc_res = MemoryCardFileLoadMain();

    if (mc_res == 1)
    {
        int dir_label = album_load_ctrl.album_sel_csr + 1;
        int size      = GetMemoryCardDataSize(dir_label, ALBUM_LOAD_ALBUM_FILE);

        if (MemoryCardCheckFileBroken(album_load_buff_addr, size) != 0 &&
            MemoryCardCheckNewFileLoad(album_load_buff_addr, size) == 0)
        {
            DevelopMemoryCardLoadData(album_load_buff_addr, dir_label, ALBUM_LOAD_ALBUM_FILE);

            album_load_ctrl.mc_step   = ALBUM_LOAD_MC_END_CONF;
            album_load_ctrl.msg_id    = ALBUM_LOAD_MSG_LOAD_OK;
            album_load_ctrl.load_done = mc_res;

            AlbumCurInfoSetSlot(album_load_ctrl.album_sel_csr);
            return;
        }

        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;
    }
    else
    {
        if (mc_res >= 0)
        {
            return;
        }

        switch (mc_res)
        {
        case -3:
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;
            break;

        case -2:
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
            break;

        default:
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_LOAD_FAILED;
            break;
        }
    }

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
}

// ---- Steps 14/15: an error message, held until OK / CANCEL ----

// FF3 0x001263e8: the album list is dropped; it is read again from step 0.
static void AlbumLoadMcErrorConfInit(void)
{
    int i;

    album_load_ctrl.dir_check_cnt = 0;

    for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++)
    {
        album_load_ctrl.album_flg[i] = 0;
    }

    AlbumSlotInfoClearAll();

    album_load_ctrl.album_sel_csr = 0;

    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_WAIT;

    if ((album_load_ctrl.msg_id >= 0 && album_load_ctrl.msg_id < ALBUM_LOAD_MSG_READ_FAILED) ||
        album_load_ctrl.msg_id == ALBUM_LOAD_MSG_NO_ALBUM_DATA)
    {
        album_load_ctrl.err_wait = 0;
    }
    else
    {
        album_load_ctrl.err_wait = (GetPALMode() != 0) ? 75 : 90;
    }
}

// FF3 0x001264a8: a card pulled or swapped (-1) restarts the machine; an
// unformatted one (-2) says "no album data"; a missing card (-0x14) replaces
// the message only after err_wait frames.
static void AlbumLoadMcErrorConfWait(void)
{
    int mc_res;

    if (AlbumLoadMcErrorConfPad() != 0)
    {
        return;
    }

    if (album_load_ctrl.err_wait > 0)
    {
        album_load_ctrl.err_wait--;
    }

    mc_res = MemoryCardCheckEveryFrameMain();

    if (mc_res == -1)
    {
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_INIT;
        album_load_ctrl.msg_id  = ALBUM_LOAD_MSG_NONE;
        return;
    }

    if (mc_res != 1)
    {
        if (mc_res == -2)
        {
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
        }
        else if (mc_res == -0x14)
        {
            if (album_load_ctrl.err_wait != 0)
            {
                return;
            }
            album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_CARD;
        }
        else if (mc_res >= 0)
        {
            return;
        }
    }

    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);
}

// FF3 0x00126570: OK or CANCEL go back to the slot select. 1 when pressed.
static int AlbumLoadMcErrorConfPad(void)
{
    ReiPan_MenuMouseConfirm();          // PC: a click acknowledges
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_LOAD_SE_OK, 0) ||
        CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_LOAD_SE_CANCEL, 0))
    {
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_WHICH_SLOT;
        return 1;
    }

    return 0;
}

// ---- Step 16: "Load successful.", OK or CANCEL leave the page ----

// FF3 0x001265f8
static void AlbumLoadMcEndConf(void)
{
    ReiPan_MenuMouseConfirm();          // PC: a click acknowledges
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_LOAD_SE_OK, 0) ||
        CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_LOAD_SE_CANCEL, 0))
    {
        AlbumLoadOutReq();
    }
}

// FF3 0x00126690: the card watch of the pages that wait for input.
static void AlbumLoadMcEveryFrameCheck(void)
{
    int mc_res = MemoryCardCheckEveryFrameMain();

    if (mc_res >= 0)
    {
        return;
    }

    switch (mc_res)
    {
    case -0x14:
    case -1:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_CARD;
        break;

    case -2:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;
        break;

    default:
        album_load_ctrl.msg_id = ALBUM_LOAD_MSG_READ_FAILED;
        break;
    }

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;
}

// ──────────────────────────────────────────────────────────────────────
// End
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00126710
void AlbumLoadEnd(void)
{
    MemoryCardEnd();
}

// FF3 0x00126728: FF2 reset its display state here; FF3 only the message.
static void AlbumLoadDispInit(void)
{
    album_load_ctrl.msg_id = ALBUM_LOAD_MSG_WHICH_SLOT;
}
