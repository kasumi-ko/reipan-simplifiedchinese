/* ==========================================================================
 *  ingame/map/MapLoad.c
 *
 *  Room streaming (see MapLoad.h). FF3 0x11afa0..0x11bf28 plus the inlines
 *  emitted out of line, MAP_ROOM_DAT's constructor (0x2ac330) and
 *  MapLoadRoomMission (0x2ac3a8); FF2's MapLoad.c rewritten for areas: a
 *  buffer is found by its area word (FF2: by a file label), an area loads as
 *  its model PK4 and one obj_pack PHF (RoomObjPackLoadReq), and the RegDat
 *  slots of a buffer are a RegList (MapRoomFile.c) instead of four ids.
 *  Written from the FF3 assembly; FF2's debug prints and asserts are gone.
 * ======================================================================== */
#include "ingame/map/MapLoad.h"

#include <string.h>                             // memcmp, memset

#include "common/pk4.h"                         // PK4_HEADER, Pk4GetData
#include "graphics/mmanage.h"                   // ModelManager, model_collision_set (MapLoadRoomMission)
#include "ingame/event/prg/ev_chapter.h"        // EvGhostAreaChange
#include "ingame/event/prg/ev_se.h"             // ev_seChangeRoom
#include "ingame/map/MapDraw.h"                 // MapDrawInit / InitRoom / RoomObjRegist / DeleteNoDraw / DeleteRoom
#include "ingame/map/MapPut.h"                  // MapPutSetFlg
#include "ingame/map/RegDat.h"                  // RegDatInit, RegDatResetNoRegistList
#include "ingame/map/foot_se.h"                 // foot_seSetRoom
#include "ingame/map/map_height.h"              // MhClearRoom, MhSetOffset2
#include "ingame/photo/photo_obj.h"              // PhotoObjAreaOn / Off
#include "ingame/map/MapEff.h"                  // MapLoadHookNop, MapLoadSlotReset
#include "ingame/map/MapDoor.h"      // door table, door motion pack
#include "ingame/map/RoomLoadAddr.h"             // RoomLoadAddrSetBase
#include "ingame/story_door.h"                  // MapAreaNoGet / NoSet / SideInit / SideGet / SideSet
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer
#include "system/eeiop/cd_file_dat.h"           // ROS00_PK4
#include "system/os/eecdvd.h"                   // IsLoadEndAll

int   MapLoadDrawID = -1;                       // sdata 0x36b858
u_int MapLoadStat;                              // sdata 0x36b85c

// Constructed at start-up: FF3 0x0011beb8 (__static_initialization_and_
// destruction_0, (1, 0xffff)) runs MAP_ROOM_DAT's constructor on both, FF3
// 0x0011bf08 (_GLOBAL__I_) calls it; on the host the definition does both.
static MAP_ROOM_DAT MapLoadBuff[2];             // bss 0x386580

// Where each buffer's files load (EE addresses).
static const u_int map_load_addr[2] = { 0x8f8000, 0xc3d000 };      // sdata 0x36b860

// The door anime buffer below them (MapDoorAnimInit).
#define MAP_DOOR_ANIM_ADDR  0x8f0000

// The room model's rotation: none.
const float map_room_rot[4] = { 0.0f, 0.0f, 0.0f, 0.0f };          // data 0x329530 (MapDraw.c too)

// ──────────────────────────────────────────────────────────────────────
// The area of a room file (seven files per area from ROS00_PK4). No callers.
// FF3 0x0011afa0.

int MapLoadGetArea4File(int file)
{
    return (file - ROS00_PK4) / 7;
}

// FF3 0x0011afc0.
int MapLoadCheckDrawFlg(int room)
{
    if ((u_int)room < 2)
    {
        return (MapLoadBuff[room].stat & MAP_ROOM_HIDE) == 0;
    }
    return 0;
}

