// ingame/plyr/plyr_mdl.c -- the player's look and viewfinder control, FF3
// 0x239ac0..0x23c028 (pinned 2026-10-15). FF2's plyr_mdl.c sits in this
// slot of the link order, but none of FF2's code is here: three weak FF2
// matches (plyr_mdlBankPlay, SetPlyrMdlNo, CostumeSetSave) were other code.
//
// The look: ten rows (data 0x330a00) each search one kind of target and
// leave it in their look work (bss 0x3f7830); PlyrLookMain takes the
// nearest target of the first priority that has one and feeds its position
// to the player model's watch control, which turns the head. A position set
// by PlyrLookPosSet (the camera NPC's head) overrides them all.
//
// The viewfinder: PlyrFinderModeStart / End move between the three finder
// flags (in, up, out; PLYR_FLG_HOLD) with the camera anime, PlayerMain runs
// the handlers while one is set, and the shutter control takes the shot
// (film, charge, lens shots) and hands a scored shot to phase 33. The shot
// judging itself (0x23c028..) is the next file's, ingame/plyr/plyr_shot.c.

#include <math.h>                       // sqrtf

#include "common/reipan_stub.h"
#include "ingame/plyr/plyr_mdl.h"

#include "ingame/camera/cam_rail.h"     // CamStanceGetTarget
#include "common/vu0math.h"             // RotLimitChk
#include "ingame/enemy/ene_main.h"     // EneAnyDmgReactBig
#include "ingame/camera/map_camera.h"   // ReqFinderInOverRap
#include "ingame/cat/cat_ctrl.h"        // CAT_WRK, CatGetWrk, CatViewInit
#include "ingame/char_model_sub.h"     // CharWatchControl
#include "ingame/cine_film.h"          // CineFlagWatchPhoto
#include "ingame/enemy/ene_main.h"      // ene_wrk, EneGetJointPos
#include "ingame/enemy/fly_ctrl.h"      // PhotoFlyChk
#include "ingame/ingame.h"              // CharPlayerModel, CHAR_MDL_CTRL, CHAR_WATCH_PRM
#include "ingame/ingame_wrk.h"          // ingame_wrk, title_start_flg
#include "ingame/item/prg/item.h"       // ItemGetCnt, ItemLost
#include "ingame/map/MapCtrl.h"         // MapAreaHitLineCheck
#include "ingame/map/MapEff.h"          // MapEffPlyrLook, MapEffVisibleUpdate
#include "ingame/map/MapLoad.h"         // MapLoadGetBuffID
#include "ingame/map/MapObjWrk.h"       // MapObjLookSearch, MapObjUpdate
#include "ingame/map/map_hit_check.h"   // MapHitLineCheck
#include "ingame/photo/finder_photo.h"  // GetPhotoModeWrk and its parts, PhotoModeSePlay
#include "ingame/photo/fndr_effect.h"   // PhotoWrk180Init, PhotoWrk288Init, PHOTO_WRK_1E8
#include "ingame/photo/fndr_parts.h"    // LENS_WRK, EneMarkInit, PhotoWrk274Init, PhotoGage4bcSnap, PhotoShakeReset, ...
#include "ingame/photo/n_plyr_camera.h" // GetPlyrCameraWrk
#include "ingame/plyr/player.h"         // PLYR_WRK, PLYR_FLG_*, SetPlyrAnime, PlyrSetMode, PlyrSePlay, ...
#include "ingame/plyr/plyr_sub.h"       // PlyrGetJointPos, PlyrSet280Req, PlyrTurnFlagClear
#include "ingame/plyr/unit_ctl.h"       // UnitGetTrgtRot, GetMidPos
#include "ingame/plyr/plyr_shot.h"      // the shot code after this file
#include "ingame/enemy/random_ghost.h" // RandGhostAppearReq
#include "ingame/story_menu.h"          // IsLockTblOn
#include "ingame/photo/photo_obj_dat.h" // PhotoObjGetEneSubject
#include "main/gphase.h"                // GetGPhaseCtrl
#include "sdk/libvu0.h"                 // sceVu0UnitMatrix, sceVu0RotMatrixX / Y, sceVu0TransMatrix, sceVu0ApplyMatrix
#include "system/os/system.h"           // GetFrameTimeScale, GetPALMode
#include "system/pad/pad.h"             // pad_wrk
#include "system/pad/pad_ctrl.h"        // pad_key, PAD_KEY_*, CheckPadTrgPlaySe, CheckPadNowPlaySe
#include "reipan/gameplay/reipan_mouse_actions.h"
#include "reipan/gameplay/reipan_over_shoulder.h"   // PC: the over-the-shoulder camera

// unassigned@00229478

// ──────────────────────────────────────────────────────────────────────
// The look rows' searches (data 0x330a00 holds their addresses).

static int PlyrLookSearchStick(void);
static int PlyrLookSearchPoint(void);
static int PlyrLookSearchDirect(void);
static int PlyrLookSearchEnemy(void);
static int PlyrLookSearchEnemySp(void);
static int PlyrLookSearchNpc(void);
static int PlyrLookSearchCat(void);
static int PlyrLookSearchSpot(void);

// data 0x330a00: priority, distance, time, search.
static PLYR_LOOK_PRM plyr_look_prm[PLYR_LOOK_NUM] =
{
    { 1, {},   0.0f,  -1.0f, PlyrLookSearchStick },     // 0x239e78: where the right stick looks
    { 1, {},   0.0f,  -1.0f, PlyrLookSearchPoint },     // 0x23a010: CamStanceGetTarget's point (PLYR_FLG_MODE10)
    { 2, {},   0.0f,  -1.0f, PlyrLookSearchDirect },    // 0x23a168: the event's direct look (time set by PlyrLookDirectSet)
    { 3, {}, 200.0f,  -1.0f, PlyrLookSearchEnemy },     // 0x23a258: an ordinary enemy
    { 3, {},   0.0f,  -1.0f, PlyrLookSearchEnemySp },   // 0x23a468: a special (type 2) enemy
    { 4, {},  40.0f, 150.0f, PlyrLookSearchNpc },       // 0x23a658: the camera NPCs
    { 4, {},  40.0f, 150.0f, PlyrLookSearchCat },       // 0x23a858: the cat
    { 5, {}, 100.0f, 150.0f, PlyrLookSearchSpot },      // 0x23aa28: the area's look spots
    { 5, {}, 100.0f,  -1.0f, MapEffPlyrLook },          // 0x1f5228: a lamp
    { 5, {}, 100.0f, 150.0f, MapObjLookSearch },        // 0x1f9750: a map object
};

