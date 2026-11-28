/* ==========================================================================
 *  system/playpss/ldimage.c
 *
 *  The GS side of the PSS movie player: the IPU's decoded picture, a run of
 *  16x16 RGBA32 macroblocks written column by column ("YX"), turned into a
 *  DMA chain of one host-to-local transfer per macroblock. BITBLTBUF and
 *  TRXREG are set once at the head; each block carries its TRXPOS, TRXDIR and
 *  IMAGE GIFtag inline and a REF tag to its 1024 bytes.
 *
 *  FF3 0x273568..0x273d40. FF2's ldimage.c in the same order with the same
 *  bodies (setLoadImageTags .. loadImage, ported from MioPan and checked
 *  against every FF3 disassembly), then three FF3-only functions:
 *  setVIFcodes, setLoadTexTagsYX and setLoadTexTags, which build the same
 *  transfer for the VIF1 packet buffer (MapSky's movie, the title backdrop).
 *
 *  PORT: the chains are built as the ROM builds them, but no DMAC runs them,
 *  and a tag's ADDR field is a 31-bit EE address:
 *   - setDMAscTag writes the block's EE address when it lives in emulated RAM
 *     and also keeps the host pointer in the tag's upper half, which the PATH3
 *     chain never transfers (CHCR.TTE is clear) and the ROM leaves alone; the
 *     VIF1 chain overwrites that half with its VIF codes, as on the EE.
 *   - setLoadImageTags / setLoadTexTags arm the direct picture path
 *     (reipan/rendering/reipan_video.h): the decoder hands its frame straight
 *     to the renderer, which answers any TEX0 naming that GS block with it.
 *     That is how a movie reaches the screen on the PC; the chains are never
 *     walked while it is armed (loadImage still walks a PATH3 chain when it
 *     is not, as MioPan does).
 * ======================================================================== */
#include "system/playpss/ldimage.h"

#include <stdint.h>
#include <string.h>                     // memcpy, memset

#include "sdk/eeregs.h"                 // REG_DMAC_2_GIF_*
#include "sdk/libgraph.h"               // SCE_GS_* / SCE_GIF_*
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetPs2Address
#include "reipan/gs/reipan_gs_c.h"      // PC: ReiPan_GsUpload
#include "reipan/rendering/reipan_video.h"  // PC: the direct picture path

// A macroblock is 16x16 RGBA32: 0x40 quadwords, 0x400 bytes.
#define LDIMAGE_MB_SIZE         16
#define LDIMAGE_MB_QWC          0x40
#define LDIMAGE_MB_BYTES        0x400

// setLoadImageTags' destination: a 640-pixel page (BITBLTBUF counts 64s),
// PSMCT32 (format 0).
#define LDIMAGE_DBW             10
#define LDIMAGE_DPSM            0

// Source-chain DMA tag ids.
#define LDIMAGE_DMATAG_CNT      1
#define LDIMAGE_DMATAG_REF      3
#define LDIMAGE_DMATAG_END      7

// VIF codes in a VIF1 chain's tag: NOP, and DIRECT (to the GIF over PATH2)
// for qwc quadwords.
#define LDIMAGE_VIF_NOP         0
#define LDIMAGE_VIF_DIRECT(qwc) (0x50000000u | (u_int)(qwc))

// The ROM writes the chains through the EE's uncached-accelerated window
// (tags & 0x0fffffff | 0x20000000) so the DMAC sees them without a cache
// flush. PORT: identity -- relocating a host pointer gives a wild pointer.
#define LDIMAGE_UNCACHED(p)     (p)

static u_int *setLoadImageTagsYX(u_int *tags, void *image, int skip, int x, int y,
                                 int width, int height, int vram_adrs);
static void   setDMAscTag(u_int *p, u_int spr, void *addr, u_int irq,
                          u_int id, u_int pce, u_int qwc);
static void   setGIFtag(u_int *p, u_long regs, u_int nreg, u_int flg,
                        u_int prim, u_int pre, u_int eop, u_int nloop);
static void   setGIFad(u_int *p, u_int reg, u_long value);
static void   setBITBLTBUF(u_int *p, u_int dbp, u_int dbw, u_int dpsm);
static void   setTRXPOS(u_int *p, u_int dir, u_int dsax, u_int dsay);
static void   setTRXREG(u_int *p, u_int rrw, u_int rrh);
static void   setTRXDIR(u_int *p, u_int xdir);
static void   setVIFcodes(u_int *p, u_int code0, u_int code1);
static int    setLoadTexTagsYX(u_int *tags, void *image, int skip, int x, int y,
                               int width, int height, int tbp);
static void   ldImageRunChain(const u_int *tags);   // PORT, see loadImage

