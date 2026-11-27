/* ==========================================================================
 *  ingame/story_menu_film.c
 *
 *  The film menu, phase 60 StoryMenu_Film; see story_menu_film.h.
 *  FF3 0x22ec88..0x230a38.
 *
 *  Two cursors (MENU_CSR_WRK): 0 walks the 8 x 2 photo slots of the
 *  camera's album, 1 the three rows of the sort menu (time, score,
 *  protect). Each keeps a saved position: the photo cursor's tells the
 *  large picture's hook when to unpack a new photo (-1 forces it), the
 *  sort cursor's is the row the album was last sorted by, kept across
 *  menu visits (FilmMenuSortInit clears it once per game).
 *
 *  The steps (FilmMenuMain): 0 browse (OK protects / unprotects, circle
 *  asks to delete, square opens the sort menu, cancel goes back to the
 *  story menu), 1 the sort menu (OK sorts by the row; on the same row
 *  again the order flips), 2 / 3 the delete question (LayoutMsg 0x67 / 1,
 *  or 0x67 / 2 "protected" with a buzzer) and its window, 4 / 5 "no
 *  photos" (0x67 / 0) and its window, then back to the story menu.
 *
 *  Drawn by item hooks: 0x44f the photo under the cursor large, 0x460 the
 *  small pictures, 0x471 / 0x43a / 0x472 / 0x473 / 0x475 its room, date,
 *  score and subjects (hook list 0x35fd50), PHOTO_MENU_PHF 0x40a the sort
 *  cursor. A B2D_GRID_SPR moves the slot cursor (item 0x43c).
 *
 *  FF3 reads GetGPhaseCtrl / GetSysWrk through gp-relative cached
 *  pointers; the calls go to the getters directly. Not written: FF3
 *  0x002309f8, the GPhaseCtrl singleton's atexit thunk (as in every file
 *  that includes the getter).
 * ======================================================================== */
#include "ingame/story_menu_film.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, capCtl
#include "common/cmn_time.h"            // CdClockBcdToInt, CLOCK_DATE_NUM
#include "graphics/graph2d/message.h"   // PrintNumber_N, MsgPrintPageMain, MsgSetSubtractMode, MSG_COL_CURRENT
#include "graphics/motion/anicode.h"    // B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState, SetCurColorPacked
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/menu/anim_2d.h"        // LayoutAnim*, LayoutMsg*, Anim2dMsgGetCsr
#include "ingame/menu/menu_bg.h"        // MenuGetTopPhase, MenuLayoutIsPlaying, B2dHookPrintMsgLine, B2dHookPrintNumber
#include "ingame/menu/menu_csr.h"       // MENU_CSR_WRK
#include "ingame/menu/menu_csr_obj.h"   // MenuCsrObj*
#include "ingame/photo/finder_photo.h"  // GetPhotoModeWrk, PHOTO_MODE_WRK::GetMode
#include "ingame/photo/photo.h"         // PICTURE_WRK, PICTURE_STATUS_*
#include "ingame/photo/photo_comp.h"    // UncompressPhoto
#include "ingame/photo/photo_file.h"    // GetFilePhoto*, GetPhotoData, SortPhotoData_*, GetCamPhotoFile
#include "ingame/photo/photo_make.h"    // DrawPhotoFromWorkArea, DrawSPhotoFromSmallPhotoArea2
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "ingame/story_menu_ghost_list.h" // SoulListMsgNo
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/b2d/B2dCtl.h"          // B2D_HOOK_ARGS, B2D_ITEM_HOOK, B2dCtlFindItem, B2dCtlSetItemHook
#include "system/b2d/B2dGridSpr.h"      // B2D_GRID_SPR
#include "system/b2d/B2dSpr.h"          // B2dSprSetDisp
#include "system/b2d/BpCtl.h"           // BpCtl*, BpPlayLabel, BpPlayLabelEx, B2dDrawSpriteDirect
#include "system/eeiop/cd_file_dat.h"   // PHOTO_PHF, PHOTO_MENU_PHF
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────

#define FILM_SE_MOVE    0x1a9b0000      // cursor moved
#define FILM_SE_CANCEL  0x1a9b0001
#define FILM_SE_BUZZER  0x1a9b0002
#define FILM_SE_OK      0x1a9b0003

#define FILM_PAD_DELETE 0x20            // circle: ask to delete the photo
#define FILM_PAD_SORT   0x80            // square: open / close the sort menu

// The menu's steps (FilmMenuMain).
enum FILM_MENU_STEP
{
    FILM_STEP_MAIN       = 0,           // browse the photos
    FILM_STEP_SORT       = 1,           // the sort menu is open
    FILM_STEP_DELETE     = 2,           // open the delete question
    FILM_STEP_DELETE_ASK = 3,           // its window plays
    FILM_STEP_EMPTY      = 4,           // no photo at all: the message
    FILM_STEP_EMPTY_WAIT = 5,           // its window plays, then the story menu
    FILM_STEP_NUM        = 6,
};

