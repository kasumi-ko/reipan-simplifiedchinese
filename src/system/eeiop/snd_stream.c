/* ==========================================================================
 *  system/eeiop/snd_stream.c
 *
 *  ADPCM streaming. Two slots, each holding one SPU voice per channel and one
 *  of the two IRQ cores; the IOP refills the SPU ring buffers from that IRQ
 *  and reports its own view of the state back through GetStreamWrkRet().
 *
 *    - the EE's `status` and the IOP's are separate. SndStreamMain() is the
 *      only place they meet, and every transition out of PRE_LOAD, PLAYING
 *      and WAIT_END is gated on what the IOP reports.
 *    - a looping stream that does not loop on a block boundary needs a third
 *      SPU buffer per channel (the "loop packet"), so SetStreamStartSub()
 *      allocates four interleave units instead of two for those.
 *    - stopping is asynchronous. SndStreamStop() only asks; WAIT_END counts
 *      `abort_cnt` down and sends REQ_STREAM_ABORT if the IOP has not come
 *      back within 600 frames.
 *
 *  From MioPan system/eeiop/snd_stream.c (FF2's snd_stream.c). FF3 dropped
 *  every assert and print, SndStreamPrintStatus and SndStreamGetEndOffset,
 *  inlined SetStreamHeaderSub (without CheckHXDData) into SndStreamMain and
 *  SndStreamStartHeaderOnMemory (which, like SndStreamSetPosition, nothing
 *  calls), sends no file name with REQ_STREAM_START
 *  (0x3c bytes, FF2 0x13c), and added the `loop` bit: SndStreamStart's last
 *  argument, without which SetStreamStartSub sends no loop points.
 *  SndStreamIsPreload answers 1 for a free slot (FF2 asserted), and
 *  SndStreamGetInfo no longer checks the status. FF3 orders the file
 *  FadeStop, Fade, AllStop, Init first.
 * ======================================================================== */
#include "system/eeiop/snd_stream.h"

#include <stdio.h>                      // fprintf
#include <stdlib.h>                     // abort
#include <string.h>                     // memcpy, memset

#include "sdk/iop_voice.h"             // ReiPan_VoiceGetSamplePosition
#include "system/eeiop/cddat.h"         // GetFileStartSector, GetFileSize
#include "system/eeiop/ee_iop.h"        // iopCommandRegister, GetStreamWrkRet, CheckEndPointThrough
#include "system/eeiop/fileload.h"      // FileLoadReqEE, FileLoadIsEnd, FileLoadCancel
#include "system/eeiop/spu_mem.h"       // GetSPUMemory, ReleaseSPUMemory
#include "system/eeiop/spu_voice.h"     // GetSPUVoiceCore, FreeSPUVoiceCore

static SND_STREAM_WRK snd_stream_wrk[2];                    /* bss 0x40f680 */
alignas(64) static char header_buf_64[2][0x800];            /* bss 0x40f800 */
static int irq_core_source[2];                              /* sbss 0x373a40 */
static int snd_stream_load_priority;                        /* sbss 0x373a48 */

static void           SndStreamFadeSub(int wi);
static void           SetStreamStartSub(SND_STREAM_WRK *wrk);
static SND_STREAM_ERR SndStreamInitWrk(SND_STREAM_WRK *wrk, int file_no, int offset, int loop);
static void           StreamWrkRelease(SND_STREAM_WRK *wrk);
static void           SndStreamSet3DSub(SND_STREAM_WRK *wrk, SND_3D_SET *s3s);

// FF3 0x0026b610
void SndStreamFadeStop(int wrk_id, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    switch (wrk->status)
    {
    case ST_STREAM_HEADER_LOAD:
        // Nothing is playing yet, so the header load is simply withdrawn.
        if (FileLoadIsEnd(wrk->header_id) == 0)
            FileLoadCancel(wrk->header_id, nullptr, nullptr);

        StreamWrkRelease(wrk);
        break;

    case ST_STREAM_PRE_LOAD:
    case ST_STREAM_START:
        SndStreamStop(wrk_id);
        break;

    case ST_STREAM_PLAYING:
        wrk->target_vol = 0;
        wrk->spd        = SndGetFrameAddVol(0, wrk->vol, time);
        wrk->fade_stop  = 1;
        break;
    }
}