// FF3 0x0011b000.
int MapLoadCheckLoadNow(void)
{
    return MapLoadStat & MAP_LOAD_NOW;
}

// FF3 0x0011b010.
int MapLoadCheckLoadEnd(int room)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(room);

    // PC: FF3 reads the area word before testing for nullptr (then answers 1).
    if (dat == nullptr)
    {
        return 1;
    }
    if (dat->area < 0)
    {
        return 0;
    }
    return (dat->stat & MAP_ROOM_LOADING) == 0;
}

// FF3 0x0011b058.
int MapLoadCheckDisp(int room)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(room);

    if (dat == nullptr || dat->area < 0)
    {
        return 0;
    }
    return (dat->stat & MAP_ROOM_HIDE) ^ 1;
}

// FF3 0x0011b098.
int MapLoadCheckFreeAreaID(int area)
{
    for (int i = 0; i < 2; i++)
    {
        if (MapLoadBuff[i].area == area)
        {
            return i;
        }
    }
    return -1;
}

// FF3 0x0011b0d0.
MAP_ROOM_DAT *MapLoadGetFreeMemAddr(int room)
{
    if ((u_int)room < 2)
    {
        return &MapLoadBuff[room];
    }
    return nullptr;
}

// FF3 0x0011b0f8.
int MapLoadGetBuffID4Label(int label)
{
    for (int i = 0; i < 2; i++)
    {
        if (RegListCheck(MapLoadBuff[i].reg_list, label) != 0)
        {
            return i;
        }
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// The RegDat slots of hidden buffers go on the no-register list.
// FF3 0x0011b168.

void MapLoadSetNoRegList(void)
{
    RegDatResetNoRegistList();
    for (int i = 0; i < 2; i++)
    {
        if (MapLoadCheckDrawFlg(i) == 0)
        {
            RegListExe(MapLoadGetFreeMemAddr(i)->reg_list);
        }
    }
}

// FF3 0x0011b1b8.
int MapLoadGetRoomNoNow(void)
{
    return MapLoadGetRoomArea(MapLoadCheckFreeAreaID(MapAreaNoGet()));
}

// FF3 0x0011b1e0.
MAP_ROOM_DAT *MapLoadGetCurRoomAddr(void)
{
    int room = MapLoadCheckFreeAreaID(MapAreaNoGet());

    if (room < 0)
    {
        return nullptr;
    }
    return MapLoadGetFreeMemAddr(room);
}

// ──────────────────────────────────────────────────────────────────────
// Show (sw 1) or hide (sw 2) a buffer when it is not that way already
// (FF2's MapLoadCheckSwitch and MapLoadSetDrawFlgSub, inlined).
// FF3 0x0011b220.

void MapLoadSetDrawFlg2(int room, int sw)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(room);

    // PC: FF3 reads (sw 1) or writes (sw 2) through nullptr for a bad buffer
    // (MapLoadSetDrawFlg3 of an area not loaded).
    if (dat == nullptr)
    {
        return;
    }

    if (sw == 1)
    {
        if (dat->area < 0 || (dat->stat & MAP_ROOM_HIDE) == 0)
        {
            return;
        }
        dat->stat &= ~MAP_ROOM_HIDE;
        MapDrawRoomObjRegist(dat);
        MapLoadSetNoRegList();
        MapLoadRoomMission(dat, 1);
    }
    else if (sw == 2)
    {
        if ((dat->stat & MAP_ROOM_HIDE) != 0)
        {
            return;
        }
        dat->stat |= MAP_ROOM_HIDE;
        gra3dSetLightData(&dat->light, nullptr);
        MapDrawDeleteNoDraw(dat);
        MapPutSetFlg(room, 8);
        MapLoadSetNoRegList();
        MapLoadRoomMission(dat, 0);
    }
}

// flg 2: the buffer the player is not in, flg 1: the one he is in.
// FF3 0x0011b320.

void MapLoadSetDrawFlg(u_int flg, int sw)
{
    int room = MapLoadCheckFreeAreaID(MapAreaNoGet());

    if (room < 0)
    {
        return;
    }
    if ((flg & 2) != 0)
    {
        MapLoadSetDrawFlg2(room ^ 1, sw);
    }
    if ((flg & 1) != 0)
    {
        MapLoadSetDrawFlg2(room, sw);
    }
}

// FF3 0x0011b3b0.
void MapLoadSetDrawFlg3(int area, int sw)
{
    MapLoadSetDrawFlg2(MapLoadCheckFreeAreaID(area), sw);
}

// Only area's buffer drawn; both hidden when it is not loaded.
// FF3 0x0011b3e0.

void MapLoadSetDrawOnly(int area)
{
    int room = MapLoadCheckFreeAreaID(area);

    if (room == -1)
    {
        MapLoadSetDrawFlg2(0, 2);
        MapLoadSetDrawFlg2(1, 2);
        return;
    }
    MapLoadSetDrawFlg2(room, 1);
    MapLoadSetDrawFlg2(room ^ 1, 2);
}

// ──────────────────────────────────────────────────────────────────────
// Move a loaded area to (x, y, z). As in FF2, the delta handed to
// MhSetOffset2 subtracts every axis from x (three sub.s against f20).
// FF3 0x0011b440.

void MapLoadSetOffSet(int area, float x, float y, float z)
{
    int           room = MapLoadGetBuffID(area);
    MAP_ROOM_DAT *dat;
    float         work[4];

    if (room < 0)
    {
        return;
    }
    dat = MapLoadGetFreeMemAddr(room);
    if (dat == nullptr)
    {
        return;
    }

    RegListSetOffset(dat->reg_list, x, y, z);
    work[0] = x - dat->origin[0];
    work[1] = x - dat->origin[1];
    work[2] = x - dat->origin[2];
    work[3] = 0.0f;                             // PC: FF3 leaves it unset
    MhSetOffset2(room, work);

    dat->origin[0] = x;
    dat->origin[1] = y;
    dat->origin[2] = z;
    dat->mdl.SetPosRot(dat->origin, map_room_rot);
}

// FF3 0x0011b510.
float *MapLoadGetOffset(int room)
{
    MAP_ROOM_DAT *dat;

    if (room < 0)
    {
        return nullptr;
    }
    dat = MapLoadGetFreeMemAddr(room);
    return dat != nullptr ? dat->origin : nullptr;
}

// FF3 0x0011b540.
void MapLoadGetOffsetVector(float *v, int room)
{
    float *ofs;

    v[0] = v[1] = v[2] = v[3] = 0.0f;
    ofs = MapLoadGetOffset(room);
    if (ofs != nullptr)
    {
        v[0] = ofs[0];
        v[1] = ofs[1];
        v[2] = ofs[2];
        v[3] = ofs[3];
    }
}

// The RegDat slot of area on floor kai (kai < 0: 0xb), -1.
// FF3 0x0011b5a0.

int MapLoadGetRegBuffID(int area, int kai)
{
    int room = MapLoadCheckFreeAreaID(area);

    if (kai < 0)
    {
        kai = 0xb;
    }
    if (room < 0)
    {
        return -1;
    }
    return RegListFindLabel(MapLoadBuff[room].reg_list, kai);
}

// FF3 0x0011b608.
int MapLoadCheckBuffPos(int room, int floor, float *pos)
{
    if (MapLoadCheckDrawFlg(room) == 0)
    {
        return 0;
    }
    return RegListGetBuffID(MapLoadGetFreeMemAddr(room)->reg_list, floor, pos);
}

// FF3 0x0011b678.
int MapLoadGetBuffID4Pos(int floor, float *pos)
{
    for (int i = 0; i < 2; i++)
    {
        if (MapLoadCheckBuffPos(i, floor, pos) != 0)
        {
            return i;
        }
    }
    return -1;
}

// FF3 0x0011b6e0.
int MapLoadGetBuffID(int area)
{
    if (area < 0)
    {
        return -2;
    }
    return MapLoadCheckFreeAreaID(area);
}

// FF3 0x0011b708.
int MapLoadGetRoomArea(int room)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(room);

    return dat != nullptr ? dat->area : -1;
}

