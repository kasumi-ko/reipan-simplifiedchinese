/* ==========================================================================
 *  album/prg/album_save.c
 *
 *  The album's memory-card save screen (see album_save.h). FF3 0x126f68..
 *  0x128a58, ported from MioPan's FF2 album_save.c and checked function by
 *  function against FF3:
 *
 *   - The machine has 31 card steps, FF2's 29 plus two: once the five album
 *     directories are listed (steps 2/3), steps 4/5 load the first file of
 *     each card album and keep its slot info (AlbumSlotInfoSetCur) for the
 *     list the select page shows. FF2's album select did not read the card.
 *   - A save writes two files (labels 0 and 1, sdata 0x36b930), FF2's one.
 *   - No drawing here and no textures to wait for (AlbumSaveTexLoadWait
 *     returns 1): FF3's page is album/prg/album_save_scr.c, which reads
 *     album_save_ctrl; the pads wait for its windows and list fade through
 *     three gates there (AlbumSaveScr*IsOpen / ListIsIn).
 *   - The message id lives in the work (+0xc), FF2's ALBUM_SAVE_DISP is gone;
 *     AlbumSaveOutReq is inlined (AlbumSaveSlotSelPad, AlbumSaveMcEndConf:
 *     both pass FF2's line 316 to AlbumMemFree).
 *   - The error page holds a missing-card report for 75 (PAL) / 90 frames
 *     (+0x1c) and there is one every-frame card check (FF2 had a second one
 *     for the album select page).
 *   - Cursor and confirm checks go through CheckPad*PlaySe; the mix of Rpt /
 *     Trg calls below is FF3's (the save confirm's cancel uses Rpt, the
 *     remake confirm's RIGHT uses Trg).
 * ======================================================================== */
#include "album/prg/album_save.h"

#include <string.h>                         // memset

#include "album/prg/album_mem.h"            // AlbumMemInit / Main / Free, GetAlbumMemAddr
#include "album/prg/album_save_scr.h"       // AlbumSaveScrSlotSelIsOpen, AlbumSaveScrListIsIn, AlbumSaveScrMsgIsOpen
#include "album/prg/album.h"                 // AlbumSlotInfo*, AlbumCurInfoStamp
#include "graphics/motion/anicode.h"         // SlotSelMouse
#include "system/mc/prg/mc.h"               // MemoryCardExeInit, MemoryCardEnd
#include "system/mc/prg/mc_check.h"
#include "system/mc/prg/mc_check_broken.h"
#include "system/mc/prg/mc_check_card.h"
#include "system/mc/prg/mc_check_dir.h"
#include "system/mc/prg/mc_check_empty.h"
#include "system/mc/prg/mc_del_dir.h"
#include "system/mc/prg/mc_format.h"
#include "system/mc/prg/mc_load.h"
#include "system/mc/prg/mc_make.h"
#include "system/mc/prg/mc_save.h"
#include "system/mc/prg/mc_set_data.h"
#include "system/os/system.h"               // GetPALMode
#include "system/pad/pad_ctrl.h"            // CheckPadTrgPlaySe, CheckPadRptPlaySe, pad_key
#include "ingame/sound/snd_ctrl.h"      // PC: SndSePlay
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────

#define ALBUM_SAVE_SLOT_MAX         2           // MEMORY CARD ports
#define ALBUM_SAVE_FILE_NUM         2           // files one save writes (album_save_file_label)
#define ALBUM_SAVE_MEM_SIZE         0x100000    // staging buffer (AlbumMemInit), also the new-make work
#define ALBUM_SAVE_PATH_NAME_LEN    55          // every caller memsets 0x37 bytes of a 64-byte buffer
#define ALBUM_SAVE_FORMAT_END_TIME  60          // frames "format successful" is held

#define ALBUM_SAVE_SE_CSR           0x1a9b0000  // SE_BANK_DEFAULT cursor
#define ALBUM_SAVE_SE_CANCEL        0x1a9b0001
#define ALBUM_SAVE_SE_OK            0x1a9b0003

#define ALBUM_SAVE_PAD_UP           0x1000
#define ALBUM_SAVE_PAD_RIGHT        0x2000
#define ALBUM_SAVE_PAD_DOWN         0x4000
#define ALBUM_SAVE_PAD_LEFT         0x8000

