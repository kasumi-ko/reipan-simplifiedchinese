// system/playpss/playpss.c -- the PSS movie player's driver (FF2 playpss.a(playpss.o)).
//
// libmpeg demuxes the program stream and decodes the video, ldimage.c builds
// the GS transfer, audiodec.c plays the audio; this file runs them. One 16 KB
// block of stream (muxBuff, read by my_strfile.c / pss_loop.c) is demuxed by
// fillBuff: videoCallback copies the video payloads into the demux ring that
// nodataCallback hands to the IPU in 4 KB slices, audioCallback the audio ones
// into audiodec.c's ring. Timer 0 counts H-blanks and each frame spends a
// budget of scanlines refilling (bgDuration, scaled by the picture area).
//
// Two decode paths. NTSC: the game calls playPssMain / playPssSetPacket /
// playPssSetTexPacket once per frame and they decode inline. PAL: vblankHandler
// releases a decode thread (videoDecMain) at 29.97 Hz off the 50 Hz vblank.
//
// FF3 against FF2: the flag word gained bAudioRun (the decode thread pauses and
// resumes the audio itself, so playPssPause/Restart leave it alone in PAL), the
// thread drops to priority 1 around the decode, the thread stack doubled to
// 8 KB and its semaphore has no max, the printfs went, and playPssSetTexPacket
// (FF3-only) decodes into a texture for the map's sky movies.
//
// PORT: the IPU and the GS transfer are behind sdk/libmpeg.cpp (a host MPEG-2
// decoder) and ldimage.c (the direct picture path, reipan_video.h).

#include <string.h>                     // memcpy

#include "system/playpss/playpss.h"
#include "system/playpss/audiodec.h"
#include "system/playpss/ldimage.h"

#include "sdk/eekernel.h"               // threads, semaphores, INTC
#include "sdk/eeregs.h"                 // REG_RCNT0_*, REG_DMAC_4_IPU_TO_*
#include "sdk/libgraph.h"               // sceGsSyncPath
#include "sdk/libsdr.h"                 // sceSdRemoteInit

#define PSS_MUX_BUFF_SIZE       0x4000  // one block of program stream per read
#define PSS_PICTURE_MB          0x460   // macroblocks per picture: 640x448 / 16x16
#define PSS_IPU_DMA_SIZE        0x1000  // the IPU is fed in 4 KB slices
#define PSS_SCREEN_W            640     // the screen playPssSetPacket centres in
#define PSS_SCREEN_H            448
#define PSS_REFILL_PER_PIXEL    0.000151f   // bgDuration scanlines per pixel
#define PSS_INTC_VBLANK         2       // INTC cause: vblank start
#define PSS_PAL_STEP            11988   // 11988/20000 of a picture per 50 Hz field
#define PSS_PAL_PERIOD          20000

// Stop states: playPssStop asks for 1, the live decode path answers 2.
#define PSS_STOP_NONE           0
#define PSS_STOP_REQ            1
#define PSS_STOP_DONE           2

// The flag word at gp+0x5d8: the stop phase, then six bits in the halfword
// after it (StartNoWait's `andi 0xffcc; ori 4` clears four and sets bAudioRun).
struct PLAY_PSS_FLAGS                   /* 0x4 */
{
    /* 0x0   */ short   sStopPhase;
    /* 0x2:0 */ u_short bStarted       : 1;    // audio started
    /* 0x2:1 */ u_short bPauseFlg      : 1;
    /* 0x2:2 */ u_short bAudioRun      : 1;    // PAL thread: audio not paused (FF3)
    /* 0x2:3 */ u_short bNtsc2Pal      : 1;    // the threaded decode path
    /* 0x2:4 */ u_short bSendImage     : 1;    // that thread sent a picture
    /* 0x2:5 */ u_short bErrorCallback : 1;
};
static_assert(sizeof(PLAY_PSS_FLAGS) == 0x4, "PLAY_PSS_FLAGS is 0x4 bytes");

// sdata (gp-0xcc4..gp-0xca8)
static int iPalVBlankCounter = 0;       // gp-0xcc4
static int iVTCounter        = 0;       // gp-0xcbc
static int sTexPacketSize    = 0;       // gp-0xcb4: setLoadTexTags' size, kept from the first frame
static int iMovieDecSema     = 0;       // gp-0xcac
static int hid_vblank        = 0;       // gp-0xca8

