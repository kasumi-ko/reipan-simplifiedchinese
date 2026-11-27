/* ==========================================================================
 *  graphics/graph2d/message.h
 *
 *  FF3's message printer: FF2's message.c cut down to the parts FF3 still
 *  uses. Messages are byte streams in MSG_OBJ (+ language), loaded to EE
 *  0xfed000 by LangDataLoadReq and found by (type, id) through two offset
 *  tables (GetMsgDataAddr). A byte below 0xf0 is a glyph of the current font
 *  bank (24x24 cells, 21 to a row of a 512x256 page); 0xf0..0xff are control
 *  codes:
 *
 *    0xf0..0xf8 <g>  glyph g of bank 1..9      0xfc <n>  number n (memory card slot)
 *    0xf9            current colour            0xfd <c>  colour label c
 *    0xfa <a> <n>    arrange (in a window)     0xfe      line break
 *    0xfb            page break                0xff      end
 *
 *  Drawing goes straight to the GS (StartDmaDirectTrans): SetFontEnv sets
 *  ALPHA/TEX1/CLAMP, then per glyph a TEX0 load and a SPRITE. Coordinates
 *  are centred (x -320..320, y -224..224). Multi-page messages keep their
 *  page in a small state block (MsgGetPage / MsgPageNext / MsgIsPageWait).
 *
 *  FF3 0x20c400..0x20df28 (MsgInit .. LangDataLoadReq). The code before it,
 *  0x20b590..0x20c400, is ingame/story_menu_top.c (phase 57 StoryMenu_Top).
 * ======================================================================== */
#ifndef _GRAPHICS_GRAPH2D_MESSAGE_H
#define _GRAPHICS_GRAPH2D_MESSAGE_H

#include <stddef.h>                     // offsetof

#include "sdk/scetypes.h"

// EE address the language's message data (MSG_OBJ + GetLanguage()) loads to.
#define MSG_DATA_ADRS       0xfed000

// Colour label meaning "the current colour" (SetCurColorRGB).
#define MSG_COL_CURRENT     0x2d

// Control codes of the message stream.
#define MSG_CODE_BANK1      0xf0        // .. 0xf8: glyph of bank 1..9 follows
#define MSG_CODE_BANK9      0xf8
#define MSG_CODE_CURCOL     0xf9
#define MSG_CODE_ARRANGE    0xfa
#define MSG_CODE_PAGE       0xfb
#define MSG_CODE_EXNUM      0xfc
#define MSG_CODE_COL        0xfd
#define MSG_CODE_RET        0xfe
#define MSG_CODE_END        0xff

// The full per-string draw record (FF2 DISP_STR; FF3 clears 0x38 bytes).
// layout: host -- str is a pointer (4 bytes on the EE, 8 here); the fields
// after it are FF3's 0x04..0x34, asserted relative to it.
struct DISP_STR                     /* 0x38 on the EE */
{
    /* 0x00 */ u_char *str;         // read position in the stream
    /* 0x04 */ int     pos_x;       // pen
    /* 0x08 */ int     pos_y;
    /* 0x0c */ int     type;        // FF2, unused in FF3's message.c
    /* 0x10 */ u_int   r;
    /* 0x14 */ u_int   g;
    /* 0x18 */ u_int   b;
    /* 0x1c */ int     alpha;
    /* 0x20 */ int     pri;         // (pri & 0xf) << 4; the sprite Z is 0xfffff - pri
    /* 0x24 */ int     x_wide;      // extra advance after each glyph
    /* 0x28 */ int     y_wide;      // extra advance after each line
    /* 0x2c */ int     brnch_num;   // FF2, unused in FF3's message.c
    /* 0x30 */ int     csr;         // PrintMsg* return csr + 1
    /* 0x34 */ int     st;
};
static_assert(offsetof(DISP_STR, pos_x) == sizeof(u_char *), "DISP_STR.pos_x follows str");
static_assert(offsetof(DISP_STR, r) == sizeof(u_char *) + 0x0c, "DISP_STR.r (EE 0x10)");
static_assert(offsetof(DISP_STR, pri) == sizeof(u_char *) + 0x1c, "DISP_STR.pri (EE 0x20)");
static_assert(offsetof(DISP_STR, csr) == sizeof(u_char *) + 0x2c, "DISP_STR.csr (EE 0x30)");
static_assert(offsetof(DISP_STR, st) == sizeof(u_char *) + 0x30, "DISP_STR.st (EE 0x34, the last field)");

