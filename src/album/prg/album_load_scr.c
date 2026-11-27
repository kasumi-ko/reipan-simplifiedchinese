/* ==========================================================================
 *  album/prg/album_load_scr.c
 *
 *  The album load screen (see album_load_scr.h). FF3 0x12d9f8..0x12e798,
 *  FF3's own code (FF2's album load drew its pages from album_load.c); it
 *  sits between the album edit page's layouts (album_view.c) and the
 *  album's texture loader (0x12e798..). Built function for function like
 *  album_save_scr.c; only the work (album_load_ctrl), the confirm step and
 *  the list colours differ.
 * ======================================================================== */
#include "album/prg/album_load_scr.h"

#include <new>                          // operator new / delete
#include <string.h>                     // strlen

#include "album/prg/album_load.h"       // album_load_ctrl, ALBUM_LOAD_*
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

#define ALS_ANIM_SLOT_SEL   7           // layout anim type of the slot select window
#define ALS_ANIM_YESNO      0xb         // layout anim type of the yes/no window
#define ALS_MSG_KIND        0x83        // message kind of album_load_ctrl.msg_id
#define ALS_YESNO_NONE      2           // YesNoSetSel: no choices shown
#define ALS_LA_OPEN         2           // LAYOUT_ANIM.state
#define ALS_LA_CLOSING      3
#define ALS_LA_CLOSED       4
#define ALS_LIST_STEP       0x28        // y distance between two album rows
#define ALS_COVER_PIC       0x82d       // MENU_PIC picture of album kind 0

// Layout items of ALBM_SENTAKU_PHF (the same as the save screen's).
enum
{
    ALS_ITEM_CSR       = 0x405,         // the list cursor
    ALS_ITEM_LIST      = 0x408,         // the five album rows
    ALS_ITEM_CARD_SLOT = 0x40d,         // "slot_M"
    ALS_ITEM_PHOTO_NUM = 0x40e,         // photo count
    ALS_ITEM_DATE      = 0x40f,         // "date"
    ALS_ITEM_ICON_A    = 0x412,         // the chosen album's cover, "albmicon_a".."_c"
    ALS_ITEM_ICON_B    = 0x413,
    ALS_ITEM_ICON_C    = 0x414,
};

static const char als_label_in[]  = "in";      // sdata 0x36ba18
static const char als_label_out[] = "out";     // sdata 0x36ba20

static u_char        album_load_scr_alpha;      // sbss 0x372cc4 (gp-0x82c): album list alpha 0..0x80
static MENU_PIC *album_load_scr_pic;        // sbss 0x372cc8 (gp-0x828): the cover picture
static int           album_load_scr_slot_anim;  // sbss 0x372ccc (gp-0x824): slot window slot, -1 none
static int           album_load_scr_msg_anim;   // sbss 0x372cd0 (gp-0x820): yes/no window slot, -1 none
static int           album_load_scr_ask;        // sbss 0x372cd4 (gp-0x81c): 1 while the choices show (no reader)
static int           album_load_scr_slot_keep;  // sdata 0x36ba10 (gp-0x7ae0): the slot window stays up

static int AlbumLoadScrHandle(void)
{
    return BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + ALBM_SENTAKU_PHF);
}

static BP_OBJ *AlbumLoadScrObj(void)
{
    return album_layout_obj[ALBUM_LAYOUT_SENTAKU];
}

// The text item value the info hooks pass on (EE item +0x124).
static int AlbumLoadScrDrawPri(void)
{
    return B2dGetDrawItem()->part[B2D_PART_TEXT].value;
}

// ──────────────────────────────────────────────────────────────────────
// The album list
// ──────────────────────────────────────────────────────────────────────

// The list fades in (0x20 a frame) on the album select steps (6 / 7) of the
// load mode and out in slot select mode or once the screen is leaving
// (step 3); in the load mode's other steps it stays as it is. 0 until the
// card's albums have been looked at (dir_check_cnt).
// FF3 0x0012d9f8.
static void AlbumLoadScrListFade(void)
{
    if (album_load_ctrl.dir_check_cnt != 0)
    {
        if (album_load_ctrl.mode != ALBUM_LOAD_MODE_SLOT_SEL && album_load_ctrl.step != ALBUM_LOAD_OUT)
        {
            if ((u_int)((u_char)album_load_ctrl.mc_step - ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT) < 2)
            {
                if (album_load_scr_alpha + 0x20 < 0x80)
                {
                    album_load_scr_alpha += 0x20;
                    return;
                }
                album_load_scr_alpha = 0x80;
            }
            return;
        }
        if (album_load_scr_alpha - 0x20 > 0)
        {
            album_load_scr_alpha -= 0x20;
            return;
        }
    }
    album_load_scr_alpha = 0;
}