// ──────────────────────────────────────────────────────────────────────
// The loaded buffer is set up and shown; the load ends.
// FF3 0x0011b738.

int MapLoadInitRoom(void)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(MapLoadDrawID);

    MapDrawInitRoom(dat);
    RegListRegist(MapLoadGetRoomData(dat, 1), dat->origin, dat->reg_list);
    MapLoadSetDrawFlg2(dat->room, 1);
    MapLoadSetNoRegList();
    dat->stat &= ~MAP_ROOM_LOADING;
    MapLoadStat &= ~MAP_LOAD_NOW;
    return MapLoadDrawID;
}

// FF3 0x0011b7c0.
void MapLoadDeleteRoom(int room)
{
    MAP_ROOM_DAT *dat;

    if ((u_int)room >= 2)
    {
        return;
    }
    dat = &MapLoadBuff[room];
    MhClearRoom(room);
    MapDrawDeleteRoom(dat);
    dat->area = -1;
    dat->stat = MAP_ROOM_HIDE;
    for (int i = 0; i < ROOM_FILE_NUM; i++)
    {
        dat->file[i] = 0;
    }
}

// FF3 0x0011b838.
void MapLoadDeleteRoomAll(void)
{
    MapLoadDeleteRoom(0);
    MapLoadDeleteRoom(1);
}

