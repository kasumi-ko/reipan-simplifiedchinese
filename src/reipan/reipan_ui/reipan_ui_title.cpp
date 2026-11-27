/*
 * The overlay's title screen window -- Settings > Title screen.  After
 * MioPan's miopan_ui_title.cpp, which does the same for FF2.
 *
 * Everything here is reipan_config.title, written in place: the overlay's
 * settle-and-save (PersistSettingsIfChanged() in reipan_ui.cpp) notices the
 * struct moved and writes reipan.ini half a second after the last change.
 * outgame/title_room.c reads the same struct every frame, so most edits --
 * the camera points, the path, the dim -- show on the next frame with nothing
 * to call.  The ones that need work done (another scene, another room,
 * another track) say so through title_room.h.
 *
 * The title rotates through its scenes by itself.  While this window is open
 * it holds on the one selected here, so a scene does not fade away while it
 * is being edited; "Keep rotating" lets it run on.
 *
 * Copy and paste move the same text reipan.ini carries (ReiPan_ConfigTitleText
 * and friends), so a scene copied here pastes into anyone's file or back into
 * this window unchanged.
 *
 * Every widget has an ID of its own: ImGui keys a widget by its label, and two
 * that share one (a list and the header above it, both "Room") clash.
 *
 * The window is drawn from the overlay's Draw(), on the game thread between
 * one game frame and the next, like every other overlay window that touches
 * the game.
 */
#include "reipan_ui_title.h"
#include "reipan_ui_locale.h"

#include "../reipan_config.h"

#include "../../ingame/ingame_wrk.h"    /* ingame_wrk, IngameIsOutgame */
#include "../../main/gphase.h"          /* GetGPhaseCtrl, GPHASE_NODE */
#include "../../outgame/title_room.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
using ReiPanUiLocale::UiText;


constexpr short kPhaseTitleMode = 5;

bool g_show_title = false;
/* The scene the editor below the list works on. */
int g_selected = -1;
bool g_rotate_while_open = false;
/* The room the last capture moved both points to, shown until the next one. */
int g_capture_moved_room = -1;
/* What the last copy or paste did, and when. */
std::string g_clip_note;
double g_clip_note_time = -100.0;

bool PhaseActive(short id)
{
    for (GPHASE_NODE *n = GetGPhaseCtrl()->root; n != nullptr; n = n->child)
    {
        if (n->id == id)
        {
            return true;
        }
    }
    return false;
}

bool OnTitle()
{
    return PhaseActive(kPhaseTitleMode);
}

bool InGame()
{
    return ingame_wrk.plyr_wrk != nullptr && IngameIsOutgame() == 0;
}