// FF3 0x0026b6e0
void SndStreamFade(int wrk_id, int vol, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    // A fade-out already in flight owns the ramp; re-targeting it would
    // strand the stop.
    if (wrk->fade_stop)
        return;

    wrk->target_vol = vol;
    wrk->spd        = SndGetFrameAddVol(vol, wrk->vol, time);
}

// FF3 0x0026b740
void SndStreamAllStop(void)
{
    SndStreamStop(0);
    SndStreamStop(1);
}

// FF3 0x0026b768
void SndStreamInit(int load_priority)
{
    snd_stream_load_priority = load_priority;

    iopCommandRegister(REQ_STREAM_CREATE, nullptr, 0);

    for (int i = 0; i < 2; i++)
    {
        snd_stream_wrk[i].s.wrk_id   = (char)i;
        snd_stream_wrk[i].header_buf = header_buf_64[i];
        snd_stream_wrk[i].status     = ST_STREAM_NO_USE;
        snd_stream_wrk[i].p.wrk_id   = (short)i;
    }

    irq_core_source[0] = 0;
    irq_core_source[1] = 0;
}

// FF3 0x0026b7e0 (unreferenced in FF3)
SND_STREAM_ERR SndStreamStartHeaderOnMemory(int wrk_id, int file_no, HXD_HEADER *header,
                                            int offset, int loop)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    SND_STREAM_ERR  ret;

    ret = SndStreamInitWrk(wrk, file_no, offset, loop);
    if (ret != SND_STREAM_OK)
        return ret;

    // FF2's SetStreamHeaderSub, inlined (FF3 drops CheckHXDData).
    wrk->s.nchannel        = (short)header->num;
    wrk->s.interleave_byte = header->interleave_byte;
    memcpy(wrk->info, header + 1, wrk->s.nchannel * sizeof(SOUND_INFO));

    SetStreamStartSub(wrk);

    if (iopCommandRegister(REQ_STREAM_START, &wrk->s, sizeof(STREAM_START)) == 0)
        return SND_STREAM_ERR_IOPSEND;

    wrk->status       = ST_STREAM_PRE_LOAD;
    wrk->header_ready = 1;

    return SND_STREAM_OK;
}

// FF3 0x0026b898
SND_STREAM_ERR SndStreamStart(int wrk_id, int file_no, int header_file_no, int offset, int loop)
{
#ifdef REIPAN_SLPS_25544_CHS
    // JP event scripts refer to 340 contiguous .str/.hxd pairs with even
    // stream IDs. The corresponding host IDs are odd and 127 higher.
    if (file_no >= 0x0e94 && file_no <= 0x113a &&
        (file_no & 1) == 0 && (header_file_no == file_no + 1 || header_file_no == -0x21))
    {
        file_no += 127;
        if (header_file_no != -0x21)
            header_file_no += 127;
    }
#endif
    if (wrk_id < 0 || wrk_id >= (int)(sizeof(snd_stream_wrk) / sizeof(snd_stream_wrk[0])))
        return SND_STREAM_ERR_NOT_USE;
    if (file_no < 0 || file_no >= GetFileNum() || GetFileSize(file_no) == (u_int)-1 ||
        GetFileExtLabel(file_no) != 37)
        return SND_STREAM_ERR_HEADER;
    const u_int header_size = GetFileSize(header_file_no);
    if (header_file_no != -0x21 &&
        (GetFileExtLabel(header_file_no) != 6 || header_size < sizeof(HXD_HEADER) ||
         header_size > sizeof(header_buf_64[0])))
    {
        printf("ReiPan: rejected stream header %04x for file %04x\n", header_file_no, file_no);
        return SND_STREAM_ERR_HEADER;
    }
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    SND_STREAM_ERR  ret;

    ret = SndStreamInitWrk(wrk, file_no, offset, loop);
    if (ret != SND_STREAM_OK)
        return ret;

    // -0x21: the caller has already filled in the header fields, so there is
    // nothing to load and the start can go straight out.
    if (header_file_no == -0x21)
    {
        SetStreamStartSub(wrk);

        if (iopCommandRegister(REQ_STREAM_START, &wrk->s, sizeof(STREAM_START)) == 0)
            return SND_STREAM_ERR_IOPSEND;

        wrk->status       = ST_STREAM_PRE_LOAD;
        wrk->header_ready = 1;
    }
    else
    {
        wrk->header_size = header_size;
        memset(wrk->header_buf, 0, header_size);
        wrk->header_id = FileLoadReqEE(header_file_no, wrk->header_buf,
                                       snd_stream_load_priority, nullptr, nullptr);
        if (wrk->header_id < 0)
        {
            wrk->status = ST_STREAM_NO_USE;
            return SND_STREAM_ERR_HEADER;
        }
    }

    return SND_STREAM_OK;
}

