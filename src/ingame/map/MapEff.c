// ingame/map/MapEff.c -- effects, point lights and load slots of the loaded
// rooms (FF3-only, 0x1f4788..0x1f5ba0; see MapEff.h).
#include <math.h>                       // sqrtf
#include <string.h>                     // memcpy

#include "ingame/map/MapEff.h"
#include "graphics/ecb.h"                    // EcbGetNum / EcbGetData
#include "ingame/map/MapCtrl.h"              // MapAreaHitLineCheck
#include "graphics/effect/effect.h"     // EffectRelease
#include "graphics/effect/effect_obj.h"
#include "graphics/effect/effect_oth.h"
#include "graphics/gs_reg.h"            // SetDefaultGsRegs
#include "ingame/map/FurnSave.h"
#include "ingame/map/MapLBuff.h"        // MapLBuffLoadVec
#include "ingame/map/MapLight.h"        // MapLBuffDeleteId
#include "ingame/map/MapLoad.h"         // MapLoadGetRoomArea, MapLoadGetBuffID, MapLoadGetBuffID4Label
#include "ingame/map/MapPut.h"
#include "ingame/map/RegDat.h"
#include "ingame/plyr/player.h"         // GetPlyrAreaNo, PlyrLineHitCheck
#include "ingame/plyr/plyr_mdl.h"       // PlyrLook*
#include "ingame/plyr/plyr_sub.h"       // PlyrGetJointPos
#include "system/eeiop/fileload.h"      // FileLoadCancel
#include "system/os/eecdvd.h"           // LoadReqGetAddr, IsLoadEnd

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@0011cea8

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

static MAP_EFF       map_eff[MAP_EFF_NUM];              // bss 0x3ef3b0
static MAP_LIGHT_REG map_light_reg[MAP_LIGHT_REG_NUM];  // bss 0x3f13b0
static int           map_load_slot[7];                  // bss 0x3f16b0
static int           map_eff_draw = 1;                  // sdata 0x370f5c (gp-0x2594)
static void         *map_eff_ret;                       // sdata 0x3734e8 (gp-8): MapEffCreate's result

static void MapEffClear(MAP_EFF *eff);

// ──────────────────────────────────────────────────────────────────────
// The first free row, now of buff; nullptr when all 128 are used.
// FF3 0x001f4788.

static MAP_EFF *MapEffAlloc(int buff)
{
    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff < 0)
        {
            eff->buff = buff;
            eff->no   = i;
            return eff;
        }
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001f47c8.

static void MapEffClear(MAP_EFF *eff)
{
    eff->eff  = nullptr;
    eff->buff = -1;
}

// ──────────────────────────────────────────────────────────────────────
// Put draw callbacks (MapPutDraw passes its flags, unused here): the lamp
// (type 4) keeps its number in the put, the others their effect.
// FF3 0x001f47e0, 0x001f4818.

static void MapEffDrawLamp(MAP_PUT *put, u_int flags)
{
    if (map_eff_draw != 0)
        EffLampDraw((int)(intptr_t)MapPutGetWork(put));
}

static void MapEffDrawFire(MAP_PUT *put, u_int flags)
{
    if (map_eff_draw != 0)
    {
        EffCandleFireDraw(MapPutGetWork(put));
        SetDefaultGsRegs();
    }
}

// ──────────────────────────────────────────────────────────────────────
// A put of buff that draws effect 'eff' (MapEffDrawFire).
// FF3 0x001f4850.

static MAP_PUT *MapEffPutSet(int buff, void *eff, float *pos)
{
    MAP_PUT *put = MapPutSetFuncObj(buff, MapEffDrawFire, pos, 0);

    MapPutSetWork(put, eff);
    MapPutSetZoffset(3000.0f, put);      /* sdata 0x370f60 */
    return put;
}

// ──────────────────────────────────────────────────────────────────────
// Make row no's effect by its type. Returns the address of a static result
// word that only some types write: for 200/201, 300..303, 400..403 and 600
// the caller reads what the last call left there (FF3 behaviour). 'action'
// and 'rot' are not read.
// FF3 0x001f48b0.

