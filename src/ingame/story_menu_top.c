/* ==========================================================================
 *  ingame/story_menu_top.c
 *
 *  Phase 57 StoryMenu_Top; see story_menu_top.h.
 *  FF3 0x20b590..0x20c400.
 *
 *  init (two steps): MENUTOP_PHF, the HUD layout and the menu window
 *  (layout type 8) start loading; once all three are ready the HUD plays
 *  label 0 and the layout "menu<n>-in" when the menu just opened (the
 *  change came from Story_Menu 37), else HUD label 2 and "menu<n>-back"
 *  (back from a row's page), n the number of rows. The seven item labels
 *  are hidden; the rows of the story menu list then take the places of
 *  the first n labels in table order, so a missing row (no camera, no
 *  memo) closes the gap. one runs the row cursor, the help line and the
 *  two cursor sprites; end plays "menu<n>_cancel" (also on the way into a
 *  row) and releases everything once it and the HUD have played out.
 *
 *  FF3 reads the singletons through gp-relative cached pointers; nothing
 *  depends on the cache, so calls go to the getters directly.
 *
 *  Not written: FF3 0x0020c3e0 (StoryMenuTopAtexitGPhaseCtrl), the
 *  GPhaseCtrl singleton's atexit thunk (as in every file that includes the
 *  getter).
 * ======================================================================== */
#include "ingame/story_menu_top.h"

#include <stdio.h>                      // sprintf

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, capCtl
#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, MenuWdwSetKeepOpen, MenuWdwSetMsg
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/ingame_wrk.h"          // ingame_wrk, IngameWrkGetPoint
#include "ingame/menu/anim_2d.h"        // LayoutAnim*, LayoutMsgSetKeepOpen, HUD_LAYOUT
#include "ingame/menu/menu_bg.h"        // MenuBgStop
#include "ingame/menu/menu_csr_obj.h"   // MenuCsrObj*
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "ingame/story_menu.h"          // StoryMenu*, IsLockTblOn
#include "main/gphase.h"                // GetGPhaseCtrl
#include "graphics/graph2d/message.h"
#include "system/b2d/B2dBase.h"         // B2dNumDrawFont
#include "system/b2d/B2dCtl.h"          // B2D_HOOK_ARGS, B2D_ITEM, B2dCtlFindItemByName
#include "system/b2d/B2dSpr.h"          // B2dSprSetDisp, B2dSprSetPos, B2dSprFreezePartByName, B2dSprGetPartData
#include "system/b2d/BpCtl.h"           // BpCtl*, BpPlayLabel*, BpGetDrawHandle, BpSetBlendAlpha
#include "system/eeiop/cd_file_dat.h"   // MENUTOP_PHF
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

#define TOP_SE_MOVE         0x1a9b0000
#define TOP_SE_CANCEL       0x1a9b0001
#define TOP_SE_BUZZER       0x1a9b0002
#define TOP_SE_OK           0x1a9b0003

#define TOP_PAD_START       0x800       // START leaves the menu like cancel
#define TOP_LOCK_MAP        0x1b        // lock id: the map row cannot be opened
#define TOP_ITEM_ALPHA      0x3ff       // layout item: the fade layer's and the window's alpha
#define TOP_MSG_HELP        0x6e        // MenuWdwSetMsg kind of the rows' help lines
#define TOP_ROW_MAX         7           // StoryMenuGetItemNum

#define TOP_CSL_DX          (-15)       // the cursor sprites, from the row label's position
#define TOP_CSL1_DY         (-10)

#define YESNO_MODE_MOVE     2

// sdata 0x37363c / 0x373640
static HUD_LAYOUT *story_top_hud;
static int         story_top_wdw;       // LayoutAnimStart slot of the menu window

// bss 0x3f6090: the label of each row, in row order (init).
static const char *story_top_row_label[TOP_ROW_MAX];

static int StoryMenuTopFile(void)
{
    return GetSysWrk()->GetLanguage() + MENUTOP_PHF;
}

