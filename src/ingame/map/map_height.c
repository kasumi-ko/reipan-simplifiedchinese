// ingame/map/map_height.c -- map heights: the resident height meshes of the two
// room buffers and the ground / segment queries over them.
// FF3 0x001fd9b0..0x001fe3d8; FF2 map_height.c (MioPan port), in FF3's order.
//
// FF3 against FF2: the in / out scale is (1, -1, -1, 1) (FF2: 1/25 in, 25
// out), so MhGetMapHeight hands back -height with no multiply; FF2's debug
// MhDrawHeight is gone; MhHitLineCheck scales by the same file-local vector
// (inline vmul) where FF2 used g_vConvertPS2SI.
//
// PC: height files live in emulated EE memory (the room's data PK4).
// MhInitMapHeight stores absolute EE addresses in the header and the cells,
// as the ROM does, and the Mh*Top helpers below follow them.

#include "ingame/map/map_height.h"

#include <stdio.h>                              // printf

#include "ingame/map/hit_check_base.h"          // HcBaseIsInTriXZ, HcBaseIsLineHitFace, HcBaseIsNearSegTri
#include "reipan/reipan_memory.h"               // PC: ReiPan_GetHostPointer, ReiPan_GetPs2Address
#include "sdk/libvu0.h"

// FF3 0x32b490: world -> mesh units (FF2 mh_l_scale_vec, 0.04 / -0.04 / -0.04).
alignas(16) static float mh_l_scale_vec[4] = { 1.0f, -1.0f, -1.0f, 1.0f };

static MhCtrl *map_height_ctrl[2];             /* sbss gp+0x50 (0x373540) */

static int     MhSetCtrlP(void *addr, int id);
static MhCtrl *MhGetCtrlP(int id);
static int     MhSetOffset(MhCtrl *mh_ctrl, float *in_offset);
static int     MhIsInArea(MhCtrl *mh_ctrl, float *pos);
static int     MhIsInTriXZ(MhCtrl *mh_ctrl, float *pos, int idx);
static float   MhIsHeightTri(MhCtrl *mh_ctrl, float *pos, int idx);
static int     MhIsHitHeightTri(MhCtrl *mh_ctrl, float *pos1, float *pos2, int idx);

// PC: the header's runs, from the EE addresses MhInitMapHeight stored.
static float *MhXTop(MhCtrl *mh_ctrl)
{
    return (float *)ReiPan_GetHostPointer(mh_ctrl->x);
}

static float *MhZTop(MhCtrl *mh_ctrl)
{
    return (float *)ReiPan_GetHostPointer(mh_ctrl->z);
}

static SgmAREA *MhAreaTop(MhCtrl *mh_ctrl)
{
    return (SgmAREA *)ReiPan_GetHostPointer(mh_ctrl->area);
}

static SgmIDX *MhIdxTop(MhCtrl *mh_ctrl)
{
    return (SgmIDX *)ReiPan_GetHostPointer(mh_ctrl->v_idx);
}

static SgmVERTEX *MhVertexTop(MhCtrl *mh_ctrl)
{
    return (SgmVERTEX *)ReiPan_GetHostPointer(mh_ctrl->V);
}

static int *MhAreaEIdx(SgmAREA *area)
{
    return (int *)ReiPan_GetHostPointer(area->e_idx);
}

// ──────────────────────────────────────────────────────────────────────
// Both room buffers empty. Called once from IngameInit, not per room.
// FF3 0x001fd9b0.

void MhFirstInit(void)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        map_height_ctrl[i] = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Binds the height file at dat to room buffer id, makes its offsets
// absolute and moves it to the room's world position ofs. 1, or 0 for a
// bad buffer. FF3 0x001fd9c0.