enum FILM_CSR
{
    FILM_CSR_PHOTO = 0,                 // the photo slot, 8 x 2
    FILM_CSR_SORT  = 1,                 // the sort row, 1 x 3
    FILM_CSR_NUM   = 2,
};

// FilmMenuSortPhotos's kinds (the sort menu's rows).
enum FILM_SORT
{
    FILM_SORT_TIME    = 0,              // order 0 newest first, 1 oldest first
    FILM_SORT_SCORE   = 1,              // order 0 highest first, 1 lowest first
    FILM_SORT_PROTECT = 2,              // order 0 protected first, 1 unprotected first
};

#define FILM_SLOT_COLS  8               // the small pictures (FilmThumbHook, FilmMenuDispUpdate)
#define FILM_SLOT_ROWS  2

// PHOTO_PHF items.
#define FILM_ITEM_DATE      0x43a
#define FILM_ITEM_FADE      0x43b       // the fade layer's item
#define FILM_ITEM_PROTECT   0x43d       // the large picture's protect mark
#define FILM_ITEM_MODE_1    0x443       // the photo was taken in a photo mode != 0
#define FILM_ITEM_MODE_0    0x44d       // ... in mode 0
#define FILM_ITEM_PHOTO     0x44f       // the large picture
#define FILM_ITEM_THUMBS    0x460       // the small pictures
#define FILM_ITEM_ROOM      0x471
#define FILM_ITEM_SCORE     0x472
#define FILM_ITEM_SUBJ_HEAD 0x473
#define FILM_ITEM_SUBJECTS  0x475

static short FilmMenuMain(void);
static void  FilmMenuUpdate(void);
static void  FilmMenuInit(void);
static void  FilmMenuRelease(void);
static int   FilmMenuGetStep(void);
static int   FilmMenuSetStep(u_int step);
static int   FilmMenuGetSortOrder(void);
static int   FilmMenuSetSortOrder(u_int order);
static void  FilmCsrInit(int i);
static void  FilmCsrRelease(int i);
static void  FilmCsrUpdate(int i);
static short FilmCsrGetPos(int i);
static short FilmCsrSetPos(int i, short pos);
static short FilmCsrGetCol(int i);
static short FilmCsrGetRow(int i);
static short FilmCsrGetSaved(int i);
static void  FilmCsrSave(int i);
static short FilmCsrRestore(int i);
static void  FilmCsrClearSaved(int i);
static short FilmMenuGetPhotoNum(void);
static void  FilmMenuSortPhotos(short kind, short order);
static void  FilmGridCreate(void);
static void  FilmGridDelete(void);
static void  FilmGridUpdate(void);
static void  FilmModeIconInit(void);
static void  FilmModeIconRelease(void);
static void  FilmMenuDispUpdate(void);
static void  FilmPhotoCreate(void);
static void  FilmPhotoRelease(void);
static void  FilmThumbCreate(void);
static void  FilmThumbRelease(void);
static void  FilmInfoCreate(void);
static void  FilmInfoRelease(void);

// The two cursors' layouts. FF3 0x002308f0
// (__static_initialization_and_destruction_0, run by _GLOBAL__I_ 0x00230a18)
// stores them at start-up with the file numbers + GetLanguage(), which is 0
// then (SYS_WRK's constructor sets language id 2); the users add
// GetLanguage() again, so the table holds the base files. bss 0x3f7788.
static const FILM_CSR_CFG film_csr_cfg[FILM_CSR_NUM] =
{
    { 8, 2, 0x4c, 0x30, 0x43c, PHOTO_PHF      },  // the slot cursor over the small pictures
    { 1, 3, 0,    0x25, 0x40a, PHOTO_MENU_PHF },  // the sort cursor
};

static int          film_menu_step;                 // bss 0x3f77a8: FILM_STEP_*
static int          film_sort_order;                // bss 0x3f77ac: FilmMenuSortPhotos's order
static MENU_CSR_WRK film_csr[FILM_CSR_NUM];         // bss 0x3f77b0 (EE 0x3c each)
static short        film_csr_saved[FILM_CSR_NUM];   // bss 0x3f7828
static B2D_GRID_SPR *film_grid;                     // sbss gp+0x308: the slot cursor
static int          film_mode_flag;                 // sbss gp+0x310: the photo's mode != 0
static int          film_msg_anim;                  // sdata 0x371994: the message window's slot

static int FilmPhotoFile(void)
{
    return PHOTO_PHF + GetSysWrk()->GetLanguage();
}

static int FilmMenuFile(void)
{
    return PHOTO_MENU_PHF + GetSysWrk()->GetLanguage();
}