// FF3 0x0026b950
int SndStreamIsHeaderReady(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].header_ready == 0)
        return 0;

    return GetStreamWrkRet(wrk_id)->status == ST_STREAM_PLAYING;
}

// FF3 0x0026b9a8
// A rejected play command never reaches the IOP, so return all reservations here.
static void ReleaseStreamPlayResources(SND_STREAM_WRK *wrk, int channels)
{
    if (wrk->p.irq_core >= 0)
        irq_core_source[wrk->p.irq_core] = 0;
    wrk->p.irq_core = -1;
    for (int i = 0; i < channels; ++i)
    {
        FreeSPUVoiceCore(wrk->p.attr[i].core, wrk->p.voice[i]);
        wrk->p.voice[i] = -1;
    }
    if (wrk->s3dhndl != nullptr)
        Snd3DFreeWrk(wrk->s3dhndl);
    wrk->s3dhndl = nullptr;
}

SND_STREAM_ERR SndStreamPlay(int wrk_id, int effect, int loop, int vol, int in_time,
                             SND_3D_SET *s3s, float play_spd, int pitch)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    int             i;
    int             head_loop;

    if (wrk->status == ST_STREAM_NO_USE)
        return SND_STREAM_ERR_NOT_USE;

    if (wrk->header_ready == 0)
        return SND_STREAM_ERR_HEADER;

    if (GetStreamWrkRet(wrk_id)->status != ST_STREAM_PLAYING)
        return SND_STREAM_ERR_HEADER;

    if (in_time == 0)
    {
        wrk->vol = (short)vol;
    }
    else
    {
        wrk->vol = 0;
        wrk->spd = SndGetFrameAddVol(vol, 0, in_time);
    }

    wrk->target_vol   = (short)vol;
    wrk->play_spd     = play_spd;
    wrk->target_pitch = (short)pitch;
    wrk->pitch        = (short)pitch;
    wrk->pspd         = 0;

    SndStreamSet3DSub(wrk, s3s);

    // Every channel loops as channel 0 does: they share one ring refill.
    head_loop = wrk->info[0].attr.loop;

    wrk->p.irq_core = (short)SetIRQCore(wrk);
    if (wrk->p.irq_core < 0)
    {
        ReleaseStreamPlayResources(wrk, 0);
        return SND_STREAM_ERR_VOICE;
    }

    for (i = 0; i < wrk->s.nchannel; i++)
    {
        wrk->p.voice[i] = (char)GetSPUVoiceCore(wrk->p.attr[i].core);

        // Partial failure unwinds: the IRQ core and every voice taken so far
        // go back before the error is reported.
        if (wrk->p.voice[i] == -1)
        {
            ReleaseStreamPlayResources(wrk, i);
            return SND_STREAM_ERR_VOICE;
        }

        SndCalcValue(wrk->vol, wrk->info[i].pan, wrk->info[i].vol,
                     wrk->pitch, wrk->info[i].pitch, wrk->info[i].attr.type,
                     wrk->s3dhndl, &wrk->p.vol[i], &wrk->p.pitch, wrk->play_spd);

        wrk->p.attr[i].loop   = head_loop & loop;
        wrk->p.attr[i].effect = effect & wrk->info[i].attr.effect;
        wrk->p.adsr1[i]       = (short)wrk->info[i].adsr1;
        wrk->p.adsr2[i]       = (short)wrk->info[i].adsr2;
    }

    if (iopCommandRegister(REQ_STREAM_PLAY, &wrk->p, sizeof(STREAM_PLAY)) == 0)
    {
        ReleaseStreamPlayResources(wrk, wrk->s.nchannel);
        return SND_STREAM_ERR_IOPSEND;
    }

    wrk->play_flg = 1;

    return SND_STREAM_OK;
}