int MhInitMapHeight(void *dat, float *ofs, int room)
{
    MhCtrl  *mh_ctrl;
    SgmAREA *area;
    u_int    base;
    int      i;
    int      n;

    if (MhSetCtrlP(dat, room) == 0)
    {
        return 0;
    }

    mh_ctrl = MhGetCtrlP(room);

    // PC: the offsets become EE addresses, so the file must be in EE memory.
    base = (u_int)ReiPan_GetPs2Address(mh_ctrl);
    if (base == 0)
    {
        printf("MhInitMapHeight: height file %p is not in EE memory\n", dat);
        MhSetCtrlP(nullptr, room);
        return 0;
    }

    n = mh_ctrl->w * mh_ctrl->h;
    mh_ctrl->x     += base;
    mh_ctrl->z     += base;
    mh_ctrl->area  += base;
    mh_ctrl->v_idx += base;
    mh_ctrl->V     += base;

    area = MhAreaTop(mh_ctrl);
    for (i = 0; i < n; i++)
    {
        area[i].e_idx += base;
    }

    MhSetOffset(mh_ctrl, ofs);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Ground height under pos in buffer id: pos_h = pos with y replaced; returns
// 1 when a triangle was found, 0 (pos_h = pos) when none, or 0 with pos_h
// untouched when the buffer is empty.
//
// With several floors over the same spot the pick is not "highest wins":
// candidates more than 20 units above the query point are a separate, worse
// class than those below it, so a surface overhead never beats the floor
// underfoot -- but when every candidate is far above, the highest of them is
// taken. limit_flg clamps the result to +-2 units of the query height (the
// walk code keeps a step from teleporting the character). FF3 0x001fda98.

int MhGetMapHeight(float *pos_h, float *pos, int id, int limit_flg)
{
    MhCtrl  *mh_ctrl;
    SgmAREA *area;
    int      i;
    int      p_area;
    int      idx;
    int      flg;
    float    height;
    float    max_height;
    alignas(16) float scale_pos[4];

    flg        = 0;
    max_height = 0.0f;

    mh_ctrl = MhGetCtrlP(id);
    if (mh_ctrl == nullptr)
    {
        return 0;
    }

    sceVu0MulVector(scale_pos, pos, mh_l_scale_vec);

    p_area = MhIsInArea(mh_ctrl, scale_pos);
    if (p_area == -1)
    {
        sceVu0CopyVector(pos_h, pos);
        return 0;
    }

    for (i = 0; i < MhAreaTop(mh_ctrl)[p_area].num; i++)
    {
        area = &MhAreaTop(mh_ctrl)[p_area];
        idx  = MhAreaEIdx(area)[i];
        if (MhIsInTriXZ(mh_ctrl, scale_pos, idx) == 0)
        {
            continue;
        }

        height = MhIsHeightTri(mh_ctrl, scale_pos, idx);
        if (flg == 0)
        {
            max_height = height;
            flg        = 1;
        }
        else if (max_height - scale_pos[1] < 20.0f)
        {
            // Both near: highest wins. A far candidate cannot displace a near one.
            if (height - scale_pos[1] < 20.0f && max_height < height)
            {
                max_height = height;
            }
        }
        else if (20.0f < max_height - scale_pos[1])
        {
            // The pick is far above: a near candidate replaces it outright,
            // another far one only if it is higher.
            if (20.0f < height - scale_pos[1])
            {
                if (max_height < height)
                {
                    max_height = height;
                }
            }
            else
            {
                max_height = height;
            }
        }
    }

    sceVu0CopyVector(pos_h, pos);
    if (flg != 0)
    {
        if (limit_flg != 0)
        {
            if (2.0f < max_height - scale_pos[1])
            {
                max_height = scale_pos[1] + 2.0f;
            }
            else if (max_height - scale_pos[1] < -2.0f)
            {
                max_height = scale_pos[1] - 2.0f;
            }
        }
        pos_h[1] = -max_height;
    }
    return flg;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the segment pos1-pos2 crosses the ground of buffer id. Brute force
// over every triangle; the grid is not used. FF3 0x001fdd70 (the scale is an
// inline vmul in the ROM).

int MhHitLineCheck(float *pos1, float *pos2, int id)
{
    MhCtrl *mh_ctrl;
    int     i;
    alignas(16) float scale_pos1[4];
    alignas(16) float scale_pos2[4];

    mh_ctrl = MhGetCtrlP(id);
    if (mh_ctrl != nullptr)
    {
        sceVu0MulVector(scale_pos1, pos1, mh_l_scale_vec);
        sceVu0MulVector(scale_pos2, pos2, mh_l_scale_vec);

        for (i = 0; i < mh_ctrl->f_count; i++)
        {
            if (MhIsHitHeightTri(mh_ctrl, scale_pos1, scale_pos2, i) != 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001fde30. The ROM keeps the file's EE address; PC: its host pointer.

static int MhSetCtrlP(void *addr, int id)
{
    if ((u_int)id < 2)
    {
        map_height_ctrl[id] = (MhCtrl *)addr;
        return 1;
    }
    return 0;
}

void MhClearRoom(int room)
{
    MhSetCtrlP(nullptr, room);
}

// FF3 0x001fde60
static MhCtrl *MhGetCtrlP(int id)
{
    if ((u_int)id < 2)
    {
        return map_height_ctrl[id];
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// Moves the whole mesh by in_offset (world units). The grid lines move with
// it, so MhIsInArea stays valid. FF3 0x001fde88.

static int MhSetOffset(MhCtrl *mh_ctrl, float *in_offset)
{
    int        i;
    int        j;
    int        w;
    int        h;
    float     *x;
    float     *z;
    SgmVERTEX *V;
    alignas(16) float offset[4];

    sceVu0MulVector(offset, in_offset, mh_l_scale_vec);

    w = mh_ctrl->w;
    h = mh_ctrl->h;

    x = MhXTop(mh_ctrl);
    for (i = 0; i <= w; i++)
    {
        x[i] += offset[0];
    }

    z = MhZTop(mh_ctrl);
    for (i = 0; i <= h; i++)
    {
        z[i] += offset[2];
    }

    V = MhVertexTop(mh_ctrl);
    for (i = 0; i < mh_ctrl->v_count; i++)
    {
        for (j = 0; j < 3; j++)
        {
            V[i].v[j] += offset[j];
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Moves the room already resident in buffer id. No null test (the ROM's): a
// buffer with no height file faults; MapLoadSetOffSet is its only caller.
// FF3 0x001fdf80.

int MhSetOffset2(int id, float *in_offset)
{
    return MhSetOffset(MhGetCtrlP(id), in_offset);
}

// ──────────────────────────────────────────────────────────────────────
// Grid cell containing pos, or -1 outside the mesh. Each axis walks its grid
// lines until one overshoots; the i == w term makes the last line exclusive,
// so sitting exactly on the far edge lands on w + 1 and is rejected.
// FF3 0x001fdfb0.

static int MhIsInArea(MhCtrl *mh_ctrl, float *pos)
{
    float *x = MhXTop(mh_ctrl);
    float *z = MhZTop(mh_ctrl);
    int    w;
    int    h;
    int    i;
    int    j;

    w = mh_ctrl->w;
    h = mh_ctrl->h;

    for (i = 0; i <= w; i++)
    {
        if (pos[0] < x[i])
        {
            break;
        }
        if (i == w && pos[0] <= x[i])
        {
            break;
        }
    }

    for (j = 0; j <= h; j++)
    {
        if (pos[2] < z[j])
        {
            break;
        }
        if (j == h && pos[2] <= z[j])
        {
            break;
        }
    }

    if (i == 0 || i == w + 1 || j == 0 || j == h + 1)
    {
        return -1;
    }
    return (j - 1) * w + i - 1;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when pos is inside triangle idx seen from above. Triangles that do not
// face clearly up are rejected first (walls and ceilings stay out of the
// ground query). FF3 0x001fe080.
// callcheck: fptodp, dpcmp (the double compare below)

static int MhIsInTriXZ(MhCtrl *mh_ctrl, float *pos, int idx)
{
    alignas(16) float y_vec[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) float tri[3][4];
    alignas(16) float outer[4];
    alignas(16) float tmp1[4];
    alignas(16) float tmp2[4];
    int   idx_num[3];
    int   i;

    for (i = 0; i < 3; i++)
    {
        idx_num[i] = MhIdxTop(mh_ctrl)[idx].n[i];
        sceVu0CopyVector(tri[i], MhVertexTop(mh_ctrl)[idx_num[i]].v);
    }

    sceVu0SubVector(tmp1, tri[1], tri[0]);
    sceVu0SubVector(tmp2, tri[2], tri[0]);
    sceVu0OuterProduct(outer, tmp1, tmp2);

    // The threshold is a double in the ROM (0x3710b8, fptodp / dpcmp), so the
    // compare promotes -- 0.001f would not be the same test.
    if (sceVu0InnerProduct(outer, y_vec) < 0.001)
    {
        return 0;
    }
    return HcBaseIsInTriXZ(pos, tri[0], tri[1], tri[2]);
}

// ──────────────────────────────────────────────────────────────────────
// y of triangle idx's plane at pos (x / z). A triangle with no x / z area
// gives its first vertex's y. FF3 0x001fe1c0.

static float MhIsHeightTri(MhCtrl *mh_ctrl, float *pos, int idx)
{
    alignas(16) float v[3][4];
    int   i;
    float x1, y1, z1;
    float x2, y2, z2;
    float x3, y3, z3;
    float d;

    for (i = 0; i < 3; i++)
    {
        sceVu0CopyVector(v[i], MhVertexTop(mh_ctrl)[MhIdxTop(mh_ctrl)[idx].n[i]].v);
    }

    x1 = v[0][0];  y1 = v[0][1];  z1 = v[0][2];
    x2 = v[1][0];  y2 = v[1][1];  z2 = v[1][2];
    x3 = v[2][0];  y3 = v[2][1];  z3 = v[2][2];

    d = (z2 - z1) * (x3 - x1) - (z3 - z1) * (x2 - x1);
    if (d != 0.0f)
    {
        return (((y2 - y1) * (z3 - z1) - (y3 - y1) * (z2 - z1)) * (x1 - pos[0]) +
                ((x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1)) * (z1 - pos[2])) / d + y1;
    }
    return y1;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the segment pos1-pos2 pierces triangle idx: a bounding-box reject,
// the plane crossing, then the x / z test on the crossing point (which also
// applies the facing-up filter). FF3 0x001fe2e0.

static int MhIsHitHeightTri(MhCtrl *mh_ctrl, float *pos1, float *pos2, int idx)
{
    alignas(16) float v0[4];
    SgmVERTEX *V   = MhVertexTop(mh_ctrl);
    int       *riv = MhIdxTop(mh_ctrl)[idx].n;

    if (HcBaseIsNearSegTri(pos1, pos2, V[riv[0]].v, V[riv[1]].v, V[riv[2]].v) != 0 &&
        HcBaseIsLineHitFace(v0, pos1, pos2, V[riv[0]].v, V[riv[1]].v, V[riv[2]].v) != 0 &&
        MhIsInTriXZ(mh_ctrl, v0, idx) != 0)
    {
        return 1;
    }
    return 0;
}
