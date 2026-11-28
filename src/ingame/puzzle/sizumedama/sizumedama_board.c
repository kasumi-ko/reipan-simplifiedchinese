/* ==========================================================================
 *  ingame/puzzle/sizumedama/sizumedama_board.c
 *
 *  The sizumedama puzzle's board (see sizumedama_board.h).
 *  FF3 0x24ca90..0x24cf18, FF3's own code.
 * ======================================================================== */
#include "ingame/puzzle/sizumedama/sizumedama_board.h"

#include "ingame/map/map_bgm.h"                 // map_bgmFadeOut, map_bgmFadeIn
#include "ingame/sound/snd_ctrl.h"              // SndConvVolume
#include "ingame/subtitle/subtitle.h"           // SubTitleReq, SubTitleStop, SubTitleIsEnd
#include "system/eeiop/cddat.h"                 // GetFileSize
#include "system/eeiop/snd_buffer.h"            // SndBufAllPauseLoopSnd, SndBufAllRestartLoopSnd
#include "system/eeiop/stream_auto.h"           // StreamAutoPlay, StreamAutoIsPlaying, StreamAutoFadeOut

#define SIZUME_SUBTITLE_KIND    4       // SubTitleReq's kind / row of the clear recording
#define SIZUME_SUBTITLE_NO      0x6b
#define SIZUME_VOICE_SUBTITLE   1       // voice while the subtitle runs it
#define SIZUME_VOICE_VOL        100
#define SIZUME_VOICE_PRIORITY   0xd
#define SIZUME_FADE_TIME        10

static bool UsesSubtitleStream(const SIZUME_BOARD &board)
{
    return board.solved != 0 &&
           SubTitleGetStreamFile(SIZUME_SUBTITLE_KIND, SIZUME_SUBTITLE_NO) == board.def->clear.file;
}

// The pedestals empty, every stone free, then the definition's stones put.
// FF3 0x0024cad8. FF3 0x0024ca90 is the constructor's other copy (no
// callers): the same head, then a jump into this body at 0x24cb20.

SIZUME_BOARD::SIZUME_BOARD(const SIZUME_BOARD_DEF *def)
{
    this->def = def;
    if (SubTitleIsEnd() == 0)
        SubTitleStop();
    voice = -1;
    for (int i = 0; i < SIZUME_STONE_NUM; i++)
        used[i] = 0;
    for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
        slot[i] = -1;
    for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
    {
        if (this->def->init[i] != -1)
            Put(i, this->def->init[i]);
    }
    show_buttons = 0;
    stone = -1;
    cursor = 0;
    solved = 0;
}

// FF3 0x0024cbf0.

int SIZUME_BOARD::IsSolved()
{
    for (int i = 0; i < SIZUME_PEDESTAL_NUM; i++)
    {
        if (slot[i] != def->answer[i])
            return 0;
    }
    solved = 1;
    return 1;
}

// FF3 0x0024cc38.

int SIZUME_BOARD::Put(int pedestal, int stone)
{
    if (slot[pedestal] != -1 || used[stone] != 0)
        return 0;
    slot[pedestal] = stone;
    used[stone] = 1;
    return 1;
}

// FF3 0x0024cc78.

int SIZUME_BOARD::Take(int pedestal)
{
    int stone = slot[pedestal];

    if (stone == -1 || used[stone] == 0)
        return 0;
    slot[pedestal] = -1;
    used[stone] = 0;
    return 1;
}

// FF3 0x0024ccb8.

int SIZUME_BOARD::CountFree()
{
    int n = 0;

    for (int i = 0; i < SIZUME_STONE_NUM; i++)
    {
        if (used[i] == 0)
            n++;
    }
    return n;
}

// FF3 0x0024cce8.

const SIZUME_VOICE *SIZUME_BOARD::GetVoice()
{
    int stone;

    if (IsSolved() != 0)
        return &def->clear;
    stone = slot[cursor];
    if (stone == -1)
        return nullptr;
    return &def->stone[stone];
}

// The voice of GetVoice, when its file exists: the map BGM ducked and the
// looping sounds paused. Use the subtitle player when the disc lists the
// clear recording there. Other discs can play it directly from the board.
// FF3 0x0024cd50.

void SIZUME_BOARD::PlayVoice()
{
    const SIZUME_VOICE *v = GetVoice();
    int file = -1;

    if (v != nullptr)
        file = v->file;
    if (file < 0 || GetFileSize(file) == (u_int)-1 || GetFileSize(file) == 0)
        return;

    map_bgmFadeOut(3, 0);
    SndBufAllPauseLoopSnd();
    if (UsesSubtitleStream(*this))
    {
        SubTitleReq(SIZUME_SUBTITLE_KIND, SIZUME_SUBTITLE_NO);
        voice = SIZUME_VOICE_SUBTITLE;
    }
    else
    {
        voice = StreamAutoPlay(file, file + 1, SIZUME_VOICE_PRIORITY, 1, 0,
                               SndConvVolume(SIZUME_VOICE_VOL), 3, nullptr);
    }
}

// FF3 0x0024ce20.

int SIZUME_BOARD::IsPlaying()
{
    if (voice < 0)
        return 0;
    if (UsesSubtitleStream(*this))
        return SubTitleIsEnd() == 0;
    return StreamAutoIsPlaying(voice);
}

// FF3 0x0024ce68.

void SIZUME_BOARD::StopVoice()
{
    if (voice >= 0)
    {
        if (UsesSubtitleStream(*this))
            SubTitleStop();
        else
            StreamAutoFadeOut(voice, SIZUME_FADE_TIME);
    }
    voice = -1;
    map_bgmFadeIn(SIZUME_FADE_TIME);
    SndBufAllRestartLoopSnd();
}

// FF3 0x0024cec0.

int SIZUME_BOARD::GetLine()
{
    const SIZUME_VOICE *v = GetVoice();

    if (v == nullptr)
        return -1;
    return v->line_msg;
}

// FF3 0x0024cef0.

int SIZUME_BOARD::GetStoneName(int stone)
{
    if (stone == -1)
        return -1;
    return def->stone[stone].name_msg;
}
