/* ==========================================================================
 *  ingame/menu/menu_file_list.c
 *
 *  Phase 62 StoryMenu_File (the file menu) and phase 87 StoryMenu_FileFirst
 *  (its first-visit explanation); see menu_file_list.h. FF3 0x204810..0x207490.
 *
 *  Two cursors (MENU_CSR_WRK objects of this file): the tab column, and per
 *  tab one for its memo rows; the focus says which one the pad moves. The
 *  tabs hold a MEMO_LIST each (ingame/memo/memo.c). The layout draws the
 *  rest through item hooks: 0x404 the tabs, 0x405 the rows' titles, 0x423
 *  the selected title, 0x406 the "new" stickers, 0x400 the category picture.
 *
 *  FF3 reads GetGPhaseCtrl, GetSysWrk and GetFinderStateWrk through
 *  gp-relative cached pointers; the calls go to the getters directly. Not
 *  written: FF3 0x00207470, the GPhaseCtrl singleton's atexit thunk (as in
 *  every file that includes the getter).
 * ======================================================================== */
#include "ingame/menu/menu_file_list.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, capCtl
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, MsgViewSetMode / SetMsg, SetDefaultGsDrawState
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/memo/memo.h"           // memo_wrk, memo_dat, memo_page, memo_flg, MEMO_LIST, MemoGetPageBreakNum
#include "ingame/menu/anim_2d.h"        // LayoutAnim*
#include "ingame/menu/menu_bg.h"        // MenuLayoutIsPlaying, MenuBgStop, MenuScrollArrowDisp
#include "ingame/menu/menu_csr_obj.h"   // MenuCsrObj*
#include "ingame/menu/menu_draw_tex.h"  // Menu_DrawTex, MENU_PIC
#include "ingame/menu/start_menu.h"     // GetFinderStateWrk
#include "ingame/plyr/plyr_mdl.h"       // PlyrIsFinderModeOff
#include "ingame/sound/snd_ctrl.h"      // SndSePlay, SndSeBank*
#include "ingame/story_menu.h"          // IsLockTblOn, IsMenuEnable
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/b2d/B2dCtl.h"          // B2D_HOOK_ARGS, B2dCtlFindItem*, B2dCtlSetItemHook
#include "system/b2d/B2dGridSpr.h"      // B2D_GRID_SPR
#include "system/b2d/B2dSpr.h"          // B2dSprSetDisp
#include "system/b2d/BpCtl.h"           // BpCtl*, BpPlayLabel, BpGetBlendAlpha
#include "system/eeiop/cd_file_dat.h"   // MEMO_PHF, CATEGORY_TM2, KOUMOKU1_TM2, FUSEN_TM2, CATEGORY_PIC_*_TM2
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage, GetPALMode
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadRptPlaySe, CheckPadTrgPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// Phases.
#define MENU_FILE_TOP_PHASE     0x39    // 57: the story menu's top
#define MENU_FILE_PHASE         0x3e    // 62 StoryMenu_File
#define MENU_FILE_DETAIL_PHASE  0x56    // 86 StoryMenu_FileDetail
#define MENU_FILE_FIRST_PHASE   0x57    // 87 StoryMenu_FileFirst

// The cursors (MenuFileCsr* csr argument; also the focus).
#define MENU_FILE_CSR_TAB       0
#define MENU_FILE_CSR_ROW       1

#define MENU_FILE_TAB_MAX       4       // memo categories
#define MENU_FILE_ROW_STEP      0x1b    // pixels between rows (tabs, list, grid sprites)

// Pad bits besides pad_key: right / R2 into the rows, left / L2 back.
#define MENU_FILE_PAD_NEXT      0x2002
#define MENU_FILE_PAD_PREV      0x8001

// Sounds (the file menu's bank 0x1bc9 and SE_BANK_DEFAULT).
#define MENU_FILE_SE_BANK       0x1bc9
#define MENU_FILE_SE_TURN       0x1bc90000  // page / focus / detail
#define MENU_FILE_SE_MOVE       0x1a9b0000  // cursor
#define MENU_FILE_SE_CANCEL     0x1a9b0001
#define MENU_FILE_SE_NG         0x1a9b0002

// MEMO_PHF items.
#define MENU_FILE_ITEM_ARROW_UP     1000
#define MENU_FILE_ITEM_ARROW_UP_FX  0x3f0
#define MENU_FILE_ITEM_ARROW_DN_FX  0x3eb
#define MENU_FILE_ITEM_ARROW_DN     0x3fd
#define MENU_FILE_ITEM_GRID_TAB     0x3fb   // grid sprite on the tab cursor, shown on the tab focus
#define MENU_FILE_ITEM_GRID_TAB2    0x3fc   // the same, always shown
#define MENU_FILE_ITEM_GRID_ROW     0x3fa   // grid sprite on the row cursor, shown on the row focus
#define MENU_FILE_ITEM_PIC          0x400
#define MENU_FILE_ITEM_TABS         0x404
#define MENU_FILE_ITEM_ROWS         0x405
#define MENU_FILE_ITEM_NEW          0x406
#define MENU_FILE_ITEM_TAB_NEW_OFF  0x40f
#define MENU_FILE_ITEM_TITLE        0x423
#define MENU_FILE_ITEM_FADE         0x3ea   // the fade layer's alpha item

// The memo-menu request (bss 0x3f6048, 0xc bytes).
struct MEMO_MENU_REQ
{
    /* 0x0 */ short memo;               // -1: none
    /* 0x4 */ int   mode;               // MEMO_MENU_REQ_*
    /* 0x8 */ int   link;               // GPhaseCtrlRequest's link
};
static_assert(sizeof(MEMO_MENU_REQ) == 0xc, "MEMO_MENU_REQ is 0xc bytes");
static_assert(offsetof(MEMO_MENU_REQ, mode) == 4, "MEMO_MENU_REQ.mode");
static_assert(offsetof(MEMO_MENU_REQ, link) == 8, "MEMO_MENU_REQ.link");

// bss 0x3f5ec0..0x3f6054
static int             file_focus;                      // 0x3f5ec0: MENU_FILE_CSR_*
static int             file_focus_old;                  // 0x3f5ec4
static MENU_CSR_WRK    file_tab_csr;                    // 0x3f5ec8
static MENU_CSR_WRK    file_row_csr[MENU_FILE_TAB_MAX]; // 0x3f5f04: per tab
static MEMO_LIST      *file_tab_list[MENU_FILE_TAB_MAX];    // 0x3f5ff4: compacted, one per tab
static short           file_tab_cat[MENU_FILE_TAB_MAX];     // 0x3f6004: the tab's category, -1 none
static int             file_tab_num;                    // 0x3f600c
static int             file_started;                    // 0x3f6010: the pad ran once (nothing reads it)
static B2D_GRID_SPR   *file_grid[3];                    // 0x3f6018: MENU_FILE_ITEM_GRID_TAB, _TAB2, _ROW
static Menu_DrawTex   *file_title_tex[7];               // 0x3f6028: KOUMOKU, 8 title strips each
static MEMO_MENU_REQ   memo_menu_req;                   // 0x3f6048