// ──────────────────────────────────────────────────────────────────────
// A buffer's files.
// FF3 0x0011b860.

u_int MapLoadGetFileAddr(MAP_ROOM_DAT *dat, int no)
{
    // PC: FF3 reads word 0x20 + no * 4 of nullptr (MapLoadGetAreaData of an
    // area not loaded).
    if (dat == nullptr)
    {
        return 0;
    }
    return dat->file[no];
}

// FF3 0x0011b870.
void *MapLoadGetRoomData(MAP_ROOM_DAT *dat, int no)
{
    const u_int address = MapLoadGetFileAddr(dat, ROOM_FILE_DATA_PK4);

    if (address == 0)
    {
        return nullptr;
    }
    auto *pk4 = (PK4_HEADER *)ReiPan_GetHostPointer(address);
    // RS410 starts with the registration PK4. Its pack omits the light
    // member entirely, shifting the height, hit and placement data.
    if (pk4->num == 5 && memcmp(Pk4GetData(pk4, 0), "PK4", 4) == 0)
        return Pk4GetData(pk4, no - 1);
    return Pk4GetData(pk4, no);
}

// FF3 0x0011b8b8.
void *MapLoadGetAreaData(int area, int no)
{
    return MapLoadGetRoomData(MapLoadGetFreeMemAddr(MapLoadGetBuffID(area)), no);
}

// FF3 0x0011b8f0.
int MapLoadCheckRoomDataNum(MAP_ROOM_DAT *dat)
{
    u_int pk4 = MapLoadGetFileAddr(dat, ROOM_FILE_DATA_PK4);

    if (pk4 == 0)
    {
        return 0;
    }
    return ((const PK4_HEADER *)ReiPan_GetHostPointer(pk4))->num == 7;
}

// ──────────────────────────────────────────────────────────────────────
// Entering area: its event SEs, its enemy set, the play data.
// FF3 0x0011b928.

void MapLoadSetNowRoom(int area)
{
    ev_seChangeRoom(area);
    EvGhostAreaChange(area);
    PhotoObjAreaOn(area);
}

// Load area into buffer room (hidden until MapLoadInitRoom).
// FF3 0x0011b958.

