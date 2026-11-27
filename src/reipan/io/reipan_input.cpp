#include "reipan_input.h"

#include "reipan_file.h"
#include "reipan_menu_mouse.h"
#include "reipan_game_mouse.h"
#include "reipan_gyro.h"
#include "reipan_haptics.h"
#include "reipan_paths.h"
#include "../os/reipan_host.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <string.h>

// Where an analog trigger starts counting as a button press.  Also the default
// threshold stamped into every axis source.
#define REIPAN_TRIGGER_THRESHOLD 8000

// Full deflection, in the internal magnitude space every source is projected
// into.  Digital sources answer 0 or this; analog ones answer the axis value.
#define REIPAN_SRC_FULL 32767

static SDL_Gamepad *s_gamepad;
static bool         s_prompt_gamepad;
static int          s_ready;
static int          s_gamepad_mappings_attempted;

// ---------------------------------------------------------------------------
//  Defaults
//
//  These reproduce the fixed tables this layer used to carry, so a fresh
//  install plays exactly as before.  The keyboard cluster rings WASD: Q/E/C and
//  Space are the four face buttons, R/F the shoulders, Z/X the triggers, V is
//  R3 and Left Shift is L3 (it clicks the stick WASD already drives).  IJKL is
//  the aim stick — the arrow keys are the D-pad, so they cannot also be aim.
// ---------------------------------------------------------------------------

#define KEY(sc)   { REIPAN_SRC_KEY, (sc), 0, 0 }
#define PAD(b)    { REIPAN_SRC_PAD_BUTTON, (b), 0, 0 }
#define AXIS(a,d) { REIPAN_SRC_PAD_AXIS, (a), (d), REIPAN_TRIGGER_THRESHOLD }
#define NOSRC     { REIPAN_SRC_NONE, 0, 0, 0 }

static const ReiPan_InputButtonBinding
    s_default_buttons[REIPAN_PAD_BUTTON_COUNT] = {
        /*  0 UP       */ { { KEY(SDL_SCANCODE_UP),     PAD(SDL_GAMEPAD_BUTTON_DPAD_UP),         NOSRC, NOSRC } },
        /*  1 DOWN     */ { { KEY(SDL_SCANCODE_DOWN),   PAD(SDL_GAMEPAD_BUTTON_DPAD_DOWN),       NOSRC, NOSRC } },
        /*  2 LEFT     */ { { KEY(SDL_SCANCODE_LEFT),   PAD(SDL_GAMEPAD_BUTTON_DPAD_LEFT),       NOSRC, NOSRC } },
        /*  3 RIGHT    */ { { KEY(SDL_SCANCODE_RIGHT),  PAD(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),      NOSRC, NOSRC } },
        /*  4 TRIANGLE */ { { KEY(SDL_SCANCODE_E),      PAD(SDL_GAMEPAD_BUTTON_NORTH),           NOSRC, NOSRC } },
        /*  5 CROSS    */ { { KEY(SDL_SCANCODE_SPACE),  PAD(SDL_GAMEPAD_BUTTON_SOUTH),           NOSRC, NOSRC } },
#ifdef REIPAN_SLPS_25544_CHS
        /*  6 SQUARE   */ { { KEY(SDL_SCANCODE_LSHIFT), PAD(SDL_GAMEPAD_BUTTON_WEST),            NOSRC, NOSRC } },
#else
        /*  6 SQUARE   */ { { KEY(SDL_SCANCODE_Q),      PAD(SDL_GAMEPAD_BUTTON_WEST),            NOSRC, NOSRC } },
#endif
        /*  7 CIRCLE   */ { { KEY(SDL_SCANCODE_C),      PAD(SDL_GAMEPAD_BUTTON_EAST),            NOSRC, NOSRC } },
        /*  8 L1       */ { { KEY(SDL_SCANCODE_R),      PAD(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),   NOSRC, NOSRC } },
        /*  9 L2       */ { { KEY(SDL_SCANCODE_Z),      AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  1), NOSRC, NOSRC } },
        /* 10 R1       */ { { KEY(SDL_SCANCODE_F),      PAD(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),  NOSRC, NOSRC } },
        /* 11 R2       */ { { KEY(SDL_SCANCODE_X),      AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), NOSRC, NOSRC } },
        /* 12 START    */ { { KEY(SDL_SCANCODE_ESCAPE), PAD(SDL_GAMEPAD_BUTTON_START),           NOSRC, NOSRC } },
        /* 13 SELECT   */ { { KEY(SDL_SCANCODE_TAB),    PAD(SDL_GAMEPAD_BUTTON_BACK),            NOSRC, NOSRC } },
#ifdef REIPAN_SLPS_25544_CHS
        /* 14 L3       */ { { NOSRC,                    PAD(SDL_GAMEPAD_BUTTON_LEFT_STICK),      NOSRC, NOSRC } },
#else
        /* 14 L3       */ { { KEY(SDL_SCANCODE_LSHIFT), PAD(SDL_GAMEPAD_BUTTON_LEFT_STICK),      NOSRC, NOSRC } },