// ──────────────────────────────────────────────────────────────────────
// Step 0 loads both layouts; step 1 waits for them, plays "in" and sets
// up the cursor sprite and the hooks; step 2 runs the menu until "in" has
// played.
// FF3 0x0022ec88
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile / FilmMenuFile); CmnFadeLayerSetItem, CmnFadeLayerShow (capCtl)

int init_StoryMenu_Film(void)
{
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        BpCtlMain();
        FilmMenuInit();
        BpCtlLoadReq(FilmPhotoFile(), nullptr);
        BpCtlLoadReq(FilmMenuFile(), nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        BpCtlMain();
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(FilmPhotoFile())) == 0)
        {
            return 0;
        }
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(FilmMenuFile())) == 0)
        {
            return 0;
        }
        GetCmnFadeLayer()->SetItem(BpCtlGetHandleByFileNo(FilmPhotoFile()), FILM_ITEM_FADE);
        GetCmnFadeLayer()->Show(FilmMenuGetStep() == FILM_STEP_EMPTY ? 2 : 4);
        BpPlayLabel(BpCtlGetHandleByFileNo(FilmPhotoFile()), "in");     // sdata 0x371988
        FilmGridCreate();
        FilmModeIconInit();
        FilmPhotoCreate();
        FilmThumbCreate();
        FilmInfoCreate();
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        return 0;

    case 2:
        FilmMenuUpdate();
        return MenuLayoutIsPlaying(FilmPhotoFile()) == 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Step 0 plays "out"; step 1 waits for it, then releases everything.
// FF3 0x0022ef30
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile / FilmMenuFile); CmnFadeLayerSetItemHandle (capCtl)

int end_StoryMenu_Film(void)
{
    FilmMenuUpdate();
    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        BpPlayLabel(BpCtlGetHandleByFileNo(FilmPhotoFile()), "out");    // sdata 0x371990
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        break;

    case 1:
        if (MenuLayoutIsPlaying(FilmPhotoFile()) != 0)
        {
            return 0;
        }
        FilmGridDelete();
        FilmModeIconRelease();
        FilmPhotoRelease();
        FilmThumbRelease();
        FilmInfoRelease();
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        BpCtlRelease(BpCtlGetHandleByFileNo(FilmPhotoFile()));
        BpCtlRelease(BpCtlGetHandleByFileNo(FilmMenuFile()));
        FilmMenuRelease();
        return 1;
    }
    return 0;
}

// FF3 0x0022f0b8

int one_StoryMenu_Film(void)
{
    short next;

    FilmMenuUpdate();
    next = FilmMenuMain();
    if (next != -1)
    {
        GPhaseCtrlRequest(next, 0);
    }
    return next != -1;
}

// ──────────────────────────────────────────────────────────────────────
// Every frame: the slot cursor, the marks, the fade layer, the layouts.
// FF3 0x0022f100
// callcheck: CmnFadeLayerUpdate (capCtl::Update)

static void FilmMenuUpdate(void)
{
    FilmGridUpdate();
    FilmMenuDispUpdate();
    GetCmnFadeLayer()->Update();
    BpCtlMain();
}

// ──────────────────────────────────────────────────────────────────────
// The steps (see the file comment); returns the phase to go to, -1 none.
// FF3 0x0022f138
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (FilmMenuFile)