void Tooltip(const char *text)
{
    if (text != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

void ClipNote(const char *text)
{
    g_clip_note = text;
    g_clip_note_time = ImGui::GetTime();
}

ReiPan_ConfigTitleScene &Selected()
{
    ReiPan_ConfigTitle &t = reipan_config.title;

    if (g_selected < 0 || g_selected >= t.scene_count)
    {
        g_selected = 0;
    }
    return t.scenes[g_selected];
}

void RoomLabel(int room, char *out, size_t size)
{
    std::snprintf(out, size, "%2d  %-6s %s", room, TitleRoomCode(room),
                  TitleRoomPlace(room));
}

void Select(int scene)
{
    g_selected = scene;
    g_capture_moved_room = -1;
    TitleRoomShowScene(scene);
}

void DrawStatus()
{
    const ImVec4 good(0.45f, 0.85f, 0.50f, 1.0f);
    const ImVec4 busy(1.00f, 0.78f, 0.35f, 1.0f);
    const ImVec4 bad(0.94f, 0.45f, 0.40f, 1.0f);
    const ReiPan_ConfigTitle &t = reipan_config.title;
    const int current = TitleRoomCurrentScene();
    const ReiPan_ConfigTitleScene &s =
        t.scenes[current >= 0 && current < t.scene_count ? current : 0];

    if (!OnTitle())
    {
        ImGui::TextDisabled("Not on the title screen. Changes show the next "
                            "time it comes up.");
        return;
    }

    switch (TitleRoomGetState())
    {
    case TITLE_ROOM_OFF:
        ImGui::TextDisabled("Showing the original background.");
        break;
    case TITLE_ROOM_LOADING:
        ImGui::TextColored(busy, "Loading \"%s\" (%s)...", s.name,
                           TitleRoomCode(s.room));
        break;
    case TITLE_ROOM_SHOWN:
        ImGui::TextColored(good, "Showing \"%s\": %s, %s%s.", s.name,
                           TitleRoomCode(s.room), TitleRoomPlace(s.room),
                           TitleRoomHasNight(s.room) && s.side != 0 ? " at night" : "");
        break;
    case TITLE_ROOM_RELEASED:
        ImGui::TextDisabled("The attract movie has the screen; the room "
                            "comes back after it.");
        break;
    case TITLE_ROOM_MISSING:
        ImGui::TextColored(bad, "%s did not load from this disc.",
                           TitleRoomCode(s.room));
        break;
    }
}

void DrawBackdrop()
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    int backdrop = t.backdrop;

    ImGui::TextUnformatted(UiText("Backdrop"));
    ImGui::SameLine();
    ImGui::RadioButton(UiText("Rooms from the game##backdrop_room"), &backdrop,
                       REIPAN_TITLE_BACKDROP_ROOM);
    Tooltip("The scenes below, lit and animated as the game plays them,\n"
            "behind the original logo and menus.");
    ImGui::SameLine();
    ImGui::RadioButton(UiText("The original background##backdrop_classic"), &backdrop,
                       REIPAN_TITLE_BACKDROP_CLASSIC);
    Tooltip("The original title, movie and picture of Rei and all -- exactly\n"
            "the original's, apart from the music choice below.");

    if (backdrop != t.backdrop)
    {
        t.backdrop = backdrop;
        TitleRoomReload();
    }
}

/* ---------------------------------------------------------------------------
 *  The scene list
 * ------------------------------------------------------------------------ */

void MoveScene(int from, int to)
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    const ReiPan_ConfigTitleScene moved = t.scenes[from];

    t.scenes[from] = t.scenes[to];
    t.scenes[to] = moved;
    Select(to);
}