void MapLoadRegistReq(int area, int room)
{
    MAP_ROOM_DAT *dat;

    MapLoadDrawID = room;
    MapLoadDeleteRoom(room);
    dat = MapLoadGetFreeMemAddr(room);
    dat->area = area;
    dat->stat = MAP_ROOM_HIDE | MAP_ROOM_LOADING;
    MapLoadStat |= MAP_LOAD_NOW;
    MapLoadSetNowRoom(area);
    RoomObjPackLoadReq(area, dat);
    MapLoadHookNop();
}

// The buffer of area: the current one, the other one when area is loaded
// there already (shown), else area is requested into the buffer the player
// is not in.
// FF3 0x0011b9c8.

int MapLoadMoveRoom(int area)
{
    int cur = MapAreaNoGet();
    int room;

    if (area == cur)
    {
        MapLoadSetNowRoom(area);
        return MapLoadDrawID;
    }

    room = MapLoadCheckFreeAreaID(area);
    if (room >= 0)
    {
        MapLoadSetNowRoom(area);
        MapLoadSetDrawFlg(2, 1);
        MapLoadDrawID = room;
        return room;
    }

    room = MapLoadCheckFreeAreaID(cur);
    if (room < 0)
    {
        room = 0;
    }
    MapLoadRegistReq(area, room ^ 1);
    return MapLoadDrawID;
}

