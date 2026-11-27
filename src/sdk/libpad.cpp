/* ==========================================================================
 *  sdk/libpad.cpp  (SCE PS2 EE <libpad> -- PC-port shim)
 *
 *  Translates the DualShock2 report format expected by the game into queries
 *  against the reipan input wrapper (reipan/io/reipan_input), which owns the
 *  host devices (SDL gamepad + keyboard).  This file keeps only the PS2 report
 *  packing (button bit layout, rdata byte offsets); all SDL/device handling
 *  lives in the wrapper.  system/pad/pad.c owns the game-side counters/key maps.
 * ======================================================================== */

#include "libpad.h"

#include "../main/gphase.h"
#include "../reipan/io/reipan_input.h"
#include "../reipan/io/reipan_menu_mouse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define REIPAN_PAD_LOGICAL_COUNT REIPAN_PAD_BUTTON_COUNT
#define REIPAN_STICK_COUNT       REIPAN_PAD_STICK_COUNT

// FF2-internal pad bit per logical index (matches reipan_input's index order
// after PadReadFunc byte-swaps rdata[2]/rdata[3]).
static const u_short s_button_bits[REIPAN_PAD_LOGICAL_COUNT] = {
    0x1000, 0x4000, 0x8000, 0x2000,
    0x0010, 0x0040, 0x0080, 0x0020,
    0x0004, 0x0001, 0x0008, 0x0002,
    0x0800, 0x0100, 0x0200, 0x0400,
};

// rdata byte each stick axis is written to (left X/Y, right X/Y).
static const int s_stick_rdata_byte[REIPAN_STICK_COUNT] = { 6, 7, 4, 5 };

/* --------------------------------------------------------------------------
 *  Scripted input for smoke runs (tools/reverse/build.py --pad).
 *
 *  REIPAN_AUTOPAD="<read>:<BUTTON>[*<reads>][,...]" makes pad 0 ignore
 *  the host devices and hold BUTTON for AUTOPAD_HOLD reads (or <reads>) from
 *  the <read>-th scePadRead on (the game reads once per frame).  Buttons are
 *  named in the logical order above: UP DOWN LEFT RIGHT TRIANGLE CROSS SQUARE
 *  CIRCLE L1 L2 R1 R2 START SELECT L3 R3; LSUP LSDOWN LSLEFT LSRIGHT push the
 *  left stick fully instead (walking).  Off unless the variable is set; with
 *  it set, sticks rest unless scripted and nothing plugged into the host
 *  reaches the game.
 * ------------------------------------------------------------------------ */
#define AUTOPAD_HOLD 3
#define AUTOPAD_MAX  64

static const char *const s_autopad_names[REIPAN_PAD_LOGICAL_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "TRIANGLE", "CROSS", "SQUARE", "CIRCLE",
    "L1", "L2", "R1", "R2", "START", "SELECT", "L3", "R3",
};

// Left-stick pushes, after the logical buttons: up, down, left, right.
static const char *const s_autopad_stick_names[4] = { "LSUP", "LSDOWN", "LSLEFT", "LSRIGHT" };

struct AutoPadPress
{
    unsigned read;
    unsigned hold;
    int      button;                // < REIPAN_PAD_LOGICAL_COUNT: button, else stick push
    int      phase;
    unsigned visit;
};

static AutoPadPress s_autopad[AUTOPAD_MAX];
static int          s_autopad_count;
static int          s_autopad_on = -1;      // -1: not read yet, 0: off, 1: scripted
static unsigned     s_autopad_reads;
static bool         s_autopad_has_phase;
static int          s_autopad_phase = -1;
static unsigned     s_autopad_phase_reads;
static unsigned     s_autopad_visits[GPHASE_NUM];

