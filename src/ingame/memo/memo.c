/* ==========================================================================
 *  ingame/memo/memo.c
 *
 *  FF3's memos (FF3 0x203828..0x20480c and the out-of-line list fill
 *  0x2bd088; FF3-only, in FF2 mdlwork.c's link slot): the memo records, the
 *  new-memo notice and the per-category memo lists. See memo.h.
 * ======================================================================== */
#include "ingame/memo/memo.h"

#include <string.h>                     // memset

#include "common/heapctrl.h"            // heapCtrlMalloc, heapCtrlFree
#include "common/vu0math.h"             // Vu0Sinf
#include "graphics/graph2d/g2d_draw.h"  // SPRT_DAT, DISP_SPRT, CopySprDToSpr, DispSprD
#include "graphics/graph2d/message.h"   // GetMsgDataAddr, MsgGetPageBreakNum, PrintMsg_ArrangeOneLine
#include "ingame/menu/anim_2d.h"        // LangTexSend
#include "ingame/menu/menu_file_list.h" // MemoMenuReq
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/system.h"           // GetPALMode
#include "system/pad/pad_ctrl.h"        // CheckPadTrgPlaySe, pad_key

MEMO_WRK memo_wrk[MEMO_NUM];            /* data 0x32b560 (zeros in the ROM) */
MEMO_FLG memo_flg;                      /* sdata 0x371158 (MemoFlgSaveGet) */
static short memo_notice_last;          /* sbss 0x3735e4 (gp+0xf4) */
MEMO_NOTICE *memo_notice;               /* sbss 0x3735e8 (gp+0xf8) */

#include "ingame/memo/memo_dat.inc"     // memo_title_msg, memo_dat, memo_page (rodata)

// ──────────────────────────────────────────────────────────────────────
// Clears the memo records and the 8-byte block at 0x371158.
// FF3 0x00203828 (IngameInit); MemoWrkClear without the empty call.