void DrawSceneList()
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    const float em = ImGui::GetFontSize();
    const int current = TitleRoomCurrentScene();
    const bool on_title = OnTitle();
    char label[96];

    if (ImGui::BeginListBox("##scene_list",
                            ImVec2(-1.0f, em * 1.5f * (float)(t.scene_count < 6 ? t.scene_count + 1 : 7))))
    {
        for (int i = 0; i < t.scene_count; i++)
        {
            ReiPan_ConfigTitleScene &s = t.scenes[i];
            ImGui::PushID(i);

            bool enabled = s.enabled != 0;
            if (ImGui::Checkbox("##enabled", &enabled))
            {
                s.enabled = enabled ? 1 : 0;
            }
            Tooltip("In the rotation.  Off keeps the scene, and keeps it out.");
            ImGui::SameLine();

            std::snprintf(label, sizeof(label), "%s%s  -  %s, %s%s",
                          on_title && i == current ? "> " : "", s.name,
                          TitleRoomCode(s.room), TitleRoomPlace(s.room),
                          TitleRoomHasNight(s.room) && s.side != 0 ? ", night" : "");
            if (ImGui::Selectable(label, i == g_selected))
            {
                Select(i);
            }

            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    const bool full = t.scene_count >= REIPAN_TITLE_SCENE_MAX;

    ImGui::BeginDisabled(full);
    if (ImGui::Button(UiText("New##scene_new")))
    {
        ReiPan_ConfigTitleScene &s = t.scenes[t.scene_count];
        ReiPan_ConfigTitleSceneDefault(&s);
        std::snprintf(s.name, sizeof(s.name), UiText("Scene %d"), t.scene_count + 1);
        t.scene_count++;
        Select(t.scene_count - 1);
    }
    Tooltip("A new scene in the opening room (RS106), placed by itself.  Pick\n"
            "its room and move its points from there.");
    ImGui::SameLine();
    if (ImGui::Button(UiText("Duplicate##scene_dup")))
    {
        ReiPan_ConfigTitleScene copy = Selected();
        char name[REIPAN_TITLE_NAME_MAX];
        std::snprintf(name, sizeof(name), "%.24s copy", copy.name);
        std::memcpy(copy.name, name, sizeof(copy.name));
        t.scenes[t.scene_count++] = copy;
        Select(t.scene_count - 1);
    }
    Tooltip("A copy of the selected scene, to vary.");
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(t.scene_count <= 1);
    if (ImGui::Button(UiText("Delete##scene_delete")))
    {
        const int gone = g_selected;
        for (int i = gone; i < t.scene_count - 1; i++)
        {
            t.scenes[i] = t.scenes[i + 1];
        }
        t.scene_count--;
        Select(gone < t.scene_count ? gone : t.scene_count - 1);
    }
    Tooltip("Remove the selected scene.  The last one cannot go.");
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(g_selected <= 0);
    if (ImGui::ArrowButton("##scene_up", ImGuiDir_Up))
    {
        MoveScene(g_selected, g_selected - 1);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_selected >= t.scene_count - 1);
    if (ImGui::ArrowButton("##scene_down", ImGuiDir_Down))
    {
        MoveScene(g_selected, g_selected + 1);
    }
    ImGui::EndDisabled();

    /* When the title moves on. */
    int change = t.change;
    ImGui::TextUnformatted(UiText("Next scene"));
    ImGui::SameLine();
    ImGui::RadioButton("after##change_pass", &change, REIPAN_TITLE_CHANGE_PASS);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 5.0f);
    ImGui::InputInt("##passes", &t.passes, 1, 1);
    if (t.passes < 1)
    {
        t.passes = 1;
    }
    if (t.passes > 100)
    {
        t.passes = 100;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(t.passes == 1 ? "pass of the path" : "passes of the path");
    ImGui::Indent(ImGui::CalcTextSize(UiText("Next scene")).x +
                  ImGui::GetStyle().ItemSpacing.x);
    ImGui::RadioButton(UiText("each time the title comes up##change_visit"), &change,
                       REIPAN_TITLE_CHANGE_VISIT);
    ImGui::Unindent(ImGui::CalcTextSize(UiText("Next scene")).x +
                    ImGui::GetStyle().ItemSpacing.x);
    t.change = change;
    Tooltip("Scenes are picked at random among the ticked ones, never the\n"
            "same one twice running while there is another.");

    ImGui::Checkbox(UiText("Keep rotating while this window is open##rotate_open"),
                    &g_rotate_while_open);
    Tooltip("Off holds the title on the selected scene for as long as this\n"
            "window is open, so it does not fade away while it is edited.");
}

/* ---------------------------------------------------------------------------
 *  Copy and paste
 * ------------------------------------------------------------------------ */

void CopyText(int scene)
{
    const int length = scene < 0 ? ReiPan_ConfigTitleText(nullptr, 0)
                                 : ReiPan_ConfigTitleSceneText(scene, nullptr, 0);
    std::string text((size_t)length + 1, '\0');

    if (scene < 0)
    {
        ReiPan_ConfigTitleText(&text[0], length + 1);
    }
    else
    {
        ReiPan_ConfigTitleSceneText(scene, &text[0], length + 1);
    }
    text.resize((size_t)length);
    ImGui::SetClipboardText(text.c_str());

    char note[96];
    std::snprintf(note, sizeof(note), "Copied %s (%d characters).",
                  scene < 0 ? "the title's settings and every scene"
                            : "the scene",
                  length);
    ClipNote(note);
}

void PasteText(bool replace)
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    const char *text = ImGui::GetClipboardText();
    const int before = t.scene_count;
    const int taken = ReiPan_ConfigTitlePaste(text, replace ? 1 : 0);
    char note[96];

    if (taken == 0)
    {
        ClipNote("Nothing to paste: the clipboard holds no [title.scene] "
                 "section.");
        return;
    }

    std::snprintf(note, sizeof(note), "Pasted %d scene%s.", taken,
                  taken == 1 ? "" : "s");
    ClipNote(note);
    Select(replace ? 0 : (before < t.scene_count ? before : t.scene_count - 1));
    if (replace)
    {
        TitleRoomReload();
        TitleRoomMusicChanged();
    }
}

void DrawClipboard()
{
    if (ImGui::Button(UiText("Copy scene##copy_scene")))
    {
        CopyText(g_selected);
    }
    Tooltip("The selected scene, as a [title.scene] section of reipan.ini.");
    ImGui::SameLine();
    if (ImGui::Button(UiText("Copy all##copy_all")))
    {
        CopyText(-1);
    }
    Tooltip("[title] and every scene, exactly as reipan.ini has them.");
    if (ImGui::Button(UiText("Paste as new scenes##paste_add")))
    {
        PasteText(false);
    }
    Tooltip("Every [title.scene] section on the clipboard, added after the\n"
            "scenes here.");
    ImGui::SameLine();
    if (ImGui::Button(UiText("Paste, replacing all##paste_replace")))
    {
        PasteText(true);
    }
    Tooltip("The clipboard's scenes instead of these, and its [title]\n"
            "settings -- backdrop, rotation, dim, music -- if it has them.");

    if (ImGui::GetTime() - g_clip_note_time < 6.0)
    {
        ImGui::TextDisabled("%s", g_clip_note.c_str());
    }
}

