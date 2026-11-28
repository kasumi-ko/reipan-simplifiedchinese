/* ==========================================================================
 *  reipan_video.h  --  the direct movie picture path
 *
 *  PORT-ONLY.  There is no ROM counterpart: this is the seam that takes the
 *  GS out of movie playback altogether.
 *
 *  WHAT THE ROM DOES, and why it cannot stand on the host.  The IPU decodes a
 *  picture as 16x16 RGBA32 macroblocks; ldimage.c walks them into GS local
 *  memory over PATH3; and the game then draws an ordinary textured primitive
 *  whose TEX0 names that block -- movie.c's full-screen sprite at GS 0x2bc0,
 *  CMovieRoom::Draw()'s screen quad at 0x3aa0.  Every step of that is free on
 *  the console.  Here each one is a copy of the whole frame:
 *
 *    - the decoder repacks its linear output into macroblock order,
 *    - loadImage() hands 1120 of them to ReiPan_GsUpload(), each swizzled
 *      into the emulated GS memory vector,
 *    - the renderer then hashes that region, un-swizzles it back out, and
 *      creates a host texture from the result.
 *
 *  That last step is the one that actually breaks.  A texture is cached by the
 *  hash of its GS content and g_texture_cache has no eviction, so a movie --
 *  whose content is different every frame, by construction -- creates one
 *  1024x512 GPU texture per frame and never releases any of them.  A
 *  fifty-second cutscene at 30 fps asks for about three gigabytes of texture
 *  memory before it ends.
 *
 *  WHAT HAPPENS INSTEAD.  The decoder publishes its linear RGBA frame here,
 *  and the renderer matches the GS address and texture page dimensions.
 *  Each page shape has a persistent texture. The picture is uploaded into
 *  its top-left corner so the original UVs work for both full-screen and
 *  in-world playback.
 *
 *  The publisher (sdk/libmpeg_video.cpp) and the consumer (the renderer) are
 *  the same thread in NTSC and two threads in PAL, where playpss.c runs the
 *  decode on videoDecMain() -- hence the lock.
 *
 *  This is the only picture path; the ROM's GS transfer chain in ldimage.c is
 *  still built and still correct, but nothing walks it while a movie is up.
 * ======================================================================== */

#ifndef REIPAN_VIDEO_H
#define REIPAN_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the GS address and the texture page used by the movie's draw. */
void ReiPan_VideoBegin(int gs_addr, int width_log2, int height_log2);

/* Disarm it.  Called when the decoder is created or reset -- i.e. at both ends
 * of a movie -- so nothing is overridden between films. */
void ReiPan_VideoEnd(void);

/* Armed?  ldimage.c's loadImage() skips the whole transfer chain while this is
 * true, and the renderer uses it to decide whether a TEX0 is the movie's. */
int  ReiPan_VideoIsLive(void);
int  ReiPan_VideoMatchesTexture(int gs_addr, int width_log2, int height_log2);
/* Changes once per newly armed movie, including reuse of its GS address. */
unsigned int ReiPan_VideoGeneration(void);


/* The publisher's two calls.  BeginFrame hands back a `width` x `height` RGBA
 * staging buffer to decode into -- tightly packed, `width` * 4 bytes per row --
 * or NULL if the direct path is off or the buffer could not be sized, in which
 * case the caller falls back to the ROM's macroblock path.  EndFrame publishes
 * what was written.  Nothing is copied by either. */
unsigned char *ReiPan_VideoBeginFrame(int width, int height);
void           ReiPan_VideoEndFrame(void);

#ifdef __cplusplus
}

#include <vector>

namespace ReiPan
{
namespace Video
{
/* The renderer's side: take the picture published since the last call, if any.
 * `out` is swapped with the published buffer rather than copied, so the caller
 * may move it straight into a pending GPU upload. */
bool AcquireFrame(std::vector<unsigned char> &out, int *width, int *height);
}
}
#endif

#endif /* REIPAN_VIDEO_H */
