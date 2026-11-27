/* ==========================================================================
 *  ingame/menu/start_menu_top.c
 *
 *  The start menu's top, difficulty and costume pages, FF3
 *  0x1aedd0..0x1b0628 (FF3-only; see start_menu_top.h): phases 80, 107,
 *  108 and 118, the SETUP layout's two text hooks and the three cursor
 *  filters.
 *
 *  FF3 reads the singletons through gp-relative cached pointers; nothing
 *  depends on the cache, so calls go to the getters directly.
 * ======================================================================== */
#include "ingame/menu/start_menu_top.h"

#include "common/WindowMsg.h"           // CWindowMsg
#include "graphics/graph2d/message.h"   // SetCurColorRGB
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, GetLabelSuffixType, SetDefaultGsDrawState
#include "ingame/cat/cat_ctrl.h"        // CatGetAcsNum, CatSetAcsNum
#include "ingame/event/prg/ev_main.h"   // EventDataLoad
#include "ingame/ingame.h"              // PHASE12_PARAM, Phase12ParamSet, IngameNewGameReset
#include "ingame/ingame_wrk.h"          // title_start_flg, TitleMenuIsItemOpen, IngameWrkSetDifficulty, PlayTimeReset
#include "ingame/map/FurnSave.h"        // FurnSaveLoad
#include "ingame/menu/menu_csr.h"
#include "ingame/menu/start_menu.h"     // FINDER_*_WRK, START_MENU_CSR_*, start_menu_sel_win
#include "ingame/plyr/player.h"         // PlyrSetHpByDifficulty
#include "ingame/plyr/plyr_sub.h"       // Plyr{Get,Set}{Costume,Acs}
#include "ingame/sound/snd_ctrl.h"      // SndSeReset, SndBgmPlay, SndBgmStop
#include "ingame/story_door.h"          // MapAreaDataLoad
#include "ingame/story_main.h"          // StoryStartSetStep
#include "main/gphase.h"
#include "outgame/option.h"             // CmnFadeLayer*Item, LayoutItemAddPos, PrintMsgPos
#include "system/b2d/B2dCtl.h"
#include "system/b2d/B2dSpr.h"
#include "system/b2d/BpCtl.h"
#include "system/eeiop/cd_file_dat.h"   // SETUP_PHF, BGM_02_MENU2_STR_STR
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage, GetPALMode
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

#define SMT_SE_MOVE         0x1a9b0000
#define SMT_SE_CANCEL       0x1a9b0001
#define SMT_SE_OK           0x1a9b0003

#define SMT_MSG             0x6a    // the pages' messages: 1..7 top descriptions, 8 the title prompt,
                                    // 10..13 difficulties, 15.. costume names
#define SMT_WIN_ITEM        0x40b   // the message window's / fade layer's item

// SETUP layout items.
#define SMT_TOP_CSR_ITEM    0x435   // top page cursor bar, SMT_TOP_ROW_DY apart
#define SMT_TOP_ENTRY1_ITEM 0x467   // entry 1's label, greyed while closed
#define SMT_DIFF_CSR_ITEM   0x431   // difficulty cursor bar, SMT_DIFF_ROW_DY apart
#define SMT_DIFF_TEXT_ITEM  0x40c   // difficulty names (text hook)
#define SMT_COS_CSR_ITEM    0x42f   // costume grid cursor
#define SMT_COS_SEL_ITEM    0x42e   // costume grid frame while choosing (phase 118)
#define SMT_COS_TEXT_ITEM   0x411   // costume names (text hook)

#define SMT_TOP_ROW_DY      0x24
#define SMT_DIFF_ROW_DY     0x20
#define SMT_COS_COL_DX      0xfc    // two columns of four rows: costumes, accessories
#define SMT_COS_ROW_DY      0x22

#define SMT_COS_ROWS        8
#define SMT_COS_SEL_MAX     5       // entries a row can list (0 and up to four bought ones)

static int start_menu_costume_sel[SMT_COS_ROWS];    // bss 0x3e77c0: the entry chosen per row