// album_save_ctrl.msg_id. The ids are FF2's (bank 0x50 there); the texts are
// FF2's English ones and are not checked against FF3's message file yet.
enum ALBUM_SAVE_MSG
{
    ALBUM_SAVE_MSG_NONE             = 0x00,     // "Checking memory card..."
    ALBUM_SAVE_MSG_NO_CARD          = 0x01,     // "No memory card in slot n"
    ALBUM_SAVE_MSG_READ_FAILED      = 0x02,     // "Failed to read memory card"
    ALBUM_SAVE_MSG_UNFORMATTED_CONF = 0x03,     // "...unformatted. Format it?"
    ALBUM_SAVE_MSG_FORMATTING       = 0x04,     // "Formatting memory card."
    ALBUM_SAVE_MSG_FORMAT_FAILED    = 0x05,     // "Format failed!"
    ALBUM_SAVE_MSG_WHICH_SLOT       = 0x06,     // "Which MEMORY CARD slot..."
    ALBUM_SAVE_MSG_SAVE_FAILED      = 0x07,     // "Save failed!"
    ALBUM_SAVE_MSG_SAVE_OK          = 0x08,     // "Save completed."
    ALBUM_SAVE_MSG_OVERWRITE_CONF   = 0x0b,     // "OK to overwrite?"
    ALBUM_SAVE_MSG_UNFORMATTED      = 0x0d,     // "Memory card is unformatted."
    ALBUM_SAVE_MSG_NEW_MAKE_CONF    = 0x1a,     // "Album Data will be created."
    ALBUM_SAVE_MSG_NO_SPACE         = 0x26,     // "Insufficient free space..."
    ALBUM_SAVE_MSG_SEL_ALBUM        = 0x27,     // "Select Album to save to."
    ALBUM_SAVE_MSG_REMAKE_CONF      = 0x28,     // "...corrupt. Create new file?"
    ALBUM_SAVE_MSG_SAVING           = 0x29,     // "Saving album data."
    ALBUM_SAVE_MSG_LOAD_UNFORMATTED = 0x2d,     // FF3-only: an album file read answered -2
    ALBUM_SAVE_MSG_MAKE_FAILED      = 0x2f,     // "Save failed!" (the make copy)
    ALBUM_SAVE_MSG_CREATING         = 0x30,     // "Creating album data."
    ALBUM_SAVE_MSG_FORMAT_OK        = 0x39,     // "Format Successful."
};

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

ALBUM_SAVE_CTRL album_save_ctrl;                                        // bss 0x2f1590

static void *album_save_buff_addr;                                      // sdata 0x36b928
static const int album_save_file_label[ALBUM_SAVE_FILE_NUM] = { 0, 1 }; // sdata 0x36b930

static int  AlbumSaveTexLoadWait(void);
static void AlbumSaveModeMain(void);
static void AlbumSaveSlotSelPad(void);
static void AlbumSaveMcSave(void);
static void AlbumSaveMcCardCheckInit(void);
static void AlbumSaveMcCardCheckWait(void);
static void AlbumSaveMcGetDirInfoInit(void);
static void AlbumSaveMcGetDirInfoWait(void);
static void AlbumSaveMcAlbumLoadInit(void);
static void AlbumSaveMcAlbumLoadWait(void);
static void AlbumSaveMcSaveAlbumSelInit(void);
static void AlbumSaveMcSaveAlbumSelWait(void);
static void AlbumSaveMcSaveAlbumSelPad(void);
static void AlbumSaveMcCheckAgainInit(void);
static void AlbumSaveMcCheckAgainWait(void);
static void AlbumSaveMcSaveConfInit(void);
static void AlbumSaveMcSaveConfWait(void);
static void AlbumSaveMcSaveConfPad(void);
static void AlbumSaveMcSaveInit(void);
static void AlbumSaveMcSaveWait(void);
static void AlbumSaveMcErrorConfInit(void);
static void AlbumSaveMcErrorConfWait(void);
static int  AlbumSaveMcErrorConfPad(void);
static void AlbumSaveMcEndConf(void);
static void AlbumSaveMcRemakeConfInit(void);
static void AlbumSaveMcRemakeConfWait(void);
static void AlbumSaveMcRemakeConfPad(void);
static void AlbumSaveMcRemakeDirDelInit(void);
static void AlbumSaveMcRemakeDirDelWait(void);
static void AlbumSaveMcNewMakeConfInit(void);
static void AlbumSaveMcNewMakeConfWait(void);
static void AlbumSaveMcNewMakeConfPad(void);
static void AlbumSaveMcNewMakeInit(void);
static void AlbumSaveMcNewMakeWait(void);
static void AlbumSaveMcFormatConfInit(void);
static void AlbumSaveMcFormatConfWait(void);
static void AlbumSaveMcFormatConfPad(void);
static void AlbumSaveMcFormatInit(void);
static void AlbumSaveMcFormatWait(void);
static void AlbumSaveMcFormatEndInit(void);
static void AlbumSaveMcFormatEndWait(void);
static void AlbumSaveMcEveryFrameCheck(void);
static void AlbumSaveDispInit(void);