static short FilmMenuMain(void)
{
    u_char no = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    short  pos;
    int    next;

    switch (FilmMenuGetStep())
    {
    case FILM_STEP_MAIN:
        FilmCsrUpdate(FILM_CSR_PHOTO);
        if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0)
        {
            if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) == 0)
            {
                return -1;
            }
            if ((GetFilePhotoState(no) & PICTURE_STATUS_PROTECT) != 0)
            {
                DelFilePhotoProtect(no);
                SndSePlay(FILM_SE_CANCEL, 100, 0x1000, 0, nullptr, 0);
            }
            else if (SetFilePhotoProtect(no) == 0)
            {
                SndSePlay(FILM_SE_BUZZER, 100, 0x1000, 0, nullptr, 0);     // 15 protected already
            }
            else
            {
                SndSePlay(FILM_SE_OK, 100, 0x1000, 0, nullptr, 0);
            }
            return -1;
        }
        if (CheckPadTrgPlaySe(FILM_PAD_DELETE, -1, 0) != 0)
        {
            if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) == 0)
            {
                return -1;
            }
            next = FILM_STEP_DELETE;
        }
        else if (CheckPadTrgPlaySe(FILM_PAD_SORT, -1, 0) != 0)
        {
            BpPlayLabelEx(BpCtlGetHandleByFileNo(FilmMenuFile()), "menu_in", 0);  // sdata 0x371998
            SndSePlay(FILM_SE_OK, 100, 0x1000, 0, nullptr, 0);
            FilmCsrRestore(FILM_CSR_SORT);
            next = FILM_STEP_SORT;
        }
        else
        {
            if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) == 0)
            {
                return -1;
            }
            SndSePlay(FILM_SE_CANCEL, 100, 0x1000, 0, nullptr, 0);
            return MenuGetTopPhase();
        }
        break;

    case FILM_STEP_SORT:
        pos = FilmCsrGetPos(FILM_CSR_SORT);
        FilmCsrUpdate(FILM_CSR_SORT);
        if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0)
        {
            if (pos == FilmCsrGetSaved(FILM_CSR_SORT))
            {
                FilmMenuSetSortOrder(FilmMenuGetSortOrder() == 0);
            }
            else
            {
                FilmCsrSave(FILM_CSR_SORT);
            }
            FilmMenuSortPhotos(pos, (short)FilmMenuGetSortOrder());
            FilmCsrClearSaved(FILM_CSR_PHOTO);
            SndSePlay(FILM_SE_OK, 100, 0x1000, 0, nullptr, 0);
            return -1;
        }
        if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0) == 0 &&
            CheckPadTrgPlaySe(FILM_PAD_SORT, -1, 0) == 0)
        {
            return -1;
        }
        BpPlayLabelEx(BpCtlGetHandleByFileNo(FilmMenuFile()), "menu_out", 0);     // rodata 0x35fd28
        FilmMenuSetStep(FILM_STEP_MAIN);
        SndSePlay(FILM_SE_CANCEL, 100, 0x1000, 0, nullptr, 0);
        return -1;

    case FILM_STEP_DELETE:
        if ((GetFilePhotoState(no) & PICTURE_STATUS_PROTECT) == 0)
        {
            SndSePlay(FILM_SE_OK, 100, 0x1000, 0, nullptr, 0);
            film_msg_anim = LayoutAnimStart(5, 0);
            LayoutMsgSet(0x67, 1);
            LayoutMsgSetType(1);        // yes / no
        }
        else
        {
            SndSePlay(FILM_SE_BUZZER, 100, 0x1000, 0, nullptr, 0);
            film_msg_anim = LayoutAnimStart(3, 0);
            LayoutMsgSet(0x67, 2);
            LayoutMsgSetType(2);
        }
        LayoutMsgSetArg(1);
        LayoutAnimClearFlag(film_msg_anim, LAYOUT_ANIM_HOLD);
        next = FILM_STEP_DELETE_ASK;
        break;

    case FILM_STEP_DELETE_ASK:
        LayoutAnimMain();
        if (LayoutAnimGetState(film_msg_anim) != 4)     // closed
        {
            return -1;
        }
        if (Anim2dMsgGetCsr() == 0)
        {
            DeletePhotoData(no);
        }
        LayoutAnimReset(film_msg_anim);
        next = FilmMenuGetPhotoNum() < 1 ? FILM_STEP_EMPTY : FILM_STEP_MAIN;
        break;

    case FILM_STEP_EMPTY:
        GetCmnFadeLayer()->Switch(2, 0x1e);
        SndSePlay(FILM_SE_BUZZER, 100, 0x1000, 0, nullptr, 0);
        film_msg_anim = LayoutAnimStart(3, 0);
        LayoutMsgSet(0x67, 0);
        LayoutMsgSetType(2);
        LayoutMsgSetArg(1);
        LayoutAnimClearFlag(film_msg_anim, LAYOUT_ANIM_HOLD);
        next = FILM_STEP_EMPTY_WAIT;
        break;

    case FILM_STEP_EMPTY_WAIT:
        LayoutAnimMain();
        if (LayoutAnimGetState(film_msg_anim) != 4)     // closed
        {
            return -1;
        }
        SndSePlay(FILM_SE_CANCEL, 100, 0x1000, 0, nullptr, 0);
        LayoutAnimReset(film_msg_anim);
        return MenuGetTopPhase();

    default:
        return -1;
    }
    FilmMenuSetStep(next);
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Once per game (IngameInit): sort order 1 and the sort cursor on row 0
// (time); the menu keeps both across visits.
// FF3 0x0022f628 (Ghidra had it as PlyrFlagInit)

void FilmMenuSortInit(void)
{
    film_sort_order               = 1;
    film_csr_saved[FILM_CSR_SORT] = 0;
}

// FF3 0x0022f640

static void FilmMenuInit(void)
{
    film_menu_step = FILM_STEP_MAIN;
    FilmCsrInit(FILM_CSR_PHOTO);
    FilmCsrInit(FILM_CSR_SORT);
    film_csr_saved[FILM_CSR_PHOTO] = -1;
    FilmCsrRestore(FILM_CSR_SORT);
    if (FilmMenuGetPhotoNum() < 1)
    {
        film_menu_step = FILM_STEP_EMPTY;
    }
}