// FF3 0x0026bc38
void SndStreamMain(void)
{
    for (int wi = 0; wi < 2; wi++)
    {
        SND_STREAM_WRK *wrk = &snd_stream_wrk[wi];

        switch (wrk->status)
        {
        case ST_STREAM_HEADER_LOAD:
            if (wrk->header_id == -1 || FileLoadIsEnd(wrk->header_id))
            {
                // Keep prepared buffers while a full command queue is retried.
                if (wrk->header_id != -1)
                {
                    // FF2's SetStreamHeaderSub, inlined: the HXD header, then its
                    // SOUND_INFO records.
                    HXD_HEADER *header = (HXD_HEADER *)wrk->header_buf;

                    if (header->name != 0x48584400 || header->num < 1 || header->num > 2 ||
                        header->interleave_byte <= 0 || header->interleave_byte > 0x8000 ||
                        header->interleave_byte % 16 != 0 ||
                        sizeof(HXD_HEADER) + (unsigned)header->num * sizeof(SOUND_INFO) > wrk->header_size)
                    {
                        printf("ReiPan: invalid HXD data for stream %04x\n", wrk->file_no);
                        wrk->header_id = -1;
                        wrk->status = ST_STREAM_NO_USE;
                        break;
                    }

                    wrk->s.nchannel        = (short)header->num;
                    wrk->s.interleave_byte = header->interleave_byte;
                    memcpy(wrk->info, header + 1, wrk->s.nchannel * sizeof(SOUND_INFO));

                    SetStreamStartSub(wrk);
                    wrk->header_id = -1;
                }

                if (iopCommandRegister(REQ_STREAM_START, &wrk->s, sizeof(STREAM_START)))
                {
                    wrk->status       = ST_STREAM_PRE_LOAD;
                    wrk->header_ready = 1;
                }
            }
            break;

        case ST_STREAM_PRE_LOAD:
            // The IOP has filled the ring, so the stream is startable; go
            // straight on to START so a Play() that already arrived takes
            // effect this frame.
            if (GetStreamWrkRet(wi)->status != ST_STREAM_PLAYING)
                break;

            wrk->status      = ST_STREAM_START;
            wrk->pre_load_ok = 1;
            [[fallthrough]];

        case ST_STREAM_START:
            if (wrk->play_flg)
                wrk->status = ST_STREAM_PLAYING;
            break;

        case ST_STREAM_PLAYING:
            if (GetStreamWrkRet(wi)->status == ST_STREAM_NO_USE)
            {
                // Ran off the end by itself.
                StreamWrkRelease(wrk);
                break;
            }

            if (wrk->fade_stop && wrk->target_vol == wrk->vol)
                SndStreamStop(wi);

            SndStreamFadeSub(wi);
            break;

        case ST_STREAM_WAIT_END:
            // The IOP has been asked to stop and has not answered; after 600
            // frames the stream is taken away from it outright. (FF2's IOP
            // module has no case for REQ_STREAM_ABORT; see MioPan.)
            if (--wrk->abort_cnt == -1)
            {
                STREAM_WRK_ID abort_req;

                abort_req.wrk_id = wi;
                iopCommandRegister(REQ_STREAM_ABORT, &abort_req, sizeof(abort_req));
            }

            if (GetStreamWrkRet(wi)->status == ST_STREAM_NO_USE)
                StreamWrkRelease(wrk);
            break;

        case ST_STREAM_END:
            StreamWrkRelease(wrk);
            break;
        }
    }
}

