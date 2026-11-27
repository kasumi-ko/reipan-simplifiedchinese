/* ==========================================================================
 *  ingame/story_menu_photo.c
 *
 *  The document menu, phase 61 StoryMenu_Photo; see story_menu_photo.h.
 *  FF3 0x1b0628..0x1b24d8 + 0x2b9118 (DocListFill) + 0x2adc78
 *  (DocGetMsgType).
 *
 *  The menu keeps one DOC_LIST and one cursor (MENU_CSR_WRK, 1 x n, a
 *  7-row view) per tab and a step (DocMenuMain): 0 wait for the way in,
 *  1 browse (cursor, the page keys a page, the tab keys or left/right the tab, OK opens
 *  the document, cancel back to the story menu), 2 wait for "next" and
 *  open phase 84 (text) / 85 (the map tab), 3 back from it ("back"), 4/5
 *  the "no documents" message (LayoutMsg 0x5e) when no tab has any.
 *  Drawn by the layout's item hooks: the titles, the new markers, the
 *  picture of the current document (MENU_PIC), plus a scroll bar, the row
 *  cursor and the tab sprites stepped every frame (DocMenuUpdate).
 *
 *  Not written: FF3 0x001b24b8, the GPhaseCtrl singleton's atexit thunk
 *  (as in every file that includes the getter).
 * ======================================================================== */
#include "ingame/story_menu_photo.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer
#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "graphics/graph2d/font.h"      // FontExTexSendVram
#include "graphics/graph2d/message.h"   // GetMsgDataAddr, PrintMsg_ArrangeOneLine, SetCurColorRGB, MsgGetPageBreakNum
#include "graphics/gs_reg.h"            // SetGsRegCached
#include "graphics/motion/anicode.h"    // MenuWdw*, B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState
#include "ingame/ingame.h"              // GPhaseCtrlRequest
#include "ingame/menu/anim_2d.h"        // LayoutAnim*, LayoutMsg*
#include "ingame/menu/menu_csr_obj.h"   // MenuCsrObj*
#include "ingame/menu/menu_draw_tex.h"  // MENU_PIC
#include "ingame/menu/menu_bg.h"      // MenuGetTopPhase, MenuLayoutIsPlaying, MenuScrollArrowDisp
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "main/gphase.h"                // GetGPhaseCtrl
#include "sdk/libgraph.h"               // SCE_GS_TEX1_1
#include "system/b2d/B2dCtl.h"          // B2dCtlFindItem, B2dCtlFindItemByName, B2dCtlSetItemHook
#include "system/b2d/B2dGridSpr.h"      // B2D_GRID_SPR
#include "system/b2d/B2dSpr.h"          // B2dSprSetDisp
#include "system/b2d/BpCtl.h"           // BpCtl*, BpPlayLabel, B2dDrawSpriteDirect
#include "system/b2d/ScrollBar.h"       // B2d_ScrollBar3
#include "system/eeiop/cd_file_dat.h"   // FILE_TOP_PHF
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad_ctrl.h"        // pad_key, CheckPadTrgPlaySe, CheckPadRptPlaySe
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

#define DOC_VIEW_ROWS   7               // rows of the list shown at once
#define DOC_ROW_DY      0x1e            // pixels between rows

#define DOC_SE_MOVE     0x1a9b0000      // cursor moved
#define DOC_SE_CANCEL   0x1a9b0001
#define DOC_SE_BUZZER   0x1a9b0002
#define DOC_SE_OK       0x1a9b0003
#define DOC_SE_PAGE     0x1a9b0005      // tab / page changed

// The menu's steps (DocMenuMain).
enum DOC_MENU_STEP
{
    DOC_STEP_IN    = 0,                 // the way in plays
    DOC_STEP_MAIN  = 1,                 // browse
    DOC_STEP_OPEN  = 2,                 // "next" plays, then phase 84 / 85
    DOC_STEP_BACK  = 3,                 // back from phase 84 / 85
    DOC_STEP_EMPTY = 4,                 // no document at all: the message
    DOC_STEP_EMPTY_WAIT = 5,
    DOC_STEP_NUM   = 6,
};

#define PHASE_STORY_MENU_PHOTO_DETAIL   0x54    // phase 84
#define PHASE_STORY_MENU_PHOTO_MAP      0x55    // phase 85

static void  DocFlagInitSub(void);
static void  DocFlagEndSub(void);
static short DocMenuMain(void);
static void  DocMenuInit(void);
static void  DocMenuRelease(void);
static int   DocMenuGetStep(void);
static int   DocMenuSetStep(u_int step);
static int   DocMenuSetTab(int tab);
static void  DocMenuNextTab(int dir);
static void  DocCsrInit(int tab);
static void  DocCsrRelease(int tab);
static int   DocCsrUpdate(int tab, bool &clicked);
static int   DocCsrPage(int tab, int prev);
static short DocCsrGetPosY(int tab);
static short DocCsrGetTopY(int tab);
static void  DocListCreate(int tab);
static void  DocListDelete(int tab);
static short DocListGetNum(int tab);
static short DocListGet(int tab, short i);
static short DocListGetPageNum(int tab, short i);
static void  DocPicCreate(void);
static void  DocPicDelete(void);
static void  DocBarCreate(void);
static void  DocBarDelete(void);
static void  DocBarDraw(void);
static void  DocCslCreate(void);
static void  DocCslDelete(void);
static void  DocCslUpdate(void);
static void  DocTabDisp(void);
static void  DocMenuSetHooks(void);
static void  DocMenuResetHooks(void);
static void  DocMenuWdwShow(void);
static void  DocWdwSetMsg(void);

// ──────────────────────────────────────────────────────────────────────
// The documents
// ──────────────────────────────────────────────────────────────────────