static PLYR_LOOK_WRK  plyr_look_wrk[PLYR_LOOK_NUM];     // bss 0x3f7830
static sceVu0FVECTOR  plyr_look_direct_pos;             // bss 0x3f7a10 (PlyrLookDirectSet)
static float         *plyr_look_pos;                    // sbss gp-0x1948 (PlyrLookPosSet)

// data 0x330aa0: the watch control's parameters; pos is the target.
static CHAR_WATCH_PRM plyr_look_watch =
{
    { 0.0f, 0.0f, 0.0f, 1.0f },
    { 5.0f, 16.0f, 50.0f, 0.34906584f, 0.448798954f, 0.184799567f, 0.196349546f,
      0.523598790f, 0.392699093f, 0.628318548f, 0.314159274f, 0.0f },
};

// data 0x32fec0: row 7's spots, by area.
static const PLYR_LOOK_SPOT plyr_look_spot[90] =
{
    {  1, {}, { 93.0f, -22.0f, 162.0f, 1.0f } },
    {  1, {}, { 105.0f, -7.0f, 218.0f, 1.0f } },
    {  1, {}, { 89.0f, -9.0f, 381.0f, 1.0f } },
    {  2, {}, { -174.0f, -25.0f, 75.0f, 1.0f } },
    {  2, {}, { -174.0f, -39.0f, 170.0f, 1.0f } },
    {  2, {}, { -125.0f, -28.0f, 286.0f, 1.0f } },
    {  4, {}, { 95.0f, -103.0f, 634.0f, 1.0f } },
    {  4, {}, { 147.0f, -21.0f, 601.0f, 1.0f } },
    {  4, {}, { 51.0f, -31.0f, 630.0f, 1.0f } },
    {  4, {}, { 47.0f, -29.0f, 600.0f, 1.0f } },
    {  5, {}, { -91.0f, -89.0f, 191.0f, 1.0f } },
    {  5, {}, { -102.0f, -116.0f, 365.0f, 1.0f } },
    {  6, {}, { -4.0f, -46.0f, 633.0f, 1.0f } },
    {  6, {}, { 140.0f, -16.0f, 24.0f, 1.0f } },
    {  7, {}, { -2.0f, -7.0f, 885.0f, 1.0f } },
    {  7, {}, { 14.0f, -3.0f, 781.0f, 1.0f } },
    {  8, {}, { -16.0f, -98.0f, 1312.0f, 1.0f } },
    {  8, {}, { -98.0f, -28.0f, 1221.0f, 1.0f } },
    {  8, {}, { -36.0f, -21.0f, 1280.0f, 1.0f } },
    {  9, {}, { 8.0f, -13.0f, 1340.0f, 1.0f } },
    {  9, {}, { 110.0f, -79.0f, 1397.0f, 1.0f } },
    { 10, {}, { -311.0f, -58.0f, 986.0f, 1.0f } },
    { 10, {}, { -211.0f, -55.0f, 1167.0f, 1.0f } },
    { 10, {}, { -257.0f, -89.0f, 1274.0f, 1.0f } },
    { 11, {}, { 188.0f, -29.0f, 949.0f, 1.0f } },
    { 11, {}, { 124.0f, -25.0f, 831.0f, 1.0f } },
    { 11, {}, { 92.0f, -28.0f, 869.0f, 1.0f } },
    { 11, {}, { 247.0f, -84.0f, 927.0f, 1.0f } },
    { 11, {}, { 195.0f, -46.0f, 739.0f, 1.0f } },
    { 12, {}, { -138.0f, 42.0f, 817.0f, 1.0f } },
    { 13, {}, { 354.0f, -36.0f, 1275.0f, 1.0f } },
    { 14, {}, { -97.0f, -51.0f, 1458.0f, 1.0f } },
    { 15, {}, { 400.0f, -21.0f, 1165.0f, 1.0f } },
    { 15, {}, { 328.0f, -40.0f, 1217.0f, 1.0f } },
    { 15, {}, { 424.0f, -105.0f, 1130.0f, 1.0f } },
    { 16, {}, { 159.0f, -42.0f, 1184.0f, 1.0f } },
    { 16, {}, { 108.0f, -17.0f, 1155.0f, 1.0f } },
    { 16, {}, { 108.0f, -17.0f, 1226.0f, 1.0f } },
    { 17, {}, { -146.0f, -51.0f, 853.0f, 1.0f } },
    { 17, {}, { -130.0f, -34.0f, 794.0f, 1.0f } },
    { 17, {}, { -138.0f, -94.0f, 832.0f, 1.0f } },
    { 18, {}, { 317.0f, -180.0f, 1185.0f, 1.0f } },
    { 19, {}, { -201.0f, -53.0f, 1004.0f, 1.0f } },
    { 19, {}, { -166.0f, -55.0f, 933.0f, 1.0f } },
    { 19, {}, { -243.0f, -51.0f, 1068.0f, 1.0f } },
    { 25, {}, { -244.0f, -58.0f, 1598.0f, 1.0f } },
    { 25, {}, { -237.0f, -59.0f, 1543.0f, 1.0f } },
    { 25, {}, { -392.0f, -64.0f, 1586.0f, 1.0f } },
    { 24, {}, { -305.0f, -63.0f, 1712.0f, 1.0f } },
    { 22, {}, { -554.0f, -64.0f, 1972.0f, 1.0f } },
    { 22, {}, { -423.0f, -69.0f, 2065.0f, 1.0f } },
    { 23, {}, { -446.0f, -78.0f, 2208.0f, 1.0f } },
    { 23, {}, { -370.0f, -76.0f, 2168.0f, 1.0f } },
    { 23, {}, { -526.0f, -65.0f, 2203.0f, 1.0f } },
    { 21, {}, { -384.0f, -143.0f, 1975.0f, 1.0f } },
    { 21, {}, { -409.0f, -129.0f, 1868.0f, 1.0f } },
    { 27, {}, { -319.0f, -129.0f, 1944.0f, 1.0f } },
    { 27, {}, { -262.0f, -204.0f, 1899.0f, 1.0f } },
    { 20, {}, { -232.0f, -129.0f, 1939.0f, 1.0f } },
    { 28, {}, { -100.0f, -36.0f, 445.0f, 1.0f } },
    { 28, {}, { -59.0f, -34.0f, 339.0f, 1.0f } },
    { 28, {}, { -52.0f, -47.0f, 444.0f, 1.0f } },
    { 29, {}, { 515.0f, 46.0f, 495.0f, 1.0f } },
    { 29, {}, { 448.0f, 23.0f, 432.0f, 1.0f } },
    { 29, {}, { 577.0f, 17.0f, 469.0f, 1.0f } },
    { 30, {}, { 325.0f, -68.0f, 571.0f, 1.0f } },
    { 30, {}, { 368.0f, -78.0f, 549.0f, 1.0f } },
    { 31, {}, { 525.0f, -54.0f, 624.0f, 1.0f } },
    { 32, {}, { 416.0f, -45.0f, 928.0f, 1.0f } },
    { 33, {}, { 669.0f, -11.0f, 1035.0f, 1.0f } },
    { 33, {}, { 796.0f, -7.0f, 981.0f, 1.0f } },
    { 34, {}, { 621.0f, -8.0f, 1219.0f, 1.0f } },
    { 35, {}, { 768.0f, -25.0f, 1410.0f, 1.0f } },
    { 35, {}, { 719.0f, -45.0f, 1379.0f, 1.0f } },
    { 36, {}, { 737.0f, -34.0f, 1565.0f, 1.0f } },
    { 36, {}, { 805.0f, -35.0f, 1658.0f, 1.0f } },
    { 38, {}, { 166.0f, -12.0f, 1667.0f, 1.0f } },
    { 38, {}, { 112.0f, -103.0f, 1781.0f, 1.0f } },
    { 39, {}, { 109.0f, -131.0f, 1757.0f, 1.0f } },
    { 40, {}, { 122.0f, -199.0f, 2032.0f, 1.0f } },
    { 40, {}, { 88.0f, -191.0f, 1978.0f, 1.0f } },
    { 40, {}, { 47.0f, -54.0f, 1989.0f, 1.0f } },
    { 40, {}, { 181.0f, -54.0f, 1986.0f, 1.0f } },
    { 40, {}, { 116.0f, -31.0f, 2030.0f, 1.0f } },
    { 40, {}, { 112.0f, -42.0f, 2089.0f, 1.0f } },
    { 41, {}, { 80.0f, 38.0f, 2721.0f, 1.0f } },
    { 42, {}, { -48.0f, 230.0f, 3251.0f, 1.0f } },
    { 42, {}, { -81.0f, 760.0f, 3292.0f, 1.0f } },
    { 43, {}, { -105.0f, 760.0f, 3558.0f, 1.0f } },
    { 43, {}, { -281.0f, 695.0f, 4026.0f, 1.0f } },
};