static int StartMenuSetupHandle(void)
{
    return BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + SETUP_PHF);
}

static int StartMenuSetupIsLoadEnd(void)
{
    return BpCtlIsLoadEnd(GetSysWrk()->GetLanguage() + SETUP_PHF);
}

// ──────────────────────────────────────────────────────────────────────
// Rows 4..7 of the costume page are accessories, rows 0..3 costumes.
// FF3 0x001aedd0

static int StartMenuCostumeIsAcs(int row)
{
    return row > 3;
}

// FF3 0x001aede0
static int StartMenuCostumeGetSel(int row)
{
    return start_menu_costume_sel[row];
}

// FF3 0x001aedf8
static void StartMenuCostumeSetSel(int row, int v)
{
    start_menu_costume_sel[row] = v;
}

// The top cursor's entry description.
// FF3 0x001aee10

static int StartMenuTopGetMsgNo(void)
{
    return MenuCsrGetPos(START_MENU_CSR_TOP) + 1;
}

// FF3 0x001aee30
static int StartMenuDiffGetMsgNo(int diff)
{
    return diff + 10;
}

// ──────────────────────────────────────────────────────────────────────
// The name of a row's chosen entry: ten messages per character, the
// accessories five after the costumes.
// FF3 0x001aee40

static int StartMenuCostumeGetMsgNo(int row)
{
    int no;

    no = (row % 4) * 10 + 0xf + StartMenuCostumeGetSel(row);
    if (StartMenuCostumeIsAcs(row) == 1) {
        no += 5;
    }
    return no;
}

// ──────────────────────────────────────────────────────────────────────
// The top page's sprites: the cursor bar on the cursor's row; entry 1's
// label greyed while it is closed (StartMenuTopCsrFilter).
// FF3 0x001aeea8
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuSetupHandle)

static void StartMenuTopUpdateSpr(void)
{
    B2D_ITEM *item;
    int       handle;
    int       pos;

    handle = StartMenuSetupHandle();
    pos = MenuCsrGetPos(START_MENU_CSR_TOP);
    B2dSprSetPos(B2dCtlFindItem(handle, SMT_TOP_CSR_ITEM), 0, pos * SMT_TOP_ROW_DY);
    B2dSprFreezePart(handle, SMT_TOP_CSR_ITEM, B2D_PART_X, 0);
    B2dSprFreezePart(handle, SMT_TOP_CSR_ITEM, B2D_PART_Y, 0);
    if (GetFinderStateWrk()->ClearSaveFlag() != 0) {
        item = B2dCtlFindItem(handle, SMT_TOP_ENTRY1_ITEM);
        B2dSprFreezePart(handle, SMT_TOP_ENTRY1_ITEM, B2D_PART_RGB, 0);
        B2dSprSetRgb(item, 0x40, 0x40, 0x40);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The difficulty page's sprites: the cursor bar on the cursor's row; four
// items shown by what the clears opened -- all with difficulty 3 open, the
// first two with difficulty 2.
// FF3 0x001aefb8
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuSetupHandle)

static void StartMenuDiffUpdateSpr(void)
{
    static const short items[4] = { 0x433, 0x40e, 0x434, 0x40f };  // sdata 0x370398, copied to the stack
    int handle;
    int pos;
    int open2;
    int open3;
    int i;

    handle = StartMenuSetupHandle();
    pos = MenuCsrGetPos(START_MENU_CSR_DIFF);
    B2dSprSetPos(B2dCtlFindItem(handle, SMT_DIFF_CSR_ITEM), 0, pos * SMT_DIFF_ROW_DY);
    B2dSprFreezePart(handle, SMT_DIFF_CSR_ITEM, B2D_PART_X, 0);
    B2dSprFreezePart(handle, SMT_DIFF_CSR_ITEM, B2D_PART_Y, 0);
    open2 = TitleMenuIsItemOpen(2);
    open3 = TitleMenuIsItemOpen(3);
    for (i = 0; i < 4; i++) {
        B2dSprSetDisp(B2dCtlFindItem(handle, items[i]), open3 != 0 || (open2 != 0 && i < 2));
    }
}

// ──────────────────────────────────────────────────────────────────────
// The costume page's sprites: the cursor on its grid cell, the choosing
// frame hidden.
// FF3 0x001af0f0
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuSetupHandle)