static void **MapEffCreate(int buff, int no, int type, int action, float *pos, float *rot)
{
    void *eff;

    (void)action;
    (void)rot;
    switch (type)
    {
    case 0:
    case 1:
        eff = EffCandleFireReq(0.5f, 0.5f, 0.7f, 0.0f, pos);    /* sdata 0x370f78, 0x370f7c */
        map_eff_ret = MapEffPutSet(buff, eff, pos);
        break;
    case 2:
        eff = EffCandleFireReq(0.7f, 0.5f, 1.0f, 0.0f, pos);    /* 0x370f7c, 0x370f78, 0x370f80 */
        map_eff_ret = MapEffPutSet(buff, eff, pos);
        break;
    case 3:
        eff = EffCandleFireReq(1.0f, 1.0f, 1.2f, 0.0f, pos);    /* 0x370f80, 0x370f84 */
        map_eff_ret = MapEffPutSet(buff, eff, pos);
        break;
    case 4:
    {
        EffLampReq(pos, no, 1);
        MAP_PUT *put = MapPutSetFuncObj(buff, MapEffDrawLamp, pos, 0);
        MapPutSetWork(put, (void *)(intptr_t)no);
        MapPutSetZoffset(200.0f, put);   /* sdata 0x370f88 */
        map_eff_ret = put;
        break;
    }
    case 100:
        eff = EffBlueFireReq(3.75f, 2.04f, 1.8f, 2.6f, pos);  /* sdata 0x370f64..0x370f70 */
        map_eff_ret = MapEffPutSet(buff, eff, pos);
        break;
    case 101:
        eff = EffBlueFireReq(3.75f, 2.04f, 1.8f, 2.0f, pos);  /* 0x370f74 */
        map_eff_ret = MapEffPutSet(buff, eff, pos);
        break;
    case 200:
        EffMapPart0Req(pos, no, MapLoadGetRoomArea(buff));
        break;
    case 201:
        EffMapPart1Req(pos, no, MapLoadGetRoomArea(buff));
        break;
    case 300: case 301: case 302: case 303:
        EffDripReq(pos, type - 300, no);
        break;
    case 400: case 401: case 402: case 403:
        EffDripFloorReq(pos, type - 400, no);
        break;
    case 500: case 501: case 502:
        map_eff_ret = EffKind1DMapReq(pos, type - 500);
        break;
    case 600:
        EffLanternReq(no, MapLoadGetRoomArea(buff));
        break;
    default:
        map_eff_ret = nullptr;
        break;
    }
    return &map_eff_ret;
}

// ──────────────────────────────────────────────────────────────────────
// Undo MapEffCreate for a row (*eff the row's result word): 0, -1 for an
// unknown type.
// FF3 0x001f4c48.