static const sceVu0FVECTOR plyr_look_origin = { 0.0f, 0.0f, 0.0f, 1.0f };    // data 0x329500

// The xyz length of a - b (FF3 inlines it on VU0: vsub, vmul, vaddabc /
// vmaddbc, vsqrt).
static float PlyrLookDist(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void PlyrLookCopy(float *dst, const float *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

// ──────────────────────────────────────────────────────────────────────
// With a look position set, the watch control is on and looks there: 1.
// FF3 0x00239ac0

int PlyrLookPosApply(CHAR_MDL_CTRL *ctrl, CHAR_WATCH_PRM *prm)
{
    if (plyr_look_pos == nullptr)
        return 0;

    ctrl->off = 0;
    PlyrLookCopy(prm->pos, plyr_look_pos);
    ctrl->Update(prm);                  // FF3: through the control's vtable +0x08
    return 1;
}

// The first caller's position wins until it is cleared.
// FF3 0x00239b08

void PlyrLookPosSet(float *pos)
{
    if (plyr_look_pos != nullptr)
        return;

    ingame_wrk.plyr_wrk->flags |= PLYR_FLG_LOOK;
    plyr_look_pos = pos;
}

// FF3 0x00239b38

void PlyrLookPosClear(void)
{
    if (plyr_look_pos == nullptr)
        return;

    plyr_look_pos = nullptr;
    ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_LOOK;
}

// FF3 0x00239b68

PLYR_LOOK_PRM *PlyrLookGetPrm(int no)
{
    return &plyr_look_prm[(u_char)no];
}

// FF3 0x00239b88

PLYR_LOOK_WRK *PlyrLookGetWrk(int no)
{
    return &plyr_look_wrk[(u_char)no];
}

// Once the player's models are loaded: _2f4 cleared, the watch control
// off, no look position.
// FF3 0x00239ba8

void PlyrLookReset(PLYR_WRK *plyr)
{
    CharPlayerModel *mdl = static_cast<CharPlayerModel *>(plyr->mdl);

    plyr->_2f4[0] = 0;
    if (mdl != nullptr && mdl->ctrl != nullptr)     // PC: guard (the model classes are stubs)
        mdl->ctrl->off = 1;
    plyr_look_pos = nullptr;
}

// Whether look no may take its candidate now: always without a time limit;
// else the same target while its time runs, a new one once it ran out.
// FF3 0x00239c40

int PlyrLookTimeCheck(int no)
{
    PLYR_LOOK_WRK *look = &plyr_look_wrk[(u_char)no];

    if (plyr_look_prm[(u_char)no].time == -1.0f)
        return 1;

    if (look->target_old == look->target)
        return look->time > 0.0f;
    return look->time <= 0.0f;
}

// For the chosen look: a new target starts its time (prm frames over the
// frame scale); one frame off.
// FF3 0x00239cd0

void PlyrLookTimeStep(PLYR_LOOK_WRK *look)
{
    float t;

    if (look->target_old != look->target)
    {
        PLYR_LOOK_PRM *prm = look->prm;

        look->target_old = look->target;
        look->time = prm->time / GetFrameTimeScale();
    }

    t = look->time - 1.0f;
    look->time = (t >= 0.0f) ? t : 0.0f;
}

// PlyrLookTimeCheck and PlyrLookTimeStep in one for a target id (FF3 keeps
// the id in target_old). No caller in FF3.
// FF3 0x00239d50

int PlyrLookTimeCheckId(int no, int id, char on)
{
    PLYR_LOOK_WRK *look = &plyr_look_wrk[(u_char)no];
    void          *tgt  = reinterpret_cast<void *>(static_cast<intptr_t>(id));
    float          t;

    if (plyr_look_prm[(u_char)no].time == -1.0f)
        return on != 0;

    if (on == 0)
    {
        look->target_old = nullptr;
        return 0;
    }

    t = look->time - 1.0f;
    look->time = t = (t >= 0.0f) ? t : 0.0f;
    if (look->target_old == tgt)
        return t > 0.0f;
    if (t > 0.0f)
        return 0;

    look->target_old = tgt;
    look->time = plyr_look_prm[(u_char)no].time / GetFrameTimeScale();
    return 1;
}

// FF3 0x00239e58

float PlyrLookGetDist(int no)
{
    return plyr_look_prm[(u_char)no].dist;
}

// Row 0: with the light motion (flags 0x8000) and the right stick moved,
// a point 40 ahead of joint 7 along the stick's pitch / yaw.
// FF3 0x00239e78

static int PlyrLookSearchStick(void)
{
    PLYR_WRK      *plyr = ingame_wrk.plyr_wrk;
    PLYR_LOOK_WRK *look = &plyr_look_wrk[0];
    sceVu0FVECTOR  ahead = { 0.0f, 0.0f, 40.0f, 1.0f };
    sceVu0FMATRIX  m;
    sceVu0FVECTOR  out;
    sceVu0FVECTOR  eye;
    sceVu0FVECTOR  head;
    float          yaw;

    look->target     = nullptr;
    look->target_old = nullptr;
    look->prm        = &plyr_look_prm[0];

    if ((plyr->flags & 0x8000) == 0 || PlyrHasMove320() == 0)
        return 0;

    PlyrGetJointPos(7, eye, nullptr);
    PlyrGetJointPos(2, head, nullptr);
    yaw = plyr->look_yaw + plyr->rot[1];
    RotLimitChk(&yaw);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, plyr->look_pitch);
    sceVu0RotMatrixY(m, m, yaw);
    sceVu0TransMatrix(m, m, eye);
    sceVu0ApplyMatrix(out, m, ahead);

    if (PlyrLookTimeCheck(0) == 0)
        return 0;

    look->dist = PlyrLookDist(out, head);
    look->no   = 0;
    PlyrLookCopy(look->pos, out);
    return 1;
}

// Row 1: in mode 10's stance, the point CamStanceGetTarget gives.
// FF3 0x0023a010

static int PlyrLookSearchPoint(void)
{
    PLYR_LOOK_WRK *look = &plyr_look_wrk[1];
    sceVu0FVECTOR  pos;
    float         *p;

    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_MODE10) == 0 || (p = CamStanceGetTarget()) == nullptr)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    PlyrLookCopy(pos, p);

    if (PlyrLookTimeCheck(1) == 0)
        return 0;

    look->dist = 1.0f;
    look->no   = 1;
    PlyrLookCopy(look->pos, pos);
    return 1;
}

