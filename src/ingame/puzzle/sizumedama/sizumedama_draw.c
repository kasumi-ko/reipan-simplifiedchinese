/* ==========================================================================
 *  ingame/puzzle/sizumedama/sizumedama_draw.c
 *
 *  The sizumedama puzzle's board definition and layout hooks (see
 *  sizumedama_draw.h). FF3 0x24c1c0..0x24ca90, FF3's own code.
 * ======================================================================== */
#include "ingame/puzzle/sizumedama/sizumedama_draw.h"

#ifdef REIPAN_SLPS_25544_CHS
#include "graphics/graph2d/message.h"
#include "reipan/io/reipan_menu_mouse.h"
#endif

#include "graphics/motion/anicode.h"                    // B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState
#include "ingame/menu/menu_bg.h"                        // B2dHookPrintMsgLine
#include "ingame/puzzle/puzzle.h"                       // PuzzleGetLayoutFile
#include "ingame/puzzle/sizumedama/sizumedama_pzl.h"    // sizume_board
#include "ingame/subtitle/subtitle.h"                   // SubTitleMain
#include "main/gphase.h"                                // GetGPhaseCtrl
#include "system/b2d/B2dCtl.h"                          // B2dCtlFindItem
#include "system/b2d/B2dSpr.h"                          // B2dSprSetDisp
#include "system/b2d/BpCtl.h"                           // BpCtlGetHandleByFileNo, BpGetDrawHandle, B2dDrawSprite*
#include "system/eeiop/cd_file_dat.h"                   // PAZZLE_SHIZUME_*_STR_STR

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// The one board: no stone placed at the start; the answer puts the clips
// in order (pedestal 0 stone 3 = clip 01, 1 stone 0 = 02, 2 stone 2 = 03,
// 3 stone 1 = 04). Lines 0..3 are the stones' names, 4..7 their subtitle
// lines. data 0x361ec8.
static const SIZUME_BOARD_DEF sizume_board_def[SIZUME_BOARD_DEF_NUM] =
{
    {
        { -1, -1, -1, -1 },
        {  3,  0,  2,  1 },
        {
            { 0, 4, PAZZLE_SHIZUME_02_STR_STR },
            { 1, 5, PAZZLE_SHIZUME_04_STR_STR },
            { 2, 6, PAZZLE_SHIZUME_03_STR_STR },
            { 3, 7, PAZZLE_SHIZUME_01_STR_STR },
        },
        { -1, -1, PAZZLE_SHIZUME_STR_STR },
    },
};

// The stones' sprites by stone. data 0x330c50 -> 0x361f28..
static const char *const sizume_sphere_name[SIZUME_STONE_NUM] =
{
    "R sphere_A", "R sphere_B", "R sphere_C", "R sphere_D",
};

// The button guide's items (sdata 0x371fe0): 0x405 hides on an empty
// pedestal, 0x413 on one with a stone.
static const short sizume_button_item[4] = { 0x403, 0x404, 0x405, 0x413 };

#define SIZUME_ITEM_BTN_EMPTY   0x405   // hidden on an empty pedestal
#define SIZUME_ITEM_BTN_STONE   0x413   // hidden on a stone

static const char sizume_item_arw_l[] = "R arw_L";      // sdata 0x371ff0
static const char sizume_item_arw_r[] = "R arw_R";      // sdata 0x371ff8

#define SIZUME_PEDESTAL_DX      0x78    // pedestals 120 px apart; items sit at pedestal 0
#define SIZUME_MSG              0x78    // the puzzle's message group
#define SIZUME_ALPHA_SCALE      (1.0f / 128.0f)         // sdata 0x371fd0 / 0x371fd8
#define SIZUME_PHASE_MSG        12

// ──────────────────────────────────────────────────────────────────────
// The board definition
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024c1c0.

const SIZUME_BOARD_DEF *SizumeGetBoardDef(int idx)
{
    return &sizume_board_def[idx];
}

// ──────────────────────────────────────────────────────────────────────
// Layout hooks
// ──────────────────────────────────────────────────────────────────────

// R flea_sphere (a sprite): solved, the glow on all four stones; else on
// the cursor pedestal when it holds a stone.
// FF3 0x0024c1e0.

