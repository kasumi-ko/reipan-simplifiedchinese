/* ==========================================================================
 *  system/eeiop/snd_stream.h
 *
 *  ADPCM streaming (FF3 snd_stream.c, 0x26b610..0x26c8c0; FF2's
 *  snd_stream.c). Two slots ("wrk" 0 and 1); each streams one interleaved
 *  multi-channel file off disc through a pair of SPU ring buffers per channel
 *  that the IOP refills on an SPU IRQ. A stream costs one SPU voice per
 *  channel plus one of the two IRQ "cores". stream_auto.c decides what plays
 *  in each slot.
 *
 *  Lifecycle: HEADER_LOAD -> PRE_LOAD -> START -> PLAYING -> WAIT_END ->
 *  END -> NO_USE, driven by SndStreamMain() against the status the IOP
 *  reports back through GetStreamWrkRet().
 * ======================================================================== */
#ifndef _SYSTEM_EEIOP_SND_STREAM_H
#define _SYSTEM_EEIOP_SND_STREAM_H

#include <stddef.h>                     // offsetof

#include "sdk/scetypes.h"               // u_int
#include "system/eeiop/snd.h"           // SOUND_INFO, VOLSET, VOICE_ATTR
#include "system/eeiop/snd_3d.h"        // SND_3D_SET, SND_3D_WRK

// A stream's state, on the EE (SND_STREAM_WRK.status) and as the IOP reports
// it (IOP_STREAM_RET.status) -- FF2 snd_def.h.
typedef enum
{
    ST_STREAM_NO_USE      = 0,
    ST_STREAM_HEADER_LOAD = 1,
    ST_STREAM_PRE_LOAD    = 2,
    ST_STREAM_PLAYING     = 3,
    ST_STREAM_WAIT_END    = 4,
    ST_STREAM_END         = 5,
    ST_STREAM_START       = 6,
} EEIOP_STREAM_STATUS;

typedef enum
{
    SND_STREAM_OK          = 0,
    SND_STREAM_ERR_IOPSEND = 1,         // the IOP command queue was full
    SND_STREAM_ERR_HEADER  = 2,         // header not loaded / IOP not preloaded yet
    SND_STREAM_ERR_NOT_USE = 3,
    SND_STREAM_ERR_IN_USE  = 4,
    SND_STREAM_ERR_VOICE   = 5,         // no free SPU voice
} SND_STREAM_ERR;

// REQ_STREAM_PLAY's payload (FF2 snd_def.h; sent in place from the work).
typedef struct
{
    /* 0x00 */ VOLSET     vol[2];       // per channel
    /* 0x08 */ VOICE_ATTR attr[2];
    /* 0x0c */ short      adsr1[2];
    /* 0x10 */ short      adsr2[2];
    /* 0x14 */ char       voice[2];     // SPU voice per channel; -1: none
    /* 0x16 */ short      pitch;
    /* 0x18 */ short      irq_core;     // index into irq_core_source
    /* 0x1a */ short      wrk_id;
} STREAM_PLAY;                          /* 0x1c */

static_assert(sizeof(STREAM_PLAY) == 0x1c, "STREAM_PLAY size");
static_assert(offsetof(STREAM_PLAY, attr) == 0x08, "STREAM_PLAY.attr");
static_assert(offsetof(STREAM_PLAY, adsr1) == 0x0c, "STREAM_PLAY.adsr1");
static_assert(offsetof(STREAM_PLAY, adsr2) == 0x10, "STREAM_PLAY.adsr2");
static_assert(offsetof(STREAM_PLAY, voice) == 0x14, "STREAM_PLAY.voice");
static_assert(offsetof(STREAM_PLAY, pitch) == 0x16, "STREAM_PLAY.pitch");
static_assert(offsetof(STREAM_PLAY, irq_core) == 0x18, "STREAM_PLAY.irq_core");
static_assert(offsetof(STREAM_PLAY, wrk_id) == 0x1a, "STREAM_PLAY.wrk_id");