// ──────────────────────────────────────────────────────────────────────
// The macroblocks are contiguous, so the stride is one macroblock.
// FF3 0x00273568
// callcheck: ReiPan_VideoBegin (PC: arms the direct picture path)

u_int *setLoadImageTags(u_int *tags, sceIpuRGB32 *image, int x, int y,
                        int width, int height, int vram_adrs)
{
    // PC: the one place the movie player says where the picture goes.
    ReiPan_VideoBegin(vram_adrs, 10, 9);

    return setLoadImageTagsYX(tags, image, LDIMAGE_MB_BYTES, x, y, width, height, vram_adrs);
}

// ──────────────────────────────────────────────────────────────────────
// Build the PATH3 chain. `i` walks macroblock columns and `j` rows, so the
// source advances down a column first, the order the IPU wrote them in. EOP
// goes on the last block's IMAGE tag; an END tag closes the chain, and the
// return is one past it.
// FF3 0x002735a0

static u_int *setLoadImageTagsYX(u_int *tags, void *image, int skip, int x, int y,
                                 int width, int height, int vram_adrs)
{
    int    mbx = width >> 4;
    int    mby = height >> 4;
    u_int *p   = LDIMAGE_UNCACHED(tags);
    int    i;
    int    j;

    setDMAscTag(p, 0, nullptr, 0, LDIMAGE_DMATAG_CNT, 0, 3);
    p += 4;
    setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED, 0, 0, 0, 2);
    p += 4;
    setBITBLTBUF(p, (u_int)vram_adrs, LDIMAGE_DBW, LDIMAGE_DPSM);
    p += 4;
    setTRXREG(p, LDIMAGE_MB_SIZE, LDIMAGE_MB_SIZE);
    p += 4;

    for (i = 0; i < mbx; i++)
    {
        for (j = 0; j < mby; j++)
        {
            u_int eop = (i == mbx - 1 && j == mby - 1);

            setDMAscTag(p, 0, nullptr, 0, LDIMAGE_DMATAG_CNT, 0, 4);
            p += 4;
            setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED, 0, 0, 0, 2);
            p += 4;
            setTRXPOS(p, 0, (u_int)(x + i * LDIMAGE_MB_SIZE), (u_int)(y + j * LDIMAGE_MB_SIZE));
            p += 4;
            setTRXDIR(p, 0);
            p += 4;
            setGIFtag(p, 0, 0, SCE_GIF_IMAGE, 0, 0, eop, LDIMAGE_MB_QWC);
            p += 4;
            setDMAscTag(p, 0, image, 0, LDIMAGE_DMATAG_REF, 0, LDIMAGE_MB_QWC);
            p += 4;

            image = (char *)image + skip;
        }
    }

    setDMAscTag(p, 0, nullptr, 0, LDIMAGE_DMATAG_END, 0, 0);
    return p + 4;
}

// ──────────────────────────────────────────────────────────────────────
// One source-chain DMA tag as a single 64-bit word; the ROM never writes the
// quadword's upper half.
// PORT: ADDR gets the block's EE address (the low 32 bits of the pointer when
// it is not in emulated RAM: nothing reads it then), and the host pointer goes
// in the upper half for ldImageRunChain.
// FF3 0x00273818
// callcheck: ReiPan_GetPs2Address (PC: host pointer -> EE address)

static void setDMAscTag(u_int *p, u_int spr, void *addr, u_int irq,
                        u_int id, u_int pce, u_int qwc)
{
    u_int ee = (u_int)ReiPan_GetPs2Address(addr);

    if (ee == 0)
        ee = (u_int)(uintptr_t)addr;

    u_long tag = ((u_long)spr << 63)
               | ((u_long)(ee & ~15u) << 32)
               | ((u_long)irq << 31)
               | ((u_long)id << 28)
               | ((u_long)pce << 26)
               | (u_long)qwc;
    memcpy(p, &tag, sizeof(tag));

    memcpy(p + 2, &addr, sizeof(addr));
}

// ──────────────────────────────────────────────────────────────────────
// A GIFtag as four words: NLOOP/EOP, PRE/PRIM/FLG/NREG, then the register
// descriptor.
// FF3 0x00273880

static void setGIFtag(u_int *p, u_long regs, u_int nreg, u_int flg,
                      u_int prim, u_int pre, u_int eop, u_int nloop)
{
    p[0] = (eop << 15) | nloop;
    p[1] = (pre << 14) | (prim << 15) | (flg << 26) | (nreg << 28);
    p[2] = (u_int)regs;
    p[3] = (u_int)(regs >> 32);
}

// ──────────────────────────────────────────────────────────────────────
// One PACKED A+D item: the 64-bit value, then the register address.
// FF3 0x002738d0