// ──────────────────────────────────────────────────────────────────────
// Set-up
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00126f68
void AlbumSaveCtrlInit(void)
{
    int i;

    memset(&album_save_ctrl, 0, sizeof(album_save_ctrl));

    album_save_ctrl.step           = ALBUM_SAVE_DISP_INIT;
    album_save_ctrl.conf_csr       = 1;             // default "no"
    album_save_ctrl.mc_step        = ALBUM_SAVE_MC_CARD_CHECK_INIT;
    album_save_ctrl.mode           = ALBUM_SAVE_MODE_SLOT_SEL;
    album_save_ctrl.dir_check_cnt  = 0;
    album_save_ctrl.slot_csr       = 0;
    album_save_ctrl.album_sel_csr  = 0;
    album_save_ctrl.save_file_cnt  = 0;
    album_save_ctrl.format_end_cnt = 0;
    album_save_ctrl.err_wait       = 0;

    for (i = 0; i < ALBUM_SAVE_ALBUM_MAX; i++)
    {
        album_save_ctrl.album_flg[i] = 0;
    }

    album_save_ctrl.save_done = 0;

    MemoryCardExeInit();
}

// FF3 0x00127000: FF2 waited for the screen's two paks here.
static int AlbumSaveTexLoadWait(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The screen
// ──────────────────────────────────────────────────────────────────────

// FF2's AlbumSaveOutReq, inlined by FF3 into its two callers (both pass
// line 316): close the page and give the staging buffer back.
static inline void AlbumSaveOutReq(void)
{
    album_save_ctrl.step = ALBUM_SAVE_OUT;

    AlbumMemFree("album_save.c", 316);
}

// FF3 0x00127008
int AlbumSaveMain(void)
{
    switch (album_save_ctrl.step)
    {
    case ALBUM_SAVE_DISP_INIT:
        AlbumSaveDispInit();
        AlbumMemInit(ALBUM_SAVE_MEM_SIZE, "album_save.c", 336);
        album_save_ctrl.step = ALBUM_SAVE_LOAD_WAIT;
        break;

    case ALBUM_SAVE_LOAD_WAIT:
        if (AlbumSaveTexLoadWait() == 0)
        {
            return 0;
        }
        if (AlbumMemMain() != 0)
        {
            album_save_ctrl.step = ALBUM_SAVE_MC_EXE;
        }
        break;

    case ALBUM_SAVE_MC_EXE:
        AlbumSaveModeMain();
        break;

    case ALBUM_SAVE_OUT:
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

// FF3 0x001270e8 (Ghidra had it as AlbumSaveOutReq, an order match).
static void AlbumSaveModeMain(void)
{
    switch (album_save_ctrl.mode)
    {
    case ALBUM_SAVE_MODE_SLOT_SEL:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
        AlbumSaveSlotSelPad();
        break;

    case ALBUM_SAVE_MODE_MC_SAVE:
        AlbumSaveMcSave();
        break;
    }
}

// FF3 0x00127130: UP / DOWN toggle the slot, OK goes to the card machine,
// CANCEL leaves the page.
static void AlbumSaveSlotSelPad(void)
{
    if (AlbumSaveScrSlotSelIsOpen() == 0)
    {
        return;
    }

    SlotSelMouse(album_save_ctrl.slot_csr);

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_UP, ALBUM_SAVE_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_SAVE_PAD_DOWN, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.slot_csr = (s_char)((album_save_ctrl.slot_csr + 1) % ALBUM_SAVE_SLOT_MAX);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        album_save_ctrl.mode    = ALBUM_SAVE_MODE_MC_SAVE;
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_INIT;

        MemoryCardSetAccessPort(album_save_ctrl.slot_csr);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        AlbumSaveOutReq();
    }
}

// ──────────────────────────────────────────────────────────────────────
// The card machine. Every Init case falls through into its Wait case.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00127240
static void AlbumSaveMcSave(void)
{
    switch (album_save_ctrl.mc_step)
    {
    case ALBUM_SAVE_MC_CARD_CHECK_INIT:
        AlbumSaveMcCardCheckInit();
        // fall through
    case ALBUM_SAVE_MC_CARD_CHECK_WAIT:
        AlbumSaveMcCardCheckWait();
        break;

    case ALBUM_SAVE_MC_GET_DIR_INFO_INIT:
        AlbumSaveMcGetDirInfoInit();
        // fall through
    case ALBUM_SAVE_MC_GET_DIR_INFO_WAIT:
        AlbumSaveMcGetDirInfoWait();
        break;

    case ALBUM_SAVE_MC_ALBUM_LOAD_INIT:
        AlbumSaveMcAlbumLoadInit();
        // fall through
    case ALBUM_SAVE_MC_ALBUM_LOAD_WAIT:
        AlbumSaveMcAlbumLoadWait();
        break;

    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT:
        AlbumSaveMcSaveAlbumSelInit();
        // fall through
    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT:
        AlbumSaveMcSaveAlbumSelWait();
        break;

    case ALBUM_SAVE_MC_CHECK_AGAIN_INIT:
        AlbumSaveMcCheckAgainInit();
        // fall through
    case ALBUM_SAVE_MC_CHECK_AGAIN_WAIT:
        AlbumSaveMcCheckAgainWait();
        break;

    case ALBUM_SAVE_MC_SAVE_CONF_INIT:
        AlbumSaveMcSaveConfInit();
        // fall through
    case ALBUM_SAVE_MC_SAVE_CONF_WAIT:
        AlbumSaveMcSaveConfWait();
        break;

    case ALBUM_SAVE_MC_SAVE_INIT:
        AlbumSaveMcSaveInit();
        // fall through
    case ALBUM_SAVE_MC_SAVE_WAIT:
        AlbumSaveMcSaveWait();
        break;

    case ALBUM_SAVE_MC_ERROR_CONF_INIT:
        AlbumSaveMcErrorConfInit();
        // fall through
    case ALBUM_SAVE_MC_ERROR_CONF_WAIT:
        AlbumSaveMcErrorConfWait();
        break;

    case ALBUM_SAVE_MC_END_CONF:
        AlbumSaveMcEndConf();
        break;

    case ALBUM_SAVE_MC_REMAKE_CONF_INIT:
        AlbumSaveMcRemakeConfInit();
        // fall through
    case ALBUM_SAVE_MC_REMAKE_CONF_WAIT:
        AlbumSaveMcRemakeConfWait();
        break;

    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT:
        AlbumSaveMcRemakeDirDelInit();
        // fall through
    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT:
        AlbumSaveMcRemakeDirDelWait();
        break;

    case ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT:
        AlbumSaveMcNewMakeConfInit();
        // fall through
    case ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT:
        AlbumSaveMcNewMakeConfWait();
        break;

    case ALBUM_SAVE_MC_NEW_MAKE_INIT:
        AlbumSaveMcNewMakeInit();
        // fall through
    case ALBUM_SAVE_MC_NEW_MAKE_WAIT:
        AlbumSaveMcNewMakeWait();
        break;

    case ALBUM_SAVE_MC_FORMAT_CONF_INIT:
        AlbumSaveMcFormatConfInit();
        // fall through
    case ALBUM_SAVE_MC_FORMAT_CONF_WAIT:
        AlbumSaveMcFormatConfWait();
        break;

    case ALBUM_SAVE_MC_FORMAT_INIT:
        AlbumSaveMcFormatInit();
        // fall through
    case ALBUM_SAVE_MC_FORMAT_WAIT:
        AlbumSaveMcFormatWait();
        break;

    case ALBUM_SAVE_MC_FORMAT_END_INIT:
        AlbumSaveMcFormatEndInit();
        // fall through
    case ALBUM_SAVE_MC_FORMAT_END_WAIT:
        AlbumSaveMcFormatEndWait();
        break;
    }
}

// Going to the format prompt (an unformatted card).
static void AlbumSaveMcToFormatConf(void)
{
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_INIT;
    album_save_ctrl.conf_csr = 1;
    album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF;
}

// ---- Steps 0/1: is there a card, and is it formatted ----

// FF3 0x001273c0
static void AlbumSaveMcCardCheckInit(void)
{
    MemoryCardGetCardInfoInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_WAIT;
}

// FF3 0x00127400
static void AlbumSaveMcCardCheckWait(void)
{
    int mc_res = MemoryCardGetCardInfoMain();

    if (mc_res == 1)
    {
        if (GetAccessMemoryCardFormat() != 1)
        {
            AlbumSaveMcToFormatConf();
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
            AlbumSaveMcToFormatConf();
            return;

        case -0x14:
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NO_CARD;
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            return;

        default:
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_READ_FAILED;
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            return;
        }
    }

    album_save_ctrl.mc_step       = ALBUM_SAVE_MC_GET_DIR_INFO_INIT;
    album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_NONE;
    album_save_ctrl.dir_check_cnt = 0;
}

// ---- Steps 2/3: list the five album directories ----

// FF3 0x00127500
static void AlbumSaveMcGetDirInfoInit(void)
{
    char path_name[64];

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);

    MemoryCardMakeSearchDirPath(path_name, album_save_ctrl.dir_check_cnt + 1);

    MemoryCardGetDirInfoInit(GetAccessMemoryCardPort(), GetAccessMemoryCardSlot(), path_name);

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_WAIT;
}

// FF3 0x00127578
static void AlbumSaveMcGetDirInfoWait(void)
{
    int mc_res = MemoryCardGetDirInfoMain();
    s_char *flg = &album_save_ctrl.album_flg[album_save_ctrl.dir_check_cnt];

    if (mc_res == 1)
    {
        *flg = (MemoryCardCheckDirBroken(album_save_ctrl.dir_check_cnt + 1) != 0) ? 1 : -1;
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
            album_save_ctrl.conf_csr = 1;
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_INIT;
            album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF;
            break;

        case -0x14:
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_NO_CARD;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.dir_check_cnt = 0;
            break;

        default:
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_READ_FAILED;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.dir_check_cnt = 0;
            break;
        }
    }

    if (mc_res == 1 || mc_res == -4 || mc_res == -6)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_INIT;
        album_save_ctrl.dir_check_cnt++;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;

        if (album_save_ctrl.dir_check_cnt >= ALBUM_SAVE_ALBUM_MAX)
        {
            album_save_ctrl.dir_check_cnt = 0;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_ALBUM_LOAD_INIT;

            AlbumSlotInfoClearAll();
        }
    }
}