static void StartMenuCostumeUpdateSpr(void)
{
    int handle;
    int pos;

    handle = StartMenuSetupHandle();
    pos = MenuCsrGetPos(START_MENU_CSR_COSTUME);
    B2dSprSetPos(B2dCtlFindItem(handle, SMT_COS_CSR_ITEM),
                 (pos / 4) * SMT_COS_COL_DX, (pos % 4) * SMT_COS_ROW_DY);
    B2dSprFreezePart(handle, SMT_COS_CSR_ITEM, B2D_PART_X, 0);
    B2dSprFreezePart(handle, SMT_COS_CSR_ITEM, B2D_PART_Y, 0);
    B2dSprSetDisp(B2dCtlFindItem(handle, SMT_COS_SEL_ITEM), 0);
}

// The choosing frame on the costume cursor's grid cell (phase 118).
// FF3 0x001af1d0
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuSetupHandle)

static void StartMenuCostumeSelUpdateSpr(void)
{
    B2D_ITEM *item;
    int       handle;
    int       pos;

    handle = StartMenuSetupHandle();
    pos = MenuCsrGetPos(START_MENU_CSR_COSTUME);
    item = B2dCtlFindItem(handle, SMT_COS_SEL_ITEM);
    B2dSprSetDisp(item, 1);
    B2dSprSetPos(item, (pos / 4) * SMT_COS_COL_DX, (pos % 4) * SMT_COS_ROW_DY);
    B2dSprFreezePart(handle, SMT_COS_SEL_ITEM, B2D_PART_X, 0);
    B2dSprFreezePart(handle, SMT_COS_SEL_ITEM, B2D_PART_Y, 0);
}

// Rows 3 and 7 are the cat's (no costume, one accessory).
// FF3 0x001af2b0

static int StartMenuCostumeIsCat(int row)
{
    return row == 3 || row == 7;
}

// ──────────────────────────────────────────────────────────────────────
// The chosen entries onto the characters: rows 0..2 the costumes, 4..6 the
// accessories, 7 the cat's accessory.
// FF3 0x001af2d8

static void StartMenuCostumeApply(void)
{
    s_char v;
    int    row;

    for (row = 0; row < SMT_COS_ROWS; row++) {
        v = (s_char)StartMenuCostumeGetSel(row);
        if (StartMenuCostumeIsAcs(row) == 0) {
            if (StartMenuCostumeIsCat(row) == 0) {
                PlyrSetCostume((s_char)row, (u_char)v);
            }
        } else if (StartMenuCostumeIsCat(row) == 0) {
            PlyrSetAcs((s_char)(row % 4), (u_char)v);
        } else {
            CatSetAcsNum(0, (u_char)v);
        }
    }
}

// The characters' current costumes and accessories into the rows.
// FF3 0x001af3a0

