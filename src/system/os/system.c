/* ==========================================================================
 *  system/os/system.c
 *
 *  FF3's OS layer: program entry and machine bring-up, the system work block,
 *  the VIF1 packet buffer, the GS display/draw double buffer, V-blank pacing
 *  and the per-frame back end.
 *
 *    * main()          InitMainSystem() once, then game sessions forever:
 *                      GameSystemStart(), GameSystemFrame() each frame until
 *                      the soft-reset combination has been held 60 frames,
 *                      GameSystemEnd().
 *    * Bring-up        InitMainSystem: heaps, packet buffers, IOP (InitSystemON),
 *                      memory card, graphics, V-blank, pads, timers.
 *    * Per frame       SendDMAMain (from after_super): WaitVBlank, frame count,
 *                      SwapDBuff, PacketBufKick.
 *
 *  FF2 had the same pieces in system.c and main.c; FF3 split vfunc into
 *  WaitVBlank + SwapDBuff, moved main() here, made sys_wrk and the packet
 *  buffer C++ singletons and switched to the two-context sceGsDBuffDc.
 *  FF3 0x2597f0..0x25a740 (+ getters 0x2ab428, 0x2abdf8).
 * ======================================================================== */

#include "system.h"

#include <eekernel.h>                   // CreateSema/WaitSema/PollSema/iSignalSema, FlushCache
#include <eeregs.h>                     // REG_GS_*, REG_RCNT*, VIF1/DMAC registers
#include <libdma.h>                     // sceDma*
#include <libgraph.h>                   // sceGs*
#include <libscf.h>                     // sceScfGetLanguage

#include "common/heapctrl.h"            // heapCtrlMalloc / heapCtrlFree
#include "graphics/gs_reg.h"            // InitGsRegCache
#include "graphics/graph3d/gra3d.h"     // gra3dInit
#include "main/main.h"                  // GameSystemStart / Frame / End
#include "system/eeiop/ee_iop.h"        // EEIOP_DEF / ee_iopInit
#include "system/mc/prg/mc.h"           // MemoryCardInit
#include "system/os/sys_heap.h"         // InitSystemHeap / GetIopHeapWrkP
#include "system/pad/pad.h"             // pad_wrk / PadInitAll

#include "reipan/io/reipan_log.h"       // PC: ReiPan_LogInit
#include "reipan/os/reipan_host.h"      // PC: ReiPan_HostMain
#include "reipan/os/reipan_pacing.h"    // PC: ReiPan_ResolveVBlankWait
#include "reipan/reipan_profiler.h"     // PC: ReiPan_ProfilerRecordVblankPacing
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "reipan/io/reipan_paths.h"           // PC: ReiPan_PathLogDataRoot
#include "reipan/reipan_ui/reipan_rml_gamefiles.h" // PC: ReiPan_RmlGameFilesTick
#include "reipan/rendering/reipan_renderer.h" // PC: ReiPan_RendererEndFrame

#include <SDL3/SDL_main.h>              // Windows GUI and console entry points

// ──────────────────────────────────────────────────────────────────────
// Statics / globals

sceGsDBuffDc   gdb;                     // data  0x333150
sceGsDrawEnv1 *pdrawenv1;               // sdata 0x372608
sceGsDrawEnv1 *pdrawenv1_other;         // sdata 0x37260c
sceGsDrawEnv2 *pdrawenv2;               // sdata 0x372610
sceGsDrawEnv2 *pdrawenv2_other;         // sdata 0x372614

// PCRTC DISPLAY1/DISPLAY2 per video mode (sdata 0x372618..0x372647).
static u_long display_ntsc[2] = { SCE_GS_SET_DISPLAY(636, 50, 3, 0, 2559, 447),
                                  SCE_GS_SET_DISPLAY(636, 50, 3, 0, 2559, 446) };
static u_long display_pal[2]  = { SCE_GS_SET_DISPLAY(656, 104, 3, 0, 2559, 447),
                                  SCE_GS_SET_DISPLAY(656, 104, 3, 0, 2559, 446) };