// ---- Steps 4/5 (FF3-only): read each card album's first file for the list ----

// FF3 0x00127710
static void AlbumSaveMcAlbumLoadInit(void)
{
    char path_name[64];
    int  dir_label;
    int  size;

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);

    dir_label = album_save_ctrl.dir_check_cnt + 1;

    MemoryCardSetFilePath(path_name, dir_label, 0);
    size = GetMemoryCardDataSize(dir_label, 0);

    album_save_buff_addr = GetAlbumMemAddr();

    MemoryCardFileLoadInit(album_save_ctrl.slot_csr, 0, path_name, album_save_buff_addr, size);

    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ALBUM_LOAD_WAIT;
}

// FF3 0x001277a0: a good file gives its album's slot info; a broken one, or
// none, marks a directory that listed as valid broken.
static void AlbumSaveMcAlbumLoadWait(void)
{
    int mc_res = MemoryCardFileLoadMain();
    s_char *flg = &album_save_ctrl.album_flg[album_save_ctrl.dir_check_cnt];

    if (mc_res == 1)
    {
        int dir_label = album_save_ctrl.dir_check_cnt + 1;
        int size      = GetMemoryCardDataSize(dir_label, 0);

        if (MemoryCardCheckFileBroken(album_save_buff_addr, size) == 0 ||
            MemoryCardCheckNewFileLoad(album_save_buff_addr, size) != 0)
        {
            if (*flg == 1)
            {
                *flg = -1;
            }
        }
        else if (*flg == 1)
        {
            DevelopMemoryCardLoadData(album_save_buff_addr, dir_label, 0);
            AlbumSlotInfoSetCur(album_save_ctrl.dir_check_cnt);
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
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_LOAD_UNFORMATTED;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.dir_check_cnt = 0;
            break;

        default:
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_READ_FAILED;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.dir_check_cnt = 0;
            break;
        }
    }

    if (mc_res == 1 || mc_res == -3 || mc_res == -4 || mc_res == -5)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ALBUM_LOAD_INIT;
        album_save_ctrl.dir_check_cnt++;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;

        if (album_save_ctrl.dir_check_cnt >= ALBUM_SAVE_ALBUM_MAX)
        {
            // dir_check_cnt stays at 5: the page layer reads it as "list read".
            album_save_ctrl.album_sel_csr = 0;
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_SEL_ALBUM;

            AlbumCurInfoStamp();
        }
    }
}

