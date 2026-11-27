/* ==========================================================================
 *  album/prg/album_disp.c
 *
 *  The album edit page's state and its draw helpers (see album_disp.h).
 *  FF3 0x121cd8..0x1229a0, FF3's own code in FF2 album_disp.c's link slot:
 *  FF2's file drew the album from album_tex[] sprites; FF3 draws its pages
 *  from layouts (album_view.c's hooks) and keeps here only what the hooks
 *  ask for -- the photo, its info lines, which side and photo are shown --
 *  and the cursors that change it.
 * ======================================================================== */
#include "album/prg/album_disp.h"

#include "common/cmn_time.h"                // CdClockBcdToInt
#include "album/prg/album.h"                // album_disp_ctrl, album_cur_info, album_file, album_photo_buf, AlbumGetPhoto*, AlbumPhoto*
#include "graphics/graph2d/message.h"       // GetMsgDataAddr, PrintMsg_ArrangeOneLine, PrintNumber_N
#include "ingame/menu/menu_csr.h"           // MenuCsr*
#include "ingame/photo/photo_comp.h"        // UncompressPhoto
#include "ingame/photo/photo_make.h"        // DrawPhotoFromWorkAreaAddr, DrawSPhotoFromSmallPhotoAreaAddr2
#include "ingame/story_menu_ghost_list.h"   // SoulListMsgNo
#include "sdk/libcdvd.h"                    // sceCdCLOCK
#include "system/pad/pad.h"                 // pad_wrk
#include "album/prg/album_view.h"       // PC: AlbumViewMouse*
#include "ingame/sound/snd_ctrl.h"      // PC: SndSePlay
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

#define ALBUM_EDIT_CSR      8           // MenuCsr ids
#define ALBUM_DST_CSR       0x19
#define ALBUM_SORT_CSR      0x1a

#define ALBUM_CSR_SE        0x1a9b0000  // cursor move SE

#define ALBUM_INFO_COL      0x2d        // info line colour label

s_char album_edit_side;                         // sdata 0x372c1c
s_char album_dst_side;                          // sdata 0x372c1d
s_char album_edit_photo;                        // sdata 0x372c1e
s_char album_dst_photo[2];                      // sdata 0x372c20
s_char album_sort_key;                          // sdata 0x372c22
s_char album_sort_list[ALBUM_SORT_KEY_NUM];     // sdata 0x372c28
s_char album_sort_key_req;                      // sdata 0x372c2d

// ──────────────────────────────────────────────────────────────────────
// The photo
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00121cd8; was AlbumInOutAnimCtrl (wrong FF2 match).

void AlbumDispSetPhoto(s_char side, s_char no)
{
    album_disp_ctrl[0] = side;
    album_disp_ctrl[1] = no;
    UncompressPhoto(AlbumPhotoGetAdrNo(side, no));
}

// FF3 0x00121d10; was AlbumSaveSelSlotDisp (wrong FF2 match).

void AlbumPhotoDisp(s_char side, s_char no, int x, int y, int w, int h, u_char alpha, int pri)
{
    if (!AlbumPhotoIsUsed(side, no))
        return;

    if (album_disp_ctrl[0] != side || album_disp_ctrl[1] != no)
        AlbumDispSetPhoto(side, no);

    DrawPhotoFromWorkAreaAddr((uintptr_t)AlbumGetPhotoBuf(side), pri, PHOTO_FTYPE_INSET, x, y, w, h, alpha);
}

// FF3 0x00121e10; was AlbumSaveAlbumMaskDisp (wrong FF2 match).

void AlbumPhotoThumbDisp(s_char side, s_char no, int x, int y, int w, int h, u_char alpha, int pri)
{
    if (AlbumPhotoIsUsed(side, no))
        DrawSPhotoFromSmallPhotoAreaAddr2((uintptr_t)AlbumGetPhotoBuf(side),
                                          AlbumPhotoGetAdrNo(side, no), pri, 0, x, y, w, h, alpha);
}

// ──────────────────────────────────────────────────────────────────────
// Info lines
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00121ef0: the room's name (message type 0x7a).