// SET_PLYR_LOOK_DIRECT: row 2 looks at pos for sec seconds.
// FF3 0x0023a0c8

void PlyrLookDirectSet(float *pos, short sec)
{
    int frames = sec * 30;

    if (GetPALMode() != 0)
        frames = sec * 25;

    plyr_look_prm[2].time    = (float)frames;
    plyr_look_wrk[2].target_old = nullptr;
    plyr_look_wrk[2].time    = (float)frames;
    PlyrLookCopy(plyr_look_direct_pos, pos);
}

// STOP_PLYR_LOOK_DIRECT
// FF3 0x0023a158

void PlyrLookDirectStop(void)
{
    plyr_look_wrk[2].time = 0.0f;
}

// Row 2: the event's direct look while its time runs.
// FF3 0x0023a168

static int PlyrLookSearchDirect(void)
{
    PLYR_LOOK_WRK *look = &plyr_look_wrk[2];
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  pos;

    look->prm = &plyr_look_prm[2];
    if (look->time <= 0.0f)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }

    PlyrGetJointPos(2, head, nullptr);
    PlyrLookCopy(pos, plyr_look_direct_pos);
    if (PlyrLookTimeCheck(2) == 0)
        return 0;

    look->dist = PlyrLookDist(pos, head);
    look->no   = 2;
    PlyrLookCopy(look->pos, pos);
    return 1;
}

// Row 3: the nearest acting ordinary enemy shown (alpha) whose head the
// player's head sees, within the row's distance.
// FF3 0x0023a258

static int PlyrLookSearchEnemy(void)
{
    PLYR_LOOK_WRK *look  = &plyr_look_wrk[3];
    int            area  = GetPlyrAreaNo();
    float          best  = 9999.0f;
    float          limit = PlyrLookGetDist(3);
    int            found = 0;
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = &plyr_look_prm[3];
    PlyrGetJointPos(2, head, nullptr);

    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK      *ene = &ene_wrk[i];
        sceVu0FVECTOR p;
        float         d;

        if (ene->state != ENE_STATE_ACT || ene->type == 2
            || (ene->flags2 & 0x10000400) == 0x400 || ene->alpha == 0)
        {
            continue;
        }

        EneGetJointPos(ene, 2, p, nullptr);
        d = PlyrLookDist(head, p);
        if (d < limit && PlyrLineHitCheck(p) == 0
            && MapAreaHitLineCheck(p, head, area) == 0 && d < best)
        {
            PlyrLookCopy(tgt, p);
            found = 1;
            best  = d;
            look->target = ene;
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(3) == 0)
        return 0;

    look->no = 3;
    PlyrLookCopy(look->pos, tgt);
    look->dist = best;
    return 1;
}

// Row 4: the nearest acting special enemy (type 2, flags2 0x10000000, not
// 0x400) shown and seen; no distance limit.
// FF3 0x0023a468

