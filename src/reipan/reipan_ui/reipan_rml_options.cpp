#include "reipan_rml_options.h"
#include "reipan_rml_common.h"
#include "reipan_rmlui.h"
#include "reipan_ui.h"
#include "reipan_ui_locale.h"
#include "../io/reipan_input.h"
#include "../reipan_config.h"
#include "../rendering/reipan_renderer.h"
#include "../gameplay/reipan_over_shoulder.h"
#include "../io/reipan_game_mouse.h"
#include "../io/reipan_gyro.h"
#include "../io/reipan_haptics.h"

#include "outgame/option.h"             // GetOptionWrk, OptionInit, OptionApplySound

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
using ReiPanUiLocale::Translate;
using ReiPanUiLocale::UiText;

constexpr const char *kDocumentPath = "resources/rml/options.rml";

/* UTF-8 for the step arrows either side of a value. */
constexpr const char *kArrowLeft = "\xE2\x80\xB9";
constexpr const char *kArrowRight = "\xE2\x80\xBA";

using Text = std::function<std::string()>;

enum class RowKind
{
    Heading,
    Choice,
    Slider,
    Action,
};

struct Row
{
    RowKind kind = RowKind::Heading;
    std::string label;
    Text help;
    /* Greyed and passed over by the cursor while this says no. */
    std::function<bool()> enabled;
    /* A setting reipan_input owns, which has to arm the save itself. */
    bool input_setting = false;

    /* Choice: an index into names (or count_of when how many there are
     * depends on the hardware). */
    std::vector<std::string> names;
    std::function<int()> count_of;
    std::function<int()> get;
    std::function<void(int)> set;

    /* Slider.  `step` is added -- or, logarithmic, multiplied. */
    std::function<float()> get_value;
    std::function<void(float)> set_value;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.05f;
    bool logarithmic = false;
    std::function<std::string(float)> format;

    /* Action. */
    std::string action;
    std::function<void()> run;

    /* Built elements, and what they show, so a refresh only touches what
     * changed. */
    Rml::Element *element = nullptr;
    Rml::Element *value = nullptr;
    Rml::Element *track = nullptr;
    Rml::Element *fill = nullptr;
    std::string shown_value;
    float shown_fill = -1.0f;
    int shown_enabled = -1;

    bool Selectable() const
    {
        return kind != RowKind::Heading && IsEnabled();
    }
    bool IsEnabled() const
    {
        return !enabled || enabled();
    }
    int Count() const
    {
        return count_of ? count_of() : (int)names.size();
    }
    std::string NameOf(int index) const
    {
        return index >= 0 && index < (int)names.size() ? names[index] : "";
    }
};

struct Page
{
    const char *id;
    const char *name;
    std::vector<Row> rows;
    Rml::Element *element = nullptr;
    Rml::Element *tab = nullptr;
    int selected = -1;
};

struct RowRef
{
    int page;
    int row;
};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct OptionsState
{
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;
    Rml::Element *tabs = nullptr;
    Rml::Element *pages_root = nullptr;
    Rml::Element *help = nullptr;
    Rml::Element *prompts = nullptr;
    std::vector<Page> pages;
    std::unordered_map<Rml::Element *, RowRef> row_of;
    int page = 0;
    int from = REIPAN_RML_OPTIONS_FROM_PAUSE;
    bool open = false;
    RowRef dragging = {-1, -1};
    std::string shown_help;
    std::string shown_prompts;
    ReiPanRmlCommon::ConfirmDialog confirm;
};

OptionsState g_opt;
Listener g_listener;

/* ---------------------------------------------------------------------------
 *  Rows.
 * ------------------------------------------------------------------------ */

Text Fixed(const char *text)
{
    return [text] { return std::string(text); };
}

Row Heading(const char *label)
{
    Row row;
    row.kind = RowKind::Heading;
    row.label = label;
    return row;
}

Row Choice(const char *label, Text help, std::vector<std::string> names,
           std::function<int()> get, std::function<void(int)> set)
{
    Row row;
    row.kind = RowKind::Choice;
    row.label = label;
    row.help = std::move(help);
    row.names = std::move(names);
    row.get = std::move(get);
    row.set = std::move(set);
    return row;
}

Row Toggle(const char *label, Text help, std::function<bool()> get,
           std::function<void(bool)> set, const char *off = "Off",
           const char *on = "On")
{
    return Choice(label, std::move(help), {off, on},
                  [get] { return get() ? 1 : 0; },
                  [set](int value) { set(value != 0); });
}

Row Slider(const char *label, Text help, float min, float max, float step,
           std::function<float()> get, std::function<void(float)> set,
           std::function<std::string(float)> format)
{
    Row row;
    row.kind = RowKind::Slider;
    row.label = label;
    row.help = std::move(help);
    row.min = min;
    row.max = max;
    row.step = step;
    row.get_value = std::move(get);
    row.set_value = std::move(set);
    row.format = std::move(format);
    return row;
}

Row Action(const char *label, Text help, const char *action,
           std::function<void()> run)
{
    Row row;
    row.kind = RowKind::Action;
    row.label = label;
    row.help = std::move(help);
    row.action = action;
    row.run = std::move(run);
    return row;
}

Row &When(Row &&row, std::function<bool()> enabled)
{
    row.enabled = std::move(enabled);
    return row;
}

std::string Printf(const char *format, double value)
{
    char text[64];
    SDL_snprintf(text, sizeof(text), format, value);
    return text;
}

std::function<std::string(float)> Format(const char *format)
{
    const char *localized = Translate(format).data();
    return [localized](float value) { return Printf(localized, value); };
}

std::function<std::string(float)> Percent(float scale)
{
    return [scale](float value) {
        return Printf("%.0f%%", (double)(value * scale));
    };
}

/* ---------------------------------------------------------------------------
 *  The game's own options.
 *
 *  The option work's live settings are what the game reads -- the pad layout
 *  every frame (PadKeyUpdate), the control scheme (PlyrCtrlOptCheck), the
 *  brightness filter, vibration and subtitles where they happen -- and what a
 *  save stores.  The game's option screens edit them in place too and keep
 *  copies only to undo a cancel, so an edit here is an OK there.  The sound
 *  layer is told at once, as phase 7's after step tells it every frame.
 * ------------------------------------------------------------------------ */

/* The game's two volumes run 0..19 (OptionGetSndVolume0/1). */
constexpr int kVolumeSteps = 19;
/* The brightness level runs 0..11 and starts at 6 (OptionInit). */
constexpr int kBrightnessLevels = 12;

OPTION_DAT &GameOptions()
{
    return GetOptionWrk()->now;
}

void SetGameVolume(signed char &volume, float value)
{
    volume = (signed char)std::clamp((int)std::lround(value), 0, kVolumeSteps);
    OptionApplySound();
}

void RestoreGameDefaults()
{
    OptionInit(GetOptionWrk());
    OptionApplySound();
}

/* What the three layouts of pad_key_cfg (system/pad/pad_ctrl.c) put on the
 * camera's buttons. */
std::string ButtonLayoutHelp()
{
    static const char *const kLayouts[] = {
        UiText("Type A: Triangle raises the Camera Obscura, Cross or R1 takes the "
               "picture and L1 a lens shot."),
        UiText("Type B: Triangle raises the Camera Obscura, Cross or R1 takes the "
               "picture and Circle a lens shot."),
        UiText("Type C: R1 raises the Camera Obscura, Cross takes the picture and "
               "Triangle or L1 a lens shot."),
    };
    const int layout = std::clamp((int)GameOptions().pad_cfg, 0, 2);
    return std::string(UiText("The game's three button layouts. ")) + kLayouts[layout];
}

