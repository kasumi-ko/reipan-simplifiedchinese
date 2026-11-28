/* ==========================================================================
 *  ingame/puzzle/sizumedama/sizumedama_pzl.c
 *
 *  The sizumedama puzzle's hooks and phases 75 / 100..103 / 116 / 117 (see
 *  sizumedama_pzl.h). FF3 0x24b490..0x24c1c0, FF3's own code.
 * ======================================================================== */
#include "ingame/puzzle/sizumedama/sizumedama_pzl.h"

#include "common/WindowMsg.h"                           // CWindowMsg
#include "common/cmn_fade_layer.h"                      // GetCmnFadeLayer, capCtl::Term
#include "ingame/ingame.h"                              // PHASE12_PARAM, Phase12ParamSet, Phase12FadeLayerOn
#include "ingame/menu/anim_2d.h"                        // LayoutAnimMain
#include "ingame/menu/menu_csr.h"                       // MenuCsr*
#include "ingame/puzzle/puzzle.h"                       // PuzzleGetIdx / GetLayoutFile / GetRetPhase / Clear
#include "ingame/puzzle/sizumedama/sizumedama_draw.h"   // SizumeGetBoardDef, SizumeDrawInit, SizumeDraw
#include "ingame/sound/snd_ctrl.h"                      // SndSePlay
#include "main/gphase.h"                                // GetGPhaseCtrl, GPHASE_CTRL::SetNext
#include "system/b2d/BpCtl.h"                           // BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, BpCtlRelease, BpPlayLabel
#include "system/pad/pad_ctrl.h"                        // CheckPadTrgPlaySe, pad_key
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

SIZUME_BOARD *sizume_board;                     // sdata 0x371fa0
CWindowMsg   *sizume_win;                       // sbss 0x3738c8

static const char sizume_label_out[]          = "out";             // sdata 0x371fa8
static const char sizume_label_daiza_isi[]    = "daiza_isi";       // data 0x361e68: pedestal -> stones
static const char sizume_label_daiza_saisei[] = "daiza_saisei";    // data 0x361e78: pedestal -> playback
static const char sizume_label_isi_saisei[]   = "isi_saisei";      // data 0x361e88: stone put -> playback
static const char sizume_label_isi_daiza[]    = "isi_daiza";       // data 0x361e98: stones -> pedestal
static const char sizume_label_seikai_next[]  = "seikai_next";     // data 0x361ea8: after the solved playback
static const char sizume_label_saisei_daiza[] = "saisei_daiza";    // data 0x361eb8: playback -> pedestal
static const char sizume_label_seikai[]       = "seikai";          // sdata 0x371fb8: the solving stone put
static const char sizume_item_bg[]            = "BG";              // sdata 0x371fb0
static const char sizume_item_cap_1[]         = "R cap_1";         // sdata 0x371fc8: playback of one stone
static const char sizume_item_cap_2[]         = "R cap_2";         // sdata 0x371fc0: the solved playback

#define SIZUME_MOUSE_BASE       0x3f5   // PC: "R base" of the first pedestal; the others follow
#define SIZUME_CSR_DAIZA        100     // MenuCsr ids: the pedestals
#define SIZUME_CSR_STONE        0x65    //   and the stones

#define SIZUME_MSG              0x78    // phase 12's / the window's message group
#define SIZUME_MSG_RULES        9
#define SIZUME_MSG_GUIDE        10      // the window's guide line under the board
#define SIZUME_MSG_TAKE         11      // take the stone back?
#define SIZUME_MSG_QUIT         12
#define SIZUME_MSG_STONE        13      // choose a stone
#define SIZUME_MSG_SOLVED       14

#define SIZUME_PHASE_MSG        12
#define SIZUME_PHASE_DAIZA      100
#define SIZUME_PHASE_STONE      101
#define SIZUME_PHASE_REMOVE     102
#define SIZUME_PHASE_PLAY       103
#define SIZUME_PHASE_PLAY_CLEAR 116
#define SIZUME_PHASE_PLAY_WRONG 117

