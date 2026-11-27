/* ==========================================================================
 *  reipan/debug/reipan_warp.cpp
 *
 *  The "Start point / snapshots" debug window and the work it asks for
 *  (reipan_warp.h). PC only; it drives game code the way the game's own
 *  paths do and adds nothing to them:
 *
 *    leave play      GPhaseCtrlRequest(5) after SndAllBufStop, as pause ->
 *                    quit (pause.c), and only from Story_Normal (32);
 *    start the game  from Title_Top / Title_Menu / Title_NewGame once idle: TitleMenuDecide(0)
 *                    and TitleMenuFlagSet as New Game, then Story_Main (9).
 *                    An idle title phase's one() returns 0, so the request
 *                    is committed (Change) and the leaf ended through the
 *                    engine's own end path (END_WAIT), what its one()
 *                    returning 1 would have done;
 *    apply           in Story_NowLoading (30) once its loads are done and
 *                    before its state 1 picks the start area: a warp writes
 *                    the area, side, world, chapter and player; a snapshot
 *                    is scattered over the save blocks like the card's
 *                    McLoadGameMcLoadWait (+ EventAfterLoad) and restored
 *                    like one_Story_Start's IngameLoadRestore, which ran
 *                    before with title_start_flg.load 0 and did nothing;
 *    place           while Story_NowLoading holds its last state (the room
 *                    is loaded): what AREA_CHANGE does for the start area
 *                    (EnterArea; without it a game that starts in place
 *                    draws nothing), then the position as PLYR_POS_SET;
 *    teleport        in play, EvAreaChange's two steps and the position;
 *    move            in play, into the other world (the event browser):
 *                    that world's EVENT_REBOOT, then the save blocks
 *                    gathered as for a snapshot and loaded back with the
 *                    area, world and player model changed;
 *    map sky        in play (sky=1), the tattoo event's map-sky effect at
 *                    Rei: the sky dome and its movies (effect_mapsky.c);
 *    game clear      in play, GAME_CLEAR_ (EvGameClear: phase 42); "treat
 *                    the game as cleared" puts a clear into FINDER_STATE_WRK
 *                    at the title and in Story_Start, as a cleared save
 *                    restores it, so New Game opens the start menu.
 *
 *  Snapshots are ReiPan's own game-file layout (the save blocks are host
 *  structs, save_data.h), so a file only loads into a build whose block
 *  sizes match; the head records them and the load checks them in play.
 * ======================================================================== */
#include "reipan/debug/reipan_warp.h"
#include "../reipan_ui/reipan_ui_locale.h"