static void setGIFad(u_int *p, u_int reg, u_long value)
{
    p[2] = reg;
    p[3] = 0;
    p[1] = (u_int)(value >> 32);
    p[0] = (u_int)value;
}

// ──────────────────────────────────────────────────────────────────────
// The four registers of a host-to-local transfer, one A+D item each.
// FF3 0x002738f8

static void setBITBLTBUF(u_int *p, u_int dbp, u_int dbw, u_int dpsm)
{
    setGIFad(p, SCE_GS_BITBLTBUF, ((u_long)dpsm << 56) | ((u_long)dbw << 48) | ((u_long)dbp << 32));
}

// FF3 0x00273948

static void setTRXPOS(u_int *p, u_int dir, u_int dsax, u_int dsay)
{
    setGIFad(p, SCE_GS_TRXPOS, ((u_long)dir << 59) | ((u_long)dsay << 48) | ((u_long)dsax << 32));
}

// FF3 0x00273998

static void setTRXREG(u_int *p, u_int rrw, u_int rrh)
{
    setGIFad(p, SCE_GS_TRXREG, ((u_long)rrh << 32) | (u_long)rrw);
}

// FF3 0x002739d0

static void setTRXDIR(u_int *p, u_int xdir)
{
    setGIFad(p, SCE_GS_TRXDIR, (u_long)xdir);
}

// ──────────────────────────────────────────────────────────────────────
// PORT, no ROM counterpart: walk a setLoadImageTagsYX chain as the DMAC and
// GIF would -- collect the four transfer registers from the PACKED A+D items
// and, when an IMAGE GIFtag arrives, hand the block the next REF tag points
// at to the host GS. The IMAGE tag closes a CNT block and its payload is the
// REF block after it, so the pending count survives one tag. (MioPan's.)

#define LDIMAGE_MAX_TAGS    0x4000  // 640x448 is 1120 blocks; this is slack

