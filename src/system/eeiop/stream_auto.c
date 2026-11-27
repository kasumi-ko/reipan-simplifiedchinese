/* ==========================================================================
 *  system/eeiop/stream_auto.c
 *
 *  Priority-scheduled streams: two play slots (one per snd_stream.c work)
 *  and a wait queue sized by the caller. A request always lands on the wait
 *  queue; StreamPlayQueueWrk sorts the queue by priority and pops its head
 *  into any slot that has gone idle. A request that beats something playing
 *  evicts the loser through PlayQueueReturn, which puts it back on the queue
 *  with its current offset, so it resumes where it was once a slot frees up.
 *
 *  Two flags shape a request: resume (FF2's play_flg) -- start by itself once
 *  the header is loaded, rather than waiting for StreamAutoPreloadPlay -- and
 *  reset_flg -- an evicted stream restarts from the top.
 *
 *  From MioPan system/eeiop/stream_auto.c (FF2's stream_auto.c). FF3 dropped
 *  the debug enable switch, every assert and print, and StreamAutoSetPlayNum's
 *  range check; added a pitch to each request (StreamAutoPlayPitch,
 *  StreamAutoPreloadPitch, StreamAutoFadePitch) and the loop bit to
 *  SndStreamStart; PlayQueueReturn also requeues a one-shot that has not
 *  started; StreamAutoSetPlayNum evicts the slot with the lowest priority
 *  number (FF2: the first slot no other ranks below).
 * ======================================================================== */
#include "system/eeiop/stream_auto.h"

#include <stdint.h>                     // uintptr_t
#include <stdlib.h>                     // qsort
#include <string.h>                     // memset

#include "sdk/libvu0.h"                 // sceVu0CopyVector
#include "system/eeiop/cddat.h"         // GetFileSectorSize
#include "system/eeiop/snd_stream.h"

static int           stream_queue_max;                          /* sbss gp+0x560 */
static STREAM_QUEUE *stream_wait_queue;                         /* sbss gp+0x564 */
static char          stream_play_disable[STREAM_PLAY_MAX];      /* sbss gp+0x568 */
static char          stream_play_disable_num;                   /* sbss gp+0x56a */
static int           stream_queue_id;                           /* sbss gp+0x56c */
static int           stream_wait_num;                           /* sbss gp+0x570 */
static STREAM_QUEUE  stream_play_queue[STREAM_PLAY_MAX];        /* bss 0x410800 */

// ──────────────────────────────────────────────────────────────────────────
// Start slot wrk_id's stream from its offset.
// FF3 0x0026c8c0; from MioPan stream_auto.c StreamAutoStartSub. FF3 passes
// the loop bit on.

static void StreamAutoStartSub(int wrk_id, int header_file_no, int loop)
{
    STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];

    const SND_STREAM_ERR result = SndStreamStart(wrk_id, play_q->file_no, header_file_no, play_q->offset, loop);
    if (result != SND_STREAM_OK)
    {
        play_q->use = 0;
        play_q->end = 1;
        return;
    }
    play_q->end     = 0;
    play_q->playing = 0;
}

// ──────────────────────────────────────────────────────────────────────────
// Descending priority, so the tail of the sorted queue (where
// PopStreamWaitQueue takes from) has the lowest number: the most important.
// FF3 0x0026c920; from MioPan stream_auto.c cmpStreamQueue (unchanged).

static int cmpStreamQueue(const void *a, const void *b)
{
    return ((const STREAM_QUEUE *)b)->priority - ((const STREAM_QUEUE *)a)->priority;
}

// ──────────────────────────────────────────────────────────────────────────
// Move the queue's last entry into play_q. A paused entry there blocks the
// queue rather than being skipped.
// FF3 0x0026c930; from MioPan stream_auto.c PopStreamWaitQueue (unchanged).