#include "graphics/graph2d/fade.h"            // fade_ctrl (the diagnostic line)
#include "ingame/cat/cat_ctrl.h"                // CatGetWrk, CatSetActive, CatMdlCreate, ...
#include "ingame/enemy/random_ghost.h"          // RandGhostAreaChange
#include "graphics/movie/movie.h"
#include "graphics/scene/scene.h"
#include "ingame/movie_room_menu/prg/movie_projecter.h"
#include "system/eeiop/cddat.h"
#include "system/playpss/playpss.h"
#include "ingame/event/prg/ev_change.h"         // GetEvState, SetEventState, GetEvExeCount, EV_EVENT_NUM
#include "ingame/event/prg/ev_exe.h"            // SetEventInitStatus
#include "ingame/event/prg/ev_command.h"        // EvGameClear (clear=)
#include "graphics/effect/effect_mapsky.h"      // EffMapSkyReq (sky=)
#include "graphics/graph3d/gra3d.h"             // gra3dcamGetPosition (sky=2)
#include "ingame/event/prg/ev_get.h"            // EvGetRot360
#include "ingame/event/prg/ev_main.h"           // EventSetMode / GetMode / SetStop, EventEnd, EventAfterSave / AfterLoad
#include "ingame/event/prg/ev_open.h"           // EventSetOpenCondition
#include "ingame/ingame.h"                      // GPhaseCtrlRequest, IngameSaveInit, IngameLoadRestore
#include "ingame/ingame_wrk.h"                  // ingame_wrk, title_start_flg
#include "ingame/menu/start_menu.h"              // GetFinderStateWrk, FinderStateHasAny (game cleared)
#include "ingame/item/prg/item.h"               // ItemSetNum
#include "ingame/memo/memo.h"                   // MemoSetStatus
#include "ingame/menu/menu_cassette.h"           // CassetteMenuReq
#include "ingame/map/MapCtrl.h"                 // MapAreaGetHeight
#include "ingame/map/map_rectangle.h"           // MrecSetCameraSet
#include "ingame/map/MapLoad.h"                 // MapLoadGetOffset, MapLoadGetBuffID, MapLoadSetDrawFlg
#include "ingame/map/MapRcb.h"                  // RcbGetFloor
#include "ingame/map/MapRoomFile.h"             // RoomFileGetObjPack
#include "ingame/map/RegDat.h"                  // RegDatGetTopAddr, RegDatVecFind4Label, RegDatGetVecPtr
#include "ingame/photo/photo_obj.h"             // PhotoObjAreaOn / Off
#include "ingame/plyr/player.h"                 // PLYR_WRK, PlyrSetCurrent, PlyrSetPos / Rot, SetPlyrAnime
#include "ingame/plyr/plyr_sub.h"               // PlyrMdlMotReset
#include "ingame/puzzle/puzzle.h"               // PuzzleSet
#include "ingame/sound/snd_ctrl.h"              // SndAllBufStop, SndSeBankRelease
#include "ingame/story_door.h"                  // MapAreaNoGet, MapAreaSideGet, MapAreaGetSaveData
#include "ingame/story_save.h"                  // StorySaveGameReq
#include "main/gphase.h"                        // GetGPhaseCtrl, GPHASE_NODE, GPHASE_STATE_*
#include "outgame/option.h"                     // OptionApplySound
#include "outgame/title_menu.h"                 // TitleMenuDecide, TitleMenuFlagSet
#include "reipan/io/reipan_log.h"               // ReiPan_LogPrintf
#include "reipan/io/reipan_paths.h"             // ReiPan_PathResolveUser / PrepareUser
#include "system/eeiop/cd_file_dat.h"           // MENU_START_BD_BD
#include "system/mc/dat/save_data.h"            // save_game_data
#include "system/mc/prg/mc_check_broken.h"      // MemoryCardCheckFileBroken
#include "system/mc/prg/mc_cmn.h"               // Calc / SetMemoryCardDataCheckSum
#include "system/os/eecdvd.h"                   // IsLoadEndAll

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace
{
using ReiPanUiLocale::UiText;


// ──────────────────────────────────────────────────────────────────────
// Phases and places
// ──────────────────────────────────────────────────────────────────────

enum
{
    PHASE_TITLE_MODE       = 5,
    PHASE_STORY_MAIN       = 9,
    PHASE_STORY_START      = 29,
    PHASE_TITLE_TOP        = 15,
    PHASE_TITLE_MENU       = 17,
    PHASE_STORY_NOWLOADING = 30,
    PHASE_TITLE_NEWGAME    = 51,
    PHASE_STORY_NORMAL     = 32,
    PHASE_STORY_SAVE       = 41,
};

constexpr int kAreaMax    = 59;     // area_data.ecb rows 1..59
constexpr int kHouseFirst = 47;     // areas above 0x2e are the real-world house (IngameIsIdOver2e)
constexpr int kChapterMax = 14;     // ingame_wrk.b01 of the 15 chapter events under event 88

// Room of each area, from map_menu.ecb (first row per area; floor * 100 +
// room, the house is floor 0). Areas 53..59 have no map room.
const short kAreaRoom[kAreaMax + 1] =
{
    0,
    101, 102, 103, 104, 105, 106,                                   //  1..6
    201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213,    //  7..19
    301, 302, 303, 304, 305, 306, 307, 308,                         // 20..27
    401, 402, 403, 404, 405, 406, 407, 408, 409, 410,               // 28..37
    501, 502, 503, 504, 505, 506, 507, 508, 509,                    // 38..46
    1, 2, 3, 4, 5, 6,                                               // 47..52 the house
    0, 0, 0, 0, 0, 0, 0,                                            // 53..59
};

// The cat's six position labels (cat_ctrl.c cat_label_tbl, rodata 0x34f920):
// area and world position, facing in degrees. Places inside the house rooms
// to put Rei next to.
struct CatLabelPlace
{
    int   area;
    float pos[3];
    float rot;
};
const CatLabelPlace kCatLabel[CAT_LABEL_NUM] =
{
    { 0x30, { 158.6f,  -7.9f, 244.0f  },  103.0f },
    { 0x30, { 187.5f,   0.0f, 218.1f  },   49.1f },
    { 0x32, {  89.6f, -58.4f, 111.47f },    0.0f },
    { 0x31, {  94.4f, -7.95f,  92.75f }, -137.0f },
    { 0x30, { 121.3f, -50.0f, 178.4f  },  180.0f },
    { 0x31, { 200.2f, -15.1f, 139.9f  },  360.0f },
};

GPHASE_NODE *Leaf()
{
    return GetGPhaseCtrl()->leaf;
}

bool StackHas(short id)
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

// Story_Normal running with nothing requested: play, between two frames.
bool InPlay()
{
    const GPHASE_NODE *leaf = Leaf();

    return leaf != nullptr && leaf->id == PHASE_STORY_NORMAL && leaf->state == GPHASE_STATE_RUN
        && GetGPhaseCtrl()->next == -1 && ingame_wrk.plyr_wrk != nullptr;
}

std::string AreaName(int area)
{
    char buf[48];

    if (area >= kHouseFirst && area <= 52)
    {
        std::snprintf(buf, sizeof(buf), "%2d  house room %d", area, kAreaRoom[area]);
    }
    else if (area >= 1 && area <= kAreaMax && kAreaRoom[area] != 0)
    {
        std::snprintf(buf, sizeof(buf), "%2d  manor room %d", area, kAreaRoom[area]);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%2d  (no map room)", area);
    }
    return buf;
}

// ──────────────────────────────────────────────────────────────────────
// Requests
// ──────────────────────────────────────────────────────────────────────

enum SpawnKind
{
    SPAWN_GAME,         // wherever the new game puts the player
    SPAWN_ROOM,         // x / z from the room origin, as PLYR_POS_SET
    SPAWN_CAT_LABEL,    // beside one of the cat's position labels
    SPAWN_EV_LABEL,     // on an event rectangle (the event browser)
};

struct WarpParams
{
    int area         = 48;
    int side         = 0;       // MapAreaSideSet: 0 day, 1 night
    int world        = 1;       // ingame_wrk.b03: 0 dream, 1 real
    int chapter      = 0;       // ingame_wrk.b01
    int player       = 0;       // 0 Rei, 1 Miku, 2 Kei
    int debug_events = 0;       // EVENT_DB_OBJ (event 1746 opens instead of 1060)
    int stop_events  = 0;       // EventSetStop: no event program runs
    int spawn        = SPAWN_CAT_LABEL;
    int room_x       = 39;
    int room_z       = 39;
    int rot          = 0;       // SPAWN_ROOM facing, degrees
    int mh           = -1;      // SPAWN_ROOM / SPAWN_EV_LABEL: map hit object (PLYR_FLOOR_CHANGE), -1 keep
    int ev_label     = -1;      // SPAWN_EV_LABEL: PLYR_AREAIN's rectangle label
    int look         = 0;       // SPAWN_ROOM / SPAWN_EV_LABEL: face (look_x, look_z) from the room origin
    int look_x       = 0;
    int look_z       = 0;
    void (*settle)(void) = nullptr;     // a warp: called once the work is applied (ReiPan_WarpJump)
    int fade_in      = 0;               // a warp: fade the screen in once placed
    int cat_label    = 0;
    int then_label   = -1;      // after a warp: teleport beside this cat label once play has run a while
    int then_save    = 0;       // after a warp: save a snapshot after 12 s of play (scripted runs)
    int then_puzzle  = -1;      // after a warp: open this puzzle type (PUZZLE_START) after 6 s of play
    int puzzle_idx   = 0;       // its board
    int then_item    = -1;      // after a warp: give this item first (a puzzle's key item)
    int then_fill    = 0;       // after a warp: three of every item, every memo found (testing the menus)
    int then_phase   = -1;      // after a warp: request this game phase after 6 s of play (testing the menus)
    int phase_wait   = 150;
    int then_clear   = -1;      // after a warp: run GAME_CLEAR_ with this ending (0 / 1) after 6 s of play
    int then_sky     = 0;       // after a warp: the map-sky effect (the sky dome) after 6 s of play: 1 at Rei, 2 on the camera
};

enum JobKind
{
    JOB_NONE,
    JOB_WARP,
    JOB_LOAD,
    JOB_MOVE,           // a load of the game in memory into another world (ReiPan_WarpGoTo)
};

enum JobStage
{
    STAGE_LEAVE,        // out of play, then wait for the idle title
    STAGE_QUITTING,     // Title_Mode requested from play
    STAGE_STARTING,     // Story_Main requested; wait for Story_NowLoading
    STAGE_APPLIED,      // work set; wait for the room, place the player
};

// A snapshot's PC-only tail: a copy of what the card's blocks 27 (ingame_wrk,
// IngameWrkSaveGet) and 28 (plyr_wrk, PlyrWrkSaveGet) hold. Written while
// both were stubs; since 2026-10-21 the blocks restore these fields too and
// the tail only re-applies them (and places the player at state 3, after
// Story_NowLoading's setup). Drop it once a snapshot load is seen to place
// her from block 28 alone.
struct SnapExtra
{
    u_char    b00, b01, b02, b03, b04;  // player, chapter, chapter shown, world, dream count
    u_char    mdl_kind, mdl_mot;        // the player's PLYR_WRK._2e8 / _2ec (PlyrMdlCreate)
    u_char    _07;
    u_int     point, best_score;
    PLAY_TIME play_time;
    int       difficulty, _24, _28;
    u_char    lock_tbl[sizeof(INGAME_WRK::lock_tbl)];
    float     pos[4], rot[4];           // the player
    int       mh_no;                    // her map hit object (MapAreaMhSet)
};

struct Job
{
    JobKind                    kind  = JOB_NONE;
    JobStage                   stage = STAGE_LEAVE;
    WarpParams                 warp;
    std::vector<unsigned char> data;        // JOB_LOAD / JOB_MOVE: the snapshot's game data
    uint32_t                   block_num  = 0;  // JOB_LOAD / JOB_MOVE: its block layout (SnapHead)
    uint32_t                   block_hash = 0;
    SnapExtra                  extra{};
    std::string                what;
    int                        frames = 0;
    int                        event_mode = 0;
};

// A move within play, as an event's AREA_CHANGE + PLYR_FLOOR_CHANGE +
// PLYR_POS_SET (EvAreaChange's two steps, then the position).
struct Teleport
{
    bool       active = false;
    int        stage  = 0;          // 0 wait, 1 request the room, 2 wait for it, 3 place
    int        wait   = 0;          // play frames before it starts
    WarpParams p;                   // area, spawn
    bool       placed = false;
};

Job                                g_job;
Teleport                           g_tp;
std::string                        g_status = UiText("Idle.");
std::vector<std::function<void()>> g_commands;  // window buttons, run at the next frame start
bool                               g_save_request;
int                                g_save_after;  // > 0: play frames before a scripted snapshot
int                                g_fill_after;    // > 0: play frames before the full inventory
int                                g_phase_after;   // > 0: play frames before a scripted phase request
int                                g_phase_id;      // ... of this phase
int                                g_puzzle_after;  // > 0: play frames before a scripted PUZZLE_START
WarpParams                         g_puzzle;        // its then_puzzle / puzzle_idx / then_item
int                                g_clear_after;   // > 0: play frames before a scripted GAME_CLEAR_
int                                g_clear_ending;  // its ending (GAME_CLEAR_'s args[0])
int                                g_sky_after;     // > 0: play frames before a scripted EffMapSkyReq
int                                g_sky_mode;      // its then_sky; 2: the dome follows the camera
sceVu0FVECTOR                      g_sky_pos;       // the effect's position (it keeps the pointer)
int                                g_cleared = -1;  // >= 0: treat the game as cleared at this difficulty (MarkCleared)
int                                g_cat_log_frames;  // > 0: log the cat again after this many frames
bool                               g_list_dirty = true;
bool                               g_env_done;

void SetStatus(const std::string &text)
{
    if (text != g_status)
    {
        g_status = text;
        ReiPan_LogPrintf("[warp] %s\n", text.c_str());
    }
}

// ──────────────────────────────────────────────────────────────────────
// Snapshot files
// ──────────────────────────────────────────────────────────────────────

constexpr char kSnapMagic[8] = { 'R', 'P', 'S', 'N', 'A', 'P', '\0', '\4' };
constexpr const char *kSnapDir = "debug/snapshots";

struct SnapHead
{
    char     magic[8];
    uint32_t head_size;
    uint32_t data_size;         // the blocks + the card's 4-byte checksum
    uint32_t extra_size;        // the SnapExtra after them
    uint32_t block_num;
    uint32_t block_hash;        // FNV-1a of the block sizes: the build's layout
    int32_t  area, side, world, chapter, chapter_disp, player;
    int32_t  hour, min, sec;
    int64_t  saved;             // time(nullptr)
    float    pos[4];
};

struct SnapEntry
{
    std::string path;
    std::string label;
    bool        ok = false;     // loadable by this build
    SnapHead    head{};
};

std::vector<SnapEntry> g_snaps;

// A game file's blocks, from the card's manifest (save_data.c). The card's
// gather (SetMemoryCardSaveDataToBuff) stops at the first block without an
// address, as FF3 does; a stubbed save callback would cut a card save short
// there (none is left since 2026-10-21). Snapshots skip such blocks instead
// and keep the card's checksum at the end.
struct SaveBlock
{
    char *addr;
    int   size;                 // 0 for a stub's block
};

std::vector<SaveBlock> SaveBlocks()
{
    std::vector<SaveBlock> blocks;

    for (int i = 0; save_game_data[i] != nullptr; i++)
    {
        void *addr = nullptr;
        int   size = save_game_data[i](&addr);

        blocks.push_back({ (char *)addr, addr != nullptr && size > 0 ? size : 0 });
    }
    return blocks;
}

// Bytes of a snapshot's game data (blocks + checksum) and the layout hash.
int SnapDataSize(const std::vector<SaveBlock> &blocks, uint32_t *hash)
{
    uint32_t h    = 2166136261u;
    int      size = 4;

    for (const SaveBlock &b : blocks)
    {
        size += b.size;
        for (int k = 0; k < 4; k++)
        {
            h = (h ^ ((uint32_t)b.size >> (8 * k) & 0xff)) * 16777619u;
        }
    }
    if (hash != nullptr)
    {
        *hash = h;
    }
    return size;
}

std::filesystem::path UserPath(const std::string &relative, bool prepare)
{
    char out[1024];
    int  ok = prepare ? ReiPan_PathPrepareUser(relative.c_str(), out, sizeof(out))
                      : ReiPan_PathResolveUser(relative.c_str(), out, sizeof(out));

    return ok ? std::filesystem::path(out) : std::filesystem::path();
}

bool ReadFile(const std::filesystem::path &path, std::vector<unsigned char> &out)
{
    std::FILE *fp = std::fopen(path.string().c_str(), "rb");

    if (fp == nullptr)
    {
        return false;
    }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    out.resize(size > 0 ? (size_t)size : 0);
    bool ok = size > 0 && std::fread(out.data(), 1, out.size(), fp) == out.size();
    std::fclose(fp);
    return ok;
}

void RefreshList()
{
    std::error_code              ec;
    const std::vector<SaveBlock> blocks = SaveBlocks();
    uint32_t                     hash;
    int                          size = SnapDataSize(blocks, &hash);

    g_snaps.clear();
    g_list_dirty = false;

    std::filesystem::path dir = UserPath(kSnapDir, false);
    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
    {
        return;
    }
    for (const auto &it : std::filesystem::directory_iterator(dir, ec))
    {
        if (it.path().extension() != ".rps")
        {
            continue;
        }
        SnapEntry  e;
        std::FILE *fp = std::fopen(it.path().string().c_str(), "rb");

        if (fp == nullptr)
        {
            continue;
        }
        bool got = std::fread(&e.head, sizeof(e.head), 1, fp) == 1;
        std::fclose(fp);
        if (!got || std::memcmp(e.head.magic, kSnapMagic, sizeof(kSnapMagic)) != 0)
        {
            continue;
        }
        e.path  = it.path().string();
        e.label = it.path().stem().string();
        // The layout can only be compared in play (blocks get their
        // addresses in IngameInit); a mismatch is refused when it loads.
        e.ok = e.head.head_size == sizeof(SnapHead)
            && (!InPlay() || (e.head.block_hash == hash && (int)e.head.data_size == size));
        g_snaps.push_back(e);
    }
    std::sort(g_snaps.begin(), g_snaps.end(),
              [](const SnapEntry &a, const SnapEntry &b) { return a.head.saved > b.head.saved; });
}

// Copy the game the way a card save copies it (game_data_save.c
// GameDataSaveMcSaveInit: IngameSaveInit's stores, EventEnd, the gather and
// its checksum, EventAfterSave), with the head and the tail. Play only.
void GatherGame(std::vector<unsigned char> &data, SnapHead &head, SnapExtra &extra)
{
    std::vector<SaveBlock> blocks = SaveBlocks();
    int                    size   = SnapDataSize(blocks, &head.block_hash);
    size_t                 at     = 0;

    data.assign((size_t)size, 0);
    IngameSaveInit();
    EventEnd();
    for (const SaveBlock &b : blocks)
    {
        if (b.size > 0)
        {
            std::memcpy(data.data() + at, b.addr, (size_t)b.size);
            at += (size_t)b.size;
        }
    }
    SetMemoryCardDataCheckSum((char *)data.data() + size - 4,
                              CalcMemoryCardDataCheckSum((char *)data.data(), size - 4));
    EventAfterSave();

    std::memcpy(head.magic, kSnapMagic, sizeof(kSnapMagic));
    head.head_size    = sizeof(SnapHead);
    head.data_size    = (uint32_t)size;
    head.block_num    = (uint32_t)blocks.size();
    head.area         = MapAreaNoGet();
    head.side         = MapAreaSideGet();
    head.world        = ingame_wrk.b03;
    head.chapter      = ingame_wrk.b01;
    head.chapter_disp = (s_char)ingame_wrk.b02;
    head.player       = PlyrGetCurNo();
    head.hour         = ingame_wrk.play_time.hour;
    head.min          = ingame_wrk.play_time.min;
    head.sec          = ingame_wrk.play_time.sec;
    head.saved        = (int64_t)std::time(nullptr);
    std::memcpy(head.pos, ingame_wrk.plyr_wrk->pos, sizeof(head.pos));
    head.extra_size   = sizeof(SnapExtra);

    extra.b00        = ingame_wrk.b00;
    extra.b01        = ingame_wrk.b01;
    extra.b02        = ingame_wrk.b02;
    extra.b03        = ingame_wrk.b03;
    extra.b04        = ingame_wrk.b04;
    extra.mdl_kind   = ingame_wrk.plyr_wrk->_2e8;
    extra.mdl_mot    = ingame_wrk.plyr_wrk->_2ec;
    extra.point      = ingame_wrk.point;
    extra.best_score = ingame_wrk.best_score;
    extra.play_time  = ingame_wrk.play_time;
    extra.difficulty = ingame_wrk.difficulty;
    extra._24        = ingame_wrk._24;
    extra._28        = ingame_wrk._28;
    std::memcpy(extra.lock_tbl, ingame_wrk.lock_tbl, sizeof(extra.lock_tbl));
    std::memcpy(extra.pos, ingame_wrk.plyr_wrk->pos, sizeof(extra.pos));
    std::memcpy(extra.rot, ingame_wrk.plyr_wrk->rot, sizeof(extra.rot));
    extra.mh_no      = ingame_wrk.plyr_wrk->mh_no;
}

void SaveSnapshot()
{
    if (!InPlay())
    {
        SetStatus("Snapshots are taken during play (Story_Normal), not in menus or scenes.");
        return;
    }

    std::vector<unsigned char> data;
    SnapHead                   head{};
    SnapExtra                  extra{};
    GatherGame(data, head, extra);

    char        stamp[32];
    char        name[96];
    std::time_t now = (std::time_t)head.saved;
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    std::snprintf(name, sizeof(name), "%s/%s_area%02d_ch%02d.rps", kSnapDir, stamp, head.area, head.chapter);

    std::filesystem::path path = UserPath(name, true);
    std::FILE            *fp   = path.empty() ? nullptr : std::fopen(path.string().c_str(), "wb");
    if (fp == nullptr)
    {
        SetStatus(std::string("Could not write ") + name + ".");
        return;
    }
    bool ok = std::fwrite(&head, sizeof(head), 1, fp) == 1
           && std::fwrite(data.data(), 1, data.size(), fp) == data.size()
           && std::fwrite(&extra, sizeof(extra), 1, fp) == 1;
    std::fclose(fp);
    g_list_dirty = true;
    SetStatus(ok ? "Saved " + path.filename().string() + "." : "Write failed: " + path.string());
}

// ──────────────────────────────────────────────────────────────────────
// The job
// ──────────────────────────────────────────────────────────────────────

void RequestWarp(const WarpParams &p)
{
    g_job       = Job{};
    g_job.kind  = JOB_WARP;
    g_job.warp  = p;
    char what[96];
    std::snprintf(what, sizeof(what), "warp to area %d (%s, %s), chapter %d", p.area,
                  p.world ? UiText("real world") : "dream", p.side ? "night" : "day", p.chapter);
    g_job.what = what;
    SetStatus("Requested " + g_job.what + ".");
}

void RequestLoad(const SnapEntry &e)
{
    std::vector<unsigned char> file;
    SnapHead                   head{};

    if (!ReadFile(e.path, file) || file.size() < sizeof(SnapHead))
    {
        SetStatus("Could not read " + e.path + ".");
        return;
    }
    std::memcpy(&head, file.data(), sizeof(head));
    int size = (int)head.data_size;
    if (std::memcmp(head.magic, kSnapMagic, sizeof(kSnapMagic)) != 0 || head.head_size != sizeof(SnapHead)
        || head.extra_size != sizeof(SnapExtra) || size < 4
        || file.size() < sizeof(SnapHead) + (size_t)size + sizeof(SnapExtra))
    {
        SetStatus(e.label + " is not a snapshot of this version.");
        return;
    }
    // The block layout is checked in Story_NowLoading (ApplyLoad): before
    // IngameInit some blocks have no address yet.
    std::vector<unsigned char> data(file.begin() + sizeof(SnapHead), file.begin() + sizeof(SnapHead) + size);
    if (MemoryCardCheckFileBroken(data.data(), size) == 0)
    {
        SetStatus(e.label + ": the checksum does not match.");
        return;
    }
    g_job            = Job{};
    g_job.kind       = JOB_LOAD;
    g_job.data       = std::move(data);
    g_job.block_num  = head.block_num;
    g_job.block_hash = head.block_hash;
    std::memcpy(&g_job.extra, file.data() + sizeof(SnapHead) + size, sizeof(SnapExtra));
    g_job.what       = "load " + e.label;
    SetStatus("Requested " + g_job.what + ".");
}

// The game in memory, loaded back into p's area in the other world. In
// play, at the top of a frame. First what the scripts' world changes run
// after GO_REAL_WORLD / GO_DREAM_WORLD: EVENT_REBOOT 7 waking up (event 2,
// the chapter starts), 8 falling asleep (event 3), which re-arm the events
// parked until then. The real world is Rei's; the player models are the
// ones ApplyWarp picks.
void RequestMove(const WarpParams &p)
{
    int         args[1] = {p.world ? 7 : 8};
    EV_EXE_CTRL ctrl    = {};
    SnapHead    head{};

    ctrl.args = args;
    EvEventReboot(&ctrl);

    g_job      = Job{};
    g_job.kind = JOB_MOVE;
    g_job.warp = p;
    GatherGame(g_job.data, head, g_job.extra);
    g_job.block_num  = head.block_num;
    g_job.block_hash = head.block_hash;
    g_job.extra.b03  = (u_char)p.world;
    if (p.world)
    {
        g_job.extra.b00 = 0;
    }
    g_job.extra.mdl_kind = (u_char)(p.world ? p.side : 2);
    g_job.extra.mdl_mot  = (u_char)(p.world ? 0 : 1);
    g_job.what = "move to area " + std::to_string(p.area) + (p.world ? UiText(" (real world)") : UiText(" (dream)"));
    SetStatus("Requested " + g_job.what + ".");
}

// New Game from an idle title phase (Title_Top, Title_Menu or the difficulty
// select Title_NewGame).
void StartFromTitle()
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    TitleMenuDecide(0);                         // load 0, mode 1, event mode 0
    TitleMenuFlagSet();                         // difficulty: normal
    SndSeBankRelease(MENU_START_BD_BD, 0);      // Title_Menu keeps it for New Game's OK
    if (g_job.kind == JOB_WARP && g_job.warp.debug_events)
    {
        EventSetMode(2);                        // EventDataLoad (IngameInit): EVENT_DB_OBJ
    }
    g_job.event_mode = EventGetMode();

    ctrl->SetNext(PHASE_STORY_MAIN, 0);
    ctrl->Change();
    ctrl->leaf->SetState(GPHASE_STATE_END_WAIT);
}