#endif
        /* 15 R3       */ { { KEY(SDL_SCANCODE_V),      PAD(SDL_GAMEPAD_BUTTON_RIGHT_STICK),     NOSRC, NOSRC } },
    };

// ramp_frames 1 on the left stick keeps the movement keys snapping to full
// deflection, which is what they have always done; the right stick ramps over
// six updates so keyboard aim is steerable.
static const ReiPan_InputStickBinding
    s_default_sticks[REIPAN_PAD_STICK_COUNT] = {
        /* 0 left X  */ { AXIS(SDL_GAMEPAD_AXIS_LEFTX,  1), KEY(SDL_SCANCODE_A), KEY(SDL_SCANCODE_D), 1 },
        /* 1 left Y  */ { AXIS(SDL_GAMEPAD_AXIS_LEFTY,  1), KEY(SDL_SCANCODE_W), KEY(SDL_SCANCODE_S), 1 },
        /* 2 right X */ { AXIS(SDL_GAMEPAD_AXIS_RIGHTX, 1), KEY(SDL_SCANCODE_J), KEY(SDL_SCANCODE_L), 6 },
        /* 3 right Y */ { AXIS(SDL_GAMEPAD_AXIS_RIGHTY, 1), KEY(SDL_SCANCODE_I), KEY(SDL_SCANCODE_K), 6 },
    };

#undef KEY
#undef PAD
#undef AXIS
#undef NOSRC

// Live bindings.  Seeded from the defaults on first use; a settings file or the
// rebinding UI writes over them through the accessors at the bottom.
static ReiPan_InputButtonBinding s_buttons[REIPAN_PAD_BUTTON_COUNT];
static ReiPan_InputStickBinding  s_sticks[REIPAN_PAD_STICK_COUNT];
static int                       s_bindings_ready;

// Digital contribution per stick, -1.0 .. +1.0, advanced once per update.  This
// is the only piece of per-frame state the layer keeps; the analog sources are
// read straight from SDL each time.
static float s_stick_ramp[REIPAN_PAD_STICK_COUNT];

// Raised by the rebinding UI while it waits for a key, and by the RmlUi menus
// while one holds the controls; s_suppressed is either.  See the header.
static int s_rebind_suppressed;
static int s_menu_suppressed;
static int s_suppressed;

// What the ImGui overlay is taking this frame.
static int s_ui_wants_mouse;
static int s_ui_wants_keyboard;
static bool s_key_pending[SDL_SCANCODE_COUNT];
static bool s_key_sample[SDL_SCANCODE_COUNT];

// Buttons that were down when the last suppression ended, one bit per logical
// button: each reads as released until it is let go, so the press that closed
// a menu does not reach the game.  See ApplySuppression().
static unsigned int s_release_mask;

// Which device the player asked for, by GUID; empty means "first available".
// Set to 1 whenever the device list may have changed, so the next update
// re-runs the choice instead of keeping whatever it opened first.
static char s_preferred_guid[REIPAN_GAMEPAD_GUID_LEN];
static int  s_devices_dirty = 1;

static void EnsureBindings(void)
{
    if (s_bindings_ready == 0)
    {
        memcpy(s_buttons, s_default_buttons, sizeof(s_buttons));
        memcpy(s_sticks, s_default_sticks, sizeof(s_sticks));
        s_bindings_ready = 1;
    }
}

// ---------------------------------------------------------------------------
//  Source evaluation
// ---------------------------------------------------------------------------

static int KeyDown(const bool *keys, int scancode)
{
    return scancode > 0 &&
           scancode < SDL_SCANCODE_COUNT &&
           (s_key_sample[scancode] || (keys != nullptr && keys[scancode]));
}

// One source projected into 0..REIPAN_SRC_FULL.  Digital sources answer either
// end; an axis answers its travel in the bound direction, which is what will
// let the pressure bytes carry the bound control's actual strength.  A gamepad source with no gamepad open answers 0.
static int SourceMagnitude(const ReiPan_InputSource *src, const bool *keys)
{
    int value;

    switch (src->kind)
    {
    case REIPAN_SRC_KEY:
        return KeyDown(keys, src->code) ? REIPAN_SRC_FULL : 0;

    case REIPAN_SRC_PAD_BUTTON:
        if (s_gamepad == 0)
        {
            return 0;
        }
        return SDL_GetGamepadButton(s_gamepad, (SDL_GamepadButton)src->code)
                   ? REIPAN_SRC_FULL
                   : 0;

    case REIPAN_SRC_PAD_AXIS:
        if (s_gamepad == 0)
        {
            return 0;
        }
        value = SDL_GetGamepadAxis(s_gamepad, (SDL_GamepadAxis)src->code);
        value = src->dir < 0 ? -value : value;
        if (value < 0)
        {
            return 0;
        }
        return value > REIPAN_SRC_FULL ? REIPAN_SRC_FULL : value;

    default:
        return 0;
    }
}

