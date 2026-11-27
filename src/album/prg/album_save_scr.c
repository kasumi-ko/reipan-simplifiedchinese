/* ==========================================================================
 *  album/prg/album_save_scr.c
 *
 *  The album save screen (see album_save_scr.h). FF3 0x12f470..0x130210,
 *  FF3's own code (FF2's album save drew its pages from album_save.c); it
 *  sits between the album zoom page (0x12e988..) and autoload_scr.c.
 * ======================================================================== */
#include "album/prg/album_save_scr.h"

#include <new>                          // operator new / delete
#include <string.h>                     // strlen

#include "album/prg/album_save.h"       // album_save_ctrl, ALBUM_SAVE_*
#include "album/prg/album_view.h"       // album_layout_obj, ALBUM_LAYOUT_SENTAKU
#include "graphics/graph2d/message.h"   // SetCurColorRGB
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, YesNo*, SlotSel*, SetCurColorPacked, SetDefaultGsDrawState
#include "ingame/menu/anim_2d.h"        // LayoutAnim*
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "album/prg/album.h"             // AlbumSlotInfoGet*
#include "outgame/save_info_disp.h"     // AlbumInfoPrint*, AlbumViewPhotoInfoDisp
#include "system/b2d/B2dCtl.h"          // B2dGetDrawItem, B2D_PART_*
#include "system/b2d/B2dSpr.h"          // B2dSprGetPartData
#include "system/b2d/BpCtl.h"           // BpCtlGetHandleByFileNo
#include "system/b2d/BpObj.h"           // BpObj*
#include "system/eeiop/cd_file_dat.h"   // ALBM_SENTAKU_PHF
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

#define ASS_ANIM_SLOT_SEL   7           // layout anim type of the slot select window
#define ASS_ANIM_YESNO      0xb         // layout anim type of the yes/no window
#define ASS_MSG_KIND        0x83        // message kind of album_save_ctrl.msg_id
#define ASS_YESNO_NONE      2           // YesNoSetSel: no choices shown
#define ASS_LA_OPEN         2           // LAYOUT_ANIM.state
#define ASS_LA_CLOSING      3
#define ASS_LA_CLOSED       4
#define ASS_LIST_STEP       0x28        // y distance between two album rows
#define ASS_COVER_PIC       0x82d       // MENU_PIC picture of album kind 0

// Layout items of ALBM_SENTAKU_PHF (the names are the ones their hooks imply).
enum
{
    ASS_ITEM_CSR       = 0x405,         // the list cursor ("csl")
    ASS_ITEM_LIST      = 0x408,         // the five album rows ("list")
    ASS_ITEM_CARD_SLOT = 0x40d,         // "slot_M"
    ASS_ITEM_PHOTO_NUM = 0x40e,         // photo count ("pht")
    ASS_ITEM_DATE      = 0x40f,         // "date"
    ASS_ITEM_ICON_A    = 0x412,         // the chosen album's cover, "albmicon_a".."_c"
    ASS_ITEM_ICON_B    = 0x413,
    ASS_ITEM_ICON_C    = 0x414,
};

static const char ass_label_in[]  = "in";      // sdata 0x36ba80
static const char ass_label_out[] = "out";     // sdata 0x36ba88

static u_char        album_save_scr_alpha;      // sbss 0x372ce4 (gp-0x80c): album list alpha 0..0x80
static MENU_PIC *album_save_scr_pic;        // sbss 0x372ce8 (gp-0x808): the cover picture
static int           album_save_scr_slot_anim;  // sbss 0x372cec (gp-0x804): slot window slot, -1 none
static int           album_save_scr_msg_anim;   // sbss 0x372cf0 (gp-0x800): yes/no window slot, -1 none
static int           album_save_scr_ask;        // sbss 0x372cf4 (gp-0x7fc): 1 while the choices show (no reader)
static int           album_save_scr_slot_keep;  // sdata 0x36ba78 (gp-0x7a78): the slot window stays up

static int AlbumSaveScrHandle(void)
{
    return BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + ALBM_SENTAKU_PHF);
}

static BP_OBJ *AlbumSaveScrObj(void)
{
    return album_layout_obj[ALBUM_LAYOUT_SENTAKU];
}

// The text item value the info hooks pass on (EE item +0x124).
static int AlbumSaveScrDrawPri(void)
{
    return B2dGetDrawItem()->part[B2D_PART_TEXT].value;
}

// ──────────────────────────────────────────────────────────────────────
// The album list
// ──────────────────────────────────────────────────────────────────────