void ApplyWarp(const WarpParams &p)
{
    int *map_area = nullptr;

    // NowLoading's state 1 sets area 1 for a new game unless the event mode
    // is 2; that is the only other reader (EventDataLoad has run).
    EventSetMode(2);
    MapAreaGetSaveData((void **)&map_area);     // {no, side}: MapAreaSideSet would free a room here
    map_area[0] = p.area;
    map_area[1] = p.side;
    ingame_wrk.b03 = (u_char)p.world;
    ingame_wrk.b01 = (u_char)p.chapter;
    PlyrSetCurrent((u_char)p.player);
    // PlyrMdlCreate's kind and motion file: the real world's by side (as
    // GO_REAL_WORLD), the dream's 2 with the light (as GO_DREAM_WORLD).
    ingame_wrk.plyr_wrk->_2e8 = (u_char)(p.world ? p.side : 2);
    ingame_wrk.plyr_wrk->_2ec = (u_char)(p.world ? 0 : 1);
    if (p.stop_events)
    {
        EventSetStop(1);
    }
}

// The scatter McLoadGameMcLoadWait does (DevelopMemoryCardLoadData, over
// the blocks SaveSnapshot wrote), then the restore one_Story_Start's
// IngameLoadRestore would have done with title_start_flg.load set.
bool ApplyLoad()
{
    const std::vector<SaveBlock> blocks = SaveBlocks();
    uint32_t                     hash;
    size_t                       at = 0;

    if ((size_t)SnapDataSize(blocks, &hash) != g_job.data.size() || hash != g_job.block_hash
        || g_job.block_num != blocks.size())
    {
        return false;                           // another build's save blocks
    }
    // REIPAN_SNAP_SKIP="51,52": blocks left as the new game has them, to
    // find a block whose restore breaks the game (bisect with it).
    std::vector<int> skip;
    if (const char *env = std::getenv("REIPAN_SNAP_SKIP"))
    {
        for (const char *p = env; *p != '\0'; p++)
        {
            if (*p >= '0' && *p <= '9' && (p == env || p[-1] < '0' || p[-1] > '9'))
            {
                skip.push_back(std::atoi(p));
            }
        }
    }
    for (size_t i = 0; i < blocks.size(); i++)
    {
        const SaveBlock &b = blocks[i];

        if (b.size > 0)
        {
            if (std::find(skip.begin(), skip.end(), (int)i) == skip.end())
            {
                std::memcpy(b.addr, g_job.data.data() + at, (size_t)b.size);
            }
            at += (size_t)b.size;
        }
    }

    // The tail again over blocks 27 / 28 (PlyrSetCurrent re-points plyr_wrk;
    // IngameLoadRestore's PlyrLoadRestore does too).
    const SnapExtra &x = g_job.extra;
    ingame_wrk.b01        = x.b01;
    ingame_wrk.b02        = x.b02;
    ingame_wrk.b03        = x.b03;
    ingame_wrk.b04        = x.b04;
    ingame_wrk.point      = x.point;
    ingame_wrk.best_score = x.best_score;
    ingame_wrk.play_time  = x.play_time;
    ingame_wrk.difficulty = x.difficulty;
    ingame_wrk._24        = x._24;
    ingame_wrk._28        = x._28;
    std::memcpy(ingame_wrk.lock_tbl, x.lock_tbl, sizeof(x.lock_tbl));
    PlyrSetCurrent(x.b00);
    ingame_wrk.plyr_wrk->_2e8 = x.mdl_kind;
    ingame_wrk.plyr_wrk->_2ec = x.mdl_mot;
    if (g_job.kind == JOB_MOVE)
    {
        int *map_area = nullptr;

        MapAreaGetSaveData((void **)&map_area);     // {no, side}, as ApplyWarp
        map_area[0] = g_job.warp.area;
        map_area[1] = g_job.warp.side;
    }

    EventAfterLoad();
    title_start_flg.load = 1;
    IngameLoadRestore();
    OptionApplySound();                         // IngameLoadEnd's: the save's options
    return true;
}