// Held is "past the threshold".  Digital sources leave threshold at 0 and so
// trip on any press; axis sources carry REIPAN_TRIGGER_THRESHOLD by default.
static int SourceHeldRaw(const ReiPan_InputSource *src, const bool *keys)
{
    if (src->kind == REIPAN_SRC_NONE)
    {
        return 0;
    }
    return SourceMagnitude(src, keys) > src->threshold ? 1 : 0;
}

static int SourceHeld(const ReiPan_InputSource *src, const bool *keys)
{
    return s_suppressed == 0 && SourceHeldRaw(src, keys) != 0 ? 1 : 0;
}

// Any of a logical button's sources down, suppressed or not.
static int ButtonHeldRaw(int index, const bool *keys)
{
    int i;

    for (i = 0; i < REIPAN_INPUT_MAX_SOURCES; i++)
    {
        if (SourceHeldRaw(&s_buttons[index].src[i], keys))
        {
            return 1;
        }
    }
    return 0;
}

static unsigned char AxisToPs2(int axis)
{
    int ps2;

    if (axis < -32768)
    {
        axis = -32768;
    }
    else if (axis > 32767)
    {
        axis = 32767;
    }

    ps2 = (axis + 32768) * 255 / 65535;
    return (unsigned char)ps2;
}

static int AbsInt(int x)
{
    return x < 0 ? -x : x;
}

// ---------------------------------------------------------------------------
//  Device lifecycle
// ---------------------------------------------------------------------------

static void CloseGamepad(void)
{
    if (s_gamepad != 0)
    {
        ReiPan_HapticsSetGamepad(0);    /* stops the motors, so while it is open */
        SDL_CloseGamepad(s_gamepad);
        s_gamepad = 0;
        s_prompt_gamepad = false;
        ReiPan_GyroSetGamepad(0);
    }
}

static void LoadGamepadMappings(void)
{
    char path[1024];

    if (s_gamepad_mappings_attempted != 0)
    {
        return;
    }
    s_gamepad_mappings_attempted = 1;

    if (ReiPan_PathResolveAsset("resources/gamecontrollerdb.txt",
                               path,
                               sizeof(path)) == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ReiPan: resources/gamecontrollerdb.txt was not found; "
                    "SDL built-in controller mappings will be used");
        return;
    }

    /* The number added, or -1: none for this platform is not a failure. */
    if (SDL_AddGamepadMappingsFromFile(path) < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ReiPan: failed to load controller mappings from %s: %s",
                    path,
                    SDL_GetError());
    }
}

// The GUID of a device id, as 32 hex characters.
static void GamepadGuidForId(SDL_JoystickID id, char *out, int len)
{
    if (out == 0 || len <= 0)
    {
        return;
    }
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), out, len);
}

static void CopyStr(char *dst, int len, const char *src)
{
    if (dst == 0 || len <= 0)
    {
        return;
    }
    if (src == 0)
    {
        dst[0] = 0;
        return;
    }
    SDL_strlcpy(dst, src, (size_t)len);
}

/*
 * Pick the device to drive the pad with.
 *
 * The preference is by GUID, not by position: SDL's list is in whatever order
 * the OS enumerated this boot, so "the first one" is a different device from
 * one session to the next as soon as more than one is plugged in.  A wheel or a
 * second pad carrying a mapping used to be able to claim the slot permanently.
 *
 * With no preference set this still takes the first device, which is the
 * previous behaviour and the right default for the overwhelmingly common case
 * of exactly one controller.
 */
static int SelectGamepad(void)
{
    int             count = 0;
    SDL_JoystickID *ids;
    SDL_JoystickID  chosen = 0;
    int             i;

    if (ReiPan_InputInit() == 0)
    {
        return 0;
    }

    /* A pad pulled out from under us. */
    if (s_gamepad != 0 && !SDL_GamepadConnected(s_gamepad))
    {
        CloseGamepad();
        s_devices_dirty = 1;
    }

    /* Nothing to reconsider: a pad is open and no device has come or gone. */
    if (s_gamepad != 0 && s_devices_dirty == 0)
    {
        return 1;
    }
    s_devices_dirty = 0;

    ids = SDL_GetGamepads(&count);
    if (ids == 0 || count <= 0)
    {
        if (ids != 0)
        {
            SDL_free(ids);
        }
        return s_gamepad != 0 ? 1 : 0;
    }

    if (s_preferred_guid[0] != 0)
    {
        char guid[REIPAN_GAMEPAD_GUID_LEN];
        for (i = 0; i < count; i++)
        {
            GamepadGuidForId(ids[i], guid, sizeof(guid));
            if (SDL_strcasecmp(guid, s_preferred_guid) == 0)
            {
                chosen = ids[i];
                break;
            }
        }
    }

    /* No preference, or the preferred device is not plugged in right now.  Fall
     * back rather than leaving the player with nothing -- the preference is
     * kept, so plugging it back in reclaims the slot. */
    if (chosen == 0)
    {
        chosen = ids[0];
    }

    SDL_free(ids);

    if (s_gamepad != 0)
    {
        if (SDL_GetGamepadID(s_gamepad) == chosen)
        {
            return 1; /* already the right one */
        }
        CloseGamepad();
    }

    s_gamepad = SDL_OpenGamepad(chosen);
    if (s_gamepad != 0)
    {
        s_prompt_gamepad = true;
        SDL_Log("ReiPan: using gamepad \"%s\"", SDL_GetGamepadName(s_gamepad));
        ReiPan_GyroSetGamepad(s_gamepad);
        ReiPan_HapticsSetGamepad(s_gamepad);
    }

    return s_gamepad != 0 ? 1 : 0;
}