// FF3 0x0026be80
static void SndStreamFadeSub(int wi)
{
    SND_STREAM_WRK *wrk          = &snd_stream_wrk[wi];
    int             vol_change   = 0;
    int             pitch_change = 0;
    int             ch;
    VOLSET          volset;
    short           pitch = 0;

    if (wrk->target_vol != wrk->vol)
        wrk->vol = (short)SndAddFadeVol(wrk->vol, wrk->target_vol, wrk->spd);

    if (wrk->target_pitch != wrk->pitch)
        wrk->pitch = (short)SndAddFadeVol(wrk->pitch, wrk->target_pitch, wrk->pspd);

    for (ch = 0; ch < wrk->s.nchannel; ch++)
    {
        SndCalcValue(wrk->vol, wrk->info[ch].pan, wrk->info[ch].vol,
                     wrk->pitch, wrk->info[ch].pitch, wrk->info[ch].attr.type,
                     wrk->s3dhndl, &volset, &pitch, wrk->play_spd);

        if (volset.l != wrk->p.vol[ch].l || volset.r != wrk->p.vol[ch].r)
        {
            wrk->p.vol[ch] = volset;
            vol_change     = 1;
        }

        if (pitch != wrk->p.pitch)
        {
            wrk->p.pitch = pitch;
            pitch_change = 1;
        }
    }

    // One command for the whole stream rather than one per channel: the
    // payload carries both volumes.
    if (vol_change)
    {
        STREAM_SETVOL set_vol;

        for (ch = 0; ch < wrk->s.nchannel; ch++)
            set_vol.vol[ch] = wrk->p.vol[ch];

        set_vol.wrk_id = wi;
        iopCommandRegister(REQ_STREAM_SETVOL, &set_vol, sizeof(STREAM_SETVOL));
    }

    if (pitch_change)
    {
        STREAM_SETPITCH set_pitch;

        set_pitch.wrk_id = (short)wi;
        set_pitch.pitch  = pitch;
        iopCommandRegister(REQ_STREAM_SETPITCH, &set_pitch, sizeof(STREAM_SETPITCH));
    }
}

// FF3 0x0026c060
static void SetStreamStartSub(SND_STREAM_WRK *wrk)
{
    int need_loop_packet;
    int unit;

    wrk->s.start_sector = GetFileStartSector(wrk->file_no);
    wrk->s.size         = (int)GetFileSize(wrk->file_no);

    // A resume offset has to leave at least 64 sectors of file behind it,
    // and a file under 128 KB is never resumed.
    if (wrk->s.size < 0x20000 || wrk->s.offset >= (wrk->s.size >> 11) - 0x40)
        wrk->s.offset = 0;

    // FF3: the loop points only go out for a stream started with `loop`.
    if (wrk->loop)
    {
        wrk->s.loop_start_block = wrk->info[0].loopstart;
        wrk->s.loop_end_block   = wrk->info[0].loopend;
    }
    else
    {
        wrk->s.loop_start_block = 0;
        wrk->s.loop_end_block   = 0;
    }

    unit = wrk->s.interleave_byte >> 4;
    wrk->s.loop_start_fraction = wrk->s.loop_start_block % unit;
    wrk->s.loop_end_fraction   = wrk->s.loop_end_block % unit;

    need_loop_packet = wrk->s.loop_start_fraction != 0 || wrk->s.loop_end_fraction != 0;

    for (int i = 0; i < wrk->s.nchannel; i++)
    {
        // Looping off a block boundary needs a fourth interleave unit to
        // splice the wrap through; the play buffer starts halfway in.
        if (need_loop_packet && wrk->info[0].attr.loop)
        {
            wrk->s.spu_loop_packet[i] = (u_int)GetSPUMemory(wrk->s.interleave_byte * 4);
            wrk->s.spu_packet[i][0]   = wrk->s.spu_loop_packet[i] + wrk->s.interleave_byte * 2;
        }
        else
        {
            wrk->s.spu_loop_packet[i] = 0;
            wrk->s.spu_packet[i][0]   = (u_int)GetSPUMemory(wrk->s.interleave_byte * 2);
        }

        wrk->s.spu_packet[i][1] = wrk->s.spu_packet[i][0] + wrk->s.interleave_byte;
    }
}