static char             videoDecStack[0x2000];  // 0x411700
static PLAY_PSS_FLAGS   Flags;                  // gp+0x5d8
static int              videoDecTh;             // gp+0x5dc
static AudioDec         audioDec;               // 0x413700
static playPssReadFunc  fileRead;               // gp+0x5e0
static int              bgDuration;             // gp+0x5e4
static char            *muxBuff;                // gp+0x5e8
static int              muxBuffFullness;        // gp+0x5ec
static char            *demuxBuff;              // gp+0x5f0
static int              demuxBuffSize;          // gp+0x5f4
static char            *demuxBuffPast;          // gp+0x5f8: where the demuxer writes
static char            *demuxBuffPresent;       // gp+0x5fc: the slice the IPU reads
static char            *demuxBuffFuture;        // gp+0x600
static sceIpuRGB32     *rsrcs_rgb32;            // gp+0x604
static u_int           *rsrcs_path3tag;         // gp+0x608
static int              rsrcs_vram_adrs;        // gp+0x60c
static sceMpeg          playpss_mp;             // 0x413760
static int              audio_play_flg;         // gp+0x610

static int  videoCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg);
static int  audioCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg);
static int  nodataCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg);
static int  backgroundCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg);
static int  errorCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg);
static int  fillBuff(sceMpeg *mp, int blocking);

// Once, at boot. bNtsc2Pal is cleared here, not per movie: playPssSetNtsc2Pal
// is a per-movie setting that survives until the next.
// FF3 0x00271cb8
void playPssInit(void)
{
    Flags.bNtsc2Pal = 0;
    sceMpegInit();
    sceSdRemoteInit();
}

// The PAL decode thread: one picture per semaphore post (vblankHandler posts
// at the film's rate), then the rest of the time refilling. FF3 pauses and
// resumes the audio here with the picture, and decodes at priority 1.
// FF3 0x00271ce0
static void videoDecMain(void *arg)
{
    (void)arg;

    while (1)
    {
        if (Flags.bErrorCallback != 0 || sceMpegIsEnd(&playpss_mp) != 0 ||
            Flags.sStopPhase == PSS_STOP_REQ)
        {
            Flags.sStopPhase = PSS_STOP_DONE;
            if (audio_play_flg != 0)
                audioDecPause(&audioDec);
            return;
        }

        if (PollSema(iMovieDecSema) == -1)
            WaitSema(iMovieDecSema);

        // posts it could not keep up with
        while (PollSema(iMovieDecSema) != -1)
            ;

        REG_RCNT0_MODE  = 0x83;
        REG_RCNT0_COUNT = 0;

        if (Flags.bPauseFlg == 0)
        {
            if (audio_play_flg != 0 && Flags.bAudioRun == 0)
            {
                audioDecResume(&audioDec);
                Flags.bAudioRun = 1;
            }
            ChangeThreadPriority(videoDecTh, 1);
            if (sceMpegGetPicture(&playpss_mp, rsrcs_rgb32, PSS_PICTURE_MB) < 0)
            {
                Flags.sStopPhase     = PSS_STOP_DONE;
                Flags.bErrorCallback = 1;
            }
            ChangeThreadPriority(videoDecTh, 3);
        }
        else if (audio_play_flg != 0 && Flags.bAudioRun != 0)
        {
            Flags.bAudioRun = 0;
            audioDecPause(&audioDec);
        }

        // the chain only changes with the picture size: built once per movie
        if (playpss_mp.frameCount == 0)
        {
            setLoadImageTags(rsrcs_path3tag, rsrcs_rgb32, 0, 0,
                             playpss_mp.width, playpss_mp.height, rsrcs_vram_adrs);
            bgDuration = (int)((float)playpss_mp.width * (float)playpss_mp.height * PSS_REFILL_PER_PIXEL);
        }

        loadImage(rsrcs_path3tag);
        Flags.bSendImage = 1;

        if (audio_play_flg != 0)
        {
            audioDecSendToIOP(&audioDec);
            if (Flags.bStarted == 0 && audioDecIsPreset(&audioDec) != 0)
            {
                audioDecStart(&audioDec);
                Flags.bStarted = 1;
            }
        }

        if (Flags.sStopPhase != PSS_STOP_NONE)
            break;

        while (fillBuff(&playpss_mp, 1) != 0)
            ;
    }

    Flags.sStopPhase = PSS_STOP_DONE;
}

