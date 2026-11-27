/* ==========================================================================
 *  ingame/map/MapRoomFile.c
 *
 *  A room's files and RegDat id lists (see MapRoomFile.h). FF3
 *  0x1f9b48..0x1fa310; FF3-only, written from the FF3 decompilation.
 *  RoomFileLoadReq / RoomFileIsLoadEnd / RoomFileLoadEnd (the data PK4 as
 *  its own file) have no callers left: the obj_pack PHF carries the data
 *  PK4 as its last member instead (RoomObjPackLoadEnd).
 *
 *  The room buffer keeps EE addresses (MAP_ROOM_DAT); the loads take host
 *  pointers, so the addresses are converted at the edges.
 * ======================================================================== */
#include "ingame/map/MapRoomFile.h"

#include "common/pk4.h"                         // PK4_HEADER, Pk4GetData
#include "ingame/map/MapDraw.h"                 // MapDrawLoadFurnAll, MapDrawInitFurn
#include "ingame/map/PosFile.h"                 // PosFileForEach
#include "ingame/map/RoomLoadAddr.h"            // RoomLoadAddrSetBase / GetCur
#include "ingame/map/MapLoad.h"                 // MAP_ROOM_DAT, MapLoadGetRoomData
#include "ingame/map/MapPhf.h"                  // PhfGetFileNum / PhfGetFileData
#include "ingame/map/MapRcb.h"                  // RcbInit
#include "ingame/story_door.h"                  // MapAreaDataGet / FileNoGet / SideGet
#include "ingame/map/RegDat.h"                  // RegDat*
#include "ingame/map/MapEff.h"                  // MapLoadSlotReq / MapLoadSlotIsEnd
#include "graphics/effect/effect_oth.h"         // RoomGlowDatSet
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer / ReiPan_GetPs2Address
#include "system/eeiop/cd_file_dat.h"           // ROS00_PK4 ..

// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────

// Areas whose obj_pack side follows MapAreaSideGet.
static const int side_area_tbl[4] = { 0x2f, 0x30, 0x31, 0x34 };        /* data 35b90c */

static void *EeToHost(u_int addr)
{
    return ReiPan_GetHostPointer(addr);
}