static int SizumeDrawFleaSphere(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw = *args;

    if (sizume_board->solved != 0)
    {
        for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
        {
            draw.t1 = (short)i * SIZUME_PEDESTAL_DX + args->t1;
            B2dDrawSpriteDirect(&draw);
        }
    }
    else if (sizume_board->slot[sizume_board->cursor] != -1)
    {
        draw.t1 = (short)sizume_board->cursor * SIZUME_PEDESTAL_DX + args->t1;
        B2dDrawSpriteDirect(&draw);
    }
    SetDefaultGsDrawState();
    return 0;
}

// R flea_base (a sprite): the glow on the cursor pedestal while it is empty.
// FF3 0x0024c348 (no Ghidra function before this session; it was the tail
// of 0x24c1e0's body).

static int SizumeDrawFleaBase(const B2D_HOOK_ARGS *args)
{
    if (sizume_board->solved == 0 && sizume_board->slot[sizume_board->cursor] == -1)
    {
        B2D_HOOK_ARGS draw = *args;

        draw.t1 = (short)sizume_board->cursor * SIZUME_PEDESTAL_DX + args->t1;
        B2dDrawSpriteDirect(&draw);
    }
    SetDefaultGsDrawState();
    return 0;
}

// sphere (t1 the item rect, t3 its colour, stack[2] its alpha): each
// pedestal's stone.
// FF3 0x0024c3d0.