static void StartMenuCostumeFetch(void)
{
    int v;
    int row;

    for (row = 0; row < SMT_COS_ROWS; row++) {
        if (StartMenuCostumeIsAcs(row) == 0) {
            v = 0;
            if (StartMenuCostumeIsCat(row) == 0) {
                v = PlyrGetCostume((s_char)row);
            }
        } else if (StartMenuCostumeIsCat(row) == 0) {
            v = PlyrGetAcs((s_char)(row % 4));
        } else {
            v = CatGetAcsNum(0);
        }
        StartMenuCostumeSetSel(row, v);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x411: every row's chosen entry in its grid cell, the
// cursor's row warm yellow, the others dark grey (row 3 has nothing).
// FF3 0x001af450 (no Ghidra function before 2026-10-21: inside 0x1af3a0)
// callcheck: GetLanguage, GetSysWrk (GetSysWrk()->GetLanguage() + SETUP_PHF)

static int StartMenuCostumeDrawNames(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         csr;
    int         type;
    int         row;

    csr = MenuCsrGetPos(START_MENU_CSR_COSTUME);
    for (row = 0; row < SMT_COS_ROWS; row++) {
        pos[0] = (float)((row / 4) * SMT_COS_COL_DX);
        pos[1] = (float)((row % 4) * SMT_COS_ROW_DY);
        if (row == 3) {
            continue;
        }
        if (row == csr) {
            SetCurColorRGB(0xf1, 0xd6, 0x9b);
        } else {
            SetCurColorRGB(0x2a, 0x2c, 0x3a);
        }
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, GetSysWrk()->GetLanguage() + SETUP_PHF, xy);
        PrintMsgPos(SMT_MSG, StartMenuCostumeGetMsgNo(row), pos, 0x2d, alpha, 0, type);
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x40c: the open difficulties' names, the cursor's one
// light.
// FF3 0x001af5c0 (no Ghidra function before 2026-10-21: inside 0x1af3a0)
// callcheck: GetLanguage, GetSysWrk (GetSysWrk()->GetLanguage() + SETUP_PHF)

static int StartMenuDiffDrawNames(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         csr;
    int         type;
    int         i;

    csr = MenuCsrGetPos(START_MENU_CSR_DIFF);
    for (i = 0; i < 4; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * SMT_DIFF_ROW_DY);
        if (TitleMenuIsItemOpen(i) == 0) {
            continue;
        }
        if (i == csr) {
            SetCurColorRGB(0xda, 0xe4, 0xbe);
        } else {
            SetCurColorRGB(0x8d, 0x97, 0x80);
        }
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, GetSysWrk()->GetLanguage() + SETUP_PHF, xy);
        PrintMsgPos(SMT_MSG, StartMenuDiffGetMsgNo(i), pos, 0x2d, alpha, 0, type);
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC start_menu_setup_hooks[] =       /* rodata 0x358950, copied to the stack */
{
    { StartMenuCostumeDrawNames, 0, SMT_COS_TEXT_ITEM,  nullptr },
    { StartMenuDiffDrawNames,    0, SMT_DIFF_TEXT_ITEM, nullptr },
    { nullptr,                  -1, -1,                 nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001af6f0
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuSetupHandle)

void StartMenuSetupSetHooks(void)
{
    B2dSetHookList(StartMenuSetupHandle(), start_menu_setup_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// Cursor 0x50's items: every entry, entry 1 closed (-1) while the clear
// state's _0c is set.
// FF3 0x001af770

void StartMenuTopCsrFilter(short *tbl, short w, short h)
{
    int i;

    (void)w;
    for (i = 0; i < h; i++, tbl++) {
        if (i == 1) {
            *tbl = GetFinderStateWrk()->ClearSaveFlag() == 0 ? 1 : -1;
        } else {
            *tbl = (short)i;
        }
    }
}

// Cursor 0x6b's items: the difficulties the title menu has open.
// FF3 0x001af808

void StartMenuDiffCsrFilter(short *tbl, short w, short h)
{
    int i;

    (void)w;
    for (i = 0; i < h; i++, tbl++) {
        *tbl = TitleMenuIsItemOpen(i) != 0 ? (short)i : -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Phase 80 init: the clears' unlocks, the menu BGM, the rows read from the
// characters, the layout and the message window in; Story_Start will start
// the game (step 2) and load first (title_start_flg.load). Done once the
// layout's "in" has played.
// FF3 0x001af878
// callcheck: GetFinderStateWrk (FF3 passes the work to FinderSlotOpenByCount / ByMission, which never read it)
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)

int init_StartMenu_Top(void)
{
    int handle;

    handle = StartMenuSetupHandle();
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        FinderSlotOpenByCount();
        FinderSlotOpenByMission();
        SndSeReset();
        SndBgmPlay(BGM_02_MENU2_STR_STR, 100, 0x1000, GetPALMode() != 0 ? 0x19 : 0x1e, nullptr, 1, 0);
        StartMenuCostumeFetch();
        BpPlayLabel(StartMenuSetupHandle(), "in");
        start_menu_sel_win->SetItem(handle, SMT_WIN_ITEM);
        start_menu_sel_win->StartMsg(SMT_MSG, StartMenuTopGetMsgNo(), 1);
        CmnFadeLayerShowItem(0, handle, SMT_WIN_ITEM);
        StoryStartSetStep(2);
        title_start_flg.load = 1;
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuTopUpdateSpr();
    return StartMenuSetupIsLoadEnd() != 0;
}

// FF3 0x001afa50
void pre_StartMenu_Top(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Phase 80 per frame: the cursor's description; OK opens the entry's
// phase, cancel jumps to entry 6, whose phase-12 prompt (message 8) goes
// back to the title menu (17) on yes and returns here otherwise.
// FF3 0x001afa58

int one_StartMenu_Top(void)
{
    static const short next_tbl[7] = { 0x6b, 0x1d, 0x53, 0x6c, 0x6d, 0x52, 0xc };  // built on the stack
    int pos;
    int ret;

    ret = 0;
    MenuMouseCsrBar(MenuCsrGetWork(START_MENU_CSR_TOP), StartMenuSetupHandle(), SMT_TOP_CSR_ITEM, 0.0f,
                    (float)SMT_TOP_ROW_DY);     // PC
    MenuCsrUpdate(START_MENU_CSR_TOP, 0);
    start_menu_sel_win->StartMsg(SMT_MSG, StartMenuTopGetMsgNo(), 1);
    if (MenuCsrIsMovedSe(START_MENU_CSR_TOP, SMT_SE_MOVE) == 0
        && (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SMT_SE_OK, 0) != 0
            || CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SMT_SE_CANCEL, 0) != 0)) {
        if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], -1, 0) != 0) {
            MenuCsrSetPos(START_MENU_CSR_TOP, 6);
        }
        pos = MenuCsrGetPos(START_MENU_CSR_TOP);
        if (pos == 6) {
            // built on the stack; FF3 leaves _0e unset
            PHASE12_PARAM to_title = { SMT_MSG, 8, 1, 0x11, 0x50, 0x50, 0, nullptr, start_menu_sel_win };
            Phase12ParamSet(&to_title);
        }
        ret = 1;
        GetGPhaseCtrl()->SetNext(next_tbl[pos], pos == 6);
    }
    StartMenuTopUpdateSpr();
    return ret;
}

// FF3 0x001afc00
void after_StartMenu_Top(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Phase 80 end: the layout's "out"; the BGM stops unless the bonus shop
// (109) follows. Done once "out" has played: the fade layer item and the
// window's alpha item go.
// FF3 0x001afc08
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)

int end_StartMenu_Top(void)
{
    int end;

    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(StartMenuSetupHandle(), "out");
        if (GetGPhaseCtrl()->now != 0x6d) {
            SndBgmStop(GetPALMode() != 0 ? 0x19 : 0x1e, 0);
        }
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuTopUpdateSpr();
    end = StartMenuSetupIsLoadEnd();
    if (end != 0) {
        CmnFadeLayerTermItem();
        start_menu_sel_win->SetAlphaItem(nullptr);
    }
    return end != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 107 init / end: the layout's "nanido_menu_in" / "_out"; done once
// it has played.
// FF3 0x001afd38
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)

int init_StartMenu_Difficulty(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(StartMenuSetupHandle(), "nanido_menu_in");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuDiffUpdateSpr();
    return StartMenuSetupIsLoadEnd();
}

// ──────────────────────────────────────────────────────────────────────
// Phase 107 per frame. OK starts a new game on the cursor's difficulty:
// the in-game state reset, the area data, furniture and events (story)
// loaded again, the play time cleared, the clear state's _0c cleared, the
// player's HP set; then Story_Start (29). Cancel goes back to the top (80).
// FF3 0x001afe00

int one_StartMenu_Difficulty(void)
{
    int diff;
    int next;

    MenuMouseCsrBar(MenuCsrGetWork(START_MENU_CSR_DIFF), StartMenuSetupHandle(), SMT_DIFF_CSR_ITEM, 0.0f,
                    (float)SMT_DIFF_ROW_DY);    // PC
    MenuCsrUpdate(START_MENU_CSR_DIFF, 0);
    StartMenuDiffUpdateSpr();
    diff = MenuCsrGetPos(START_MENU_CSR_DIFF);
    if (MenuCsrIsMovedSe(START_MENU_CSR_DIFF, SMT_SE_MOVE) != 0) {
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SMT_SE_OK, 0) != 0) {
        title_start_flg.load = 0;
        IngameNewGameReset();
        MapAreaDataLoad();
        FurnSaveLoad();
        EventDataLoad(0);
        IngameWrkSetDifficulty(diff);
        PlayTimeReset();
        GetFinderStateWrk()->SetClearSaveFlag(0);
        PlyrSetHpByDifficulty((u_char)diff);
        next = 0x1d;
    } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SMT_SE_CANCEL, 0) != 0) {
        next = 0x50;
    } else {
        return 0;
    }
    GetGPhaseCtrl()->SetNext(next, 0);
    return 1;
}