// ---- Steps 6/7: pick the card album to write ----

// FF3 0x00127990
static void AlbumSaveMcSaveAlbumSelInit(void)
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT;
    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;
}

// FF3 0x001279d0
static void AlbumSaveMcSaveAlbumSelWait(void)
{
    AlbumSaveMcSaveAlbumSelPad();
    AlbumSaveMcEveryFrameCheck();
}

// FF3 0x001279f0: UP / DOWN walk the five albums.
static void AlbumSaveMcSaveAlbumSelPad(void)
{
    if (AlbumSaveScrListIsIn() == 0)
    {
        return;
    }

    // PC: the mouse.
    {
        ReiPan_MenuBox box;
        const int      old_csr = album_save_ctrl.album_sel_csr;

        ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_VERTICAL, nullptr);
        if (AlbumSaveScrMouseBox(&box.x, &box.y, &box.w, &box.h))
        {
            album_save_ctrl.album_sel_csr =
                (s_char)MenuMouseStepList(old_csr, &box, 0.0f, AlbumSaveScrMouseStep(), ALBUM_SAVE_ALBUM_MAX, nullptr);
            if (album_save_ctrl.album_sel_csr != old_csr)
                SndSePlay(ALBUM_SAVE_SE_CSR, 100, 0x1000, 0, nullptr, 0);
        }
    }

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_UP, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.album_sel_csr = (s_char)((album_save_ctrl.album_sel_csr + ALBUM_SAVE_ALBUM_MAX - 1) % ALBUM_SAVE_ALBUM_MAX);
    }
    else if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_DOWN, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.album_sel_csr = (s_char)((album_save_ctrl.album_sel_csr + 1) % ALBUM_SAVE_ALBUM_MAX);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        album_save_ctrl.mc_step  = ALBUM_SAVE_MC_CHECK_AGAIN_INIT;
        album_save_ctrl.conf_csr = 1;
        album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_NONE;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
    }
}

// ---- Steps 8/9: check the chosen directory and pick the way in ----

// FF3 0x00127b00
static void AlbumSaveMcCheckAgainInit(void)
{
    char path_name[64];

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);

    MemoryCardMakeSearchDirPath(path_name, album_save_ctrl.album_sel_csr + 1);

    MemoryCardCheckInit(album_save_ctrl.slot_csr, 0, path_name);

    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_CHECK_AGAIN_WAIT;
}