Page GamePage()
{
    Page page{"game", "Game", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Toggle(
        "Use HD Fonts when available",
        Fixed("Use matching HD font textures when installed; otherwise use the original fonts."),
        [] { return reipan_config.renderer.hd_fonts != 0; },
        [](bool on) {
            reipan_config.renderer.hd_fonts = on ? 1 : 0;
            ReiPan_ConfigSave();
        }));
    rows.push_back(Toggle(
        "Vibration", Fixed("Let the controller shake when the game calls for it."),
        [] { return GameOptions().vibration != 0; },
        [](bool on) { GameOptions().vibration = on ? 1 : 0; }));
    rows.push_back(Toggle(
        "Subtitles", Fixed("Captions under the spoken lines of the cutscenes."),
        [] { return GameOptions().subtitle != 0; },
        [](bool on) { GameOptions().subtitle = on ? 1 : 0; }));
    rows.push_back(Slider(
        "Brightness",
        [] {
            return std::string(UiText(
                       "The game's own brightness filter over the world, in "
                       "twelve steps; it starts on 7. ")) +
                   (g_opt.from == REIPAN_RML_OPTIONS_FROM_PAUSE
                        ? UiText("It shows once play resumes.")
                        : UiText("It shows once play starts."));
        },
        0.0f, (float)(kBrightnessLevels - 1), 1.0f,
        [] { return (float)GameOptions().b00; },
        [](float value) {
            GameOptions().b00 = (u_char)std::clamp((int)std::lround(value), 0,
                                                   kBrightnessLevels - 1);
        },
        [](float value) { return Printf("%.0f", (double)value + 1.0); }));

    rows.push_back(Heading(""));
    rows.push_back(Action(
        "Restore defaults",
        Fixed("The game's own settings -- here and on the Audio and Controls "
              "pages -- back as the game starts them. Bindings are kept."),
        "Restore", [] {
            g_opt.confirm.Open("Restore the game's default settings?", "",
                               RestoreGameDefaults);
        }));
    return page;
}

Page AudioPage()
{
    Page page{"audio", "Audio", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Sound output", Fixed("Stereo, or both speakers carrying the same mix."),
        {"Stereo", "Mono"}, [] { return GameOptions().mono != 0 ? 1 : 0; },
        [](int mono) {
            GameOptions().mono = (u_char)mono;
            OptionApplySound();
        }));
    rows.push_back(Slider(
        "Sound effects", Fixed("The level of the game's sound effects."),
        0.0f, (float)kVolumeSteps, 1.0f,
        [] { return (float)GameOptions().vol_ch0; },
        [](float value) { SetGameVolume(GameOptions().vol_ch0, value); },
        Percent(100.0f / (float)kVolumeSteps)));
    rows.push_back(Slider(
        "BG music", Fixed("The level of the music."), 0.0f,
        (float)kVolumeSteps, 1.0f, [] { return (float)GameOptions().vol_ch1; },
        [](float value) { SetGameVolume(GameOptions().vol_ch1, value); },
        Percent(100.0f / (float)kVolumeSteps)));
    return page;
}

/* ---------------------------------------------------------------------------
 *  The port's settings -- the overlay's Settings menu.  The overlay's
 *  settle-and-save notices every renderer change and writes reipan.ini.
 * ------------------------------------------------------------------------ */

bool HdrActive()
{
    return ReiPan_RendererGetHdrActive() != 0;
}

void SetGrade(int which, float value)
{
    float grade[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ReiPan_RendererGetGrade(&grade[0], &grade[1], &grade[2], &grade[3]);
    grade[which] = value;
    ReiPan_RendererSetGrade(grade[0], grade[1], grade[2], grade[3]);
}

float GetGrade(int which)
{
    float grade[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ReiPan_RendererGetGrade(&grade[0], &grade[1], &grade[2], &grade[3]);
    return grade[which];
}

std::string HdrHelp()
{
    float white = 0.0f;
    float peak = 0.0f;
    ReiPan_RendererGetHdrDisplayInfo(&white, &peak);
    std::string text = HdrActive() ? UiText("Presenting in HDR.") : UiText("Presenting in SDR.");
    if (white > 0.0f)
    {
        char note[160];
        SDL_snprintf(note, sizeof(note),
                     UiText(" The display reports SDR white at %.0f nits and a "
                            "%.0f nit peak."),
                     (double)white, (double)peak);
        text += note;
    }
    if (HdrActive() && ReiPan_RendererHdrIsFullPrecision() == 0)
    {
        text += UiText(" The scene is 8-bit; restart with HDR on for 10-bit.");
    }
    return text;
}

Page DisplayPage()
{
    Page page{"display", "Display", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Window", Fixed("A window on the desktop, or the whole screen. Alt+Enter "
                        "switches too."),
        {"Windowed", "Borderless fullscreen"},
        [] { return ReiPan_RendererGetWindowMode(); },
        [](int mode) { ReiPan_RendererSetWindowMode(mode); }));
    rows.push_back(Choice(
        "Aspect ratio",
        Fixed("Auto fills the window and shows more of the world at the "
              "sides. Original keeps the PS2's 640x448 framing."),
        {"Auto", "Original (PS2)", "4:3", "16:9", "16:10", "Custom"},
        [] {
            int mode = REIPAN_ASPECT_AUTO;
            ReiPan_RendererGetAspectMode(&mode, nullptr);
            return mode;
        },
        [](int mode) {
            float custom = 16.0f / 9.0f;
            ReiPan_RendererGetAspectMode(nullptr, &custom);
            ReiPan_RendererSetAspectMode(mode, custom);
        }));
    rows.push_back(When(
        Slider("Custom ratio", Fixed("Width over height, for Custom."), 0.5f,
               5.0f, 0.05f,
               [] {
                   float custom = 16.0f / 9.0f;
                   ReiPan_RendererGetAspectMode(nullptr, &custom);
                   return custom;
               },
               [](float ratio) {
                   ReiPan_RendererSetAspectMode(REIPAN_ASPECT_CUSTOM, ratio);
               },
               Format("%.2f : 1")),
        [] {
            int mode = REIPAN_ASPECT_AUTO;
            ReiPan_RendererGetAspectMode(&mode, nullptr);
            return mode == REIPAN_ASPECT_CUSTOM;
        }));

    rows.push_back(Heading("Picture"));
    static const char *const kGradeNames[] = {"Brightness", "Contrast",
                                              "Gamma", "Saturation"};
    static const float kGradeRange[4][2] = {
        {0.25f, 2.0f}, {0.5f, 2.0f}, {0.5f, 2.5f}, {0.0f, 2.0f}};
    static const char *const kGradeHelp[] = {
        "Brightness of the finished picture, after the game's own filter.",
        "Spread between the darkest and lightest tones.",
        "Lifts or deepens the mid-tones without touching black or white.",
        "Strength of colour; 0 is black and white."};
    for (int i = 0; i < 4; i++)
    {
        rows.push_back(Slider(
            kGradeNames[i], Fixed(kGradeHelp[i]), kGradeRange[i][0],
            kGradeRange[i][1], 0.05f, [i] { return GetGrade(i); },
            [i](float value) { SetGrade(i, value); }, Format("%.2f")));
    }
    rows.push_back(Action(
        "Reset picture", Fixed("All four back to 1.00, which skips the pass."),
        "Reset", [] { ReiPan_RendererSetGrade(1.0f, 1.0f, 1.0f, 1.0f); }));

    rows.push_back(Heading("HDR"));
    rows.push_back(Choice(
        "HDR output", HdrHelp, {"Off", "Auto", "scRGB", "HDR10"},
        [] { return ReiPan_RendererGetHdrMode(); },
        [](int mode) { ReiPan_RendererSetHdrMode(mode); }));
    rows.push_back(When(
        Choice("Paper white",
               Fixed("How bright the game's white is in HDR: the system's SDR "
                     "white, or a level of your own."),
               {"Follow system", "Manual"},
               [] { return ReiPan_RendererGetHdrPaperWhite() > 0.0f ? 1 : 0; },
               [](int manual) {
                   float paper = 0.0f;
                   ReiPan_RendererGetHdrEffective(&paper, nullptr);
                   ReiPan_RendererSetHdrPaperWhite(
                       manual != 0 ? (paper > 0.0f ? paper : 200.0f) : 0.0f);
               }),
        HdrActive));
    rows.push_back(When(
        Slider("Paper white level", Fixed("Nits for the game's white."), 50.0f,
               1000.0f, 10.0f,
               [] {
                   float paper = 0.0f;
                   ReiPan_RendererGetHdrEffective(&paper, nullptr);
                   return paper;
               },
               [](float nits) { ReiPan_RendererSetHdrPaperWhite(nits); },
               Format("%.0f nits")),
        [] { return HdrActive() && ReiPan_RendererGetHdrPaperWhite() > 0.0f; }));
    rows.push_back(When(
        Choice("Peak brightness",
               Fixed("The brightest the picture may go: what the display "
                     "reports, or a level of your own."),
               {"Follow display", "Manual"},
               [] { return ReiPan_RendererGetHdrPeak() > 0.0f ? 1 : 0; },
               [](int manual) {
                   float peak = 0.0f;
                   ReiPan_RendererGetHdrEffective(nullptr, &peak);
                   ReiPan_RendererSetHdrPeak(
                       manual != 0 ? (peak > 0.0f ? peak : 1000.0f) : 0.0f);
               }),
        HdrActive));
    rows.push_back(When(
        Slider("Peak level", Fixed("Nits at the top of the range."), 100.0f,
               4000.0f, 50.0f,
               [] {
                   float peak = 0.0f;
                   ReiPan_RendererGetHdrEffective(nullptr, &peak);
                   return peak;
               },
               [](float nits) { ReiPan_RendererSetHdrPeak(nits); },
               Format("%.0f nits")),
        [] { return HdrActive() && ReiPan_RendererGetHdrPeak() > 0.0f; }));
    rows.push_back(When(
        Slider("Highlight expansion",
               Fixed("Stretches the brightest tones into the HDR headroom. "
                     "0 invents nothing -- the faithful setting."),
               0.0f, 1.0f, 0.05f,
               [] {
                   float strength = 0.0f;
                   float knee = 0.0f;
                   ReiPan_RendererGetHdrExpansion(&strength, &knee);
                   return strength;
               },
               [](float strength) {
                   float old_strength = 0.0f;
                   float knee = 0.0f;
                   ReiPan_RendererGetHdrExpansion(&old_strength, &knee);
                   ReiPan_RendererSetHdrExpansion(strength, knee);
               },
               Format("%.2f")),
        HdrActive));
    rows.push_back(When(
        Slider("Expansion knee", Fixed("Where in the range the stretch begins."),
               0.5f, 1.0f, 0.05f,
               [] {
                   float strength = 0.0f;
                   float knee = 0.0f;
                   ReiPan_RendererGetHdrExpansion(&strength, &knee);
                   return knee;
               },
               [](float knee) {
                   float strength = 0.0f;
                   float old_knee = 0.0f;
                   ReiPan_RendererGetHdrExpansion(&strength, &old_knee);
                   ReiPan_RendererSetHdrExpansion(strength, knee);
               },
               Format("%.2f")),
        HdrActive));
    return page;
}

