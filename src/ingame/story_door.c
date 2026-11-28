/* ==========================================================================
 *  ingame/story_door.c
 *
 *  The area table, the current area and side, the door action check and
 *  the door modes of phase 0x22 Story_Door_Open. FF3 0x1f20b8..0x1f3780;
 *  see story_door.h.
 *
 *  In address order: the area_data.ecb buffer and lookup, area number and
 *  side, door mode queries, area changes, the door lock set, the action
 *  check (rectangles of kind 0x12), the room change behind a door, the
 *  door modes and phase 0x22's init/end/one.
 * ======================================================================== */
#include "ingame/story_door.h"
#include "common/reipan_stub.h"

#include <string.h>                                     // memcpy

#include "common/WindowMsg.h"                           // CWindowMsg, SelWin*
#include "common/heapctrl.h"                            // heapCtrlMalloc / heapCtrlFree
#include "graphics/ecb.h"                               // EcbGetNum / EcbGetData / EcbGetSize
#include "ingame/camera/event_camera.h"                 // EventCamera*, EventCameraCut
#include "ingame/cat/cat_ctrl.h"                        // CatDoorFollowStart / 431f8 / 43228
#include "ingame/event/telephone.h"                 // TelRingRaiseArea / TelRingLowerArea
#include "ingame/event/prg/ev_command.h"                // EvRotInRange
#include "ingame/event/prg/ev_chapter.h"                // EvGhostPreloadIsEnd, EvGhostAreaChange
#include "ingame/event/prg/ev_se.h"                     // ev_seIsReady, ev_seFlipToggle
#include "ingame/ingame.h"                              // GPhaseCtrlRequest, IngameMain, IngameDrawSub
#include "ingame/ingame_wrk.h"                          // ingame_wrk.plyr_wrk
#include "ingame/map/BgSe.h"                            // GetLayoutSeWrk, CBgSe::SetArea
#include "ingame/map/LightCtl.h"                        // LightCtlIsRoomDrawn, LightCtlGetLightData
#include "ingame/map/MapDBuff.h"                        // CMapDBuff, MapDBuff*
#include "ingame/map/MapDraw.h"                         // MapDrawGetLightPtr
#include "ingame/map/MapLBuff.h"                        // MapDoorVecGet*, MapDoorVecSetDisp
#include "ingame/map/MapLight.h"                        // MapDoorVecSelect / CoordSet, MapLightMakeDual / DualProc / GetDualWork
#include "ingame/map/MapLoad.h"                         // MapLoad*, MapLoadGetRoomNoNow / b6e0 / b8b8
#include "ingame/map/MapObjWrk.h"                       // MAP_OBJ_MDL
#include "ingame/map/MapPut.h"                          // MapPutGetFlag / GetMatrix / SetMatrixPtr
#include "ingame/map/MapRcb.h"                          // RcbGetFloor
#include "ingame/map/hit_check_base.h"                  // HcBaseIsInRectXZ
#include "ingame/map/map_rectangle.h"                   // MrecFindRectStat, MrecGetTestRect, MrecSetCameraSet
#include "ingame/photo/photo_obj.h"                      // PhotoObjAreaOn / Off
#include "ingame/plyr/player.h"                         // PLYR_WRK, GetPlyrAreaNo, Plyr*
#include "ingame/plyr/plyr_sub.h"                       // PlyrSet280Req, PlyrTurnFlagClear
#include "ingame/map/MapDoor.h"              // door table, door motion pack
#include "ingame/enemy/random_ghost.h"            // RandGhostAreaChange
#include "ingame/sound/snd_ctrl.h"                      // SndSePlay, SndSeBank*
#include "ingame/story_menu.h"                          // LockTblInc / Dec, IsLockTblOn
#include "system/eeiop/cd_file_dat.h"                   // AREA_DATA_ECB
#include "system/os/eecdvd.h"                           // LoadReq
#include "system/os/sys_heap.h"                         // GetSystemHeapWrkP
#include "system/pad/pad_ctrl.h"                        // CheckPadTrgPlaySe, pad_key

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────


// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

#define MAP_AREA_DATA_SIZE  0xa00                       // area_data.ecb buffer (the file is 2156 bytes)
#define MAP_DOOR_HIT_MAX    4                           // 0x3ef380..0x3ef3b0; the next file's bss follows

typedef int (*MAP_DOOR_STEP)(void);