// FF3 0x00127b60
static void AlbumSaveMcCheckAgainWait(void)
{
    int mc_res = MemoryCardCheckMain();

    if (mc_res == 1)
    {
        if (MemoryCardCheckDirBroken(album_save_ctrl.album_sel_csr + 1) == 0)
        {
            // a broken directory: offer to remake it when the card has room
            if (MemoryCardCheckEmptyBroken(album_save_ctrl.album_sel_csr + 1) != 0)
            {
                album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_INIT;
                album_save_ctrl.conf_csr = 1;
                album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;
            }
            else
            {
                album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
                album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NO_SPACE;
            }
        }
        else if (album_save_ctrl.album_flg[album_save_ctrl.album_sel_csr] == -1)
        {
            // the directory is fine but its album file was not
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_INIT;
            album_save_ctrl.conf_csr = 1;
            album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;
        }
        else
        {
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_SAVE_CONF_INIT;
            album_save_ctrl.conf_csr = 1;
            album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_OVERWRITE_CONF;
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
        if (MemoryCardCheckEmptyBroken(album_save_ctrl.album_sel_csr + 1) != 0)
        {
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_INIT;
            album_save_ctrl.conf_csr = 1;
            album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;
        }
        else
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NO_SPACE;
        }
        return;

    case -4:
        if (MemoryCardCheckEmpty(album_save_ctrl.album_sel_csr + 1) != 0)
        {
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT;
            album_save_ctrl.conf_csr = 1;
            album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_NEW_MAKE_CONF;
        }
        else
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NO_SPACE;
        }
        return;

    case -2:
        AlbumSaveMcToFormatConf();
        return;

    case -0x14:
    case -1:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_NO_CARD;
        break;

    default:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_READ_FAILED;
        break;
    }

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
}

// ---- Steps 10/11: "OK to overwrite?" ----

// FF3 0x00127d20
static void AlbumSaveMcSaveConfInit(void)
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_SAVE_CONF_WAIT;
    album_save_ctrl.conf_csr = 1;
    album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_OVERWRITE_CONF;
}

// FF3 0x00127d68
static void AlbumSaveMcSaveConfWait(void)
{
    AlbumSaveMcSaveConfPad();
    AlbumSaveMcEveryFrameCheck();
}

// FF3 0x00127d88: LEFT / RIGHT toggle yes / no; "no" and CANCEL go back to
// the album list.
static void AlbumSaveMcSaveConfPad(void)
{
    if (AlbumSaveScrMsgIsOpen() == 0)
    {
        return;
    }

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_LEFT, ALBUM_SAVE_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_SAVE_PAD_RIGHT, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.conf_csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;

        if (album_save_ctrl.conf_csr == 0)
        {
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_SAVE_INIT;
            album_save_ctrl.save_file_cnt = 0;
            album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_SAVING;
        }
        else
        {
            album_save_ctrl.msg_id = ALBUM_SAVE_MSG_SEL_ALBUM;
        }
    }
    else if (CheckPadRptPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;
    }
}

// ---- Steps 12/13: write the album, one file per pass ----

// FF3 0x00127e78
static void AlbumSaveMcSaveInit(void)
{
    char path_name[64];
    int  dir_label;
    int  file_label;
    int  size;

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);

    dir_label  = album_save_ctrl.album_sel_csr + 1;
    file_label = album_save_file_label[album_save_ctrl.save_file_cnt];

    MemoryCardSetFilePath(path_name, dir_label, file_label);
    size = GetMemoryCardDataSize(dir_label, file_label);

    album_save_buff_addr = GetAlbumMemAddr();

    SetMemoryCardSaveDataToBuff(album_save_buff_addr, dir_label, file_label);

    MemoryCardFileSaveInit(album_save_ctrl.slot_csr, 0, path_name, album_save_buff_addr, size);

    album_save_ctrl.msg_id    = ALBUM_SAVE_MSG_SAVING;
    album_save_ctrl.save_done = 0;
    album_save_ctrl.save_file_cnt++;
    album_save_ctrl.mc_step   = ALBUM_SAVE_MC_SAVE_WAIT;
}

// FF3 0x00127f70
static void AlbumSaveMcSaveWait(void)
{
    int mc_res = MemoryCardFileSaveMain();
    int sel    = album_save_ctrl.album_sel_csr;

    if (mc_res == 1)
    {
        if (album_save_ctrl.save_file_cnt < ALBUM_SAVE_FILE_NUM)
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_SAVING;
        }
        else
        {
            album_save_ctrl.msg_id         = ALBUM_SAVE_MSG_SAVE_OK;
            album_save_ctrl.mc_step        = ALBUM_SAVE_MC_END_CONF;
            album_save_ctrl.album_flg[sel] = 1;

            AlbumSlotInfoSetCur(sel);

            album_save_ctrl.save_done = 1;
        }
    }
    else if (mc_res < 0)
    {
        album_save_ctrl.msg_id         = ALBUM_SAVE_MSG_SAVE_FAILED;
        album_save_ctrl.album_flg[sel] = -1;
        album_save_ctrl.mc_step        = ALBUM_SAVE_MC_ERROR_CONF_INIT;

        AlbumSlotInfoClear(sel);
    }
}

