#include "reipan_config.h"

#include "io/reipan_file.h"
#include "io/reipan_input.h"
#include "io/reipan_paths.h"
#include "rendering/reipan_renderer.h"
#include "gameplay/reipan_over_shoulder.h"
#include "io/reipan_game_mouse.h"
#include "io/reipan_gyro.h"
#include "io/reipan_haptics.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Defaults.  The window size is the one EnsureRenderer() used before this
 * existed -- twice the ROM's 640x448 -- so a first run with no file behaves
 * exactly as the port did before settings were persisted at all. */
ReiPan_Config reipan_config = {
    /* .renderer = */ {
        /* .window_mode       = */ REIPAN_WINDOW_MODE_WINDOWED,
        /* .window_width      = */ 1280,
        /* .window_height     = */ 896,
        /* .backend           = */ REIPAN_BACKEND_AUTO,
        /* .render_mode       = */ REIPAN_RENDER_RES_MATCH_WINDOW,
        /* .render_scale      = */ 1.0f,
        /* .upscale_filter    = */ REIPAN_RENDER_FILTER_LINEAR,
        /* .hd_fonts          = */ 1,
        /* .smooth_2d         = */ 1,
        /* Both off by default, and for the same reason HDR is: they are
         * additions rather than reconstructions, and each has a cost the port
         * should not spend on a player's behalf.  MSAA in particular is not
         * the near-free 4x of a single-pass forward renderer here -- this game
         * samples the frame buffer for its effects, and every one of those
         * samples now resolves the multisample target.  See the blocks above
         * ReiPan_RendererSetMsaaSamples() and ...SetAnisotropy() in
         * rendering/reipan_renderer.h. */
        /* .msaa_samples      = */ 1,
        /* .anisotropy        = */ 1,
        /* .aspect_mode       = */ REIPAN_ASPECT_AUTO,
        /* .aspect_ratio      = */ 16.0f / 9.0f,
        /* .lighting_mode     = */ REIPAN_LIGHTING_VERTEX,
        /* .shadow_filter     = */ REIPAN_SHADOW_FILTER_SOFT,
        /* .extra_presents    = */ 0,
        /* .interpolate_presents = */ 1,
        /* .interpolate_geometry = */ 1,
        /* HDR is off by default and deliberately so: it is the one setting
         * here that cannot be judged from the code, only from the panel in
         * front of the player, and a first run that silently re-grades the
         * picture is worse than one that asks. */
        /* .hdr_mode          = */ REIPAN_HDR_OFF,
        /* .hdr_paper_white   = */ 200.0f,
        /* .hdr_peak          = */ 0.0f,
        /* .hdr_expansion     = */ 0.35f,
        /* .hdr_expansion_knee = */ 0.75f,
        /* .grade_brightness  = */ 1.0f,
        /* .grade_contrast    = */ 1.0f,
        /* .grade_gamma       = */ 1.0f,
        /* .grade_saturation  = */ 1.0f,
        /* Native by default.  The grain itself is the ROM's -- it is part of
         * how the game was photographed, and turning it off is the deviation --
         * but its 128x128 sheet was drawn one texel to a PS2 pixel, and there
         * is no reading of that which survives a 1080p window: either the
         * grain magnifies into dots or it stops being one texel per pixel.
         * NATIVE keeps the density and gives up only the sheet's size, which
         * is the part of the original that was a memory budget rather than a
         * look.  PS2 is here for anyone who wants the dots. */
        /* .film_grain        = */ REIPAN_FILM_GRAIN_NATIVE,
        /* On by default.  It is an addition rather than a reconstruction, so
         * the first instinct was to ship it off the way HDR is -- but HDR is a
         * property of the display and this is a property of the viewfinder,
         * and something that only ever appears while the camera is raised is
         * not a thing a player should have to go and find a menu for.  It also
         * does nothing at all on a 4:3 window, where there is no world outside
         * the frame to treat. */
        /* .finder_mask       = */ 1,
        /* .finder_mask_blur  = */ 0.75f,
        /* .finder_mask_darken = */ 0.45f,
        /* .finder_mask_tint  = */ 0.55f,
        /* .finder_mask_scale = */ 1.0f,
        /* Off by default: a deliberate deviation from the GS, so a
         * player who has not asked for it gets the hardware's own
         * edges.  0.75 is a good starting point at 3x native. */
        /* .alpha_sharpen     = */ 0.0f,
        /* Off for the same reason, and it is the blunter of the two:
         * it aliases the edge and can erase fine geometry. */
        /* .alpha_cutoff      = */ 0.0f,
        /* On: it changes where the vertices live, not what is drawn, and
         * off is kept only as the A/B. */
        /* .resident_meshes   = */ 1,
        /* On, for the same reason: the textures are the ones the GS path
         * resolves, only no longer re-sent. */
        /* .resident_textures = */ 1,
    },
    /* .paths = */ {},
    /* .title = */ {
        /* Room: it is what the setting exists for, and the logo, the menu and
         * every screen behind them are the original's either way. */
        /* .backdrop          = */ REIPAN_TITLE_BACKDROP_ROOM,
        /* .change            = */ REIPAN_TITLE_CHANGE_PASS,
        /* .passes            = */ 1,
        /* .dim               = */ 0.2f,
        /* 0: the game's own title music, played the way the ROM plays it. */
        /* .music             = */ 0,
        /* .music_volume      = */ 1.0f,
        /* Off: the room is the thing to look at while the title sits idle,
         * and the movie would take it away every thirty seconds. */
        /* .attract_movie     = */ 0,
        /* The Opening room below; ConfigLoadOnce() appends the rest of the
         * default rotation (kTitleDefaultRooms) before reading the file. */
        /* .scene_count       = */ 1,
        /* .scenes            = */ {
            {
                /* RS106, where a new game puts Rei, seen from above the
                 * wall: the frame the user picked from the running title,
                 * 0.6 of the way from the map camera's shot of the room at
                 * the start of play to its high opening shot.  B is 0.66 of
                 * that way, so the camera rises a few units over the travel
                 * and settles back without the framing changing. */
                /* .name              = */ "Opening room",
                /* .enabled           = */ 1,
                /* .room              = */ 6,
                /* .side              = */ 0,
                /* .camera_set        = */ 1,
                /* .a_eye             = */ { -69.6f, -113.7f, 526.3f },
                /* .a_target          = */ { -86.4f, -117.2f, 585.4f },
                /* .a_fov             = */ 46.1f,
                /* .b_eye             = */ { -72.7f, -122.1f, 526.5f },
                /* .b_target          = */ { -91.3f, -126.1f, 585.2f },
                /* .b_fov             = */ 45.5f,
                /* .travel_seconds    = */ 30.0f,
                /* .hold_seconds      = */ 4.0f,
                /* .motion            = */ REIPAN_TITLE_MOTION_PINGPONG,
                /* .easing            = */ REIPAN_TITLE_EASING_SMOOTH,
            },
        },
    },
    /* .profiler_summary = */ 0,
    /* .profiler_fps_only = */ 0,
    /* .camera = */ {
        /* Off: the fixed angles are the game as it was made. */
        /* .over_shoulder     = */ 0,
        /* .side_left         = */ 0,
        /* .collision         = */ 1,
        /* .auto_follow       = */ 1,
        /* .invert_y          = */ 0,
        /* .scare_camera      = */ REIPAN_OTS_SCARE_GUIDED,
        /* The framing the first play-test settled on. */
        /* .distance          = */ 25.0f,
        /* .height            = */ 26.0f,
        /* .side              = */ 3.5f,
        /* .fov               = */ 60.0f,
        /* .sensitivity       = */ 1.0f,
        /* .smoothing         = */ 0.3f,
    },
    /* .mouse = */ {
        /* MikuPan's: look on, right raises the Camera Obscura while held,
         * left the shutter, middle the lens shot, the wheel the film. */
        /* .look              = */ 1,
        /* .sensitivity       = */ 1.0f,
        /* .camera_toggle     = */ 0,
        /* .bind              = */ { REIPAN_MOUSE_SRC_RIGHT, REIPAN_MOUSE_SRC_LEFT,
                                     REIPAN_MOUSE_SRC_MIDDLE, REIPAN_MOUSE_SRC_WHEEL_UP,
                                     REIPAN_MOUSE_SRC_WHEEL_DOWN },
    },
    /* .gyro = */ {
        /* MioPan's: on, for a pad that has one. */
        /* .look              = */ 1,
        /* .sensitivity       = */ REIPAN_GYRO_SENSITIVITY_DEFAULT,
        /* .turn              = */ REIPAN_GYRO_TURN_YAW,
        /* .invert_x          = */ 0,
        /* .invert_y          = */ 0,
    },
    /* .rumble = */ {
        /* As hard as the game asks, with the triggers of a pad that has motors
         * there; the Camera Obscura's are not the original's. */
        /* .strength          = */ 1.0f,
        /* .triggers          = */ 1,
        /* .trigger_strength  = */ 1.0f,
        /* .camera            = */ 0,
    },
#ifdef REIPAN_SLPS_25544_CHS
    /* .overlay_menu_bar = */ 0,
#else
    /* Up on a first run, so the settings are there to be found. */
    /* .overlay_menu_bar = */ 1,
#endif
};