// data 0x358a10: one entry per key (key == index).
const MSG_PIC_KEY_DAT msg_pic_key_dat[MSG_PIC_KEY_NUM] =
{
    {    0,    1,    0,    0,  284,    3 },
    {    1,    1,    0,    0,  286,    0 },
    {    2,    1,    0,    0,  356,   99 },
    {    3,    1,    0,    0,  302,    6 },
    {    4,    1,    0,    0,  302,  102 },
    {    5,    1,    0,    0,  302,  105 },
    {    6,    1,    0,    0,  288,    9 },
    {    7,    1,    0,    0,  288,   12 },
    {    8,    1,    0,    0,  288,   15 },
    {    9,    1,    0,    0,  288,   18 },
    {   10,    1,    0,    0,  288,   21 },
    {   11,    1,    0,    0,  290,   24 },
    {   12,    1,    0,    0,  296,   27 },
    {   13,    1,    0,    0,  296,   30 },
    {   14,    1,    0,    0,  300,   33 },
    {   15,    1,    0,    0,  300,   36 },
    {   16,    1,    0,    0,  300,   39 },
    {   17,    1,    0,    0,  300,   42 },
    {   18,    1,    0,    0,  308,  108 },
    {   19,    1,    0,    0,  308,   45 },
    {   20,    1,    0,    0,  310,   48 },
    {   21,    1,    0,    0,  310,  111 },
    {   22,    1,    0,    0,  310,   51 },
    {   23,    1,    0,    0,  312,   54 },
    {   24,    1,    0,    0,  314,   57 },
    {   25,    1,    0,    0,  304,   60 },
    {   26,    1,    0,    0,  304,   63 },
    {   27,    1,    0,    0,  304,   66 },
    {   28,    1,    0,    0,  304,   69 },
    {   29,    1,    0,    0,  294,   75 },
    {   30,    1,    0,    0,  294,   78 },
    {   31,    1,    0,    0,  294,   81 },
    {   32,    1,    0,    0,  292,   84 },
    {   33,    1,    0,    0,  292,   87 },
    {   34,    1,    0,    0,  292,   72 },
    {   35,    1,    0,    0,  306,   90 },
    {   36,    1,    0,    0,  356,   93 },
    {   37,    1,    0,    0,  356,   96 },
    {   38,    0,    0,    0,  356,    0 },
    {   39,    0,    0,    0,  322,    9 },
    {   40,    0, 1647,    1,  322,   12 },
    {   41,    0,    0,    0,  298,   15 },
    {   42,    0, 1648,    1,  320,   18 },
    {   43,    0,    0,    0,  320,   21 },
    {   44,    0, 1649,    1,  318,   24 },
    {   45,    0,    0,    0,  340,   27 },
    {   46,    0, 1650,    1,  318,   30 },
    {   47,    0,    0,    0,  336,   33 },
    {   48,    0,    0,    0,  338,   36 },
    {   49,    0,    0,    0,  318,   39 },
    {   50,    0,    0,    0,  332,   42 },
    {   51,    0,    0,    0,  342,   45 },
    {   52,    0,    0,    0,  344,   48 },
    {   53,    0,    0,    0,  318,   51 },
    {   54,    0,    0,    0,  318,   54 },
    {   55,    0,    0,    0,  346,   60 },
    {   56,    0,    0,    0,  348,   75 },
    {   57,    0,    0,    0,  334,   78 },
    {   58,    0, 1651,    1,  324,   66 },
    {   59,    0, 1651,    1,  324,   69 },
    {   60,    0, 1651,    1,  324,   72 },
    {   61,    2,    0,    0,  328,    0 },
    {   62,    2, 1644,    1,  280,    3 },
    {   63,    2,    0,    0,  352,    6 },
    {   64,    2, 1645,    1,  282,    9 },
    {   65,    2,    0,    0,  326,   12 },
    {   66,    2,    0,    0,  354,  153 },
    {   67,    2,    0,    0,  354,   60 },
    {   68,    2,    0,    0,  354,   69 },
    {   69,    2,    0,    0,  246,   18 },
    {   70,    2,    0,    0,  246,   21 },
    {   71,    2,    0,    0,  246,   24 },
    {   72,    2,    0,    0,  246,   27 },
    {   73,    2,    0,    0,  246,   30 },
    {   74,    2,    0,    0,  246,   63 },
    {   75,    2,    0,    0,  350,   33 },
    {   76,    2,    0,    0,  330,   39 },
    {   77,    2,    0,    0,  330,  156 },
    {   78,    2,    0,    0,  330,   72 },
    {   79,    2,    0,    0,  358,   42 },
    {   80,    2,    0,    0,  358,   45 },
    {   81,    2,    0,    0,  366,   48 },
    {   82,    2,    0,    0,  366,   51 },
    {   83,    2,    0,    0,  366,   54 },
    {   84,    2,    0,    0,  366,   57 },
    {   85,    2,    0,    0,  366,   66 },
    {   86,    2,    0,    0,  270,  138 },
    {   87,    2,    0,    0,  270,  126 },
    {   88,    2,    0,    0,  270,  129 },
    {   89,    2,    0,    0,  270,  150 },
    {   90,    2,    0,    0,  270,  132 },
    {   91,    2,    0,    0,  270,  135 },
    {   92,    2,    0,    0,  270,  144 },
    {   93,    2,    0,    0,  270,  147 },
    {   94,    2,    0,    0,  270,  141 },
    {   95,    2,    0,    0,  272,  102 },
    {   96,    2,    0,    0,  272,  105 },
    {   97,    2,    0,    0,  272,   99 },
    {   98,    2,    0,    0,  272,  111 },
    {   99,    2,    0,    0,  272,  108 },
    {  100,    2,    0,    0,  272,  114 },
    {  101,    2,    0,    0,  272,  117 },
    {  102,    2,    0,    0,  272,  120 },
    {  103,    2,    0,    0,  272,  123 },
    {  104,    2,    0,    0,  274,   90 },
    {  105,    2,    0,    0,  276,   93 },
    {  106,    2,    0,    0,  278,   96 },
    {  107,    3,    0,    0, 1344,    0 },
    {  108,    3,    0,    0, 1095,    3 },
    {  109,    3,    0,    0,  501,    6 },
    {  110,    3,    0,    0,  801,    9 },
    {  111,    3,    0,    0, 1362,   12 },
    {  112,    3,    0,    0,  519,   15 },
    {  113,    3,    0,    0,  819,   18 },
    {  114,    3,    0,    0,  450,   21 },
    {  115,    3,    0,    0,  750,   24 },
    {  116,    3,    0,    0,  492,   27 },
    {  117,    3,    0,    0,  792,   30 },
    {  118,    3,    0,    0,  474,   33 },
    {  119,    3,    0,    0,  774,   36 },
    {  120,    3,    0,    0,  495,   39 },
    {  121,    3,    0,    0,  795,   42 },
    {  122,    3,    0,    0,  489,   45 },
    {  123,    3,    0,    0,  789,   48 },
    {  124,    3,    0,    0,  480,   51 },
    {  125,    3,    0,    0,  780,   54 },
    {  126,    3,    0,    0,  486,   60 },
    {  127,    3,    0,    0,  786,   63 },
    {  128,    3,    0,    0,  483,   66 },
    {  129,    3,    0,    0,  783,   69 },
    {  130,    3,    0,    0, 1089,   72 },
    {  131,    3,    0,    0,  471,   75 },
    {  132,    3,    0,    0,  771,   78 },
    {  133,    3,    0,    0,  522,   81 },
    {  134,    3,    0,    0,  822,   84 },
    {  135,    3,    0,    0, 1092,   87 },
    {  136,    3,    0,    0, 1098,   90 },
    {  137,    3,    0,    0,  477,   93 },
    {  138,    3,    0,    0,  777,   96 },
    {  139,    3,    0,    0, 1062,   99 },
    {  140,    3,    0,    0, 1353,  102 },
    {  141,    3,    0,    0, 1365,  105 },
    {  142,    3,    0,    0, 1056,  108 },
    {  143,    3,    0,    0,  510,  111 },
    {  144,    3,    0,    0,  810,  114 },
    {  145,    3,    0,    0,  462,  168 },
    {  146,    3,    0,    0,  762,  171 },
    {  147,    3,    0,    0,  513,  117 },
    {  148,    3,    0,    0,  813,  120 },
    {  149,    3,    0,    0, 1068,  123 },
    {  150,    3,    0,    0,  468,  126 },
    {  151,    3,    0,    0,  768,  129 },
    {  152,    3,    0,    0, 1359,  132 },
    {  153,    3,    0,    0,  507,  135 },
    {  154,    3,    0,    0,  807,  138 },
    {  155,    3,    0,    0,  453,  141 },
    {  156,    3,    0,    0,  753,  144 },
    {  157,    3,    0,    0, 1053,  147 },
    {  158,    3,    0,    0, 1350,  150 },
    {  159,    3,    0,    0, 1047,  153 },
    {  160,    3,    0,    0, 1356,  156 },
    {  161,    3,    0,    0,  444,  159 },
    {  162,    3,    0,    0,  744,  162 },
    {  163,    3,    0,    0, 1347,  165 },
};

