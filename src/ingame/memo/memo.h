/* ==========================================================================
 *  ingame/memo/memo.h
 *
 *  FF3's memos (the "files" Rei finds; FF3 0x203828..0x20480c and the list
 *  fill 0x2bd088). FF3-only: the slot is FF2 mdlwork.c's in link order, but
 *  nothing of FF2's model work is left in it. Three parts:
 *  - the memo records (memo_wrk, saved) and the ROM tables behind them;
 *  - the new-memo notice: a queue of memos to announce (MEMO_NOTICE at
 *    gp+0xf8), stepped and drawn from IngameDrawSub, whose key opens the
 *    memo menu (phase 0x3e) at the memo just announced;
 *  - MEMO_LIST, the memos of one category, for the menus.
 * ======================================================================== */
#ifndef _INGAME_MEMO_MEMO_H
#define _INGAME_MEMO_MEMO_H

#include <stddef.h>                     // offsetof
#include "sdk/scetypes.h"

#include "common/SingleLinkList.h"

#define MEMO_NUM        33
#define MEMO_PAGE_NUM   97              // pages of all memos (memo_dat page_top + page_num)
#define MEMO_TITLE_NUM  56

// One memo's progress (data 0x32b560, saved by MemoWrkSaveGet).
struct MEMO_WRK                         /* 0x2 */
{
    /* 0x0 */ u_char status;            // 0 not found, 1 new (unread), 2 read
    /* 0x1 */ s_char page;              // last page found (MemoSetPage)
};
static_assert(sizeof(MEMO_WRK) == 2, "MEMO_WRK is 2 bytes");
static_assert(offsetof(MEMO_WRK, page) == 1, "MEMO_WRK.page");

// A memo of the ROM (rodata 0x35c178).
struct MEMO_DAT                         /* 0x8 */
{
    /* 0x0 */ short  no;                // the row's index
    /* 0x2 */ short  category;          // 0..3, MEMO_LIST.category
    /* 0x4 */ short  page_top;          // first row of memo_page
    /* 0x6 */ s_char page_num;          // pages
};
static_assert(sizeof(MEMO_DAT) == 8, "MEMO_DAT is 8 bytes");
static_assert(offsetof(MEMO_DAT, category) == 2, "MEMO_DAT.category");
static_assert(offsetof(MEMO_DAT, page_top) == 4, "MEMO_DAT.page_top");
static_assert(offsetof(MEMO_DAT, page_num) == 6, "MEMO_DAT.page_num");

// A memo page (rodata 0x35c280).
struct MEMO_PAGE                        /* 0x6 */
{
    /* 0x0 */ short title;              // index into memo_title_msg
    /* 0x2 */ short text;               // message id (type 0x57) of the page's text
    /* 0x4 */ short pic;                // MENU_PIC picture 1875..1928 (the file detail page shows 0x750..0x7b3)
};
static_assert(sizeof(MEMO_PAGE) == 6, "MEMO_PAGE is 6 bytes");
static_assert(offsetof(MEMO_PAGE, text) == 2, "MEMO_PAGE.text");
static_assert(offsetof(MEMO_PAGE, pic) == 4, "MEMO_PAGE.pic");

extern MEMO_WRK        memo_wrk[MEMO_NUM];              // data 0x32b560
extern const MEMO_DAT  memo_dat[MEMO_NUM];              // rodata 0x35c178
extern const MEMO_PAGE memo_page[MEMO_PAGE_NUM];        // rodata 0x35c280
extern const short     memo_title_msg[MEMO_TITLE_NUM];  // rodata 0x35c108

// The 8-byte block 0x371158 (saved by MemoFlgSaveGet).
struct MEMO_FLG                         /* 0x8 */
{
    /* 0x0 */ int open_flg;             // non-zero: MemoSetPage queues the notice (SET_MEMO_OPEN_FALG)
    /* 0x4 */ int file_seen;            // the file menu was visited (its first visit opens phase 87)
};
static_assert(sizeof(MEMO_FLG) == 8, "MEMO_FLG is 8 bytes");
static_assert(offsetof(MEMO_FLG, file_seen) == 4, "MEMO_FLG.file_seen");
extern MEMO_FLG memo_flg;               // sdata 0x371158

void MemoWrkInit(void);                 // FF3 0x00203828: IngameInit
void MemoWrkClear(void);                // FF3 0x002038a8: the same without MemoWrkInitSub; IngameNewGameReset
void MemoWrkEnd(void);                  // FF3 0x00203928: IngameEnd
void MemoWrkInitSub(void);              // FF3 0x00203c00: empty
void MemoWrkEndSub(void);               // FF3 0x00203c08: empty

int  MemoSetStatus(short memo, s_char status);          // FF3 0x00203940: status 0..2, 1 when set
// Raise memo to page (only upwards, below its page count): status new and,
// with notice, the new-memo notice. 1 when set.
int  MemoSetPage(short memo, s_char page, int notice);  // FF3 0x00203980
int  MemoOpen(short memo);              // FF3 0x00203a28: MEMO_OPEN; 1 when it was not found before
int  MemoSetNotFound(short memo);       // FF3 0x00203a70: 1 when it was found
int  MemoSetRead(short memo);           // FF3 0x00203ab8: 1 when it was found
int  MemoNextPage(short memo);          // FF3 0x00203b00
int  MemoGetOpenPageNum(void);          // FF3 0x00203b48: pages found, all memos
int  MemoGetAllPageNum(void);           // FF3 0x00203b90: pages of all memos
short  MemoGetCurrentPageIndex(short memo);
s_char MemoGetPageBreakNum(short memo, s_char page);    // FF3 0x00203c10
void MemoDummy(void);                   // FF3 0x00203c80: empty