// The list fades in (0x20 a frame) on the album select steps (6 / 7) of the
// save mode and out in slot select mode or once the screen is leaving
// (step 3); in the save mode's other steps it stays as it is. 0 until the
// card's albums have been looked at (dir_check_cnt).
// FF3 0x0012f470.
static void AlbumSaveScrListFade(void)
{
    if (album_save_ctrl.dir_check_cnt != 0)
    {
        if (album_save_ctrl.mode != ALBUM_SAVE_MODE_SLOT_SEL && album_save_ctrl.step != ALBUM_SAVE_OUT)
        {
            if ((u_int)((u_char)album_save_ctrl.mc_step - ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT) < 2)
            {
                if (album_save_scr_alpha + 0x20 < 0x80)
                {
                    album_save_scr_alpha += 0x20;
                    return;
                }
                album_save_scr_alpha = 0x80;
            }
            return;
        }
        if (album_save_scr_alpha - 0x20 > 0)
        {
            album_save_scr_alpha -= 0x20;
            return;
        }
    }
    album_save_scr_alpha = 0;
}

// alpha scaled by the list's fade (float, 1/128 at 0x36ba70); 0 until the
// card's albums have been looked at.
// FF3 0x0012f4f8.
static int AlbumSaveScrListAlpha(int alpha)
{
    if (album_save_ctrl.dir_check_cnt == 0)
        return 0;
    return (int)((float)(album_save_scr_alpha * alpha) * (1.0f / 128.0f));
}

// FF3 0x0012f538.
static int AlbumSaveScrListIsShown(void)
{
    return album_save_scr_alpha == 0x80;
}