// Bring the decoder up and demux the first block, without waiting for a
// picture. The audio half is optional: without all four audio buffers the
// movie plays silently and the PCM callback is never registered.
// FF3 0x00271f68
void playPssStartNoWait(playPssReadFunc read, playPssRsrcs *rsrcs)
{
    Flags.bStarted       = 0;
    Flags.bPauseFlg      = 0;
    Flags.bSendImage     = 0;
    Flags.bErrorCallback = 0;
    Flags.bAudioRun      = 1;
    muxBuff = rsrcs->read_buf;

    sceMpegCreate(&playpss_mp, rsrcs->mpegBuff, rsrcs->mpegBuffSize);
    sceMpegAddStrCallback(&playpss_mp, sceMpegStrM2V, 0, videoCallback, nullptr);
    sceMpegAddCallback(&playpss_mp, sceMpegCbNodata, nodataCallback, nullptr);
    sceMpegAddCallback(&playpss_mp, sceMpegCbBackground, backgroundCallback, nullptr);
    sceMpegAddCallback(&playpss_mp, sceMpegCbError, errorCallback, nullptr);

    if (rsrcs->audioBuff != nullptr && rsrcs->iopBuff != 0 &&
        rsrcs->zeroBuff != nullptr && rsrcs->iopZeroBuff != 0)
    {
        audio_play_flg = 1;
        audioDecCreate(&audioDec, rsrcs->audioBuff, rsrcs->audioBuffSize,
                       rsrcs->iopBuff, rsrcs->iopBuffSize,
                       rsrcs->zeroBuff, rsrcs->iopZeroBuff, 0x800,
                       rsrcs->audio_vol_percent, rsrcs->auto_dma_core);
        sceMpegAddStrCallback(&playpss_mp, sceMpegStrPCM, 0, audioCallback, nullptr);
    }
    else
    {
        audio_play_flg = 0;
    }

    demuxBuff        = (char *)rsrcs->demuxBuff;
    demuxBuffSize    = rsrcs->demuxBuffSize;
    demuxBuffPast    = demuxBuff + 16;
    muxBuffFullness  = 0;
    Flags.sStopPhase = PSS_STOP_NONE;
    fileRead         = read;
    demuxBuffPresent = demuxBuff;
    demuxBuffFuture  = demuxBuffPast;

    fillBuff(&playpss_mp, 0);
}

// The PAL divider on the vblank interrupt: 11988/20000 of a picture per 50 Hz
// field releases videoDecMain at 29.97 Hz.
// PORT: nothing raises INTC on the host yet (ReiPan_IntcRaise has no caller),
// so this never runs; see playPssSetNtsc2Pal.
// FF3 0x002720f0
static int vblankHandler(int cause)
{
    (void)cause;

    iPalVBlankCounter += PSS_PAL_STEP;
    if (iPalVBlankCounter >= PSS_PAL_PERIOD)
    {
        iPalVBlankCounter -= PSS_PAL_PERIOD;
        iSignalSema(iMovieDecSema);
    }
    iVTCounter++;

    SYNC(0);
    EI();
    return 0;
}

// Arm the PAL path for the next movie and say which GS block its thread
// sends pictures to.
// FF3 0x00272140
void playPssSetNtsc2Pal(int on, int vram_adrs)
{
    rsrcs_vram_adrs = vram_adrs;
    // PORT: FF3 stores `on & 1` in bNtsc2Pal. The threaded path is never armed
    // here (MioPan's decision): with no vblank interrupt on the host the
    // thread would wait on its semaphore for ever and the movie would show
    // nothing; the host decoder paces itself off the stream's frame rate, so
    // the inline path plays a PAL disc at the film's rate.
    (void)on;
    Flags.bNtsc2Pal = 0;
}