namespace
{

/* ------------------------------------------------------------------------
 *  Enumerated settings are stored by name so the file can be edited by hand.
 *  Each table is terminated by a null label and every lookup falls back to
 *  strtol, so a file written by an older build -- or by someone who typed the
 *  number -- still loads.
 * --------------------------------------------------------------------- */

struct EnumName
{
    const char *label;
    int value;
};

const EnumName kWindowModes[] = {
    {"windowed", REIPAN_WINDOW_MODE_WINDOWED},
    {"borderless", REIPAN_WINDOW_MODE_BORDERLESS},
    /* Accepted on read, never written: what someone would type by hand. */
    {"fullscreen", REIPAN_WINDOW_MODE_BORDERLESS},
    {nullptr, 0},
};

const EnumName kBackends[] = {
    {"auto", REIPAN_BACKEND_AUTO},
    {"direct3d12", REIPAN_BACKEND_D3D12},
    {"vulkan", REIPAN_BACKEND_VULKAN},
    {"metal", REIPAN_BACKEND_METAL},
    /* Accepted on read, never written: what someone would type by hand. */
    {"d3d12", REIPAN_BACKEND_D3D12},
    {"dx12", REIPAN_BACKEND_D3D12},
    {nullptr, 0},
};

const EnumName kRenderModes[] = {
    {"match_window", REIPAN_RENDER_RES_MATCH_WINDOW},
    {"native", REIPAN_RENDER_RES_NATIVE_PS2},
    {"window_scale", REIPAN_RENDER_RES_WINDOW_SCALE},
    {nullptr, 0},
};

const EnumName kFilters[] = {
    {"nearest", REIPAN_RENDER_FILTER_NEAREST},
    {"linear", REIPAN_RENDER_FILTER_LINEAR},
    {nullptr, 0},
};

const EnumName kAspectModes[] = {
    {"auto", REIPAN_ASPECT_AUTO},
    {"original", REIPAN_ASPECT_ORIGINAL},
    {"4:3", REIPAN_ASPECT_4_3},
    {"16:9", REIPAN_ASPECT_16_9},
    {"16:10", REIPAN_ASPECT_16_10},
    {"custom", REIPAN_ASPECT_CUSTOM},
    {nullptr, 0},
};

const EnumName kLightingModes[] = {
    {"vertex", REIPAN_LIGHTING_VERTEX},
    {"fragment", REIPAN_LIGHTING_FRAGMENT},
    {"fragment-all", REIPAN_LIGHTING_FRAGMENT_ALL},
    {nullptr, 0},
};

const EnumName kFilmGrain[] = {
    {"off", REIPAN_FILM_GRAIN_OFF},
    {"ps2", REIPAN_FILM_GRAIN_PS2},
    {"native", REIPAN_FILM_GRAIN_NATIVE},
    /* What the 0/1 an older build wrote meant.  "on" was the ROM's sheet. */
    {"on", REIPAN_FILM_GRAIN_PS2},
    {nullptr, 0},
};

const EnumName kShadowFilters[] = {
    {"none", REIPAN_SHADOW_FILTER_NONE},
    {"soft", REIPAN_SHADOW_FILTER_SOFT},
    {nullptr, 0},
};


const EnumName kHdrModes[] = {
    {"off", REIPAN_HDR_OFF},
    {"auto", REIPAN_HDR_AUTO},
    {"scrgb", REIPAN_HDR_SCRGB},
    {"hdr10", REIPAN_HDR_HDR10},
    /* Accepted on read, never written: what someone would type by hand. */
    {"on", REIPAN_HDR_AUTO},
    {"pq", REIPAN_HDR_HDR10},
    {"st2084", REIPAN_HDR_HDR10},
    {nullptr, 0},
};

const EnumName kTitleBackdrops[] = {
    {"room", REIPAN_TITLE_BACKDROP_ROOM},
    {"classic", REIPAN_TITLE_BACKDROP_CLASSIC},
    /* Accepted on read, never written: what someone would type by hand. */
    {"original", REIPAN_TITLE_BACKDROP_CLASSIC},
    {nullptr, 0},
};

const EnumName kTitleMotions[] = {
    {"pingpong", REIPAN_TITLE_MOTION_PINGPONG},
    {"loop", REIPAN_TITLE_MOTION_LOOP},
    {"once", REIPAN_TITLE_MOTION_ONCE},
    {nullptr, 0},
};

const EnumName kTitleEasings[] = {
    {"smooth", REIPAN_TITLE_EASING_SMOOTH},
    {"linear", REIPAN_TITLE_EASING_LINEAR},
    {nullptr, 0},
};

const EnumName kGyroTurns[] = {
    {"yaw", REIPAN_GYRO_TURN_YAW},
    {"roll", REIPAN_GYRO_TURN_ROLL},
    {nullptr, 0},
};

const EnumName kTitleChanges[] = {
    {"pass", REIPAN_TITLE_CHANGE_PASS},
    {"visit", REIPAN_TITLE_CHANGE_VISIT},
    {nullptr, 0},
};

const char *EnumToName(const EnumName *table, int value)
{
    for (const EnumName *e = table; e->label != nullptr; e++)
    {
        if (e->value == value)
        {
            return e->label;
        }
    }
    return table[0].label;
}

/* ------------------------------------------------------------------------
 *  A very small INI reader.  Sections, `key = value`, `;` or `#` comments,
 *  and nothing else -- there is no escaping or continuation to get wrong, and
 *  every value this file stores is a number or a short identifier.
 * --------------------------------------------------------------------- */

struct IniEntry
{
    std::string key; /* "section.name", lower-cased */
    std::string section; /* lower-cased; the title's scenes are found by it */
    std::string value;
};

std::vector<IniEntry> g_entries;
char g_path[1024];
bool g_path_resolved;

std::string Trim(const std::string &s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return std::string();
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
    }
    return s;
}

void ParseIni(const std::string &text)
{
    g_entries.clear();

    std::string section;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            if (close != std::string::npos)
            {
                section = Lower(Trim(line.substr(1, close - 1)));
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        IniEntry entry;
        entry.key = section + "." + Lower(Trim(line.substr(0, eq)));
        entry.section = section;
        entry.value = Trim(line.substr(eq + 1));
        g_entries.push_back(entry);
    }
}

const char *Lookup(const char *section, const char *name)
{
    std::string key = Lower(std::string(section) + "." + name);
    for (const IniEntry &entry : g_entries)
    {
        if (entry.key == key)
        {
            return entry.value.c_str();
        }
    }
    return nullptr;
}

void ReadInt(const char *section, const char *name, int *dst)
{
    const char *value = Lookup(section, name);
    if (value != nullptr && value[0] != '\0')
    {
        *dst = (int)std::strtol(value, nullptr, 10);
    }
}

void ReadFloat(const char *section, const char *name, float *dst)
{
    const char *value = Lookup(section, name);
    if (value != nullptr && value[0] != '\0')
    {
        *dst = (float)std::atof(value);
    }
}

/* Verbatim, less surrounding whitespace, which ParseIni has already trimmed.
 * A path is the one setting that must not be interpreted -- no case folding, no
 * separator rewriting -- because on Android it can be a content:// tree URI
 * rather than a filesystem path at all. */
void ReadString(const char *section, const char *name, char *dst, size_t size)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    /* One pair of surrounding double quotes is not part of the value: Windows'
     * "Copy as path" puts them around every path, and a quoted data_folder
     * used to fail the data-root test and be replaced by whatever discovery
     * found. */
    const size_t len = SDL_strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"')
    {
        const size_t inner = len - 2;
        const size_t n = (inner < size - 1) ? inner : size - 1;
        SDL_memcpy(dst, value + 1, n);
        dst[n] = '\0';
        return;
    }
    SDL_strlcpy(dst, value, size);
}

void ReadEnum(const char *section, const char *name, const EnumName *table,
              int *dst)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    for (const EnumName *e = table; e->label != nullptr; e++)
    {
        if (SDL_strcasecmp(value, e->label) == 0)
        {
            *dst = e->value;
            return;
        }
    }
    /* Not a name we know: a number from an older file, or a typo.  A typo
     * lands on 0, which every one of these enums defines as its default. */
    *dst = (int)std::strtol(value, nullptr, 10);
}

/*
 * "W:H" as a ratio, e.g. "21:9" -> 2.333.  Returns false if the text is not
 * that shape.  The named aspects cover the common cases, but this file is meant
 * to be hand-edited and a colon form is what someone will type for anything
 * else; without it "21:9" parses as the number 21 and clamps to nonsense.
 */
bool ParseRatio(const char *text, float *out)
{
    const char *colon = std::strchr(text, ':');
    if (colon == nullptr)
    {
        return false;
    }
    double w = std::atof(text);
    double h = std::atof(colon + 1);
    if (!(w > 0.0) || !(h > 0.0))
    {
        return false;
    }
    *out = (float)(w / h);
    return true;
}

/* Aspect needs its own reader: a name, then W:H, then a bare number. */
void ReadAspect(const char *section, const char *name, int *mode, float *ratio)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }
    for (const EnumName *e = kAspectModes; e->label != nullptr; e++)
    {
        if (SDL_strcasecmp(value, e->label) == 0)
        {
            *mode = e->value;
            return;
        }
    }
    float parsed = 0.0f;
    if (ParseRatio(value, &parsed) ||
        (parsed = (float)std::atof(value)) > 0.0f)
    {
        *mode = REIPAN_ASPECT_CUSTOM;
        *ratio = parsed;
    }
}

/* Three numbers separated by spaces or commas; a short or empty value leaves
 * the vector as it was. */
void ReadVec3(const char *section, const char *name, float *dst)
{
    const char *value = Lookup(section, name);
    if (value == nullptr || value[0] == '\0')
    {
        return;
    }

    float v[3];
    const char *p = value;
    for (int i = 0; i < 3; i++)
    {
        char *end = nullptr;
        v[i] = (float)std::strtod(p, &end);
        if (end == p)
        {
            return;
        }
        p = end;
        while (*p == ' ' || *p == '\t' || *p == ',')
        {
            p++;
        }
    }
    dst[0] = v[0];
    dst[1] = v[1];
    dst[2] = v[2];
}

/* ------------------------------------------------------------------------
 *  The title screen: [title], then one [title.sceneN] per scene.
 *
 *  The same text is the file's and the clipboard's, so a scene copied out of
 *  the overlay pastes back in, or into someone else's reipan.ini, unchanged.
 *  Sections are found by name rather than by number: [title.scene3] after
 *  [title.scene1] is the second scene, and a pasted block whose number is
 *  already taken is simply the next one.
 * --------------------------------------------------------------------- */

const ReiPan_ConfigTitleScene kTitleSceneDefault = {
    /* .name           = */ "",
    /* .enabled        = */ 1,
    /* .room           = */ 6,
    /* .side           = */ 0,
    /* 0: placed by itself when its room loads. */
    /* .camera_set     = */ 0,
    /* .a_eye          = */ { 0.0f, 0.0f, 0.0f },
    /* .a_target       = */ { 0.0f, 0.0f, 1.0f },
    /* .a_fov          = */ 50.0f,
    /* .b_eye          = */ { 0.0f, 0.0f, 0.0f },
    /* .b_target       = */ { 0.0f, 0.0f, 1.0f },
    /* .b_fov          = */ 50.0f,
    /* .travel_seconds = */ 30.0f,
    /* .hold_seconds   = */ 4.0f,
    /* .motion         = */ REIPAN_TITLE_MOTION_PINGPONG,
    /* .easing         = */ REIPAN_TITLE_EASING_SMOOTH,
};

/* The rest of the default rotation, after the Opening room: every room the
 * survey of 2026-10-27 showed (a title build stepping through the areas with
 * each framed by itself), all of which the user asked to keep, and 51 / 52,
 * which the survey ended before and which frame as well.  Each is framed when
 * it loads from the room's first shot of its own -- the designers' fixed
 * camera, panned a few degrees either side (outgame/title_room.c,
 * TitleRoomFrame) -- which is what the survey showed; area 36, with no shot of
 * its own, from its walkable floor.  Left out: 12 (no shot of its own and no
 * walk map: the box framing put the camera outside it), 37 and 45 (not on the
 * disc). */
const int kTitleDefaultRooms[] = {
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36,
    38, 39, 40, 41, 42, 43, 44,
    46, 47, 48, 49, 50, 51, 52,
};

/* Into the defaults, before the file is read: a file with scenes of its own
 * replaces the whole list (ReadTitle), so this only shows on a first run or
 * with every [title.sceneN] section removed. */
