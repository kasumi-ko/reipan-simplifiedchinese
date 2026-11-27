/* ==========================================================================
 *  outgame/lang_sel.c
 *
 *  Phase 2, LangSel_Main (see lang_sel.h). FF3 0x1ecdd8..0x1ed658, ported
 *  from MioPan's FF2 lang_sel.c. FF3 keeps FF2's three draw functions and
 *  its pad handling, but loads one PK4 (LANG_SL_PK4 + language: flags,
 *  names and cursor flares) instead of FF2's PK2 + title background, draws
 *  the common fade layer's label 10 behind it, and fades the whole screen
 *  in and out (steps 4 and 6). FF2's LangData_LoadReq is empty in FF3: the
 *  language data loads in phase 1 (LangDataLoadReq).
 *  FF3 0x001ed638 is not written: the compiler-emitted static-destruction
 *  thunk (GPhaseCtrlTerm on the controller singleton) that ends the file.
 * ======================================================================== */
#include "outgame/lang_sel.h"

#include <stddef.h>                     // offsetof

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, CmnFadeLayer*
#include "graphics/graph2d/g2d_draw.h"  // SPRT_DAT, DISP_SPRT, CopySprDToSpr, DispSprD
#include "graphics/graph2d/tim2.h"      // PK4SendVram
#include "ingame/sound/snd_ctrl.h"      // SndSeBankLoad
#include "main/gphase.h"                // GetGPhaseCtrl, GPHASE_CTRL::SetNext
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "system/b2d/BpCtl.h"           // BpCtlMain
#include "system/eeiop/cd_file_dat.h"   // LANG_SL_PK4
#include "system/os/eecdvd.h"           // LoadReq, IsLoadEndAll
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // CheckPadRptPlaySe, CheckPadTrgPlaySe, pad_key
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────

#define LANG_NUM            5

// EE address the flag pack loads to (FF3 passes the constant; it is also
// LocalCopy's buffer 0, free during the boot).
#define LANG_SEL_TEX_ADRS   0x10ef000

// LANG_SEL_CTRL::step
enum
{
    LS_START      = 0,
    LS_LOAD_REQ   = 1,                  // request the pack and the fade layer
    LS_LOAD_WAIT  = 2,
    LS_SE_LOAD    = 3,                  // system SE bank
    LS_FADE_IN    = 4,
    LS_SELECT     = 5,
    LS_FADE_OUT   = 6,
    LS_DATA_REQ   = 7,
    LS_DATA_WAIT  = 8,
};

struct LANG_SEL_CTRL                    /* 0x8, sbss gp-0xb0 */
{
    /* 0x00 */ int    anim_timer;       // cleared only (FF2's background timer)
    /* 0x04 */ u_char step;
    /* 0x05 */ s_char csr;              // 0..4: English, French, German, Spanish, Italian
    /* 0x06 */ u_char alpha;            // screen alpha, 0..0x80
};
static_assert(offsetof(LANG_SEL_CTRL, step) == 0x04, "LANG_SEL_CTRL.step");
static_assert(offsetof(LANG_SEL_CTRL, csr) == 0x05, "LANG_SEL_CTRL.csr");
static_assert(offsetof(LANG_SEL_CTRL, alpha) == 0x06, "LANG_SEL_CTRL.alpha");

static LANG_SEL_CTRL lang_sel_ctrl;     /* sbss gp-0xb0 */

