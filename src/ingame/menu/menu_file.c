/* ==========================================================================
 *  ingame/menu/menu_file.c
 *
 *  Phase 86 StoryMenu_FileDetail; see menu_file.h. FF3 0x207490..0x208140.
 *
 *  The page lives in the file menu's layout (MEMO_PHF + language, label
 *  syousai_next in / syousai_back out) and draws through item hooks:
 *  0x422 the memo page's picture, 0x421 its text on the current text page,
 *  0x3f1 the window texture as a turning 3D panel, 0x425 "n / m". Page
 *  prev / next switch to the previous / next found memo when the page was
 *  opened from the file list (phase 62), OK turns the text page, cancel
 *  goes back. The file list keeps running under it (MenuFileListMain).
 *
 *  FF3 reads GetGPhaseCtrl through a gp-relative cached pointer; the calls
 *  go to the getter directly. Not written: FF3 0x00208120, the GPhaseCtrl
 *  singleton's atexit thunk (as in every file that includes the getter).
 * ======================================================================== */
#include "ingame/menu/menu_file.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, capCtl::Switch
#include "graphics/effect/effect_obj.h" // EFF_FLIP_PANEL, EffFlipPanel*
#include "graphics/graph2d/message.h"   // SetCurColorRGB, MsgSetPage, MsgPrintPageZ, MsgReset, MSG_COL_CURRENT
#include "graphics/motion/anicode.h"    // SetDefaultGsDrawState
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/memo/memo.h"           // memo_wrk, memo_dat, memo_page, MemoSetRead
#include "ingame/menu/menu_bg.h"        // MenuLayoutIsPlaying, B2dHookPrintMsgLine, B2dHookPrintNumber
#include "ingame/menu/menu_file_list.h" // MenuFileListMain, MenuFileGetRow, MenuFileStepMemo, MenuFileGetMemo, MenuFileGetPageNum, GetMenuLangFileNo
#include "ingame/menu/menu_csr.h"       // MenuCsr*
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/b2d/B2dCtl.h"          // B2D_HOOK_ARGS, B2dCtlFindItem, B2dCtlSetItemHook
#include "system/b2d/B2dSpr.h"          // B2dSprGetPartData
#include "system/b2d/BpCtl.h"           // BpCtlGetHandleByFileNo, BpPlayLabel, BpGetDrawHandle, BpSetBlendAlpha
#include "system/b2d/BpTex.h"           // BpTexGetTex0
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadRptPlaySe, CheckPadTrgPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────

#define MENU_FILE_PAGE_CSR  0x56        // the text page cursor (MenuCsr id)
#define MENU_FILE_PHASE     0x3e        // 62 StoryMenu_File, the file list

#define MENU_FILE_ITEM_FLIP     0x3f1   // the window: a turning panel
#define MENU_FILE_ITEM_TEXT     0x421
#define MENU_FILE_ITEM_PIC      0x422
#define MENU_FILE_ITEM_PAGE_NO  0x425

static short           file_detail_from;    // sbss gp+0x124: the phase the page opened from (and goes back to)
static MENU_PIC       *file_detail_pic;     // sbss gp+0x128
static EFF_FLIP_PANEL *file_detail_flip;    // sbss gp+0x12c

static void  MenuFilePageCsrInit(void);
static void  MenuFilePageCsrRelease(void);
static int   MenuFilePageCsrUpdate(void);
static void  MenuFilePageCsrReset(void);
static short MenuFilePageCsrGetPos(void);
static short MenuFilePageCsrSetPos(int pos);
static void  MenuFileDetailSetRead(void);

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00207718

static void MenuFileDetailListMain(void)
{
    MenuFileListMain(0);
}

// The phase to go to, -1 to stay.
// FF3 0x00207730

