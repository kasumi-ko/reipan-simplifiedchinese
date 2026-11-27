/* ==========================================================================
 *  outgame/save_info_disp.c
 *
 *  Save-slot info printers of the memory-card screens (see
 *  save_info_disp.h). FF3 0x1340f8..0x134f60, FF3's own code.
 * ======================================================================== */
#include "outgame/save_info_disp.h"

#include "graphics/graph2d/g2d_draw.h"  // SPRT_DAT, DISP_SPRT, CopySprDToSpr, DispSprD
#include "common/cmn_util.h"            // NumGetDigits
#include "graphics/graph2d/message.h"   // GetMsgDataAddr, MsgGetLineWidth, PrintMsg_ArrangeOneLine, PrintNumber_N
#include "graphics/graph2d/tim2.h"      // Tim2GetFirstPicture, MakeTim2SendPacket_3Dpkt

#define SAVE_INFO_COL   0x2d            // colour label of every text here

// Message ids of kind 0x69 (save / load texts) used here.
#define MSG_SAVE        0x69
#define MSG_ALBUM       0x59

// One line of message (kind, id) at x, y.
static void SaveInfoPrintMsg(int kind, int id, int x, int y, int alpha, int pri)
{
    PrintMsg_ArrangeOneLine(GetMsgDataAddr(kind, id), x, y, SAVE_INFO_COL, alpha, pri, 0, 0, 0);
}

// The 108x60 thumbnail of a save. Its TEX0 is rewritten to the screen's
// thumbnail VRAM (TBP0 0x2cc4, CBP 0x2bc0, TBW from TW, TCC 1, CLD 1)
// before the image and clut are sent, then it is drawn at 100/108 width.
// FF3 0x001340f8.
void SaveInfoDrawThumb(void *tim2, int x, int y, u_char alpha, int pri)
{
    TIM2_PICTUREHEADER *pic;
    SPRT_DAT  dat;
    DISP_SPRT spr;
    u_long    tex0;
    int       tw;

    if (tim2 == nullptr)
        return;

    // PC: TEX0 is 64 bits, and so must every mask and bit here be.  These were
    // written with UL constants, which are 32 bits on Windows (LLP64): 1UL <<
    // 34 and 0x3fffUL << 37 shifted past the type, and ~0x3fffUL and ~0UL >> 3
    // cleared the whole upper word -- CBP, TCC and CLD among it -- so the
    // thumbnail was sampled through a wrong CLUT setup and came out striped.
    // Only Linux's 64-bit unsigned long ever got it right.
    pic = Tim2GetFirstPicture(tim2);
    tex0 = pic->GsTex0;
    tex0 = (tex0 & ~(u_long)0x3fff) | SAVE_INFO_THUMB_TBP;                        // TBP0
    tex0 = (tex0 & ~((u_long)0x3fff << 37)) | ((u_long)SAVE_INFO_THUMB_CBP << 37);  // CBP
    tw = 1 << (int)((tex0 >> 26) & 0xf);                                          // TW
    tex0 = (tex0 & ~((u_long)0x3f << 14)) | ((u_long)((tw / 64) & 0x3f) << 14);  // TBW
    tex0 |= (u_long)1 << 34;                                                      // TCC
    tex0 = (tex0 & (~(u_long)0 >> 3)) | ((u_long)1 << 61);                        // CLD
    pic->GsTex0 = tex0;
    MakeTim2SendPacket_3Dpkt(tim2, 0, -1, -1);

    dat.tex0  = 0;
    dat.u     = 0;
    dat.v     = 0;
    dat.w     = 0x6c;
    dat.h     = 0x3c;
    dat.x     = x;
    dat.y     = y;
    dat.pri   = pri;
    dat.alpha = alpha;
    dat.flip  = 0;
    dat.bln   = 0;
    CopySprDToSpr(&spr, &dat);
    spr.scw  = 0.9259259f;              // sdata 0x36bae8 (0x3f6d097b)
    spr.sch  = 1.0f;                    // sdata 0x36baec
    spr.csx  = spr.x;
    spr.csy  = spr.y;
    spr.tex0 = pic->GsTex0;
    DispSprD(&spr);
}

