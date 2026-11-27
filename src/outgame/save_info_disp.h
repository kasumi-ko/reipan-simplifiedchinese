/* ==========================================================================
 *  outgame/save_info_disp.h
 *
 *  Text and thumbnail printers of save-slot info (FF3-only, FF3
 *  0x1340f8..0x134f78), called from the item hooks of the memory-card
 *  screens: the SaveInfo* set by the load screen (outgame/save_load_scr.c)
 *  and the save screen (0x132118..), the AlbumInfo* set by the album load /
 *  save screens (0x12e388.. and 0x12fe00..). Texts come from message kinds
 *  0x69 (save / load), 0x59 (album), 0x7a and 0x82; all print in colour
 *  label 0x2d. `pri` is the hook's text item value (EE item +0x124).
 * ======================================================================== */
#ifndef _OUTGAME_SAVE_INFO_DISP_H
#define _OUTGAME_SAVE_INFO_DISP_H

#include "ingame/ingame_wrk.h"          // PLAY_TIME

#define SAVE_INFO_THUMB_TBP 0x2cc4      // VRAM of the thumbnail image
#define SAVE_INFO_THUMB_CBP 0x2bc0      // and of its clut

void SaveInfoDrawThumb(void *tim2, int x, int y, u_char alpha, int pri);           // FF3 0x001340f8
void SaveInfoPrintCardSlot(int port, int cx, int y, int w, int alpha, int pri);    // FF3 0x00134298
void SaveInfoPrintPlayTime(const PLAY_TIME *time, int x, int y, int step, int alpha, int pri); // FF3 0x00134390
void SaveInfoPrintDifficulty(int diff, int x, int y, int w, int alpha, int pri);   // FF3 0x00134510
void SaveInfoPrintChapter(int no, int x, int y, int alpha, int pri, int flag);     // FF3 0x00134650
void SaveInfoPrintPlayer(int player, int x, int y, int alpha, int pri);            // FF3 0x00134700
void SaveInfoPrintNumber(int n, int x, int y, int w, int alpha, int pri);          // FF3 0x00134780
void SaveInfoPrintInfo1D(int n, int x, int y, int alpha, int pri, int flag);       // FF3 0x00134848
void AlbumInfoPrintCardSlot(int port, int cx, int y, int w, int alpha, int pri);   // FF3 0x001348f8
void AlbumInfoPrintAlbumNo(s_char no, s_char kind, int x, int y, int step, int alpha, int pri); // FF3 0x001349f0
void AlbumInfoPrintPhotoNum(int n, int x, int y, int step, int alpha, int pri);    // FF3 0x00134b60
void AlbumViewPhotoInfoDisp(const int *date, int x, int y, int step, int alpha, int pri); // FF3 0x00134cc8

#ifdef REIPAN_SLPS_25544_CHS
struct B2D_HOOK_ARGS;
int AlbumInfoDrawPhotoLabel(const B2D_HOOK_ARGS *args);
#endif

#endif /* _OUTGAME_SAVE_INFO_DISP_H */