#define SE_CURSOR               0x1a9b0000
#define SE_CANCEL               0x1a9b0001
#define SE_BUZZER               0x1a9b0002
#define SE_OK                   0x1a9b0003
#define SE_SIZUME_PUT           0x1bd10000  // the puzzle's own bank (PAZZLE_SHIZUME_BD_BD)

// The stone cursor's items: stone i, or -1 where it is on a pedestal.
// FF3 0x0024b490 (no Ghidra function before this session; it was past the
// end of KazaBoardTurnCursor).
static void SizumeStoneCsrFilter(short *tbl, short w, short h)
{
    (void)h;
    for (int i = 0; i < w; i++)
        tbl[i] = sizume_board->used[i] != 0 ? -1 : i;
}

// GetCmnFadeLayer()->Term(): the dimming phase 12 put up goes. FF3
// 0x0024b598 (FF2's SavePointMainMemFree matched it).
static void SizumeFadeLayerTerm(void)
{
    GetCmnFadeLayer()->Term();
}

// A phase-12 message in the puzzle's window (puzzle.c's one_Story_PuzzleNoItem
// fills it the same way). PC: a shared helper; FF3 fills the stack param
// inline in each of its four callers, leaving _0e unset.
static void SizumeMsgSet(short msg1, int type, short next_ok, short next_alt, short next_cancel,
                         void (*end_func)(int))
{
    PHASE12_PARAM param;

    param.msg0        = SIZUME_MSG;
    param.msg1        = msg1;
    param.type        = type;
    param.next_ok     = next_ok;
    param.next_alt    = next_alt;
    param.next_cancel = next_cancel;
    param._0e         = 0;
    param.end_func    = end_func;
    param.win         = sizume_win;
    Phase12ParamSet(&param);
    Phase12FadeLayerOn();
    GetGPhaseCtrl()->SetNext(SIZUME_PHASE_MSG, 1);
}

static void SizumePlayLabel(const char *label)
{
    BpPlayLabel(BpCtlGetHandleByFileNo(PuzzleGetLayoutFile()), label);
}

static void SizumeWinAlphaItem(const char *name)
{
    sizume_win->SetAlphaItemByName(BpCtlGetHandleByFileNo(PuzzleGetLayoutFile()), name);
}