static u_long display_480p[2] = { SCE_GS_SET_DISPLAY(310, 57, 1, 0, 1279, 447),
                                  SCE_GS_SET_DISPLAY(310, 57, 1, 0, 1279, 446) };

static float frame_time_scale_60 = 1.0f;    // sdata 0x372648
static float frame_time_scale_50 = 1.2f;    // sdata 0x37264c

static int SYSTEM_VBLANK_WAIT_NUM = 2;      // sdata 0x372658: V-blanks per frame
static int vblank_sema;                     // sbss  0x373994
static sceDmaChan *dma_vif1;                // sbss  0x373980: channel 1, cached by PacketBufKick

static int v_callback(int i);

// ──────────────────────────────────────────────────────────────────────
// VIF1 packet buffer
// ──────────────────────────────────────────────────────────────────────

// Clear a stalled or faulted VIF1: when any of VSS/VFS/VIS/INT/ER0/ER1 is set,
// suspend the DMAC, drop channel 1's STR bit and reset VIF1. The manager is
// passed but not used. On the PC nothing sets VIF1_STAT, so this never fires.
// FF3 0x002597f0; FF2 dmaVif1CheckDMA's place in SendDMAMain.

void PACKET_BUF_MGR::CheckVif1Stall()
{
    (void)this;

    if ((REG_VIF1_STAT & 0x3f00) != 0)
    {
        REG_DMAC_ENABLEW = 0x10000;                         // suspend all channels
        REG_DMAC_1_VIF1_CHCR = REG_DMAC_1_VIF1_CHCR & ~0x100u;  // STR off
        REG_DMAC_ENABLEW = 0;
        sceDevVif1Reset();
    }
}

// ──────────────────────────────────────────────────────────────────────
// Send the half being filled: terminate it with a DMA END tag and start a
// tag-transfer chain on VIF1 from its top, then flip halves and rewind the new
// one. An empty half is not sent but the halves still flip.
// FF3 0x00259820; called by g3dDmaFlush and SendDMAMain (FF2 dmaVif1Kick).

void PACKET_BUF_MGR::Kick()
{
    int    side;
    u_int *tag;

    side = this->side;
    if (top[side] < cur[side])
    {
        dma_vif1 = sceDmaGetChan(1);

        tag = (u_int *)ReiPan_GetHostPointer(cur[side]);
        tag[0] = 0x70000000;                    // DMAtag END, qwc 0
        tag[3] = 0;
        tag[1] = 0;
        tag[2] = 0;

        CheckVif1Stall();
        sceDmaSync(dma_vif1, 0, 0);
        dma_vif1->chcr.TTE = 1;                 // transfer the tags' upper halves (VIF codes)
        FlushCache(0);
        sceDmaSync(dma_vif1, 0, 0);
        // PC: the chain keeps its EE address, as the DMAC would see it; the
        // platform's sceDmaSend records it without walking it.
        sceDmaSend(dma_vif1, (void *)(uintptr_t)top[this->side]);

        side = this->side;
    }

    this->side = (char)(1 - side);
    cur[this->side] = top[this->side] & 0x0fffffff;
}

// ──────────────────────────────────────────────────────────────────────
// Address of the next packet's data: one qword past the write pointer, which
// is left for the tag PacketBufSetPacket writes when the packet is closed.
// FF3 0x00259930; FF2 dmaVif1GetPacketFLUSH_DIRECT's role.

u_int PACKET_BUF_MGR::GetPacket()
{
    return cur[side] + 0x10;
}

// ──────────────────────────────────────────────────────────────────────
// Close the open packet at `end` (EE address): a DMA CNT tag covering the data,
// with VIF FLUSH in the tag's third word when `flush` and DIRECT (to the GIF
// over PATH2) for the same qword count when `direct`; then advance to `end`.
// FF3 0x00259948; FF2 dmaVif1SetPacketFLUSH_DIRECT's role.