// The pack's pictures (FF2 tim_dat/lang_sl_dat.h). FF3 data 0x00329180.
// 0 is a 512x512 picture no FF3 code draws; 1..5 flags, 6..11 names (French
// in two parts), 12..17 cursor flares (French in two parts).
static SPRT_DAT lang_sl_tex[18] =
{
    { 0x2005f80265322bc0ull,   0,   0, 512, 512,  64, -32, 0, 0x80, 0, 0 },
    { 0x2005c80621312bc0ull,   1,   1,  94,  64, 183,  29, 0, 0x80, 0, 1 },
    { 0x2005c80621312bc0ull,  97,   1,  94,  64, 183, 109, 0, 0x80, 0, 1 },
    { 0x2005c80621312bc0ull,   1,  67,  94,  64, 183, 189, 0, 0x80, 0, 1 },
    { 0x2005c80621312bc0ull,  97,  67,  94,  64, 183, 269, 0, 0x80, 0, 1 },
    { 0x2005c80621312bc0ull,   1, 133,  94,  64, 183, 349, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull,   1,   1, 154,  41, 287,  44, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull, 157,   1,  98,  41, 287, 124, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull,   1,  44,  56,  41, 385, 124, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull,  59,  44, 154,  41, 287, 204, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull,   1,  87, 154,  41, 287, 284, 0, 0x80, 0, 1 },
    { 0x2005c88621412cc0ull,   1, 130, 154,  44, 287, 364, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull,   1,   1, 168,  60, 279,  33, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull, 171,   1,  84,  60, 279, 113, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull,   1,  63,  84,  60, 363, 113, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull,  87,  63, 168,  60, 279, 193, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull,   1, 125, 168,  60, 279, 273, 0, 0x80, 0, 1 },
    { 0x2005c8a621312d40ull,   1, 187, 168,  60, 279, 353, 0, 0x80, 0, 1 },
};

static void LangSelCtrlInit(void);
static int  LangSelTexLoadWait(void);
static int  LangSelMain(void);
static int  LangSelMainPad(void);
static void LangSelDispMain(u_char alpha);
static void LangSelNationalFlagDisp(int off_x, int off_y, u_char alpha);
static void LangSelCsrFlareDisp(int off_x, int off_y, u_char alpha);
static void LangSelCountryNameDisp(int off_x, int off_y, u_char alpha);
static void LangSelDataLoadReq(void);
static int  LangSelDataLoadWait(void);

// The cursor starts on the language already set (ids 2..6; anything else
// starts on English). FF3 0x001ecdd8.
static void LangSelCtrlInit(void)
{
    lang_sel_ctrl.anim_timer = 0;
    lang_sel_ctrl.step = LS_START;
    lang_sel_ctrl.csr = (s_char)(GetSysWrk()->language - 2);
    if ((u_char)lang_sel_ctrl.csr > 4)
        lang_sel_ctrl.csr = 0;
    lang_sel_ctrl.alpha = 0;
}

// Sends the pack to VRAM once it is in. FF3 0x001ece40.
static int LangSelTexLoadWait(void)
{
    int res = IsLoadEndAll();

    if (res != 0)
        PK4SendVram(ReiPan_GetHostPointer(LANG_SEL_TEX_ADRS), 0, -1, -1);
    return res != 0;
}

// Returns 1 on the frame it moves on to phase 3. FF3 0x001ece88.
static int LangSelMain(void)
{
    switch (lang_sel_ctrl.step)
    {
    case LS_START:
        lang_sel_ctrl.step = LS_LOAD_REQ;
        return 0;

    case LS_LOAD_REQ:
        LoadReq(LANG_SL_PK4 + GetSysWrk()->GetLanguage(), ReiPan_GetHostPointer(LANG_SEL_TEX_ADRS));
        GetCmnFadeLayer()->Load();
        lang_sel_ctrl.step = LS_LOAD_WAIT;
        /* fall through */

    case LS_LOAD_WAIT:
        BpCtlMain();
        if (!LangSelTexLoadWait())
            return 0;
        if (!CmnFadeLayerIsReady())
            return 0;
        lang_sel_ctrl.step = LS_SE_LOAD;
        return 0;

    case LS_SE_LOAD:
        if (SndSeBankLoad(-1) != 0)
            return 0;
        lang_sel_ctrl.step = LS_FADE_IN;
        return 0;

    case LS_FADE_IN:
        if (lang_sel_ctrl.alpha + 0x20 < 0x80)
        {
            lang_sel_ctrl.alpha += 0x20;
        }
        else
        {
            lang_sel_ctrl.alpha = 0x80;
            lang_sel_ctrl.step = LS_SELECT;
        }
        LangSelDispMain(lang_sel_ctrl.alpha);
        return 0;

    case LS_SELECT:
        if (!LangSelMainPad())
            return 0;
        LangSelDispMain(lang_sel_ctrl.alpha);
        return 0;

    case LS_FADE_OUT:
        if (lang_sel_ctrl.alpha - 0x20 > 0)
        {
            lang_sel_ctrl.alpha -= 0x20;
            LangSelDispMain(lang_sel_ctrl.alpha);
            return 0;
        }
        GetCmnFadeLayer()->Term();
        GetCmnFadeLayer()->Release();
        lang_sel_ctrl.alpha = 0;
        lang_sel_ctrl.step = LS_DATA_REQ;
        return 0;

    case LS_DATA_REQ:
        LangSelDataLoadReq();
        lang_sel_ctrl.step = LS_DATA_WAIT;
        /* fall through */

    case LS_DATA_WAIT:
        if (!LangSelDataLoadWait())
            return 0;
        GetGPhaseCtrl()->SetNext(3, 0);   // Boot_Main
        return 1;

    default:
        return 0;
    }
}