static int PopStreamWaitQueue(STREAM_QUEUE *play_q)
{
    if (stream_wait_num == 0)
        return 0;

    if (stream_wait_queue[stream_wait_num - 1].pause != 0)
        return 0;

    *play_q = stream_wait_queue[stream_wait_num - 1];
    stream_wait_num--;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026c9b8; from MioPan stream_auto.c DeleteStreamWaitQueue (no print).

static int DeleteStreamWaitQueue(int id)
{
    for (int i = 0; i < stream_wait_num; i++)
    {
        if (stream_wait_queue[i].id == id)
        {
            for (; i < stream_wait_num - 1; i++)
            {
                stream_wait_queue[i] = stream_wait_queue[i + 1];
            }

            stream_wait_num--;
            return 1;
        }
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026ca70; from MioPan stream_auto.c GetWrkNoFromID (unchanged).

static int GetWrkNoFromID(int id)
{
    for (int i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].id == id)
            return i;
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────────
// Evict play slot wrk_id. A loop still running, a stream not waiting to be
// started by hand, or a one-shot that has not started goes back on the queue
// (at the top again with reset_flg); the slot is freed either way.
// FF3 0x0026cac0; from MioPan stream_auto.c PlayQueueReturn. FF3 adds the
// third case and drops the queue-full assert.

static void PlayQueueReturn(int wrk_id, int time)
{
    STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];

    if ((play_q->loop == 1 && play_q->end == 0) || play_q->resume == 0 ||
        (play_q->loop == 0 && play_q->playing == 0))
    {
        if (play_q->reset_flg)
            play_q->offset = 0;
        else
            play_q->offset = SndStreamGetNowOffset(wrk_id);

        stream_wait_queue[stream_wait_num++] = *play_q;
        SndStreamFadeStop(wrk_id, time);
        play_q->first_flg = 0;
    }

    play_q->use = 0;
}

// ──────────────────────────────────────────────────────────────────────────
// Copy a SND_3D_SET's vectors into a queue entry.
// FF3 0x0026cbf8; from MioPan stream_auto.c StreamAutoSet3DSub (no assert).

static void StreamAutoSet3DSub(STREAM_QUEUE *queue, SND_3D_SET *s3s)
{
    sceVu0CopyVector(queue->pos, s3s->pos);

    if (s3s->vel != nullptr)
    {
        queue->vel_flg = 1;
        sceVu0CopyVector(queue->vel, s3s->vel);
    }
    else
    {
        queue->vel_flg = 0;
    }

    if (s3s->dir != nullptr)
    {
        queue->dir_flg = 1;
        sceVu0CopyVector(queue->dir, s3s->dir);
    }
    else
    {
        queue->dir_flg = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Append a request to the wait queue and hand out its id (wraps at INT_MAX).
// FF3 0x0026cca0; from MioPan stream_auto.c StreamAutoNew. FF3 takes a pitch
// and has no asserts or prints.

static int StreamAutoNew(int file_no, int header_file_no, int priority, int effect, int loop,
                         int vol, int in_time, int reset_in_time, SND_3D_SET *s3s,
                         int play_flg, int reset_flg, int offset, int pitch)
{
    STREAM_QUEUE *queue = &stream_wait_queue[stream_wait_num++];

    queue->file_no        = file_no;
    queue->header_file_no = header_file_no;
    queue->priority       = priority;
    queue->vol            = vol;
    queue->effect         = effect;
    queue->resume         = play_flg;
    queue->use            = 1;
    queue->loop           = loop;
    queue->reset_in_time  = reset_in_time;
    queue->pitch          = pitch;
    queue->in_time        = in_time;
    queue->end            = 0;
    queue->pause          = 0;
    queue->reset_flg      = reset_flg;
    queue->offset         = offset;
    queue->first_flg      = 1;
    queue->play_spd       = 1.0f;

    if (s3s != nullptr)
    {
        queue->s3d = 1;
        StreamAutoSet3DSub(queue, s3s);
    }
    else
    {
        queue->s3d = 0;
    }

    stream_queue_id++;
    if (stream_queue_id < 0)
        stream_queue_id = 0;

    queue->id = stream_queue_id;
    return stream_queue_id;
}

// ──────────────────────────────────────────────────────────────────────────
// After a request: with no free enabled slot, evict the playing stream with
// the highest priority number if the new request beats it.
// FF3 0x0026ce00; from MioPan stream_auto.c StreamAutoSub (unchanged).

static void StreamAutoSub(int priority)
{
    int least_priority    = -1;
    int least_priority_no = 0;

    for (int i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_disable[i] != 0)
            continue;

        if (stream_play_queue[i].use == 0)
            return;

        if (stream_play_queue[i].pause == 0 && stream_play_queue[i].priority > least_priority)
        {
            least_priority    = stream_play_queue[i].priority;
            least_priority_no = i;
        }
    }

    if (least_priority != -1 && priority < least_priority)
        PlayQueueReturn(least_priority_no, 5);
}

// ──────────────────────────────────────────────────────────────────────────
// The request forms.
// FF3 0x0026ceb8 / 0x0026cf18 / 0x0026cf80 / 0x0026cfe0 / 0x0026d040 /
// 0x0026d0a0; from MioPan stream_auto.c (no enable switch, pitch 0x1000
// unless given).

int StreamAutoPreload(int file_no, int header_file_no, int priority, int effect, int loop,
                      int vol, int in_time, SND_3D_SET *s3s)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, in_time, s3s, 0, 1, 0, 0x1000);

    StreamAutoSub(priority);
    return id;
}

// FF3 0x0026cf18
int StreamAutoPlayNonReset(int file_no, int header_file_no, int priority, int effect, int loop,
                           int vol, int in_time, int reset_in_time, SND_3D_SET *s3s,
                           int start_sector)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, reset_in_time, s3s, 1, 0, start_sector, 0x1000);

    StreamAutoSub(priority);
    return id;
}

