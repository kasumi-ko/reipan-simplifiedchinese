/* ==========================================================================
 *  graphics/graph2d/message.c
 *
 *  FF3's message printer (see message.h): FF2 message.c's control-code
 *  interpreter, glyph packets and number printing, with FF2's windowed,
 *  vertical and typewriter printers gone, 10 font banks instead of 5 and a
 *  page mode (MsgPrintPage). Ported from MioPan's message.c and checked
 *  function by function against FF3 0x20c400..0x20df28.
 *
 *  PC: the GS packets are built as FF3 builds them; SetFontPat also hands
 *  each glyph to the renderer (MioPan's font bridge), with the TEX0 that
 *  SetFontTex loaded last.
 * ======================================================================== */
#include "graphics/graph2d/message.h"

#include <stdio.h>                      // sprintf
#include <string.h>                     // memset
#include <libgraph.h>                   // SCE_GS_* register addresses
#include <SDL3/SDL_log.h>
#include <set>
#include <utility>

#include "graphics/graph2d/font.h"      // FontTexSendVram
#include "graphics/graph2d/g2d_draw.h"  // StartDmaDirectTrans, EndDmaDirectTrans
#include "graphics/graph2d/message_data.h"  // fntdat, msg_font_cell, msg_col, font_w_tbl
#include "graphics/gs_reg.h"            // GS_REG_SET, SetGsRegsCached, GetGsRegCached
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "reipan/rendering/reipan_renderer.h"   // PC: the glyph quad
#include "system/eeiop/cd_file_dat.h"   // MSG_OBJ
#include "system/eeiop/cddat.h"
#include "reipan/io/reipan_file.h"
#include "reipan/io/reipan_input.h"
#include "reipan/io/reipan_paths.h"
#include "system/mc/prg/mc_check_card.h"    // GetAccessMemoryCardPort
#include "system/os/eecdvd.h"           // LoadReq
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad_ctrl.h"
#include "common/get_byte.h"     // Get1Byte, Get4Byte
#include "ingame/event/prg/ev_get.h"    // EvBinChangeAddr4

// The page state of MsgPrintPage (data 0x32d688).
// layout: host -- msg is a pointer; page / state / _06 are FF3's 0x04..0x06.
struct MSG_PAGE_CTRL                /* 0xc on the EE */
{
    /* 0x00 */ u_char *msg;         // the stream printed last (another one restarts at page 0)
    /* 0x04 */ s_char  page;        // page shown
    /* 0x05 */ u_char  state;       // MsgIsPageWait: 1 next page exists, 2 drawing, 0 last page
    /* 0x06 */ u_char  _06;         // cleared only
    /* 0x08 */ int     _08;         // cleared only
};
static_assert(offsetof(MSG_PAGE_CTRL, page) == sizeof(u_char *), "MSG_PAGE_CTRL.page (EE 0x04)");

static MSG_PAGE_CTRL msg_page;      /* data 0x32d688 */
static unsigned int msg_data_size;
#ifdef REIPAN_SLPS_25544_CHS
static u_char chs_font_widths[FNTDAT_BANK_NUM][FONT_W_NUM];
static bool use_chs_font_widths;
#endif

static u_char msg_cur_col[3];       /* sdata 0x373650 (gp+0x160): SetCurColorRGB's r, g, b */
static u_char msg_subtract;         /* sdata 0x373653 (gp+0x163): MsgSetSubtractMode */

static int GetAccessMcSlot_Msg(void);

// The numbers 0xfc <n> prints (FF2 mes_ex_num_tbl). sdata 0x371248 (gp-0x22a8).
static int (*mes_ex_num_tbl[1])(void) = { GetAccessMcSlot_Msg };

// The glyph SPRITE's GIF tag: one loop, PRIM sprite + TME + ABE + FST,
// packed, 5 registers (RGBAQ, UV, XYZF2, UV, XYZF2). data 0x371250.
#define FONT_PAT_GIFTAG     0x50ab400000008001ULL
#define FONT_PAT_REGS       0x43431ULL

// PC: the TEX0 the last SetFontTex loaded, for the renderer's glyph quads.
static int font_bank_host;
static u_long font_tex0_host;

static void MsgExeCtrlInit(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_char disp_type);
static u_long *SetFontTex(u_long *pbuf, u_long tex0, int bank);
static u_long *SetFontPat(u_long *pbuf, int type, int pri, int fn, int x, int y, u_char r,
                          u_char g, u_char b, u_char a);
static void SetMsgFont(u_long **ppbuf, DISP_STR *disp, int bank, u_char fn);
static int  PrintMsgOneLine(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_long **ppbuf);
static void MesBankExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl);
static void MesColExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl);
static void MesArrangeExe(DISP_STR *disp, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl);
static void MesRetExe(DISP_STR *disp, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl);
static void MesExNumExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_long **ppbuf);

// ──────────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────────

// Reset, then the current colour white. Boot (one_Boot_Load) and the pad
// check call it. FF3 0x0020c400.

void MsgInit(void)
{
    MsgReset();
    msg_cur_col[1] = 0xff;
    msg_cur_col[2] = 0xff;
    msg_cur_col[0] = 0xff;
}

// Subtract mode off, page state cleared. The message windows call it when
// they close. FF3 0x0020c438 (not FF2's MesPassCheck, whose name it had).

void MsgReset(void)
{
    msg_subtract = 0;
    msg_page._08 = 0;
    msg_page.page = 0;
    msg_page.state = 0;
    msg_page._06 = 0;
}

// The interpreter state of one print: line start at the pen, 24x24 font,
// bank 0, no arrangement. FF3 0x0020c460; FF2 MsgExeCtrlInit (bank 1 there).

static void MsgExeCtrlInit(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_char disp_type)
{
    ctrl->start_x   = disp->pos_x;
    ctrl->start_y   = disp->pos_y;
    ctrl->disp_type = disp_type;
    ctrl->font_h    = 0x18;
    ctrl->font_w    = 0x18;
    ctrl->font_pos  = 0;
    ctrl->now_bank  = 0;
    ctrl->arrange   = 0;
}

// ──────────────────────────────────────────────────────────────────────
// GS packets
// ──────────────────────────────────────────────────────────────────────

// The text environment: alpha blend (Cs - Cd) * As + Cd, or Cd - Cs * As in
// subtract mode; TEX1 LCM, MMIN 5, K 1; CLAMP repeat. FF3 0x0020c498; FF2
// SetFontEnv (TEST / ZBUF moved to SetFontEnvZ).