static short MenuFileDetailPad(void)
{
    short next = -1;
    int   row  = MenuFileGetRow();
    int   page = MenuFilePageCsrGetPos();
    int   dir;

    ReiPan_MenuMouseConfirm();          // PC: a click turns the page
    MenuFilePageCsrUpdate();
    if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_PREV], -1, 0) != 0 && file_detail_from == MENU_FILE_PHASE)
    {
        dir = -1;
    }
    else if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_NEXT], -1, 0) != 0 && file_detail_from == MENU_FILE_PHASE)
    {
        dir = 1;
    }
    else
    {
        if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0)
        {
            MenuFilePageCsrSetPos(MenuFilePageCsrGetPos() + 1);
        }
        else if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) != 0)
        {
            SndSePlay(0x1a9b0001, 100, 0x1000, 0, nullptr, 0);
            next = file_detail_from;
        }
        dir = 0;
    }
    if (dir != 0 && MenuFileStepMemo(dir) != row)
    {
        SndSePlay(0x1bc90000, 100, 0x1000, 0, nullptr, 0);
        MenuFilePageCsrReset();
        MenuFileDetailSetRead();
    }
    if (page != MenuFilePageCsrGetPos())
    {
        SndSePlay(0x1bc90000, 100, 0x1000, 0, nullptr, 0);
    }
    return next;
}

// FF3 0x002078a0 / 0x002078b8

static void MenuFileDetailPageCsrInit(void)
{
    MenuFilePageCsrInit();
}

static void MenuFileDetailPageCsrRelease(void)
{
    MenuFilePageCsrRelease();
}

// ──────────────────────────────────────────────────────────────────────
// The text page cursor: one item per text page of the memo page.
// FF3 0x002078d0

static void MenuFilePageCsrInit(void)
{
    int num = MenuFileGetPageNum();

    MenuCsrInit(MENU_FILE_PAGE_CSR, (short)(num == 0 ? 1 : num), 1, 2);
}

// FF3 0x00207910

static void MenuFilePageCsrRelease(void)
{
    MenuCsrRelease(MENU_FILE_PAGE_CSR);
}

// FF3 0x00207928

static int MenuFilePageCsrUpdate(void)
{
    MenuCsrUpdate(MENU_FILE_PAGE_CSR, 0);
    return MenuCsrIsMovedSe(MENU_FILE_PAGE_CSR, -1);
}

// Another memo: the cursor rebuilt for its page count.
// FF3 0x00207950

static void MenuFilePageCsrReset(void)
{
    MenuFilePageCsrRelease();
    MenuFilePageCsrInit();
}

// FF3 0x00207970

static short MenuFilePageCsrGetPos(void)
{
    return MenuCsrGetPos(MENU_FILE_PAGE_CSR);
}

// FF3 0x00207990

static short MenuFilePageCsrSetPos(int pos)
{
    MenuCsrSetPos(MENU_FILE_PAGE_CSR, (short)pos);
    return MenuCsrGetPos(MENU_FILE_PAGE_CSR);
}

// FF3 0x002079c0

static void MenuFileDetailSetRead(void)
{
    MemoSetRead(MenuFileGetMemo());
}

// The memo_page row of the selected memo: its last found page. Nothing in
// FF3 calls it; the hooks below carry the same expression.
// FF3 0x002079e0

short MenuFileGetMemoPage(void)
{
    return MemoGetCurrentPageIndex(MenuFileGetMemo());
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x422: the memo page's picture, at the item's alpha. A page without
// one (outside 0x750..0x7b3) shows blank.
// FF3 0x00207b60
// callcheck: MenuPicStep, MenuPicDraw, MenuPicDrawNext (MENU_PIC methods)

static int MenuFileDetailPicHook(const B2D_HOOK_ARGS *args)
{
    short row;
    int   pic;

    if (file_detail_pic == nullptr)
        return 0;
    row = MenuFileGetMemoPage();
    if (row == -1)
        return 0;

    file_detail_pic->scale = (float)(int)args->stack[2] * (1.0f / 128.0f);  // sdata 0x3711b8
    pic = memo_page[row].pic;
    file_detail_pic->pic = (u_int)(pic - 0x750) < 100 ? pic : -2;
    file_detail_pic->Step();
    file_detail_pic->Draw();
    file_detail_pic->DrawNext();
    SetDefaultGsDrawState();
    return 0;
}

// The picture at item 0x422's place (its parts 6 / 7), 512 x 512.
// FF3 0x00207a30
// callcheck: operator_new, MenuPicCtor (new MENU_PIC)

static void MenuFileDetailPicInit(void)
{
    B2D_ITEM     *item = B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_PIC);
    short         x    = (short)B2dSprGetPartData(item, B2D_PART_X);
    short         y    = (short)B2dSprGetPartData(item, B2D_PART_Y);
    B2D_ITEM_HOOK hook;

    file_detail_pic = new MENU_PIC(0);
    file_detail_pic->v      = 0;
    file_detail_pic->u      = 0;
    file_detail_pic->h      = 0x200;
    file_detail_pic->w      = 0x200;
    file_detail_pic->x      = x;
    file_detail_pic->y      = y;
    file_detail_pic->pri    = 0xa0;
    file_detail_pic->z      = 0;
    file_detail_pic->enable = 1;
    hook.func = MenuFileDetailPicHook;
    B2dCtlSetItemHook(item, &hook);
}