void AddDefaultTitleRooms(ReiPan_ConfigTitle *t)
{
    for (const int room : kTitleDefaultRooms)
    {
        if (t->scene_count >= REIPAN_TITLE_SCENE_MAX)
        {
            break;
        }

        ReiPan_ConfigTitleScene *s = &t->scenes[t->scene_count++];
        *s = kTitleSceneDefault;
        s->room = room;
        if (room >= 47)
        {
            SDL_snprintf(s->name, sizeof(s->name), "House %d", room - 46);
        }
        else
        {
            /* RS101 .. RS509, as the room list counts them. */
            static const int kFirst[] = { 1, 7, 20, 28, 38, 47 };
            int part = 0;
            while (room >= kFirst[part + 1])
            {
                part++;
            }
            SDL_snprintf(s->name, sizeof(s->name), "Room %d%02d", part + 1,
                         room - kFirst[part] + 1);
        }
    }
}

/* Nothing between this file and outgame/title_room.c clamps these, and an
 * area number off the end of the table would load whatever file sits there.
 * title_room.c refuses an area the disc does not carry. */
void ClampTitleScene(ReiPan_ConfigTitleScene *s)
{
    if (s->room < 1 || s->room > 52)
    {
        s->room = kTitleSceneDefault.room;
        s->camera_set = 0;
    }
    s->side = s->side != 0 ? 1 : 0;
    s->enabled = s->enabled != 0 ? 1 : 0;
    s->camera_set = s->camera_set != 0 ? 1 : 0;
    s->a_fov = SDL_clamp(s->a_fov, 10.0f, 120.0f);
    s->b_fov = SDL_clamp(s->b_fov, 10.0f, 120.0f);
    s->travel_seconds = SDL_clamp(s->travel_seconds, 0.5f, 600.0f);
    s->hold_seconds = SDL_clamp(s->hold_seconds, 0.0f, 600.0f);
    if (s->motion < REIPAN_TITLE_MOTION_PINGPONG || s->motion > REIPAN_TITLE_MOTION_ONCE)
    {
        s->motion = REIPAN_TITLE_MOTION_PINGPONG;
    }
    if (s->easing != REIPAN_TITLE_EASING_LINEAR)
    {
        s->easing = REIPAN_TITLE_EASING_SMOOTH;
    }
    s->name[REIPAN_TITLE_NAME_MAX - 1] = '\0';
}

void ClampTitle(ReiPan_ConfigTitle *t)
{
    if (t->backdrop != REIPAN_TITLE_BACKDROP_ROOM)
    {
        t->backdrop = REIPAN_TITLE_BACKDROP_CLASSIC;
    }
    if (t->change != REIPAN_TITLE_CHANGE_VISIT)
    {
        t->change = REIPAN_TITLE_CHANGE_PASS;
    }
    t->passes = SDL_clamp(t->passes, 1, 100);
    t->dim = SDL_clamp(t->dim, 0.0f, 1.0f);
    t->music = t->music < 0 ? 0 : t->music;
    t->music_volume = SDL_clamp(t->music_volume, 0.0f, 1.0f);
    t->attract_movie = t->attract_movie != 0 ? 1 : 0;
    t->scene_count = SDL_clamp(t->scene_count, 1, REIPAN_TITLE_SCENE_MAX);
}

/* The scene keys out of one section, over the defaults.  The section is the
 * scene's whole description, so nothing of whatever the slot held before
 * survives into it. */
void ReadTitleScene(const char *section, int number, ReiPan_ConfigTitleScene *s)
{
    *s = kTitleSceneDefault;
    SDL_snprintf(s->name, sizeof(s->name), "Scene %d", number);

    ReadString(section, "name", s->name, sizeof(s->name));
    ReadInt(section, "enabled", &s->enabled);
    ReadInt(section, "room", &s->room);
    ReadInt(section, "night", &s->side);
    ReadInt(section, "camera_set", &s->camera_set);
    ReadVec3(section, "a_eye", s->a_eye);
    ReadVec3(section, "a_target", s->a_target);
    ReadFloat(section, "a_fov", &s->a_fov);
    ReadVec3(section, "b_eye", s->b_eye);
    ReadVec3(section, "b_target", s->b_target);
    ReadFloat(section, "b_fov", &s->b_fov);
    ReadFloat(section, "travel_seconds", &s->travel_seconds);
    ReadFloat(section, "hold_seconds", &s->hold_seconds);
    ReadEnum(section, "motion", kTitleMotions, &s->motion);
    ReadEnum(section, "easing", kTitleEasings, &s->easing);
    ClampTitleScene(s);
}

/* Every scene the parsed text holds, in the order it holds them.  A
 * [title.sceneN] section is one, whatever N is; so is a block of scene keys
 * with no section header, which is what a hand-picked paste looks like. */
int CollectTitleScenes(ReiPan_ConfigTitleScene *out, int max)
{
    std::vector<std::string> sections;
    for (const IniEntry &entry : g_entries)
    {
        const bool scene = entry.section.compare(0, 11, "title.scene") == 0 ||
                           entry.section.empty();
        bool seen = false;
        for (const std::string &s : sections)
        {
            seen = seen || s == entry.section;
        }
        if (scene && !seen)
        {
            sections.push_back(entry.section);
        }
    }

    int n = 0;
    for (const std::string &section : sections)
    {
        if (n >= max)
        {
            break;
        }
        /* A headerless block counts only if it says which room. */
        if (section.empty() && Lookup("", "room") == nullptr)
        {
            continue;
        }
        ReadTitleScene(section.c_str(), n + 1, &out[n]);
        n++;
    }
    return n;
}

/* [title]'s own settings, then its scenes.  With `scenes` false the scene
 * list is left as it is -- the paste that adds scenes. */
void ReadTitle(ReiPan_ConfigTitle *t, bool scenes)
{
    ReadEnum("title", "backdrop", kTitleBackdrops, &t->backdrop);
    ReadEnum("title", "change", kTitleChanges, &t->change);
    ReadInt("title", "passes", &t->passes);
    ReadFloat("title", "dim", &t->dim);
    ReadInt("title", "music", &t->music);
    ReadFloat("title", "music_volume", &t->music_volume);
    ReadInt("title", "attract_movie", &t->attract_movie);

    if (scenes)
    {
        ReiPan_ConfigTitleScene found[REIPAN_TITLE_SCENE_MAX];
        const int n = CollectTitleScenes(found, REIPAN_TITLE_SCENE_MAX);

        if (n > 0)
        {
            for (int i = 0; i < n; i++)
            {
                t->scenes[i] = found[i];
            }
            t->scene_count = n;
        }
    }
    ClampTitle(t);
}

/* A name is written verbatim, so it must not be able to end its own line or
 * open a section. */
std::string TitleSceneName(const char *name)
{
    std::string out;
    for (const char *c = name; *c != '\0'; c++)
    {
        out += (*c == '\n' || *c == '\r' || *c == '[' || *c == ']') ? ' ' : *c;
    }
    return Trim(out);
}

std::string TitleSceneText(const ReiPan_ConfigTitleScene &s, int number)
{
    char buf[1024];
    SDL_snprintf(
        buf, sizeof(buf),
        "[title.scene%d]\n"
        "name = %s\n"
        "enabled = %d\n"
        "room = %d\n"
        "night = %d\n"
        "camera_set = %d\n"
        "a_eye = %.2f %.2f %.2f\n"
        "a_target = %.2f %.2f %.2f\n"
        "a_fov = %.2f\n"
        "b_eye = %.2f %.2f %.2f\n"
        "b_target = %.2f %.2f %.2f\n"
        "b_fov = %.2f\n"
        "travel_seconds = %.2f\n"
        "hold_seconds = %.2f\n"
        "motion = %s\n"
        "easing = %s\n"
        "\n",
        number,
        TitleSceneName(s.name).c_str(),
        s.enabled,
        s.room,
        s.side,
        s.camera_set,
        (double)s.a_eye[0], (double)s.a_eye[1], (double)s.a_eye[2],
        (double)s.a_target[0], (double)s.a_target[1], (double)s.a_target[2],
        (double)s.a_fov,
        (double)s.b_eye[0], (double)s.b_eye[1], (double)s.b_eye[2],
        (double)s.b_target[0], (double)s.b_target[1], (double)s.b_target[2],
        (double)s.b_fov,
        (double)s.travel_seconds,
        (double)s.hold_seconds,
        EnumToName(kTitleMotions, s.motion),
        EnumToName(kTitleEasings, s.easing));
    return buf;
}

std::string TitleText()
{
    const ReiPan_ConfigTitle &t = reipan_config.title;
    char buf[3072];
    SDL_snprintf(
        buf, sizeof(buf),
        "[title]\n"
        "; The title screen's backdrop: room | classic.  room draws one of the\n"
        ";   game's rooms, lit as the game lights it, behind the original logo\n"
        ";   and menus while the camera travels between two points.  classic is\n"
        ";   the original title, movie and picture of Rei and all.  The\n"
        ";   overlay's Settings > Title screen window edits everything here\n"
        ";   live, and copies it out and pastes it back in.\n"
        "backdrop = %s\n"
        "; When the title moves on to another scene, picked at random among\n"
        ";   the enabled ones: pass (after `passes` runs of the camera path)\n"
        ";   | visit (each time the title comes up).\n"
        "change = %s\n"
        "passes = %d\n"
        "; Black laid over the room under the logo, 0 none .. 1 black.\n"
        "dim = %.3f\n"
        "; 0 is the game's own title music, played as the original plays it.\n"
        ";   Anything else is a stream file number, looped -- the overlay\n"
        ";   lists them; 4009 (0xfa9) is BGM_00_TITLE, 3861 (0xf15) the\n"
        ";   entrance hall's ambience.\n"
        "music = %d\n"
        "; Its volume, 0..1; 1 is the level the game plays its music at.\n"
        "music_volume = %.3f\n"
        "; 1 lets the attract movie start after thirty idle seconds, as the\n"
        ";   original does.  Room backdrop only: classic always does.\n"
        "attract_movie = %d\n"
        "\n"
        "; The scenes, one section each.  room is the game's area number:\n"
        ";   1..46 the Manor of Sleep (RS101..RS509 in the order the game\n"
        ";   opens them), 47..52 the Hinasaki house (RRE01..RRE06), whose\n"
        ";   night lighting night = 1 shows.  camera_set 0 has the camera\n"
        ";   placed by itself when the room loads.  The points are where the\n"
        ";   eye is and what it looks at, in the game's world units (Y grows\n"
        ";   downwards; Rei is about 17 tall), and the field of view in\n"
        ";   degrees.  travel_seconds is one pass from A to B, hold_seconds\n"
        ";   the rest at each end.  motion: pingpong (there and back) | loop\n"
        ";   (cut back to A) | once.  easing: smooth | linear.  enabled 0\n"
        ";   keeps a scene out of the rotation.\n"
        "\n",
        EnumToName(kTitleBackdrops, t.backdrop),
        EnumToName(kTitleChanges, t.change),
        t.passes,
        (double)t.dim,
        t.music,
        (double)t.music_volume,
        t.attract_movie);

    std::string out = buf;
    for (int i = 0; i < t.scene_count; i++)
    {
        out += TitleSceneText(t.scenes[i], i + 1);
    }
    return out;
}

/* NUL-terminated into buf when it fits; the length the whole needs. */
int CopyOut(const std::string &text, char *buf, int size)
{
    if (buf != nullptr && size > 0)
    {
        SDL_strlcpy(buf, text.c_str(), (size_t)size);
    }
    return (int)text.size();
}