// What AREA_CHANGE's MapAreaChange does, for the area the game started in:
// the player's map hit object and floor, and the area's camera set. A game
// that starts in place gets them from the save's player block (block 28,
// PlyrWrkSaveGet) or from its start event's AREA_CHANGE; without
// them the room draws nothing.
void EnterArea(int mh)
{
    int           area = MapAreaNoGet();
    MAP_AREA_DAT *dat  = MapAreaDataGet((s_char)area);

    MapAreaMhSet(area, mh);
    if (dat != nullptr)
    {
        MrecSetCameraSet(dat->cam_type);
    }
}

// A point well inside a registration rectangle, in world space
// (RegDatSetOffset moved the corners to the area): halfway between its
// middle and the middle of its triangle 0-1-2. Not the middle itself: that
// lies on the diagonal the hit tests split the rectangle along, and a point
// on an edge counts as outside (HcBaseIsInTriXZ), so PLYR_AREAIN would never
// hold there.
void RectCentre(const MB_OUT_RECT *rec, float *out)
{
    for (int i = 0; i < 3; i += 2)
    {
        const float middle = (rec->vec[0][i] + rec->vec[1][i] + rec->vec[2][i] + rec->vec[3][i]) * 0.25f;
        const float tri    = (rec->vec[0][i] + rec->vec[1][i] + rec->vec[2][i]) / 3.0f;

        out[i] = (middle + tri) * 0.5f;
    }
}

// Twice the XZ area of a rectangle's quad.
float RectArea(const MB_OUT_RECT *rec)
{
    float sum = 0.0f;

    for (int i = 0; i < 4; i++)
    {
        const float *a = rec->vec[i];
        const float *b = rec->vec[(i + 1) % 4];

        sum += a[0] * b[2] - b[0] * a[2];
    }
    return std::fabs(sum);
}

// Event rectangle `label` (PLYR_AREAIN's: area_id * 1000 + record): the
// middle of its first rectangle in the registration files resident, and the
// floor of the file it is in (MB_OUT_HEAD.kai).
bool EvLabelCentre(int label, float *pos, int *kai)
{
    for (int b = 0; b < REG_DAT_BUFF_NUM; b++)
    {
        MB_OUT_HEAD *hp = RegDatGetTopAddr(b);

        if (hp == nullptr || hp->area_id != label / 1000 || RegDatVecFind4Label(b, label) < 0)
        {
            continue;
        }
        const MB_OUT_RECT *rec = RegDatVecNextFind(b);
        if (rec != nullptr)
        {
            RectCentre(rec, pos);
            *kai = hp->kai;
            return true;
        }
    }
    return false;
}

// The middle of the area's largest room outline rectangle and its floor.
bool RoomCentre(int area, float *pos, int *kai)
{
    MAP_ROOM_DAT      *dat  = MapLoadGetFreeMemAddr(MapLoadGetBuffID(area));
    const MB_OUT_RECT *best = nullptr;
    float              most = 0.0f;

    for (int i = 0; dat != nullptr && i < REG_LIST_NUM; i++)
    {
        int          id = dat->reg_list[i];
        MB_OUT_HEAD *hp = id >= 0 ? RegDatGetTopAddr(id) : nullptr;

        if (hp == nullptr)
        {
            continue;
        }
        MB_OUT_RECT *rec = RegDatGetVecPtr(id, RECORD_TYPE_ROOM_OUTLINE);
        int          num = RegDatGetVecNum(id, RECORD_TYPE_ROOM_OUTLINE);
        for (int j = 0; rec != nullptr && j < num; j++, rec = RegDatNextVec(rec))
        {
            if (best == nullptr || RectArea(rec) > most)
            {
                best = rec;
                most = RectArea(rec);
                *kai = hp->kai;
            }
        }
    }
    if (best != nullptr)
    {
        RectCentre(best, pos);
    }
    return best != nullptr;
}

// The area's map hit object on floor kai (what RcbGetFloor answers), -1.
int MhOfFloor(int area, int kai)
{
    void *hit = MapLoadGetAreaData(area, 3);

    for (int mh = 0; hit != nullptr && mh < 16; mh++)
    {
        if (RcbGetFloor(hit, mh) == kai)
        {
            return mh;
        }
    }
    return -1;
}

// The player into the room as PLYR_FLOOR_CHANGE / PLYR_POS_SET /
// PLYR_ROT_SET do. False (and the status says why) when she was not put
// where asked.
bool PlacePlayer(const WarpParams &p)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR ground;
    sceVu0FVECTOR rot;
    float         deg;
    int           mh  = p.mh;
    int           kai = -1;
    bool          ok  = true;

    if (p.spawn == SPAWN_EV_LABEL || (p.spawn == SPAWN_ROOM && p.room_x == REIPAN_WARP_ROOM_CENTRE))
    {
        bool found = p.spawn == SPAWN_EV_LABEL && EvLabelCentre(p.ev_label, pos, &kai);

        if (!found && !RoomCentre(MapAreaNoGet(), pos, &kai))
        {
            SetStatus("Area " + std::to_string(MapAreaNoGet()) + " has no registration data to stand on.");
            return false;
        }
        if (!found && p.spawn == SPAWN_EV_LABEL)
        {
            SetStatus("No rectangle " + std::to_string(p.ev_label) + " in area " + std::to_string(MapAreaNoGet())
                      + "; standing in the middle of the room.");
            ok = false;
        }
        mh     = MhOfFloor(MapAreaNoGet(), kai);
        pos[1] = plyr->pos[1];
        deg    = (float)p.rot;
    }
    else if (p.spawn == SPAWN_ROOM)
    {
        float *org = MapLoadGetOffset(MapLoadGetBuffID(MapAreaNoGet()));

        pos[0] = (float)p.room_x + (org != nullptr ? org[0] : 0.0f);
        pos[1] = plyr->pos[1];
        pos[2] = (float)p.room_z + (org != nullptr ? org[2] : 0.0f);
        deg    = (float)p.rot;
    }
    else if (p.spawn == SPAWN_CAT_LABEL)
    {
        const CatLabelPlace &l = kCatLabel[std::clamp(p.cat_label, 0, CAT_LABEL_NUM - 1)];
        float                r = l.rot * 3.14159265f / 180.0f;

        // A step in front of the label, facing it.
        pos[0] = l.pos[0] + 25.0f * std::sin(r);
        pos[1] = l.pos[1];
        pos[2] = l.pos[2] + 25.0f * std::cos(r);
        deg    = l.rot + 180.0f;
    }
    else
    {
        return true;
    }
    if (p.look && p.spawn != SPAWN_CAT_LABEL)
    {
        // Turned to the point, as facing deg walks along (sin, cos).
        float *org = MapLoadGetOffset(MapLoadGetBuffID(MapAreaNoGet()));
        float  dx  = (float)p.look_x + (org != nullptr ? org[0] : 0.0f) - pos[0];
        float  dz  = (float)p.look_z + (org != nullptr ? org[2] : 0.0f) - pos[2];

        deg = std::atan2(dx, dz) * 180.0f / 3.14159265f;
    }
    if (mh >= 0)
    {
        MapAreaFloorChange(mh);
    }
    pos[3] = 0.0f;
    if (MapAreaGetHeight(ground, pos, MapAreaNoGet(), 0) < 0)
    {
        std::memcpy(ground, pos, sizeof(ground));
    }
    PlyrSetPos(ground);
    if ((plyr->flags & PLYR_FLG_1000000) != 0)
    {
        plyr->flags &= ~(u_long)PLYR_FLG_1000000;
    }
    rot[0] = plyr->rot[0];
    rot[1] = (int)deg == REIPAN_WARP_KEEP_ROT ? plyr->rot[1] : EvGetRot360((short)deg);
    rot[2] = plyr->rot[2];
    rot[3] = 1.0f;
    PlyrSetRot(rot);
    SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
    PlyrMdlMotReset();
    if (p.spawn == SPAWN_EV_LABEL)
    {
        ReiPan_LogPrintf("[warp] on rect %d: kai %d (mh %d, floor now %d) at (%.1f %.1f %.1f), facing %.0f\n",
                         p.ev_label, kai, mh, plyr->floor, ground[0], ground[1], ground[2], deg);
    }
    return ok;
}