static int SizumeDrawSpheres(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    u_int      rgb   = (u_int)args->t3;
    float      alpha = (float)(int)args->stack[2] * SIZUME_ALPHA_SCALE;

    for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
    {
        u_int stone = (u_int)sizume_board->slot[i];

        if (stone < SIZUME_STONE_NUM)
        {
            B2dDrawSpriteByName(BpGetDrawHandle(), sizume_sphere_name[stone],
                                (short)i * SIZUME_PEDESTAL_DX + rect[0], rect[1],
                                rgb & 0xff, rgb >> 8 & 0xff, rgb >> 16 & 0xff,
                                alpha, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);   // sdata 0x371fd4: 1.0
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// sphere2D: the stone under the stone cursor.
// FF3 0x0024c530.

static int SizumeDrawSphere2D(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    u_int      rgb   = (u_int)args->t3;
    u_int      stone = (u_int)sizume_board->stone;
    float      alpha = (float)(int)args->stack[2] * SIZUME_ALPHA_SCALE;

    if (stone < SIZUME_STONE_NUM)
    {
        B2dDrawSpriteByName(BpGetDrawHandle(), sizume_sphere_name[stone], rect[0], rect[1],
                            rgb & 0xff, rgb >> 8 & 0xff, rgb >> 16 & 0xff,
                            alpha, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);       // sdata 0x371fdc: 1.0
    }
    SetDefaultGsDrawState();
    return 0;
}

// A message line centred in a text item's rect (rect[0] .. rect[6] across,
// at rect[1]). PC: a shared helper; FF3 builds the point inline in both
// callers.
static void SizumePrintLine(int msg, const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;
    int        xy[2];

    xy[0] = (rect[0] + rect[6]) / 2;
    xy[1] = rect[1];
    B2dHookPrintMsgLine(SIZUME_MSG, msg, 2, xy, (u_int)args->t3, (int)args->stack[2], (u_int)args->stack[3]);
}

// spherename_M: the name of the stone under the stone cursor.
// FF3 0x0024c628.
// callcheck: B2dHookPrintMsgLine (SizumePrintLine)

static int SizumeDrawSphereName(const B2D_HOOK_ARGS *args)
{
    int msg = sizume_board->GetStoneName(sizume_board->stone);

    if (msg >= 0)
        SizumePrintLine(msg, args);
    SetDefaultGsDrawState();
    return 0;
}

// jimaku_M: the cursor pedestal's subtitle line; solved, the subtitle of
// the recording while it plays.
// FF3 0x0024c6c0.
// callcheck: B2dHookPrintMsgLine (SizumePrintLine)

static int SizumeDrawJimaku(const B2D_HOOK_ARGS *args)
{
    if (sizume_board->solved == 0)
    {
        int msg = sizume_board->GetLine();

        if (msg >= 0)
            SizumePrintLine(msg, args);
    }
    else if (sizume_board->IsPlaying() != 0)
    {
        SubTitleMain(1, 0);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Items 0x40b / 0x400 / 0x40c (sprites around the line): only while the
// cursor pedestal has a line to show.
// FF3 0x0024c788 (no Ghidra function before this session; it was the tail
// of 0x24c6c0's body).

static int SizumeDrawJimakuItem(const B2D_HOOK_ARGS *args)
{
    if (sizume_board->solved == 0 && sizume_board->GetLine() >= 0)
        B2dDrawSpriteDirect(args);
    SetDefaultGsDrawState();
    return 0;
}

#ifdef REIPAN_SLPS_25544_CHS
static int SizumeDrawStopGuide(const B2D_HOOK_ARGS *args)
{
    constexpr int key_strip = 40;
    constexpr int first_row_height = 34;
    if (args->a3 <= key_strip || args->t0 < first_row_height || (args->stack[3] & 0x3000))
        return B2dDrawSpriteDirect(args);

    const int text_x = (key_strip * args->t3 + 512) / 1024;
    B2D_HOOK_ARGS lettering = *args;
    lettering.a1 += key_strip;
    lettering.a3 -= key_strip;
    lettering.t0 = first_row_height;
    lettering.t1 += text_x;
    B2dDrawSpriteDirect(&lettering);

    const int key_h = (20 * args->stack[0] + 512) / 1024;
    const int key_y = (10 * args->stack[0] + 512) / 1024;
    MsgDrawButtonPrompt(REIPAN_MENU_CROSS, (int)args->t1, (int)args->t2 + key_y,
                        text_x - 3, key_h, (int)args->stack[1]);
    SetDefaultGsDrawState();
    return 0;
}
#endif

// data 0x361fb8 (FF3 copies the list to the stack before passing it).
static const B2D_HOOK_REC sizume_hooks[] =
{
    { SizumeDrawFleaSphere,  0, 0,     "R flea_sphere" },      // 0x361f68
    { SizumeDrawFleaBase,    0, 0,     "R flea_base" },        // 0x361f78
    { SizumeDrawSpheres,     0, 0,     "sphere" },             // sdata 0x371fe8
    { SizumeDrawSphere2D,    0, 0,     "sphere2D" },           // 0x361f88
    { SizumeDrawSphereName,  0, 0,     "spherename_M" },       // 0x361f98
    { SizumeDrawJimaku,      0, 0,     "jimaku_M" },           // 0x361fa8
    { SizumeDrawJimakuItem,  0, 0x40b, nullptr },
    { SizumeDrawJimakuItem,  0, 0x400, nullptr },
    { SizumeDrawJimakuItem,  0, 0x40c, nullptr },
#ifdef REIPAN_SLPS_25544_CHS
    { SizumeDrawStopGuide,   0, 0,     "R cap_3" },
#endif
    { nullptr,              -1, -1,    nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// The guide
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024c870.

void SizumeDrawButtons(int show)
{
    int disp = 0;
    int handle;

    if (show != 0)
        disp = GetGPhaseCtrl()->leaf->id != SIZUME_PHASE_MSG;

    handle = BpCtlGetHandleByFileNo(PuzzleGetLayoutFile());
    for (int i = 0; i < 4; i++)
        B2dSprSetDisp(B2dCtlFindItem(handle, sizume_button_item[i]), disp);

    if (disp != 0)
    {
        // the guide not meant for the cursor pedestal goes
        int hide = SIZUME_ITEM_BTN_EMPTY;

        if (sizume_board->slot[sizume_board->cursor] != -1)
            hide = SIZUME_ITEM_BTN_STONE;
        B2dSprSetDisp(B2dCtlFindItem(handle, hide), 0);
    }
}

// FF3 0x0024c970.

void SizumeDrawInit(void)
{
    B2dSetHookList(BpCtlGetHandleByFileNo(PuzzleGetLayoutFile()), sizume_hooks);
    SizumeDrawButtons(sizume_board->show_buttons);
}

// The stone cursor's arrows while there is more than one stone to choose.
// FF3 0x0024c9f0.

void SizumeDraw(void)
{
    int handle = BpCtlGetHandleByFileNo(PuzzleGetLayoutFile());
    int many   = sizume_board->CountFree() > 1;

    B2dCtlSetItemDispByName(handle, sizume_item_arw_l, many);
    B2dCtlSetItemDispByName(handle, sizume_item_arw_r, many);
    SizumeDrawButtons(sizume_board->show_buttons);
}

// FF3 0x0024ca70 is compiler output: the atexit thunk of the GPhaseCtrl
// singleton (GPhaseCtrlTerm(0x41e468)), whose C++ static gphase.c owns here;
// it marks the end of the file.