// FF3 0x0026c1e8
static SND_STREAM_ERR SndStreamInitWrk(SND_STREAM_WRK *wrk, int file_no, int offset, int loop)
{
    if (wrk->status != ST_STREAM_NO_USE)
        return SND_STREAM_ERR_IN_USE;

    wrk->file_no      = file_no;
    wrk->s3dhndl      = nullptr;
    wrk->offset       = 0;
    wrk->status       = ST_STREAM_HEADER_LOAD;
    wrk->header_ready = 0;
    wrk->pre_load_ok  = 0;
    wrk->play_flg     = 0;
    wrk->fade_stop    = 0;
    wrk->stop         = 0;
    wrk->loop         = loop & 1;
    wrk->header_id    = -1;

    for (int i = 0; i < 2; i++)
    {
        wrk->p.voice[i]           = -1;
        wrk->s.spu_packet[i][0]   = 0;
        wrk->s.spu_loop_packet[i] = 0;
    }

    wrk->s.offset = offset;

    return SND_STREAM_OK;
}

// FF3 0x0026c298
int SetIRQCore(SND_STREAM_WRK *wrk)
{
    (void)wrk;

    for (int i = 0; i < 2; i++)
    {
        if (irq_core_source[i] == 0)
        {
            irq_core_source[i] = 1;
            return i;
        }
    }
    return -1;
}

// FF3 0x0026c2f0
static void StreamWrkRelease(SND_STREAM_WRK *wrk)
{
    int i;

    // Every voice has to be past its end point before the SPU memory under it
    // can go back, so a slot that is not there yet goes to END and is retried
    // next frame.
    for (i = 0; i < wrk->s.nchannel; i++)
    {
        if (wrk->p.voice[i] >= 0 &&
            CheckEndPointThrough(wrk->p.attr[i].core, wrk->p.voice[i]) == 0)
        {
            wrk->status = ST_STREAM_END;
            return;
        }
    }

    if (wrk->header_ready || wrk->s.spu_packet[0][0] != 0)
    {
        for (i = 0; i < wrk->s.nchannel; i++)
        {
            // The loop packet is the base of the allocation when there is
            // one, so freeing it frees the play buffer with it.
            if (wrk->s.spu_loop_packet[i] != 0)
                ReleaseSPUMemory((int)wrk->s.spu_loop_packet[i]);
            else if (wrk->s.spu_packet[i][0] != 0)
                ReleaseSPUMemory((int)wrk->s.spu_packet[i][0]);
        }

        // The voices and the IRQ core only exist once Play() ran.
        if (wrk->play_flg)
        {
            irq_core_source[wrk->p.irq_core] = 0;

            if (wrk->s3dhndl != nullptr)
                Snd3DFreeWrk(wrk->s3dhndl);

            for (i = 0; i < wrk->s.nchannel; i++)
            {
                if (wrk->p.voice[i] >= 0)
                    FreeSPUVoiceCore(wrk->p.attr[i].core, wrk->p.voice[i]);
            }
        }
    }

    wrk->status = ST_STREAM_NO_USE;
    wrk->offset = GetStreamWrkRet(wrk->s.wrk_id)->offset;
}

// FF3 0x0026c4a0
static void SndStreamSet3DSub(SND_STREAM_WRK *wrk, SND_3D_SET *s3s)
{
    for (int i = 0; i < wrk->s.nchannel; i++)
    {
        if (s3s == nullptr)
        {
            wrk->s3d[i] = 0;
        }
        else
        {
            // One handle for the whole stream, not one per channel.
            if (wrk->s3dhndl == nullptr)
                wrk->s3dhndl = Snd3DCreateWrk(s3s);

            wrk->s3d[i] = 1;
        }
    }
}

// FF3 0x0026c540
void SndStreamStop(int wrk_id)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    STREAM_WRK_ID   stop;

    if (wrk->stop == 0 && wrk->status != ST_STREAM_NO_USE)
    {
        wrk->stop      = 1;
        wrk->abort_cnt = 600;

        stop.wrk_id = wrk_id;

        if (iopCommandRegister(REQ_STREAM_STOP, &stop, sizeof(stop)))
            wrk->status = ST_STREAM_WAIT_END;
    }
}

// FF3 0x0026c5c0
void SndStreamPause(int wrk_id)
{
    STREAM_WRK_ID pause;

    if (snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE &&
        snd_stream_wrk[wrk_id].status != ST_STREAM_WAIT_END)
    {
        pause.wrk_id = wrk_id;
        iopCommandRegister(REQ_STREAM_PAUSE, &pause, sizeof(pause));
    }
}

