/* ==========================================================================
 *  reipan/debug/reipan_events_window.cpp
 *
 *  The Debug menu's event browser window (reipan_events.h): every event in
 *  the game, Hour by Hour, with a way to get to each. After MioPan's
 *  miopan_ui_events.cpp.
 *
 *  Story events sit under their Hour as the event tree has them -- the
 *  Hour's start event, then its parts in order, then the story beats inside
 *  each part. The room events (ev1060's tree: examine points, doors,
 *  pick-ups, which run in every Hour) are a second tab, by area, and the
 *  trees that belong to no Hour or area a third.
 *
 *  Two ways to get to an event:
 *
 *    Jump  a new game in its Hour loaded straight where it starts, with
 *          everything it waits on settled. Story events only.
 *    Go    the player taken to where it starts in the game as it stands --
 *          nothing else about the game changes.
 *
 *  and, on the right-click menu, the ROM's own forced state changes for when
 *  a condition will not come true by itself. Clicking a row shows all of the
 *  event -- conditions, programs with their arguments, live state -- in the
 *  pane at the bottom, with a button to copy it. reipan_events.cpp holds the
 *  work; this file only draws.
 * ======================================================================== */
#include "reipan/debug/reipan_events.h"
#include "../reipan_ui/reipan_ui_locale.h"

#include "ingame/event/prg/ev_change.h"         // EV_STATE_*
#include "ingame/event/prg/ev_get.h"            // EV_TBL_OPEN_COND / EV_TBL_CLOSE_COND

#include <imgui.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using ReiPanUiLocale::UiText;


using ReiPanEvents::Catalogue;
using ReiPanEvents::Event;

char g_filter[64] = "";
bool g_hide_flags = true;
int  g_selected   = -1;