// REQ_STREAM_START's payload. FF2's ended in a 256-byte file name; FF3 sends
// 0x3c bytes and has no name (SetStreamStartSub fills no buffer).
typedef struct
{
    /* 0x00 */ int   start_sector;
    /* 0x04 */ int   size;
    /* 0x08 */ u_int spu_packet[2][2];  // [ch][half]: SPU addresses of the ring halves
    /* 0x18 */ short nchannel;
    /* 0x1a */ char  irq_core;          // FF2, unverified (FF3 never writes it)
    /* 0x1b */ char  wrk_id;
    /* 0x1c */ int   interleave_byte;
    /* 0x20 */ int   loop_start_block;
    /* 0x24 */ int   loop_end_block;
    /* 0x28 */ int   loop_start_fraction;
    /* 0x2c */ int   loop_end_fraction;
    /* 0x30 */ u_int spu_loop_packet[2];   // base of a 4-unit allocation, or 0
    /* 0x38 */ int   offset;            // sectors to resume from
} STREAM_START;                         /* 0x3c */

static_assert(sizeof(STREAM_START) == 0x3c, "STREAM_START size");
static_assert(offsetof(STREAM_START, size) == 0x04, "STREAM_START.size");
static_assert(offsetof(STREAM_START, spu_packet) == 0x08, "STREAM_START.spu_packet");
static_assert(offsetof(STREAM_START, nchannel) == 0x18, "STREAM_START.nchannel");
static_assert(offsetof(STREAM_START, wrk_id) == 0x1b, "STREAM_START.wrk_id");
static_assert(offsetof(STREAM_START, interleave_byte) == 0x1c, "STREAM_START.interleave_byte");
static_assert(offsetof(STREAM_START, loop_start_block) == 0x20, "STREAM_START.loop_start_block");
static_assert(offsetof(STREAM_START, loop_end_block) == 0x24, "STREAM_START.loop_end_block");
static_assert(offsetof(STREAM_START, loop_start_fraction) == 0x28, "STREAM_START.loop_start_fraction");
static_assert(offsetof(STREAM_START, loop_end_fraction) == 0x2c, "STREAM_START.loop_end_fraction");
static_assert(offsetof(STREAM_START, spu_loop_packet) == 0x30, "STREAM_START.spu_loop_packet");
static_assert(offsetof(STREAM_START, offset) == 0x38, "STREAM_START.offset");

// The one-word payloads (REQ_STREAM_STOP / ABORT / PAUSE / RESTART).
typedef struct
{
    /* 0x0 */ int wrk_id;
} STREAM_WRK_ID;

static_assert(sizeof(STREAM_WRK_ID) == 0x4, "STREAM_WRK_ID size");

// REQ_STREAM_SETVOL's payload.
typedef struct
{
    /* 0x0 */ VOLSET vol[2];
    /* 0x8 */ int    wrk_id;
} STREAM_SETVOL;                        /* 0xc */

static_assert(sizeof(STREAM_SETVOL) == 0xc, "STREAM_SETVOL size");
static_assert(offsetof(STREAM_SETVOL, wrk_id) == 0x8, "STREAM_SETVOL.wrk_id");

// REQ_STREAM_SETPITCH's payload.
typedef struct
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short pitch;
} STREAM_SETPITCH;                      /* 0x4 */

static_assert(sizeof(STREAM_SETPITCH) == 0x4, "STREAM_SETPITCH size");
static_assert(offsetof(STREAM_SETPITCH, pitch) == 0x2, "STREAM_SETPITCH.pitch");

// One stream slot (bss 0x40f680, 2 x 0xc0). p and s are the IOP payloads and
// are sent in place. FF3 adds `loop` (SndStreamStart's last argument): only
// then do the header's loop points reach the IOP.
// layout: host -- s3dhndl and header_buf are pointers, so the EE offsets hold
// up to 0x98 only.
typedef struct
{
    /* 0x000 */ STREAM_PLAY  p;
    /* 0x01c */ STREAM_START s;
    /* 0x058 */ SOUND_INFO   info[2];   // per channel, from the HXD header
    /* 0x090 */ int          status;    // an EEIOP_STREAM_STATUS
    /* 0x094:0 */ u_int      header_ready : 1;
    /* 0x094:1 */ u_int      pre_load_ok  : 1;  // the IOP has filled the ring
    /* 0x094:2 */ u_int      flag2        : 1;  // never written in FF3 (SndStreamInitWrk keeps it)
    /* 0x094:3 */ u_int      play_flg     : 1;  // SndStreamPlay got its voices
    /* 0x094:4 */ u_int      fade_stop    : 1;  // stop once the fade-out reaches 0
    /* 0x094:5 */ u_int      stop         : 1;  // REQ_STREAM_STOP sent
    /* 0x094:6 */ u_int      loop         : 1;  // FF3: use the header's loop points
    /* 0x098 */ int          header_id; // fileload id of the HXD load
    /* 0x09c */ SND_3D_WRK  *s3dhndl;
    /* 0x0a0 */ void        *header_buf;    // header_buf_64[slot]
    /* 0x0a4 */ short        vol;
    /* 0x0a6 */ short        target_vol;
    /* 0x0a8 */ short        spd;
    /* 0x0aa */ short        pitch;
    /* 0x0ac */ short        target_pitch;
    /* 0x0ae */ short        pspd;
    /* 0x0b0 */ short        abort_cnt; // frames before a forced abort
    /* 0x0b2 */ char         s3d[2];    // per channel
    /* 0x0b4 */ int          offset;    // where playback ended (sectors)
    /* 0x0b8 */ float        play_spd;
    /* 0x0bc */ int          file_no;
                unsigned int header_size;
} SND_STREAM_WRK;                       /* 0xc0 */