static int StoryMenuTopHandle(void)
{
    return BpCtlGetHandleByFileNo(StoryMenuTopFile());
}

// ──────────────────────────────────────────────────────────────────────
// Cursor helpers nothing in the ROM calls.
// ──────────────────────────────────────────────────────────────────────

// A one-column list of num items: the cursor on item pos (pos < 0: the
// last, pos >= num: the first), its row in the view kept where it can be,
// and no empty rows below the last item.
// FF3 0x0020b590

void MenuCsrObjSetListPos(MENU_CSR_WRK *wrk, short pos, short num)
{
    short size;
    short row;

    size = wrk->vy.size;
    row = MenuCsrObjGetViewPosY(wrk);
    if (num < size)
        size = num;
    if (pos < 0)
    {
        pos = (short)(num - 1);
        row = (short)(size - 1);
    }
    if (num <= pos)
    {
        pos = 0;
        row = 0;
    }
    else if (pos < row)
    {
        row = pos;
    }
    if (num - pos < size - row)
        row = (short)(size - (num - pos));
    MenuCsrObjSetPos(wrk, pos, 0, row, -1, -1);
}

// FF3 0x0020b658

MENU_YESNO *MenuYesNoCreate(int mode, int arg, short pos)
{
    MENU_YESNO *obj;

    obj = (MENU_YESNO *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(MENU_YESNO), 0, 0, 0);
    if (obj == nullptr)
        return nullptr;
    obj->mode = mode;
    obj->arg = arg;
    MenuCsrObjInit(&obj->csr, 2, 1, 0);
    MenuCsrObjSetPos(&obj->csr, pos, -1, -1, -1, -1);
    return obj;
}

// FF3 0x0020b710

void MenuYesNoDelete(MENU_YESNO *obj)
{
    if (obj == nullptr)
        return;
    MenuCsrObjRelease(&obj->csr);
    heapCtrlFree(GetSystemHeapWrkP(), obj, 0, 0, 0);
}

// OK or cancel decides; cancel also puts the cursor on 1. The SEs are the
// other way round from the menus' (OK plays 0x1a9b0001, cancel
// 0x1a9b0003), as in the ROM.
// FF3 0x0020b770

int MenuYesNoUpdate(MENU_YESNO *obj)
{
    int decided = 0;

    if (obj == nullptr)
        return 0;
    if (obj->mode == YESNO_MODE_MOVE)
    {
        MenuCsrObjUpdate(&obj->csr, 0);
        MenuCsrObjIsMovedSe(&obj->csr, TOP_SE_MOVE);
    }
    decided = CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0;
    if (decided)
        SndSePlay(TOP_SE_CANCEL, 100, 0x1000, 0, nullptr, 0);
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) != 0)
    {
        decided = 1;
        SndSePlay(TOP_SE_OK, 100, 0x1000, 0, nullptr, 0);
        MenuCsrObjSetPos(&obj->csr, 1, -1, -1, -1, -1);
    }
    return decided;
}

// ──────────────────────────────────────────────────────────────────────
// The layout's number hooks (rodata 0x35c5f0): the play time and the
// points in the "R NUM_0" digits (sdata 0x371240).
// ──────────────────────────────────────────────────────────────────────

// Item 0x3e9: the hours, one digit below 10.
// FF3 0x0020b878

static int StoryMenuTopHourHook(const B2D_HOOK_ARGS *args)
{
    const int *xy = (const int *)args->t1;
    int digits = ingame_wrk.play_time.hour < 10 ? 1 : 2;

    BpSetBlendAlpha((u_int)args->stack[3]);
    B2dNumDrawFont(ingame_wrk.play_time.hour, BpGetDrawHandle(), "R NUM_0", xy[4], xy[5],
                   (u_char)args->stack[2], digits);
    return 0;
}

// Item 0x3f0: the minutes.
// FF3 0x0020b8f8