// sbss
static short           file_from;                       // gp+0x10c: the phase the menu opened from
static Menu_DrawTex   *file_category_tex[1];            // gp+0x110: CATEGORY_TM2, 10 cells each
static Menu_DrawTex   *file_fusen_tex[1];               // gp+0x118: FUSEN_TM2, 4 cells each
static MENU_PIC       *file_pic;                        // gp+0x11c

// sdata
static short file_category_pic[MENU_FILE_TAB_MAX] =     // 0x371160
{
    CATEGORY_PIC_01_TM2, CATEGORY_PIC_02_TM2, CATEGORY_PIC_03_TM2, CATEGORY_PIC_04_TM2,
};
static int file_first_anim;                             // 0x3711b4: phase 87's LayoutAnim slot

// data 0x32b5a8: the tab stickers, one per tab
static const char *file_tab_new_item[MENU_FILE_TAB_MAX] = { "R fsn1", "R fsn2", "R fsn3", "R fsn4" };

static short MenuFileListPad(void);
static void  MenuFileListInit(void);
static void  MenuFileListRelease(void);
static int   MenuFileCsrGetPageRows(int csr);
static int   MenuFileGetFocus(void);
static int   MenuFileSetFocus(u_int focus);
static int   MenuFileFocusIsChanged(void);
static int   MenuFileGetStarted(void);
static int   MenuFileSetStarted(u_int started);
static int   MenuFileCsrGetNum(int csr);
static void  MenuFileCsrInit(void);
static void  MenuFileCsrRelease(void);
static void  MenuFileCsrUpdate(int csr);
static void  MenuFileCsrPagePrev(int csr);
static void  MenuFileCsrPageNext(int csr);
static short MenuFileCsrGetPos(int csr);
static short MenuFileCsrSetPos(int csr, int pos);
static int   MenuFileCsrGetLastPage(int csr);
static int   MenuFileCsrGetPage(int csr);
static int   MenuFileCsrGetPageTop(int csr);
static int   MenuFileCsrGetPageRow(int csr);
static void  MenuFileTabClear(void);
static void  MenuFileTabBuild(void);
static void  MenuFileTabFree(void);
static int   MenuFileGetTabNum(void);
static short MenuFileTabGetCategory(int tab);
static short MenuFileTabGetMemoNum(short tab);
static int   MenuFileTabHasOpen(short tab);
static s_char MenuFileListGetPageNum(short tab, short row);
static int   MenuFileCsrSetMemo(short memo);
static void  MenuFileGridInit(void);
static void  MenuFileGridRelease(void);
static void  MenuFileGridMain(void);
static void  MenuFileArrowInit(void);
static void  MenuFileArrowRelease(void);
static void  MenuFileArrowMain(void);
static void  MenuFileCategoryTexInit(void);
static void  MenuFileCategoryTexRelease(void);
static int   MenuFileCategoryTexIsReady(void);
static void  MenuFileTabHookInit(void);
static void  MenuFileTabHookRelease(void);
static void  MenuFileTitleTexInit(void);
static void  MenuFileTitleTexRelease(void);
static int   MenuFileTitleTexIsReady(void);
static void  MenuFileListHookInit(void);
static void  MenuFileListHookRelease(void);
static void  MenuFileFusenTexInit(void);
static void  MenuFileFusenTexRelease(void);
static int   MenuFileFusenTexIsReady(void);
static void  MenuFileNewHookInit(void);
static void  MenuFileNewHookRelease(void);
static void  MenuFileTabNewMain(void);
static void  MenuFileTabNewInit(void);
static void  MenuFileTabNewRelease(void);
static void  MenuFilePicInit(void);
static void  MenuFilePicRelease(void);

// ──────────────────────────────────────────────────────────────────────
// Phase 62 StoryMenu_File
// ──────────────────────────────────────────────────────────────────────

// Step 0 builds the tabs and cursors and starts the loads (layout, the
// three pictures, the SE bank); step 1 waits for them, plays "in" (or
// "koumoku_in" when the request opens on the rows) and installs the hooks;
// step 2 waits for the label.
// FF3 0x00204810
// callcheck: CmnFadeLayerSetItem, CmnFadeLayerShow (capCtl methods)

int init_StoryMenu_File(void)
{
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        file_from = GetGPhaseCtrl()->from;
        BpCtlMain();
        MenuFileListInit();
        MenuFileCategoryTexInit();
        MenuFileTitleTexInit();
        MenuFileFusenTexInit();
        BpCtlLoadReq(GetMenuLangFileNo(), nullptr);
        SndSeBankLoad(MENU_FILE_SE_BANK);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        BpCtlMain();
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetMenuLangFileNo())) != 0
            && MenuFileCategoryTexIsReady() != 0
            && MenuFileTitleTexIsReady() != 0
            && MenuFileFusenTexIsReady() != 0
            && SndSeBankIsLoaded(MENU_FILE_SE_BANK) != 0)
        {
            if (MemoMenuReqIsRowSelect() == 0)
            {
                BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "in");            // sdata 0x371180
            }
            else
            {
                BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "koumoku_in");
            }
            GetCmnFadeLayer()->SetItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_FADE);
            GetCmnFadeLayer()->Show(1);
            MenuFileGridInit();
            MenuFileArrowInit();
            MenuFileTabHookInit();
            MenuFileListHookInit();
            MenuFileNewHookInit();
            MenuFileTabNewInit();
            MenuFilePicInit();
            GetGPhaseCtrl()->leaf->state_cnt = 2;
        }
        return 0;

    case 2:
        MenuFileListMain(1);
        return MenuLayoutIsPlaying(GetMenuLangFileNo()) == 0;
    }
    return 1;
}

// Step 0 plays "out"; step 1 waits for it and releases everything. The
// menu background stops unless the menu goes back to the story menu's top
// or was requested with a link. The first visit is over (memo_flg).
// FF3 0x00204a80
// callcheck: CmnFadeLayerSetItemHandle (capCtl::SetItemHandle)