static int PlyrLookSearchEnemySp(void)
{
    PLYR_LOOK_WRK *look  = &plyr_look_wrk[4];
    int            area  = GetPlyrAreaNo();
    float          best  = 9999.0f;
    int            found = 0;
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = &plyr_look_prm[4];
    PlyrGetJointPos(2, head, nullptr);

    for (int i = 0; i < ENE_WRK_MAX; i++)
    {
        ENE_WRK      *ene = &ene_wrk[i];
        sceVu0FVECTOR p;
        float         d;

        if (ene->state != ENE_STATE_ACT || ene->type != 2 || (ene->flags2 & 0x400) != 0
            || (ene->flags2 & 0x10000000) == 0 || ene->alpha == 0)
        {
            continue;
        }

        EneGetJointPos(ene, 2, p, nullptr);
        d = PlyrLookDist(head, p);
        if (PlyrLineHitCheck(p) == 0 && MapAreaHitLineCheck(p, head, area) == 0 && d < best)
        {
            PlyrLookCopy(tgt, p);
            found = 1;
            best  = d;
            look->target = ene;
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(4) == 0)
        return 0;

    look->no = 4;
    PlyrLookCopy(look->pos, tgt);
    look->dist = best;
    return 1;
}

// Row 5: the nearest active, ready, drawn camera NPC's coordinate 2.
// FF3 0x0023a658

static int PlyrLookSearchNpc(void)
{
    PLYR_LOOK_WRK *look  = &plyr_look_wrk[5];
    int            area  = GetPlyrAreaNo();
    float          best  = 9999.0f;
    float          limit = PlyrLookGetDist(5);
    int            found = 0;
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = &plyr_look_prm[5];
    PlyrGetJointPos(2, head, nullptr);

    for (int i = 0; i < 2; i++)
    {
        PLYR_CAMERA_WRK *cam = GetPlyrCameraWrk((s_char)i);
        sceVu0FVECTOR    p;
        float            d;

        if (cam->active == 0 || cam->ready == 0 || cam->visible == 0 || cam->mdl == nullptr)
            continue;

        cam->mdl->GetCoordPos(p, 2, const_cast<float *>(plyr_look_origin));     // FF3: vtable +0x2c
        d = PlyrLookDist(head, p);
        if (d < limit && PlyrLineHitCheck(p) == 0
            && MapAreaHitLineCheck(p, head, area) == 0 && d < best)
        {
            PlyrLookCopy(tgt, p);
            found = 1;
            best  = d;
            look->target = cam;
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(5) == 0)
        return 0;

    look->no = 5;
    PlyrLookCopy(look->pos, tgt);
    look->dist = best;
    return 1;
}

// Row 6: the cat's coordinate 2 while its three look flags are set.
// FF3 0x0023a858

static int PlyrLookSearchCat(void)
{
    PLYR_LOOK_WRK *look  = &plyr_look_wrk[6];
    int            area  = GetPlyrAreaNo();
    float          best  = 9999.0f;
    float          limit = PlyrLookGetDist(6);
    int            found = 0;
    CAT_WRK       *cat;
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = &plyr_look_prm[6];
    PlyrGetJointPos(2, head, nullptr);

    cat = CatGetWrk(0);
    if (cat->active != 0 && cat->loaded != 0 && cat->visible != 0 && cat->mdl != nullptr)
    {
        sceVu0FVECTOR p;
        float         d;

        // FF3: the cat model's vtable +0x2c (its class is not traced;
        // GameModel's 0x1d2e90 as for the camera NPCs)
        cat->mdl->GetCoordPos(p, 2, const_cast<float *>(plyr_look_origin));
        d = PlyrLookDist(head, p);
        if (d < limit && PlyrLineHitCheck(p) == 0
            && MapAreaHitLineCheck(p, head, area) == 0 && d < best)
        {
            PlyrLookCopy(tgt, p);
            found = 1;
            best  = d;
            look->target = cat;
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(6) == 0)
        return 0;

    look->no = 6;
    PlyrLookCopy(look->pos, tgt);
    look->dist = best;
    return 1;
}

// Row 7: the nearest look spot of the player's area.
// FF3 0x0023aa28

static int PlyrLookSearchSpot(void)
{
    PLYR_LOOK_WRK *look  = &plyr_look_wrk[7];
    int            area  = GetPlyrAreaNo();
    float          best  = 9999.0f;
    float          limit = PlyrLookGetDist(7);
    int            found = 0;
    sceVu0FVECTOR  head;
    sceVu0FVECTOR  tgt;

    look->prm = &plyr_look_prm[7];
    PlyrGetJointPos(2, head, nullptr);

    for (int i = 0; i < 90; i++)
    {
        const PLYR_LOOK_SPOT *spot = &plyr_look_spot[i];
        sceVu0FVECTOR         p;
        float                 d;

        if (spot->area != area)
            continue;

        PlyrLookCopy(p, spot->pos);
        d = PlyrLookDist(head, spot->pos);
        if (d < limit && PlyrLineHitCheck(p) == 0
            && MapAreaHitLineCheck(p, head, area) == 0 && d < best)
        {
            PlyrLookCopy(tgt, p);
            found = 1;
            best  = d;
            look->target = const_cast<PLYR_LOOK_SPOT *>(spot);
        }
    }

    if (!found)
    {
        look->target_old = nullptr;
        look->target     = nullptr;
        return 0;
    }
    if (PlyrLookTimeCheck(7) == 0)
        return 0;

    look->no = 7;
    PlyrLookCopy(look->pos, tgt);
    look->dist = best;
    return 1;
}

// FF3 0x0023ac08

int PlyrLookIsEnable(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    return plyr->mode != PLYR_MODE_9 && (plyr->flags & PLYR_FLG_HOLD) == 0 && PlyrAnimeIsLookOff() == 0;
}

// Per frame outside the viewfinder (PlyrMdlMain): a set look position, else
// the nearest target of the first priority (1..5) with one; lock 3 only
// lets the event's direct look (row 2) through. The chosen target's time
// runs, its position goes to the watch control (off when it is beyond the
// control's reach, CharWatchControl::IsInRange) and PLYR_FLG_LOOK is set; without one the
// control is off and the flag cleared.
// FF3 0x0023ac68

void PlyrLookMain(void)
{
    PLYR_WRK        *plyr = ingame_wrk.plyr_wrk;
    CharPlayerModel *mdl  = static_cast<CharPlayerModel *>(plyr->mdl);
    CHAR_MDL_CTRL   *ctrl;
    PLYR_LOOK_WRK   *sel  = nullptr;
    float            best = 9999.0f;
    sceVu0FVECTOR    rot;

    if (mdl == nullptr || (ctrl = mdl->ctrl) == nullptr)    // PC: guard (the model classes are stubs)
        return;

    if (PlyrLookPosApply(ctrl, &plyr_look_watch))
        return;

    if (PlyrLookIsEnable() == 0)
    {
        ctrl->off = 1;
    }
    else
    {
        for (u_int prio = 1; ; prio++)
        {
            for (int i = 0; i < PLYR_LOOK_NUM; i++)
            {
                PLYR_LOOK_PRM *prm = &plyr_look_prm[i];

                if (prio == prm->prio && prm->func != nullptr && prm->func() != 0
                    && plyr_look_wrk[i].dist < best)
                {
                    best = plyr_look_wrk[i].dist;
                    sel  = &plyr_look_wrk[i];
                }
            }
            if (sel != nullptr || prio + 1 > 5)
                break;
        }

        if (sel != nullptr && (IsLockTblOn(3) == 0 || sel->no == 2))
        {
            PlyrLookTimeStep(sel);
            ctrl->off = 0;
            PlyrLookCopy(plyr_look_watch.pos, sel->pos);
            plyr->flags |= PLYR_FLG_LOOK;
            UnitGetTrgtRot(plyr->pos, plyr_look_watch.pos, rot, 2);
            rot[1] -= plyr->rot[1];
            RotLimitChk(&rot[1]);
            if (ctrl->IsInRange(&plyr_look_watch, rot[1]) == 0)
            {
                ctrl->off = 1;
                ctrl->Update(&plyr_look_watch);         // FF3: through the control's vtable +0x08
                ctrl->off = 0;
                return;
            }
            ctrl->Update(&plyr_look_watch);             // FF3: through the control's vtable +0x08
            return;
        }
        ctrl->off = 1;
    }

    plyr->flags &= ~(u_long)PLYR_FLG_LOOK;
    ctrl->Update(&plyr_look_watch);                     // FF3: through the control's vtable +0x08
}

// Whether the room's hit data (the player's area and floor) or the area
// blocks the line from -> to.
// FF3 0x0023aef0

int PlyrAreaLineHitCheck(float *from, float *to)
{
    u_char area = (u_char)GetPlyrAreaNo();

    if (MapHitLineCheck(from, to, MapLoadGetBuffID(area), ingame_wrk.plyr_wrk->floor, 0) == 0
        && MapAreaHitLineCheck(from, to, area) == 0)
    {
        return 0;
    }
    return 1;
}

// The anime that keep the head still: 0x18..0x1c, 0x65..0x8f, 0x95..0x98,
// 0x9c, 0x9e..0xa0 and 0xa4..0xae.
// FF3 0x0023af80

int PlyrAnimeIsLookOff(void)
{
    u_int no = ingame_wrk.plyr_wrk->anime_no;

    return (no >= 0x18 && no <= 0x1c) || (no >= 0x65 && no <= 0x8f)
        || (no >= 0x95 && no <= 0x98) || no == 0x9c
        || (no >= 0x9e && no <= 0xa0) || (no >= 0xa4 && no <= 0xae);
}

// ──────────────────────────────────────────────────────────────────────
// The finder button with a camera (item 8 or 10), outside lock 0xc, flags2 /
// flags3 0x3f800 and statuses 1 / 3: into the viewfinder.
// FF3 0x0023b000

void PlyrFinderStartCheck(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if (*ItemGetCnt(8) == 0 && *ItemGetCnt(10) == 0)
        return;
    if (IsLockTblOn(0xc) || (plyr->flags2 & 0x3f800) != 0 || (plyr->flags3 & 0x3f800) != 0)
        return;
    if (plyr->status == 1 || plyr->status == 3)
        return;

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_FINDER], -1, 0))
        PlyrFinderModeStart();
}

