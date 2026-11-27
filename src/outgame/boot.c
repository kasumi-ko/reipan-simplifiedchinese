/* ==========================================================================
 *  outgame/boot.c
 *
 *  Phases 3 Boot_Main, 13 Boot_Load, 14 AutoLoad_Main and 4 Logo_Main (see
 *  boot.h). FF3 0x100388..0x100b80, FF3-only as one file. FF3 reads the
 *  GPhaseCtrl singleton through the shared cached pointer (sdata 0x37284c);
 *  nothing depends on the cache, so calls go to GetGPhaseCtrl directly.
 *  FF3 0x00100b60 is not written: the compiler's atexit thunk for the
 *  GPhaseCtrl singleton (GPhaseCtrlTerm), the file-end marker.
 * ======================================================================== */
#include "outgame/boot.h"

#include "common/cmn_fade_layer.h"
#include "graphics/effect/effect_data.h"  // ScreenEffectDataLoadReq / Setup / Free
#include "graphics/graph2d/font.h"      // FontTexLoadReq, FontTexSendVram
#include "graphics/graph2d/message.h"   // MsgInit, LangDataLoadReq
#include "graphics/scene/scene_dat.h"   // SceneVolDataLoadReq / Apply / Free
#include "ingame/map/MarkDat.h"         // MarkDatLoadAll
#include "ingame/menu/anim_2d.h"        // LayoutAnimMain, LangTexLoadReq
#include "ingame/sound/snd_ctrl.h"      // SndSeBankLoad
#include "main/gphase.h"
#include "outgame/autoload.h"
#include "outgame/autoload_scr.h"
#include "system/b2d/B2dFile.h"
#include "reipan/reipan_memory.h"       // PC: ReiPan_GetHostPointer
#include "system/b2d/BpCtl.h"
#include "system/os/system.h"           // GetPALMode
#include "system/pad/pad_ctrl.h"        // CheckPadTrgPlaySe
#include "system/os/eecdvd.h"         // LoadReq / LoadReqGetAddr / IsLoadEnd / IsLoadEndAll
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@002590e0 (before subtitle.c): file 0xf11 on the system heap
void SubTitleDataLoadReq(void);


// ──────────────────────────────────────────────────────────────────────

#define LOGO_BP_FILE 0x122f             // logo layout: labels "T2/TECMO/ZERO in/out"

static char auto_load_step;             // sdata 0x372b04 (gp-0x9ec): one_AutoLoad_Main step

// ──────────────────────────────────────────────────────────────────────
// Phase 3, Boot_Main: a container for 13 and 14.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00100388.
int init_Boot_Main(void)
{
    return 1;
}

// FF3 0x00100390.
int end_Boot_Main(void)
{
    return 1;
}

// FF3 0x00100398.
void pre_Boot_Main(void)
{
}

// FF3 0x001003a0.
void after_Boot_Main(void)
{
}