// FF3 0x0026cf80
int StreamAutoPreloadNonReset(int file_no, int header_file_no, int priority, int effect, int loop,
                              int vol, int in_time, int reset_in_time, SND_3D_SET *s3s)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, reset_in_time, s3s, 0, 0, 0, 0x1000);

    StreamAutoSub(priority);
    return id;
}

// FF3 0x0026cfe0
int StreamAutoPlay(int file_no, int header_file_no, int priority, int effect, int loop,
                   int vol, int in_time, SND_3D_SET *s3s)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, in_time, s3s, 1, 1, 0, 0x1000);

    StreamAutoSub(priority);
    return id;
}

// FF3 0x0026d040
int StreamAutoPlayPitch(int file_no, int header_file_no, int priority, int effect, int loop,
                        int vol, int in_time, SND_3D_SET *s3s, int pitch)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, in_time, s3s, 1, 1, 0, pitch);

    StreamAutoSub(priority);
    return id;
}

// FF3 0x0026d0a0
int StreamAutoPreloadPitch(int file_no, int header_file_no, int priority, int effect, int loop,
                           int vol, int in_time, SND_3D_SET *s3s, int pitch)
{
    int id = StreamAutoNew(file_no, header_file_no, priority, effect, loop, vol,
                           in_time, in_time, s3s, 0, 1, 0, pitch);

    StreamAutoSub(priority);
    return id;
}

// ──────────────────────────────────────────────────────────────────────────
// Allow num of the two slots to play. Disabling takes idle slots first, then
// evicts the playing stream with the lowest priority number (FF3's choice,
// even though that is the most important one) over fade_time.
// FF3 0x0026d100; from MioPan stream_auto.c StreamAutoSetPlayNum.