// "<slot> N" (message 8, then port + 1) centred on cx.
// FF3 0x00134298.
void SaveInfoPrintCardSlot(int port, int cx, int y, int w, int alpha, int pri)
{
#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44 message 0x59/9 without the slot number.
    static u_char slot_label[] = {0x8d, 0xf0, 0xb5, 0xf3, 0x4a, 0xf3, 0x4b, 0x7d, 0xf2, 0x30, MSG_CODE_END};
    u_char *str = slot_label;
#else
    u_char *str = GetMsgDataAddr(MSG_SAVE, 8);
#endif
    int     width = MsgGetLineWidth(str, nullptr);
    int     x = cx - (width + 0x18) / 2;

    (void)w;
    PrintMsg_ArrangeOneLine(str, x, y, SAVE_INFO_COL, alpha, pri, 0, 0, 0);
    PrintNumber_N(port + 1, 1, x + width + 0xc, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
}

// hh:mm:ss, step = one character's width (message 0xb is the separator).
// FF3 0x00134390.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void SaveInfoPrintPlayTime(const PLAY_TIME *time, int x, int y, int step, int alpha, int pri)
{
    PrintNumber_N(time->hour, 2, x, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    SaveInfoPrintMsg(MSG_SAVE, 0xb, x + step * 2, y, alpha, pri);
    PrintNumber_N(time->min, 2, x + step * 3, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    SaveInfoPrintMsg(MSG_SAVE, 0xb, x + step * 5, y, alpha, pri);
    PrintNumber_N(time->sec, 2, x + step * 6, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
}

// Message 10, the separator 0xb and the difficulty's name (0xc + diff).
// FF3 0x00134510.
void SaveInfoPrintDifficulty(int diff, int x, int y, int w, int alpha, int pri)
{
    u_char *label = GetMsgDataAddr(MSG_SAVE, 10);
    int     x1 = x + MsgGetLineWidth(label, nullptr);
    u_char *sep = GetMsgDataAddr(MSG_SAVE, 0xb);
    int     sep_w = MsgGetLineWidth(sep, nullptr);

    (void)w;
    PrintMsg_ArrangeOneLine(label, x, y, SAVE_INFO_COL, alpha, pri, 0, 0, 0);
    PrintMsg_ArrangeOneLine(sep, x1, y, SAVE_INFO_COL, alpha, pri, 0, 0, 0);
    SaveInfoPrintMsg(MSG_SAVE, diff + 0xc, x1 + sep_w, y, alpha, pri);
}

// FF3 0x00134650.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void SaveInfoPrintChapter(int no, int x, int y, int alpha, int pri, int flag)
{
    if (flag == 0)
        SaveInfoPrintMsg(0x7a, no, x, y, alpha, pri);
    else
        SaveInfoPrintMsg(MSG_SAVE, 0x2a, x, y, alpha, pri);
}

// FF3 0x00134700.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void SaveInfoPrintPlayer(int player, int x, int y, int alpha, int pri)
{
    SaveInfoPrintMsg(MSG_SAVE, player + 0x10, x, y, alpha, pri);
}

// n + 1 as a number, or message 9 for n < 0.
// FF3 0x00134780.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void SaveInfoPrintNumber(int n, int x, int y, int w, int alpha, int pri)
{
    (void)w;
    if (n < 0)
    {
        SaveInfoPrintMsg(MSG_SAVE, 9, x, y, alpha, pri);
        return;
    }
    PrintNumber_N(n + 1, NumGetDigits(n + 1, 1), x, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 0);
}

// FF3 0x00134848.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void SaveInfoPrintInfo1D(int n, int x, int y, int alpha, int pri, int flag)
{
    if (flag == 0)
        SaveInfoPrintMsg(MSG_SAVE, n + 0x12, x, y, alpha, pri);
    else
        SaveInfoPrintMsg(MSG_SAVE, 0x29, x, y, alpha, pri);
}

// The album screens' copy of SaveInfoPrintCardSlot.
// FF3 0x001348f8.
void AlbumInfoPrintCardSlot(int port, int cx, int y, int w, int alpha, int pri)
{
    SaveInfoPrintCardSlot(port, cx, y, w, alpha, pri);
}

// Album message 0x26, no + 1, message 0x27, then message `kind` (0x2c for
// -2, 0x2b for -1).
// FF3 0x001349f0.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void AlbumInfoPrintAlbumNo(s_char no, s_char kind, int x, int y, int step, int alpha, int pri)
{
    s_char id;

    SaveInfoPrintMsg(MSG_ALBUM, 0x26, x, y, alpha, pri);
    PrintNumber_N(no + 1, 1, x + step + 0x10, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    SaveInfoPrintMsg(MSG_ALBUM, 0x27, x + step * 2 + 0x10, y, alpha, pri);

    id = (kind == -2) ? 0x2c : kind;
    if (kind == -1)
        id = 0x2b;
    SaveInfoPrintMsg(MSG_ALBUM, id, x + step * 3 + 0x18, y, alpha, pri);
}

// n (two album 0x27 marks for n < 0), message (0x82, 6) and 16.
// FF3 0x00134b60.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void AlbumInfoPrintPhotoNum(int n, int x, int y, int step, int alpha, int pri)
{
#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44 0x133ea8: layout origin, count/capacity, then the unit.
    constexpr int separator_group = 0x81, empty_shift = 0, leading_zero = 1;
#else
    constexpr int separator_group = 0x82, empty_shift = 1, leading_zero = 0;
    x += 4;
#endif
    if (n < 0)
    {
        int xx = x - step * empty_shift;

        for (s_char i = 0; i < 2; i++, xx += step)
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, xx, y, alpha, pri);
    }
    else
    {
        PrintNumber_N(n, 2, x, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, leading_zero);
    }
    SaveInfoPrintMsg(separator_group, 6, x + step * 2, y, alpha, pri);
    PrintNumber_N(0x10, 2, x + step * 3, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
#ifdef REIPAN_SLPS_25544_CHS
    SaveInfoPrintMsg(MSG_ALBUM, 0x2a, x + step * 5, y, alpha, pri);
#endif
}

// date[2] (0x82,6) date[1] (0x82,6) date[0], two digits each; a negative
// part prints two album 0x27 marks instead. FF3 starts the marks of the
// second and third parts at their separator's x (one step left of where
// the number goes), kept here.
// FF3 0x00134cc8.
// callcheck: GetMsgDataAddr, PrintMsg_ArrangeOneLine (SaveInfoPrintMsg)
void AlbumViewPhotoInfoDisp(const int *date, int x, int y, int step, int alpha, int pri)
{
#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44 0x134050: two fields with the month and day suffixes.
    if (date == nullptr)
        return;
    for (int part = 0; part < 2; ++part)
    {
        const int column = x + part * 4 * step;
        if (date[part + 1] < 0)
        {
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, column, y, alpha, pri);
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, column + step, y, alpha, pri);
        }
        else
        {
            PrintNumber_N(date[part + 1], 2, column, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, part);
        }
        SaveInfoPrintMsg(MSG_ALBUM, 0x28 + part, column + 2 * step, y, alpha, pri);
    }
#else
    int xx;

    if (date == nullptr)
        return;

    x += 4;
    if (date[2] < 0)
    {
        xx = x - step;
        for (int i = 0; i < 2; i++, xx += step)
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, xx, y, alpha, pri);
    }
    else
    {
        PrintNumber_N(date[2], 2, x, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    }

    xx = x + step * 2;
    SaveInfoPrintMsg(0x82, 6, xx, y, alpha, pri);
    if (date[1] < 0)
    {
        for (int i = 0; i < 2; i++, xx += step)
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, xx, y, alpha, pri);
    }
    else
    {
        PrintNumber_N(date[1], 2, x + step * 3, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    }

    xx = x + step * 5;
    SaveInfoPrintMsg(0x82, 6, xx, y, alpha, pri);
    if (date[0] < 0)
    {
        for (int i = 0; i < 2; i++, xx += step)
            SaveInfoPrintMsg(MSG_ALBUM, 0x27, xx, y, alpha, pri);
    }
    else
    {
        PrintNumber_N(date[0], 2, x + step * 6, y, SAVE_INFO_COL, (u_char)alpha, pri, 1, 1);
    }
#endif
}