int Log2(int value)
{
    int bits = 0;
    while (value > 1)
    {
        value >>= 1;
        bits++;
    }
    return bits;
}

bool Smoothing()
{
    return ReiPan_RendererGetExtraPresents() != 0;
}

void GetFinder(int *enable, float values[4])
{
    ReiPan_RendererGetFinderMask(enable, &values[0], &values[1], &values[2],
                                 &values[3]);
}

void SetFinderValue(int which, float value)
{
    int enable = 0;
    float values[4] = {};
    GetFinder(&enable, values);
    values[which] = value;
    ReiPan_RendererSetFinderMask(enable, values[0], values[1], values[2],
                                 values[3]);
}

bool FinderOn()
{
    int enable = 0;
    float values[4] = {};
    GetFinder(&enable, values);
    return enable != 0;
}

/* AUTO and the backends this platform has, in the order the row steps through
 * them. */
std::vector<int> Backends()
{
    std::vector<int> backends;
    for (int i = 0; i < REIPAN_BACKEND_COUNT; i++)
    {
        if (ReiPan_RendererBackendIsAvailable(i) != 0)
        {
            backends.push_back(i);
        }
    }
    return backends;
}

std::string BackendHelp()
{
    const auto asked = Translate(ReiPan_RendererGetBackendLabel(ReiPan_RendererGetBackend()));
    const auto active = Translate(ReiPan_RendererGetBackendLabel(ReiPan_RendererGetBackendActive()));
    char note[256];
    SDL_snprintf(note, sizeof(note),
                 UiText("Which graphics API draws the game. Auto suits most systems. Drawing with %s now."),
                 active.data());
    std::string text = note;
    switch (ReiPan_RendererGetBackendStatus())
    {
    case REIPAN_BACKEND_STATUS_RESTART:
        SDL_snprintf(note, sizeof(note), UiText(" Restart ReiPan to switch to %s."), asked.data());
        text += note;
        break;
    case REIPAN_BACKEND_STATUS_FALLBACK:
        SDL_snprintf(note, sizeof(note), UiText(" %s could not be started."), asked.data());
        text += note;
        break;
    case REIPAN_BACKEND_STATUS_OVERRIDDEN:
        text += UiText(" SDL_GPU_DRIVER is set and overrides this setting.");
        break;
    default:
        break;
    }
    return text;
}