void PACKET_BUF_MGR::SetPacket(u_int end, int direct, int flush)
{
    u_int  vif_flush;
    u_int *tag;
    u_int  qwc;

    vif_flush = 0x11000000;                     // VIF FLUSH
    if (flush == 0)
    {
        vif_flush = 0;                          // VIF NOP
    }

    tag = (u_int *)ReiPan_GetHostPointer(cur[side]);
    tag[2] = vif_flush;
    tag[1] = 0;
    qwc = ((end - cur[side]) >> 4) - 1;
    tag[0] = qwc | 0x10000000;                  // DMAtag CNT
    if (direct == 0)
    {
        qwc = 0;
    }
    else
    {
        qwc = qwc | 0x50000000;                 // VIF DIRECT qwc
    }
    tag[3] = qwc;

    cur[side] = end & 0x0fffffff;
}

// ──────────────────────────────────────────────────────────────────────
// Append a DMA REF tag pointing at `qwc` qwords at `addr` (the SPR bit is set
// for scratchpad addresses), with optional VIF FLUSH / DIRECT as above.
// FF3 0x002599b8; callers g3dDmaAddPacket, ShadowMeshDataVU (FF2 dmaVif1AddRefTagVIF).

void PACKET_BUF_MGR::AddRefTag(u_int addr, int qwc, int direct, int flush)
{
    u_int *tag;
    u_int  vif_flush;

    tag = (u_int *)ReiPan_GetHostPointer(cur[side]);
    vif_flush = 0x11000000;
    if (flush == 0)
    {
        vif_flush = 0;
    }

    tag[0] = qwc | 0x30000000;                  // DMAtag REF
    tag[1] = (addr & 0x0fffffff) | ((u_int)((addr & 0x70000000) == 0x70000000) << 31);
    tag[2] = vif_flush;
    if (direct == 0)
    {
        qwc = 0;
    }
    else
    {
        qwc = qwc | 0x50000000;
    }
    tag[3] = qwc;

    cur[side] = (cur[side] + 0x10) & 0x0fffffff;
}

// ──────────────────────────────────────────────────────────────────────
// Graphics
// ──────────────────────────────────────────────────────────────────────

// Bring up the DMA/VIF/VU/GS path in progressive mode, prime both halves of the double
// buffer, then the GS register cache and the 3D renderer's packet area.
// FF3 0x00259a48; FF2 InitGraphics plus the PAL default, InitGsRegCache and
// gra3dInit (FF2 called those from init_super).

void InitGraphics(void)
{
    SYS_WRK  *sys;
    sceDmaEnv env;

    sys = GetSysWrk();
    sceDmaReset(1);
    sceGsResetPath();
    sceDevVif0Reset();
    sceDevVu0Reset();

    sys->video_mode = VIDEO_MODE_DTV480P;
    SetGsResetGraph(sys->video_mode, 0);

    sceDmaGetEnv(&env);
    env.notify = 4;
    sceDmaPutEnv(&env);

    SetFrameBuffer();
    SetDisplayPos(sys->video_mode);
    FlushCache(0);
    sceGsSyncV(0);

    sceGsPutDrawEnv(&gdb.giftag0);
    sceGsSyncPath(0, 0);
    sceGsPutDrawEnv(&gdb.giftag1);
    sceGsSyncPath(0, 0);

    InitGsRegCache();
    // PC: 0x8e0000 is an EE address (0x10000 bytes after the IOP heap).
    gra3dInit(ReiPan_GetHostPointer(0x8e0000), 0x10000);
}

// ──────────────────────────────────────────────────────────────────────
// Default 640x448 double buffer for both contexts, then the frame/Z base
// pointers: display 0 shows FBP 0 and display 1 FBP 140; buffer 0 draws into
// 140, buffer 1 into 0; Z at 280. Clear colours black.
// FF3 0x00259b38; FF2 SetFrameBuffer (one context, both draws into 140).

