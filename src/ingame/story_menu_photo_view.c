/* ==========================================================================
 *  ingame/story_menu_photo_view.c
 *
 *  The document views, phases 84 StoryMenu_PhotoDetail and 85
 *  StoryMenu_PhotoMap; see story_menu_photo_view.h. FF3 0x1b24d8..0x1b3078.
 *
 *  Both keep running the document menu's per-frame part (DocMenuUpdate)
 *  under them and move its cursor when the page keys change the document, so the
 *  menu shows the last one viewed when they close.
 *
 *  Not written: FF3 0x001b3058, the GPhaseCtrl singleton's atexit thunk
 *  (as in every file that includes the getter).
 * ======================================================================== */
#include "ingame/story_menu_photo_view.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer
#include "graphics/graph2d/message.h"   // GetMsgDataAddr, PrintMsg_ArrangeOneLine, SetCurColorRGB
#include "graphics/motion/anicode.h"    // MsgView*, MenuWdwSetMsg, B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/menu/anim_2d.h"        // LayoutAnim*
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/menu/menu_bg.h"      // MenuLayoutIsPlaying
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "ingame/story_menu_photo.h"    // Doc*, msg_pic_key_dat
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/b2d/B2dCtl.h"          // B2dCtlFindItemByName
#include "system/b2d/B2dSpr.h"          // B2dSprSetDisp
#include "system/b2d/BpCtl.h"           // BpCtl*, BpPlayLabel
#include "system/eeiop/cd_file_dat.h"   // FILE_ETURAN_PHOTO_PHF
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe, CheckPadRptPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

#define PHASE_STORY_MENU_PHOTO  0x3d    // phase 61, the document menu

#define VIEW_SE_CANCEL  0x1a9b0001
#define VIEW_SE_BUZZER  0x1a9b0002      // already the first / last
#define VIEW_SE_PAGE    0x1a9b0005

static int       photo_detail_anim;     // sbss gp-0x314: the message view's layout slot
static MENU_PIC *photo_map_pic;         // sdata 0x370434 (gp-0x30bc)

static short PhotoDetailMain(void);
static void  PhotoDetailMove(int dir);
static void  PhotoDetailOpen(void);
static void  PhotoDetailClose(void);
static int   PhotoDetailIsOpen(void);
static void  PhotoDetailSetDoc(void);
static void  PhotoDetailBackToText(void);
static void  PhotoDetailUpdate(void);
static void  PhotoMapUpdate(void);
static short PhotoMapMain(void);
static void  PhotoMapMove(int dir);
static void  PhotoMapPicCreate(void);
static void  PhotoMapPicDelete(void);
static void  PhotoMapWdwSetMsg(void);

// ──────────────────────────────────────────────────────────────────────
// Phase 84 StoryMenu_PhotoDetail
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001b24d8
int init_StoryMenu_PhotoDetail(void)
{
    DocMenuUpdate();
    PhotoDetailOpen();
    return 1;
}

// FF3 0x001b2500
int end_StoryMenu_PhotoDetail(void)
{
    PhotoDetailUpdate();
    PhotoDetailClose();
    return 1;
}

// FF3 0x001b2528
int one_StoryMenu_PhotoDetail(void)
{
    short next;

    PhotoDetailUpdate();
    next = PhotoDetailMain();
    if (next != -1)
        GPhaseCtrlRequest(next, 0);
    return next != -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b2570

static void PhotoDetailUpdate(void)
{
    DocMenuUpdate();
}

// ──────────────────────────────────────────────────────────────────────
// The page keys (with two documents or more) the previous / next one; back to the
// menu once the message view has closed (the view handles its own keys).
// FF3 0x001b2588

static short PhotoDetailMain(void)
{
    short next = -1;
    int   dir;

    if (DocMenuGetNum(DocMenuGetTab()) > 1) {
        dir = 0;
        if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_PREV], -1, 0))
            dir = -1;
        else if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_NEXT], -1, 0))
            dir = 1;
        if (dir != 0)
            PhotoDetailMove(dir);
    }
    if (PhotoDetailIsOpen() == 0)
        next = PHASE_STORY_MENU_PHOTO;
    return next;
}

// ──────────────────────────────────────────────────────────────────────
// The menu cursor dir documents on; a new one replaces the text and the
// pictures and the view goes back to the text.
// FF3 0x001b2618