// alpha scaled by the list's fade (float, 1/128 at 0x36ba08); 0 until the
// card's albums have been looked at.
// FF3 0x0012da80.
static int AlbumLoadScrListAlpha(int alpha)
{
    if (album_load_ctrl.dir_check_cnt == 0)
        return 0;
    return (int)((float)(album_load_scr_alpha * alpha) * (1.0f / 128.0f));
}

// FF3 0x0012dac0.
static int AlbumLoadScrListIsShown(void)
{
    return album_load_scr_alpha == 0x80;
}

// Item 0x408: the card's five albums, one row each 0x28 apart: "Album n"
// and its name (kind -1 for no album, -2 for a broken one). Unlike the
// save screen the rows that cannot be loaded (no / broken album) are
// dimmed blue-grey; the cursor's row is a warm yellow.
// FF3 0x0012dad0.
// callcheck: B2dGetDrawItem (AlbumLoadScrDrawPri)
static int AlbumLoadScrDrawList(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumLoadScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumLoadScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
    {
        int y = 0;

        for (s_char i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++, y += ALS_LIST_STEP)
        {
            s_char kind = -1;

            if (album_load_ctrl.album_flg[i] != 0)
            {
                kind = -2;
                if (album_load_ctrl.album_flg[i] != -1)
                    kind = AlbumSlotInfoGetKind(i);
            }
            if ((u_char)(kind + 2) < 2)
                SetCurColorRGB(0x5a, 0x85, 0x89);
            else
                SetCurColorRGB(0xd4, 0xd4, 0xca);
            if (i == album_load_ctrl.album_sel_csr)
                SetCurColorRGB(0xf9, 0xdf, 0x91);
            AlbumInfoPrintAlbumNo(i, kind, xy[0], xy[1] + y, (int)args->a1, alpha, pri);
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_load_scr_list_hooks[] =     // built on the stack by FF3
{
    { AlbumLoadScrDrawList, 0, ALS_ITEM_LIST, nullptr },
    { nullptr,             -1, -1,            nullptr },
};

// FF3 0x0012dc28.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (AlbumLoadScrHandle)
static void AlbumLoadScrSetListHook(void)
{
    B2dSetHookList(AlbumLoadScrHandle(), album_load_scr_list_hooks);
}

// FF3 0x0012dcb8.
int AlbumLoadScrListIsIn(void)
{
    return AlbumLoadScrListIsShown();
}

// ──────────────────────────────────────────────────────────────────────
// The chosen album's cover
// ──────────────────────────────────────────────────────────────────────

// Items "albmicon_a".."_c": the chosen album's cover picture (its kind
// + 0x82d) through the picture view, sized by the item's w / h parts, its
// fade from part 10's alpha. The name's last letter picks the picture's
// offset (ROM 0x34e8b8).
// FF3 0x0012dcd0 (no Ghidra function before 2026-10-14: inside 0x12dcb8).
// callcheck: strlen (strlen)
static int AlbumLoadScrDrawIcon(const B2D_HOOK_ARGS *args)
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

    if (album_load_scr_pic == nullptr)
        return 0;

    int alpha = AlbumLoadScrListAlpha(a);
    if (alpha > 0)
    {
        MENU_PIC *pic = album_load_scr_pic;

        pic->pic = AlbumSlotInfoGetKind(album_load_ctrl.album_sel_csr) + ALS_COVER_PIC;
        pic->Step();
        pic->u = icon_ofs[icon][0];
        pic->v = icon_ofs[icon][1];
        pic->w    = w;
        pic->h    = h;
        pic->pri = p11;
        pic->x    = (short)xy[0];      // FF3 reads the low halves (lh)
        pic->y    = (short)xy[1];
        pic->z = 0;
        pic->scale = (float)alpha * (1.0f / 128.0f);    // 0x36ba0c
        pic->Draw();
        pic->DrawNext();
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_load_scr_icon_hooks[] =     // ROM 0x34e8d0
{
    { AlbumLoadScrDrawIcon, 0, ALS_ITEM_ICON_A, nullptr },
    { AlbumLoadScrDrawIcon, 0, ALS_ITEM_ICON_B, nullptr },
    { AlbumLoadScrDrawIcon, 0, ALS_ITEM_ICON_C, nullptr },
    { nullptr,             -1, -1,              nullptr },
};

// FF3 copies the ROM list to the stack first.
// FF3 0x0012de78.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk, operator_new (AlbumLoadScrHandle, new MENU_PIC)
// callcheck: MenuPicCtor (MENU_PIC's constructor)
static void AlbumLoadScrIconCreate(int pic_mode)
{
    B2dSetHookList(AlbumLoadScrHandle(), album_load_scr_icon_hooks);
    album_load_scr_pic = new MENU_PIC(pic_mode);
    album_load_scr_pic->enable = 1;
}

// FF3 0x0012df30.
// callcheck: operator_delete (delete)
// callcheck: MenuPicDtor (MENU_PIC's destructor)
static void AlbumLoadScrIconRelease(void)
{
    MENU_PIC *pic = album_load_scr_pic;

    if (pic != nullptr)
    {
        delete pic;
        album_load_scr_pic = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The slot select window
// ──────────────────────────────────────────────────────────────────────

// While the card machine runs: on in slot select mode, and once on it
// stays up until the album select steps (6 / 7) begin.
// FF3 0x0012df70.
static int AlbumLoadScrIsSlotSelStep(void)
{
    if (album_load_ctrl.step != ALBUM_LOAD_MC_EXE)
        return 0;
    if (album_load_ctrl.mode == ALBUM_LOAD_MODE_SLOT_SEL)
    {
        album_load_scr_slot_keep = 1;
        return 1;
    }
    int keep = album_load_scr_slot_keep;
    if (keep != 0)
    {
        if ((u_int)(album_load_ctrl.mc_step - ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT) < 2)
            keep = 0;
        album_load_scr_slot_keep = keep;
    }
    return keep;
}

// Starts the window again once it has closed (while wanted) and keeps its
// cursor on album_load_ctrl's slot; asks it to close when not wanted.
// FF3 0x0012dfc8.
static void AlbumLoadScrSlotSelUpdate(void)
{
    if (album_load_scr_slot_anim != -1 && LayoutAnimGetState(album_load_scr_slot_anim) == ALS_LA_CLOSED)
    {
        LayoutAnimReset(album_load_scr_slot_anim);
        album_load_scr_slot_anim = -1;
    }
    if (album_load_scr_slot_anim == -1 && AlbumLoadScrIsSlotSelStep())
        album_load_scr_slot_anim = LayoutAnimStart(ALS_ANIM_SLOT_SEL, 0);
    if (album_load_scr_slot_anim == -1)
        return;

    if (AlbumLoadScrIsSlotSelStep())
        SlotSelSetSel(album_load_ctrl.slot_csr);
    else
        SlotSelReqClose(1);
}

// FF3 0x0012e078.
static void AlbumLoadScrSlotSelInit(void)
{
    album_load_scr_slot_anim = -1;
    AlbumLoadScrSlotSelUpdate();
}

// FF3 0x0012e098.
static void AlbumLoadScrSlotSelClose(void)
{
    SlotSelReqClose(1);
}

// 0 while the window plays its close.
// FF3 0x0012e0b0.
static int AlbumLoadScrSlotSelIsNotClosing(void)
{
    if (album_load_scr_slot_anim != -1 && LayoutAnimGetState(album_load_scr_slot_anim) == ALS_LA_CLOSING)
        return 0;
    return 1;
}

// FF3 0x0012e0f0.
static void AlbumLoadScrSlotSelReset(void)
{
    if (album_load_scr_slot_anim != -1)
    {
        LayoutAnimReset(album_load_scr_slot_anim);
        album_load_scr_slot_anim = -1;
    }
}

// FF3 0x0012e128.
int AlbumLoadScrSlotSelIsOpen(void)
{
    if (album_load_scr_slot_anim != -1 && LayoutAnimGetState(album_load_scr_slot_anim) != ALS_LA_OPEN)
        return 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The yes/no window
// ──────────────────────────────────────────────────────────────────────

// The load mode's one confirm step ("Load now?").
// FF3 0x0012e168.
static int AlbumLoadScrIsAskStep(void)
{
    if (album_load_ctrl.mode != ALBUM_LOAD_MODE_MC_LOAD)
        return 0;
    return (u_int)(album_load_ctrl.mc_step - ALBUM_LOAD_MC_LOAD_CONF_INIT) < 2;
}

// Opens the window once the slot window is up (and again after it has
// closed) with album_load_ctrl's message; the choices only in the confirm
// step.
// FF3 0x0012e1a0.
static void AlbumLoadScrMsgUpdate(void)
{
    if (album_load_scr_msg_anim != -1 && LayoutAnimGetState(album_load_scr_msg_anim) == ALS_LA_CLOSED)
    {
        LayoutAnimReset(album_load_scr_msg_anim);
        album_load_scr_msg_anim = -1;
    }
    if (album_load_scr_msg_anim == -1 && album_load_scr_slot_anim != -1)
        album_load_scr_msg_anim = LayoutAnimStart(ALS_ANIM_YESNO, 0);
    if (album_load_scr_msg_anim == -1)
        return;

    if (AlbumLoadScrIsAskStep())
    {
        if (album_load_scr_ask == 0)
            album_load_scr_ask = 1;
        YesNoSetMsg(ALS_MSG_KIND, album_load_ctrl.msg_id);
        YesNoSetSel((u_char)album_load_ctrl.conf_csr);
    }
    else
    {
        if (album_load_scr_ask == 1)
            album_load_scr_ask = 0;
        YesNoSetMsg(ALS_MSG_KIND, album_load_ctrl.msg_id);
        YesNoSetSel(ALS_YESNO_NONE);
    }
}

// FF3 0x0012e280.
static void AlbumLoadScrMsgInit(void)
{
    album_load_scr_msg_anim = -1;
    album_load_scr_ask = AlbumLoadScrIsAskStep() != 0;
    AlbumLoadScrMsgUpdate();
}

// FF3 0x0012e2b8.
static void AlbumLoadScrMsgClose(void)
{
    YesNoReqClose(1);
}

// 0 while the window plays its close.
// FF3 0x0012e2d0.
static int AlbumLoadScrMsgIsNotClosing(void)
{
    if (album_load_scr_msg_anim != -1 && LayoutAnimGetState(album_load_scr_msg_anim) == ALS_LA_CLOSING)
        return 0;
    return 1;
}

// FF3 0x0012e310.
static void AlbumLoadScrMsgReset(void)
{
    if (album_load_scr_msg_anim != -1)
    {
        LayoutAnimReset(album_load_scr_msg_anim);
        album_load_scr_msg_anim = -1;
    }
}

// FF3 0x0012e348.
int AlbumLoadScrMsgIsOpen(void)
{
    if (album_load_scr_msg_anim != -1 && LayoutAnimGetState(album_load_scr_msg_anim) != ALS_LA_OPEN)
        return 0;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The chosen album's info (faded with the list)
// ──────────────────────────────────────────────────────────────────────

// Item "slot_M": MEMORY CARD slot n, centred on the item.
// FF3 0x0012e388.
// callcheck: B2dGetDrawItem (AlbumLoadScrDrawPri)
static int AlbumLoadScrDrawCardSlot(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumLoadScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumLoadScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumInfoPrintCardSlot(album_load_ctrl.slot_csr, (xy[0] + xy[6]) / 2, xy[1], (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x40e: the chosen album's photo count "nn/16".
// FF3 0x0012e428.
// callcheck: B2dGetDrawItem (AlbumLoadScrDrawPri)
static int AlbumLoadScrDrawPhotoNum(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumLoadScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumLoadScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumInfoPrintPhotoNum(AlbumSlotInfoGetPhotoNum(album_load_ctrl.album_sel_csr), xy[0], xy[1],
                               (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

// Item "date": the chosen album's date.
// FF3 0x0012e4c0.
// callcheck: B2dGetDrawItem (AlbumLoadScrDrawPri)
static int AlbumLoadScrDrawDate(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    int        pri = AlbumLoadScrDrawPri();
    int        alpha;

    SetCurColorPacked((u_int)args->t3);
    alpha = AlbumLoadScrListAlpha((int)args->stack[2]);
    if (alpha > 0)
        AlbumViewPhotoInfoDisp(AlbumSlotInfoGet(album_load_ctrl.album_sel_csr), xy[0], xy[1],
                               (int)args->a1, alpha, pri);
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC album_load_scr_info_hooks[] =     // ROM 0x34e910
{
    { AlbumLoadScrDrawCardSlot, 0, ALS_ITEM_CARD_SLOT, nullptr },
    { AlbumLoadScrDrawPhotoNum, 0, ALS_ITEM_PHOTO_NUM, nullptr },
    { AlbumLoadScrDrawDate,     0, ALS_ITEM_DATE,      nullptr },
#ifdef REIPAN_SLPS_25544_CHS
    { AlbumInfoDrawPhotoLabel,  0, 0,                 "R pht" },
#endif
    { nullptr,                 -1, -1,                 nullptr },
};

// FF3 copies the ROM list to the stack first.
// FF3 0x0012e558.
// callcheck: BpCtlGetHandleByFileNo, GetLanguage, GetSysWrk (AlbumLoadScrHandle)
static void AlbumLoadScrSetInfoHooks(void)
{
    B2dSetHookList(AlbumLoadScrHandle(), album_load_scr_info_hooks);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 28's sequence
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0012e5d8.
void AlbumLoadScrInit(void)
{
    album_load_scr_alpha = 0;
}

// FF3 0x0012e5f0.
int AlbumLoadScrIn(int pic_mode)
{
    LayoutAnimMain();
    AlbumLoadScrObj()->In(als_label_in);
    AlbumLoadScrObj()->AddCsr(ALS_ITEM_CSR, 0, ALS_LIST_STEP, 0);
    AlbumLoadScrSlotSelInit();
    AlbumLoadScrMsgInit();
    AlbumLoadScrSetListHook();
    AlbumLoadScrIconCreate(pic_mode);
    AlbumLoadScrSetInfoHooks();
    return 1;
}

// FF3 0x0012e678.
int AlbumLoadScrIsInEnd(void)
{
    return AlbumLoadScrObj()->IsInEnd() != 0;
}

// FF3 0x0012e6a0.
void AlbumLoadScrMain(void)
{
    LayoutAnimMain();
    AlbumLoadScrListFade();
    AlbumLoadScrObj()->SetCsrPos(ALS_ITEM_CSR, 0, album_load_ctrl.album_sel_csr);
    AlbumLoadScrMsgUpdate();
    AlbumLoadScrSlotSelUpdate();
}

// FF3 0x0012e6e8.
void AlbumLoadScrClose(void)
{
    AlbumLoadScrMsgClose();
    AlbumLoadScrSlotSelClose();
}

// FF3 0x0012e708.
int AlbumLoadScrOutWait(void)
{
    LayoutAnimMain();
    AlbumLoadScrListFade();
    int out  = AlbumLoadScrObj()->Out(als_label_out);
    int msg  = AlbumLoadScrMsgIsNotClosing();
    int slot = AlbumLoadScrSlotSelIsNotClosing();
    return slot != 0 && msg != 0 && out != 0;
}

// FF3 0x0012e760.
void AlbumLoadScrRelease(void)
{
    AlbumLoadScrObj()->FreeCsr();
    AlbumLoadScrIconRelease();
    AlbumLoadScrMsgReset();
    AlbumLoadScrSlotSelReset();
}

// PC: the mouse. The row cursor as drawn, and the distance between rows.
int AlbumLoadScrMouseBox(float *x, float *y, float *w, float *h)
{
    ReiPan_MenuBox box;

    if (!ReiPan_MenuMouseItemBox(AlbumLoadScrObj()->GetHandle(), ALS_ITEM_CSR, &box))
        return 0;
    *x = box.x;
    *y = box.y;
    *w = box.w;
    *h = box.h;
    return 1;
}

float AlbumLoadScrMouseStep(void)
{
    return (float)ALS_LIST_STEP;
}