// Up/down (with repeat) move the cursor round the five rows; OK sets the
// language and starts the fade-out. Always 1 (FF3 keeps the draw call of
// step 5 behind it). FF3 0x001ed098.
static int LangSelMainPad(void)
{
    // PC: the mouse. A row is its flag and its name (the sprites' places are
    // this file's table, LangSelNationalFlagDisp / LangSelCountryNameDisp).
    static const int flag[LANG_NUM] = { 1, 2, 3, 4, 5 };
    static const int name[LANG_NUM][2] = { { 6, -1 }, { 7, 8 }, { 9, -1 }, { 10, -1 }, { 11, -1 } };

    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_VERTICAL, nullptr);
    for (int i = 0; i < LANG_NUM; i++)
    {
        const SPRT_DAT *f = &lang_sl_tex[flag[i]];
        ReiPan_MenuBox box = { (float)f->x, (float)f->y, (float)f->w, (float)f->h };

        for (int j = 0; j < 2 && name[i][j] != -1; j++)
        {
            const SPRT_DAT *n = &lang_sl_tex[name[i][j]];
            const ReiPan_MenuBox word = { (float)n->x, (float)n->y, (float)n->w, (float)n->h };

            box = MenuMouseBoxUnion(box, word);
        }
        ReiPan_MenuMouseSeekBox(lang_sel_ctrl.csr, i, &box, REIPAN_MENU_UP, REIPAN_MENU_DOWN,
                                REIPAN_MENU_CROSS);
    }

    if (CheckPadRptPlaySe(0x1000, 0x1a9b0000, 0))                       // up
    {
        lang_sel_ctrl.csr = (s_char)((lang_sel_ctrl.csr + LANG_NUM - 1) % LANG_NUM);
    }
    else if (CheckPadRptPlaySe(0x4000, 0x1a9b0000, 0))                  // down
    {
        lang_sel_ctrl.csr = (s_char)((lang_sel_ctrl.csr + 1) % LANG_NUM);
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], 0x1a9b0003, 0))
    {
        GetSysWrk()->language = lang_sel_ctrl.csr + 2;
        lang_sel_ctrl.step = LS_FADE_OUT;
    }
    return 1;
}

// The fade layer (label 10) at the screen alpha, then the pack's sprites.
// FF3 0x001ed178.
static void LangSelDispMain(u_char alpha)
{
    BpCtlMain();
    if (CmnFadeLayerIsReady())
    {
        GetCmnFadeLayer()->Update();
        GetCmnFadeLayer()->base_alpha = alpha;
        GetCmnFadeLayer()->Show(10);
    }
    PK4SendVram(ReiPan_GetHostPointer(LANG_SEL_TEX_ADRS), 0, -1, -1);
    LangSelNationalFlagDisp(0, 0, alpha);
    LangSelCsrFlareDisp(0, 0, alpha);
    LangSelCountryNameDisp(0, 0, alpha);
}