int end_StoryMenu_File(void)
{
    MenuFileListMain(1);
    if (GetGPhaseCtrl()->leaf->state_cnt == 0)
    {
        BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "out");                   // sdata 0x371188
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (GetGPhaseCtrl()->leaf->state_cnt == 1)
    {
        if (MenuLayoutIsPlaying(GetMenuLangFileNo()) != 0)
        {
            return 0;
        }
        if (file_from != MENU_FILE_TOP_PHASE && MemoMenuReqGetLink() == 0)
        {
            MenuBgStop();
        }
        MenuFileGridRelease();
        MenuFileArrowRelease();
        MenuFileTabHookRelease();
        MenuFileListHookRelease();
        MenuFileNewHookRelease();
        MenuFileTabNewRelease();
        MenuFilePicRelease();
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        BpCtlRelease(BpCtlGetHandleByFileNo(GetMenuLangFileNo()));
        MenuFileCategoryTexRelease();
        MenuFileTitleTexRelease();
        MenuFileFusenTexRelease();
        SndSeBankRelease(MENU_FILE_SE_BANK, 0);
        MenuFileListRelease();
        memo_flg.file_seen = 1;
    }
    return 1;
}

// FF3 0x00204c00

int one_StoryMenu_File(void)
{
    short next;

    MenuFileListMain(1);
    next = MenuFileListPad();
    if (next != -1)
    {
        GPhaseCtrlRequest(next, 0);
    }
    return next != -1;
}

// FF3 0x00204c48

void pre_StoryMenu_File(void)
{
}

// FF3 0x00204c50

void after_StoryMenu_File(void)
{
}

// FF3 0x00204c58
// callcheck: CmnFadeLayerUpdate (capCtl::Update)

void MenuFileListMain(int anim)
{
    int focus = MenuFileGetFocus();

    if (MenuFileFocusIsChanged() != 0)
    {
        if (focus == MENU_FILE_CSR_TAB)
        {
            BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "koumoku_back");
        }
        else if (focus == MENU_FILE_CSR_ROW)
        {
            BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "koumoku_next");
        }
    }
    MenuFileGridMain();
    MenuFileArrowMain();
    MenuFileTabNewMain();
    GetCmnFadeLayer()->Update();
    if (anim != 0)
    {
        LayoutAnimMain();
    }
    BpCtlMain();
}

// The phase to go to, -1 to stay. The first visit goes to phase 87 once.
// A row whose memo is not found buzzes (FF3 reads memo_wrk[-1] when the
// list is empty; a tab always has memos, so it does not happen).
// FF3 0x00204d28

static short MenuFileListPad(void)
{
    short next = -1;
    int   focus;
    short memo;
    int   to;
    u_int se;

    if (memo_flg.file_seen == 0)
    {
        memo_flg.file_seen = 1;
        return MENU_FILE_FIRST_PHASE;
    }
    focus = MenuFileGetFocus();
    memo  = MenuFileGetMemo();
    MenuFileCsrUpdate(focus);
    if (MenuFileGetStarted() == 0)
    {
        MenuFileSetStarted(1);
    }
    if (MenuFileCsrGetPageRows(focus) < MenuFileCsrGetNum(focus))
    {
        if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_PREV], -1, 0) != 0)
        {
            SndSePlay(MENU_FILE_SE_TURN, 100, 0x1000, 0, nullptr, 0);
            MenuFileCsrPagePrev(focus);
            return -1;
        }
        if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_NEXT], -1, 0) != 0)
        {
            SndSePlay(MENU_FILE_SE_TURN, 100, 0x1000, 0, nullptr, 0);
            MenuFileCsrPageNext(focus);
            return -1;
        }
    }

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0
        || (focus == MENU_FILE_CSR_TAB && CheckPadTrgPlaySe(MENU_FILE_PAD_NEXT, -1, 0) != 0))
    {
        to = focus + 1;
        if (to >= 2)
        {
            if ((u_short)memo < MEMO_NUM && memo_wrk[memo].status != 0)
            {
                SndSePlay(MENU_FILE_SE_TURN, 100, 0x1000, 0, nullptr, 0);
                return MENU_FILE_DETAIL_PHASE;
            }
            se = MENU_FILE_SE_NG;
        }
        else if (MenuFileCsrGetNum(focus) > 0)
        {
            MenuFileSetFocus(to);
            se = MENU_FILE_SE_TURN;
        }
        else
        {
            se = MENU_FILE_SE_NG;
        }
    }
    else if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) != 0
             || (focus == MENU_FILE_CSR_ROW && CheckPadTrgPlaySe(MENU_FILE_PAD_PREV, -1, 0) != 0))
    {
        to = focus - 1;
        if (to < 0)
        {
            se   = MENU_FILE_SE_CANCEL;
            next = file_from;
        }
        else
        {
            MenuFileSetFocus(to);
            se = MENU_FILE_SE_TURN;
        }
    }
    else
    {
        return -1;
    }
    SndSePlay(se, 100, 0x1000, 0, nullptr, 0);
    return next;
}

// FF3 0x00204f88

static void MenuFileListInit(void)
{
    file_focus   = MENU_FILE_CSR_TAB;
    file_started = 0;
    MenuFileTabClear();
    MenuFileTabBuild();
    MenuFileCsrInit();
    if (MemoMenuReqIsSet() != 0)
    {
        MenuFileCsrSetMemo(MemoMenuReqGetMemo());
        if (MemoMenuReqIsRowSelect() != 0)
        {
            file_focus = MENU_FILE_CSR_ROW;
        }
    }
    file_focus_old = file_focus;
}

// FF3 0x00205010

static void MenuFileListRelease(void)
{
    MenuFileCsrRelease();
    MenuFileTabFree();
    MemoMenuReqInit();
}

// ──────────────────────────────────────────────────────────────────────
// The cursors
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00205038

static int MenuFileCsrGetPageRows(int csr)
{
    if (csr == MENU_FILE_CSR_TAB)
        return 5;
    if (csr == MENU_FILE_CSR_ROW)
        return 9;
    return 0;
}

// FF3 0x00205058

static int MenuFileGetFocus(void)
{
    return file_focus;
}

// FF3 0x00205068

static int MenuFileSetFocus(u_int focus)
{
    if (focus < 2)
    {
        file_focus = (int)focus;
    }
    return file_focus;
}

// FF3 0x00205080

static int MenuFileFocusIsChanged(void)
{
    if (file_focus_old != file_focus)
    {
        file_focus_old = file_focus;
        return 1;
    }
    return 0;
}

// FF3 0x002050b0

static int MenuFileGetStarted(void)
{
    return file_started;
}

// FF3 0x002050c0

static int MenuFileSetStarted(u_int started)
{
    if (started < 2)
    {
        file_started = (int)started;
    }
    return file_started;
}

// FF3 0x002050e0