// ---- Steps 14/15: an error message, held until OK / CANCEL ----

// FF3 0x00128038: the album list is dropped; it is read again from step 0.
static void AlbumSaveMcErrorConfInit(void)
{
    int i;

    album_save_ctrl.dir_check_cnt = 0;

    for (i = 0; i < ALBUM_SAVE_ALBUM_MAX; i++)
    {
        album_save_ctrl.album_flg[i] = 0;
    }

    AlbumSlotInfoClearAll();

    album_save_ctrl.album_sel_csr = 0;

    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_WAIT;

    if (album_save_ctrl.msg_id < ALBUM_SAVE_MSG_READ_FAILED)
    {
        album_save_ctrl.err_wait = 0;
    }
    else
    {
        album_save_ctrl.err_wait = (GetPALMode() != 0) ? 75 : 90;
    }
}

// FF3 0x001280e0: a card pulled or swapped (-1 / -2) restarts the machine;
// a missing card (-0x14) replaces the message only after err_wait frames.
static void AlbumSaveMcErrorConfWait(void)
{
    int mc_res;

    if (AlbumSaveMcErrorConfPad() != 0)
    {
        return;
    }

    if (album_save_ctrl.err_wait > 0)
    {
        album_save_ctrl.err_wait--;
    }

    mc_res = MemoryCardCheckEveryFrameMain();

    if (mc_res == -1 || mc_res == -2)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_NONE;
        return;
    }

    if (mc_res != 1)
    {
        if (mc_res == -0x14)
        {
            if (album_save_ctrl.err_wait != 0)
            {
                return;
            }
            album_save_ctrl.msg_id = ALBUM_SAVE_MSG_NO_CARD;
        }
        else if (mc_res >= 0)
        {
            return;
        }
    }

    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);
}

// FF3 0x00128198: OK or CANCEL go back to the slot select. 1 when pressed.
static int AlbumSaveMcErrorConfPad(void)
{
    ReiPan_MenuMouseConfirm();          // PC: a click acknowledges
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0) ||
        CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
        return 1;
    }

    return 0;
}

// ---- Step 16: "Save completed.", OK or CANCEL leave the page ----

// FF3 0x00128220
static void AlbumSaveMcEndConf(void)
{
    ReiPan_MenuMouseConfirm();          // PC: a click acknowledges
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0) ||
        CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        AlbumSaveOutReq();
    }
}

// ---- Steps 17..20: a broken album directory, deleted and made again ----

// FF3 0x001282b8
static void AlbumSaveMcRemakeConfInit(void)
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.conf_csr = 1;
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_WAIT;
    album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;
}

// FF3 0x00128300
static void AlbumSaveMcRemakeConfWait(void)
{
    AlbumSaveMcRemakeConfPad();
    AlbumSaveMcEveryFrameCheck();
}

// FF3 0x00128320
static void AlbumSaveMcRemakeConfPad(void)
{
    if (AlbumSaveScrMsgIsOpen() == 0)
    {
        return;
    }

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_LEFT, ALBUM_SAVE_SE_CSR, 0) ||
        CheckPadTrgPlaySe(ALBUM_SAVE_PAD_RIGHT, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.conf_csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;

        if (album_save_ctrl.conf_csr == 0)
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
        }
        else
        {
            album_save_ctrl.msg_id = ALBUM_SAVE_MSG_SEL_ALBUM;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;
    }
}

// FF3 0x00128410
static void AlbumSaveMcRemakeDirDelInit(void)
{
    MemoryCardDirDelInit(album_save_ctrl.slot_csr, 0, album_save_ctrl.album_sel_csr + 1);

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT;
    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
}

// FF3 0x00128458
static void AlbumSaveMcRemakeDirDelWait(void)
{
    int mc_res = MemoryCardDirDelMain();

    if (mc_res == 1)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
    }
    else if (mc_res < 0)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
    }
}

// ---- Steps 21..24: no album directory yet, make one ----

// FF3 0x001284b8
static void AlbumSaveMcNewMakeConfInit(void)
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.conf_csr = 1;
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT;
    album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_NEW_MAKE_CONF;
}

// FF3 0x00128500
static void AlbumSaveMcNewMakeConfWait(void)
{
    AlbumSaveMcNewMakeConfPad();
    AlbumSaveMcEveryFrameCheck();
}