// FF3 0x001aff40
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)
int end_StartMenu_Difficulty(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(StartMenuSetupHandle(), "nanido_menu_out");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuDiffUpdateSpr();
    return StartMenuSetupIsLoadEnd();
}

// ──────────────────────────────────────────────────────────────────────
// Phase 108 init / end: the layout's "costume_in" / "costume_out"; done
// once it has played.
// FF3 0x001b0008
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)

int init_StartMenu_Costume(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(StartMenuSetupHandle(), "costume_in");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuCostumeUpdateSpr();
    return StartMenuSetupIsLoadEnd();
}

// FF3 0x001b00d0
void pre_StartMenu_Costume(void)
{
}

// Phase 108 per frame: OK chooses on the cursor's row (118), cancel goes
// back to the top (80).
// FF3 0x001b00d8

int one_StartMenu_Costume(void)
{
    int next;

    // PC: the mouse. The frame stands on the item's cell: four rows in two
    // columns, by item number (StartMenuCostumeUpdateSpr).
    {
        ReiPan_MenuBox at;
        const int      cur = MenuCsrGetPos(START_MENU_CSR_COSTUME);

        ReiPan_MenuMouseBegin(0, nullptr);
        if (ReiPan_MenuMouseItemBox(StartMenuSetupHandle(), SMT_COS_CSR_ITEM, &at))
        {
            for (int i = 0; i < 8; i++)
            {
                ReiPan_MenuBox box = at;

                box.x += (float)((i / 4 - cur / 4) * SMT_COS_COL_DX);
                box.y += (float)((i % 4 - cur % 4) * SMT_COS_ROW_DY);
                MenuMouseCsr(START_MENU_CSR_COSTUME, i, &box);
            }
        }
    }
    MenuCsrUpdate(START_MENU_CSR_COSTUME, 0);
    StartMenuCostumeUpdateSpr();
    if (MenuCsrIsMovedSe(START_MENU_CSR_COSTUME, SMT_SE_MOVE) != 0) {
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SMT_SE_OK, 0) != 0) {
        next = 0x76;
    } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SMT_SE_CANCEL, 0) != 0) {
        next = 0x50;
    } else {
        return 0;
    }
    GetGPhaseCtrl()->SetNext(next, 0);
    return 1;
}