void SetFontEnv(void)
{
    static const GS_REG_SET font_env[3] =           /* data 0x35c6a0 */
    {
        { 0x0000008000000044ULL, SCE_GS_ALPHA_1 },
        { 0x0000000100000141ULL, SCE_GS_TEX1_1 },
        { 0,                     SCE_GS_CLAMP_1 },
    };
    static const GS_REG_SET font_env_sub[3] =       /* data 0x35c670 */
    {
        { 0x0000000000000042ULL, SCE_GS_ALPHA_1 },
        { 0x0000000100000141ULL, SCE_GS_TEX1_1 },
        { 0,                     SCE_GS_CLAMP_1 },
    };
    GS_REG_SET env[3];

    memcpy(env, (msg_subtract == 0) ? font_env : font_env_sub, sizeof(env));
    SetGsRegsCached(env, 3, 1);
}

// SetFontEnv plus alpha test GREATER 0 and Z test GEQUAL on the 0x118 Z
// buffer. FF3 0x0020c510.

void SetFontEnvZ(void)
{
    static const GS_REG_SET font_env_z[2] =         /* data 0x35c6d0 */
    {
        { 0x000000000005000dULL, SCE_GS_TEST_1 },
        { 0x000000000a000118ULL, SCE_GS_ZBUF_1 },
    };
    GS_REG_SET env[2];

    SetFontEnv();
    memcpy(env, font_env_z, sizeof(env));
    SetGsRegsCached(env, 2, 1);
}

// A+D packet: TEXFLUSH, then TEX0_1 = tex0 (PSMT4: first with CLD/CSM set
// so the CLUT loads). FF3 0x0020c568; FF2 SetFontTex took a bank and also
// knew PSMT4HH / PSMT4HL (FF3's font pages, which load no CLUT here).

static u_long *SetFontTex(u_long *pbuf, u_long tex0, int bank)
{
    font_bank_host = bank;
    font_tex0_host = tex0;          // PC: SetFontPat's renderer quad

    pbuf[1] = SCE_GIF_PACKED_AD;
    pbuf[0] = 0x1000000000008003ULL;    // NLOOP 3, EOP, NREG 1
    pbuf[2] = 0;
    pbuf[3] = SCE_GS_TEXFLUSH;
    if ((tex0 & 0x3f00000) == 0x1400000)
    {
        pbuf[4] = (tex0 & 0xfffffffc0fffffULL) | 0x2000000001300000ULL;
        pbuf[5] = SCE_GS_TEX0_1;
        pbuf[6] = tex0 & 0xffffffffffffffULL;
        pbuf[7] = SCE_GS_TEX0_1;
    }
    else
    {
        pbuf[4] = tex0;
        pbuf[5] = SCE_GS_TEX0_1;
        pbuf[7] = 0x7f;             // A+D to 0x7f: no register (pad)
        pbuf[6] = 0;
    }
    return pbuf + 8;
}

static void DrawFontCell(const MSG_FONT_CELL &cell, int fn, u_int z, float x, float y,
                         float scale, int width, u_char r, u_char g, u_char b, u_char a)
{
    const int u = (fn % cell.cols) * cell.w;
    const int v = (fn / cell.cols) * cell.h;
    const float x1 = x + width * scale;
    const float y1 = y + cell.h * scale;
    const float xy[8] = {x, y, x1, y, x, y1, x1, y1};
    const float u0 = (float)(u + cell.off_u);
    const float v0 = (float)(v + cell.off_v);
    const float u1 = (float)(u + width - cell.off_u);
    const float v1 = (float)(v + cell.h - cell.off_v);
    const float uv[8] = {u0, v0, u1, v0, u0, v1, u1, v1};

    ReiPan_RendererSetGs2dDepth(z);
    ReiPan_RendererDrawFontQuadTex0(font_bank_host, (const sceGsTex0 *)&font_tex0_host,
                                    (const sceGsTex1 *)GetGsRegCached(SCE_GS_TEX1_1),
                                    xy, uv, r, g, b, a);
}

// One glyph: a SPRITE of cell `fn` (row-major, msg_font_cell[type]) with its
// top-left at screen pixel x, y (origin top-left, 640x448), cell-sized. Subtract mode inverts the colour.
// FF3 0x0020c628; FF2 SetFontPat (FF3: one cell table, centred y, no
// interlace halving, glyph drawn cell-wide whatever its advance).

static u_long *SetFontPat(u_long *pbuf, int type, int pri, int fn, int x, int y, u_char r,
                          u_char g, u_char b, u_char a)
{
    const MSG_FONT_CELL *cell = &msg_font_cell[type];
    int    px;
    int    py;
    u_int  z;
    int    du;
    int    dv;
    u_int *q;

    py = (y + 0x720) * 0x10;
    px = (x + 0x6c0) * 0x10;
    z  = 0xfffff - (pri & 0xfffff);
    du = (fn % cell->cols) * cell->w;
    dv = (fn / cell->cols) * cell->h;

    pbuf[1] = FONT_PAT_REGS;
    pbuf[0] = FONT_PAT_GIFTAG;
    q = (u_int *)&pbuf[2];
    if (msg_subtract == 0)
    {
        q[0] = r;
        q[1] = g;
        q[2] = b;
    }
    else
    {
        q[0] = 0xff - r;
        q[1] = 0xff - g;
        q[2] = 0xff - b;
    }
    q[3]  = a;
    q[4]  = (du + cell->off_u) * 0x10;
    q[5]  = (dv + cell->off_v) * 0x10;
    q[6]  = 0;
    q[7]  = 0;
    q[9]  = py;
    q[8]  = px;
    q[10] = z;
    q[11] = 0;
    q[12] = ((du + cell->w) - cell->off_u) * 0x10;
    q[13] = ((dv + cell->h) - cell->off_v) * 0x10;
    q[14] = 0;
    q[15] = 0;
    q[16] = px + cell->w * 0x10;
    q[17] = py + cell->h * 0x10;
    q[18] = z;
    q[19] = 0;

    // The packet is not executed on PC. x and y already use the screen origin.
    DrawFontCell(*cell, fn, z, (float)x, (float)y, 1.0f, cell->w,
                 (u_char)q[0], (u_char)q[1], (u_char)q[2], a);

    return pbuf + 12;
}

#ifdef REIPAN_SLPS_25544_CHS
// Unused glyph indices below the message control codes.
enum SlpsPrompt : u_char
{
    SLPS_PROMPT_SKIP = FONT_W_NUM,
    SLPS_PROMPT_CONFIRM,
    SLPS_PROMPT_CANCEL,
    SLPS_PROMPT_ALBUM_ERASE,
    SLPS_PROMPT_ALBUM_MENU,
    SLPS_PROMPT_L1,
    SLPS_PROMPT_R1,
    SLPS_PROMPT_FLASH,
    SLPS_PROMPT_SHUTTER
};