// The caller's subset of DISP_STR (FF2 STR_DAT; CopyStrDToStr copies 9 words).
// layout: host -- str is a pointer; the fields after it are FF3's 0x04..0x20.
struct STR_DAT                      /* 0x24 on the EE */
{
    /* 0x00 */ u_char *str;
    /* 0x04 */ int     pos_x;
    /* 0x08 */ int     pos_y;
    /* 0x0c */ int     type;
    /* 0x10 */ u_int   r;
    /* 0x14 */ u_int   g;
    /* 0x18 */ u_int   b;
    /* 0x1c */ int     alpha;
    /* 0x20 */ int     pri;
};
static_assert(offsetof(STR_DAT, pri) == sizeof(u_char *) + 0x1c, "STR_DAT.pri (EE 0x20)");
static_assert(sizeof(STR_DAT) == sizeof(u_char *) + 0x20, "STR_DAT is 0x24 bytes on the EE");

// The interpreter state of one print (FF2 MSG_EXE_CTRL).
struct MSG_EXE_CTRL                 /* 0x14 */
{
    /* 0x00 */ u_char disp_type;    // 0 horizontal lines, 1 vertical
    /* 0x04 */ int    now_bank;     // font bank 0..9
    /* 0x08 */ int    start_x;      // line start
    /* 0x0c */ int    start_y;
    /* 0x10 */ u_char font_pos;     // glyph
    /* 0x11 */ u_char font_w;       // its advance (GetFontSize)
    /* 0x12 */ u_char font_h;       // 24
    /* 0x13 */ u_char arrange;      // 0 left, 1 right, 2 centre
};
static_assert(sizeof(MSG_EXE_CTRL) == 0x14, "MSG_EXE_CTRL is 0x14 bytes on the PS2");
static_assert(offsetof(MSG_EXE_CTRL, now_bank) == 0x04, "MSG_EXE_CTRL.now_bank");
static_assert(offsetof(MSG_EXE_CTRL, start_y) == 0x0c, "MSG_EXE_CTRL.start_y");
static_assert(offsetof(MSG_EXE_CTRL, font_pos) == 0x10, "MSG_EXE_CTRL.font_pos");
static_assert(offsetof(MSG_EXE_CTRL, arrange) == 0x13, "MSG_EXE_CTRL.arrange");

// A window rectangle for arranged lines (FF2 MSG_WIN_DAT). FF3's printers
// all pass none.
struct MSG_WIN_DAT                  /* 0x10 */
{
    /* 0x00 */ float x;
    /* 0x04 */ float y;
    /* 0x08 */ float w;
    /* 0x0c */ float h;
};
static_assert(sizeof(MSG_WIN_DAT) == 0x10, "MSG_WIN_DAT is 0x10 bytes on the PS2");
static_assert(offsetof(MSG_WIN_DAT, w) == 0x08, "MSG_WIN_DAT.w");

// ── State ────────────────────────────────────────────────────────────

// Current colour white, then MsgReset (boot, pad check).
void MsgInit(void);                                         // FF3 0x0020c400
// Subtract mode off, page state cleared (message windows on close).
void MsgReset(void);                                        // FF3 0x0020c438
// Text colour of label MSG_COL_CURRENT.
void SetCurColorRGB(u_char r, u_char g, u_char b);          // FF3 0x0020deb8
// On: text is subtracted from the frame (dark text on bright backgrounds).
void MsgSetSubtractMode(u_char on);                         // FF3 0x0020dea8
// Queue the load of MSG_OBJ + language to MSG_DATA_ADRS.
void LangDataLoadReq(void);                                 // FF3 0x0020dee0

// ── Message data ─────────────────────────────────────────────────────

u_char *GetMsgDataAddr(int msg_type, int msg_id);           // FF3 0x0020de38
// Pixel width of the line at str; *next (if given) = the next line or null.
int     MsgGetLineWidth(u_char *str, u_char **next);        // FF3 0x0020dbf0
u_char  GetFontSize(u_char bank, u_char fn);                // FF3 0x0020ddf8