void Tooltip(const char *text)
{
    if (text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

std::string Lower(const char *text)
{
    std::string out = text;

    for (char &c : out)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return out;
}

// An event with no program and no sub-events is a flag other events test --
// nothing to see, nothing to go to.
bool IsFlag(const Event &ev)
{
    return ev.subs.empty() && !ev.has_program;
}

ImVec4 StateColour(int state, bool armed)
{
    if (armed)
    {
        return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    }
    switch (state)
    {
    case EV_STATE_INIT:
    case EV_STATE_EXE:
    case EV_STATE_WAIT_END:
        return ImVec4(1.00f, 0.78f, 0.35f, 1.0f);
    case EV_STATE_SUSPEND:
    case 7:
    case 8:
        return ImVec4(0.60f, 0.65f, 0.90f, 1.0f);
    case EV_STATE_END:
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    case EV_STATE_LOCK:
        return ImVec4(0.75f, 0.45f, 0.45f, 1.0f);
    default:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

// What the rows need to know once a frame rather than once a row.
struct Rows
{
    const Catalogue &cat;
    bool             can_jump;
    const char      *jump_why;
    bool             can_go;
    const char      *go_why;
    std::string      filter;
};

void EventTooltip(const Catalogue &cat, int id)
{
    if (!ImGui::BeginItemTooltip())
    {
        return;
    }

    const Event &ev = cat.events[id];
    ImGui::Text("ev%d", id);
    if (ev.chapter >= 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ReiPanEvents::ChapterLabel(ev.chapter));
    }
    if (ev.entry)
    {
        ImGui::TextDisabled(UiText("The Hour's start: what a new game in it runs."));
    }
    else if (ev.intro_leaf)
    {
        ImGui::TextDisabled(UiText("The Hour's set-up: runs as soon as its start has."));
    }

    ImGui::SeparatorText(UiText("Opens on"));
    ImGui::TextUnformatted(ReiPanEvents::DescribeConditions(id, EV_TBL_OPEN_COND).c_str());
    const std::string close = ReiPanEvents::DescribeConditions(id, EV_TBL_CLOSE_COND);
    if (close.find("nothing") != 0)
    {
        ImGui::SeparatorText(UiText("Closes on"));
        ImGui::TextUnformatted(close.c_str());
    }
    ImGui::SeparatorText(UiText("Programs"));
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(ReiPanEvents::DescribePrograms(id, false).c_str());
    ImGui::PopTextWrapPos();
    if (!ev.place.Known() && ev.go.Known())
    {
        ImGui::Separator();
        ImGui::TextDisabled(UiText("No place of its own; Go takes her to %s."), ReiPanEvents::DescribePlace(ev.go).c_str());
    }
    ImGui::TextDisabled(UiText("Click for everything, with the arguments."));
    ImGui::EndTooltip();
}

void EventContextMenu(int id)
{
    if (!ImGui::BeginPopupContextItem())
    {
        return;
    }

    const int  state   = ReiPanEvents::StateOf(id);
    const bool in_game = state >= 0;
    const bool running = state == EV_STATE_INIT || state == EV_STATE_EXE || state == EV_STATE_WAIT_END;

    ImGui::TextDisabled("ev%d%s%s", id, in_game ? " -- " : "", in_game ? ReiPanEvents::StateName(state) : "");
    ImGui::Separator();
    if (ImGui::MenuItem(UiText("Start now"), nullptr, false, in_game && !running && state != EV_STATE_LOCK))
    {
        ReiPanEvents::ForceStart(id);
    }
    Tooltip(UiText("Run it without its open conditions -- the ROM's forced start\n"
            "(EV_SET_STATE 1), applied in the next frame's event pass.\n"
            "What it expects (a room, a ghost, an NPC) is not set up for it."));
    if (ImGui::MenuItem(UiText("Mark as ended"), nullptr, false,
                        in_game && !running && state != EV_STATE_END && state != EV_STATE_LOCK))
    {
        ReiPanEvents::ForceEnd(id);
    }
    Tooltip(UiText("Set it to ended without running it, so whatever waits on it\n"
            "can go ahead."));
    if (ImGui::MenuItem(UiText("Waiting again"), nullptr, false, in_game && !running && state != EV_STATE_WAIT_OPEN))
    {
        ReiPanEvents::Rearm(id);
    }
    Tooltip(UiText("Put it back to waiting for its open conditions, so it can\n"
            "happen again."));
    ImGui::Separator();
    if (ImGui::MenuItem(UiText("Copy id")))
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%d", id);
        ImGui::SetClipboardText(text);
    }
    ImGui::EndPopup();
}

// Columns 1..4 of an event's row.
void EventCells(const Rows &rows, int id)
{
    const Event &ev = rows.cat.events[id];

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(ev.trigger.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(ev.content.c_str());

    ImGui::TableSetColumnIndex(3);
    const int state = ReiPanEvents::StateOf(id);
    if (state >= 0)
    {
        const bool armed = ReiPanEvents::IsArmed(id);
        ImGui::TextColored(StateColour(state, armed), "%s", armed ? "armed" : ReiPanEvents::StateName(state));
    }

    ImGui::TableSetColumnIndex(4);
    ImGui::PushID(id);
    if (ev.chapter >= 0)
    {
        ImGui::BeginDisabled(!rows.can_jump);
        if (ImGui::SmallButton(UiText("Jump")))
        {
            ReiPanEvents::JumpToEvent(id);
        }
        ImGui::EndDisabled();
        Tooltip(rows.can_jump ? "A new game in this Hour, loaded straight where this\n"
                                "event starts, with everything it waits on settled."
                              : rows.jump_why);
        ImGui::SameLine();
    }

    const bool can_go = rows.can_go && ev.go.Known();
    ImGui::BeginDisabled(!can_go);
    if (ImGui::SmallButton(UiText("Go")))
    {
        ReiPanEvents::TeleportToEvent(id);
    }
    ImGui::EndDisabled();
    if (!ev.go.Known())
    {
        Tooltip(UiText("Its conditions name no place to go to."));
    }
    else if (!rows.can_go)
    {
        Tooltip(rows.go_why);
    }
    else
    {
        const std::string text = "Take her to " + ReiPanEvents::DescribePlace(ev.go)
                               + " in the game as it stands\n(into the other world: the game is reloaded there).";
        Tooltip(text.c_str());
    }
    ImGui::PopID();
}

ImGuiTreeNodeFlags RowFlags(int id, bool leaf)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

    if (leaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    else
    {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    }
    if (id == g_selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    return flags;
}

// One row, as a tree node; recurses into its sub-events.
void EventNode(const Rows &rows, int id, const std::string &label)
{
    const Event &ev = rows.cat.events[id];

    if (g_hide_flags && IsFlag(ev))
    {
        return;
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    const bool leaf = ev.subs.empty();
    const bool open = ImGui::TreeNodeEx((void *)(intptr_t)id, RowFlags(id, leaf), "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        g_selected = id;
    }
    EventTooltip(rows.cat, id);
    EventContextMenu(id);
    EventCells(rows, id);

    if (open && !leaf)
    {
        for (int sub : ev.subs)
        {
            const Event &child = rows.cat.events[sub];
            char         text[48];
            if (child.place.label < 0 && child.place.area >= 0 && !child.subs.empty())
            {
                std::snprintf(text, sizeof(text), "Area %d  ev%d", child.place.area, sub);
            }
            else
            {
                std::snprintf(text, sizeof(text), "ev%d", sub);
            }
            EventNode(rows, sub, text);
        }
        ImGui::TreePop();
    }
}

bool Matches(const Rows &rows, int id)
{
    const Event &ev = rows.cat.events[id];

    return ev.search.find(rows.filter) != std::string::npos && !(g_hide_flags && IsFlag(ev));
}

// Under a filter: every match below `root`, flat, in tree order.
void MatchRows(const Rows &rows, int root)
{
    if (Matches(rows, root))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TreeNodeEx((void *)(intptr_t)root, RowFlags(root, true), "ev%d", root);
        if (ImGui::IsItemClicked())
        {
            g_selected = root;
        }
        EventTooltip(rows.cat, root);
        EventContextMenu(root);
        EventCells(rows, root);
    }
    for (int sub : rows.cat.events[root].subs)
    {
        MatchRows(rows, sub);
    }
}

int CountMatches(const Rows &rows, int root)
{
    int count = Matches(rows, root) ? 1 : 0;

    for (int sub : rows.cat.events[root].subs)
    {
        count += CountMatches(rows, sub);
    }
    return count;
}

bool BeginEventTable(const char *id)
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 5, flags))
    {
        return false;
    }

    const float em = ImGui::GetFontSize();
    ImGui::TableSetupColumn(UiText("Event"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn(UiText("Starts on"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
    ImGui::TableSetupColumn(UiText("Does"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn(UiText("State"), ImGuiTableColumnFlags_WidthFixed, em * 6.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, em * 5.5f);
    ImGui::TableHeadersRow();
    return true;
}

// An Hour's children: its start event, then its parts, then whatever else
// hangs off the chapter event directly.
void ChapterRows(const Rows &rows, int chapter_event)
{
    int part = 0;

    for (int sub : rows.cat.events[chapter_event].subs)
    {
        const Event &ev = rows.cat.events[sub];
        char         label[48];
        if (ev.entry)
        {
            std::snprintf(label, sizeof(label), "Start  ev%d", sub);
        }
        else if (!ev.subs.empty())
        {
            std::snprintf(label, sizeof(label), "Part %d  ev%d", ++part, sub);
        }
        else
        {
            std::snprintf(label, sizeof(label), "ev%d", sub);
        }
        EventNode(rows, sub, label);
    }
}

void DrawStoryTab(const Rows &rows)
{
    const int playing = ReiPanEvents::CurrentChapter();
    const int root    = rows.cat.chapter_root;

    // ev88's sub-events that are no Hour: the new game's set-up.
    if (rows.filter.empty() && BeginEventTable("##setup"))
    {
        for (int sub : rows.cat.events[root].subs)
        {
            bool hour = false;
            for (int c = 0; c < ReiPanEvents::kChapterCount; c++)
            {
                hour |= rows.cat.chapter_event[c] == sub;
            }
            if (!hour)
            {
                char label[48];
                std::snprintf(label, sizeof(label), "New game  ev%d", sub);
                EventNode(rows, sub, label);
            }
        }
        ImGui::EndTable();
    }

    for (int c = 0; c < ReiPanEvents::kChapterCount; c++)
    {
        const int chapter_event = rows.cat.chapter_event[c];
        if (chapter_event < 0)
        {
            continue;
        }

        ImGui::PushID(c);
        if (!rows.filter.empty())
        {
            if (CountMatches(rows, chapter_event) != 0)
            {
                ImGui::SeparatorText(ReiPanEvents::ChapterLabel(c));
                if (BeginEventTable("##matches"))
                {
                    MatchRows(rows, chapter_event);
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
            continue;
        }

        char header[96];
        std::snprintf(header, sizeof(header), "%s%s###chapter", ReiPanEvents::ChapterLabel(c),
                      c == playing ? "   (playing)" : "");
        if (ImGui::CollapsingHeader(header))
        {
            ImGui::BeginDisabled(!rows.can_jump);
            if (ImGui::Button(UiText("Start from the beginning")))
            {
                ReiPanEvents::StartChapter(c);
            }
            ImGui::EndDisabled();
            Tooltip(rows.can_jump ? "A new game in this Hour, from its start event: FF3's\n"
                                    "chapter select, as the start point window warps."
                                  : rows.jump_why);
            ImGui::SameLine();
            ImGui::TextDisabled("ev%d", chapter_event);

            if (BeginEventTable("##chapter"))
            {
                ChapterRows(rows, chapter_event);
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
}

void DrawTreeTab(const Rows &rows, int root, const char *table_id)
{
    if (root < 0)
    {
        return;
    }
    if (BeginEventTable(table_id))
    {
        if (rows.filter.empty())
        {
            for (int sub : rows.cat.events[root].subs)
            {
                const Event &ev = rows.cat.events[sub];
                char         label[48];
                if (ev.place.label < 0 && ev.place.area >= 0)
                {
                    std::snprintf(label, sizeof(label), "Area %d  ev%d", ev.place.area, sub);
                }
                else
                {
                    std::snprintf(label, sizeof(label), "ev%d", sub);
                }
                EventNode(rows, sub, label);
            }
        }
        else
        {
            MatchRows(rows, root);
        }
        ImGui::EndTable();
    }
}

void DrawSharedTab(const Rows &rows)
{
    if (!BeginEventTable("##shared"))
    {
        return;
    }
    for (int root : rows.cat.other_roots)
    {
        if (rows.filter.empty())
        {
            char label[48];
            std::snprintf(label, sizeof(label), root == 1 ? "Everywhere  ev%d" : "ev%d", root);
            EventNode(rows, root, label);
        }
        else
        {
            MatchRows(rows, root);
        }
    }
    ImGui::EndTable();
}

// A condition stream judged now, one line per condition: "yes" / "no" in
// colour, "?" where the window cannot judge it without side effects.
void DrawJudged(const char *title, const std::vector<std::vector<ReiPanEvents::LiveCond>> &groups,
                std::string &copy)
{
    ImGui::TextUnformatted(title);
    copy += std::string("\n") + title;
    for (size_t g = 0; g < groups.size(); g++)
    {
        if (g != 0)
        {
            ImGui::TextDisabled(UiText("  or"));
            copy += "\n  or";
        }
        if (groups[g].empty())
        {
            ImGui::TextDisabled(UiText("    nothing (at once)"));
            copy += "\n    nothing (at once)";
        }
        for (const ReiPanEvents::LiveCond &c : groups[g])
        {
            const char  *mark   = c.live == 1 ? "yes" : c.live == 0 ? " no" : "  ?";
            const ImVec4 colour = c.live == 1   ? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
                                : c.live == 0   ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f)
                                                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::TextColored(colour, "    %s", mark);
            ImGui::SameLine();
            ImGui::TextUnformatted(c.text.c_str());
            copy += std::string("\n    ") + mark + " " + c.text;
        }
    }
}

// Everything about the selected event, with the arguments and its
// conditions judged now: what is needed to see why it does not open, or what
// it did.
void DrawDetails(const Catalogue &cat)
{
    if (g_selected < 0 || g_selected >= (int)cat.events.size())
    {
        ImGui::TextDisabled(UiText("Click an event to see all of it here."));
        return;
    }

    const int    id = g_selected;
    const Event &ev = cat.events[id];
    std::string  text;
    char         line[160];

    std::snprintf(line, sizeof(line), "ev%d", id);
    text += line;
    if (ev.chapter >= 0)
    {
        text += std::string("  (") + ReiPanEvents::ChapterLabel(ev.chapter) + ")";
    }
    const int state = ReiPanEvents::StateOf(id);
    if (state >= 0)
    {
        std::snprintf(line, sizeof(line), "\nstate: %s (%d), ran %d time(s)%s", ReiPanEvents::StateName(state), state,
                      ReiPanEvents::RunCount(id), ReiPanEvents::IsArmed(id) ? ", armed" : "");
        text += line;
    }
    std::snprintf(line, sizeof(line), "\nparent: ev%d   sub-events: %d", ev.parent, (int)ev.subs.size());
    text += line;
    if (!ev.subs.empty())
    {
        text += " (";
        for (size_t i = 0; i < ev.subs.size(); i++)
        {
            std::snprintf(line, sizeof(line), i == 0 ? "ev%d" : ", ev%d", ev.subs[i]);
            text += line;
        }
        text += ")";
    }
    if (ev.go.Known())
    {
        text += "\ngo: " + ReiPanEvents::DescribePlace(ev.go) + (ev.place.Known() ? "" : UiText(" (inherited)"));
    }
    const std::string programs = ReiPanEvents::DescribePrograms(id, true);

    // Drawn first and copied a frame late: the copy is built as it draws.
    static std::string copy;
    if (ImGui::SmallButton(UiText("Copy")))
    {
        ImGui::SetClipboardText(copy.c_str());
    }
    Tooltip(UiText("Copy all of this, the judgement included, to paste in a note\n"
            "or a bug report."));
    ImGui::SameLine();
    if (ImGui::SmallButton(UiText("Parent")) && ev.parent >= 0)
    {
        g_selected = ev.parent;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UiText("Clear")))
    {
        g_selected = -1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(UiText("(conditions judged now: yes / no; ? not judged here)"));

    ImGui::BeginChild("##details_text", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(text.c_str());
    copy = text + "\n";
    ImGui::Spacing();
    DrawJudged("opens on:", ReiPanEvents::JudgeConditions(id, EV_TBL_OPEN_COND), copy);
    DrawJudged("closes on:", ReiPanEvents::JudgeConditions(id, EV_TBL_CLOSE_COND), copy);
    ImGui::Spacing();
    ImGui::TextUnformatted(programs.c_str());
    copy += "\n\n" + programs;
    ImGui::EndChild();
}

// Before the event file has been read only Hours can be started -- the list
// of what is in them comes out of the file.
void DrawWithoutCatalogue(bool can_start, const char *why)
{
    ImGui::TextWrapped(UiText("The event list is read out of the game's event file, which a game loads. "
                       "Start an Hour (or any game) and it fills in; it stays up at the title."));
    ImGui::Separator();

    for (int c = 0; c < ReiPanEvents::kChapterCount; c++)
    {
        ImGui::PushID(c);
        ImGui::BeginDisabled(!can_start);
        if (ImGui::Button(ReiPanEvents::ChapterLabel(c)))
        {
            ReiPanEvents::StartChapter(c);
        }
        ImGui::EndDisabled();
        Tooltip(can_start ? UiText("A new game in this Hour, from its start event.") : why);
        ImGui::PopID();
    }
}

void DrawHeader()
{
    const std::string status = ReiPanEvents::Status();
    const auto translated = ReiPanUiLocale::Translate(status);
    ImGui::TextWrapped("%.*s", (int)translated.size(), translated.data());
    if (ReiPanEvents::Busy())
    {
        if (ReiPanEvents::CanProceed())
        {
            if (ImGui::SmallButton(UiText("Teleport anyway")))
            {
                ReiPanEvents::Proceed();
            }
            Tooltip(UiText("Do not wait for the event program that is running. The\n"
                    "room change cuts it short, so expect it to misbehave."));
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(UiText("Cancel")))
        {
            ReiPanEvents::Cancel();
        }
    }

    const ReiPanEvents::Place here = ReiPanEvents::PlayerPlace();
    if (here.Known())
    {
        ImGui::TextDisabled(UiText("Playing %s, %s, %s"), ReiPanEvents::ChapterLabel(ReiPanEvents::CurrentChapter()),
                            ReiPanEvents::DescribePlace(here).c_str(),
                            ReiPanEvents::CurrentWorld() == 1 ? UiText("real world") : "dream");
    }
    else
    {
        ImGui::TextDisabled(UiText("Not in a game."));
    }
}

} // namespace

void ReiPan_EventsDrawWindow(bool *open)
{
    if (open == nullptr || !*open)
    {
        return;
    }

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 62.0f, em * 44.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(UiText("Event browser"), open))
    {
        ImGui::End();
        return;
    }

    DrawHeader();

    const char *jump_why = nullptr;
    const char *go_why   = nullptr;
    const bool  can_jump = ReiPanEvents::CanStartChapter(&jump_why);
    const bool  can_go   = ReiPanEvents::CanTeleport(&go_why);

    const Catalogue *cat = ReiPanEvents::GetCatalogue();
    if (cat == nullptr)
    {
        ImGui::Separator();
        DrawWithoutCatalogue(can_jump, jump_why);
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(em * 16.0f);
    ImGui::InputTextWithHint("##filter", UiText("Filter: ev438, movie, area 47, examine..."), g_filter, sizeof(g_filter));
    ImGui::SameLine();
    ImGui::Checkbox(UiText("Hide flags"), &g_hide_flags);
    Tooltip(UiText("Hide events with no program and no sub-events: markers other\n"
            "events test, with nothing to see."));
    if (!cat->from_memory)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(UiText("(event file from the last game)"));
    }

    const Rows rows = {*cat, can_jump, jump_why, can_go, go_why, Lower(g_filter)};

    // The list above, the selected event's details below.
    const float details = em * 14.0f;
    const float list    = ImGui::GetContentRegionAvail().y - details - ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##list", ImVec2(0, list > em * 6.0f ? list : em * 6.0f));
    if (ImGui::BeginTabBar("##events"))
    {
        if (ImGui::BeginTabItem(UiText("Story")))
        {
            ImGui::BeginChild("##story");
            DrawStoryTab(rows);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("Rooms")))
        {
            ImGui::TextDisabled(UiText("Examine points, doors and pick-ups, a group per area. They run in "
                                "every Hour; their conditions say which."));
            ImGui::BeginChild("##rooms");
            DrawTreeTab(rows, cat->room_root, "##room_events");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(UiText("Shared")))
        {
            ImGui::TextDisabled(UiText("The bed, the ways between the worlds, and the trees of no Hour or area."));
            ImGui::BeginChild("##shared");
            DrawSharedTab(rows);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::SeparatorText(UiText("Details"));
    ImGui::BeginChild("##details");
    DrawDetails(*cat);
    ImGui::EndChild();

    ImGui::End();
}
