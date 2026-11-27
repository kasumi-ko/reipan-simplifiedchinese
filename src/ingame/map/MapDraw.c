// ingame/map/MapDraw.c -- FF3 0x117720..0x118900 (FF2 MapDraw.c, much
// changed): the room buffers' light sets, the furniture model loads, the
// registration of a room's area_pos objects, the room teardown and the room
// draw. FF2's own draw path (MapDrawRoomOne, MapDrawObj, the SGD coordinate
// helpers) is gone: a room is a ModelManager now (MAP_OBJ_MDL).

#include <string.h>                     // memset, strchr

#include "ingame/map/MapDraw.h"
#include "graphics/effect/effect_obj.h"         // EffectWaterFlowRegist / Delete, EffectLightComeIn*, EffThunderApplyLight
#include "graphics/graph3d/g3dLight.h"          // G3DLIGHT
#include "graphics/graph3d/gra3d.h"             // GRA3DLIGHTDATA, gra3dSetLightData ..
#include "graphics/graph3d/gra3dSGDData.h"      // sgdVerifyLightData
#include "graphics/mmanage.h"                   // ModelManager
#include "graphics/model_object.h"              // ModelObject::GetSgd
#include "ingame/ingame.h"                      // CharPlayerModel
#include "ingame/ingame_effect.h"               // IgEffectWaterFlowModelDraw, IgEffectLightComeInModelDraw
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/map/FurnLoad.h"                // the furniture records, FurnName*
#include "ingame/map/LightCtl.h"                // GetLightCtl, CLightCtl::Apply
#include "ingame/map/MapDBuff.h"                // MapSpEventPutVanish / Appear
#include "ingame/map/MapEff.h"                  // MapEffRegist, MapEffRoomRegist, MapLightRegOnBuff ..
#include "ingame/map/MapLBuff.h"                // MapLBuffIsDoorLoaded
#include "ingame/map/MapLight.h"                // MapLBuffDeleteRoom, MapLBuffLoadDoor
#include "ingame/map/MapLoad.h"                 // MAP_ROOM_DAT, MapLoadGetFreeMemAddr ..
#include "ingame/map/MapObjWrk.h"               // MapObjRegist, MapObjUpdate ..
#include "ingame/map/MapPhf.h"                  // PhfGetFileNum / Data / Head, PhfFindFileSize
#include "ingame/map/MapPut.h"                  // MapPutSet*, MapPutGet*
#include "ingame/map/MapRoomFile.h"             // RoomFileGetAreaPos, RegListDelete
#include "ingame/map/PosFile.h"                 // POS_FILE_ENT, PosFileGetNum / GetEnt
#include "ingame/map/RoomLoadAddr.h"            // RoomLoadAddrReset
#include "ingame/map/map_height.h"              // MhInitMapHeight
#include "ingame/photo/n_plyr_camera.h"         // PlyrCameraMapObjDraw
#include "reipan/gameplay/reipan_ots_geometry.h"   // PC: the camera collision's scenery
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_FLG_800000
#include "ingame/plyr/plyr_sub.h"               // AllPlyrIsReady
#include "ingame/map/MapDoor.h"      // door table, door motion pack
#include "ingame/story_door.h"                  // MapAreaSideGet, MapDoorModeIsOpen, MapDoorAreaGet
#include "reipan/reipan_memory.h"               // ReiPan_GetHostPointer
#include "system/eeiop/cddat.h"                 // GetFileSize
#include "system/os/eecdvd.h"                   // LoadReqGetAddr

// The light sets [bank][buffer]: bank 0 bss 0x37a4d0, bank 1 bss 0x37d0f0.
static GRA3DLIGHTDATA map_draw_light[2][2];

static GRA3DLIGHTDATA *map_draw_prelight[2];    // sdata 0x36b7f0: MapDrawPreLight's set per buffer
static int map_draw_flashlight_only = 1;        // sdata 0x36b7f8