/* The old name, kept because the rumble path reads as "open one if needed". */
static int OpenGamepad(void)
{
    return SelectGamepad();
}

// Move each stick's digital contribution one step toward what its keys are
// asking for.  Both keys down, or neither, decays to centre — the same "no
// clear direction" answer the old either/or test gave.
static void AdvanceStickRamps(void)
{
    const bool *keys = SDL_GetKeyboardState(0);
    int         i;

    for (i = 0; i < REIPAN_PAD_STICK_COUNT; i++)
    {
        const ReiPan_InputStickBinding *b = &s_sticks[i];
        int   neg = SourceHeld(&b->neg, keys);
        int   pos = SourceHeld(&b->pos, keys);
        float target = (float)(pos - neg);
        float step;

        if (b->ramp_frames > 1)
        {
            step = 1.0f / (float)b->ramp_frames;
        }
        else
        {
            step = 1.0f;
        }

        if (s_stick_ramp[i] < target)
        {
            s_stick_ramp[i] += step;
            if (s_stick_ramp[i] > target)
            {
                s_stick_ramp[i] = target;
            }
        }
        else if (s_stick_ramp[i] > target)
        {
            s_stick_ramp[i] -= step;
            if (s_stick_ramp[i] < target)
            {
                s_stick_ramp[i] = target;
            }
        }
    }
}

void ReiPan_InputPromptEvent(const SDL_Event *event)
{
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (!event->key.repeat &&
            !((event->key.mod & SDL_KMOD_ALT) != 0 &&
              (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER)))
            s_prompt_gamepad = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        s_prompt_gamepad = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.xrel != 0.0f || event->motion.yrel != 0.0f)
            s_prompt_gamepad = false;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (s_gamepad != nullptr && event->gbutton.which == SDL_GetGamepadID(s_gamepad))
            s_prompt_gamepad = true;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (s_gamepad != nullptr && event->gaxis.which == SDL_GetGamepadID(s_gamepad) &&
            AbsInt(event->gaxis.value) > REIPAN_SRC_FULL / 2)
            s_prompt_gamepad = true;
        break;
    }
}

static const char *KeyPromptName(SDL_Scancode code)
{
    static const char *const numpad_digits[] = {
        "Num1", "Num2", "Num3", "Num4", "Num5",
        "Num6", "Num7", "Num8", "Num9", "Num0"
    };
    if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_0)
        return numpad_digits[code - SDL_SCANCODE_KP_1];

    switch (code)
    {
    case SDL_SCANCODE_SPACE: return "Spc";
    case SDL_SCANCODE_LSHIFT: return "LSh";
    case SDL_SCANCODE_RSHIFT: return "RSh";
    case SDL_SCANCODE_LCTRL: return "LCtrl";
    case SDL_SCANCODE_RCTRL: return "RCtrl";
    case SDL_SCANCODE_LALT: return "LAlt";
    case SDL_SCANCODE_RALT: return "RAlt";
    case SDL_SCANCODE_LGUI: return "LWin";
    case SDL_SCANCODE_RGUI: return "RWin";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_ESCAPE: return "Esc";
    case SDL_SCANCODE_BACKSPACE: return "Bksp";
    case SDL_SCANCODE_PAGEUP: return "PgUp";
    case SDL_SCANCODE_PAGEDOWN: return "PgDn";
    case SDL_SCANCODE_DELETE: return "Del";
    case SDL_SCANCODE_INSERT: return "Ins";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Eq";
    case SDL_SCANCODE_LEFTBRACKET: return "LBrk";
    case SDL_SCANCODE_RIGHTBRACKET: return "RBrk";
    case SDL_SCANCODE_BACKSLASH: return "BSl";
    case SDL_SCANCODE_NONUSBACKSLASH: return "IntSl";
    case SDL_SCANCODE_SEMICOLON: return "Semi";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_GRAVE: return "Grave";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Dot";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_KP_PLUS: return "NumAdd";
    case SDL_SCANCODE_KP_MINUS: return "NumSub";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumMul";
    case SDL_SCANCODE_KP_DIVIDE: return "NumDiv";
    case SDL_SCANCODE_KP_PERIOD: return "NumDot";
    case SDL_SCANCODE_KP_ENTER: return "NumEnt";
    default: return SDL_GetScancodeName(code);
    }
}