// ──────────────────────────────────────────────────────────────────────
// The board and the puzzle table's hooks
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024b4d8 (FF2's SavePointFadeInMsgWinDisp matched it).
// callcheck: operator_new, SizumeBoardCtor (new SIZUME_BOARD)

void SizumeBoardCreate(void)
{
    int idx = PuzzleGetIdx();

    sizume_board = new SIZUME_BOARD(SizumeGetBoardDef(idx));
    MenuCsrInit(SIZUME_CSR_DAIZA, SIZUME_PEDESTAL_NUM, 1, 0);
    MenuCsrInitWithFilter(SizumeStoneCsrFilter, SIZUME_CSR_STONE, SIZUME_STONE_NUM, 1, 0);
}

// FF3 0x0024b558 (FF2's SavePointFadeOutDispMain matched it).
// callcheck: operator_delete (delete sizume_board)

void SizumeBoardDelete(void)
{
    MenuCsrRelease(SIZUME_CSR_DAIZA);
    MenuCsrRelease(SIZUME_CSR_STONE);
    if (sizume_board != nullptr)
        delete sizume_board;
    sizume_board = nullptr;
}

// FF3 0x0024b5b8.

void SizumePzlOpen(void)
{
    SizumeBoardCreate();
    SizumeDrawInit();
}

// FF3 0x0024b5d8.

int SizumePzlHasItem(void)
{
    return 1;
}

// Phase 12's end_func after the puzzle's prompts (and phase 100's init):
// unless the answer was yes, the window's guide line comes back.
// FF3 0x0024b890.

static void SizumeGuideMsg(int result)
{
    if (result != 1)
        sizume_win->StartMsg(SIZUME_MSG, SIZUME_MSG_GUIDE, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Phase 75 Story_PzlSizume
// ──────────────────────────────────────────────────────────────────────

// The window opens; once it and the layout are loaded it is emptied.
// FF3 0x0024b5e0.
// callcheck: operator_new, SelWinCtor (new CWindowMsg)

int init_Story_PzlSizume(void)
{
    GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;

    if (leaf->state_cnt == 0)
    {
        sizume_win = new CWindowMsg();
        sizume_win->Open();
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (leaf->state_cnt == 1)
    {
        if (BpCtlIsLoadEnd(PuzzleGetLayoutFile()) == 0)
            return 0;
        if (sizume_win->IsReady() != 0)
        {
            sizume_win->SetItem(-1, -1);
            return 1;
        }
    }
    return 0;
}

// The rules, then the pedestals.
// FF3 0x0024b6b8.
// callcheck: Phase12ParamSet, Phase12FadeLayerOn, GetGPhaseCtrl, GPhaseCtrlSetNext (SizumeMsgSet)

int one_Story_PzlSizume(void)
{
    SizumeMsgSet(SIZUME_MSG_RULES, 2, SIZUME_PHASE_DAIZA, -1, -1, nullptr);
    return 1;
}

// The layout plays "out" (the window fading with its BG); once it ended
// the layout, the window, the fade layer and the board go.
// FF3 0x0024b740.
// callcheck: BpPlayLabel, SelWinSetAlphaItemByName (SizumePlayLabel, SizumeWinAlphaItem)

int end_Story_PzlSizume(void)
{
    GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;

    if (leaf->state_cnt == 0)
    {
        SizumePlayLabel(sizume_label_out);
        SizumeWinAlphaItem(sizume_item_bg);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;
    }
    if (leaf->state_cnt == 1)
    {
        if (BpCtlIsLoadEnd(PuzzleGetLayoutFile()) == 0)
            return 0;
        BpCtlRelease(BpCtlGetHandleByFileNo(PuzzleGetLayoutFile()));
        sizume_win->Close();
        if (sizume_win != nullptr)
            delete sizume_win;
        sizume_win = nullptr;
        SizumeFadeLayerTerm();
        SizumeBoardDelete();
        return 1;
    }
    return 0;
}

// FF3 0x0024b860.

void pre_Story_PzlSizume(void)
{
}

// FF3 0x0024b868 (FF2's pre_SavePoint_Main matched it).

void after_Story_PzlSizume(void)
{
    LayoutAnimMain();
    sizume_win->Main();
    SizumeDraw();
}

// ──────────────────────────────────────────────────────────────────────
// Phase 100 PzlSizume_Daiza
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024b8c8 (FF2's init_SavePoint_Save matched it).

int init_PzlSizume_Daiza(void)
{
    SizumeGuideMsg(2);
    return 1;
}

// The pedestal cursor. OK: an empty pedestal goes to the stones, a stone
// asks to be taken back; key 10 plays a pedestal's stone (a buzz on an
// empty one); cancel asks to quit -- yes returns to the phase that opened
// the puzzle. A "no" brings the guide line back (SizumeGuideMsg).
// FF3 0x0024b8e8.
// callcheck: BpCtlGetHandleByFileNo, BpPlayLabel, PuzzleGetLayoutFile, Phase12ParamSet, Phase12FadeLayerOn (SizumePlayLabel / SizumeMsgSet)

// PC: the mouse. A pedestal is its base and the stone on it.
static void SizumeMouseDaiza(void)
{
    static const int guide_empty[3] = { -1, REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE };
    static const int guide_stone[4] = { -1, REIPAN_MENU_CROSS, REIPAN_MENU_SQUARE, REIPAN_MENU_TRIANGLE };
    const int handle = BpCtlGetHandleByFileNo(PuzzleGetLayoutFile());
    ReiPan_MenuBox box;

    MenuMouseGuide(handle, "R cap_4", guide_empty, 3);
    MenuMouseGuide(handle, "R cap_1", guide_stone, 4);
    for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
    {
        if (ReiPan_MenuMouseItemBox(handle, SIZUME_MOUSE_BASE + i, &box))
        {
            box.x -= 8.0f;
            box.w += 16.0f;
            box.y -= 66.0f;
            box.h += 66.0f;
            MenuMouseCsr(SIZUME_CSR_DAIZA, i, &box);
        }
    }
}

// PC: the mouse. One stone between two arrows.
static void SizumeMouseStone(void)
{
    static const int guide[3] = { -1, REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE };
    const int handle = BpCtlGetHandleByFileNo(PuzzleGetLayoutFile());
    ReiPan_MenuBox box;

    ReiPan_MenuMouseBegin(0, nullptr);
    MenuMouseGuide(handle, "R cap_2", guide, 3);
#ifndef REIPAN_SLPS_25544_CHS
    MenuMouseGuide(handle, "R cap_3", guide, 3);
#endif
    if (ReiPan_MenuMouseNameBox(handle, "R arw_L", &box))
        ReiPan_MenuMouseButton(box.x, box.y, box.w, box.h, REIPAN_MENU_LEFT);
    if (ReiPan_MenuMouseNameBox(handle, "R arw_R", &box))
        ReiPan_MenuMouseButton(box.x, box.y, box.w, box.h, REIPAN_MENU_RIGHT);
    if (ReiPan_MenuMouseNameBox(handle, "sphere2D", &box))
        ReiPan_MenuMouseButton(box.x, box.y, box.w, box.h, REIPAN_MENU_CROSS);
}

int one_PzlSizume_Daiza(void)
{
    sizume_board->show_buttons = 1;
    SizumeFadeLayerTerm();
    SizumeMouseDaiza();                 // PC
    MenuCsrUpdate(SIZUME_CSR_DAIZA, 0);
    sizume_board->cursor = MenuCsrGetPos(SIZUME_CSR_DAIZA);
    if (MenuCsrIsMovedSe(SIZUME_CSR_DAIZA, SE_CURSOR) != 0)
        return 0;

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], SE_OK, 0) != 0)
    {
        if (sizume_board->slot[sizume_board->cursor] == -1)
        {
            SizumePlayLabel(sizume_label_daiza_isi);
            GetGPhaseCtrl()->SetNext(SIZUME_PHASE_STONE, 0);
            return 1;
        }
        SizumeMsgSet(SIZUME_MSG_TAKE, 0, SIZUME_PHASE_REMOVE, SIZUME_PHASE_DAIZA, SIZUME_PHASE_DAIZA,
                     SizumeGuideMsg);
        return 1;
    }

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_10], -1, 0) != 0)
    {
        if (sizume_board->slot[sizume_board->cursor] != -1)
        {
            SizumePlayLabel(sizume_label_daiza_saisei);
            GetGPhaseCtrl()->SetNext(SIZUME_PHASE_PLAY, 0);
            return 1;
        }
        SndSePlay(SE_BUZZER, 100, 0x1000, 0, nullptr, 0);
        return 0;
    }

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], SE_CANCEL, 0) != 0)
    {
        SizumeMsgSet(SIZUME_MSG_QUIT, 1, PuzzleGetRetPhase(), SIZUME_PHASE_DAIZA, SIZUME_PHASE_DAIZA,
                     SizumeGuideMsg);
        sizume_board->show_buttons = 0;
        return 1;
    }
    return 0;
}