// The default motion of the furniture models that have one (data 0x2efb58):
// model file, motion file (F004_PK4 -> F004_DEFAULT_PK4 ...).
static const int map_draw_furn_mot_tbl[26][2] =
{
    { 2787, 5300 }, { 2843, 5356 }, { 2913, 5426 }, { 2933, 5446 }, { 2875, 5388 },
    { 2921, 5434 }, { 2942, 5455 }, { 2944, 5457 }, { 2943, 5456 }, { 2964, 5477 },
    { 2958, 5471 }, { 2962, 5475 }, { 2963, 5476 }, { 2925, 5438 }, { 2959, 5472 },
    { 2960, 5473 }, { 2911, 5424 }, { 2961, 5474 }, { 2939, 5452 }, { 2788, 5301 },
    { 2790, 5303 }, { 2822, 5335 }, { 2814, 5327 }, { 2970, 5483 }, { 2827, 5340 },
    { 2899, 5340 },
};

#define MAP_DRAW_ZOFS_NEAR      30.0f   // sdata 0x36b800: furniture 0, 56, 109, 111, 119
#define MAP_DRAW_ZOFS_BACK   -1000.0f   // sdata 0x36b804: furniture 511
#define MAP_DRAW_ZOFS_DOOR   -3000.0f   // sdata 0x36b808: every door
#define MAP_DRAW_ZOFS_UNIT      10.0f   // sdata 0x36b80c: a "_zN" suffix's step

static float MapDrawDegToRad(float deg);

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00117720.

void MapDrawInit(void)
{
    FurnLoadInitRec();
}

// ──────────────────────────────────────────────────────────────────────
// The default motion file of a furniture model file, -1.
// FF3 0x00117740.