void RunJob()
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();
    GPHASE_NODE *leaf = Leaf();

    if (g_job.kind == JOB_NONE || leaf == nullptr)
    {
        return;
    }
    g_job.frames++;

    switch (g_job.stage)
    {
    case STAGE_LEAVE:
        if (StackHas(PHASE_STORY_MAIN))
        {
            if (InPlay())
            {
                SndAllBufStop();
                GPhaseCtrlRequest(PHASE_TITLE_MODE, 0);
                g_job.stage = STAGE_QUITTING;
                SetStatus("Leaving play for the title (" + g_job.what + ")...");
            }
            else
            {
                SetStatus("Waiting for play to resume (close menus, let the scene end)...");
            }
            return;
        }
        if ((leaf->id == PHASE_TITLE_TOP || leaf->id == PHASE_TITLE_MENU || leaf->id == PHASE_TITLE_NEWGAME)
            && leaf->state == GPHASE_STATE_RUN && ctrl->next == -1)
        {
            StartFromTitle();
            g_job.stage = STAGE_STARTING;
            SetStatus("Starting the game (" + g_job.what + ")...");
            return;
        }
        SetStatus("Waiting for the title screen (" + g_job.what + ")...");
        return;

    case STAGE_QUITTING:
        if (!StackHas(PHASE_STORY_MAIN))
        {
            g_job.stage = STAGE_LEAVE;
        }
        return;

    case STAGE_STARTING:
        if (leaf->id != PHASE_STORY_NOWLOADING || leaf->state != GPHASE_STATE_RUN)
        {
            return;
        }
        if (leaf->state_cnt > 1)
        {
            g_job.kind = JOB_NONE;
            SetStatus("Missed Story_NowLoading's start; " + g_job.what + " not applied.");
            return;
        }
        if (!IsLoadEndAll())
        {
            return;
        }
        if (g_job.kind == JOB_WARP)
        {
            ApplyWarp(g_job.warp);
            if (g_job.warp.settle != nullptr)
            {
                g_job.warp.settle();
            }
        }
        else if (!ApplyLoad())
        {
            g_job.kind = JOB_NONE;
            SetStatus(g_job.what + ": saved by a build with other save blocks; this is a new game instead.");
            return;
        }
        g_job.stage = STAGE_APPLIED;
        return;

    case STAGE_APPLIED:
        // State 3: every load waited for, play starts once the layout leaves.
        if (leaf->id == PHASE_STORY_NOWLOADING && leaf->state_cnt == 3)
        {
            if (g_job.kind == JOB_WARP)
            {
                EnterArea(0);                   // as AREA_CHANGE[area, 0]; a start event may move her on
                PlacePlayer(g_job.warp);
                EventSetMode(g_job.event_mode);
                if (g_job.warp.stop_events || g_job.warp.fade_in)
                {
                    // The screen starts covered (FadeCtrlInit) and only an
                    // event's FADE_IN uncovers it.
                    FadeInReq(0, 0, 0, 30);
                }
            }
            else if (g_job.kind == JOB_MOVE)
            {
                EnterArea(0);
                if (!PlacePlayer(g_job.warp))
                {
                    g_job.kind = JOB_NONE;      // the status says where she is instead
                    return;
                }
            }
            else
            {
                // Where the snapshot had her (block 28's part, again after the setup).
                EnterArea(g_job.extra.mh_no);
                PlyrSetPos(g_job.extra.pos);
                PlyrSetRot(g_job.extra.rot);
                SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
                PlyrMdlMotReset();
                g_cat_log_frames = 100;
            }
            char done[160];
            std::snprintf(done, sizeof(done), "Done: %s. Now area %d, side %d, world %d, chapter %d.",
                          g_job.what.c_str(), MapAreaNoGet(), MapAreaSideGet(), ingame_wrk.b03, ingame_wrk.b01);
            g_job.kind = JOB_NONE;
            SetStatus(done);
            if (g_job.warp.then_save)
            {
                g_save_after = 300;
            }
            if (g_job.warp.then_fill != 0)
            {
                g_fill_after = 60;
            }
            if (g_job.warp.then_phase >= 0)
            {
                g_phase_after = g_job.warp.phase_wait;
                g_phase_id    = g_job.warp.then_phase;
            }
            if (g_job.warp.then_puzzle >= 0)
            {
                g_puzzle       = g_job.warp;
                g_puzzle_after = 150;
            }
            if (g_job.warp.then_clear >= 0)
            {
                g_clear_ending = g_job.warp.then_clear;
                g_clear_after  = 150;
            }
            if (g_job.warp.then_sky)
            {
                g_sky_mode  = g_job.warp.then_sky;
                g_sky_after = 150;
            }
            if (g_job.warp.then_label >= 0)
            {
                // Past the chapter's start event (fade, wake-up motion): 6 s of play.
                WarpParams tp = g_job.warp;
                tp.spawn      = SPAWN_CAT_LABEL;
                tp.cat_label  = std::clamp(tp.then_label, 0, CAT_LABEL_NUM - 1);
                tp.area       = kCatLabel[tp.cat_label].area;
                g_tp          = Teleport{ true, 0, 150, tp };
            }
        }
        else if (leaf->id != PHASE_STORY_NOWLOADING && !StackHas(PHASE_STORY_NOWLOADING))
        {
            g_job.kind = JOB_NONE;
            SetStatus("Story_NowLoading ended early; " + g_job.what + " is incomplete.");
        }
        return;
    }
}

// The cat's work in one log line (scripted runs have no window to read).
void LogCat(const char *when)
{
    const CAT_WRK *c = CatGetWrk(0);
    const float   *r = ingame_wrk.plyr_wrk != nullptr ? ingame_wrk.plyr_wrk->pos : nullptr;

    ReiPan_LogPrintf("[warp] cat %s: active %d loaded %d visible %d run_act %d act %d label %d area %d "
                     "pos (%.1f %.1f %.1f) mot %d/%d stay %d pet %d door %d | Rei area %d pos (%.1f %.1f %.1f) "
                     "| phase %d fade %d/%d draw skip 0x%x\n",
                     when, c->active, c->loaded, c->visible, c->run_act, c->act, c->label, c->area, c->pos[0],
                     c->pos[1], c->pos[2], c->mot, c->mot2, c->stay_stat, c->pet_step, c->door_stat, MapAreaNoGet(),
                     r ? r[0] : 0.0f, r ? r[1] : 0.0f, r ? r[2] : 0.0f, Leaf() != nullptr ? Leaf()->id : -1,
                     fade_ctrl.fade_state, fade_ctrl.alpha, IngameGetDrawFlags());
}

void RunTeleport()
{
    WarpParams &p = g_tp.p;

    if (g_cat_log_frames > 0 && --g_cat_log_frames == 0 && StackHas(PHASE_STORY_MAIN))
    {
        LogCat("4 s later");
    }

    if (!g_tp.active)
    {
        return;
    }
    if (!StackHas(PHASE_STORY_MAIN) || g_job.kind != JOB_NONE)
    {
        g_tp.active = false;                    // the game ended or restarts
        return;
    }
    switch (g_tp.stage)
    {
    case 0:
        if (InPlay() && --g_tp.wait <= 0)
        {
            g_tp.stage = 1;
        }
        return;

    case 1:
        if (!InPlay())
        {
            return;
        }
        if (p.area == MapAreaNoGet())
        {
            g_tp.stage = 3;
            return;
        }
        MapAreaMoveReq(p.area, 0);
        g_tp.stage = 2;
        SetStatus("Teleport: loading area " + std::to_string(p.area) + "...");
        return;

    case 2:
        if (MapAreaChange(p.area, 0) != 0)
        {
            return;
        }
        PhotoObjAreaOn(p.area);
        PhotoObjAreaOff(MapLoadGetRoomArea(MapLoadGetBuffID(p.area) ^ 1));
        RandGhostAreaChange((short)p.area);
        MapLoadSetDrawFlg(2, 2);
        MapAreaFloorChange(0);
        g_tp.stage = 3;
        return;

    case 3:
        g_tp.active = false;
        g_tp.placed = PlacePlayer(p);
        if (g_tp.placed)
        {
            SetStatus("Teleported to area " + std::to_string(MapAreaNoGet()) + ".");
        }
        LogCat("at the teleport");
        g_cat_log_frames = 100;
        return;
    }
}

WarpParams ChapterParams(int chapter);