static int MenuFileCsrGetNum(int csr)
{
    if (csr == MENU_FILE_CSR_TAB)
        return MenuFileGetTabNum();
    if (csr == MENU_FILE_CSR_ROW)
        return MenuFileTabGetMemoNum(MenuFileCsrGetPos(MENU_FILE_CSR_TAB));
    return 0;
}

// The cursor object: the tab cursor, or the current tab's row cursor (an
// inline: every MenuFileCsr* function below carries it).
static inline MENU_CSR_WRK *MenuFileCsrGetWrk(int csr)
{
    if (csr == MENU_FILE_CSR_TAB)
        return &file_tab_csr;
    if (csr == MENU_FILE_CSR_ROW)
        return &file_row_csr[MenuFileCsrGetPos(MENU_FILE_CSR_TAB)];
    return nullptr;
}

// FF3 0x00205130

static void MenuFileCsrInit(void)
{
    short num  = (short)MenuFileGetTabNum();
    int   rows = MenuFileCsrGetPageRows(MENU_FILE_CSR_TAB);
    int   i;

    MenuCsrObjInit(&file_tab_csr, 1, num == 0 ? 1 : num, 0);
    MenuCsrObjSetViewX(&file_tab_csr, 1, 0);
    MenuCsrObjSetViewY(&file_tab_csr, rows, 0);
    for (i = 0; i < MENU_FILE_TAB_MAX; i++)
    {
        num  = MenuFileTabGetMemoNum((short)i);
        rows = MenuFileCsrGetPageRows(MENU_FILE_CSR_ROW);
        MenuCsrObjInit(&file_row_csr[i], 1, num == 0 ? 1 : num, 0);
        MenuCsrObjSetViewX(&file_row_csr[i], 1, 0);
        MenuCsrObjSetViewY(&file_row_csr[i], rows, 0);
    }
}

// FF3 0x00205248

static void MenuFileCsrRelease(void)
{
    int i;

    MenuCsrObjRelease(&file_tab_csr);
    for (i = 0; i < MENU_FILE_TAB_MAX; i++)
    {
        MenuCsrObjRelease(&file_row_csr[i]);
    }
}

// FF3 0x00205298
// callcheck: MenuFileCsrGetPos (inline MenuFileCsrGetWrk)

static void MenuFileCsrUpdate(int csr)
{
    MENU_CSR_WRK *wrk = MenuFileCsrGetWrk(csr);

    // PC: the mouse. The tab cursor's bar, or the row cursor's.
    MenuMouseCsrGrid(wrk, file_grid[csr == MENU_FILE_CSR_TAB ? 0 : 2],
                     BpCtlGetHandleByFileNo(GetMenuLangFileNo()));
    MenuCsrObjUpdate(wrk, 0);
    MenuCsrObjIsMovedSe(wrk, MENU_FILE_SE_MOVE);
}

// FF3 0x00205300
// callcheck: MenuFileCsrGetPos (inline MenuFileCsrGetWrk)

static void MenuFileCsrPagePrev(int csr)
{
    MenuCsrObjPagePrevY(MenuFileCsrGetWrk(csr));
}

// FF3 0x00205350
// callcheck: MenuFileCsrGetPos (inline MenuFileCsrGetWrk)

static void MenuFileCsrPageNext(int csr)
{
    MenuCsrObjPageNextY(MenuFileCsrGetWrk(csr));
}

// FF3 0x002053a0
// callcheck: MenuFileCsrGetPos (inline MenuFileCsrGetWrk)

static short MenuFileCsrGetPos(int csr)
{
    return MenuCsrObjGetPos(MenuFileCsrGetWrk(csr));
}

// Below 0 wraps to the last item, past the last to the first.
// FF3 0x002053f0
// callcheck: MenuFileCsrGetPos (inline MenuFileCsrGetWrk)

static short MenuFileCsrSetPos(int csr, int pos)
{
    MENU_CSR_WRK *wrk  = MenuFileCsrGetWrk(csr);
    int           last = MenuFileCsrGetNum(csr) - 1;
    int           p    = last;

    if (pos >= 0)
    {
        p = pos > last ? 0 : pos;
    }
    return MenuCsrObjSetPos(wrk, (short)p, -1, -1, -1, -1);
}

// FF3 0x00205498

static int MenuFileCsrGetLastPage(int csr)
{
    int page = (MenuFileCsrGetNum(csr) - 1) / MenuFileCsrGetPageRows(csr);

    return page < 0 ? 0 : page;
}

// FF3 0x002054f0

static int MenuFileCsrGetPage(int csr)
{
    int pos = MenuFileCsrGetPos(csr);

    return pos / MenuFileCsrGetPageRows(csr);
}

// FF3 0x00205538

static int MenuFileCsrGetPageTop(int csr)
{
    int page = MenuFileCsrGetPage(csr);

    return page * MenuFileCsrGetPageRows(csr);
}

// FF3 0x00205578

static int MenuFileCsrGetPageRow(int csr)
{
    int pos = MenuFileCsrGetPos(csr);

    return pos % MenuFileCsrGetPageRows(csr);
}

// FF3 0x002055c0

short MenuFileGetTab(void)
{
    return MenuFileCsrGetPos(MENU_FILE_CSR_TAB);
}

// FF3 0x002055d8

short MenuFileGetRow(void)
{
    return MenuFileCsrGetPos(MENU_FILE_CSR_ROW);
}

// FF3 0x002055f0 (no callers)

short MenuFileSetRow(int row)
{
    return MenuFileCsrSetPos(MENU_FILE_CSR_ROW, row);
}

// FF3 0x00205610

short MenuFileStepMemo(int dir)
{
    int   step = dir > 0 ? 1 : -1;
    short row  = MenuFileGetRow();
    int   num  = MenuFileCsrGetNum(MENU_FILE_CSR_ROW);
    int   left;
    int   d;
    int   r;
    short memo;

    if (num > 1)
    {
        for (left = num - 1, d = step; left != 0; left--, d += step)
        {
            r = row + d;
            if (r < 0)
            {
                r += num;
            }
            else if (r >= num)
            {
                r -= num;
            }
            memo = MenuFileListGetMemo(MenuFileGetTab(), (short)r);
            if (memo != -1 && memo_wrk[memo].status != 0)
            {
                return MenuFileCsrSetPos(MENU_FILE_CSR_ROW, r);
            }
        }
    }
    return row;
}

// FF3 0x00205748

short MenuFileGetMemo(void)
{
    short tab = MenuFileGetTab();

    return MenuFileListGetMemo(tab, MenuFileGetRow());
}

// FF3 0x00205780

s_char MenuFileGetPageNum(void)
{
    short tab = MenuFileGetTab();

    return MenuFileListGetPageNum(tab, MenuFileGetRow());
}

// ──────────────────────────────────────────────────────────────────────
// The tabs: a MEMO_LIST per category that has memos.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x002057b8