// ── Pages of a multi-page message (MsgPrintPage) ─────────────────────

int  MsgGetPageBreakNum(int msg_type, int msg_id);          // FF3 0x0020dac0: 0xfb codes
int  MsgGetPage(void);                                      // FF3 0x0020db48
void MsgPageReset(void);                                    // FF3 0x0020db58
void MsgSetPage(signed char page);                          // FF3 0x0020db68
void MsgPageNext(void);                                     // FF3 0x0020db80: while the wait state is 1
void MsgPagePrev(void);                                     // FF3 0x0020dbb0: same, clamped at 0
// 1: the shown page has a next one; 2: the page is being drawn; 0: last page.
int  MsgIsPageWait(void);                                   // FF3 0x0020de28

// ── Printing ─────────────────────────────────────────────────────────
// col_label 0..0x2c from the colour table, MSG_COL_CURRENT the current
// colour; alpha 0..0x80; pri 0..15; arrange 0 left (x = left edge),
// 1 right (x = right edge), 2 centred. All return the record's csr + 1.

// Text environment: alpha (subtractive in subtract mode), TEX1, CLAMP.
void SetFontEnv(void);                                      // FF3 0x0020c498
// SetFontEnv plus alpha/Z test and the Z buffer.
void SetFontEnvZ(void);                                     // FF3 0x0020c510

#ifdef REIPAN_SLPS_25544_CHS
void MsgDrawButtonPrompt(int button, int x, int y, int width, int height, int alpha);
#endif

void CopyStrDToStr(DISP_STR *s, const STR_DAT *d);          // FF3 0x0020c848
void MsgDispStrInit(DISP_STR *disp, u_char *str, int x, int y, u_char col_label, int alpha,
                    int pri, int x_wide, int y_wide);       // FF3 0x0020cb70
int  MsgSetArrange(DISP_STR *disp, int arrange);            // FF3 0x0020caf0
void MsgColChange(DISP_STR *disp, u_char col_label);        // FF3 0x0020da30
void MsgSetColorRGB(DISP_STR *disp, u_char r, u_char g, u_char b);    // FF3 0x0020daa0

// The stream of disp to its end (one_line_flg 0) or its first line break.
int  PrintMsg(DISP_STR *disp, int one_line_flg);            // FF3 0x0020c8a8
int  PrintMsgZ(DISP_STR *disp, int one_line_flg);           // FF3 0x0020c938

// One line of the stream str (SetFontEnvZ).
int  PrintMsg_ArrangeOneLine(u_char *str, int x, int y, int col_label, int alpha, int pri,
                             int x_wide, int y_wide, int arrange);    // FF3 0x0020cc30
// The whole stream str (SetFontEnv).
int  MsgPrintData(u_char *str, int x, int y, int col_label, int alpha, int pri,
                  int x_wide, int y_wide, int arrange);     // FF3 0x0020cca0
// The current page of message (type, id) (SetFontEnv / SetFontEnvZ).
// The same without setting the text environment first (the caller's own;
// the film menu's date hook calls it directly).
int  MsgPrintPageMain(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                      int x_wide, int y_wide, int arrange); // FF3 0x0020cd18
int  MsgPrintPage(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                  int x_wide, int y_wide, int arrange);     // FF3 0x0020d060
int  MsgPrintPageZ(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                   int x_wide, int y_wide);                 // FF3 0x0020d118

// Numbers from x: type 0 full-width digits (0x18 apart), 1 half-width (0xc).
void PrintNumber(int data, int x, int y, u_char col_label, u_char alpha, int pri,
                 u_char type);                              // FF3 0x0020d1d0: no leading zeros
// num digits (0x12 / 0xe apart); zero_flg 1 keeps leading zeros, 0 still
// prints a lone 0.
void PrintNumber_N(int data, int num, int x, int y, u_char col_label, u_char alpha, int pri,
                   u_char type, u_char zero_flg);           // FF3 0x0020d308
void PrintNumber_One(int data, int x, int y, u_char col_label, u_char alpha, int pri,
                     u_char type);                          // FF3 0x0020d460

#endif /* _GRAPHICS_GRAPH2D_MESSAGE_H */
