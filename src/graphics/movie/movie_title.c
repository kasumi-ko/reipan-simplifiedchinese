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
#include "system/eeiop/cd_file_dat.h"
#include <stdio.h>
#include <stdlib.h>
#endif

#define MOVIE_TITLE_CENTER_X    320.0f  // captions are centred here
#define MOVIE_TITLE_BASE_MIN_W  48.0f   // narrowest plate
#define MOVIE_TITLE_BASE_MIN_X  218.0f  // where a plate at the floor starts

static MOVIE_TITLE_CTRL movie_title_ctrl;   // sbss 0x3736e0
static void MovieTitleDrawText(u_char *text, int y, int col, char base_disp_flg, u_char alpha);

#ifdef REIPAN_SLPS_25544_CHS
struct ChineseMovieCue
{
    int first_ms;
    int last_ms;
    u_char text[24];
};

// The two PSS entries contain the same opening. The cues follow the PCM
// dialogue in this edition's movie, in milliseconds from the start of the track.
// Text uses the glyph indices of SLPS_255.44's Chinese font_tex.pk4.
static ChineseMovieCue opening_cues[] = {
    {72150, 73750, {0xf0, 0xc1, 0x9f, 0xac, 0xf6, 0x3c, MSG_CODE_END}},  // 怜小姐！
    {78350, 80250, {0xf4, 0x40, 0x69, 0xf3, 0x9b, 0xf0, 0x8f, 0xf2, 0x55, 0xf2, 0x3f, 0x56, 0x53, MSG_CODE_END}},  // 差不多该收工了。
    {81750, 84000, {0xf2, 0x13, 0x82, 0x71, 0x6b, 0x5c, 0xf0, 0x94, 0xf1, 0x4d, 0x55, MSG_CODE_END}},  // 听说这里有幽灵，
    {84100, 86200, {0x63, 0xf6, 0x57, 0x81, 0x73, 0xf1, 0x74, 0xf3, 0x58, 0xf2, 0x1a, 0x65, 0x53, MSG_CODE_END}},  // 可惜什么都没拍到。
    {88950, 91450, {0xf1, 0x76, 0xf1, 0xba, 0xf3, 0x9b, 0xf0, 0x8e, 0xf2, 0xcb, 0x59, 0x71, 0x7b, 0x53, MSG_CODE_END}},  // 传闻多半就是这样。
    {91550, 93700, {0x92, 0xf0, 0x44, 0xf0, 0xb0, 0x71, 0x66, 0xf4, 0xae, 0x62, 0xf0, 0x01, 0x53, MSG_CODE_END}},  // 我先把这个搬上车。
};
static int last_opening_caption = -1;
#endif

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
    MovieTitleMain(cnt, subtitle_on);
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
#ifdef REIPAN_SLPS_25544_CHS
    last_opening_caption = -1;
#endif
}

// FF3 0x00218eb8.

void MovieTitleMain(int timer, int subtitle_on)
{
#ifdef REIPAN_SLPS_25544_CHS
    int active = -1;
    const int file = GetFileNoFromSceneNo(movie_title_ctrl.scene_no);
    if (subtitle_on && timer >= 0 && (file == SCENE0010M_PSS || file == SCENE5001M_PSS))
    {
        const int milliseconds = timer * 1000 / 30;
        for (int i = 0; i < (int)(sizeof(opening_cues) / sizeof(opening_cues[0])); i++)
        {
            ChineseMovieCue &cue = opening_cues[i];
            if (milliseconds >= cue.first_ms && milliseconds < cue.last_ms)
            {
                MovieTitleDrawText(cue.text, MOVIE_TITLE_DISP_Y, MOVIE_TITLE_MSG_COL, 1, 0x80);
                active = i;
                break;
            }
        }
    }
    static const bool trace = getenv("REIPAN_TRACE_MOVIE_CAPTIONS") != nullptr;
    if (trace && active >= 0 && active != last_opening_caption)
        printf("[movie caption] scene %d cue %d frame %d (SLPS_255.44 glyphs)\n",
               movie_title_ctrl.scene_no, active + 1, timer);
    last_opening_caption = active;
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

static void MovieTitleDrawText(u_char *text, int y, int col, char base_disp_flg, u_char alpha)
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