Page GraphicsPage()
{
    Page page{"graphics", "Graphics", {}};
    std::vector<Row> &rows = page.rows;

    {
        const std::vector<int> backends = Backends();
        std::vector<std::string> names;
        for (int backend : backends)
        {
            names.push_back(ReiPan_RendererGetBackendLabel(backend));
        }
        rows.push_back(Choice(
            "Graphics API", BackendHelp, std::move(names),
            [backends] {
                /* Not in the list: a settings file from another system.  It
                 * runs as Auto here, so that is what the row says. */
                const auto at = std::find(backends.begin(), backends.end(),
                                          ReiPan_RendererGetBackend());
                return at != backends.end() ? (int)(at - backends.begin()) : 0;
            },
            [backends](int index) {
                if (index >= 0 && index < (int)backends.size())
                {
                    ReiPan_RendererSetBackend(backends[index]);
                }
            }));
    }
    rows.push_back(Choice(
        "Render resolution",
        Fixed("Match the window, the PS2's own 640x448 times a scale, or the "
              "window times a scale."),
        {"Match window", "Native PS2", "Window scale"},
        [] {
            int mode = REIPAN_RENDER_RES_MATCH_WINDOW;
            ReiPan_RendererGetRenderResolution(&mode, nullptr);
            return mode;
        },
        [](int mode) {
            float scale = 1.0f;
            ReiPan_RendererGetRenderResolution(nullptr, &scale);
            ReiPan_RendererSetRenderResolution(mode, scale);
        }));
    rows.push_back(When(
        Slider("Resolution scale",
               [] {
                   int width = 0;
                   int height = 0;
                   ReiPan_RendererGetRenderSize(&width, &height);
                   char text[96];
                   SDL_snprintf(text, sizeof(text),
                                UiText("The multiplier for the mode above. The game "
                                       "is drawn at %dx%d now."),
                                width, height);
                   return std::string(text);
               },
               0.25f, 4.0f, 0.25f,
               [] {
                   float scale = 1.0f;
                   ReiPan_RendererGetRenderResolution(nullptr, &scale);
                   return scale;
               },
               [](float scale) {
                   int mode = REIPAN_RENDER_RES_MATCH_WINDOW;
                   ReiPan_RendererGetRenderResolution(&mode, nullptr);
                   ReiPan_RendererSetRenderResolution(mode, scale);
               },
               Format("%.2fx")),
        [] {
            int mode = REIPAN_RENDER_RES_MATCH_WINDOW;
            ReiPan_RendererGetRenderResolution(&mode, nullptr);
            return mode != REIPAN_RENDER_RES_MATCH_WINDOW;
        }));
    rows.push_back(Choice(
        "Upscaling", Fixed("How a smaller picture is enlarged to the window."),
        {"Sharp", "Smooth"}, [] { return ReiPan_RendererGetUpscaleFilter(); },
        [](int filter) { ReiPan_RendererSetUpscaleFilter(filter); }));
    rows.push_back(Toggle(
        "Smooth sprites",
        Fixed("Bilinear filtering on text, sprites and effects. Off samples "
              "them as the PS2 did."),
        [] { return ReiPan_RendererGetSmooth2d() != 0; },
        [](bool on) { ReiPan_RendererSetSmooth2d(on ? 1 : 0); }));
    {
        Row msaa = Choice(
            "Anti-aliasing",
            Fixed("Multisampling smooths the edges of the world. Costly in "
                  "scenes full of effects, which read the frame back."),
            {"Off", "2x", "4x", "8x"},
            [] { return Log2(ReiPan_RendererGetMsaaSamples()); },
            [](int index) { ReiPan_RendererSetMsaaSamples(1 << index); });
        msaa.count_of = [] {
            return std::clamp(Log2(ReiPan_RendererGetMsaaMaxSamples()) + 1, 1, 4);
        };
        rows.push_back(std::move(msaa));
    }
    rows.push_back(Choice(
        "Texture filtering",
        Fixed("Anisotropic filtering keeps floors sharp at grazing angles. "
              "Close to free."),
        {"Off", "2x", "4x", "8x", "16x"},
        [] { return std::clamp(Log2(ReiPan_RendererGetAnisotropy()), 0, 4); },
        [](int index) { ReiPan_RendererSetAnisotropy(1 << index); }));

    rows.push_back(Heading("Motion"));
    {
        Row extra = Choice(
            "Extra frames",
            Fixed("Frames shown between the game's own, the camera moved to "
                  "each one's moment. The game still runs at its own pace."),
            {"Off", "1 extra", "2 extra", "3 extra"},
            [] { return ReiPan_RendererGetExtraPresents(); },
            [](int extra) { ReiPan_RendererSetExtraPresents(extra); });
        extra.count_of = [] {
            return std::clamp(ReiPan_RendererGetMaxExtraPresents() + 1, 1, 4);
        };
        rows.push_back(std::move(extra));
    }
    rows.push_back(When(
        Toggle("Interpolate camera",
               Fixed("Move the camera between the game's frames. Off repeats "
                     "the same picture."),
               [] { return ReiPan_RendererGetPresentInterpolation() != 0; },
               [](bool on) { ReiPan_RendererSetPresentInterpolation(on ? 1 : 0); }),
        Smoothing));
    rows.push_back(When(
        Toggle("Interpolate motion",
               Fixed("Move characters and objects between the game's frames "
                     "too, not only the camera."),
               [] { return ReiPan_RendererGetGeometryInterpolation() != 0; },
               [](bool on) { ReiPan_RendererSetGeometryInterpolation(on ? 1 : 0); }),
        [] { return Smoothing() && ReiPan_RendererGetPresentInterpolation() != 0; }));

    rows.push_back(Heading("Lighting and effects"));
    rows.push_back(Choice(
        "Lighting",
        Fixed("Vertex lights every vertex on the CPU, as the PS2 did. The "
              "per-pixel modes light spotlights, or everything, per pixel."),
        {"Vertex (PS2)", "Per-pixel spotlights", "Per-pixel, all lights"},
        [] { return ReiPan_RendererGetLightingMode(); },
        [](int mode) { ReiPan_RendererSetLightingMode(mode); }));
    rows.push_back(Choice(
        "Shadows", Fixed("Hard is the PS2's one-tap edge; soft averages it."),
        {"Hard (PS2)", "Soft"}, [] { return ReiPan_RendererGetShadowFilter(); },
        [](int filter) { ReiPan_RendererSetShadowFilter(filter); }));
    rows.push_back(Choice(
        "Film grain",
        Fixed("The grain over the picture: off, the PS2's sheet at its own "
              "size, or one grain per screen pixel."),
        {"Off", "PS2", "Native resolution"},
        [] { return ReiPan_RendererGetFilmGrain(); },
        [](int mode) { ReiPan_RendererSetFilmGrain(mode); }));
    rows.push_back(Slider(
        "Cut-out sharpening",
        Fixed("Restores the crisp alpha edge on foliage, fences and hair "
              "when the picture is drawn above the PS2's size."),
        0.0f, 1.0f, 0.05f, [] { return ReiPan_RendererGetAlphaSharpen(); },
        [](float value) { ReiPan_RendererSetAlphaSharpen(value); },
        Format("%.2f")));
    rows.push_back(Slider(
        "Cut-out threshold",
        Fixed("Discards any texel fainter than this. A blunter tool; 0 is "
              "off."),
        0.0f, 1.0f, 0.05f, [] { return ReiPan_RendererGetAlphaCutoff(); },
        [](float value) { ReiPan_RendererSetAlphaCutoff(value); },
        Format("%.2f")));

    rows.push_back(Heading("Viewfinder surround"));
    rows.push_back(Toggle(
        "Surround",
        Fixed("Treat the world beyond the PS2's 640x448 while the Camera "
              "Obscura is raised -- a port addition. Nothing to treat in a "
              "4:3 window."),
        FinderOn, [](bool on) {
            int enable = 0;
            float values[4] = {};
            GetFinder(&enable, values);
            ReiPan_RendererSetFinderMask(on ? 1 : 0, values[0], values[1],
                                         values[2], values[3]);
        }));
    static const char *const kFinderNames[] = {"Blur", "Darken", "Indigo",
                                               "Frame size"};
    static const char *const kFinderHelp[] = {
        "How far out of focus the surround falls.",
        "How dark it is; full with no blur is plain black.",
        "How far it drains toward ink indigo and a cold, moonlit blue-white.",
        "1.00 is the original frame; smaller covers part of the picture."};
    for (int i = 0; i < 4; i++)
    {
        const bool frame = i == 3;
        rows.push_back(When(
            Slider(kFinderNames[i], Fixed(kFinderHelp[i]),
                   frame ? 0.25f : 0.0f, frame ? 2.0f : 1.0f, 0.05f,
                   [i] {
                       int enable = 0;
                       float values[4] = {};
                       GetFinder(&enable, values);
                       return values[i];
                   },
                   [i](float value) { SetFinderValue(i, value); },
                   Format("%.2f")),
            FinderOn));
    }
    return page;
}

/* The mouse in play (io/reipan_game_mouse.h); the overlay's settle-and-save
 * writes reipan.ini. */
ReiPan_GameMouseSettings MouseSettings()
{
    ReiPan_GameMouseSettings s;
    ReiPan_GameMouseGetSettings(&s);
    return s;
}

void EditMouse(const std::function<void(ReiPan_GameMouseSettings &)> &edit)
{
    ReiPan_GameMouseSettings s = MouseSettings();
    edit(s);
    ReiPan_GameMouseSetSettings(&s);
}

/* A gamepad's gyro (io/reipan_gyro.h), saved the same way. */
ReiPan_GyroSettings GyroSettings()
{
    ReiPan_GyroSettings s;
    ReiPan_GyroGetSettings(&s);
    return s;
}

void EditGyro(const std::function<void(ReiPan_GyroSettings &)> &edit)
{
    ReiPan_GyroSettings s = GyroSettings();
    edit(s);
    ReiPan_GyroSetSettings(&s);
}

bool GyroOn()
{
    return GyroSettings().look != 0;
}

std::string GyroHelp()
{
    ReiPan_GyroStatus status;
    ReiPan_GyroGetStatus(&status);
    std::string text = UiText("Aim the raised Camera Obscura by moving the "
                              "controller, alongside the stick. ");
    if (status.has_gamepad == 0)
    {
        text += UiText("No controller is connected.");
    }
    else if (status.has_gyro == 0)
    {
        text += UiText("This controller has no gyro.");
    }
    else if (status.active != 0 && status.calibration < 1.0f)
    {
        text += UiText("Learning its drift: set it down and keep it still.");
    }
    else
    {
        text += UiText("Its drift is learned whenever it lies still.");
    }
    return text;
}

/* The controller's motors (io/reipan_haptics.h), saved the same way.  The
 * game's Vibration, on the Game page, is the switch for all of them. */
ReiPan_HapticsSettings RumbleSettings()
{
    ReiPan_HapticsSettings s;
    ReiPan_HapticsGetSettings(&s);
    return s;
}

void EditRumble(const std::function<void(ReiPan_HapticsSettings &)> &edit)
{
    ReiPan_HapticsSettings s = RumbleSettings();
    edit(s);
    ReiPan_HapticsSetSettings(&s);
}

bool RumbleOn()
{
    return GameOptions().vibration != 0;
}

bool TriggerRumbleOn()
{
    return RumbleOn() && RumbleSettings().triggers != 0;
}

std::string RumbleHelp()
{
    if (!RumbleOn())
    {
        return "Vibration is off on the Game page.";
    }
    return "How hard the controller shakes. 100% is as hard as the game asks.";
}

std::string TriggerRumbleHelp()
{
    ReiPan_HapticsStatus status;
    ReiPan_HapticsGetStatus(&status);
    std::string text = UiText("Feel heartbeats, blows, a ghost's grip, thunder and the "
                              "cutscenes in the triggers as well. ");
    if (!RumbleOn())
    {
        text += UiText("Vibration is off on the Game page.");
    }
    else if (status.has_gamepad == 0)
    {
        text += UiText("No controller is connected.");
    }
    else if (status.triggers == REIPAN_TRIGGERS_SEARCHING)
    {
        text += UiText("Looking for this controller's trigger motors: press a button "
                       "on it.");
    }
    else if (RumbleSettings().triggers == 0)
    {
        text += UiText("For a controller with motors behind its triggers: Xbox One "
                       "and later.");
    }
    else if (status.triggers == REIPAN_TRIGGERS_NONE)
    {
        text += UiText("This controller has no motors there.");
    }
    else
    {
        text += UiText("On for this controller.");
    }
    return text;
}

