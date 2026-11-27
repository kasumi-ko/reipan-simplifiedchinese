/* ==========================================================================
 *  ingame/menu/start_menu.c
 *
 *  FF3's start menu, FF3 0x1a4be0..0x1aa9a8 (FF3-only; see start_menu.h).
 *  The whole range is written, in FF3's four files: the bonus shop (phases
 *  81 / 109 / 110); the two save works the menu reads -- the clear counts
 *  (0x41e490) and the unlock slots (0x424b10) -- with the unlock rules, the
 *  gallery (82 / 111 / 112 / 113) and Story_Opening (56); StartMenu_Main
 *  (55); the extra-mission menu (83 / 114 / 119 / 115).
 *
 *  FF3 reads the singletons through gp-relative cached pointers; nothing
 *  depends on the cache, so calls go to the getters directly.
 * ======================================================================== */
#include "ingame/menu/start_menu.h"

#include "common/WindowMsg.h"           // SelWin*
#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, CmnFadeLayerIsReady, capCtl::Update
#include "graphics/graph2d/g2d_draw.h"  // SQAR_DAT, CopySqrDToSqr, DispSqrD
#include "graphics/graph2d/message.h"   // SetCurColorRGB, PrintNumber_N
#include "graphics/gs_reg.h"            // SetGsRegCached
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, GetLabelSuffixType
#include "ingame/camera/finder_camera.h"    // FINDER_MODE_WRK, mission lists
#include "ingame/story_menu_photo.h"        // DocFlagSet
#include "ingame/ingame.h"              // PHASE12_PARAM, Phase12ParamSet
#include "common/cmn_time.h"           // PlayTimeToHms
#include "ingame/ingame_wrk.h"          // TitleMenuItemOpenSet, title_start_flg, IngameWrk*Point
#include "ingame/menu/anim_2d.h"        // LayoutAnimMain
#include "ingame/menu/menu_csr.h"
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/menu/start_menu_top.h"  // StartMenuSetupSetHooks, StartMenu{Top,Diff}CsrFilter
#include "ingame/mission/mission_list.h" // Mission*, FINDER_BANK_ENTRY
#include "ingame/mission/mission_mode.h" // FINDER_MODE_WRK
#include "ingame/photo/finder_photo.h"  // GetLensWrk
#include "ingame/photo/fndr_parts.h"    // LensAddFunc, LensAddHave
#include "ingame/movie_room_menu/prg/movie_projecter.h"  // MovieReqPlay, MovieReqSetFilmMode
#include "ingame/mission/ExtraMissionShelt.h"  // CExtraMissionShelt
#include "ingame/story_main.h"          // StoryStartSetStep
#include "ingame/sound/snd_ctrl.h"      // SndSePlay, SndBgmPlay, SndBgmStop
#include "main/gphase.h"
#include "main/gphase_tree.h"           // GPHASE_TREE::Reset
#include "outgame/outgame_bg.h"         // OutgameBg*
#include "outgame/option.h"             // CmnFadeLayer*Item, LayoutItemAddPos, PrintMsgPos, CsrMoveUpDownSe
#include "system/b2d/B2dCtl.h"
#include "system/b2d/B2dSpr.h"
#include "system/b2d/BpCtl.h"
#include "system/b2d/ScrollBar.h"       // B2d_ScrollBar1
#include "system/eeiop/cd_file_dat.h"   // SETUP_PHF, SETUP_BG_PHF, MISSION_TOP_PHF, ...
#include "system/os/eecdvd.h"           // IsLoadEndAll
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage, GetPALMode
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPad*PlaySe
#include "graphics/graph2d/fade.h"
#include "ingame/item/prg/item.h"      // ItemSetNum
#include "ingame/item/prg/soul_list.h" // SoulListBonusIsReached
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────





// The first and last slot of each unlock group (rodata 0x358540).
static const int finder_slot_group[6][2] =                  /* rodata 0x358540 */
{
    { 0, 9 }, { 10, 23 }, { 24, 24 }, { 25, 25 }, { 26, 28 }, { 29, 32 },
};

// ══════════════════════════════════════════════════════════════════════
// The bonus shop (phases 81 StartMenu_Option, 109 OptionMain, 110
// OptionSave), FF3 0x1a4be0..0x1a5d68: the 33 unlock slots listed six rows
// at a time with their names (message 0x1b), prices and the player's
// points; a slot for sale (1) is bought with points and becomes 2.
// ══════════════════════════════════════════════════════════════════════

// One shop entry (rodata 0x358438): its preview group and its price.
struct START_MENU_BONUS
{
    /* 0x00 */ int group;           // preview item shown at the cursor (StartMenuOptionDispCsr)
    /* 0x04 */ int cost;            // points
};
static_assert(sizeof(START_MENU_BONUS) == 0x8, "START_MENU_BONUS is 0x8 bytes");

#ifdef REIPAN_SLPS_25544_CHS
static constexpr int SM_SHOP_PROMO_MOVIE = 26;
static constexpr int SM_SHOP_ENTRIES = FINDER_SLOT_MAX + 1;
#else
static constexpr int SM_SHOP_ENTRIES = FINDER_SLOT_MAX;
#endif

static const START_MENU_BONUS start_menu_bonus[SM_SHOP_ENTRIES] =     /* rodata 0x358438 */
{
    { 0,  5000 }, { 0, 10000 }, { 0, 20000 }, { 0, 30000 },
    { 1,  5000 }, { 1, 10000 }, { 1, 20000 }, { 1, 30000 },
    { 2, 10000 }, { 2, 20000 },
    { 0,  3000 }, { 0,  6000 }, { 0,  9000 }, { 0, 12000 },
    { 1,  3000 }, { 1,  6000 }, { 1,  9000 }, { 1, 12000 },
    { 2,  3000 }, { 2,  6000 }, { 2,  9000 }, { 2, 12000 },
    { 3,  3000 }, { 3,  9000 },
    { 4,  5000 }, { 4,  5000 },
#ifdef REIPAN_SLPS_25544_CHS
    { 4,  5000 },    // SLPS_255.44 0x34f978
#endif
    { 5, 30000 }, { 5, 30000 }, { 5, 15000 }, { 5,  5000 }, { 5, 10000 },
    { 5, 100000 }, { 5, 100000 },
};

#define SM_SHOP_ROWS        6       // rows on screen
#define SM_SHOP_ROW_DY      0x22    // pixels per row
#define SM_SHOP_MSG         0x1b    // entry names (no * 2) and descriptions (no * 2 + 1)
#define SM_SHOP_MSG_SKIP    0x19    // entries after this skip one message pair
#define SM_SHOP_CSR_ITEM    0x416   // cursor bar
#define SM_SHOP_WIN_ITEM    0x42b   // the message window's / fade layer's item
#define SM_SHOP_PROMPT_MSG  0x6b    // phase 12 messages of the shop

// The menu SEs (the title menu's too).
#define SM_SE_MOVE          0x1a9b0000
#define SM_SE_CANCEL        0x1a9b0001
#define SM_SE_BUZZ          0x1a9b0002
#define SM_SE_OK            0x1a9b0003
#define SM_SE_PAGE          0x1a9b0005

static int             start_menu_option_bought;    // sbss 0x37314c (gp-0x3a4): something was bought this visit
static B2d_ScrollBar1 *start_menu_option_bar;       // sbss 0x373150 (gp-0x3a0)

static int StartMenuShopFile(void)
{
    return GetSysWrk()->GetLanguage() + START_MENU_SHOP_FILE;
}

static int StartMenuShopHandle(void)
{
    return BpCtlGetHandleByFileNo(StartMenuShopFile());
}

static int StartMenuShopSlot(int row)
{
    if ((unsigned)row >= SM_SHOP_ENTRIES)
        return 0;
#ifdef REIPAN_SLPS_25544_CHS
    const unsigned state = row == SM_SHOP_PROMO_MOVIE ? GetFinderStateWrk()->PromoMovieState() :
        GetFinderSlotWrk()->slot[row - (row > SM_SHOP_PROMO_MOVIE)];
#else
    const unsigned state = GetFinderSlotWrk()->slot[row];
#endif
    return state <= 2 ? state : 0;
}

// ──────────────────────────────────────────────────────────────────────
// The cursor bar on the cursor's row, and the preview picture of the
// cursor entry's group (one of six items, the others hidden), all pinned.
// FF3 0x001a4be0
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuShopHandle)