/* ---------------------------------------------------------------------------
 *  The selected scene
 * ------------------------------------------------------------------------ */

void DrawRoom(ReiPan_ConfigTitleScene &s)
{
    const float em = ImGui::GetFontSize();
    char label[64];

    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::InputText(UiText("Name##scene_name"), s.name, sizeof(s.name));

    RoomLabel(s.room, label, sizeof(label));
    ImGui::SetNextItemWidth(em * 18.0f);
    if (ImGui::BeginCombo(UiText("Room##room_pick"), label, ImGuiComboFlags_HeightLarge))
    {
        for (int room = 1; room < TitleRoomCount(); room++)
        {
            if (TitleRoomPlace(room)[0] == '\0')
            {
                continue;
            }

            const bool on_disc = TitleRoomIsOnDisc(room) != 0;
            RoomLabel(room, label, sizeof(label));
            ImGui::PushID(room);

            if (ImGui::Selectable(label, room == s.room,
                                  on_disc ? 0 : ImGuiSelectableFlags_Disabled))
            {
                if (room != s.room)
                {
                    s.room = room;
                    s.camera_set = 0;
                    g_capture_moved_room = -1;
                }
            }
            if (room == s.room)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    Tooltip("Rooms by the area number the game gives them and their model\n"
            "file's name.  A new room starts with the camera placed by\n"
            "itself; set the two points to taste after.  Greyed rooms are\n"
            "not on this disc.");

    ImGui::SameLine();
    if (ImGui::Button(UiText("Reload##room_reload")))
    {
        TitleRoomReload();
    }
    Tooltip("Load the room again from the disc.");

    ImGui::BeginDisabled(!TitleRoomHasNight(s.room));
    bool night = TitleRoomHasNight(s.room) && s.side != 0;
    if (ImGui::Checkbox(UiText("At night##night"), &night))
    {
        s.side = night ? 1 : 0;
    }
    Tooltip("The house's rooms have night lighting of their own, as the game\n"
            "shows them after dark.  The Manor has one lighting only.");
    ImGui::EndDisabled();
}

/* One camera point: capture, hold, and the three values by hand. */
void DrawPoint(int point, float *eye, float *target, float *fov)
{
    const float em = ImGui::GetFontSize();
    const bool on_title = OnTitle();

    ImGui::PushID(point == 0 ? "point_a" : "point_b");

    ImGui::SeparatorText(point == 0 ? UiText("Point A") : UiText("Point B"));

    if (ImGui::Button(UiText("Set from the current view")))
    {
        if (TitleRoomCaptureView(g_selected, point) != 0)
        {
            g_capture_moved_room = Selected().room;
        }
    }
    Tooltip(InGame()
                ? "Take the camera as it is now.  In a game the scene's room\n"
                  "becomes the one Rei is in -- if that is another room, both\n"
                  "points take this view, to be moved apart from there."
                : "Take the camera as it is now -- the path's, or the one you\n"
                  "are flying with \"Fly the camera\" above.");
    ImGui::SameLine();
    if (ImGui::Button(UiText("Look from here")))
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(TitleRoomGetPreview() == point ? -1 : point);
    }
    Tooltip("Hold the camera on this point; again to let the path run.");
    if (on_title && TitleRoomGetPreview() == point)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(holding)");
    }

    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::DragFloat3(UiText("Eye"), eye, 0.25f, -100000.0f, 100000.0f, "%.1f");
    Tooltip("Where the camera is, in the game's world units.  Y grows\n"
            "downwards; Rei is about seventeen tall.");
    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::DragFloat3(UiText("Looks at"), target, 0.25f, -100000.0f, 100000.0f, "%.1f");
    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::SliderFloat(UiText("Field of view"), fov, 10.0f, 120.0f, UiText("%.1f deg"));

    ImGui::PopID();
}