static bool IsSlpsPrompt(int bank, u_char fn)
{
    return use_chs_font_widths && fn >= SLPS_PROMPT_SKIP && fn <= SLPS_PROMPT_SHUTTER &&
           (bank == 0 || (bank == 8 && fn == SLPS_PROMPT_CANCEL));
}

static ReiPan_InputPrompt GetSlpsPrompt(u_char fn)
{
    unsigned int mask;
    switch (fn)
    {
    case SLPS_PROMPT_CONFIRM: mask = pad_key[PAD_KEY_OK]; break;
    case SLPS_PROMPT_CANCEL: mask = pad_key[PAD_KEY_CANCEL]; break;
    case SLPS_PROMPT_ALBUM_ERASE: mask = 0x20; break;
    case SLPS_PROMPT_ALBUM_MENU: mask = 0x80; break;
    case SLPS_PROMPT_L1: mask = 0x04; break;
    case SLPS_PROMPT_R1: mask = 0x08; break;
    case SLPS_PROMPT_FLASH: mask = pad_key[PAD_KEY_18]; break;
    case SLPS_PROMPT_SHUTTER: mask = pad_key[PAD_KEY_06]; break;
    default: mask = 0; break;
    }

    // Low eight bits of pad_wrk.held mapped to ReiPan's logical button order.
    static const int buttons[] = {9, 11, 8, 10, 4, 7, 5, 6};
    for (unsigned int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
        if (mask & (1u << i))
            return ReiPan_InputGetButtonPrompt(buttons[i]);
    return {REIPAN_PROMPT_TEXT, "-"};
}

static u_char PromptGlyph(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return 0x01 + c - 'A';
    if (c >= 'a' && c <= 'z')
        return 0x1b + c - 'a';
    if (c >= '0' && c <= '9')
        return 0x3f + c - '0';
    if (c == ' ')
        return 0;
    return 0x4d;
}

static void DrawMessagePrompt(const DISP_STR &disp, ReiPan_InputPrompt prompt, float advance,
                              float height = 24.0f)
{
    const MSG_FONT_CELL &cell = msg_font_cell[1];
    const u_int z = 0xfffff - (disp.pri & 0xfffff);
    const u_char r = msg_subtract ? 0xff - disp.r : disp.r;
    const u_char g = msg_subtract ? 0xff - disp.g : disp.g;
    const u_char b = msg_subtract ? 0xff - disp.b : disp.b;
    if (prompt.symbol != REIPAN_PROMPT_TEXT)
    {
        const float scale_w = advance / cell.w;
        const float scale_h = height / cell.h;
        const float scale = scale_w < scale_h ? scale_w : scale_h;
        const float x = disp.pos_x + (advance - cell.w * scale) * 0.5f;
        const float y = disp.pos_y + (height - cell.h * scale) * 0.5f;
        const u_char glyph = 0x49 + prompt.symbol - REIPAN_PROMPT_CIRCLE;
        DrawFontCell(cell, glyph, z, x, y, scale, cell.w, r, g, b, disp.alpha);
        return;
    }

    const auto glyph_width = [&cell](u_char glyph) {
        const int width = GetFontSize(0, glyph);
        return glyph == 0 ? 6 : (width > 0 && width <= cell.w ? width : cell.w);
    };
    int width = 0;
    for (const char *p = prompt.text; *p; ++p)
        width += glyph_width(PromptGlyph(*p));
    if (width == 0 || advance <= 2)
        return;

    // Keep the original button's advance, including in fixed-position guides.
    const float fit = (advance - 2) / width;
    const float max_scale = 0.9f * height / cell.h;
    const float scale = fit < max_scale ? fit : max_scale;
    float x = disp.pos_x + (advance - width * scale) * 0.5f;
    const float y = disp.pos_y + (height - cell.h * scale) * 0.5f;
    for (const char *p = prompt.text; *p; ++p)
    {
        const u_char glyph = PromptGlyph(*p);
        const int glyph_w = glyph_width(glyph);
        if (glyph != 0)
            DrawFontCell(cell, glyph, z, x, y, scale, glyph_w, r, g, b, disp.alpha);
        x += glyph_w * scale;
    }
}

void MsgDrawButtonPrompt(int button, int x, int y, int width, int height, int alpha)
{
    if (!use_chs_font_widths || width <= 2 || height <= 0 || alpha <= 0)
        return;

    DISP_STR disp = {};
    disp.pos_x = x;
    disp.pos_y = y;
    disp.r = disp.g = disp.b = 0x98;
    disp.alpha = alpha;
    disp.pri = 0x20;

    const int saved_bank = font_bank_host;
    const u_long saved_tex0 = font_tex0_host;
    font_bank_host = 0;
    font_tex0_host = fntdat[0].tex0;
    DrawMessagePrompt(disp, ReiPan_InputGetButtonPrompt(button), width, height);
    font_bank_host = saved_bank;
    font_tex0_host = saved_tex0;
}
#endif

// Glyph fn of `bank` at the record's pen, in its colour and alpha.
// FF3 0x0020c7b8; FF2 SetMsgFont.

static void SetMsgFont(u_long **ppbuf, DISP_STR *disp, int bank, u_char fn)
{
#ifdef REIPAN_SLPS_25544_CHS
    if (IsSlpsPrompt(bank, fn))
    {
        if (fn != SLPS_PROMPT_SKIP)
        {
            *ppbuf = SetFontTex(*ppbuf, fntdat[0].tex0, 0);
            DrawMessagePrompt(*disp, GetSlpsPrompt(fn), GetFontSize(bank, fn));
        }
        return;
    }
#endif
    *ppbuf = SetFontTex(*ppbuf, fntdat[bank].tex0, bank);
    *ppbuf = SetFontPat(*ppbuf, 1, disp->pri, fn, disp->pos_x, disp->pos_y, (u_char)disp->r,
                        (u_char)disp->g, (u_char)disp->b, (u_char)disp->alpha);
}

// ──────────────────────────────────────────────────────────────────────
// Records
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0020c848; FF2 CopyStrDToStr.

void CopyStrDToStr(DISP_STR *s, const STR_DAT *d)
{
    s->str    = d->str;
    s->pos_x  = d->pos_x;
    s->pos_y  = d->pos_y;
    s->type   = d->type;
    s->r      = d->r;
    s->g      = d->g;
    s->b      = d->b;
    s->alpha  = d->alpha;
    s->pri    = d->pri;
    s->st     = 0;
    s->x_wide = 0;
    s->y_wide = 0;
    s->csr    = 0;
}

// The record of one print: stream, pen, alpha, pri, colour label, extra
// glyph / line spacing. FF3 0x0020cb70 (FF2 inlined this in each printer).

void MsgDispStrInit(DISP_STR *disp, u_char *str, int x, int y, u_char col_label, int alpha,
                    int pri, int x_wide, int y_wide)
{
    memset(disp, 0, sizeof(DISP_STR));
    disp->str   = str;
    disp->pos_x = x;
    disp->pri   = (pri & 0xf) << 4;
    disp->pos_y = y;
    disp->alpha = alpha;
    MsgColChange(disp, col_label);
    disp->x_wide = x_wide;
    disp->y_wide = y_wide;
}

// x is the line's right edge (1) or centre (2): move the pen to its left
// end. Returns the pen x. FF3 0x0020caf0.

int MsgSetArrange(DISP_STR *disp, int arrange)
{
    if (arrange == 1)
    {
        disp->pos_x = disp->pos_x - MsgGetLineWidth(disp->str, nullptr);
    }
    else if (arrange == 2)
    {
        disp->pos_x = disp->pos_x - MsgGetLineWidth(disp->str, nullptr) / 2;
    }
    return disp->pos_x;
}

// Colour label -> the record's colour; MSG_COL_CURRENT takes the current
// colour. FF3 0x0020da30; FF2 MsgColChange (no current colour there).

void MsgColChange(DISP_STR *disp, u_char col_label)
{
    if (col_label == MSG_COL_CURRENT || col_label >= MSG_COL_NUM)
    {
        MsgSetColorRGB(disp, msg_cur_col[0], msg_cur_col[1], msg_cur_col[2]);
        return;
    }
    disp->b = msg_col[col_label].b;
    disp->r = msg_col[col_label].r;
    disp->g = msg_col[col_label].g;
}

// FF3 0x0020daa0.

void MsgSetColorRGB(DISP_STR *disp, u_char r, u_char g, u_char b)
{
    disp->r = r;
    disp->b = b;
    disp->g = g;
}

// ──────────────────────────────────────────────────────────────────────
// Printing
// ──────────────────────────────────────────────────────────────────────

// The stream to its end, or to its first line break when one_line_flg is
// set. Returns csr + 1. FF3 0x0020c8a8; FF2 PrintMsg (no SetFontEnv here:
// the callers set the environment).

int PrintMsg(DISP_STR *disp, int one_line_flg)
{
    MSG_EXE_CTRL ctrl;
    u_long      *pbuf;
    int          mask;

    mask = 1;
    if (one_line_flg == 0)
    {
        mask = 3;
    }
    MsgExeCtrlInit(disp, &ctrl, 0);
    pbuf = (u_long *)StartDmaDirectTrans();
    pbuf = SetFontTex(pbuf, fntdat[0].tex0, 0);
    do
    {
    } while ((PrintMsgOneLine(disp, &ctrl, &pbuf) & mask) != 0);
    EndDmaDirectTrans((qword *)pbuf);
    return disp->csr + 1;
}

// FF3 0x0020c938.

int PrintMsgZ(DISP_STR *disp, int one_line_flg)
{
    SetFontEnvZ();
    return PrintMsg(disp, one_line_flg);
}

// One step of the stream: a control code or a glyph. Returns 0 at the end
// (0xfb / 0xff), 2 after a line break, else 1. FF3 0x0020c970; FF2
// PrintMsgOneLine (FF3: 9 banks, 0xf9 current colour, 0xfa arrange).

static int PrintMsgOneLine(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_long **ppbuf)
{
    int draw;
    int loop;

    ctrl->now_bank = 0;
    draw = 0;
    loop = 1;
    switch (*disp->str)
    {
    case 0xf0:
    case 0xf1:
    case 0xf2:
    case 0xf3:
    case 0xf4:
    case 0xf5:
    case 0xf6:
    case 0xf7:
    case 0xf8:
        draw = 1;
        MesBankExe(disp, ctrl);
        break;
    case MSG_CODE_CURCOL:
        disp->str = disp->str + 1;
        MsgSetColorRGB(disp, msg_cur_col[0], msg_cur_col[1], msg_cur_col[2]);
        break;
    case MSG_CODE_ARRANGE:
        MesArrangeExe(disp, nullptr, ctrl);
        break;
    case MSG_CODE_PAGE:
    case MSG_CODE_END:
        loop = 0;
        disp->str = disp->str + 1;
        break;
    case MSG_CODE_EXNUM:
        MesExNumExe(disp, ctrl, ppbuf);
        break;
    case MSG_CODE_COL:
        MesColExe(disp, ctrl);
        break;
    case MSG_CODE_RET:
        loop = 2;
        MesRetExe(disp, nullptr, ctrl);
        break;
    default:
        draw = 1;
        ctrl->font_pos = Get1Byte(disp->str);
        disp->str = disp->str + 1;
        break;
    }
    if (draw)
    {
        ctrl->font_w = GetFontSize((u_char)ctrl->now_bank, ctrl->font_pos);
        SetMsgFont(ppbuf, disp, ctrl->now_bank, ctrl->font_pos);
        disp->pos_x = disp->pos_x + ctrl->font_w + disp->x_wide;
    }
    return loop;
}

// One line of a stream at x, y (arranged), with the Z test.
// FF3 0x0020cc30; FF2 PrintMsg_ArrangeOneLine.

int PrintMsg_ArrangeOneLine(u_char *str, int x, int y, int col_label, int alpha, int pri,
                            int x_wide, int y_wide, int arrange)
{
    DISP_STR disp;

    MsgDispStrInit(&disp, str, x, y, (u_char)col_label, alpha, pri, x_wide, y_wide);
    MsgSetArrange(&disp, arrange);
    PrintMsgZ(&disp, 1);
    return disp.csr + 1;
}

// A whole stream at x, y (arranged), without the Z test. The first line
// sets the arrangement; later lines start at x. FF3 0x0020cca0.

int MsgPrintData(u_char *str, int x, int y, int col_label, int alpha, int pri, int x_wide,
                 int y_wide, int arrange)
{
    DISP_STR disp;

    MsgDispStrInit(&disp, str, x, y, (u_char)col_label, alpha, pri, x_wide, y_wide);
    MsgSetArrange(&disp, arrange);
    SetFontEnv();
    PrintMsg(&disp, 1);
    return disp.csr + 1;
}

// The current page of message (type, id): the pages are the parts between
// 0xfb codes; only the glyphs of page msg_page.page draw. Ends with the
// page state 1 when a later page exists, 0 at the message end (which also
// pulls an out-of-range page back by one). A different message than the
// last one starts at page 0. Sends the font pages first. FF3 0x0020cd18.

int MsgPrintPageMain(int msg_type, int msg_id, int x, int y, int col_label, int alpha,
                     int pri, int x_wide, int y_wide, int arrange)
{
    DISP_STR     disp;
    MSG_EXE_CTRL ctrl;
    u_long      *pbuf;
    int          loop;
    int          draw;
    u_char       page;

    memset(&disp, 0, sizeof(disp));
    MsgDispStrInit(&disp, GetMsgDataAddr(msg_type, msg_id), x, y, (u_char)col_label, alpha, pri,
                   x_wide, y_wide);
    loop = 1;
    draw = 0;
    page = 0;
    MsgSetArrange(&disp, arrange);
    MsgExeCtrlInit(&disp, &ctrl, 0);
    FontTexSendVram();
    SetFontEnvZ();
    pbuf = (u_long *)StartDmaDirectTrans();
    pbuf = SetFontTex(pbuf, fntdat[0].tex0, 0);
    if (msg_page.msg != disp.str)
    {
        MsgPageReset();
    }
    msg_page.msg = disp.str;
    do
    {
        ctrl.now_bank = 0;
        switch (*disp.str)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xf7:
        case 0xf8:
            MesBankExe(&disp, &ctrl);
            draw = 1;
            break;
        case MSG_CODE_CURCOL:
            disp.str = disp.str + 1;
            MsgSetColorRGB(&disp, msg_cur_col[0], msg_cur_col[1], msg_cur_col[2]);
            break;
        case MSG_CODE_ARRANGE:
            MesArrangeExe(&disp, nullptr, &ctrl);
            break;
        case MSG_CODE_PAGE:
            disp.str = disp.str + 1;
            page = (u_char)(page + 1);
            disp.pos_x = ctrl.start_x;
            disp.pos_y = ctrl.start_y;
            if (msg_page.page < page)
            {
                loop = 0;
                msg_page.state = 1;
            }
            break;
        case MSG_CODE_EXNUM:
            MesExNumExe(&disp, &ctrl, &pbuf);
            break;
        case MSG_CODE_COL:
            MesColExe(&disp, &ctrl);
            break;
        case MSG_CODE_RET:
            MesRetExe(&disp, nullptr, &ctrl);
            break;
        case MSG_CODE_END:
            disp.str = disp.str + 1;
            msg_page.page = (s_char)(msg_page.page - 1);
            if (msg_page.page < 0)
            {
                msg_page.page = 0;
            }
            disp.pos_x = ctrl.start_x;
            loop = 0;
            disp.pos_y = ctrl.start_y;
            msg_page.state = 0;
            msg_page._06 = 0;
            break;
        default:
            ctrl.font_pos = Get1Byte(disp.str);
            disp.str = disp.str + 1;
            draw = 1;
            break;
        }
        if (msg_page.page == page && loop)
        {
            msg_page.state = 2;
            if (draw)
            {
                ctrl.font_w = GetFontSize((u_char)ctrl.now_bank, ctrl.font_pos);
                SetMsgFont(&pbuf, &disp, ctrl.now_bank, ctrl.font_pos);
                disp.pos_x = disp.pos_x + ctrl.font_w + disp.x_wide;
            }
        }
        draw = 0;
    } while (loop);
    EndDmaDirectTrans((qword *)pbuf);
    return disp.csr + 1;
}

