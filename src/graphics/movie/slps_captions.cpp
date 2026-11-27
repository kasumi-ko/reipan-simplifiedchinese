#ifdef REIPAN_SLPS_25544_CHS

#include "graphics/movie/slps_captions.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <stdio.h>

#include "graphics/movie/movie_title.h"
#include "system/eeiop/cd_file_dat.h"
#include "system/eeiop/snd_stream.h"

namespace
{
struct CaptionCue
{
    int resource;
    int start;
    int end;
    u_char text[110];
};

#include "graphics/movie/slps_captions.inc"

template <size_t N>
CaptionCue *FindCaption(CaptionCue (&cues)[N], int resource, int position)
{
    auto *cue = std::lower_bound(std::begin(cues), std::end(cues), resource,
                                 [](const CaptionCue &entry, int key) { return entry.resource < key; });
    for (; cue != std::end(cues) && cue->resource == resource && cue->start <= position; ++cue)
    {
        if (position < cue->end)
            return cue;
    }
    return nullptr;
}
}

bool SlpsDrawSceneCaption(int scene, int frame)
{
    // The opening sequence and both endings reuse the same dialogue tracks.
    const int resource = scene == 24 ? 0 : scene == 22 ? 21 : scene;
    CaptionCue *cue = FindCaption(scene_cues, resource, frame);
    static const bool trace = std::getenv("REIPAN_TRACE_MOVIE_CAPTIONS") != nullptr;
    static const CaptionCue *last = nullptr;
    if (cue != nullptr && trace && cue != last)
    {
        printf("[movie caption] scene %d frame %d (cue %d..%d)\n", scene, frame, cue->start, cue->end);
    }
    last = cue;
    if (cue == nullptr)
        return false;

    MovieTitleDrawText(cue->text, MOVIE_TITLE_DISP_Y, MOVIE_TITLE_MSG_COL, 1, 0x80);
    return true;
}

void SlpsDrawVoiceCaption(int y, int color)
{
    CaptionCue *caption = nullptr;
    int elapsed = -1;
    for (int slot = 0; slot < 2; ++slot)
    {
        int file, position;
        if (!SndStreamGetCaptionPosition(slot, &file, &position))
            continue;
        CaptionCue *cue = FindCaption(voice_cues, file, position);
        if (cue != nullptr && (caption == nullptr || position < elapsed))
        {
            caption = cue;
            elapsed = position;
        }
    }

    static const bool trace = std::getenv("REIPAN_TRACE_SUBTITLES") != nullptr;
    static const CaptionCue *last = nullptr;
    if (caption != nullptr)
    {
        if (trace && last != caption)
            printf("[subtitle] voice file=%04x ms=%d (cue %d..%d)\n",
                   caption->resource, elapsed, caption->start, caption->end);
        MovieTitleDrawText(caption->text, y, color, 1, 0x80);
    }
    last = caption;
}

#endif