static void ldImageRunChain(const u_int *tags)
{
    sceGsLoadImage load;
    const u_int   *p       = tags;
    int            have    = 0;     // one bit per register seen
    u_int          pending = 0;     // quadwords an IMAGE tag is waiting for
    int            guard;

    if (tags == nullptr)
        return;

    memset(&load, 0, sizeof(load));

    for (guard = 0; guard < LDIMAGE_MAX_TAGS; guard++)
    {
        const u_int *body;
        u_long       tag;
        void        *ref;
        u_int        qwc;
        u_int        id;
        u_int        nloop;
        u_int        k;

        memcpy(&tag, p, sizeof(tag));
        memcpy(&ref, p + 2, sizeof(ref));

        qwc = (u_int)(tag & 0xffffu);
        id  = (u_int)((tag >> 28) & 7u);

        if (id == LDIMAGE_DMATAG_REF)
        {
            body = (const u_int *)ref;
            p   += 4;
        }
        else
        {
            body = p + 4;
            p   += 4 + qwc * 4;
        }

        if (pending != 0)
        {
            if (have == 0xf && body != nullptr && qwc >= pending)
                ReiPan_GsUpload(&load, (unsigned char *)body);
            pending = 0;
        }
        else
        {
            nloop = 0;

            for (k = 0; k < qwc; k++)
            {
                const u_int *qw = body + k * 4;
                u_long       lo;
                u_long       hi;

                memcpy(&lo, qw, sizeof(lo));
                memcpy(&hi, qw + 2, sizeof(hi));

                if (nloop == 0)
                {
                    // A GIFtag: the PACKED A+D run that follows, or the IMAGE
                    // tag that closes the block.
                    if ((u_int)((lo >> 58) & 3u) == SCE_GIF_IMAGE)
                    {
                        pending = (u_int)(lo & 0x7fffu);
                        break;
                    }
                    nloop = (u_int)(lo & 0x7fffu);
                    continue;
                }

                switch ((u_int)(hi & 0x7fu))
                {
                case SCE_GS_BITBLTBUF:
                    memcpy(&load.bitbltbuf, &lo, sizeof(lo));
                    load.bitbltbufaddr = SCE_GS_BITBLTBUF;
                    have |= 1;
                    break;
                case SCE_GS_TRXPOS:
                    memcpy(&load.trxpos, &lo, sizeof(lo));
                    load.trxposaddr = SCE_GS_TRXPOS;
                    have |= 2;
                    break;
                case SCE_GS_TRXREG:
                    memcpy(&load.trxreg, &lo, sizeof(lo));
                    load.trxregaddr = SCE_GS_TRXREG;
                    have |= 4;
                    break;
                case SCE_GS_TRXDIR:
                    memcpy(&load.trxdir, &lo, sizeof(lo));
                    load.trxdiraddr = SCE_GS_TRXDIR;
                    have |= 8;
                    break;
                default:
                    break;
                }

                nloop--;
            }
        }

        if (id == LDIMAGE_DMATAG_END)
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Kick the chain down PATH3: QWC 0 (the tags carry their counts), CHCR 0x105
// = to device, source chain, start.
// PORT: no DMAC. While the direct picture path is armed the decoder has
// already handed the frame to the renderer, so the walk is skipped; otherwise
// ldImageRunChain does the transfer. STR is cleared afterwards as the channel
// would have (g3dGsPutDrawEnv-style waits spin on it).
// FF3 0x002739f8
// callcheck: ReiPan_VideoIsLive, ldImageRunChain (PC)

void loadImage(u_int *tags)
{
    REG_DMAC_2_GIF_TADR = (u_int)((uintptr_t)tags & 0x0fffffff);
    REG_DMAC_2_GIF_QWC  = 0;
    REG_DMAC_2_GIF_CHCR = 0x105;

    if (ReiPan_VideoIsLive() == 0)
        ldImageRunChain(tags);
    REG_DMAC_2_GIF_CHCR &= ~0x100u;
}

// ──────────────────────────────────────────────────────────────────────
// FF3-only: two words into the upper half of a VIF1 chain's DMA tag (the VIF
// codes the DMAC sends ahead of the data when CHCR.TTE is set).
// FF3 0x00273a30

static void setVIFcodes(u_int *p, u_int code0, u_int code1)
{
    p[0] = code0;
    p[1] = code1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3-only twin of setLoadImageTagsYX for the VIF1 packet buffer: each DMA
// tag carries NOP + DIRECT so its data goes on to the GIF over PATH2, the
// destination buffer is the picture's own width rounded up to 64, and there is
// no END tag (the packet buffer closes its own chain). Returns the chain's
// size in bytes.
// FF3 0x00273a40

static int setLoadTexTagsYX(u_int *tags, void *image, int skip, int x, int y,
                            int width, int height, int tbp)
{
    int    mbx = width >> 4;
    int    mby = height >> 4;
    u_int *p   = LDIMAGE_UNCACHED(tags);
    int    i;
    int    j;

    setDMAscTag(p, 0, nullptr, 0, LDIMAGE_DMATAG_CNT, 0, 3);
    setVIFcodes(p + 2, LDIMAGE_VIF_NOP, LDIMAGE_VIF_DIRECT(3));
    p += 4;
    setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED, 0, 0, 0, 2);
    p += 4;
    setBITBLTBUF(p, (u_int)tbp, (u_int)((width + 63) / 64), 0);
    p += 4;
    setTRXREG(p, LDIMAGE_MB_SIZE, LDIMAGE_MB_SIZE);
    p += 4;

    for (i = 0; i < mbx; i++)
    {
        for (j = 0; j < mby; j++)
        {
            u_int eop = (i == mbx - 1 && j == mby - 1);

            setDMAscTag(p, 0, nullptr, 0, LDIMAGE_DMATAG_CNT, 0, 4);
            setVIFcodes(p + 2, LDIMAGE_VIF_NOP, LDIMAGE_VIF_DIRECT(4));
            p += 4;
            setGIFtag(p, SCE_GIF_PACKED_AD, 1, SCE_GIF_PACKED, 0, 0, 0, 2);
            p += 4;
            setTRXPOS(p, 0, (u_int)(x + i * LDIMAGE_MB_SIZE), (u_int)(y + j * LDIMAGE_MB_SIZE));
            p += 4;
            setTRXDIR(p, 0);
            p += 4;
            setGIFtag(p, 0, 0, SCE_GIF_IMAGE, 0, 0, eop, LDIMAGE_MB_QWC);
            p += 4;
            setDMAscTag(p, 0, image, 0, LDIMAGE_DMATAG_REF, 0, LDIMAGE_MB_QWC);
            setVIFcodes(p + 2, LDIMAGE_VIF_NOP, LDIMAGE_VIF_DIRECT(LDIMAGE_MB_QWC));
            p += 4;

            image = (char *)image + skip;
        }
    }

    return (int)((char *)p - (char *)LDIMAGE_UNCACHED(tags));
}

// ──────────────────────────────────────────────────────────────────────
// FF3-only: stride one macroblock, as setLoadImageTags. Caller
// playPssSetTexPacket, on the first frame of a movie.
// FF3 0x00273d08

int setLoadTexTags(u_int *tags, sceIpuRGB32 *image, int x, int y,
                   int width, int height, int tbp)
{
    return setLoadTexTagsYX(tags, image, LDIMAGE_MB_BYTES, x, y, width, height, tbp);
}