// FF3 0x0024bb98.

int end_PzlSizume_Daiza(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 101 PzlSizume_Stone
// ──────────────────────────────────────────────────────────────────────

// The stone cursor is rebuilt without the placed stones and put on the
// first free stone from where it was.
// FF3 0x0024bba0.

int init_PzlSizume_Stone(void)
{
    int pos;

    sizume_win->StartMsg(SIZUME_MSG, SIZUME_MSG_STONE, 0);
    pos = MenuCsrGetPos(SIZUME_CSR_STONE);
    MenuCsrRelease(SIZUME_CSR_STONE);
    MenuCsrInitWithFilter(SizumeStoneCsrFilter, SIZUME_CSR_STONE, SIZUME_STONE_NUM, 1, 0);
    for (int i = 0; i < SIZUME_STONE_NUM; i++)
    {
        int stone = (pos + i) % SIZUME_STONE_NUM;

        // PC: FF3 indexes used[] with a negative remainder too (the words of
        // slot[] before it); the old cursor always sat on a stone.
        if (stone < 0)
            continue;
        if (sizume_board->used[stone] == 0)
        {
            MenuCsrSetPos(SIZUME_CSR_STONE, (short)stone);
            sizume_board->stone = stone;
            break;
        }
    }
    return 1;
}

// The stone cursor. OK puts the stone on the pedestal and plays it (the
// layout's "seikai" when that solved the board); cancel goes back to the
// pedestals.
// FF3 0x0024bc70.
// callcheck: BpCtlGetHandleByFileNo, BpPlayLabel, PuzzleGetLayoutFile (SizumePlayLabel)

int one_PzlSizume_Stone(void)
{
    SizumeMouseStone();                 // PC
    MenuCsrUpdate(SIZUME_CSR_STONE, 0);
    sizume_board->stone = MenuCsrGetPos(SIZUME_CSR_STONE);
    // FF3 asks cursor 100 (not updated here), so the stone cursor's moves
    // never return early.
    if (MenuCsrIsMovedSe(SIZUME_CSR_DAIZA, SE_CURSOR) != 0)
        return 0;

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) != 0)
    {
        sizume_board->Put(sizume_board->cursor, sizume_board->stone);
        SndSePlay(SE_SIZUME_PUT, 100, 0x1000, 0, nullptr, 0);
        SizumePlayLabel(sizume_board->IsSolved() != 0 ? sizume_label_seikai : sizume_label_isi_saisei);
        GetGPhaseCtrl()->SetNext(SIZUME_PHASE_PLAY, 0);
        return 1;
    }
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_CANCEL], SE_CANCEL, 0) != 0)
    {
        SizumePlayLabel(sizume_label_isi_daiza);
        GetGPhaseCtrl()->SetNext(SIZUME_PHASE_DAIZA, 0);
        return 1;
    }
    return 0;
}