/* The game's Game Play and Controller screens (phases 22 and 23), and the
 * port's bindings. */
Page ControlsPage()
{
    Page page{"controls", "Controls", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Player controls",
        Fixed("3rd person: push where to go, as the camera sees it. 1st "
              "person: forward and back walk, left and right turn."),
        {"3rd person", "1st person"},
        [] { return GameOptions().b11 != 0 ? 1 : 0; },
        [](int value) { GameOptions().b11 = (u_char)value; }));
    rows.push_back(Choice(
        "Change film",
        Fixed("What changes the film with the Camera Obscura raised: the "
              "d-pad, or a stick. The other one aims."),
        {"D-pad", "Analog stick"}, [] { return GameOptions().b12 != 0 ? 1 : 0; },
        [](int value) { GameOptions().b12 = (u_char)value; }));
    rows.push_back(Choice(
        "Point of view", Fixed("Which way up and down turn the Camera Obscura."),
        {"Normal", "Reverse"}, [] { return GameOptions().b09 != 0 ? 1 : 0; },
        [](int value) { GameOptions().b09 = (u_char)value; }));
    rows.push_back(Choice(
        "Analog stick",
        Fixed("With the Camera Obscura raised: which stick aims it and which "
              "walks. Without it, the left stick always walks."),
        {"Left aims, right walks", "Right aims, left walks"},
        [] { return GameOptions().b13 != 0 ? 1 : 0; },
        [](int value) { GameOptions().b13 = (u_char)value; }));
    rows.push_back(Choice(
        "Button layout", ButtonLayoutHelp, {"Type A", "Type B", "Type C"},
        [] { return std::clamp((int)GameOptions().pad_cfg, 0, 2); },
        [](int value) { GameOptions().pad_cfg = (signed char)value; }));

    rows.push_back(Heading("Mouse"));
    rows.push_back(Toggle(
        "Mouse look",
        Fixed("The mouse turns the over-the-shoulder camera and aims the "
              "Camera Obscura. The pointer is captured while it does and free "
              "in every menu, and while the overlay's bar is up: F1 hides it."),
        [] { return MouseSettings().look != 0; },
        [](bool on) {
            EditMouse([on](ReiPan_GameMouseSettings &s) { s.look = on ? 1 : 0; });
        }));
    rows.push_back(When(
        Slider("Mouse sensitivity", Fixed("How far the mouse turns."), 0.25f,
               4.0f, 0.05f, [] { return MouseSettings().sensitivity; },
               [](float value) {
                   EditMouse([value](ReiPan_GameMouseSettings &s) {
                       s.sensitivity = value;
                   });
               },
               Format("%.2fx")),
        [] { return MouseSettings().look != 0; }));
    rows.push_back(Choice(
        "Camera Obscura button",
        Fixed("The mouse button that raises the Camera Obscura (right, unless "
              "the overlay's Settings > Controls > Mouse moves it): held to "
              "keep it up, or clicked once to raise it and again to lower it."),
        {"Hold", "Toggle"},
        [] { return MouseSettings().camera_toggle != 0 ? 1 : 0; },
        [](int value) {
            EditMouse([value](ReiPan_GameMouseSettings &s) {
                s.camera_toggle = value != 0 ? 1 : 0;
            });
        }));

    rows.push_back(Heading("Gyro"));
    rows.push_back(Toggle(
        "Gyro aiming", GyroHelp, GyroOn, [](bool on) {
            EditGyro([on](ReiPan_GyroSettings &s) { s.look = on ? 1 : 0; });
        }));
    /* Sensitivities span two orders of magnitude, so each step multiplies. */
    Row gyro_sensitivity = When(
        Slider("Gyro sensitivity",
               Fixed("1.00x turns the view as far as the controller turns."),
               REIPAN_GYRO_SENSITIVITY_MIN, REIPAN_GYRO_SENSITIVITY_MAX, 1.1f,
               [] { return GyroSettings().sensitivity; },
               [](float value) {
                   EditGyro([value](ReiPan_GyroSettings &s) {
                       s.sensitivity = value;
                   });
               },
               Format("%.2fx")),
        GyroOn);
    gyro_sensitivity.logarithmic = true;
    rows.push_back(std::move(gyro_sensitivity));
    rows.push_back(When(
        Choice("Turn with",
               Fixed("Yaw: point the controller left and right. Roll: tip it "
                     "sideways like a wheel."),
               {"Yaw", "Roll"}, [] { return GyroSettings().turn; },
               [](int value) {
                   EditGyro([value](ReiPan_GyroSettings &s) { s.turn = value; });
               }),
        GyroOn));
    rows.push_back(When(
        Toggle("Invert gyro X", Fixed("Swap left and right."),
               [] { return GyroSettings().invert_x != 0; },
               [](bool on) {
                   EditGyro([on](ReiPan_GyroSettings &s) {
                       s.invert_x = on ? 1 : 0;
                   });
               }),
        GyroOn));
    rows.push_back(When(
        Toggle("Invert gyro Y", Fixed("Swap up and down."),
               [] { return GyroSettings().invert_y != 0; },
               [](bool on) {
                   EditGyro([on](ReiPan_GyroSettings &s) {
                       s.invert_y = on ? 1 : 0;
                   });
               }),
        GyroOn));
    rows.push_back(When(
        Action("Recalibrate gyro",
               Fixed("Forget the learned drift; set the controller down and "
                     "keep it still to learn it again."),
               "Recalibrate", [] { ReiPan_GyroRecalibrate(); }),
        GyroOn));
    rows.push_back(When(
        Action("Reset gyro", Fixed("Gyro aiming back to its defaults."),
               "Reset", [] {
                   EditGyro([](ReiPan_GyroSettings &s) {
                       ReiPan_GyroDefaults(&s);
                   });
               }),
        [] { return ReiPan_GyroSettingsAreDefault() == 0; }));

    rows.push_back(Heading("Rumble"));
    rows.push_back(When(
        Slider("Rumble strength", RumbleHelp, 0.0f, 1.0f, 0.05f,
               [] { return RumbleSettings().strength; },
               [](float value) {
                   EditRumble([value](ReiPan_HapticsSettings &s) {
                       s.strength = value;
                   });
               },
               Percent(100.0f)),
        RumbleOn));
    rows.push_back(When(
        Toggle("Trigger rumble", TriggerRumbleHelp,
               [] { return RumbleSettings().triggers != 0; },
               [](bool on) {
                   EditRumble([on](ReiPan_HapticsSettings &s) {
                       s.triggers = on ? 1 : 0;
                   });
               }),
        RumbleOn));
    rows.push_back(When(
        Slider("Trigger strength",
               Fixed("The triggers alone, on top of the rumble strength."), 0.0f,
               1.0f, 0.05f, [] { return RumbleSettings().trigger_strength; },
               [](float value) {
                   EditRumble([value](ReiPan_HapticsSettings &s) {
                       s.trigger_strength = value;
                   });
               },
               Percent(100.0f)),
        TriggerRumbleOn));
    rows.push_back(When(
        Toggle("Camera Obscura rumble",
               Fixed("Feel the shutter, the charge and a shutter chance in the "
                     "triggers. The original does not shake for these."),
               [] { return RumbleSettings().camera != 0; },
               [](bool on) {
                   EditRumble([on](ReiPan_HapticsSettings &s) {
                       s.camera = on ? 1 : 0;
                   });
               }),
        TriggerRumbleOn));
    rows.push_back(When(
        Action("Test rumble",
               Fixed("Each motor in turn: the left grip, the right grip, and "
                     "with trigger rumble the left and right triggers."),
               "Test", [] { ReiPan_HapticsTest(); }),
        RumbleOn));
    rows.push_back(When(
        Action("Reset rumble", Fixed("The rumble settings above back to their "
                                     "defaults."),
               "Reset", [] {
                   EditRumble([](ReiPan_HapticsSettings &s) {
                       ReiPan_HapticsDefaults(&s);
                   });
               }),
        [] { return ReiPan_HapticsSettingsAreDefault() == 0; }));

    rows.push_back(Heading("Bindings"));
    Row reset = When(
        Action("Reset all bindings",
               Fixed("Every key, mouse button and controller button back to "
                     "the defaults. F1 opens the overlay, whose Settings > "
                     "Controls rebinds them one by one."),
               "Reset", [] {
                   g_opt.confirm.Open("Reset every binding to its default?", "",
                                      [] {
                                          ReiPan_InputResetBindings();
                                          ReiPanUi::NoteSettingsChanged();
                                      });
               }),
        [] { return ReiPan_InputBindingsAreDefault() == 0; });
    rows.push_back(std::move(reset));
    return page;
}