// REIPAN_WARP="chapter=2,area=47,world=1,side=0,player=0,db=0,stop=0,cat=0,tp=0"
// (chapter alone: that chapter's start area and world; cat=N: beside cat
// label N; x=..,z=..[,rot=..]: room position; neither: the game's own start;
// tp=N: then teleport beside cat label N) and REIPAN_SNAPSHOT=<file or name
// in the snapshot folder>: requested once at boot, carried out when the
// title is up.
void ReadEnvironment()
{
    const char *warp = std::getenv("REIPAN_WARP");
    const char *snap = std::getenv("REIPAN_SNAPSHOT");

    if (warp != nullptr && *warp != '\0')
    {
        std::string spec = warp;
        size_t      at   = spec.find("chapter=");
        WarpParams  p    = ChapterParams(at != std::string::npos ? std::atoi(spec.c_str() + at + 8) : 0);
        bool        room  = false;
        bool        warp_keys = false;  // anything but cleared=: start a game
        size_t      start = 0;

        while (start <= spec.size())
        {
            size_t      end  = spec.find(',', start);
            std::string item = spec.substr(start, end == std::string::npos ? std::string::npos : end - start);
            size_t      eq   = item.find('=');
            if (eq != std::string::npos)
            {
                std::string key = item.substr(0, eq);
                int         v   = std::atoi(item.c_str() + eq + 1);

                if (key == "area")         p.area = v;
                else if (key == "side")    p.side = v;
                else if (key == "world")   p.world = v;
                else if (key == "chapter") p.chapter = v;
                else if (key == "player")  p.player = v;
                else if (key == "db")      p.debug_events = v;
                else if (key == "stop")    p.stop_events = v;
                else if (key == "cat")     { p.spawn = SPAWN_CAT_LABEL; p.cat_label = v; p.area = kCatLabel[std::clamp(v, 0, CAT_LABEL_NUM - 1)].area; }
                else if (key == "x")       { p.room_x = v; room = true; }
                else if (key == "z")       { p.room_z = v; room = true; }
                else if (key == "rot")     p.rot = v;
                else if (key == "tp")      p.then_label = v;
                else if (key == "snap")    p.then_save = v;
                else if (key == "puzzle")  p.then_puzzle = v;
                else if (key == "pidx")    p.puzzle_idx = v;
                else if (key == "item")    p.then_item = v;
                else if (key == "fill")    p.then_fill = v;
                else if (key == "phase")   p.then_phase = v;
                else if (key == "phase_wait") p.phase_wait = std::max(1, v);
                else if (key == "clear")   p.then_clear = v;
                else if (key == "sky")     p.then_sky = v;
                else if (key == "cleared") g_cleared = std::clamp(v, 0, 3);
                warp_keys |= key != "cleared";
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        if (room)
        {
            p.spawn = SPAWN_ROOM;
        }
        if (warp_keys)
        {
            RequestWarp(p);
        }
    }
    if (snap != nullptr && *snap != '\0' && (warp == nullptr || *warp == '\0' || g_job.kind == JOB_NONE))
    {
        SnapEntry   e;
        std::string name = snap;


        if (!std::filesystem::exists(name))
        {
            name = UserPath(std::string(kSnapDir) + "/" + name, false).string();
        }
        e.path  = name;
        e.label = std::filesystem::path(name).filename().string();
        RequestLoad(e);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The window
// ──────────────────────────────────────────────────────────────────────

WarpParams g_ui;                        // set to chapter 2 (the house) on first use
bool       g_ui_set;
int        g_event_id = 2;
int        g_cat_label;
std::string g_delete_armed;

const char *EventStateName(int state)
{
    static const char *const kNames[] = {
        "waiting to open", "init", "running", "waiting to end", "suspended", "ended", "locked",
    };
    return state >= 0 && state < 7 ? kNames[state] : "?";
}

void Command(std::function<void()> fn)
{
    g_commands.push_back(std::move(fn));
}

void DrawGameLine()
{
    const GPHASE_NODE *leaf = Leaf();

    ImGui::Text(UiText("Phase %d%s"), leaf != nullptr ? leaf->id : -1, InPlay() ? UiText(" (play)") : "");
    if (StackHas(PHASE_STORY_MAIN) && ingame_wrk.plyr_wrk != nullptr)
    {
        const float *pos = ingame_wrk.plyr_wrk->pos;

        ImGui::SameLine();
        ImGui::Text(UiText("| area %d side %d | %s | chapter %d (title %d) | player %d"),
                    MapAreaNoGet(), MapAreaSideGet(), ingame_wrk.b03 == 1 ? "real" : "dream",
                    ingame_wrk.b01, (s_char)ingame_wrk.b02, PlyrGetCurNo());
        ImGui::Text(UiText("Rei at (%.1f, %.1f, %.1f)"), pos[0], pos[1], pos[2]);
        float *org = MapLoadGetOffset(MapLoadGetBuffID(MapAreaNoGet()));
        if (org != nullptr)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(UiText("room x/z %.0f, %.0f"), pos[0] - org[0], pos[2] - org[2]);
        }
    }
}

// Where each chapter starts (EVENT_OBJ, decoded 2026-10-19): the chapter
// events under event 88 open on EV_NOW_CHAPTER (ingame_wrk.b01), and each
// one's first sub-event is a chapter-select entry that normal play retires
// (149 / 322 mark the others ended); a game that starts in that chapter
// runs it. Area and world are where the warp loads before it runs.
struct ChapterStart
{
    short       entry;              // the chapter's start event
    short       area;
    short       world;
    const char *where;
};
const ChapterStart kChapterStart[kChapterMax + 1] =
{
    {  149,  1, 0, "the prologue, a normal new game (151: area 6, 153: the opening movie)" },
    {  322,  1, 0, "the manor (330: the dream and the chapter title)" },
    {  438, 47, 1, "the house: 438 wakes Rei in her bedroom" },
    {   91, 47, 1, "the house: 91, Rei's bedroom" },
    {  184, 47, 1, "the house (the bed leads to the dream)" },
    {  245, 47, 1, "the house (245: the cat, the memo, fade in)" },
    {  489, 47, 1, "the house: 489, then 493 to the dream (area 13)" },
    {  566, 47, 1, "the house (587: the dream, area 16)" },
    {  647, 47, 1, "the house (the bed leads to the dream)" },
    {  708, 47, 1, "the house: 708, Rei's bedroom" },
    {  748, 47, 1, "the house (the bed leads to the dream)" },
    {  821, 47, 1, "the house (the bed leads to the dream)" },
    {  897, 47, 1, "the house (the bed leads to the dream)" },
    {  961, 47, 1, "the house: 961, Rei's bedroom" },
    { 1033, 41, 0, "the manor: 1033 (area 41)" },
};

WarpParams ChapterParams(int chapter)
{
    const ChapterStart &c = kChapterStart[std::clamp(chapter, 0, kChapterMax)];
    WarpParams          p;

    p.chapter = chapter;
    p.area    = c.area;
    p.world   = c.world;
    p.spawn   = SPAWN_GAME;             // the start event places Rei
    return p;
}

void DrawAreaCombo(const char *label, int *area, int *world)
{
    std::string current = AreaName(*area);

    if (ImGui::BeginCombo(label, current.c_str()))
    {
        for (int i = 0; i < kAreaMax; i++)
        {
            int a = (kHouseFirst - 1 + i) % kAreaMax + 1;     // the house first, then the manor

            if (ImGui::Selectable(AreaName(a).c_str(), a == *area))
            {
                *area = a;
                if (world != nullptr)
                {
                    *world = a >= kHouseFirst && a <= 52;
                }
            }
        }
        ImGui::EndCombo();
    }
}

void DrawSpawn(WarpParams &p, bool allow_game)
{
    if (allow_game)
    {
        ImGui::RadioButton(UiText("Where the game puts her"), &p.spawn, SPAWN_GAME);
        ImGui::SameLine();
    }
    ImGui::RadioButton(UiText("Room x / z"), &p.spawn, SPAWN_ROOM);
    ImGui::SameLine();
    ImGui::RadioButton(UiText("Beside a cat label"), &p.spawn, SPAWN_CAT_LABEL);
    if (p.spawn == SPAWN_ROOM)
    {
        ImGui::DragInt("x", &p.room_x, 1.0f, -2000, 2000);
        ImGui::DragInt("z", &p.room_z, 1.0f, -2000, 2000);
        ImGui::DragInt("facing", &p.rot, 1.0f, -180, 360);
    }
    else if (p.spawn == SPAWN_CAT_LABEL)
    {
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt(UiText("Cat label"), &p.cat_label, 0, CAT_LABEL_NUM - 1))
        {
            p.area  = kCatLabel[p.cat_label].area;
            p.world = 1;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(UiText("area %d"), kCatLabel[p.cat_label].area);
    }
}

void DrawWarp()
{
    if (!ImGui::CollapsingHeader(UiText("Warp: a new game from a chapter"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt(UiText("Chapter"), &g_ui.chapter, 0, kChapterMax))
    {
        WarpParams p   = ChapterParams(g_ui.chapter);
        p.player       = g_ui.player;
        p.debug_events = g_ui.debug_events;
        p.stop_events  = g_ui.stop_events;
        p.then_label   = g_ui.then_label;
        g_ui           = p;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(UiText("ingame_wrk.b01 at the start. The chapter's event (under event 88)\n"
                          "opens and runs its start event, the developers' chapter select:\n"
                          "it moves the world, the area and Rei as the story would."));
    }
    const ChapterStart &cs = kChapterStart[std::clamp(g_ui.chapter, 0, kChapterMax)];
    ImGui::TextDisabled(UiText("start event %d: %s"), cs.entry, cs.where);

    if (ImGui::TreeNode(UiText("Load first into (the start event may move her on)")))
    {
        DrawAreaCombo(UiText("Area"), &g_ui.area, &g_ui.world);
        ImGui::RadioButton(UiText("Dream (manor)"), &g_ui.world, 0);
        ImGui::SameLine();
        ImGui::RadioButton(UiText("Real world (house)"), &g_ui.world, 1);
        ImGui::RadioButton(UiText("Day"), &g_ui.side, 0);
        ImGui::SameLine();
        ImGui::RadioButton(UiText("Night"), &g_ui.side, 1);
        ImGui::SameLine();
        ImGui::TextDisabled(UiText("(map side)"));
        ImGui::RadioButton(UiText("Rei"), &g_ui.player, 0);
        ImGui::SameLine();
        ImGui::RadioButton(UiText("Miku"), &g_ui.player, 1);
        ImGui::SameLine();
        ImGui::RadioButton(UiText("Kei"), &g_ui.player, 2);
        DrawSpawn(g_ui, true);
        ImGui::TreePop();
    }

    bool then_cat = g_ui.then_label >= 0;
    if (ImGui::Checkbox(UiText("Then teleport beside the cat's label"), &then_cat))
    {
        g_ui.then_label = then_cat ? 0 : -1;
    }
    if (then_cat)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("##then", &g_ui.then_label, 0, CAT_LABEL_NUM - 1);
    }

    bool db   = g_ui.debug_events != 0;
    bool stop = g_ui.stop_events != 0;
    if (ImGui::Checkbox(UiText("FF3's debug event file"), &db))
    {
        g_ui.debug_events = db;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(UiText("EVENT_DB_OBJ instead of EVENT_OBJ: the developers' debug start\n"
                          "event 1746 opens instead of 1060 (fade in, control, camera\n"
                          "unlocked, random ghosts on). Everything else is the same file."));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(UiText("Stop events"), &stop))
    {
        g_ui.stop_events = stop;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(UiText("EventSetStop(1): no event program or condition runs (NPCs, the\n"
                          "cat and the map still do; the cat stays off unless switched on\n"
                          "below, since event 5 does it). Use Events below to run one."));
    }

    if (ImGui::Button(g_job.kind == JOB_WARP ? UiText("Warping...") : UiText("Start here")))
    {
        RequestWarp(g_ui);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(UiText("(through the title, as New Game; a running game is left first)"));
}

void DrawTeleport()
{
    static WarpParams tp = [] {
        WarpParams p;
        p.area      = 48;
        p.spawn     = SPAWN_CAT_LABEL;
        p.cat_label = 0;
        return p;
    }();

    if (!ImGui::CollapsingHeader(UiText("Teleport (in play)"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    DrawAreaCombo(UiText("To area"), &tp.area, nullptr);
    DrawSpawn(tp, false);
    ImGui::BeginDisabled(!InPlay() || g_tp.active);
    if (ImGui::Button(UiText("Teleport")))
    {
        WarpParams p = tp;
        if (p.spawn == SPAWN_CAT_LABEL)
        {
            p.area = kCatLabel[p.cat_label].area;
        }
        Command([p]() { g_tp = Teleport{ true, 1, 0, p }; });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(UiText("(the room loads as AREA_CHANGE; events keep running)"));
}

void DrawSnapshots()
{
    if (!ImGui::CollapsingHeader(UiText("Snapshots"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (g_list_dirty)
    {
        RefreshList();
    }

    ImGui::BeginDisabled(!InPlay());
    if (ImGui::Button(UiText("Save snapshot now")))
    {
        g_save_request = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(UiText("Refresh")))
    {
        g_list_dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(UiText("(user folder/%s)"), kSnapDir);

    if (g_snaps.empty())
    {
        ImGui::TextDisabled(UiText("No snapshots yet."));
        return;
    }
    if (!ImGui::BeginTable("snaps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }
    for (size_t i = 0; i < g_snaps.size(); i++)
    {
        const SnapEntry &e = g_snaps[i];
        char             info[96];

        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e.label.c_str());
        ImGui::TableNextColumn();
        std::snprintf(info, sizeof(info), "area %d %s, chapter %d, %02d:%02d:%02d", e.head.area,
                      e.head.world == 1 ? "real" : "dream", e.head.chapter, e.head.hour, e.head.min, e.head.sec);
        ImGui::TextUnformatted(e.ok ? info : UiText("another build's layout"));
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!e.ok || g_job.kind != JOB_NONE);
        if (ImGui::SmallButton(UiText("Load")))
        {
            SnapEntry copy = e;
            Command([copy]() { RequestLoad(copy); });
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        {
            bool armed = g_delete_armed == e.path;
            if (ImGui::SmallButton(armed ? UiText("Sure?") : UiText("Delete")))
            {
                if (armed)
                {
                    std::error_code ec;
                    std::filesystem::remove(e.path, ec);
                    g_delete_armed.clear();
                    g_list_dirty = true;
                }
                else
                {
                    g_delete_armed = e.path;
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DrawEvents()
{
    if (!ImGui::CollapsingHeader(UiText("Events")))
    {
        return;
    }
    ImGui::SetNextItemWidth(120);
    ImGui::DragInt(UiText("Event"), &g_event_id, 1.0f, 0, EV_EVENT_NUM - 1);
    g_event_id = std::clamp(g_event_id, 0, EV_EVENT_NUM - 1);
    if (!StackHas(PHASE_STORY_MAIN))
    {
        ImGui::TextDisabled(UiText("(in game only)"));
        return;
    }
    int state = GetEvState(g_event_id);
    ImGui::SameLine();
    ImGui::Text(UiText("%s, ran %d time(s)"), EventStateName(state), GetEvExeCount(g_event_id));
    ImGui::BeginDisabled(!InPlay());
    if (ImGui::Button(UiText("Open (post its conditions)")))
    {
        int id = g_event_id;
        Command([id]() {
            SetEventState(id, EV_STATE_WAIT_OPEN);
            EventSetOpenCondition(id);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("Start now")))
    {
        int id = g_event_id;
        Command([id]() { SetEventInitStatus(id); });
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("Mark ended")))
    {
        int id = g_event_id;
        Command([id]() { SetEventState(id, EV_STATE_END); });
    }
    ImGui::EndDisabled();
    bool stopped = false;
    {
        // EventSetStop has no getter; the window keeps the last value it set.
        static bool stop_ui;
        stopped = stop_ui;
        if (ImGui::Checkbox(UiText("Stop events now"), &stopped))
        {
            stop_ui = stopped;
            int on  = stopped;
            Command([on]() { EventSetStop(on); });
        }
    }
}

void DrawGameClear()
{
    if (!ImGui::CollapsingHeader(UiText("Game clear"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    static const char *const kDifficulty[] = {UiText("Easy"), UiText("Normal"), UiText("Hard"), UiText("Nightmare")};
    static int               difficulty    = 1;
    static int               ending        = 0;
    bool                     cleared       = g_cleared >= 0;

    if (ImGui::Checkbox(UiText("Treat the game as cleared"), &cleared))
    {
        g_cleared = cleared ? difficulty : -1;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo(UiText("cleared on"), &difficulty, kDifficulty, IM_ARRAYSIZE(kDifficulty)) && g_cleared >= 0)
    {
        g_cleared = difficulty;
    }
    ImGui::TextDisabled(UiText("New Game then opens the start menu (extra mode, missions, gallery, costumes);\n"
                        "Hard (and Nightmare after a Hard clear) on the difficulty select. Kept until unticked."));
    ImGui::SetNextItemWidth(120);
    ImGui::Combo(UiText("ending"), &ending, "first\0other\0");
    ImGui::SameLine();
    ImGui::BeginDisabled(!InPlay());
    if (ImGui::Button(UiText("Play the game clear now")))
    {
        int e = ending;
        Command([e]() {
            int         args[1] = {e};
            EV_EXE_CTRL ctrl    = {};
            ctrl.args           = args;
            EvGameClear(&ctrl);
        });
    }
    ImGui::EndDisabled();
}

void DrawCat()
{
    if (!ImGui::CollapsingHeader(UiText("Cat"), ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (!StackHas(PHASE_STORY_MAIN))
    {
        ImGui::TextDisabled(UiText("(in game only)"));
        return;
    }
    const CAT_WRK *c = CatGetWrk(0);

    ImGui::Text(UiText("active %d  loaded %d  visible %d  run_act %d  type %d  acs %d"), c->active, c->loaded,
                c->visible, c->run_act, c->type, c->acs_no);
    ImGui::Text(UiText("models: body %s, part %s, accessory %s"), c->mdl ? "yes" : "no", c->mdl2 ? "yes" : "no",
                c->acs ? "yes" : "no");
    ImGui::Text(UiText("area %d  pos (%.1f, %.1f, %.1f)  facing %.2f"), c->area, c->pos[0], c->pos[1], c->pos[2],
                c->rot[1]);
    ImGui::Text(UiText("act %d  label %d  step %d  move %d/%d  sub %d/%d  next %d"), c->act, c->label, c->step, c->move,
                c->move_stat, c->sub, c->sub_stat, c->next_label);
    ImGui::Text(UiText("stay %d  idle %d step %d left %d  pet %d->%d  look %d eye %d  door %d"), c->stay_stat, c->idle,
                c->idle_step, c->idle_cnt, c->pet_step, c->pet_next, c->look_step, c->eye, c->door_stat);
    ImGui::Text(UiText("motion %d / %d  fixed %d  locks %d%d%d%d%d%d"), c->mot, c->mot2, c->fix_pos, c->lock[0] != 0,
                c->lock[1] != 0, c->lock[2] != 0, c->lock[3] != 0, c->lock[4] != 0, c->lock[5] != 0);

    ImGui::BeginDisabled(!InPlay());
    if (ImGui::Button(c->active ? UiText("Deactivate") : UiText("Activate")))
    {
        int on = c->active == 0;
        Command([on]() { CatSetActive(0, on); });
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("Create model")))
    {
        Command([]() { CatMdlCreate(0, -1, 1); });
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("Restart acts")))
    {
        Command([]() { CatActRestart(0); });
    }
    ImGui::SameLine();
    if (ImGui::Button(UiText("Bring to Rei")))
    {
        Command([]() {
            PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
            sceVu0FVECTOR pos;

            std::memcpy(pos, plyr->pos, sizeof(pos));
            pos[0] += 25.0f * std::sin(plyr->rot[1]);
            pos[2] += 25.0f * std::cos(plyr->rot[1]);
            CatSetPos(0, MapAreaNoGet(), pos, plyr->rot[1]);
        });
    }
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("##label", &g_cat_label, 0, CAT_LABEL_NUM - 1);
    ImGui::SameLine();
    if (ImGui::Button(UiText("Put at label")))
    {
        int label = g_cat_label;
        Command([label]() { CatSetPosLabel(0, (s_char)label); });
    }
    ImGui::EndDisabled();
}

} // namespace

// ──────────────────────────────────────────────────────────────────────
// Game cleared: what GAME_CLEAR_ (EvGameClear) leaves behind, without the
// sequence -- the clear flag, one clear at g_cleared's difficulty and its
// unlocks (FINDER_STATE_WRK::Clear: Hard on the difficulty select, the
// shop slots; Nightmare after a Hard clear). Story_Start then opens the
// start menu (extra mode, missions, gallery, costumes) as for a cleared
// save. IngameInit (init_Story_Start) resets the clear counts, so the
// mark is put back whenever they are empty at the title or in
// Story_Start; step 1 of one_Story_Start reads them a frame later.
// ──────────────────────────────────────────────────────────────────────

void MarkCleared()
{
    FINDER_STATE_WRK *wrk = GetFinderStateWrk();

    wrk->_04 = 1;
    wrk->Clear(g_cleared);
    wrk->SetClearSaveFlag(1);
}

void KeepCleared()
{
    if (g_cleared < 0 || FinderStateHasAny())
    {
        return;
    }
    const GPHASE_NODE *leaf = Leaf();
    if (leaf != nullptr && (leaf->id == PHASE_STORY_START || StackHas(PHASE_TITLE_MODE)))
    {
        MarkCleared();
        ReiPan_LogPrintf("[warp] game marked cleared (difficulty %d)\n", g_cleared);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Entry points
// ──────────────────────────────────────────────────────────────────────

static void RunSceneSmoke()
{
    static const char *range = std::getenv("REIPAN_SCENE_SMOKE");
    if (range == nullptr)
        return;
    static int next = -1;
    static int last = -1;
    static int active = -1;
    static int movie_tick_limit = 90;
    static bool entered;
    static bool stopped;
    if (next < 0)
    {
        next = 0;
        last = SCENE_NUM - 1;
        // Optional third field 0 leaves the movie playing to its own end.
        if (std::sscanf(range, "%d:%d:%d", &next, &last, &movie_tick_limit) < 1)
            next = 0;
        next = std::clamp(next, 0, SCENE_NUM);
        last = std::clamp(last, next < SCENE_NUM ? next : SCENE_NUM - 1, SCENE_NUM - 1);
    }
    if (active >= 0)
    {
        const GPHASE_NODE *leaf = Leaf();
        if (leaf != nullptr && (leaf->id == 89 || leaf->id == 90) &&
            leaf->state == GPHASE_STATE_RUN)
        {
            if (!entered)
                ReiPan_LogPrintf("[scene regression] scene %d entered playback phase %d\n", active, leaf->id);
            entered = true;
            if (leaf->id == 90 && movie_tick_limit > 0 &&
                MovieCountGet() >= movie_tick_limit && !stopped)
            {
                playPssStop();
                stopped = true;
            }
        }
        if (!InPlay())
            return;
        ReiPan_LogPrintf("[scene regression] scene %d returned to play, entered=%d, skipped=%d\n",
                         active, entered, stopped);
        active = -1;
    }
    if (!InPlay() || g_job.kind != JOB_NONE || next > last)
        return;
    const int scene = next++;
    const int file = GetFileNoFromSceneNo(scene);
    const u_int size = GetFileSize(file);
    if (size == 0 || size == (u_int)-1)
    {
        ReiPan_LogPrintf("[scene regression] scene %d has no playable data\n", scene);
        return;
    }
    active = scene;
    entered = stopped = false;
    ReiPan_LogPrintf("[scene regression] scene %d requested, resource %04x\n", scene, file);
    MovieReqPlay(scene, 1);
}

static void RunRoomSmoke()
{
    static const char *range = std::getenv("REIPAN_ROOM_SMOKE");
    if (range == nullptr)
        return;

    static int next = 0;
    static int last = 0;
    static int area = 0;
    static unsigned elapsed = 0;
    static unsigned settle = 45;
    if (next == 0)
    {
        if (std::sscanf(range, "%d:%d", &next, &last) != 2 ||
            next < 1 || last < next || last > 52 ||
            (next < kHouseFirst) != (last < kHouseFirst))
        {
            ReiPan_LogPrintf("[room regression] use a range within areas 1-46 or 47-52\n");
            next = kAreaMax + 1;
        }
    }
    if (area != 0)
    {
        if (g_tp.active || !InPlay())
        {
            if (++elapsed >= 450)
            {
                ReiPan_LogPrintf("[room regression] area %d timed out, teleport stage %d\n", area, g_tp.stage);
                area = 0;
                next = last + 1;
            }
            return;
        }
        if (--settle != 0)
            return;

        const auto *player = ingame_wrk.plyr_wrk;
        const bool passed = g_tp.placed && MapAreaNoGet() == area && player != nullptr &&
                            std::isfinite(player->pos[0]) && std::isfinite(player->pos[1]) &&
                            std::isfinite(player->pos[2]);
        if (player != nullptr)
            ReiPan_LogPrintf("[room regression] area %d %s, current %d, position %.2f %.2f %.2f, draw %x\n",
                             area, passed ? "loaded" : "failed", MapAreaNoGet(),
                             player->pos[0], player->pos[1], player->pos[2], IngameGetDrawFlags());
        else
            ReiPan_LogPrintf("[room regression] area %d failed, no player\n", area);
        area = 0;
    }
    if (!InPlay() || g_job.kind != JOB_NONE || g_tp.active || next > last)
        return;

    const int requested = next++;
    const u_int pack_size = GetFileSize(RoomFileGetObjPack(requested));
    if (pack_size == 0 || pack_size == (u_int)-1)
    {
        ReiPan_LogPrintf("[room regression] area %d has no object and positioning pack for this side\n", requested);
        return;
    }
    area = requested;
    elapsed = 0;
    settle = 45;
    WarpParams params = ChapterParams(ingame_wrk.b01);
    params.area = area;
    params.spawn = SPAWN_ROOM;
    params.room_x = REIPAN_WARP_ROOM_CENTRE;
    params.rot = REIPAN_WARP_KEEP_ROT;
    g_tp = Teleport{true, 1, 0, params};
    ReiPan_LogPrintf("[room regression] area %d requested\n", area);
}

void ReiPan_WarpFrame(void)
{
    if (!g_env_done)
    {
        g_env_done = true;
        ReadEnvironment();
    }

    std::vector<std::function<void()>> commands;
    commands.swap(g_commands);
    for (auto &fn : commands)
    {
        fn();
    }
    RunSceneSmoke();

    if (g_save_after > 0 && InPlay() && --g_save_after == 0)
    {
        g_save_request = true;
    }
    if (g_save_request)
    {
        g_save_request = false;
        SaveSnapshot();
    }
    if (g_fill_after > 0 && InPlay() && --g_fill_after == 0)
    {
        for (short i = 0; i < ITEM_NUM; i++)
        {
            ItemSetNum(i, 3, 0);
        }
        for (int i = 0; i < MEMO_NUM; i++)
        {
            MemoSetStatus((short)i, 1);
        }
        ReiPan_LogPrintf("[warp] the full inventory\n");
    }
    if (g_phase_after > 0 && InPlay() && --g_phase_after == 0)
    {
        if (g_phase_id == PHASE_STORY_SAVE)
            StorySaveGameReq();
        else if (g_phase_id == 46)
            CassetteMenuReq();
        else
            GPhaseCtrlRequest(g_phase_id, 0);
        ReiPan_LogPrintf("[warp] phase %d\n", g_phase_id);
    }
    if (g_puzzle_after > 0 && InPlay() && --g_puzzle_after == 0)
    {
        // As an event's PUZZLE_START (EvPuzzleStart); without its item the
        // puzzle shows phase 71's "missing item" message instead.
        if (g_puzzle.then_item >= 0)
        {
            ItemSetNum((short)g_puzzle.then_item, 1, 0);
        }
        PuzzleSet(g_puzzle.then_puzzle, g_puzzle.puzzle_idx);
        GPhaseCtrlRequest(49, 0);
        ReiPan_LogPrintf("[warp] puzzle %d board %d (item %d)\n", g_puzzle.then_puzzle, g_puzzle.puzzle_idx,
                         g_puzzle.then_item);
    }
    if (g_clear_after > 0 && InPlay() && --g_clear_after == 0)
    {
        // As an event's GAME_CLEAR_ (EvGameClear): the clear flags and count,
        // then phase 42 (Story_GameClear) at the game's difficulty.
        int          args[1] = {g_clear_ending};
        EV_EXE_CTRL  ctrl    = {};
        ctrl.args            = args;
        EvGameClear(&ctrl);
        ReiPan_LogPrintf("[warp] game clear (ending %d)\n", g_clear_ending);
    }
    if (g_sky_after > 0 && InPlay() && --g_sky_after == 0)
    {
        // As the tattoo event's dream look (TattooEvCameraMain state 4): the
        // map-sky effect -- the sky dome PssModel with its movies -- at Rei.
        // Kept until the game ends (request flag 2: nothing releases it).
        // The dome is a hemisphere of radius 84 seen from inside (the event's
        // camera is 10 units from Rei), so the map camera outside it shows
        // nothing: sky=2 keeps it on the camera instead.
        sceVu0CopyVector(g_sky_pos, g_sky_mode == 2 ? gra3dcamGetPosition() : ingame_wrk.plyr_wrk->pos);
        g_sky_pos[3] = 0.0f;            // MapSkyDataDraw adds 1 for the matrix's w
        EffMapSkyReq(g_sky_pos);
        ReiPan_LogPrintf("[warp] map sky at (%.0f, %.0f, %.0f)\n", g_sky_pos[0], g_sky_pos[1], g_sky_pos[2]);
    }
    else if (g_sky_mode == 2 && g_sky_after == 0 && InPlay())
    {
        sceVu0CopyVector(g_sky_pos, gra3dcamGetPosition());
        g_sky_pos[3] = 0.0f;
    }
    KeepCleared();
    RunJob();
    RunTeleport();
    RunRoomSmoke();
}

void ReiPan_WarpDrawWindow(bool *open)
{
    if (open == nullptr || !*open)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(UiText("Start point / snapshots"), open))
    {
        ImGui::End();
        return;
    }
    if (!g_ui_set)
    {
        g_ui_set = true;
        g_ui     = ChapterParams(2);
    }
    ImGui::TextWrapped("%s", g_status.c_str());
    DrawGameLine();
    ImGui::Separator();
    DrawWarp();
    DrawTeleport();
    DrawSnapshots();
    DrawEvents();
    DrawGameClear();
    DrawCat();
    ImGui::End();
}

// ──────────────────────────────────────────────────────────────────────
// For the event browser (reipan_events.cpp), on the game thread
// ──────────────────────────────────────────────────────────────────────

int ReiPan_WarpIdle(void)
{
    return g_job.kind == JOB_NONE && !g_tp.active;
}

int ReiPan_WarpInGame(void)
{
    return StackHas(PHASE_STORY_MAIN);
}

int ReiPan_WarpAtTitle(void)
{
    return StackHas(PHASE_TITLE_MODE);
}

int ReiPan_WarpInPlay(void)
{
    return InPlay();
}

const char *ReiPan_WarpStatus(void)
{
    return g_status.c_str();
}

int ReiPan_WarpWorldOf(int area)
{
    return area >= kHouseFirst && area <= 52;
}

void ReiPan_WarpChapter(int chapter)
{
    RequestWarp(ChapterParams(chapter));
}

// The warp's work for a spot.
static void SpotParams(WarpParams &p, const ReiPanWarpSpot *spot)
{
    p.area     = spot->area;
    p.world    = ReiPan_WarpWorldOf(spot->area);
    p.spawn    = spot->label >= 0 ? SPAWN_EV_LABEL : SPAWN_ROOM;
    p.ev_label = spot->label;
    p.mh       = spot->mh;
    p.room_x   = spot->x;
    p.room_z   = spot->z;
    p.rot      = spot->rot;
    p.look     = spot->look;
    p.look_x   = spot->look_x;
    p.look_z   = spot->look_z;
}

void ReiPan_WarpJump(int chapter, const ReiPanWarpSpot *spot, int side, void (*settle)(void))
{
    WarpParams p = ChapterParams(chapter);

    if (spot == nullptr || !ReiPan_WarpIdle())
    {
        return;
    }
    SpotParams(p, spot);
    p.side    = p.world ? side : 0;
    p.settle  = settle;
    p.fade_in = 1;
    RequestWarp(p);
}

void ReiPan_WarpGoTo(const ReiPanWarpSpot *spot)
{
    WarpParams p;

    if (spot == nullptr || !InPlay() || !ReiPan_WarpIdle())
    {
        return;
    }
    SpotParams(p, spot);
    p.side = p.world ? MapAreaSideGet() : 0;    // the dream is entered by day (SET_NIGHT_FLG 0)
    if (p.world == ingame_wrk.b03)
    {
        g_tp = Teleport{ true, 1, 0, p };
    }
    else
    {
        RequestMove(p);
    }
}
