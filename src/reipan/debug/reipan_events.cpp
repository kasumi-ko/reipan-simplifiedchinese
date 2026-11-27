/* ==========================================================================
 *  reipan/debug/reipan_events.cpp
 *
 *  The event browser's engine (reipan_events.h): what FF3's events are, and
 *  how to take the game to one of them. After MioPan's
 *  miopan_event_jump.cpp, with FF3's tree, conditions and two worlds.
 *
 *  THE DATA. Every event lives in the event file (EVENT_OBJ, or the
 *  developers' EVENT_DB_OBJ) that EventDataLoad puts at EVENT_DATA_ADDR:
 *  seven tables of one stream per event id (ev_get.h has the layout). The
 *  ids form one tree, measured on the disc:
 *
 *      ev0 ─┬─ ev1 ──── what runs everywhere: the bed (falling asleep,
 *           │           waking up), the ways between the worlds
 *           ├─ ev88 ─── ev89, the new game's set-up, then a chapter event
 *           │           per Hour opening on EV_NOW_CHAPTER(b01): ev148 the
 *           │           prologue, ev321 Hour I, ev437 Hour II, ev90 Hour III..
 *           ├─ ev1060 ─ the room events: a group per area opening on
 *           │           PLYR_ROOMIN, then its examine points, doors, pick-ups
 *           └─ ev1602, ev1746 (EVENT_DB_OBJ's debug start), ev1754, ev1763
 *
 *  Every chapter event has the same shape. Its first sub-event is the
 *  chapter's start (ev438: wake Rei in her bedroom, CHANGE_CHAPTER, mark the
 *  other chapters' starts ended), which a new game in that chapter runs --
 *  FF3's chapter select, what the start point window warps to. The rest are
 *  its parts, chained by their open conditions: a part opens on
 *  EV_STATE(previous part = END), often with CHECK_GAME_WORLD. So
 *  "everything before this event" is not a list anyone wrote down: it is
 *  the closure of the EV_STATE references in the open conditions of the
 *  event and of its ancestors, which FastForward() computes.
 *
 *  FF3 adds two states to FF2's: 7 and 8 park an event until the next
 *  EVENT_REBOOT 7 (waking up) or 8 (falling asleep) puts it back to
 *  waiting. The room events park themselves in 8 when they end, so they
 *  come back every dream.
 *
 *  THE DRIVER. A jump is one load and three things done in order by Step(),
 *  at the top of every frame (ReiPan_EventsFrame, after the warp's work):
 *
 *    1. Load where the target is. A new game in the chapter that starts in
 *       the target's area and world (ReiPan_WarpJump: leave play, New Game,
 *       b01, the area, world and side set in Story_NowLoading) with the
 *       player on its rectangle, facing the way its PLYR_ROT or
 *       PLYR_INTO_SIGHT asks.
 *
 *    2. Fast-forward. In Story_NowLoading, before any event has opened,
 *       SettleJump() marks the chapter's start event ended -- the load was
 *       its move -- and forces every prerequisite of the target to the state
 *       its condition asks for through CompulsionSetEventState(),
 *       EV_SET_STATE's own path. The parts before the target's are among
 *       them, so they never open and their scenes never play. The chapters'
 *       set-up leaves are left to run. The target's own direct prerequisites
 *       are held back until the player is in place: an event that waits only
 *       on an EV_STATE would otherwise fire before she is.
 *
 *       Then, once the new game's set-up (ev89) has ended and the chapter's
 *       event has opened -- when the start event would have run -- Replay()
 *       runs the start event's commands that leave state behind (the camera,
 *       items, PHOTO_UNLOCK, doors, objects) and the world's own.
 *
 *       A skipped event can be the one that hands back a lock (PLYR_LOCK,
 *       PLYR_PAD) or fades the screen in. Once the game has sat locked or
 *       black for a second with no event program running,
 *       ReleaseStrandedLocks() does what PLYR_LOCK 0, PLYR_PAD 1 and
 *       FADE_IN would have.
 *
 *    3. Teleport, if need be. Each frame StepAdvance() finds the first event
 *       on the path down to the target that has not opened and, if its
 *       conditions name a place -- or a world -- the player is not in, takes
 *       her there (ReiPan_WarpGoTo): within the world the room loads as
 *       AREA_CHANGE does, into the other world the game is reloaded from
 *       memory there. Go uses the same move.
 *
 *  An event with no place of its own or inherited goes the long way: the
 *  chapter's own start (ReiPan_WarpChapter), the fast-forward at the top of
 *  the first frame its start event is over, then the walk.
 *
 *  The driver only teleports while the game is quiet: Story_Normal with
 *  nothing requested, no event program running, no message up. When one is,
 *  the status line says which, and Proceed() overrides it.
 * ======================================================================== */
#include "reipan/debug/reipan_events.h"
#include "reipan/reipan_ui/reipan_ui_locale.h"