// The album keeps its used photos first when the menu closes.
// FF3 0x0022f6a0

static void FilmMenuRelease(void)
{
    if (FilmMenuGetPhotoNum() > 0)
    {
        SortPhotoData_Before(GetCamPhotoFile());
    }
    FilmCsrRelease(FILM_CSR_PHOTO);
    FilmCsrRelease(FILM_CSR_SORT);
}

// FF3 0x0022f6e8 (Ghidra had it as GetPlyrEquipmentFilmType)

static int FilmMenuGetStep(void)
{
    return film_menu_step;
}

// FF3 0x0022f6f8

static int FilmMenuSetStep(u_int step)
{
    if (step < FILM_STEP_NUM)
    {
        film_menu_step = (int)step;
    }
    return film_menu_step;
}

// FF3 0x0022f710 (Ghidra had it as GetPlyrOldAreaNo)

static int FilmMenuGetSortOrder(void)
{
    return film_sort_order;
}

// FF3 0x0022f720

static int FilmMenuSetSortOrder(u_int order)
{
    if (order < 2)
    {
        film_sort_order = (int)order;
    }
    return film_sort_order;
}

// ──────────────────────────────────────────────────────────────────────
// The cursors

// FF3 0x0022f740

static void FilmCsrInit(int i)
{
    MenuCsrObjInit(&film_csr[i], film_csr_cfg[i].w, film_csr_cfg[i].h, 0);
}

// FF3 0x0022f788

static void FilmCsrRelease(int i)
{
    MenuCsrObjRelease(&film_csr[i]);
}

// FF3 0x0022f7b0

static void FilmCsrUpdate(int i)
{
    MENU_CSR_WRK *csr = &film_csr[i];

    // PC: the mouse. The slot cursor's sprite moves; the sort cursor's is drawn
    // moved by its hook, so its item stays on the first row.
    if (i == FILM_CSR_PHOTO)
    {
        MenuMouseCsrGrid(csr, film_grid, BpCtlGetHandleByFileNo(FilmMenuFile()));
    }
    else
    {
        ReiPan_MenuBox box;

        if (ReiPan_MenuMouseItemBox(BpCtlGetHandleByFileNo(FilmMenuFile()), film_csr_cfg[i].item, &box))
            MenuMouseCsrOrigin(csr, &box, (float)film_csr_cfg[i].dx, (float)film_csr_cfg[i].dy);
    }
    MenuCsrObjUpdate(csr, 0);
    MenuCsrObjIsMovedSe(csr, FILM_SE_MOVE);
}

// FF3 0x0022f7f8

static short FilmCsrGetPos(int i)
{
    return MenuCsrObjGetPos(&film_csr[i]);
}

// Wraps pos once into the cursor's range. The ROM's range is w + h (not
// w * h): 10 for the slots, 4 for the sort rows; only the sort cursor is
// set this way, from its saved row 0..2. Kept.
// FF3 0x0022f828

static short FilmCsrSetPos(int i, short pos)
{
    short num = (short)(film_csr_cfg[i].w + film_csr_cfg[i].h);

    if (pos < 0)
    {
        pos = (short)(pos + num);
    }
    if (pos >= num)
    {
        pos = (short)(pos - num);
    }
    MenuCsrObjSetPos(&film_csr[i], pos, -1, -1, -1, -1);
    return FilmCsrGetPos(i);
}

// FF3 0x0022f8c8

static short FilmCsrGetCol(int i)
{
    return (short)(FilmCsrGetPos(i) % film_csr_cfg[i].w);
}

// FF3 0x0022f918

static short FilmCsrGetRow(int i)
{
    return (short)(FilmCsrGetPos(i) / film_csr_cfg[i].w);
}

// FF3 0x0022f968

static short FilmCsrGetSaved(int i)
{
    return film_csr_saved[i];
}

// FF3 0x0022f980

static void FilmCsrSave(int i)
{
    film_csr_saved[i] = FilmCsrGetPos(i);
}

// FF3 0x0022f9b8

static short FilmCsrRestore(int i)
{
    return FilmCsrSetPos(i, FilmCsrGetSaved(i));
}

// FF3 0x0022f9e8

static void FilmCsrClearSaved(int i)
{
    film_csr_saved[i] = -1;
}

// FF3 0x0022fa08

static short FilmMenuGetPhotoNum(void)
{
    return (short)GetFilePhotoNum();
}

// FF3 0x0022fa30

