/* ==========================================================================
 *  ingame/map/map_height.h
 *
 *  Map heights (FF2 map_height.c; FF3 0x001fd9b0..0x001fe3d8). A room's
 *  height file (member 2 of its data PK4) is a triangle mesh plus a uniform
 *  x / z grid over it: MhIsInArea picks the grid cell a position falls in and
 *  only that cell's triangles are tested. Two rooms are resident, one per
 *  room buffer (map_height_ctrl[0..1]).
 *
 *  Unlike FF2, FF3 works in world units: the in / out scale is (1, -1, -1, 1)
 *  (FF2: 1/25 in, 25 out), so a height comes back as -y of the mesh.
 *
 *  The file lives in emulated EE memory. As on the PS2, MhInitMapHeight
 *  turns its 32-bit file offsets into absolute EE addresses in place;
 *  map_height.c follows them with ReiPan_GetHostPointer.
 * ======================================================================== */
#ifndef _INGAME_MAP_MAP_HEIGHT_H
#define _INGAME_MAP_MAP_HEIGHT_H

#include <stddef.h>                     // offsetof
#include "sdk/scetypes.h"

// One mesh vertex.
// layout: EE (file data)
struct SgmVERTEX                        /* 0x10 */
{
    /* 0x00 */ float v[4];
};
static_assert(sizeof(SgmVERTEX) == 0x10, "SgmVERTEX");

// One triangle: three vertex indices (n[3] unused).
// layout: EE (file data)
struct SgmIDX                           /* 0x10 */
{
    /* 0x00 */ int n[4];
};
static_assert(sizeof(SgmIDX) == 0x10, "SgmIDX");

// One grid cell: the triangles registered to it.
// layout: EE (file data)
struct SgmAREA                          /* 0x08 */
{
    /* 0x00 */ int   num;
    /* 0x04 */ u_int e_idx;             // int[num] triangle indices (EE address after MhInitMapHeight)
};
static_assert(sizeof(SgmAREA) == 0x08, "SgmAREA");
static_assert(offsetof(SgmAREA, e_idx) == 0x04, "SgmAREA.e_idx");

// The file header, at offset 0 of the height file. The five run fields are
// file offsets on disc; MhInitMapHeight adds the file's EE address to each
// (and to every SgmAREA::e_idx).
// layout: EE (file data)
struct MhCtrl                           /* 0x40 */
{
    /* 0x00 */ int   inf[4];
    /* 0x10 */ int   w;                 // x grid divisions
    /* 0x14 */ int   h;                 // z grid divisions
    /* 0x18 */ int   v_count;
    /* 0x1c */ int   f_count;
    /* 0x20 */ u_int x;                 // float[w + 1] x grid lines
    /* 0x24 */ u_int z;                 // float[h + 1] z grid lines
    /* 0x28 */ u_int area;              // SgmAREA[w * h]
    /* 0x2c */ u_int v_idx;             // SgmIDX[f_count]
    /* 0x30 */ u_int V;                 // SgmVERTEX[v_count]
    /* 0x34 */ int   dmy[3];
};
static_assert(sizeof(MhCtrl) == 0x40, "MhCtrl");
static_assert(offsetof(MhCtrl, w) == 0x10, "MhCtrl.w");
static_assert(offsetof(MhCtrl, h) == 0x14, "MhCtrl.h");
static_assert(offsetof(MhCtrl, v_count) == 0x18, "MhCtrl.v_count");
static_assert(offsetof(MhCtrl, f_count) == 0x1c, "MhCtrl.f_count");
static_assert(offsetof(MhCtrl, x) == 0x20, "MhCtrl.x");
static_assert(offsetof(MhCtrl, z) == 0x24, "MhCtrl.z");
static_assert(offsetof(MhCtrl, area) == 0x28, "MhCtrl.area");
static_assert(offsetof(MhCtrl, v_idx) == 0x2c, "MhCtrl.v_idx");
static_assert(offsetof(MhCtrl, V) == 0x30, "MhCtrl.V");

void MhFirstInit(void);                     // FF3 0x001fd9b0: both room buffers empty (IngameInit)
void MhClearRoom(int room);
int  MhInitMapHeight(void *dat, float *ofs, int room);  // FF3 0x001fd9c0: room's height data from dat (its offsets made absolute), moved by ofs; 1, 0
int  MhSetOffset2(int room, float *ofs);    // FF3 0x001fdf80: moves room's height data by ofs (MapLoadSetOffSet); 1

int  MhGetMapHeight(float *pos_h, float *pos, int id, int limit_flg);    // FF3 0x001fda98: pos_h = pos on buffer id's ground; 0 none
int  MhHitLineCheck(float *pos1, float *pos2, int id);                  // FF3 0x001fdd70: 1 when the segment crosses the ground, else 0

#endif /* _INGAME_MAP_MAP_HEIGHT_H */