#include "graphics/graph2d/fade.h"              // fade_ctrl, FadeInReq
#include "ingame/event/prg/ev_change.h"         // GetEvState, CompulsionSetEventState, GetEvCondWrk, EV_STATE_*
#include "ingame/event/prg/ev_command.h"        // EvPlyrLock, EvPlyrPad
#include "ingame/event/prg/ev_exe.h"            // CheckEventExeEntry, EventEnd_SubStateChange, EV_EXE_CTRL
#include "ingame/event/prg/ev_get.h"            // EV_TBL_*, EvGetCmdArgs
#include "ingame/event/prg/ev_macro.h"          // EvMacroGetWrk, EV_COM_*
#include "ingame/event/prg/ev_main.h"           // EVENT_DATA_ADDR
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/map/map_rectangle.h"           // MrecIsInEvent
#include "ingame/plyr/player.h"                 // GetPlyrAreaNo, PLYR_WRK
#include "ingame/plyr/plyr_sub.h"               // PlyrGetJointRotY
#include "reipan/debug/reipan_warp.h"           // ReiPan_WarpChapter / GoTo, ReiPanWarpSpot
#include "reipan/io/reipan_log.h"               // ReiPan_LogPrintf
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer
#include "system/pad/pad_ctrl.h"                // IsPadLocked

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ReiPanEvents
{
using ReiPanUiLocale::UiText;

namespace
{

// EVENT_OBJ and EVENT_DB_OBJ have edition-specific sizes and table lengths.
// Keep the original event state capacity for compatibility with saved games.
#ifdef REIPAN_SLPS_25544_CHS
constexpr int kEventCount = 1780;
constexpr u_int kPakSize   = 0x40a8c;
constexpr int kChapterRoot = 87;
constexpr int kRoomRoot    = 1059;
#else
constexpr int kEventCount = EV_EVENT_NUM;
constexpr u_int kPakSize   = 0x40b30;
constexpr int kChapterRoot = 88;
constexpr int kRoomRoot    = 1060;
#endif
constexpr u_int kPakHeader = EV_TBL_MAX * 4;
constexpr u_int kPakTables = kPakHeader + EV_TBL_MAX * kEventCount * 4;

// Condition labels: rows of the condition table (ev_change.c ev_cond_wrk,
// tools/reverse/evtbl.py cond), which the streams store -- data, not the
// port's choice. EV_COND_OR / EV_COND_END are the structural bytes.
enum
{
    kCondPlyrAreaIn       = 0,
    kCondPlyrAreaInDef    = 1,
    kCondPlyrAreaOut      = 2,
    kCondPlyrAreaOutDef   = 3,
    kCondPlyrRoomIn       = 4,
    kCondPlyrRoomOut      = 5,
    kCondPlyrRot          = 6,
    kCondPlyrIntoSight    = 7,
    kCondEvCntExec        = 8,
    kCondDoorStatCheck    = 13,
    kCondGhostExist       = 14,
    kCondGhostLost        = 15,
    kCondGhostReleaseType = 16,
    kCondNpcAreaOut       = 19,
    kCondNpcAreaOutDef    = 20,
    kCondNpcIntoPlyrSight = 21,
    kCondNpcDistance      = 22,
    kCondPushPad          = 23,
    kCondPlyrItemUse      = 24,
    kCondPlyrItemHave     = 25,
    kCondEvState          = 26,
    kCondEvNotState       = 27,
    kCondEvRandom         = 28,
    kCondPhotoObj         = 29,
    kCondFinderMode       = 31,
    kCondObjIntoFinder    = 32,
    kCondObjIntoPlyrSight = 33,
    kCondBattleEnd        = 34,
    kCondBattleMode       = 35,
    kCondChapterLoadWait  = 36,
    kCondEvFailure        = 37,
    kCondPuzzleSuccess    = 39,
    kCondNowChapter       = 40,
    kCondNotNowChapter    = 41,
    kCondNotHaveItem      = 42,
    kCondCheckGameWorld   = 49,
    kCondSelectChoice     = 52,
    kCondPeepingEnd       = 53,
    kCondTelStatus        = 54,
    kCondTelTalkEnd       = 55,
    kCondTapeCheck        = 56,
    kCondCineMenuEnd      = 58,
    kCondCineFilmEnd      = 59,
    kCondCheckDayFlag     = 64,
};

// Opcodes: rows of the command table (ev_macro.c ev_exe_wrk).
enum
{
    kComPlyrPosSet     = 2,
    kComPlyrRotSet     = 4,
    kComPlyrFloorChange = 8,
    kComAreaChange     = 90,
    kComEvSetState     = 112,
    kComElseIf         = 0x95,          // IF_ (EV_COM_IF) and ELSEIF_ carry a condition
};

// The chapter title cards' text (CHAPTER_TITLE_01..14_TM2; the prologue has
// none), by ingame_wrk.b01.
const char *const kChapterNames[kChapterCount] = {
    "Prologue",
    "Hour I: The Sign",
    "Hour II: The Manor of Sleep",
    "Hour III: The Subduing Song",
    "Hour IV: The Evil Dream",
    "Hour V: Vanishing",
    "Hour VI: The Sacrificial Pillar",
    "Hour VII: The Hanging Prison",
    "Hour VIII: The Vacant Dream",
    "Hour IX: The Unleashing",
    "Hour X: The Piercing of Soul",
    "Hour XI: The Last Passage",
    "Hour XII: The Ceremony of Commandment",
    "Hour XIII: Calling of the Tattoo",
    "Final Hour: The Abyss of the Horizon",
};

// Where a world's own scripts put the player when she enters it: event 3
// (falling asleep) area 1, event 2 (waking up) area 47, Rei's bedroom.
const int kWorldArea[2] = {1, 47};

// ---------------------------------------------------------------------------
//  Rectangle -> area
//
//  An event rectangle's label is area_id * 1000 + record
//  (RegDatGetStID4Label), and nothing in the ROM says which of the game's
//  areas holds an area_id: the registration data is only looked at once its
//  area is loaded. A teleport has to know before, so this is measured off
//  the disc: every area's data PK4 (ROS00_DATA_PK4 + area * 7, member 1)
//  holds one registration file per floor, whose MB_OUT_HEAD names its area_id
//  and kai (the floor, 11 + RcbGetFloor's number). area_id 3 is in areas 48
//  and 49; 48 is listed. Areas 37 and 45 have no registration files.
//
//  Being wrong here costs a teleport, nothing more: the warp looks the label
//  up in the registration data once the area is in and says so when it is
//  not there.
// ---------------------------------------------------------------------------

struct LabelArea
{
    signed char area;
    signed char kai;
};

const LabelArea kLabelArea[] = {
    {47, 13}, {48, 11}, {48, 12}, {48, 13}, {49, 11}, {50, 13},   // 0..
    {51, 11}, {52, 13}, {50, 15}, {52, 15}, { 1, 11}, { 2, 11},   // 6..
    { 3, 11}, { 3, 12}, { 3, 13}, { 3, 14}, { 3, 15}, { 4, 11},   // 12..
    { 4, 12}, { 4, 13}, { 5, 13}, { 5, 15}, { 6, 11}, { 6, 13},   // 18..
    { 6, 15}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},   // 24..
    { 7, 11}, { 7, 13}, { 8, 11}, { 8, 12}, { 8, 13}, { 8, 14},   // 30..
    { 8, 15}, { 9, 11}, {10, 11}, {10, 12}, {10, 13}, {11, 11},   // 36..
    {11, 13}, {-1, -1}, {12,  7}, {13, 11}, {14, 11}, {15, 11},   // 42..
    {15, 12}, {15, 13}, {15, 15}, {16, 11}, {16, 15}, {17,  9},   // 48..
    {17, 11}, {17, 13}, {18, 15}, {19, 11}, {-1, -1}, {-1, -1},   // 54..
    {20, 11}, {27, 13}, {27, 14}, {27, 15}, {21, 13}, {22, 11},   // 60..
    {22, 12}, {22, 13}, {23, 11}, {24, 11}, {25,  9}, {25, 10},   // 66..
    {25, 11}, {26,  9}, {26, 10}, {26, 11}, {-1, -1}, {-1, -1},   // 72..
    {-1, -1}, {-1, -1}, {28, 11}, {28, 10}, {28,  9}, {29,  9},   // 78..
    {30,  9}, {30, 10}, {30, 11}, {30, 12}, {30, 13}, {31, 11},   // 84..
    {32, 11}, {33, 11}, {34, 11}, {-1, -1}, {35, 11}, {36, 11},   // 90..
    {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {38, 11}, {39, 15},   // 96..
    {40, 11}, {40, 12}, {40, 13}, {40, 14}, {40, 15}, {41,  9},   // 102..
    {42,  9}, {42,  8}, {42,  7}, {42,  6}, {42,  5}, {42,  4},   // 108..
    {42,  3}, {42,  2}, {42,  1}, {43,  3}, {44,  3}, {46,  9},   // 114..
    {46, 10}, {46, 11},                                           // 120..
};
constexpr int kLabelAreaCount = (int)(sizeof(kLabelArea) / sizeof(kLabelArea[0]));

int AreaOfLabel(int label)
{
    const int area_id = label / 1000;

    return label >= 0 && area_id < kLabelAreaCount ? kLabelArea[area_id].area : -1;
}

int WorldOf(int area)
{
    return area >= 0 ? ReiPan_WarpWorldOf(area) : -1;
}

// ---------------------------------------------------------------------------
//  Reading the file
//
//  The catalogue and every description read a copy of the file taken when
//  it was built, so they outlive the game that loaded it.
// ---------------------------------------------------------------------------

std::vector<u_char> g_pak;

u_char *PakTop()
{
    return (u_char *)ReiPan_GetHostPointer(EVENT_DATA_ADDR);
}

u_int Word(const u_char *top, u_int offset)
{
    u_int value = 0;

    if (offset <= kPakSize - 4)
    {
        std::memcpy(&value, top + offset, 4);
    }
    return value;
}

// The header of a resident event file is fixed: seven table offsets, each
// one event count of words after the last. Nothing else loaded there (the
// mission files, with fewer events) starts that way.
bool PakResident(const u_char *top)
{
    for (u_int t = 0; t < EV_TBL_MAX; t++)
    {
        if (Word(top, t * 4) != kPakHeader + t * kEventCount * 4)
        {
            return false;
        }
    }
    return true;
}

// Event `id`'s stream in `table` of the copy, or nullptr.
u_char *Stream(int table, int id)
{
    if (g_pak.size() < kPakSize)
    {
        return nullptr;
    }
    const u_int offset = Word(g_pak.data(), Word(g_pak.data(), (u_int)table * 4) + (u_int)id * 4);

    return offset >= kPakTables && offset < kPakSize ? g_pak.data() + offset : nullptr;
}

const u_char *PakEnd()
{
    return g_pak.data() + g_pak.size();
}

struct Cond
{
    int     label;
    u_char *p;                          // the entry, label byte first
};
using Group = std::vector<Cond>;

// A condition stream as its alternatives: entries within a group AND, groups
// OR (ev_open.h). A stream with no conditions is one empty group.
std::vector<Group> ParseConds(u_char *s)
{
    std::vector<Group> groups(1);

    for (int guard = 0; s != nullptr && s < PakEnd() && guard < 256; guard++)
    {
        const int label = *s;
        if (label == EV_COND_END)
        {
            break;
        }
        if (label == EV_COND_OR)
        {
            groups.emplace_back();
            s += 4;
            continue;
        }
        if (label >= EV_COND_NUM)
        {
            break;
        }
        const int size = GetEvCondWrk(label)->size;
        if (size <= 0 || s + size > PakEnd())
        {
            break;
        }
        groups.back().push_back({label, s});
        s += size;
    }
    return groups;
}

// Arguments as a handler reads them (its row's format). The rows that take
// none have an empty format, which EvGetCmdArgs would read past.
int Args(u_char *p, const char *fmt, int *args)
{
    return fmt != nullptr && fmt[0] != '\0' ? EvGetCmdArgs(args, p, fmt) : 0;
}

int CondArgs(const Cond &c, int *args)
{
    return Args(c.p, GetEvCondWrk(c.label)->fmt, args);
}

int CondArg(const Cond &c, int i)
{
    int args[8] = {};

    CondArgs(c, args);
    return args[i];
}

struct Op
{
    int     com;
    u_char *p;
};

// A program's commands up to EV_END. IF_ and ELSEIF_ carry their condition
// after their own bytes (EventExeFuncCall steps over both).
std::vector<Op> ParseProgram(u_char *s)
{
    std::vector<Op> ops;

    for (int guard = 0; s != nullptr && s < PakEnd() && guard < 2048; guard++)
    {
        const int com = *s;
        if (com == EV_COM_END)
        {
            break;
        }
        const EV_EXE_WRK *row = EvMacroGetWrk(com);
        if (row == nullptr || row->size <= 0)
        {
            break;
        }
        int size = row->size;
        if ((com == EV_COM_IF || com == kComElseIf) && s + 5 <= PakEnd() && s[4] < EV_COND_NUM)
        {
            size += GetEvCondWrk(s[4])->size;
        }
        if (s + size > PakEnd())
        {
            break;
        }
        ops.push_back({com, s});
        s += size;
    }
    return ops;
}

int OpArgs(const Op &op, int *args)
{
    return Args(op.p, EvMacroGetWrk(op.com)->fmt, args);
}

std::vector<int> ParseIdList(u_char *s)
{
    std::vector<int> ids;

    for (int i = 0; s != nullptr && s + 4 <= PakEnd() && i < EV_SUB_ID_MAX; i++, s += 4)
    {
        u_int id;
        std::memcpy(&id, s, 4);
        if (id == 0xffffffffu)
        {
            break;
        }
        if (id < (u_int)kEventCount)
        {
            ids.push_back((int)id);
        }
    }
    return ids;
}

bool IsAreaIn(int label)
{
    return label == kCondPlyrAreaIn || label == kCondPlyrAreaInDef;
}

// EV_STATE references that ask for an END or a forced end: the ones a
// fast-forward can satisfy.
struct Ref
{
    int    id;
    u_char state;
};

std::vector<Ref> DoneRefs(const Group &group)
{
    std::vector<Ref> refs;

    for (const Cond &c : group)
    {
        const int state = CondArg(c, 0);
        const int id    = CondArg(c, 1);
        if (c.label == kCondEvState && id >= 0 && id < kEventCount
            && (state == EV_STATE_END || state == EV_STATE_LOCK))
        {
            refs.push_back({id, (u_char)state});
        }
    }
    return refs;
}

// ---------------------------------------------------------------------------
//  Describing
// ---------------------------------------------------------------------------

std::string Format(const char *fmt, ...)
{
    char    buf[256];
    va_list args;

    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::string AreaText(int area)
{
    return Format(WorldOf(area) == 1 ? UiText("area %d (house)") : UiText("area %d"), area);
}

std::string PlaceText(const Place &place)
{
    if (place.label >= 0)
    {
        return place.area >= 0 ? Format(UiText("rect %d, "), place.label) + AreaText(place.area)
                               : Format(UiText("rect %d"), place.label);
    }
    return place.area >= 0 ? AreaText(place.area) : "";
}

void Append(std::string &out, const std::string &part, const char *sep = ", ")
{
    if (part.empty())
    {
        return;
    }
    if (!out.empty())
    {
        out += sep;
    }
    out += part;
}

const char *ChapterName(int chapter)
{
    return chapter >= 0 && chapter < kChapterCount ? kChapterNames[chapter] : "chapter ?";
}

// The place one alternative of open conditions names: the player's own
// rectangle (its area from PLYR_ROOMIN when the group has one), else a bare
// area; the facing from PLYR_ROT, whose arc is about her facing (EvPlyrRot
// adds 180 degrees and EvRotInRange takes them off again), or towards
// PLYR_INTO_SIGHT's point.
Place PlaceOfGroup(const Group &group)
{
    Place place;
    int   room = -1;

    for (const Cond &c : group)
    {
        int a[8] = {};
        CondArgs(c, a);

        if (IsAreaIn(c.label) && place.label < 0)
        {
            place.label = a[2];
        }
        else if (c.label == kCondPlyrRoomIn && room < 0)
        {
            room = a[0];
        }
        else if (c.label == kCondPlyrRot)
        {
            place.rot = a[0];
        }
        else if (c.label == kCondPlyrIntoSight)
        {
            place.look   = true;
            place.look_x = a[0];
            place.look_z = a[2];
        }
    }
    place.area = room >= 0 ? room : AreaOfLabel(place.label);
    if (place.label < 0 && room < 0)
    {
        place = Place{};
    }
    return place;
}

// One short line for a group: the conditions a player can act on, in words.
// The ones that only gate are left to the tooltip.
std::string Summarise(const Group &group)
{
    if (group.empty())
    {
        return "at once";
    }

    std::string action;
    std::string where = PlaceText(PlaceOfGroup(group));
    std::string rest;
    std::string after;
    int         after_count = 0;

    for (const Cond &c : group)
    {
        int a[8] = {};
        CondArgs(c, a);

        switch (c.label)
        {
        case kCondPushPad:
            action = "examine";
            break;
        case kCondPhotoObj:
            action = "photograph";
            break;
        case kCondPlyrAreaOut:
        case kCondPlyrAreaOutDef:
            Append(rest, Format("leave rect %d", a[2]));
            break;
        case kCondPlyrRoomOut:
            Append(rest, Format("leave area %d", a[0]));
            break;
        case kCondCheckGameWorld:
            Append(rest, a[0] ? "real world" : "dream");
            break;
        case kCondCheckDayFlag:
            Append(rest, a[0] ? "night side" : "day side");
            break;
        case kCondNpcAreaOut:
        case kCondNpcAreaOutDef:
        case kCondNpcDistance:
            Append(rest, "NPC");
            break;
        case kCondPlyrIntoSight:
        case kCondNpcIntoPlyrSight:
        case kCondObjIntoPlyrSight:
            Append(rest, "in sight");
            break;
        case kCondGhostExist:
            Append(rest, "ghost present");
            break;
        case kCondGhostLost:
            Append(rest, "ghost gone");
            break;
        case kCondGhostReleaseType:
            Append(rest, "ghost released");
            break;
        case kCondPlyrItemUse:
            Append(rest, "use item");
            break;
        case kCondPlyrItemHave:
            Append(rest, "carrying item");
            break;
        case kCondNotHaveItem:
            Append(rest, "without item");
            break;
        case kCondEvRandom:
            Append(rest, "random");
            break;
        case kCondFinderMode:
        case kCondObjIntoFinder:
            Append(rest, "camera raised");
            break;
        case kCondBattleEnd:
            Append(rest, "after a fight");
            break;
        case kCondBattleMode:
            Append(rest, "in a fight");
            break;
        case kCondPuzzleSuccess:
            Append(rest, "puzzle solved");
            break;
        case kCondChapterLoadWait:
            Append(rest, "chapter loaded");
            break;
        case kCondDoorStatCheck:
            Append(rest, "door");
            break;
        case kCondSelectChoice:
            Append(rest, "choice");
            break;
        case kCondPeepingEnd:
            Append(rest, "after peeping");
            break;
        case kCondTelStatus:
        case kCondTelTalkEnd:
            Append(rest, "phone");
            break;
        case kCondTapeCheck:
            Append(rest, "tape");
            break;
        case kCondCineMenuEnd:
        case kCondCineFilmEnd:
            Append(rest, "projector");
            break;
        case kCondEvFailure:
            Append(rest, "never");
            break;
        case kCondNowChapter:
        {
            const std::string name = ChapterName(a[0]);     // "Hour II: ..." -> "Hour II"
            Append(rest, (a[1] == 8 ? "from " : a[1] == 9 ? "up to " : "in ") + name.substr(0, name.find(':')));
            break;
        }
        case kCondEvState:
            if (a[0] == EV_STATE_END || a[0] == EV_STATE_LOCK)
            {
                if (after_count++ < 2)
                {
                    Append(after, Format("ev%d", a[1]));
                }
            }
            break;
        default:
            break;
        }
    }

    std::string text = action;
    Append(text, where);
    Append(text, rest);
    if (after_count != 0)
    {
        Append(text, "after " + after + (after_count > 2 ? ", ..." : ""));
    }
    return text.empty() ? "gated" : text;
}

struct Tag
{
    const char *command;
    const char *tag;
};

// What a program's commands are worth calling out, most telling first.
const Tag kTags[] = {
    {"MOVIE_PLAY", "movie"},
    {"WAKEUP_MOVIE_PLAY", "wake-up movie"},
    {"SLEEP_MOVIE_PLAY", "sleep movie"},
    {"EV_CAM", "cutscene"},
    {"MSG_DISP", "message"},
    {"MSG_DISP_SEL", "message"},
    {"MSG_CHOICE", "choice"},
    {"GHOST_APPEAR", "ghost"},
    {"GHOST_REGIST", "ghost"},
    {"PUZZLE_START", "puzzle"},
    {"ITEM_GET", "item"},
    {"FILE_GET", "file"},
    {"LEVELGEM_GET", "gem"},
    {"CAM_SPECIALSHOT_GET", "camera upgrade"},
    {"CAM_EQUIP_FUNCTION_GET", "camera upgrade"},
    {"MEMO_UPDATE", "memo"},
    {"CHAPTER_DISP_START", "chapter title"},
    {"SUBTITLE_DISP_REQ", "voice"},
    {"SUBTITLE3D_OBJ_DISP_REQ", "voice"},
    {"SUBTITLE3D_POS_DISP_REQ", "voice"},
    {"STREAM_PLAY", "stream"},
    {"STREAM3D_OBJ_PLAY", "stream"},
    {"STREAM3D_POS_PLAY", "stream"},
    {"TEL_START", "phone"},
    {"TEL_TALK_START", "phone"},
    {"TAPE_START", "tape"},
    {"CINE_FILM_PLAY", "projector"},
    {"PEEPING_REQUEST", "peeping"},
    {"RESEARCH_START", "research"},
    {"DEVELOP_REQUEST", "develop"},
    {"SET_BUTTERFLY", "butterfly"},
    {"SYNCHRO_MODE_START", "synchro"},
    {"NPC_ACTIVE_FLG", "NPC"},
    {"GO_DREAM_WORLD", "to the dream"},
    {"GO_REAL_WORLD", "wakes up"},
    {"AREA_CHANGE", "room change"},
    {"CHANGE_CHAPTER", "chapter change"},
    {"PRIVATE_DOOR_LOCK", "door lock"},
    {"PRIVATE_DOOR_UNLOCK", "door lock"},
    {"SET_GHOST_SEAL_DOOR", "seals door"},
    {"GAME_DATA_SAVE", "save"},
    {"GAMEOVER_REQUEST", "game over"},
    {"GAME_CLEAR_", "game clear"},
    {"EV_MISSION_START", "mission"},
};

std::string Tags(const std::vector<Op> &ops)
{
    std::set<std::string> names;
    std::string           text;

    for (const Op &op : ops)
    {
        names.insert(EvMacroGetWrk(op.com)->name);
    }
    for (const Tag &tag : kTags)
    {
        if (names.count(tag.command) != 0 && text.find(tag.tag) == std::string::npos)
        {
            Append(text, tag.tag);
        }
    }
    return text;
}

std::string Lower(std::string text)
{
    for (char &c : text)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return text;
}

// ---------------------------------------------------------------------------
//  The catalogue
// ---------------------------------------------------------------------------

Catalogue g_catalogue;
bool      g_catalogue_valid = false;

// Where the scripts put the player in an area: the first AREA_CHANGE[area,
// mh] + PLYR_FLOOR_CHANGE + PLYR_POS_SET (+ PLYR_ROT_SET) of any program, a
// spot the game itself stands her on. 26 areas have one.
std::map<int, ReiPanWarpSpot> g_spawn;
// The first event rectangle of each area, for the others.
std::map<int, int> g_area_label;

void HarvestSpawns(const std::vector<Op> &ops)
{
    ReiPanWarpSpot spot  = {-1, -1, -1, 0, 0, REIPAN_WARP_KEEP_ROT};
    ReiPanWarpSpot *last = nullptr;

    for (const Op &op : ops)
    {
        int a[8] = {};
        OpArgs(op, a);

        if (op.com == kComAreaChange)
        {
            spot = {a[0], -1, a[1], 0, 0, REIPAN_WARP_KEEP_ROT};
            last = nullptr;
        }
        else if (op.com == kComPlyrFloorChange && spot.area >= 0)
        {
            spot.mh = a[0];
        }
        else if (op.com == kComPlyrPosSet && spot.area >= 0)
        {
            spot.x = a[0];
            spot.z = a[1];
            last   = &g_spawn.emplace(spot.area, spot).first->second;
            if (last->x != spot.x || last->z != spot.z)
            {
                last = nullptr;             // the area had one already
            }
            spot.area = -1;
        }
        else if (op.com == kComPlyrRotSet && last != nullptr)
        {
            last->rot = a[0];
            last      = nullptr;
        }
    }
}

Place InheritedPlace(int id);

void Build()
{
    Catalogue &cat = g_catalogue;

    cat.events.assign(kEventCount, Event{});
    std::fill(std::begin(cat.chapter_event), std::end(cat.chapter_event), -1);
    cat.chapter_root = kChapterRoot;
    cat.room_root    = kRoomRoot;
    cat.other_roots.clear();
    g_spawn.clear();
    g_area_label.clear();

    std::vector<std::vector<Group>> open(kEventCount);

    for (int id = 0; id < kEventCount; id++)
    {
        Event &ev = cat.events[id];

        ev.subs  = ParseIdList(Stream(EV_TBL_SUB_ID, id));
        open[id] = ParseConds(Stream(EV_TBL_OPEN_COND, id));

        // Its place: the first alternative that names one.
        for (const Group &group : open[id])
        {
            const Place place = PlaceOfGroup(group);
            if (place.label >= 0 || place.area >= 0)
            {
                ev.place = place;
                break;
            }
        }
        for (const Cond &c : open[id][0])
        {
            if (c.label == kCondCheckGameWorld)
            {
                ev.world = CondArg(c, 0) != 0;
            }
            else if (c.label == kCondCheckDayFlag)
            {
                ev.side = CondArg(c, 0) != 0;
            }
        }

        std::vector<Op> ops = ParseProgram(Stream(EV_TBL_INIT_PRG, id));
        std::vector<Op> exe = ParseProgram(Stream(EV_TBL_EXE_PRG, id));
        std::vector<Op> end = ParseProgram(Stream(EV_TBL_END_PRG, id));
        ev.has_program = !ops.empty() || !exe.empty();
        ops.insert(ops.end(), exe.begin(), exe.end());
        ops.insert(ops.end(), end.begin(), end.end());
        HarvestSpawns(ops);

        ev.trigger = Summarise(open[id][0]);
        if (open[id].size() > 1)
        {
            ev.trigger += Format(" (+%d more)", (int)open[id].size() - 1);
        }
        ev.content = Tags(ops);
    }

    for (int id = 0; id < kEventCount; id++)
    {
        for (int s : cat.events[id].subs)
        {
            cat.events[s].parent = id;
        }
    }
    for (int s : cat.events[0].subs)
    {
        if (s != kChapterRoot && s != kRoomRoot)
        {
            cat.other_roots.push_back(s);
        }
    }
    for (int s : cat.events[kChapterRoot].subs)
    {
        for (const Cond &c : open[s][0])
        {
            const int chapter = CondArg(c, 0);
            if (c.label == kCondNowChapter && CondArg(c, 1) == 10 && chapter >= 0 && chapter < kChapterCount)
            {
                cat.chapter_event[chapter] = s;
            }
        }
    }

    // Chapter and room hint flow down the tree. The visited set is only a
    // guard: the disc's tree has no cycles.
    std::vector<char> visited(kEventCount, 0);
    struct Item
    {
        int id;
        int chapter;
        int room;
    };
    std::vector<Item> stack = {{0, -1, -1}};
    while (!stack.empty())
    {
        Item item = stack.back();
        stack.pop_back();
        if (visited[item.id]++)
        {
            continue;
        }

        Event &ev = cat.events[item.id];
        for (int c = 0; c < kChapterCount; c++)
        {
            if (cat.chapter_event[c] == item.id)
            {
                item.chapter = c;
            }
        }
        const std::vector<Group> &groups = open[item.id];
        if (groups.size() == 1 && groups[0].size() == 1 && groups[0][0].label == kCondPlyrRoomIn)
        {
            item.room = CondArg(groups[0][0], 0);
        }
        ev.chapter   = item.chapter;
        ev.room_hint = item.room;

        for (int s : ev.subs)
        {
            stack.push_back({s, item.chapter, item.room});
        }
    }

    for (int c = 0; c < kChapterCount; c++)
    {
        const int chapter_event = cat.chapter_event[c];
        if (chapter_event >= 0 && !cat.events[chapter_event].subs.empty())
        {
            cat.events[cat.events[chapter_event].subs[0]].entry = true;
        }
    }

    for (int id = 0; id < kEventCount; id++)
    {
        Event       &ev    = cat.events[id];
        const Group &group = open[id][0];

        bool intro = ev.subs.empty() && !group.empty();
        for (const Cond &c : group)
        {
            const int ref = CondArg(c, 1);
            intro = intro && c.label == kCondEvState && ref >= 0 && ref < kEventCount && cat.events[ref].entry;
        }
        ev.intro_leaf = intro;

        // A rectangle no table places is in the area its ancestors open in.
        if (ev.place.label >= 0 && ev.place.area < 0)
        {
            ev.place.area = ev.room_hint;
        }
        if (ev.place.label >= 0 && ev.place.area >= 0)
        {
            g_area_label.emplace(ev.place.area, ev.place.label);
        }

        ev.search = Lower(Format("ev%d ", id) + ev.trigger + " " + ev.content);
    }

    for (int id = 0; id < kEventCount; id++)
    {
        Event &ev = cat.events[id];
        ev.go     = ev.place.Known() ? ev.place : InheritedPlace(id);
    }

    ReiPan_LogPrintf("[events] catalogue: %d events, %d chapters, %d areas with a scripted spot\n", kEventCount,
                     (int)std::count_if(std::begin(cat.chapter_event), std::end(cat.chapter_event),
                                        [](int e) { return e >= 0; }),
                     (int)g_spawn.size());
}

// ---------------------------------------------------------------------------
//  Where the game is
// ---------------------------------------------------------------------------

bool Running(int state)
{
    return state == EV_STATE_INIT || state == EV_STATE_EXE || state == EV_STATE_WAIT_END;
}

bool Waiting(int state)
{
    return state == EV_STATE_WAIT_OPEN || state == EV_STATE_SUSPEND;
}

// No event program running and no message held up. EXE is a main program
// running; an end program runs with its event already END, so that half has
// to ask the execution slots.
bool EventsQuiet(int *busy_id)
{
    for (int id = 0; id < kEventCount; id++)
    {
        const int state = GetEvState(id);
        if (state == EV_STATE_INIT || state == EV_STATE_EXE
            || (state == EV_STATE_END && CheckEventExeEntry(id) == 1))
        {
            *busy_id = id;
            return false;
        }
    }
    *busy_id = -1;
    return GetEvWrkWaitFlg() == 0;
}

bool Here(const Place &place)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (plyr == nullptr || GetPlyrAreaNo() != place.area)
    {
        return false;
    }
    return place.label < 0 || MrecIsInEvent(plyr->pos, place.label, plyr->floor) != 0;
}

// ---------------------------------------------------------------------------
//  Forcing states
// ---------------------------------------------------------------------------

// CompulsionSetEventState() on an event that is not running. An END of a
// part also parks its sub-events, as a real end would
// (SetEventEndExeStatus); a forced end seals the whole sub-tree by itself.
bool Force(int id, u_char state)
{
    const int now = GetEvState(id);

    if (Running(now))
    {
        return false;
    }
    if (now == EV_STATE_END || now == EV_STATE_LOCK)
    {
        return true;
    }
    CompulsionSetEventState(id, state);
    if (state == EV_STATE_END && !g_catalogue.events[id].subs.empty())
    {
        EventEnd_SubStateChange(id);
    }
    return true;
}

// Events a fast-forward leaves alone: they bring the chapter up.
bool RunsByItself(int id)
{
    const Event &ev = g_catalogue.events[id];

    return ev.entry || ev.intro_leaf;
}

// ---------------------------------------------------------------------------
//  The job
// ---------------------------------------------------------------------------

enum class Kind
{
    Chapter,
    Jump,
    Teleport,
};

enum class Stage
{
    Idle,
    Restart,            // the warp's new game in the chapter, from its start
    ChapterStarting,    // until the chapter's start event has ended
    Loading,            // the warp's new game in the chapter, at the target
    Settling,           // until the chapter's event runs, then Replay()
    Advance,            // walking the path down to the target (a teleport: to its place)
    Moving,             // a warp teleport or move under way
};

struct Job
{
    Kind             kind  = Kind::Chapter;
    Stage            stage = Stage::Idle;
    int              chapter = -1;
    int              target  = -1;
    bool             asked   = false;   // Restart: the warp has been asked for
    std::vector<Ref> held;              // the target's own prerequisites
    bool             released = false;
    bool             placed   = false;  // the target's place has been dealt with
    int              teleported_for = -1;   // path event the last teleport was for
    Place            dest;
    bool             proceed = false;
    bool             waiting_on_events = false;
    int              quiet_frames = 0;  // locked or black with no event program running
    int              at_frames = 0;     // frames waiting at the target
    int              frames = 0;
};

Job                                g_job;
std::string                        g_status = UiText("Pick an Hour or an event.");
std::vector<std::function<void()>> g_commands;  // window buttons, run at the next frame's top

void SetStage(Stage stage)
{
    g_job.stage             = stage;
    g_job.frames            = 0;
    g_job.waiting_on_events = false;
}

void Finish(const std::string &status)
{
    g_status = status;
    g_job    = Job{};
}

const Event &Ev(int id)
{
    return g_catalogue.events[id];
}

// The chapter's start event, -1 (also before the catalogue is read).
int EntryOf(int chapter)
{
    const int chapter_event = g_catalogue_valid && chapter >= 0 && chapter < kChapterCount
                                ? g_catalogue.chapter_event[chapter]
                                : -1;

    return chapter_event >= 0 && !Ev(chapter_event).subs.empty() ? Ev(chapter_event).subs[0] : -1;
}

// The path from just below the chapter event down to `id`.
std::vector<int> PathBelowChapter(int id)
{
    std::vector<int> path;
    const int        chapter_event = g_catalogue.chapter_event[Ev(id).chapter];

    for (int e = id; e >= 0 && e != chapter_event; e = Ev(e).parent)
    {
        path.push_back(e);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> AncestorsAndSelf(int id)
{
    std::vector<int> ids;

    for (int e = id; e >= 0; e = Ev(e).parent)
    {
        ids.push_back(e);
    }
    return ids;
}

std::vector<Ref> OpenRefs(int id)
{
    return DoneRefs(ParseConds(Stream(EV_TBL_OPEN_COND, id))[0]);
}

// Where to put the player for an event with no place of its own: where the
// nearest event it waits on happens -- the story beat just before it --
// else the nearest ancestor's place, else its area.
Place InheritedPlace(int id)
{
    std::vector<int> queue;
    std::set<int>    seen = {id};

    for (const Ref &ref : OpenRefs(id))
    {
        queue.push_back(ref.id);
    }
    for (size_t i = 0; i < queue.size() && i < 64; i++)
    {
        const int e = queue[i];
        if (!seen.insert(e).second || Ev(e).entry)
        {
            continue;
        }
        if (Ev(e).place.Known())
        {
            return Ev(e).place;
        }
        for (const Ref &ref : OpenRefs(e))
        {
            queue.push_back(ref.id);
        }
    }
    for (int e = Ev(id).parent; e >= 0; e = Ev(e).parent)
    {
        if (Ev(e).place.Known())
        {
            return Ev(e).place;
        }
    }
    Place place;
    place.area = Ev(id).room_hint;
    return place;
}

// How the warp stands her on a place: on its rectangle; for a bare area on
// the scripts' own spot there, else on the area's first event rectangle,
// else in the middle of the room.
ReiPanWarpSpot SpotOf(const Place &place)
{
    ReiPanWarpSpot spot = {place.area, place.label, -1, REIPAN_WARP_ROOM_CENTRE, 0,
                           place.rot == kKeepRot ? REIPAN_WARP_KEEP_ROT : place.rot};

    if (place.label < 0)
    {
        auto spawn = g_spawn.find(place.area);
        auto label = g_area_label.find(place.area);
        if (spawn != g_spawn.end())
        {
            spot = spawn->second;
        }
        else if (label != g_area_label.end())
        {
            spot.label = label->second;
        }
    }
    if (place.look && place.rot == kKeepRot)
    {
        spot.look   = 1;
        spot.look_x = place.look_x;
        spot.look_z = place.look_z;
    }
    return spot;
}

Place WorldPlace(int world)
{
    Place place;

    place.area = kWorldArea[world != 0];
    return place;
}

// Step 2 of the banner.
void FastForward()
{
    const int     target = g_job.target;
    std::set<int> keep;

    for (int e : AncestorsAndSelf(target))
    {
        keep.insert(e);
    }

    std::map<int, u_char> want;
    std::vector<int>      work = PathBelowChapter(target);
    std::set<int>         seen;
    while (!work.empty())
    {
        const int e = work.back();
        work.pop_back();
        if (!seen.insert(e).second)
        {
            continue;
        }
        for (const Ref &ref : OpenRefs(e))
        {
            if (keep.count(ref.id) != 0 || RunsByItself(ref.id))
            {
                continue;
            }
            // A part of another tree is somebody else's business.
            if (Ev(ref.id).chapter != g_job.chapter && !Ev(ref.id).subs.empty())
            {
                continue;
            }
            want.emplace(ref.id, ref.state);
            work.push_back(ref.id);
        }
    }

    g_job.held.clear();
    for (const Ref &ref : OpenRefs(target))
    {
        if (want.count(ref.id) != 0)
        {
            g_job.held.push_back(ref);
        }
    }

    int forced = 0;
    for (const auto &[id, state] : want)
    {
        const bool held = std::any_of(g_job.held.begin(), g_job.held.end(), [&](const Ref &r) { return r.id == id; });
        if (!held && Force(id, state))
        {
            forced++;
        }
    }
    ReiPan_LogPrintf("[events] ev%d: %d prerequisite(s) settled, %d held until the player is placed\n", target,
                     forced, (int)g_job.held.size());
}

void Release()
{
    for (const Ref &ref : g_job.held)
    {
        if (Waiting(GetEvState(ref.id)))
        {
            Force(ref.id, ref.state);
        }
    }
    g_job.released = true;
}

// ReiPan_WarpJump's settle, in Story_NowLoading before any event opens: the
// chapter's start event marked ended -- the load has made its move, Replay()
// makes the rest -- and step 2's prerequisites forced. Nothing is armed yet,
// and arming leaves ended events alone (SetEventInitStatus).
void SettleJump()
{
    const int entry = EntryOf(g_job.chapter);

    if (entry >= 0)
    {
        CompulsionSetEventState(entry, EV_STATE_END);
    }
    FastForward();
}

// What a skipped start event leaves behind besides the move: its commands
// that only set state -- items and the camera, locks, doors, objects, flags,
// event states -- run through their handlers in its programs' order. What
// moves her, changes the world, plays, waits or fades was the load's.
const char *const kReplayed[] = {
    "ITEM_RESET", "ITEM_GET", "ITEM_LOST", "ITEM_TABLE_CHANGE_", "PHOTO_LOCK", "PHOTO_UNLOCK",
    "MENU_LOCK", "MENU_UNLOCK", "PAUSE_LOCK", "PAUSE_UNLOCK", "SET_MEMO_OPEN_FALG", "NPC_ACTIVE_FLG",
    "SET_OBJ_VISIBLE", "SET_OBJ_ALPHA", "SET_OBJ_REQ_ACTION", "SET_OBJ_ACTIONTYPE", "SET_OBJ_HITCHECK",
    "SET_EVENT_REC_HIT", "PRIVATE_DOOR_LOCK", "PRIVATE_DOOR_UNLOCK", "CHANGE_CHAPTER", "EV_SET_STATE",
    "SET_DREAM_PLAYER", "CHANGE_TATTOO_LEVEL", "SET_EFF_VISIBLE", "GHOST_REGIST",
};

// What the scripts' own world changes set besides the move: falling asleep
// (event 3) and waking up (event 2). PLYR_FLASHLIGHT_SET's model change is
// the warp's (ApplyWarp picks the world's player model).
struct WorldCommand
{
    const char *name;
    int         args[2];
};
const WorldCommand kWorldState[2][3] = {
    {{"SET_REAL_CAMERA", {0, 0}}, {"EV_SET_SCREEN_EFFECT", {0, 0}}, {"SET_RADIO_LOCK_FLG", {0, 0}}},
    {{"SET_RADIO_LOCK_FLG", {1, 0}}, {"SET_REAL_CAMERA", {1, 0}}, {"EV_SET_SCREEN_EFFECT", {0, 4}}},
};

int ComOf(const char *name)
{
    for (int com = 0; EvMacroGetWrk(com) != nullptr; com++)
    {
        if (std::strcmp(EvMacroGetWrk(com)->name, name) == 0)
        {
            return com;
        }
    }
    return -1;
}

// A command's handler run as EventExeFuncCall would, on a control of its
// own for the event; the replayed ones finish in one call.
void RunCommand(int event_id, int com, u_char *addr, int *args)
{
    static u_char     none[16];
    const EV_EXE_WRK *row  = EvMacroGetWrk(com);
    EV_EXE_CTRL       ctrl = {};

    ctrl.event_id   = event_id;
    ctrl.event_addr = addr != nullptr ? addr : none;
    ctrl.args       = args;
    for (int i = 0; i < 4 && row->func(&ctrl) != 1; i++)
    {
    }
}

void Replay(int entry, int world)
{
    int count = 0;

    for (int table : {EV_TBL_INIT_PRG, EV_TBL_EXE_PRG, EV_TBL_END_PRG})
    {
        for (const Op &op : ParseProgram(Stream(table, entry)))
        {
            const char *name = EvMacroGetWrk(op.com)->name;
            if (std::any_of(std::begin(kReplayed), std::end(kReplayed),
                            [&](const char *r) { return std::strcmp(r, name) == 0; }))
            {
                int a[16] = {};
                OpArgs(op, a);
                RunCommand(entry, op.com, op.p, a);
                count++;
            }
        }
    }
    for (const WorldCommand &c : kWorldState[world != 0])
    {
        const int com = ComOf(c.name);
        if (com >= 0)
        {
            int a[16] = {c.args[0], c.args[1]};
            RunCommand(entry, com, nullptr, a);
        }
    }
    // Every start event ends on FADE_IN[0, 0, 0, 30]: ev0 covers the screen
    // (FADE_OUT) when a game starts.
    FadeInReq(0, 0, 0, 30);
    ReiPan_LogPrintf("[events] ev%d skipped: %d of its commands replayed, then the %s's state\n", entry, count,
                     world ? "real world" : "dream");
}

// The map side a jump starts on: CHECK_DAY_FLAG of the target or its
// nearest ancestor that asks, else day.
int SideOf(int id)
{
    for (int e : AncestorsAndSelf(id))
    {
        if (Ev(e).side >= 0)
        {
            return Ev(e).side;
        }
    }
    return 0;
}

// Through the warp: a teleport within the world, a reload into the other.
void Teleport(const Place &place)
{
    const ReiPanWarpSpot spot = SpotOf(place);

    g_job.dest = place;
    ReiPan_WarpGoTo(&spot);
    SetStage(Stage::Moving);
    g_status = Format(UiText("Going to %s..."), PlaceText(place).c_str());
}

// Blocked only by an event program? Say which and let Proceed() through.
bool HeldByEvents()
{
    int busy = -1;

    if (EventsQuiet(&busy) || g_job.proceed)
    {
        g_job.waiting_on_events = false;
        return false;
    }
    g_job.waiting_on_events = true;
    g_status = busy >= 0 ? Format(UiText("Waiting for ev%d to finish before teleporting."), busy)
                         : std::string("Waiting for a message to close before teleporting.");
    return true;
}

// A skipped event can be the one that hands back a lock an earlier one took
// or fades the screen in. Stranded means: the player locked (PLYR_LOCK's
// flag 1), the pad locked or the screen left black, with no event program
// running for a second. The ROM's own sequences go quiet for a frame or two
// where one event chains into the next, never for 25. True while it is
// dealing with one, so the caller waits.
bool ReleaseStrandedLocks()
{
    int        busy  = -1;
    PLYR_WRK  *plyr  = ingame_wrk.plyr_wrk;
    const bool held  = plyr != nullptr && ((plyr->flags & 1) != 0 || IsPadLocked(0) != 0);
    const bool black = fade_ctrl.fade_state == 0 && fade_ctrl.alpha != 0;

    if (!(held || black) || !ReiPan_WarpInPlay() || !EventsQuiet(&busy))
    {
        g_job.quiet_frames = 0;
        return false;
    }
    if (++g_job.quiet_frames < 25)
    {
        return true;
    }

    g_job.quiet_frames = 0;
    if (held)
    {
        int         args[2] = {0, 1};
        EV_EXE_CTRL ctrl    = {};

        ctrl.args = args;
        EvPlyrLock(&ctrl);              // PLYR_LOCK[0, 1]
        args[0] = 1;
        EvPlyrPad(&ctrl);               // PLYR_PAD[1]
    }
    if (black)
    {
        FadeInReq(0, 0, 0, 30);         // FADE_IN[0, 0, 0, 30], as the chapter starts write it
    }
    ReiPan_LogPrintf("[events] released what a skipped event would have (locked %d, black %d)\n", held, black);
    return true;
}

void LogJudged(int id);

void StepTeleport()
{
    if (!ReiPan_WarpInPlay())
    {
        g_status = UiText("Waiting for normal play...");
        return;
    }
    if (HeldByEvents())
    {
        return;
    }
    g_job.proceed = false;
    if (Here(g_job.dest))
    {
        Finish("Already at " + PlaceText(g_job.dest) + ".");
        return;
    }
    Teleport(g_job.dest);
}

void StepAdvance()
{
    if (g_job.kind == Kind::Teleport)
    {
        StepTeleport();
        return;
    }

    const int target = g_job.target;
    const int state  = GetEvState(target);
    if (!Waiting(state))
    {
        Finish(Format("ev%d has started.", target));
        return;
    }
    if (ReleaseStrandedLocks())
    {
        return;
    }
    if (!ReiPan_WarpInPlay())
    {
        g_status = UiText("Waiting for normal play...");
        return;
    }

    const std::vector<int> path = PathBelowChapter(target);
    int                    node = -1;
    for (int e : path)
    {
        if (Waiting(GetEvState(e)))
        {
            node = e;
            break;
        }
    }
    if (node < 0)
    {
        return;
    }

    const int parent = Ev(node).parent;
    if (parent >= 0 && !Running(GetEvState(parent)))
    {
        Finish(Format("ev%d cannot open: ev%d, its parent, is %s.", node, parent, StateName(GetEvState(parent))));
        return;
    }
    if (GetEvState(node) == EV_STATE_SUSPEND)
    {
        Finish(Format("ev%d was parked when its part ended.", node));
        return;
    }

    const int world = CurrentWorld();
    if (node != target)
    {
        // A part on the way down has not opened. If it opens on a place, go
        // there -- to the deepest place below it in the same area, so one
        // load serves both; if on a world, into it.
        Place aim = Ev(node).place;
        if (!aim.Known() && Ev(node).world >= 0 && Ev(node).world != world)
        {
            aim = WorldOf(Ev(target).go.area) == Ev(node).world ? Ev(target).go : WorldPlace(Ev(node).world);
        }
        if (aim.Known() && !Here(aim) && g_job.teleported_for != node)
        {
            if (HeldByEvents())
            {
                return;
            }
            g_job.proceed = false;
            for (auto it = std::find(path.begin(), path.end(), node); it != path.end(); ++it)
            {
                const Place &below = Ev(*it).place;
                if (below.label >= 0 && below.area == aim.area)
                {
                    aim = below;
                }
            }
            g_job.teleported_for = node;
            Teleport(aim);
            return;
        }
        g_status = Format(UiText("Waiting for ev%d to open: %s."), node, Ev(node).trigger.c_str());
        return;
    }

    if (!g_job.placed)
    {
        Place aim = Ev(target).go;
        if (!aim.Known() && Ev(target).world >= 0 && Ev(target).world != world)
        {
            aim = WorldPlace(Ev(target).world);
        }
        if (aim.Known() && !Here(aim))
        {
            if (HeldByEvents())
            {
                return;
            }
            g_job.proceed = false;
            g_job.placed  = true;
            Teleport(aim);
            return;
        }
        g_job.placed = true;
    }
    if (!g_job.released)
    {
        Release();
    }
    g_status = Format(UiText("At ev%d -- waiting for it: %s."), target, Ev(target).trigger.c_str());
    if (++g_job.at_frames == 1 || g_job.at_frames == 50)
    {
        LogJudged(target);
    }
}

void Begin(Kind kind, int chapter, int target, Stage stage)
{
    g_job         = Job{};
    g_job.kind    = kind;
    g_job.chapter = chapter;
    g_job.target  = target;
    SetStage(stage);
    g_status = Format(UiText("Starting %s..."), ChapterName(chapter));
}

// REIPAN_EVENT="chapter:2,jump:500,go:1061": asked for at boot, one after
// the other, each once the one before is done and the game can take it (a
// jump or a teleport needs the event file, which a chapter start loads).
std::vector<std::pair<std::string, int>> g_env;
bool                                     g_env_read = false;

void ReadEnvironment()
{
    const char *spec = std::getenv("REIPAN_EVENT");

    g_env_read = true;
    for (const char *p = spec; p != nullptr && *p != '\0';)
    {
        const char *end  = std::strchr(p, ',');
        std::string item = end != nullptr ? std::string(p, end) : std::string(p);
        size_t      colon = item.find(':');
        if (colon == std::string::npos)
        {
            g_env.push_back({"jump", std::atoi(item.c_str())});
        }
        else
        {
            g_env.push_back({item.substr(0, colon), std::atoi(item.c_str() + colon + 1)});
        }
        p = end != nullptr ? end + 1 : p + item.size();
    }
}

void StepEnvironment()
{
    if (!g_env_read)
    {
        ReadEnvironment();
    }
    if (g_env.empty() || Busy() || !ReiPan_WarpIdle())
    {
        return;
    }
    const auto &[what, n] = g_env.front();
    if (what == "chapter" && CanStartChapter(nullptr))
    {
        StartChapter(n);
    }
    else if (what == "jump" && GetCatalogue() != nullptr && CanStartChapter(nullptr))
    {
        JumpToEvent(n);
    }
    else if (what == "go" && GetCatalogue() != nullptr && CanTeleport(nullptr) && ReiPan_WarpInPlay())
    {
        TeleportToEvent(n);
    }
    else
    {
        return;
    }
    ReiPan_LogPrintf("[events] REIPAN_EVENT: %s %d\n", what.c_str(), n);
    g_env.erase(g_env.begin());
}

void Step()
{
    std::vector<std::function<void()>> commands;
    commands.swap(g_commands);
    for (auto &fn : commands)
    {
        fn();
    }
    StepEnvironment();

    // Every status line goes to the log too, once: a jump spans loads, and
    // the log is where to read back what it did.
    static std::string logged;
    if (g_status != logged)
    {
        logged = g_status;
        ReiPan_LogPrintf("[events] %s\n", logged.c_str());
    }

    if (g_job.stage == Stage::Idle)
    {
        return;
    }
    g_job.frames++;

    switch (g_job.stage)
    {
    case Stage::Idle:
        break;

    case Stage::Restart:
        if (!g_job.asked)
        {
            ReiPan_WarpChapter(g_job.chapter);
            g_job.asked = true;
            break;
        }
        if (!ReiPan_WarpIdle())
        {
            g_status = Format(UiText("Starting %s: "), ChapterName(g_job.chapter)) + ReiPan_WarpStatus();
            break;
        }
        if (CurrentChapter() != g_job.chapter)
        {
            Finish(std::string("The new game did not start in ") + ChapterName(g_job.chapter) + ": "
                   + ReiPan_WarpStatus());
            break;
        }
        SetStage(Stage::ChapterStarting);
        break;

    case Stage::ChapterStarting:
    {
        if (!ReiPan_WarpInGame())
        {
            Finish("The game ended before the chapter came up.");
            break;
        }
        // The first game of a session: the event file is read once play has
        // begun.
        if (GetCatalogue() == nullptr)
        {
            g_status = Format(UiText("Starting %s: waiting for play..."), ChapterName(g_job.chapter));
            break;
        }

        const int entry = EntryOf(g_job.chapter);
        const int state = entry >= 0 ? GetEvState(entry) : EV_STATE_END;
        if ((state != EV_STATE_END && state != EV_STATE_LOCK) || (entry >= 0 && CheckEventExeEntry(entry) == 1))
        {
            g_status = Format(UiText("Waiting for ev%d, the start of %s, to end..."), entry, ChapterName(g_job.chapter));
            if (g_job.frames > 25 * 120)
            {
                Finish(Format("ev%d, the start of %s, never ended (%s).", entry, ChapterName(g_job.chapter),
                              StateName(state)));
            }
            break;
        }
        if (g_job.kind == Kind::Chapter)
        {
            Finish(std::string(ChapterName(g_job.chapter)) + " started.");
            break;
        }
        FastForward();
        SetStage(Stage::Advance);
        g_status = Format(UiText("Heading for ev%d..."), g_job.target);
        break;
    }

    case Stage::Loading:
    {
        const Place &go = Ev(g_job.target).go;

        if (!g_job.asked)
        {
            const ReiPanWarpSpot spot = SpotOf(go);
            ReiPan_WarpJump(g_job.chapter, &spot, SideOf(g_job.target), SettleJump);
            g_job.asked = true;
            break;
        }
        if (!ReiPan_WarpIdle())
        {
            g_status = Format(UiText("Loading %s for ev%d: "), PlaceText(go).c_str(), g_job.target) + ReiPan_WarpStatus();
            break;
        }
        if (CurrentChapter() != g_job.chapter)
        {
            Finish(std::string("The new game did not start in ") + ChapterName(g_job.chapter) + ": "
                   + ReiPan_WarpStatus());
            break;
        }
        SetStage(Stage::Settling);
        break;
    }

    case Stage::Settling:
    {
        // The start event would have run once ev89 (the new game's set-up,
        // which takes the camera away) has ended and the chapter's event
        // has opened.
        const int chapter_event = g_catalogue.chapter_event[g_job.chapter];
        bool      ready         = ReiPan_WarpInPlay() && Running(GetEvState(chapter_event));

        for (int sub : Ev(g_catalogue.chapter_root).subs)
        {
            const int state = GetEvState(sub);
            if (std::find(std::begin(g_catalogue.chapter_event), std::end(g_catalogue.chapter_event), sub)
                    == std::end(g_catalogue.chapter_event)
                && (Waiting(state) || Running(state) || CheckEventExeEntry(sub) == 1))
            {
                ready = false;
            }
        }
        if (!ReiPan_WarpInGame())
        {
            Finish("The game ended before the chapter came up.");
            break;
        }
        if (!ready)
        {
            g_status = Format(UiText("Waiting for ev%d, %s, to open..."), chapter_event, ChapterName(g_job.chapter));
            if (g_job.frames > 25 * 60)
            {
                Finish(Format(UiText("ev%d, %s, never opened."), chapter_event, ChapterName(g_job.chapter)));
            }
            break;
        }
        if (EntryOf(g_job.chapter) >= 0)
        {
            Replay(EntryOf(g_job.chapter), WorldOf(Ev(g_job.target).go.area));
        }
        SetStage(Stage::Advance);
        g_status = Format(UiText("Heading for ev%d..."), g_job.target);
        break;
    }

    case Stage::Advance:
        StepAdvance();
        break;

    case Stage::Moving:
        if (!ReiPan_WarpIdle() || !ReiPan_WarpInPlay())
        {
            g_status = Format(UiText("Going to %s: "), PlaceText(g_job.dest).c_str()) + ReiPan_WarpStatus();
            if (g_job.frames > 25 * 120)
            {
                Finish(std::string("The move did not finish: ") + ReiPan_WarpStatus());
            }
            break;
        }
        if (g_job.kind == Kind::Teleport)
        {
            Finish(Here(g_job.dest) ? "Moved to " + PlaceText(g_job.dest) + "."
                                    : "Moved near " + PlaceText(g_job.dest) + ": " + ReiPan_WarpStatus());
            break;
        }
        SetStage(Stage::Advance);
        break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
//  Public
// ---------------------------------------------------------------------------

const Catalogue *GetCatalogue()
{
    const u_char *top      = PakTop();
    const bool    resident = ReiPan_WarpInGame() && PakResident(top);

    // The tables are all the identity a file has; a reload of the same file
    // is the same catalogue. Read in play only: while Story_NowLoading runs,
    // the loader thread may still be writing it.
    if (resident && ReiPan_WarpInPlay()
        && (!g_catalogue_valid || std::memcmp(g_pak.data(), top, kPakTables) != 0))
    {
        g_pak.assign(top, top + kPakSize);
        Build();
        g_catalogue_valid = true;
    }
    g_catalogue.from_memory = resident;
    return g_catalogue_valid ? &g_catalogue : nullptr;
}

bool CanStartChapter(const char **why)
{
    const char *reason = nullptr;

    if (Busy())
    {
        reason = "A jump is already under way.";
    }
    else if (!ReiPan_WarpIdle())
    {
        reason = "The start point window is busy.";
    }
    else if (!ReiPan_WarpInGame() && !ReiPan_WarpAtTitle())
    {
        reason = "Only from the title screen or a game.";
    }
    if (why != nullptr)
    {
        *why = reason;
    }
    return reason == nullptr;
}

bool CanTeleport(const char **why)
{
    const char *reason = nullptr;

    if (Busy())
    {
        reason = "A jump is already under way.";
    }
    else if (!ReiPan_WarpIdle())
    {
        reason = "The start point window is busy.";
    }
    else if (!ReiPan_WarpInGame())
    {
        reason = "Only in a game.";
    }
    if (why != nullptr)
    {
        *why = reason;
    }
    return reason == nullptr;
}

void StartChapter(int chapter)
{
    if (chapter >= 0 && chapter < kChapterCount && CanStartChapter(nullptr))
    {
        Begin(Kind::Chapter, chapter, -1, Stage::Restart);
    }
}

void JumpToEvent(int id)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount || !CanStartChapter(nullptr))
    {
        return;
    }
    const Event &ev = Ev(id);
    if (ev.chapter < 0)
    {
        g_status = Format(UiText("ev%d is in no chapter's tree; Go takes the player to it."), id);
        return;
    }
    // The chapter event and its start event are the chapter's start. Any
    // other event with a place loads there; one without goes through the
    // chapter's own start and is walked to.
    if (id == g_catalogue.chapter_event[ev.chapter] || ev.entry)
    {
        Begin(Kind::Chapter, ev.chapter, -1, Stage::Restart);
    }
    else
    {
        Begin(Kind::Jump, ev.chapter, id, ev.go.Known() ? Stage::Loading : Stage::Restart);
    }
}

void TeleportToEvent(int id)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount || !CanTeleport(nullptr))
    {
        return;
    }
    const Place place = Ev(id).go;
    if (!place.Known())
    {
        g_status = Format(UiText("ev%d names no place to go to."), id);
        return;
    }
    g_job        = Job{};
    g_job.kind   = Kind::Teleport;
    g_job.target = id;
    g_job.dest   = place;
    SetStage(Stage::Advance);
    g_status = Format(UiText("Going to %s..."), PlaceText(place).c_str());
}

// The parked queue only takes an event that is waiting or done and off the
// execution slots (CompulsionSetEventStateMain), which is what keeps a
// program from being torn down under itself.
void ForceStart(int id)
{
    if (ReiPan_WarpInGame() && id >= 0 && id < kEventCount)
    {
        g_commands.push_back([id]() {
            Req_CompulsionSetEventState(id, id, EV_STATE_INIT);
            g_status = Format(UiText("ev%d will start in the next event pass."), id);
        });
    }
}

void ForceEnd(int id)
{
    if (ReiPan_WarpInGame() && GetCatalogue() != nullptr && id >= 0 && id < kEventCount)
    {
        g_commands.push_back([id]() {
            g_status = Force(id, EV_STATE_END) ? Format(UiText("ev%d marked as ended."), id)
                                               : Format("ev%d is running; it was left alone.", id);
        });
    }
}

void Rearm(int id)
{
    if (ReiPan_WarpInGame() && id >= 0 && id < kEventCount)
    {
        g_commands.push_back([id]() {
            Req_CompulsionSetEventState(id, id, EV_STATE_WAIT_OPEN);
            g_status = Format(UiText("ev%d will be waiting again from the next event pass."), id);
        });
    }
}

bool Busy()
{
    return g_job.stage != Stage::Idle;
}

const std::string &Status()
{
    return g_status;
}

bool CanProceed()
{
    return Busy() && g_job.waiting_on_events && !g_job.proceed;
}

void Proceed()
{
    g_job.proceed = true;
}

void Cancel()
{
    if (Busy())
    {
        Finish("Cancelled.");
    }
}

int StateOf(int id)
{
    return ReiPan_WarpInGame() && id >= 0 && id < kEventCount ? GetEvState(id) : -1;
}

int RunCount(int id)
{
    return ReiPan_WarpInGame() && id >= 0 && id < kEventCount ? GetEvExeCount(id) : -1;
}

const char *StateName(int state)
{
    // FF2's EvDbg_EventStatePrint labels, in words, and FF3's two parked states.
    switch (state)
    {
    case EV_STATE_WAIT_OPEN:
        return UiText("waiting");
    case EV_STATE_INIT:
        return UiText("starting");
    case EV_STATE_EXE:
        return UiText("running");
    case EV_STATE_WAIT_END:
        return UiText("closing");
    case EV_STATE_SUSPEND:
        return UiText("parked");
    case EV_STATE_END:
        return UiText("done");
    case EV_STATE_LOCK:
        return UiText("sealed");
    case 7:
        return UiText("until waking");
    case 8:
        return UiText("until the dream");
    default:
        return "";
    }
}

bool IsArmed(int id)
{
    // Per row per frame from the window, so the catalogue the window was
    // drawn from is trusted rather than checked against the file again.
    if (!g_catalogue_valid || StateOf(id) != EV_STATE_WAIT_OPEN)
    {
        return false;
    }
    const int parent = Ev(id).parent;
    return parent < 0 || Running(GetEvState(parent));
}

namespace
{

// One condition with its arguments in words where they name something.
std::string CondText(const Cond &c)
{
    const EV_COND_WRK *row = GetEvCondWrk(c.label);
    int                a[8] = {};
    const int          n    = CondArgs(c, a);
    std::string        text = row->name;

    switch (c.label)
    {
    case kCondEvState:
    case kCondEvNotState:
        return text + Format("(ev%d %s)", a[1], StateName(a[0]));
    case kCondPlyrAreaIn:
    case kCondPlyrAreaInDef:
    case kCondPlyrAreaOut:
    case kCondPlyrAreaOutDef:
    {
        Place place;
        place.label = a[2];
        place.area  = AreaOfLabel(a[2]);
        return text + "(" + PlaceText(place) + ")";
    }
    case kCondPlyrRoomIn:
    case kCondPlyrRoomOut:
        return text + "(" + AreaText(a[0]) + ")";
    case kCondPlyrRot:
        return text + Format("(facing %d +- %d)", a[0], a[1]);
    case kCondNowChapter:
    case kCondNotNowChapter:
        return text + Format("(%s %s)", a[1] == 8 ? ">=" : a[1] == 9 ? "<=" : "==", ChapterName(a[0]));
    case kCondCheckGameWorld:
        return text + (a[0] ? "(real world)" : "(dream)");
    case kCondCheckDayFlag:
        return text + (a[0] ? "(night side)" : "(day side)");
    default:
        break;
    }
    for (int i = 0; i < n; i++)
    {
        text += Format(i == 0 ? "[%d" : ", %d", a[i]);
    }
    return n != 0 ? text + "]" : text;
}

std::string OpText(const Op &op)
{
    const EV_EXE_WRK *row  = EvMacroGetWrk(op.com);
    int               a[16] = {};
    const int         n    = OpArgs(op, a);
    std::string       text = row->name;

    if (op.com == kComEvSetState)
    {
        return text + Format("(ev%d %s)", a[1], StateName(a[0]));
    }
    if (op.com == kComAreaChange)
    {
        return text + "(" + AreaText(a[0]) + Format(", mh %d)", a[1]);
    }
    for (int i = 0; i < n; i++)
    {
        text += Format(i == 0 ? "[%d" : ", %d", a[i]);
    }
    if (n != 0)
    {
        text += "]";
    }
    if ((op.com == EV_COM_IF || op.com == kComElseIf) && op.p[4] < EV_COND_NUM)
    {
        text += " (" + CondText({op.p[4], op.p + 4}) + ")";
    }
    return text;
}

} // namespace

namespace
{

// A condition judged with no side effect: the player's rectangles here
// (PLYR_AREAIN's handler also claims a priority for the event), the
// conditions whose handlers only read through those handlers, the rest -1.
int Judge(int id, const Cond &c)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       a[8] = {};

    if (!ReiPan_WarpInGame() || plyr == nullptr)
    {
        return -1;
    }
    CondArgs(c, a);
    switch (c.label)
    {
    case kCondPlyrAreaIn:
    case kCondPlyrAreaInDef:
        return MrecIsInEvent(plyr->pos, a[2], plyr->floor) != 0;
    case kCondPlyrAreaOut:
    case kCondPlyrAreaOutDef:
        return MrecIsInEvent(plyr->pos, a[2], plyr->floor) == 0;
    case kCondPlyrRoomIn:
    case kCondPlyrRoomOut:
    case kCondPlyrRot:
    case kCondPlyrIntoSight:
    case kCondEvCntExec:
    case kCondDoorStatCheck:
    case kCondGhostExist:
    case kCondGhostLost:
    case kCondPlyrItemHave:
    case kCondNotHaveItem:
    case kCondEvState:
    case kCondEvNotState:
    case kCondEvFailure:
    case kCondNowChapter:
    case kCondNotNowChapter:
    case kCondCheckGameWorld:
    case kCondCheckDayFlag:
    case kCondBattleMode:
        return EvCondCallJudge((u_char)c.label, id, c.p) == 1;
    default:
        return -1;
    }
}

// The target's open conditions judged, in one log line (scripted runs have
// no window to read).
void LogJudged(int id)
{
    std::string line;

    for (const auto &group : JudgeConditions(id, EV_TBL_OPEN_COND))
    {
        std::string part;
        for (const LiveCond &c : group)
        {
            Append(part, c.text + (c.live == 1 ? " yes" : c.live == 0 ? " NO" : " ?"), ", ");
        }
        Append(line, part, " | or ");
    }
    ReiPan_LogPrintf("[events] ev%d now: %s (area %d, floor %d, mh %d, at %.1f %.1f %.1f, facing %.0f)\n", id,
                     line.c_str(), GetPlyrAreaNo(), ingame_wrk.plyr_wrk->floor, ingame_wrk.plyr_wrk->mh_no,
                     ingame_wrk.plyr_wrk->pos[0], ingame_wrk.plyr_wrk->pos[1], ingame_wrk.plyr_wrk->pos[2],
                     PlyrGetJointRotY(3) * 180.0f / 3.1415927f);
}

} // namespace

std::vector<std::vector<LiveCond>> JudgeConditions(int id, int table)
{
    std::vector<std::vector<LiveCond>> out;

    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount)
    {
        return out;
    }
    for (const Group &group : ParseConds(Stream(table, id)))
    {
        out.emplace_back();
        for (const Cond &c : group)
        {
            out.back().push_back({CondText(c), Judge(id, c)});
        }
    }
    return out;
}