// Is there a decoded picture to show: always in NTSC (decoded inline a moment
// ago), in PAL once the thread has sent one (nothing clears it).
// FF3 0x00272168
int playPssAlreadySendImage(void)
{
    if (Flags.bNtsc2Pal == 0)
        return 1;
    return Flags.bSendImage != 0;
}

// playPssStartNoWait, then demux until the decoder takes no more. In PAL it
// then builds the decode thread, its semaphore and the vblank divider.
// FF3 0x00272190
void playPssStart(playPssReadFunc read, playPssRsrcs *rsrcs)
{
    struct ThreadParam th_param;
    struct SemaParam   sema_param;

    playPssStartNoWait(read, rsrcs);

    while (fillBuff(&playpss_mp, 1) != 0)
        ;

    if (Flags.bNtsc2Pal != 0)
    {
        // PC: FF3 fills only these two fields; the rest went to the kernel
        // as stack garbage.
        memset(&sema_param, 0, sizeof(sema_param));
        sema_param.initCount = 0;
        sema_param.maxCount  = 0x7fffffff;
        iMovieDecSema = CreateSema(&sema_param);

        rsrcs_rgb32    = rsrcs->rgb32;
        rsrcs_path3tag = rsrcs->path3tag;

        memset(&th_param, 0, sizeof(th_param));
        th_param.option       = 0;
        th_param.entry        = videoDecMain;
        th_param.stack        = videoDecStack;
        th_param.stackSize    = sizeof(videoDecStack);
        th_param.initPriority = 3;
        th_param.gpReg        = nullptr;    // PC: FF3 passes _gp; the host thread has none
        videoDecTh = CreateThread(&th_param);
        StartThread(videoDecTh, nullptr);

        hid_vblank = AddIntcHandler(PSS_INTC_VBLANK, vblankHandler, 0);
        EnableIntc(PSS_INTC_VBLANK);

        iPalVBlankCounter = 0;
        iVTCounter        = 0;
    }
}

// Enough has buffered to start: one non-blocking demux took nothing.
// FF3 0x00272288
int playPssIsReady(void)
{
    return fillBuff(&playpss_mp, 0) == 0;
}

// FF3 0x002722b0
int playPssMain(playPssRsrcs *rsrcs, int vram_adrs)
{
    return playPssSetPacket(rsrcs, vram_adrs, nullptr);
}

// FF3 0x002722c8
void playPssGetMpegInfo(PLAY_PSS_MPEG_INFO *info)
{
    info->width  = playpss_mp.width;
    info->height = playpss_mp.height;
}

int playPssGetFrameIndex(void)
{
    return playpss_mp.frameCount - 1;
}