static_assert(offsetof(SND_STREAM_WRK, s) == 0x1c, "SND_STREAM_WRK.s");
static_assert(offsetof(SND_STREAM_WRK, info) == 0x58, "SND_STREAM_WRK.info");
static_assert(offsetof(SND_STREAM_WRK, status) == 0x90, "SND_STREAM_WRK.status");
static_assert(offsetof(SND_STREAM_WRK, header_id) == 0x98, "SND_STREAM_WRK.header_id");

void SndStreamFadeStop(int wrk_id, int time);           // FF3 0x0026b610: cancel the load, stop, or fade to 0 and stop
void SndStreamFade(int wrk_id, int vol, int time);      // FF3 0x0026b6e0
void SndStreamAllStop(void);                            // FF3 0x0026b740
void SndStreamInit(int load_priority);                  // FF3 0x0026b768: REQ_STREAM_CREATE, reset both slots
SND_STREAM_ERR SndStreamStartHeaderOnMemory(int wrk_id, int file_no, HXD_HEADER *header,
                                            int offset, int loop);    // FF3 0x0026b7e0: unreferenced
// header_file_no -0x21: the header is already in the work (resume path).
SND_STREAM_ERR SndStreamStart(int wrk_id, int file_no, int header_file_no, int offset, int loop);   // FF3 0x0026b898
int  SndStreamIsHeaderReady(int wrk_id);                // FF3 0x0026b950
SND_STREAM_ERR SndStreamPlay(int wrk_id, int effect, int loop, int vol, int in_time,
                             SND_3D_SET *s3s, float play_spd, int pitch);      // FF3 0x0026b9a8
void SndStreamMain(void);                               // FF3 0x0026bc38: per frame, from SndMain
int  SetIRQCore(SND_STREAM_WRK *wrk);                   // FF3 0x0026c298
void SndStreamStop(int wrk_id);                         // FF3 0x0026c540: asks the IOP; WAIT_END
void SndStreamPause(int wrk_id);                        // FF3 0x0026c5c0
void SndStreamRestart(int wrk_id);                      // FF3 0x0026c610
void SndStreamSetPosition(int wrk_id, float *pos);      // FF3 0x0026c658: unreferenced
int  SndStreamIsUse(int wrk_id);                        // FF3 0x0026c6e0
bool SndStreamGetCaptionPosition(int slot, int *file, int *milliseconds);
int  SndStreamIsPreload(int wrk_id);                    // FF3 0x0026c700: 1 for a free slot
SND_STREAM_ERR SndStreamGetInfo(int wrk_id, int *nchannel, int *interleave_byte,
                                SOUND_INFO **info);     // FF3 0x0026c738
int  SndStreamGetNowOffset(int wrk_id);                 // FF3 0x0026c7a0: sectors played; 0 unless PLAYING
SND_STREAM_ERR SndStreamChangePlaySpeed(int wrk_id, float rate);   // FF3 0x0026c7e8
void SndStreamSet3D(int wrk_id, SND_3D_SET *s3s);       // FF3 0x0026c808
void SndStreamFadePitch(int wrk_id, int pitch, int time);   // FF3 0x0026c860

#endif /* _SYSTEM_EEIOP_SND_STREAM_H */