void SetFrameBuffer(void)
{
    // PSMCT32, ztest 2 (GEQUAL), zpsm 10 (PSMZ16S), clear on.
    sceGsSetDefDBuffDc(&gdb, SCE_GS_PSMCT32, 640, 448, 2, 10, 1);

    gdb.disp[0].dispfb.FBP = 0;
    gdb.draw11.frame1.FBP  = 0;
    gdb.disp[1].dispfb.FBP = 140;
    gdb.draw12.frame2.FBP  = 0;
    gdb.draw01.frame1.FBP  = 140;
    gdb.draw01.zbuf1.ZBP   = 280;
    gdb.draw11.zbuf1.ZBP   = 280;
    gdb.draw02.frame2.FBP  = 140;
    gdb.draw02.zbuf2.ZBP   = 280;
    gdb.draw12.zbuf2.ZBP   = 280;

    gdb.clear1.rgbaq.B = 0;
    gdb.clear0.rgbaq.R = 0;
    gdb.clear0.rgbaq.G = 0;
    gdb.clear0.rgbaq.B = 0;
    gdb.clear1.rgbaq.R = 0;
    gdb.clear1.rgbaq.G = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Position the displayed image (DX/DY) for a video mode.
// FF3 0x00259c30; FF2 SetDisplayPos (same PAL/NTSC values, mode now passed in,
// 480p added).

void SetDisplayPos(int video_mode)
{
    int dx;
    int dy;

    switch (video_mode)
    {
    case VIDEO_MODE_PAL:
        dx = 656;
        dy = 104;
        break;
    case VIDEO_MODE_NTSC:
        dx = 636;
        dy = 50;
        break;
    case VIDEO_MODE_DTV480P:
        dx = 310;
        dy = 57;
        break;
    default:
        return;
    }

    gdb.disp[1].display.DX = dx;
    gdb.disp[1].display.DY = dy;
    gdb.disp[0].display.DX = dx;
    gdb.disp[0].display.DY = dy;
}

// ──────────────────────────────────────────────────────────────────────
// Reset the GS for a video mode: interlaced except 480p, field mode.
// FF3 0x00259d30; FF2 SetGsResetGraph (mode passed in; FF2 used frame mode).

void SetGsResetGraph(int video_mode, int n)
{
    int omode;

    omode = video_mode;
    if (video_mode == VIDEO_MODE_DTV480P)
    {
        omode = VIDEO_MODE_DTV480P;
    }

    // inter: 1 interlaced / 0 progressive; ffmode 0 = field.
    sceGsResetGraph(n, video_mode != VIDEO_MODE_DTV480P, omode, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Program the PCRTC directly for a frame: both read circuits from FBP 0 or 140
// by frame parity (DISPFB 0x9400: FBW 10, PSMCT24; circuit 2 one line down).
// FF3 0x00259d78; FF2 SetPCRTC (mode passed in, 480p added).

void SetPCRTC(u_int frame, int video_mode)
{
    u_long fbp;

    REG_GS_PMODE = 0x8023;

    switch (video_mode)
    {
    case VIDEO_MODE_PAL:
        REG_GS_SMODE2   = 1;
        fbp = (u_long)(int)((frame & 1) * 140);
        REG_GS_DISPLAY1 = display_pal[0];
        REG_GS_DISPFB1  = fbp | 0x9400;
        REG_GS_DISPLAY2 = display_pal[1];
        REG_GS_DISPFB2  = fbp | 0x80000009400;
        return;
    case VIDEO_MODE_NTSC:
        fbp = (u_long)(int)((frame & 1) * 140);
        REG_GS_SMODE2   = 1;
        REG_GS_DISPLAY1 = display_ntsc[0];
        REG_GS_DISPFB1  = fbp | 0x9400;
        REG_GS_DISPLAY2 = display_ntsc[1];
        break;
    case VIDEO_MODE_DTV480P:
        fbp = (u_long)(int)((frame & 1) * 140);
        REG_GS_SMODE2   = 0;
        REG_GS_DISPLAY1 = display_480p[0];
        REG_GS_DISPFB1  = fbp | 0x9400;
        REG_GS_DISPLAY2 = display_480p[1];
        break;
    default:
        return;
    }

    REG_GS_DISPFB2 = fbp | 0x80000009400;
}

// ──────────────────────────────────────────────────────────────────────
// Swap the double buffer for the new frame count, re-arm the PCRTC and point
// the draw-env pointers at this frame's buffer (odd frames draw buffer 0).
// FF3 0x00259f18; the tail of FF2 vfunc (FF2 kept one pointer, pdrawenv).

void SwapDBuff(void)
{
    SYS_WRK *sys;

    sys = GetSysWrk();
    sceGsSwapDBuffDc(&gdb, (int)sys->count);
    SetPCRTC(sys->count, (char)sys->video_mode);

    pdrawenv1 = &gdb.draw11;
    if ((sys->count & 1) != 0)
    {
        pdrawenv1 = &gdb.draw01;
    }
    pdrawenv2       = &gdb.draw02;
    pdrawenv1_other = &gdb.draw11;
    if ((sys->count & 1) == 0)
    {
        pdrawenv1_other = &gdb.draw01;
    }
    if ((sys->count & 1) == 0)
    {
        pdrawenv2 = &gdb.draw12;
    }
    if ((sys->count & 1) == 0)
    {
        pdrawenv2_other = &gdb.draw02;
    }
    else
    {
        pdrawenv2_other = &gdb.draw12;
    }
}

// ──────────────────────────────────────────────────────────────────────
// True in PAL mode.
// FF3 0x0025a008; FF2 GetPALMode.

int GetPALMode(void)
{
    return GetSysWrk()->video_mode == VIDEO_MODE_PAL;
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame step scale for the video mode: 1.2 at 50 Hz so motion and timers
// written for 60 Hz keep their speed, 1.0 otherwise. 57 call sites.
// FF3 0x0025a040; no FF2 counterpart.

float GetFrameTimeScale(void)
{
    switch ((char)GetSysWrk()->video_mode)
    {
    case VIDEO_MODE_PAL:
        return frame_time_scale_50;
    case VIDEO_MODE_NTSC:
    case VIDEO_MODE_DTV480P:
    default:
        return frame_time_scale_60;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Switch video mode: let ten V-blanks pass, store the mode, reset the DMA/VIF0/
// VU0/GS path for it, reinstall the V-blank sema and callback, ten more.
// FF3 0x0025a0b8; FF2 ChangeVideoMode (no range check, no CRTC writes here).

void ChangeVideoMode(u_char mode)
{
    SYS_WRK  *sys;
    sceDmaEnv env;
    int       i;

    sys = GetSysWrk();
    if ((char)sys->video_mode != (char)mode)
    {
        for (i = 9; i >= 0; i--)
        {
            WaitVBlank();
        }

        sys->video_mode = mode;
        sceDmaReset(1);
        sceDevVif0Reset();
        sceDevVu0Reset();
        SetGsResetGraph(mode, 0);

        sceDmaGetEnv(&env);
        env.notify = 4;
        sceDmaPutEnv(&env);

        SetDisplayPos(mode);
        InitVBlank();     // a new sema each time; the old one is never deleted

        for (i = 9; i >= 0; i--)
        {
            WaitVBlank();
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Bring-up
// ──────────────────────────────────────────────────────────────────────

// EEIOP_DEF allocator callbacks for ee_iopInit: the IOP layer's own heap.
// FF3 0x0025a190; FF2 systemGetMem (FF2 used the system heap).

void *systemGetMem(int size)
{
    return heapCtrlMalloc(GetIopHeapWrkP(), size, 0, 0, 0);
}

// FF3 0x0025a1c8; FF2 systemFreeMem.

void systemFreeMem(void *adrs)
{
    heapCtrlFree(GetIopHeapWrkP(), adrs, 0, 0, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Boot the IOP side: FF3's IRX set (IOPRP300, ZEROIRX), the file header table
// ZERO3.FHD and the data image IMG_BD.BIN, all through one EEIOP_DEF.
// FF3 0x0025a200; FF2 InitSystemON did all of the bring-up, FF3's does only
// this (InitMainSystem has the rest).

void InitSystemON(void)
{
    EEIOP_DEF def;

    def.malloc64                 = systemGetMem;
    def.command_send_buffer_size = 0;
    def.iop_def_module           = "cdrom0:\\IOPRP300.IMG;1";
    def.free64                   = systemFreeMem;
    def.dil_file_name            = "cdrom0:\\ZEROIRX.DIL;1";
    def.rom_boot                 = 1;
    def.hil_file_name            = "cdrom0:\\ZEROIRX.HIL;1";
    def.file_load_wrk_num        = 50;
    def.snd_bank_load_priority   = 5;
    def.media                    = 2;
    def.stream_auto_wrk_num      = 10;
    def.spu_mem_size             = 0x1faff0;
    def.snd_bank_wrk_num         = 40;
    def.auto_bd_wrk_num          = 5;
    def.fhd_file_name            = "cdrom0:\\ZERO3.FHD;1";
    def.spu_mem_start            = 0x5010;
    def.snd_stream_load_priority = 1;
    def.cmp_use_flg              = 1;
    def.pc_path                  = "host0:";       // sdata 0x372650
    def.cd_dat_name              = "IMG_BD.BIN";
    ee_iopInit(&def);
}

// ──────────────────────────────────────────────────────────────────────
// sys_wrk
// ──────────────────────────────────────────────────────────────────────

// SYS_WRK's constructor: frame count 0, NTSC, English (language id 2), soft
// reset idle and enabled. InitGraphics switches to progressive straight after.
// FF3 0x0025a2f0; FF2 InitSysWrk (no RTC read any more, default language 2).

SYS_WRK::SYS_WRK()
{
    count        = 0;
    video_mode   = VIDEO_MODE_NTSC;
    language     = 2;
    sreset_count = 0;
    sreset_ng    = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Count the frames pad 0 has held L1+L2+R1+R2+START+SELECT; any release resets
// the count. With sreset_ng set the count restarts every frame, so it can never
// reach IsSoftResetCountOver's limit. main() polls this once a frame.
// FF3 0x0025a310; FF2 CheckSoftReset also fired the reset itself (CallSoftReset).

void SYS_WRK::CheckSoftReset()
{
    u_short held;

    if (sreset_ng != 0)
    {
        sreset_count = 0;
    }

    held = pad_wrk[0].held;
    if ((held & 0x0004) != 0 &&             // L1
        (held & 0x0001) != 0 &&             // L2
        (held & 0x0008) != 0 &&             // R1
        (held & 0x0002) != 0 &&             // R2
        (held & 0x0800) != 0 &&             // START
        (held & 0x0100) != 0)               // SELECT
    {
        sreset_count++;
        return;
    }

    sreset_count = 0;
}

// ──────────────────────────────────────────────────────────────────────
// True once the combination has been held for 60 frames.
// FF3 0x0025a378.

int SYS_WRK::IsSoftResetCountOver()
{
    return sreset_count > 59;
}

// ──────────────────────────────────────────────────────────────────────
// The game's language (file offset 0..4: English, French, German, Spanish,
// Italian) for the console language id kept in sys_wrk (GetSystemLanguage's
// 2..6; ids 0 and 1 also give English).
// FF3 0x0025a388; FF2 kept the game language itself in sys_wrk.

int SYS_WRK::GetLanguage()
{
    int tbl[7] = { 0, 0, 0, 1, 2, 3, 4 };

    return tbl[language];
}

// ──────────────────────────────────────────────────────────────────────
// Program entry: bring the machine up once, then run game sessions forever.
// A session ends when the soft-reset combination has been held 60 frames;
// GameSystemEnd tears it down and the next GameSystemStart begins anew.
// FF3 0x0025a3e8 (main); FF2 main ran GPhaseSysMain() forever and reset by
// phase. PC: this is the game thread's body; the process's main() below keeps
// the OS main thread for the window (reipan_host.h).

static int GameMain(void)
{
    SYS_WRK *sys;

    // PC: nothing of the game has run yet, and nothing does until it has its
    // files: a player without them is asked first.
    while (ReiPan_RmlGameFilesTick() != 0)
    {
    }
    ReiPan_PathLogDataRoot();

    InitMainSystem();
    for (;;)
    {
        GameSystemStart();
        do
        {
            GameSystemFrame();
            sys = GetSysWrk();
            sys->CheckSoftReset();
        } while (sys->IsSoftResetCountOver() == 0);

        sys->sreset_count = 0;
        GameSystemEnd();
    }
}

// PC: the process entry. The log opens before anything can report into it,
// then the game runs on its own thread while this one pumps window events, so
// moving or resizing the window no longer stops the game.

int main(int, char **)
{
    ReiPan_LogInit();
    return ReiPan_HostMain(GameMain);
}

// ──────────────────────────────────────────────────────────────────────
// One-time machine bring-up: heaps, the two 2 MB packet-buffer halves
// (0x440000, 0x640000), IOP, memory card, graphics, V-blank, pads, then seed
// rand from timer 0 and start timers 0 (H-blank) and 1 (BUSCLK/256).
// FF3 0x0025a450; FF2 had this in InitSystemON/init_super.
// callcheck: srand (PC: ReiPan_SRand seeds the platform's stream, the ROM's rand)

void InitMainSystem(void)
{
    PACKET_BUF_MGR *mgr;

    InitSystemHeap();

    mgr = GetPacketBufMgr();
    mgr->side   = 0;
    mgr->top[0] = 0x440000;
    mgr->top[1] = 0x640000;
    mgr->size   = 0x200000;
    mgr->cur[0] = 0x440000;
    mgr->cur[1] = 0x640000;

    InitSystemON();
    MemoryCardInit();
    InitGraphics();
    InitVBlank();
    PadInitAll();

    // PC: newlib's srand; the platform's stream is the ROM's rand().
    ReiPan_SRand(REG_RCNT0_COUNT);
    REG_RCNT0_MODE  = 0xc93;    // H-blank clock, count up, clear flags
    REG_RCNT1_MODE  = 0xc92;    // BUSCLK/256
    REG_RCNT0_COUNT = 0;
    REG_RCNT1_COUNT = 0;
}

// ──────────────────────────────────────────────────────────────────────
// V-blank
// ──────────────────────────────────────────────────────────────────────

// Two V-blanks per frame by default; a counting sema the V-blank callback
// signals, then the callback itself.
// FF3 0x0025a510; FF2 InitVBlank (FF2's max count was the wait number).

void InitVBlank(void)
{
    struct SemaParam semap;

    SetVBlankWaitNum(2);
    semap.initCount = 0;
    semap.maxCount  = 0x7fffffff;
    vblank_sema = CreateSema(&semap);
    SystemSetVCallback();
}

// ──────────────────────────────────────────────────────────────────────
// V-blank interrupt handler: count the V-blank.
// FF3 0x0025a550; FF2 v_callback.

static int v_callback(int i)
{
    (void)i;
    iSignalSema(vblank_sema);
    SYNC(0);
    EI();

    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0025a580; FF2 SystemSetVCallback.

void SystemSetVCallback(void)
{
    sceGsSyncVCallback(v_callback);
}

// ──────────────────────────────────────────────────────────────────────
// Frame pacing: wait for GS path idle, take the V-blanks that already passed,
// then block until SYSTEM_VBLANK_WAIT_NUM have passed since the last frame
// (at least one); restart both timers.
// FF3 0x0025a5a0; the head of FF2 vfunc.

void WaitVBlank(void)
{
    int count;
    int remain;
    int target;
    int waits;
    uint64_t wait_start;

    count = 0;
    sceGsSyncPath(0, 0);
    while (PollSema(vblank_sema) != -1)
    {
        count++;
    }

    // PC: the platform's pacing override (a profiler harness) may replace the
    // game's wait count; it returns the game's own value when off.
    target = ReiPan_ResolveVBlankWait(SYSTEM_VBLANK_WAIT_NUM);
    remain = (target - count) - 1;

    // PC: time the wait for the profiler, so its Game CPU figure is the
    // frame's work and not the work plus the fields spent idle here.  Without
    // this a frame that overran by a little and then waited out the next
    // field read as a frame that took that long to compute.
    wait_start = ReiPan_ProfilerNow();
    waits = 1;
    if (remain > 0)
    {
        waits += remain;
        do
        {
            remain--;
            WaitSema(vblank_sema);
        } while (remain != 0);
    }
    WaitSema(vblank_sema);
    ReiPan_ProfilerRecordVblankPacing(ReiPan_ProfilerNow() - wait_start,
                                      count, waits, target);

    REG_RCNT0_COUNT = 0;
    REG_RCNT1_COUNT = 0;
}

// ──────────────────────────────────────────────────────────────────────
// The per-frame back end, run by after_super: recover VIF1, wait out the
// frame's V-blanks, count the frame, swap buffers, send the frame's packets.
// FF3 0x0025a638; FF2 SendDMAMain (vfunc split into WaitVBlank + SwapDBuff).

void SendDMAMain(void)
{
    PACKET_BUF_MGR *mgr;
    SYS_WRK        *sys;

    mgr = GetPacketBufMgr();
    sys = GetSysWrk();
    mgr->CheckVif1Stall();
    WaitVBlank();
    sys->count++;
    SwapDBuff();
    mgr->Kick();

    // PC: present the frame (main.c's pre_super begins it).
    ReiPan_RendererEndFrame();
}

// ──────────────────────────────────────────────────────────────────────
// Language and pacing settings
// ──────────────────────────────────────────────────────────────────────

// The console's language as a sys_wrk language id: Japanese/English 2,
// French 3, Spanish 5, German 4, Italian 6, anything else 2.
// FF3 0x0025a6b8; FF2 GetSystemLanguage returned the game language directly.

int GetSystemLanguage(void)
{
    int lang;

    lang = 2;
    switch (sceScfGetLanguage())
    {
    case SCE_JAPANESE_LANGUAGE:
    case SCE_ENGLISH_LANGUAGE:
        lang = 2;
        break;
    case SCE_FRENCH_LANGUAGE:
        lang = 3;
        break;
    case SCE_SPANISH_LANGUAGE:
        lang = 5;
        break;
    case SCE_GERMAN_LANGUAGE:
        lang = 4;
        break;
    case SCE_ITALIAN_LANGUAGE:
        lang = 6;
        break;
    }

    return lang;
}

// ──────────────────────────────────────────────────────────────────────
// V-blanks per frame for WaitVBlank (2 = 30 fps at 60 Hz; movies use 1).
// FF3 0x0025a728; FF2 SetVBlankWaitNum (FF2 also reset the renderer's interval).

void SetVBlankWaitNum(int num)
{
    SYSTEM_VBLANK_WAIT_NUM = num;
}

// ──────────────────────────────────────────────────────────────────────
// Singletons (FF3 emits these getters with the template instances at 0x2ab000+)
// ──────────────────────────────────────────────────────────────────────

// FF3 0x002ab428: the packet-buffer manager, zeroed on first use;
// InitMainSystem lays out the halves. The compiler emits the first-use guard
// and the atexit registration of the destructor that FF3's getter spells out.
// callcheck: atexit (the static's destructor, registered by the compiler)

PACKET_BUF_MGR *GetPacketBufMgr(void)
{
    static PACKET_BUF_MGR packet_buf_mgr;   // bss 0x41e478

    return &packet_buf_mgr;
}

// FF3 0x002abdf8: the system work block, built by its constructor on first use
// (SYS_WRK's constructor; the guard and atexit come from the compiler).
// callcheck: InitSysWrk, atexit (the static's constructor and destructor, implicit)

SYS_WRK *GetSysWrk(void)
{
    static SYS_WRK sys_wrk;                 // bss 0x41e4b0

    return &sys_wrk;
}
