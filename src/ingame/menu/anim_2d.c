/* ==========================================================================
 *  ingame/menu/anim_2d.c
 *
 *  The HUD's item hooks, message pictures, layout animations and the layout
 *  message window, and the HUD layout object that runs them (see
 *  anim_2d.h). FF3 0x1502a0..0x153460, all of it.
 * ======================================================================== */
#include "ingame/menu/anim_2d.h"

#include <string.h>                     // memset, strncmp

#include "ingame/candle_ctrl.h"          // GetCandleControl, CandleControl::GetGrade
#include "ingame/ingame_wrk.h"           // ingame_wrk
#include "common/reipan_stub.h"
#include "graphics/gs_reg.h"            // GetGsRegCached, SetGsRegCached
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/photo/finder_photo.h"     // GetFilmWrk, GetLensWrk, GetHudHpWrk
#include "ingame/photo/fndr_parts.h"       // FilmGet*, LensGet*
#include "ingame/plyr/player.h"         // PlyrGetFlashNum, PlyrGetCurNo, PlyrFlashAble, PlyrCamAble
#include "ingame/plyr/plyr_gauge.h"     // PlyrGaugeGetRate
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "reipan/os/reipan_rand.h"      // PC: ReiPan_Rand
#include "graphics/graph2d/message.h"   // MsgReset, MsgPrintPage, MsgPage*, SetCurColorRGB
#include "graphics/graph2d/tim2.h"      // Tim2PackSend
#include "graphics/motion/anicode.h"    // PadStickDpad*, type callbacks, hook helpers
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "system/b2d/B2dCtl.h"
#include "system/b2d/BpCtl.h"
#include "system/b2d/B2dSpr.h"          // B2dSprGetPartData, B2dSprFreezePartByName
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // CheckPadTrgPlaySe, pad_key
#include "system/os/eecdvd.h"         // LoadReq / LoadReqGetAddr / IsLoadEnd / IsLoadEndAll
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@001ef150 (libvu0)
float Vu0Cosf(float x);

// unassigned@001ef150 (pad driver)
int PadIsPressMode(int pad);                            // scePadInfoPressMode(...) == 1

// ──────────────────────────────────────────────────────────────────────

static int FUN_00152398(int handle);
static int FUN_00152490(int handle);
static int FUN_00152d70(const B2D_HOOK_ARGS *args);

static LAYOUT_MSG  layout_msg;                          /* bss 0x3919b0 */
static LAYOUT_ANIM layout_anim[8];                      /* bss 0x3919e8 */

// Bp files that exist once per language (data 0x36dbc8): file + GetLanguage().
static const int layout_anim_lang_file[2] = { 0x12dd, 0x12e3 };

// Item lists of types 1 and 4 (data 0x351428, 0x351440).
static const LAYOUT_ANIM_ITEM layout_anim_items_1[] =
{
    { 0x3ee, 0x52, 0 },
    { -1, -1, -1 },
};
static const LAYOUT_ANIM_ITEM layout_anim_items_4[] =
{
    { 0x3f1, 0x66, 0 },
    { 0x3f0, 0x66, 1 },
    { 0x41b, 0x66, 2 },
    { -1, -1, -1 },
};

// The layout types (data 0x3512b8, 13 x 0x1c). Type 11 loads with
// BpCtlLoadReqLast; 3/12 and 5/9 are the message window variants.
static const LAYOUT_ANIM_TBL layout_anim_tbl[13] =
{
    {  0, 0x12f1, nullptr,            MsgViewOnOpen,     FUN_0014e040,      MsgViewOnClose,    nullptr },
    {  1, 0x12da, nullptr,            FUN_0014e2f8,      FUN_0014e040,      MsgViewOnClose,    layout_anim_items_1 },
    {  2, 0x12db, FUN_0014e710,       ItemGetOnOpen,     ItemGetOnUpdate,   ItemGetOnClose,    nullptr },
    {  3, 0x12dc, LayoutMsgOnLoadReq, FUN_001526d8,      LayoutMsgOnUpdate, LayoutMsgOnClose,  nullptr },
    {  4, 0x12dd, nullptr,            PauseMenuOnOpen,   PauseMenuOnUpdate, PauseMenuOnClose,  layout_anim_items_4 },
    {  5, 0x12e2, LayoutMsgOnLoadReq, FUN_00152820,      LayoutMsgOnUpdateB, LayoutMsgOnCloseB, nullptr },
    {  6, 0x12e2, nullptr,            YesNoOnOpen,       YesNoOnUpdate,     FUN_0014f338,      nullptr },
    {  7, 0x12e3, nullptr,            FUN_00150190,      SlotSelOnUpdate,   FUN_00150290,      nullptr },
    {  8, 0x12e8, nullptr,            FUN_0014f798,      MenuWdwOnUpdate,   FUN_0014f850,      nullptr },
    {  9, 0x12e9, LayoutMsgOnLoadReq, FUN_00152820,      LayoutMsgOnUpdateB, LayoutMsgOnCloseB, nullptr },
    { 10, 0x12eb, nullptr,            YesNoOnOpen,       YesNoOnUpdate,     FUN_0014f338,      nullptr },
    { 11, 0x12ea, nullptr,            YesNoOnOpen,       YesNoOnUpdate,     FUN_0014f338,      nullptr },
    { 12, 0x12eb, LayoutMsgOnLoadReq, FUN_001526d8,      LayoutMsgOnUpdate, LayoutMsgOnClose,  nullptr },
};

// ══════════════════════════════════════════════════════════════════════
// The in-game HUD's items
// ══════════════════════════════════════════════════════════════════════

// HUD layout files by HudGetFileNo's index (data 0x351100); 0x11e2 is the
// layout without camera items.
static const int hud_file_tbl[8] = { 0x11cb, 0x11ce, 0x11d3, 0x11cc, 0x11d8, 0x11dd, 0x11cd, 0x11e2 };

// Files that exist once per language: file + GetLanguage() (data 0x351120).
static const int hud_lang_file_tbl[4] = { 0x11ce, 0x11d3, 0x11d8, 0x11dd };

// Face items by HUD_LAYOUT.chr (data 0x2f6320).
static const char *const hud_face_name[3] = { "R rei_face", "R miku_face", "R kei_face" };

// Film icons by FilmGetType (data 0x2f6330); _07 carries the draw hook.
static const char *const hud_film_name[5] =
{
    "R filmicon_07", "R filmicon_14", "R filmicon_61", "R filmicon_90", "R filmicon_00",
};

// Fire items by CandleControl::GetGrade (data 0x2f6348; the first names are
// not read here).
static const char *const hud_fire_name[5][2] =
{
    { "cdl4", "R fire_4" },
    { "cdl3", "R fire_3" },
    { "cdl2", "R fire_2" },
    { "cdl1", "R fire_1" },
    { nullptr, nullptr },
};

// Lens slot items by HUD_LAYOUT.lens (data 0x2f6370, 3 slots each).
static const char *const hud_lens_name[3][3] =
{
    { nullptr, nullptr, nullptr },
    { "renz",  nullptr, nullptr },
    { "renz1", "renz2", "renz3" },
};

