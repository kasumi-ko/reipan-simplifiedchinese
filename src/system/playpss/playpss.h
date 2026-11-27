/* ==========================================================================
 *  system/playpss/playpss.h
 *
 *  PSS stream player (FF2 playpss.c). Minimal so far: what reconstructed code
 *  calls.
 * ======================================================================== */
#ifndef _SYSTEM_PLAYPSS_PLAYPSS_H
#define _SYSTEM_PLAYPSS_PLAYPSS_H

#include <stddef.h>                     // offsetof

#include "sdk/libmpeg.h"                // sceIpuRGB32

// The decoded frame's size, from the MPEG sequence header (FF2's struct).
struct PLAY_PSS_MPEG_INFO               /* 0x8 */
{
    /* 0x0 */ int width;
    /* 0x4 */ int height;
};
static_assert(sizeof(PLAY_PSS_MPEG_INFO) == 0x8, "PLAY_PSS_MPEG_INFO is 0x8 bytes");
static_assert(offsetof(PLAY_PSS_MPEG_INFO, height) == 0x4, "PLAY_PSS_MPEG_INFO.height");

// Everything the decoder needs: the caller allocates the buffers and hands
// the block to playPssStart (FF2's playPssRsrcs, 0x3c on the EE; FF3's map
// sky keeps one at MAP_SKY +0x28 and fills the same offsets). The iop*
// members are IOP addresses.
// layout: host -- the pointers are 4 bytes on the EE; EE offsets in the comments.
struct playPssRsrcs
{
    /* 0x00 */ u_char      *mpegBuff;       // demuxed video ES ring
    /* 0x04 */ int          mpegBuffSize;
    /* 0x08 */ sceIpuRGB32 *rgb32;          // IPU output, one per macroblock
    /* 0x0c */ u_int       *path3tag;       // GS load-image DMA chain
    /* 0x10 */ void        *demuxBuff;      // raw PSS sectors
    /* 0x14 */ int          demuxBuffSize;
    /* 0x18 */ u_char      *audioBuff;      // demuxed audio ES
    /* 0x1c */ int          audioBuffSize;
    /* 0x20 */ int          iopBuff;        // its IOP-side twin
    /* 0x24 */ int          iopBuffSize;
    /* 0x28 */ u_char      *zeroBuff;       // one sector of silence
    /* 0x2c */ int          iopZeroBuff;
    /* 0x30 */ char        *read_buf;       // stream read staging
    /* 0x34 */ int          audio_vol_percent;
    /* 0x38 */ int          auto_dma_core;  // SPU core claimed for it
};
static_assert(offsetof(playPssRsrcs, mpegBuffSize) == sizeof(void *), "playPssRsrcs.mpegBuffSize (EE +0x04)");
static_assert(offsetof(playPssRsrcs, demuxBuffSize) == offsetof(playPssRsrcs, demuxBuff) + sizeof(void *),
              "playPssRsrcs.demuxBuffSize (EE +0x14)");
static_assert(offsetof(playPssRsrcs, auto_dma_core) == offsetof(playPssRsrcs, audio_vol_percent) + 4,
              "playPssRsrcs.auto_dma_core (EE +0x38)");

// The stream reader playPssStart is handed (MyStrRead, PssLoopRead).
typedef int (*playPssReadFunc)(void *buff, int size);

void playPssInit(void);                                  // FF3 0x00271cb8
void playPssStartNoWait(playPssReadFunc read, playPssRsrcs *rsrcs);  // FF3 0x00271f68
void playPssStart(playPssReadFunc read, playPssRsrcs *rsrcs);  // FF3 0x00272190
int  playPssIsReady(void);                              // FF3 0x00272288: the first frame has decoded
void playPssGetMpegInfo(PLAY_PSS_MPEG_INFO *info);     // FF3 0x002722c8
int  playPssGetFrameIndex(void);                      // Zero-based film frame, -1 before the first picture.

// One frame: 0 decoding, 1 end of stream or an error, 2 once a requested
// stop has drained. playPssMain sends the picture to vram_adrs itself;
// playPssSetPacket with pTagEnd leaves the chain (centred in 640x448) for
// the caller and returns its end through pTagEnd.
int  playPssMain(playPssRsrcs *rsrcs, int vram_adrs);  // FF3 0x002722b0
int  playPssSetPacket(playPssRsrcs *rsrcs, int vram_adrs, u_int **pTagEnd);  // FF3 0x002722e8

void playPssStop(void);                                 // FF3 0x00272598
int  playPssIsPause(void);                              // FF3 0x002725f0
void playPssPause(void);                                // FF3 0x00272608
void playPssRestart(void);                              // FF3 0x00272660
void playPssEnd(void);                                  // FF3 0x002726a8

// The PAL path (a vblank-paced decode thread) and its "a frame landed" flag.
void playPssSetNtsc2Pal(int on, int vram_adrs);         // FF3 0x00272140
int  playPssAlreadySendImage(void);                     // FF3 0x00272168

// FF3's twin of playPssSetPacket: decode one frame and build the chain that
// loads it as a width x height texture at tbp into rsrcs->path3tag; its size
// in bytes through *size (0 when no new frame). 0 decoding, 1 end of stream
// or an error, 2 once a requested stop has drained.
int  playPssSetTexPacket(playPssRsrcs *rsrcs, int tbp, int *size, int width, int height);  // FF3 0x00272b38

#endif /* _SYSTEM_PLAYPSS_PLAYPSS_H */