static void PhotoDetailMove(int dir)
{
    int tab   = DocMenuGetTab();
    int index = DocCsrGetIndex(tab);
    u_int se  = VIEW_SE_BUZZER;

    if (index != DocCsrSetIndex(tab, index + dir)) {
        PhotoDetailSetDoc();
        PhotoDetailBackToText();
        se = VIEW_SE_PAGE;
    }
    SndSePlay(se, 100, 0x1000, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b26c0 / 0x001b26e8 / 0x001b2708

static void PhotoDetailOpen(void)
{
    photo_detail_anim = LayoutAnimStart(0, 0);
    PhotoDetailSetDoc();
}

// FF3 0x001b26e8
static void PhotoDetailClose(void)
{
    LayoutAnimReset(photo_detail_anim);
    MsgViewSetPics(nullptr, 0);
}

// FF3 0x001b2708
static int PhotoDetailIsOpen(void)
{
    return LayoutAnimGetState(photo_detail_anim) != 4;
}

// ──────────────────────────────────────────────────────────────────────
// The current document into the message view: its text, mode 0, its
// pictures (files pic .. pic + pic_num - 1); it counts as read.
// FF3 0x001b2730

static void PhotoDetailSetDoc(void)
{
    short key = DocMenuGetCurDoc();
    if ((u_short)key >= MSG_PIC_KEY_NUM)
    {
        MsgViewSetMsg(-1, -1);
        MsgViewSetPics(nullptr, 0);
        return;
    }
    const MSG_PIC_KEY_DAT &doc = msg_pic_key_dat[key];
    int pics[MSG_VIEW_PIC_MAX];
    const int count = (u_short)doc.pic_num <= MSG_VIEW_PIC_MAX ? doc.pic_num : 0;
    for (int i = 0; i < count; i++)
        pics[i] = doc.pic + i;

    MsgViewSetMsg(DocGetMsgType(key, 0), doc.msg1);
    MsgViewSetMode(0);
    MsgViewSetPics(pics, count);
    DocFlagSetRead(key);
}

// FF3 0x001b2880
static void PhotoDetailBackToText(void)
{
    MsgViewBackToText(LayoutAnimGetSlot(photo_detail_anim)->handle);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 85 StoryMenu_PhotoMap
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001b28a8. Step 0: the layout loads; step 1: the map picture, "in",
// the menu window's alpha on this layout's "wasi", its caption, the fade
// layer to label 8, the map counts as read; step 2 waits for the way in.

int init_StoryMenu_PhotoMap(void)
{
    switch (GetGPhaseCtrl()->leaf->state_cnt) {
    case 0:
        DocMenuUpdate();
        BpCtlLoadReq(FILE_ETURAN_PHOTO_PHF, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        break;

    case 1:
        DocMenuUpdate();
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF)) == 0)
            return 0;
        PhotoMapPicCreate();
        BpPlayLabel(BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF), "in");               /* sdata 0x370420 */
        LayoutAnimSetAlphaItemByName(DocMenuGetWdwAnim(), BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF),
                                     "wasi");                                           /* sdata 0x370428 */
        PhotoMapWdwSetMsg();
        GetCmnFadeLayer()->Switch(8, 0x1e);
        DocFlagSetRead(DocMenuGetCurDoc());
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        break;

    case 2:
        PhotoMapUpdate();
        return MenuLayoutIsPlaying(FILE_ETURAN_PHOTO_PHF) == 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b2a30: "out", then the menu window's caption back, the layout
// and the picture released.

int end_StoryMenu_PhotoMap(void)
{
    PhotoMapUpdate();
    switch (GetGPhaseCtrl()->leaf->state_cnt) {
    case 0:
        BpPlayLabel(BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF), "out");             /* sdata 0x370430 */
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        break;

    case 1:
        if (MenuLayoutIsPlaying(FILE_ETURAN_PHOTO_PHF) != 0)
            return 0;
        DocMenuWdwUpdate();
        BpCtlRelease(BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF));
        PhotoMapPicDelete();
        return 1;
    }
    return 0;
}

// FF3 0x001b2af8
int one_StoryMenu_PhotoMap(void)
{
    short next;

    PhotoMapUpdate();
    next = PhotoMapMain();
    if (next != -1)
        GPhaseCtrlRequest(next, 0);
    return next != -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b2b40

static void PhotoMapUpdate(void)
{
    PhotoMapWdwSetMsg();
    DocMenuUpdate();
}

// ──────────────────────────────────────────────────────────────────────
// Cancel back to the menu; the page keys or left/right (two maps or more) the
// previous / next map.
// FF3 0x001b2b60

static short PhotoMapMain(void)
{
    int dir;

    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_HORIZONTAL, nullptr);     // PC: the wheel turns the map
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0)) {
        SndSePlay(VIEW_SE_CANCEL, 100, 0x1000, 0, 0, 0);
        return PHASE_STORY_MENU_PHOTO;
    }
    if (DocMenuGetNum(DocMenuGetTab()) > 1) {
        if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_PREV] | 0x8000, -1, 0))
            dir = -1;
        else if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_NEXT] | 0x2000, -1, 0))
            dir = 1;
        else
            return -1;
        PhotoMapMove(dir);
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b2c30