// Item 0x408: the card's five albums, one row each 0x28 apart: "Album n"
// and its name (kind -1 for no album, -2 for a broken one), the cursor's
// row in a warm yellow.
// FF3 0x0012f548.
// callcheck: B2dGetDrawItem (AlbumSaveScrDrawPri)
static int AlbumSaveScrDrawList(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumSaveScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumSaveScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
    {
        int y = 0;

        for (s_char i = 0; i < ALBUM_SAVE_ALBUM_MAX; i++, y += ASS_LIST_STEP)
        {
            s_char kind = -1;

            if (album_save_ctrl.album_flg[i] != 0)
            {
                kind = -2;
                if (album_save_ctrl.album_flg[i] != -1)
                    kind = AlbumSlotInfoGetKind(i);
            }
            SetCurColorRGB(0xd4, 0xd4, 0xca);
            if (i == album_save_ctrl.album_sel_csr)
                SetCurColorRGB(0xf9, 0xdf, 0x91);
            AlbumInfoPrintAlbumNo(i, kind, xy[0], xy[1] + y, (int)args->a1, alpha, pri);
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_save_scr_list_hooks[] =     // built on the stack by FF3
{
    { AlbumSaveScrDrawList, 0, ASS_ITEM_LIST, nullptr },
    { nullptr,             -1, -1,            nullptr },
};

// FF3 0x0012f680.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (AlbumSaveScrHandle)
static void AlbumSaveScrSetListHook(void)
{
    B2dSetHookList(AlbumSaveScrHandle(), album_save_scr_list_hooks);
}

// FF3 0x0012f710.
int AlbumSaveScrListIsIn(void)
{
    return AlbumSaveScrListIsShown();
}

// ──────────────────────────────────────────────────────────────────────
// The chosen album's cover
// ──────────────────────────────────────────────────────────────────────

// Items "albmicon_a".."_c": the chosen album's cover picture (its kind
// + 0x82d) through the picture view, sized by the item's w / h parts, its
// fade from part 10's alpha. The name's last letter picks the picture's
// offset (ROM 0x34ebf0).
// FF3 0x0012f728 (no Ghidra function before 2026-10-14).
// callcheck: strlen (strlen)
static int AlbumSaveScrDrawIcon(const B2D_HOOK_ARGS *args)
{
    static const int icon_ofs[3][2] = { { 1, 1 }, { 0xf, 0xb2 }, { 1, 0xb2 } };

    const int  *xy   = (const int *)args->t1;
    B2D_ITEM   *item = B2dGetDrawItem();
    const char *name = item->name;
    int         icon = name[strlen(name) - 1] - 'a';
    int         w    = B2dSprGetPartData(item, B2D_PART_W);
    int         h    = B2dSprGetPartData(item, B2D_PART_H);
    int         a    = B2dSprGetPartData(item, B2D_PART_ALPHA);
    short       p11  = (short)B2dSprGetPartData(item, B2D_PART_TEXT);

    if (album_save_scr_pic == nullptr)
        return 0;

    int alpha = AlbumSaveScrListAlpha(a);
    if (alpha > 0)
    {
        MENU_PIC *pic = album_save_scr_pic;

        pic->pic = AlbumSlotInfoGetKind(album_save_ctrl.album_sel_csr) + ASS_COVER_PIC;
        pic->Step();
        pic->u = icon_ofs[icon][0];
        pic->v = icon_ofs[icon][1];
        pic->w    = w;
        pic->h    = h;
        pic->pri = p11;
        pic->x    = (short)xy[0];      // FF3 reads the low halves (lh)
        pic->y    = (short)xy[1];
        pic->z = 0;
        pic->scale = (float)alpha * (1.0f / 128.0f);    // 0x36ba74
        pic->Draw();
        pic->DrawNext();
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_save_scr_icon_hooks[] =     // ROM 0x34ec08
{
    { AlbumSaveScrDrawIcon, 0, ASS_ITEM_ICON_A, nullptr },
    { AlbumSaveScrDrawIcon, 0, ASS_ITEM_ICON_B, nullptr },
    { AlbumSaveScrDrawIcon, 0, ASS_ITEM_ICON_C, nullptr },
    { nullptr,             -1, -1,              nullptr },
};

// FF3 copies the ROM list to the stack first.
// FF3 0x0012f8d0.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk, operator_new (AlbumSaveScrHandle, new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)
static void AlbumSaveScrIconCreate(int pic_mode)
{
    B2dSetHookList(AlbumSaveScrHandle(), album_save_scr_icon_hooks);
    album_save_scr_pic = new MENU_PIC(pic_mode);
    album_save_scr_pic->enable = 1;
}

// FF3 0x0012f988.
// callcheck: operator_delete (delete)
// callcheck: MenuPicDtor (MENU_PIC's destructor)
static void AlbumSaveScrIconRelease(void)
{
    MENU_PIC *pic = album_save_scr_pic;

    if (pic != nullptr)
    {
        delete pic;
        album_save_scr_pic = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The slot select window
// ──────────────────────────────────────────────────────────────────────

// While the card machine runs: on in slot select mode, and once on it
// stays up until the album select steps (6 / 7) begin.
// FF3 0x0012f9c8.
static int AlbumSaveScrIsSlotSelStep(void)
{
    if (album_save_ctrl.step != ALBUM_SAVE_MC_EXE)
        return 0;
    if (album_save_ctrl.mode == ALBUM_SAVE_MODE_SLOT_SEL)
    {
        album_save_scr_slot_keep = 1;
        return 1;
    }
    int keep = album_save_scr_slot_keep;
    if (keep != 0)
    {
        if ((u_int)(album_save_ctrl.mc_step - ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT) < 2)
            keep = 0;
        album_save_scr_slot_keep = keep;
    }
    return keep;
}

// Starts the window again once it has closed (while wanted) and keeps its
// cursor on album_save_ctrl's slot; asks it to close when not wanted.
// FF3 0x0012fa20.
static void AlbumSaveScrSlotSelUpdate(void)
{
    if (album_save_scr_slot_anim != -1 && LayoutAnimGetState(album_save_scr_slot_anim) == ASS_LA_CLOSED)
    {
        LayoutAnimReset(album_save_scr_slot_anim);
        album_save_scr_slot_anim = -1;
    }
    if (album_save_scr_slot_anim == -1 && AlbumSaveScrIsSlotSelStep())
        album_save_scr_slot_anim = LayoutAnimStart(ASS_ANIM_SLOT_SEL, 0);
    if (album_save_scr_slot_anim == -1)
        return;

    if (AlbumSaveScrIsSlotSelStep())
        SlotSelSetSel(album_save_ctrl.slot_csr);
    else
        SlotSelReqClose(1);
}

// FF3 0x0012fad0.
static void AlbumSaveScrSlotSelInit(void)
{
    album_save_scr_slot_anim = -1;
    AlbumSaveScrSlotSelUpdate();
}

// FF3 0x0012faf0.
static void AlbumSaveScrSlotSelClose(void)
{
    SlotSelReqClose(1);
}

// 0 while the window plays its close.
// FF3 0x0012fb08.
static int AlbumSaveScrSlotSelIsNotClosing(void)
{
    if (album_save_scr_slot_anim != -1 && LayoutAnimGetState(album_save_scr_slot_anim) == ASS_LA_CLOSING)
        return 0;
    return 1;
}

// FF3 0x0012fb48.
static void AlbumSaveScrSlotSelReset(void)
{
    if (album_save_scr_slot_anim != -1)
    {
        LayoutAnimReset(album_save_scr_slot_anim);
        album_save_scr_slot_anim = -1;
    }
}

// FF3 0x0012fb80.
int AlbumSaveScrSlotSelIsOpen(void)
{
    if (album_save_scr_slot_anim != -1 && LayoutAnimGetState(album_save_scr_slot_anim) != ASS_LA_OPEN)
        return 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The yes/no window
// ──────────────────────────────────────────────────────────────────────

// The save mode's four confirm steps (save, remake, new make, format).
// FF3 0x0012fbc0.
static int AlbumSaveScrIsAskStep(void)
{
    if (album_save_ctrl.mode != ALBUM_SAVE_MODE_MC_SAVE)
        return 0;
    switch (album_save_ctrl.mc_step)
    {
    case ALBUM_SAVE_MC_SAVE_CONF_INIT:
    case ALBUM_SAVE_MC_SAVE_CONF_WAIT:
    case ALBUM_SAVE_MC_REMAKE_CONF_INIT:
    case ALBUM_SAVE_MC_REMAKE_CONF_WAIT:
    case ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT:
    case ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT:
    case ALBUM_SAVE_MC_FORMAT_CONF_INIT:
    case ALBUM_SAVE_MC_FORMAT_CONF_WAIT:
        return 1;
    }
    return 0;
}

// Opens the window once the slot window is up (and again after it has
// closed) with album_save_ctrl's message; the choices only in the confirm
// steps.
// FF3 0x0012fc18.
static void AlbumSaveScrMsgUpdate(void)
{
    if (album_save_scr_msg_anim != -1 && LayoutAnimGetState(album_save_scr_msg_anim) == ASS_LA_CLOSED)
    {
        LayoutAnimReset(album_save_scr_msg_anim);
        album_save_scr_msg_anim = -1;
    }
    if (album_save_scr_msg_anim == -1 && album_save_scr_slot_anim != -1)
        album_save_scr_msg_anim = LayoutAnimStart(ASS_ANIM_YESNO, 0);
    if (album_save_scr_msg_anim == -1)
        return;

    if (AlbumSaveScrIsAskStep())
    {
        if (album_save_scr_ask == 0)
            album_save_scr_ask = 1;
        YesNoSetMsg(ASS_MSG_KIND, album_save_ctrl.msg_id);
        YesNoSetSel((u_char)album_save_ctrl.conf_csr);
    }
    else
    {
        if (album_save_scr_ask == 1)
            album_save_scr_ask = 0;
        YesNoSetMsg(ASS_MSG_KIND, album_save_ctrl.msg_id);
        YesNoSetSel(ASS_YESNO_NONE);
    }
}

// FF3 0x0012fcf8.
static void AlbumSaveScrMsgInit(void)
{
    album_save_scr_msg_anim = -1;
    album_save_scr_ask = AlbumSaveScrIsAskStep() != 0;
    AlbumSaveScrMsgUpdate();
}

// FF3 0x0012fd30.
static void AlbumSaveScrMsgClose(void)
{
    YesNoReqClose(1);
}

// 0 while the window plays its close.
// FF3 0x0012fd48.
static int AlbumSaveScrMsgIsNotClosing(void)
{
    if (album_save_scr_msg_anim != -1 && LayoutAnimGetState(album_save_scr_msg_anim) == ASS_LA_CLOSING)
        return 0;
    return 1;
}

// FF3 0x0012fd88.
static void AlbumSaveScrMsgReset(void)
{
    if (album_save_scr_msg_anim != -1)
    {
        LayoutAnimReset(album_save_scr_msg_anim);
        album_save_scr_msg_anim = -1;
    }
}

// FF3 0x0012fdc0.
int AlbumSaveScrMsgIsOpen(void)
{
    if (album_save_scr_msg_anim != -1 && LayoutAnimGetState(album_save_scr_msg_anim) != ASS_LA_OPEN)
        return 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The chosen album's info (faded with the list)
// ──────────────────────────────────────────────────────────────────────

// Item "slot_M": MEMORY CARD slot n, centred on the item.
// FF3 0x0012fe00.
// callcheck: B2dGetDrawItem (AlbumSaveScrDrawPri)
static int AlbumSaveScrDrawCardSlot(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumSaveScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumSaveScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumInfoPrintCardSlot(album_save_ctrl.slot_csr, (xy[0] + xy[6]) / 2, xy[1], (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x40e: the chosen album's photo count "nn/16".
// FF3 0x0012fea0.
// callcheck: B2dGetDrawItem (AlbumSaveScrDrawPri)
static int AlbumSaveScrDrawPhotoNum(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumSaveScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumSaveScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumInfoPrintPhotoNum(AlbumSlotInfoGetPhotoNum(album_save_ctrl.album_sel_csr), xy[0], xy[1],
                               (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

// Item "date": the chosen album's date.
// FF3 0x0012ff38.
// callcheck: B2dGetDrawItem (AlbumSaveScrDrawPri)
static int AlbumSaveScrDrawDate(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumSaveScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumSaveScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumViewPhotoInfoDisp(AlbumSlotInfoGet(album_save_ctrl.album_sel_csr), xy[0], xy[1],
                               (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_save_scr_info_hooks[] =     // ROM 0x34ec90
{
    { AlbumSaveScrDrawCardSlot, 0, ASS_ITEM_CARD_SLOT, nullptr },
    { AlbumSaveScrDrawPhotoNum, 0, ASS_ITEM_PHOTO_NUM, nullptr },
    { AlbumSaveScrDrawDate,     0, ASS_ITEM_DATE,      nullptr },
#ifdef REIPAN_SLPS_25544_CHS
    { AlbumInfoDrawPhotoLabel,  0, 0,                 "R pht" },
#endif
    { nullptr,                 -1, -1,                 nullptr },
};

// FF3 copies the ROM list to the stack first.
// FF3 0x0012ffd0.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (AlbumSaveScrHandle)
static void AlbumSaveScrSetInfoHooks(void)
{
    B2dSetHookList(AlbumSaveScrHandle(), album_save_scr_info_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 27's sequence
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00130050.
void AlbumSaveScrInit(void)
{
    album_save_scr_alpha = 0;
}

// FF3 0x00130068.
int AlbumSaveScrIn(int pic_mode)
{
    LayoutAnimMain();
    AlbumSaveScrObj()->In(ass_label_in);
    AlbumSaveScrObj()->AddCsr(ASS_ITEM_CSR, 0, ASS_LIST_STEP, 0);
    AlbumSaveScrSlotSelInit();
    AlbumSaveScrMsgInit();
    AlbumSaveScrSetListHook();
    AlbumSaveScrIconCreate(pic_mode);
    AlbumSaveScrSetInfoHooks();
    return 1;
}

// FF3 0x001300f0.
int AlbumSaveScrIsInEnd(void)
{
    return AlbumSaveScrObj()->IsInEnd() != 0;
}

// FF3 0x00130118.
void AlbumSaveScrMain(void)
{
    LayoutAnimMain();
    AlbumSaveScrListFade();
    AlbumSaveScrObj()->SetCsrPos(ASS_ITEM_CSR, 0, album_save_ctrl.album_sel_csr);
    AlbumSaveScrMsgUpdate();
    AlbumSaveScrSlotSelUpdate();
}

// FF3 0x00130160.
void AlbumSaveScrClose(void)
{
    AlbumSaveScrMsgClose();
    AlbumSaveScrSlotSelClose();
}

// FF3 0x00130180.
int AlbumSaveScrOutWait(void)
{
    LayoutAnimMain();
    AlbumSaveScrListFade();
    int out  = AlbumSaveScrObj()->Out(ass_label_out);
    int msg  = AlbumSaveScrMsgIsNotClosing();
    int slot = AlbumSaveScrSlotSelIsNotClosing();
    return slot != 0 && msg != 0 && out != 0;
}

// FF3 0x001301d8.
void AlbumSaveScrRelease(void)
{
    AlbumSaveScrObj()->FreeCsr();
    AlbumSaveScrIconRelease();
    AlbumSaveScrMsgReset();
    AlbumSaveScrSlotSelReset();
}

// PC: the mouse. The row cursor as drawn, and the distance between rows.
int AlbumSaveScrMouseBox(float *x, float *y, float *w, float *h)
{
    ReiPan_MenuBox box;

    if (!ReiPan_MenuMouseItemBox(AlbumSaveScrObj()->GetHandle(), ASS_ITEM_CSR, &box))
        return 0;
    *x = box.x;
    *y = box.y;
    *w = box.w;
    *h = box.h;
    return 1;
}

float AlbumSaveScrMouseStep(void)
{
    return (float)ASS_LIST_STEP;
}