static int AutoPadActive(void)
{
    if (s_autopad_on >= 0)
    {
        return s_autopad_on;
    }

    s_autopad_on = 0;
    const char *env = std::getenv("REIPAN_AUTOPAD");
    if (env == nullptr)
    {
        return 0;
    }
    s_autopad_on = 1;               // set but empty still masks the host pad

    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", env);
    for (char *tok = std::strtok(buf, ", "); tok != nullptr && s_autopad_count < AUTOPAD_MAX;
         tok = std::strtok(nullptr, ", "))
    {
        char *colon = std::strchr(tok, ':');
        if (colon == nullptr)
        {
            continue;
        }
        *colon = '\0';
        unsigned phase, visit, read;
        int phase_id = -1;
        char trailing;
        // Optionally count reads from an initialized phase's Nth visit.
        if (std::sscanf(tok, "%u/%u/%u%c", &phase, &visit, &read, &trailing) == 3)
        {
            if (phase >= GPHASE_NUM || visit == 0 || read == 0)
                continue;
            phase_id = (int)phase;
            s_autopad_has_phase = true;
        }
        else
        {
            read = (unsigned)std::strtoul(tok, nullptr, 10);
            visit = 0;
        }
        unsigned hold = AUTOPAD_HOLD;
        char *star = std::strchr(colon + 1, '*');
        if (star != nullptr)
        {
            *star = '\0';
            hold = (unsigned)std::strtoul(star + 1, nullptr, 10);
        }
        for (int i = 0; i < REIPAN_PAD_LOGICAL_COUNT + 4; i++)
        {
            const char *name = i < REIPAN_PAD_LOGICAL_COUNT ? s_autopad_names[i]
                                                            : s_autopad_stick_names[i - REIPAN_PAD_LOGICAL_COUNT];
            if (std::strcmp(colon + 1, name) == 0)
            {
                s_autopad[s_autopad_count] = {read, hold, i, phase_id, visit};
                s_autopad_count++;
                break;
            }
        }
    }
    std::fprintf(stderr, "[libpad] REIPAN_AUTOPAD: %d scripted presses\n", s_autopad_count);
    return 1;
}

static int AutoPadHeld(int button)
{
    for (int i = 0; i < s_autopad_count; i++)
    {
        const AutoPadPress &press = s_autopad[i];
        if (press.button != button)
            continue;
        unsigned read = s_autopad_reads;
        if (press.phase >= 0)
        {
            if (s_autopad_phase != press.phase || s_autopad_visits[press.phase] != press.visit)
                continue;
            read = s_autopad_phase_reads;
        }
        if (read >= press.read && read - press.read < press.hold)
        {
            return 1;
        }
    }
    return 0;
}

static void AutoPadAdvance()
{
    s_autopad_reads++;
    if (!s_autopad_has_phase)
        return;
    const GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;
    const int phase = leaf != nullptr && leaf->state == GPHASE_STATE_RUN ? leaf->id : -1;
    if (phase != s_autopad_phase)
    {
        s_autopad_phase = phase;
        s_autopad_phase_reads = 0;
        if (phase >= 0 && phase < GPHASE_NUM)
            s_autopad_visits[phase]++;
    }
    s_autopad_phase_reads++;
}

static void PackPressureBytes(u_char *report, u_short buttons, bool scripted)
{
    // DualShock 2 pressure order differs from the logical button order.
    static const int logical[12] = {3, 2, 0, 1, 4, 7, 5, 6, 8, 10, 9, 11};
    for (int i = 0; i < 12; ++i)
    {
        const int button = logical[i];
        report[8 + i] = (buttons & s_button_bits[button]) != 0 ? 0
            : (scripted ? 255 : ReiPan_InputButtonPressure(button));
    }
}