// FF3 0x0011ba70.
int MapLoadReload(int area)
{
    int room = MapLoadCheckFreeAreaID(area);

    if (room < 0)
    {
        return -1;
    }
    MapAreaNoSet(area);
    MapLoadRegistReq(area, room);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Once the obj_pack is in: the area's data, footstep SEs and the buffer's
// next load address. 1 when done now, -1 while loading, 0 when done before.
// FF3 0x0011bac8.

int MapLoadCallFurn(void)
{
    MAP_ROOM_DAT *dat = MapLoadGetFreeMemAddr(MapLoadDrawID);
    void         *addr;

    if (dat != nullptr && (dat->stat & MAP_ROOM_FURN) != 0)
    {
        return 0;
    }
    if (RoomObjPackIsLoadEnd() == 0)
    {
        return -1;
    }
    addr = RoomObjPackLoadEnd(dat);
    foot_seSetRoom();
    RoomLoadAddrSetBase(MapLoadDrawID, addr);
    dat->stat |= MAP_ROOM_FURN;
    return 1;
}

// FF2 MapLoadMain: -1 while a room loads; then the room is set up.
// FF3 0x0011bb60.

int MapLoadWaitMain(void)
{
    if ((MapLoadStat & MAP_LOAD_NOW) == 0)
    {
        return 0;
    }
    if (MapLoadCallFurn() != 0)
    {
        return -1;
    }
    if (IsLoadEndAll() == 0)
    {
        return -1;
    }

    MapLoadHookNop();
    MapLoadStat &= ~MAP_LOAD_NOW;
    MapLoadSlotReset();
    if ((MapLoadStat & MAP_LOAD_NO_INIT) != 0)
    {
        MapLoadStat &= ~MAP_LOAD_NO_INIT;
    }
    else
    {
        MapLoadInitRoom();
    }
    return 0;
}

// FF3 0x0011bbf8.
void MapLoadIngameInit(void)
{
    MapAreaSideInit();
}

// ──────────────────────────────────────────────────────────────────────
// Both buffers emptied, the map subsystems reset and area (>= 0) requested
// into buffer 0.
// FF3 0x0011bc10.

int MapLoadInit(int area)
{
    u_int addr[2] = { map_load_addr[0], map_load_addr[1] };

    memset((void *)MapLoadBuff, 0, sizeof(MapLoadBuff));
    for (int i = 0; i < 2; i++)
    {
        MAP_ROOM_DAT *dat = &MapLoadBuff[i];

        dat->room      = i;
        dat->stat      = MAP_ROOM_HIDE;
        dat->load_addr = addr[i];
        dat->area      = -1;
        RegListClear(dat->reg_list);
    }

    MapLoadDrawID = 0;
    MapLoadStat   = 0;
    MapLoadSlotReset();
    MapLoadSetDrawFlg(3, 2);
    MapAreaNoSet(area);
    RegDatInit();
    MapDrawInit();
    MapDoorAnimInit(MAP_DOOR_ANIM_ADDR);
    if (area >= 0)
    {
        MapLoadRegistReq(area, 0);
    }
    return 0;
}

// Empty the buffer the player is not in (hidden first) and hand back its
// load address; 0 while loading or when the current area is not loaded.
// FF3 0x0011bd38.

u_int MapLoadGetFreeArea(void)
{
    int room;

    if ((MapLoadStat & MAP_LOAD_NOW) != 0)
    {
        return 0;
    }
    room = MapLoadCheckFreeAreaID(MapAreaNoGet());
    if (room < 0)
    {
        return 0;
    }
    room ^= 1;
    if (MapLoadCheckDrawFlg(room) != 0)
    {
        MapLoadSetDrawFlg(2, 2);
    }
    MapLoadDeleteRoom(room);
    return MapLoadBuff[room].load_addr;
}

// ──────────────────────────────────────────────────────────────────────
// Unless a room load is running: when area or side (-1: keep the side)
// differ from the current ones, the buffer holding the current area is
// emptied and area (on side) requested into it in its place. That buffer,
// -1 when nothing changed, a load is running or the current area is not
// loaded. SceneRoomLoadMain: the scene's room on its header's side.
// FF3 0x0011bdc8 (the wrong FF2 match MapObjCheckNuno until 2026-10-21).

int MapLoadChangeArea(int area, s_char side)
{
    int cur;
    int room;
    int side_differs;

    if ((MapLoadStat & MAP_LOAD_NOW) != 0)
    {
        return -1;
    }
    cur  = MapAreaNoGet();
    room = MapLoadCheckFreeAreaID(MapAreaNoGet());
    if (room < 0)
    {
        return -1;
    }

    side_differs = 0;
    if (side != -1)
    {
        side_differs = side != MapAreaSideGet();
    }
    if (side_differs == 0 && area == cur)
    {
        return -1;
    }

    MapLoadDeleteRoom(room);
    if (side != -1)
    {
        MapAreaSideSet(side);
    }
    MapLoadSlotReset();
    MapLoadRegistReq(area, room);
    MapAreaNoSet(area);
    return room;
}

// 0x0011beb0 (8 bytes, jr ra) is an empty function with no references
// (Ghidra folds it into BorrowBufCtor's body); nothing to write.

// ──────────────────────────────────────────────────────────────────────
// An empty buffer: no area, nothing loaded, an empty RegList. origin and
// light are left as they are (bss: zero).
// FF3 0x002ac330 (an inline emitted out of line).

MAP_ROOM_DAT::MAP_ROOM_DAT()
{
    room      = 0;
    area      = -1;
    stat      = 0;
    load_addr = 0;
    for (int i = 0; i < ROOM_FILE_NUM; i++)
    {
        file[i] = 0;
    }
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        reg_list[i] = -1;
    }
    mdl.mot     = nullptr;
    mdl.mot_pk4 = nullptr;
    mdl.builder = nullptr;
    mdl.mdl_pk4 = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// The room model's collision shapes (its ModelManager's, vtable +0x50) join
// the collision registry while the buffer is shown (on 1) and leave it when
// it is hidden (on 0).
// FF3 0x002ac3a8 (an inline emitted out of line).

void MapLoadRoomMission(MAP_ROOM_DAT *dat, int on)
{
    if (dat == nullptr || dat->mdl.mot == nullptr)
    {
        return;
    }
    ModelManager *mdl = dat->mdl.mot;

    if (on != 0)
        model_collision_set.Add(mdl->GetCollision());
    else
        model_collision_set.Remove(mdl->GetCollision());
}