// FF3 0x00128520
static void AlbumSaveMcNewMakeConfPad(void)
{
    if (AlbumSaveScrMsgIsOpen() == 0)
    {
        return;
    }

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_LEFT, ALBUM_SAVE_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_SAVE_PAD_RIGHT, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.conf_csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;

        if (album_save_ctrl.conf_csr == 0)
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
        }
        else
        {
            album_save_ctrl.msg_id = ALBUM_SAVE_MSG_SEL_ALBUM;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;
    }
}

// FF3 0x00128610: the staging buffer doubles as the make's work buffer.
static void AlbumSaveMcNewMakeInit(void)
{
    MemoryCardNewMakeInit(album_save_ctrl.slot_csr, 0, album_save_ctrl.album_sel_csr + 1,
                          GetAlbumMemAddr(), ALBUM_SAVE_MEM_SIZE);

    AlbumCurInfoStamp();

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_WAIT;
    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
}

// FF3 0x00128670
static void AlbumSaveMcNewMakeWait(void)
{
    int mc_res = MemoryCardNewMakeMain();

    if (mc_res == 1)
    {
        album_save_ctrl.save_file_cnt = 0;
        album_save_ctrl.mc_step       = ALBUM_SAVE_MC_SAVE_INIT;
        album_save_ctrl.msg_id        = ALBUM_SAVE_MSG_SAVING;
    }
    else if (mc_res < 0)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_MAKE_FAILED;
    }
}

// ---- Steps 25..30: an unformatted card ----

// FF3 0x001286d8
static void AlbumSaveMcFormatConfInit(void)
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF;
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_WAIT;
    album_save_ctrl.conf_csr = 1;
}

// FF3 0x00128720: its own card triage; -2 (the card it is asking about) is
// not an error here.
static void AlbumSaveMcFormatConfWait(void)
{
    int mc_res;

    AlbumSaveMcFormatConfPad();

    mc_res = MemoryCardCheckEveryFrameMain();

    if (mc_res >= 0)
    {
        return;
    }

    switch (mc_res)
    {
    case -2:
        return;

    case -0x14:
    case -1:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_NO_CARD;
        break;

    default:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_READ_FAILED;
        break;
    }

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
}

// FF3 0x001287a8: "no" and CANCEL go back to the slot select.
static void AlbumSaveMcFormatConfPad(void)
{
    if (AlbumSaveScrMsgIsOpen() == 0)
    {
        return;
    }

    if (CheckPadRptPlaySe(ALBUM_SAVE_PAD_LEFT, ALBUM_SAVE_SE_CSR, 0) ||
        CheckPadRptPlaySe(ALBUM_SAVE_PAD_RIGHT, ALBUM_SAVE_SE_CSR, 0))
    {
        album_save_ctrl.conf_csr ^= 1;
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], ALBUM_SAVE_SE_OK, 0))
    {
        if (album_save_ctrl.conf_csr == 0)
        {
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_INIT;
            album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_FORMATTING;
        }
        else
        {
            album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;
            album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
        }
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], ALBUM_SAVE_SE_CANCEL, 0))
    {
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;
    }
}

// FF3 0x00128898
static void AlbumSaveMcFormatInit(void)
{
    MemoryCardFormatInit(album_save_ctrl.slot_csr, 0);

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_WAIT;
    album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_FORMATTING;
}

// FF3 0x001288d8
static void AlbumSaveMcFormatWait(void)
{
    int mc_res = MemoryCardFormatMain();

    if (mc_res == 1)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_END_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_FORMAT_OK;
    }
    else if (mc_res < 0)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_FORMAT_FAILED;
    }
}

// FF3 0x00128938
static void AlbumSaveMcFormatEndInit(void)
{
    album_save_ctrl.mc_step        = ALBUM_SAVE_MC_FORMAT_END_WAIT;
    album_save_ctrl.msg_id         = ALBUM_SAVE_MSG_FORMAT_OK;
    album_save_ctrl.format_end_cnt = 0;
}

// FF3 0x00128958: hold "Format Successful." for two seconds, then make the
// album directory.
static void AlbumSaveMcFormatEndWait(void)
{
    album_save_ctrl.format_end_cnt++;

    if (album_save_ctrl.format_end_cnt >= ALBUM_SAVE_FORMAT_END_TIME)
    {
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;
        album_save_ctrl.msg_id  = ALBUM_SAVE_MSG_CREATING;
    }
}

// FF3 0x00128998: the card watch of the pages that wait for input.
static void AlbumSaveMcEveryFrameCheck(void)
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
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_NO_CARD;
        break;

    case -2:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_UNFORMATTED;
        break;

    default:
        album_save_ctrl.msg_id = ALBUM_SAVE_MSG_READ_FAILED;
        break;
    }

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;
}

// ──────────────────────────────────────────────────────────────────────
// End
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00128a30
void AlbumSaveEnd(void)
{
    MemoryCardEnd();
}

// FF3 0x00128a48: FF2 reset its display state here; FF3 only the message.
static void AlbumSaveDispInit(void)
{
    album_save_ctrl.msg_id = ALBUM_SAVE_MSG_WHICH_SLOT;
}