// FF3 0x0024bdd0.

int end_PzlSizume_Stone(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 102 PzlSizume_Remove
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024bdd8.

int init_PzlSizume_Remove(void)
{
    return 1;
}

// FF3 0x0024bde0.

int one_PzlSizume_Remove(void)
{
    sizume_board->Take(sizume_board->cursor);
    GetGPhaseCtrl()->SetNext(SIZUME_PHASE_DAIZA, 0);
    return 1;
}

// FF3 0x0024be30.

int end_PzlSizume_Remove(void)
{
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 103 PzlSizume_Play and its children 116 / 117
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024be38.

int init_PzlSizume_Play(void)
{
    return 1;
}

// FF3 0x0024be40.

int one_PzlSizume_Play(void)
{
    if (sizume_board->IsSolved() != 0)
        GetGPhaseCtrl()->SetNext(SIZUME_PHASE_PLAY_CLEAR, 0);
    else
        GetGPhaseCtrl()->SetNext(SIZUME_PHASE_PLAY_WRONG, 0);
    return 1;
}

// Once the layout's label ended the window stops fading with an item.
// FF3 0x0024beb0.

int end_PzlSizume_Play(void)
{
    int loaded = BpCtlIsLoadEnd(PuzzleGetLayoutFile());

    if (loaded != 0)
        sizume_win->SetAlphaItem(nullptr);
    return loaded != 0;
}

// FF3 0x0024bef0.

void pre_PzlSizume_Play(void)
{
}

// FF3 0x0024bef8.

void after_PzlSizume_Play(void)
{
}

// The whole recording (the subtitle player runs it); the window fades
// with R cap_2.
// FF3 0x0024bf00.
// callcheck: BpCtlGetHandleByFileNo, PuzzleGetLayoutFile, SelWinSetAlphaItemByName (SizumeWinAlphaItem)

int init_PzlSizume_PlayClear(void)
{
    sizume_board->PlayVoice();
    SizumeWinAlphaItem(sizume_item_cap_2);
    return 1;
}

// Until it ends (OK or cancel cut it short): the clear flag, the layout's
// "seikai_next" and the "solved" message, back to the phase that opened the
// puzzle.
// FF3 0x0024bf48.
// callcheck: BpCtlGetHandleByFileNo, BpPlayLabel, SelWinSetAlphaItemByName, Phase12ParamSet, Phase12FadeLayerOn, GetGPhaseCtrl, GPhaseCtrlSetNext (SizumePlayLabel / SizumeWinAlphaItem / SizumeMsgSet)

int one_PzlSizume_PlayClear(void)
{
    if (BpCtlIsLoadEnd(PuzzleGetLayoutFile()) == 0)
        return 0;
    ReiPan_MenuMouseConfirm();          // PC: a click stops the stone's voice
    if (sizume_board->IsPlaying() != 0 &&
        CheckPadTrgPlaySe(pad_key[PAD_KEY_OK] | pad_key[PAD_KEY_CANCEL], -1, 0) == 0)
        return 0;

    sizume_board->StopVoice();
    SizumePlayLabel(sizume_label_seikai_next);
    PuzzleClear();
    SizumeWinAlphaItem(sizume_item_bg);
    sizume_win->StartMsg(SIZUME_MSG, SIZUME_MSG_SOLVED, 0);
    SizumeMsgSet(SIZUME_MSG_SOLVED, 2, PuzzleGetRetPhase(), -1, -1, nullptr);
    sizume_board->show_buttons = 0;
    return 1;
}

// FF3 0x0024c0a0.

int end_PzlSizume_PlayClear(void)
{
    return 1;
}

// The pedestal's stone's clip; the window fades with R cap_1.
// FF3 0x0024c0a8.
// callcheck: BpCtlGetHandleByFileNo, PuzzleGetLayoutFile, SelWinSetAlphaItemByName (SizumeWinAlphaItem)

int init_PzlSizume_PlayWrong(void)
{
    sizume_board->PlayVoice();
    SizumeWinAlphaItem(sizume_item_cap_1);
    return 1;
}

// Until it ends (OK or cancel cut it short), then back to the pedestals.
// FF3 0x0024c0f0.
// callcheck: BpCtlGetHandleByFileNo, BpPlayLabel, PuzzleGetLayoutFile (SizumePlayLabel)

int one_PzlSizume_PlayWrong(void)
{
    ReiPan_MenuMouseConfirm();          // PC: a click stops the stone's voice
    if (sizume_board->IsPlaying() != 0 &&
        CheckPadTrgPlaySe(pad_key[PAD_KEY_OK] | pad_key[PAD_KEY_CANCEL], -1, 0) == 0)
        return 0;

    sizume_board->StopVoice();
    SizumePlayLabel(sizume_label_saisei_daiza);
    GetGPhaseCtrl()->SetNext(SIZUME_PHASE_DAIZA, 0);
    SizumeGuideMsg(2);
    return 1;
}

// FF3 0x0024c198.

int end_PzlSizume_PlayWrong(void)
{
    return 1;
}

// FF3 0x0024c1a0 is compiler output: the atexit thunk of the GPhaseCtrl
// singleton (GPhaseCtrlTerm(0x41e468)), whose C++ static gphase.c owns here;
// it marks the end of the file.