/* The over-the-shoulder camera (gameplay/reipan_over_shoulder.h).  Each row
 * reads and writes the camera's own settings; the overlay's settle-and-save
 * notices the change and writes reipan.ini. */
ReiPan_OverShoulderSettings CameraSettings()
{
    ReiPan_OverShoulderSettings s;
    ReiPan_OverShoulderGetSettings(&s);
    return s;
}

void EditCamera(const std::function<void(ReiPan_OverShoulderSettings &)> &edit)
{
    ReiPan_OverShoulderSettings s = CameraSettings();
    edit(s);
    ReiPan_OverShoulderSetSettings(&s);
}

bool CameraOn()
{
    return CameraSettings().enabled != 0;
}

Row CameraSlider(const char *label, const char *help, float min, float max,
                 float step, float ReiPan_OverShoulderSettings::*field,
                 const char *format)
{
    return When(Slider(label, Fixed(help), min, max, step,
                       [field] { return CameraSettings().*field; },
                       [field](float value) {
                           EditCamera([&](ReiPan_OverShoulderSettings &s) {
                               s.*field = value;
                           });
                       },
                       Format(format)),
                CameraOn);
}

Row CameraToggle(const char *label, const char *help,
                 int ReiPan_OverShoulderSettings::*field, const char *off,
                 const char *on)
{
    return When(Toggle(label, Fixed(help),
                       [field] { return CameraSettings().*field != 0; },
                       [field](bool value) {
                           EditCamera([&](ReiPan_OverShoulderSettings &s) {
                               s.*field = value ? 1 : 0;
                           });
                       },
                       off, on),
                CameraOn);
}

Page CameraPage()
{
    Page page{"camera", "Camera", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Camera view",
        Fixed("Fixed: the game's own camera angles. Over the shoulder: a "
              "camera behind and beside her that the right stick turns and R3 "
              "puts back behind her; the left stick walks the way it is "
              "pushed, as the camera sees it. Events, the Camera Obscura and "
              "ladders keep their own cameras."),
        {"Fixed", "Over the shoulder"},
        [] { return CameraOn() ? 1 : 0; },
        [](int value) {
            EditCamera([value](ReiPan_OverShoulderSettings &s) {
                s.enabled = value != 0 ? 1 : 0;
            });
        }));
    rows.push_back(CameraToggle(
        "Shoulder", "Which shoulder the camera looks over.",
        &ReiPan_OverShoulderSettings::side_left, "Right", "Left"));
    rows.push_back(CameraToggle(
        "Collision",
        "Pull in against the rooms and furniture as they are drawn: over a "
        "low table, short of a wall or a closed door.",
        &ReiPan_OverShoulderSettings::collision, "Off", "On"));
    rows.push_back(When(
        Choice("Scare camera",
               Fixed("When the game cuts to an event camera while you can "
                     "still walk -- a ghost appearing, something to look at, "
                     "the dream's graveyard path. Guided: stay over the "
                     "shoulder and turn once towards what the shot frames; "
                     "the right stick or mouse takes over at once. Original: "
                     "the game's own shot. Off: stay over the shoulder. "
                     "Moments that take control away always play their own "
                     "shot."),
               {"Guided", "Original", "Off"},
               [] { return CameraSettings().scare_camera; },
               [](int value) {
                   EditCamera([value](ReiPan_OverShoulderSettings &s) {
                       s.scare_camera = value;
                   });
               }),
        CameraOn));
    rows.push_back(CameraToggle(
        "Follow behind",
        "Drift back behind her while she walks and the right stick is idle.",
        &ReiPan_OverShoulderSettings::auto_follow, "Off", "On"));
    rows.push_back(CameraToggle(
        "Vertical look",
        "Which way up and down turn the camera, on top of the Controls page's "
        "Point of view.",
        &ReiPan_OverShoulderSettings::invert_y, "Normal", "Reverse"));

    rows.push_back(Heading("Framing"));
    rows.push_back(CameraSlider("Distance", "How far behind her the camera hangs.",
                                10.0f, 45.0f, 0.5f,
                                &ReiPan_OverShoulderSettings::distance, "%.1f"));
    rows.push_back(CameraSlider("Height", "How high above her feet it looks from.",
                                10.0f, 32.0f, 0.5f,
                                &ReiPan_OverShoulderSettings::height, "%.1f"));
    rows.push_back(CameraSlider("Shoulder offset", "How far to the side of her it sits.",
                                0.0f, 12.0f, 0.25f,
                                &ReiPan_OverShoulderSettings::side, "%.2f"));
    rows.push_back(CameraSlider("Field of view", "Vertical, in degrees.",
                                35.0f, 80.0f, 1.0f,
                                &ReiPan_OverShoulderSettings::fov_deg, "%.0f"));
    rows.push_back(CameraSlider("Look speed", "How fast the right stick turns it.",
                                0.25f, 3.0f, 0.05f,
                                &ReiPan_OverShoulderSettings::sensitivity, "%.2fx"));
    rows.push_back(CameraSlider("Smoothing", "How far the camera lags her movement.",
                                0.0f, 0.8f, 0.05f,
                                &ReiPan_OverShoulderSettings::smoothing, "%.2f"));

    rows.push_back(Heading(""));
    rows.push_back(When(
        Action("Reset camera", Fixed("The framing and options above back to "
                                     "their defaults; the camera stays on."),
               "Reset", [] {
                   ReiPan_OverShoulderSettings d;
                   ReiPan_OverShoulderDefaults(&d);
                   d.enabled = 1;
                   ReiPan_OverShoulderSetSettings(&d);
               }),
        CameraOn));
    return page;
}

/* ---------------------------------------------------------------------------
 *  Building the document.
 * ------------------------------------------------------------------------ */

std::string RowId(int page, int row)
{
    return "row-" + std::to_string(page) + "-" + std::to_string(row);
}

std::string RowRml(const Row &row, int page, int index)
{
    using ReiPanRmlCommon::Escape;
    const std::string id = RowId(page, index);
    if (row.kind == RowKind::Heading)
    {
        return "<div class=\"heading\" id=\"" + id + "\">" + Escape(Translate(row.label)) +
               "</div>";
    }

    const char *kind = row.kind == RowKind::Choice   ? "choice"
                       : row.kind == RowKind::Slider ? "slider"
                                                     : "action";
    std::string rml = "<div class=\"row selectable " + std::string(kind) +
                      "\" id=\"" + id + "\"><span class=\"row-label\">" +
                      Escape(Translate(row.label)) + "</span><span class=\"row-value\">";
    if (row.kind == RowKind::Action)
    {
        rml += "<span class=\"value action-value\" id=\"" + id + "-value\">" +
               Escape(Translate(row.action)) + "</span>";
    }
    else
    {
        rml += "<span class=\"arrow left\">" + std::string(kArrowLeft) + "</span>";
        if (row.kind == RowKind::Slider)
        {
            rml += "<span class=\"track\" id=\"" + id +
                   "-track\"><span class=\"fill\" id=\"" + id +
                   "-fill\"></span></span>";
        }
        rml += "<span class=\"value\" id=\"" + id + "-value\"></span>";
        rml += "<span class=\"arrow right\">" + std::string(kArrowRight) + "</span>";
    }
    rml += "</span></div>";
    return rml;
}