// FF3 0x001b01a8
void after_StartMenu_Costume(void)
{
}

// FF3 0x001b01b0
// callcheck: BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, GetLanguage, GetSysWrk (StartMenuSetupHandle, StartMenuSetupIsLoadEnd)
int end_StartMenu_Costume(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(StartMenuSetupHandle(), "costume_out");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuCostumeUpdateSpr();
    return StartMenuSetupIsLoadEnd();
}

// ──────────────────────────────────────────────────────────────────────
// A row's entries: out[0] = 0 (the default), then n + 1 for each of the
// row's unlock slots that is bought (2). The rows' slots run from
// FINDER_SLOT_WRK slot 0 (costumes, rows 0..2) and slot 10 (accessories,
// rows 4..7). Returns the entry count. FF3 passes a third argument (5,
// the list size) that it never reads, and tests StartMenuCostumeIsAcs once
// more before the loop without using the result.
// FF3 0x001b0278

static int StartMenuCostumeListOwned(int *out, int row)
{
    static const int count_tbl[SMT_COS_ROWS] = { 4, 4, 2, 0, 4, 4, 4, 2 };    // rodata 0x3589d0
    static const int base_tbl[SMT_COS_ROWS]  = { 0, 4, 8, 8, 0, 4, 8, 12 };   // rodata 0x3589f0
    FINDER_SLOT_WRK *slot_wrk = GetFinderSlotWrk();
    int  count;
    int  base;
    int  acs;
    int  n;
    int  v;
    int  i;

    n = 1;
    out[0] = 0;
    count = count_tbl[row];
    base = base_tbl[row];
    if (count > 0) {
        acs = StartMenuCostumeIsAcs(row);
        for (i = 0; i < count; i++) {
            if (acs == 0) {
                v = slot_wrk->slot[base + i];
            } else {
                v = slot_wrk->slot[10 + base + i];
            }
            if (v == 2) {
                out[n++] = i + 1;
            }
        }
    }
    return n;
}