static int StoryMenuTopMinHook(const B2D_HOOK_ARGS *args)
{
    const int *xy = (const int *)args->t1;

    BpSetBlendAlpha((u_int)args->stack[3]);
    B2dNumDrawFont(ingame_wrk.play_time.min, BpGetDrawHandle(), "R NUM_0", xy[4], xy[5],
                   (u_char)args->stack[2], 2);
    return 0;
}

// Item 0x3f1: the seconds.
// FF3 0x0020b958

static int StoryMenuTopSecHook(const B2D_HOOK_ARGS *args)
{
    const int *xy = (const int *)args->t1;

    BpSetBlendAlpha((u_int)args->stack[3]);
    B2dNumDrawFont(ingame_wrk.play_time.sec, BpGetDrawHandle(), "R NUM_0", xy[4], xy[5],
                   (u_char)args->stack[2], 2);
    return 0;
}

// Item 0x3fb: the points.
// FF3 0x0020b9b8

static int StoryMenuTopPointHook(const B2D_HOOK_ARGS *args)
{
    const int *xy = (const int *)args->t1;

    BpSetBlendAlpha((u_int)args->stack[3]);
    B2dNumDrawFont((int)IngameWrkGetPoint(), BpGetDrawHandle(), "R NUM_0", xy[4], xy[5],
                   (u_char)args->stack[2], 0);
    return 0;
}

#ifdef REIPAN_SLPS_25544_CHS
static int StoryMenuTopHourLabel(const B2D_HOOK_ARGS *args)
{
    // "时" from the disc's original message font.
    static u_char text[] = {0x91, MSG_CODE_END};
    SetCurColorRGB(0xaa, 0x98, 0x68);
    PrintMsg_ArrangeOneLine(text, (int)args->t1 + 4, (int)args->t2 - 1,
                           MSG_COL_CURRENT, (int)args->stack[1], 4, 0, 0, 0);
    SetDefaultGsDrawState();
    return 0;
}
#endif