static int MapEffDelete(int type, int no, void **eff)
{
    if (*eff == nullptr)
        return 0;

    if (type == 201)
    {
        EffMapPart1Cut(no);
    }
    else if (type < 202)
    {
        if (type == 4)
        {
            EffLampCut(no);
        }
        else if ((type >= 0 && type < 4) || type == 100 || type == 101)
        {
            EffectRelease(MapPutGetWork((MAP_PUT *)*eff));
        }
        else if (type == 200)
        {
            EffMapPart0Cut(no);
            return 0;
        }
        else
        {
            return -1;
        }
        MpaPutDeleteOneObj((MAP_PUT *)*eff);
        *eff = nullptr;
    }
    else if (type < 404)
    {
        if (type >= 400)
        {
            EffDripFloorCut(no);
            return 0;
        }
        if ((u_int)(type - 300) < 4)
        {
            EffDripCut(no);
            return 0;
        }
        return -1;
    }
    else
    {
        if (type < 500)
            return -1;
        if (type > 502)
        {
            if (type == 600)
            {
                EffLanternCut(no);
                return 0;
            }
            return -1;
        }
        EffKind1DRelease(*eff);
        *eff = nullptr;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// A row for an effect object of buff; the effect is made right away unless
// a furniture row decides its visibility (MapEffVisibleUpdate).
// FF3 0x001f4d90.

void MapEffRegist(int buff, int type, const float *pos, const float *rot, int label)
{
    int area = MapLoadGetRoomArea(buff);

    if (area < 0)
        return;

    MAP_EFF *eff = MapEffAlloc(buff);
    if (eff == nullptr)
        return;

    eff->type  = type;
    eff->label = label;
    memcpy(eff->pos, pos, sizeof(eff->pos));
    memcpy(eff->rot, rot, sizeof(eff->rot));
    eff->furn = FurnSaveFind(area, FURN_SAVE_KIND_EFF, (short)type, label);
    if (eff->furn == nullptr)
    {
        eff->eff = *MapEffCreate(eff->buff, eff->no, eff->type, 0, eff->pos, eff->rot);
        if (eff->eff == nullptr)
            MapEffClear(eff);
    }
}

// ──────────────────────────────────────────────────────────────────────
// 1 when a row of buff has this type and label.
// FF3 0x001f4eb0.

static int MapEffCheckExist(int buff, int type, int label)
{
    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff == buff && eff->type == type && eff->label == label)
            return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Every effect object (FurnSave kind 0) of buff's area without a row yet
// gets one at its position.
// FF3 0x001f4f00.

void MapEffRoomRegist(int buff)
{
    int area = MapLoadGetRoomArea(buff);

    if (area < 0)
        return;

    void *ecb = FurnSaveGetBuf();
    int   num = EcbGetNum(ecb);
    for (int i = 0; i < num; i++)
    {
        FURN_SAVE_ROW *row = (FURN_SAVE_ROW *)EcbGetData(ecb, i);
        if (row->area != (short)area || row->kind != FURN_SAVE_KIND_EFF)
            continue;
        if (MapEffCheckExist(buff, row->id, row->label) != 0)
            continue;

        sceVu0FVECTOR rot = { 0.0f, 0.0f, 0.0f, 0.0f };
        sceVu0FVECTOR pos = { row->pos[0], row->pos[1], row->pos[2], 1.0f };  /* sdata 0x370f8c */
        MapEffRegist(buff, row->id, pos, rot, row->label);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Every row of buff loses its effect and is freed.
// FF3 0x001f5018.

void MapEffRoomDelete(int buff)
{
    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff == buff)
        {
            MapEffDelete(eff->type, eff->no, &eff->eff);
            MapEffClear(eff);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FurnSaveSetVisible on the furniture rows of buff's effects of type
// (9999: any). No callers.
// FF3 0x001f5088.

void MapEffSetVisible(int buff, int type, u_int mask, u_char val)
{
    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff == buff && eff->furn != nullptr && (type == 9999 || eff->type == type))
            FurnSaveSetVisible(eff->furn, mask, val);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Rows whose furniture went hidden lose their effect, shown ones without an
// effect get one.
// FF3 0x001f5138.

void MapEffVisibleUpdate(void)
{
    int mask = FurnSaveGetVisMask();

    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff < 0 || eff->furn == nullptr)
            continue;

        u_char vis = FurnSaveGetVisible(eff->furn, mask);
        if (vis == 0)
        {
            if (eff->eff != nullptr)
                MapEffDelete(eff->type, eff->no, &eff->eff);
        }
        else if (vis == 1 && eff->eff == nullptr)
        {
            eff->eff = *MapEffCreate(eff->buff, eff->no, eff->type, eff->furn->req_action,
                                     eff->pos, eff->rot);
            if (eff->eff == nullptr)
                MapEffClear(eff);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Player look row 5: the nearest shown lamp (type 4) within look 8's
// distance that the player's head can see becomes look 8's target.
// FF3 0x001f5228.

int MapEffPlyrLook(void)
{
    int            found = 0;
    float          dist  = PlyrLookGetDist(8);
    float          best  = 10000.0f;    /* sdata 0x370f90 */
    PLYR_LOOK_WRK *look  = PlyrLookGetWrk(8);
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = PlyrLookGetPrm(8);
    PlyrGetJointPos(2, head, nullptr);
    int mask = FurnSaveGetVisMask();

    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff < 0 || eff->furn == nullptr)
            continue;
        if (FurnSaveGetVisible(eff->furn, mask) != 1)
            continue;

        sceVu0FVECTOR p;
        memcpy(p, eff->pos, sizeof(p));
        if (eff->type != 4)
            continue;

        float dx = eff->pos[0] - head[0];
        float dy = eff->pos[1] - head[1];
        float dz = eff->pos[2] - head[2];
        float d  = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < dist && PlyrLineHitCheck(p) == 0
            && MapAreaHitLineCheck(p, head, GetPlyrAreaNo()) == 0 && d < best)
        {
            memcpy(tgt, p, sizeof(tgt));
            look->target = eff;
            found = 1;
            best  = d;
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(8) == 0)
        return 0;
    memcpy(look->pos, tgt, sizeof(look->pos));
    look->dist = best;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FURN_CANDLE_BLAZE_UP: the candles (types 0..3) of area's buffer with
// label (label < 0: all) flare up.
// FF3 0x001f5428.

void MapEffBlazeUp(int area, int label)
{
    int buff = MapLoadGetBuffID(area);

    for (int i = 0; i < MAP_EFF_NUM; i++)
    {
        MAP_EFF *eff = &map_eff[i];
        if (eff->buff != buff)
            continue;
        if (label >= 0 && eff->label != label)
            continue;
        if ((u_int)eff->type < 4 && eff->eff != nullptr)
        {
            void *e = MapPutGetWork((MAP_PUT *)eff->eff);
            if (e != nullptr)
                EffOthCandleFlameFlareUpReq(e);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// MapPlayStartReset: drawing on, every row freed (the effects themselves
// went with the rooms).
// FF3 0x001f54d8, 0x001f5528.

void MapEffResetAll(void)
{
    map_eff_draw = 1;
    for (int i = 0; i < MAP_EFF_NUM; i++)
        MapEffClear(&map_eff[i]);
}

void MapEffDeleteAll(void)
{
    MapEffRoomDelete(0);
    MapEffRoomDelete(1);
}

// ──────────────────────────────────────────────────────────────────────
// Save block 52.
// FF3 0x001f5548.

int MapLightRegSaveGet(void **addr)
{
    *addr = map_light_reg;
    return sizeof(map_light_reg);       // 0x300
}

// ──────────────────────────────────────────────────────────────────────
// A row's lights off / on (on: in buffer buff, from its label's RegDat
// vectors): 0, -1 no RegDat slot, -4 no data, -3 no vectors.
// FF3 0x001f5560, 0x001f55a8.

static void MapLightRegOff(MAP_LIGHT_REG *reg)
{
    if (reg->buff != -1)
    {
        MapLBuffDeleteId(reg->label, MAP_LBUFF_REG);
        reg->buff = -1;
    }
}

static int MapLightRegOn(int buff, MAP_LIGHT_REG *reg)
{
    if (reg->buff != -1)
        return 0;

    reg->buff = buff;
    int id = RegDatBuffID4Label(reg->label);
    if (id < 0)
        return -1;
    MB_OUT_HEAD *dat = RegDatGetTopAddr(id);
    if (dat == nullptr)
        return -4;
    if (RegDatVecFind4Label(id, reg->label) < 0)
        return -3;

    u_int flags = 2;
    if (reg->flags & 1)
        flags = 10;
    if (reg->flags & 2)
        flags |= 0x10;

    MB_OUT_RECT *rec;
    while ((rec = RegDatVecNextFind(id)) != nullptr)
        MapLBuffLoadVec(buff, reg->label, reg->label, nullptr, dat->kai, rec->vec, flags);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// By label.
// FF3 0x001f5698, 0x001f56f8.

static void MapLightRegOffLabel(int label)
{
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label == label)
            MapLightRegOff(&map_light_reg[i]);
    }
}

static void MapLightRegOnLabel(int label)
{
    int buff = MapLoadGetBuffID4Label(label);

    if (buff < 0)
        return;
    MapLightRegOffLabel(label);
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label == label)
            MapLightRegOn(buff, &map_light_reg[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// By buffer (a room shown / hidden).
// FF3 0x001f5778, 0x001f57f0, 0x001f5848.

void MapLightRegOffBuff(int buff)
{
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label != -1 && map_light_reg[i].buff == buff)
            MapLightRegOff(&map_light_reg[i]);
    }
}

static void MapLightRegOnBuffRow(int buff, MAP_LIGHT_REG *reg)
{
    if (buff == MapLoadGetBuffID4Label(reg->label))
        MapLightRegOn(buff, reg);
}

void MapLightRegOnBuff(int buff)
{
    MapLightRegOffBuff(buff);
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label != -1)
            MapLightRegOnBuffRow(buff, &map_light_reg[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// SET_EVENT_REC_HIT: a label in (once) / out.
// FF3 0x001f58b8, 0x001f5958.

void MapLightRegAdd(int label, u_char flags)
{
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label == label)
            return;
    }
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        MAP_LIGHT_REG *reg = &map_light_reg[i];
        if (reg->label == -1)
        {
            reg->flags = flags;
            reg->buff  = -1;
            reg->label = label;
            MapLightRegOnLabel(label);
            return;
        }
    }
}

void MapLightRegDel(int label)
{
    MapLightRegOffLabel(label);
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        if (map_light_reg[i].label == label)
            map_light_reg[i].label = -1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// After a load (labels from the save, nothing lit) / for a new game.
// FF3 0x001f59a8, 0x001f59e0.

void MapLightRegClear(void)
{
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
        map_light_reg[i].buff = -1;
}

void MapLightRegReset(void)
{
    for (int i = 0; i < MAP_LIGHT_REG_NUM; i++)
    {
        int label = map_light_reg[i].label;
        if (label != -1)
        {
            MapLightRegOffLabel(label);
            map_light_reg[i].label = -1;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Load slots.
// FF3 0x001f5a60, 0x001f5ab0, 0x001f5ab8, 0x001f5af0, 0x001f5b58.

void *MapLoadSlotReq(int file_no, int slot, void **addr)
{
    void *old = *addr;

    *addr = LoadReqGetAddr(file_no, old, &map_load_slot[slot]);
    // A table label consumes no storage. In that case the next file can
    // start at old, so it must not be exposed as this file's address.
    return map_load_slot[slot] >= 0 ? old : nullptr;
}

void MapLoadHookNop(void)
{
}

void MapLoadSlotReset(void)
{
    for (int i = 0; i < 7; i++)
        map_load_slot[i] = -1;
}

void MapLoadSlotCancelAll(void)
{
    for (int i = 0; i < 7; i++)
    {
        if (map_load_slot[i] != -1)
        {
            FileLoadCancel(map_load_slot[i], nullptr, nullptr);
            map_load_slot[i] = -1;
        }
    }
}

int MapLoadSlotIsEnd(int slot)
{
    if (map_load_slot[slot] != -1)
        return IsLoadEnd(map_load_slot[slot]);
    return 1;
}