// Into the viewfinder: the finder view centred, the photo mode's parts
// reset, the eye height taken (standing), the enemies' frame flags cleared.
// Crawling, the camera goes up at once (PLYR_FLG_800000, map camera 8);
// else the camera is raised (anime 0x65 / 0x67 while held, 0x69, 0xad or
// 0x96, AcsModel 1 or 2 faded in) with PLYR_FLG_400000.
// FF3 0x0023b0b0

void PlyrFinderModeStart(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       no;
    u_char    anime;

    if ((plyr->flags & PLYR_FLG_HOLD) != 0)
        return;

    PlyrTurnFlagClear();
    plyr->finder_pitch = 0.0f;
    // PC: from the over-the-shoulder camera, the viewfinder opens on what it
    // was looking at: the player turns that way and the view takes its pitch.
    ReiPan_OverShoulderFinderStart(&plyr->rot[1], &plyr->finder_pitch);
    plyr->finder_x     = 320.0f;
    plyr->finder_y     = 224.0f;
    plyr->hold_yaw     = plyr->rot[1];
    plyr->hold_pitch   = plyr->finder_pitch;

    GetPhotoModeWrk()->ZoomReset();
    (void)GetHudHpWrk();                // FF3 drops the result
    GetPhotoModeWrk1e8()->Init();
    GetPhotoModeWrk1e8()->Reset();
    PhotoWrk274Init(GetPhotoModeWrk274());
    PhotoWrk180Init(GetPhotoModeWrk180());
    PhotoWrk288Init(GetPhotoModeWrk288());
    EneMarkInit(GetPhotoEneMark());
    GetLensWrk()->LabelReset();
    PhotoGage4bcSnap(GetPhotoModeWrk4bc());
    PhotoShakeReset();

    if ((plyr->flags & PLYR_FLG_8000000) == 0)
    {
        const float   ofs[3] = { 0.0f, -1.5f, 1.2f };
        u_char        cur    = PlyrGetCurNo();
        sceVu0FVECTOR a;
        sceVu0FVECTOR b;
        sceVu0FVECTOR mid;

        PlyrGetJointPos(0x10, a, nullptr);
        PlyrGetJointPos(4, b, nullptr);
        GetMidPos(mid, a, b);
        plyr->crawl_h = (mid[1] - plyr->pos[1]) + ofs[(s_char)cur];
    }

    if ((plyr->_0e0 & 2) != 0)
        CatViewInit();
    PlyrFinderAutoTurn();

    for (int i = 0; i < ENE_WRK_MAX; i++)
        ene_wrk[i].flags &= ~(u_long)0x00000900000006d0ULL;    // the frame / target bits PlyrFinderEneCheck rebuilds, 0x10000000000, 0x80000000000

    if ((plyr->flags & PLYR_FLG_8000000) != 0)
    {
        plyr->flags = (plyr->flags & ~(u_long)(PLYR_FLG_400000 | PLYR_FLG_1000000)) | PLYR_FLG_800000;
        ReqFinderInOverRap(8);
        pad_wrk[0].stick_dpad = 0;
        return;
    }

    no = (GetPhotoModeWrk()->GetMode() != 0) ? 2 : 1;
    if ((plyr->flags2 & 0x2000000) == 0 && (plyr->_0e0 & 2) == 0)
    {
        if (plyr->status == 4 && (u_int)(plyr->mode - 1) < 4)
        {
            PlyrSet280Req(no, 0.0f, 0.0f, 128.0f, 8.0f);
            if (plyr->_147 == 4)
                anime = 0x65;
            else if (plyr->_147 == 1)
                anime = 0x67;
            else
                goto flags;
        }
        else
        {
            PlyrSet280Req(no, 0.0f, 0.0f, 128.0f, 10.0f);
            anime = 0x69;
        }
    }
    else if ((plyr->flags2 & 0x2000000) == 0)
    {
        PlyrSet280Req(no, 0.0f, 0.0f, 128.0f, 8.0f);
        anime = 0xad;
    }
    else
    {
        PlyrSet280Req(no, 0.0f, 0.0f, 128.0f, 8.0f);
        anime = 0x96;
    }
    SetPlyrAnime(anime, 0xff, 0xff, 0xff, 0, 0.0f);

flags:
    plyr->flags = (plyr->flags & ~(u_long)(PLYR_FLG_800000 | PLYR_FLG_1000000)) | PLYR_FLG_400000;
    pad_wrk[0].stick_dpad = 0;
}