// FF3 0x00207b20
// callcheck: MenuPicDtor, operator_delete (delete)

static void MenuFileDetailPicRelease(void)
{
    if (file_detail_pic != nullptr)
    {
        delete file_detail_pic;
        file_detail_pic = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x421: the memo page's text, the cursor's text page, in the item's
// colour and alpha.
// FF3 0x00207ca0

static int MenuFileDetailTextHook(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;
    u_int      rgb  = (u_int)args->t3;
    short      row;
    short      text;
    short      page;

    row = MenuFileGetMemoPage();
    if (row == -1)
        return 0;

    text = memo_page[row].text;
    page = MenuFilePageCsrGetPos();
    if (text >= 0 && page >= 0)
    {
        SetCurColorRGB(rgb & 0xff, rgb >> 8 & 0xff, rgb >> 16 & 0xff);
        MsgSetPage((s_char)page);
        MsgPrintPageZ(0x57, text, rect[0], rect[1], MSG_COL_CURRENT, (int)args->stack[2], 0, 0, 3);
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00207c50

static void MenuFileDetailTextInit(void)
{
    B2D_ITEM_HOOK hook;

    hook.func = MenuFileDetailTextHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_TEXT), &hook);
}

// FF3 0x00207c98

static void MenuFileDetailTextRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x3f1 (a sprite): its texture rectangle (scaled by the item's 0x400
// = 1.0 scales) drawn as the flip panel, in the item's colour and alpha.
// FF3 0x00207eb0

static int MenuFileDetailFlipHook(const B2D_HOOK_ARGS *args)
{
    u_long tex0;
    u_int  rgb;
    int    uv[4];
    int    col[4];

    if (file_detail_flip != nullptr)
    {
        tex0   = BpTexGetTex0(BpGetDrawHandle(), (int)args->a0);
        rgb    = (u_int)args->stack[2];
        uv[0]  = (int)args->a1;
        uv[1]  = (int)args->a2;
        uv[2]  = (int)args->a3 * (int)args->t3 / 1024;
        uv[3]  = (int)args->t0 * (int)args->stack[0] / 1024;
        col[0] = rgb & 0xff;
        col[1] = rgb >> 8 & 0xff;
        col[2] = rgb >> 16 & 0xff;
        col[3] = (int)args->stack[1];
        BpSetBlendAlpha((u_int)args->stack[3]);
        EffFlipPanelStep(file_detail_flip);
        EffFlipPanelDraw(file_detail_flip, tex0, uv, col);
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00207da8
// callcheck: operator_new (new EFF_FLIP_PANEL)

static void MenuFileDetailFlipInit(void)
{
    B2D_ITEM_HOOK hook;

    file_detail_flip = new EFF_FLIP_PANEL;
    EffFlipPanelInit(file_detail_flip);
    file_detail_flip->_1c    = 0;
    file_detail_flip->ZOff   = -25.0f;  // sdata 0x3711bc
    file_detail_flip->ScaleX = 1.12f;   // sdata 0x3711c0
    file_detail_flip->ScaleY = 1.28f;   // sdata 0x3711c4
    hook.func = MenuFileDetailFlipHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_FLIP), &hook);
}

// FF3 0x00207e48
// callcheck: operator_delete (delete)

static void MenuFileDetailFlipRelease(void)
{
    if (file_detail_flip != nullptr)
    {
        EffFlipPanelCamRestore(file_detail_flip);
        delete file_detail_flip;
        file_detail_flip = nullptr;
    }
}

// FF3 0x00207e88

static void MenuFileDetailFlipStart(int dir)
{
    EffFlipPanelStart(file_detail_flip, dir, 5, 0x18, 0x1e);
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x425 (text): "n / m", the three parts one item width apart.
// FF3 0x00208020

static int MenuFileDetailPageNoHook(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    int        w     = (int)args->a1;
    u_int      rgb   = (u_int)args->t3;
    int        alpha = (int)args->stack[2];
    u_int      flags = (u_int)args->stack[3];
    int        pos[2];
    int        num;

    pos[0] = rect[0];
    pos[1] = rect[1];
    B2dHookPrintNumber(MenuFilePageCsrGetPos() + 1, 1, 1, pos, rgb, alpha, flags);
    pos[0] = rect[0] + w;
#ifdef REIPAN_SLPS_25544_CHS
    B2dHookPrintMsgLine(0x81, 6, 0, pos, rgb, alpha, flags);    // SLPS_255.44 0x202918
#else
    B2dHookPrintMsgLine(0x82, 6, 0, pos, rgb, alpha, flags);
#endif
    num = MenuFileGetPageNum();
    pos[0] = rect[0] + w * 2;
    B2dHookPrintNumber(num > 0 ? num : 1, 1, 1, pos, rgb, alpha, flags);
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00207fd0

static void MenuFileDetailPageNoInit(void)
{
    B2D_ITEM_HOOK hook;

    hook.func = MenuFileDetailPageNoHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), MENU_FILE_ITEM_PAGE_NO), &hook);
}

// FF3 0x00208018

static void MenuFileDetailPageNoRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Step 0 sets the page up and plays syousai_next (the flip panel turns in
// when it opens from the file list); step 1 waits for the layout.
// FF3 0x00207490
// callcheck: CmnFadeLayerSwitch (capCtl::Switch)

int init_StoryMenu_FileDetail(void)
{
    MenuFileDetailListMain();
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        file_detail_from = GetGPhaseCtrl()->from;
        MenuFileDetailPageCsrInit();
        MenuFileDetailPicInit();
        MenuFileDetailTextInit();
        MenuFileDetailFlipInit();
        MenuFileDetailPageNoInit();
        MenuFileDetailSetRead();
        BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "syousai_next");  // rodata 0x35c568
        if (file_detail_from == MENU_FILE_PHASE)
        {
            MenuFileDetailFlipStart(1);
        }
        GetCmnFadeLayer()->Switch(5, 0x1e);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        return MenuLayoutIsPlaying(GetMenuLangFileNo()) == 0;
    }
    return 1;
}

// FF3 0x002076d0

int one_StoryMenu_FileDetail(void)
{
    short next;

    MenuFileDetailListMain();
    next = MenuFileDetailPad();
    if (next != -1)
    {
        GPhaseCtrlRequest(next, 0);
    }
    return next != -1;
}

// Step 0 plays syousai_back; step 1 waits for it, then releases the page.
// FF3 0x002075c8
// callcheck: CmnFadeLayerSwitch (capCtl::Switch)

int end_StoryMenu_FileDetail(void)
{
    MenuFileDetailListMain();
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        BpPlayLabel(BpCtlGetHandleByFileNo(GetMenuLangFileNo()), "syousai_back");  // rodata 0x35c578
        GetCmnFadeLayer()->Switch(1, 0x1e);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        if (MenuLayoutIsPlaying(GetMenuLangFileNo()) != 0)
        {
            return 0;
        }
        MenuFileDetailPicRelease();
        MenuFileDetailTextRelease();
        MenuFileDetailFlipRelease();
        MenuFileDetailPageNoRelease();
        MenuFileDetailPageCsrRelease();
        MsgReset();
        return 1;
    }
    return 1;
}