static void MenuFileTabClear(void)
{
    short i;

    for (i = 0; i < MENU_FILE_TAB_MAX; i++)
    {
        file_tab_list[i] = nullptr;
        file_tab_cat[i]  = -1;
    }
    file_tab_num = 0;
}

// Once the game was cleared (any difficulty) every memo is listed, found
// or not, and every category gets a tab.
// FF3 0x00205800
// callcheck: operator_new, MemoListCtor, MemoListDtor, operator_delete (new / delete MEMO_LIST)
// callcheck: FUN_001a5e60 (FINDER_STATE_WRK method)

static void MenuFileTabBuild(void)
{
    int        clear = GetFinderStateWrk()->FUN_001a5e60();
    int        num   = 0;
    short      cat;
    MEMO_LIST *list;

    for (cat = 0; cat < MENU_FILE_TAB_MAX; cat++)
    {
        list = new MEMO_LIST(cat, clear > 0);
        file_tab_list[num] = list;
        if (clear > 0 || list->num > 0)
        {
            file_tab_cat[num] = cat;
            num++;
        }
        else
        {
            delete list;
            file_tab_list[num] = nullptr;
        }
    }
    file_tab_num = num;
}

// The lists are deleted but not cleared (MenuFileTabClear does that on
// the next visit).
// FF3 0x00205910
// callcheck: MemoListDtor, operator_delete (delete MEMO_LIST)

static void MenuFileTabFree(void)
{
    short i;

    for (i = 0; i < MENU_FILE_TAB_MAX; i++)
    {
        if (file_tab_list[i] != nullptr)
        {
            delete file_tab_list[i];
        }
    }
}

// FF3 0x00205988

static int MenuFileGetTabNum(void)
{
    return file_tab_num;
}

// FF3 0x00205998

static short MenuFileTabGetCategory(int tab)
{
    if (tab >= 0 && tab < MenuFileGetTabNum())
    {
        return file_tab_cat[tab];
    }
    return -1;
}

// The tab's list (an inline: the functions below carry it).
static inline MEMO_LIST *MenuFileTabGetList(short tab)
{
    if (tab < 0 || tab >= MenuFileGetTabNum())
        return nullptr;
    return file_tab_list[tab];
}

// FF3 0x002059e8
// callcheck: MenuFileGetTabNum (inline MenuFileTabGetList)

static short MenuFileTabGetMemoNum(short tab)
{
    MEMO_LIST *list = MenuFileTabGetList(tab);

    return list != nullptr ? list->num : 0;
}

// FF3 0x00205a50
// callcheck: MemoListHasOpen (MEMO_LIST::HasOpen)
// callcheck: MenuFileGetTabNum (inline MenuFileTabGetList)

static int MenuFileTabHasOpen(short tab)
{
    MEMO_LIST *list = MenuFileTabGetList(tab);

    if (list != nullptr)
    {
        return list->HasOpen();
    }
    return 0;
}

// FF3 0x00205b30
// callcheck: MenuFileGetTabNum (inline MenuFileTabGetList)

short MenuFileListGetMemo(short tab, short row)
{
    MEMO_LIST *list = MenuFileTabGetList(tab);

    if (list == nullptr)
        return -1;
    return list->Get(row);
}

// FF3 0x00205bd8

static s_char MenuFileListGetPageNum(short tab, short row)
{
    short memo = MenuFileListGetMemo(tab, row);

    if ((u_short)memo >= MEMO_NUM)
        return 0;
    return MemoGetPageBreakNum(memo, memo_wrk[memo].page);
}

// FF3 0x00205c18
// callcheck: MemoListIndexOf (MEMO_LIST::IndexOf)