// One frame of the inline path: decode a picture, send it (pTagEnd null: to
// where setLoadImageTags was told, from the top left) or leave the chain
// centred in 640x448 for the caller, keep the audio going, refill. In PAL the
// thread does all that and this only reports.
// PORT: FF3 returns the chain's end through an int *; a host pointer does not
// fit, so the out-parameter is a u_int ** (no FF3 caller passes one:
// playPssMain is the only caller and passes null).
// FF3 0x002722e8
int playPssSetPacket(playPssRsrcs *rsrcs, int vram_adrs, u_int **pTagEnd)
{
    if (Flags.sStopPhase == PSS_STOP_DONE)
        return 2;
    if (Flags.bNtsc2Pal != 0)
        return 0;
    if (Flags.bErrorCallback != 0 || sceMpegIsEnd(&playpss_mp) != 0)
        return 1;

    REG_RCNT0_MODE  = 0x83;
    REG_RCNT0_COUNT = 0;

    if (Flags.bPauseFlg == 0 && sceMpegGetPicture(&playpss_mp, rsrcs->rgb32, PSS_PICTURE_MB) < 0)
        Flags.bErrorCallback = 1;

    if (pTagEnd == nullptr)
    {
        if (playpss_mp.frameCount == 0)
        {
            setLoadImageTags(rsrcs->path3tag, rsrcs->rgb32, 0, 0,
                             playpss_mp.width, playpss_mp.height, vram_adrs);
            bgDuration = (int)((float)playpss_mp.width * (float)playpss_mp.height * PSS_REFILL_PER_PIXEL);
        }
        // PATH3 is shared with the game's own GS traffic
        sceGsSyncPath(0, 0);
        loadImage(rsrcs->path3tag);
    }
    else
    {
        if (playpss_mp.frameCount == 0)
            bgDuration = (int)((float)playpss_mp.width * (float)playpss_mp.height * PSS_REFILL_PER_PIXEL);
        *pTagEnd = setLoadImageTags(rsrcs->path3tag, rsrcs->rgb32,
                                    (PSS_SCREEN_W - playpss_mp.width) / 2,
                                    (PSS_SCREEN_H - playpss_mp.height) / 2,
                                    playpss_mp.width, playpss_mp.height, vram_adrs);
    }

    if (audio_play_flg != 0)
    {
        audioDecSendToIOP(&audioDec);
        if (Flags.bStarted == 0 && audioDecIsPreset(&audioDec) != 0)
        {
            audioDecStart(&audioDec);
            Flags.bStarted = 1;
        }
    }

    if (Flags.sStopPhase != PSS_STOP_NONE)
        Flags.sStopPhase = PSS_STOP_DONE;

    // PORT: the break is not FF3's. REG_RCNT0_COUNT is a host clock scaled to
    // the H-blank rate and this is the game's only thread: a non-blocking
    // fillBuff that took nothing has nothing more to give this frame
    // (MioPan's playPssSetPacket).
    while (REG_RCNT0_COUNT < (u_int)((playpss_mp.width * playpss_mp.height * 500) / (PSS_SCREEN_W * PSS_SCREEN_H)))
    {
        if (fillBuff(&playpss_mp, 0) == 0)
            break;
    }
    return 0;
}

// Ask for a stop; the live decode path answers once it has drained. The audio
// is silenced here because the SPU2's auto-DMA would play on by itself (in PAL
// the thread pauses it).
// FF3 0x00272598
void playPssStop(void)
{
    if (Flags.sStopPhase == PSS_STOP_NONE)
    {
        if (Flags.bNtsc2Pal == 0 && audio_play_flg != 0)
            audioDecPause(&audioDec);
        Flags.sStopPhase = PSS_STOP_REQ;
    }
}

// FF3 0x002725f0
int playPssIsPause(void)
{
    return Flags.bPauseFlg;
}

// Only once the audio has started: before that there is nothing to hold in
// step. In PAL the thread pauses the audio when it sees the flag.
// FF3 0x00272608
void playPssPause(void)
{
    if (Flags.bStarted != 0)
    {
        Flags.bPauseFlg = 1;
        if (Flags.bNtsc2Pal == 0 && audio_play_flg != 0)
            audioDecPause(&audioDec);
    }
}

// FF3 0x00272660
void playPssRestart(void)
{
    Flags.bPauseFlg = 0;
    if (Flags.bNtsc2Pal == 0 && audio_play_flg != 0)
        audioDecResume(&audioDec);
}

// Tear down: the PAL machine (handler before thread before semaphore), the
// audio, the decoder. The buffers are the caller's.
// FF3 0x002726a8 (returns sceMpegDelete's 1; no caller reads it)
void playPssEnd(void)
{
    if (Flags.bNtsc2Pal != 0)
    {
        RemoveIntcHandler(PSS_INTC_VBLANK, hid_vblank);
        TerminateThread(videoDecTh);
        DeleteThread(videoDecTh);
        DeleteSema(iMovieDecSema);
    }
    if (audio_play_flg != 0)
        audioDecReset(&audioDec);

    sceMpegReset(&playpss_mp);
    sceMpegDelete(&playpss_mp);
}