// FF3 data 0x3299d0: the door mode of each step function. The name
// strings live at 0x35b6d0 (0x18 bytes each).
// layout: host -- pointers; only read through this file
typedef struct
{
    MAP_DOOR_STEP step;
    int           mode;
    const char   *name;
} MAP_DOOR_MODE_TBL;

static const MAP_DOOR_MODE_TBL map_door_mode_tbl[] =
{
    { MapDoorModeAft,    MAP_DOOR_MODE_AFT,   "MAP_DOOR_MODE_AFT"   },
    { MapDoorModeNow,    MAP_DOOR_MODE_NOW,   "MAP_DOOR_MODE_NOW"   },
    { MapDoorModeClose,  MAP_DOOR_MODE_CLOSE, "MAP_DOOR_MODE_CLOSE" },
    { MapDoorModeWait,   MAP_DOOR_MODE_WAIT,  "MAP_DOOR_MODE_WAIT"  },
    { MapDoorModeBfo,    MAP_DOOR_MODE_BFO,   "MAP_DOOR_MODE_BFO"   },
    { MapDoorModeAftEnd, MAP_DOOR_MODE_AFT,   "MAP_DOOR_MODE_AFT"   },
};

// FF3 data 0x35b748 (words; the low byte is the lock id): the locks held
// while a door mode runs.
static const int map_door_lock_tbl[13] = { 1, 3, 9, 0xc, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };

// The door rectangles of kind 0x12 the player stands in, counted per door
// (MapDoorHitCallback).
// layout: host -- rect is a host pointer; only used in this file
typedef struct
{
    int    id;                                          // door id
    float *rect;                                        // the larger of its rectangles (MrecGetTestRect)
    int    cnt;                                         // rectangles hit
} MAP_DOOR_HIT;

// sdata 0x370ef0.. (gp-0x2600..)
static void *map_area_data;                             // 0x370ef0: area_data.ecb
static struct
{
    int no;                                             // 0x370ef8
    int side;                                           // 0x370efc
} map_area = { -1, 0 };                                 // the 8-byte save block (MapAreaGetSaveData)
static int           map_door_phase;                    // 0x370f00: phase 0x22 runs (one_Story_Door_Open clears it)
static int           map_door_id   = -1;                // 0x370f04
static int           map_door_area = -1;                // 0x370f08: the player's area at the door action
static MAP_DOOR_STEP map_door_step;                     // 0x370f0c: the running door mode
static CMapDBuff    *map_door_obj;                      // 0x370f10: the opened door's model (none for a locked door)
static int           map_door_hit_num;                  // 0x370f14

// bss 0x3ef330..
static sceVu0FVECTOR map_door_rect[4];                  // 0x3ef330: the taken door's rectangle (MapDoorModeAft)
static sceVu0FVECTOR map_door_plyr_pos;                 // 0x3ef370: the player's pos when the door was taken (MapDoorModeClose puts it back)
static MAP_DOOR_HIT  map_door_hit[MAP_DOOR_HIT_MAX];    // 0x3ef380

// sbss gp-0x28 (0x3734c8): the locked-door message window.
static CWindowMsg   *map_door_win;

// sbss gp-0x30 (0x3734c0, a common symbol): the player's mh per room while
// a door mode runs (written by init_Story_Door_Open, 0x1f34f4).
int map_door_room_mh[2];

// ──────────────────────────────────────────────────────────────────────
// The save block: the whole table buffer (events edit its rows).
// FF3 0x001f20b8.

int MapAreaDataGetSaveData(void **out)
{
    *out = map_area_data;
    return MAP_AREA_DATA_SIZE;
}

// ──────────────────────────────────────────────────────────────────────
// The last row of the area table for area; nullptr without the table or
// with a table of another row size.
// FF3 0x001f20c8.