void StreamAutoSetPlayNum(int num, int fade_time)
{
    int newly_disable = (STREAM_PLAY_MAX - num) - stream_play_disable_num;

    if (newly_disable > 0)
    {
        for (int i = 0; i < STREAM_PLAY_MAX && newly_disable > 0; i++)
        {
            if (stream_play_disable[i] == 0 &&
                !(stream_play_queue[i].use && stream_play_queue[i].end == 0))
            {
                stream_play_disable[i] = 1;
                newly_disable--;
            }
        }

        while (newly_disable > 0)
        {
            int best          = -1;
            int best_priority = 1000;

            for (int i = 0; i < STREAM_PLAY_MAX; i++)
            {
                if (stream_play_disable[i] == 0 && stream_play_queue[i].priority < best_priority)
                {
                    best_priority = stream_play_queue[i].priority;
                    best          = i;
                }
            }

            if (best >= 0)
            {
                stream_play_disable[best] = 1;
                newly_disable--;
                PlayQueueReturn(best, fade_time);
            }
        }

        qsort(stream_wait_queue, stream_wait_num, sizeof(STREAM_QUEUE), cmpStreamQueue);
    }
    else if (newly_disable < 0)
    {
        // Re-enable the first disabled slot, once per slot given back.
        for (int sub = 0; newly_disable < sub; sub--)
        {
            for (int i = 0; i < STREAM_PLAY_MAX; i++)
            {
                if (stream_play_disable[i] != 0)
                {
                    stream_play_disable[i] = 0;
                    break;
                }
            }
        }
    }

    stream_play_disable_num = (char)(STREAM_PLAY_MAX - num);
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d2b0; from MioPan stream_auto.c StreamAutoSetExclusiveMode (no
// enable switch).

void StreamAutoSetExclusiveMode(int flg, int fade_time)
{
    StreamAutoSetPlayNum(flg ? 1 : STREAM_PLAY_MAX, fade_time);
}

// ──────────────────────────────────────────────────────────────────────────
// 0x70 rather than sizeof(STREAM_QUEUE) (0x60): the slack pays for aligning
// the queue to 16 bytes.
// FF3 0x0026d2d8; from MioPan stream_auto.c StreamAutoGetOneWrkSize.

int StreamAutoGetOneWrkSize(void)
{
    return 0x70;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d2e0; from MioPan stream_auto.c StreamAutoPlayInit (unchanged).

void *StreamAutoPlayInit(void *buffer, int num)
{
    STREAM_QUEUE *queue;
    int           i;

    // FF3: GetAlignUp(buffer, 4). PC: through uintptr_t, a host address is 64-bit.
    // callcheck: GetAlignUp (replaced by the uintptr_t rounding on PC)
    queue = (STREAM_QUEUE *)(((uintptr_t)buffer + 15) & ~(uintptr_t)15);

    stream_wait_num = 0;
    for (i = 0; i < num; i++)
    {
        queue[i].id  = i;
        queue[i].use = 0;
    }

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        stream_play_disable[i]   = 0;
        stream_play_queue[i].use = 0;
    }

    stream_play_disable_num = 0;
    stream_queue_max        = num;
    stream_wait_queue       = queue;

    return (char *)queue + num * StreamAutoGetOneWrkSize();
}

// ──────────────────────────────────────────────────────────────────────────
// The scheduler, once per enabled slot: a slot gone idle restarts its loop
// (header already loaded: -0x21) or takes the best queued stream; a slot
// whose header has arrived starts playing if it may.
// FF3 0x0026d3a0; from MioPan stream_auto.c StreamPlayQueueWrk. FF3 passes
// the pitch (FF2: 0x1000) and the loop bit.

static void StreamPlayQueueWrk(int wrk_id)
{
    STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];
    SND_3D_SET    s3s;
    SND_3D_SET   *ptr;

    if (SndStreamIsUse(wrk_id) == 0)
    {
        if (play_q->use)
        {
            if (play_q->loop == 1 && play_q->end == 0)
                StreamAutoStartSub(wrk_id, -0x21, play_q->loop);
            else
                play_q->use = 0;
        }

        if (play_q->use == 0)
        {
            qsort(stream_wait_queue, stream_wait_num, sizeof(STREAM_QUEUE), cmpStreamQueue);

            if (PopStreamWaitQueue(play_q))
            {
                play_q->pause = 0;
                play_q->use   = 1;
                StreamAutoStartSub(wrk_id, play_q->header_file_no, play_q->loop);
            }
        }
    }
    else if (play_q->playing == 0 && play_q->resume == 1 && play_q->pause == 0 &&
             SndStreamIsHeaderReady(wrk_id))
    {
        memset(&s3s, 0, sizeof(SND_3D_SET));

        if (play_q->s3d == 0)
        {
            ptr = nullptr;
        }
        else
        {
            ptr     = &s3s;
            s3s.pos = play_q->pos;
            s3s.vel = play_q->vel_flg ? play_q->vel : nullptr;
            s3s.dir = play_q->dir_flg ? play_q->dir : nullptr;
        }

        // A first play fades in over reset_in_time, a resume over in_time.
        SND_STREAM_ERR result;
        if (play_q->first_flg == 0)
        {
            result = SndStreamPlay(wrk_id, play_q->effect, play_q->loop, play_q->vol, play_q->in_time,
                          ptr, play_q->play_spd, play_q->pitch);
        }
        else
        {
            result = SndStreamPlay(wrk_id, play_q->effect, play_q->loop, play_q->vol, play_q->reset_in_time,
                          ptr, play_q->play_spd, play_q->pitch);
        }

        if (result == SND_STREAM_OK)
        {
            play_q->first_flg = 0;
            play_q->playing = 1;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d5c8; from MioPan stream_auto.c StreamAutoPlayMain (no enable
// switch).

void StreamAutoPlayMain(void)
{
    for (int wrk_id = 0; wrk_id < STREAM_PLAY_MAX; wrk_id++)
    {
        if (stream_play_disable[wrk_id] == 0)
            StreamPlayQueueWrk(wrk_id);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d620 / 0x0026d670; from MioPan stream_auto.c (unchanged).

static int StreamAutoSearchWrkID(int id)
{
    for (int i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].id == id)
            return i;
    }

    return -1;
}

// FF3 0x0026d670
static int StreamAutoSearchWaitWrkID(int id)
{
    for (int i = 0; i < stream_wait_num; i++)
    {
        if (stream_wait_queue[i].id == id)
            return i;
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d6b0; from MioPan stream_auto.c StreamAutoGetInfo (no enable
// switch).

int StreamAutoGetInfo(int id, int *num, int *interleave_byte, SOUND_INFO **info)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id < 0)
        return 0;

    return SndStreamGetInfo(wrk_id, num, interleave_byte, info) == SND_STREAM_OK;
}

// ──────────────────────────────────────────────────────────────────────────
// Stop both slots at once and throw the wait queue away.
// FF3 0x0026d718; from MioPan stream_auto.c StreamAutoAllStop (no print).

void StreamAutoAllStop(void)
{
    int i;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use && stream_play_queue[i].end == 0)
        {
            SndStreamFadeStop(i, 1);
            stream_play_queue[i].use       = 0;
            stream_play_queue[i].first_flg = 0;
        }
    }

    stream_wait_num = 0;
    for (i = 0; i < stream_queue_max; i++)
    {
        stream_wait_queue[i].id  = i;
        stream_wait_queue[i].use = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026d7e0 / 0x0026d878; from MioPan stream_auto.c (no enable switch).

void StreamAutoAllPause(void)
{
    int i;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use)
        {
            SndStreamPause(i);
            stream_play_queue[i].pause = 1;
        }
    }

    for (i = 0; i < stream_wait_num; i++)
    {
        stream_wait_queue[i].pause = 1;
    }
}

// FF3 0x0026d878
void StreamAutoAllRestart(void)
{
    int i;

    for (i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (stream_play_queue[i].use)
        {
            SndStreamRestart(i);
            stream_play_queue[i].pause = 0;
        }
    }

    for (i = 0; i < stream_wait_num; i++)
    {
        stream_wait_queue[i].pause = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// A queued stream just starts at the new volume (pitch); a playing one fades.
// FF3 0x0026d920; from MioPan stream_auto.c StreamAutoFade (no enable
// switch). FF3 0x0026d9a8 is its pitch twin (FF3 only).

// FF3 0x0026d920
void StreamAutoFade(int id, int vol, int time)
{
    int wrk_id = StreamAutoSearchWaitWrkID(id);

    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].vol = vol;
        return;
    }

    wrk_id = GetWrkNoFromID(id);
    if (wrk_id >= 0)
        SndStreamFade(wrk_id, vol, time);
}

// FF3 0x0026d9a8
void StreamAutoFadePitch(int id, int pitch, int time)
{
    int wrk_id = StreamAutoSearchWaitWrkID(id);

    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].pitch = pitch;
        return;
    }

    wrk_id = GetWrkNoFromID(id);
    if (wrk_id >= 0)
        SndStreamFadePitch(wrk_id, pitch, time);
}

// ──────────────────────────────────────────────────────────────────────────
// Stop a stream: a queued one is dropped, a playing one fades out over time.
// FF3 0x0026da30; from MioPan stream_auto.c StreamAutoFadeOut (no enable
// switch).

void StreamAutoFadeOut(int id, int time)
{
    int wrk_id;

    if (DeleteStreamWaitQueue(id))
        return;

    wrk_id = GetWrkNoFromID(id);
    if (wrk_id >= 0)
    {
        SndStreamFadeStop(wrk_id, time);
        stream_play_queue[wrk_id].use       = 0;
        stream_play_queue[wrk_id].first_flg = 0;
        stream_play_queue[wrk_id].end       = 1;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dac0 / 0x0026db58; from MioPan stream_auto.c (unchanged).

int StreamAutoPause(int id)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].pause = 1;
        SndStreamPause(wrk_id);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].pause = 1;
        return 1;
    }

    return 0;
}