// PLYR_FLG_400000: when the raise anime ends the camera is up.
// FF3 0x0023b4f8

void PlyrFinderStartEnd(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if ((plyr->flags & PLYR_FLG_ANIME_END) == 0)
        return;

    plyr->flags = (plyr->flags & ~(u_long)(PLYR_FLG_400000 | PLYR_FLG_1000000)) | PLYR_FLG_800000;
    ReqFinderInOverRap(3);
    MapObjUpdate(-1);
    MapEffVisibleUpdate();
}

// The finder-off button, outside flags2 0x3f800 and statuses 1 / 3.
// FF3 0x0023b568

void PlyrFinderEndCheck(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if ((plyr->flags2 & 0x3f800) != 0 || plyr->status == 1 || plyr->status == 3)
        return;

    if (ReiPan_MouseActionLowerRequested() ||
        CheckPadTrgPlaySe(pad_key[PAD_KEY_FINDER_OFF], -1, 0))
        PlyrFinderModeEnd();
}

// Out of the viewfinder. Crawling: at once (map camera 5). Else the camera
// is lowered (anime 0x66 / 0x68 while held, 0x6a, 0xae or 0x97, AcsModel 1
// or 2 faded out) with PLYR_FLG_1000000. Held, the facing goes back to the
// hold's; the map objects and effects update their visibility.
// FF3 0x0023b5d8

void PlyrFinderModeEnd(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       no;
    u_char    anime;
    u_char    arg;

    if ((plyr->flags & PLYR_FLG_HOLD) == 0)
        return;

    (void)GetHudHpWrk();                // FF3 drops the result
    if ((plyr->flags & PLYR_FLG_8000000) != 0)
    {
        ReqFinderInOverRap(5);
        plyr->flags &= ~(u_long)PLYR_FLG_HOLD;
        plyr->finder_pitch = 0.0f;
        goto end;
    }

    no = (GetPhotoModeWrk()->GetMode() != 0) ? 2 : 1;
    if ((plyr->flags2 & 0x2000000) == 0 && (plyr->_0e0 & 2) == 0)
    {
        if (plyr->status != 4 || (u_int)(plyr->mode - 1) > 3)
        {
            PlyrSet280Req(no, 128.0f, 4.0f, 0.0f, 10.0f);
            anime = 0x6a;
            arg   = 1;
            SetPlyrAnime(anime, 0xff, 0xff, 0xff, arg, 0.0f);
        }
        else
        {
            PlyrSet280Req(no, 128.0f, 4.0f, 0.0f, 8.0f);
            if (plyr->_147 == 4)
                SetPlyrAnime(0x66, 0xff, 0xff, 0xff, 0, 0.0f);
            else if (plyr->_147 == 1)
                SetPlyrAnime(0x68, 0xff, 0xff, 0xff, 0, 0.0f);
        }
    }
    else
    {
        if ((plyr->flags2 & 0x2000000) == 0)
        {
            PlyrSet280Req(no, 128.0f, 4.0f, 0.0f, 8.0f);
            anime = 0xae;
        }
        else
        {
            PlyrSet280Req(no, 128.0f, 4.0f, 0.0f, 8.0f);
            anime = 0x97;
        }
        SetPlyrAnime(anime, 0xff, 0xff, 0xff, 1, 0.0f);
    }
    plyr->flags = (plyr->flags & ~(u_long)(PLYR_FLG_400000 | PLYR_FLG_800000)) | PLYR_FLG_1000000;

end:
    if (plyr->status == 4)
        plyr->rot[1] = plyr->hold_yaw;
    MapObjUpdate(-1);
    MapEffVisibleUpdate();
}

// PLYR_FLG_1000000: when the lower anime ends the finder flags go; held,
// the hold anime of the attacker kind takes over.
// FF3 0x0023b898

void PlyrFinderEndEnd(void)
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    if ((plyr->flags & PLYR_FLG_ANIME_END) == 0)
        return;

    plyr->flags &= ~(u_long)PLYR_FLG_HOLD;
    if (plyr->status == 4 && (u_int)(plyr->mode - 1) < 4)
        SetPlyrAnime(plyr_hold_anime[PLYR_HOLD_LOOP][plyr->_147], 0xff, 0xff, 0xff, 0, 0.0f);
}

// Going in or up (not coming out): the finder flags drop at once, mode 0,
// and anime 0 unless a crawl or revive flag is left.
// FF3 0x0023b930

void PlyrFinderModeCancel(void)
{
    PLYR_WRK *plyr  = ingame_wrk.plyr_wrk;
    u_long    flags = plyr->flags;

    if ((flags & (PLYR_FLG_400000 | PLYR_FLG_800000)) == 0)
        return;

    if ((flags & PLYR_FLG_8000000) != 0)
    {
        plyr->finder_pitch = 0.0f;
        flags = plyr->flags;
    }
    plyr->flags = flags & ~(u_long)PLYR_FLG_HOLD;
    PlyrSetMode(0, nullptr);
    if ((plyr->flags & 0x3c00000) == 0)
        SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
}

// FF3 0x0023b9d8

int PlyrIsFinderModeOff(void)
{
    return (ingame_wrk.plyr_wrk->flags & PLYR_FLG_HOLD) == 0;
}