std::string DescribeConditions(int id, int table)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount)
    {
        return "";
    }

    std::string text;
    for (const Group &group : ParseConds(Stream(table, id)))
    {
        std::string line;
        for (const Cond &c : group)
        {
            Append(line, CondText(c), " and ");
        }
        Append(text, line.empty() ? "nothing (at once)" : line, "\n  or ");
    }
    return text;
}

std::string DescribePrograms(int id, bool args)
{
    if (GetCatalogue() == nullptr || id < 0 || id >= kEventCount)
    {
        return "";
    }

    const struct
    {
        int         table;
        const char *name;
    } programs[] = {
        {EV_TBL_INIT_PRG, "init"},
        {EV_TBL_EXE_PRG, "main"},
        {EV_TBL_END_PRG, "end"},
    };

    std::string text;
    for (const auto &program : programs)
    {
        const std::vector<Op> ops = ParseProgram(Stream(program.table, id));
        std::string           body;

        if (args)
        {
            int depth = 1;
            for (const Op &op : ops)
            {
                const bool branch = op.com > EV_COM_IF && op.com <= EV_COM_ENDIF;   // ELSE_, ELSEIF_, ENDIF_
                body += "\n" + std::string((size_t)std::max(depth - branch, 1) * 2, ' ') + OpText(op);
                depth += op.com == EV_COM_IF ? 1 : op.com == EV_COM_ENDIF ? -1 : 0;
            }
        }
        else
        {
            for (size_t i = 0; i < ops.size();)
            {
                size_t run = 1;
                while (i + run < ops.size() && ops[i + run].com == ops[i].com)
                {
                    run++;
                }
                std::string item = EvMacroGetWrk(ops[i].com)->name;
                if (run > 1)
                {
                    item += Format(" x%d", (int)run);
                }
                Append(body, item);
                i += run;
            }
            if (!body.empty())
            {
                body = " " + body;
            }
        }
        if (!body.empty())
        {
            Append(text, std::string(program.name) + ":" + body, "\n");
        }
    }
    return text.empty() ? "no programs" : text;
}