/* The renderer environment overrides, by name.  Listed here rather than asked
 * of the renderer so the rule stays visible next to the code it governs; the
 * cost is that a new override has to be added in both places, which the comment
 * on each getenv in reipan_renderer.cpp points out. */
/* ------------------------------------------------------------------------
 *  [input] -- the rebindable controls.
 *
 *  Unlike everything else in this file these do not live in ReiPan_Config:
 *  reipan_input owns them, and this section is only the text form.  Keeping
 *  one copy is what stops the file and the live bindings drifting apart.
 * --------------------------------------------------------------------- */

const char *const kInputButtonNames[REIPAN_PAD_BUTTON_COUNT] = {
    "up",    "down", "left", "right", "triangle", "cross",  "square", "circle",
    "l1",    "l2",   "r1",   "r2",    "start",    "select", "l3",     "r3"
};
#ifdef REIPAN_SLPS_25544_CHS
constexpr int kSquareBinding = 6;
constexpr int kLeftStickClickBinding = 14;
#endif

const char *const kInputStickNames[REIPAN_PAD_STICK_COUNT] = {
    "leftx", "lefty", "rightx", "righty"
};

/*
 * Split a source list.
 *
 * Sources are located by their prefixes rather than by splitting on commas,
 * because a key's own name may BE a comma: SDL_GetScancodeName() answers ","
 * for SDL_SCANCODE_COMMA, so "key:," is a perfectly legal binding that a comma
 * split would tear in half.  A prefix only opens a new source at a list
 * boundary -- start of line, or after a comma -- so "key:none" stays one
 * source rather than becoming two.
 */
std::vector<std::string> SplitSources(const std::string &value)
{
    static const char *const kPrefixes[] = {
        "key:", "pad:", "axis+:", "axis-:", "none"
    };

    std::vector<size_t> starts;
    for (size_t i = 0; i < value.size(); i++)
    {
        for (const char *prefix : kPrefixes)
        {
            size_t n = std::strlen(prefix);
            if (value.compare(i, n, prefix) != 0)
            {
                continue;
            }
            size_t back = i;
            while (back > 0 && (value[back - 1] == ' ' || value[back - 1] == '\t'))
            {
                back--;
            }
            if (back == 0 || value[back - 1] == ',')
            {
                starts.push_back(i);
            }
            break;
        }
    }

    std::vector<std::string> out;
    for (size_t s = 0; s < starts.size(); s++)
    {
        bool last = (s + 1 == starts.size());
        size_t end = last ? value.size() : starts[s + 1];
        std::string frag = Trim(value.substr(starts[s], end - starts[s]));

        /* Drop the separator, but only where there is one to drop.  A comma
         * ends the fragment in two quite different ways: in "key:,, pad:a" the
         * last character separates two sources, while in "key:," it IS the
         * source -- the comma key.  Only a fragment with another source after
         * it can be carrying a separator, so the last one is left alone and
         * ParseSourceList forgives a stray trailing comma instead. */
        if (!last && !frag.empty() && frag.back() == ',')
        {
            frag.pop_back();
            frag = Trim(frag);
        }
        if (!frag.empty())
        {
            out.push_back(frag);
        }
    }
    return out;
}

/*
 * Parse a source list into `dst`, up to `max` entries.  Returns how many
 * parsed.  Unrecognised sources are skipped with a warning rather than
 * clearing the slot: a settings file written by a newer build, or a typo,
 * should cost one binding and not the whole line.
 */
int ParseSourceList(const char *label, const char *value,
                    ReiPan_InputSource *dst, int max)
{
    int count = 0;
    for (const std::string &frag : SplitSources(value))
    {
        if (count >= max)
        {
            SDL_Log("ReiPan: [input] %s lists more than %d sources; "
                    "the rest are ignored", label, max);
            break;
        }
        ReiPan_InputSource src;
        std::memset(&src, 0, sizeof(src));
        bool ok = ReiPan_InputSourceFromString(frag.c_str(), &src) != 0;

        /* A trailing separator with nothing after it -- "key:A," -- is a
         * common enough typo to forgive, and it can only be reached here,
         * because SplitSources cannot tell it from the comma key. */
        if (!ok && frag.size() > 1 && frag.back() == ',')
        {
            ok = ReiPan_InputSourceFromString(frag.substr(0, frag.size() - 1).c_str(),
                                              &src) != 0;
        }

        if (ok)
        {
            dst[count++] = src;
        }
        else
        {
            SDL_Log("ReiPan: [input] %s: unrecognised source \"%s\", skipped",
                    label, frag.c_str());
        }
    }
    return count;
}

void LoadInputBindings()
{
    /* Which physical pad to use.  "auto" (or absent) keeps the first device SDL
     * lists, which is what a single-controller desk always wants; a GUID pins
     * one, so a wheel or a second pad enumerating first cannot take the slot. */
    const char *pad = Lookup("input", "gamepad");
    if (pad != nullptr && pad[0] != '\0' && SDL_strcasecmp(pad, "auto") != 0)
    {
        ReiPan_InputSetPreferredGamepad(pad);
    }

#ifdef REIPAN_SLPS_25544_CHS
    const char *square = Lookup("input", kInputButtonNames[kSquareBinding]);
    const char *left_click = Lookup("input", kInputButtonNames[kLeftStickClickBinding]);
    const bool use_new_defaults =
        Lookup("input", "keyboard_layout_version") == nullptr &&
        square != nullptr && std::strcmp(square, "key:Q, pad:x") == 0 &&
        left_click != nullptr && std::strcmp(left_click, "key:Left Shift, pad:leftstick") == 0;
#endif

    for (int i = 0; i < REIPAN_PAD_BUTTON_COUNT; i++)
    {
#ifdef REIPAN_SLPS_25544_CHS
        if (use_new_defaults && (i == kSquareBinding || i == kLeftStickClickBinding))
            continue;
#endif
        const char *value = Lookup("input", kInputButtonNames[i]);
        if (value == nullptr || value[0] == '\0')
        {
            continue; /* absent key keeps the default */
        }

        ReiPan_InputButtonBinding binding;
        std::memset(&binding, 0, sizeof(binding));
        int got = ParseSourceList(kInputButtonNames[i], value, binding.src,
                                  REIPAN_INPUT_MAX_SOURCES);

        /* Nothing at all parsed: the line is a typo rather than an intent, and
         * honouring it would silently leave the button dead.  "none" parses,
         * so deliberately clearing a button still works. */
        if (got == 0)
        {
            SDL_Log("ReiPan: [input] %s had no usable source; keeping the "
                    "default", kInputButtonNames[i]);
            continue;
        }
        ReiPan_InputSetButtonBinding(i, &binding);
    }

    for (int i = 0; i < REIPAN_PAD_STICK_COUNT; i++)
    {
        ReiPan_InputStickBinding binding;
        char name[64];

        if (!ReiPan_InputGetStickBinding(i, &binding))
        {
            continue;
        }

        SDL_snprintf(name, sizeof(name), "%s.analog", kInputStickNames[i]);
        const char *analog = Lookup("input", name);
        if (analog != nullptr && analog[0] != '\0')
        {
            ReiPan_InputSource src;
            std::memset(&src, 0, sizeof(src));
            if (ParseSourceList(name, analog, &src, 1) == 1)
            {
                binding.analog = src;
            }
        }

        SDL_snprintf(name, sizeof(name), "%s.keys", kInputStickNames[i]);
        const char *keys = Lookup("input", name);
        if (keys != nullptr && keys[0] != '\0')
        {
            ReiPan_InputSource pair[2];
            std::memset(pair, 0, sizeof(pair));
            int got = ParseSourceList(name, keys, pair, 2);
            if (got >= 1)
            {
                binding.neg = pair[0];
            }
            if (got >= 2)
            {
                binding.pos = pair[1];
            }
        }

        SDL_snprintf(name, sizeof(name), "%s.ramp_frames", kInputStickNames[i]);
        ReadInt("input", name, &binding.ramp_frames);
        if (binding.ramp_frames < 1)
        {
            binding.ramp_frames = 1; /* 0 would freeze the digital contribution */
        }

        ReiPan_InputSetStickBinding(i, &binding);
    }
}

/* One source as text, or "none" for an empty slot. */
std::string InputSourceText(const ReiPan_InputSource &src)
{
    char buf[128];
    if (ReiPan_InputSourceToString(&src, buf, sizeof(buf)) > 0)
    {
        return std::string(buf);
    }
    return std::string("none");
}

std::string BuildInputSection()
{
    std::string out =
        "[input]\n"
#ifdef REIPAN_SLPS_25544_CHS
        "keyboard_layout_version = 1\n"
#endif
        "; Rebindable controls.  Each line lists every source that can press\n"
        ";   one PS2 button -- any of them will do, up to four, separated by\n"
        ";   commas.  These 16 buttons are what the whole game reads through,\n"
        ";   so rebinding one here moves it everywhere at once.\n"
        ";\n"
        ";   key:<name>     a keyboard key, by SDL's name: \"Space\",\n"
        ";                  \"Left Shift\", \"F1\", \",\"\n"
        ";   pad:<name>     a gamepad button: \"a\", \"leftshoulder\", \"start\"\n"
        ";   axis+:<name>   a gamepad axis, positive half: \"lefttrigger\"\n"
        ";   axis-:<name>   the negative half of one\n"
        ";   none           an empty slot -- how you deliberately unbind\n"
        ";\n"
        ";   An axis counts as pressed past 8000 of its 0..32767 travel;\n"
        ";   append \"@<n>\" to change that, e.g. axis+:lefttrigger@20000.\n"
        ";\n"
        "; Delete a line to restore that control's default; delete the whole\n"
        ";   section to restore all of them.  A name this build does not know\n"
        ";   is skipped with a note in the log, and a line with nothing usable\n"
        ";   on it keeps its default rather than leaving the button dead.\n";

    {
        const char *pref = ReiPan_InputGetPreferredGamepad();
        char        active[128];

        out += "\n; Which gamepad to use: \"auto\" for the first one SDL\n"
               ";   lists, or a device GUID to pin one -- useful when a wheel\n"
               ";   or a second pad also carries a mapping.  An unplugged\n"
               ";   preference is kept and reclaimed when it comes back.\n";
        if (ReiPan_InputGetActiveGamepad(active, sizeof(active), nullptr, 0) &&
            active[0] != '\0')
        {
            out += "; in use now: ";
            out += active;
            out += "\n";
        }
        for (int i = 0, n = ReiPan_InputGetGamepadCount(); i < n; i++)
        {
            char name[128];
            char guid[REIPAN_GAMEPAD_GUID_LEN];
            if (ReiPan_InputGetGamepadInfo(i, name, sizeof(name), guid,
                                           sizeof(guid)))
            {
                out += ";   ";
                out += guid;
                out += "  ";
                out += name;
                out += "\n";
            }
        }
        out += "gamepad = ";
        out += (pref != nullptr && pref[0] != '\0') ? pref : "auto";
        out += "\n\n";
    }

    for (int i = 0; i < REIPAN_PAD_BUTTON_COUNT; i++)
    {
        ReiPan_InputButtonBinding binding;
        if (!ReiPan_InputGetButtonBinding(i, &binding))
        {
            continue;
        }

        std::string list;
        for (int s = 0; s < REIPAN_INPUT_MAX_SOURCES; s++)
        {
            if (binding.src[s].kind == REIPAN_SRC_NONE)
            {
                continue;
            }
            if (!list.empty())
            {
                list += ", ";
            }
            list += InputSourceText(binding.src[s]);
        }
        if (list.empty())
        {
            list = "none";
        }

        out += kInputButtonNames[i];
        out += " = ";
        out += list;
        out += "\n";
    }

    out +=
        "\n"
        "; Sticks.  `analog` is the gamepad axis; `keys` is the pair of keys\n"
        ";   pulling toward 0 and toward 255, in that order.  ramp_frames is\n"
        ";   how many updates a held key needs to reach full deflection: 1\n"
        ";   snaps, which is what movement wants, while the aim stick ramps so\n"
        ";   that keyboard aiming is steerable rather than all-or-nothing.\n";

    for (int i = 0; i < REIPAN_PAD_STICK_COUNT; i++)
    {
        ReiPan_InputStickBinding binding;
        char line[256];

        if (!ReiPan_InputGetStickBinding(i, &binding))
        {
            continue;
        }

        SDL_snprintf(line, sizeof(line), "%s.analog = %s\n",
                     kInputStickNames[i],
                     InputSourceText(binding.analog).c_str());
        out += line;

        SDL_snprintf(line, sizeof(line), "%s.keys = %s, %s\n",
                     kInputStickNames[i],
                     InputSourceText(binding.neg).c_str(),
                     InputSourceText(binding.pos).c_str());
        out += line;

        SDL_snprintf(line, sizeof(line), "%s.ramp_frames = %d\n",
                     kInputStickNames[i], binding.ramp_frames);
        out += line;
    }

    return out;
}