void DrawCamera(ReiPan_ConfigTitleScene &s)
{
    const float em = ImGui::GetFontSize();
    const bool shown = TitleRoomCurrentScene() == g_selected;

    static const char *const kMotions[] = {
        UiText("There and back"), UiText("A to B, then cut back"), UiText("A to B, then hold"),
    };
    static const char *const kEasings[] = { UiText("Smooth"), UiText("Linear") };

    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::Combo(UiText("Path##motion"), &s.motion, kMotions, IM_ARRAYSIZE(kMotions));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::Combo(UiText("Easing##easing"), &s.easing, kEasings, IM_ARRAYSIZE(kEasings));
    Tooltip("Smooth eases out of each end and into the next.");

    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::DragFloat(UiText("Travel (s)##travel"), &s.travel_seconds, 0.1f, 0.5f, 600.0f,
                     "%.1f");
    Tooltip("Seconds from A to B.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 7.0f);
    ImGui::DragFloat(UiText("Hold (s)##hold"), &s.hold_seconds, 0.1f, 0.0f, 600.0f, "%.1f");
    Tooltip("Seconds the camera rests at each end.");
    if (s.travel_seconds < 0.5f)
    {
        s.travel_seconds = 0.5f;
    }
    if (s.hold_seconds < 0.0f)
    {
        s.hold_seconds = 0.0f;
    }

    /* The live controls drive whatever scene is on screen; with the
     * rotation running that need not be this one. */
    ImGui::BeginDisabled(!shown);
    float progress = TitleRoomGetProgress();
    ImGui::SetNextItemWidth(em * 14.0f);
    if (ImGui::SliderFloat(UiText("Position##progress"), &progress, 0.0f, 1.0f, "%.2f"))
    {
        TitleRoomSetPreview(-1);
        TitleRoomSetProgress(progress);
    }
    Tooltip("Where along the path the camera is -- drag it to scrub.");
    ImGui::SameLine();
    bool paused = TitleRoomGetPaused() != 0;
    if (ImGui::Checkbox(UiText("Pause##paused"), &paused))
    {
        TitleRoomSetPaused(paused ? 1 : 0);
    }

    bool fly = TitleRoomGetFly() != 0;
    if (ImGui::Checkbox(UiText("Fly the camera##fly"), &fly))
    {
        TitleRoomSetPreview(-1);
        TitleRoomSetFly(fly ? 1 : 0);
    }
    Tooltip("Steer the camera by hand, then \"Set from the current view\".\n"
            "With the pad (or the keys bound to it in Settings > Controls):\n"
            "  move     left stick\n"
            "  look     right stick\n"
            "  up/down  R1 / L1\n"
            "  speed    hold L2 for slow, R2 for fast\n"
            "The title's menus do not hear the pad while this is on.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(UiText("Place it by itself##frame")))
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(-1);
        TitleRoomFrameAutomatically(g_selected);
    }
    Tooltip("Put both points back where a new room starts them: the room's\n"
            "first shot of its own below, or with none, along the floor at\n"
            "eye height looking at the middle.");

    /* The designers' own angles on the room on screen. */
    const int shots = shown ? TitleRoomShotCount() : 0;
    const int shot = TitleRoomShotCurrent();
    ImGui::BeginDisabled(shots == 0);
    if (ImGui::ArrowButton("##shot_prev", ImGuiDir_Left) && shots > 0)
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(-1);
        TitleRoomUseShot(g_selected, shot <= 0 ? shots - 1 : shot - 1);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##shot_next", ImGuiDir_Right) && shots > 0)
    {
        TitleRoomSetFly(0);
        TitleRoomSetPreview(-1);
        TitleRoomUseShot(g_selected, shot < 0 || shot >= shots - 1 ? 0 : shot + 1);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (shots == 0)
    {
        ImGui::TextDisabled(shown ? "The room has no fixed shots of its own."
                                  : "The room's own shots: show the scene first.");
    }
    else if (shot >= 0)
    {
        ImGui::Text("The room's own shot %d of %d", shot + 1, shots);
    }
    else
    {
        ImGui::Text("The room's own shots: %d", shots);
    }
    Tooltip("The fixed cameras the game cuts between in this room, as its\n"
            "designers placed them.  Each sets both points: that eye and\n"
            "field of view, panning a few degrees either side of its aim.");
    if (!shown)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Show it##show_selected"))
        {
            Select(g_selected);
        }
    }

    DrawPoint(0, s.a_eye, s.a_target, &s.a_fov);
    DrawPoint(1, s.b_eye, s.b_target, &s.b_fov);

    if (g_capture_moved_room >= 0)
    {
        ImGui::TextColored(ImVec4(1.00f, 0.78f, 0.35f, 1.0f),
                           "This scene's room is now %s: both points took "
                           "that view.", TitleRoomCode(g_capture_moved_room));
    }
}

void DrawLook()
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    const float em = ImGui::GetFontSize();

    ImGui::SetNextItemWidth(em * 14.0f);
    ImGui::SliderFloat(UiText("Dim under the logo##dim"), &t.dim, 0.0f, 1.0f, "%.2f");
    Tooltip("Black laid over the room and under the logo and menus, to keep\n"
            "them readable over a bright room.");

    bool movie = t.attract_movie != 0;
    if (ImGui::Checkbox(UiText("Play the attract movie when idle##attract"), &movie))
    {
        t.attract_movie = movie ? 1 : 0;
    }
    Tooltip("The original rolls a movie after thirty idle seconds on PRESS\n"
            "START.  Off keeps the room up.  The original background always\n"
            "plays it.");
}