static void StartMenuOptionDispCsr(void)
{
    static const short preview[SM_SHOP_ROWS] = { 0x3ee, 0x405, 0x40a, 0x409, 0x40b, 0x40c };   // built on the stack
    int       handle;
    int       row;
    int       pos;
    int       group;
    int       i;
    B2D_ITEM *item;

    handle = StartMenuShopHandle();
    row = MenuCsrGetPosY(START_MENU_CSR_SHOP);
    pos = MenuCsrGetPos(START_MENU_CSR_SHOP);
    item = B2dCtlFindItem(handle, SM_SHOP_CSR_ITEM);
    B2dSprSetPos(item, 0, row * SM_SHOP_ROW_DY);
    B2dSprFreezePart(handle, SM_SHOP_CSR_ITEM, B2D_PART_X, 0);
    B2dSprFreezePart(handle, SM_SHOP_CSR_ITEM, B2D_PART_Y, 0);

    if ((unsigned)pos >= SM_SHOP_ENTRIES)
        return;
    group = start_menu_bonus[pos].group;
    for (i = 0; i < SM_SHOP_ROWS; i++) {
        item = B2dCtlFindItem(handle, preview[i]);
        B2dSprSetDisp(item, i == group);
        B2dSprSetPos(item, 8, 0);
        B2dSprFreezePart(handle, preview[i], B2D_PART_X, 0);
        B2dSprFreezePart(handle, preview[i], B2D_PART_Y, 0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Sprite hook of the row pieces, drawn once per visible row: 0x3f1 / 0x3f3
// / 0x401 on the cursor's row, 0x3f2 / 0x3f4 / 0x402 on the others. The
// lock marks (0x3f1 / 0x3f2) show on locked slots only, the bought marks
// (0x3f3 / 0x3f4) on bought ones; 0x401 / 0x402 are the row backgrounds.
// FF3 0x001a4d70

static int StartMenuOptionDrawRowSpr(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw;
    int           id;
    int           top;
    int           row;
    int           csr_kind;
    int           v;
    int           i;

    draw = *args;
    id = B2dGetDrawItem()->id;
    top = MenuCsrGetTopY(START_MENU_CSR_SHOP);
    row = MenuCsrGetPosY(START_MENU_CSR_SHOP);
    csr_kind = id == 0x3f1 || id == 0x3f3 || id == 0x401;

    for (i = 0; i < SM_SHOP_ROWS; i++, draw.t2 += SM_SHOP_ROW_DY) {
        v = StartMenuShopSlot(top + i);
        if (csr_kind != (i == row)) {
            continue;
        }
        if ((u_int)(id - 0x3f1) < 2 && v != 0) {
            continue;
        }
        if ((u_int)(id - 0x3f3) < 2 && v != 2) {
            continue;
        }
        B2dDrawSpriteDirect(&draw);
    }
    return 0;
}

// The cursor's row warm yellow, the others dark grey.
static void StartMenuOptionSetRowColor(int is_csr)
{
    if (is_csr) {
        SetCurColorRGB(0xf1, 0xd6, 0x9b);
    } else {
        SetCurColorRGB(0x2a, 0x2c, 0x3a);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x419: the six visible entries' names.
// FF3 0x001a4f00 (no Ghidra function before 2026-10-14: inside 0x1a4d70)
// callcheck: GetLanguage, GetSysWrk, SetCurColorRGB (StartMenuShopFile, StartMenuOptionSetRowColor)

static int StartMenuOptionDrawName(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         top;
    int         row;
    int         type;
    int         no;
    int         i;

    top = MenuCsrGetTopY(START_MENU_CSR_SHOP);
    row = MenuCsrGetPosY(START_MENU_CSR_SHOP);
    for (i = 0; i < SM_SHOP_ROWS; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * SM_SHOP_ROW_DY);
        StartMenuOptionSetRowColor(i == row);
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, StartMenuShopFile(), xy);
        no = top + i;
#ifndef REIPAN_SLPS_25544_CHS
        if (no > SM_SHOP_MSG_SKIP) {
            no++;
        }
#endif
        PrintMsgPos(SM_SHOP_MSG, no * 2, pos, 0x2d, alpha, 0, type);
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x41f: the six visible entries' prices.
// FF3 0x001a5048 (no Ghidra function before 2026-10-14: inside 0x1a4d70)
// callcheck: SetCurColorRGB (StartMenuOptionSetRowColor)

static int StartMenuOptionDrawCost(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    int        top;
    int        row;
    int        i;

    top = MenuCsrGetTopY(START_MENU_CSR_SHOP);
    row = MenuCsrGetPosY(START_MENU_CSR_SHOP);
    for (i = 0; i < SM_SHOP_ROWS && (unsigned)(top + i) < SM_SHOP_ENTRIES; i++) {
        StartMenuOptionSetRowColor(i == row);
        PrintNumber_N(start_menu_bonus[top + i].cost, 6, xy[0], xy[1] + i * SM_SHOP_ROW_DY,
                      0x2d, alpha, 0, 1, 0);
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x418: the player's points.
// FF3 0x001a5178 (no Ghidra function before 2026-10-14: inside 0x1a4d70)

static int StartMenuOptionDrawPoint(const B2D_HOOK_ARGS *args)
{
    const int *xy = (const int *)args->t1;
    int        point;

    point = (int)IngameWrkGetPoint();
    SetCurColorRGB(0xf1, 0xd6, 0x9b);
    PrintNumber_N(point, 7, xy[0], xy[1], 0x2d, (u_char)args->stack[2], 0, 1, 0);
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC start_menu_option_hooks[] =      /* rodata 0x3583e8, copied to the stack */
{
    { StartMenuOptionDrawName,  0, 0x419, nullptr },
    { StartMenuOptionDrawCost,  0, 0x41f, nullptr },
    { StartMenuOptionDrawPoint, 0, 0x418, nullptr },
    { nullptr,                 -1, -1,    nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a51f0
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (StartMenuShopHandle)

void StartMenuOptionSetHooks(void)
{
    static const int row_items[6] = { 0x3f1, 0x3f2, 0x3f3, 0x3f4, 0x401, 0x402 };    // built on the stack
    static const B2D_ITEM_HOOK row_hook = { StartMenuOptionDrawRowSpr };
    int handle;
    int i;

    handle = StartMenuShopHandle();
    for (i = 0; i < 6; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, row_items[i]), &row_hook);
    }
    B2dSetHookList(handle, start_menu_option_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// What the bought lens entries give: slots 29..32 lens functions 5..8,
// slots 26..28 lenses 2, 3 and 5 (both forms), each with its finder flag.
// FF3 0x001a5308

void StartMenuOptionApplyBought(void)
{
    FINDER_SLOT_WRK *slot;
    LENS_WRK        *lens;

    slot = GetFinderSlotWrk();
    lens = GetLensWrk();
    if (slot->slot[29] == 2) {
        lens->AddFunc(5);
        DocFlagSet(100);
    }
    if (slot->slot[30] == 2) {
        lens->AddFunc(6);
        DocFlagSet(0x65);
    }
    if (slot->slot[31] == 2) {
        lens->AddFunc(7);
        DocFlagSet(0x66);
    }
    if (slot->slot[32] == 2) {
        lens->AddFunc(8);
        DocFlagSet(0x67);
    }
    if (slot->slot[26] == 2) {
        lens->AddHave(2, 0);
        lens->AddHave(2, 2);
        DocFlagSet(0x5a);
    }
    if (slot->slot[27] == 2) {
        lens->AddHave(3, 0);
        lens->AddHave(3, 2);
        DocFlagSet(0x5b);
    }
    if (slot->slot[28] == 2) {
        lens->AddHave(5, 0);
        lens->AddHave(5, 2);
        DocFlagSet(0x5e);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Phase 12's end function after the buy prompt: yes buys the cursor's
// entry.
// FF3 0x001a5488

static void StartMenuOptionBuyEnd(int result)
{
    int pos;

    if (result == 1) {
        pos = MenuCsrGetPos(START_MENU_CSR_SHOP);
        if (StartMenuShopSlot(pos) == 1 && IngameWrkGetPoint() >= (u_int)start_menu_bonus[pos].cost) {
#ifdef REIPAN_SLPS_25544_CHS
            if (pos == SM_SHOP_PROMO_MOVIE)
                GetFinderStateWrk()->SetPromoMovieState(2);
            else
                GetFinderSlotWrk()->Set(pos - (pos > SM_SHOP_PROMO_MOVIE), 2);
#else
            GetFinderSlotWrk()->Set(pos, 2);
#endif
            StartMenuOptionApplyBought();
            IngameWrkSubPoint(start_menu_bonus[pos].cost);
            start_menu_option_bought = 1;
        }
    }
    CmnFadeLayerSwitchItem(9);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 12's end function after the shop's other messages.
// FF3 0x001a5518

static void StartMenuOptionMsgEnd(int result)
{
    (void)result;
    CmnFadeLayerSwitchItem(9);
}

// ──────────────────────────────────────────────────────────────────────
// The cursor entry's description in message 0x1b.
// FF3 0x001a5530

static int StartMenuOptionGetMsgNo(void)
{
    int pos;

    pos = MenuCsrGetPos(START_MENU_CSR_SHOP);
#ifndef REIPAN_SLPS_25544_CHS
    if (pos > SM_SHOP_MSG_SKIP) {
        pos++;
    }
#endif
    return pos * 2 + 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 81: opens what the clears and missions earned, then shows the
// cursor entry's description. FF3 passes the clear-count work to the two
// open functions, which do not read it.
// FF3 0x001a5560
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk, GetFinderStateWrk (StartMenuShopHandle; the open functions' unread argument)

int init_StartMenu_Option(void)
{
    int handle;

    handle = StartMenuShopHandle();
    FinderSlotOpenByCount();
    FinderSlotOpenByMission();
    start_menu_option_bought = 0;
    start_menu_option_bar = nullptr;
    start_menu_sel_win->SetItem(handle, SM_SHOP_WIN_ITEM);
    start_menu_sel_win->StartMsg(SM_SHOP_MSG, StartMenuOptionGetMsgNo(), 1);
    return 1;
}

// FF3 0x001a5628
void pre_StartMenu_Option(void)
{
}

// FF3 0x001a5630
int one_StartMenu_Option(void)
{
    return 1;
}

// FF3 0x001a5638
void after_StartMenu_Option(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The scroll bar goes; FF3 leaves the pointer set (phase 81's init clears
// it before 109 can test it).
// FF3 0x001a5640

int end_StartMenu_Option(void)
{
    if (start_menu_option_bar != nullptr) {
        delete start_menu_option_bar;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 109: the list plays in, the scroll bar (33 rows, 6 a page) is made
// once, the fade layer and the window take item 0x42b.
// FF3 0x001a5678
// callcheck: B2dScrollBar1CtorByName, operator_new, GetLanguage, GetSysWrk (new B2d_ScrollBar1, StartMenuShopFile)

int init_StartMenu_OptionMain(void)
{
    int file;
    int handle;

    file = (short)StartMenuShopFile();
    BpPlayLabel(BpCtlGetHandleByFileNo(file), "in");
    if (start_menu_option_bar == nullptr) {
        start_menu_option_bar = new B2d_ScrollBar1(file, "slider", "R_tumami");
        start_menu_option_bar->SetRange(SM_SHOP_ENTRIES, SM_SHOP_ROWS);
    }
    handle = StartMenuShopHandle();
    CmnFadeLayerShowItem(9, handle, SM_SHOP_WIN_ITEM);
    start_menu_sel_win->SetItem(handle, SM_SHOP_WIN_ITEM);
    start_menu_sel_win->StartMsg(SM_SHOP_MSG, StartMenuOptionGetMsgNo(), 1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 109 per frame. OK on an entry opens phase 12 with the prompt for
// its slot: locked (message 1), buy? (4, yes buys), already bought (2), or
// not enough points (3). Cancel goes back to StartMenu_Top (80) -- through
// the leave prompt (message 0: yes saves in phase 110) once something was
// bought. The page keys page the list.
// FF3 0x001a57a0
// callcheck: B2dScrollBar1Draw (start_menu_option_bar->Draw)

int one_StartMenu_OptionMain(void)
{
    CWindowMsg   *win = start_menu_sel_win;
    PHASE12_PARAM no_point =                    /* built on the stack */
        { SM_SHOP_PROMPT_MSG, 3, 2, 0x6d, -1, 0x6d, 0, StartMenuOptionMsgEnd, win };
    PHASE12_PARAM by_slot[3] =                  /* built on the stack; by the slot's value */
    {
        { SM_SHOP_PROMPT_MSG, 1, 2, 0x6d, -1,   0x6d, 0, StartMenuOptionMsgEnd, win },
        { SM_SHOP_PROMPT_MSG, 4, 1, 0x6d, 0x6d, 0x6d, 0, StartMenuOptionBuyEnd, win },
        { SM_SHOP_PROMPT_MSG, 2, 2, 0x6d, -1,   0x6d, 0, StartMenuOptionMsgEnd, win },
    };
    PHASE12_PARAM leave =                       /* built on the stack */
        { SM_SHOP_PROMPT_MSG, 0, 1, 0x6e, 0x50, 0x6d, 0, StartMenuOptionMsgEnd, win };
    const PHASE12_PARAM *param;
    int ret;
    int pos;
    int v;

    ret = 0;
    MenuMouseCsrBar(MenuCsrGetWork(START_MENU_CSR_SHOP), StartMenuShopHandle(), SM_SHOP_CSR_ITEM, 0.0f,
                    (float)SM_SHOP_ROW_DY);     // PC
    MenuCsrUpdate(START_MENU_CSR_SHOP, 0);
    if (MenuCsrIsMovedSe(START_MENU_CSR_SHOP, SM_SE_MOVE) == 0) {
        if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0) != 0) {
            pos = MenuCsrGetPos(START_MENU_CSR_SHOP);
            v = StartMenuShopSlot(pos);
            if (v == 1 && IngameWrkGetPoint() < (u_int)start_menu_bonus[pos].cost) {
                SndSePlay(SM_SE_BUZZ, 100, 0x1000, 0, nullptr, 0);
                CmnFadeLayerSwitchItem(2);
                param = &no_point;
            } else {
                if (v == 1) {
                    SndSePlay(SM_SE_OK, 100, 0x1000, 0, nullptr, 0);
                    CmnFadeLayerSwitchItem(0);
                } else {
                    SndSePlay(SM_SE_BUZZ, 100, 0x1000, 0, nullptr, 0);
                    CmnFadeLayerSwitchItem(2);
                }
                param = &by_slot[v];
            }
            Phase12ParamSet(param);
            GetGPhaseCtrl()->SetNext(0xc, 1);
            ret = 1;
        } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
            if (start_menu_option_bought == 0) {
                GetGPhaseCtrl()->SetNext(0x50, 0);
            } else {
                CmnFadeLayerSwitchItem(0);
                Phase12ParamSet(&leave);
                GetGPhaseCtrl()->SetNext(0xc, 1);
            }
            ret = 1;
        } else if (CheckPadRptPlaySe((u_short)pad_key[PAD_KEY_PAGE_PREV], SM_SE_PAGE, 0) != 0) {
            MenuCsrPagePrevY(START_MENU_CSR_SHOP);
        } else if (CheckPadRptPlaySe((u_short)pad_key[PAD_KEY_PAGE_NEXT], SM_SE_PAGE, 0) != 0) {
            MenuCsrPageNextY(START_MENU_CSR_SHOP);
        }
    }
    win->StartMsg(SM_SHOP_MSG, StartMenuOptionGetMsgNo(), 1);
    StartMenuOptionDispCsr();
    start_menu_option_bar->Draw(MenuCsrGetPosY(START_MENU_CSR_SHOP), MenuCsrGetTopY(START_MENU_CSR_SHOP));
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 109 end: plays "out" once, then waits for it.
// FF3 0x001a5b70
// callcheck: GetLanguage, GetSysWrk (StartMenuShopFile)

int end_StartMenu_OptionMain(void)
{
    int file;
    int done;

    file = (short)StartMenuShopFile();
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(file), "out");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuOptionDispCsr();
    done = BpCtlIsLoadEnd(file);
    if (done != 0) {
        CmnFadeLayerTermItem();
        start_menu_sel_win->SetAlphaItem(nullptr);
    }
    return done != 0;
}

// FF3 0x001a5c50
int init_StartMenu_OptionSave(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 110: Story_SaveGame_Mc (69) as a child, then back to StartMenu_Top
// when it saved or to the list when it did not.
// FF3 0x001a5c58

int one_StartMenu_OptionSave(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        GetGPhaseCtrl()->SetNext(0x45, 1);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    } else {
        GetGPhaseTree()->Reset();
        GetGPhaseCtrl()->SetNext(title_start_flg.save != 0 ? 0x50 : 0x6d, 0);
    }
    return 1;
}

// FF3 0x001a5d58
int end_StartMenu_OptionSave(void)
{
    return 1;
}

// FF3 0x001a5d68 is the GPhaseCtrl atexit thunk that ends FF3's shop file
// (compiler-generated; GPHASE_CTRL's destructor here).

// ══════════════════════════════════════════════════════════════════════
// The save works (FF3's next file, 0x1a5da0..)
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Clear-count save block: {GetFinderStateWrk(), 0x10}. FF3 caches the getter
// in a shared static (sbss gp-0xc80); calling it is the same.
// FF3 0x001a5da0

int FinderStateSaveGet(void **addr)
{
    *addr = GetFinderStateWrk();
    return 0x10;
}

// ──────────────────────────────────────────────────────────────────────
// No clears, both words and the save-head flag cleared.
// FF3 0x001a5df8

void FINDER_STATE_WRK::Init()
{
    int i;

    for (i = 0; i < 4; i++) {
        cnt[i] = 0;
    }
    _0c = 0;
    _04 = 0;
    _08 = 0;
}

FINDER_STATE_WRK::FINDER_STATE_WRK()
{
    Init();
}

// ──────────────────────────────────────────────────────────────────────
// One more clear on a difficulty, kept within 0..99.
// FF3 0x001a5e30

void FINDER_STATE_WRK::AddClear(int difficulty)
{
    int cnt;
    int v;

    cnt = this->cnt[difficulty] + 1;
    v = 0;
    if (cnt >= 0) {
        v = 99;
        if (cnt < 100) {
            v = cnt;
        }
    }
    this->cnt[difficulty] = (s_char)v;
}

// ──────────────────────────────────────────────────────────────────────
// Clears over all four difficulties.
// FF3 0x001a5e60

int FINDER_STATE_WRK::FUN_001a5e60()
{
    int sum;
    int i;

    sum = 0;
    for (i = 0; i < 4; i++) {
        sum += cnt[i];
    }
    return sum;
}

// ──────────────────────────────────────────────────────────────────────
// What the first clear on any difficulty opens: title menu item 2 and nine
// slots. FF3 passes the object in a0 but does not read it.
// FF3 0x001a5e90

void FINDER_STATE_WRK::SlotOpenFirstClear()
{
    FINDER_SLOT_WRK *slot;

    slot = GetFinderSlotWrk();
    TitleMenuItemOpenSet(2, 1);
    slot->Set(0, 1);
    slot->Set(4, 1);
    slot->Set(8, 1);
    slot->Set(10, 1);
    slot->Set(14, 1);
    slot->Set(18, 1);
    slot->Set(22, 1);
    slot->Set(28, 1);
    slot->Set(29, 1);
}

// ──────────────────────────────────────────────────────────────────────
// GAME_CLEAR_: count the clear, then open what it earns -- title item 3
// after difficulty 2, slot 31 after difficulty 3, two more sets of slots
// once two and three clears are reached.
// FF3 0x001a5f60

void FINDER_STATE_WRK::Clear(int difficulty)
{
    FINDER_SLOT_WRK *slot;
    int              total;

    slot = GetFinderSlotWrk();
    AddClear(difficulty);
    total = FUN_001a5e60();
    SlotOpenFirstClear();

    if (difficulty == 2) {
        TitleMenuItemOpenSet(3, 1);
    }
    if (difficulty == 3) {
        slot->Set(31, 1);
    }
    if (total > 1) {
        slot->Set(1, 1);
        slot->Set(5, 1);
        slot->Set(9, 1);
        slot->Set(11, 1);
        slot->Set(15, 1);
        slot->Set(19, 1);
    }
    if (total > 2) {
        slot->Set(2, 1);
        slot->Set(6, 1);
        slot->Set(12, 1);
        slot->Set(16, 1);
        slot->Set(20, 1);
        slot->Set(23, 1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Slots opened by the soul list's four bonus thresholds (SoulListBonusIsReached:
// 50 / 100 / 150 / 217 ghosts photographed).
// FF3 0x001a60f8

void FinderSlotOpenByCount(void)
{
    FINDER_SLOT_WRK *slot;

    slot = GetFinderSlotWrk();
    if (SoulListBonusIsReached(0) != 0) {
        slot->Set(24, 1);
        slot->Set(25, 1);
#ifdef REIPAN_SLPS_25544_CHS
        if (GetFinderStateWrk()->PromoMovieState() != 2)
            GetFinderStateWrk()->SetPromoMovieState(1);
#endif
    }
    if (SoulListBonusIsReached(1) != 0) {
        slot->Set(30, 1);
    }
    if (SoulListBonusIsReached(2) != 0) {
        slot->Set(26, 1);
    }
    if (SoulListBonusIsReached(3) != 0) {
        slot->Set(13, 1);
        slot->Set(17, 1);
        slot->Set(21, 1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Slots opened by the mission-mode clears.
// FF3 0x001a61e0

void FinderSlotOpenByMission(void)
{
    FINDER_SLOT_WRK *slot;

    slot = GetFinderSlotWrk();
    if (MissionIsComplete(1) != 0) {
        slot->Set(32, 1);
    }
    if (MissionIsComplete(0) != 0) {
        slot->Set(3, 1);
        slot->Set(7, 1);
        slot->Set(27, 1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Unlock-slot save block: {GetFinderSlotWrk(), 0x84}.
// FF3 0x001a6288

int FinderSlotSaveGet(void **addr)
{
    *addr = GetFinderSlotWrk();
    return 0x84;
}

// ──────────────────────────────────────────────────────────────────────
// Every slot to 0.
// FF3 0x001a62e0

void FINDER_SLOT_WRK::Init()
{
    int i;

    for (i = 0; i < FINDER_SLOT_MAX; i++) {
        SetForce(i, 0);
    }
}

FINDER_SLOT_WRK::FINDER_SLOT_WRK()
{
    Init();
}

// ──────────────────────────────────────────────────────────────────────
// How many slots of a group hold `value`. The object is not read; FF3
// counts in the cached GetFinderSlotWrk().
// FF3 0x001a6330

int FINDER_SLOT_WRK::Count(int group, int value)
{
    FINDER_SLOT_WRK *slot;
    int              cnt;
    int              i;

    slot = GetFinderSlotWrk();
    cnt = 0;
    for (i = finder_slot_group[group][0]; i <= finder_slot_group[group][1]; i++) {
        if (slot->slot[i] == value) {
            cnt++;
        }
    }
    return cnt;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a63d0

void FINDER_SLOT_WRK::SetForce(int no, int value)
{
    SetSub(no, value, 1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a63e8

void FINDER_SLOT_WRK::Set(int no, int value)
{
    SetSub(no, value, 0);
}

// ──────────────────────────────────────────────────────────────────────
// A slot only goes up unless forced.
// FF3 0x001a6400

void FINDER_SLOT_WRK::SetSub(int no, int value, int force)
{
    if (force != 0 || slot[no] <= value) {
        slot[no] = value;
    }
}

// ══════════════════════════════════════════════════════════════════════
// The gallery (phases 82 StartMenu_Gallery, 111 GalleryMain, 112
// GalleryView), FF3 0x1a6430..0x1a7930: six entries -- two picture sets
// bought in the shop (slots 24 / 25) paged in the viewer, two movies the
// clears open (Story_Opening, 56) and two always open (phase 113).
// ══════════════════════════════════════════════════════════════════════

enum
{
    SM_GALLERY_SETTINGS,
    SM_GALLERY_PROMO_PICTURES,
#ifdef REIPAN_SLPS_25544_CHS
    SM_GALLERY_PROMO_MOVIE,
#endif
    SM_GALLERY_ENDING_1,
    SM_GALLERY_ENDING_2,
    SM_GALLERY_ZERO_1,
    SM_GALLERY_ZERO_2,
    SM_GALLERY_ENTRIES
};
#define SM_GALLERY_MSG      0x5f    // entry descriptions
#define SM_GALLERY_WIN_ITEM 0x401   // the window's / fade layer's item in the list layout
#define SM_GALLERY_BGM      0xfbd   // played while the gallery is up
#define SM_GALLERY_PIC_A    0x852   // first picture file of entry 0 (also the blank)
#define SM_GALLERY_PIC_B    0x867   // first picture file of entry 1

// Items of the list layout per entry.
#ifdef REIPAN_SLPS_25544_CHS
// SLPS_255.44 0x34fbb0..0x34fbfc includes a separate promotional film row.
static const short sm_gallery_entry_csr[SM_GALLERY_ENTRIES] = { 0x3f2, 0x3f4, 0x3fc, 0x3fe, 0x402, 0x3f8, 0x3fa };
static const short sm_gallery_entry[SM_GALLERY_ENTRIES]     = { 0x3f1, 0x3f3, 0x3fb, 0x3fd, 0x434, 0x3f7, 0x3f9 };
static const short sm_gallery_lock_csr[SM_GALLERY_ENTRIES]  = { 0x418, 0x41a, 0x422, 0x424, 0x432, 0x41e, 0x420 };
static const short sm_gallery_lock[SM_GALLERY_ENTRIES]      = { 0x428, 0x429, 0x42d, 0x42e, 0x430, 0x42b, 0x42c };
static const short sm_gallery_mark[SM_GALLERY_ENTRIES]      = { 0x41b, 0x41d, 0x417, 0x419, 0x412, 0x421, 0x423 };
#else
static const short sm_gallery_entry_csr[SM_GALLERY_ENTRIES] = { 0x3f2, 0x3f4, 0x3fe, 0x402, 0x3f8, 0x3fa };  // 0x358650: on the cursor
static const short sm_gallery_entry[SM_GALLERY_ENTRIES]     = { 0x3f1, 0x3f3, 0x3fd, 0x434, 0x3f7, 0x3f9 };  // 0x358660: elsewhere
static const short sm_gallery_lock_csr[SM_GALLERY_ENTRIES]  = { 0x418, 0x41a, 0x424, 0x432, 0x41e, 0x420 };  // 0x358670
static const short sm_gallery_lock[SM_GALLERY_ENTRIES]      = { 0x428, 0x429, 0x42e, 0x430, 0x42b, 0x42c };  // 0x358680
static const short sm_gallery_mark[SM_GALLERY_ENTRIES]      = { 0x41b, 0x41d, 0x419, 0x412, 0x421, 0x423 };  // 0x358690
#endif

static int       start_menu_gallery_page;       // sbss 0x373160 (gp-0x390): viewer page
static int       start_menu_gallery_pages;      // sbss 0x373164 (gp-0x38c): viewer page count
static int       start_menu_gallery_csr;        // sbss 0x37316c (gp-0x384): entry to come back to
static MENU_PIC *start_menu_gallery_pic;        // sbss 0x373170 (gp-0x380): the viewer's picture

static int StartMenuGalleryFile(void)
{
    return GetSysWrk()->GetLanguage() + START_MENU_GALLERY_FILE;
}

static int StartMenuViewFile(void)
{
    return GetSysWrk()->GetLanguage() + START_MENU_VIEW_FILE;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 leaves the pointer set (the viewer's init makes a new one).
// FF3 0x001a6430
// callcheck: operator_delete (delete)
// callcheck: MenuPicDtor (MENU_PIC's destructor)

static void StartMenuGalleryPicRelease(void)
{
    delete start_menu_gallery_pic;
    start_menu_gallery_pic = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The viewer's picture: one slot, 512 x 422 at (64, 10).
// FF3 0x001a6478
// callcheck: operator_new (new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)

static void StartMenuGalleryPicCreate(void)
{
    MENU_PIC *pic;

    pic = new MENU_PIC(0);
    start_menu_gallery_pic = pic;
    pic->v      = 0;
    pic->u      = 0;
    pic->h      = 0x1a6;
    pic->w      = 0x200;
    pic->y      = 10;
    pic->x      = 0x40;
    pic->pri    = 0;
    pic->z      = 0;
    pic->enable = 1;
}

// ──────────────────────────────────────────────────────────────────────
// Pictures in a set.
// FF3 0x001a64f8

static int StartMenuGalleryGetPageNum(int no)
{
    if (no == 0) {
        return 21;
    }
    if (no == 1) {
        return 7;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The picture file of a page; the first file of set 0 while the set is
// not bought.
// FF3 0x001a6528

static int StartMenuGalleryGetPicNo(int no, int page)
{
    FINDER_SLOT_WRK *slot;
    int              v;

    slot = GetFinderSlotWrk();
    if (no == 0) {
        v = slot->slot[24];
        page += SM_GALLERY_PIC_A;
    } else if (no == 1) {
        v = slot->slot[25];
        page += SM_GALLERY_PIC_B;
    } else {
        return SM_GALLERY_PIC_A;
    }
    if (v != 2) {
        page = SM_GALLERY_PIC_A;
    }
    return page;
}

// ──────────────────────────────────────────────────────────────────────
// Entries 0 / 1 once bought, 2 / 3 after the matching clear, 4 / 5 always.
// FF3 0x001a65a8

int StartMenuGalleryIsOpen(int no)
{
    FINDER_STATE_WRK *state;
    int               v;

    switch (no) {
    case SM_GALLERY_SETTINGS:
        v = GetFinderSlotWrk()->slot[24];
        break;
    case SM_GALLERY_PROMO_PICTURES:
        v = GetFinderSlotWrk()->slot[25];
        break;
#ifdef REIPAN_SLPS_25544_CHS
    case SM_GALLERY_PROMO_MOVIE:
        return GetFinderStateWrk()->PromoMovieState() == 2;
#endif
    case SM_GALLERY_ENDING_1:
    case SM_GALLERY_ENDING_2:
        state = GetFinderStateWrk();
        return (no == SM_GALLERY_ENDING_1 ? state->_04 : state->_08) != 0;
    case SM_GALLERY_ZERO_1:
    case SM_GALLERY_ZERO_2:
        return 1;
    default:
        return 0;
    }
    return v == 2;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the viewer's item 0x402: the page's picture, faded with the item.
// FF3 0x001a6660 (no Ghidra function before 2026-10-14: inside 0x1a65a8)

static int StartMenuGalleryDrawPic(const B2D_HOOK_ARGS *args)
{
    MENU_PIC *pic;
    float     scale;

    start_menu_gallery_pic->pic = StartMenuGalleryGetPicNo(MenuCsrGetPos(START_MENU_CSR_GALLERY),
                                                           start_menu_gallery_page);
    scale = (float)(int)args->stack[2] * (1.0f / 128.0f);     /* sdata 0x3702c8 */
    start_menu_gallery_pic->Step();
    pic = start_menu_gallery_pic;
    pic->scale = scale;
    pic->Draw();
    pic->DrawNext();
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of item 0x3f2 in layout 0x12a2: the whole screen black at the item's
// alpha.
// FF3 0x001a66e8

static int StartMenuGalleryDrawFade(const B2D_HOOK_ARGS *args)
{
    int       alpha = (int)args->stack[2];
    SQAR_DAT  sq;
    DISP_SQAR ds;

    sq.w     = 0x280;
    sq.h     = 0x1c0;
    sq.x     = 0;
    sq.y     = 0;
    sq.pri   = 0;
    sq.r     = 0;
    sq.g     = 0;
    sq.b     = 0;
    sq.alpha = (u_char)alpha;
    CopySqrDToSqr(&ds, &sq);
    if (alpha > 0) {
        DispSqrD(&ds);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Sprite hook of the entries: the cursor variant on the cursor's entry,
// the normal one on the others.
// FF3 0x001a6758 (Ghidra: DispCharaRGB, an FF2 position guess)

static int StartMenuGalleryDrawEntry(const B2D_HOOK_ARGS *args)
{
    int id;
    int pos;
    int draw;
    int i;

    id = B2dGetDrawItem()->id;
    pos = MenuCsrGetPos(START_MENU_CSR_GALLERY);
    draw = 0;
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (id == sm_gallery_entry_csr[i] && i == pos) {
            draw = 1;
        }
    }
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (id == sm_gallery_entry[i] && i != pos) {
            draw = 1;
        }
    }
    if (draw) {
#ifdef REIPAN_SLPS_25544_CHS
        if (id == sm_gallery_entry_csr[SM_GALLERY_ENDING_2]) {
            // Its 206-pixel atlas strip also contains the adjacent "Settings" glyphs.
            B2D_HOOK_ARGS cropped = *args;
            cropped.a3 = 128;
            return B2dDrawSpriteDirect(&cropped);
        }
#endif
        B2dDrawSpriteDirect(args);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the lock items: R_lockon over a closed entry under the cursor,
// R_lockoff over the other closed ones (the item's own sprite is not
// drawn).
// FF3 0x001a68a0
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryFile)

static int StartMenuGalleryDrawLock(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    u_int      rgb = (u_int)args->t3;
    int        id;
    int        pos;
    int        draw;
    int        on;
    int        i;

    draw = 0;
    on = 1;
    id = B2dGetDrawItem()->id;
    pos = MenuCsrGetPos(START_MENU_CSR_GALLERY);
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (StartMenuGalleryIsOpen(i) == 0 && id == sm_gallery_lock_csr[i] && i == pos) {
            draw = 1;
        }
    }
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (StartMenuGalleryIsOpen(i) == 0 && id == sm_gallery_lock[i] && i != pos) {
            draw = 1;
            on = 0;
        }
    }
    if (draw && args->a1 > 0 && args->a2 > 0) {
        const float sx = (float)(xy[6] - xy[0]) / (int)args->a1;
        const float sy = (float)(xy[3] - xy[1]) / (int)args->a2;
        B2dDrawSpriteByName(BpCtlGetHandleByFileNo(StartMenuGalleryFile()), on ? "R_lockon" : "R_lockoff",
                            xy[0], xy[1], rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff,
                            (float)(int)args->stack[2] * (1.0f / 128.0f),   /* sdata 0x3702cc */
                            0.0f, sx, sy, 0.0f, 0.0f);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the cursor marks: R_casoru00 on the cursor's entry only.
// FF3 0x001a6a90 (no Ghidra function before 2026-10-14: inside 0x1a68a0)
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryFile)

static int StartMenuGalleryDrawCsr(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    u_int      rgb = (u_int)args->t3;
    int        id;
    int        pos;
    int        draw;
    int        i;

    id = B2dGetDrawItem()->id;
    pos = MenuCsrGetPos(START_MENU_CSR_GALLERY);
    draw = 0;
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (id == sm_gallery_mark[i] && i == pos) {
            draw = 1;
        }
    }
    if (draw) {
        B2dDrawSpriteByName(BpCtlGetHandleByFileNo(StartMenuGalleryFile()), "R_casoru00",    /* rodata 0x3585a8 */
                            xy[0], xy[1], rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff,
                            (float)(int)args->stack[2] * (1.0f / 128.0f),   /* sdata 0x3702d4 */
                            0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// A sprite drawn with bilinear filtering (TEX1_1: MMAG / MMIN linear).
// FF3 0x001a6ba0 (Ghidra: DispPointNumberNew, an FF2 position guess)

static int B2dDrawSpriteLinear(const B2D_HOOK_ARGS *args)
{
    SetGsRegCached(0x100000161, 0x14, 1);
    B2dDrawSpriteDirect(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the viewer's item 0x401: "page / pages", the second number
// further right when the count has two digits.
// FF3 0x001a6c78

static int StartMenuGalleryDrawPage(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    int        page  = start_menu_gallery_page;
    int        pages = start_menu_gallery_pages;
    int        dx;

    dx = 0x2a;
    if (pages < 10) {
        dx = 0x1c;
    }
    SetCurColorPacked((u_int)args->t3);
    PrintNumber_N(page + 1, 2, xy[0], xy[1], 0x2d, alpha, 0, 1, 0);
    PrintNumber_N(pages, 2, xy[0] + dx, xy[1], 0x2d, alpha, 0, 1, 0);
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC start_menu_view_hooks[] =        /* rodata 0x3585b8, copied to the stack */
{
    { StartMenuGalleryDrawPage, 0, 0x401, nullptr },
    { StartMenuGalleryDrawPic,  0, 0x402, nullptr },
    { nullptr,                 -1, -1,    nullptr },
};

static const B2D_ITEM_HOOK sm_hook_linear = { B2dDrawSpriteLinear };

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a6d40
// callcheck: GetLanguage, GetSysWrk (StartMenuViewFile)

static void StartMenuGalleryViewSetHooks(void)
{
    static const int linear_items[2] = { 0x405, 0x406 };       /* sdata 0x3702e0 */
    int handle;
    int i;

    handle = BpCtlGetHandleByFileNo(StartMenuViewFile());
    for (i = 0; i < 2; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, linear_items[i]), &sm_hook_linear);
    }
    B2dSetHookList(handle, start_menu_view_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a6e40 (phase 113's init)

void StartMenuGalleryFadeSetHooks(void)
{
    static const B2D_HOOK_REC hooks[] =                     // built on the stack (as rodata 0x3585e8)
    {
        { StartMenuGalleryDrawFade, 0, 0x3f2, nullptr },
        { nullptr,                 -1, -1,    nullptr },
    };

    B2dSetHookList(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_FADE_FILE), hooks);
}

// ──────────────────────────────────────────────────────────────────────
// The list layout's hooks: entries, locks, cursor marks, and the two
// filtered sprites 0x410 / 0x425.
// FF3 0x001a6ed0
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryFile)

void StartMenuGallerySetHooks(void)
{
    static const B2D_ITEM_HOOK entry_hook = { StartMenuGalleryDrawEntry };
    static const B2D_ITEM_HOOK lock_hook  = { StartMenuGalleryDrawLock };
    static const B2D_ITEM_HOOK csr_hook   = { StartMenuGalleryDrawCsr };
    static const int linear_items[2] = { 0x410, 0x425 };       /* sdata 0x3702f8 */
    int handle;
    int i;

    handle = BpCtlGetHandleByFileNo(StartMenuGalleryFile());
#ifdef REIPAN_SLPS_25544_CHS
    // The PS2 put these source sprites outside its viewport. Keep them
    // available to the row hooks while suppressing their standalone draws.
    static constexpr int gallery_templates[] = { 0x3e9, 0x3eb, 0x3ec, 0x3ef, 0x3f6 };
    for (int id : gallery_templates) {
        if (B2D_ITEM *item = B2dCtlFindItem(handle, id))
            B2dSprSetDisp(item, 0);
    }
#endif
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, sm_gallery_entry_csr[i]), &entry_hook);
        B2dCtlSetItemHook(B2dCtlFindItem(handle, sm_gallery_entry[i]), &entry_hook);
    }
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, sm_gallery_lock_csr[i]), &lock_hook);
        B2dCtlSetItemHook(B2dCtlFindItem(handle, sm_gallery_lock[i]), &lock_hook);
    }
    for (i = 0; i < SM_GALLERY_ENTRIES; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, sm_gallery_mark[i]), &csr_hook);
    }
    for (i = 0; i < 2; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, linear_items[i]), &sm_hook_linear);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Phase 82: back from a movie (56), the cursor returns to its entry.
// FF3 0x001a70d0

int init_StartMenu_Gallery(void)
{
    if (GetGPhaseCtrl()->from == 0x38) {
        MenuCsrSetPos(START_MENU_CSR_GALLERY, start_menu_gallery_csr);
    }
    return 1;
}

// FF3 0x001a7120
void pre_StartMenu_Gallery(void)
{
}

// FF3 0x001a7128
int one_StartMenu_Gallery(void)
{
    GetGPhaseCtrl()->SetNext(0x6f, 0);
    return 1;
}

// FF3 0x001a7168
void after_StartMenu_Gallery(void)
{
}

// FF3 0x001a7170
int end_StartMenu_Gallery(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The cursor entry's description in message 0x5f.
// FF3 0x001a7178 (Ghidra: finderTriggerEneLifeDecrease, an FF2 position guess)

static int StartMenuGalleryGetMsgNo(void)
{
    int pos;

    pos = MenuCsrGetPos(START_MENU_CSR_GALLERY);
#ifndef REIPAN_SLPS_25544_CHS
    if (pos > 1) {
        pos++;
    }
#endif
    return pos;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 111: the list plays in ("ingallary" back from the viewer), the
// gallery BGM starts when coming from phase 82.
// FF3 0x001a71a0
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryFile)

int init_StartMenu_GalleryMain(void)
{
    GPHASE_CTRL *ctrl;
    int          handle;
    int          from;

    handle = BpCtlGetHandleByFileNo(StartMenuGalleryFile());
    ctrl = GetGPhaseCtrl();
    from = ctrl->from;
    if (ctrl->leaf->state_cnt == 0) {
        CmnFadeLayerShowItem(0, handle, SM_GALLERY_WIN_ITEM);
        start_menu_sel_win->SetItem(handle, SM_GALLERY_WIN_ITEM);
        start_menu_sel_win->StartMsg(SM_GALLERY_MSG, StartMenuGalleryGetMsgNo(), 1);
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuGalleryFile()), from == 0x70 ? "ingallary" : "in");
        if (from == 0x52) {
            SndBgmPlay(SM_GALLERY_BGM, 100, 0x1000, GetPALMode() != 0 ? 0x19 : 0x1e, nullptr, 1, 0);
        }
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    return BpCtlIsLoadEnd(StartMenuGalleryFile()) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 111 per frame: OK on an open entry goes to the viewer (112), the
// movie (56) or phase 113; cancel returns to StartMenu_Top with its cursor
// on Gallery (5).
// FF3 0x001a7368

int one_StartMenu_GalleryMain(void)
{
    int pos;
    int next;

    // PC: the mouse. Each entry is an item of the layout.
    const int handle = BpCtlGetHandleByFileNo(StartMenuGalleryFile());
    for (int i = 0; i < SM_GALLERY_ENTRIES; i++) {
        if (!MenuMouseCsrId(START_MENU_CSR_GALLERY, i, handle, sm_gallery_entry[i]))
            MenuMouseCsrId(START_MENU_CSR_GALLERY, i, handle, sm_gallery_entry_csr[i]);
    }
    MenuCsrUpdate(START_MENU_CSR_GALLERY, 0);
    start_menu_sel_win->StartMsg(SM_GALLERY_MSG, StartMenuGalleryGetMsgNo(), 1);
    pos = MenuCsrGetPos(START_MENU_CSR_GALLERY);
    if (MenuCsrIsMovedSe(START_MENU_CSR_GALLERY, SM_SE_MOVE) != 0) {
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0) != 0) {
        if (StartMenuGalleryIsOpen(pos) == 0) {
            SndSePlay(SM_SE_BUZZ, 100, 0x1000, 0, nullptr, 0);
            return 0;
        }
        SndSePlay(SM_SE_OK, 100, 0x1000, 0, nullptr, 0);
        start_menu_gallery_csr = pos;
        switch (pos) {
        case SM_GALLERY_SETTINGS:
        case SM_GALLERY_PROMO_PICTURES:
            next = 0x70;
            break;
#ifdef REIPAN_SLPS_25544_CHS
        case SM_GALLERY_PROMO_MOVIE:
#endif
        case SM_GALLERY_ENDING_1:
        case SM_GALLERY_ENDING_2:
            next = 0x38;
            break;
        default:
            next = 0x71;
            break;
        }
        GetGPhaseCtrl()->SetNext(next, 0);
        return 1;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
        GetGPhaseCtrl()->SetNext(0x50, 0);
        MenuCsrSetPos(START_MENU_CSR_TOP, 5);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 111 end: plays out ("outgallary" into the viewer) and stops the
// BGM when leaving for a movie or StartMenu_Top, then waits.
// FF3 0x001a7538
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryFile)

int end_StartMenu_GalleryMain(void)
{
    int now;

    now = GetGPhaseCtrl()->now;
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuGalleryFile()), now == 0x70 ? "outgallary" : "out");
        if (now == 0x38 || now == 0x50) {
            SndBgmStop(GetPALMode() != 0 ? 0x19 : 0x1e, 0);
        }
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (BpCtlIsLoadEnd(StartMenuGalleryFile()) == 0) {
        return 0;
    }
    CmnFadeLayerTermItem();
    start_menu_sel_win->SetAlphaItem(nullptr);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 112: loads the viewer layout (state 0), sets it up once loaded
// (state 1), then waits for its "in" (state 2). FF3 shows the fade layer
// item with the handle it looked up on entry.
// FF3 0x001a76b0
// callcheck: GetLanguage, GetSysWrk (StartMenuViewFile)

int init_StartMenu_GalleryView(void)
{
    GPHASE_NODE *leaf;
    int          handle;

    handle = BpCtlGetHandleByFileNo(StartMenuViewFile());
    leaf = GetGPhaseCtrl()->leaf;
    if (leaf->state_cnt == 0) {
        start_menu_gallery_page = 0;
        start_menu_gallery_pages = StartMenuGalleryGetPageNum(MenuCsrGetPos(START_MENU_CSR_GALLERY));
        BpCtlLoadReq(StartMenuViewFile(), nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (leaf->state_cnt != 1) {
        return BpCtlIsLoadEnd(StartMenuViewFile());
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(StartMenuViewFile())) == 0) {
        return 0;
    }
    StartMenuGalleryViewSetHooks();
    StartMenuGalleryPicCreate();
    CmnFadeLayerShowItem(8, handle, 0x3fe);
    BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuViewFile()), "in");
    GetGPhaseCtrl()->leaf->state_cnt = 2;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 112 per frame: the direction keys turn the pages (wrapping),
// cancel returns to the list.
// FF3 0x001a7890

int one_StartMenu_GalleryView(void)
{
    // PC: the mouse. A click or the wheel turns the page.
    if (ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_HORIZONTAL, nullptr) && ReiPan_MenuMouseClicked())
        ReiPan_MenuMousePress(REIPAN_MENU_RIGHT);
    if (CheckPadRptPlaySe(0xa00c, -1, 0) != 0) {
        start_menu_gallery_page = CsrMoveUpDownSe(start_menu_gallery_page, 0, start_menu_gallery_pages,
                                                  0x2008, 0x8004, 1, SM_SE_MOVE);
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
        GetGPhaseCtrl()->SetNext(0x6f, 0);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 112 end: plays out, then frees the picture and the layout.
// FF3 0x001a7930
// callcheck: GetLanguage, GetSysWrk (StartMenuViewFile)

int end_StartMenu_GalleryView(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuViewFile()), "out");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (BpCtlIsLoadEnd(StartMenuViewFile()) == 0) {
        return 0;
    }
    StartMenuGalleryPicRelease();
    CmnFadeLayerTermItem();
    BpCtlRelease(BpCtlGetHandleByFileNo(StartMenuViewFile()));
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 113 StartMenu_GalleryZero (gallery entries 4 and 5): one full-screen
// layout, GALLARY_ZERO1 for entry 4, GALLARY_ZERO2 for entry 5. FF3 loads
// both and frees both.

static int start_menu_gallery_zero1;            // sbss 0x373168 (gp-0x388): entry 4 picked

static int StartMenuGalleryZeroFile(void)
{
    return GetSysWrk()->GetLanguage() + (start_menu_gallery_zero1 != 0 ? START_MENU_FADE_FILE : START_MENU_ZERO2_FILE);
}

// ──────────────────────────────────────────────────────────────────────
// Loads both layouts (state 0), shows the picked one on the fade layer
// (label 0x12, item 0x3f1) and plays it in (state 1), then waits (state 2).
// FF3 0x001a7a40
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryZeroFile)

int init_StartMenu_GalleryZero(void)
{
    GPHASE_NODE *leaf;
    int          lang;
    int          handle;

    leaf = GetGPhaseCtrl()->leaf;
    if (leaf->state_cnt == 0) {
        start_menu_gallery_zero1 = MenuCsrGetPos(START_MENU_CSR_GALLERY) == SM_GALLERY_ZERO_1;
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + START_MENU_FADE_FILE, nullptr);
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + START_MENU_ZERO2_FILE, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (leaf->state_cnt != 1) {
        return BpCtlIsLoadEnd(StartMenuGalleryZeroFile());
    }
    lang = GetSysWrk()->GetLanguage();
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(lang + START_MENU_FADE_FILE)) == 0) {
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_ZERO2_FILE)) == 0) {
        return 0;
    }
    handle = BpCtlGetHandleByFileNo(StartMenuGalleryZeroFile());
    StartMenuGalleryFadeSetHooks();
    CmnFadeLayerShowItem(0x12, handle, 0x3f1);
    BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuGalleryZeroFile()), "in");
    GetGPhaseCtrl()->leaf->state_cnt = 2;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel goes back to the list (111).
// FF3 0x001a7d10

int one_StartMenu_GalleryZero(void)
{
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
        GetGPhaseCtrl()->SetNext(0x6f, 0);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Plays out, then frees both layouts and puts the list cursor back on the
// entry.
// FF3 0x001a7d78
// callcheck: GetLanguage, GetSysWrk (StartMenuGalleryZeroFile)

int end_StartMenu_GalleryZero(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuGalleryZeroFile()), "out");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    if (BpCtlIsLoadEnd(StartMenuGalleryZeroFile()) == 0) {
        return 0;
    }
    CmnFadeLayerTermItem();
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_FADE_FILE));
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_ZERO2_FILE));
    MenuCsrSetPos(START_MENU_CSR_GALLERY, start_menu_gallery_zero1 != 0 ? SM_GALLERY_ZERO_1 : SM_GALLERY_ZERO_2);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 56 Story_Opening (gallery entries 2 and 3): plays movie 0x15 or
// 0x16 through phase 67 Story_Movie, then back to the gallery (82). The
// start mode is cleared while it runs.

static int story_opening_mode;                  // sbss 0x373174 (gp-0x37c): title_start_flg.mode on entry

// FF3 0x001a7f20
int init_Story_Opening(void)
{
    story_opening_mode = title_start_flg.mode;
    title_start_flg.mode = 0;
    SndBgmStop(GetPALMode() != 0 ? 0x19 : 0x1e, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// State 0 starts the movie (linked under this phase), state 1 is the
// return from it.
// FF3 0x001a7f68

int one_Story_Opening(void)
{
    static const int movie_tbl[][2] = {
#ifdef REIPAN_SLPS_25544_CHS
        { SM_GALLERY_PROMO_MOVIE, 0x17 },    // SLPS_255.44 0x34fb98
#endif
        { SM_GALLERY_ENDING_1, 0x15 }, { SM_GALLERY_ENDING_2, 0x16 }
    };
    int movie;
    int i;

    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        movie = 0x15;
        for (i = 0; i < (int)(sizeof(movie_tbl) / sizeof(movie_tbl[0])); i++) {
            if (start_menu_gallery_csr == movie_tbl[i][0]) {
                movie = movie_tbl[i][1];
                break;
            }
        }
        MovieReqPlay(movie, 1);
        MovieReqSetFilmMode(0);
        GetGPhaseCtrl()->SetNext(0x43, 1);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    } else {
        GetGPhaseTree()->Reset();
        GetGPhaseCtrl()->SetNext(0x52, 0);
    }
    return 1;
}

// FF3 0x001a80b8
int end_Story_Opening(void)
{
    title_start_flg.mode = story_opening_mode;
    return 1;
}

// FF3 0x001a80d8 (StartMenuGalleryGPhaseCtrlAtExit) is the GPhaseCtrl atexit
// thunk that ends FF3's second file (GPHASE_CTRL's destructor here).

// ══════════════════════════════════════════════════════════════════════
// StartMenu_Main (phase 55), FF3's third file 0x1a8110..0x1a8a38
// ══════════════════════════════════════════════════════════════════════

CWindowMsg *start_menu_sel_win;         // sdata 0x370310 (init_StartMenu_Main makes it)

static int start_menu_loaded;           // sbss 0x37317c (gp-0x374): pre_StartMenu_Main draws once set

// ──────────────────────────────────────────────────────────────────────
// The menu's cursors: top (0x50, entry 1 only before the first clear's
// flag), difficulties (0x6b), 0x6c from table 0, the shop, the gallery,
// the mission lists and the mission sub menu; the stick drives the cursors.
// FF3 0x001a8110

static void StartMenuCsrInit(void)
{
    MenuCsrInitWithFilter(StartMenuTopCsrFilter, START_MENU_CSR_TOP, 1, 7, 0);
    if (GetFinderStateWrk()->ClearSaveFlag() == 0) {
        MenuCsrSetPos(START_MENU_CSR_TOP, 1);
    }
    MenuCsrInitWithFilter(StartMenuDiffCsrFilter, START_MENU_CSR_DIFF, 1, 4, 0);
    MenuCsrInitTbl(0x6c, 0, 0);
    MenuCsrInit(START_MENU_CSR_SHOP, 1, SM_SHOP_ENTRIES, 0);
    MenuCsrSetViewX(START_MENU_CSR_SHOP, 1, 0);
    MenuCsrSetViewY(START_MENU_CSR_SHOP, SM_SHOP_ROWS, 0);
    MenuCsrInit(START_MENU_CSR_GALLERY, 1, SM_GALLERY_ENTRIES, 0);
    MissionCsrInit();
    MenuCsrInit(START_MENU_CSR_MISSION_SUB, 1, 2, 0);
    pad_wrk[0].stick_dpad = 1;
}

// FF3 0x001a8210
static void StartMenuCsrRelease(void)
{
    MenuCsrRelease(START_MENU_CSR_TOP);
    MenuCsrRelease(START_MENU_CSR_DIFF);
    MenuCsrRelease(0x6c);
    MenuCsrRelease(START_MENU_CSR_SHOP);
    MenuCsrRelease(START_MENU_CSR_GALLERY);
    MissionCsrRelease();
    MenuCsrRelease(START_MENU_CSR_MISSION_SUB);
    pad_wrk[0].stick_dpad = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Whether any difficulty has been cleared (Story_Start opens the start menu
// then).
// FF3 0x001a8268

int FinderStateHasAny(void)
{
    return GetFinderStateWrk()->FUN_001a5e60() > 0;
}

static void StartMenuMissionSetHooks(void);

// ──────────────────────────────────────────────────────────────────────
// Phase 55 init. State 0 waits for every load; back from phase 11 (a
// mission ended by retry / quit) it loads the story game kept aside and
// frees the keeper; then the cursors, the movie backdrop and its fade-in.
// State 1 loads SETUP_BG, state 2 the menu layouts and the message window,
// state 3 waits for them and hooks them, state 4 brings the backdrop's
// cover in and plays SETUP_BG in.
// FF3 0x001a82a0
// callcheck: operator_new, SelWinCtor (new CWindowMsg)
// callcheck: GetFinderStateWrk (FF3 passes the work to FinderSlotOpenByCount / ByMission, which never read it)

int init_StartMenu_Main(void)
{
    CExtraMissionShelt *shelt;
    int                 state;

    state = GetGPhaseCtrl()->leaf->state_cnt;
    if (state == 0) {
        start_menu_loaded = 0;
        if (IsLoadEndAll() == 0) {
            return 0;
        }
        start_menu_loaded = 1;
        if (GetGPhaseCtrl()->from == 0xb) {
            shelt = GetExtraMissionShelt();
            shelt->Load();
            shelt->CarryRestore();
            if (shelt != nullptr) {
                delete shelt;                   // vtable +4: deleting destructor
            }
        }
        title_start_flg.mode = 1;
        StartMenuCsrInit();
        OutgameBgStart(0, OUTGAME_BG_PSS, -1);
        FadeInReq(0, 0, 0, 0);
        start_menu_sel_win = nullptr;
        FinderSlotOpenByCount();
        FinderSlotOpenByMission();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (state == 1) {
        if (OutgameBgIsLoaded() == 0) {
            return 0;
        }
        BpCtlLoadReq(SETUP_BG_PHF, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        return 0;
    }
    if (state == 2) {
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(SETUP_BG_PHF)) == 0) {
            return 0;
        }
        if (OutgameBgLoadWait() == 0) {
            return 0;
        }
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + SETUP_PHF, nullptr);
        BpCtlLoadReq(MISSION_TOP_MASK_PHF, nullptr);
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + START_MENU_SHOP_FILE, nullptr);
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + START_MENU_GALLERY_FILE, nullptr);
        start_menu_sel_win = new CWindowMsg;    // operator_new(0x1c) + SelWinCtor
        start_menu_sel_win->Open();
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + MISSION_TOP_PHF, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 3;
        return 0;
    }
    if (state != 3) {
        OutgameBgEnable();
        if (OutgameBgIsReady() != 0) {
            BpPlayLabel(BpCtlGetHandleByFileNo(SETUP_BG_PHF), "in");
            return 1;
        }
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + SETUP_PHF)) == 0) {
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + MISSION_TOP_PHF)) == 0) {
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(MISSION_TOP_MASK_PHF)) == 0) {
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_SHOP_FILE)) == 0) {
        return 0;
    }
    if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_GALLERY_FILE)) == 0) {
        return 0;
    }
    if (start_menu_sel_win->IsReady() == 0) {
        return 0;
    }
    start_menu_sel_win->SetAlpha(0);
    start_menu_sel_win->SetFlags(1, 1, 0);
    StartMenuSetupSetHooks();
    StartMenuMissionSetHooks();
    StartMenuOptionSetHooks();
    StartMenuGallerySetHooks();
    GetGPhaseCtrl()->leaf->state_cnt = 4;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 55 end: SETUP_BG and the backdrop go out; once both are done the
// cursors, every layout, the backdrop, the fade layer item and the message
// window are freed and the BGM stops.
// FF3 0x001a8778

int end_StartMenu_Main(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(SETUP_BG_PHF), "out");
        OutgameBgStop();
        OutgameBgDisable();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    if (BpCtlIsLoadEnd(SETUP_BG_PHF) == 0) {
        return 0;
    }
    if (OutgameBgIsStopped() == 0) {
        return 0;
    }
    StartMenuCsrRelease();
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + SETUP_PHF));
    BpCtlRelease(BpCtlGetHandleByFileNo(SETUP_BG_PHF));
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + MISSION_TOP_PHF));
    BpCtlRelease(BpCtlGetHandleByFileNo(MISSION_TOP_MASK_PHF));
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_SHOP_FILE));
    BpCtlRelease(BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + START_MENU_GALLERY_FILE));
    OutgameBgRelease();
    CmnFadeLayerTermItem();
    if (start_menu_sel_win != nullptr) {
        delete start_menu_sel_win;              // vtable +4: deleting destructor
        start_menu_sel_win = nullptr;           // PC: FF3 leaves the pointer (the next init clears it)
    }
    SndBgmStop(GetPALMode() != 0 ? 0x19 : 0x1e, 0);
    return 1;
}

// FF3 0x001a8958
int one_StartMenu_Main(void)
{
    GetGPhaseCtrl()->SetNext(0x50, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 55 per frame (before its children), once the loads are done: the
// backdrop, the fade layer, the message window with the layout anims, the
// layouts and the screen fade.
// FF3 0x001a8998

void pre_StartMenu_Main(void)
{
    capCtl *layer;

    if (start_menu_loaded == 0) {
        return;
    }
    OutgameBgDraw();
    layer = GetCmnFadeLayer();
    if (CmnFadeLayerIsReady() != 0) {
        layer->Update();
    }
    if (start_menu_sel_win != nullptr) {
        start_menu_sel_win->Main();
        LayoutAnimMain();
    }
    BpCtlMain();
    FadeMain();
}

// FF3 0x001a8a30 (StartMenuMainGPhaseCtrlAtExit), the GPhaseCtrl atexit
// thunk after this function, ends FF3's third file.
// FF3 0x001a8a20
void after_StartMenu_Main(void)
{
}

// ══════════════════════════════════════════════════════════════════════
// The extra-mission menu (phases 83 StartMenu_Mission, 114 MissionMain,
// 119 MissionSel, 115 MissionSave), FF3's fourth file 0x1a8a50..0x1aa910:
// MISSION_TOP_PHF + language with its mask layout, ten groups of missions
// (finder_camera.h's Mission* lists), one row per mission 0x22 apart:
// name and condition (message 0x6f, six per mission; the description
// is the window's), state (message 100, 0xf + state) and best record.
// ══════════════════════════════════════════════════════════════════════

#define SM_MISSION_ROW_DY   0x22    // pixels per mission row
#define SM_MISSION_MSG      0x6f    // six messages per mission (MissionGetMsgNo)
#define SM_MISSION_WIN_ITEM 0x419   // the window's / fade layer's item
#define SM_MISSION_BGM      0xfb3

static int start_menu_mission_quit;     // sbss 0x373184 (gp-0x36c): the leave prompt was answered (phase 115 then saves)
static int start_menu_mission_back;     // sbss 0x373188 (gp-0x368): the menu opened back from a mission

static int StartMenuMissionFile(void)
{
    return GetSysWrk()->GetLanguage() + MISSION_TOP_PHF;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 12's end callback of the leave prompt: cancel puts the fade layer
// back on the list's label.
// FF3 0x001a8a50

static void StartMenuMissionQuitMsgEnd(int result)
{
    if (result == 3) {
        CmnFadeLayerSwitchItem(0x13);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The list's sprites each frame: the portrait of the cursor mission's
// player (0x40e..0x410), the cursor bar on its row, the group's title, and
// the ten group tabs (hidden while locked, warm on the group shown).
// FF3 0x001a8a80
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

static void StartMenuMissionDispList(void)
{
    static const short plyr_item[3]  = { 0x40e, 0x40f, 0x410 };     /* sdata 0x370328 */
    static const short title_item[MISSION_GROUP_NUM] =              // built on the stack
    {
        0x3f5, 0x3f6, 0x3f7, 0x3f8, 0x3f9, 0x426, 0x427, 0x446, 0x447, 0x448,
    };
    static const short tab_item[MISSION_GROUP_NUM] =                // built on the stack
    {
        0x3f0, 0x3f1, 0x3f2, 0x3f3, 0x3f4, 0x418, 0x41a, 0x41b, 0x41c, 0x41d,
    };
    static const short tab_name[MISSION_GROUP_NUM] =                // built on the stack
    {
        0x406, 0x407, 0x408, 0x449, 0x44a, 0x44b, 0x44c, 0x44d, 0x44e, 0x44f,
    };
    static const short tab_mark[MISSION_GROUP_NUM] =                // built on the stack
    {
        0x409, 0x40a, 0x40b, 0x411, 0x412, 0x413, 0x414, 0x415, 0x416, 0x417,
    };
    FINDER_BANK_ENTRY *def;
    B2D_ITEM          *item;
    B2D_ITEM          *name;
    B2D_ITEM          *mark;
    int                handle;
    int                no;
    int                open;
    int                i;

    handle = BpCtlGetHandleByFileNo(StartMenuMissionFile());
    no = MissionCsrGetNo();
    def = MissionGetDef(MissionCsrGetGroup(), MissionCsrGetNo());
    for (i = 0; i < 3; i++) {
        B2dSprSetDisp(B2dCtlFindItem(handle, plyr_item[i]), i == (s_char)def->plyr);
    }

    item = B2dCtlFindItem(handle, 0x422);
    B2dSprSetPos(item, 0, no * SM_MISSION_ROW_DY);
    B2dSprFreezePart(handle, 0x422, B2D_PART_X, 0);
    B2dSprFreezePart(handle, 0x422, B2D_PART_Y, 0);

    for (i = 0; i < MISSION_GROUP_NUM; i++) {
        item = B2dCtlFindItem(handle, title_item[i]);
        B2dSprSetDisp(item, i == MissionCsrGetGroup());
    }

    for (i = 0; i < MISSION_GROUP_NUM; i++) {
        open = MissionGroupIsOpen(i);
        item = B2dCtlFindItem(handle, tab_item[i]);
        name = B2dCtlFindItem(handle, tab_name[i]);
        mark = B2dCtlFindItem(handle, tab_mark[i]);
        B2dSprFreezePart(handle, tab_name[i], B2D_PART_RGB, 0);
        B2dSprFreezePart(handle, tab_mark[i], B2D_PART_RGB, 0);
        if (i == MissionCsrGetGroup()) {
            B2dSprSetRgb(name, 0xff, 200, 0xa0);
            B2dSprSetRgb(mark, 0xff, 200, 0xa0);
        } else {
            B2dSprSetRgb(name, 0x80, 0x80, 0x80);
            B2dSprSetRgb(mark, 0x80, 0x80, 0x80);
        }
        B2dSprSetDisp(item, open);
        B2dSprSetDisp(name, open);
        B2dSprSetDisp(mark, open);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Sprite hook of the row pieces 0x3fe..0x401, drawn once per mission of the
// group: 0x3fe / 0x400 on the cursor's row, 0x3ff / 0x401 on the others;
// 0x3fe / 0x3ff only for state 5, 0x400 / 0x401 for every state but 5 and 7.
// FF3 0x001a8df8 (no Ghidra function before 2026-10-15: inside 0x1a8a80)

static int StartMenuMissionDrawRowSpr(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw;
    int           id;
    int           no;
    int           group;
    int           num;
    int           state;
    int           csr_kind;
    int           i;

    draw = *args;
    id = B2dGetDrawItem()->id;
    no = MissionCsrGetNo();
    group = MissionCsrGetGroup();
    num = MissionGetNum(group);
    csr_kind = id == 0x3fe || id == 0x400;

    for (i = 0; i < num; i++, draw.t2 += SM_MISSION_ROW_DY) {
        if (csr_kind != (i == no)) {
            continue;
        }
        state = MissionGetState(group, i);
        if ((u_int)(id - 0x3fe) < 2 && state != 5) {
            continue;
        }
        if ((u_int)(id - 0x400) < 2 && (state == 5 || state == 7)) {
            continue;
        }
        B2dDrawSpriteDirect(&draw);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Sprite hook of 0x477 / 0x478: drawn on the rows without a best record.
// FF3 0x001a8f78 (no Ghidra function before 2026-10-15: inside 0x1a8a80)

static int StartMenuMissionDrawNoRecord(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw;
    int           group;
    int           num;
    int           i;

    draw = *args;
    group = MissionCsrGetGroup();
    num = MissionGetNum(group);
    for (i = 0; i < num; i++, draw.t2 += SM_MISSION_ROW_DY) {
        if (MissionGetRecord(group, i)->best == -1) {
            B2dDrawSpriteDirect(&draw);
        }
    }
    return 0;
}

// The rows' message of kind base (0 / 1) of message 0x6f, one per mission:
// the body of the two hooks below (FF3 has two copies, base 1 and 0).

static void StartMenuMissionDrawMsg(const B2D_HOOK_ARGS *args, int base)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         no;
    int         group;
    int         num;
    int         type;
    int         i;

    no = MissionCsrGetNo();
    group = MissionCsrGetGroup();
    num = MissionGetNum(group);
    for (i = 0; i < num; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * SM_MISSION_ROW_DY);
        StartMenuOptionSetRowColor(i == no);
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, StartMenuMissionFile(), xy);
        PrintMsgPos(SM_MISSION_MSG, MissionGetMsgNo(base, group, i), pos, 0x2d, alpha, 0, type);
    }
    SetDefaultGsDrawState();
}

// Text hook of item 0x428.
// FF3 0x001a9088
// callcheck: GetLabelSuffixType, GetLanguage, GetSysWrk, LayoutItemAddPos, MissionCsrGetGroup, MissionCsrGetNo
// callcheck: MissionGetMsgNo, MissionGetNum, PrintMsgPos, SetCurColorRGB, SetDefaultGsDrawState (StartMenuMissionDrawMsg)
static int StartMenuMissionDrawMsg1(const B2D_HOOK_ARGS *args)
{
    StartMenuMissionDrawMsg(args, 1);
    return 0;
}

// Text hook of item 0x42d.
// FF3 0x001a91e0 (no Ghidra function before 2026-10-15: inside 0x1a9088)
// callcheck: GetLabelSuffixType, GetLanguage, GetSysWrk, LayoutItemAddPos, MissionCsrGetGroup, MissionCsrGetNo
// callcheck: MissionGetMsgNo, MissionGetNum, PrintMsgPos, SetCurColorRGB, SetDefaultGsDrawState (StartMenuMissionDrawMsg)
static int StartMenuMissionDrawMsg0(const B2D_HOOK_ARGS *args)
{
    StartMenuMissionDrawMsg(args, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x432: each mission's state (message 100, 0xf + state;
// nothing for state 7).
// FF3 0x001a9338 (no Ghidra function before 2026-10-15: inside 0x1a9088)
// callcheck: GetLanguage, GetSysWrk, SetCurColorRGB (StartMenuMissionFile, StartMenuOptionSetRowColor)

static int StartMenuMissionDrawState(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         no;
    int         group;
    int         num;
    int         state;
    int         type;
    int         i;

    no = MissionCsrGetNo();
    group = MissionCsrGetGroup();
    num = MissionGetNum(group);
    for (i = 0; i < num; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * SM_MISSION_ROW_DY);
        state = MissionGetState(group, i);
        StartMenuOptionSetRowColor(i == no);
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, StartMenuMissionFile(), xy);
        if (state != 7) {
            PrintMsgPos(100, state + 0xf, pos, 0x2d, alpha, 0, type);
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x437: each mission's best record in its condition's
// unit.
// FF3 0x001a9490
// callcheck: GetLanguage, GetSysWrk, SetCurColorRGB (StartMenuMissionFile, StartMenuOptionSetRowColor)


static int StartMenuMissionDrawRecord(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         no;
    int         group;
    int         num;
    int         type;
    int         i;

    no = MissionCsrGetNo();
    group = MissionCsrGetGroup();
    num = MissionGetNum(group);
    for (i = 0; i < num; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * SM_MISSION_ROW_DY);
        StartMenuOptionSetRowColor(i == no);
        LayoutItemAddPos(pos, name, StartMenuMissionFile(), xy);
        type = MissionGetDef(group, i)->type;
        StartMenuMissionDrawValue(type, MissionGetRecord(group, i)->best, pos, alpha);
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Text hook of item 0x442: the sub menu's two entries (message 100, 0 / 1),
// 0x1e apart, pale green on the cursor.
// FF3 0x001a95e0 (no Ghidra function before 2026-10-15: inside 0x1a9490)
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

static int StartMenuMissionDrawSubMenu(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    int        *xy    = (int *)args->t1;
    u_char      alpha = (u_char)args->stack[2];
    float       pos[2];
    int         csr;
    int         type;
    int         i;

    csr = MenuCsrGetPos(START_MENU_CSR_MISSION_SUB);
    for (i = 0; i < 2; i++) {
        pos[0] = 0.0f;
        pos[1] = (float)(i * 0x1e);
        if (i == csr) {
            SetCurColorRGB(0xdb, 0xe4, 0xbe);
        } else {
            SetCurColorRGB(0x8d, 0x97, 0x80);
        }
        type = GetLabelSuffixType(name);
        LayoutItemAddPos(pos, name, StartMenuMissionFile(), xy);
        PrintMsgPos(100, i, pos, 0x2d, alpha, 0, type);
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC start_menu_mission_hooks[] =     /* rodata 0x358710, copied to the stack */
{
    { StartMenuMissionDrawMsg1,    0, 0x428, nullptr },
    { StartMenuMissionDrawMsg0,    0, 0x42d, nullptr },
    { StartMenuMissionDrawState,   0, 0x432, nullptr },
    { StartMenuMissionDrawRecord,  0, 0x437, nullptr },
    { StartMenuMissionDrawSubMenu, 0, 0x442, nullptr },
    { nullptr,                    -1, -1,    nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a9708
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

static void StartMenuMissionSetHooks(void)
{
    static const int row_items[4] = { 0x3fe, 0x3ff, 0x400, 0x401 };   // built on the stack
    static const int no_rec_items[2] = { 0x477, 0x478 };             /* sdata 0x370330 */
    static const B2D_ITEM_HOOK row_hook    = { StartMenuMissionDrawRowSpr };
    static const B2D_ITEM_HOOK no_rec_hook = { StartMenuMissionDrawNoRecord };
    int handle;
    int i;

    handle = BpCtlGetHandleByFileNo(StartMenuMissionFile());
    for (i = 0; i < 4; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, row_items[i]), &row_hook);
    }
    for (i = 0; i < 2; i++) {
        B2dCtlSetItemHook(B2dCtlFindItem(handle, no_rec_items[i]), &no_rec_hook);
    }
    B2dSetHookList(handle, start_menu_mission_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 119's sprites each frame: the list's, the sub menu pinned below
// the first two rows (at the top past them), its cursor 0x1e per entry;
// 0x454 / 0x455 hidden.
// FF3 0x001a9868
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

static void StartMenuMissionDispSub(void)
{
    static const short sub_item[2]  = { 0x445, 0x458 };   /* sdata 0x370338 */
    static const short hide_item[2] = { 0x454, 0x455 };   /* sdata 0x370340 */
    int handle;
    int csr;
    int y;
    int i;

    StartMenuMissionDispList();
    handle = BpCtlGetHandleByFileNo(StartMenuMissionFile());
    csr = MenuCsrGetPos(START_MENU_CSR_MISSION_SUB);
    y = 0x96;
    if (MissionCsrGetNo() > 1) {
        y = 0;
    }
    for (i = 0; i < 2; i++) {
        B2dSprSetPos(B2dCtlFindItem(handle, sub_item[i]), 0, y);
        B2dSprFreezePart(handle, sub_item[i], B2D_PART_X, 0);
        B2dSprFreezePart(handle, sub_item[i], B2D_PART_Y, 0);
    }
    B2dSprSetPos(B2dCtlFindItem(handle, 0x457), 0, csr * 0x1e + y);
    B2dSprFreezePart(handle, 0x457, B2D_PART_X, 0);
    B2dSprFreezePart(handle, 0x457, B2D_PART_Y, 0);
    for (i = 0; i < 2; i++) {
        B2dSprSetDisp(B2dCtlFindItem(handle, hide_item[i]), 0);
    }
}

// The cursor mission's description (message 0x6f, kind 2).
// FF3 0x001a9a08
static int StartMenuMissionGetMsgNo(void)
{
    return MissionGetMsgNo(2, MissionCsrGetGroup(), MissionCsrGetNo());
}

// ──────────────────────────────────────────────────────────────────────
// A record in its condition's unit; -1 shows nothing. Types 2 and 6 are
// play times (h:mm:ss, ending 86 left of pos), the others a number centred
// on x 0x21c with its unit after it (message 100: 0x25 type 3, 0x26 type
// 4, 0x27 type 5, else 0x15).
// FF3 0x001a9a40

void StartMenuMissionDrawValue(int type, int value, const float *pos, int alpha)
{
    float tpos[2];
    int   hms[3];
    int   unit;
    int   digits;
    int   width;
    int   x;
    int   v;

    if (value == -1) {
        return;
    }
    if (type == 2 || type == 6) {
        tpos[1] = pos[1];
        tpos[0] = pos[0] - 84.0f - 2.0f;            /* sdata 0x370344 / 0x370348 */
        PlayTimeToHms(hms, value);
        StartMenuDrawTime(hms, tpos, alpha, 1);
        return;
    }
    switch (type) {
    case 3:  unit = 0x25; break;
    case 4:  unit = 0x26; break;
    case 5:  unit = 0x27; break;
    default: unit = 0x15; break;
    }
    digits = 0;
    for (v = value; v > 0; v /= 10) {
        digits++;
    }
    if (digits == 0) {
        digits = 1;
    }
    width = MsgGetLineWidth(GetMsgDataAddr(100, unit), nullptr);
    x = digits * 0xe - (width + digits * 0xe) / 2 + 0x21c;
    PrintNumber_N(value, digits, x - digits * 0xe, (int)pos[1], 0x2d, (u_char)alpha, 0, 1, 0);
    tpos[0] = (float)x;
    tpos[1] = pos[1];
    PrintMsgPos(100, unit, tpos, 0x2d, alpha, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// ITEM_RESET in start mode 2: the mission entry's six item counts (item 5
// first, then 0..4).
// FF3 0x001a9c10

void MissionItemReset(FINDER_BANK_ENTRY *entry)
{
    ItemSetNum(5, entry->item_num[0], 0);
    ItemSetNum(0, entry->item_num[1], 0);
    ItemSetNum(1, entry->item_num[2], 0);
    ItemSetNum(2, entry->item_num[3], 0);
    ItemSetNum(3, entry->item_num[4], 0);
    ItemSetNum(4, entry->item_num[5], 0);
}

// ──────────────────────────────────────────────────────────────────────
// Two-digit numbers with leading zeros and the separator message (0x69,
// 0xb) 0x1c after each; 0x2a per field.
// FF3 0x001a9c90

void StartMenuDrawTime(const int *hms, const float *pos, int alpha, int with_hours)
{
    u_char *sep;
    int     x;
    int     y;

    y = (int)pos[1];
    x = (int)pos[0];
    if (hms == nullptr) {
        return;
    }
    sep = GetMsgDataAddr(0x69, 0xb);
    if (with_hours != 0) {
        PrintNumber_N(hms[0], 2, x, y, 0x2d, (u_char)alpha, 0, 1, 1);
        PrintMsg_ArrangeOneLine(sep, x + 0x1c, y, 0x2d, alpha, 0, 0, 0, 0);
        x += 0x2a;
    }
    PrintNumber_N(hms[1], 2, x, y, 0x2d, (u_char)alpha, 0, 1, 1);
    PrintMsg_ArrangeOneLine(sep, x + 0x1c, y, 0x2d, alpha, 0, 0, 0, 0);
    PrintNumber_N(hms[2], 2, x + 0x2a, y, 0x2d, (u_char)alpha, 0, 1, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 83: the window shows the cursor mission's description. Back from a
// mission (not from StartMenu_Top) a clear adds the mission's points, the
// story start step goes to 2 and the cursor returns to that mission.
// FF3 0x001a9e10
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

int init_StartMenu_Mission(void)
{
    FINDER_MODE_WRK *wrk;

    start_menu_sel_win->SetItem(BpCtlGetHandleByFileNo(StartMenuMissionFile()), SM_MISSION_WIN_ITEM);
    start_menu_sel_win->StartMsg(SM_MISSION_MSG, StartMenuMissionGetMsgNo(), 1);
    start_menu_mission_quit = 0;
    start_menu_mission_back = 0;
    if (GetGPhaseCtrl()->from != 0x50) {
        wrk = GetFinderModeWrk();
        if (wrk->result != 0) {
            IngameWrkAddPoint(wrk->GetDef()->point);
        }
        StoryStartSetStep(2);
        MissionCsrSetFromModeWrk();
        start_menu_mission_back = 1;
    }
    return 1;
}

// FF3 0x001a9f08
void pre_StartMenu_Mission(void)
{
}

// FF3 0x001a9f10
int one_StartMenu_Mission(void)
{
    GetGPhaseCtrl()->SetNext(0x72, 0);
    return 1;
}

// FF3 0x001a9f50
void after_StartMenu_Mission(void)
{
}

// FF3 0x001a9f58
int end_StartMenu_Mission(void)
{
    MenuCsrSetPos(START_MENU_CSR_TOP, 2);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 114: the list plays in with the mission BGM. The fade layer takes
// label 0x13 coming from phase 83 or back from a save (115) not headed for
// the sub menu, else label 0.
// FF3 0x001a9f80
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

int init_StartMenu_MissionMain(void)
{
    GPHASE_CTRL *ctrl;
    int          handle;
    int          label;

    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        SndBgmPlay(SM_MISSION_BGM, 100, 0x1000, GetPALMode() != 0 ? 0x19 : 0x1e, nullptr, 1, 0);
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuMissionFile()), "in");
        BpPlayLabel(BpCtlGetHandleByFileNo(MISSION_TOP_MASK_PHF), "in");
        handle = BpCtlGetHandleByFileNo(StartMenuMissionFile());
        ctrl = GetGPhaseCtrl();
        label = 0x13;
        if (ctrl->from != 0x53) {
            label = 0;
            if (ctrl->from == 0x73 && ctrl->now != 0x77) {
                label = 0x13;
            }
        }
        CmnFadeLayerShowItem(label, handle, SM_MISSION_WIN_ITEM);
        start_menu_sel_win->StartMsg(SM_MISSION_MSG, StartMenuMissionGetMsgNo(), 1);
        start_menu_sel_win->SetItem(handle, SM_MISSION_WIN_ITEM);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuMissionDispList();
    return BpCtlIsLoadEnd(StartMenuMissionFile()) != 0;
}

// FF3 0x001aa150
void pre_StartMenu_MissionMain(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Phase 114 per frame. Square opens the sub menu (119); OK keeps the story
// game aside, sets the finder-mode work for the mission and starts it
// (start mode 2, phase 29 Story_Start); cancel returns to StartMenu_Top,
// or after a mission asks first (phase 12, message 100/2: OK saves
// through 115, the alternative leaves, cancel stays). Otherwise L2/R2 and
// left/right page the groups.
// FF3 0x001aa158
// callcheck: operator_new, CExtraMissionSheltCtor (new CExtraMissionShelt)

int one_StartMenu_MissionMain(void)
{
    PHASE12_PARAM       param;
    CExtraMissionShelt *shelt;
    int                 next;
    int                 link;

    MenuMouseCsrBar(MissionCsrGetWork(), BpCtlGetHandleByFileNo(StartMenuMissionFile()), 0x422, 0.0f,
                    (float)SM_MISSION_ROW_DY);  // PC
    MissionCsrUpdate();
    start_menu_sel_win->StartMsg(SM_MISSION_MSG, StartMenuMissionGetMsgNo(), 1);
    StartMenuMissionDispList();
    if (MissionCsrIsMovedSe() != 0) {
        return 0;
    }
    link = 0;
    if (CheckPadTrgPlaySe(0x80, SM_SE_OK, 0) != 0) {             // square
        next = 0x77;
    } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SM_SE_OK, 0) != 0) {
        shelt = new CExtraMissionShelt;                         // operator_new(0xc) + CExtraMissionSheltCtor
        shelt->Save();
        FinderModeWrkStart(GetFinderModeWrk(), MissionCsrGetGroup(), MissionCsrGetNo());
        title_start_flg.mode = 2;
        next = 0x1d;
    } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
        if (start_menu_mission_back != 0) {
            param.msg0 = 100;
            param.msg1 = 2;
            param.type = 1;
            param.next_ok = 0x73;
            param.next_alt = 0x50;
            param.next_cancel = 0x72;
            param._0e = 0;                                      // FF3 leaves it unset
            param.end_func = StartMenuMissionQuitMsgEnd;
            param.win = start_menu_sel_win;
            start_menu_mission_quit = 1;
            CmnFadeLayerSwitchItem(0);
            Phase12ParamSet(&param);
            next = 0xc;
            link = 1;
        } else {
            next = 0x50;
        }
    } else {
        MissionCsrGroupMove();
        StartMenuMissionDispList();
        return 0;
    }
    GetGPhaseCtrl()->SetNext(next, link);
    return 1;
}

// FF3 0x001aa380
void after_StartMenu_MissionMain(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Phase 114 end: the list and its mask play out, the BGM stops; done when
// the list's animation ends.
// FF3 0x001aa388
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

int end_StartMenu_MissionMain(void)
{
    int end;

    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuMissionFile()), "out");
        BpPlayLabel(BpCtlGetHandleByFileNo(MISSION_TOP_MASK_PHF), "out");
        SndBgmStop(GetPALMode() != 0 ? 0x19 : 0x1e, 0);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuMissionDispList();
    end = BpCtlIsLoadEnd(StartMenuMissionFile());
    if (end != 0) {
        CmnFadeLayerTermItem();
        start_menu_sel_win->SetAlphaItem(nullptr);
    }
    return end != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 119 (under 114): the sub menu plays in; from the list the fade
// layer goes to label 0.
// FF3 0x001aa4b0
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

int init_StartMenu_MissionSel(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuMissionFile()), "menu_in");
        if (GetGPhaseCtrl()->from == 0x72) {
            CmnFadeLayerSwitchItem(0);
        }
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuMissionDispSub();
    return BpCtlIsLoadEnd(StartMenuMissionFile()) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 119 per frame: OK goes to 115 with the entry picked, cancel back
// to the list.
// FF3 0x001aa5b0

int one_StartMenu_MissionSel(void)
{
    int next;

    MenuMouseCsrBar(MenuCsrGetWork(START_MENU_CSR_MISSION_SUB), BpCtlGetHandleByFileNo(StartMenuMissionFile()), 0x457,
                    0.0f, 30.0f);       // PC
    MenuCsrUpdate(START_MENU_CSR_MISSION_SUB, 0);
    StartMenuMissionDispSub();
    if (MenuCsrIsMovedSe(START_MENU_CSR_MISSION_SUB, SM_SE_MOVE) != 0) {
        return 0;
    }
    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], SM_SE_OK, 0) != 0) {
        next = 0x73;
    } else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], SM_SE_CANCEL, 0) != 0) {
        next = 0x72;
    } else {
        return 0;
    }
    GetGPhaseCtrl()->SetNext(next, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 119 end: the sub menu plays out; back to the list the fade layer
// returns to label 0x13.
// FF3 0x001aa680
// callcheck: GetLanguage, GetSysWrk (StartMenuMissionFile)

int end_StartMenu_MissionSel(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        BpPlayLabel(BpCtlGetHandleByFileNo(StartMenuMissionFile()), "menu_out");
        if (GetGPhaseCtrl()->now == 0x72) {
            CmnFadeLayerSwitchItem(0x13);
        }
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    StartMenuMissionDispSub();
    return BpCtlIsLoadEnd(StartMenuMissionFile()) != 0;
}

// FF3 0x001aa780
int init_StartMenu_MissionSave(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 115: runs a linked child -- phase 8 for the sub menu's second
// entry, else 69 Story_SaveGame_Mc (the sub menu's first entry, or the
// leave prompt's OK) -- then returns to the sub menu, or after the prompt
// to the list (nothing saved) or StartMenu_Top (saved).
// FF3 0x001aa788

int one_StartMenu_MissionSave(void)
{
    int next;

    if (GetGPhaseCtrl()->leaf->state_cnt == 0) {
        if (start_menu_mission_quit == 0 && MenuCsrGetPos(START_MENU_CSR_MISSION_SUB) != 0) {
            next = 8;
        } else {
            next = 0x45;
        }
        GetGPhaseCtrl()->SetNext(next, 1);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 1;
    }
    GetGPhaseTree()->Reset();
    if (start_menu_mission_quit == 0) {
        next = 0x77;
    } else if (title_start_flg.save == 0) {
        next = 0x72;
    } else {
        next = 0x50;
    }
    GetGPhaseCtrl()->SetNext(next, 0);
    return 1;
}

// FF3 0x001aa8f0
int end_StartMenu_MissionSave(void)
{
    start_menu_mission_quit = 0;
    return 1;
}

// FF3 0x001aa908 (StartMenuMissionGPhaseCtrlAtExit) is the GPhaseCtrl atexit
// thunk that ends FF3's fourth file.

// ──────────────────────────────────────────────────────────────────────
// The finder-mode work for a new mission: its group and number, the
// counters cleared, +0x24 the frame rate (50 PAL / 60), +0x2c on. It sits
// after the fourth file's thunk: probably the first function of the
// finder-mode file (FINDER_MODE_WRK::GetDef 0x1aa9a8 follows, ingame/mission/mission_mode.c).
// FF3 0x001aa940

void FinderModeWrkStart(FINDER_MODE_WRK *wrk, int group, int no)
{
    wrk->mission_grp = group;
    wrk->stop = 1;
    wrk->mission_no = no;
    wrk->score = 0;
    wrk->best_shot = 0;
    wrk->kills = 0;
    wrk->shots = 0;
    wrk->max_level = 0;
    wrk->time_348 = 0;
    wrk->result = 0;
    wrk->time = 0;
    wrk->hud_fade = GetPALMode() != 0 ? 0x32 : 0x3c;
}

// ──────────────────────────────────────────────────────────────────────
// The clear-count singleton (bss 0x41e490, guard sbss gp+0x698).
// FinderStateWrkInit runs in FINDER_STATE_WRK's constructor.
// FF3 0x002ab688
// callcheck: FinderStateWrkInit (FINDER_STATE_WRK constructor)

FINDER_STATE_WRK *GetFinderStateWrk(void)
{
    static FINDER_STATE_WRK wrk;        /* bss 0x41e490 */
    return &wrk;
}

// ──────────────────────────────────────────────────────────────────────
// The unlock-slot singleton (bss 0x424b10, guard sbss gp+0x790).
// FinderSlotWrkInit runs in FINDER_SLOT_WRK's constructor.
// FF3 0x002b90b8
// callcheck: FinderSlotWrkInit (FINDER_SLOT_WRK constructor)

FINDER_SLOT_WRK *GetFinderSlotWrk(void)
{
    static FINDER_SLOT_WRK wrk;         /* bss 0x424b10 */
    return &wrk;
}