static int MenuFileCsrSetMemo(short memo)
{
    short tab;
    short row;

    for (tab = 0; tab < MENU_FILE_TAB_MAX; tab++)
    {
        if (file_tab_list[tab] != nullptr && (row = file_tab_list[tab]->IndexOf(memo)) != -1)
        {
            MenuFileCsrSetPos(MENU_FILE_CSR_TAB, tab);
            MenuFileCsrSetPos(MENU_FILE_CSR_ROW, row);
            return 1;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The grid sprites: highlight bars that follow the cursors' page rows.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00205cd0
// callcheck: operator_new, B2dGridSprCtor (new B2D_GRID_SPR)

static void MenuFileGridInit(void)
{
    file_grid[0] = new B2D_GRID_SPR(GetMenuLangFileNo(), MENU_FILE_ITEM_GRID_TAB, 0, MENU_FILE_ROW_STEP);
    file_grid[1] = new B2D_GRID_SPR(GetMenuLangFileNo(), MENU_FILE_ITEM_GRID_TAB2, 0, MENU_FILE_ROW_STEP);
    file_grid[2] = new B2D_GRID_SPR(GetMenuLangFileNo(), MENU_FILE_ITEM_GRID_ROW, 0, MENU_FILE_ROW_STEP);
}

// FF3 0x00205d88
// callcheck: B2dGridSprDtor, operator_delete (delete)

static void MenuFileGridRelease(void)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        if (file_grid[i] != nullptr)
        {
            delete file_grid[i];
        }
        file_grid[i] = nullptr;
    }
}

// FF3 0x00205df0
// callcheck: B2dGridSprSetPos, B2dGridSprSetDisp (B2D_GRID_SPR methods)

static void MenuFileGridMain(void)
{
    int i;
    int focus;

    for (i = 0; i < 3; i++)
    {
        if (file_grid[i] == nullptr)
            return;
    }
    file_grid[0]->SetPos(0, (short)MenuFileCsrGetPageRow(MENU_FILE_CSR_TAB));
    file_grid[1]->SetPos(0, (short)MenuFileCsrGetPageRow(MENU_FILE_CSR_TAB));
    file_grid[2]->SetPos(0, (short)MenuFileCsrGetPageRow(MENU_FILE_CSR_ROW));
    focus = MenuFileGetFocus();
    if (focus != MENU_FILE_CSR_TAB)
    {
        file_grid[0]->SetDisp(0);
        file_grid[1]->SetDisp(1);
    }
    else
    {
        file_grid[0]->SetDisp(1);
        file_grid[1]->SetDisp(1);
    }
    file_grid[2]->SetDisp(focus != MENU_FILE_CSR_TAB);
}

// ──────────────────────────────────────────────────────────────────────
// The scroll arrows of the row list.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00205ef8

static void MenuFileArrowInit(void)
{
}

// FF3 0x00205f00

static void MenuFileArrowRelease(void)
{
}

// FF3 0x00205f08

static void MenuFileArrowMain(void)
{
    int handle = BpCtlGetHandleByFileNo(GetMenuLangFileNo());
    int last   = MenuFileCsrGetLastPage(MENU_FILE_CSR_ROW);
    int page   = MenuFileCsrGetPage(MENU_FILE_CSR_ROW);

    MenuScrollArrowDisp(handle, MENU_FILE_ITEM_ARROW_UP, MENU_FILE_ITEM_ARROW_DN,
                        MENU_FILE_ITEM_ARROW_UP_FX, MENU_FILE_ITEM_ARROW_DN_FX, page > 0, page < last);
}

// FF3 0x00205f70

int GetMenuLangFileNo(void)
{
    return GetSysWrk()->GetLanguage() + MEMO_PHF;
}

// ──────────────────────────────────────────────────────────────────────
// The tab column (item 0x404): CATEGORY_TM2, 128 x 25 cells in two
// columns of five, cell = category, 5 = a category with nothing found.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00205fa8
// callcheck: operator_new, MenuDrawTexCtor (new Menu_DrawTex)

static void MenuFileCategoryTexInit(void)
{
    file_category_tex[0] = new Menu_DrawTex;
    file_category_tex[0]->Load(GetSysWrk()->GetLanguage() + CATEGORY_TM2);
    file_category_tex[0]->direct = 1;
}

// FF3 0x00206020

static void MenuFileCategoryTexRelease(void)
{
    if (file_category_tex[0] != nullptr)
    {
        delete file_category_tex[0];
    }
    file_category_tex[0] = nullptr;
}

// FF3 0x00206058
// callcheck: MenuDrawTexIsSet (Menu_DrawTex::IsSet)

static int MenuFileCategoryTexIsReady(void)
{
    if (file_category_tex[0]->IsSet() != 0 && file_category_tex[0]->IsReady() == 0)
    {
        return 0;
    }
    return 1;
}

// Five tabs a page, dark red under the cursor.
// FF3 0x00206120
// callcheck: MenuDrawTexDraw (Menu_DrawTex::Draw)

static int MenuFileTabHook(const B2D_HOOK_ARGS *args)
{
    const int    *rect = (const int *)args->t1;
    u_char        alpha;
    int           top;
    short         cur;
    int           i;
    int           tab;
    short         cat;
    int           cell;
    int           x;
    int           y;
    Menu_DrawTex *tex;

    if ((int)args->stack[2] == 0)
    {
        return 0;
    }
    alpha = (u_char)args->stack[2];
    top   = MenuFileCsrGetPageTop(MENU_FILE_CSR_TAB);
    cur   = MenuFileCsrGetPos(MENU_FILE_CSR_TAB);
    for (i = 0; i < 5; i++)
    {
        tab = top + i;
        cat = MenuFileTabGetCategory(tab);
        if (cat == -1)
        {
            continue;
        }
        cell = MenuFileTabHasOpen((short)tab) != 0 ? cat : 5;
        x    = rect[0];
        y    = rect[1] + i * MENU_FILE_ROW_STEP;
        tex  = file_category_tex[cell / 10];
        tex->u      = cell % 10 / 5 << 7;
        tex->v      = cell % 10 % 5 * 0x19;
        tex->h      = 0x19;
        tex->w      = 0x80;
        tex->alphar = BpGetBlendAlpha((u_int)args->stack[3]);
        tex->test   = MENU_DRAW_TEX_TEST_ALWAYS;
        if (tab == cur)
        {
            tex->Draw(x, y, 0, 0, 0x55, 0x04, 0x08, alpha, 0);
        }
        else
        {
            tex->Draw(x, y, 0, 0, 0x20, 0x17, 0x11, alpha, 0);
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x002060a0

static void MenuFileTabHookInit(void)
{
    B2D_HOOK_REC recs[2] =
    {
        { MenuFileTabHook, 0, MENU_FILE_ITEM_TABS, nullptr },
        { nullptr, -1, -1, nullptr },
    };

    B2dSetHookList(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), recs);
}

// FF3 0x00206118

static void MenuFileTabHookRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The memo titles (items 0x405, 0x423): KOUMOKU1_TM2 + language * 10 + i,
// seven pictures of eight 256 x 32 strips; strip = memo_page.title + 1 of
// the memo's last found page, 0 for a memo not found.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00206390
// callcheck: operator_new, MenuDrawTexCtor (new Menu_DrawTex)

static void MenuFileTitleTexInit(void)
{
    int i;

    for (i = 0; i < 7; i++)
    {
        file_title_tex[i] = new Menu_DrawTex;
        file_title_tex[i]->Load(GetSysWrk()->GetLanguage() * 10 + i + KOUMOKU1_TM2);
        file_title_tex[i]->direct = 1;
    }
}

// FF3 0x00206450

static void MenuFileTitleTexRelease(void)
{
    int i;

    for (i = 0; i < 7; i++)
    {
        if (file_title_tex[i] != nullptr)
        {
            delete file_title_tex[i];
        }
        file_title_tex[i] = nullptr;
    }
}

// FF3 0x002064b0
// callcheck: MenuDrawTexIsSet (Menu_DrawTex::IsSet)

static int MenuFileTitleTexIsReady(void)
{
    int i;

    for (i = 0; i < 7; i++)
    {
        if (file_title_tex[i]->IsSet() != 0 && file_title_tex[i]->IsReady() == 0)
        {
            return 0;
        }
    }
    return 1;
}

// The strip of a memo's title (an inline: both hooks carry it); -1 when
// the memo's page row is -1.
static inline int MenuFileTitleStrip(short memo)
{
    short page = MemoGetCurrentPageIndex(memo);

    if (page == -1)
        return -1;
    return memo_wrk[memo].status != 0 ? memo_page[page].title + 1 : 0;
}

// The strip's picture, set up for drawing (an inline as above).
static inline Menu_DrawTex *MenuFileTitleTexSet(int strip, u_int flags)
{
    Menu_DrawTex *tex = file_title_tex[strip / 8];

    tex->v      = strip % 8 * 0x20;
    tex->u      = 0;
    tex->h      = 0x20;
    tex->w      = 0x100;
    tex->alphar = BpGetBlendAlpha(flags);
    tex->test   = MENU_DRAW_TEX_TEST_ALWAYS;
    return tex;
}

// Nine rows a page; the row under the cursor in red. A memo whose page row
// is -1 ends the drawing (without SetDefaultGsDrawState).
// FF3 0x002065a0
// callcheck: MenuDrawTexDraw (Menu_DrawTex::Draw)
// callcheck: BpGetBlendAlpha (inline MenuFileTitleTexSet)

static int MenuFileListHook(const B2D_HOOK_ARGS *args)
{
    const int    *rect = (const int *)args->t1;
    u_char        alpha;
    short         tab;
    int           top;
    short         cur;
    int           i;
    int           row;
    short         memo;
    int           strip;
    int           x;
    int           y;
    Menu_DrawTex *tex;

    if ((int)args->stack[2] == 0)
    {
        return 0;
    }
    alpha = (u_char)args->stack[2];
    tab   = MenuFileCsrGetPos(MENU_FILE_CSR_TAB);
    top   = MenuFileCsrGetPageTop(MENU_FILE_CSR_ROW);
    cur   = MenuFileCsrGetPos(MENU_FILE_CSR_ROW);
    for (i = 0; i < 9; i++)
    {
        row  = top + i;
        memo = MenuFileListGetMemo(tab, (short)row);
        if (memo == -1)
        {
            continue;
        }
        strip = MenuFileTitleStrip(memo);
        if (strip == -1)
        {
            return 0;
        }
        x   = rect[0];
        y   = rect[1] + i * MENU_FILE_ROW_STEP;
        tex = MenuFileTitleTexSet(strip, (u_int)args->stack[3]);
        if (row == cur)
        {
            tex->Draw(x, y, 0, 0, 0x41, 0x03, 0x06, alpha, 0);
        }
        else
        {
            tex->Draw(x, y, 0, 0, 0x30, 0x1c, 0x0e, alpha, 0);
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// The selected memo's title (no alpha test).
// FF3 0x00206830
// callcheck: MenuDrawTexDraw (Menu_DrawTex::Draw)
// callcheck: BpGetBlendAlpha (inline MenuFileTitleTexSet)

static int MenuFileTitleHook(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;
    short      memo = MenuFileGetMemo();
    int        strip;

    if (memo != -1 && (strip = MenuFileTitleStrip(memo)) != -1)
    {
        MenuFileTitleTexSet(strip, (u_int)args->stack[3])
            ->Draw(rect[0], rect[1], 0, 0, 0x30, 0x1c, 0x0e, (u_char)args->stack[2], 0);
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00206528 (the list: rodata 0x35c518)

static void MenuFileListHookInit(void)
{
    B2D_HOOK_REC recs[3] =
    {
        { MenuFileListHook, 0, MENU_FILE_ITEM_ROWS, nullptr },
        { MenuFileTitleHook, 0, MENU_FILE_ITEM_TITLE, nullptr },
        { nullptr, -1, -1, nullptr },
    };

    B2dSetHookList(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), recs);
}

// FF3 0x00206598

static void MenuFileListHookRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The "new" stickers (item 0x406): FUSEN_TM2 + language, four 96 x 28
// sticky notes.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00206980
// callcheck: operator_new, MenuDrawTexCtor (new Menu_DrawTex)

static void MenuFileFusenTexInit(void)
{
    file_fusen_tex[0] = new Menu_DrawTex;
    file_fusen_tex[0]->Load(GetSysWrk()->GetLanguage() + FUSEN_TM2);
    file_fusen_tex[0]->direct = 1;
}

// FF3 0x002069f8

static void MenuFileFusenTexRelease(void)
{
    if (file_fusen_tex[0] != nullptr)
    {
        delete file_fusen_tex[0];
    }
    file_fusen_tex[0] = nullptr;
}

// FF3 0x00206a30
// callcheck: MenuDrawTexIsSet (Menu_DrawTex::IsSet)

static int MenuFileFusenTexIsReady(void)
{
    if (file_fusen_tex[0]->IsSet() != 0 && file_fusen_tex[0]->IsReady() == 0)
    {
        return 0;
    }
    return 1;
}

// A sticker beside each new (unread) memo of the page; which of the four
// notes depends on the tab and the row.
// FF3 0x00206af8
// callcheck: MenuDrawTexDraw (Menu_DrawTex::Draw)

static int MenuFileNewHook(const B2D_HOOK_ARGS *args)
{
    const int    *rect = (const int *)args->t1;
    u_char        alpha;
    short         tab;
    int           top;
    int           base;
    int           i;
    int           row;
    short         memo;
    int           cell;
    Menu_DrawTex *tex;

    if ((int)args->stack[2] == 0)
    {
        return 0;
    }
    alpha = (u_char)args->stack[2];
    tab   = MenuFileCsrGetPos(MENU_FILE_CSR_TAB);
    top   = MenuFileCsrGetPageTop(MENU_FILE_CSR_ROW);
    base  = tab * 3 + tab / 4 * 2;
    for (i = 0; i < 9; i++)
    {
        row  = top + i;
        memo = MenuFileListGetMemo(tab, (short)row);
        if (memo == -1 || memo_wrk[memo].status != 1)
        {
            continue;
        }
        cell = (base + row) % 4;
        tex  = file_fusen_tex[cell / 4];
        tex->u      = 0;
        tex->v      = cell % 4 * 0x1c;
        tex->h      = 0x1c;
        tex->w      = 0x60;
        tex->alphar = BpGetBlendAlpha((u_int)args->stack[3]);
        tex->test   = MENU_DRAW_TEX_TEST_ALWAYS;
        tex->Draw(rect[0], rect[1] + i * MENU_FILE_ROW_STEP, 0, 0, 0x80, 0x80, 0x80, alpha, 0);
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00206a78

static void MenuFileNewHookInit(void)
{
    B2D_HOOK_REC recs[2] =
    {
        { MenuFileNewHook, 0, MENU_FILE_ITEM_NEW, nullptr },
        { nullptr, -1, -1, nullptr },
    };

    B2dSetHookList(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), recs);
}

// FF3 0x00206af0

static void MenuFileNewHookRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The tab stickers: items "R fsn1".."R fsn4" shown while the tab has a new
// memo.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00206cd0
// callcheck: MemoListHasNew (MEMO_LIST::HasNew)

static void MenuFileTabNewMain(void)
{
    int handle = BpCtlGetHandleByFileNo(GetMenuLangFileNo());
    int i;

    for (i = 0; i < MENU_FILE_TAB_MAX; i++)
    {
        if (file_tab_new_item[i] != nullptr)
        {
            B2D_ITEM *item = B2dCtlFindItemByName(handle, file_tab_new_item[i]);

            B2dSprSetDisp(item, file_tab_list[i] != nullptr && file_tab_list[i]->HasNew() != 0);
        }
    }
}

// FF3 0x00206d88

static void MenuFileTabNewInit(void)
{
    B2dSprSetDisp(B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_TAB_NEW_OFF), 0);
}

// FF3 0x00206dc0

static void MenuFileTabNewRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The category picture (item 0x400): CATEGORY_PIC_01..04 of the current
// tab, the picture and a strip of it turned on its side.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00206e80
// callcheck: MenuPicStep, MenuPicDraw, MenuPicDrawNext (MENU_PIC methods)

static int MenuFilePicHook(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    int        alpha = (int)args->stack[2];
    short      cat;

    if (alpha != 0 && file_pic != nullptr)
    {
        cat = MenuFileTabGetCategory(MenuFileCsrGetPos(MENU_FILE_CSR_TAB));
        if ((u_short)cat < MENU_FILE_TAB_MAX)
        {
            file_pic->pic = file_category_pic[cat];
            file_pic->Step();
            file_pic->scale = (float)alpha * (1.0f / 128.0f);  // sdata 0x3711b0
            file_pic->rot   = -90;
            file_pic->v     = 0xea;
            file_pic->u     = 0;
            file_pic->h     = 0x14;
            file_pic->w     = 0x91;
            file_pic->pri   = 0xa0;
            file_pic->z     = 0;
            file_pic->x     = (short)(rect[0] + 0xfc);
            file_pic->y     = (short)(rect[1] + 0xd3);
            file_pic->Draw();
            file_pic->DrawNext();
            file_pic->rot = 0;
            file_pic->v   = 0;
            file_pic->u   = 0;
            file_pic->h   = 0xea;
            file_pic->w   = 0xfe;
            file_pic->pri = 0xa0;
            file_pic->x   = (short)rect[0];
            file_pic->y   = (short)rect[1];
            file_pic->z   = 0;
            file_pic->Draw();
            file_pic->DrawNext();
        }
        SetDefaultGsDrawState();
    }
    return 0;
}

// FF3 0x00206dc8
// callcheck: operator_new, MenuPicCtor (new MENU_PIC)

static void MenuFilePicInit(void)
{
    B2D_ITEM_HOOK hook;

    file_pic = new MENU_PIC(0);
    file_pic->fade_len = 0;
    file_pic->enable   = 1;
    hook.func = MenuFilePicHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_PIC), &hook);
}

// FF3 0x00206e40
// callcheck: MenuPicDtor, operator_delete (delete)

static void MenuFilePicRelease(void)
{
    if (file_pic != nullptr)
    {
        delete file_pic;
        file_pic = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The memo-menu request
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00207010

void MemoMenuReqInit(void)
{
    memo_menu_req.memo = -1;
    memo_menu_req.link = 0;
    memo_menu_req.mode = MEMO_MENU_REQ_TAB;
}

// FF3 0x00207030

short MemoMenuReqGetMemo(void)
{
    return memo_menu_req.memo;
}

// FF3 0x00207040

int MemoMenuReqGetMode(void)
{
    return memo_menu_req.mode;
}

// FF3 0x00207050

int MemoMenuReqIsSet(void)
{
    return memo_menu_req.memo != -1;
}

// FF3 0x00207068

int MemoMenuReqIsRowSelect(void)
{
    return MemoMenuReqIsSet() != 0 && MemoMenuReqGetMode() == MEMO_MENU_REQ_ROW;
}

// FF3 0x002070a0

int MemoMenuReqGetLink(void)
{
    return memo_menu_req.link;
}

// FF3 0x002070b0
// callcheck: FUN_001a5e60 (FINDER_STATE_WRK method)

int MemoMenuIsEnable(int force)
{
    if (force == 0)
    {
        if (IsLockTblOn(0x12) != 0)     // the finder lock
        {
            return 0;
        }
        if (IsMenuEnable() == 0)
        {
            return 0;
        }
    }
    if (memo_flg.open_flg == 0 || PlyrIsFinderModeOff() == 0)
    {
        return 0;
    }
    if (GetFinderStateWrk()->FUN_001a5e60() > 0)
    {
        return 1;
    }
    return MemoGetOpenPageNum() > 0;
}

// A memo outside 0..MEMO_NUM - 1 or not found opens the menu as it was.
// FF3 0x00207148

int MemoMenuReq(short memo, int select, int link, int force)
{
    if (MemoMenuIsEnable(force) == 0)
    {
        return 0;
    }
    if (memo != -1 && (u_short)memo < MEMO_NUM && memo_wrk[memo].status != 0)
    {
        memo_menu_req.memo = memo;
        memo_menu_req.mode = select != 0 ? MEMO_MENU_REQ_ROW : MEMO_MENU_REQ_TAB;
    }
    memo_menu_req.link = link;
    GPhaseCtrlRequest(MENU_FILE_PHASE, link);
    return 1;
}

// FF3 0x00207200 (no callers)

int MemoDetailReq(short memo, int link)
{
    if (MemoMenuIsEnable(0) != 0 && (u_short)memo < MEMO_NUM && memo_wrk[memo].status != 0)
    {
        memo_menu_req.memo = memo;
        memo_menu_req.mode = MEMO_MENU_REQ_DETAIL;
        memo_menu_req.link = link;
        GPhaseCtrlRequest(MENU_FILE_DETAIL_PHASE, link);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 87 StoryMenu_FileFirst: the file menu's first visit. After 30
// frames (25 on PAL) message 99 opens in the message viewer (LayoutAnim
// type 1, MsgView mode 2) over the darkened menu (fade layer label 0xe);
// when it has closed, the fade layer comes back and the phase returns to
// the one it came from (62).
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00207290

int init_StoryMenu_FileFirst(void)
{
    return 1;
}

// FF3 0x00207298

int end_StoryMenu_FileFirst(void)
{
    return 1;
}

// The node's state_cnt is the step, state_wrk the frame count.
// FF3 0x002072a0
// callcheck: CmnFadeLayerTerm, CmnFadeLayerSwitch, CmnFadeLayerSetItem (capCtl methods)
// callcheck: GPhaseCtrlSetNext (GPHASE_CTRL::SetNext)

int one_StoryMenu_FileFirst(void)
{
    short wait;

    MenuFileListMain(1);
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        wait = (short)GetGPhaseCtrl()->leaf->state_wrk;
        if (GetPALMode() == 0 ? wait < 30 : wait < 25)
        {
            GetGPhaseCtrl()->leaf->state_wrk = (short)(wait + 1);
            return 0;
        }
        file_first_anim = LayoutAnimStart(1, 0);
        MsgViewSetMode(MSG_VIEW_MODE_2);
        MsgViewSetMsg(99, 0);
        GetCmnFadeLayer()->Switch(0xe, 0x1e);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        if (LayoutAnimGetState(file_first_anim) != 4)
        {
            return 0;
        }
        LayoutAnimReset(file_first_anim);
        GetCmnFadeLayer()->Term();
        GetCmnFadeLayer()->Switch(1, 0x1e);
        GetCmnFadeLayer()->SetItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_FADE);
        GetGPhaseCtrl()->SetNext(GetGPhaseCtrl()->from, 0);
        return 1;
    }
    return 0;
}