// FF3 copies the records to the stack before B2dSetHookList.
static const B2D_HOOK_REC story_top_hooks[] =
{
    { StoryMenuTopHourHook,  0, 0x3e9, nullptr },
    { StoryMenuTopMinHook,   0, 0x3f0, nullptr },
    { StoryMenuTopSecHook,   0, 0x3f1, nullptr },
    { StoryMenuTopPointHook, 0, 0x3fb, nullptr },
#ifdef REIPAN_SLPS_25544_CHS
    { StoryMenuTopHourLabel, 0, 0, "R time" },
#endif
    { nullptr,              -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// The menu window (layout type 8): held until the menu is ready.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0020ba20

static void StoryMenuTopWdwOpen(void)
{
    story_top_wdw = LayoutAnimStart(8, 0);
    LayoutAnimSetFlag(story_top_wdw, LAYOUT_ANIM_WAIT | LAYOUT_ANIM_LABEL_LAYOUT);
    MenuWdwSetKeepOpen(1);
}

// FF3 0x0020ba58

static int StoryMenuTopWdwIsReady(void)
{
    return LayoutAnimIsDataReady(story_top_wdw);
}

// FF3 0x0020ba70

static void StoryMenuTopWdwShow(int handle, int id)
{
    LayoutAnimClearFlag(story_top_wdw, LAYOUT_ANIM_WAIT);
    if (handle >= 0)
        LayoutAnimSetAlphaItemById(story_top_wdw, handle, id);
}

// The ROM clears LayoutMsg's keep-open here, not the MenuWdw one
// StoryMenuTopWdwOpen set (kept; the row pages set their own).
// FF3 0x0020bad0

static void StoryMenuTopWdwClose(void)
{
    LayoutMsgSetKeepOpen(0);
    LayoutAnimSetAlphaItem(story_top_wdw, nullptr);
    LayoutAnimReset(story_top_wdw);
}

// FF3 0x0020bb00

static void StoryMenuTopSetHelp(int row)
{
    MenuWdwSetMsg(TOP_MSG_HELP, StoryMenuGetHelpMsg(row));
}

// FF3 0x0020bb28

static void StoryMenuTopSprSetPos(int handle, const char *name, int x, int y)
{
    B2dSprSetPos(B2dCtlFindItemByName(handle, name), x, y);
}

// The two cursor sprites beside the cursor row's label.
// FF3 0x0020bb68
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (StoryMenuTopHandle)

static void StoryMenuTopCslUpdate(void)
{
    B2D_ITEM *item;
    int       handle;
    int       row;
    int       x;
    int       y;

    row = StoryMenuGetCsrRow();
    handle = StoryMenuTopHandle();
    item = B2dCtlFindItemByName(handle, story_top_row_label[row]);
    if (item == nullptr)
        return;                         // PC: FF3 reads the item through the nullptr
    y = item->part[B2D_PART_Y].value;
    x = item->part[B2D_PART_X].value + TOP_CSL_DX;
    StoryMenuTopSprSetPos(handle, "R csl_on1", x, y + TOP_CSL1_DY);    // rodata 0x35c5d0
    StoryMenuTopSprSetPos(handle, "R csl_on2", x, y);                  // rodata 0x35c5e0
}

// OK opens the cursor row's phase (the map row buzzes while lock 0x1b is
// on), cancel or START stops the backdrop and goes back to where the menu
// was opened from; -1 otherwise.
// FF3 0x0020bc20

static short StoryMenuTopMain(void)
{
    short row;

    row = StoryMenuGetCsrRow();
    StoryMenuCsrMovedSe();
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0)
    {
        if (IsLockTblOn(TOP_LOCK_MAP) == 0 || row != 0)
        {
            SndSePlay(TOP_SE_OK, 100, 0x1000, 0, nullptr, 0);
            return StoryMenuGetRowPhase(row);
        }
        SndSePlay(TOP_SE_BUZZER, 100, 0x1000, 0, nullptr, 0);
        return -1;
    }
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], TOP_SE_CANCEL, 0) != 0
        || CheckPadTrgPlaySe(TOP_PAD_START, TOP_SE_CANCEL, 0) != 0)
    {
        MenuBgStop();
        return StoryMenuGetRetPhase();
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 57 StoryMenu_Top
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0020bd28
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (StoryMenuTopFile / StoryMenuTopHandle)
// callcheck: operator_new, HudLayoutInit (new HUD_LAYOUT)
// callcheck: CmnFadeLayerUpdate, CmnFadeLayerSetItem, CmnFadeLayerShow (capCtl::Update / SetItem / Show)

int init_StoryMenu_Top(void)
{
    char        label[0x100];           // FF3: 0x100 bytes of stack
    int         pos[TOP_ROW_MAX][2];    // the first labels' x / y
    const char *name;
    B2D_ITEM   *item;
    int         handle;
    int         from_menu;
    int         num;
    int         i;

    LayoutAnimMain();
    BpCtlMain();
    GetCmnFadeLayer()->Update();
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        BpCtlLoadReq(StoryMenuTopFile(), nullptr);
        story_top_hud = new HUD_LAYOUT(0);
        StoryMenuTopWdwOpen();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        if (BpCtlIsDataReady(StoryMenuTopHandle()) == 0)
            return 0;
        if (StoryMenuTopWdwIsReady() == 0)
            return 0;
        if (story_top_hud->IsReady() == 0)
            return 0;

        from_menu = GetGPhaseCtrl()->from == 0x25;     // 37 Story_Menu: the menu just opened
        story_top_hud->Play(from_menu ? 0 : 2);
        num = StoryMenuGetRowNum();
        if (num > TOP_ROW_MAX)
            num = TOP_ROW_MAX;          // PC: the list holds at most the seven items
        sprintf(label, from_menu ? "menu%d-in" : "menu%d-back", num);  // rodata 0x35c650 / 0x35c640
        BpPlayLabel(StoryMenuTopHandle(), label);

        handle = StoryMenuTopHandle();
        StoryMenuTopWdwShow(handle, TOP_ITEM_ALPHA);
        GetCmnFadeLayer()->SetItem(handle, TOP_ITEM_ALPHA);
        GetCmnFadeLayer()->Show(0);

        for (i = 0; i < StoryMenuGetItemNum(); i++)
        {
            name = StoryMenuGetItemLabel(i);
            B2dSprSetDisp(B2dCtlFindItemByName(handle, name), 0);
            B2dSprFreezePartByName(handle, name, B2D_PART_X, 0);
            B2dSprFreezePartByName(handle, name, B2D_PART_Y, 0);
        }
        for (i = 0; i < num; i++)
        {
            item = B2dCtlFindItemByName(handle, StoryMenuGetItemLabel(i));
            pos[i][0] = B2dSprGetPartData(item, B2D_PART_X);
            pos[i][1] = B2dSprGetPartData(item, B2D_PART_Y);
        }
        for (i = 0; i < num; i++)
        {
            story_top_row_label[i] = StoryMenuGetItemLabelById(StoryMenuGetRowId((short)i));
            item = B2dCtlFindItemByName(handle, story_top_row_label[i]);
            B2dSprSetDisp(item, 1);
            B2dSprSetPos(item, pos[i][0], pos[i][1]);
        }
        B2dSprFreezePartByName(handle, "R csl_on1", B2D_PART_X, 0);
        B2dSprFreezePartByName(handle, "R csl_on1", B2D_PART_Y, 0);
        B2dSprFreezePartByName(handle, "R csl_on2", B2D_PART_X, 0);
        B2dSprFreezePartByName(handle, "R csl_on2", B2D_PART_Y, 0);
        B2dSetHookList(handle, story_top_hooks);
        return 1;
    }
    return 1;
}