void MemoWrkInit(void)
{
    short i;

    MemoWrkInitSub();
    for (i = 0; i < MEMO_NUM; i++)
        memset(&memo_wrk[i], 0, sizeof(MEMO_WRK));
    // FF3 stores both words 0 again after the memset
    memset(&memo_flg, 0, sizeof(MEMO_FLG));
    memo_flg.open_flg = 0;
    memo_flg.file_seen = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002038a8 (IngameNewGameReset).

void MemoWrkClear(void)
{
    short i;

    for (i = 0; i < MEMO_NUM; i++)
        memset(&memo_wrk[i], 0, sizeof(MEMO_WRK));
    memset(&memo_flg, 0, sizeof(MEMO_FLG));
    memo_flg.open_flg = 0;
    memo_flg.file_seen = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203928 (IngameEnd).

void MemoWrkEnd(void)
{
    MemoWrkEndSub();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203940

int MemoSetStatus(short memo, s_char status)
{
    if ((u_short)memo < MEMO_NUM && (u_char)status < 3) {
        memo_wrk[memo].status = status;
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203980 (MEMO_UPDATE, the event queue's rewards)

int MemoSetPage(short memo, s_char page, int notice)
{
    if ((u_short)memo >= MEMO_NUM || memo_wrk[memo].page > page)
        return 0;
    if (page >= 0 && page < memo_dat[memo].page_num) {
        memo_wrk[memo].page = page;
        MemoSetStatus(memo, 1);
        if (notice)
            MemoNoticeReq(memo);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203a28 (MEMO_OPEN)

int MemoOpen(short memo)
{
    if ((u_short)memo >= MEMO_NUM || memo_wrk[memo].status != 0)
        return 0;
    MemoSetPage(memo, 0, 1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203a70 (no caller)

int MemoSetNotFound(short memo)
{
    if ((u_short)memo >= MEMO_NUM || memo_wrk[memo].status == 0)
        return 0;
    MemoSetStatus(memo, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203ab8 (no jal caller)

int MemoSetRead(short memo)
{
    if ((u_short)memo >= MEMO_NUM || memo_wrk[memo].status == 0)
        return 0;
    MemoSetStatus(memo, 2);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203b00 (no caller)

int MemoNextPage(short memo)
{
    return (u_short)memo < MEMO_NUM &&
           MemoSetPage(memo, (s_char)(memo_wrk[memo].page + 1), 1) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203b48 (MemoMenuIsEnable)

int MemoGetOpenPageNum(void)
{
    int   num = 0;
    short i;

    for (i = 0; i < MEMO_NUM; i++) {
        if (memo_wrk[i].status != 0)
            num += memo_wrk[i].page + 1;
    }
    return num;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203b90 (no caller)

int MemoGetAllPageNum(void)
{
    int   num = 0;
    short i;

    for (i = 0; i < MEMO_NUM; i++)
        num += memo_dat[i].page_num;
    return num;
}

// ──────────────────────────────────────────────────────────────────────
// The save blocks (save_game_data rows 1 and 2).
// FF3 0x00203bd0 / 0x00203be8

int MemoWrkSaveGet(void **addr)
{
    *addr = memo_wrk;
    return sizeof(memo_wrk);            // 0x42
}

int MemoFlgSaveGet(void **addr)
{
    *addr = &memo_flg;
    return sizeof(memo_flg);            // 0x8
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203c00 / 0x00203c08 / 0x00203c80: empty (jr ra). MemoWrkInit
// calls the first, MemoWrkEnd tail-jumps to the second, the third has no
// caller.

void MemoWrkInitSub(void)
{
}

void MemoWrkEndSub(void)
{
}

void MemoDummy(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// The page breaks of a memo page's text (the memo menu).
// FF3 0x00203c10

static short MemoGetPageIndex(short memo, s_char page)
{
    if ((u_short)memo >= MEMO_NUM || page < 0 || page >= memo_dat[memo].page_num)
        return -1;

    const int row = memo_dat[memo].page_top + page;
    return (unsigned)row < MEMO_PAGE_NUM ? (short)row : -1;
}

short MemoGetCurrentPageIndex(short memo)
{
    return (u_short)memo < MEMO_NUM ? MemoGetPageIndex(memo, memo_wrk[memo].page) : -1;
}

s_char MemoGetPageBreakNum(short memo, s_char page)
{
    const short row = MemoGetPageIndex(memo, page);
    if (row < 0 || memo_page[row].text < 0)
        return 0;
    return (s_char)MsgGetPageBreakNum(0x57, memo_page[row].text);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203c88 / 0x00203c90

short MemoNoticeGetLast(void)
{
    return memo_notice_last;
}

void MemoNoticeSetLast(short memo)
{
    memo_notice_last = memo;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203ca0 (IngameInit); callcheck: operator_new, MemoNoticeCtor (new)

void MemoNoticeCreate(void)
{
    memo_notice = new MEMO_NOTICE;
    MemoNoticeSetLast(-1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203cd8 (IngameEnd); callcheck: MemoNoticeDtor, operator_delete (delete)

void MemoNoticeDelete(void)
{
    if (memo_notice != nullptr)
        delete memo_notice;
    memo_notice = nullptr;
    MemoNoticeSetLast(-1);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203d18 (MemoSetPage)

int MemoNoticeReq(short memo)
{
    if (memo_flg.open_flg != 0)
        return memo_notice->Add(memo);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203d58 (no jal caller)

void MemoNoticeClear(void)
{
    memo_notice->AllFree();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203d70 (IngameDrawSub)

void MemoNoticeMain(void)
{
    memo_notice->Step();
}

// ──────────────────────────────────────────────────────────────────────
// The title is left out while the pause phase is requested.
// FF3 0x00203d88 (IngameDrawSub; GetGPhaseCtrl through the shared cache
// sdata 0x37284c)

void MemoNoticeDraw(void)
{
    memo_notice->DispHead(GetGPhaseCtrl()->next != 0x2c);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203dc8 (no caller)

short MemoNoticeGetMemo(void)
{
    return memo_notice->GetHeadMemo();
}

// ──────────────────────────────────────────────────────────────────────
// pad_key[13] opens the memo menu at the memo the notice showed last.
// FF3 0x00203de8 (IngameMain)

int MemoNoticeMenuCheck(void)
{
    return MemoNoticeMenuOpen(memo_notice, 0, pad_key[PAD_KEY_MEMO]);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203e10. A free function, not a member: FF3 tests the object
// for null first (a null this would be undefined behaviour in C++).

int MemoNoticeMenuOpen(MEMO_NOTICE *notice, int ret, u_int mask)
{
    short last;

    if (notice == nullptr)
        return 0;
    last = MemoNoticeGetLast();
    if (CheckPadTrgPlaySe((u_short)mask, -1, 0) == 0)
        return 0;
    return MemoMenuReq(last, last != -1, ret, 0) != 0;
}

// FF3 0x00203e98 (MemoGPhaseCtrlAtExit) is compiler output: the atexit
// thunk of the GPhaseCtrl singleton (GPhaseCtrlTerm(0x41e468)), whose C++
// static gphase.c owns here.

// ──────────────────────────────────────────────────────────────────────
// MEMO_NOTICE
// FF3 0x00203ee8 (MemoNoticeCtor; the base constructor 0x00203eb8 is the
// same body) / 0x00203f48 (MemoNoticeDtor; base 0x00203f18)

MEMO_NOTICE::MEMO_NOTICE()
{
    list = SingleLinkListAlloc(sizeof(MEMO_NOTICE_CELL));
}

// FF3 0x00203f48 (MemoNoticeDtor; base 0x00203f18)
MEMO_NOTICE::~MEMO_NOTICE()
{
    SingleLinkListFree(list);
    list = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203f78

MEMO_NOTICE_CELL *MEMO_NOTICE::GetHead()
{
    SLL_CELL *cell = SingleLinkListBeginCell(list);

    if (cell == nullptr)
        return nullptr;
    return (MEMO_NOTICE_CELL *)SingleLinkListCellBodyPtr(cell);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203fb8

void MEMO_NOTICE::AllFree()
{
    SingleLinkListAllCellFree(list);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00203fd0

int MEMO_NOTICE::Add(short memo)
{
    MEMO_NOTICE_CELL cell;

    cell.memo  = memo;
    cell.timer = GetPALMode() ? 65 : 70;
    cell.se    = 1;
    return SingleLinkListAddEnd(list, &cell) != nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00204028 (no jal caller)

int MEMO_NOTICE::Remove(short memo)
{
    SLL_CELL *cell = SingleLinkListBeginCell(list);
    SLL_CELL *next;

    while (cell != nullptr) {
        next = cell->pNext;
        if (((MEMO_NOTICE_CELL *)SingleLinkListCellBodyPtr(cell))->memo == memo) {
            SingleLinkListRemove(list, cell);
            return 1;
        }
        cell = next;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002040c0

void MEMO_NOTICE::RemoveHead()
{
    SingleLinkListRemoveBegin(list);
}

// ──────────────────────────────────────────────────────────────────────
// The notice: two pieces of language-pack picture 10 at the top right,
// faded in by a sine over its first 10 frames and out over its last 30,
// and with title the memo's title.
// FF3 0x002040d8

void MEMO_NOTICE::DispCell(MEMO_NOTICE_CELL *cell, int title)
{
    SPRT_DAT  sprt[2] = {
        // tex0, u, v, w, h, x, y, pri, alpha, flip, bln
        { 0x2005a545e1312caaULL, 1,    1, 0xda, 0x45, 0x19f, 0xb, 0, 0x80, 0, 0 },
        { 0x2005a545e1312caaULL, 1, 0x48, 0x40, 0x36, 0x15f, 0xb, 0, 0x80, 0, 0 },
    };
    DISP_SPRT ds;
    float     rate = 1.0f;
    int       total;
    int       alpha;
    int       i;
    u_char   *str;

    if (cell->timer < 30) {
        rate = Vu0Sinf(((float)cell->timer * 1.5707964f) / 30.0f);
    } else if (cell->timer >= (GetPALMode() ? 0x38 : 0x3d)) {
        total = GetPALMode() ? 0x41 : 0x46;
        rate = Vu0Sinf(((float)(total - cell->timer) * 1.5707964f) / 10.0f);
    }
    alpha = (int)(rate * 128.0f);

    LangTexSend(10);
    for (i = 0; i < 2; i++) {
        CopySprDToSpr(&ds, &sprt[i]);
        ds.alpha = (u_char)alpha;
        DispSprD(&ds);
    }

    if (title == 0)
        return;
    const short row = MemoGetCurrentPageIndex(cell->memo);
    if (row < 0 || (u_short)memo_page[row].title >= MEMO_TITLE_NUM)
        return;
    str = GetMsgDataAddr(0x58, memo_title_msg[memo_page[row].title]);
    if (str != nullptr) {
        SetCurColorRGB(0x1a, 9, 3);
        PrintMsg_ArrangeOneLine(str, 0x171, 0x19, 0x2d, alpha, 0, 0, 0, 0);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Once a frame on the head notice: its SE once, the timer down, the memo
// remembered for the menu key; done (or its memo lost) it is dropped.
// FF3 0x00204398 (MemoNoticeMain, 0x1b42f0)

void MEMO_NOTICE::Step()
{
    MEMO_NOTICE_CELL *cell = GetHead();
    short             memo;

    if (cell == nullptr)
        return;
    memo = cell->memo;
    if (memo_wrk[memo].status == 0) {
        cell->timer = 0;
    } else {
        if (cell->se != 0) {
            SndSePlay(0x1a9b000c, 100, 0x1000, 0, 0, 0);
            cell->se = 0;
        }
        cell->timer--;
        MemoNoticeSetLast(memo);
    }
    if (cell->timer < 1)
        RemoveHead();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00204458 (MemoNoticeDraw)

void MEMO_NOTICE::DispHead(int title)
{
    MEMO_NOTICE_CELL *cell = GetHead();

    if (cell != nullptr && memo_wrk[cell->memo].status != 0 && cell->timer > 0)
        DispCell(cell, title);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002044d8

short MEMO_NOTICE::GetHeadMemo()
{
    MEMO_NOTICE_CELL *cell = GetHead();

    if (cell == nullptr || cell->timer < 1)
        return -1;
    return cell->memo;
}

// ──────────────────────────────────────────────────────────────────────
// MEMO_LIST
// FF3 0x00204540 (MemoListCtor; the base constructor 0x00204510 is the same
// body) / 0x00204588 (MemoListDtor; base 0x00204570)

MEMO_LIST::MEMO_LIST(short category, int all)
{
    this->all      = all;
    num            = 0;
    this->category = category;
    list           = nullptr;
    Build();
}

// FF3 0x00204588 (MemoListDtor; base 0x00204570)
MEMO_LIST::~MEMO_LIST()
{
    Free();
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002045a0

void MEMO_LIST::Build()
{
    short n = Fill(0);

    if (n != 0) {
        list = (short *)heapCtrlMalloc(GetSystemHeapWrkP(), n * sizeof(short), 0, 0, 0);
        num  = Fill(1);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00204610

void MEMO_LIST::Free()
{
    if (list != nullptr) {
        heapCtrlFree(GetSystemHeapWrkP(), list, 0, 0, 0);
        list = nullptr;
    }
    num = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00204660 (MenuCamModeFilmSelDisp). Get(i) is -1 only outside the
// list, which the loop never reaches.

int MEMO_LIST::HasNew()
{
    short i;

    for (i = 0; i < num; i++) {
        if (memo_wrk[Get(i)].status == 1)
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002046e0 (no jal caller)

int MEMO_LIST::HasOpen()
{
    short i;

    for (i = 0; i < num; i++) {
        if (memo_wrk[Get(i)].status != 0)
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00204760 (0x205c18)

short MEMO_LIST::IndexOf(short memo)
{
    short i;

    for (i = 0; i < num; i++) {
        if (Get(i) == memo)
            return i;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// The memos of the category, found ones only unless all; with store (and
// a list) written into it.
// FF3 0x002bd088 (out of line; MemoListBuild)

short MEMO_LIST::Fill(int store)
{
    short n = 0;
    short i;

    for (i = 0; i < MEMO_NUM; i++) {
        if (all == 0 && memo_wrk[i].status == 0)
            continue;
        if (category != memo_dat[i].category)
            continue;
        if (store != 0 && list != nullptr)
            list[n] = i;
        n++;
    }
    return n;
}