static void FilmMenuSortPhotos(short kind, short order)
{
    switch (kind)
    {
    case FILM_SORT_TIME:
        if (order == 0)
        {
            SortPhotoData_NewTime(GetCamPhotoFile());
        }
        else
        {
            SortPhotoData_OldTime(GetCamPhotoFile());
        }
        break;

    case FILM_SORT_SCORE:
        if (order == 0)
        {
            SortPhotoData_BigScore(GetCamPhotoFile());
        }
        else
        {
            SortPhotoData_SmallScore(GetCamPhotoFile());
        }
        break;

    case FILM_SORT_PROTECT:
        if (order == 0)
        {
            SortPhotoData_Protect(GetCamPhotoFile());
        }
        else
        {
            SortPhotoData_NonProtect(GetCamPhotoFile());
        }
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The cursor sprites

// Sprite hook of PHOTO_MENU_PHF item 0x40a: the sort cursor on its row.
// FF3 0x0022fc80

static int FilmSortCsrHook(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS draw = *args;
    int           col  = FilmCsrGetCol(FILM_CSR_SORT);
    int           dx   = film_csr_cfg[FILM_CSR_SORT].dx;
    int           row  = FilmCsrGetRow(FILM_CSR_SORT);

    draw.t1 = args->t1 + col * dx;
    draw.t2 = args->t2 + row * film_csr_cfg[FILM_CSR_SORT].dy;
    B2dDrawSpriteDirect(&draw);
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x0022fb20
// callcheck: operator_new, B2dGridSprCtor (new B2D_GRID_SPR)

static void FilmGridCreate(void)
{
    const FILM_CSR_CFG *cfg = &film_csr_cfg[FILM_CSR_PHOTO];
    B2D_ITEM_HOOK       hook;

    film_grid = new B2D_GRID_SPR(cfg->file + GetSysWrk()->GetLanguage(), cfg->item, cfg->dx, cfg->dy);
    hook.func = FilmSortCsrHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(FilmMenuFile()),
                                     film_csr_cfg[FILM_CSR_SORT].item), &hook);
}

// FF3 0x0022fbe8
// callcheck: B2dGridSprDtor, operator_delete (delete)

static void FilmGridDelete(void)
{
    if (film_grid != nullptr)
    {
        delete film_grid;
    }
    film_grid = nullptr;
}

// FF3 0x0022fc28

static void FilmGridUpdate(void)
{
    if (film_grid != nullptr)
    {
        film_grid->SetPos(FilmCsrGetCol(FILM_CSR_PHOTO), FilmCsrGetRow(FILM_CSR_PHOTO));
    }
}

// ──────────────────────────────────────────────────────────────────────
// The marks

// The photo under the cursor: taken in a photo mode other than 0? A free
// slot asks the camera's current mode.
// FF3 0x0022fd80
// callcheck: PhotoModeGetMode (PHOTO_MODE_WRK::GetMode)

static void FilmModeIconInit(void)
{
    u_char no = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    int    mode;

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) == 0)
    {
        mode = GetPhotoModeWrk()->GetMode();
    }
    else
    {
        mode = GetFilePhotoMode(no);
    }
    film_mode_flag = mode != 0;
}

// FF3 0x0022fde8

static void FilmModeIconRelease(void)
{
}

// Per slot of the 8 x 2 grid: the "photo" and "protected" marks.
// rodata 0x35fdb0 / 0x35fdf0.
static const int film_slot_used_item[FILM_SLOT_COLS * FILM_SLOT_ROWS] =
{
    0x407, 0x451, 0x452, 0x453, 0x454, 0x455, 0x456, 0x457,
    0x458, 0x459, 0x45a, 0x45b, 0x45c, 0x45d, 0x45e, 0x45f,
};
static const int film_slot_protect_item[FILM_SLOT_COLS * FILM_SLOT_ROWS] =
{
    0x4b3, 0x4b5, 0x4b7, 0x4b9, 0x4ba, 0x4bb, 0x4bc, 0x4bd,
    0x4b4, 0x4b6, 0x4b8, 0x4be, 0x4bf, 0x4c0, 0x4c1, 0x4c2,
};

// FF3 0x0022fdf0
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile)