static ReiPan_InputPrompt PadButtonPrompt(SDL_GamepadButton button)
{
    const SDL_GamepadButtonLabel label = s_gamepad != nullptr ?
        SDL_GetGamepadButtonLabel(s_gamepad, button) :
        SDL_GetGamepadButtonLabelForType(SDL_GAMEPAD_TYPE_PS5, button);
    switch (label)
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return {REIPAN_PROMPT_TEXT, "A"};
    case SDL_GAMEPAD_BUTTON_LABEL_B: return {REIPAN_PROMPT_TEXT, "B"};
    case SDL_GAMEPAD_BUTTON_LABEL_X: return {REIPAN_PROMPT_TEXT, "X"};
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return {REIPAN_PROMPT_TEXT, "Y"};
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return {REIPAN_PROMPT_CIRCLE, nullptr};
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return {REIPAN_PROMPT_CROSS, nullptr};
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return {REIPAN_PROMPT_TRIANGLE, nullptr};
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return {REIPAN_PROMPT_SQUARE, nullptr};
    default: break;
    }

    const bool ps = ReiPan_InputGamepadUsesPlayStationPrompts();
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return {REIPAN_PROMPT_TEXT, ps ? "L1" : "LB"};
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return {REIPAN_PROMPT_TEXT, ps ? "R1" : "RB"};
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return {REIPAN_PROMPT_TEXT, ps ? "L3" : "LS"};
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return {REIPAN_PROMPT_TEXT, ps ? "R3" : "RS"};
    case SDL_GAMEPAD_BUTTON_START: return {REIPAN_PROMPT_TEXT, ps ? "Start" : "Menu"};
    case SDL_GAMEPAD_BUTTON_BACK: return {REIPAN_PROMPT_TEXT, ps ? "Select" : "View"};
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return {REIPAN_PROMPT_TEXT, "Up"};
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return {REIPAN_PROMPT_TEXT, "Down"};
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return {REIPAN_PROMPT_TEXT, "Left"};
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return {REIPAN_PROMPT_TEXT, "Right"};
    default: return {REIPAN_PROMPT_TEXT, SDL_GetGamepadStringForButton(button)};
    }
}

static const char *PadAxisPromptName(SDL_GamepadAxis axis, int direction)
{
    switch (axis)
    {
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return ReiPan_InputGamepadUsesPlayStationPrompts() ? "L2" : "LT";
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return ReiPan_InputGamepadUsesPlayStationPrompts() ? "R2" : "RT";
    case SDL_GAMEPAD_AXIS_LEFTX: return direction < 0 ? "LS L" : "LS R";
    case SDL_GAMEPAD_AXIS_LEFTY: return direction < 0 ? "LS U" : "LS D";
    case SDL_GAMEPAD_AXIS_RIGHTX: return direction < 0 ? "RS L" : "RS R";
    case SDL_GAMEPAD_AXIS_RIGHTY: return direction < 0 ? "RS U" : "RS D";
    default: return nullptr;
    }
}