// Lens sprites by LensGetEquip, 9 and up: blank (data 0x2f6398).
static const char *const hud_lens_sprite[10] =
{
    "R renz_blast", "R renz_zero", "R renz_crush", "R renz_serial", "R renz_see",
    "R renz_stp",   "R renz_slw",  "R renz_kan",   "R renz_geki",   "R renz_blank",
};

static const char hud_hp_name[]   = "R HP";       /* sdata 0x36db58 */
static const char hud_num_font[]  = "R 0";        /* sdata 0x36db98 */

// The fire wobble; its constructor is the TU's static initialisation.
HUD_FIRE hud_fire;                                  /* bss 0x391988 */

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001502a0
HUD_FIRE *FUN_001502a0(void)
{
    return &hud_fire;
}

// HUD_LAYOUT::Play's labels by arg (data 0x2f63c0, 2 x 4): 0 in, 1 out after
// it, 2 back, 3 cancel (out while "in" still plays).
static const char *const hud_label[2][4] =
{
    { "in",             "out_next", "back",  "cancel" },
    { "item_camera_in", "out_next", nullptr, "cancel" },
};

// GetCandleControl through the file's cache (FF3: sbss gp-0xb90, the inline
// getter's copy).
static CandleControl *HudCandle(void)
{
    static CandleControl *candle;

    if (candle == nullptr)
        candle = GetCandleControl();
    return candle;
}

// Freezes an item's scale part at `scale` (the part's start and value,
// 0x400 = 1.0); UpdateItems froze those parts so the layout's keys leave
// them alone.
static void HudSetItemScale(B2D_ITEM *item, int part, float scale)
{
    int value = (int)(scale * 1024.0f);

    // PC: the ROM writes through a missing item (low EE memory, harmless).
    if (item == nullptr)
        return;
    item->part[part].value = value;
    item->part[part].start = value;
}

// The HP gauge restarts from its current value and the slow gauge from its
// fill (the constructor and the destructor both inline it).
static void HudResetGauges(void)
{
    HUD_HP_WRK *hp    = GetHudHpWrk();
    PLYR_GAUGE *gauge = GetAlbumSaveDisp();

    hp->wait_disp = 0;
    hp->disp      = hp->now;
    hp->tail      = hp->now;
    hp->wait_tail = 0;
    gauge->disp   = PlyrGaugeGetRate(gauge);
}

