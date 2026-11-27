/* ==========================================================================
 *  ingame/map/MapEff.h
 *
 *  Effects, point lights and load slots attached to the loaded rooms
 *  (FF3-only, 0x1f4788..0x1f5ba0; FF2's kaza_pzl.c link slot holds it).
 *
 *  - Map effects (bss 0x3ef3b0, 128 rows of 0x40): every effect object of a
 *    room ("e<type>" rows of FurnSave with kind 0) gets a row when the room
 *    is shown (MapEffRoomRegist); the row makes the effect (candle flames,
 *    lamps, deform parts, ...) while its furniture row is visible.
 *  - Point-light labels (bss 0x3f13b0, 64 rows of 12, save block 52):
 *    SET_EVENT_REC_HIT turns the RegDat vectors of a label into point lights.
 *  - Load slots (bss 0x3f16b0, 7 words): the load ids of the room files.
 * ======================================================================== */
#ifndef _INGAME_MAP_MAPEFF_H
#define _INGAME_MAP_MAPEFF_H

#include <stddef.h>                     // offsetof
#include "sdk/scetypes.h"
#include "sdk/vu0_intrin.h"             // sceVu0FVECTOR

struct FURN_SAVE_ROW;                   // ingame/map/FurnSave.h

#define MAP_EFF_NUM 128

// One map effect, 0x40 bytes on the EE.
// layout: host -- eff and furn are EE pointers (4 bytes at +0x10 / +0x14);
// pos and rot are quadwords at +0x20 / +0x30 on both.
struct MAP_EFF
{
    /* 0x00 */ int            no;       // the row index (MapEffAlloc); the effect families' number
    /* 0x04 */ int            buff;     // the map load buffer (0 / 1); -1 free
    /* 0x08 */ int            type;     // the object's number: 0..3 candles, 4 lamp, 100/101 deform parts, 200/201, 300..303, 400..403, 500..502, 600
    /* 0x0c */ int            label;    // FurnSave label
    /* 0x10 */ void          *eff;      // MapEffCreate's result: a MAP_PUT for 0..4 / 100..101, the effect for 500..502
    /* 0x14 */ FURN_SAVE_ROW *furn;     // its furniture row; nullptr: always shown
    /* 0x20 */ sceVu0FVECTOR  pos;
    /* 0x30 */ sceVu0FVECTOR  rot;
};
static_assert(offsetof(MAP_EFF, label) == 0x0c, "MAP_EFF.label");
static_assert(offsetof(MAP_EFF, pos) == 0x20, "MAP_EFF.pos");
static_assert(offsetof(MAP_EFF, rot) == 0x30, "MAP_EFF.rot");

#define MAP_LIGHT_REG_NUM 64

// One point-light label row (12 bytes); the table is save block 52.
struct MAP_LIGHT_REG                    /* 0xc */
{
    /* 0x00 */ int    buff;             // the buffer it is lit in, -1 off
    /* 0x04 */ int    label;            // RegDat label; -1 free
    /* 0x08 */ u_char flags;            // SET_EVENT_REC_HIT kind: bit 0 / bit 1 -> light flags 10 / | 0x10
    /* 0x09 */ u_char _09[3];
};
static_assert(sizeof(MAP_LIGHT_REG) == 0xc, "MAP_LIGHT_REG is 0xc bytes");
static_assert(offsetof(MAP_LIGHT_REG, label) == 0x04, "MAP_LIGHT_REG.label");
static_assert(offsetof(MAP_LIGHT_REG, flags) == 0x08, "MAP_LIGHT_REG.flags");

// Map effects.
void  MapEffRoomRegist(int buff);       // FF3 0x001f4f00: MapDrawRoomObjRegist
void  MapEffRoomDelete(int buff);       // FF3 0x001f5018: MapDrawDeleteNoDraw
void  MapEffSetVisible(int buff, int type, u_int mask, u_char val);    // FF3 0x001f5088 (type 9999: any; no callers)
void  MapEffVisibleUpdate(void);        // FF3 0x001f5138: SET_EFF_VISIBLE, MapDrawRoomObjRegist
int   MapEffPlyrLook(void);             // FF3 0x001f5228: player look row 8 (data 0x330a80)
void  MapEffBlazeUp(int area, int label);   // FF3 0x001f5428: FURN_CANDLE_BLAZE_UP (label < 0: all)
void  MapEffResetAll(void);             // FF3 0x001f54d8: MapPlayStartReset
void  MapEffDeleteAll(void);            // FF3 0x001f5528: MapReleaseAll

// Point-light labels.
int   MapLightRegSaveGet(void **addr);  // FF3 0x001f5548: {the table, 0x300} (block 52)
void  MapLightRegOffBuff(int buff);     // FF3 0x001f5778: MapDrawDeleteNoDraw
void  MapLightRegOnBuff(int buff);      // FF3 0x001f5848: MapDrawRoomObjRegist
void  MapLightRegAdd(int label, u_char flags);  // FF3 0x001f58b8: SET_EVENT_REC_HIT 1
void  MapLightRegDel(int label);        // FF3 0x001f5958: SET_EVENT_REC_HIT 0
void  MapLightRegClear(void);           // FF3 0x001f59a8: IngameLoadRestore
void  MapLightRegReset(void);           // FF3 0x001f59e0: IngameInit, IngameNewGameReset, MapReleaseAll

// Load slots.
void *MapLoadSlotReq(int file_no, int slot, void **addr);  // FF3 0x001f5a60: returns the file's load address, or null when it was not queued
void  MapLoadHookNop(void);             // FF3 0x001f5ab0: empty (MapLoadRegistReq, MapLoadWaitMain)
void  MapLoadSlotReset(void);           // FF3 0x001f5ab8: every slot -1
void  MapLoadSlotCancelAll(void);       // FF3 0x001f5af0 (no callers)
int   MapLoadSlotIsEnd(int slot);       // FF3 0x001f5b58: 1 when done or empty

void  MapEffRegist(int buff, int type, const float *pos, const float *rot, int label);  // FF3 0x001f4d90: an "e" object of buff (MapDrawRoomObjRegist)

#endif /* _INGAME_MAP_MAPEFF_H */