// FF3 0x0020c340
// callcheck: CmnFadeLayerUpdate (capCtl::Update), HudLayoutMain (HUD_LAYOUT::Main)

int one_StoryMenu_Top(void)
{
    short next;

    // PC: the mouse. Each row's label is an item of the layout.
    for (int i = 0, num = StoryMenuGetRowNum(); i < num && i < TOP_ROW_MAX; i++)
        MenuMouseCsrName(STORY_MENU_CSR, i, StoryMenuTopHandle(), story_top_row_label[i]);
    StoryMenuCsrUpdate();
    StoryMenuTopSetHelp(StoryMenuGetCsrRow());
    StoryMenuTopCslUpdate();
    story_top_hud->Main();
    LayoutAnimMain();
    BpCtlMain();
    GetCmnFadeLayer()->Update();
    next = StoryMenuTopMain();
    if (next != -1)
        GPhaseCtrlRequest(next, 0);
    return next != -1;
}

// FF3 0x0020c1a8
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (StoryMenuTopHandle)
// callcheck: CmnFadeLayerUpdate, CmnFadeLayerSetItemHandle (capCtl::Update / SetItemHandle)
// callcheck: HudLayoutMain, HudLayoutClose, HudLayoutRelease, operator_delete (HUD_LAYOUT::Main / Close, delete)

int end_StoryMenu_Top(void)
{
    char label[0x100];
    int  handle;

    handle = StoryMenuTopHandle();
    if (story_top_hud != nullptr)
        story_top_hud->Main();
    LayoutAnimMain();
    BpCtlMain();
    GetCmnFadeLayer()->Update();
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        sprintf(label, "menu%d_cancel", StoryMenuGetRowNum());     // rodata 0x35c660
        BpPlayLabelEx(handle, label, 0);
        story_top_hud->Close();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        if (B2dPartIsPlaying(B2dCtlGetLongestPart(handle)) != 0)
            return 0;
        if (story_top_hud->state != HUD_LAYOUT_DONE)
            return 0;
        delete story_top_hud;
        story_top_hud = nullptr;
        StoryMenuTopWdwClose();
        BpCtlRelease(StoryMenuTopHandle());
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        return 1;
    }
    return 0;
}