static void FilmMenuDispUpdate(void)
{
    int       handle = BpCtlGetHandleByFileNo(FilmPhotoFile());
    u_char    no     = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    B2D_ITEM *mode0  = B2dCtlFindItem(handle, FILM_ITEM_MODE_0);
    B2D_ITEM *mode1  = B2dCtlFindItem(handle, FILM_ITEM_MODE_1);
    B2D_ITEM *prot   = B2dCtlFindItem(handle, FILM_ITEM_PROTECT);
    short     row;
    short     col;

    B2dSprSetDisp(mode0, 0);
    B2dSprSetDisp(mode1, 0);
    B2dSprSetDisp(prot, 0);
    B2dSprSetDisp(film_mode_flag != 0 ? mode1 : mode0, 1);
    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0 &&
        (GetFilePhotoState(no) & PICTURE_STATUS_PROTECT) != 0)
    {
        B2dSprSetDisp(prot, 1);
    }

    for (row = 0; row < FILM_SLOT_ROWS; row++)
    {
        for (col = 0; col < FILM_SLOT_COLS; col++)
        {
            int       k    = col + row * FILM_SLOT_COLS;
            u_char    slot = (u_char)k;
            B2D_ITEM *used = B2dCtlFindItem(handle, film_slot_used_item[k]);
            B2D_ITEM *mark = B2dCtlFindItem(handle, film_slot_protect_item[k]);

            B2dSprSetDisp(used, 0);
            B2dSprSetDisp(mark, 0);
            if ((GetFilePhotoState(slot) & PICTURE_STATUS_USED) != 0)
            {
                B2dSprSetDisp(used, 1);
                if ((GetFilePhotoState(slot) & PICTURE_STATUS_PROTECT) != 0)
                {
                    B2dSprSetDisp(mark, 1);
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The large picture

// Hook item 0x44f (hook 5: t1 the corners, stack[2] alpha). When the
// cursor has moved it saves the position, refreshes the mode flag (FF3
// inlines FilmModeIconInit here) and unpacks the photo into the work area.
// FF3 0x00230078

static int FilmPhotoHook(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    short      pos   = FilmCsrGetPos(FILM_CSR_PHOTO);
    u_char     no;
    int        mode;

    if (pos != FilmCsrGetSaved(FILM_CSR_PHOTO))
    {
        FilmCsrSave(FILM_CSR_PHOTO);
        no = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
        if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) == 0)
        {
            mode = GetPhotoModeWrk()->GetMode();
        }
        else
        {
            mode = GetFilePhotoMode(no);
        }
        film_mode_flag = mode != 0;
        if ((GetFilePhotoState((u_char)pos) & PICTURE_STATUS_USED) != 0)
        {
            UncompressPhoto((u_char)GetFilePhotoAdrNo((u_char)pos));
        }
    }
    if ((GetFilePhotoState((u_char)pos) & PICTURE_STATUS_USED) != 0)
    {
        DrawPhotoFromWorkArea(0, 0, xy[0], xy[1], 0x15a, 0xe6, alpha);
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00230010
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile)

static void FilmPhotoCreate(void)
{
    B2D_ITEM_HOOK hook;

    hook.func = FilmPhotoHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(FilmPhotoFile()), FILM_ITEM_PHOTO), &hook);
}

// FF3 0x00230070

static void FilmPhotoRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The small pictures

// Hook item 0x460: every used slot's small picture, 0x4c / 0x30 apart.
// FF3 0x002301f0

static int FilmThumbHook(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_char     alpha = (u_char)args->stack[2];
    short      row;
    short      col;
    int        y = 0;

    for (row = 0; row < FILM_SLOT_ROWS; row++)
    {
        int x = 0;

        for (col = 0; col < FILM_SLOT_COLS; col++)
        {
            u_char slot = (u_char)(col + row * FILM_SLOT_COLS);

            if ((GetFilePhotoState(slot) & PICTURE_STATUS_USED) != 0)
            {
                DrawSPhotoFromSmallPhotoArea2(GetFilePhotoAdrNo(slot), 0, 0, xy[0] + x, xy[1] + y,
                                              0x3b, 0x25, alpha);
            }
            x += 0x4c;
        }
        y += 0x30;
    }
    SetDefaultGsDrawState();
    return 0;
}

// FF3 0x00230188
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile)

static void FilmThumbCreate(void)
{
    B2D_ITEM_HOOK hook;

    hook.func = FilmThumbHook;
    B2dCtlSetItemHook(B2dCtlFindItem(BpCtlGetHandleByFileNo(FilmPhotoFile()), FILM_ITEM_THUMBS), &hook);
}

// FF3 0x002301e8