MAP_AREA_DAT *MapAreaDataGet(s_char area)
{
    MAP_AREA_DAT *found = nullptr;

    if (map_area_data != nullptr && EcbGetSize(map_area_data) == sizeof(MAP_AREA_DAT))
    {
        int num = EcbGetNum(map_area_data);

        for (int i = 0; i < num; i++)
        {
            MAP_AREA_DAT *row = (MAP_AREA_DAT *)EcbGetData(map_area_data, i);

            if (row->area == area)
            {
                found = row;
            }
        }
    }
    return found;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2160 (IngameInit).

void MapAreaDataAlloc(void)
{
    map_area_data = heapCtrlMalloc(GetSystemHeapWrkP(), MAP_AREA_DATA_SIZE, 0, 0, 0);
    LoadReq(AREA_DATA_ECB, map_area_data);
}

// ──────────────────────────────────────────────────────────────────────
// The disc's table again (a new game, the difficulty select).
// FF3 0x001f21a0.

void MapAreaDataLoad(void)
{
    LoadReq(AREA_DATA_ECB, map_area_data);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f21c0 (IngameEnd).

void MapAreaDataFree(void)
{
    if (map_area_data != nullptr)
    {
        heapCtrlFree(GetSystemHeapWrkP(), map_area_data, 0, 0, 0);
        map_area_data = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The save block: area number and side.
// FF3 0x001f2208 (no direct caller).

int MapAreaGetSaveData(void **out)
{
    *out = &map_area;
    return sizeof(map_area);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2218.

void MapAreaNoSet(int no)
{
    map_area.no = no;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2220.

int MapAreaNoGet(void)
{
    return map_area.no;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2228.

int MapAreaSideGet(void)
{
    return map_area.side;
}

// ──────────────────────────────────────────────────────────────────────
// A new side (0 / 1) frees the room that is not the loaded area's.
// FF3 0x001f2238.

void MapAreaSideSet(u_int side)
{
    if (side < 2 && (u_int)map_area.side != side)
    {
        MapLoadDeleteRoom(MapLoadGetBuffID(MapLoadGetRoomNoNow()) ^ 1);
        map_area.side = (int)side;
    }
}

// ──────────────────────────────────────────────────────────────────────
// The areas with a second side.
// FF3 0x001f2298.

int MapAreaIsSideArea(int area)
{
    return (u_int)(area - 0x2f) < 6;
}

// ──────────────────────────────────────────────────────────────────────
// The area's room file index: side 1 of a side area uses the six after
// them.
// FF3 0x001f22a8.

int MapAreaFileNoGet(int area)
{
    if (map_area.side == 1 && MapAreaIsSideArea(area))
    {
        area += 6;
    }
    return area;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f22f0 (no direct caller).

void MapAreaSideInit(void)
{
    map_area.side = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2300.

int MapDoorAreaGet(void)
{
    return map_door_area;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2308.

int MapDoorRoomMhGet(int room)
{
    return map_door_room_mh[room];
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2320.

int MapDoorModeGet(void)
{
    for (const MAP_DOOR_MODE_TBL &tbl : map_door_mode_tbl)
    {
        if (tbl.step == map_door_step)
        {
            return tbl.mode;
        }
    }
    return MAP_DOOR_MODE_NONE;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2360.

int MapDoorModeIsOpen(void)
{
    return (u_int)MapDoorModeGet() < 2;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2380 (FrameRateSelPad).

int MapDoorModeIsMove(void)
{
    int mode = MapDoorModeGet();

    if (mode < 0)
    {
        return 0;
    }
    if (mode < 2)
    {
        return 1;
    }
    return mode == MAP_DOOR_MODE_WAIT;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f23c8 (DOOR_STAT_CHECK).

int MapDoorIdGet(void)
{
    return map_door_phase != 0 ? map_door_id : -1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f23e0.

MAP_DOOR_DAT *MapDoorDataGet(void)
{
    return DoorStatGetData(map_door_id);
}

// ──────────────────────────────────────────────────────────────────────
// The player into map hit object mh of area.
// FF3 0x001f23f8.

void MapAreaMhSet(int area, int mh)
{
    void *hit = MapLoadGetAreaData(area, 3);

    ingame_wrk.plyr_wrk->mh_no = mh;
    ingame_wrk.plyr_wrk->floor = RcbGetFloor(hit, mh);
    MapAreaNoSet(area);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2460 (EvAreaChange's first step; mh is not used).

void MapAreaMoveReq(int area, int mh)
{
    (void)mh;
    MapLoadMoveRoom(area);
}

// ──────────────────────────────────────────────────────────────────────
// Busy (1) while a room loads, an event stream is released or the event
// SEs load; then the player moves to (area, mh) with the area's camera
// type and ambient SEs, unless already there. 0 once done.
// FF3 0x001f2478.

int MapAreaChange(int area, int mh)
{
    MapLoadWaitMain();
    if (MapLoadCheckLoadNow() != 0)
    {
        return 1;
    }
    if (EvGhostPreloadIsEnd() == 0)
    {
        return 1;
    }
    if (ev_seIsReady() == 0)
    {
        return 1;
    }
    if (area == MapLoadGetRoomNoNow() && mh == ingame_wrk.plyr_wrk->mh_no)
    {
        return 0;
    }
    MapAreaMhSet(area, mh);
    MrecSetCameraSet(MapAreaDataGet((s_char)area)->cam_type);
    GetLayoutSeWrk()->SetArea(area);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_FLOOR_CHANGE: another map hit object in the player's area.
// FF3 0x001f2538.

void MapAreaFloorChange(int mh)
{
    MapAreaMhSet(GetPlyrAreaNo(), mh);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2568.

void MapDoorLockInc(void)
{
    for (int no : map_door_lock_tbl)
    {
        LockTblInc((u_char)no);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f25b0.

void MapDoorLockDec(void)
{
    for (int no : map_door_lock_tbl)
    {
        LockTblDec((u_char)no);
    }
}

// ──────────────────────────────────────────────────────────────────────
// A rectangle's size as FF3 measures it (x and z of its four vertices;
// FF3's mix of x and z terms kept as is).

static float MapDoorRectSize(const float *r)
{
    float x0 = r[0], z0 = r[2], x1 = r[4], z1 = r[6];
    float x2 = r[8], z2 = r[10], x3 = r[12], z3 = r[14];
    float s  = (x0 - x2) * (z1 - z2) + (x1 - x2) * (x2 - x0)
             + (x2 - x0) * (z3 - z0) + (x3 - x0) * (x0 - x2);

    return s < 0.0f ? -s : s;
}

// MrecFindRectStat's callback for the door rectangles: counts the hits per
// door and keeps its larger rectangle; 1 keeps the search going.
// FF3 0x001f25f8.

static int MapDoorHitCallback(MREC_HIT *hit)
{
    float *rect = MrecGetTestRect();
    int    id   = hit->id;

    for (int i = 0; i < map_door_hit_num; i++)
    {
        MAP_DOOR_HIT *h = &map_door_hit[i];

        if (h->id == id)
        {
            h->cnt++;
            if (MapDoorRectSize(rect) < MapDoorRectSize(h->rect))
            {
                rect = h->rect;
            }
            h->rect = rect;
            return 1;
        }
    }
    if (map_door_hit_num >= MAP_DOOR_HIT_MAX)           // PC: FF3 wrote past the table into the next file's bss
    {
        return 1;
    }
    map_door_hit[map_door_hit_num].cnt  = 1;
    map_door_hit[map_door_hit_num].id   = id;
    map_door_hit[map_door_hit_num].rect = rect;
    map_door_hit_num++;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The action button at a door: of the doors whose rectangles (kind 0x12)
// hold pos, the one hit most often is taken unless it is no-use
// (DOOR_STAT_NOUSE) and the player faces it (within 30 degrees of its
// angle). 1 opens it; a door in lock state 2 is only marked known (and 0).
// FF3 0x001f2778.

int PlyrCheckAction(float rot, float *pos, short floor)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    MAP_DOOR_HIT *best;
    int           id;

    if (IsLockTblOn(3) != 0 || DoorStatGetFlag(1) != 0)
    {
        return 0;
    }
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_OK], -1, 0) == 0)
    {
        return 0;
    }
    if ((plyr->flags & 0x1c00000) != 0 || plyr->mode != 0)
    {
        return 0;
    }

    map_door_hit_num = 0;
    if (MrecFindRectStat(floor, pos, 0x12, MapDoorHitCallback) == nullptr || map_door_hit_num <= 0)
    {
        return 0;
    }

    best = &map_door_hit[0];
    for (int i = 1; i < map_door_hit_num; i++)
    {
        if (best->cnt < map_door_hit[i].cnt)
        {
            best = &map_door_hit[i];
        }
    }

    id = best->id;
    if (DoorStatIsNoUse(id) != 0)
    {
        return 0;
    }
    memcpy(map_door_rect, best->rect, sizeof(map_door_rect));
    map_door_id   = id;
    map_door_area = GetPlyrAreaNo();

    if (EvRotInRange(MapDoorDataGet()->rot, 30, (short)((int)(rot * 180.0f / 3.1415927f) + 180)) != 0)
    {
        if (DoorStatGetLock(id) != 2)
        {
            return 1;
        }
        DoorStatSetKnown(id, 1);
    }
    map_door_id = -1;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f2950 (IngameMain).

int PlyrCheckActionPhaseReq(float rot, float *pos, short floor)
{
    if (PlyrCheckAction(rot, pos, floor) == 0)
    {
        return 0;
    }
    GPhaseCtrlRequest(0x22, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The area on either side of the taken door that holds pos: the door
// area's room at floor, else the room behind at the door's mh floor; -1
// for neither (FrameRateSelPad, MapAreaHitCircle while a door mode moves).
// FF3 0x001f2990.

int MapDoorAreaAtPos(short floor, float *pos)
{
    MAP_DOOR_DAT *door = MapDoorDataGet();
    int           room = MapLoadGetBuffID(map_door_area);

    if (MapLoadCheckBuffPos(room, floor, pos) != 0)
    {
        return map_door_area;
    }
    if (MapLoadCheckBuffPos(room ^ 1, RcbGetFloor(MapLoadGetAreaData(door->area, 3), door->mh), pos) != 0)
    {
        return door->area;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// The door's SE file and SE number n of it (0 open, 1 close, 2 locked).

static int MapDoorSeFile(const MAP_DOOR_DAT *door)
{
    return door->se * 2 + 0x1c63;
}

static u_int MapDoorSe(const MAP_DOOR_DAT *door, int n)
{
    return ((u_int)MapDoorSeFile(door) << 16) + n;
}

// ──────────────────────────────────────────────────────────────────────
// The player is through the door: the door anime back to its start, the
// close SE, then the area the player now stands in (the room behind the
// door, or still the door area's) becomes the current one with its camera
// type, ambient SEs and lights; the door vec is released and the locks
// taken again (MapDoorModeAftEnd drops them).
// FF3 0x001f2a38 (MapDoorModeAft, MapDoorModeNow).

static void MapDoorRoomChange(void)
{
    MAP_OBJ_MDL  *mot  = MapDoorVecGetMot();
    MAP_DOOR_DAT *door = MapDoorDataGet();
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    MAP_PUT        *put;
    int           room;
    int           area;

    if (mot != nullptr)
    {
        mot->SetPose(DoorDatGetMotNo(door->dat_id));
    }
    MapDoorVecCoordSet();
    SndSePlay(MapDoorSe(door, 1), 100, 0x1000, 0, nullptr, 0);
    SndSeBankReleaseAfterPlay((int)MapDoorSe(door, 1), MapDoorSeFile(door), 0);

    room = MapLoadGetBuffID(map_door_area);
    if (MapLoadCheckBuffPos(room, plyr->floor, plyr->pos) == 0)
    {
        // Through: the room behind the door.
        area = MapLoadGetRoomArea(room ^ 1);
        MapAreaMhSet(area, MapDoorDataGet()->mh);
        MapLoadCheckBuffPos(room ^ 1, plyr->floor, plyr->pos);
        RandGhostAreaChange((short)area);
        PhotoObjAreaOff(map_door_area);
        TelRingLowerArea(area);
    }
    else
    {
        // Back: still the door area's room.
        area = MapLoadGetRoomArea(room);
        EvGhostAreaChange(area);
        PhotoObjAreaOff(door->area);
        PhotoObjAreaOn(area);
        TelRingLowerArea(map_door_area);
        ev_seFlipToggle();
    }

    MrecSetCameraSet(MapAreaDataGet((s_char)area)->cam_type);
    GetLayoutSeWrk()->SetArea(area);

    void *light = MapDrawGetLightPtr(area, 0);

    put = MapDoorVecGetPut();
    MapPutSetLitPtr(put, light);
    if (mot != nullptr)                                 // PC: FF3 calls it with nullptr too (reads address 0)
    {
        mot->SetMatrix(MapPutGetMatrix(put));
    }
    *MapPutGetFlag(put) |= 0x40000;
    MapDoorVecSelect(-1);
    MapLoadSetDrawFlg(2, 2);
    EventCameraCut();
    MapDoorLockInc();
}

// ──────────────────────────────────────────────────────────────────────
// One frame with the locks held once more, then the door phase ends.
// FF3 0x001f2c38.

int MapDoorModeAftEnd(void)
{
    int ret;

    MapDoorLockInc();
    ret = IngameMain(0x1f);
    IngameDrawSub(0);
    MapDoorLockDec();
    MapDoorLockDec();
    map_door_step = nullptr;
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The door is open: the game runs (locked while the player is outside
// the door rectangle) until the player leaves the rectangle.
// FF3 0x001f2c88.
// callcheck: HcBaseLineStraddle (the old cached name of HcBaseIsInRectXZ)

int MapDoorModeAft(void)
{
    int in = HcBaseIsInRectXZ(ingame_wrk.plyr_wrk->pos, &map_door_rect[0][0]);
    int ret;

    if (in == 0)
    {
        MapDoorLockInc();
    }
    ret = IngameMain(0x10);
    IngameDrawSub(0);
    if (in == 0)
    {
        MapDoorLockDec();
    }
    if (ret == 0)
    {
        MapDoorVecCoordSet();
        if (HcBaseIsInRectXZ(ingame_wrk.plyr_wrk->pos, &map_door_rect[0][0]) == 0)
        {
            MapDoorRoomChange();
            map_door_step = MapDoorModeAftEnd;
        }
        ret = 0;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The door opens: lights blend while its anime plays; at the end the door
// vec shows again with the map's own data, the door model goes, and the
// player is either still in the door rectangle (Aft) or already through.
// FF3 0x001f2d58.
// callcheck: HcBaseLineStraddle (the old cached name of HcBaseIsInRectXZ)

int MapDoorModeNow(void)
{
    int ret = IngameMain(0x18);

    IngameDrawSub(0);
    MapDoorVecCoordSet();
    MapLightDualProc();
    if (DoorMotionIsEnd() != 0)
    {
        MAP_PUT        *put;

        MapDoorVecSetDisp(1);
        put = MapDoorVecGetPut();
        MapPutSetLitPtr(put, LightCtlIsRoomDrawn() != 0 ? LightCtlGetLightData() : nullptr);
        map_door_obj->PutChange();
        *MapPutGetFlag(put) &= 0xfff9a7df;   // clears 0x20, 0x800, 0x1000, 0x4000, 0x20000, 0x40000
        MapDoorLockDec();
        if (HcBaseIsInRectXZ(ingame_wrk.plyr_wrk->pos, &map_door_rect[0][0]) == 0)
        {
            MapDoorRoomChange();
            map_door_step = MapDoorModeAftEnd;
        }
        else
        {
            map_door_step = MapDoorModeAft;
        }
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The end of a door that did not open: locks dropped, the door vec shown
// and released.
// FF3 0x001f2e58.

static void MapDoorModeEnd(void)
{
    LockTblDec(8);
    MapDoorLockDec();
    MapDoorVecSetDisp(1);
    MapDoorVecSelect(-1);
}

// ──────────────────────────────────────────────────────────────────────
// The locked-door message until the window closes.
// FF3 0x001f2e88.

static int MapDoorModeMsg(void)
{
    int ret = IngameMain(0x1c);

    IngameDrawSub(0);
    map_door_win->Main();
    if (map_door_win->mode == SEL_WIN_MODE_NONE)
    {
        map_door_win->Close();
        MapDoorModeEnd();
        delete map_door_win;                            // vtable +4: deleting destructor (the pointer is not cleared)
        map_door_step = nullptr;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Waits for the message window's layout data.
// FF3 0x001f2f08.

static int MapDoorModeMsgOpen(void)
{
    int ret = IngameMain(0x1c);

    IngameDrawSub(0);
    map_door_win->Main();
    if (map_door_win->IsReady() != 0)
    {
        map_door_win->SetItem(-1, -1);
        map_door_step = MapDoorModeMsg;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The door stays shut: once the try anime ends the player gets back the
// normal mode and the pos from before the door; lock states 3 / 4 show
// locked-door message 8 / 9, any other ends the phase.
// FF3 0x001f2f70.
// callcheck: operator_new, SelWinCtor (new CWindowMsg)

int MapDoorModeClose(void)
{
#ifdef REIPAN_SLPS_25544_CHS
    constexpr int door_message_group = 0x81; // SLPS_255.44 0x001edc50
#else
    constexpr int door_message_group = 0x82;
#endif
    int ret = IngameMain(0x1c);

    IngameDrawSub(0);
    if (DoorMotionIsEnd() != 0)
    {
        int msg   = -1;
        int state = DoorStatGetLock(map_door_id);

        if (state == 3)
        {
            msg = 8;
        }
        else if (state == 4)
        {
            msg = 9;
        }
        ingame_wrk.plyr_wrk->flags &= ~(u_long)(PLYR_FLG_DOOR_LOCK | PLYR_FLG_DOOR_OPEN | PLYR_FLG_ANIME_END);
        PlyrSetMode(PLYR_MODE_NORMAL, nullptr);
        PlyrSet280Req(0, 0.0f, 50.0f, 128.0f, 0.0f);
        PlyrSet338();
        MapDoorVecSetDisp(1);
        PlyrSet264(4.8f);
        PlyrSet280Req(0, 0.0f, 10.0f, 128.0f, 10.0f);
        memcpy(ingame_wrk.plyr_wrk->pos, map_door_plyr_pos, sizeof(map_door_plyr_pos));
        EventCameraCut();

        if (msg < 0)
        {
            if (DoorMotionIsEnd() != 0)
            {
                MapDoorModeEnd();
                map_door_step = nullptr;
            }
        }
        else
        {
            map_door_win = new CWindowMsg;
            map_door_win->Open();
            map_door_win->StartMsg(door_message_group, msg, 1);
            map_door_win->SetAutoClose(1);
            map_door_step = MapDoorModeMsgOpen;
        }
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Waits for the door's SE bank. A locked door (or one without a model)
// plays SE 2 and closes; an open one waits for the room behind it, event
// streams and SEs and its model, then starts the door anime (SE 0), the
// light blend into the room behind and the door model on the door vec.
// FF3 0x001f3150.

int MapDoorModeWait(void)
{
    int           ret  = IngameMain(0x19);
    MAP_DOOR_DAT *door;

    IngameDrawSub(0);
    door = MapDoorDataGet();
    if (SndSeBankIsLoaded(MapDoorSeFile(door)) == 0)
    {
        return ret;
    }

    if (DoorStatGetLock(map_door_id) == 0 && map_door_obj != nullptr)
    {
        if (MapLoadCheckLoadNow() == 0 && EvGhostPreloadIsEnd() != 0 && ev_seIsReady() != 0
            && map_door_obj->IsReady() != 0)
        {
            int             anime = -1;
            MAP_OBJ_MDL    *mot;
            MAP_PUT        *put;

            LockTblDec(8);
            PlyrDoorStart(door->dat_id, 1);
            mot = MapDoorVecGetMot();
            if (mot != nullptr)
            {
                anime = DoorDatGetMotNo(door->dat_id);
                mot->PlayMotion(anime, 1.0f);
                SndSePlay(MapDoorSe(door, 0), 100, 0x1000, 0, nullptr, 0);
                PlyrSet338();
            }

            GRA3DLIGHTDATA *from = MapDrawGetLightPtr(map_door_area, 0);
            GRA3DLIGHTDATA *to   = MapDrawGetLightPtr(door->area, 0);

            MapLightMakeDual(to, from, ingame_wrk.plyr_wrk->pos);
            put = MapDoorVecGetPut();
            MapPutSetLitPtr(put, MapLightGetDualWork(2));
            *MapPutGetFlag(put) = (*MapPutGetFlag(put) & 0xfff9a7df) | 0x1000;
            map_door_obj->Set(put, anime);
            PhotoObjAreaOn(door->area);
            TelRingRaiseArea(door->area);
            CatDoorFollowGo();
            map_door_step = MapDoorModeNow;
        }
    }
    else
    {
        map_door_step = MapDoorModeClose;
        SndSePlay(MapDoorSe(door, 2), 100, 0x1000, 0, nullptr, 0);
        SndSeBankReleaseAfterPlay((int)MapDoorSe(door, 2), MapDoorSeFile(door), 0);
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The player's reach for the door; then the door SE bank lock (8).
// FF3 0x001f3398.

int MapDoorModeBfo(void)
{
    int ret = IngameMain(0x1e);

    IngameDrawSub(0);
    if (DoorMotionIsEnd() != 0)
    {
        LockTblInc(8);
        map_door_step = MapDoorModeWait;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 0x22 (34), Story_Door_Open: entered from IngameMain through
// PlyrCheckActionPhaseReq; back to 0x20 when the door steps end.
// ──────────────────────────────────────────────────────────────────────

// Once per door: the player faces the door at its start pos (from the
// door's DOOR_DAT through the door vec's matrix), the door SE bank loads,
// the pos is kept for a shut door. An unlocked door (state 0) loads the
// room behind it, keeps each room's mh and loads its door model; a locked
// one only plays the try anime. The event camera takes the door's camera
// (or the cat's), the locks are taken and MapDoorModeBfo runs.
// FF3 0x001f33f0.
// callcheck: operator_new, MapDBuffCtor (new CMapDBuff)

int init_Story_Door_Open(void)
{
    if (map_door_phase != 0)
    {
        return 1;
    }
    map_door_phase = 1;

    MAP_DOOR_DAT *door = MapDoorDataGet();
    int           area = GetPlyrAreaNo();
    float        *org  = MapLoadGetOffset(MapLoadGetBuffID(area));
    float         rot[4];
    float         pos[4];
    float         cam_pos[4] = {};                      // PC: w is not written in FF3
    float         cam_tgt[4] = {};
    int           state;

    rot[0] = 0.0f;
    rot[1] = door->plyr_rot * 3.1415927f / 180.0f;
    rot[2] = 0.0f;
    rot[3] = 0.0f;
    pos[0] = 0.0f;                                      // FF3 fills it as rot; DoorDatGetPlyrPos overwrites it
    pos[1] = rot[1];
    pos[2] = 0.0f;
    pos[3] = 0.0f;
    PlyrSetRot(rot);
    SndSeBankLoad(MapDoorSeFile(door));
    DoorStatSetKnown(map_door_id, 1);
    memcpy(map_door_plyr_pos, ingame_wrk.plyr_wrk->pos, sizeof(map_door_plyr_pos));

    state = DoorStatGetLock(map_door_id);
    if (state == 0)
    {
        int room = MapLoadGetBuffID(area);

        MapLoadMoveRoom(door->area);
        map_door_room_mh[room]     = ingame_wrk.plyr_wrk->mh_no;
        map_door_room_mh[room ^ 1] = door->mh;
        PlyrDoorStart(door->dat_id, 0);
        map_door_obj = new CMapDBuff;
        map_door_obj->LoadReq(door->dbuff);
    }
    else
    {
        map_door_obj = nullptr;
        PlyrSetMode(PLYR_MODE_DOOR, nullptr);
        ingame_wrk.plyr_wrk->flags |= PLYR_FLG_DOOR_LOCK;
        PlyrDoorStart(door->dat_id, 0);
    }

    MapDoorVecSelect(map_door_id);
    DoorDatGetPlyrPos(PlyrGetCurNo(), pos, door->dat_id, MapPutGetMatrix(MapDoorVecGetPut()), state == 0);
    PlyrSetPos(pos);
    PlyrTurnFlagClear();
    MapDoorVecSetDisp(0);

    EventCameraReq();
    EventCameraSetWorldFlg(1);
    for (int i = 0; i < 3; i++)
    {
        cam_pos[i] = door->cam_pos[i] + org[i];
        cam_tgt[i] = door->cam_tgt[i] + org[i];
    }
    CatDoorFollowStart(door->dbuff, cam_pos, cam_tgt);
    EventCameraSetPosition(cam_pos);
    EventCameraSetTarget(cam_tgt);
    MapDoorLockInc();
    map_door_step = MapDoorModeBfo;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// After one_Story_Door_Open has ended the steps: the door model goes, the
// door state and the event camera's world flag are cleared.
// FF3 0x001f3680.

int end_Story_Door_Open(void)
{
    if (map_door_phase == 0)
    {
        if (map_door_obj != nullptr)
        {
            delete map_door_obj;                        // vtable +4: deleting destructor
            map_door_obj = nullptr;
        }
        map_door_id = -1;
        EventCameraSetWorldFlg(0);
        map_door_area = -1;
        map_door_step = nullptr;
        CatDoorFollowEnd();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The current door step; once a step has cleared it, back to phase 0x20.
// FF3 0x001f36e8.

int one_Story_Door_Open(void)
{
    int ret = map_door_step();

    if (map_door_step == nullptr)
    {
        map_door_phase = 0;
        GPhaseCtrlRequest(0x20, 0);
        ret = 1;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f3728 (IngameInit).

int MapDoorInit(void)
{
    map_door_phase = 0;
    map_door_id    = -1;
    map_door_area  = -1;
    map_door_step  = nullptr;
    map_door_obj   = nullptr;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f3748 (IngameEnd).

void MapDoorDelete(void)
{
    if (map_door_obj != nullptr)
    {
        delete map_door_obj;                            // vtable +4: deleting destructor
        map_door_obj = nullptr;
    }
    map_door_phase = 0;
}