std::string DescribePlace(const Place &place)
{
    return PlaceText(place);
}

const char *ChapterLabel(int chapter)
{
#ifdef REIPAN_SLPS_25544_CHS
    static constexpr const char *labels[kChapterCount] = {
        "序章", "一之刻 征兆", "二之刻 沉睡之家", "三之刻 镇魂歌",
        "四之刻 祸梦", "五之刻 消失", "六之刻 人柱", "七之刻 牢狱",
        "八之刻 虚梦", "九之刻 破戒", "十之刻 刺魂之仪", "十一之刻 终途",
        "十二之刻 戒之仪", "十三之刻 刺青之声", "终之刻 涯之渊",
    };
    return chapter >= 0 && chapter < kChapterCount ? labels[chapter] : "?";
#else
    return chapter >= 0 && chapter < kChapterCount ? kChapterNames[chapter] : "?";
#endif
}

int CurrentChapter()
{
    return ReiPan_WarpInGame() ? (int)ingame_wrk.b01 : -1;
}

int CurrentWorld()
{
    return ReiPan_WarpInGame() ? (int)ingame_wrk.b03 : -1;
}

Place PlayerPlace()
{
    Place place;

    if (ReiPan_WarpInGame() && ingame_wrk.plyr_wrk != nullptr)
    {
        place.area = GetPlyrAreaNo();
    }
    return place;
}

} // namespace ReiPanEvents

void ReiPan_EventsFrame(void)
{
    ReiPanEvents::Step();
}
