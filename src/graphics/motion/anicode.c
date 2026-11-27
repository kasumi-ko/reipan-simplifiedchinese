/* ==========================================================================
 *  graphics/motion/anicode.c
 *
 *  FF3 0x14cd30..0x1506d8 (see anicode.h): the stick_dpad stack, layout
 *  label and item hook helpers, and the layout type callbacks: the message
 *  viewer (0/1), item get (2), pause menu (4), yes/no (6/10/11), slot select
 *  (7) and menu window (8), plus the Now Loading classes.
 * ======================================================================== */
#include "graphics/motion/anicode.h"

#include <string.h>                     // strlen

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, CmnFadeLayer*
#include "common/cmn_util.h"            // NumGetDigits
#include "common/reipan_stub.h"
#include "graphics/gs_reg.h"            // SetGsRegCached
#include "graphics/graph2d/font.h"      // FontExTexSendVram
#include "graphics/graph2d/g2d_debug.h" // DispSqrRect
#include "graphics/graph2d/message.h"   // MsgPrintData, MsgPrintPage, PrintNumber, MsgReset, MsgPage*, SetCurColorRGB
#include "ingame/mission/mission_end.h"  // MissionLoadSetHooks
#include "ingame/ingame_wrk.h"          // title_start_flg
#include "ingame/menu/anim_2d.h"        // CTutorial, MsgPicHasKey, LayoutAnim*, Anim2dMsg*
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "outgame/pad_check.h"          // PadErrMsg*
#include "system/b2d/B2dBase.h"         // B2D_PLACE
#include "system/b2d/B2dCtl.h"
#include "system/b2d/B2dSpr.h"          // B2dSprGetPartData, B2dSprSetDisp/Pos, B2dSprFreeze*
#include "system/b2d/BpCtl.h"           // BpPlayLabel, BpCtl*, B2dDrawSpriteDirect
#include "system/pad/pad.h"             // pad_wrk, PadIsPressMode
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe, CheckPadRptPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// B2D_PLACE moved to system/b2d/B2dBase.h on 2026-10-15.

// ──────────────────────────────────────────────────────────────────────

// ══════════════════════════════════════════════════════════════════════
// The stick_dpad stack
// ══════════════════════════════════════════════════════════════════════