extern "C" {

int ReiPan_InputUsesGamepadPrompts(void)
{
    return s_gamepad != nullptr && s_prompt_gamepad;
}

int ReiPan_InputGamepadUsesPlayStationPrompts(void)
{
    if (s_gamepad == nullptr)
        return 1;
    const SDL_GamepadType type = SDL_GetGamepadType(s_gamepad);
    return type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 ||
           type == SDL_GAMEPAD_TYPE_PS5;
}

ReiPan_InputPrompt ReiPan_InputGetGamepadButtonPrompt(int button)
{
    return PadButtonPrompt(static_cast<SDL_GamepadButton>(button));
}

int ReiPan_InputInit(void)
{
    if (s_ready == 0)
    {
        if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
        {
            return 0;
        }
        LoadGamepadMappings();
        s_ready = 1;
    }
    EnsureBindings();
    return 1;
}

void ReiPan_InputShutdown(void)
{
    CloseGamepad();
}

void ReiPan_InputUpdate(void)
{
    if (ReiPan_InputInit() == 0)
    {
        return;
    }
    ReiPan_HostPumpEvents();    /* the main thread pumps when the game has its own */
    memcpy(s_key_sample, s_key_pending, sizeof(s_key_sample));
    memset(s_key_pending, 0, sizeof(s_key_pending));
    SelectGamepad();
    ReiPan_MenuMousePoll();
    ReiPan_GameMousePoll();     /* after the menus: they have the mouse first */
    ReiPan_GyroPoll();
    AdvanceStickRamps();
}

void ReiPan_InputQueueKeyDown(int scancode)
{
    if (scancode > 0 && scancode < SDL_SCANCODE_COUNT && ReiPan_InputMenuKeysFree())
        s_key_pending[scancode] = true;
}

void ReiPan_InputClearKeyboard(void)
{
    memset(s_key_pending, 0, sizeof(s_key_pending));
    memset(s_key_sample, 0, sizeof(s_key_sample));
    memset(s_stick_ramp, 0, sizeof(s_stick_ramp));
}

int ReiPan_InputGetGamepadCount(void)
{
    int             count = 0;
    SDL_JoystickID *ids;

    if (ReiPan_InputInit() == 0)
    {
        return 0;
    }

    ids = SDL_GetGamepads(&count);
    if (ids != 0)
    {
        SDL_free(ids);
    }
    return count > 0 ? count : 0;
}

int ReiPan_InputGetGamepadInfo(int index, char *name, int name_len,
                               char *guid, int guid_len)
{
    int             count = 0;
    SDL_JoystickID *ids;
    int             ok = 0;

    CopyStr(name, name_len, "");
    CopyStr(guid, guid_len, "");

    if (index < 0 || ReiPan_InputInit() == 0)
    {
        return 0;
    }

    ids = SDL_GetGamepads(&count);
    if (ids == 0)
    {
        return 0;
    }
    if (index < count)
    {
        CopyStr(name, name_len, SDL_GetGamepadNameForID(ids[index]));
        GamepadGuidForId(ids[index], guid, guid_len);
        ok = 1;
    }
    SDL_free(ids);
    return ok;
}

int ReiPan_InputGetActiveGamepad(char *name, int name_len,
                                 char *guid, int guid_len)
{
    CopyStr(name, name_len, "");
    CopyStr(guid, guid_len, "");

    if (s_gamepad == 0)
    {
        return 0;
    }

    CopyStr(name, name_len, SDL_GetGamepadName(s_gamepad));
    GamepadGuidForId(SDL_GetGamepadID(s_gamepad), guid, guid_len);
    return 1;
}

void ReiPan_InputSetPreferredGamepad(const char *guid)
{
    if (guid == 0)
    {
        guid = "";
    }
    SDL_strlcpy(s_preferred_guid, guid, sizeof(s_preferred_guid));

    /* Re-run the choice on the next update rather than switching here: this is
     * called from the settings UI and from the config load, neither of which
     * should be opening devices. */
    s_devices_dirty = 1;
}

const char *ReiPan_InputGetPreferredGamepad(void)
{
    return s_preferred_guid;
}

void ReiPan_InputRefreshDevices(void)
{
    s_devices_dirty = 1;
}

int ReiPan_InputOpenGamepad(void)
{
    return SelectGamepad();
}

int ReiPan_InputButtonHeld(int index)
{
    int held;

    if (index < 0 || index >= REIPAN_PAD_BUTTON_COUNT)
    {
        return 0;
    }
    EnsureBindings();

    // A press a menu made for the mouse, Enter or Backspace.
    if (s_suppressed == 0 && ReiPan_MenuMouseHeld(index) != 0)
    {
        return 1;
    }

    held = ButtonHeldRaw(index, SDL_GetKeyboardState(0));

    // Still down from before the suppression ended: the menu's until let go.
    if ((s_release_mask & (1u << index)) != 0)
    {
        if (held != 0)
        {
            return 0;
        }
        s_release_mask &= ~(1u << index);
    }

    return s_suppressed == 0 ? held : 0;
}

unsigned char ReiPan_InputButtonPressure(int index)
{
    if (!ReiPan_InputButtonHeld(index))
        return 0;
    if (ReiPan_MenuMouseHeld(index) != 0)
        return 255;
    EnsureBindings();
    const bool *keys = SDL_GetKeyboardState(nullptr);
    int magnitude = 0;
    for (int i = 0; i < REIPAN_INPUT_MAX_SOURCES; ++i)
    {
        const ReiPan_InputSource *source = &s_buttons[index].src[i];
        if (SourceHeld(source, keys))
        {
            const int value = SourceMagnitude(source, keys);
            if (value > magnitude)
                magnitude = value;
        }
    }
    return (unsigned char)(magnitude * 255 / REIPAN_SRC_FULL);
}

unsigned char ReiPan_InputStickValue(int index, int digital_index)
{
    int analog_value = 127;
    int digital_value = 127;

    if (index < 0 || index >= REIPAN_PAD_STICK_COUNT ||
        digital_index < 0 || digital_index >= REIPAN_PAD_STICK_COUNT || s_suppressed != 0)
    {
        return 127;
    }
    EnsureBindings();

    if (s_sticks[index].analog.kind == REIPAN_SRC_PAD_AXIS && s_gamepad != 0)
    {
        int raw = SDL_GetGamepadAxis(
            s_gamepad, (SDL_GamepadAxis)s_sticks[index].analog.code);
        analog_value = AxisToPs2(s_sticks[index].analog.dir < 0 ? -raw : raw);
    }

    // The ramp is signed -1..+1; 32767 rather than 32768 on the negative side
    // keeps AxisToPs2 symmetric about 127.
    if (s_stick_ramp[digital_index] != 0.0f)
    {
        digital_value = AxisToPs2((int)(s_stick_ramp[digital_index] * 32767.0f));
    }

    return (unsigned char)((AbsInt(analog_value - 127) >= AbsInt(digital_value - 127))
                               ? analog_value
                               : digital_value);
}

int ReiPan_InputRumble(unsigned char small_on, unsigned char big_power)
{
    if (OpenGamepad() == 0)
    {
        return 0;
    }
    return ReiPan_HapticsMotors(small_on, big_power);
}

// ---------------------------------------------------------------------------
//  Binding access
// ---------------------------------------------------------------------------

int ReiPan_InputGetButtonBinding(int button, ReiPan_InputButtonBinding *out)
{
    if (button < 0 || button >= REIPAN_PAD_BUTTON_COUNT || out == 0)
    {
        return 0;
    }
    EnsureBindings();
    *out = s_buttons[button];
    return 1;
}

ReiPan_InputPrompt ReiPan_InputGetButtonPrompt(int button)
{
    const ReiPan_InputPrompt unbound = {REIPAN_PROMPT_TEXT, "-"};
    if (button < 0 || button >= REIPAN_PAD_BUTTON_COUNT)
        return unbound;

    EnsureBindings();
    ReiPan_InputPrompt fallback = {};
    for (const ReiPan_InputSource &source : s_buttons[button].src)
    {
        ReiPan_InputPrompt prompt = {};
        switch (source.kind)
        {
        case REIPAN_SRC_KEY:
            prompt.text = KeyPromptName(static_cast<SDL_Scancode>(source.code));
            break;
        case REIPAN_SRC_PAD_BUTTON:
            if (s_gamepad != nullptr &&
                SDL_GamepadHasButton(s_gamepad, static_cast<SDL_GamepadButton>(source.code)))
                prompt = PadButtonPrompt(static_cast<SDL_GamepadButton>(source.code));
            break;
        case REIPAN_SRC_PAD_AXIS:
            if (s_gamepad != nullptr &&
                SDL_GamepadHasAxis(s_gamepad, static_cast<SDL_GamepadAxis>(source.code)))
                prompt.text = PadAxisPromptName(static_cast<SDL_GamepadAxis>(source.code), source.dir);
            break;
        }
        if (prompt.symbol == REIPAN_PROMPT_TEXT && (prompt.text == nullptr || *prompt.text == '\0'))
            continue;

        if (s_prompt_gamepad == (source.kind != REIPAN_SRC_KEY))
            return prompt;
        if (fallback.symbol == REIPAN_PROMPT_TEXT && fallback.text == nullptr)
            fallback = prompt;
    }
    return fallback.symbol == REIPAN_PROMPT_TEXT && fallback.text == nullptr ? unbound : fallback;
}

int ReiPan_InputSetButtonBinding(int button,
                                 const ReiPan_InputButtonBinding *in)
{
    if (button < 0 || button >= REIPAN_PAD_BUTTON_COUNT || in == 0)
    {
        return 0;
    }
    EnsureBindings();
    s_buttons[button] = *in;
    return 1;
}

int ReiPan_InputGetStickBinding(int stick, ReiPan_InputStickBinding *out)
{
    if (stick < 0 || stick >= REIPAN_PAD_STICK_COUNT || out == 0)
    {
        return 0;
    }
    EnsureBindings();
    *out = s_sticks[stick];
    return 1;
}

int ReiPan_InputSetStickBinding(int stick, const ReiPan_InputStickBinding *in)
{
    if (stick < 0 || stick >= REIPAN_PAD_STICK_COUNT || in == 0)
    {
        return 0;
    }
    EnsureBindings();
    s_sticks[stick] = *in;

    // A stick whose keys just moved should not keep the deflection the old
    // pair had built up.
    s_stick_ramp[stick] = 0.0f;
    return 1;
}

static void ApplySuppression(void)
{
    const int was = s_suppressed;
    int       i;

    s_suppressed = (s_rebind_suppressed != 0 || s_menu_suppressed != 0) ? 1 : 0;

    // Drop whatever deflection the digital keys had built up, so releasing
    // suppression does not hand the game a stick that is still pushed over.
    if (s_suppressed != 0)
    {
        ReiPan_InputClearKeyboard();
        return;
    }

    // Handing the controls back: a menu closes on a press, and the game must
    // not see that press.  Only the buttons down right now are held back, each
    // until it is let go -- waiting for every key and pad button anywhere to
    // be up instead leaves the game deaf for as long as any one of them reads
    // as down, which a device dropped from under a press (Steam Input
    // switching configurations) can do indefinitely.
    if (was != 0)
    {
        const bool *keys = SDL_GetKeyboardState(0);

        EnsureBindings();
        s_release_mask = 0;
        for (i = 0; i < REIPAN_PAD_BUTTON_COUNT; i++)
        {
            if (ButtonHeldRaw(i, keys))
            {
                s_release_mask |= 1u << i;
            }
        }
    }
}

void ReiPan_InputSetSuppressed(int suppressed)
{
    s_rebind_suppressed = suppressed != 0 ? 1 : 0;
    ApplySuppression();
}

void ReiPan_InputSetMenuSuppressed(int suppressed)
{
    s_menu_suppressed = suppressed != 0 ? 1 : 0;
    ApplySuppression();
}

int ReiPan_InputIsSuppressed(void)
{
    return s_suppressed;
}

void ReiPan_InputSetUiCapture(int wants_mouse, int wants_keyboard)
{
    s_ui_wants_mouse = wants_mouse != 0 ? 1 : 0;
    s_ui_wants_keyboard = wants_keyboard != 0 ? 1 : 0;
}

int ReiPan_InputMenuPointerFree(void)
{
    return (s_suppressed == 0 && s_ui_wants_mouse == 0) ? 1 : 0;
}

int ReiPan_InputMenuKeysFree(void)
{
    return (s_suppressed == 0 && s_ui_wants_keyboard == 0) ? 1 : 0;
}

void ReiPan_InputResetBindings(void)
{
    memcpy(s_buttons, s_default_buttons, sizeof(s_buttons));
    memcpy(s_sticks, s_default_sticks, sizeof(s_sticks));
    memset(s_stick_ramp, 0, sizeof(s_stick_ramp));
    s_bindings_ready = 1;
}

int ReiPan_InputBindingsAreDefault(void)
{
    EnsureBindings();
    return memcmp(s_buttons, s_default_buttons, sizeof(s_buttons)) == 0 &&
           memcmp(s_sticks, s_default_sticks, sizeof(s_sticks)) == 0;
}

// ---------------------------------------------------------------------------
//  Text form
// ---------------------------------------------------------------------------

int ReiPan_InputSourceToString(const ReiPan_InputSource *src, char *buf, int len)
{
    const char *name;
    const char *prefix;
    int         written;

    if (src == 0 || buf == 0 || len <= 0)
    {
        return 0;
    }

    switch (src->kind)
    {
    case REIPAN_SRC_KEY:
        name = SDL_GetScancodeName((SDL_Scancode)src->code);
        prefix = "key:";
        break;

    case REIPAN_SRC_PAD_BUTTON:
        name = SDL_GetGamepadStringForButton((SDL_GamepadButton)src->code);
        prefix = "pad:";
        break;

    case REIPAN_SRC_PAD_AXIS:
        name = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)src->code);
        prefix = src->dir < 0 ? "axis-:" : "axis+:";
        break;

    default:
        buf[0] = 0;
        return 0;
    }

    if (name == 0 || name[0] == 0)
    {
        buf[0] = 0;
        return 0;
    }

    if (src->kind == REIPAN_SRC_PAD_AXIS &&
        src->threshold != REIPAN_TRIGGER_THRESHOLD)
    {
        written = SDL_snprintf(buf, (size_t)len, "%s%s@%d",
                               prefix, name, src->threshold);
    }
    else
    {
        written = SDL_snprintf(buf, (size_t)len, "%s%s", prefix, name);
    }

    return (written > 0 && written < len) ? written : 0;
}