// Cursor 0x76's items: the costume cursor's row's entries, -1 after them.
// FF3 0x001b0420 (no Ghidra function before 2026-10-21: inside 0x1b0278)

static void StartMenuCostumeSelCsrFilter(short *tbl, short w, short h)
{
    int list[8];
    int n;
    int i;

    (void)w;
    (void)h;
    n = StartMenuCostumeListOwned(list, MenuCsrGetPos(START_MENU_CSR_COSTUME));
    for (i = 0; i < n; i++) {
        *tbl++ = (short)list[i];
    }
    for (; n < SMT_COS_SEL_MAX; n++) {
        *tbl++ = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Phase 118 init: the entry cursor (5 x 1) on the row's chosen entry.
// FF3 0x001b04b0

int init_StartMenu_CostumeSel(void)
{
    MenuCsrInitWithFilter(StartMenuCostumeSelCsrFilter, START_MENU_CSR_COSTUME_SEL, SMT_COS_SEL_MAX, 1, 0);
    MenuCsrSetPos(START_MENU_CSR_COSTUME_SEL,
                  (short)StartMenuCostumeGetSel(MenuCsrGetPos(START_MENU_CSR_COSTUME)));
    return 1;
}

// Phase 118 per frame: the entry under the cursor becomes the row's choice
// at once; OK or cancel go back to the rows (108).
// FF3 0x001b0508

int one_StartMenu_CostumeSel(void)
{
    ReiPan_MenuMouseConfirm();          // PC: the wheel picks, a click takes it
    MenuCsrUpdate(START_MENU_CSR_COSTUME_SEL, 0);
    StartMenuCostumeSelUpdateSpr();
    StartMenuCostumeSetSel(MenuCsrGetPos(START_MENU_CSR_COSTUME), MenuCsrGetPos(START_MENU_CSR_COSTUME_SEL));
    if (MenuCsrIsMovedSe(START_MENU_CSR_COSTUME_SEL, SMT_SE_MOVE) != 0) {
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SMT_SE_OK, 0) == 0
        && CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SMT_SE_CANCEL, 0) == 0) {
        return 0;
    }
    GetGPhaseCtrl()->SetNext(0x6c, 0);
    return 1;
}

// Phase 118 end: the rows onto the characters, the entry cursor freed.
// FF3 0x001b05d8

int end_StartMenu_CostumeSel(void)
{
    StartMenuCostumeApply();
    MenuCsrRelease(START_MENU_CSR_COSTUME_SEL);
    return 1;
}

// FF3 0x001b0608 (StartMenuTopAtexitGPhaseCtrl) is the GPhaseCtrl atexit
// thunk that ends this FF3 file (GPHASE_CTRL's destructor here).