static void FilmThumbRelease(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The photo's details. Text hooks: t1 the item rectangle (rect[0] x,
// rect[1] y, rect[6] the right edge), a1 its character width, t3 the
// colour word, stack[2] alpha, stack[3] flags (0x20 subtractive).

// Item 0x471: the room the photo was taken in.
// FF3 0x00230378

static int FilmRoomHook(const B2D_HOOK_ARGS *args)
{
    u_char no = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0)
    {
        B2dHookPrintMsgLine(0x7a, GetPhotoData(no)->room, 0, (const int *)args->t1, (u_int)args->t3,
                            (int)args->stack[2], (u_int)args->stack[3]);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x43a: the date and time, with the date fields 3 characters apart.
// FF3 0x00230408

static int FilmDateHook(const B2D_HOOK_ARGS *args)
{
    const int *rect  = (const int *)args->t1;
    int        w     = (int)args->a1;
    int        alpha = (int)args->stack[2];
    u_int      flags = (u_int)args->stack[3];
    u_char     no    = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    int        date[CLOCK_DATE_NUM];
    short      x;
    short      y;

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0)
    {
        CdClockBcdToInt(date, &GetPhotoData(no)->time);
        SetCurColorPacked((u_int)args->t3);
        if ((flags & 0x20) != 0)
        {
            MsgSetSubtractMode(1);
        }
        y = (short)rect[1];
        x = (short)rect[0];
#ifdef REIPAN_SLPS_25544_CHS
        // SLPS_255.44 0x229eb0: yy/mm/dd, with "/" from group 0x81.
        constexpr int first = 0, last = 2, separator_group = 0x81;
#else
        constexpr int first = 2, last = 0, separator_group = 0x82;
#endif
        PrintNumber_N(date[first], 2, x,       y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);
        MsgPrintPageMain(separator_group, 6, x + w * 2, y, MSG_COL_CURRENT, alpha, 0, 0, 0, 0);
        PrintNumber_N(date[1], 2, x + w * 3,  y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);     // month
        MsgPrintPageMain(separator_group, 6, x + w * 5, y, MSG_COL_CURRENT, alpha, 0, 0, 0, 0);
        PrintNumber_N(date[last], 2, x + w * 6, y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);
        PrintNumber_N(date[3], 2, x + w * 9,  y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);     // hour
        MsgPrintPageMain(0x67, 3, x + w * 11, y, MSG_COL_CURRENT, alpha, 0, 0, 0, 0);         // ":"
        PrintNumber_N(date[4], 2, x + w * 12, y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);     // minute
        MsgPrintPageMain(0x67, 3, x + w * 14, y, MSG_COL_CURRENT, alpha, 0, 0, 0, 0);
        PrintNumber_N(date[5], 2, x + w * 15, y, MSG_COL_CURRENT, (u_char)alpha, 0, 1, 1);     // second
        MsgSetSubtractMode(0);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x472: the score, 6 digits ending 0x54 left of the item's right edge.
// FF3 0x002306c8

static int FilmScoreHook(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;
    u_char     no   = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    int        pos[2];

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0)
    {
        PICTURE_WRK *pic = GetPhotoData(no);

        pos[0] = rect[6] - 0x54;
        pos[1] = rect[1];
        B2dHookPrintNumber((int)pic->score, 6, 1, pos, (u_int)args->t3, (u_char)args->stack[2],
                           (u_int)args->stack[3]);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x473: the subjects' heading, only over a photo.
// FF3 0x00230770

static int FilmSubjectTitleHook(const B2D_HOOK_ARGS *args)
{
    u_char no = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0)
    {
        B2dHookPrintMsgLine(0x67, 4, 0, (const int *)args->t1, (u_int)args->t3,
                            (int)args->stack[2], (u_int)args->stack[3]);
    }
    SetDefaultGsDrawState();
    return 0;
}

// Item 0x475: the named subjects, one line each, 0x1b apart.
// FF3 0x002307e8

static int FilmSubjectHook(const B2D_HOOK_ARGS *args)
{
    const int *rect = (const int *)args->t1;
    u_char     no   = (u_char)FilmCsrGetPos(FILM_CSR_PHOTO);
    int        pos[2];
    int        i;

    if ((GetFilePhotoState(no) & PICTURE_STATUS_USED) != 0)
    {
        PICTURE_WRK *pic = GetPhotoData(no);

        for (i = 0; i < PICTURE_SUBJECT_MAX; i++)
        {
            const PICTURE_SUBJECT *subject = &pic->subject[i];

            if (subject->type >= 0 && subject->obj_no >= 0)
            {
                pos[0] = rect[0];
                pos[1] = rect[1] + i * 0x1b;
                B2dHookPrintMsgLine(subject->type, SoulListMsgNo(0, subject->obj_no), 0, pos,
                                    (u_int)args->t3, (int)args->stack[2], (u_int)args->stack[3]);
            }
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// rodata 0x35fd50, copied to the stack by FilmInfoCreate.
static const B2D_HOOK_REC film_info_hooks[] =
{
    { FilmRoomHook,         0, FILM_ITEM_ROOM,      nullptr },
    { FilmDateHook,         0, FILM_ITEM_DATE,      nullptr },
    { FilmScoreHook,        0, FILM_ITEM_SCORE,     nullptr },
    { FilmSubjectTitleHook, 0, FILM_ITEM_SUBJ_HEAD, nullptr },
    { FilmSubjectHook,      0, FILM_ITEM_SUBJECTS,  nullptr },
    { nullptr,             -1, -1,                  nullptr },
};

// FF3 0x002302f0
// callcheck: GetSysWrk, GetLanguage (FilmPhotoFile)

static void FilmInfoCreate(void)
{
    B2dSetHookList(BpCtlGetHandleByFileNo(FilmPhotoFile()), film_info_hooks);
}

// FF3 0x00230370

static void FilmInfoRelease(void)
{
}