// Goes straight on to phase 13 (Boot_Load).
// FF3 0x001003a8.
int one_Boot_Main(void)
{
    GetGPhaseCtrl()->SetNext(13, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 13, Boot_Load
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001003e8.
int init_Boot_Load(void)
{
    return 1;
}

// FF3 0x001003f0.
int end_Boot_Load(void)
{
    return 1;
}

// Steps: 0 waits for the system SE bank; 1 requests the boot data (language
// textures, system and language data, files 0x1979/0xac3/0xac4 to fixed EE
// addresses, three heap-loaded data files, the fade layer and MarkDatLoadAll's
// layouts); 2 waits for the loads and the fade layer, applies and frees the
// heap-loaded data, then goes on to phase 14 (AutoLoad_Main).
// FF3 0x001003f8.
int one_Boot_Load(void)
{
    BpCtlMain();

    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        if (SndSeBankLoad(-1) != 0)
            return 0;
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
        LangTexLoadReq();
        FontTexLoadReq();
        LangDataLoadReq();
        LoadReq(0x1979, ReiPan_GetHostPointer(0xfce000));
        ScreenEffectDataLoadReq();
        SceneVolDataLoadReq();
        SubTitleDataLoadReq();
        LoadReq(0xac3, ReiPan_GetHostPointer(0x12ef000));
        LoadReq(0xac4, ReiPan_GetHostPointer(0x12f7000));
        GetCmnFadeLayer()->Load();
        MarkDatLoadAll();
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        return 0;

    case 2:
        if (!IsLoadEndAll())
            return 0;
        if (!CmnFadeLayerIsReady())
            return 0;
        ScreenEffectDataSetup();
        ScreenEffectDataFree();
        SceneVolDataApply();
        SceneVolDataFree();
        MsgInit();
        GetGPhaseCtrl()->SetNext(14, 0);
        return 1;

    default:
        return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Phase 14, AutoLoad_Main
// ──────────────────────────────────────────────────────────────────────

// FF2 called MemoryCardExeInit first; FF3 resets this file's step instead.
// FF3 0x001005b8; FF2 init_AutoLoad_Main 0x0012c088.
int init_AutoLoad_Main(void)
{
    AutoLoadInit();
    auto_load_step = 0;
    return 1;
}

// FF2 freed the auto-load buffer and called MemoryCardEnd here.
// FF3 0x001005e0; FF2 end_AutoLoad_Main 0x0012c0d8.
int end_AutoLoad_Main(void)
{
    AutoLoadMcEnd();
    return 1;
}

// Steps (auto_load_step; several can pass in one frame): 0 opens the screen;
// 1 waits until it is ready; 2 (always passes); 3 runs the screen and the
// auto load until AutoLoadMain finishes, then closes the screen; 4 waits for
// it to close and goes on to phase 4 (Logo_Main). FF2's version only ran
// AutoLoadMain and AutoLoadDispMain.
// FF3 0x00100600; FF2 one_AutoLoad_Main 0x0012c0b0.
int one_AutoLoad_Main(void)
{
    int ret = 0;

    LayoutAnimMain();
    BpCtlMain();
    FontTexSendVram();

    if (auto_load_step == 0)
    {
        AutoLoadScrInit();
        auto_load_step = 1;
    }
    if (auto_load_step == 1 && AutoLoadScrIsReady())
        auto_load_step = 2;
    if (auto_load_step == 2 && AutoLoadScrIsOpen())
        auto_load_step = 3;
    if (auto_load_step == 3)
    {
        AutoLoadScrMain();
        if (AutoLoadMain())
        {
            AutoLoadScrClose();
            auto_load_step = 4;
        }
    }
    if (auto_load_step == 4 && AutoLoadScrIsClosed())
    {
        AutoLoadScrEnd();
        ret = 1;
        GetGPhaseCtrl()->SetNext(4, 0);
    }

    GetCmnFadeLayer()->Update();
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 4, Logo_Main. The leaf node's state_cnt is the logo step and its
// state_wrk the step's frame counter (read as a short).
// ──────────────────────────────────────────────────────────────────────

// Starts loading the logo layout; done once it is loaded (BpCtlIsDataReady
// is also non-zero, -1, when the handle has no entry).
// FF3 0x00100758.
int init_Logo_Main(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt == 0)
    {
        BpCtlLoadReq(LOGO_BP_FILE, nullptr);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    LayoutAnimMain();
    BpCtlMain();
    return BpCtlIsDataReady(BpCtlGetHandleByFileNo(LOGO_BP_FILE));
}

// Next logo step, frame counter back to 0.
// FF3 0x001007e0.
static void LogoStepNext(void)
{
    GetGPhaseCtrl()->leaf->state_wrk = 0;
    GetGPhaseCtrl()->leaf->state_cnt = GetGPhaseCtrl()->leaf->state_cnt + 1;
}

// 1 when pad 0 triggers a button of mask 0xf0ff (no SE) or the step has
// lasted 'frames' frames.
// FF3 0x00100860.
static int LogoIsWaitEnd(short frames)
{
    if (CheckPadTrgPlaySe(0xf0ff, -1, 0))
        return 1;
    if ((short)GetGPhaseCtrl()->leaf->state_wrk < frames)
        return 0;
    return 1;
}

// Steps: even steps start a label, odd steps wait for it to end.
//   0 "T2 in"; 2 wait 75/90 frames or a button, "T2 out";
//   4 wait 25/30 frames, "TECMO in"; 6 wait, "TECMO out";
//   8 wait 25/30 frames, "ZERO in"; 10 wait, "ZERO out"; 12 -> phase 15.
// FF3 0x001008d8.
int one_Logo_Main(void)
{
    // PC: any button skips the rest of the logos. FF3 only cuts a logo's
    // hold short (LogoIsWaitEnd, not START) and still plays every in / out
    // label and gap, about 8 s in all. Checked before BpCtlMain so nothing
    // is drawn from the layout end_Logo_Main is about to release.
    ReiPan_MenuMouseConfirm();          // PC: a click is any button
    if (CheckPadTrgPlaySe(0xffff, -1, 0))
    {
        GetGPhaseCtrl()->SetNext(15, 0);
        return 1;
    }

    short cnt = (short)GetGPhaseCtrl()->leaf->state_wrk;

    GetGPhaseCtrl()->leaf->state_wrk = (short)(cnt + 1);
    BpCtlMain();

    int handle;
    const char *label;

    switch (GetGPhaseCtrl()->leaf->state_cnt)
    {
    case 0:
        handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
        if (B2dFileFindLabel(handle, "T2 in") == nullptr)
        {
            // The Japanese layouts begin with TECMO.
            GetGPhaseCtrl()->leaf->state_cnt = 4;
            label = "TECMO in";
        }
        else
        {
            label = "T2 in";
        }
        break;

    case 1: case 3: case 5: case 7: case 9: case 11:
        if (!BpCtlIsLoadEnd(LOGO_BP_FILE))
            return 0;
        LogoStepNext();
        return 0;

    case 2: case 6: case 10:
        if (!LogoIsWaitEnd(GetPALMode() ? 75 : 90))
            return 0;
        if (GetGPhaseCtrl()->leaf->state_cnt == 2)
        {
            handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
            label = "T2 out";
        }
        else if (GetGPhaseCtrl()->leaf->state_cnt == 6)
        {
            handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
            label = "TECMO out";
        }
        else
        {
            handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
            label = "ZERO out";
        }
        break;

    case 4: case 8:
        if (!GetPALMode())
        {
            if (cnt < 30)
                return 0;
        }
        else if (cnt < 25)
            return 0;
        if (GetGPhaseCtrl()->leaf->state_cnt == 4)
        {
            handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
            label = "TECMO in";
        }
        else
        {
            handle = BpCtlGetHandleByFileNo(LOGO_BP_FILE);
            label = "ZERO in";
        }
        break;

    case 12:
        GetGPhaseCtrl()->SetNext(15, 0);
        return 1;

    default:
        return 0;
    }

    if (BpPlayLabel(handle, label) < 0)
    {
        GetGPhaseCtrl()->SetNext(15, 0);
        return 1;
    }
    LogoStepNext();
    return 0;
}

// FF3 0x00100b38.
int end_Logo_Main(void)
{
    BpCtlRelease(BpCtlGetHandleByFileNo(LOGO_BP_FILE));
    return 1;
}