// FF3 0x0026c610
void SndStreamRestart(int wrk_id)
{
    STREAM_WRK_ID restart;

    if (snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE)
    {
        restart.wrk_id = wrk_id;
        iopCommandRegister(REQ_STREAM_RESTART, &restart, sizeof(restart));
    }
}

// FF3 0x0026c658 (unreferenced in FF3)
void SndStreamSetPosition(int wrk_id, float *pos)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];
    SND_3D_SET      s3s;

    if (wrk->status == ST_STREAM_NO_USE)
        return;

    if (wrk->s3dhndl == nullptr)
    {
        memset(&s3s, 0, sizeof(s3s));
        s3s.pos = pos;
        s3s.vel = nullptr;
        s3s.dir = nullptr;

        SndStreamSet3DSub(wrk, &s3s);
    }
    else
    {
        Snd3DSetPosition(wrk->s3dhndl, pos);
    }
}

// FF3 0x0026c6e0
int SndStreamIsUse(int wrk_id)
{
    return snd_stream_wrk[wrk_id].status != ST_STREAM_NO_USE;
}

bool SndStreamGetCaptionPosition(int slot, int *file, int *milliseconds)
{
    if ((u_int)slot >= sizeof(snd_stream_wrk) / sizeof(snd_stream_wrk[0]))
        return false;

    const SND_STREAM_WRK &wrk = snd_stream_wrk[slot];
    const int rate = wrk.info[0].smpl_rate;
    if (wrk.status != ST_STREAM_PLAYING || !wrk.header_ready || rate <= 0 || wrk.s.nchannel <= 0)
        return false;

    const long long samples = ReiPan_VoiceGetSamplePosition(wrk.p.attr[0].core, wrk.p.voice[0]);
    if (samples < 0)
        return false;

    // A resumed stream only consumes samples after its starting sector.
    const long long skipped = 2048LL * wrk.s.offset / (16 * wrk.s.nchannel) * 28;
    *file = wrk.file_no;
    *milliseconds = (int)((samples + skipped) * 1000 / rate);
    return true;
}

// FF3 0x0026c700
int SndStreamIsPreload(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].status == ST_STREAM_NO_USE)
        return 1;

    return snd_stream_wrk[wrk_id].pre_load_ok != 0;
}

// FF3 0x0026c738
SND_STREAM_ERR SndStreamGetInfo(int wrk_id, int *nchannel, int *interleave_byte,
                                SOUND_INFO **info)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (wrk->header_ready == 0)
        return SND_STREAM_ERR_HEADER;

    *info            = wrk->info;
    *nchannel        = wrk->s.nchannel;
    *interleave_byte = wrk->s.interleave_byte;

    return SND_STREAM_OK;
}

// FF3 0x0026c7a0
int SndStreamGetNowOffset(int wrk_id)
{
    if (snd_stream_wrk[wrk_id].status != ST_STREAM_PLAYING)
        return 0;

    return GetStreamWrkRet(snd_stream_wrk[wrk_id].s.wrk_id)->offset;
}

// FF3 0x0026c7e8
SND_STREAM_ERR SndStreamChangePlaySpeed(int wrk_id, float rate)
{
    snd_stream_wrk[wrk_id].play_spd = rate;

    return SND_STREAM_OK;
}

// FF3 0x0026c808
void SndStreamSet3D(int wrk_id, SND_3D_SET *s3s)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (wrk->status == ST_STREAM_NO_USE)
        return;

    if (wrk->s3dhndl != nullptr)
        snd3DSetSET(wrk->s3dhndl, s3s);
    else
        SndStreamSet3DSub(wrk, s3s);
}

// FF3 0x0026c860
void SndStreamFadePitch(int wrk_id, int pitch, int time)
{
    SND_STREAM_WRK *wrk = &snd_stream_wrk[wrk_id];

    if (wrk->status == ST_STREAM_NO_USE)
        return;

    if (time == 0)
    {
        wrk->pitch = (short)pitch;
    }
    else
    {
        // ROM BUG, kept (FF2 has it too): target and current are the wrong
        // way round (SndBufFadePitch passes (pitch, now, time)), so the step
        // comes out negated and a timed pitch fade walks away from its target.
        wrk->pspd = SndGetFrameAddVol(wrk->pitch, pitch, time);
    }

    wrk->target_pitch = (short)pitch;
}
