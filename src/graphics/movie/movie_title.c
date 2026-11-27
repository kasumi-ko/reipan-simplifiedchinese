/* ==========================================================================
 *  graphics/movie/movie_title.c
 *
 *  Movie captions, FF3 0x218d90..0x2193a0 (see movie_title.h). Ported from
 *  MioPan's FF2 movie_title.c and checked against FF3, which differs:
 *  - the *MovieWithTitle wrappers (FF2 movie.c) live here, and Play takes
 *    the effect data, the subtitle option, the skip and the pad mask;
 *  - MovieTitleMain / DispMain take the subtitle option (FF2 read opt_wrk)
 *    and DispMain an alpha; the always-shown check is a function of
 *    movie_title_dat.c, and the caption bank is 0x7e;
 *  - the plate's floor is 48 wide (FF2 204; the x it pins short lines to is
 *    still 218) and its sprites are drawn with r g b 0;
 *  - no asserts.
 *  0x002192b8 / 0x00219380 are the static initializer of
 *  movie_title_base_tex and its global constructor; a constant initializer
 *  stands in for both.
 * ======================================================================== */
#include "graphics/movie/movie_title.h"

#include "graphics/graph2d/font.h"      // FontExTexSendVram
#include "graphics/graph2d/message.h"   // GetMsgDataAddr, MsgGetLineWidth, PrintMsg_ArrangeOneLine
#include "graphics/effect/effect_oth.h" // ScreenFlashDraw
#include "graphics/movie/movie.h"       // StartMovie, MovieDispMain, EndMovie, MovieCountGet
#include "graphics/movie/movie_title_dat.h" // GetMovieTitleDat, MovieTitleIsEveryDisp
#include "graphics/scene/scene.h"       // GetFileNoFromSceneNo, SceneDatGetValue
#include "graphics/scene/scene_effect.h"    // SceneMovieEffectMain
#ifdef REIPAN_SLPS_25544_CHS
#include "graphics/movie/slps_captions.h"
#include "system/playpss/playpss.h"    // playPssGetFrameIndex
#endif

#define MOVIE_TITLE_CENTER_X    320.0f  // captions are centred here
#define MOVIE_TITLE_BASE_MIN_W  48.0f   // narrowest plate
#define MOVIE_TITLE_BASE_MIN_X  218.0f  // where a plate at the floor starts

static MOVIE_TITLE_CTRL movie_title_ctrl;   // sbss 0x3736e0