// One video payload into the demux ring; 0 refuses it (libmpeg offers it
// again on the next demux), so a full ring stalls rather than loses data.
// Present == Past is ambiguous: Present == Future tells empty from full.
// FF3 0x00272720
static int videoCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg)
{
    sceMpegCbDataStr *cbstr = &cbdata->str;
    int               availSpace;
    int               spill;

    (void)mp;
    (void)arg;

    availSpace = (int)(demuxBuffPresent - demuxBuffPast);
    if (availSpace < 0)
        availSpace += demuxBuffSize;
    if (availSpace == 0)
        availSpace = (demuxBuffPresent == demuxBuffFuture) ? demuxBuffSize : 0;

    if ((int)cbstr->len > availSpace)
        return 0;

    // PORT: FF3 writes through the uncached-accelerated view
    // ((p & 0x0fffffff) | 0x20000000) so the IPU DMA sees the bytes without a
    // cache flush; on the host that is the same pointer.
    spill = (int)(demuxBuffPast + cbstr->len - (demuxBuff + demuxBuffSize));
    if (spill <= 0)
    {
        memcpy(demuxBuffPast, cbstr->data, cbstr->len);
        demuxBuffPast += cbstr->len;
    }
    else
    {
        memcpy(demuxBuffPast, cbstr->data, cbstr->len - spill);
        memcpy(demuxBuff, cbstr->data + (cbstr->len - spill), spill);
        demuxBuffPast = demuxBuff + spill;
    }
    return 1;
}

// One audio payload into audiodec.c's ring through its two-span producer
// API; the first four bytes are the PSS sub-stream header.
// FF3 0x00272838
static int audioCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg)
{
    sceMpegCbDataStr *cbstr = &cbdata->str;
    u_char           *pd0;
    u_char           *pd1;
    int               d0;
    int               d1;
    int               spill;

    (void)mp;
    (void)arg;

    // PC: audioDecBeginPut always writes all four; the nulls are a guard
    // only (the ROM leaves them unset).
    pd0 = nullptr;
    pd1 = nullptr;
    d0  = 0;
    d1  = 0;
    audioDecBeginPut(&audioDec, &pd0, &d0, &pd1, &d1);

    cbstr->len  -= 4;
    cbstr->data += 4;
    spill = (int)cbstr->len - d0;

    if ((int)cbstr->len > d0 + d1)
        return 0;

    if (spill <= 0)
    {
        memcpy(pd0, cbstr->data, cbstr->len);
    }
    else
    {
        memcpy(pd0, cbstr->data, cbstr->len - spill);
        memcpy(pd1, cbstr->data + (cbstr->len - spill), spill);
    }
    audioDecEndPut(&audioDec, (int)cbstr->len);
    return 1;
}

// The IPU ran dry: retire the slice it read (Present catches up to Future)
// and queue the next, whatever the demuxer has written since, rounded down to
// a quadword, at most 4 KB, clipped at the ring's end. Under 4 KB it demuxes
// more first; only a finished stream gets a short last slice.
// FF3 0x002728f8
static int nodataCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg)
{
    int availData;
    int dmaSize;

    (void)cbdata;
    (void)arg;

    demuxBuffPresent = demuxBuffFuture;
    availData        = (int)(demuxBuffPast - demuxBuffFuture);
    if (availData < 0)
        availData += demuxBuffSize;

    while (availData < PSS_IPU_DMA_SIZE)
    {
        if (fillBuff(mp, 1) == 0)
        {
            availData += 15;            // the last partial quadword still goes
            break;
        }
        availData = (int)(demuxBuffPast - demuxBuffFuture);
        if (availData < 0)
            availData += demuxBuffSize;
    }

    dmaSize = availData & ~15;
    if (dmaSize > PSS_IPU_DMA_SIZE)
        dmaSize = PSS_IPU_DMA_SIZE;

    demuxBuffFuture = demuxBuffPresent + dmaSize;
    if (demuxBuffFuture > demuxBuff + demuxBuffSize)
    {
        dmaSize         = (int)(demuxBuff + demuxBuffSize - demuxBuffPresent);
        demuxBuffFuture = demuxBuffPresent + dmaSize;
    }

    REG_DMAC_4_IPU_TO_QWC  = dmaSize / 16;
    REG_DMAC_4_IPU_TO_MADR = (u_int)(uintptr_t)demuxBuffPresent;   // PC: the low half of a host pointer; nothing reads it
    REG_DMAC_4_IPU_TO_CHCR = 0x101;

    if (demuxBuffFuture >= demuxBuff + demuxBuffSize)
        demuxBuffFuture -= demuxBuffSize;
    return 1;
}