int  MemoWrkSaveGet(void **addr);       // FF3 0x00203bd0: {0x32b560, 0x42}
int  MemoFlgSaveGet(void **addr);       // FF3 0x00203be8: {0x371158, 0x8}

// One queued notice (the SingleLinkList element, 8 bytes).
struct MEMO_NOTICE_CELL                 /* 0x8 */
{
    /* 0x0 */ short memo;
    /* 0x2 */ short timer;              // frames left: 70 (65 with GetPALMode) down to 0
    /* 0x4 */ int   se;                 // 1 until the notice SE has played
};
static_assert(sizeof(MEMO_NOTICE_CELL) == 8, "MEMO_NOTICE_CELL is 8 bytes");
static_assert(offsetof(MEMO_NOTICE_CELL, timer) == 2, "MEMO_NOTICE_CELL.timer");
static_assert(offsetof(MEMO_NOTICE_CELL, se) == 4, "MEMO_NOTICE_CELL.se");

// The new-memo notice queue (4 bytes on the EE). The game's is at gp+0xf8;
// DevelopMenuInit 0x1b3df0 makes a second one (ingame/story_develop.c).
// layout: host -- list is a pointer (EE 4 bytes); only ever made with new.
struct MEMO_NOTICE
{
    MEMO_NOTICE();                      // FF3 0x00203ee8 (MemoNoticeCtor; base 0x00203eb8)
    ~MEMO_NOTICE();                     // FF3 0x00203f48 (MemoNoticeDtor; base 0x00203f18)

    MEMO_NOTICE_CELL *GetHead();        // FF3 0x00203f78
    void  AllFree();                    // FF3 0x00203fb8
    int   Add(short memo);              // FF3 0x00203fd0
    int   Remove(short memo);           // FF3 0x00204028
    void  RemoveHead();                 // FF3 0x002040c0
    void  DispCell(MEMO_NOTICE_CELL *cell, int title);  // FF3 0x002040d8
    void  Step();                       // FF3 0x00204398
    void  DispHead(int title);          // FF3 0x00204458
    short GetHeadMemo();                // FF3 0x002044d8: -1 when none

    /* 0x0 */ SINGLE_LINK_LIST *list;   // of MEMO_NOTICE_CELL
};

extern MEMO_NOTICE *memo_notice;        // sbss 0x3735e8 (gp+0xf8)

short MemoNoticeGetLast(void);          // FF3 0x00203c88: the memo the notice showed last, -1 none
void  MemoNoticeSetLast(short memo);    // FF3 0x00203c90
void  MemoNoticeCreate(void);           // FF3 0x00203ca0: IngameInit
void  MemoNoticeDelete(void);           // FF3 0x00203cd8: IngameEnd
int   MemoNoticeReq(short memo);        // FF3 0x00203d18
void  MemoNoticeClear(void);            // FF3 0x00203d58
void  MemoNoticeMain(void);             // FF3 0x00203d70: IngameDrawSub
void  MemoNoticeDraw(void);             // FF3 0x00203d88: IngameDrawSub
short MemoNoticeGetMemo(void);          // FF3 0x00203dc8
int   MemoNoticeMenuCheck(void);        // FF3 0x00203de8: IngameMain
int   MemoNoticeMenuOpen(MEMO_NOTICE *notice, int ret, u_int mask);  // FF3 0x00203e10

// The memos of one category (0x10 bytes on the EE; the camera menu and
// StoryMenu_File build them on the stack).
// layout: host -- list is a pointer (EE 4 bytes at +8); num follows it.
struct MEMO_LIST
{
    MEMO_LIST(short category, int all); // FF3 0x00204540 (MemoListCtor; base 0x00204510)
    ~MEMO_LIST();                       // FF3 0x00204588 (MemoListDtor; base 0x00204570)

    void  Build();                      // FF3 0x002045a0
    void  Free();                       // FF3 0x00204610
    int   HasNew();                     // FF3 0x00204660: a memo with status 1
    int   HasOpen();                    // FF3 0x002046e0: a memo found
    short IndexOf(short memo);          // FF3 0x00204760: -1 when not listed
    short Fill(int store);              // FF3 0x002bd088: the count; with store into list

    // list[i], -1 outside the list (an inline: its callers carry the body).
    short Get(short i) const
    {
        if (i < 0 || i >= num)
            return -1;
        return list[i];
    }

    /* 0x00 */ short  category;
    /* 0x04 */ int    all;              // non-zero: memos not found yet too
    /* 0x08 */ short *list;             // num memo numbers (system heap)
    /* 0x0c */ short  num;
};
static_assert(offsetof(MEMO_LIST, all) == 4, "MEMO_LIST.all");
static_assert(offsetof(MEMO_LIST, list) == 8, "MEMO_LIST.list");
static_assert(offsetof(MEMO_LIST, num) == 8 + sizeof(short *), "MEMO_LIST.num follows list");

#endif /* _INGAME_MEMO_MEMO_H */