int ReiPan_InputSourceFromString(const char *text, ReiPan_InputSource *out)
{
    ReiPan_InputSource parsed;
    char               name[128];
    const char         *body;
    const char         *at;
    size_t              n;

    if (text == 0 || out == 0)
    {
        return 0;
    }

    memset(&parsed, 0, sizeof(parsed));

    if (SDL_strncmp(text, "key:", 4) == 0)
    {
        parsed.kind = REIPAN_SRC_KEY;
        body = text + 4;
    }
    else if (SDL_strncmp(text, "pad:", 4) == 0)
    {
        parsed.kind = REIPAN_SRC_PAD_BUTTON;
        body = text + 4;
    }
    else if (SDL_strncmp(text, "axis+:", 6) == 0)
    {
        parsed.kind = REIPAN_SRC_PAD_AXIS;
        parsed.dir = 1;
        body = text + 6;
    }
    else if (SDL_strncmp(text, "axis-:", 6) == 0)
    {
        parsed.kind = REIPAN_SRC_PAD_AXIS;
        parsed.dir = -1;
        body = text + 6;
    }
    else if (SDL_strcmp(text, "none") == 0)
    {
        out->kind = REIPAN_SRC_NONE;
        out->code = 0;
        out->dir = 0;
        out->threshold = 0;
        return 1;
    }
    else
    {
        return 0;
    }

    // An "@<n>" suffix overrides the axis threshold.
    parsed.threshold = parsed.kind == REIPAN_SRC_PAD_AXIS
                           ? REIPAN_TRIGGER_THRESHOLD
                           : 0;
    at = SDL_strchr(body, '@');
    if (at != 0)
    {
        n = (size_t)(at - body);

        // A bare or non-numeric "@" keeps the default threshold instead of
        // parsing as 0.  Zero on an axis is a hair trigger that fires on
        // stick drift, which is the worst thing a typo in a hand-edited
        // settings file could quietly produce.
        if (at[1] >= '0' && at[1] <= '9')
        {
            parsed.threshold = SDL_atoi(at + 1);
        }
    }
    else
    {
        n = SDL_strlen(body);
    }

    if (n == 0 || n >= sizeof(name))
    {
        return 0;
    }
    memcpy(name, body, n);
    name[n] = 0;

    switch (parsed.kind)
    {
    case REIPAN_SRC_KEY:
    {
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        if (sc == SDL_SCANCODE_UNKNOWN)
        {
            return 0;
        }
        parsed.code = (int)sc;
        break;
    }

    case REIPAN_SRC_PAD_BUTTON:
    {
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name);
        if (b == SDL_GAMEPAD_BUTTON_INVALID)
        {
            return 0;
        }
        parsed.code = (int)b;
        break;
    }

    default:
    {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(name);
        if (a == SDL_GAMEPAD_AXIS_INVALID)
        {
            return 0;
        }
        parsed.code = (int)a;
        break;
    }
    }

    *out = parsed;
    return 1;
}

}