static void PhotoMapMove(int dir)
{
    int   tab   = DocMenuGetTab();
    int   index = DocCsrGetIndex(tab);
    short now   = DocCsrSetIndex(tab, index + dir);

    DocFlagSetRead(DocMenuGetCurDoc());
    SndSePlay(index == now ? VIEW_SE_BUZZER : VIEW_SE_PAGE, 100, 0x1000, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// The map picture on FILE_PHOTO (the item's place and size, alpha from the
// item): file menu_pic + 2 of the current document.
// FF3 0x001b2e80 (no Ghidra function before 2026-10-20: it followed
// PhotoMapWdwSetMsg's tail call)

static int PhotoMapPicHook(const B2D_HOOK_ARGS *args)
{
    const int *xy  = (const int *)args->t1;
    short      key = DocMenuGetCurDoc();
    MENU_PIC  *pic;

    if (key != -1 && photo_map_pic != nullptr) {
        photo_map_pic->pic = msg_pic_key_dat[key].menu_pic + 2;
        photo_map_pic->Step();
        photo_map_pic->v     = 0;
        photo_map_pic->u     = 0;
        photo_map_pic->h     = (int)args->a2;
        photo_map_pic->w     = (int)args->a1;
        photo_map_pic->pri   = 0xa0;
        photo_map_pic->x     = (short)xy[0];
        photo_map_pic->y     = (short)xy[1];
        photo_map_pic->z     = 0;
        photo_map_pic->scale = (float)(int)args->stack[2] * (1.0f / 128.0f);   /* sdata 0x37043c */
        pic = photo_map_pic;
        pic->Draw();
        pic->DrawNext();
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// photoname_M: the title, centred over the item, in the item's colour.
// FF3 0x001b2f70

static int PhotoMapNameHook(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_int      rgb   = (u_int)args->t3;
    int        alpha = (int)args->stack[2];
    short      key   = DocMenuGetCurDoc();
    u_char    *str;

    if (key != -1) {
        str = GetMsgDataAddr(DocGetMsgType(key, 0), msg_pic_key_dat[key].msg1);
        if (str != nullptr) {
            SetCurColorRGB(rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff);
            PrintMsg_ArrangeOneLine(str, (xy[0] + xy[6]) / 2, xy[1], 0x2d, alpha, 0xa0, 0, 0, 0);
        }
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The map picture (both slots placed directly in VRAM), the "arw" arrows
// when the tab has two maps or more, and the item hooks (rodata 0x3592d8,
// copied onto the stack).
// FF3 0x001b2cd8
// callcheck: operator_new, MenuPicCtor (new)

static void PhotoMapPicCreate(void)
{
    static const B2D_HOOK_REC hooks[3] =
    {
        { PhotoMapPicHook,  0,  0,  "FILE_PHOTO"  },
        { PhotoMapNameHook, 0,  0,  "photoname_M" },
        { nullptr,          -1, -1, nullptr       },
    };
    int handle;
    int num;

    photo_map_pic = new MENU_PIC(0);
    if (photo_map_pic->load[0] != nullptr)
        photo_map_pic->load[0]->direct = 1;
    if (photo_map_pic->load[1] != nullptr)
        photo_map_pic->load[1]->direct = 1;
    photo_map_pic->enable = 1;
    handle = BpCtlGetHandleByFileNo(FILE_ETURAN_PHOTO_PHF);
    num    = DocMenuGetNum(DocMenuGetTab());
    B2dSprSetDisp(B2dCtlFindItemByName(handle, "arw"), num > 1);    /* sdata 0x370438 */
    B2dSetHookList(handle, hooks);
}

// FF3 0x001b2dc8
// callcheck: MenuPicDtor, operator_delete (delete)
static void PhotoMapPicDelete(void)
{
    if (photo_map_pic != nullptr)
        delete photo_map_pic;
    photo_map_pic = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The menu window's caption for the current map (msg1 + 1), (0, 0) none.
// FF3 0x001b2e08

static void PhotoMapWdwSetMsg(void)
{
    short key = DocMenuGetCurDoc();

    if (key == -1)
        MenuWdwSetMsg(0, 0);
    else
        MenuWdwSetMsg(DocGetMsgType(key, 1), (short)(msg_pic_key_dat[key].msg1 + 1));
}