// The five flags; rows off the cursor are tinted to 0x40. FF3 0x001ed228.
static void LangSelNationalFlagDisp(int off_x, int off_y, u_char alpha)
{
    static const int tex_label_tbl[LANG_NUM] = { 1, 2, 3, 4, 5 };      // rdata 0x35b5a8
    DISP_SPRT ds;

    for (int i = 0; i < LANG_NUM; i++)
    {
        CopySprDToSpr(&ds, &lang_sl_tex[tex_label_tbl[i]]);
        ds.x += (float)off_x;
        ds.alphar = 0x44;
        ds.y += (float)off_y;
        ds.alpha = (u_char)((ds.alpha * alpha) >> 7);
        if (lang_sel_ctrl.csr != i)
        {
            ds.r = 0x40;
            ds.g = 0x40;
            ds.b = 0x40;
        }
        DispSprD(&ds);
    }
}

// The cursor row's flare, additive; French has two parts. FF3 0x001ed328.
static void LangSelCsrFlareDisp(int off_x, int off_y, u_char alpha)
{
    static const int flare_tex_tbl[LANG_NUM][2] =                      // rdata 0x35b5c0
    {
        { 12, -1 },
        { 13, 14 },
        { 15, -1 },
        { 16, -1 },
        { 17, -1 },
    };
    DISP_SPRT ds;

    for (int i = 0; i < 2; i++)
    {
        int no = flare_tex_tbl[lang_sel_ctrl.csr][i];

        if (no == -1)
            continue;
        CopySprDToSpr(&ds, &lang_sl_tex[no]);
        ds.alphar = 0x48;
        ds.x += (float)off_x;
        ds.alpha = (u_char)((ds.alpha * alpha) >> 7);
        ds.y += (float)off_y;
        DispSprD(&ds);
    }
}

// All five names: the cursor row additive, the others tinted to 0x40.
// FF3 0x001ed430.
static void LangSelCountryNameDisp(int off_x, int off_y, u_char alpha)
{
    static const int tex_label_tbl[LANG_NUM][2] =                      // rdata 0x35b5e8
    {
        { 6, -1 },
        { 7,  8 },
        { 9, -1 },
        { 10, -1 },
        { 11, -1 },
    };
    DISP_SPRT ds;

    for (int i = 0; i < LANG_NUM; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            int no = tex_label_tbl[i][j];

            if (no == -1)
                continue;
            CopySprDToSpr(&ds, &lang_sl_tex[no]);
            ds.x += (float)off_x;
            ds.alpha = (u_char)((ds.alpha * alpha) >> 7);
            ds.y += (float)off_y;
            if (lang_sel_ctrl.csr == i)
            {
                ds.alphar = 0x48;
            }
            else
            {
                ds.r = 0x40;
                ds.g = 0x40;
                ds.b = 0x40;
                ds.alphar = 0x44;
            }
            DispSprD(&ds);
        }
    }
}

// Empty in FF3 (FF2 loaded the language's message and font packs here).
// FF3 0x001ed568.
static void LangSelDataLoadReq(void)
{
}

// FF3 0x001ed570.
static int LangSelDataLoadWait(void)
{
    return IsLoadEndAll() != 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase functions
// ──────────────────────────────────────────────────────────────────────

// Soft reset off and the left stick as d-pad while the screen runs.
// FF3 0x001ed590.
int init_LangSel_Main(void)
{
    GetSysWrk()->sreset_ng = 1;
    pad_wrk[0].stick_dpad = 1;
    LangSelCtrlInit();
    return 1;
}

// FF3 0x001ed5d8.
int one_LangSel_Main(void)
{
#ifdef REIPAN_SLPS_25544_CHS
    GetSysWrk()->language = 2;
    GetGPhaseCtrl()->SetNext(3, 0);
    return 1;
#else
    return LangSelMain() != 0;
#endif
}

// FF3 0x001ed5f8.
int end_LangSel_Main(void)
{
    pad_wrk[0].stick_dpad = 0;
    GetSysWrk()->sreset_ng = 0;
    return 1;
}