// ──────────────────────────────────────────────────────────────────────
// Takes the player's state (character, flash, Miku's camera, the finder,
// lens slots), picks the layout file from it and starts loading that.
// FF3 0x001503c8; GCC's base-object copy FF3 0x001502b0 is the same bytes.
// callcheck: GetHudHpWrk, GetAlbumSaveDisp, PlyrGaugeGetRate (HudResetGauges, inline in the ROM)
HUD_LAYOUT::HUD_LAYOUT(int arg)
{
    int slots = 0;

    this->arg = arg;
    a1 = 0;
    a2 = 0;
    HudResetGauges();
    chr       = (s_char)PlyrGetCurNo();
    flash     = PlyrFlashAble();
    kaku      = PlyrCamAble();
    no_camera = ingame_wrk.b03 == 1 || GetPhotoModeWrk()->IsFinderReady() == 0;

    if (this->arg == 0 && no_camera == 0)
        slots = GetLensWrk()->GetSlotNum();
    lens = slots == 0 ? 0 : slots == 1 ? 1 : 2;

    file = GetFileNo();
    BpCtlLoadReqLast(file, nullptr);
    state = HUD_LAYOUT_LOADING;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00150540; GCC's base-object copy FF3 0x001504e0 is the same bytes.
// callcheck: GetHudHpWrk, GetAlbumSaveDisp, PlyrGaugeGetRate (HudResetGauges, inline in the ROM)
HUD_LAYOUT::~HUD_LAYOUT()
{
    BpCtlRelease(BpCtlGetHandleByFileNo(file));
    HudResetGauges();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001505a0 (BpCtlIsDataReady: -1 without the entry, also != 0).
int HUD_LAYOUT::IsReady() const
{
    return BpCtlIsDataReady(BpCtlGetHandleByFileNo(file));
}

// ──────────────────────────────────────────────────────────────────────
// Not loaded yet: done at once. Else "out_next" once "in" has played, or
// "cancel" while it still plays. FF3 0x001505c0.
void HUD_LAYOUT::Close()
{
    if (state == HUD_LAYOUT_LOADING)
    {
        state = HUD_LAYOUT_DONE;
        return;
    }
    Play(BpCtlIsLoadEnd(file) != 0 ? 1 : 3);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00150620.
void HUD_LAYOUT::Main()
{
    switch (state)
    {
    case HUD_LAYOUT_LOADING:
        BpCtlMain();
        if (IsReady() != 0)
        {
            Start();
            state = HUD_LAYOUT_RUN;
        }
        break;

    case HUD_LAYOUT_RUN:
        if (BpCtlIsLoadEnd(file) != 0)
            state = HUD_LAYOUT_DONE;
        Animate();
        break;

    case HUD_LAYOUT_DONE:
        Idle();
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001506d8.
void HUD_LAYOUT::Start()
{
    UpdateItems();
#ifdef REIPAN_SLPS_25544_CHS
    if (file == 0x11e2)
    {
        const int handle = BpCtlGetHandleByFileNo(file);
        // status_H stores its digit samples to the left of the PS2 viewport.
        for (int id = 0x3e9; id <= 0x3f2; ++id)
            if (B2D_ITEM *item = B2dCtlFindItem(handle, id))
                B2dSprSetDisp(item, 0);
    }
#endif
}

// ──────────────────────────────────────────────────────────────────────
// Every frame: places the layout, steps the HP gauge and sizes R HP by it
// (and R HP_kaku by Miku's slow gauge), shows the candle item of the
// current grade and wobbles its fire by hud_fire (a 7-frame ease to a new
// random target), then draws the layouts (BpCtlMain). FF3 0x001506f0.
// callcheck: rand (ReiPan_Rand)
// callcheck: GetCandleControl (HudCandle, the inline cached getter)
void HUD_LAYOUT::Animate()
{
    int            handle = BpCtlGetHandleByFileNo(file);
    CandleControl *candle;
    int            grade  = CANDLE_GRADE_OUT;
    const char    *fire;

    B2dCtlSetPos(handle, a1, a2);
    HudHpWork(GetHudHpWrk());
    HudSetItemScale(B2dCtlFindItemByName(handle, hud_hp_name), B2D_PART_SX, GetHudHpWrk()->disp);

    if (no_camera == 0 && kaku != 0)
        HudSetItemScale(B2dCtlFindItemByName(handle, "R HP_kaku"), B2D_PART_SX,  /* data 0x3510d0 */
                        GetAlbumSaveDisp()->disp);

    candle = HudCandle();
    if (candle->enable != 0 && candle->lit != 0)
        grade = HudCandle()->GetGrade();

    for (int i = 0; i < 5; i++)
    {
        if (hud_fire_name[i][0] != nullptr)
            B2dSprSetDisp(B2dCtlFindItemByName(handle, hud_fire_name[i][0]), grade == i);
    }

    fire = hud_fire_name[grade][1];
    if (fire != nullptr)
    {
        HUD_FIRE *f = FUN_001502a0();
        float     t = f->time;

        for (int k = 0; k < 3; k++)
            f->cur[k] = f->from[k] + (t * (f->to[k] - f->from[k])) / 7.0f;

        f->time = t + GetFrameTimeScale();
        if (7.0f < f->time)
        {
            for (int k = 0; k < 3; k++)
                f->from[k] = f->to[k];
            f->time = 0.0f;
            // PC: ReiPan_Rand is the ROM's 31-bit rand(); the host's is 15-bit.
            f->to[0] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.2f + 0.9f;
            f->to[1] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.2f + 0.9f;
            f->to[2] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.1f + 0.9f;
        }

        B2D_ITEM *item = B2dCtlFindItemByName(handle, fire);

        HudSetItemScale(item, B2D_PART_SX, FUN_001502a0()->cur[0]);
        HudSetItemScale(item, B2D_PART_SY, FUN_001502a0()->cur[1]);
    }

    BpCtlMain();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00150aa8.
void HUD_LAYOUT::Idle()
{
    Animate();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00150ac0.
int HUD_LAYOUT::Play(int label)
{
    const char *name;

    if (IsReady() == 0)
        return 0;
    name = hud_label[arg][label];
    if (name == nullptr)
        return 0;
    BpPlayLabel(BpCtlGetHandleByFileNo(file), name);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The HUD layout file for the character, flash, lens slots and camera
// state. FF3 0x00150b40 (FF2's Anim2D_CalcNowAlpha slot).
int HUD_LAYOUT::GetFileNo() const
{
    int file = 0x11e2;

    if (no_camera == 0)
    {
        int idx = 0;

        if (chr == 1)
            idx = kaku != 0 ? 6 : 0;
        else if (chr == 0)
            idx = (flash != 0 ? 3 : 0) + lens;
        else if (chr == 2)
            idx = lens;
        file = hud_file_tbl[idx];
    }

    for (int i = 0; i < 4; i++)
    {
        if (file == hud_lang_file_tbl[i])
            file += GetSysWrk()->GetLanguage();
    }
    return file;
}

// ---- item draw hooks (HudUpdateItems installs them) ----
// These items get B2D_HOOK_ARGS with a0 = the item name, t1/t2 = x, y
// (film icon) or t1 = the item rect, and an alpha in stack[1] (film icon,
// HP, fire) or stack[2] (numbers, lens).

// ──────────────────────────────────────────────────────────────────────
// R filmicon_07: the icon of the loaded film, grey without shots left.
// FF3 0x00150ff8.
static int HudDrawFilmIcon(const B2D_HOOK_ARGS *args)
{
    u_char r = 0x80, g = 0x80, b = 0x80;
    int    type = FilmGetType(GetFilmWrk());

    if (FilmGetNum(GetFilmWrk()) == 0)
        r = g = b = 0x20;
    B2dDrawSpriteByName(BpGetDrawHandle(), hud_film_name[type], (int)args->t1, (int)args->t2, r, g, b,
                        (float)(int)args->stack[1] * (1.0f / 128.0f), 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// NUM_film: shots left, at rect[4], rect[5]. FF3 0x001510f8.
static int HudDrawFilmNum(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    int        num   = FilmGetNum(GetFilmWrk());
    int        x     = (int)(float)rect[4];
    int        y     = (int)(float)rect[5];

    B2dNumSetFont(BpGetDrawHandle(), hud_num_font, nullptr);
    B2dNumSetTex();
    B2dNumDraw(num, x, y, alpha, 0);
    B2dResetCurTex();
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// NUM_flash: as HudDrawFilmNum with PlyrGetFlashNum. FF3 0x001511b8.
static int HudDrawFlashNum(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    int        num   = PlyrGetFlashNum();
    int        x     = (int)(float)rect[4];
    int        y     = (int)(float)rect[5];

    B2dNumSetFont(BpGetDrawHandle(), hud_num_font, nullptr);
    B2dNumSetTex();
    B2dNumDraw(num, x, y, alpha, 0);
    B2dResetCurTex();
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The lens slot items: the slot is the item name's column in
// hud_lens_name (the last row that has it); draws the lens equipped there.
// FF3 0x00151270.
// callcheck: CandleControlGetGrade, B2dDrawSpriteDirect, FUN_001502a0, GetCandleControl, GetGsRegCached, SetGsRegCached (Ghidra's body runs on into HudDrawFire 0x151400)
static int HudDrawLens(const B2D_HOOK_ARGS *args)
{
    const char *name = (const char *)args->a0;
    const int  *rect = (const int *)args->t1;
    int         slot = -1;

    if (name == nullptr)
        return 0;

    for (int row = 0; row < 3; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            const char *item = hud_lens_name[row][col];

            if (item != nullptr && strncmp(name, item, 5) == 0)
            {
                slot = (s_char)col;
                break;
            }
        }
    }

    if (slot >= 0 && slot < GetLensWrk()->GetSlotNum())
    {
        u_char lens = GetLensWrk()->GetEquip((u_char)slot, 0xff);
        int    no   = lens < 9 ? lens : 9;

        B2dDrawSpriteByName(BpGetDrawHandle(), hud_lens_sprite[no], rect[0], rect[1], 0x80, 0x80, 0x80,
                            (float)(int)args->stack[2] * (1.0f / 128.0f), 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// R fire_N: drawn with ALPHA reg 0x161 and the alpha scaled by
// hud_fire.cur[2], while the album view grades below 4.
// FF3 0x00151400 (no Ghidra function; decoded from the bytes).
static int HudDrawFire(const B2D_HOOK_ARGS *args)
{
    CandleControl *ctrl  = GetCandleControl();
    int              grade = 4;

    if (ctrl->enable != 0 && ctrl->lit != 0)
        grade = GetCandleControl()->GetGrade();

    if (grade != 4)
    {
        u_long        alpha_reg = *GetGsRegCached(0x14);
        B2D_HOOK_ARGS draw      = *args;

        SetGsRegCached(0x161, 0x14, 1);
        draw.stack[1] = (int)(FUN_001502a0()->cur[2] * (float)(int)args->stack[1]);
        B2dDrawSpriteDirect(&draw);
        SetGsRegCached(alpha_reg, 0x14, 1);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// R HP: the sprite with its alpha pulsing between 0.5 and 1.0.
// FF3 0x00151578.
static int HudDrawHp(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw = *args;
    float         k    = Vu0Cosf(GetHudHpWrk()->phase) * 0.25f + 0.75f;

    draw.stack[1] = (int)(k * (float)(int)args->stack[1]);
    B2dDrawSpriteDirect(&draw);
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Installs one hook record (FF3 builds {func, 0, 0, name} + the end record
// on the stack for every call).
static void HudSetHook(int file, B2D_ITEM_HOOK_FUNC func, const char *name)
{
    B2D_HOOK_REC recs[2] =
    {
        { func,     0,  0, name    },
        { nullptr, -1, -1, nullptr },
    };

    B2dSetHookList(BpCtlGetHandleByFileNo(file), recs);
}

// ──────────────────────────────────────────────────────────────────────
// Shows the current character's face, sets the HP / camera / fire items
// up with their draw hooks, then runs the HUD animation (0x1506f0).
// FF3 0x00150c38.
void HUD_LAYOUT::UpdateItems()
{
    int handle = BpCtlGetHandleByFileNo(file);

    for (int i = 0; i < 3; i++)
    {
        if (B2dCtlHasItemName(handle, hud_face_name[i]))
            B2dCtlSetItemDispByName(handle, hud_face_name[i], i == chr);
    }

    B2dSprFreezePartByName(handle, hud_hp_name, 8, 0);
    HudSetHook(file, HudDrawHp, hud_hp_name);

    if (no_camera == 0)
    {
        if (kaku != 0)
            B2dSprFreezePartByName(handle, "R HP_kaku", 8, 0);    /* data 0x3510d0 */

        for (int i = 1; i < 5; i++)
            B2dCtlSetItemDispByName(handle, hud_film_name[i], 0);

        {
            B2D_HOOK_REC recs[3] =
            {
                { HudDrawFilmIcon, 0,  0, hud_film_name[0] },
                { HudDrawFilmNum,  0,  0, "NUM_film" },     /* data 0x351180 */
                { nullptr,        -1, -1, nullptr },
            };

            B2dSetHookList(BpCtlGetHandleByFileNo(file), recs);
        }

        if (flash != 0)
            HudSetHook(file, HudDrawFlashNum, "NUM_flash");  /* data 0x351150 */

        for (int i = 0; i < 3; i++)
        {
            const char *name = hud_lens_name[lens][i];

            if (name != nullptr && B2dCtlHasItemName(handle, name))
                HudSetHook(file, HudDrawLens, name);
        }
    }

    for (int i = 0; i < 5; i++)
    {
        const char *name = hud_fire_name[i][1];

        if (name != nullptr)
        {
            B2dSprFreezePartByName(handle, name, 8, 0);
            B2dSprFreezePartByName(handle, name, 9, 0);
            HudSetHook(file, HudDrawFire, name);
        }
    }

    Animate();
}

// ──────────────────────────────────────────────────────────────────────
// The fire wobble starts at 1.0 with a random target. FF3 0x00151680:
// __static_initialization_and_destruction_0(1, 0xffff), run by the global
// constructor 0x1517d0. (0x1517b0 next to it is the at-exit destructor of
// the AlbumViewCtrl static, 0x2ad380(0x41e540); the host runs that class's
// destructor itself.)
// callcheck: rand (ReiPan_Rand)
HUD_FIRE::HUD_FIRE()
{
    cur[0]  = cur[1]  = cur[2]  = 1.0f;
    time    = 0.0f;
    from[0] = from[1] = from[2] = 1.0f;
    // PC: ReiPan_Rand is the ROM's 31-bit rand(); the host's is 15-bit.
    to[0] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.2f + 0.9f;
    to[1] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.2f + 0.9f;
    to[2] = ((float)ReiPan_Rand() / REIPAN_RAND_MAXF) * 0.1f + 0.9f;
}

// ══════════════════════════════════════════════════════════════════════
// Message pictures
// ══════════════════════════════════════════════════════════════════════

// data 0x351190
static const MSG_PIC_TBL msg_pic_tbl[] =
{
    { 0x51, 0x1478, 2 },
    { 0x52, 0x147a, 3 },
    { 0x53, 0x147d, 2 },
    { 0x54, 0x147f, 2 },
    { 0x55, 0x1481, 2 },
    { -1, -1, -1 },
};

// ──────────────────────────────────────────────────────────────────────
// The record whose key is *key, nullptr when none (a template instance in
// FF3's shared code area). FF3 0x002add40.
static const MSG_PIC_TBL *KeyTblFind(const MSG_PIC_TBL *tbl, const int *key)
{
    if (tbl->key == -1)
        return nullptr;
    while (tbl->key != *key)
    {
        tbl++;
        if (tbl->key == -1)
            return nullptr;
    }
    return tbl;
}

// ──────────────────────────────────────────────────────────────────────
// Picture of page `page` of `key` (page 0 when out of range) in the
// current language. FF3 0x001517f0.
int MsgPicGetPicNo(int key, int page)
{
    const MSG_PIC_TBL *ent = KeyTblFind(msg_pic_tbl, &key);

    if (page < 0 || page >= ent->count)
        page = 0;
    return ent->base + page + GetSysWrk()->GetLanguage() * 0xb;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001518d0 (the base-object copy 0x00151870 is the same bytes).
// callcheck: operator_new (new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)
CTutorial::CTutorial()
{
    page   = -1;
    key    = -1;
    active = 0;
    pic    = new MENU_PIC(0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151980 (base-object 0x00151930, deleting 0x001519d0).
// callcheck: MenuPicDtor, operator_delete (delete pic: MENU_PIC's destructor, then operator delete)
CTutorial::~CTutorial()
{
    Reset();
    if (pic != nullptr)
    {
        delete pic;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151a28
int CTutorial::IsActive(void) const
{
    return active != 0 && key != -1 && page != -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151a58
// callcheck: MsgPicIsActive (IsActive)
void CTutorial::SetPage(int no)
{
    if (!IsActive())
        return;
    if (no < 0 || no >= KeyTblFind(msg_pic_tbl, &key)->count)
        no = 0;
    page     = no;
    pic->pic = MsgPicGetPicNo(key, no);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151ad8
// callcheck: MsgPicOpen (Open)
void CTutorial::Set(int no)
{
    key  = no;
    page = 0;
    if (active == 0)
    {
        Open();
        active = 1;
    }
    pic->pic = MsgPicGetPicNo(key, page);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151b38
void CTutorial::Reset(void)
{
    active   = 0;
    key      = -1;
    page     = -1;
    pic->pic = -2;
    if (pic->load[0] != nullptr)
        pic->load[0]->Release();
    if (pic->load[1] != nullptr)
        pic->load[1]->Release();
    pic->fade = -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151bb8
void CTutorial::Open(void)
{
    pic->v = 1;
    pic->u = 1;
    pic->h    = 0x133;
    pic->w    = 0x1fe;
    if (pic->load[0] != nullptr)
        pic->load[0]->direct = 1;
    if (pic->load[1] != nullptr)
        pic->load[1]->direct = 1;
    pic->enable = 1;
}

// ──────────────────────────────────────────────────────────────────────
// Follows the message's key: none -> reset, a new one -> set; then steps
// the picture. FF3 0x00151c08.
// callcheck: MsgPicIsActive, MsgPicReset, MsgPicSet (members IsActive, Reset, Set)
void CTutorial::Update(int no)
{
    if (!MsgPicHasKey(no))
        Reset();
    else if (no != key)
        Set(no);
    if (IsActive())
        pic->Step();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151c88
// callcheck: MsgPicIsActive (IsActive)
void CTutorial::Draw(short x, short y, float f)
{
    if (!IsActive())
        return;
    pic->y    = y;
    pic->pri = 0xa0;
    pic->x    = x;
    pic->z = 0;
    pic->scale = f;
    pic->Draw();
    pic->DrawNext();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151d30
void CTutorial::End(void)
{
    Reset();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151d48
int MsgPicHasKey(int key)
{
    return KeyTblFind(msg_pic_tbl, &key) != nullptr;
}

// ══════════════════════════════════════════════════════════════════════
// The layout message window
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151d80
int LayoutMsgGetCsr(void)
{
    return layout_msg.csr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151d90
int LayoutMsgGetResult(void)
{
    return layout_msg.result;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151da0
void LayoutMsgSetKeepOpen(int keep)
{
    layout_msg.keep_open = keep;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151db0
void LayoutMsgSetArg(int arg)
{
    layout_msg.arg = arg;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151dc0
void LayoutMsgSetFlags(int flags)
{
    layout_msg.flags = flags;
}

// ──────────────────────────────────────────────────────────────────────
// Sets the message (msg0, msg1) and picks the step routine: the page-break
// one (after MsgPageReset) when the message has pages, else the single page.
// FF3 0x00151dd0 (LayoutMsgGetPageBreakNum inlined).
void LayoutMsgSet(int msg0, int msg1)
{
    int pages;

    layout_msg.msg1   = msg1;
    layout_msg.msg0   = msg0;
    layout_msg.result = LAYOUT_MSG_NONE;
    if (msg1 == -1 || msg0 == -1)
        pages = 0;
    else
        pages = MsgGetPageBreakNum(msg0, msg1);

    if (pages == 0)
    {
        layout_msg.step = FUN_00152398;
    }
    else
    {
        MsgPageReset();
        layout_msg.step = FUN_00152490;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151e48
int LayoutMsgGetPageBreakNum(void)
{
    if (layout_msg.msg1 == -1 || layout_msg.msg0 == -1)
        return 0;
    return MsgGetPageBreakNum(layout_msg.msg0, layout_msg.msg1);
}

// ──────────────────────────────────────────────────────────────────────
// Mode 1: a choice with cursor csr (kept unless 0/1); mode 2: no choice.
// FF3 0x00151e90.
void LayoutMsgSetMode(int mode, int csr)
{
    layout_msg.result = LAYOUT_MSG_NONE;
    layout_msg.arg    = 0;
    if (mode == 1)
    {
        if ((u_int)csr < 2)
            layout_msg.csr = csr;
        layout_msg.choice = mode;
        return;
    }
    if (mode == 2)
    {
        layout_msg.csr    = mode;
        layout_msg.choice = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00151ed8
void LayoutMsgSetType(int mode)
{
    LayoutMsgSetMode(mode, 1);
}

// ---- item draw hooks of the window (hook lists 0x3511d8 / 0x351238) ----
// Text items get B2D_HOOK_ARGS with t1 = the item rect, t3 = its packed
// colour and stack[2] = its alpha (B2dCtl.h).

// ──────────────────────────────────────────────────────────────────────
// Item 0x415: the current page of the message, while one is set.
// FF3 0x00151ef0.
static int LayoutMsgDrawText(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    int        alpha = (int)args->stack[2];

    if (alpha != 0 && layout_msg.msg0 >= 0 && layout_msg.msg1 >= 0)
    {
        SetCurColorPacked((u_int)args->t3);
        MsgPrintPage(layout_msg.msg0, layout_msg.msg1, rect[0], rect[1], 0x2d, alpha, 0, 0, 0, 0);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x416: choice 0, message (1, 1), centred between rect[0] and
// rect[4]; in the item colour when the cursor is on it, else 0xd6f4ab.
// FF3 0x00151f98.
static int LayoutMsgDrawChoice0(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;

    if (layout_msg.choice != 0)
    {
        if (layout_msg.csr == 0)
            SetCurColorPacked((u_int)args->t3);
        else
            SetCurColorRGB(0xd6, 0xf4, 0xab);
        MsgPrintAt(1, 1, (rect[0] + rect[4]) / 2, rect[1], (int)args->stack[2], 0, 0, 0, 2);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x417: choice 1, message (1, 2), as LayoutMsgDrawChoice0.
// FF3 0x00152040 (no Ghidra function; inside LayoutMsgDrawChoice0's body).
static int FUN_00152040(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;

    if (layout_msg.choice != 0)
    {
        if (layout_msg.csr == 1)
            SetCurColorPacked((u_int)args->t3);
        else
            SetCurColorRGB(0xd6, 0xf4, 0xab);
        MsgPrintAt(1, 2, (rect[0] + rect[4]) / 2, rect[1], (int)args->stack[2], 0, 0, 0, 2);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x3f4: with a choice, drawn by the default item draw with a1/a2 from
// cursor item csr != 0 (the two cursor items swap their part data).
// FF3 0x001520f0 (no Ghidra function; inside LayoutMsgDrawChoice0's body).
static int FUN_001520f0(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw;
    int           i;

    if (layout_msg.choice == 0)
        return 0;
    MenuMouseNoteChoice(layout_msg.csr);    // PC
    i       = layout_msg.csr != 0;
    draw    = *args;
    draw.a1 = layout_msg.cursor_part[i][0];
    draw.a2 = layout_msg.cursor_part[i][1];
    return B2dDrawItemDefault(&draw);
}

// ──────────────────────────────────────────────────────────────────────
// Item 0x3f5: as FUN_001520f0 with cursor item csr == 0. FF3 0x00152148.
static int LayoutMsgDrawCsr1(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw;
    int           i;

    if (layout_msg.choice == 0)
        return 0;
    i       = layout_msg.csr == 0;
    draw    = *args;
    draw.a1 = layout_msg.cursor_part[i][0];
    draw.a2 = layout_msg.cursor_part[i][1];
    return B2dDrawItemDefault(&draw);
}

// Hook lists the window's on_open installs (data 0x3511d8 for types 3/12,
// 0x351238 for 5/9; the two are identical).
static const B2D_HOOK_REC layout_msg_hooks[] =
{
    { LayoutMsgDrawText,    0, 0x415, nullptr },
    { LayoutMsgDrawChoice0, 0, 0x416, nullptr },
    { FUN_00152040,         0, 0x417, nullptr },
    { FUN_001520f0,         0, 0x3f4, nullptr },
    { LayoutMsgDrawCsr1,    0, 0x3f5, nullptr },
    { nullptr,             -1,    -1, nullptr },
};
static const B2D_HOOK_REC layout_msg_hooks_b[] =
{
    { LayoutMsgDrawText,    0, 0x415, nullptr },
    { LayoutMsgDrawChoice0, 0, 0x416, nullptr },
    { FUN_00152040,         0, 0x417, nullptr },
    { FUN_001520f0,         0, 0x3f4, nullptr },
    { LayoutMsgDrawCsr1,    0, 0x3f5, nullptr },
    { nullptr,             -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Reads the pad for the window (pad 0 connected and in pressure mode only):
// confirm gives LAYOUT_MSG_OK; cancel, when allowed (arg 1 always, arg 2
// never, arg 0 unless there is no choice), plays "out" (unless keep_open),
// sets csr 3 and gives LAYOUT_MSG_CANCEL; left/right toggle the choice
// cursor. FF3 0x001521a0.
// callcheck: LayoutMsgInput, MsgGetPage, MsgGetPageBreakNum, MsgIsPageWait, MsgPageNext (Ghidra's body includes the step routines FUN_00152398/FUN_00152490)
int LayoutMsgInput(int handle)
{
    int can_cancel;

    if (pad_wrk[0].connected == 0)
        return LAYOUT_MSG_NONE;
    if (!PadIsPressMode(0))
        return LAYOUT_MSG_NONE;

    // PC: the mouse. A message with no choice takes a click anywhere, unless
    // it only captions a screen with targets of its own (the puzzles' guide
    // lines); a Yes / No is answered by the poll (MenuMousePoll).
    if (layout_msg.csr == 2 && !ReiPan_MenuMouseHasTargets())
        ReiPan_MenuMouseConfirm();
    else
        ReiPan_MenuMouseBegin(0, nullptr);

    can_cancel = 1;
    switch (layout_msg.arg)
    {
    case 0:
        if (layout_msg.csr == 2)
            can_cancel = 0;
        break;
    case 2:
        can_cancel = 0;
        break;
    }

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0))
        return LAYOUT_MSG_OK;

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], -1, 0) && can_cancel)
    {
        if (!(layout_msg.flags & LAYOUT_MSG_NO_CANCEL_SE) && layout_msg.csr != 2)
            SndSePlay(0x1a9b0001, 100, 0x1000, 0, 0, 0);
        layout_msg.csr = 3;
        if (layout_msg.keep_open == 0)
            BpPlayLabel(handle, "out");     /* sdata 0x36dbc0 */
        return LAYOUT_MSG_CANCEL;
    }

    if (layout_msg.csr == 2)
        return LAYOUT_MSG_NONE;
    if (!CheckPadTrgPlaySe(0xa000, -1, 0))  // left / right
        return LAYOUT_MSG_NONE;

    if (layout_msg.csr == 0)
        layout_msg.csr = 1;
    else if (layout_msg.csr == 1)
        layout_msg.csr = 0;
    else
        return LAYOUT_MSG_NONE;

    if (layout_msg.flags & LAYOUT_MSG_SILENT_CSR)
        return LAYOUT_MSG_NONE;
    if (layout_msg.csr != 2)
        SndSePlay(0x1a9b0000, 100, 0x1000, 0, 0, 0);
    return LAYOUT_MSG_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// Step routine of a single-page message. With keep_open the input is only
// recorded (result); else confirm plays "out" and closes (1), and cancel
// closes (LayoutMsgInput already played "out").
// FF3 0x00152398 (no Ghidra function; after LayoutMsgInput's body).
static int FUN_00152398(int handle)
{
    int res = LayoutMsgInput(handle);

    if (layout_msg.keep_open != 0)
    {
        if (res == LAYOUT_MSG_NONE)
            return 0;
        if (res == LAYOUT_MSG_OK && !(layout_msg.flags & LAYOUT_MSG_NO_OK_SE) && layout_msg.csr != 2)
            SndSePlay(0x1a9b0003, 100, 0x1000, 0, 0, 0);
        layout_msg.result = res;
        return 0;
    }

    if (res != LAYOUT_MSG_OK)
        return res;
    BpPlayLabel(handle, "out");             /* sdata 0x36dbc0 */
    if (!(layout_msg.flags & LAYOUT_MSG_NO_OK_SE) && layout_msg.csr != 2)
        SndSePlay(0x1a9b0003, 100, 0x1000, 0, 0, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Step routine of a message with page breaks: waits until the page is fully
// shown; confirm turns the page, and on the last page plays "out" and closes.
// FF3 0x00152490 (no Ghidra function; LayoutMsgGetPageBreakNum inlined).
static int FUN_00152490(int handle)
{
    int res;
    int pages;

    if (MsgIsPageWait() != 1)
        return 0;

    res = LayoutMsgInput(handle);
    if (layout_msg.keep_open != 0)
    {
        if (res != LAYOUT_MSG_NONE)
            layout_msg.result = res;
        return 0;
    }

    if (res != LAYOUT_MSG_OK)
        return res;

    if (layout_msg.msg1 == -1 || layout_msg.msg0 == -1)
        pages = 0;
    else
        pages = MsgGetPageBreakNum(layout_msg.msg0, layout_msg.msg1);
    if (MsgGetPage() < pages - 1)
    {
        MsgPageNext();
        return 0;
    }

    BpPlayLabel(handle, "out");             /* sdata 0x36dbc0 */
    if (!(layout_msg.flags & LAYOUT_MSG_NO_OK_SE) && layout_msg.csr != 2)
        SndSePlay(0x1a9b0003, 100, 0x1000, 0, 0, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Message window on_update: runs the step routine LayoutMsgSet picked.
// FF3 0x001525b0.
int LayoutMsgOnUpdate(int handle)
{
    return layout_msg.step(handle);
}

// ──────────────────────────────────────────────────────────────────────
// LayoutMsgSet's step choice for the current message (LayoutMsgSet inlined),
// then the part data 1/2 of the two cursor items id0 and id1.
// FF3 0x001525d8.
// callcheck: B2dSetHookList, LayoutMsgSetCursorItems (Ghidra's body includes the on_open FUN_001526d8)
void LayoutMsgSetCursorItems(int handle, int id0, int id1)
{
    B2D_ITEM *item;
    int       pages;

    layout_msg.result = LAYOUT_MSG_NONE;
    if (layout_msg.msg1 == -1 || layout_msg.msg0 == -1)
        pages = 0;
    else
        pages = MsgGetPageBreakNum(layout_msg.msg0, layout_msg.msg1);

    if (pages == 0)
    {
        layout_msg.step = FUN_00152398;
    }
    else
    {
        MsgPageReset();
        layout_msg.step = FUN_00152490;
    }

    item = B2dCtlFindItem(handle, id0);
    layout_msg.cursor_part[0][0] = B2dSprGetPartData(item, 1);
    layout_msg.cursor_part[0][1] = B2dSprGetPartData(item, 2);
    item = B2dCtlFindItem(handle, id1);
    layout_msg.cursor_part[1][0] = B2dSprGetPartData(item, 1);
    layout_msg.cursor_part[1][1] = B2dSprGetPartData(item, 2);
}

// ──────────────────────────────────────────────────────────────────────
// Types 3/12 on_open: installs the window's item hooks (FF3 copies the ROM
// list to the stack first) and reads the cursor items 0x3f4/0x3f5.
// FF3 0x001526d8 (no Ghidra function; inside LayoutMsgSetCursorItems' body;
// returns whatever v0 held, 0 here).
int FUN_001526d8(int handle)
{
    B2dSetHookList(handle, layout_msg_hooks);
    LayoutMsgSetCursorItems(handle, 0x3f4, 0x3f5);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Message window on_load_req. FF3 0x00152750 (a tail call; the setter
// returns nothing, so the result is undefined on the EE and 0 here).
int LayoutMsgOnLoadReq(int handle)
{
    LayoutMsgSetFlags(LAYOUT_MSG_NO_CANCEL_SE);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Message window on_close: always releases the slot. FF3 0x00152768.
int LayoutMsgOnClose(int handle)
{
    layout_msg.keep_open = 0;
    MsgReset();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152790
int Anim2dMsgGetCsr(void)
{
    return LayoutMsgGetCsr();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001527a8
void Anim2dMsgSet(int msg0, int msg1)
{
    LayoutMsgSet(msg0, msg1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001527c0
int Anim2dMsgGetPageBreakNum(void)
{
    return LayoutMsgGetPageBreakNum();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001527d8
void Anim2dMsgSetMode(int mode, int csr)
{
    LayoutMsgSetMode(mode, csr);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001527f0
void Anim2dMsgSetType(int mode)
{
    LayoutMsgSetMode(mode, 1);
}

// ──────────────────────────────────────────────────────────────────────
// Types 5/9 on_update. FF3 0x00152808.
// callcheck: B2dSetHookList, LayoutMsgSetCursorItems (Ghidra's body includes the on_open FUN_00152820)
int LayoutMsgOnUpdateB(int handle)
{
    return LayoutMsgOnUpdate(handle);
}

// ──────────────────────────────────────────────────────────────────────
// Types 5/9 on_open: FUN_001526d8 with the second (identical) hook list.
// FF3 0x00152820 (no Ghidra function; inside LayoutMsgOnUpdateB's body).
int FUN_00152820(int handle)
{
    B2dSetHookList(handle, layout_msg_hooks_b);
    LayoutMsgSetCursorItems(handle, 0x3f4, 0x3f5);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Types 5/9 on_close. FF3 0x00152898.
int LayoutMsgOnCloseB(int handle)
{
    return LayoutMsgOnClose(handle);
}

// ══════════════════════════════════════════════════════════════════════
// Layout animations
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// The slot's alpha: its alpha item's part 10 (else its alpha word), scaled
// by alpha_scale / 128. FF3 has no function for it: LayoutAnimMain and
// LayoutAnimIsAnyVisible inline the same code (with the null check).
static int LayoutAnimCalcAlpha(const LAYOUT_ANIM *anim)
{
    int alpha;

    if (anim == nullptr)
        return 0;
    if (anim->item == nullptr)
        alpha = anim->alpha;
    else
        alpha = B2dSprGetPartData(anim->item, 10);
    return (alpha * anim->alpha_scale) / 128;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001528b0
int LayoutAnimIsActive(const LAYOUT_ANIM *anim)
{
    if (anim->handle == -1)
        return 0;
    return anim->state != 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001528d0
LAYOUT_ANIM *LayoutAnimGetSlot(int slot)
{
    if ((u_int)slot < 8)
        return &layout_anim[slot];
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001528f8
LAYOUT_ANIM *LayoutAnimFindType(int type)
{
    for (int i = 0; i < 8; i++)
    {
        LAYOUT_ANIM *anim = LayoutAnimGetSlot(i);

        if (LayoutAnimIsActive(anim) && anim->type == type)
            return anim;
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152968
int LayoutAnimIsAnyActive(void)
{
    for (LAYOUT_ANIM *anim = layout_anim; anim < layout_anim + 8; anim++)
    {
        if (LayoutAnimIsActive(anim))
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when an active slot shows with alpha > 0 (RadioDraw). FF3 0x001529c0.
// callcheck: B2dSprGetPartData (LayoutAnimCalcAlpha, inlined in FF3)
int LayoutAnimIsAnyVisible(void)
{
    for (LAYOUT_ANIM *anim = layout_anim; anim < layout_anim + 8; anim++)
    {
        if (LayoutAnimIsActive(anim) && LayoutAnimCalcAlpha(anim) > 0)
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152a58
void LayoutAnimSetFlag(int slot, u_int bits)
{
    LayoutAnimGetSlot(slot)->flags |= bits;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152a88
void LayoutAnimClearFlag(int slot, u_int bits)
{
    LayoutAnimGetSlot(slot)->flags &= ~bits;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152ac0
int LayoutAnimGetState(int slot)
{
    return LayoutAnimGetSlot(slot)->state;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152ae0
int LayoutAnimIsDataReady(int slot)
{
    return BpCtlIsDataReady(LayoutAnimGetSlot(slot)->handle);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152b00
const LAYOUT_ANIM_TBL *LayoutAnimGetTypeTbl(int type)
{
    for (const LAYOUT_ANIM_TBL *tbl = layout_anim_tbl; tbl < layout_anim_tbl + 13; tbl++)
    {
        if (tbl->type == type)
            return tbl;
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152b38
const LAYOUT_ANIM_TBL *LayoutAnimGetTypeTblByHandle(int handle)
{
    for (int i = 0; i < 8; i++)
    {
        LAYOUT_ANIM *anim = LayoutAnimGetSlot(i);

        if (anim->handle == handle)
            return LayoutAnimGetTypeTbl(anim->type);
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152ba0
int LayoutAnimIsStateChanged(int slot)
{
    LAYOUT_ANIM *anim = LayoutAnimGetSlot(slot);

    return anim->state_old != anim->state;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152bd0
void LayoutAnimSetAlpha(int slot, int alpha)
{
    LayoutAnimGetSlot(slot)->alpha = alpha;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152bf8
int LayoutAnimGetAlpha(int slot)
{
    return LayoutAnimGetSlot(slot)->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152c18
void LayoutAnimSetAlphaScale(int slot, int scale)
{
    LayoutAnimGetSlot(slot)->alpha_scale = scale;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152c40
void LayoutAnimSetAlphaItem(int slot, B2D_ITEM *item)
{
    LayoutAnimGetSlot(slot)->item = item;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152c68
void LayoutAnimSetAlphaItemById(int slot, int handle, int id)
{
    LAYOUT_ANIM *anim = LayoutAnimGetSlot(slot);

    anim->item = B2dCtlFindItem(handle, id);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152cb8
void LayoutAnimSetAlphaItemByName(int slot, int handle, const char *name)
{
    LAYOUT_ANIM *anim = LayoutAnimGetSlot(slot);

    anim->item = B2dCtlFindItemByName(handle, name);
}

// ──────────────────────────────────────────────────────────────────────
// The item record of id in the item list of the type playing on handle.
// FF3 0x00152d08 (Ghidra's body runs on into the draw hook 0x152d70).
// callcheck: B2dGetDrawItem, BpGetDrawHandle, MsgPrintAt, GetLabelSuffixType, LabelAlignGetX, LayoutAnimFindItemAttr, SetCurColorPacked, SetDefaultGsDrawState (the hook FUN_00152d70's calls)
const LAYOUT_ANIM_ITEM *LayoutAnimFindItemAttr(int handle, int id)
{
    const LAYOUT_ANIM_TBL *tbl = LayoutAnimGetTypeTblByHandle(handle);

    if (tbl == nullptr || tbl->items->id < 0)
        return nullptr;
    for (const LAYOUT_ANIM_ITEM *item = tbl->items; item->id >= 0; item++)
    {
        if (item->id == id)
            return item;
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// Draw hook of the items in a type's item list: prints the item record's
// message in the item rect (t1), in its colour (t3) and alpha (stack[2]),
// aligned by the suffix of the item's label (_R right, _M centre). FF3
// does not check the record for nullptr.
// FF3 0x00152d70 (no Ghidra function; after LayoutAnimFindItemAttr's body).
static int FUN_00152d70(const B2D_HOOK_ARGS *args)
{
    const int              *rect = (const int *)args->t1;
    B2D_ITEM               *item = B2dGetDrawItem();
    const LAYOUT_ANIM_ITEM *attr = LayoutAnimFindItemAttr(BpGetDrawHandle(), item->id);
    int                     align;

    SetCurColorPacked((u_int)args->t3);
    align = GetLabelSuffixType(item->name);
    MsgPrintAt(attr->msg0, attr->msg1, LabelAlignGetX(align, rect), rect[1], (int)args->stack[2],
               item->part[11].value, 0, 0, align);
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Installs the draw hook on every item of the type's item list.
// FF3 0x00152e20 (FF3 leaves the hook struct's other words uninitialised).
void LayoutAnimSetItemHooks(int handle, const LAYOUT_ANIM_TBL *tbl)
{
    const LAYOUT_ANIM_ITEM *item = tbl->items;
    B2D_ITEM_HOOK           hook;

    if (item == nullptr || item->id < 0)
        return;
    do
    {
        B2D_ITEM *b2d = B2dCtlFindItem(handle, item->id);

        item++;
        hook.func = FUN_00152d70;
        B2dCtlSetItemHook(b2d, &hook);
    } while (item->id >= 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152e98
void LayoutAnimClear(int slot)
{
    LAYOUT_ANIM *anim = LayoutAnimGetSlot(slot);

    memset(anim, 0, sizeof(*anim));
    anim->state  = 0;
    anim->handle = -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00152ed8
void LayoutAnimClearAll(void)
{
    for (int i = 0; i < 8; i++)
        LayoutAnimClear(i);
}

// ──────────────────────────────────────────────────────────────────────
// Starts layout type `type` in a free slot: an active animation of the same
// type is closed (on_close) and reset first, the type's Bp file (per
// language for some) is requested, and the slot starts in state 1 with
// alpha and alpha_scale 0x80. Returns the slot, -1 for an unknown type or
// when all 8 are busy. FF3 0x00152f10.
int LayoutAnimStart(int type, int arg)
{
    const LAYOUT_ANIM_TBL *tbl = LayoutAnimGetTypeTbl(type);
    LAYOUT_ANIM           *old;
    LAYOUT_ANIM           *anim;
    int                    slot;

    if (tbl == nullptr)
        return -1;

    old = LayoutAnimFindType(type);
    if (old != nullptr)
    {
        tbl->on_close(old->handle);
        LayoutAnimReset(old->no);
    }

    PadStickDpadPush(1);
    anim = layout_anim;
    for (slot = 0; slot < 8; slot++, anim++)
    {
        int file;
        int handle;

        if (LayoutAnimIsActive(anim))
            continue;

        anim->no = slot;
        file = tbl->file;
        for (int i = 0; i < 2; i++)
        {
            if (file == layout_anim_lang_file[i])
                file += GetSysWrk()->GetLanguage();
        }

        if (tbl->type == 11)
            handle = BpCtlLoadReqLast(file, nullptr);
        else
            handle = BpCtlLoadReq(file, nullptr);

        anim->type        = type;
        anim->state       = 1;
        anim->arg         = arg;
        anim->alpha_scale = 0x80;
        anim->item        = nullptr;
        anim->handle      = handle;
        anim->state_old   = 0;
        anim->alpha       = 0x80;
        if (tbl->on_load_req != nullptr)
            tbl->on_load_req(handle);
        return slot;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001530a0
void LayoutAnimResetOne(int slot)
{
    LAYOUT_ANIM *anim = LayoutAnimGetSlot(slot);

    if (anim->handle >= 0)
        BpCtlRelease(anim->handle);
    LayoutAnimClear(slot);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001530e0
void LayoutAnimReset(int slot)
{
    PadStickDpadPop();
    if (slot < 0)
    {
        for (int i = 0; i < 8; i++)
            LayoutAnimResetOne(i);
        return;
    }
    LayoutAnimResetOne(slot);
}

// ──────────────────────────────────────────────────────────────────────
// 1 once the Bp layout's longest running part has stopped. FF3 0x00153138.
// callcheck: B2dCtlGetLongestPart, B2dPartIsPlaying (called; the nested call is not seen)
int LayoutAnimIsPlayEnd(int handle)
{
    return B2dPartIsPlaying(B2dCtlGetLongestPart(handle)) == 0;
}

// ──────────────────────────────────────────────────────────────────────
// Steps the 8 slots once per frame (state machine in anim_2d.h's comment).
// A slot whose on_close asks for it is reset and still ends the frame in
// state 4; a freed slot in state 4 drops to 0 the next frame. State 4 with
// a handle goes to 0 without releasing it. FF3 0x00153160 (returns 0, which
// no caller reads).
// callcheck: B2dSprGetPartData (LayoutAnimCalcAlpha, inlined in FF3)
void LayoutAnimMain(void)
{
    for (int i = 0; i < 8; i++)
    {
        LAYOUT_ANIM           *anim  = LayoutAnimGetSlot(i);
        const LAYOUT_ANIM_TBL *tbl   = LayoutAnimGetTypeTbl(anim->type);
        int                    alpha = LayoutAnimCalcAlpha(anim);

        anim->state_old = anim->state;
        if (anim->handle == -1)
        {
            if (anim->state == 4)
                anim->state = 0;
            continue;
        }

        switch (anim->state)
        {
        case 0:
            break;

        case 1:
            if (BpCtlIsDataReady(anim->handle) && !(anim->flags & LAYOUT_ANIM_WAIT))
            {
                const char *label;

                B2dCtlSetAlpha(anim->handle, alpha);
                LayoutAnimSetItemHooks(anim->handle, tbl);
                anim->state = 2;
                B2dCtlSetPos(anim->handle, 0, anim->arg);
                if (anim->flags & LAYOUT_ANIM_LABEL_NEW)
                    label = "NewLabel_0";   /* data 0x351298 */
                else if (anim->flags & LAYOUT_ANIM_LABEL_LAYOUT)
                    label = "layout";       /* sdata 0x36dbd8 */
                else
                    label = "in";           /* sdata 0x36dbd0 */
                BpPlayLabel(anim->handle, label);
                tbl->on_open(anim->handle);
            }
            break;

        case 2:
            B2dCtlSetAlpha(anim->handle, alpha);
            if (!(anim->flags & LAYOUT_ANIM_HOLD) && tbl->on_update(anim->handle))
                anim->state = 3;
            break;

        case 3:
            B2dCtlSetAlpha(anim->handle, alpha);
            if (LayoutAnimIsPlayEnd(anim->handle))
            {
                if (tbl->on_close(anim->handle))
                    LayoutAnimReset(i);
                anim->state = 4;
            }
            break;

        case 4:
            anim->state = 0;
            break;

        default:
            anim->state = 4;
            break;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00153370
void Anim2dInit(void)
{
    PadStickDpadStackReset();
    LayoutAnimClearAll();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00153390
void LayoutAnimResetAll(void)
{
    LayoutAnimReset(-1);
}

// ══════════════════════════════════════════════════════════════════════
// The per-language texture pack
// ══════════════════════════════════════════════════════════════════════

#define LANG_TEX_EE_ADRS 0x12f7100      // EE load address of the pack

// ──────────────────────────────────────────────────────────────────────
// Requests file 0xac9 + language into LANG_TEX_EE_ADRS. FF3 0x001533b0.
void LangTexLoadReq(void)
{
    LoadReq(0xac9 + GetSysWrk()->GetLanguage(), ReiPan_GetHostPointer(LANG_TEX_EE_ADRS));
}

// ──────────────────────────────────────────────────────────────────────
// Sends TIM2 'no' of the pack to the GS. FF3 0x00153418.
void LangTexSend(int no)
{
    Tim2PackSend(ReiPan_GetHostPointer(LANG_TEX_EE_ADRS), no, 0, -1, -1);
}

// ──────────────────────────────────────────────────────────────────────
// Out-of-line no-ops the STL allocator code calls (lock helpers?).
// FF3 0x00153450
int NullFuncRet0(void)
{
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00153458
void NullFunc(void)
{
}
