/* ==========================================================================
 *  graphics/movie/movie_title.h
 *
 *  Movie captions (FF2 movie_title.c), FF3 0x218d90..0x2193a0, plus FF2
 *  movie.c's *MovieWithTitle wrappers, which FF3 links here.
 *
 *  InitMovieWithTitle starts a scene's movie and latches the scene number;
 *  PlayMovieWithTitle runs one frame of the movie, its scene effects and
 *  the caption whose frame window holds MovieCountGet (MovieTitleMain).
 *  MovieTitleDispMain is the drawing half: a message centred on x 320 a
 *  line at a time, a three-piece plate behind each line; subtitle.c calls
 *  it for spoken lines and SceneDraw calls MovieTitleMain for engine scenes.
 * ======================================================================== */
#ifndef _GRAPHICS_MOVIE_MOVIE_TITLE_H
#define _GRAPHICS_MOVIE_MOVIE_TITLE_H

#include "graphics/graph2d/g2d_draw.h"  // SPRT_DAT
#include "sdk/scetypes.h"

// One caption: message msg_id is shown for movie frames [start_frame,
// end_frame) (NTSC frames; 5/6 of them in PAL). A list ends with msg_id -1.
struct MOVIE_TITLE_DAT                  /* 0xc */
{
    /* 0x0 */ int msg_id;
    /* 0x4 */ int start_frame;
    /* 0x8 */ int end_frame;
};
static_assert(sizeof(MOVIE_TITLE_DAT) == 0xc, "MOVIE_TITLE_DAT is 0xc bytes");

// sbss 0x3736e0
struct MOVIE_TITLE_CTRL                 /* 0x4 */
{
    /* 0x0 */ int scene_no;
};
static_assert(sizeof(MOVIE_TITLE_CTRL) == 0x4, "MOVIE_TITLE_CTRL is 4 bytes");

#define MOVIE_TITLE_MSG_TYPE    0x7e    // message bank of the movie captions (FF2 0x4d)
#define MOVIE_TITLE_DISP_Y      0x17c   // first line's y
#define MOVIE_TITLE_MSG_COL     0xc     // colour label

// The plate behind a caption line: left cap, stretched middle, right cap
// (the left cap mirrored). bss 0x3f6138, filled by the static initializer.
extern SPRT_DAT movie_title_base_tex[3];

// Starts scene_no's PSS (StartMovie with the scene's volume) and latches the
// scene for the captions; StartMovie's result (0: no buffers).
int  InitMovieWithTitle(int scene_no, int audio_flg);       // FF3 0x00218d90
// One movie frame: MovieDispMain(skip, pad), then eff_data's scene effects
// (null: none) and the caption. Nonzero when the movie has finished.
int  PlayMovieWithTitle(void *eff_data, int subtitle_on, int skip, u_int pad);  // FF3 0x00218df8
void EndMovieWithTitle(void);                               // FF3 0x00218e78

void MovieTitleInit(int scene_no);                          // FF3 0x00218eb0
// The latched scene's caption at movie frame timer, if any (subtitle_on:
// the option; off, only the always-shown messages are drawn).
void MovieTitleMain(int timer, int subtitle_on);            // FF3 0x00218eb8
void MovieTitleEnd(void);                                   // FF3 0x00218f50: empty
// Message msg_id of bank msg_type centred on x 320, a line at a time down
// from y; base_disp_flg 1 puts the plate behind each line. Drawn only when
// subtitle_on or the message is always shown (MovieTitleIsEveryDisp).
void MovieTitleDispMain(int msg_type, int msg_id, int y, int msg_col,
                        char base_disp_flg, u_char alpha, int subtitle_on);   // FF3 0x00218f58
void MovieTitleBaseDisp(float x, float y, float w, u_char alp, u_int pri);    // FF3 0x002190f0
void MovieTitleDrawText(u_char *text, int y, int col, char base_disp_flg, u_char alpha);

#endif /* _GRAPHICS_MOVIE_MOVIE_TITLE_H */