static s_char pad_stick_dpad_stack[0x10];       /* bss 0x391870 */
static int    pad_stick_dpad_depth;             /* sdata gp-0x5a88 */

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cd30
void PadStickDpadStackReset(void)
{
    pad_stick_dpad_depth = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cd38
void PadStickDpadPush(int value)
{
    pad_stick_dpad_stack[pad_stick_dpad_depth] = (s_char)pad_wrk[0].stick_dpad;
    pad_stick_dpad_depth++;
    pad_wrk[0].stick_dpad = value;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cd68
void PadStickDpadPop(void)
{
    pad_stick_dpad_depth--;
    if (pad_stick_dpad_depth < 0)
        pad_stick_dpad_depth = 0;
    pad_wrk[0].stick_dpad = pad_stick_dpad_stack[pad_stick_dpad_depth];
}

// ══════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cd98
int PadTrgGetMenuInput(void)
{
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0))     return 1;
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0)) return 2;
    if (CheckPadTrgPlaySe(0x8000, -1, 0))                  return 3;   // left
    if (CheckPadTrgPlaySe(0x2000, -1, 0))                  return 4;   // right
    if (CheckPadTrgPlaySe(0x1000, -1, 0))                  return 5;   // up
    if (CheckPadTrgPlaySe(0x4000, -1, 0))                  return 6;   // down
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ce60
int LabelAlignGetX(int type, const int *rect)
{
    if (type >= 0)
    {
        if (type < 2)
            return rect[0];
        if (type == 2)
            return (rect[0] + rect[6]) / 2;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// "..._M": 2, "..._R": 1, anything else 0. FF3 0x0014cea8.
int GetLabelSuffixType(const char *label)
{
    int len = (int)strlen(label);

    if (len > 2 && label[len - 2] == '_')
    {
        if (label[len - 1] == 'M')
            return 2;
        if (label[len - 1] == 'R')
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cf10
void SetCurColorPacked(u_int rgba)
{
    SetCurColorRGB(rgba & 0xff, (rgba >> 8) & 0xff, (rgba >> 16) & 0xff);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cf38
void MsgPrintAt(int msg0, int msg1, int x, int y, int a4, int a5, int a6, int a7, int a8)
{
    MsgPrintData(GetMsgDataAddr(msg0, msg1), x, y, 0x2d, a4, a5, a6, a7, a8);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014cfd0
void SetDefaultGsDrawState(void)
{
    B2dResetCurTex();
    SetGsRegCached(0x30003, 0x47, 1);           // TEST_1
    SetGsRegCached(0x10a000118ULL,0x4e, 1);     // ZBUF_1
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d018
int B2dDrawQuadDefault(const B2D_HOOK_ARGS *args)
{
    return B2dGetQuadFunc()(args);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d098
int B2dDrawItemDefault(const B2D_HOOK_ARGS *args)
{
    return B2dGetSpriteFunc()(args);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d158 (returns 0, which no caller reads)
void B2dSetHookList(int handle, const B2D_HOOK_REC *recs)
{
    for (; recs->func != nullptr; recs++)
    {
        B2D_ITEM     *item;
        B2D_ITEM_HOOK hook;

        if (recs->name == nullptr)
            item = B2dCtlFindItem(handle, recs->id);
        else
            item = B2dCtlFindItemByName(handle, recs->name);

        hook.func = recs->func;
        if (recs->kind == 0)
            B2dCtlSetItemHook(item, &hook);
        else
            B2dCtlSetItemHook2(item, &hook);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002adbf0 (linked with the singletons and template instances)
int MsgGetPicKey(int msg0, int msg1)
{
    for (int i = 0; i < MSG_PIC_KEY_NUM; i++)
    {
        if (DocGetMsgType((short)i, 0) == msg0 && msg_pic_key_dat[i].msg1 == msg1)
            return msg_pic_key_dat[i].key;
    }
    return -1;
}

// ══════════════════════════════════════════════════════════════════════
// The message viewer
// ══════════════════════════════════════════════════════════════════════

// The viewer's work (data 0x2f62e8, 0x38 bytes, zero in the ROM).
struct MSG_VIEW
{
    /* 0x00 */ int msg0;
    /* 0x04 */ int msg1;                // title; the paged body is msg1 + 2
    /* 0x08 */ int state;               // 0 text, 1 pictures
    /* 0x0c */ int pic_num;
    /* 0x10 */ int pics[MSG_VIEW_PIC_MAX];  // MENU_PIC picture numbers
    /* 0x30 */ int pic_cur;
    /* 0x34 */ int mode;                // MSG_VIEW_MODE_*
};
static_assert(sizeof(MSG_VIEW) == 0x38, "MSG_VIEW is 0x38 bytes");
static_assert(offsetof(MSG_VIEW, state) == 0x08, "MSG_VIEW.state");
static_assert(offsetof(MSG_VIEW, pic_num) == 0x0c, "MSG_VIEW.pic_num");
static_assert(offsetof(MSG_VIEW, pics) == 0x10, "MSG_VIEW.pics");
static_assert(offsetof(MSG_VIEW, pic_cur) == 0x30, "MSG_VIEW.pic_cur");
static_assert(offsetof(MSG_VIEW, mode) == 0x34, "MSG_VIEW.mode");

static MSG_VIEW      msg_view;                  /* data 0x2f62e8 */
static MENU_PIC *msg_view_map;              /* sdata gp-0x5a80: the picture state's pictures */
static CTutorial      *msg_view_pic;              /* sdata gp-0x5a7c: the text state's message picture */
static bool msg_view_alternate_layout;

static int MsgViewSelectPic(int no, int force);
static void MsgViewPicFirst(void);

// ──────────────────────────────────────────────────────────────────────
// The fade layer label: 5 when the message has its own picture, 7 when
// only viewer pictures exist, else 5. FF3 0x0014d228.
static int MsgViewGetFadeLabel(void)
{
    if (MsgPicHasKey(MsgGetPicKey(msg_view.msg0, msg_view.msg1)))
        return 5;
    return msg_view.pic_num > 0 ? 7 : 5;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d280
static void MsgViewPicReset(void)
{
    if (msg_view_map != nullptr)
        msg_view_map->pic = -2;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d298
void MsgViewSetMode(int mode)
{
    msg_view.mode = mode;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d2a8
void MsgViewSetMsg(int msg0, int msg1)
{
    msg_view.pic_cur = 0;
    msg_view.msg0    = msg0;
    msg_view.msg1    = msg1;
    MsgPageReset();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d2d0 (no bound check on n)
// callcheck: B2dGetDrawItem, FontExTexSendVram, MsgPicDraw, MsgPrintPage, SetCurColorPacked, SetDefaultGsDrawState
//   (Ghidra's body runs on into the hook FUN_0014d350 after the tail call)
void MsgViewSetPics(const int *pics, int n)
{
    MsgViewPicReset();
    if (pics == nullptr || n <= 0 || n > MSG_VIEW_PIC_MAX)
    {
        msg_view.pic_num = 0;
    }
    else
    {
        msg_view.pic_num = n;
        for (int i = 0; i < n; i++)
            msg_view.pics[i] = pics[i];
    }
    MsgViewPicFirst();
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the body text item: the current page of (msg0, msg1 + 2) and the
// message's own picture. Words: t1 xy, t3 colour, stack[2] alpha.
// FF3 0x0014d350 (no Ghidra function: it follows MsgViewSetPics's tail call).
static int FUN_0014d350(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_int      rgba  = (u_int)args->t3;
    int        alpha = (int)args->stack[2];
    B2D_ITEM  *item  = B2dGetDrawItem();

    FontExTexSendVram();
    SetCurColorPacked(rgba);
    MsgPrintPage(msg_view.msg0, msg_view.msg1 + 2, xy[0], xy[1], 0x2d, alpha,
                 item->part[B2D_PART_TEXT].value, 0, 3, 0);
    if (msg_view_pic->active != 0)
        msg_view_pic->Draw(0x40, 0x68, (float)alpha * (1.0f / 128.0f));   /* sdata 0x36da78 */
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the title item. FF3 0x0014d420.
static int MsgViewDrawTitle(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_int      rgba  = (u_int)args->t3;
    int        alpha = (int)args->stack[2];

    FontExTexSendVram();
    SetCurColorPacked(rgba);
    MsgPrintAt(msg_view.msg0, msg_view.msg1, xy[0], xy[1], alpha, 0, 0, 0, 0);
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the page number item: "page / pages", the separator is message
// (0x82, 6) and the numbers sit around x + 0x18. FF3 0x0014d498.
static int MsgViewDrawPageNum(const B2D_HOOK_ARGS *args)
{
#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44's hook at 0x0014d470 draws nothing. Its group 0x82
    // contains memory-card prompts, including the port's separator ID.
    (void)args;
#else
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];
    int        x     = xy[0];
    int        y     = xy[1];
    int        page;
    int        pages;

    SetCurColorPacked((u_int)args->t3);
    page  = MsgGetPage() + 1;
    pages = MsgGetPageBreakNum(msg_view.msg0, msg_view.msg1 + 2);
    PrintNumber(page, x + 0x18 - NumGetDigits(page, 1) * 0xc, y, 0x2d, alpha & 0xff, 0, 1);
    MsgPrintAt(0x82, 6, x + 0x1b, y, alpha, 0, 0, 0, 0);
    PrintNumber(pages, x + 0x18 + MsgGetLineWidth(GetMsgDataAddr(0x82, 6), nullptr), y, 0x2d,
                alpha & 0xff, 0, 1);
    SetDefaultGsDrawState();
#endif
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the picture item: steps the viewer's pictures and draws both
// buffers at the item's place, at least alpha 0x20. FF3 0x0014d5d0.
static int MsgViewDrawPic(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];

    if (msg_view_map == nullptr)
        return 0;

    msg_view_map->Step();
    if (alpha < 0x20)
        alpha = 0x20;
    msg_view_map->pri = 0xa0;
    msg_view_map->x    = (short)xy[0];  // the low halves (lh)
    msg_view_map->y    = (short)xy[1];
    msg_view_map->z = 0;
    msg_view_map->scale = (float)alpha * (1.0f / 128.0f);   /* sdata 0x36da7c */
    msg_view_map->Draw();
    msg_view_map->DrawNext();
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Previous-page arrow. FF3 0x0014d680.
static int MsgViewDrawPrevArrow(const B2D_HOOK_ARGS *args)
{
    if (MsgGetPage() == 0)
        return 0;
    return B2dDrawItemDefault(args);
}

// ──────────────────────────────────────────────────────────────────────
// Next-page arrow. FF3 0x0014d798.
static int MsgViewDrawNextArrow(const B2D_HOOK_ARGS *args)
{
    if (MsgGetPage() + 1 < MsgGetPageBreakNum(msg_view.msg0, msg_view.msg1 + 2) &&
        msg_view.mode != MSG_VIEW_MODE_1)
        return B2dDrawItemDefault(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d8e8
static int MsgViewDrawPrevArrowQuad(const B2D_HOOK_ARGS *args)
{
    if (MsgGetPage() != 0 && msg_view.mode != MSG_VIEW_MODE_1)
        return B2dDrawQuadDefault(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014d9a8
static int MsgViewDrawNextArrowQuad(const B2D_HOOK_ARGS *args)
{
    if (MsgGetPage() + 1 < MsgGetPageBreakNum(msg_view.msg0, msg_view.msg1 + 2) &&
        msg_view.mode != MSG_VIEW_MODE_1)
        return B2dDrawQuadDefault(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Picture paging marks, only with more than one picture. FF3 0x0014da90.
static int MsgViewDrawPicSelQuad(const B2D_HOOK_ARGS *args)
{
    if (msg_view.pic_num > 1)
        return B2dDrawQuadDefault(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Draws nothing. FF3 0x0014dac0 (no Ghidra function).
static int FUN_0014dac0(const B2D_HOOK_ARGS *args)
{
    (void)args;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014dac8 (returns 0, which no caller reads)
static int MsgViewSelectPic(int no, int force)
{
    if (msg_view_map != nullptr && msg_view.pic_num != 0 && (force || no != msg_view.pic_cur))
    {
        msg_view.pic_cur  = no;
        msg_view_map->pic = msg_view.pics[no];
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014db10
static void MsgViewPicFirst(void)
{
    int label;

    MsgViewSelectPic(0, 1);
    label = MsgViewGetFadeLabel();
    if (label != GetCmnFadeLayer()->next_label)
        GetCmnFadeLayer()->Switch(label, 0x1e);
}

static bool MsgViewMouseArrowClicked(int handle, int item)
{
    ReiPan_MenuBox box;
    return ReiPan_MenuMouseItemBox(handle, item, &box) &&
           ReiPan_MenuMouseClick(box.x, box.y, box.w, box.h);
}

// ──────────────────────────────────────────────────────────────────────
// The text state: page with OK/right/left, R1/L1 back to the first
// picture, button 0x20 to the picture state, close with cancel (or OK on
// the last page, by mode). 1 when closing. FF3 0x0014db78.
// callcheck: MsgPicSetPage (member SetPage)
static int MsgViewUpdateText(int handle)
{
    int wait = MsgIsPageWait();
    int se;
    int page;
    int page_count;
    bool clicked_prev;
    bool clicked_next;

    if (wait == 0)
    {
        if (msg_view.mode != MSG_VIEW_MODE_1)
            return 0;
        goto close;
    }
    if (wait != 1)
        return 0;

    // The page arrows take their own clicks before the page handles a click as OK.
    page = MsgGetPage();
    page_count = MsgGetPageBreakNum(msg_view.msg0, msg_view.msg1 + 2);
    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_HORIZONTAL, nullptr);
    clicked_prev = msg_view.mode != MSG_VIEW_MODE_1 && page > 0 &&
        MsgViewMouseArrowClicked(handle, msg_view_alternate_layout ? 0x40a : 0x3fc);
    clicked_next = msg_view.mode != MSG_VIEW_MODE_1 && page + 1 < page_count &&
        MsgViewMouseArrowClicked(handle, msg_view_alternate_layout ? 0x3f1 : 0x3fa);
    ReiPan_MenuMouseConfirm();

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0))
    {
        if (page + 1 < page_count)
            goto next_page;
        if (msg_view.mode == MSG_VIEW_MODE_1 || msg_view.mode == MSG_VIEW_MODE_2)
        {
            se = 0x1a9b0005;
            goto close_se;
        }
        goto buzzer;
    }

    if (clicked_next || CheckPadTrgPlaySe(0x2000, -1, 0))       // right
    {
        if (msg_view.mode != MSG_VIEW_MODE_0 && msg_view.mode != MSG_VIEW_MODE_2)
            goto buzzer;
        if (page + 1 < page_count)
            goto next_page;
        goto buzzer;
    }

    if (clicked_prev || CheckPadTrgPlaySe(0x8000, -1, 0))       // left
    {
        if (msg_view.mode != MSG_VIEW_MODE_0 && msg_view.mode != MSG_VIEW_MODE_2)
            goto buzzer;
        if (page <= 0)
            goto buzzer;
        SndSePlay(0x1a9b0005, 100, 0x1000, 0, 0, 0);
        MsgPagePrev();
        goto set_page;
    }

    if ((CheckPadTrgPlaySe(8, -1, 0) && msg_view.mode != MSG_VIEW_MODE_2) ||   // R1
        (CheckPadTrgPlaySe(4, -1, 0) && msg_view.mode != MSG_VIEW_MODE_2))     // L1
    {
        if (msg_view.mode == MSG_VIEW_MODE_0 || msg_view.mode == MSG_VIEW_MODE_2)
            MsgViewPicFirst();
        return 0;
    }

    if (CheckPadTrgPlaySe(0x20, -1, 0) &&
        !MsgPicHasKey(MsgGetPicKey(msg_view.msg0, msg_view.msg1)))
    {
        if (msg_view.pic_num <= 0)
            return 0;
        BpPlayLabel(handle, "gazou");                           /* sdata 0x36da88 */
        msg_view.state = 1;
        return 0;
    }

    if (!CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0))
        return 0;
    if (msg_view.mode != MSG_VIEW_MODE_0 && msg_view.mode != MSG_VIEW_MODE_2)
        goto buzzer;
    se = 0x1a9b0001;

close_se:
    SndSePlay(se, 100, 0x1000, 0, 0, 0);
close:
    BpPlayLabel(handle, "txt_out");                             /* sdata 0x36da80 */
    MsgViewPicReset();
    return 1;

next_page:
    SndSePlay(0x1a9b0005, 100, 0x1000, 0, 0, 0);
    MsgPageNext();
set_page:
    msg_view_pic->SetPage(MsgGetPage());
    return 0;

buzzer:
    SndSePlay(0x1a9b0002, 100, 0x1000, 0, 0, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014deb8
void MsgViewBackToText(int handle)
{
    if (msg_view.state == 1)
    {
        BpPlayLabel(handle, "txt");                             /* sdata 0x36da90 */
        msg_view.state = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The picture state: OK/right next picture, left previous (both wrap),
// R1/L1 first picture, button 0x20 back to the text, cancel closes. 1 when
// closing. FF3 0x0014df00.
// callcheck: CmnFadeLayerUpdate, GetCmnFadeLayer, MsgGetPicKey, MsgPicUpdate, MsgViewUpdatePic, MsgViewUpdateText
//   (Ghidra's body runs on into FUN_0014e040)
static int MsgViewUpdatePic(int handle)
{
    int no;

    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_HORIZONTAL, nullptr);
    const bool clicked_prev = msg_view.pic_num > 1 &&
        MsgViewMouseArrowClicked(handle, msg_view_alternate_layout ? 0x40f : 0x3f5);
    const bool clicked_next = msg_view.pic_num > 1 &&
        MsgViewMouseArrowClicked(handle, msg_view_alternate_layout ? 0x40d : 0x3ed);
    ReiPan_MenuMouseConfirm();

    if (clicked_next || CheckPadTrgPlaySe(pad_key[PAD_KEY_OK] | 0x2000, -1, 0))
    {
        no = msg_view.pic_cur + 1;
        if (no >= msg_view.pic_num)
            no = 0;
    }
    else if (clicked_prev || CheckPadTrgPlaySe(0x8000, -1, 0))
    {
        no = msg_view.pic_cur - 1;
        if (no < 0)
            no = msg_view.pic_num - 1;
    }
    else
    {
        if (CheckPadTrgPlaySe(8, -1, 0) || CheckPadTrgPlaySe(4, -1, 0))
        {
            MsgViewPicFirst();
            return 0;
        }
        if (CheckPadTrgPlaySe(0x20, -1, 0))
        {
            MsgViewBackToText(handle);
            return 0;
        }
        if (!CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], 0x1a9b0001, 0))
            return 0;
        BpPlayLabel(handle, "gazou_out");                       /* data 0x350af0 */
        MsgViewPicReset();
        return 1;
    }
    MsgViewSelectPic(no, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Types 0/1 on_update. FF3 0x0014e040 (no Ghidra function: it follows
// MsgViewUpdatePic's body).
int FUN_0014e040(int handle)
{
    int ret = 0;

    msg_view_pic->Update(MsgGetPicKey(msg_view.msg0, msg_view.msg1));
    if (msg_view.mode == MSG_VIEW_MODE_1)
        GetCmnFadeLayer()->Update();

    if (msg_view.state == 0)
        ret = MsgViewUpdateText(handle);
    else if (msg_view.state == 1)
        ret = MsgViewUpdatePic(handle);
    else
        msg_view.state = 0;
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014e0f8
// callcheck: operator_new, MsgPicCtor (new CTutorial, new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)
static void MsgViewOpen(void)
{
    msg_view_pic = new CTutorial();
    msg_view_map = new MENU_PIC(0);
    msg_view_map->v = 0;
    msg_view_map->u = 0;
    msg_view_map->h    = 0xfe;
    msg_view_map->w    = 0x17c;
    msg_view_map->enable = 1;
    msg_view.state   = 0;
    msg_view.pic_cur = 0;
    if (msg_view.pic_num > 0)
        msg_view_map->pic = msg_view.pics[0];
}

// ──────────────────────────────────────────────────────────────────────
// Kind 1 hook of item 0x3f4. FF3 0x0014e198.
static int MsgViewDrawQuadReset(const B2D_HOOK_ARGS *args)
{
    SetDefaultGsDrawState();
    return B2dDrawQuadDefault(args);
}

// Item hooks of layout types 0 and 1 (data 0x350b00 / 0x350c20, copied to
// the stack before B2dSetHookList).
static const B2D_HOOK_REC msg_view_hooks_0[] =
{
    { FUN_0014d350,             0, 0x3ff, nullptr },
    { MsgViewDrawTitle,         0, 0x402, nullptr },
    { MsgViewDrawPageNum,       0, 0x403, nullptr },
    { MsgViewDrawPic,           0, 0x400, nullptr },
    { MsgViewDrawPrevArrow,     0, 0x3fb, nullptr },
    { MsgViewDrawPrevArrow,     0, 0x3fc, nullptr },
    { MsgViewDrawNextArrow,     0, 0x3fa, nullptr },
    { MsgViewDrawNextArrow,     0, 0x3f6, nullptr },
    { MsgViewDrawPrevArrowQuad, 1, 0x3fb, nullptr },
    { MsgViewDrawPrevArrowQuad, 1, 0x3fc, nullptr },
    { MsgViewDrawNextArrowQuad, 1, 0x3fa, nullptr },
    { MsgViewDrawNextArrowQuad, 1, 0x3f6, nullptr },
    { MsgViewDrawQuadReset,     1, 0x3f4, nullptr },
    { MsgViewDrawPicSelQuad,    0, 0x3ee, nullptr },
    { MsgViewDrawPicSelQuad,    0, 0x3f5, nullptr },
    { MsgViewDrawPicSelQuad,    0, 0x408, nullptr },
    { MsgViewDrawPicSelQuad,    0, 0x3ed, nullptr },
    { nullptr,                 -1,    -1, nullptr },
};
static const B2D_HOOK_REC msg_view_hooks_1[] =
{
    { FUN_0014d350,             0, 0x3ee, nullptr },
    { MsgViewDrawTitle,         0, 0x3ea, nullptr },
    { MsgViewDrawPageNum,       0, 0x3eb, nullptr },
    { MsgViewDrawPic,           0, 0x40c, nullptr },
    { MsgViewDrawPrevArrow,     0, 0x40b, nullptr },
    { MsgViewDrawPrevArrow,     0, 0x40a, nullptr },
    { MsgViewDrawNextArrow,     0, 0x3f0, nullptr },
    { MsgViewDrawNextArrow,     0, 0x3f1, nullptr },
    { MsgViewDrawPrevArrowQuad, 1, 0x40b, nullptr },
    { MsgViewDrawPrevArrowQuad, 1, 0x40a, nullptr },
    { MsgViewDrawNextArrowQuad, 1, 0x3f0, nullptr },
    { MsgViewDrawNextArrowQuad, 1, 0x3f1, nullptr },
    { MsgViewDrawPicSelQuad,    1, 0x410, nullptr },
    { MsgViewDrawPicSelQuad,    1, 0x40f, nullptr },
    { MsgViewDrawPicSelQuad,    1, 0x40e, nullptr },
    { MsgViewDrawPicSelQuad,    1, 0x40d, nullptr },
    { FUN_0014dac0,             0, 0x3f2, nullptr },
    { nullptr,                 -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Type 0 on_open. FF3 0x0014e210.
int MsgViewOnOpen(int handle)
{
    msg_view_alternate_layout = false;
    B2dSetHookList(handle, msg_view_hooks_0);
    if (msg_view.mode == MSG_VIEW_MODE_2)
        GetCmnFadeLayer()->Show(MsgViewGetFadeLabel());
    else
        GetCmnFadeLayer()->Switch(MsgViewGetFadeLabel(), 0x1e);
    GetCmnFadeLayer()->SetItem(handle, 0x405);
    MsgViewOpen();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Type 1 on_open. FF3 0x0014e2f8 (no Ghidra function: it follows
// MsgViewOnOpen).
int FUN_0014e2f8(int handle)
{
    msg_view_alternate_layout = true;
    B2dSetHookList(handle, msg_view_hooks_1);
    if (msg_view.mode != MSG_VIEW_MODE_2)
    {
        GetCmnFadeLayer()->Show(10);
        GetCmnFadeLayer()->SetItem(handle, 0x3f3);
        GetCmnFadeLayer()->base_alpha = 0;
    }
    MsgViewOpen();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Types 0/1 on_close. FF3 0x0014e3b8.
// callcheck: operator_delete (delete msg_view_map; msg_view_pic through its deleting dtor)
// callcheck: MenuPicDtor (MENU_PIC's destructor)
int MsgViewOnClose(int handle)
{
    (void)handle;
    if (msg_view.mode != MSG_VIEW_MODE_2)
    {
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        if (msg_view.mode == MSG_VIEW_MODE_1)
            GetCmnFadeLayer()->Update();
    }
    if (msg_view_map != nullptr)
    {
        delete msg_view_map;
        msg_view_map = nullptr;
    }
    if (msg_view_pic != nullptr)
    {
        delete msg_view_pic;
        msg_view_pic = nullptr;
    }
    MsgReset();
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// The item-get window (layout type 2)
// ══════════════════════════════════════════════════════════════════════

// The window's work (bss 0x391880).
struct ITEM_GET
{
    /* 0x00 */ int   pic;               // MENU_PIC picture number
    /* 0x04 */ int   msg0;
    /* 0x08 */ int   msg1;
    /* 0x0c */ int   close_req;
    /* 0x10 */ u_int flags;             // ITEM_GET_*
};
static_assert(sizeof(ITEM_GET) == 0x14, "ITEM_GET is 0x14 bytes");
static_assert(offsetof(ITEM_GET, close_req) == 0x0c, "ITEM_GET.close_req");
static_assert(offsetof(ITEM_GET, flags) == 0x10, "ITEM_GET.flags");

static ITEM_GET      item_get;                  /* bss 0x391880 */
static MENU_PIC *item_get_pic;              /* sdata gp-0x5a58 */

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014e470
void ItemGetSetPic(int pic)
{
    item_get.pic = pic;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014e480
void ItemGetSetMsg(int msg0, int msg1)
{
    item_get.msg1 = msg1;
    item_get.msg0 = msg0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014e498
void ItemGetReqClose(int req)
{
    item_get.close_req = req;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014e4a8
void ItemGetSetFlags(u_int flags)
{
    item_get.flags = flags;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the name text item (hook 5: a0 item name, t1 xy, t3 colour,
// stack[2] alpha), aligned by the item name's suffix. FF3 0x0014e4b8.
static int ItemGetDrawMsg(const B2D_HOOK_ARGS *args)
{
    const char *name  = (const char *)args->a0;
    const int  *xy    = (const int *)args->t1;
    int         alpha = (int)args->stack[2];
    int         type;

    if ((item_get.flags & ITEM_GET_NO_MSG) == 0 && item_get.msg0 >= 0 && item_get.msg1 >= 0)
    {
        SetCurColorPacked((u_int)args->t3);
        type = GetLabelSuffixType(name);
        MsgPrintAt(item_get.msg0, item_get.msg1, LabelAlignGetX(type, xy), xy[1], alpha,
                   0, 0, 0, type);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Draws nothing (item 0x415). FF3 0x0014e570 (no Ghidra function: the
// tail of ItemGetDrawMsg's).
static int FUN_0014e570(const B2D_HOOK_ARGS *args)
{
    (void)args;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the picture item (rect: a3 x, t0 y, t3 alpha). FF3 0x0014e578.
static int ItemGetDrawPic(const B2D_HOOK_ARGS *args)
{
    MENU_PIC *pic = item_get_pic;

    if ((item_get.flags & ITEM_GET_NO_PIC) == 0 && pic != nullptr)
    {
        pic->z = 0;
        pic->x    = (short)args->a3;    // the low halves
        pic->pri = 0xa0;
        pic->y    = (short)args->t0;
        pic->scale = (float)(int)args->t3 * (1.0f / 128.0f);  /* sdata 0x36da9c */
        pic->Draw();
        pic->DrawNext();
        SetGsRegCached(0x100000141ULL, 0x14, 1);    // TEX1_1
        SetDefaultGsDrawState();
    }
    return 0;
}

// Item hooks of type 2 (data 0x350d50, copied to the stack).
static const B2D_HOOK_REC item_get_hooks[] =
{
    { FUN_0014e570,   0, 0x415, nullptr },
    { ItemGetDrawMsg, 0, 0x40c, nullptr },
    { ItemGetDrawPic, 0, 0x40b, nullptr },
    { nullptr,        0,    -1, nullptr },      // EE: id -1, name word -1
};

// ──────────────────────────────────────────────────────────────────────
// Type 2 on_update: steps the picture, shows items 0x40d / 0x3ec by the
// flags, plays "out" on a close request. FF3 0x0014e630.
int ItemGetOnUpdate(int handle)
{
    static const struct { u_int flag; int id; } disp[] =   /* data 0x350d40 */
    {
        { ITEM_GET_NO_MSG, 0x40d },
        { ITEM_GET_NO_3EC, 0x3ec },
    };

    item_get_pic->Step();
    for (int i = 0; i < 2; i++)
        B2dSprSetDisp(B2dCtlFindItem(handle, disp[i].id), (item_get.flags & disp[i].flag) == 0);

    if (item_get.close_req == 0)
        return 0;
    BpPlayLabel(handle, "out");                                 /* sdata 0x36daa0 */
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Type 2 on_load_req: nothing (jr ra; v0 is left as it was, nobody reads
// it). FF3 0x0014e710 (no Ghidra function).
int FUN_0014e710(int handle)
{
    (void)handle;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Type 2 on_open. FF3 0x0014e718.
// callcheck: operator_new (new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)
int ItemGetOnOpen(int handle)
{
    B2dSetHookList(handle, item_get_hooks);

    item_get_pic = new MENU_PIC(0);
    item_get_pic->v = 0;
    item_get_pic->u = 0;
    item_get_pic->h    = 0x100;
    item_get_pic->w    = 0x100;
    item_get_pic->enable = 1;

    B2dSprFreezeItem(handle, 0x3ec, 0);
    item_get.close_req = 0;
    if (item_get.flags & ITEM_GET_NO_MSG)
    {
        B2D_ITEM *item = B2dCtlFindItem(handle, 0x40e);

        B2dSprFreezePart(handle, item->id, B2D_PART_X, 0);
        B2dSprFreezePart(handle, item->id, B2D_PART_Y, 0);
        int x = B2dSprGetPartData(item, B2D_PART_X);
        B2dSprSetPos(item, x, B2dSprGetPartData(item, B2D_PART_Y) - 0x1e);
    }
    item_get_pic->pic = item_get.pic;
    return 0;                           // EE: v0 of the last call; nobody reads it
}

// ──────────────────────────────────────────────────────────────────────
// Type 2 on_close. FF3 0x0014e870.
// callcheck: operator_delete (delete item_get_pic)
// callcheck: MenuPicDtor (MENU_PIC's destructor)
int ItemGetOnClose(int handle)
{
    (void)handle;
    if (item_get_pic != nullptr)
    {
        delete item_get_pic;
        item_get_pic = nullptr;
    }
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// The Now Loading screens
// ══════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────
// Its copy: FF3 0x0014e908
// FF3 0x0014e8b0
CcmLoading::CcmLoading(int file_no)
{
    bg_a  = 0x80;
    file  = file_no;
    bg_r  = 0;
    bg_g  = 0;
    bg_b  = 0;
    BpCtlLoadReq(file_no, nullptr);
    state = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Its copy: FF3 0x0014e998; the deleting destructor: FF3 0x0014e9d0
// FF3 0x0014e960
CcmLoading::~CcmLoading()
{
    BpCtlRelease(BpCtlGetHandleByFileNo(file));
}

// ──────────────────────────────────────────────────────────────────────
// The whole screen (640 x 448). FF3 0x0014ea10.
void CcmLoading::DrawBg(u_char r, u_char g, u_char b, u_char a)
{
    DispSqrRect(0.0f, 0.0f, 640.0f, 448.0f, 0, r, g, b, a);    /* sdata 0x36daa8, 0x36daac */
}

// ──────────────────────────────────────────────────────────────────────
// Nonzero also for -1 (no entry). FF3 0x0014ea58.
int CcmLoading::IsReady(void)
{
    return BpCtlIsDataReady(BpCtlGetHandleByFileNo(file));
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ea78
// callcheck: CcmLoadingIsReady, CcmLoadingCtor (member IsReady; Ghidra's body runs on into the
//   CcmLoadingNormal ctor copy at 0x14eb58)
void CcmLoading::Main(void)
{
    DrawBg(bg_r, bg_g, bg_b, bg_a);
    BpCtlMain();

    switch (state)
    {
    case 0:
        if (IsReady())
        {
            OnReady();
            state = 1;
        }
        break;
    case 1:
        StepShow();
        break;
    case 2:
        StepLeave();
        break;
    case 3:
        StepDone();
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014eb90 (and its copy 0x0014eb58)
// callcheck: CcmLoadingCtor (the base constructor)
CcmLoadingNormal::CcmLoadingNormal(int file_no) : CcmLoading(file_no)
{
    alpha = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ec50 (no Ghidra function)
void CcmLoadingNormal::OnReady(void)
{
    B2dCtlSetAlpha(BpCtlGetHandleByFileNo(file), 0);
    BpPlayLabel(BpCtlGetHandleByFileNo(file), "NewLabel_0");    /* data 0x350d90 */
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ec98
// callcheck: CcmLoadingCtor (Ghidra's body runs on into the CcmLoadingMission ctor copy at 0x14ed38)
void CcmLoadingNormal::StepShow(void)
{
    alpha += 8;
    if (alpha > 0x80)
        alpha = 0x80;
    B2dCtlSetAlpha(BpCtlGetHandleByFileNo(file), alpha);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ece8 (no Ghidra function)
void CcmLoadingNormal::StepLeave(void)
{
    alpha -= 8;
    if (alpha < 0)
    {
        alpha = 0;
        state = 3;
    }
    B2dCtlSetAlpha(BpCtlGetHandleByFileNo(file), alpha);
}

// ──────────────────────────────────────────────────────────────────────
// Its copy: FF3 0x0014ebf0; the deleting destructor: FF3 0x0014ec18
// FF3 0x0014ebc8
// callcheck: CcmLoadingDtor (the base destructor)
CcmLoadingNormal::~CcmLoadingNormal()
{
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ed68 (and its copy 0x0014ed38)
// callcheck: CcmLoadingCtor (the base constructor)
CcmLoadingMission::CcmLoadingMission(int file_no) : CcmLoading(file_no)
{
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ee20 (no Ghidra function)
void CcmLoadingMission::OnReady(void)
{
    BpPlayLabel(BpCtlGetHandleByFileNo(file), "in");            /* sdata 0x36dab0 */
    MissionLoadSetHooks();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ee50 (no Ghidra function): only the call of the empty 0x1ad1b8.
void CcmLoadingMission::StepShow(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ee68 (no Ghidra function; then the empty 0x1ad1b8)
void CcmLoadingMission::StepLeave(void)
{
    if (BpCtlIsLoadEnd(file))
        state = 3;
}

// ──────────────────────────────────────────────────────────────────────
// Before the layout is ready: done at once. FF3 0x0014eea0 (no Ghidra
// function).
void CcmLoadingMission::End(void)
{
    if (state == 0)
    {
        state = 3;
        return;
    }
    BpPlayLabel(BpCtlGetHandleByFileNo(file), "out");           /* sdata 0x36dab8 */
    state = 2;
}

// ──────────────────────────────────────────────────────────────────────
// Its copy: FF3 0x0014edc0; the deleting destructor: FF3 0x0014ede8
// FF3 0x0014ed98
// callcheck: CcmLoadingDtor (the base destructor)
CcmLoadingMission::~CcmLoadingMission()
{
}

// ══════════════════════════════════════════════════════════════════════
// The yes/no dialog (layout types 6, 10, 11)
// ══════════════════════════════════════════════════════════════════════

// The dialog's work (bss 0x391898).
struct YES_NO
{
    /* 0x00 */ int uv[2][2];            // parts 1, 2 (u, v) of the sprites 0x3f4 and 0x3f5
    /* 0x10 */ int msg0;
    /* 0x14 */ int msg1;
    /* 0x18 */ int close_req;
    /* 0x1c */ int sel;                 // 0, 1; 2 none
};
static_assert(sizeof(YES_NO) == 0x20, "YES_NO is 0x20 bytes");
static_assert(offsetof(YES_NO, msg0) == 0x10, "YES_NO.msg0");
static_assert(offsetof(YES_NO, close_req) == 0x18, "YES_NO.close_req");
static_assert(offsetof(YES_NO, sel) == 0x1c, "YES_NO.sel");

static YES_NO yes_no;                           /* bss 0x391898 */

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014eef0
void YesNoSetMsg(int msg0, int msg1)
{
    yes_no.msg0 = msg0;
    yes_no.msg1 = msg1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ef08
void YesNoSetSel(int sel)
{
    yes_no.sel = sel;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ef18
void YesNoReqClose(int req)
{
    yes_no.close_req = req;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the question item (hook 5). FF3 0x0014ef28.
static int YesNoDrawMsg(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];

    if (yes_no.msg1 >= 0 && yes_no.msg0 >= 0)
    {
        SetCurColorPacked((u_int)args->t3);
        MsgPrintPage(yes_no.msg0, yes_no.msg1, xy[0], xy[1], 0x2d, alpha, 0, 0, 0, 0);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// A choice: message (1, msg1) centred between xy[0] and xy[4], in the
// item's colour when selected, else (0xd6, 0xf4, 0xab). Not drawn with
// selection 2. FF3 0x0014efb0 and 0x0014f058 inline this.
static void YesNoDrawChoice(const B2D_HOOK_ARGS *args, int msg1, int no)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];

    if (yes_no.sel == 2)
        return;
    if (yes_no.sel == no)
        SetCurColorPacked((u_int)args->t3);
    else
        SetCurColorRGB(0xd6, 0xf4, 0xab);
#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44 uses the left anchor stored in its yes/no layout.
    MsgPrintAt(1, msg1, xy[0], xy[1], alpha, 0, 0, 0, 0);
#else
    MsgPrintAt(1, msg1, (xy[0] + xy[4]) / 2, xy[1], alpha, 0, 0, 0, 2);
#endif
    SetDefaultGsDrawState();
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the first choice. FF3 0x0014efb0.
// callcheck: MsgPrintAt, SetCurColorPacked, SetCurColorRGB, SetDefaultGsDrawState (in YesNoDrawChoice)
static int YesNoDrawYes(const B2D_HOOK_ARGS *args)
{
    YesNoDrawChoice(args, 1, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the second choice. FF3 0x0014f058 (no Ghidra function: it
// follows YesNoDrawYes).
static int FUN_0014f058(const B2D_HOOK_ARGS *args)
{
    YesNoDrawChoice(args, 2, 1);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Sprite 0x3f4 with the texture of 0x3f5 while the selection is 1 (and
// the other way round in YesNoDrawCursor1); a1 / a2 are the sprite's u, v.
// FF3 0x0014f100 (no Ghidra function: it follows YesNoDrawYes).
static int FUN_0014f100(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS a = *args;
    int           n;

    if (yes_no.sel == 2)
        return 0;
    MenuMouseNoteChoice(yes_no.sel);    // PC
    n    = yes_no.sel != 0;
    a.a1 = yes_no.uv[n][0];
    a.a2 = yes_no.uv[n][1];
    return B2dDrawItemDefault(&a);
}

// ──────────────────────────────────────────────────────────────────────
// Sprite 0x3f5. FF3 0x0014f158.
static int YesNoDrawCursor1(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS a = *args;
    int           n;

    if (yes_no.sel == 2)
        return 0;
    n    = yes_no.sel == 0;
    a.a1 = yes_no.uv[n][0];
    a.a2 = yes_no.uv[n][1];
    return B2dDrawItemDefault(&a);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f1b0
static void YesNoGetUV(int handle, int id0, int id1)
{
    B2D_ITEM *item;

    item = B2dCtlFindItem(handle, id0);
    yes_no.uv[0][0] = B2dSprGetPartData(item, B2D_PART_U);
    yes_no.uv[0][1] = B2dSprGetPartData(item, B2D_PART_V);
    item = B2dCtlFindItem(handle, id1);
    yes_no.uv[1][0] = B2dSprGetPartData(item, B2D_PART_U);
    yes_no.uv[1][1] = B2dSprGetPartData(item, B2D_PART_V);
}

// Item hooks of types 6/10/11 (data 0x350da0, copied to the stack).
static const B2D_HOOK_REC yes_no_hooks[] =
{
    { YesNoDrawMsg,     0, 0x415, nullptr },
    { YesNoDrawYes,     0, 0x416, nullptr },
    { FUN_0014f058,     0, 0x417, nullptr },
    { FUN_0014f100,     0, 0x3f4, nullptr },
    { YesNoDrawCursor1, 0, 0x3f5, nullptr },
    { nullptr,          0,    -1, nullptr },    // EE: id -1, name word -1
};

// ──────────────────────────────────────────────────────────────────────
// Types 6/10/11 on_open. FF3 0x0014f258 (Ghidra kept FF2's motAniCodeReadMOT).
int YesNoOnOpen(int handle)
{
    B2dSetHookList(handle, yes_no_hooks);
    yes_no.sel       = 2;
    yes_no.close_req = 0;
    YesNoGetUV(handle, 0x3f4, 0x3f5);
    return 0;                           // EE: v0 of the last call; nobody reads it
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f2e8
static int YesNoCheckClose(int handle)
{
    if (yes_no.close_req == 0)
        return 0;
    BpPlayLabel(handle, "out");                                 /* sdata 0x36dac0 */
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Types 6/10/11 on_update. FF3 0x0014f320.
int YesNoOnUpdate(int handle)
{
    return YesNoCheckClose(handle);
}

// ──────────────────────────────────────────────────────────────────────
// Types 6/10/11 on_close. FF3 0x0014f338 (no Ghidra function).
int FUN_0014f338(int handle)
{
    (void)handle;
    yes_no.close_req = 0;
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// The menu window (layout type 8)
// ══════════════════════════════════════════════════════════════════════

// The window's work (bss 0x3918b8).
struct MENU_WDW
{
    /* 0x00 */ int msg0;
    /* 0x04 */ int msg1;
    /* 0x08 */ int input;               // last nonzero PadTrgGetMenuInput
    /* 0x0c */ int keep_open;
    /* 0x10 */ int _10;                 // not used by this TU
    /* 0x14 */ int yes_no;              // the yes/no is shown
    /* 0x18 */ int result;              // MENU_WDW_RESULT_*
    /* 0x1c */ int yes;                 // 1 yes selected, 0 no
    /* 0x20 */ int pos[2][2];           // x, y of "R YES" and "R NO" as the layout placed them
};
static_assert(sizeof(MENU_WDW) == 0x30, "MENU_WDW is 0x30 bytes");
static_assert(offsetof(MENU_WDW, input) == 0x08, "MENU_WDW.input");
static_assert(offsetof(MENU_WDW, keep_open) == 0x0c, "MENU_WDW.keep_open");
static_assert(offsetof(MENU_WDW, yes_no) == 0x14, "MENU_WDW.yes_no");
static_assert(offsetof(MENU_WDW, result) == 0x18, "MENU_WDW.result");
static_assert(offsetof(MENU_WDW, yes) == 0x1c, "MENU_WDW.yes");
static_assert(offsetof(MENU_WDW, pos) == 0x20, "MENU_WDW.pos");

static MENU_WDW menu_wdw;                       /* bss 0x3918b8 */

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f348 (no Ghidra function; nothing calls it)
int FUN_0014f348(void)
{
    return menu_wdw.input;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f358
void MenuWdwSetKeepOpen(int keep)
{
    menu_wdw.keep_open = keep;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f368
int MenuWdwGetResult(void)
{
    return menu_wdw.result;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f378
void MenuWdwSetYesNo(int show, int yes)
{
    menu_wdw.yes_no = show;
    menu_wdw.yes    = yes;
    menu_wdw.result = MENU_WDW_RESULT_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f390
void MenuWdwSetMsg(int msg0, int msg1)
{
    menu_wdw.msg0  = msg0;
    menu_wdw.msg1  = msg1;
    menu_wdw.input = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the message item 0x415 (hook 5). FF3 0x0014f3a8.
static int MenuWdwDrawMsg(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];

    if (alpha != 0 && menu_wdw.msg0 >= 0 && menu_wdw.msg1 >= 0)
    {
        SetCurColorPacked((u_int)args->t3);
        FontExTexSendVram();
        MsgPrintPage(menu_wdw.msg0, menu_wdw.msg1, xy[0], xy[1], 0x2d, alpha, 0, 0, 0, 0);
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// A choice: message (1, 1) "yes", or (1, 2) "no" for the items whose name
// starts with 'n'.
static void MenuWdwDrawChoice(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];
    int        msg1  = B2dGetDrawItem()->name[0] == 'n' ? 2 : 1;

    SetCurColorPacked((u_int)args->t3);
#ifdef REIPAN_SLPS_25544_CHS
    MsgPrintPage(1, msg1, xy[0], xy[1], 0x2d, alpha, 0, 0, 0, 0);
#else
    MsgPrintPage(1, msg1, (xy[0] + xy[4]) / 2, xy[1], 0x2d, alpha, 0, 0, 0, 2);
#endif
    SetDefaultGsDrawState();
}

// ──────────────────────────────────────────────────────────────────────
// Hook of "yes_on" / "no_off": drawn while yes is selected. FF3 0x0014f450.
// callcheck: B2dGetDrawItem, MsgPrintPage, SetCurColorPacked, SetDefaultGsDrawState (in MenuWdwDrawChoice)
static int MenuWdwDrawChoiceYes(const B2D_HOOK_ARGS *args)
{
    if (args->stack[2] != 0 && menu_wdw.yes != 0 && menu_wdw.yes_no != 0)
        MenuWdwDrawChoice(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of "yes_off" / "no_on": drawn while no is selected. FF3 0x0014f528.
// callcheck: B2dGetDrawItem, MsgPrintPage, SetCurColorPacked, SetDefaultGsDrawState (in MenuWdwDrawChoice)
static int MenuWdwDrawChoiceNo(const B2D_HOOK_ARGS *args)
{
    if (args->stack[2] != 0 && menu_wdw.yes == 0 && menu_wdw.yes_no != 0)
        MenuWdwDrawChoice(args);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the cursor sprites "R YES" / "R NO": the sprite as given, only
// while the yes/no is shown. FF3 0x0014f600.
static int MenuWdwDrawCursor(const B2D_HOOK_ARGS *args)
{
    if (menu_wdw.yes_no != 0)
    {
        MenuMouseNoteChoice(menu_wdw.yes != 0 ? 0 : 1);     // PC
        B2dDrawSpriteDirect(args);
    }
    return 0;
}

// Item hooks of type 8 (data 0x350e00, copied to the stack).
static const char menu_wdw_yes_name[] = "R YES";   /* sdata 0x36dac8 */
static const char menu_wdw_no_name[]  = "R NO";    /* sdata 0x36dad0 */

static const B2D_HOOK_REC menu_wdw_hooks[] =
{
    { MenuWdwDrawMsg,       0, 0x415, nullptr },
    { MenuWdwDrawChoiceYes, 0,     0, "yes_on" },               /* sdata 0x36dae0 */
    { MenuWdwDrawChoiceYes, 0,     0, "no_off" },               /* sdata 0x36dae8 */
    { MenuWdwDrawChoiceNo,  0,     0, "yes_off" },              /* sdata 0x36daf0 */
    { MenuWdwDrawChoiceNo,  0,     0, "no_on" },                /* sdata 0x36daf8 */
    { MenuWdwDrawCursor,    0,     0, menu_wdw_yes_name },
    { MenuWdwDrawCursor,    0,     0, menu_wdw_no_name },
    { nullptr,             -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Type 8 on_update. OK / cancel set the result (with the yes/no shown) and
// close unless keep_open; left / right move between yes and no. "R YES"
// stands on its own place while yes is selected, else on "R NO"'s, and
// "R NO" the other way round. FF3 0x0014f650.
// callcheck: B2dPlaceGetXYByName, B2dSetHookList (Ghidra's body runs on into the on_open FUN_0014f798)
// callcheck: B2dPlaceInit (B2D_PLACE's constructor)
int MenuWdwOnUpdate(int handle)
{
    int        input = PadTrgGetMenuInput();
    int        yes;

    if (input != 0)
        menu_wdw.input = input;

    switch (input)
    {
    case 1:                             // OK
        if (menu_wdw.yes_no != 0)
            menu_wdw.result = menu_wdw.yes != 0 ? MENU_WDW_RESULT_YES : MENU_WDW_RESULT_NO;
        break;
    case 2:                             // cancel
        if (menu_wdw.yes_no != 0)
            menu_wdw.result = MENU_WDW_RESULT_CANCEL;
        break;
    default:
        if ((input == 3 || input == 4) && menu_wdw.yes_no != 0)    // left, right
        {
            SndSePlay(0x1a9b0000, 100, 0x1000, 0, 0, 0);
            menu_wdw.yes ^= 1;
        }
        yes = menu_wdw.yes;
        B2D_PLACE place(handle);
        place.SetXYByName(menu_wdw_yes_name, menu_wdw.pos[yes ^ 1][0], menu_wdw.pos[yes ^ 1][1]);
        place.SetXYByName(menu_wdw_no_name, menu_wdw.pos[yes][0], menu_wdw.pos[yes][1]);
        return 0;
    }

    if (menu_wdw.keep_open != 0)
        return 0;
    BpPlayLabel(handle, "out");                                 /* sdata 0x36dad8 */
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Type 8 on_open. FF3 0x0014f798 (no Ghidra function: it follows
// MenuWdwOnUpdate).
// callcheck: B2dPlaceInit (B2D_PLACE's constructor)
int FUN_0014f798(int handle)
{
    B2dSetHookList(handle, menu_wdw_hooks);
    B2D_PLACE place(handle);
    place.GetXYByName(menu_wdw_yes_name, menu_wdw.pos[0]);
    place.GetXYByName(menu_wdw_no_name, menu_wdw.pos[1]);
    menu_wdw.result = MENU_WDW_RESULT_NONE;
    menu_wdw.input  = 0;
    return 0;                           // EE: v0 of the last call; nobody reads it
}

// FF3 0x14f848 is an empty function (jr ra) that nothing references.

// ──────────────────────────────────────────────────────────────────────
// Type 8 on_close. FF3 0x0014f850 (no Ghidra function).
int FUN_0014f850(int handle)
{
    (void)handle;
    menu_wdw.keep_open = 0;
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// The pause menu (layout type 4)
// ══════════════════════════════════════════════════════════════════════

#define PAUSE_MENU_POS_MAX  8

typedef int (*PAUSE_MENU_STEP)(int handle);

// The menu's work (bss 0x3918e8).
// layout: host -- step is a pointer (EE 0x50 bytes).
struct PAUSE_MENU
{
    /* 0x00 */ int             sel;     // 0 continue, 1 options, 2 quit
    /* 0x04 */ int             pos[PAUSE_MENU_POS_MAX][2];  // x, y of the choice items
    /* 0x44 */ int             num;     // choices
    /* 0x48 */ int             sub;     // the quit question's layout slot, or the pad error's
    /* 0x4c */ PAUSE_MENU_STEP step;    // run by PauseMenuOnUpdate
};
static_assert(offsetof(PAUSE_MENU, pos) == 0x04, "PAUSE_MENU.pos");
static_assert(offsetof(PAUSE_MENU, num) == 0x44, "PAUSE_MENU.num");
static_assert(offsetof(PAUSE_MENU, sub) == 0x48, "PAUSE_MENU.sub");

static PAUSE_MENU pause_menu;                   /* bss 0x3918e8 */

// A choice item and its text (data 0x350e80).
struct PAUSE_MENU_CHOICE
{
    /* 0x00 */ int id;
    /* 0x04 */ int msg0;
    /* 0x08 */ int msg1;
};
static_assert(sizeof(PAUSE_MENU_CHOICE) == 0xc, "PAUSE_MENU_CHOICE is 0xc bytes");

static const PAUSE_MENU_CHOICE pause_menu_choice[3] =
{
    { 0x41b, 0x66, 0 },                 // continue
    { 0x3f0, 0x66, 1 },                 // options
    { 0x3f1, 0x66, 2 },                 // quit (4 when title_start_flg.mode is 2)
};

static int PauseMenuStepSelect(int handle);
static int FUN_0014fbc8(int handle);
static int FUN_0014fca0(int handle);

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f860
int PauseMenuGetSel(void)
{
    return pause_menu.sel;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014f870
void PauseMenuSetSel(int sel)
{
    pause_menu.sel = sel;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the cursor item 0x3ee: placed on the selected choice (relative
// to the first one, where the layout put it). FF3 0x0014f880.
static int PauseMenuDrawCursor(const B2D_HOOK_ARGS *args)
{
    (void)args;
    B2dSprSetPos(B2dGetDrawItem(),
                 pause_menu.pos[pause_menu.sel][0] - pause_menu.pos[0][0],
                 pause_menu.pos[pause_menu.sel][1] - pause_menu.pos[0][1]);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the choice items (hook 5), aligned by the item name's suffix.
// FF3 0x0014f8d8.
static int PauseMenuDrawChoice(const B2D_HOOK_ARGS *args)
{
    const int        *xy    = (const int *)args->t1;
    int               alpha = (int)args->stack[2];
    B2D_ITEM         *item  = B2dGetDrawItem();
    PAUSE_MENU_CHOICE choice[3] = { pause_menu_choice[0], pause_menu_choice[1], pause_menu_choice[2] };
    int               i;
    int               type;

    if (title_start_flg.mode == 2)
        choice[2].msg1 = 4;

    // Every hooked item is in the table (FF3 reads past it otherwise).
    for (i = 0; i < 3; i++)
    {
        if (choice[i].id == item->id)
            break;
    }

    SetCurColorPacked(i == pause_menu.sel ? 0x97cdd380 : 0x47797480);
    type = GetLabelSuffixType(item->name);
    MsgPrintAt(choice[i].msg0, choice[i].msg1, LabelAlignGetX(type, xy), xy[1], alpha,
               item->part[B2D_PART_TEXT].value, 0, 0, type);
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The step while choosing. A lost or digital pad opens the pad error
// message; OK on quit asks again (layout message type 5); any other OK,
// cancel or the pause key play "out" (cancel and pause first select
// continue). Up / down wrap. FF3 0x0014fa28.
// callcheck: Anim2dMsgGetCsr, CmnFadeLayerShow, GetCmnFadeLayer, LayoutAnimGetState, LayoutAnimIsStateChanged, PadErrMsgIsClosed, PadErrMsgRelease
//   (Ghidra's body runs on into the steps FUN_0014fbc8 / FUN_0014fca0)
static int PauseMenuStepSelect(int handle)
{
    if (pad_wrk[0].connected == 0 || !PadIsPressMode(0))
    {
        pause_menu.step = FUN_0014fca0;
        pause_menu.sub  = PadErrMsgOpen();
        return 0;
    }

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], 0x1a9b0003, 0))
    {
        if (pause_menu.sel == 2)
        {
            pause_menu.step = FUN_0014fbc8;
            pause_menu.sub  = LayoutAnimStart(5, 0);
            Anim2dMsgSet(0x66, title_start_flg.mode == 2 ? 5 : 3);
            Anim2dMsgSetType(1);
            return 0;
        }
    }
    else if (CheckPadRptPlaySe(0x1000, 0x1a9b0000, 0))         // up
    {
        pause_menu.sel--;
        if (pause_menu.sel < 0)
            pause_menu.sel = pause_menu.num - 1;
        return 0;
    }
    else if (CheckPadRptPlaySe(0x4000, 0x1a9b0000, 0))         // down
    {
        pause_menu.sel++;
        if (pause_menu.sel >= pause_menu.num)
            pause_menu.sel = 0;
        return 0;
    }
    else if (CheckPadTrgPlaySe((u_short)(pad_key[PAD_KEY_CANCEL] | pad_key[PAD_KEY_PAUSE]), 0x1a9b0001, 0))
    {
        pause_menu.sel = 0;
    }
    else
    {
        return 0;
    }

    BpPlayLabel(handle, "out");                                 /* sdata 0x36db00 */
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The step while the quit question is up: once it has played out, cursor 0
// closes the menu; 1 or 3 go back to choosing. Cursor 0 on the closing
// question already plays "out". FF3 0x0014fbc8 (no Ghidra function: it
// follows PauseMenuStepSelect).
static int FUN_0014fbc8(int handle)
{
    if (!LayoutAnimIsStateChanged(pause_menu.sub))
        return 0;

    switch (LayoutAnimGetState(pause_menu.sub))
    {
    case 3:                             // closing
        if (Anim2dMsgGetCsr() == 0)
            BpPlayLabel(handle, "out");                         /* sdata 0x36db00 */
        return 0;
    case 4:                             // closed
        switch (Anim2dMsgGetCsr())
        {
        case 0:
            return 1;
        case 1:
        case 3:
            pause_menu.step = PauseMenuStepSelect;
            return 0;
        }
        return 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The step while the pad error message is up. FF3 0x0014fca0 (no Ghidra
// function).
static int FUN_0014fca0(int handle)
{
    (void)handle;
    if (PadErrMsgIsClosed(pause_menu.sub))
    {
        pause_menu.step = PauseMenuStepSelect;
        PadErrMsgRelease(pause_menu.sub);
        GetCmnFadeLayer()->Show(0xf);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Type 4 on_update. FF3 0x0014fd00.
int PauseMenuOnUpdate(int handle)
{
    return pause_menu.step(handle);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014fd28
static void PauseMenuGetItemPos(int handle, const int *ids, int n)
{
    pause_menu.num = n;
    for (int i = 0; i < n; i++)
    {
        B2D_ITEM *item = B2dCtlFindItem(handle, ids[i]);

        pause_menu.pos[i][0] = B2dSprGetPartData(item, B2D_PART_X);
        pause_menu.pos[i][1] = B2dSprGetPartData(item, B2D_PART_Y);
    }
}

// Item hooks of type 4 (data 0x350ea8, copied to the stack).
static const B2D_HOOK_REC pause_menu_hooks[] =
{
    { PauseMenuDrawCursor, 0, 0x3ee, nullptr },
    { PauseMenuDrawChoice, 0, 0x3f1, nullptr },
    { PauseMenuDrawChoice, 0, 0x3f0, nullptr },
    { PauseMenuDrawChoice, 0, 0x41b, nullptr },
    { nullptr,            -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Type 4 on_open: the choice positions, the cursor on the current choice,
// and the common fade layer behind it. FF3 0x0014fdc8.
int PauseMenuOnOpen(int handle)
{
    static const int ids[3] = { 0x41b, 0x3f0, 0x3f1 };     /* data 0x350ef8 */
    B2D_ITEM *item;

    B2dSetHookList(handle, pause_menu_hooks);
    B2dSprFreezePart(handle, 0x3ee, B2D_PART_X, 0);
    B2dSprFreezePart(handle, 0x3ee, B2D_PART_Y, 0);
    pause_menu.sub  = -1;
    pause_menu.step = PauseMenuStepSelect;
    PauseMenuGetItemPos(handle, ids, 3);

    item = B2dCtlFindItem(handle, 0x3ee);
    B2dSprSetPos(item, pause_menu.pos[pause_menu.sel][0] - pause_menu.pos[0][0],
                 pause_menu.pos[pause_menu.sel][1] - pause_menu.pos[0][1]);
    GetCmnFadeLayer()->SetItemHandle(item);
    GetCmnFadeLayer()->Show(0xf);
    return 0;                           // EE: v0 of the last call; nobody reads it
}

// ──────────────────────────────────────────────────────────────────────
// Type 4 on_close. FF3 0x0014ff18.
int PauseMenuOnClose(int handle)
{
    (void)handle;
    GetCmnFadeLayer()->SetItemHandle(nullptr);
    GetCmnFadeLayer()->Term();
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// The slot select (layout type 7)
// ══════════════════════════════════════════════════════════════════════

#define SLOT_SEL_POS_MAX    8

// The menu's work (bss 0x391938).
struct SLOT_SEL
{
    /* 0x00 */ int sel;
    /* 0x04 */ int pos[SLOT_SEL_POS_MAX][2];    // x, y of the choice items
    /* 0x44 */ int num;
    /* 0x48 */ int close_req;
};
static_assert(sizeof(SLOT_SEL) == 0x4c, "SLOT_SEL is 0x4c bytes");
static_assert(offsetof(SLOT_SEL, num) == 0x44, "SLOT_SEL.num");
static_assert(offsetof(SLOT_SEL, close_req) == 0x48, "SLOT_SEL.close_req");

static SLOT_SEL slot_sel;                       /* bss 0x391938 */
static int slot_sel_handle = -1;

// The choice items and their texts (data 0x350f48), up to id -1.
static const PAUSE_MENU_CHOICE slot_sel_choice[] =
{
    { 0x3e9, 0x59,  9 },
    { 0x3ed, 0x59, 10 },
    {    -1,   -1, -1 },
};

void SlotSelMouse(int sel)
{
    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_VERTICAL, nullptr);
    for (int i = 0; slot_sel_handle >= 0 && slot_sel_choice[i].id >= 0; i++)
    {
        ReiPan_MenuBox box;
        if (ReiPan_MenuMouseItemBox(slot_sel_handle, slot_sel_choice[i].id, &box))
            ReiPan_MenuMouseSeekBox(sel, i, &box, REIPAN_MENU_UP, REIPAN_MENU_DOWN, REIPAN_MENU_CROSS);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ff58
void SlotSelSetSel(int sel)
{
    if (sel >= 0 && sel < slot_sel.num)
        slot_sel.sel = sel;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ff68
int SlotSelIsSel(int sel)
{
    return slot_sel.sel == sel;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ff80
void SlotSelReqClose(int req)
{
    slot_sel.close_req = req;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0014ff90
static void SlotSelGetItemPos(int handle, const int *ids, int n)
{
    slot_sel.num = 0;
    for (int i = 0; i < n; i++)
    {
        B2D_ITEM *item = B2dCtlFindItem(handle, ids[i]);
        if (item == nullptr)
            return;

        slot_sel.pos[i][0] = B2dSprGetPartData(item, B2D_PART_X);
        slot_sel.pos[i][1] = B2dSprGetPartData(item, B2D_PART_Y);
    }
    slot_sel.num = n;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the cursor item 0x3ee: on the selected choice, 10 lower.
// FF3 0x00150030.
static int SlotSelDrawCursor(const B2D_HOOK_ARGS *args)
{
    (void)args;
    if (slot_sel.num == 0)
        return 0;
    B2dSprSetPos(B2dGetDrawItem(),
                 slot_sel.pos[slot_sel.sel][0] - slot_sel.pos[0][0],
                 slot_sel.pos[slot_sel.sel][1] - slot_sel.pos[0][1] + 10);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Hook of the choice items (hook 5), coloured as the pause menu's.
// FF3 0x00150088.
// callcheck: B2dSetHookList, B2dSprFreezePart, SlotSelGetItemPos (Ghidra's body runs on into the on_open FUN_00150190)
static int SlotSelDrawChoice(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];
    B2D_ITEM  *item  = B2dGetDrawItem();
    if (alpha == 0)
        return 0;
    int i;
    for (i = 0; slot_sel_choice[i].id >= 0; i++)
    {
        if (slot_sel_choice[i].id == item->id)
            break;
    }
    if (slot_sel_choice[i].id < 0)
        return 0;

    int type = GetLabelSuffixType(item->name);
    int x = LabelAlignGetX(type, xy);
    SetCurColorPacked(SlotSelIsSel(i) ? 0x97cdd380 : 0x47797480);
    MsgPrintAt(slot_sel_choice[i].msg0, slot_sel_choice[i].msg1, x, xy[1], alpha,
               item->part[B2D_PART_TEXT].value, 0, 0, type);
    SetDefaultGsDrawState();
    return 0;
}

#ifdef REIPAN_SLPS_25544_CHS
static int SlotSelDrawTitle(const B2D_HOOK_ARGS *args)
{
    // The right half of the old title begins at the centre of the panel.
    // Glyphs from SLPS_255.44 messages 0x83/0x2a and 0x59/9.
    static u_char title[] = {0xf3, 0x4e, 0xf3, 0x4f, 0x8d, 0xf0, 0xb5, 0xf3, 0x4a, MSG_CODE_END};
    if (B2dGetDrawItem()->id == 0x3ec && args->stack[1] > 0)
    {
        SetCurColorRGB(0xb0, 0xd6, 0xd0);
        PrintMsg_ArrangeOneLine(title, (int)args->t1, (int)args->t2 + 3,
                                MSG_COL_CURRENT, (int)args->stack[1], 4, 0, 0, 2);
        SetDefaultGsDrawState();
    }
    return 0;
}
#endif

// Item hooks of type 7 (data 0x350f08, copied to the stack).
static const B2D_HOOK_REC slot_sel_hooks[] =
{
    { SlotSelDrawCursor, 0, 0x3ee, nullptr },
    { SlotSelDrawChoice, 0, 0x3e9, nullptr },
    { SlotSelDrawChoice, 0, 0x3ed, nullptr },
#ifdef REIPAN_SLPS_25544_CHS
    { SlotSelDrawTitle,  0, 0x3ec, nullptr },
    { SlotSelDrawTitle,  0, 0x3f2, nullptr },
#endif
    { nullptr,          -1,    -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Type 7 on_open. FF3 0x00150190 (no Ghidra function: it follows
// SlotSelDrawChoice).
int FUN_00150190(int handle)
{
    static const int ids[2] = { 0x3e9, 0x3ed };             /* sdata 0x36db08 */

    B2dSetHookList(handle, slot_sel_hooks);
    B2dSprFreezePart(handle, 0x3ee, B2D_PART_X, 0);
    B2dSprFreezePart(handle, 0x3ee, B2D_PART_Y, 0);
    slot_sel.sel       = 0;
    slot_sel.close_req = 0;
    SlotSelGetItemPos(handle, ids, 2);
    slot_sel_handle = handle;
    return 0;                           // EE: v0 of the last call; nobody reads it
}

// ──────────────────────────────────────────────────────────────────────
// Type 7 on_update. FF3 0x00150258.
int SlotSelOnUpdate(int handle)
{
    if (slot_sel.close_req == 0)
        return 0;
    BpPlayLabel(handle, "out");                                 /* sdata 0x36db10 */
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Type 7 on_close. FF3 0x00150290 (no Ghidra function).
int FUN_00150290(int handle)
{
    if (slot_sel_handle == handle)
        slot_sel_handle = -1;
    slot_sel.close_req = 0;
    return 1;
}