// FF3 0x0026db58
int StreamAutoRestart(int id)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].pause = 0;
        SndStreamRestart(wrk_id);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].pause = 0;
        return 1;
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dbf8; from MioPan stream_auto.c StreamAutoSetPosition (no enable
// switch; FF2 took the slot's address before checking it).

int StreamAutoSetPosition(int id, float *pos)
{
    SND_3D_SET set;
    int        wrk_id;

    memset(&set, 0, sizeof(SND_3D_SET));

    wrk_id = StreamAutoSearchWrkID(id);
    if (wrk_id >= 0)
    {
        STREAM_QUEUE *play_q = &stream_play_queue[wrk_id];

        sceVu0CopyVector(play_q->pos, pos);
        if (play_q->vel_flg)
            set.vel = play_q->vel;
        if (play_q->dir_flg)
            set.dir = play_q->dir;
        set.pos = play_q->pos;

        SndStreamSet3D(wrk_id, &set);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        sceVu0CopyVector(stream_wait_queue[wrk_id].pos, pos);
        return 1;
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dd08; from MioPan stream_auto.c PrintStreamAutoStatus. Every
// print is compiled out in FF3: what is left walks the wait queue.

void PrintStreamAutoStatus(void)
{
    for (int i = 0; i < stream_wait_num; i++)
    {
    }
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dd40; from MioPan stream_auto.c StreamAutoIsPreload (FF3 does
// not look at the wait queue: it answers 0 there too).

int StreamAutoIsPreload(int id)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
        return SndStreamIsPreload(wrk_id);

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// Start a preloaded stream.
// FF3 0x0026dd78; from MioPan stream_auto.c StreamAutoPreloadPlay (no enable
// switch).

int StreamAutoPreloadPlay(int id)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id < 0 || SndStreamIsPreload(wrk_id) == 0)
        return 0;

    stream_play_queue[wrk_id].resume = 1;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dde8; from MioPan stream_auto.c StreamAutoIsAllStop (no enable
// switch).

int StreamAutoIsAllStop(void)
{
    for (int i = 0; i < STREAM_PLAY_MAX; i++)
    {
        if (SndStreamIsUse(i))
            return 0;
    }

    return 1;
}

// ──────────────────────────────────────────────────────────────────────────
// Playing or still queued.
// FF3 0x0026de30; from MioPan stream_auto.c StreamAutoIsPlaying (no print).

int StreamAutoIsPlaying(int id)
{
    if (StreamAutoSearchWrkID(id) >= 0)
        return 1;

    return StreamAutoSearchWaitWrkID(id) >= 0;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026de78; from MioPan stream_auto.c StreamAutoChangePlaySpeed (no
// enable switch).

int StreamAutoChangePlaySpeed(int id, float rate)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
    {
        stream_play_queue[wrk_id].play_spd = rate;
        SndStreamChangePlaySpeed(wrk_id, rate);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        stream_wait_queue[wrk_id].play_spd = rate;
        return 1;
    }

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// How far into its file a stream is (a queued one: where it was evicted).
// FF3 0x0026df08; from MioPan stream_auto.c StreamAutoNowPlayPercentage (no
// enable switch).

float StreamAutoNowPlayPercentage(int id)
{
    float offset;
    int   file_no;
    int   wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
    {
        offset  = (float)SndStreamGetNowOffset(wrk_id);
        file_no = stream_play_queue[wrk_id].file_no;
    }
    else
    {
        wrk_id = StreamAutoSearchWaitWrkID(id);
        if (wrk_id < 0)
            return 0.0f;

        offset  = (float)stream_wait_queue[wrk_id].offset;
        file_no = stream_wait_queue[wrk_id].file_no;
    }

    return offset / (float)GetFileSectorSize(file_no);
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dfc0; from MioPan stream_auto.c StreamAutoGetNowSector (unchanged).

int StreamAutoGetNowSector(int id)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
        return SndStreamGetNowOffset(wrk_id);

    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// FF3 0x0026dff8; from MioPan stream_auto.c StreamAutoSet3D (no enable
// switch).

int StreamAutoSet3D(int id, SND_3D_SET *s3s)
{
    int wrk_id = StreamAutoSearchWrkID(id);

    if (wrk_id >= 0)
    {
        StreamAutoSet3DSub(&stream_play_queue[wrk_id], s3s);
        SndStreamSet3D(wrk_id, s3s);
        return 1;
    }

    wrk_id = StreamAutoSearchWaitWrkID(id);
    if (wrk_id >= 0)
    {
        StreamAutoSet3DSub(&stream_wait_queue[wrk_id], s3s);
        return 1;
    }

    return 0;
}