static u_int HostToEe(const void *ptr)
{
    return (u_int)ReiPan_GetPs2Address(ptr);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f9b48.

int RoomFileGetMdlPk4(int area)
{
    return ROS00_PK4 + MapAreaFileNoGet(area) * 7;
}

// ──────────────────────────────────────────────────────────────────────
// PosFileForEach's area_pos callback: the room's origin from the entry.
// FF3 0x001f9b78 (hidden after RoomFileGetMdlPk4; created 2026-10-15).

static int RoomFileAreaPosHook(void *ent, void *arg)
{
    const float  *pos = (const float *)ent;
    MAP_ROOM_DAT *dat = (MAP_ROOM_DAT *)arg;

    dat->origin[0] = pos[0];
    dat->origin[1] = pos[1];
    dat->origin[2] = pos[2];
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The obj_pack side of an area: the side areas take MapAreaSideGet into
// their area row first.
// FF3 0x001f9b98.

u_char RoomFileGetSide(int area)
{
    MAP_AREA_DAT *row = MapAreaDataGet((s_char)area);

    if (row == nullptr)                 // PC: guard (FF3 read address 1 for an area without a row)
    {
        return 0;
    }
    for (int i = 0; i < 4; i++)
    {
        if (side_area_tbl[i] == area)
        {
            row->side = MapAreaSideGet() != 0;
            return row->side;
        }
    }
    return row->side;
}

int RoomFileGetObjPack(int area)
{
    return ROS00_0_PHF + area * 7 + RoomFileGetSide(area);
}

// ──────────────────────────────────────────────────────────────────────
// Load request 0: the area's model PK4 to *addr.
// FF3 0x001f9c10.

void *RoomFileMdlLoadReq(MAP_ROOM_DAT *dat, int area, void **addr)
{
    (void)dat;
    return MapLoadSlotReq(RoomFileGetMdlPk4(area), 0, addr);
}

// ──────────────────────────────────────────────────────────────────────
// The data PK4's hit data (member 3) at the room's origin, and member 5
// to the room's slot of 0x3d46f0.
// FF3 0x001f9c48.

void RoomFileSetHit(MAP_ROOM_DAT *dat)
{
    RcbInit(dat->origin[0], dat->origin[2], MapLoadGetRoomData(dat, 3));
    RoomGlowDatSet(dat->room, MapLoadGetRoomData(dat, 5));
}

// FF3 0x001f9c98.
void *RoomFileGetAreaPos(MAP_ROOM_DAT *dat)
{
    u_char side = RoomFileGetSide(dat->area);

    return PhfGetFileData(MapLoadGetRoomData(dat, 4), side);
}

// ──────────────────────────────────────────────────────────────────────
// The model PK4 (request 0) and the data PK4 (request 1) from the room's
// load address on; the address after them.
// FF3 0x001f9ce0.

void *RoomFileLoadReq(int area, MAP_ROOM_DAT *dat)
{
    void *addr = EeToHost(dat->load_addr);

    dat->file[ROOM_FILE_MDL_PK4] = HostToEe(RoomFileMdlLoadReq(dat, area, &addr));
    dat->file[ROOM_FILE_DATA_PK4] = HostToEe(MapLoadSlotReq(ROS00_DATA_PK4 + area * 7, 1, &addr));
    RoomLoadAddrSetBase(dat->room, addr);
    return addr;
}

// FF3 0x001f9d58.
int RoomFileIsLoadEnd(void)
{
    return MapLoadSlotIsEnd(1);
}

// FF3 0x001f9d70.
void RoomFileLoadEnd(MAP_ROOM_DAT *dat)
{
    void *pos = RoomFileGetAreaPos(dat);
    int   room;

    PosFileForEach(pos, "area_pos", RoomFileAreaPosHook, dat);           /* rodata 35b900 */
    RoomFileSetHit(dat);
    room = dat->room;
    MapDrawLoadFurnAll(room, pos, RoomLoadAddrGetCur(room));
}

// ──────────────────────────────────────────────────────────────────────
// The model PK4 (request 0) and the side's obj_pack PHF (request 2); the
// address after them.
// FF3 0x001f9de8.

void *RoomObjPackLoadReq(int area, MAP_ROOM_DAT *dat)
{
    void *addr = EeToHost(dat->load_addr);

    dat->file[ROOM_FILE_MDL_PK4] = HostToEe(RoomFileMdlLoadReq(dat, area, &addr));
    dat->file[ROOM_FILE_OBJ_PACK] = HostToEe(MapLoadSlotReq(RoomFileGetObjPack(area), 2, &addr));
    RoomLoadAddrSetBase(dat->room, addr);
    return addr;
}

// FF3 0x001f9e70.
int RoomObjPackIsLoadEnd(void)
{
    return MapLoadSlotIsEnd(2);
}

// ──────────────────────────────────────────────────────────────────────
// The pack's last member is the data PK4; then the room's origin, its hit
// data and its furniture; the address after the furniture (MapLoadCallFurn
// makes it the room's next load address).
// FF3 0x001f9e88.

void *RoomObjPackLoadEnd(MAP_ROOM_DAT *dat)
{
    void *pack = dat->file[ROOM_FILE_OBJ_PACK] != 0 ? EeToHost(dat->file[ROOM_FILE_OBJ_PACK]) : nullptr;
    int   num  = PhfGetFileNum(pack);
    void *pos;
    int   room;

    dat->file[ROOM_FILE_DATA_PK4] = HostToEe(PhfGetFileData(pack, num - 1));
    pos = RoomFileGetAreaPos(dat);
    PosFileForEach(pos, "area_pos", RoomFileAreaPosHook, dat);           /* rodata 35b900 */
    RoomFileSetHit(dat);
    room = dat->room;
    return MapDrawInitFurn(room, pack, RoomLoadAddrGetCur(room));
}

// ──────────────────────────────────────────────────────────────────────
// The RegDat id lists: 16 slot ids, -1 unused.
// FF3 0x001f9f18.

int RegListCheck(const s_char *list, int arg)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        if (list[i] != -1 && RegDatGetStPtr4Label(list[i], arg) != 0)
        {
            return 1;
        }
    }
    return 0;
}

// FF3 0x001f9f98.
void RegListExe(const s_char *list)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        if (list[i] != -1)
        {
            RegDatAddNoRegistList(list[i]);
        }
    }
}

// FF3 0x001f9ff8.
s_char RegListFindLabel(const s_char *list, int label)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        MB_OUT_HEAD *dat;

        if (list[i] != -1 && (dat = RegDatGetTopAddr(list[i])) != nullptr && dat->kai == label)
        {
            return list[i];
        }
    }
    return -1;
}

// FF3 0x001fa090 (hidden after RegListFindLabel; created 2026-10-15).
void RegListSetOffset(const s_char *list, float x, float y, float z)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        if (list[i] != -1)
        {
            RegDatSetOffset(list[i], x, y, z);
        }
    }
}

// FF3 0x001fa120.
int RegListGetBuffID(const s_char *list, int floor, float *pos)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        if (list[i] != -1 && RegDatCheckBuffPos(list[i], floor, pos) == 1)
        {
            return 1;
        }
    }
    return 0;
}

// FF3 0x001fa1b8.
void RegListClear(s_char *list)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        list[i] = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Registers every member of pk4 with RegDat, its slot id into the list,
// and moves it to the offset.
// FF3 0x001fa1e8.

void RegListRegist(void *pk4, const float *ofs, s_char *list)
{
    int num = 0;

    RegListClear(list);
    if (pk4 != nullptr)
    {
        num = ((PK4_HEADER *)pk4)->num;
    }
    for (int i = 0; i < num; i++)
    {
        MB_OUT_HEAD *dat = (MB_OUT_HEAD *)Pk4GetData(pk4, i);
        s_char       id  = (s_char)RegDatRegist(dat);

        list[i] = id;
        RegDatSetOffset(id, ofs[0], ofs[1], ofs[2]);
    }
}

// FF3 0x001fa2a0.
void RegListDelete(s_char *list)
{
    for (int i = 0; i < REG_LIST_NUM; i++)
    {
        if (list[i] != -1)
        {
            RegDatDeleteBuffList(list[i]);
            list[i] = -1;
        }
    }
}