extern "C" {

int scePadInit(int mode)
{
    (void)mode;
    return ReiPan_InputInit();
}

int scePadEnd(void)
{
    ReiPan_InputShutdown();
    return 1;
}

int scePadPortOpen(int port, int slot, void *addr)
{
    (void)slot;
    (void)addr;

    if (port != 0)
    {
        return 0;
    }

    ReiPan_InputUpdate();
    return 1;
}

int scePadPortClose(int port, int slot)
{
    (void)slot;

    if (port == 0)
    {
        ReiPan_InputShutdown();
    }

    return 1;
}

int scePadGetState(int port, int slot)
{
    (void)slot;

    if (port != 0)
    {
        return scePadStateDiscon;
    }

    ReiPan_InputInit();
    return scePadStateStable;
}

int scePadRead(int port, int slot, unsigned char *rdata)
{
    u_short raw_buttons;
    int i;

    (void)slot;

    if (port != 0 || rdata == 0 || ReiPan_InputInit() == 0)
    {
        return 0;
    }

    ReiPan_InputUpdate();

    for (i = 1; i < 32; i++)
    {
        rdata[i] = 0xff;
    }

    rdata[0] = 0;
    rdata[1] = 0x79;
    raw_buttons = 0xffff;

    if (AutoPadActive())
    {
        AutoPadAdvance();
        for (i = 0; i < REIPAN_PAD_LOGICAL_COUNT; i++)
        {
            if (AutoPadHeld(i) || ReiPan_MenuMouseHeld(i))
            {
                raw_buttons ^= s_button_bits[i];
            }
        }
        rdata[2] = (u_char)((raw_buttons >> 8) & 0x00ff);
        rdata[3] = (u_char)(raw_buttons & 0x00ff);
        for (i = 0; i < REIPAN_STICK_COUNT; i++)
        {
            rdata[s_stick_rdata_byte[i]] = 0x7f;
        }
        // Left stick: byte 6 is X (0 left, 0xff right), byte 7 is Y (0 up, 0xff down).
        if (AutoPadHeld(REIPAN_PAD_LOGICAL_COUNT + 0)) rdata[7] = 0x00;
        if (AutoPadHeld(REIPAN_PAD_LOGICAL_COUNT + 1)) rdata[7] = 0xff;
        if (AutoPadHeld(REIPAN_PAD_LOGICAL_COUNT + 2)) rdata[6] = 0x00;
        if (AutoPadHeld(REIPAN_PAD_LOGICAL_COUNT + 3)) rdata[6] = 0xff;
        PackPressureBytes(rdata, raw_buttons, true);
        return 1;
    }

    for (i = 0; i < REIPAN_PAD_LOGICAL_COUNT; i++)
    {
        if (ReiPan_InputButtonHeld(i))
        {
            raw_buttons ^= s_button_bits[i];
        }
    }

    rdata[2] = (u_char)((raw_buttons >> 8) & 0x00ff);
    rdata[3] = (u_char)(raw_buttons & 0x00ff);

    for (i = 0; i < REIPAN_STICK_COUNT; i++)
    {
        rdata[s_stick_rdata_byte[i]] = ReiPan_InputStickValue(i);
    }

    PackPressureBytes(rdata, raw_buttons, false);
    return 1;
}

int scePadInfoMode(int port, int slot, int term, int offs)
{
    (void)slot;
    (void)offs;

    if (port != 0)
    {
        return 0;
    }

    if (term == InfoModeCurID || term == InfoModeCurExID)
    {
        return 7;
    }

    return 0;
}

int scePadGetReqState(int port, int slot)
{
    (void)port;
    (void)slot;
    return scePadReqStateComplete;
}

int scePadInfoAct(int port, int slot, int actno, int term)
{
    (void)slot;
    (void)term;

    if (port != 0)
    {
        return 0;
    }

    if (actno < 0)
    {
        return 2;
    }

    return actno < 2 ? 1 : 0;
}

int scePadInfoComb(int port, int slot, int listno, int offs)
{
    (void)port;
    (void)slot;
    (void)listno;
    (void)offs;
    return 0;
}

int scePadSetMainMode(int port, int slot, int offs, int lock)
{
    (void)slot;
    (void)offs;
    (void)lock;
    return port == 0 ? 1 : 0;
}

int scePadSetActDirect(int port, int slot, const unsigned char *data)
{
    (void)slot;

    if (port != 0 || data == 0)
    {
        return 0;
    }

    // data[0] = the small motor, on or off; data[1] = the big motor's power.
    return ReiPan_InputRumble(data[0], data[1]);
}

int scePadSetActAlign(int port, int slot, const unsigned char *data)
{
    (void)slot;
    (void)data;
    return port == 0 ? 1 : 0;
}

/* 1 when the controller supports pressure-sensitive mode -- a yes/no answer,
 * not a request state.  The PC pad stands in for a DUALSHOCK 2, which does.
 * FF3's boot pad check (PadIsPressMode) shows its controller-error message and
 * pad.c never enters pressure mode while this says 0 (FF2 never asked). */
int scePadInfoPressMode(int port, int slot)
{
    (void)slot;
    return port == 0 ? 1 : 0;
}

int scePadEnterPressMode(int port, int slot)
{
    (void)slot;
    return port == 0 ? 1 : 0;
}

int scePadExitPressMode(int port, int slot)
{
    (void)slot;
    return port == 0 ? 1 : 0;
}

int scePadGetSlotMax(int port)
{
    return port == 0 ? 1 : 0;
}

int scePadSetWarningLevel(int level)
{
    (void)level;
    return 1;
}

}