// The shutter, each frame in the viewfinder: none in statuses 1 / 3, while
// _250 counts down, or under lock 0xe. The real camera (mode 1) and player
// 1 without the charge take the shutter buttons at once; players 0 and 2
// also the lens shot. Player 1 with the charge (equip 5, the 0x180 rate up,
// no focus): holding the shutter charges (+0x1e8 mode 1), letting go
// shoots. Non-zero: a shot was handled.
// FF3 0x0023ba00

int PlyrFinderShotCtrl(void)
{
    PLYR_WRK      *plyr = ingame_wrk.plyr_wrk;
    PHOTO_WRK_1E8 *w1e8 = GetPhotoModeWrk1e8();
    PHOTO_WRK_180 *w180 = GetPhotoModeWrk180();
    u_char         cur;

    RandGhostAppearReq(2);
    if (plyr->status == 1 || plyr->status == 3)
        return 0;

    PlyrShotChargeCtrl();
    if (plyr->_250 != 0)
    {
        plyr->_250--;
        return 0;
    }
    if (IsLockTblOn(0xe))
        return 0;

    if (GetPhotoModeWrk()->GetMode() != PHOTO_MODE_REAL)
    {
        cur = PlyrGetCurNo();
        if (cur != 1)
        {
            if (cur != 0 && cur != 2)
                return 0;
            return PlyrFinderShutterCheck() | PlyrFinderLensShotCheck();
        }

        if (EneAnyDmgReactBig() == 0 && PlyrGetEquipLv(5) != 0 && w180->rate > 0.0f)
        {
            if (CheckPadNowPlaySe(pad_key[PAD_KEY_06], -1, 0) == 0
                && CheckPadNowPlaySe(pad_key[PAD_KEY_07], -1, 0) == 0)
            {
                if (w1e8->mode == 0)
                    return 0;
                if (PlyrFinderShotCanTake())
                    PlyrFinderShot(0);
                return 1;
            }

            w1e8->mode = 1;
            if (CheckPadTrgPlaySe(pad_key[PAD_KEY_06], -1, 0) == 0
                && CheckPadTrgPlaySe(pad_key[PAD_KEY_07], -1, 0) == 0)
            {
                return 0;
            }
            PlyrSePlay(plyr, 0xb, 100, 0x1000);
            return 0;
        }
    }
    return PlyrFinderShutterCheck();
}

// A shot may be taken: the charger is ready and, in the finder mode, a
// film of the loaded type is left (one is used up unless LENS_FUNC_FREE_FILM
// outside title_start_flg.mode 2). Else the empty SE and the charge mode
// off.
// FF3 0x0023bc18

int PlyrFinderShotCanTake(void)
{
    PHOTO_MODE_WRK *pm   = GetPhotoModeWrk();
    PHOTO_WRK_1E8  *w1e8 = GetPhotoModeWrk1e8();
    u_short         type = (u_short)FilmGetType(GetFilmWrk());

    if (PhotoChargerIsReady(&pm->charger))
    {
        short num;

        if (pm->GetMode() != PHOTO_MODE_FINDER)
            return 1;

        num = 0;
        if (type < 0x45)
            num = *ItemGetCnt(type);
        if (num > 0)
        {
            if (GetLensWrk()->GetFunc(LENS_FUNC_FREE_FILM) && title_start_flg.mode != 2)
                return 1;
            ItemLost(type, 1);
            return 1;
        }
    }

    PhotoModeSePlay(0x1a9d0011, 100, 0x1000);
    w1e8->mode = 0;
    return 0;
}

// A shot (lens 1: a lens shot): the 60-frame timer, the score panel reset,
// the photo spots and the shot judged; an ordinary shot, or a lens shot
// with a special subject pending, makes the photo record and goes to
// phase 33.
// FF3 0x0023bd08

void PlyrFinderShot(char lens)
{
    PHOTO_WRK_288  *w288 = GetPhotoModeWrk288();
    PHOTO_MODE_WRK *pm   = GetPhotoModeWrk();
    PHOTO_OBJ_DAT  *sp;

    ingame_wrk.plyr_wrk->_254 = 60.0f;
    pm->_4d0 = 0;
    PhotoWrk288Init(w288);
    PlyrFinderShotSpots(lens);
    PlyrShotJudge(lens);
    sp = PhotoObjGetEneSubject();

    if (lens == 0 || (lens == 1 && sp != nullptr))
    {
        CineFlagWatchPhoto();
        PlyrShotRecord();
        GetGPhaseCtrl()->SetNext(0x21, 0);
        ingame_wrk.plyr_wrk->_234 = 1;
    }
}

// The shutter buttons: 1 when pressed (and the shot taken if it can be).
// FF3 0x0023bde0

int PlyrFinderShutterCheck(void)
{
    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_07], -1, 0) == 0
        && CheckPadTrgPlaySe(pad_key[PAD_KEY_06], -1, 0) == 0)
    {
        return 0;
    }

    if (PlyrFinderShotCanTake())
        PlyrFinderShot(0);
    return 1;
}

// The lens-shot buttons with a usable lens equip: the shot with SE 6
// (equips 5..7) or 9, then the equip's cost (equip 8 uses every stone).
// FF3 0x0023be60

int PlyrFinderLensShotCheck(void)
{
    LENS_WRK *lens = GetLensWrk();

    if (CheckPadTrgPlaySe(pad_key[PAD_KEY_LENS_SHOT], -1, 0) == 0
        && CheckPadTrgPlaySe(pad_key[PAD_KEY_LENS_SHOT2], -1, 0) == 0)
    {
        return 0;
    }
    if (lens->IsSelUsable() == 0)
        return 0;

    if (PlyrFinderShotCanTake())
    {
        s_char equip = lens->GetSelEquip(0xff);
        u_int  se;

        if (equip == 5 || equip == 6 || equip == 7)
            se = 6;
        else
            se = 9;
        PlyrSePlay(ingame_wrk.plyr_wrk, se, 100, 0x1000);
        PlyrFinderShot(1);

        if (lens->GetSelEquip(0xff) == 8)
            lens->UseStone(lens->GetStoneNum(0xff), 0xff);
        else
            lens->UseSel();
    }
    return 1;
}

// The photo spots in the frame are taken, except by a lens shot with equip
// 5 or 6.
// FF3 0x0023bf98

void PlyrFinderShotSpots(char lens)
{
    LENS_WRK *w = GetLensWrk();

    if (lens == 1 && (w->GetSelEquip(0xff) == 6 || w->GetSelEquip(0xff) == 5))
        return;

    PhotoFlyChk();
}