// FF3 0x0020d060.

int MsgPrintPage(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                 int x_wide, int y_wide, int arrange)
{
    SetFontEnv();
    return MsgPrintPageMain(msg_type, msg_id, x, y, col_label, alpha, pri, x_wide, y_wide,
                            arrange);
}

// FF3 0x0020d118.

int MsgPrintPageZ(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                  int x_wide, int y_wide)
{
    SetFontEnvZ();
    return MsgPrintPageMain(msg_type, msg_id, x, y, col_label, alpha, pri, x_wide, y_wide, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Numbers
// ──────────────────────────────────────────────────────────────────────

// Up to 7 digits from x, no leading zeros; 0 prints nothing. FF3 0x0020d1d0;
// FF2 PrintNumber (asserts dropped).

void PrintNumber(int data, int x, int y, u_char col_label, u_char alpha, int pri, u_char type)
{
    int    i;
    int    j;
    int    ten_tmp;
    u_char set_flg;
    int    x_half;

    set_flg = 0;
    i = 7;
    x_half = x;
    do
    {
        i = i - 1;
        ten_tmp = 1;
        for (j = i; j > 0; j--)
        {
            ten_tmp = ten_tmp * 10;
        }
        if (data / ten_tmp != 0)
        {
            set_flg = 1;
        }
        if (set_flg)
        {
            if (type == 0)
            {
                PrintNumber_One(data / ten_tmp, x, y, col_label, alpha, pri, 0);
            }
            else if (type == 1)
            {
                PrintNumber_One(data / ten_tmp, x_half, y, col_label, alpha, pri, 1);
            }
            x_half = x_half + 0xc;
            x = x + 0x18;
        }
    } while (0 < i);
}

// num digits from x. FF3 0x0020d308; FF2 PrintNumber_N (asserts dropped).

void PrintNumber_N(int data, int num, int x, int y, u_char col_label, u_char alpha, int pri,
                   u_char type, u_char zero_flg)
{
    int    i;
    int    j;
    int    ten_tmp;
    u_char set_flg;
    int    x_half;

    set_flg = (zero_flg == 1);
    x_half = x;
    while (0 < num)
    {
        ten_tmp = 1;
        i = num - 1;
        for (j = i; j > 0; j--)
        {
            ten_tmp = ten_tmp * 10;
        }
        if (data / ten_tmp != 0)
        {
            set_flg = 1;
        }
        if (zero_flg == 0 && data == 0 && num == 1)
        {
            set_flg = 1;
        }
        if (set_flg)
        {
            if (type == 0)
            {
                PrintNumber_One(data / ten_tmp, x, y, col_label, alpha, pri, 0);
            }
            else if (type == 1)
            {
                PrintNumber_One(data / ten_tmp, x_half, y, col_label, alpha, pri, 1);
            }
        }
        x_half = x_half + 0xe;
        x = x + 0x12;
        num = i;
    }
}

// One digit (data % 10): full-width glyphs 0x35.. (type 0) or half-width
// 0x3f.. (type 1), with the Z test. FF3 0x0020d460; FF2 PrintNumber_One.

void PrintNumber_One(int data, int x, int y, u_char col_label, u_char alpha, int pri,
                     u_char type)
{
    u_char   str[2];
    DISP_STR disp;
    u_char   em_size_tbl[10] = { 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e };
    u_char   normal_tbl[10]  = { 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };

    // PC: FF3 leaves the record (and str[0] for another type) uninitialised;
    // nothing it leaves out is read for a single glyph.
    memset(&disp, 0, sizeof(disp));
    str[0] = 0;

    disp.str = str;
    if (type == 0)
    {
        str[0] = em_size_tbl[data % 10];
    }
    else if (type == 1)
    {
        str[0] = normal_tbl[data % 10];
    }
    disp.pri = (pri & 0xf) << 4;
    str[1] = MSG_CODE_END;
    disp.alpha = alpha;
    disp.pos_x = x;
    disp.pos_y = y;
    MsgColChange(&disp, col_label);
    PrintMsgZ(&disp, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Control codes
// ──────────────────────────────────────────────────────────────────────

// 0xf0..0xf8 <g>: glyph g of bank 1..9. FF3 0x0020d558; FF2 MesBankExe.

static void MesBankExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl)
{
    u_char bank_tbl[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    u_char b0;

    b0 = Get1Byte(disp->str);
    disp->str = disp->str + 1;
    ctrl->font_pos = Get1Byte(disp->str);
    ctrl->now_bank = bank_tbl[(u_char)(b0 + 0x11)];
    disp->str = disp->str + 1;
}

// 0xfd <c>: colour label c. FF3 0x0020d5f0; FF2 MesColExe.

static void MesColExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl)
{
    u_char col_label;

    disp->str = disp->str + 1;
    col_label = Get1Byte(disp->str);
    disp->str = disp->str + 1;
    MsgColChange(disp, col_label);
}

// 0xfa <a> <?>: line arrangement against a window; without one (every FF3
// print) the code and two bytes are skipped. FF3 0x0020d638; FF2
// MesArrangeExe (FF2's code was 0xf9).

static void MesArrangeExe(DISP_STR *disp, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl)
{
    u_char *dat_addr;
    float   off;

    dat_addr = disp->str;
    disp->str = dat_addr + 1;
    if (win == nullptr)
    {
        disp->str = dat_addr + 3;
        return;
    }
    ctrl->arrange = Get1Byte(dat_addr + 1);
    disp->str = disp->str + 1;
    if (ctrl->arrange == 2)
    {
        disp->pos_x = (int)((win->x + win->w * 0.5f) - (float)(MsgGetLineWidth(disp->str, nullptr) / 2));
    }
    else if (ctrl->arrange == 1)
    {
        off = win->x - (float)ctrl->start_x;
        disp->pos_x = (int)(((float)ctrl->start_x + win->w + off + off)
                            - (float)MsgGetLineWidth(disp->str, nullptr));
    }
    else if (ctrl->arrange == 0)
    {
        disp->pos_x = ctrl->start_x;
    }
}

// 0xfe: line break: back to the line start (or the window arrangement),
// then down by font_h + y_wide (or left, vertical text). FF3 0x0020d750;
// FF2 MesRetExe.

static void MesRetExe(DISP_STR *disp, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl)
{
    u_char *msg_addr;
    u_char  disp_type;
    float   off;

    msg_addr = disp->str + 1;
    disp->str = msg_addr;
    if (win == nullptr)
    {
        disp_type = ctrl->disp_type;
        if (disp_type == 0)
        {
            disp->pos_x = ctrl->start_x;
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == 1)
        {
            disp->pos_y = ctrl->start_y;
            disp_type = ctrl->disp_type;
        }
    }
    else
    {
        disp_type = ctrl->arrange;
        if (disp_type == 2)
        {
            disp->pos_x = (int)((win->x + win->w * 0.5f) - (float)(MsgGetLineWidth(msg_addr, nullptr) / 2));
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == 1)
        {
            off = win->x - (float)ctrl->start_x;
            disp->pos_x = (int)(((float)ctrl->start_x + win->w + off + off)
                                - (float)MsgGetLineWidth(msg_addr, nullptr));
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == 0)
        {
            disp->pos_x = ctrl->start_x;
            disp_type = ctrl->disp_type;
        }
        else
        {
            disp_type = ctrl->disp_type;
        }
    }
    if (disp_type == 0)
    {
        disp->pos_y = disp->pos_y + ctrl->font_h + disp->y_wide;
    }
    else if (disp_type == 1)
    {
        disp->pos_x = disp->pos_x - (ctrl->font_w + disp->x_wide);
    }
}

// 0xfc <n>: the number mes_ex_num_tbl[n] returns, as half-width digits of
// the current bank. FF3 0x0020d8b0; FF2 MesExNumExe.

static void MesExNumExe(DISP_STR *disp, MSG_EXE_CTRL *ctrl, u_long **ppbuf)
{
    static const u_char normal_tbl[10] =            /* data 0x35c7a0 */
        { 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };
    char   cwo[256];
    int    num;
    int    i;
    u_char ex;

    disp->str = disp->str + 1;
    ex = Get1Byte(disp->str);
    disp->str = disp->str + 1;
    num = 0;
    // FF3 tests mes_ex_num_tbl[ex] (reading past the one entry) before
    // ex == 0; only entry 0 is ever called, so only it is read here.
    if (ex == 0 && mes_ex_num_tbl[0] != nullptr)
    {
        num = mes_ex_num_tbl[0]();
    }
    sprintf(cwo, "%d", num);
    for (i = 0; cwo[i] != '\0'; i++)
    {
        ctrl->font_pos = normal_tbl[cwo[i] - '0'];
        ctrl->font_w = GetFontSize((u_char)ctrl->now_bank, ctrl->font_pos);
        SetMsgFont(ppbuf, disp, ctrl->now_bank, ctrl->font_pos);
        if (ctrl->disp_type == 0)
        {
            disp->pos_x = disp->pos_x + ctrl->font_w + disp->x_wide;
        }
        else if (ctrl->disp_type == 1)
        {
            disp->pos_y = disp->pos_y + ctrl->font_h + disp->y_wide;
        }
    }
}

// The memory card slot (1, 2; 0 for none). FF3 0x0020da10 (Ghidra had it
// inside MesExNumExe); FF2 GetAccessMcSlot_Msg.

static int GetAccessMcSlot_Msg(void)
{
    return GetAccessMemoryCardPort() + 1;
}

// ──────────────────────────────────────────────────────────────────────
// Pages
// ──────────────────────────────────────────────────────────────────────

// Page breaks (0xfb) in message (type, id). Use the same control lengths
// as the printer, including 0xfc followed by one number selector.

int MsgGetPageBreakNum(int msg_type, int msg_id)
{
    u_char *p;
    int     loop;
    int     num;

    p = GetMsgDataAddr(msg_type, msg_id);
    loop = 1;
    num = 0;
    while (loop)
    {
        switch (*p)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xf7:
        case 0xf8:
        case MSG_CODE_EXNUM:
        case MSG_CODE_COL:
            p = p + 2;
            break;
        case MSG_CODE_ARRANGE:
            p = p + 3;
            break;
        case MSG_CODE_PAGE:
            p = p + 1;
            num = num + 1;
            break;
        case MSG_CODE_END:
            p = p + 1;
            loop = 0;
            break;
        default:
            p = p + 1;
            break;
        }
    }
    return num;
}

// FF3 0x0020db48.

int MsgGetPage(void)
{
    return msg_page.page;
}

// FF3 0x0020db58.

void MsgPageReset(void)
{
    msg_page.page = 0;
}

// FF3 0x0020db68.

void MsgSetPage(signed char page)
{
    msg_page.page = page;
}

// FF3 0x0020db80.

void MsgPageNext(void)
{
    if (msg_page.state == 1)
    {
        msg_page.page = (s_char)(msg_page.page + 1);
    }
}

// FF3 0x0020dbb0.

void MsgPagePrev(void)
{
    if (msg_page.state == 1)
    {
        msg_page.page = (s_char)(msg_page.page - 1);
        if (msg_page.page < 0)
        {
            msg_page.page = 0;
        }
    }
}

// FF3 0x0020de28.

int MsgIsPageWait(void)
{
    return msg_page.state;
}

// ──────────────────────────────────────────────────────────────────────
// Message data
// ──────────────────────────────────────────────────────────────────────

// Pixel width of the line at str: glyph advances (0xfc numbers included)
// up to a line break (*next = the next line) or the end (*next = null).
// FF3 0x0020dbf0; FF2 GetMsgLineLength (0xf9 is one byte in FF3).

int MsgGetLineWidth(u_char *str, u_char **next)
{
    u_char bank_tbl[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    static const u_char normal_tbl[10] =            /* data 0x35c800 */
        { 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };
    char   cwo[256];
    int    width;
    int    loop;
    int    draw;
    int    num;
    int    i;
    u_char bank;
    u_char fn;
    u_char ex;

    width = 0;
    loop = 1;
    draw = 0;
    fn = 0;
    num = 0;
    do
    {
        bank = 0;
        switch (*str)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xf7:
        case 0xf8:
            draw = 1;
            bank = Get1Byte(str);
            str = str + 1;
            fn = Get1Byte(str);
            str = str + 1;
            bank = bank_tbl[(u_char)(bank + 0x11)];
            break;
        case MSG_CODE_CURCOL:
            str = str + 1;
            break;
        case MSG_CODE_ARRANGE:
            str = str + 3;
            break;
        case MSG_CODE_COL:
            str = str + 2;
            break;
        case MSG_CODE_PAGE:
        case MSG_CODE_END:
            if (next != nullptr)
            {
                *next = nullptr;
            }
            loop = 0;
            break;
        case MSG_CODE_EXNUM:
            str = str + 1;
            ex = Get1Byte(str);
            str = str + 1;
            // See MesExNumExe on the table test.
            if (ex == 0 && mes_ex_num_tbl[0] != nullptr)
            {
                num = mes_ex_num_tbl[0]();
            }
            sprintf(cwo, "%d", num);
            for (i = 0; cwo[i] != '\0'; i++)
            {
                width = width + GetFontSize(0, normal_tbl[cwo[i] - '0']);
            }
            break;
        case MSG_CODE_RET:
            if (next != nullptr)
            {
                *next = str + 1;
            }
            loop = 0;
            break;
        default:
            draw = 1;
            fn = Get1Byte(str);
            str = str + 1;
            break;
        }
        if (draw)
        {
            width = width + GetFontSize(bank, fn);
        }
        draw = 0;
    } while (loop);
    return width;
}

// Advance of glyph fn in `bank`. FF3 0x0020ddf8; FF2 GetFontSize (a
// switch over 5 tables there).

u_char GetFontSize(u_char bank, u_char fn)
{
#ifdef REIPAN_SLPS_25544_CHS
    if (IsSlpsPrompt(bank, fn))
    {
        if (fn == SLPS_PROMPT_SKIP)
            return 0;
        if (fn == SLPS_PROMPT_CANCEL)
            return chs_font_widths[8][0x28];
        if (fn == SLPS_PROMPT_L1)
            return chs_font_widths[0][0x0c] + chs_font_widths[0][0x40];
        if (fn == SLPS_PROMPT_R1)
            return chs_font_widths[0][0x12] + chs_font_widths[0][0x40];
        return 24;
    }
#endif
    if (bank >= FNTDAT_BANK_NUM || fn >= FONT_W_NUM)
        return 24;
#ifdef REIPAN_SLPS_25544_CHS
    if (use_chs_font_widths)
        return chs_font_widths[bank][fn];
#endif
    return (u_char)font_w_tbl[bank][fn];
}

#ifdef REIPAN_SLPS_25544_CHS
static void ApplySlpsButtonPrompts(int group, int id, u_char *message, unsigned int length)
{
    // Offsets within the SLPS_255.44 messages. Some crosses are redactions
    // in documents, so only the confirmed control instructions are patched.
    static const struct
    {
        int group;
        int id;
        unsigned int offset;
        u_char original;
        SlpsPrompt prompt;
        u_char trailing;
    } changes[] = {
        {0x04, 0x0b,  20, 0x49, SLPS_PROMPT_CONFIRM, 0},
        {0x04, 0x26,  59, 0x49, SLPS_PROMPT_CONFIRM, 0},
        {0x1c, 0x5c,  80, 0x4c, SLPS_PROMPT_FLASH, 0},
        {0x1c, 0x5c, 227, 0x4c, SLPS_PROMPT_FLASH, 0},
        {0x1c, 0x5f,  76, 0x4c, SLPS_PROMPT_FLASH, 0},
        {0x1c, 0x62,  78, 0x49, SLPS_PROMPT_SHUTTER, 0},
        {0x1c, 0x62, 106, 0x49, SLPS_PROMPT_SHUTTER, 0},
        {0x53, 0x0f, 118, 0x49, SLPS_PROMPT_CONFIRM, 0},
        {0x59, 0x32,  72, 0x4c, SLPS_PROMPT_ALBUM_MENU, 0},
        {0x59, 0x35,  20, 0x49, SLPS_PROMPT_CONFIRM, 0},
        {0x59, 0x35,  49, 0x4b, SLPS_PROMPT_ALBUM_ERASE, 0},
        {0x59, 0x35,  75, 0x4c, SLPS_PROMPT_ALBUM_MENU, 0},
        {0x59, 0x35,  92, 0x28, SLPS_PROMPT_CANCEL, 0},
        {0x59, 0x35, 121, 0x0c, SLPS_PROMPT_L1, 0x40},
        {0x59, 0x35, 125, 0x12, SLPS_PROMPT_R1, 0x40},
    };

    for (const auto &change : changes)
    {
        if (change.group != group || change.id != id || change.offset >= length)
            continue;
        u_char *at = message + change.offset;
        if (*at != change.original)
            continue;
        if (change.trailing != 0)
        {
            if (change.offset + 1 >= length || at[1] != change.trailing)
                continue;
            at[1] = SLPS_PROMPT_SKIP;
        }
        *at = change.prompt;
    }
}
#endif

// Message `msg_id` of group `msg_type` in the loaded MSG_OBJ: two tables of
// offsets from the data start. FF3 0x0020de38; FF2 GetMsgDataAddr.
static u_char *FindMessage(u_char *top, unsigned int size, int type, int id)
{
    if (top == nullptr || size < 8 || type < 0 || id < 0)
        return nullptr;

    const auto read_offset = [top](unsigned int at) {
        unsigned int offset;
        memcpy(&offset, top + at, sizeof(offset));
        return offset;
    };
    const unsigned int table_size = read_offset(0);
    if (table_size < 4 || table_size > size - 4 || table_size % 4 != 0 ||
        (unsigned int)type >= table_size / 4)
        return nullptr;

    const unsigned int group = read_offset((unsigned int)type * 4);
    if (group < table_size || group > size - 4)
        return nullptr;

    // Group IDs can alias the same table (the Chinese card groups 0x82/0x83).
    unsigned int group_end = size;
    for (unsigned int at = 0; at < table_size; at += 4)
    {
        const unsigned int offset = read_offset(at);
        if (offset > group && offset < group_end)
            group_end = offset;
    }

    const unsigned int first = read_offset(group);
    if (first <= group || first >= group_end || (first - group) % 4 != 0 ||
        (unsigned int)id >= (first - group) / 4)
        return nullptr;

    const unsigned int entry = group + (unsigned int)id * 4;
    const unsigned int start = read_offset(entry);
    unsigned int end = group_end;
    for (unsigned int at = entry + 4; at < first; at += 4)
    {
        const unsigned int offset = read_offset(at);
        if (offset == start)
            continue;
        if (offset < start || offset > group_end)
            return nullptr;
        end = offset;
        break;
    }
    if (start < first || start >= end)
        return nullptr;

    for (unsigned int at = start; at < end; )
    {
        const u_char code = top[at++];
        if (code == MSG_CODE_END)
        {
#ifdef REIPAN_SLPS_25544_CHS
            if (use_chs_font_widths)
                ApplySlpsButtonPrompts(type, id, top + start, at - start - 1);
#endif
            return top + start;
        }
        if (code == MSG_CODE_ARRANGE)
            at += 2;
        else if ((code >= MSG_CODE_BANK1 && code <= MSG_CODE_BANK9) ||
                 code == MSG_CODE_EXNUM || code == MSG_CODE_COL)
            at++;
    }
    return nullptr;
}

u_char *GetMsgDataAddr(int msg_type, int msg_id)
{
    u_char *top = (u_char *)ReiPan_GetHostPointer(MSG_DATA_ADRS);
    if (u_char *message = FindMessage(top, msg_data_size, msg_type, msg_id))
    {
#ifdef REIPAN_SLPS_25544_CHS
        if (use_chs_font_widths && msg_type == 0x56)
        {
            // Use the native zero glyph for the two mixed-script film names.
            static u_char film07[] = {0xf0, 0xd1, 0xf3, 0x73, 0x96, 0xf0, 0xb2, 0x75, MSG_CODE_END};
            static u_char film90[] = {0xf3, 0x77, 0xf0, 0xd1, 0x96, 0xf0, 0xb2, 0x75, MSG_CODE_END};
            if (msg_id == 0)
                return film07;
            if (msg_id == 3)
                return film90;
        }
#endif
        return message;
    }

    static u_char blank[] = {0xff, 0, 0, 0};
    static std::set<std::pair<int, int>> reported;
    if (reported.size() < 256 && reported.emplace(msg_type, msg_id).second)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ReiPan: message unavailable (group 0x%x, index 0x%x)", msg_type, msg_id);
    return blank;
}

// FF3 0x0020dea8.

void MsgSetSubtractMode(u_char on)
{
    msg_subtract = on;
}

// The colour of label MSG_COL_CURRENT. FF3 0x0020deb8.

void SetCurColorRGB(u_char r, u_char g, u_char b)
{
    msg_cur_col[1] = g;
    msg_cur_col[2] = b;
    msg_cur_col[0] = r;
}

// FF3 0x0020dee0 (inferred in mission_ctl.c before; it loads this file's data).

void LangDataLoadReq(void)
{
    const int file = MSG_OBJ + GetSysWrk()->GetLanguage();
    const unsigned int size = GetFileSize(file);
    msg_data_size = size <= FONT_TEX_ADRS - MSG_DATA_ADRS ? size : 0;
    if (msg_data_size == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ReiPan: invalid message archive size %u", size);
        return;
    }
#ifdef REIPAN_SLPS_25544_CHS
    if (!use_chs_font_widths)
    {
        char path[1024];
        if (ReiPan_PathResolveData("font_widths.bin", path, sizeof(path)) &&
            ReiPan_FileSize(path) == sizeof(chs_font_widths) &&
            ReiPan_FileReadAt(path, 0, chs_font_widths, sizeof(chs_font_widths)))
        {
            use_chs_font_widths = true;
            for (const auto &bank : chs_font_widths)
                for (u_char width : bank)
                    if (width > 24)
                        use_chs_font_widths = false;
        }
        if (!use_chs_font_widths)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ReiPan: Chinese font widths unavailable");
    }
#endif
    LoadReq(file, ReiPan_GetHostPointer(MSG_DATA_ADRS));
}