void AlbumPhotoPrintRoom(s_char side, s_char no, int x, int y, int alpha, int pri)
{
    if (AlbumPhotoIsUsed(side, no))
        PrintMsg_ArrangeOneLine(GetMsgDataAddr(0x7a, AlbumGetPhoto(side, no)->room),
                                x, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
}

// FF3 0x00121fa8: "DD/MM/YY HH:MM:SS", characters step apart (the
// separators are messages 0x82/6 and 0x67/3).

void AlbumPhotoPrintDate(s_char side, s_char no, int x, int y, int step, int alpha, int pri)
{
    int date[6];

    if (!AlbumPhotoIsUsed(side, no))
        return;

    CdClockBcdToInt(date, &AlbumGetPhoto(side, no)->time);

#ifdef REIPAN_SLPS_25544_CHS
    // SLPS_255.44 0x121c6c..0x121d64 uses year, month, day.
    constexpr int first = 0, last = 2, separator_group = 0x81;
#else
    constexpr int first = 2, last = 0, separator_group = 0x82;
#endif
    PrintNumber_N(date[first], 2, x, y, ALBUM_INFO_COL, alpha, pri, 1, 1);
    PrintMsg_ArrangeOneLine(GetMsgDataAddr(separator_group, 6), x + step * 2, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
    PrintNumber_N(date[1], 2, x + step * 3, y, ALBUM_INFO_COL, alpha, pri, 1, 1);
    PrintMsg_ArrangeOneLine(GetMsgDataAddr(separator_group, 6), x + step * 5, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
    PrintNumber_N(date[last], 2, x + step * 6, y, ALBUM_INFO_COL, alpha, pri, 1, 1);

    PrintNumber_N(date[3], 2, x + step * 9, y, ALBUM_INFO_COL, alpha, pri, 1, 1);
    PrintMsg_ArrangeOneLine(GetMsgDataAddr(0x67, 3), x + step * 11, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
    PrintNumber_N(date[4], 2, x + step * 12, y, ALBUM_INFO_COL, alpha, pri, 1, 1);
    PrintMsg_ArrangeOneLine(GetMsgDataAddr(0x67, 3), x + step * 14, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
    PrintNumber_N(date[5], 2, x + step * 15, y, ALBUM_INFO_COL, alpha, pri, 1, 1);
}

// FF3 0x00122258: six digits, no leading zeros.

void AlbumPhotoPrintScore(s_char side, s_char no, int x, int y, u_char alpha, int pri)
{
    if (AlbumPhotoIsUsed(side, no))
        PrintNumber_N(AlbumGetPhoto(side, no)->score, 6, x, y, ALBUM_INFO_COL, alpha, pri, 0, 0);
}

// FF3 0x00122308: message 0x67/4.

void AlbumPhotoPrintScoreLabel(s_char side, s_char no, int x, int y, int alpha, int pri)
{
    if (AlbumPhotoIsUsed(side, no))
        PrintMsg_ArrangeOneLine(GetMsgDataAddr(0x67, 4), x, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
}

// FF3 0x001223a0: subject idx's name, when it has one.

void AlbumPhotoPrintSubject(int idx, s_char side, s_char no, int x, int y, int alpha, int pri)
{
    PICTURE_SUBJECT *subject;

    if (!AlbumPhotoIsUsed(side, no))
        return;

    subject = &AlbumGetPhoto(side, no)->subject[idx];
    if (subject->type >= 0 && subject->obj_no >= 0)
        PrintMsg_ArrangeOneLine(GetMsgDataAddr(subject->type, SoulListMsgNo(0, subject->obj_no)),
                                x, y, ALBUM_INFO_COL, alpha, pri, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Save manifest entries
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00122490.

int AlbumIngameSaveGet(void **addr)
{
    *addr = &album_ingame_save;
    return sizeof(album_ingame_save);
}

// FF3 0x001224a0.

int AlbumSaveGetInfo(void **addr)
{
    *addr = &album_cur_info[AlbumDispGetCurAlbum()];
    return sizeof(ALBUM_INFO);
}

// FF3 0x001224e0.

int AlbumSaveGetData(void **addr)
{
    *addr = &album_file[AlbumDispGetCurAlbum()];
    return sizeof(ALBUM_FILE);
}

// FF3 0x00122520.

int AlbumSaveGetPhoto(void **addr)
{
    *addr = album_photo_buf[AlbumDispGetCurAlbum()];
    return ALBUM_PHOTO_BUF_SIZE;
}

// ──────────────────────────────────────────────────────────────────────
// The shown side and photo
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00122560.

s_char AlbumDispGetCurAlbum(void)
{
    return album_dst_side != -1 ? album_dst_side : album_edit_side;
}

// FF3 0x00122578.

s_char AlbumDispGetCurPhoto(void)
{
    if (album_dst_side != -1)
        return album_dst_photo[album_dst_side];
    return album_edit_photo;
}

// ──────────────────────────────────────────────────────────────────────
// The photo grid
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001225a0; was AlbumMcMsgWinDisp (wrong FF2 match).

void AlbumEditCsrInit(void)
{
    MenuCsrInit(ALBUM_EDIT_CSR, 8, 2, 0);
    album_edit_side = 0;
    pad_wrk[0].stick_dpad = 1;
    album_edit_photo = (s_char)MenuCsrGetPos(ALBUM_EDIT_CSR);
    album_dst_side = -1;
    album_dst_photo[1] = -1;
    album_dst_photo[0] = -1;
}

// FF3 0x001225f8.

// PC: the mouse. The current page's cells are the cursor's; a cell of the
// other page moves over to it when the page can change.
static int AlbumEditCsrMouse(int lock_side)
{
    ReiPan_MenuBox box;

    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_VERTICAL, nullptr);
    for (int cell = 0; cell < 16; cell++)
    {
        if (AlbumViewMouseCell(album_edit_side, cell, &box))
            MenuMouseCsr(ALBUM_EDIT_CSR, cell, &box);
        if (lock_side != 0 || !AlbumViewMouseCell((s_char)(album_edit_side ^ 1), cell, &box))
            continue;

        const int click = ReiPan_MenuMouseClick(box.x, box.y, box.w, box.h);

        if (click || ReiPan_MenuMouseHover(box.x, box.y, box.w, box.h))
        {
            album_edit_side ^= 1;
            MenuCsrSetPos(ALBUM_EDIT_CSR, cell);
            album_edit_photo = (s_char)cell;
            SndSePlay(ALBUM_CSR_SE, 100, 0x1000, 0, nullptr, 0);
            if (click)
                ReiPan_MenuMousePress(REIPAN_MENU_CROSS);
            return 1;
        }
    }
    return 0;
}

int AlbumEditCsrUpdate(int lock_side)
{
    if (AlbumEditCsrMouse(lock_side))   // PC
        return 1;
    MenuCsrUpdate(ALBUM_EDIT_CSR, 0);
    if (!MenuCsrIsMovedSe(ALBUM_EDIT_CSR, ALBUM_CSR_SE))
        return 0;

    if (lock_side == 0 && MenuCsrIsWrapY(ALBUM_EDIT_CSR))
        album_edit_side ^= 1;
    album_edit_photo = (s_char)MenuCsrGetPos(ALBUM_EDIT_CSR);
    return 1;
}

// FF3 0x00122670.

void AlbumEditCsrRelease(void)
{
    MenuCsrRelease(ALBUM_EDIT_CSR);
    pad_wrk[0].stick_dpad = 0;
}

// ──────────────────────────────────────────────────────────────────────
// The destination grid
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00122698.

void AlbumDstCsrInit(void)
{
    s_char no;

    MenuCsrInit(ALBUM_DST_CSR, 8, 2, 0);
    album_dst_side = album_edit_side ^ 1;
    if (album_dst_photo[album_dst_side] == -1)
    {
        no = (s_char)AlbumSearchFreePhoto(album_dst_side);
        if (no < 0)
            no = 0;
        album_dst_photo[album_dst_side] = no;
    }
    MenuCsrSetPos(ALBUM_DST_CSR, album_dst_photo[album_dst_side]);
}

// FF3 0x00122740.

int AlbumDstCsrUpdate(void)
{
    MenuCsrUpdate(ALBUM_DST_CSR, 0);
    if (!MenuCsrIsMovedSe(ALBUM_DST_CSR, ALBUM_CSR_SE))
        return 0;

    album_dst_photo[album_dst_side] = (s_char)MenuCsrGetPos(ALBUM_DST_CSR);
    return 1;
}

// FF3 0x001227a0.

void AlbumDstCsrRelease(void)
{
    album_dst_side = -1;
    MenuCsrRelease(ALBUM_DST_CSR);
}

// ──────────────────────────────────────────────────────────────────────
// The sort key list
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001227c0.

void AlbumSortCsrInit(u_char mask, s_char key)
{
    s_char num;
    s_char pos;
    s_char i;

    if (mask == 0)
    {
        album_sort_key = -1;
        return;
    }

    num = 0;
    for (i = 0; i < ALBUM_SORT_KEY_NUM; i++)
    {
        album_sort_list[i] = -1;
        if ((mask >> i) & 1)
            album_sort_list[num++] = i;
    }

    MenuCsrInit(ALBUM_SORT_CSR, 1, num, 0);
    pos = (s_char)MenuCsrGetPos(ALBUM_SORT_CSR);
    if (key != -1)
    {
        for (i = 0; i < num; i++)
        {
            if (album_sort_list[i] == key)
                pos = i;
        }
    }
    MenuCsrSetPos(ALBUM_SORT_CSR, pos);
    album_sort_key = album_sort_list[pos];
}

// FF3 0x001228d8.

int AlbumSortCsrUpdate(void)
{
    if (album_sort_key < 0)
        return 0;

    // PC: the mouse. The sprite stands on the row of the key.
    {
        ReiPan_MenuBox at;

        if (AlbumViewMouseMenuBox(&at))
        {
            for (int i = 0; i < ALBUM_SORT_KEY_NUM && album_sort_list[i] >= 0; i++)
            {
                ReiPan_MenuBox box = at;

                box.y += (float)(album_sort_list[i] - album_sort_key) * ALBUM_VIEW_MOUSE_ROW_DY;
                MenuMouseCsr(ALBUM_SORT_CSR, i, &box);
            }
        }
    }

    MenuCsrUpdate(ALBUM_SORT_CSR, 0);
    if (!MenuCsrIsMovedSe(ALBUM_SORT_CSR, ALBUM_CSR_SE))
        return 0;

    album_sort_key = album_sort_list[MenuCsrGetPos(ALBUM_SORT_CSR)];
    return 1;
}

// FF3 0x00122940.

void AlbumSortCsrRelease(void)
{
    if (album_sort_key >= 0)
        MenuCsrRelease(ALBUM_SORT_CSR);
}

// FF3 0x00122968.

void AlbumSortCsrRestore(u_char mask)
{
    if (album_sort_key_req >= 0)
    {
        AlbumSortCsrInit(mask, album_sort_key_req);
        album_sort_key_req = -1;
    }
}