void BuildPages()
{
    g_opt.pages.clear();
    g_opt.pages.push_back(GamePage());
    g_opt.pages.push_back(DisplayPage());
    g_opt.pages.push_back(GraphicsPage());
    g_opt.pages.push_back(AudioPage());
    g_opt.pages.push_back(ControlsPage());
    g_opt.pages.push_back(CameraPage());

    using ReiPanRmlCommon::Escape;
    std::string tabs;
    std::string pages;
    for (int p = 0; p < (int)g_opt.pages.size(); p++)
    {
        const Page &page = g_opt.pages[p];
        tabs += "<span class=\"tab\" id=\"tab-" + std::to_string(p) + "\">" +
                Escape(Translate(page.name)) + "</span>";
        pages += "<div class=\"page hidden\" id=\"page-" + std::string(page.id) +
                 "\">";
        for (int r = 0; r < (int)page.rows.size(); r++)
        {
            pages += RowRml(page.rows[r], p, r);
        }
        pages += "</div>";
    }
    g_opt.tabs->SetInnerRML(tabs);
    g_opt.pages_root->SetInnerRML(pages);

    g_opt.row_of.clear();
    for (int p = 0; p < (int)g_opt.pages.size(); p++)
    {
        Page &page = g_opt.pages[p];
        page.element = g_opt.document->GetElementById(std::string("page-") + page.id);
        page.tab = g_opt.document->GetElementById("tab-" + std::to_string(p));
        page.selected = -1;
        for (int r = 0; r < (int)page.rows.size(); r++)
        {
            Row &row = page.rows[r];
            const std::string id = RowId(p, r);
            row.element = g_opt.document->GetElementById(id);
            row.value = g_opt.document->GetElementById(id + "-value");
            row.track = g_opt.document->GetElementById(id + "-track");
            row.fill = g_opt.document->GetElementById(id + "-fill");
            row.shown_value.clear();
            row.shown_fill = -1.0f;
            row.shown_enabled = -1;
            if (row.element != nullptr)
            {
                g_opt.row_of[row.element] = {p, r};
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 *  Values.
 * ------------------------------------------------------------------------ */

float SliderFraction(const Row &row, float value)
{
    if (row.logarithmic)
    {
        return (std::log(value) - std::log(row.min)) /
               (std::log(row.max) - std::log(row.min));
    }
    return (value - row.min) / (row.max - row.min);
}

float SliderFromFraction(const Row &row, float fraction)
{
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (row.logarithmic)
    {
        return std::exp(std::log(row.min) +
                        fraction * (std::log(row.max) - std::log(row.min)));
    }
    const float value = row.min + fraction * (row.max - row.min);
    return row.min + std::round((value - row.min) / row.step) * row.step;
}

void Changed(const Row &row)
{
    if (row.input_setting)
    {
        ReiPanUi::NoteSettingsChanged();
    }
}

void SetSlider(Row &row, float value)
{
    value = std::clamp(value, row.min, row.max);
    if (value != row.get_value())
    {
        row.set_value(value);
        Changed(row);
    }
}

/* Left (-1) or right (+1).  Choices go round; sliders stop at their ends,
 * and the tick only sounds when something moved. */
void Adjust(Row &row, int direction)
{
    if (!row.IsEnabled())
    {
        return;
    }
    if (row.kind == RowKind::Choice)
    {
        const int count = row.Count();
        if (count <= 1)
        {
            return;
        }
        const int current = std::clamp(row.get(), 0, count - 1);
        row.set((current + direction + count) % count);
        Changed(row);
        ReiPanRmlCommon::PlayCursor();
    }
    else if (row.kind == RowKind::Slider)
    {
        const float before = row.get_value();
        float value;
        if (row.logarithmic)
        {
            value = before * std::pow(row.step, (float)direction);
        }
        else
        {
            /* On the step grid, so repeated steps do not drift. */
            const float steps = std::round((before - row.min) / row.step);
            value = row.min + (steps + (float)direction) * row.step;
        }
        SetSlider(row, value);
        if (row.get_value() != before)
        {
            ReiPanRmlCommon::PlayCursor();
        }
    }
}

void Activate(Row &row)
{
    if (!row.IsEnabled())
    {
        return;
    }
    if (row.kind == RowKind::Choice)
    {
        Adjust(row, 1);
    }
    else if (row.kind == RowKind::Action)
    {
        ReiPanRmlCommon::PlayDecide();
        row.run();
        Changed(row);
    }
}

std::string ValueText(const Row &row)
{
    switch (row.kind)
    {
    case RowKind::Choice:
        return row.NameOf(std::clamp(row.get(), 0, std::max(row.Count() - 1, 0)));
    case RowKind::Slider:
        return row.format(row.get_value());
    case RowKind::Action:
        return row.action;
    default:
        return "";
    }
}

void RefreshRow(Row &row)
{
    if (row.element == nullptr || row.kind == RowKind::Heading)
    {
        return;
    }
    const int enabled = row.IsEnabled() ? 1 : 0;
    if (enabled != row.shown_enabled)
    {
        row.element->SetClass("disabled", enabled == 0);
        row.shown_enabled = enabled;
    }
    const std::string text = ValueText(row);
    if (row.value != nullptr && text != row.shown_value)
    {
        row.value->SetInnerRML(ReiPanRmlCommon::Escape(Translate(text)));
        row.shown_value = text;
    }
    if (row.fill != nullptr)
    {
        const float fraction =
            std::clamp(SliderFraction(row, row.get_value()), 0.0f, 1.0f);
        if (std::fabs(fraction - row.shown_fill) > 0.001f)
        {
            row.fill->SetProperty("width", Printf("%.2f%%", fraction * 100.0));
            row.shown_fill = fraction;
        }
    }
}

/* ---------------------------------------------------------------------------
 *  The cursor and the sections.
 * ------------------------------------------------------------------------ */

Page &CurrentPage()
{
    return g_opt.pages[g_opt.page];
}

Row *SelectedRow()
{
    Page &page = CurrentPage();
    if (page.selected < 0 || page.selected >= (int)page.rows.size())
    {
        return nullptr;
    }
    return &page.rows[page.selected];
}

void Select(int index, bool scroll)
{
    Page &page = CurrentPage();
    if (page.selected >= 0 && page.selected < (int)page.rows.size() &&
        page.rows[page.selected].element != nullptr)
    {
        page.rows[page.selected].element->SetClass("selected", false);
    }
    page.selected = index;
    if (index >= 0 && index < (int)page.rows.size() &&
        page.rows[index].element != nullptr)
    {
        Rml::Element *element = page.rows[index].element;
        element->SetClass("selected", true);
        if (scroll)
        {
            /* The heading above a section's first row comes into view with
             * it, so the cursor never sits under an unlabelled group. */
            Rml::Element *target = element;
            if (index > 0 && page.rows[index - 1].kind == RowKind::Heading &&
                page.rows[index - 1].element != nullptr)
            {
                target = page.rows[index - 1].element;
            }
            target->ScrollIntoView(Rml::ScrollAlignment::Nearest);
            element->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        }
    }
}

int NextSelectable(const Page &page, int from, int direction)
{
    for (int i = from + direction; i >= 0 && i < (int)page.rows.size();
         i += direction)
    {
        if (page.rows[i].Selectable())
        {
            return i;
        }
    }
    return -1;
}

void MoveCursor(int direction)
{
    Page &page = CurrentPage();
    const int next = NextSelectable(page, page.selected, direction);
    if (next >= 0)
    {
        Select(next, true);
        ReiPanRmlCommon::PlayCursor();
    }
}

void ShowPage(int index, bool sound)
{
    const int count = (int)g_opt.pages.size();
    index = (index % count + count) % count;
    for (int p = 0; p < count; p++)
    {
        Page &page = g_opt.pages[p];
        if (page.element != nullptr)
        {
            page.element->SetClass("hidden", p != index);
        }
        if (page.tab != nullptr)
        {
            page.tab->SetClass("active", p == index);
        }
    }
    g_opt.page = index;
    Page &page = CurrentPage();
    for (Row &row : page.rows)
    {
        RefreshRow(row);
    }
    int selected = page.selected;
    if (selected < 0 || !page.rows[selected].Selectable())
    {
        selected = NextSelectable(page, -1, 1);
    }
    Select(selected, true);
    if (sound)
    {
        ReiPanRmlCommon::PlayCursor();
    }
}

void CloseByPlayer()
{
    ReiPanRmlCommon::PlayCancel();
    ReiPan_RmlOptionsClose();
}

/* ---------------------------------------------------------------------------
 *  Input.
 * ------------------------------------------------------------------------ */

void HandleKey(Rml::Event &event)
{
    const int key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool shift = event.GetParameter<int>("shift_key", 0) != 0;
    if (g_opt.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }

    Row *row = SelectedRow();
    switch (key)
    {
    case Rml::Input::KI_UP: MoveCursor(-1); break;
    case Rml::Input::KI_DOWN: MoveCursor(1); break;
    case Rml::Input::KI_LEFT:
        if (row != nullptr)
        {
            Adjust(*row, -1);
        }
        break;
    case Rml::Input::KI_RIGHT:
        if (row != nullptr)
        {
            Adjust(*row, 1);
        }
        break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE:
        if (row != nullptr)
        {
            Activate(*row);
        }
        break;
    case Rml::Input::KI_HOME:
        Select(NextSelectable(CurrentPage(), -1, 1), true);
        break;
    case Rml::Input::KI_END:
        Select(NextSelectable(CurrentPage(), (int)CurrentPage().rows.size(), -1),
               true);
        break;
    case Rml::Input::KI_PRIOR:
    case Rml::Input::KI_Q: ShowPage(g_opt.page - 1, true); break;
    case Rml::Input::KI_NEXT:
    case Rml::Input::KI_E: ShowPage(g_opt.page + 1, true); break;
    /* Tab, and SELECT with it. */
    case Rml::Input::KI_TAB: ShowPage(g_opt.page + (shift ? -1 : 1), true); break;
    case Rml::Input::KI_ESCAPE: CloseByPlayer(); break;
    default: return;
    }
    event.StopPropagation();
}

/* The row an event happened in, walking up from its target. */
Row *RowAt(Rml::Element *element, RowRef *ref_out)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        auto found = g_opt.row_of.find(element);
        if (found != g_opt.row_of.end())
        {
            if (ref_out != nullptr)
            {
                *ref_out = found->second;
            }
            return &g_opt.pages[found->second.page].rows[found->second.row];
        }
    }
    return nullptr;
}

bool HasAncestorClass(Rml::Element *element, const char *name, Rml::Element **found)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->IsClassSet(name))
        {
            if (found != nullptr)
            {
                *found = element;
            }
            return true;
        }
    }
    return false;
}