// data 0x325198 (zeros in the ROM): DOC_FLAG_* per key; save block 3.
static s_char doc_flag[MSG_PIC_KEY_NUM];

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0628 (IngameInit)

void DocFlagInit(void)
{
    short i;

    DocFlagInitSub();
    for (i = 0; i < MSG_PIC_KEY_NUM; i++)
        doc_flag[i] = DOC_FLAG_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// A new game forgets the maps.
// FF3 0x001b0670 (IngameNewGameReset)

void DocFlagNewGame(void)
{
    short i;

    for (i = 0; i < MSG_PIC_KEY_NUM; i++) {
        if (msg_pic_key_dat[i].kind == DOC_KIND_MAP)
            doc_flag[i] = DOC_FLAG_NONE;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b06c0 (IngameEnd)

void DocFlagEnd(void)
{
    DocFlagEndSub();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b06d8 (FILE_GET, the event queue's rewards, photos, bought extras)

int DocFlagSet(short key)
{
    if ((u_short)key < MSG_PIC_KEY_NUM && doc_flag[key] == DOC_FLAG_NONE) {
        doc_flag[key] = DOC_FLAG_NEW;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0710 (FILE_LOST)

int DocFlagClear(short key)
{
    if ((u_short)key < MSG_PIC_KEY_NUM && doc_flag[key] != DOC_FLAG_NONE) {
        doc_flag[key] = DOC_FLAG_NONE;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0748 (the views of phases 84 / 85)

int DocFlagSetRead(short key)
{
    if ((u_short)key < MSG_PIC_KEY_NUM && doc_flag[key] != DOC_FLAG_NONE) {
        doc_flag[key] = DOC_FLAG_READ;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0780 (save block table 0x332acc)

int DocFlagSaveGet(void **addr)
{
    *addr = doc_flag;
    return MSG_PIC_KEY_NUM;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0798 / 0x001b07a0: empty (jr ra), called by DocFlagInit /
// DocFlagEnd.

static void DocFlagInitSub(void)
{
}

// FF3 0x001b07a0
static void DocFlagEndSub(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b07a8 (no caller): a loop over the flags whose body is empty
// (the ROM keeps it, unrolled four times with nops).

void DocFlagDummy(void)
{
    short i;

    for (i = 0; i < MSG_PIC_KEY_NUM; i++) {
    }
}

// ──────────────────────────────────────────────────────────────────────
// The message type of a document.
// FF3 0x002adc78 (out of line in the linkonce run)

int DocGetMsgType(short key, int part)
{
    (void)part;

    if ((u_short)key >= MSG_PIC_KEY_NUM)
        return 0;
    switch (msg_pic_key_dat[key].kind) {
    case DOC_KIND_SIRYOU:
        return 0x1d;
    case DOC_KIND_KOSYO:
        return 0x1e;
    case DOC_KIND_TEGAMI:
        return 0x1c;
    case DOC_KIND_MAP:
        return 0x1f;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// DOC_LIST
// FF3 0x001b07d8: the base-object constructor, the same body as
// FF3 0x001b0808 (DocListCtor, the complete-object one)

DOC_LIST::DOC_LIST(short kind, int all)
{
    this->all  = all;
    num        = 0;
    this->kind = kind;
    list       = nullptr;
    Build();
}

// FF3 0x001b0838: the base-object destructor, the same body as
// FF3 0x001b0850 (DocListDtor)
DOC_LIST::~DOC_LIST()
{
    Free();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0868

void DOC_LIST::Build()
{
    short n = Fill(0);

    if (n != 0) {
        list = (short *)heapCtrlMalloc(GetSystemHeapWrkP(), n * sizeof(short), 0, 0, 0);
        if (list != nullptr)
            num = Fill(1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b08d8

void DOC_LIST::Free()
{
    if (list != nullptr) {
        heapCtrlFree(GetSystemHeapWrkP(), list, 0, 0, 0);
        list = nullptr;
    }
    num = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0928 (no caller): empty (jr ra).

void DocListDummy(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The documents of the kind, held ones only unless all; with store (and a
// list) written into it.
// FF3 0x002b9118 (out of line in the linkonce run; DocListBuild)

short DOC_LIST::Fill(int store)
{
    short n = 0;
    short i;

    for (i = 0; i < MSG_PIC_KEY_NUM; i++) {
        if (all == 0 && doc_flag[i] == DOC_FLAG_NONE)
            continue;
        if (kind != msg_pic_key_dat[i].kind)
            continue;
        if (store != 0 && list != nullptr)
            list[n] = i;
        n++;
    }
    return n;
}

// ──────────────────────────────────────────────────────────────────────
// The menu
// ──────────────────────────────────────────────────────────────────────

static int           doc_menu_step;                 // bss 0x3e77e0: DOC_MENU_STEP
static int           doc_menu_tab;                  // bss 0x3e77e4: DOC_KIND_*
static MENU_CSR_WRK  doc_csr[DOC_KIND_NUM];         // bss 0x3e77e8 (0x3c each)
static DOC_LIST     *doc_list[DOC_KIND_NUM];        // bss 0x3e78d8
static int           doc_wdw_anim;                  // sdata 0x3703b0: the menu window (layout type 8)
static int           doc_msg_anim;                  // sdata 0x3703c4: the "no documents" message (type 3)
static MENU_PIC       *doc_pic;                     // sbss gp-0x328
static B2d_ScrollBar3 *doc_bar;                     // sbss gp-0x324
static B2D_GRID_SPR   *doc_csl;                     // sbss gp-0x320: the row cursor

// The menu layout (FILE_TOP_PHF + language), as every use computes it.
static int DocMenuFile(void)
{
    return GetSysWrk()->GetLanguage() + FILE_TOP_PHF;
}

static int DocMenuHandle(void)
{
    return BpCtlGetHandleByFileNo(DocMenuFile());
}

// ──────────────────────────────────────────────────────────────────────
// Phase 61 StoryMenu_Photo
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001b0930. Step 0: lists and cursors, the layout and the menu window
// start loading; step 1: once both are loaded, "in", the fade layer on item
// 0x430 (label 2 when the menu is empty), the window caption, the picture,
// scroll bar, row cursor and item hooks.
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

int init_StoryMenu_Photo(void)
{
    int handle;

    switch (GetGPhaseCtrl()->leaf->state_cnt) {
    case 0:
        BpCtlMain();
        DocMenuInit();
        BpCtlLoadReq(DocMenuFile(), nullptr);
        doc_wdw_anim = LayoutAnimStart(8, 0);
        LayoutAnimSetFlag(doc_wdw_anim, LAYOUT_ANIM_WAIT | LAYOUT_ANIM_LABEL_LAYOUT);
        MenuWdwSetKeepOpen(1);
        MenuWdwSetYesNo(0, 0);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        break;

    case 1:
        BpCtlMain();
        if (BpCtlIsDataReady(DocMenuHandle()) == 0)
            return 0;
        if (LayoutAnimIsDataReady(doc_wdw_anim) == 0)
            break;
        LayoutAnimClearFlag(doc_wdw_anim, LAYOUT_ANIM_WAIT);
        BpPlayLabel(DocMenuHandle(), "in");                     /* sdata 0x3703b8 */
        handle = DocMenuHandle();
        GetCmnFadeLayer()->SetItem(handle, 0x430);
        GetCmnFadeLayer()->Show(DocMenuGetStep() != DOC_STEP_EMPTY ? 1 : 2);
        DocMenuWdwShow();
        DocMenuWdwUpdate();
        DocPicCreate();
        DocBarCreate();
        DocCslCreate();
        DocMenuSetHooks();
        return 1;

    case 2:
        DocMenuUpdate();
        return MenuLayoutIsPlaying(DocMenuFile()) == 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0ba0: "out", then once it has played everything is released.
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

int end_StoryMenu_Photo(void)
{
    DocMenuUpdate();
    switch (GetGPhaseCtrl()->leaf->state_cnt) {
    case 0:
        BpPlayLabel(DocMenuHandle(), "out");                    /* sdata 0x3703c0 */
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        break;

    case 1:
        if (MenuLayoutIsPlaying(DocMenuFile()) != 0)
            return 0;
        DocPicDelete();
        DocBarDelete();
        DocCslDelete();
        DocMenuResetHooks();
        LayoutAnimSetAlphaItem(doc_wdw_anim, nullptr);
        MenuWdwSetKeepOpen(0);
        LayoutAnimReset(doc_wdw_anim);
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        BpCtlRelease(DocMenuHandle());
        DocMenuRelease();
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b0d10

int one_StoryMenu_Photo(void)
{
    short next;

    DocMenuUpdate();
    next = DocMenuMain();
    if (next != -1)
        GPhaseCtrlRequest(next, 0);
    return next != -1;
}

// FF3 0x001b0d58 / 0x001b0d60: empty.
void pre_StoryMenu_Photo(void)
{
}

// FF3 0x001b0d60
void after_StoryMenu_Photo(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Every frame of phases 61 / 84 / 85: the row cursor, scroll bar and tabs,
// the fade layer, the layout animations and the Bp layouts.
// FF3 0x001b0d68

void DocMenuUpdate(void)
{
    DocCslUpdate();
    DocBarDraw();
    DocTabDisp();
    GetCmnFadeLayer()->Update();
    LayoutAnimMain();
    BpCtlMain();
}

// ──────────────────────────────────────────────────────────────────────
// The steps (see the file comment); returns the phase to go to, -1 none.
// FF3 0x001b0db0
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

static short DocMenuMain(void)
{
    int tab   = DocMenuGetTab();
    int prev;
    short num;
    bool clicked = false;

    switch (DocMenuGetStep()) {
    case DOC_STEP_IN:
        if (MenuLayoutIsPlaying(DocMenuFile()) != 0)
            return -1;
        DocMenuSetStep(DOC_STEP_MAIN);
        // fall through
    case DOC_STEP_MAIN:
        DocMenuWdwUpdate();
        num = DocListGetNum(tab);
        // PC: the mouse. Each tab has its label, lit or not.
        {
            static const char *const tabs[4][2] =
            {
                { "R siryou_on", "R siryou_off" }, { "R kosyo_on", "R kosyo_off" },
                { "R tegami_on", "R tegami_off" }, { "R map_on", "R map_off" },
            };

            for (int i = 0; i < 4; i++)
            {
                ReiPan_MenuBox box;

                if (ReiPan_MenuMouseNameBox(DocMenuHandle(), tabs[i][0], &box)
                    || ReiPan_MenuMouseNameBox(DocMenuHandle(), tabs[i][1], &box))
                    MenuMouseTab(tab, i, &box, REIPAN_MENU_LEFT, REIPAN_MENU_RIGHT);
            }
        }
        if (DocCsrUpdate(tab, clicked) != 0)
            return -1;
        if (num > DOC_VIEW_ROWS) {
            if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_PREV], -1, 0)) {
                SndSePlay(DOC_SE_PAGE, 100, 0x1000, 0, 0, 0);
                DocCsrPage(tab, 1);
                return -1;
            }
            if (CheckPadRptPlaySe(pad_key[PAD_KEY_PAGE_NEXT], -1, 0)) {
                SndSePlay(DOC_SE_PAGE, 100, 0x1000, 0, 0, 0);
                DocCsrPage(tab, 0);
                return -1;
            }
        }
        if (CheckPadRptPlaySe(pad_key[PAD_KEY_TAB_PREV] | 0x8000, -1, 0)) {
            DocMenuSetTab(tab - 1);
            prev = 1;
        } else if (CheckPadRptPlaySe(pad_key[PAD_KEY_TAB_NEXT] | 0x2000, -1, 0)) {
            DocMenuSetTab(tab + 1);
            prev = 0;
        } else if (clicked || CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0)) {
            // A document without text pages cannot be opened (maps have none).
            if (tab != DOC_KIND_MAP && DocListGetPageNum(tab, (short)DocCsrGetIndex(tab)) < 1) {
                SndSePlay(DOC_SE_BUZZER, 100, 0x1000, 0, 0, 0);
                return -1;
            }
            SndSePlay(DOC_SE_OK, 100, 0x1000, 0, 0, 0);
            BpPlayLabel(DocMenuHandle(), "next");               /* sdata 0x3703c8 */
            DocMenuSetStep(DOC_STEP_OPEN);
            return -1;
        } else if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], -1, 0)) {
            SndSePlay(DOC_SE_CANCEL, 100, 0x1000, 0, 0, 0);
            return MenuGetTopPhase();
        } else {
            return -1;
        }
        // The tab moved: on to the first one with documents that way.
        DocMenuNextTab(prev);
        SndSePlay(tab == DocMenuGetTab() ? DOC_SE_BUZZER : DOC_SE_PAGE, 100, 0x1000, 0, 0, 0);
        return -1;

    case DOC_STEP_OPEN:
        if (MenuLayoutIsPlaying(DocMenuFile()) != 0)
            return -1;
        DocMenuSetStep(DOC_STEP_BACK);
        return tab != DOC_KIND_MAP ? PHASE_STORY_MENU_PHOTO_DETAIL : PHASE_STORY_MENU_PHOTO_MAP;

    case DOC_STEP_BACK:
        BpPlayLabel(DocMenuHandle(), "back");                   /* sdata 0x3703d0 */
        DocMenuWdwShow();
        DocMenuWdwUpdate();
        GetCmnFadeLayer()->SetItem(DocMenuHandle(), 0x430);
        GetCmnFadeLayer()->Show(1);
        DocMenuSetStep(DOC_STEP_IN);
        return -1;

    case DOC_STEP_EMPTY:
        GetCmnFadeLayer()->Switch(2, 0x1e);
        SndSePlay(DOC_SE_BUZZER, 100, 0x1000, 0, 0, 0);
        doc_msg_anim = LayoutAnimStart(3, 0);
        LayoutMsgSet(0x5e, 0);
        LayoutMsgSetType(2);
        LayoutMsgSetArg(1);
        LayoutAnimClearFlag(doc_msg_anim, LAYOUT_ANIM_HOLD);
        DocMenuSetStep(DOC_STEP_EMPTY_WAIT);
        // fall through
    case DOC_STEP_EMPTY_WAIT:
        if (LayoutAnimGetState(doc_msg_anim) != 4)
            return -1;
        SndSePlay(DOC_SE_CANCEL, 100, 0x1000, 0, 0, 0);
        LayoutAnimReset(doc_msg_anim);
        return MenuGetTopPhase();
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1258: the lists, then the cursors (sized by them), then the
// first tab with documents.

static void DocMenuInit(void)
{
    int i;

    for (i = 0; i < DOC_KIND_NUM; i++)
        DocListCreate(i);
    for (i = 0; i < DOC_KIND_NUM; i++)
        DocCsrInit(i);
    doc_menu_step = DOC_STEP_IN;
    doc_menu_tab  = 0;
    DocMenuNextTab(0);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b12c0

static void DocMenuRelease(void)
{
    int i;

    for (i = 0; i < DOC_KIND_NUM; i++)
        DocCsrRelease(i);
    for (i = 0; i < DOC_KIND_NUM; i++)
        DocListDelete(i);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1310 / 0x001b1320

static int DocMenuGetStep(void)
{
    return doc_menu_step;
}

// FF3 0x001b1320
static int DocMenuSetStep(u_int step)
{
    if (step < DOC_STEP_NUM)
        doc_menu_step = (int)step;
    return doc_menu_step;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1340 / 0x001b1350 (wraps into 0..3)

short DocMenuGetTab(void)
{
    return (short)doc_menu_tab;
}

// FF3 0x001b1350
static int DocMenuSetTab(int tab)
{
    while (tab < 0)
        tab += DOC_KIND_NUM;
    while (tab >= DOC_KIND_NUM)
        tab -= DOC_KIND_NUM;
    doc_menu_tab = tab;
    return tab;
}

// ──────────────────────────────────────────────────────────────────────
// From the current tab on (backwards when prev), the first tab that has
// documents; none at all: the "no documents" step.
// FF3 0x001b13c0

static void DocMenuNextTab(int prev)
{
    int cur = DocMenuGetTab();
    int i   = 0;
    int tab;

    for (;;) {
        tab = (prev == 0) ? cur + i : cur - i;
        while (tab < 0)
            tab += DOC_KIND_NUM;
        while (tab >= DOC_KIND_NUM)
            tab -= DOC_KIND_NUM;
        i++;
        if (DocListGetNum(tab) > 0)
            break;
        if (i >= DOC_KIND_NUM) {
            DocMenuSetStep(DOC_STEP_EMPTY);
            return;
        }
    }
    DocMenuSetTab(tab);
}

// ──────────────────────────────────────────────────────────────────────
// The tab's cursor: one column, a row per document (one when empty), a
// 1 x 7 view.
// FF3 0x001b1500

static void DocCsrInit(int tab)
{
    MENU_CSR_WRK *wrk = &doc_csr[tab];
    short num = DocListGetNum(tab);

    MenuCsrObjInit(wrk, 1, (num != 0) ? num : 1, 0);
    MenuCsrObjSetViewX(wrk, 1, 0);
    MenuCsrObjSetViewY(wrk, DOC_VIEW_ROWS, 0);
}

// FF3 0x001b1578
static void DocCsrRelease(int tab)
{
    MenuCsrObjRelease(&doc_csr[tab]);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b15a0: 1 when the cursor moved (its SE played).

static int DocCsrUpdate(int tab, bool &clicked)
{
    MENU_CSR_WRK *wrk = &doc_csr[tab];
    const int handle = DocMenuHandle();
    const short top = DocCsrGetTopY(tab);
    const short num = DocListGetNum(tab);
    ReiPan_MenuBox left_edge, right_edge;

    ReiPan_MenuMouseBegin(0, nullptr);
    // The row dividers stay put while the selected cursor sprite moves.
    if (ReiPan_MenuMouseItemBox(handle, 0x42a, &left_edge) &&
        ReiPan_MenuMouseItemBox(handle, 0x432, &right_edge))
    {
        ReiPan_MenuBox box = {left_edge.x, left_edge.y,
                              right_edge.x + right_edge.w - left_edge.x, DOC_ROW_DY};
        for (short row = 0; row < DOC_VIEW_ROWS && top + row < num; row++, box.y += DOC_ROW_DY)
        {
            const short index = (short)(top + row);
            if (ReiPan_MenuMouseClick(box.x, box.y, box.w, box.h))
            {
                MenuCsrObjSetPos(wrk, index, 0, row, -1, -1);
                clicked = true;
                return 0;
            }
            if (MenuMouseCsrWork(wrk, index, &box))
                break;
        }
    }

    MenuCsrObjUpdate(wrk, 0);
    return MenuCsrObjIsMovedSe(wrk, DOC_SE_MOVE);
}

// FF3 0x001b15e8
static int DocCsrPage(int tab, int prev)
{
    if (prev != 0)
        return MenuCsrObjPagePrevY(&doc_csr[tab]);
    return MenuCsrObjPageNextY(&doc_csr[tab]);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1628 (was the FF2 match FinderInCameraCtrl)

short DocMenuGetNum(int tab)
{
    return DocListGetNum(tab);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1648

int DocCsrGetIndex(int tab)
{
    MENU_CSR_WRK *wrk = &doc_csr[tab];
    int row = MenuCsrObjGetViewPosY(wrk);

    return row + MenuCsrObjGetTopY(wrk);
}

// ──────────────────────────────────────────────────────────────────────
// The cursor on list index (negative: the last, past the end: the first),
// the view kept full when the list allows. Returns the index now selected.
// FF3 0x001b16a0

short DocCsrSetIndex(int tab, int index)
{
    MENU_CSR_WRK *wrk = &doc_csr[tab];
    int row  = DocCsrGetPosY(tab);
    int num  = DocListGetNum(tab);
    int page = (short)((num < DOC_VIEW_ROWS + 1) ? num : DOC_VIEW_ROWS);

    if (index < 0) {
        index = num - 1;
        row   = (short)(page - 1);
    }
    if (index >= num) {
        index = 0;
        row   = 0;
    } else if (index < row) {
        row = (short)index;
    }
    if (num - index < page - row)
        row = (short)(page - (num - index));
    MenuCsrObjSetPos(wrk, (short)index, 0, row, -1, -1);
    return MenuCsrObjGetPos(wrk);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b17a0 / 0x001b17d0: the cursor's row in the view, the first row
// shown.

static short DocCsrGetPosY(int tab)
{
    return MenuCsrObjGetViewPosY(&doc_csr[tab]);
}

// FF3 0x001b17d0
static short DocCsrGetTopY(int tab)
{
    return MenuCsrObjGetTopY(&doc_csr[tab]);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1800 / 0x001b1870
// callcheck: operator_new, DocListCtor (new)

static void DocListCreate(int tab)
{
    doc_list[tab] = new DOC_LIST((short)tab, 0);
}

// FF3 0x001b1870
// callcheck: DocListDtor, operator_delete (delete)
static void DocListDelete(int tab)
{
    if (doc_list[tab] != nullptr)
        delete doc_list[tab];
    doc_list[tab] = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b18c8 / 0x001b18e8 (-1 outside the list)

static short DocListGetNum(int tab)
{
    return doc_list[tab]->num;
}

// FF3 0x001b18e8
static short DocListGet(int tab, short i)
{
    DOC_LIST *list = doc_list[tab];

    if (list == nullptr || i < 0 || i >= list->num)
        return -1;
    return list->list[i];
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1938 / 0x001b1970 (no callers): document i's pictures.

short DocListGetPic(int tab, short i)
{
    const short key = DocListGet(tab, i);
    return key < 0 ? -1 : msg_pic_key_dat[key].pic;
}

// FF3 0x001b1970
short DocListGetPicNum(int tab, short i)
{
    const short key = DocListGet(tab, i);
    return key < 0 ? 0 : msg_pic_key_dat[key].pic_num;
}

// ──────────────────────────────────────────────────────────────────────
// The page breaks of document i's text (msg1 + 2); below 1: nothing to
// read. FF3 0x001b19a8

static short DocListGetPageNum(int tab, short i)
{
    short key  = DocListGet(tab, i);
    if (key < 0)
        return 0;
    short msg1 = msg_pic_key_dat[key].msg1;

    return (short)MsgGetPageBreakNum(DocGetMsgType(key, 2), (short)(msg1 + 2));
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1a10

short DocMenuGetCurDoc(void)
{
    int tab = DocMenuGetTab();

    return DocListGet(tab, (short)DocCsrGetIndex(tab));
}

// ──────────────────────────────────────────────────────────────────────
// The current document's picture on item 0x435 (alpha from the item).
// FF3 0x001b1b78

static int DocPicHook(const B2D_HOOK_ARGS *args)
{
    int   tab = DocMenuGetTab();
    short key = DocListGet(tab, (short)DocCsrGetIndex(tab));
    float scale;
    MENU_PIC *pic;

    if (key != -1 && doc_pic != nullptr) {
        scale = (float)(int)args->stack[2] * (1.0f / 128.0f);  /* sdata 0x3703d8 */
        doc_pic->pic = msg_pic_key_dat[key].menu_pic;
        doc_pic->Step();
        doc_pic->scale = scale;
        pic = doc_pic;
        pic->Draw();
        pic->DrawNext();
        SetDefaultGsDrawState();
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1a48: the picture at item 0x435's place, 256 x 256.
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)
// callcheck: operator_new, MenuPicCtor (new)

static void DocPicCreate(void)
{
    B2D_ITEM *item = B2dCtlFindItem(DocMenuHandle(), 0x435);
    short x = (short)item->part[B2D_PART_X].value;
    short y = (short)item->part[B2D_PART_Y].value;
    B2D_ITEM_HOOK hook;

    doc_pic = new MENU_PIC(0);
    doc_pic->v      = 0;
    doc_pic->u      = 0;
    doc_pic->h      = 0x100;
    doc_pic->w      = 0x100;
    doc_pic->x      = x;
    doc_pic->y      = y;
    doc_pic->pri    = 0xa0;
    doc_pic->z      = 0;
    doc_pic->enable = 1;
    hook.func = DocPicHook;
    B2dCtlSetItemHook(item, &hook);
}

// FF3 0x001b1b38
// callcheck: MenuPicDtor, operator_delete (delete)
static void DocPicDelete(void)
{
    if (doc_pic != nullptr)
        delete doc_pic;
    doc_pic = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1c30 / 0x001b1ca8
// callcheck: GetSysWrk, GetLanguage (DocMenuFile)
// callcheck: operator_new (new B2d_ScrollBar3)

static void DocBarCreate(void)
{
    doc_bar = new B2d_ScrollBar3(DocMenuFile(), "slider", "R sld_U", "R sld_M", "R sld_B");  /* sdata 0x3703e0.. */
}

// FF3 0x001b1ca8
static void DocBarDelete(void)
{
    if (doc_bar != nullptr)
        delete doc_bar;
    doc_bar = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The scroll bar over the current tab's list and the up / down arrows.
// FF3 0x001b1ce0
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

static void DocBarDraw(void)
{
    int   tab;
    short top, row, num;
    int   handle;

    if (doc_bar == nullptr)
        return;
    tab    = DocMenuGetTab();
    top    = DocCsrGetTopY(tab);
    row    = DocCsrGetPosY(tab);
    num    = DocListGetNum(tab);
    handle = DocMenuHandle();
    doc_bar->SetRange(num, DOC_VIEW_ROWS);
    doc_bar->Draw(row, top);
    MenuScrollArrowDisp(handle, 0x42b, 0x3f8, 0x42c, 0x3f7, doc_bar->CanScrollUp(), doc_bar->CanScrollDown());
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1e08 / 0x001b1e70 / 0x001b1eb0: the row cursor "csl", 30 px a
// callcheck: GetSysWrk, GetLanguage (DocMenuFile)
// callcheck: operator_new, B2dGridSprCtorByName2 (new B2D_GRID_SPR)
// row, on the cursor's view row.

static void DocCslCreate(void)
{
    doc_csl = new B2D_GRID_SPR(DocMenuFile(), "csl", 0, DOC_ROW_DY);  /* sdata 0x370400 */
}

// FF3 0x001b1e70
// callcheck: B2dGridSprDtor, operator_delete (delete)
static void DocCslDelete(void)
{
    if (doc_csl != nullptr)
        delete doc_csl;
    doc_csl = nullptr;
}

// FF3 0x001b1eb0
static void DocCslUpdate(void)
{
    if (doc_csl != nullptr)
        doc_csl->SetPos(0, DocCsrGetPosY(DocMenuGetTab()));
}

// ──────────────────────────────────────────────────────────────────────
// The tab sprites: "on" for the current tab, "off" for the others.
// FF3 0x001b1ef8

// data 0x325240
static const char *const doc_tab_name[DOC_KIND_NUM][2] =
{
    { "tab_siryou_on", "tab_siryou_off" },
    { "tab_kosyo_on",  "tab_kosyo_off"  },
    { "tab_tegami_on", "tab_tegami_off" },
    { "tab_map_on",    "tab_map_off"    },
};

static void DocTabDisp(void)
{
    int tab    = DocMenuGetTab();
    int handle = DocMenuHandle();
    int i;

    for (i = 0; i < DOC_KIND_NUM; i++) {
        B2D_ITEM *on  = B2dCtlFindItemByName(handle, doc_tab_name[i][0]);
        B2D_ITEM *off = B2dCtlFindItemByName(handle, doc_tab_name[i][1]);

        B2dSprSetDisp(on, i == tab);
        B2dSprSetDisp(off, i != tab);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The item hooks of the menu layout
// ──────────────────────────────────────────────────────────────────────

// Item 0x43d (a text item): the titles of the 7 rows shown, the cursor's
// row in the highlight colour.
// FF3 0x001b2070

static int DocNameHook(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    int        alpha = (int)args->stack[2];
    int        tab   = DocMenuGetTab();
    short      top   = DocCsrGetTopY(tab);
    short      row   = DocCsrGetPosY(tab);
    int        i, dy;

    for (i = 0, dy = 0; i < DOC_VIEW_ROWS; i++, dy += DOC_ROW_DY) {
        short key = DocListGet(tab, (short)(i + top));

        if (key != -1) {
            u_char *str = GetMsgDataAddr(DocGetMsgType(key, 0), msg_pic_key_dat[key].msg1);

            if (str != nullptr) {
                FontExTexSendVram();
                SetCurColorRGB(0x19, 0x1b, 0x1f);
                if (i == row)
                    SetCurColorRGB(0xf1, 0xd6, 0x9b);
                PrintMsg_ArrangeOneLine(str, xy[0], xy[1] + dy, 0x2d, alpha, 0xa0, 0, 0, 0);
            }
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Items 0x447 / 0x45d: the item's sprite on every row shown whose document
// is new.
// FF3 0x001b21c8

static int DocNewHook(const B2D_HOOK_ARGS *args)
{
    B2D_HOOK_ARGS a   = *args;
    int           tab = DocMenuGetTab();
    short         top = DocCsrGetTopY(tab);
    int           i;

    for (i = 0; i < DOC_VIEW_ROWS; i++) {
        short key = DocListGet(tab, (short)(i + top));

        if (key != -1 && doc_flag[key] == DOC_FLAG_NEW)
            B2dDrawSpriteDirect(&a);
        a.t2 += DOC_ROW_DY;
    }
    SetDefaultGsDrawState();
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// "R ttlL" / "R ttlR": the sprite with bilinear filtering (TEX1 MMAG / MMIN
// linear).
// FF3 0x001b22f0

static int DocTitleHook(const B2D_HOOK_ARGS *args)
{
    int ret;

    SetGsRegCached(0x100000161ULL, SCE_GS_TEX1_1, 1);
    ret = B2dDrawSpriteDirect(args);
    SetDefaultGsDrawState();
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001b1fe8 (the list, rodata 0x359258, is copied onto the stack)
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

static void DocMenuSetHooks(void)
{
    static const B2D_HOOK_REC hooks[6] =
    {
        { DocNameHook,  0,  0x43d, nullptr  },
        { DocNewHook,   0,  0x447, nullptr  },
        { DocNewHook,   0,  0x45d, nullptr  },
        { DocTitleHook, 0,  0,     "R ttlL" },                  /* sdata 0x370408 */
        { DocTitleHook, 0,  0,     "R ttlR" },                  /* sdata 0x370410 */
        { nullptr,      -1, -1,    nullptr  },
    };

    B2dSetHookList(DocMenuHandle(), hooks);
}

// FF3 0x001b2068: empty (jr ra).
static void DocMenuResetHooks(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The menu window
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001b23d0
int DocMenuGetWdwAnim(void)
{
    return doc_wdw_anim;
}

// FF3 0x001b23d8
void DocMenuWdwUpdate(void)
{
    DocWdwSetMsg();
}

// ──────────────────────────────────────────────────────────────────────
// The window's alpha follows the layout's "wasi" item.
// FF3 0x001b23f0
// callcheck: GetSysWrk, GetLanguage, BpCtlGetHandleByFileNo (DocMenuFile / DocMenuHandle)

static void DocMenuWdwShow(void)
{
    LayoutAnimSetAlphaItemByName(doc_wdw_anim, DocMenuHandle(), "wasi");    /* sdata 0x370418 */
}

// ──────────────────────────────────────────────────────────────────────
// The caption of the current document (msg1 + 1), none without one.
// FF3 0x001b2438

static void DocWdwSetMsg(void)
{
    short key = DocMenuGetCurDoc();

    if (key == -1)
        MenuWdwSetMsg(-1, -1);
    else
        MenuWdwSetMsg(DocGetMsgType(key, 1), (short)(msg_pic_key_dat[key].msg1 + 1));
}