// libmpeg waits on the IPU: spend bgDuration scanlines keeping the ring fed.
// FF3 0x00272a18
static int backgroundCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg)
{
    int dueTime;

    (void)cbdata;
    (void)arg;

    dueTime = (int)REG_RCNT0_COUNT + bgDuration;
    while ((int)REG_RCNT0_COUNT < dueTime)
        fillBuff(mp, 0);
    return 1;
}

// Stop decoding: both decode paths end the movie on the flag. (FF2 printed
// the message; FF3 does not.)
// FF3 0x00272a88
static int errorCallback(sceMpeg *mp, sceMpegCbData *cbdata, void *arg)
{
    (void)mp;
    (void)cbdata;
    (void)arg;

    Flags.bErrorCallback = 1;
    return 0;
}

// Demux from the mux block, reading a fresh one first when it is spent.
// Returns the bytes the demuxer took; 0 when it took nothing. blocking 1
// waits for the reader, 0 gives up. muxBuffFullness counts down from the
// block's end, and the cursor is derived from it.
// FF3 0x00272aa0
static int fillBuff(sceMpeg *mp, int blocking)
{
    char *startPos;
    int   size;

    if (muxBuffFullness == 0)
    {
        while ((muxBuffFullness = fileRead(muxBuff, PSS_MUX_BUFF_SIZE)) == 0)
        {
            if (blocking == 0)
                return 0;
        }
        startPos = muxBuff;
    }
    else
    {
        startPos = muxBuff + (PSS_MUX_BUFF_SIZE - muxBuffFullness);
    }

    size = sceMpegDemuxPss(mp, startPos, muxBuffFullness);
    muxBuffFullness -= size;
    return size;
}

// FF3-only twin of playPssSetPacket for a movie shown as a texture (the map's
// sky): decode a width x height picture and build the chain that loads it at
// tbp into rsrcs->path3tag, its size through *size (0 when no new picture;
// the size is kept from the first frame, the chain does not change). Returns
// as playPssSetPacket.
// FF3 0x00272b38
int playPssSetTexPacket(playPssRsrcs *rsrcs, int tbp, int *size, int width, int height)
{
    *size = 0;

    if (Flags.sStopPhase == PSS_STOP_DONE)
        return 2;
    if (Flags.bNtsc2Pal != 0)
        return 0;
    if (Flags.bErrorCallback != 0 || sceMpegIsEnd(&playpss_mp) != 0)
        return 1;

    REG_RCNT0_MODE  = 0x83;
    REG_RCNT0_COUNT = 0;

    if (Flags.bPauseFlg == 0 &&
        sceMpegGetPicture(&playpss_mp, rsrcs->rgb32, (width / 16) * (height / 16)) < 0)
        Flags.bErrorCallback = 1;

    if (playpss_mp.frameCount == 0)
    {
        sTexPacketSize = setLoadTexTags(rsrcs->path3tag, rsrcs->rgb32, 0, 0,
                                        playpss_mp.width, playpss_mp.height, tbp);
        bgDuration = (int)((float)playpss_mp.width * (float)playpss_mp.height * PSS_REFILL_PER_PIXEL);
    }
    *size = sTexPacketSize;

    if (audio_play_flg != 0)
    {
        audioDecSendToIOP(&audioDec);
        if (Flags.bStarted == 0 && audioDecIsPreset(&audioDec) != 0)
        {
            audioDecStart(&audioDec);
            Flags.bStarted = 1;
        }
    }

    if (Flags.sStopPhase != PSS_STOP_NONE)
        Flags.sStopPhase = PSS_STOP_DONE;

    // PORT: the break is not FF3's. REG_RCNT0_COUNT is a host clock scaled to
    // the H-blank rate and this is the game's only thread: a non-blocking
    // fillBuff that took nothing has nothing more to give this frame
    // (MioPan's playPssSetPacket).
    while (REG_RCNT0_COUNT < (u_int)((playpss_mp.width * playpss_mp.height * 500) / (PSS_SCREEN_W * PSS_SCREEN_H)))
    {
        if (fillBuff(&playpss_mp, 0) == 0)
            break;
    }
    return 0;
}