void DragTo(Row &row, float mouse_x)
{
    if (row.track == nullptr)
    {
        return;
    }
    const float left = row.track->GetAbsoluteOffset(Rml::BoxArea::Border).x;
    const float width = row.track->GetBox().GetSize(Rml::BoxArea::Border).x;
    if (width <= 0.0f)
    {
        return;
    }
    SetSlider(row, SliderFromFraction(row, (mouse_x - left) / width));
}

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    const float mouse_x = event.GetParameter<float>("mouse_x", 0.0f);

    if (event == Rml::EventId::Mouseup)
    {
        g_opt.dragging = {-1, -1};
        return;
    }
    if (event == Rml::EventId::Mousemove)
    {
        if (g_opt.dragging.page == g_opt.page && g_opt.dragging.row >= 0)
        {
            DragTo(CurrentPage().rows[g_opt.dragging.row], mouse_x);
        }
        return;
    }
    if (g_opt.confirm.IsOpen())
    {
        return;
    }

    RowRef ref{-1, -1};
    Row *row = RowAt(target, &ref);
    if (event == Rml::EventId::Mouseover)
    {
        if (ReiPanRml::PointerActive() && row != nullptr &&
            ref.page == g_opt.page && row->Selectable() &&
            ref.row != CurrentPage().selected)
        {
            Select(ref.row, false);
        }
        return;
    }
    if (event == Rml::EventId::Mousedown)
    {
        if (row != nullptr && row->kind == RowKind::Slider && row->IsEnabled() &&
            HasAncestorClass(target, "track", nullptr))
        {
            g_opt.dragging = ref;
            DragTo(*row, mouse_x);
        }
        return;
    }

    /* Click. */
    Rml::Element *tagged = nullptr;
    if (HasAncestorClass(target, "tab", &tagged))
    {
        const int index = std::atoi(tagged->GetId().c_str() + 4);
        if (index != g_opt.page)
        {
            ShowPage(index, true);
        }
        return;
    }
    if (HasAncestorClass(target, "prompt", &tagged))
    {
        const Rml::String &id = tagged->GetId();
        if (id == "prompt-back")
        {
            CloseByPlayer();
        }
        else if (id == "prompt-next")
        {
            ShowPage(g_opt.page + 1, true);
        }
        return;
    }
    if (row == nullptr || ref.page != g_opt.page)
    {
        return;
    }
    if (HasAncestorClass(target, "arrow", &tagged))
    {
        Adjust(*row, tagged->IsClassSet("left") ? -1 : 1);
    }
    else if (row->kind != RowKind::Slider)
    {
        Activate(*row);
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_opt.open)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
    }
    else
    {
        HandlePointer(event);
    }
}

void RefreshHelpAndPrompts()
{
    std::string help;
    if (const Row *row = SelectedRow(); row != nullptr && !g_opt.confirm.IsOpen())
    {
        help = row->help ? std::string(Translate(row->help())) : "";
        if (!row->IsEnabled() && !help.empty())
        {
            help += UiText(" (Not available with the current settings.)");
        }
    }
    if (help != g_opt.shown_help && g_opt.help != nullptr)
    {
        g_opt.help->SetInnerRML(ReiPanRmlCommon::Escape(help));
        g_opt.shown_help = help;
    }

    static const ReiPanRmlCommon::Prompt kPrompts[] = {
        {"Q / E", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Section", "prompt-next",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts = ReiPanRmlCommon::PromptsRml(kPrompts, 3);
    if (prompts != g_opt.shown_prompts && g_opt.prompts != nullptr)
    {
        g_opt.prompts->SetInnerRML(prompts);
        g_opt.shown_prompts = prompts;
    }
}
}

namespace ReiPanRmlOptions
{
bool Init(Rml::Context *context)
{
    g_opt.context = context;
    if (context == nullptr)
    {
        return false;
    }
    g_opt.document = ReiPanRml::LoadDocument(context, kDocumentPath);
    if (g_opt.document == nullptr)
    {
        SDL_Log("ReiPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_opt.tabs = g_opt.document->GetElementById("tabs");
    g_opt.pages_root = g_opt.document->GetElementById("pages");
    g_opt.help = g_opt.document->GetElementById("help");
    g_opt.prompts = g_opt.document->GetElementById("prompts");
    if (g_opt.tabs == nullptr || g_opt.pages_root == nullptr)
    {
        SDL_Log("ReiPan RmlUi: %s is missing #tabs or #pages", kDocumentPath);
        g_opt.document->Close();
        g_opt.document = nullptr;
        return false;
    }
    g_opt.confirm.Attach(g_opt.document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousedown,
                            Rml::EventId::Mousemove, Rml::EventId::Mouseup})
    {
        g_opt.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_opt.confirm.Detach();
    /* The context owns the document and takes it down with it. */
    g_opt = OptionsState();
}

void Frame()
{
    if (!g_opt.open)
    {
        return;
    }

    /* Every frame, because a setting can move under the screen -- Alt+Enter,
     * the overlay -- and one that enables another greys or frees it. */
    Page &page = CurrentPage();
    for (Row &row : page.rows)
    {
        RefreshRow(row);
    }
    if (const Row *row = SelectedRow(); row == nullptr || !row->Selectable())
    {
        const int next = NextSelectable(page, page.selected, 1);
        Select(next >= 0 ? next : NextSelectable(page, page.selected, -1), true);
    }
    RefreshHelpAndPrompts();
}
}

extern "C" {

int ReiPan_RmlOptionsOpen(int from)
{
    if (g_opt.document == nullptr)
    {
        return 0;
    }
    g_opt.from = from;
    g_opt.dragging = {-1, -1};
    g_opt.shown_help.clear();
    g_opt.shown_prompts.clear();
    g_opt.confirm.Close();
    /* Rebuilt on every open: what the pages hold depends on where the screen
     * was opened from, and on the hardware. */
    BuildPages();
    g_opt.open = true;
    g_opt.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
    ShowPage(0, false);
    RefreshHelpAndPrompts();
    ReiPanRmlCommon::PlayDecide();
    SDL_Log("ReiPan RmlUi: options opened from %s",
            from == REIPAN_RML_OPTIONS_FROM_TITLE ? "title" : "pause");
    return 1;
}

void ReiPan_RmlOptionsClose(void)
{
    if (g_opt.document != nullptr)
    {
        g_opt.confirm.Close();
        g_opt.document->Hide();
    }
    g_opt.open = false;
    g_opt.dragging = {-1, -1};
}

int ReiPan_RmlOptionsIsOpen(void)
{
    return g_opt.open ? 1 : 0;
}
}