// FF3 0x002192b8 (MovieTitle_static_init), from its stores.
SPRT_DAT movie_title_base_tex[3] =      // bss 0x3f6138
{
    // tex0                 u   v   w   h   x  y  pri alpha flip bln
    { 0x2007c7655540bd5cull,  1,  1, 12, 30, 0, 0, 0, 0x80, 0, 1 },  // left cap
    { 0x2007c7655540bd5cull, 15,  1, 12, 30, 0, 0, 0, 0x80, 0, 1 },  // middle
    { 0x2007c7655540bd5cull,  1,  1, 12, 30, 0, 0, 0, 0x80, 2, 1 },  // right cap
};

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00218d90; FF2 movie.c InitMovieWithTitle (which called InitMovie
// with the scene's volume).

int InitMovieWithTitle(int scene_no, int audio_flg)
{
    int ret = StartMovie(GetFileNoFromSceneNo(scene_no), SceneDatGetValue(scene_no), audio_flg);

    MovieTitleInit(scene_no);
    return ret;
}

// FF3 0x00218df8; FF2 movie.c PlayMovieWithTitle (the effects were
// one_Story_Movie_Main's there).

int PlayMovieWithTitle(void *eff_data, int subtitle_on, int skip, u_int pad)
{
    int ret = MovieDispMain(skip, (int)pad);
    int cnt = MovieCountGet();

    if (eff_data != nullptr)
    {
        SceneMovieEffectMain(cnt, eff_data);
        ScreenFlashDraw();
    }
    if (ret == 0)
    {
#ifdef REIPAN_SLPS_25544_CHS
        MovieTitleMain(playPssGetFrameIndex(), subtitle_on);
#else
        MovieTitleMain(cnt, subtitle_on);
#endif
    }
    return ret;
}

// FF3 0x00218e78.

void EndMovieWithTitle(void)
{
    MovieTitleEnd();
    EndMovie();
}

// FF3 0x00218eb0.

void MovieTitleInit(int scene_no)
{
    movie_title_ctrl.scene_no = scene_no;
}

// FF3 0x00218eb8.

void MovieTitleMain(int timer, int subtitle_on)
{
#ifdef REIPAN_SLPS_25544_CHS
    if (subtitle_on && !SlpsDrawSceneCaption(movie_title_ctrl.scene_no, timer))
        SlpsDrawVoiceCaption(MOVIE_TITLE_DISP_Y, MOVIE_TITLE_MSG_COL);
#else
    MOVIE_TITLE_DAT *data    = GetMovieTitleDat(movie_title_ctrl.scene_no);
    int              tbl_pos = GetMovieTitleDatTblPos(data, timer);

    if (tbl_pos != -1)
    {
        MovieTitleDispMain(MOVIE_TITLE_MSG_TYPE, data[tbl_pos].msg_id, MOVIE_TITLE_DISP_Y,
                           MOVIE_TITLE_MSG_COL, 1, 0x80, subtitle_on);
    }
#endif
}

// FF3 0x00218f50.

void MovieTitleEnd(void)
{
}

// The plate is sized from the line but never below the floor, and a plate at
// the floor is pinned at x 218 instead of following the text.
// FF3 0x00218f58.

void MovieTitleDispMain(int msg_type, int msg_id, int y, int msg_col,
                        char base_disp_flg, u_char alpha, int subtitle_on)
{
    if (subtitle_on == 0 && !MovieTitleIsEveryDisp(msg_id))
        return;

    MovieTitleDrawText(GetMsgDataAddr(msg_type, msg_id), y, msg_col, base_disp_flg, alpha);
}

void MovieTitleDrawText(u_char *text, int y, int col, char base_disp_flg, u_char alpha)
{
    if (text == nullptr || *text == MSG_CODE_END)
        return;

#ifdef REIPAN_SLPS_25544_CHS
    // Movies and cutscenes can draw without the HUD having loaded the font pages.
    FontExTexSendVram();
#endif
    u_char *p_next_line_adrs = nullptr;
    u_char *p_now_line_adrs = text;
    do
    {
        float msg_length = (float)MsgGetLineWidth(p_now_line_adrs, &p_next_line_adrs);

        if (base_disp_flg == 1)
        {
            float base_x      = (MOVIE_TITLE_CENTER_X - msg_length * 0.5f)
                              - (float)movie_title_base_tex[0].w;
            float base_length = msg_length + (float)(movie_title_base_tex[0].w
                                                   + movie_title_base_tex[2].w);

            if (base_length < MOVIE_TITLE_BASE_MIN_W)
            {
                base_length = MOVIE_TITLE_BASE_MIN_W;
                base_x      = MOVIE_TITLE_BASE_MIN_X;
            }
            MovieTitleBaseDisp(base_x, (float)(y - 2), base_length, alpha, 0);
        }

        PrintMsg_ArrangeOneLine(p_now_line_adrs, 320, y, col, alpha, 0, 0, 0, 2);

        // The plate's height is the line pitch.
        y += movie_title_base_tex[0].h;
        p_now_line_adrs = p_next_line_adrs;
    }
    while (p_now_line_adrs != nullptr);
}

// Left cap, right cap, then the middle stretched across the gap from the
// seam behind the left cap.
// FF3 0x002190f0.

void MovieTitleBaseDisp(float x, float y, float w, u_char alp, u_int pri)
{
    DISP_SPRT ds;
    float     line_w;
    float     line_scr;

    if (w < MOVIE_TITLE_BASE_MIN_W)
    {
        w = MOVIE_TITLE_BASE_MIN_W;
    }
    line_w   = w - (float)(movie_title_base_tex[0].w + movie_title_base_tex[2].w);
    line_scr = line_w / (float)movie_title_base_tex[1].w;

    CopySprDToSpr(&ds, &movie_title_base_tex[0]);
    ds.pri   = pri;
    ds.z     = 0xfffff - (pri & 0xfffff);
    ds.r     = 0;
    ds.g     = 0;
    ds.b     = 0;
    ds.alpha = (u_char)((ds.alpha * alp) >> 7);
    ds.x     = x;
    ds.y     = y;
    DispSprD(&ds);

    CopySprDToSpr(&ds, &movie_title_base_tex[2]);
    ds.pri   = pri;
    ds.z     = 0xfffff - (pri & 0xfffff);
    ds.r     = 0;
    ds.g     = 0;
    ds.b     = 0;
    ds.alpha = (u_char)((ds.alpha * alp) >> 7);
    ds.x     = x + line_w + (float)movie_title_base_tex[0].w;
    ds.y     = y;
    DispSprD(&ds);

    CopySprDToSpr(&ds, &movie_title_base_tex[1]);
    ds.pri   = pri;
    ds.z     = 0xfffff - (pri & 0xfffff);
    ds.r     = 0;
    ds.g     = 0;
    ds.b     = 0;
    ds.alpha = (u_char)((ds.alpha * alp) >> 7);
    ds.x     = x + (float)movie_title_base_tex[0].w;
    ds.y     = y;
    ds.csx   = ds.x;
    ds.csy   = y;
    ds.scw   = line_scr;
    ds.sch   = 1.0f;
    DispSprD(&ds);
}