void DrawMusic()
{
    ReiPan_ConfigTitle &t = reipan_config.title;
    const float em = ImGui::GetFontSize();
    char label[96];
    const int current = TitleRoomMusicIndex(t.music);

    auto track_label = [&label](int index) {
        const char *note = TitleRoomMusicNote(index);
        if (note[0] != '\0')
        {
            std::snprintf(label, sizeof(label), "%s - %s",
                          TitleRoomMusicName(index), note);
        }
        else
        {
            std::snprintf(label, sizeof(label), "%s",
                          TitleRoomMusicName(index));
        }
        return label;
    };

    ImGui::SetNextItemWidth(em * 24.0f);
    if (ImGui::BeginCombo(UiText("Track##track"), current >= 0 ? track_label(current) : "?",
                          ImGuiComboFlags_HeightLarge))
    {
        for (int i = 0; i < TitleRoomMusicCount(); i++)
        {
            const bool on_disc = TitleRoomMusicIsOnDisc(i) != 0;

            ImGui::PushID(i);
            if (ImGui::Selectable(track_label(i), i == current,
                                  on_disc ? 0 : ImGuiSelectableFlags_Disabled))
            {
                t.music = TitleRoomMusicFile(i);
                TitleRoomMusicChanged();
            }
            if (i == current)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    Tooltip("Original is the title theme the way the game plays it.\n"
            "Anything else loops for as long as the title is up, and stops\n"
            "with it as the theme does.  The KAN tracks are the rooms' own\n"
            "ambience.  Greyed tracks are not on this disc.");

    ImGui::BeginDisabled(t.music == 0);
    ImGui::SetNextItemWidth(em * 14.0f);
    if (ImGui::SliderFloat(UiText("Volume##volume"), &t.music_volume, 0.0f, 1.0f, "%.2f"))
    {
        TitleRoomMusicChanged();
    }
    Tooltip("1 is the level the game plays its music at.");
    ImGui::EndDisabled();
}

} // namespace

namespace ReiPanUiTitle
{

void MenuItem()
{
    ImGui::MenuItem(UiText("Title screen..."), nullptr, &g_show_title);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("The title screen's backdrop -- scenes from the\n"
                          "game, or the original movie -- their cameras and\n"
                          "the music.");
    }
}

void Draw()
{
    if (!g_show_title)
    {
        TitleRoomHoldScene(0);
        g_selected = -1;
        return;
    }

    /* Opening on the scene the title is showing. */
    if (g_selected < 0)
    {
        g_selected = TitleRoomCurrentScene();
    }
    TitleRoomHoldScene(g_rotate_while_open ? 0 : 1);

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 36.0f, em * 50.0f),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(UiText("ReiPan Title Screen"), &g_show_title))
    {
        ImGui::End();
        return;
    }

    DrawStatus();
    ImGui::Spacing();
    DrawBackdrop();

    ImGui::BeginDisabled(reipan_config.title.backdrop !=
                         REIPAN_TITLE_BACKDROP_ROOM);
    if (ImGui::CollapsingHeader(UiText("Scenes##scenes_header"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawSceneList();
        DrawClipboard();
    }

    ReiPan_ConfigTitleScene &s = Selected();
    char header[REIPAN_TITLE_NAME_MAX + 32];
    std::snprintf(header, sizeof(header), UiText("Scene: %s###scene_header"), s.name);
    if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawRoom(s);
        DrawCamera(s);
    }
    if (ImGui::CollapsingHeader(UiText("Look##look_header"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawLook();
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader(UiText("Music##music_header"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawMusic();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Saved to reipan.ini, [title] and [title.sceneN].");

    ImGui::End();
}

} // namespace ReiPanUiTitle