bool EnvOverrideActive()
{
    static const char *const kNames[] = {
        "REIPAN_RENDER_RES",   "REIPAN_RENDER_SCALE",
        "REIPAN_RENDER_FILTER", "REIPAN_ASPECT",
        "REIPAN_WINDOW_MODE",  "REIPAN_LIGHTING_MODE",
    };
    for (const char *name : kNames)
    {
        const char *value = std::getenv(name);
        if (value != nullptr && value[0] != '\0')
        {
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" {

const char *ReiPan_ConfigPath(void)
{
    if (!g_path_resolved)
    {
        g_path_resolved = true;
        if (ReiPan_PathResolveUser("reipan.ini", g_path, sizeof(g_path)) == 0)
        {
            g_path[0] = '\0';
        }
    }
    return g_path[0] != '\0' ? g_path : nullptr;
}

static void ConfigLoadOnce(void)
{
    AddDefaultTitleRooms(&reipan_config.title);

    const char *path = ReiPan_ConfigPath();
    if (path == nullptr)
    {
        return;
    }

    ReiPan_File *file = ReiPan_FileOpen(path, "rb");
    if (file == nullptr)
    {
        /*
         * First run.  Write the defaults out rather than waiting for a setting
         * to change.
         *
         * This used to be left alone on purpose, so that a file appeared only
         * once the player had chosen something.  [paths] data_folder ends that:
         * on a platform with no shell and no file manager pointed at the
         * install, an absent file is a setting that cannot be reached, and the
         * folder holding the game's data is exactly the setting most likely to
         * need reaching there.
         */
        ReiPan_ConfigSave();
        return;
    }

    std::string text;
    int64_t size = ReiPan_FileStreamSize(file);
    if (size > 0 && size < (1 << 20))
    {
        text.resize((size_t)size);
        size_t got = ReiPan_FileRead(file, &text[0], (size_t)size);
        text.resize(got);
    }
    ReiPan_FileCloseHandle(file);

    ParseIni(text);

    /* Before the renderer keys, because the paths module is what called us and
     * this is the value it is waiting on. */
    ReadString("paths", "data_folder", reipan_config.paths.data_folder,
               sizeof(reipan_config.paths.data_folder));

    ReiPan_ConfigRenderer *r = &reipan_config.renderer;
    ReadEnum("window", "mode", kWindowModes, &r->window_mode);
    ReadInt("window", "width", &r->window_width);
    ReadInt("window", "height", &r->window_height);
    ReadEnum("render", "backend", kBackends, &r->backend);
    ReadEnum("render", "mode", kRenderModes, &r->render_mode);
    ReadFloat("render", "scale", &r->render_scale);
    ReadEnum("render", "upscale_filter", kFilters, &r->upscale_filter);
    ReadInt("render", "hd_fonts", &r->hd_fonts);
    ReadInt("render", "smooth_2d", &r->smooth_2d);
    ReadInt("render", "msaa", &r->msaa_samples);
    ReadInt("render", "anisotropy", &r->anisotropy);
    ReadFloat("render", "aspect_ratio", &r->aspect_ratio);
    ReadFloat("render", "alpha_sharpen", &r->alpha_sharpen);
    ReadFloat("render", "alpha_cutoff", &r->alpha_cutoff);
    ReadInt("render", "resident_meshes", &r->resident_meshes);
    ReadInt("render", "resident_textures", &r->resident_textures);
    /* After aspect_ratio, so a "21:9" written in `aspect` wins over a stale
     * ratio left in the companion key. */
    ReadAspect("render", "aspect", &r->aspect_mode, &r->aspect_ratio);
    ReadEnum("lighting", "spotlights", kLightingModes, &r->lighting_mode);
    ReadEnum("shadow", "filter", kShadowFilters, &r->shadow_filter);
    ReadInt("smoothing", "extra_presents", &r->extra_presents);
    ReadInt("smoothing", "interpolate_camera", &r->interpolate_presents);
    ReadInt("smoothing", "interpolate_motion", &r->interpolate_geometry);
    ReadEnum("hdr", "mode", kHdrModes, &r->hdr_mode);
    ReadFloat("hdr", "paper_white", &r->hdr_paper_white);
    ReadFloat("hdr", "peak", &r->hdr_peak);
    ReadFloat("hdr", "highlight_expansion", &r->hdr_expansion);
    ReadFloat("hdr", "highlight_knee", &r->hdr_expansion_knee);
    ReadFloat("grade", "brightness", &r->grade_brightness);
    ReadFloat("grade", "contrast", &r->grade_contrast);
    ReadFloat("grade", "gamma", &r->grade_gamma);
    ReadFloat("grade", "saturation", &r->grade_saturation);
    ReadEnum("effects", "film_grain", kFilmGrain, &r->film_grain);
    ReadInt("finder", "surround", &r->finder_mask);
    ReadFloat("finder", "surround_blur", &r->finder_mask_blur);
    ReadFloat("finder", "surround_darken", &r->finder_mask_darken);
    ReadFloat("finder", "surround_tint", &r->finder_mask_tint);
    ReadFloat("finder", "surround_scale", &r->finder_mask_scale);

    /* The window size is the one value with no setter to clamp it, and a zero
     * or negative one would fail SDL_CreateWindow outright. */
    if (r->window_width < 320)
    {
        r->window_width = 320;
    }
    if (r->window_height < 240)
    {
        r->window_height = 240;
    }

    ReadInt("profiler", "summary", &reipan_config.profiler_summary);
    ReadInt("profiler", "fps_only", &reipan_config.profiler_fps_only);
#ifndef REIPAN_SLPS_25544_CHS
    ReadInt("overlay", "menu_bar", &reipan_config.overlay_menu_bar);
#endif

    ReiPan_ConfigCamera *cam = &reipan_config.camera;
    ReadInt("over_shoulder_camera", "enabled", &cam->over_shoulder);
    ReadInt("over_shoulder_camera", "left_shoulder", &cam->side_left);
    ReadInt("over_shoulder_camera", "collision", &cam->collision);
    ReadInt("over_shoulder_camera", "auto_follow", &cam->auto_follow);
    ReadInt("over_shoulder_camera", "invert_y", &cam->invert_y);
    {
        /* Before the Scare camera there was only event_cameras: 0 showed the
         * event's shot, 1 kept this camera (now Guided). */
        int legacy = -1;
        ReadInt("over_shoulder_camera", "event_cameras", &legacy);
        if (legacy == 0)
        {
            cam->scare_camera = REIPAN_OTS_SCARE_ORIGINAL;
        }
        const char *scare = Lookup("over_shoulder_camera", "scare_camera");
        if (scare != nullptr && scare[0] != '\0')
        {
            if (SDL_strcasecmp(scare, "original") == 0)
                cam->scare_camera = REIPAN_OTS_SCARE_ORIGINAL;
            else if (SDL_strcasecmp(scare, "off") == 0)
                cam->scare_camera = REIPAN_OTS_SCARE_OFF;
            else
                cam->scare_camera = REIPAN_OTS_SCARE_GUIDED;
        }
    }
    ReadFloat("over_shoulder_camera", "distance", &cam->distance);
    ReadFloat("over_shoulder_camera", "height", &cam->height);
    ReadFloat("over_shoulder_camera", "side", &cam->side);
    ReadFloat("over_shoulder_camera", "fov", &cam->fov);
    ReadFloat("over_shoulder_camera", "sensitivity", &cam->sensitivity);
    ReadFloat("over_shoulder_camera", "smoothing", &cam->smoothing);

    ReiPan_ConfigMouse *mouse = &reipan_config.mouse;
    ReadInt("mouse", "look", &mouse->look);
    ReadFloat("mouse", "sensitivity", &mouse->sensitivity);
    {
        const char *mode = Lookup("mouse", "camera_button");
        if (mode != nullptr && mode[0] != '\0')
        {
            mouse->camera_toggle = SDL_strcasecmp(mode, "toggle") == 0 ? 1 : 0;
        }
        static const char *const kActKeys[REIPAN_MOUSE_ACT_COUNT] = {
            "raise_camera", "shutter", "lens_shot", "film_previous", "film_next",
        };
        for (int i = 0; i < REIPAN_MOUSE_ACT_COUNT; i++)
        {
            const int src = ReiPan_GameMouseSourceFromName(Lookup("mouse", kActKeys[i]));
            if (src >= 0)
            {
                mouse->bind[i] = src;
            }
        }
    }

    ReiPan_ConfigGyro *gyro = &reipan_config.gyro;
    ReadInt("gyro", "look", &gyro->look);
    ReadFloat("gyro", "sensitivity", &gyro->sensitivity);
    ReadEnum("gyro", "turn", kGyroTurns, &gyro->turn);
    ReadInt("gyro", "invert_x", &gyro->invert_x);
    ReadInt("gyro", "invert_y", &gyro->invert_y);

    ReiPan_ConfigRumble *rumble = &reipan_config.rumble;
    ReadFloat("rumble", "strength", &rumble->strength);
    ReadInt("rumble", "triggers", &rumble->triggers);
    ReadFloat("rumble", "trigger_strength", &rumble->trigger_strength);
    ReadInt("rumble", "camera", &rumble->camera);

    LoadInputBindings();
    ReadTitle(&reipan_config.title, true);

    g_entries.clear();
    SDL_Log("ReiPan: loaded settings from %s", path);
}

void ReiPan_ConfigLoad(void)
{
    /*
     * Exactly once, whichever thread arrives first.
     *
     * Two do: the paths module calls this the moment anything asks where the
     * game data is -- and the IOP emulation asks from its own reader thread --
     * while EnsureRenderer() calls it again on the main thread before applying
     * the settings.  A function-local static is initialized once and the
     * language serializes it, which is both halves of what is needed here; a
     * plain bool would be a data race, and a second parse would undo whatever
     * the settings UI had changed since the first.
     */
    static const bool once = (ConfigLoadOnce(), true);
    (void)once;
}

namespace
{

/* [over_shoulder_camera], with the reasons beside the keys as the rest of the
 * file has them. */
std::string BuildCameraSection()
{
    const ReiPan_ConfigCamera *c = &reipan_config.camera;
    char text[2048];
    std::snprintf(
        text, sizeof(text),
        "\n[over_shoulder_camera]\n"
        "; A modern camera behind and beside the player instead of the\n"
        ";   game's fixed angles. The right stick turns it, R3 puts it back\n"
        ";   behind her, and the left stick walks the way it is pushed as\n"
        ";   seen through it. Event cameras, the viewfinder, ladders and the\n"
        ";   game's other special shots still play as they were made.\n"
        ";   0 off, 1 on.\n"
        "enabled = %d\n"
        "; 1 over the left shoulder instead of the right.\n"
        "left_shoulder = %d\n"
        "; Pull in against the rooms and furniture as they are drawn: over a\n"
        ";   low table, short of a wall or a closed door. 0 never pulls in.\n"
        "collision = %d\n"
        "; Drift back behind her while she walks and the right stick is idle.\n"
        "auto_follow = %d\n"
        "; Stick up looks down. On top of the game's own \"Point of view\"\n"
        ";   option, which this camera follows too.\n"
        "invert_y = %d\n"
        "; When the game cuts to an event camera while she can still walk\n"
        ";   (a ghost appearing, something to look at, the dream's graveyard\n"
        ";   path): guided keeps this camera and turns it once towards what\n"
        ";   the event's shot frames; original shows the event's shot; off\n"
        ";   keeps this camera and looks nowhere. Moments that take control\n"
        ";   away always play their own shot.\n"
        "scare_camera = %s\n"
        "; World units; Rei stands about 27 tall.\n"
        ";   distance 10..45 behind the shoulder, height 10..32 of the pivot\n"
        ";   above her feet, side 0..12 off her shoulder.\n"
        "distance = %.2f\n"
        "height = %.2f\n"
        "side = %.2f\n"
        "; Vertical field of view in degrees, 35..80.\n"
        "fov = %.1f\n"
        "; Right-stick speed, 0.25..3.\n"
        "sensitivity = %.2f\n"
        "; How far the camera lags her movement, 0 (rigid)..0.8.\n"
        "smoothing = %.2f\n",
        c->over_shoulder, c->side_left, c->collision, c->auto_follow,
        c->invert_y,
        c->scare_camera == REIPAN_OTS_SCARE_ORIGINAL ? "original"
            : c->scare_camera == REIPAN_OTS_SCARE_OFF ? "off" : "guided",
        (double)c->distance, (double)c->height, (double)c->side,
        (double)c->fov, (double)c->sensitivity, (double)c->smoothing);
    return text;
}

/* [mouse]. */
std::string BuildMouseSection()
{
    const ReiPan_ConfigMouse *m = &reipan_config.mouse;
    char text[2048];
    std::snprintf(
        text, sizeof(text),
        "\n[mouse]\n"
        "; The mouse in play: it turns the over-the-shoulder camera and aims\n"
        ";   the Camera Obscura (the pointer is captured while it does, and\n"
        ";   free in every menu and while the overlay's bar is up; F1 hides\n"
        ";   it). 0 off, 1 on.\n"
        "look = %d\n"
        "; How far it turns, 0.25..4.\n"
        "sensitivity = %.2f\n"
        "; hold: the Camera Obscura is up while its button is held.\n"
        ";   toggle: a click raises it, another lowers it.\n"
        "camera_button = %s\n"
        "; What each is on: left, right, middle, x1 (back), x2 (forward),\n"
        ";   wheel_up, wheel_down or none. The shutter, lens shot and film\n"
        ";   changes work while the Camera Obscura is up.\n"
        "raise_camera = %s\n"
        "shutter = %s\n"
        "lens_shot = %s\n"
        "film_previous = %s\n"
        "film_next = %s\n",
        m->look, (double)m->sensitivity, m->camera_toggle ? "toggle" : "hold",
        ReiPan_GameMouseSourceName(m->bind[0]), ReiPan_GameMouseSourceName(m->bind[1]),
        ReiPan_GameMouseSourceName(m->bind[2]), ReiPan_GameMouseSourceName(m->bind[3]),
        ReiPan_GameMouseSourceName(m->bind[4]));
    return text;
}

/* [gyro]. */
std::string BuildGyroSection()
{
    const ReiPan_ConfigGyro *g = &reipan_config.gyro;
    char text[2048];
    std::snprintf(
        text, sizeof(text),
        "\n[gyro]\n"
        "; While the Camera Obscura is raised, a gamepad with a gyroscope aims\n"
        ";   it by being turned; the stick still works alongside it. The\n"
        ";   gyro's drift is learned whenever the pad lies still with the\n"
        ";   Camera Obscura down. 0 off, 1 on.\n"
        "look = %d\n"
        "; Multiplies the pad's own rotation: 1.0 turns the view as far as the\n"
        ";   pad turned. 0.1..8.\n"
        "sensitivity = %.3f\n"
        "; What turns it left and right: yaw (point the pad left and right) or\n"
        ";   roll (tip it sideways, like a wheel).\n"
        "turn = %s\n"
        "; 1 swaps left and right, or up and down.\n"
        "invert_x = %d\n"
        "invert_y = %d\n",
        g->look, (double)g->sensitivity, EnumToName(kGyroTurns, g->turn),
        g->invert_x, g->invert_y);
    return text;
}

/* [rumble]. */
std::string BuildRumbleSection()
{
    const ReiPan_ConfigRumble *r = &reipan_config.rumble;
    char text[2048];
    std::snprintf(
        text, sizeof(text),
        "\n[rumble]\n"
        "; How hard the controller shakes, 0..1: 1 is as hard as the game asks.\n"
        ";   The game's own Vibration option turns it off altogether.\n"
        "strength = %.3f\n"
        "; 1 also plays what the shaking is for -- heartbeats, blows, a ghost's\n"
        ";   grip, thunder, the cutscenes -- on the motors behind the triggers of\n"
        ";   a controller that has them (Xbox One and later). 0 leaves every\n"
        ";   controller to the game's two motors.\n"
        "triggers = %d\n"
        "; The trigger motors alone, 0..1, on top of strength.\n"
        "trigger_strength = %.3f\n"
        "; 1 also plays the Camera Obscura there: the shutter, the charge and a\n"
        ";   shutter chance. The original does not shake for these.\n"
        "camera = %d\n",
        (double)r->strength, r->triggers, (double)r->trigger_strength, r->camera);
    return text;
}

} // namespace

int ReiPan_ConfigSave(void)
{
    /*
     * Saving is disabled outright for any session that took an environment
     * override.
     *
     * The alternative is worse than it looks: the capture reads the renderer's
     * live state, which under an override is the environment's value, so the
     * first unrelated settings change would quietly write the debug value into
     * the player's file.  An override is a one-off test run by intent -- the
     * honest behaviour is to leave the file exactly as it was.
     */
    if (EnvOverrideActive())
    {
        static bool warned;
        if (!warned)
        {
            warned = true;
            SDL_Log("ReiPan: REIPAN_* override in effect; settings will not "
                    "be saved this session");
        }
        return 0;
    }

    const char *path = ReiPan_ConfigPath();
    if (path == nullptr)
    {
        return 0;
    }

    const ReiPan_ConfigRenderer *r = &reipan_config.renderer;

    /* Formatted through a lambda so the format string and its arguments stay
     * in one place while the buffer is sized around them.
     *
     * This was a fixed char[4096], and a settings file that outgrew it simply
     * stopped being written -- the log line below was the only sign, and every
     * later change went on being lost.  Adding one commented setting was enough
     * to cross it.  Re-running the format at the length it asks for cannot go
     * stale the same way. */
    /* Built once, outside the lambda, because the lambda may run twice: the
     * section is variable-length text rather than a fixed format string, and
     * regenerating it on the resize pass would be pure waste. */
    const std::string input_section = BuildInputSection();
    const std::string camera_section = BuildCameraSection() + BuildMouseSection() +
        BuildGyroSection() + BuildRumbleSection() +
        "\n[overlay]\n"
#ifdef REIPAN_SLPS_25544_CHS
        "; F1 changes the debug menu for the current session.\n"
#else
        "; The menu bar F1 shows and hides, as it was left.\n"
#endif
        "menu_bar = " + std::to_string(reipan_config.overlay_menu_bar != 0 ? 1 : 0) +
        "\n";
    const std::string title_section = TitleText();

    auto format = [r, &input_section, &camera_section,
                   &title_section](char *buf, size_t cap) {
        return SDL_snprintf(
        buf, cap,
        "; ReiPan settings.  Deleting this file restores the defaults.\n"
        "; REIPAN_* environment variables override anything set here.\n"
        "\n"
        "[paths]\n"
        "; The folder holding the game's files -- ZERO3.FHD with IMG_BD.BIN or\n"
        ";   an extracted bin/data tree -- or a disc image (.iso) of the game.\n"
        ";   It may be anywhere; nothing requires it to be near ReiPan itself,\n"
        ";   which is the point of the setting.\n"
        ";   Leave it empty to have the folder looked for beside the\n"
        ";   executable and above it, and whatever is found written back here.\n"
        ";   REIPAN_DATA_DIR overrides it for one run.\n"
        "data_folder = %s\n"
        "\n"
        "[window]\n"
        "; windowed | borderless   (borderless is desktop fullscreen)\n"
        "mode = %s\n"
        "; windowed size only -- going fullscreen does not overwrite these\n"
        "width = %d\n"
        "height = %d\n"
        "\n"
        "[render]\n"
        "; Graphics API: auto | direct3d12 | vulkan | metal. Read at startup.\n"
        ";   auto takes the first one that works here. One that cannot be\n"
        ";   started falls back to it; one that starts and then misbehaves\n"
        ";   is undone by writing auto.\n"
        ";   SDL_GPU_DRIVER overrides it for one run.\n"
        "backend = %s\n"
        "; match_window | native | window_scale\n"
        ";   native rasterises at 640x448 x scale, i.e. PS2 pixel density\n"
        "mode = %s\n"
        "scale = %.4f\n"
        "; nearest = sharp but uneven at non-integer scale; linear = soft\n"
        "upscale_filter = %s\n"
        "; Optional replacement font PNGs: 0 = original, 1 = HD when available\n"
        "hd_fonts = %d\n"
        "; Bilinear font and HUD/menu sprites: 0 = PS2 sampling, 1 = smooth\n"
        "smooth_2d = %d\n"
        "; Multisample anti-aliasing on the finished frame: 1 (off), 2, 4, 8.\n"
        ";   Only the image the game is rasterised into is multisampled; it is\n"
        ";   resolved before anything reads it back, so the effects, the\n"
        ";   photographs and the pause still are unchanged by it.\n"
        ";   This game samples the frame buffer for its effects and\n"
        ";   every such sample now forces a resolve of the whole target, so a\n"
        ";   frame full of effects is much more expensive than a plain one.\n"
        ";   Gameplay visibility uses a separate single-sample depth pass\n"
        ";   at every MSAA setting.\n"
        ";   A level the GPU will not grant is clamped down.\n"
        "msaa = %d\n"
        "; Anisotropic filtering on the game's textures: 1 (off), 2, 4, 8, 16.\n"
        ";   No extra passes and no extra memory, and it is what stops tatami\n"
        ";   and corridor floors shimmering at grazing angles. Applies only\n"
        ";   where the PS2 asked for linear filtering in both directions, so\n"
        ";   a point-sampled texture stays point-sampled.\n"
        "anisotropy = %d\n"
        "; auto | original | 4:3 | 16:9 | 16:10 | custom\n"
        ";   a wider aspect shows more world, it does not stretch the image\n"
        ";   original is 640:448, the framing the game was composed for\n"
        "aspect = %s\n"
        "aspect_ratio = %.4f\n"
        "; Restores the one-texel alpha edge on cut-out textures --\n"
        ";   foliage, fences, hair -- that bilinear magnification softens.\n"
        ";   The game's own alpha test only ever discards fully transparent\n"
        ";   texels, which was enough at 640x448 where a texel was a pixel;\n"
        ";   above that the same step spreads over several pixels and drags\n"
        ";   the colour behind the cut-out into the silhouette.\n"
        ";   0 off (hardware behaviour) .. 1 full. Does nothing at or below\n"
        ";   native resolution whatever it is set to.\n"
        "alpha_sharpen = %.3f\n"
        "; A hard cut-out threshold: discard any texel whose alpha is below\n"
        ";   this. Blunter than alpha_sharpen and independent of it -- this\n"
        ";   aliases the edge, and can thin or erase fine geometry that is\n"
        ";   semi-transparent all the way across. Unlike alpha_sharpen it\n"
        ";   applies at every resolution. 0 off (hardware behaviour).\n"
        "alpha_cutoff = %.3f\n"
        "; Keep rooms, furniture and doors on the GPU: decoded once when first\n"
        ";   drawn, then drawn by reference with neighbouring parts merged.\n"
        ";   0 re-decodes and re-uploads every mesh every frame, as the PS2-\n"
        ";   shaped path always has. Same picture either way; 0 is the A/B.\n"
        "resident_meshes = %d\n"
        "; Resolve each room's, furniture's and door's textures once and stop\n"
        ";   uploading them to the emulated GS every frame. 0 re-sends and\n"
        ";   re-hashes them every draw, as the PS2 did -- the A/B.\n"
        "resident_textures = %d\n"
        "\n"
        "[lighting]\n"
        "; vertex reproduces the PS2 VU1 path; fragment is smoother\n"
        "spotlights = %s\n"
        "\n"
        "[smoothing]\n"
        "; Extra presents of each simulated frame, 0..3.  The game keeps its\n"
        ";   own 30 Hz tick either way -- these are the same frame shown\n"
        ";   again through a camera interpolated toward it, so 1 gives 60\n"
        ";   fps of camera motion and no change at all to game speed.\n"
        ";   Costs one more record and submit per extra frame.\n"
        "extra_presents = %d\n"
        "; 0 makes the extras identity copies -- a rendering self-test,\n"
        ";   not something to play with.\n"
        "interpolate_camera = %d\n"
        "; Move the world as well as the eye, so characters and moving\n"
        ";   objects stop stepping at 30 Hz.  Needs interpolate_camera.\n"
        ";   Costs one vertex-buffer upload per presented frame.\n"
        "interpolate_motion = %d\n"
        "\n[shadow]\n"
        "; How the projected shadow silhouette is sampled. soft averages a\n"
        ";   3x3 of it; none is the hard single tap the PS2's projected\n"
        ";   sprite gave.\n"
        "filter = %s\n"
        "\n"
        "[hdr]\n"
        "; off | auto | scrgb | hdr10\n"
        ";   auto uses HDR only when the display reports headroom, and\n"
        ";   follows it if you switch the OS setting or move the window.\n"
        ";   scrgb is preferred wherever both work: it is linear with sRGB\n"
        ";   primaries, so the conversion is one scale and nothing is\n"
        ";   requantised. hdr10 is for displays that only accept PQ.\n"
        ";   The game is SDR and stays SDR -- this is an output transform,\n"
        ";   not a rendering change. Set at launch it also buys a 10-bit\n"
        ";   scene target; turned on mid-session it works from 8.\n"
        "mode = %s\n"
        "; Nits an SDR white pixel is shown at. THIS is the brightness\n"
        ";   control that matters in HDR: it moves the whole picture without\n"
        ";   touching a ratio inside it, so nothing clips and nothing\n"
        ";   crushes. 0 follows the OS's own SDR white level; 200 is the\n"
        ";   reference both BT.2408 and Windows use.\n"
        "paper_white = %.1f\n"
        "; Brightest a lifted highlight may reach, in nits. 0 follows what\n"
        ";   the display reports it can do.\n"
        "peak = %.1f\n"
        "; How far into that headroom the top of the SDR range is stretched,\n"
        ";   0..1. 0 presents the image at paper_white and invents nothing,\n"
        ";   which is the faithful setting; higher makes the camera flash,\n"
        ";   lanterns and fire read as light rather than as paper.\n"
        "highlight_expansion = %.3f\n"
        "; Where that lift starts, as a fraction of paper white. Below it\n"
        ";   the picture is untouched, so a low knee reaches further down\n"
        ";   into the midtones.\n"
        "highlight_knee = %.3f\n"
        "\n"
        "[grade]\n"
        "; Display controls, applied in SDR as well as HDR and in the same\n"
        ";   order a monitor's own menu applies them. All four at 1.0 is\n"
        ";   identity, and identity costs nothing -- the output pass is\n"
        ";   skipped entirely unless HDR needs it.\n"
        "brightness = %.3f\n"
        "contrast = %.3f\n"
        "gamma = %.3f\n"
        "saturation = %.3f\n"
        "\n"
        "[effects]\n"
        "; The film grain scrolled over the picture, armed every frame the\n"
        ";   player is walking around.\n"
        ";     off     no grain at all.\n"
        ";     ps2     the ROM's own 128x128 sheet, one texel to a PS2\n"
        ";             pixel. Faithful, but a PS2 pixel is three or four of\n"
        ";             yours, so the grain arrives magnified to match.\n"
        ";     native  the same grain at one texel per screen pixel, off a\n"
        ";             larger sheet generated to match your resolution.\n"
        ";             Same density, same drift, same blend -- it is the\n"
        ";             sheet's size that changes, and that was a memory\n"
        ";             budget rather than a look.\n"
        "film_grain = %s\n"
        "\n"
        "[finder]\n"
        "; While the viewfinder is up, treat the world a wide window shows\n"
        ";   OUTSIDE the original 640x448 frame -- geometry the game was\n"
        ";   never composed to display. The 640x448 itself is left exactly\n"
        ";   as rendered. Does nothing on a 4:3 window. 0 off, 1 on.\n"
        "surround = %d\n"
        "; How far out of focus it goes, 0..1. 0 with a darken of 1 is a\n"
        ";   plain black surround instead.\n"
        "surround_blur = %.3f\n"
        "; How far it is darkened, 0 none .. 1 black.\n"
        "surround_darken = %.3f\n"
        "; How strongly it is drained of colour and cast cold: ink indigo\n"
        ";   in the shadows to a pale moonlit blue-white, 0 none .. 1 full.\n"
        "surround_tint = %.3f\n"
        "; The clear rectangle, against the original 640x448 frame. 1.0 is\n"
        ";   that frame exactly. Below 1.0 the surround climbs over the frame\n"
        ";   line and into the picture the game did compose, which is the one\n"
        ";   value here that can spoil the original look.\n"
        "surround_scale = %.3f\n"
        "%s"
        "\n[profiler]\nsummary = %d\nfps_only = %d\n"
        "\n%s\n%s",
        reipan_config.paths.data_folder,
        EnumToName(kWindowModes, r->window_mode),
        r->window_width,
        r->window_height,
        EnumToName(kBackends, r->backend),
        EnumToName(kRenderModes, r->render_mode),
        (double)r->render_scale,
        EnumToName(kFilters, r->upscale_filter),
        r->hd_fonts,
        r->smooth_2d,
        r->msaa_samples,
        r->anisotropy,
        EnumToName(kAspectModes, r->aspect_mode),
        (double)r->aspect_ratio,
        (double)r->alpha_sharpen,
        (double)r->alpha_cutoff,
        r->resident_meshes,
        r->resident_textures,
        EnumToName(kLightingModes, r->lighting_mode),
        r->extra_presents,
        r->interpolate_presents,
        r->interpolate_geometry,
        EnumToName(kShadowFilters, r->shadow_filter),
        EnumToName(kHdrModes, r->hdr_mode),
        (double)r->hdr_paper_white,
        (double)r->hdr_peak,
        (double)r->hdr_expansion,
        (double)r->hdr_expansion_knee,
        (double)r->grade_brightness,
        (double)r->grade_contrast,
        (double)r->grade_gamma,
        (double)r->grade_saturation,
        EnumToName(kFilmGrain, r->film_grain),
        r->finder_mask,
        (double)r->finder_mask_blur,
        (double)r->finder_mask_darken,
        (double)r->finder_mask_tint,
        (double)r->finder_mask_scale,
        camera_section.c_str(),
        reipan_config.profiler_summary,
        reipan_config.profiler_fps_only,
        input_section.c_str(),
        title_section.c_str());
    };

    std::vector<char> text(4096);
    int length = format(text.data(), text.size());
    if (length > 0 && length >= (int)text.size())
    {
        text.resize((size_t)length + 1);
        length = format(text.data(), text.size());
    }

    if (length <= 0 || length >= (int)text.size())
    {
        SDL_Log("ReiPan: settings did not fit the write buffer; not saved");
        return 0;
    }

    ReiPan_File *file = ReiPan_FileOpen(path, "wb");
    if (file == nullptr)
    {
        SDL_Log("ReiPan: could not open %s for writing", path);
        return 0;
    }
    size_t written = ReiPan_FileWrite(file, text.data(), (size_t)length);
    ReiPan_FileCloseHandle(file);

    if (written != (size_t)length)
    {
        SDL_Log("ReiPan: short write saving %s", path);
        return 0;
    }
    return 1;
}

void ReiPan_ConfigTitleSceneDefault(ReiPan_ConfigTitleScene *scene)
{
    *scene = kTitleSceneDefault;
}

int ReiPan_ConfigTitleText(char *buf, int size)
{
    return CopyOut(TitleText(), buf, size);
}

int ReiPan_ConfigTitleSceneText(int scene, char *buf, int size)
{
    if (scene < 0 || scene >= reipan_config.title.scene_count)
    {
        return CopyOut(std::string(), buf, size);
    }

    return CopyOut(TitleSceneText(reipan_config.title.scenes[scene], scene + 1),
                   buf, size);
}

int ReiPan_ConfigTitlePaste(const char *text, int replace)
{
    if (text == nullptr)
    {
        return 0;
    }

    /* The reader's helpers all look in g_entries, which holds nothing once the
     * file has been loaded; the pasted text borrows it and gives it back. */
    std::vector<IniEntry> saved;
    saved.swap(g_entries);
    ParseIni(text);

    ReiPan_ConfigTitle *t = &reipan_config.title;
    ReiPan_ConfigTitleScene found[REIPAN_TITLE_SCENE_MAX];
    const int n = CollectTitleScenes(found, REIPAN_TITLE_SCENE_MAX);
    int taken = 0;

    if (n > 0)
    {
        if (replace != 0)
        {
            ReadTitle(t, false);
            t->scene_count = 0;
        }
        for (int i = 0; i < n && t->scene_count < REIPAN_TITLE_SCENE_MAX; i++)
        {
            t->scenes[t->scene_count++] = found[i];
            taken++;
        }
        ClampTitle(t);
    }

    g_entries.swap(saved);
    return taken;
}

int ReiPan_ConfigSetDataFolder(const char *folder)
{
    char *dst = reipan_config.paths.data_folder;

    if (folder == nullptr)
    {
        folder = "";
    }
    if (SDL_strcmp(dst, folder) == 0)
    {
        return 1;
    }

    SDL_strlcpy(dst, folder, sizeof(reipan_config.paths.data_folder));

    /* Persisted immediately.  Discovery's value is only useful to the player if
     * it survives to the next run -- that is how a folder guessed wrongly
     * becomes a line they can correct instead of a mystery -- and the settings
     * UI has nothing to show for a choice that was not written down. */
    return ReiPan_ConfigSave();
}

void ReiPan_ConfigApply(void)
{
    const ReiPan_ConfigRenderer *r = &reipan_config.renderer;

    /* Through the setters, so their clamps are the only place a range is
     * enforced and a hand-edited file cannot put the renderer in a state the
     * UI could not. */
    ReiPan_RendererSetBackend(r->backend);
    ReiPan_RendererSetAspectMode(r->aspect_mode, r->aspect_ratio);
    ReiPan_RendererSetRenderResolution(r->render_mode, r->render_scale);
    ReiPan_RendererSetUpscaleFilter(r->upscale_filter);
    ReiPan_RendererSetSmooth2d(r->smooth_2d);
    ReiPan_RendererSetMsaaSamples(r->msaa_samples);
    ReiPan_RendererSetAnisotropy(r->anisotropy);
    ReiPan_RendererSetLightingMode(r->lighting_mode);
    ReiPan_RendererSetShadowFilter(r->shadow_filter);
    ReiPan_RendererSetExtraPresents(r->extra_presents);
    ReiPan_RendererSetPresentInterpolation(r->interpolate_presents);
    ReiPan_RendererSetGeometryInterpolation(r->interpolate_geometry);
    ReiPan_RendererSetFilmGrain(r->film_grain);
    ReiPan_RendererSetAlphaSharpen(r->alpha_sharpen);
    ReiPan_RendererSetAlphaCutoff(r->alpha_cutoff);
    ReiPan_RendererSetResidentMeshes(r->resident_meshes);
    ReiPan_RendererSetResidentTextures(r->resident_textures);
    ReiPan_RendererSetFinderMask(r->finder_mask, r->finder_mask_blur,
                                 r->finder_mask_darken, r->finder_mask_tint,
                                 r->finder_mask_scale);
    /* Before the window mode, and so before EnsureRenderer() creates the
     * device: the HDR mode is what decides whether the scene target gets its
     * ten bits, and that decision is taken once, when the pipelines are
     * built. */
    ReiPan_RendererSetHdrMode(r->hdr_mode);
    ReiPan_RendererSetHdrPaperWhite(r->hdr_paper_white);
    ReiPan_RendererSetHdrPeak(r->hdr_peak);
    ReiPan_RendererSetHdrExpansion(r->hdr_expansion, r->hdr_expansion_knee);
    ReiPan_RendererSetGrade(r->grade_brightness, r->grade_contrast,
                            r->grade_gamma, r->grade_saturation);
    ReiPan_RendererSetWindowMode(r->window_mode);

    const ReiPan_ConfigCamera  *c = &reipan_config.camera;
    ReiPan_OverShoulderSettings ots;
    ots.enabled = c->over_shoulder;
    ots.side_left = c->side_left;
    ots.collision = c->collision;
    ots.auto_follow = c->auto_follow;
    ots.invert_y = c->invert_y;
    ots.scare_camera = c->scare_camera;
    ots.distance = c->distance;
    ots.height = c->height;
    ots.side = c->side;
    ots.fov_deg = c->fov;
    ots.sensitivity = c->sensitivity;
    ots.smoothing = c->smoothing;
    ReiPan_OverShoulderSetSettings(&ots);

    const ReiPan_ConfigMouse *m = &reipan_config.mouse;
    ReiPan_GameMouseSettings  gm;
    gm.look = m->look;
    gm.sensitivity = m->sensitivity;
    gm.camera_toggle = m->camera_toggle;
    for (int i = 0; i < REIPAN_MOUSE_ACT_COUNT; i++)
    {
        gm.bind[i] = m->bind[i];
    }
    ReiPan_GameMouseSetSettings(&gm);

    const ReiPan_ConfigGyro *g = &reipan_config.gyro;
    ReiPan_GyroSettings      gs;
    gs.look = g->look;
    gs.sensitivity = g->sensitivity;
    gs.turn = g->turn;
    gs.invert_x = g->invert_x;
    gs.invert_y = g->invert_y;
    ReiPan_GyroSetSettings(&gs);

    const ReiPan_ConfigRumble *rumble = &reipan_config.rumble;
    ReiPan_HapticsSettings     hs;
    hs.strength = rumble->strength;
    hs.triggers = rumble->triggers;
    hs.trigger_strength = rumble->trigger_strength;
    hs.camera = rumble->camera;
    ReiPan_HapticsSetSettings(&hs);
}

void ReiPan_ConfigCapture(void)
{
    ReiPan_ConfigRenderer *r = &reipan_config.renderer;

    /* The requested backend, not the one the device runs, for the reason the
     * sample count below is the requested one. */
    r->backend = ReiPan_RendererGetBackend();
    ReiPan_RendererGetAspectMode(&r->aspect_mode, &r->aspect_ratio);
    ReiPan_RendererGetRenderResolution(&r->render_mode, &r->render_scale);
    r->upscale_filter = ReiPan_RendererGetUpscaleFilter();
    r->smooth_2d = ReiPan_RendererGetSmooth2d();
    /* The requested levels, not the granted ones: a device that cannot do 8x
     * today must not rewrite the player's choice into the file. */
    r->msaa_samples = ReiPan_RendererGetMsaaSamples();
    r->anisotropy = ReiPan_RendererGetAnisotropy();
    r->lighting_mode = ReiPan_RendererGetLightingMode();
    r->shadow_filter = ReiPan_RendererGetShadowFilter();
    r->extra_presents = ReiPan_RendererGetExtraPresents();
    r->interpolate_presents = ReiPan_RendererGetPresentInterpolation();
    r->interpolate_geometry = ReiPan_RendererGetGeometryInterpolation();
    r->hdr_mode = ReiPan_RendererGetHdrMode();
    r->hdr_paper_white = ReiPan_RendererGetHdrPaperWhite();
    r->hdr_peak = ReiPan_RendererGetHdrPeak();
    ReiPan_RendererGetHdrExpansion(&r->hdr_expansion, &r->hdr_expansion_knee);
    ReiPan_RendererGetGrade(&r->grade_brightness, &r->grade_contrast,
                            &r->grade_gamma, &r->grade_saturation);
    ReiPan_RendererGetFinderMask(&r->finder_mask, &r->finder_mask_blur,
                                 &r->finder_mask_darken, &r->finder_mask_tint,
                                 &r->finder_mask_scale);
    r->film_grain = ReiPan_RendererGetFilmGrain();
    r->alpha_sharpen = ReiPan_RendererGetAlphaSharpen();
    r->alpha_cutoff = ReiPan_RendererGetAlphaCutoff();
    r->resident_meshes = ReiPan_RendererGetResidentMeshes();
    r->resident_textures = ReiPan_RendererGetResidentTextures();
    r->window_mode = ReiPan_RendererGetWindowMode();

    ReiPan_OverShoulderSettings ots;
    ReiPan_OverShoulderGetSettings(&ots);
    ReiPan_ConfigCamera *c = &reipan_config.camera;
    c->over_shoulder = ots.enabled;
    c->side_left = ots.side_left;
    c->collision = ots.collision;
    c->auto_follow = ots.auto_follow;
    c->invert_y = ots.invert_y;
    c->scare_camera = ots.scare_camera;
    c->distance = ots.distance;
    c->height = ots.height;
    c->side = ots.side;
    c->fov = ots.fov_deg;
    c->sensitivity = ots.sensitivity;
    c->smoothing = ots.smoothing;

    ReiPan_GameMouseSettings gm;
    ReiPan_GameMouseGetSettings(&gm);
    ReiPan_ConfigMouse *m = &reipan_config.mouse;
    m->look = gm.look;
    m->sensitivity = gm.sensitivity;
    m->camera_toggle = gm.camera_toggle;
    for (int i = 0; i < REIPAN_MOUSE_ACT_COUNT; i++)
    {
        m->bind[i] = gm.bind[i];
    }

    ReiPan_GyroSettings gs;
    ReiPan_GyroGetSettings(&gs);
    ReiPan_ConfigGyro *g = &reipan_config.gyro;
    g->look = gs.look;
    g->sensitivity = gs.sensitivity;
    g->turn = gs.turn;
    g->invert_x = gs.invert_x;
    g->invert_y = gs.invert_y;

    ReiPan_HapticsSettings hs;
    ReiPan_HapticsGetSettings(&hs);
    ReiPan_ConfigRumble *rumble = &reipan_config.rumble;
    rumble->strength = hs.strength;
    rumble->triggers = hs.triggers;
    rumble->trigger_strength = hs.trigger_strength;
    rumble->camera = hs.camera;

    /* The windowed size, which the renderer latches while the window is not
     * fullscreen.  Reading the live window instead would store the desktop
     * size the moment anyone saved settings from borderless mode, and the
     * window would never return to its old shape. */
    int width = 0;
    int height = 0;
    ReiPan_RendererGetWindowedSize(&width, &height);
    if (width >= 320 && height >= 240)
    {
        r->window_width = width;
        r->window_height = height;
    }
}

int ReiPan_ConfigCaptureAndSave(void)
{
    ReiPan_ConfigCapture();
    return ReiPan_ConfigSave();
}

}