static int MapDrawGetFurnMotFile(int file)
{
    for (int i = 0; i < 26; i++)
    {
        if (map_draw_furn_mot_tbl[i][0] == file)
            return map_draw_furn_mot_tbl[i][1];
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Loads file at addr when it exists and is not empty; the address after it
// (LoadReqGetAddr), else addr.
// FF3 0x00117778.

static void *MapDrawLoadFile(int file, void *addr, int *id)
{
    if (GetFileSize(file) != (u_int)-1 && GetFileSize(file) != 0)
        return LoadReqGetAddr(file, addr, id);

    return addr;
}

// ──────────────────────────────────────────────────────────────────────
// The default motion of name's model, loaded at addr into its record; the
// address after it.
// FF3 0x001177f8.

static void *MapDrawLoadFurnMot(int room, const char *name, void *addr, int *id)
{
    int   file = MapDrawGetFurnMotFile(FurnNameGetFileNo(name));
    void *next = addr;

    if (file >= 0)
    {
        next = MapDrawLoadFile(file, addr, id);
        FurnLoadSetMotPk4(room, name, addr);
    }
    return next;
}

// ──────────────────────────────────────────────────────────────────────
// name's model loaded at addr and recorded, then its motion; the address
// after them (addr when the name has no model file).
// FF3 0x00117880 (FF2's MapDrawInitRegDat slot).

static void *MapDrawLoadFurn(int room, const char *name, void *addr, int *id)
{
    int   file = FurnNameGetFileNo(name);
    void *next;

    if (file < 0)
        return addr;

    next = MapDrawLoadFile(file, addr, id);
    FurnLoadAddRec(room, name, addr, (int)((u_char *)next - (u_char *)addr));
    return MapDrawLoadFurnMot(room, name, next, id);
}

// ──────────────────────────────────────────────────────────────────────
// The models of the area_pos objects ("<file>.<object>") that the obj_pack
// did not bring.
// FF3 0x00117940.
// callcheck: FUN_002a305c (strchr)

void *MapDrawLoadFurnAll(int room, void *pos_data, void *addr)
{
    int num = PosFileGetNum(pos_data);

    for (int i = 0; i < num; i++)
    {
        POS_FILE_ENT *ent  = PosFileGetEnt(pos_data, i);
        const char   *name = strchr(ent->name, '.') + 1;

        if (FurnLoadIsRec(room, name) == 0)
            addr = MapDrawLoadFurn(room, name, addr, nullptr);
    }
    return addr;
}

// ──────────────────────────────────────────────────────────────────────
// The obj_pack's furniture models are used where they lie: each member with
// a model file is recorded, and its motion loads from addr.
// FF3 0x00117a00.

void *MapDrawInitFurn(int room, void *pack, void *addr)
{
    int num = PhfGetFileNum(pack);

    for (int i = 0; i < num; i++)
    {
        void       *data = PhfGetFileData(pack, i);
        const char *name = PhfGetFileHead(pack, i)->name;
        int         size = PhfFindFileSize(pack, name);

        if (FurnNameGetFileNo(name) >= 0)
        {
            FurnLoadAddRec(room, name, data, size);
            addr = MapDrawLoadFurnMot(room, name, addr, nullptr);
        }
    }
    return addr;
}

// ──────────────────────────────────────────────────────────────────────
// One light set for both buffers (the scene / special-light code); nullptr
// gives them back their own.
// FF3 0x00117af8.

void MapDrawPreLight(GRA3DLIGHTDATA *light)
{
    for (int i = 0; i < 2; i++)
        map_draw_prelight[i] = MapLoadGetFreeMemAddr(i) != nullptr ? light : nullptr;
}

// FF3 0x00117b58.
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffBank(int buff, int bank)
{
    return &map_draw_light[bank != 0][buff & 1];
}

// FF3 0x00117b90.
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffID(int buff, int bank)
{
    GRA3DLIGHTDATA *light = map_draw_prelight[buff];
    MAP_ROOM_DAT   *dat;

    if (light == nullptr)
    {
        dat = MapLoadGetFreeMemAddr(buff);
        if (dat != nullptr && dat->area >= 0)
            light = &map_draw_light[bank != 0][buff & 1];
    }
    return light;
}

// FF3 0x00117c28.
GRA3DLIGHTDATA *MapDrawGetLightPtr(int area, int bank)
{
    int buff = MapLoadGetBuffID(area);

    if (buff < 0)
        return nullptr;

    return MapDrawGetLightPtr4BuffID(buff, bank);
}

// FF3 0x00117c70.
GRA3DLIGHTDATA *MapDrawGetLightPtrDoor(int area, int bank)
{
    if (MapDoorModeIsOpen() != 0)
        area = MapDoorAreaGet();

    return MapDrawGetLightPtr(area, bank);
}

// ──────────────────────────────────────────────────────────────────────
// A loaded buffer's room: its model from the model PK4, its light file
// (data member 0, member 6 on the other side when the data PK4 has it) at
// its origin into bank 0, the model prelit with it in monotone mode, and
// the map height (member 2).
// FF3 0x00117cc0.

int MapDrawInitRoom(MAP_ROOM_DAT *dat)
{
    int             room = dat->room;
    int             no   = 0;
    GRA3DLIGHTDATA *light;
    void           *height;

    const u_int model_addr = MapLoadGetFileAddr(dat, ROOM_FILE_MDL_PK4);
    if (model_addr != 0)
        dat->mdl.Build(ReiPan_GetHostPointer(model_addr), nullptr);

    if (MapAreaSideGet() == 1)
    {
        no = 6;
        if (MapLoadCheckRoomDataNum(dat) == 0)
            no = 0;
    }
    auto *light_file = (ZERO2LIGHTDATAFILE *)MapLoadGetRoomData(dat, no);
    if (light_file != nullptr)
        sgdVerifyLightData(&dat->light, light_file);
    else
        memset(&dat->light, 0, sizeof(dat->light));

    light = &map_draw_light[0][room];
    gra3dLightDataAddOffsetPosition(light, &dat->light, dat->origin);
    gra3dSetLightData(light, nullptr);
    if (dat->mdl.mot != nullptr && gra3dIsMonotoneDrawEnable() != 0)
        dat->mdl.mot->ExecPrelight(dat->origin, (float *)map_room_rot);
    gra3dSetLightData(light, nullptr);

    height = MapLoadGetRoomData(dat, 2);
    if (height != nullptr)
        MhInitMapHeight(height, dat->origin, room);

    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Puts every area_pos object of a buffer being shown: furniture, effect
// furniture, doors and effect marks, each at the room's origin plus its
// entry, turned by the entry's degrees; then the room's effects, lights and
// object states.
// FF3 0x00117de8.
// callcheck: FUN_002a305c (strchr), FUN_0016e0c8 (EffectLightComeInRegist)

void MapDrawRoomObjRegist(MAP_ROOM_DAT *dat)
{
    int     room = dat->room;
    void   *area_pos;
    int     num;
    float   pos[4];
    float   rot[4];
    MAP_PUT *put;
    SGDFILEHEADER *sgd;
    int     no;
    int     z;
    int     kind;

    gra3dSetLightData(&map_draw_light[0][room], nullptr);
    RoomLoadAddrReset(room);
    FurnLoadClearPrelit(room);

    area_pos = RoomFileGetAreaPos(dat);
    num = PosFileGetNum(area_pos);
    for (int i = 0; i < num; i++)
    {
        POS_FILE_ENT *ent  = PosFileGetEnt(area_pos, i);
        const char   *name = strchr(ent->name, '.') + 1;

        for (int k = 0; k < 4; k++)
            pos[k] = dat->origin[k] + ent->pos[k];
        rot[0] = MapDrawDegToRad(ent->rot[0]);
        rot[1] = MapDrawDegToRad(360.0f - ent->rot[1]);     // sdata 0x36b7fc
        rot[2] = MapDrawDegToRad(ent->rot[2]);
        rot[3] = 0.0f;

        put = nullptr;
        switch (FurnLoadGetAttr(name))
        {
        case FURN_ATTR_FURN0:
        case FURN_ATTR_FURN1:
        case FURN_ATTR_FURN2:
        case FURN_ATTR_FURN3:
        {
            u_int *flag;

            put  = MapObjRegist(room, name, pos, rot, ent->scale, nullptr);
            flag = MapPutGetFlag(put);
            if (put == nullptr)         // PC: FF3 goes on and writes through the null put
                break;
            switch (FurnNameGetNo(name))
            {
            case 0:
            case 56:
            case 109:
            case 111:
            case 119:
                MapPutSetZoffset(MAP_DRAW_ZOFS_NEAR, put);
                break;
            case 1:
            case 57:
            case 110:
            case 112:
            case 120:
                *flag |= 0x10000;
                break;
            case 49:
                MapPutSetDrawFunc(put, PlyrCameraMapObjDraw, 0, 0, 0);
                break;
            case 211:
                MapSpEventPutVanish(put);
                break;
            case 511:
                MapPutSetZoffset(MAP_DRAW_ZOFS_BACK, put);
                break;
            }
            break;
        }

        case FURN_ATTR_EFFECT:
            put = MapObjRegist(room, name, pos, rot, ent->scale, nullptr);
            // PC: when MapObjRegist fails FF3 reads through a null handle
            // (address 0); the effects ignore a null SGD.
            {
                MAP_OBJ_MDL *mdl = MapPutGetMdl(put);

                sgd = mdl != nullptr ? mdl->mot->ani_ctrl->GetSgd(0) : nullptr;
            }
            no  = FurnNameGetNo(name);
            if ((u_int)(no - 801) < 99)
            {
                int move = (no - 801) % 10;

                EffectWaterFlowRegist(sgd, room, move);
                MapPutSetWork(put, (void *)(intptr_t)move);
                MapPutSetDrawFunc(put, IgEffectWaterFlowModelDraw, 0, 0, 0);
            }
            else if ((u_int)(no - 900) < 100)
            {
                EffectLightComeInRegist(sgd, room, 0);
                MapPutSetWork(put, sgd);
                MapPutSetDrawFunc(put, IgEffectLightComeInModelDraw, 0x8000, 0, 0);
                MapPutSetFirst(put, -1);
            }
            break;

        case FURN_ATTR_DOOR:
            put = MapObjRegist(room, name, pos, rot, ent->scale, ReiPan_GetHostPointer(MapDoorAnimGetAddr()));
            if (put == nullptr)
            {
                // FF3 returns here: the rest of the objects, the room's
                // effects and lights and MAP_ROOM_DAT stat 4 are skipped.
                return;
            }
            MapPutSetZoffset(MAP_DRAW_ZOFS_DOOR, put);
            if (MapLBuffIsDoorLoaded(name) != 0)
                *MapPutGetFlag(put) |= 8;
            MapLBuffLoadDoor(room, name, put);
            if (FurnNameGetNo(name) == 72)
                MapSpEventPutAppear(put);
            break;

        case FURN_ATTR_EVENT:
            no = FurnNameGetNum(name, 1, 3);
            if (no >= 0)
                MapEffRegist(room, no, pos, ent->rot, FurnNameGetEventNo(name));
            break;
        }

        if (put == nullptr)
            continue;

        // The "_z" suffix: L / F set the draw order (put flag 0x8000 /
        // 0x400 with first id -z), N moves the put by z * 10.
        z = FurnNameGetZ(name, &kind);
        if (kind == 3)
            continue;

        if (kind == 0)
        {
            *MapPutGetFlag(put) |= 0x8000;
            MapPutSetFirst(put, (short)-z);
        }
        else if (kind == 2)
        {
            *MapPutGetFlag(put) |= 0x400;
            MapPutSetFirst(put, (short)-z);
        }
        else
        {
            MapPutSetZoffset((float)z * MAP_DRAW_ZOFS_UNIT, put);
        }
    }

    MapEffRoomRegist(room);
    MapLightRegOnBuff(room);
    MapObjUpdate(-1);
    MapObjSetActionRoom(room);
    MapEffVisibleUpdate();
    dat->stat |= 4;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x00118318.

void MapDrawDeleteNoDraw(MAP_ROOM_DAT *dat)
{
    int room = dat->room;

    MapLightRegOffBuff(room);
    MapLBuffDeleteRoom(room);
    FurnLoadClearFlag1(room);
    MapObjReleaseRoom(room);
    MapEffRoomDelete(room);
    EffectWaterFlowDelete(room);
    EffectLightComeInDeleteMapBuffId(room);
}

// FF3 0x00118370.
void MapDrawDeleteRoom(MAP_ROOM_DAT *dat)
{
    dat->mdl.Release();
    RegListDelete(dat->reg_list);
    MapDrawDeleteNoDraw(dat);
    FurnLoadDeleteRoom(dat->room);
}

// FF3 0x001183b0.
void MapDrawDeleteRoomAll(void)
{
    MapDrawDeleteRoom(MapLoadGetFreeMemAddr(0));
    MapDrawDeleteRoom(MapLoadGetFreeMemAddr(1));
}

// ──────────────────────────────────────────────────────────────────────
// The lights a room draws with: its bank 0 set through the light control
// and the lightning (EffThunderApplyLight), then the player's flashlight in light 38; the result is
// kept in bank 1 for the room's objects.
// FF3 0x001183e0.

static void MapDrawSetRoomLight(int room, int area)
{
    GRA3DLIGHTDATA *light;
    CLightCtl      *ctl;
    G3DLIGHT        fl;
    int             on;

    if (MapDrawIsEnableFlashlightOnly() == 0)
        return;

    gra3dLightEnableAll(0);
    light = &map_draw_light[0][room];
    ctl = GetLightCtl();
    if (ctl != nullptr)                 // PC: FF3 calls through a null control too (Apply then reads low memory)
        ctl->Apply(light, area);
    EffThunderApplyLight();
    gra3dSetAmbient(light->vAmbient);
    gra3dApplyLight();

    // PC: AllPlyrIsReady passes while the player model is not built yet
    // (CharPlayerModel's ctor 0x145c50 is a stub, plyr_wrk->mdl nullptr);
    // in FF3 a ready player always has its model.
    if (AllPlyrIsReady() == 0 || ingame_wrk.plyr_wrk->mdl == nullptr)
    {
        gra3dLightEnable(MAP_DRAW_LIGHT_PLAYER, 0);
        return;
    }

    fl = ((CharPlayerModel *)ingame_wrk.plyr_wrk->mdl)->fl;
    if (fl.Type == G3DLIGHT_SPOT)
    {
        fl.vDirection[0] = -fl.vDirection[0];
        fl.vDirection[1] = -fl.vDirection[1];
        fl.vDirection[2] = -fl.vDirection[2];
    }
    on = fl.Type != INVALID_G3DLIGHTTYPE;
    if (on)
        gra3dSetLight(MAP_DRAW_LIGHT_PLAYER, &fl);
    gra3dLightEnable(MAP_DRAW_LIGHT_PLAYER, on);

    map_draw_light[1][room] = map_draw_light[0][room];
    gra3dGetLightData(&map_draw_light[1][room]);
}

// ──────────────────────────────────────────────────────────────────────
// A buffer's room model: placed at its origin, lit, drawn (flags 0x200: the
// alpha pass).
// FF3 0x001185b8.

static void MapDrawRoom(MAP_ROOM_DAT *dat, u_int flags)
{
    ModelManager *mot;

    if (dat->area < 0)
        return;

    mot = dat->mdl.mot;
    if (mot == nullptr)
        return;

    dat->mdl.SetPosRot(dat->origin, map_room_rot);
    MapDrawSetRoomLight(dat->room, dat->area);
    mot->ReqDraw();
    if ((flags & 0x200) != 0)
        mot->DrawAlpha();
    else
        mot->Draw();
}

// ──────────────────────────────────────────────────────────────────────
// Both drawn buffers. The light selection parameter is on (1) unless the
// current area is 0x26, 0x27, 0x2a or 0x2b, the viewfinder is up or the
// player's _0e0 bit 0 is set.
// FF3 0x00118650.

void MapDrawRooms(u_int flags)
{
    PLYR_WRK *plyr;
    int       area;
    int       on;
    // PC: the rooms as drawn are what the over-the-shoulder camera collides
    // with; not the mirror pass (0x200), whose world is reflected.
    const int ots_scenery = (flags & 0x200) == 0;

    if (ots_scenery)
        ReiPan_OtsGeomBeginScenery();

    for (int i = 0; i < 2; i++)
    {
        if (MapLoadCheckDrawFlg(i) == 0)
            continue;

        area = MapLoadGetRoomNoNow();
        plyr = ingame_wrk.plyr_wrk;
        on = 0;
        if ((u_int)(area - 0x26) > 1 && area != 0x2a && area != 0x2b && (plyr->flags & PLYR_FLG_800000) == 0)
            on = (plyr->_0e0 & 1) ^ 1;

        gra3dSetDrawParam(on, 1.3f);    // sdata 0x36b810
        MapDrawRoom(MapLoadGetFreeMemAddr(i), flags);
    }
    gra3dSetDrawParam(0, 1.3f);

    if (ots_scenery)
        ReiPan_OtsGeomEndScenery();
}

// FF3 0x00118758.
void MapDrawEnableFlashlightOnly(int b)
{
    map_draw_flashlight_only = b;
}

// FF3 0x00118760.
int MapDrawIsEnableFlashlightOnly(void)
{
    return map_draw_flashlight_only;
}

// FF3 0x00118768.
void MapDrawLightReset(void)
{
    for (int i = 0; i < 2; i++)
    {
        map_draw_prelight[i] = nullptr;
        memset(&map_draw_light[0][i], 0, sizeof(GRA3DLIGHTDATA));
    }
    MapDrawEnableFlashlightOnly(1);
}

// FF3 0x001187d0.
void MapDrawRelease(void)
{
    MapDrawDeleteRoomAll();
}

// ──────────────────────────────────────────────────────────────────────
// Degrees wrapped into -180..180, in radians.
// FF3 0x001187f0.

static float MapDrawDegToRad(float deg)
{
    if (deg > 0.0f)
    {
        while (deg > 180.0f)            // sdata 0x36b814
            deg -= 360.0f;              // sdata 0x36b818
    }
    else
    {
        while (deg < -180.0f)           // sdata 0x36b81c
            deg += 360.0f;
    }
    return deg * 3.141592f / 180.0f;    // sdata 0x36b820
}
