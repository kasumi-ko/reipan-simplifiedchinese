/* ==========================================================================
 *  ingame/photo/finder_view.c
 *
 *  The viewfinder camera and the camera HUD's sprite helpers: FF3-only code,
 *  FF3 0x1b6f20..0x1bb4c8 between finder_photo.c and finder_view_hud.c (no
 *  FF2 counterpart; the name is a link-order guess). The object file ends
 *  with the static initializer pair 0x1b7998 / 0x1bb4a8 that fills the HUD
 *  sprite table hud_sprt; its float constants are the sdata run
 *  0x3704e0..0x370500. Roles and arguments: finder_view.h.
 * ======================================================================== */
#include "ingame/photo/finder_view.h"

#include "common/vu0math.h"                 // RotVectorZXY, RotLimitChk
#include "graphics/graph2d/g2d_draw.h"      // SPRT_DAT, DISP_SPRT, CopySprDToSpr, DispSprD
#include "graphics/graph3d/gra3d.h"         // gra3dcamSetPosition / SetTarget / SetFov, gra3dApplyCamera
#include "ingame/cat/cat_ctrl.h"            // CatViewCameraSet
#include "ingame/ingame_wrk.h"              // ingame_wrk
#include "ingame/menu/anim_2d.h"            // LangTexSend
#include "ingame/photo/finder_photo.h"      // GetPhotoModeWrk, GetPhotoModeWrk1e8, PhotoModeGetZoom, PhotoModeGetViewAlpha
#include "ingame/photo/photo.h"             // CameraSetFromRotPair
#include "ingame/photo/fndr_effect.h"       // PhotoWrk1e8GetDispRate
#include "ingame/photo/fndr_parts.h"        // PhotoShakeWork
#include "ingame/plyr/player.h"             // PLYR_WRK, PlyrCamAble, PlyrFlashAble
#include "sdk/libvu0.h"                     // sceVu0AddVector

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// DISP_SPRT.alphar for HudSprtSetBlend's blend 0..3, data 0x3596e8.
static const u_long hud_sprt_alphar[4] = { 0x44, 0x48, 0x42, 0x84 };

// sbss 0x373224 (gp - 0x2cc): the eased view turn's length in frames
// (FinderViewSetTurnTime; zero at start-up).
static float finder_view_turn_time;

// data 0x3252b0: the camera HUD's sprites. FF3 builds this table at
// start-up: FF3 0x001b7998 (__static_initialization_and_destruction_0)
// stores every field, FF3 0x001bb4a8 (_GLOBAL__I_) calls it with (1,
// 0xffff); an aggregate initializer does the same here. Rows recovered with
// tools/reverse/sinit.py 0x1b7998 0x1bb4a8 0x3252b0 0x1ee0 --sprt; x / y
// are the HUD layout on the 640x448 screen (HudSprtSetOfs adds them; zero
// for parts the ring gauges place themselves).
SPRT_DAT hud_sprt[HUD_SPRT_NUM] =
{
#ifdef REIPAN_SLPS_25544_CHS
#include "finder_view_slps25544.inc"
#else
    { 0x200681a5dd30b32dull, 109,  79,  18,  16, 601, 206, 160, 0x80, 0, 0 },   // 0x00
    { 0x200681a5dd30b32dull, 109,  97,  18,   8, 601, 222, 160, 0x80, 0, 0 },   // 0x01
    { 0x200681a5dd30b32dull, 109,  79,  18,  16, 601, 418, 160, 0x80, 1, 0 },   // 0x02
    { 0x200681a5dd30b32dull, 109,  97,  18,   8, 601, 410, 160, 0x80, 1, 0 },   // 0x03
    { 0x200681a5dd30b32dull, 109, 107,  18,  18, 601, 230, 160, 0x80, 0, 0 },   // 0x04
    { 0x200681a5dd30b32dull,  12,  41,  11,   5, 605, 420, 160, 0x80, 0, 0 },   // 0x05
    { 0x200681a5dd30b32dull,  96,   1,  32,  16, 588, 206, 160, 0x80, 0, 0 },   // 0x06
    { 0x200681a5dd30b32dull,  96,  17,  32,   8, 588, 222, 160, 0x80, 0, 0 },   // 0x07
    { 0x200681a5dd30b32dull,  96,   1,  32,  16, 588, 418, 160, 0x80, 1, 0 },   // 0x08
    { 0x200681a5dd30b32dull,  96,  17,  32,   8, 588, 410, 160, 0x80, 1, 0 },   // 0x09
    { 0x200681a5dd30b32dull,  96,  25,  32,  18, 588, 230, 160, 0x80, 0, 0 },   // 0x0a
    { 0x200681a5dd30b32dull,   1,  41,   9,   5, 591, 420, 160, 0x80, 0, 0 },   // 0x0b
    { 0x200682259930b36dull,   1,   1,  17,  16, 503, 400, 160, 0x80, 0, 0 },   // 0x0c
    { 0x200682259930b36dull,  20,   1,   7,  16, 520, 400, 160, 0x80, 0, 0 },   // 0x0d
    { 0x200682259930b36dull,   1,   1,  17,  16, 548, 400, 160, 0x80, 2, 0 },   // 0x0e
    { 0x200682259930b36dull,   1,   1,  17,  16, 503, 411, 160, 0x80, 1, 0 },   // 0x0f
    { 0x200682259930b36dull,  20,   1,   7,  16, 520, 411, 160, 0x80, 1, 0 },   // 0x10
    { 0x200682259930b36dull,   1,   1,  17,  16, 548, 411, 160, 0x80, 3, 0 },   // 0x11
    { 0x200682259930b36dull,   1,  43,  11,  20, 511, 404, 160, 0x80, 0, 0 },   // 0x12
    { 0x200682259930b36dull,   1,  30,  10,  11, 524, 412, 160, 0x80, 0, 0 },   // 0x13
    { 0x200682259930b36dull,  40,   1,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x14
    { 0x200682259930b36dull,  52,   1,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x15
    { 0x200682259930b36dull,  16,  23,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x16
    { 0x200682259930b36dull,  28,  23,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x17
    { 0x200682259930b36dull,  40,  23,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x18
    { 0x200682259930b36dull,  52,  23,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x19
    { 0x200682259930b36dull,  16,  44,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x1a
    { 0x200682259930b36dull,  28,  44,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x1b
    { 0x200682259930b36dull,  40,  44,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x1c
    { 0x200682259930b36dull,  52,  44,  10,  19, 535, 404, 160, 0x80, 0, 0 },   // 0x1d
    { 0x200681a5dd30b32dull,   1,   1,  35,  38, 566, 327, 160, 0x80, 0, 0 },   // 0x1e
    { 0x200681a5dd30b32dull,   1,   1,  35,  38, 566, 414, 160, 0x80, 2, 0 },   // 0x1f
    { 0x200681a5dd30b32dull,  36,   1,  52,  38, 566, 362, 160, 0x80, 0, 0 },   // 0x20
    { 0x200681a5dd30b32dull,   1,  77,  50,  24, 570, 362, 160, 0x80, 0, 0 },   // 0x21
    { 0x200681a5dd30b32dull,   1, 103,  50,  24, 570, 362, 160, 0x80, 0, 0 },   // 0x22
    { 0x200681a5dd30b32dull,   1,  48,  59,  27, 583, 386, 160, 0x80, 0, 0 },   // 0x23
    { 0x200681a5dd30b32dull,  53,  77,  50,  24, 570, 362, 160, 0x80, 0, 0 },   // 0x24
    { 0x200681a5dd30b32dull,  53, 103,  50,  24, 570, 362, 160, 0x80, 0, 0 },   // 0x25
    { 0x200681a5dd30b32dull,  62,  48,  59,  27, 583, 386, 160, 0x80, 0, 0 },   // 0x26
    { 0x200681a5dd30b32dull,   1,   1,  35,  38, 261, 9, 160, 0x80, 0, 0 },   // 0x27
    { 0x200681a5dd30b32dull,   1,   1,  35,  38, 345, 9, 160, 0x80, 2, 0 },   // 0x28
    { 0x200681a5dd30b32dull,  36,   1,  52,  38, 295, 9, 160, 0x80, 0, 0 },   // 0x29
    { 0x200681a5dd30b32dull,   1,  77,  50,  24, 295, 20, 160, 0x80, 0, 0 },   // 0x2a
    { 0x200681a5dd30b32dull,   1, 103,  50,  24, 295, 20, 160, 0x80, 0, 0 },   // 0x2b
    { 0x200681a5dd30b32dull,   1,  48,  59,  27, 320, 29, 160, 0x80, 0, 0 },   // 0x2c
    { 0x200681a5dd30b32dull,  53,  77,  50,  24, 295, 20, 160, 0x80, 0, 0 },   // 0x2d
    { 0x200681a5dd30b32dull,  53, 103,  50,  24, 295, 20, 160, 0x80, 0, 0 },   // 0x2e
    { 0x200681a5dd30b32dull,  62,  48,  59,  27, 320, 29, 160, 0x80, 0, 0 },   // 0x2f
    { 0x200680a62532302dull,   1,   1, 177, 253, -20, -16, 160, 0x80, 0, 0 },   // 0x30
    { 0x200680a62532302dull,   1,   1, 177, 253, 483, -16, 160, 0x80, 2, 0 },   // 0x31
    { 0x200680a62532302dull, 180,   1, 163, 103, 157, -16, 160, 0x80, 0, 0 },   // 0x32
    { 0x200680a62532302dull, 180,   1, 163, 103, 320, -16, 160, 0x80, 2, 0 },   // 0x33
    { 0x200680a62532302dull, 345,   1, 163,  95, 157, 369, 160, 0x80, 0, 0 },   // 0x34
    { 0x200680a62532302dull, 345,   1, 163,  95, 320, 369, 160, 0x80, 2, 0 },   // 0x35
    { 0x200680a62532302dull, 361,  98, 144,  79, -20, 237, 160, 0x80, 0, 0 },   // 0x36
    { 0x200680a62532302dull, 361,  98, 144,  79, 516, 237, 160, 0x80, 2, 0 },   // 0x37
    { 0x200680a62532302dull, 181, 106, 177, 148, -20, 316, 160, 0x80, 0, 0 },   // 0x38
    { 0x200680a62532302dull, 181, 106, 177, 148, 483, 316, 160, 0x80, 2, 0 },   // 0x39
    { 0x200680a62532302dull, 361, 179, 149,  14, 83, 54, 160, 0x80, 0, 0 },   // 0x3a
    { 0x200680a62532302dull, 361, 195, 126,   6, 95, 58, 160, 0x80, 0, 0 },   // 0x3b
    { 0x200680a62532302dull, 427, 225,  29,  29, 307, 36, 160, 0x80, 0, 0 },   // 0x3c
    { 0x200680a62532302dull, 473, 217,  38,  38, 302, 29, 160, 0x80, 0, 0 },   // 0x3d
    { 0x200680a62532302dull, 361, 203,  31,  20, 475, 401, 160, 0x80, 0, 0 },   // 0x3e
    { 0x200680a62532302dull, 394, 203,  31,  20, 475, 401, 160, 0x80, 0, 0 },   // 0x3f
    { 0x200680a62532302dull, 427, 203,  31,  20, 475, 401, 160, 0x80, 0, 0 },   // 0x40
    { 0x200680a62532302dull, 361, 225,  31,  20, 475, 401, 160, 0x80, 0, 0 },   // 0x41
    { 0x200680a62532302dull, 394, 225,  31,  20, 475, 401, 160, 0x80, 0, 0 },   // 0x42
    { 0x2005dd059d40ae72ull,   1,   1,  45,  42, 435, 391, 160, 0x80, 0, 0 },   // 0x43
    { 0x2005dd059d40ae72ull,  70,   1,  45,  42, 580, 391, 160, 0x80, 0, 0 },   // 0x44
    { 0x2005dd059d40ae72ull,  48,   1,  20,  42, 480, 391, 160, 0x80, 0, 0 },   // 0x45
    { 0x2005dd059d40ae72ull,   1,   1,  45,  42, 405, 391, 160, 0x80, 0, 0 },   // 0x46
    { 0x2005dd059d40ae72ull,  70,   1,  45,  42, 590, 391, 160, 0x80, 0, 0 },   // 0x47
    { 0x2005dd059d40ae72ull,  48,   1,  20,  42, 450, 391, 160, 0x80, 0, 0 },   // 0x48
    { 0x2005dc45e1412db2ull, 115,   1, 113, 113, 207, 111, 160, 0x80, 0, 0 },   // 0x49
    { 0x2005dc45e1412db2ull,   1,   1, 112, 112, 320, 224, 160, 0x80, 0, 0 },   // 0x4a
    { 0x2005dc85dd40ae32ull,   1,   1, 123, 123, 320, 224, 160, 0x80, 0, 0 },   // 0x4b
    { 0x20067fa5dd30aeedull,   0,  32, 128,  48, 0, 0, 160, 0x80, 0, 0 },   // 0x4c
    { 0x20067fa5dd30aeedull,   0,  80, 128,  48, 0, 0, 160, 0x80, 0, 0 },   // 0x4d
    { 0x2005dba5dd40ad52ull,  29,  31,  50,  17, 207, 111, 160, 0x80, 0, 0 },   // 0x4e
    { 0x2005dba5dd40ad52ull,  33,  86,  47,  41, 207, 111, 160, 0x80, 0, 0 },   // 0x4f
    { 0x2005dba5dd40ad52ull,   1,  77,  30,  50, 207, 111, 160, 0x80, 0, 0 },   // 0x50
    { 0x2005dba5dd40ad52ull,  83,  82,  44,  45, 207, 111, 160, 0x80, 0, 0 },   // 0x51
    { 0x2005dba5dd40ad52ull,  90,  31,  37,  49, 207, 111, 160, 0x80, 0, 0 },   // 0x52
    { 0x2005dba5dd40ad52ull,  70,  22,  57,   7, -29, -4, 160, 0x80, 0, 0 },   // 0x53
    { 0x2005dba5dd40ad52ull,  85,  31,   3,  27, -2, -32, 160, 0x80, 0, 0 },   // 0x54
    { 0x2005dba5dd40ad52ull,  85,  31,   3,  27, -2, 3, 160, 0x80, 0, 0 },   // 0x55
    { 0x2005dba5dd40ad52ull,  34,  50,  34,  34, 207, 111, 160, 0x80, 0, 0 },   // 0x56
    { 0x2005dba5dd40ad52ull, 108,   1,  18,  19, 0, 0, 160, 0x80, 0, 0 },   // 0x57
    { 0x2005dba5dd40ad52ull,   1,  25,  12,  25, 0, 0, 160, 0x80, 0, 0 },   // 0x58
    { 0x2005dba5dd40ad52ull,  17,  52,  15,  19, 0, 0, 160, 0x80, 0, 0 },   // 0x59
    { 0x2005dba5dd40ad52ull,   1,   1,  14,  22, 0, 0, 160, 0x80, 0, 0 },   // 0x5a
    { 0x2005dba5dd40ad52ull,  34,   1,  12,  23, 0, 0, 160, 0x80, 0, 0 },   // 0x5b
    { 0x2005dba5dd40ad52ull,  88,   1,  18,  19, 0, 0, 160, 0x80, 0, 0 },   // 0x5c
    { 0x2005dba5dd40ad52ull,  17,   1,  15,  22, 0, 0, 160, 0x80, 0, 0 },   // 0x5d
    { 0x2005dba5dd40ad52ull,  48,   1,  18,  21, 0, 0, 160, 0x80, 0, 0 },   // 0x5e
    { 0x2005dba5dd40ad52ull,  15,  25,  12,  24, 0, 0, 160, 0x80, 0, 0 },   // 0x5f
    { 0x2005dba5dd40ad52ull,  68,   1,  18,  19, 0, 0, 160, 0x80, 0, 0 },   // 0x60
    { 0x2005dba5dd40ad52ull,   1,  52,  14,  19, 0, 0, 160, 0x80, 0, 0 },   // 0x61
    { 0x2005dbc5dd40ad72ull,  32,   1,  29,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x62
    { 0x2005dbc5dd40ad72ull,  26,  68,  24,  37, 0, 0, 160, 0x80, 0, 0 },   // 0x63
    { 0x2005dbc5dd40ad72ull,  63,   1,  26,  30, 0, 0, 160, 0x80, 0, 0 },   // 0x64
    { 0x2005dbc5dd40ad72ull,  84,  33,  25,  33, 0, 0, 160, 0x80, 0, 0 },   // 0x65
    { 0x2005dbc5dd40ad72ull,   1,  34,  22,  33, 0, 0, 160, 0x80, 0, 0 },   // 0x66
    { 0x2005dbc5dd40ad72ull,  91,   1,  30,  29, 0, 0, 160, 0x80, 0, 0 },   // 0x67
    { 0x2005dbc5dd40ad72ull,  25,  34,  25,  32, 0, 0, 160, 0x80, 0, 0 },   // 0x68
    { 0x2005dbc5dd40ad72ull,  52,  34,  30,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x69
    { 0x2005dbc5dd40ad72ull,   1,  69,  23,  36, 0, 0, 160, 0x80, 0, 0 },   // 0x6a
    { 0x2005dbc5dd40ad72ull,   1,   1,  29,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x6b
    { 0x2005dbc5dd40ad72ull,  52,  67,  25,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x6c
    { 0x2005dc05dd40ad92ull,  80,  39,  31,  28, 0, 0, 160, 0x80, 0, 0 },   // 0x6d
    { 0x2005dc05dd40ad92ull,   1,   1,  21,  30, 0, 0, 160, 0x80, 0, 0 },   // 0x6e
    { 0x2005dc05dd40ad92ull,  24,  34,  25,  28, 0, 0, 160, 0x80, 0, 0 },   // 0x6f
    { 0x2005dc05dd40ad92ull,  46,   1,  22,  32, 0, 0, 160, 0x80, 0, 0 },   // 0x70
    { 0x2005dc05dd40ad92ull,   1,  33,  21,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x71
    { 0x2005dc05dd40ad92ull,   2,  66,  29,  28, 0, 0, 160, 0x80, 0, 0 },   // 0x72
    { 0x2005dc05dd40ad92ull,  24,   1,  20,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x73
    { 0x2005dc05dd40ad92ull,  70,   1,  24,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x74
    { 0x2005dc05dd40ad92ull,  96,   1,  21,  36, 0, 0, 160, 0x80, 0, 0 },   // 0x75
    { 0x2005dc05dd40ad92ull,  51,  35,  27,  29, 0, 0, 160, 0x80, 0, 0 },   // 0x76
    { 0x2005dc05dd40ad92ull,  33,  66,  24,  31, 0, 0, 160, 0x80, 0, 0 },   // 0x77
    { 0x200681262131322dull,   1,   1, 198, 138, -20, -16, 160, 0x80, 0, 0 },   // 0x78
    { 0x200681262131322dull,   1,   1, 198, 138, -20, 326, 160, 0x80, 1, 0 },   // 0x79
    { 0x200681262131322dull,   1,   1, 198, 138, 462, -16, 160, 0x80, 2, 0 },   // 0x7a
    { 0x200681262131322dull,   1,   1, 198, 138, 462, 326, 160, 0x80, 3, 0 },   // 0x7b
    { 0x200681262131322dull, 201,   1,  54,  99, 178, -16, 160, 0x80, 0, 0 },   // 0x7c
    { 0x200681262131322dull, 201,   1,  54,  99, 178, 365, 160, 0x80, 1, 0 },   // 0x7d
    { 0x200681262131322dull, 201,   1,  54,  99, 408, -16, 160, 0x80, 2, 0 },   // 0x7e
    { 0x200681262131322dull, 201,   1,  54,  99, 408, 365, 160, 0x80, 3, 0 },   // 0x7f
    { 0x200681262131322dull, 201, 102,  54,  99, 232, -16, 160, 0x80, 0, 0 },   // 0x80
    { 0x200681262131322dull, 201, 102,  54,  99, 232, 365, 160, 0x80, 1, 0 },   // 0x81
    { 0x200681262131322dull, 201, 102,  54,  99, 354, -16, 160, 0x80, 2, 0 },   // 0x82
    { 0x200681262131322dull, 201, 102,  54,  99, 354, 365, 160, 0x80, 3, 0 },   // 0x83
    { 0x200681262131322dull, 165, 141,  34,  99, 286, -16, 160, 0x80, 0, 0 },   // 0x84
    { 0x200681262131322dull, 165, 141,  34,  99, 320, -16, 160, 0x80, 2, 0 },   // 0x85
    { 0x200681262131322dull, 165, 141,  34,  99, 286, 365, 160, 0x80, 1, 0 },   // 0x86
    { 0x200681262131322dull, 165, 141,  34,  99, 320, 365, 160, 0x80, 3, 0 },   // 0x87
    { 0x200681262131322dull,   1, 141, 151, 102, -20, 122, 160, 0x80, 0, 0 },   // 0x88
    { 0x200681262131322dull,   1, 141, 151, 102, -20, 224, 160, 0x80, 1, 0 },   // 0x89
    { 0x200681262131322dull,   1, 141, 151, 102, 509, 122, 160, 0x80, 2, 0 },   // 0x8a
    { 0x200681262131322dull,   1, 141, 151, 102, 509, 224, 160, 0x80, 3, 0 },   // 0x8b
    { 0x2005dc65e1412df2ull, 115,   1, 113, 113, 207, 111, 160, 0x80, 0, 0 },   // 0x8c
    { 0x2005dc65e1412df2ull,   1,   1, 112, 112, 320, 224, 160, 0x80, 0, 0 },   // 0x8d
    { 0x2005dca59940ae52ull,   1,  37,  59,  23, -30, -12, 160, 0x80, 0, 0 },   // 0x8e
    { 0x2005dca59940ae52ull,   1,   1,  34,  34, 286, 190, 160, 0x80, 0, 0 },   // 0x8f
    { 0x2005dcc59940ae5aull,   1,   1,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x90
    { 0x2005dcc59940ae5aull,  13,   1,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x91
    { 0x2005dcc59940ae5aull,  25,   1,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x92
    { 0x2005dcc59940ae5aull,  37,   1,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x93
    { 0x2005dcc59940ae5aull,  49,   1,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x94
    { 0x2005dcc59940ae5aull,   1,  17,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x95
    { 0x2005dcc59940ae5aull,  13,  17,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x96
    { 0x2005dcc59940ae5aull,  25,  17,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x97
    { 0x2005dcc59940ae5aull,  37,  17,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x98
    { 0x2005dcc59940ae5aull,  49,  17,  10,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x99
    { 0x2005dcc59940ae5aull,   1,  33,  53,  14, 91, 37, 160, 0x80, 0, 0 },   // 0x9a
    { 0x2005dce59d40ae62ull,   1,   1, 126,  27, 91, 80, 160, 0x80, 0, 0 },   // 0x9b
    { 0x2005dce59d40ae62ull,   1,  30,  18,  27, 217, 80, 160, 0x80, 0, 0 },   // 0x9c
    { 0x2005db85dd40ad32ull,   1,   1, 126,  27, 91, 80, 160, 0x80, 0, 0 },   // 0x9d
    { 0x2005db85dd40ad32ull,   1,  30,  27,  27, 217, 80, 160, 0x80, 0, 0 },   // 0x9e
    { 0x2005db85dd40ad32ull,  89,  88,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0x9f
    { 0x2005db85dd40ad32ull,   1,  59,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa0
    { 0x2005db85dd40ad32ull,  23,  59,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa1
    { 0x2005db85dd40ad32ull,  45,  59,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa2
    { 0x2005db85dd40ad32ull,  67,  59,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa3
    { 0x2005db85dd40ad32ull,  89,  59,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa4
    { 0x2005db85dd40ad32ull,   1,  88,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa5
    { 0x2005db85dd40ad32ull,  23,  88,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa6
    { 0x2005db85dd40ad32ull,  45,  88,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa7
    { 0x2005db85dd40ad32ull,  67,  88,  20,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa8
    { 0x2005db85dd40ad32ull,  30,  30,  97,  27, 91, 80, 160, 0x80, 0, 0 },   // 0xa9
    { 0x2005dd65e1412e8cull,   1,   1, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xaa
    { 0x2005dd65e1412e8cull, 129,   1, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xab
    { 0x2005dd65e1412e8cull,   1,  22, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xac
    { 0x2005dd65e1412e8cull, 129,  22, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xad
    { 0x2005dd65e1412e8cull,   1,  43, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xae
    { 0x2005dd65e1412e8cull, 129,  43, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xaf
    { 0x2005dd65e1412e8cull,   1,  64, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb0
    { 0x2005dd65e1412e8cull, 129,  64, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb1
    { 0x2005dd65e1412e8cull,   1,  85, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb2
    { 0x2005dd65e1412e8cull, 129,  85, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb3
    { 0x2005dd65e1412e8cull,   1, 106, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb4
    { 0x2005dd65e1412e8cull, 129, 106, 126,  19, 91, 111, 160, 0x80, 0, 0 },   // 0xb5
    { 0x2005dd859d40aeccull,   1,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xb6
    { 0x2005dd859d40aeccull,  19,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xb7
    { 0x2005dd859d40aeccull,  37,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xb8
    { 0x2005dd859d40aeccull,  55,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xb9
    { 0x2005dd859d40aeccull,  73,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xba
    { 0x2005dd859d40aeccull,  91,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xbb
    { 0x2005dd859d40aeccull, 109,   1,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xbc
    { 0x2005dd859d40aeccull,   1,  24,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xbd
    { 0x2005dd859d40aeccull,  19,  24,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xbe
    { 0x2005dd859d40aeccull,  37,  24,  16,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xbf
    { 0x2005dd859d40aeccull,  55,  24,  72,  21, 91, 111, 160, 0x80, 0, 0 },   // 0xc0
    { 0x200682a5e131337dull, 169,   1,  48,  24, 52, 321, 160, 0x80, 0, 0 },   // 0xc1
    { 0x200682a5e131337dull, 248,   1,   5,  23, 53, 322, 160, 0x80, 0, 0 },   // 0xc2
    { 0x200682a5e131337dull, 127,  63,  70,  64, 52, 338, 160, 0x80, 0, 0 },   // 0xc3
    { 0x200682a5e131337dull, 169,  27,  26,  22, 53, 323, 160, 0x80, 0, 0 },   // 0xc4
    { 0x200682a5e131337dull, 197,  27,   8,  12, 57, 345, 160, 0x80, 0, 0 },   // 0xc5
    { 0x200682a5e131337dull, 207,  27,   8,  12, 57, 429, 160, 0x80, 0, 0 },   // 0xc6
    { 0x200682a5e131337dull, 201,  41,  20,  38, 122, 364, 160, 0x80, 0, 0 },   // 0xc7
    { 0x200682a5e131337dull, 223,  41,  32,  38, 122, 364, 160, 0x80, 0, 0 },   // 0xc8
    { 0x200682a5e131337dull, 201,  81,  54,  46, 57, 345, 160, 0x80, 0, 0 },   // 0xc9
    { 0x200682a5e131337dull,   1,  43,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xca
    { 0x200682a5e131337dull,  43,   1,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xcb
    { 0x200682a5e131337dull,  85,   1,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xcc
    { 0x200682a5e131337dull,  43,  43,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xcd
    { 0x200682a5e131337dull,  85,  43,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xce
    { 0x200682a5e131337dull,  43,  85,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xcf
    { 0x200682a5e131337dull,   1,   1,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xd0
    { 0x200682a5e131337dull,   1,  85,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xd1
    { 0x200682a5e131337dull,  85,  85,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xd2
    { 0x200682a5e131337dull, 127,   1,  40,  40, 64, 344, 160, 0x80, 0, 0 },   // 0xd3
    { 0x2005dd259940ae82ull,   1,   1,  47,  47, 60, 345, 160, 0x80, 0, 0 },   // 0xd4
    { 0x200682a5e131337dull, 147,  43,  18,  18, 132, 384, 160, 0x80, 0, 0 },   // 0xd5
    { 0x2005dd455540ae8aull,   1,   1,  18,  18, 123, 375, 160, 0x80, 0, 0 },   // 0xd6
    { 0x200682a5e131337dull, 127,  43,  18,  16, 123, 375, 160, 0x80, 0, 0 },   // 0xd7
    { 0x200682a5e131337dull, 219,   1,  27,  27, 119, 370, 160, 0x80, 0, 0 },   // 0xd8
    { 0x2006802621312f2dull,  84,  86, 137,  82, 0, 0, 160, 0x80, 0, 0 },   // 0xd9
    { 0x2006802621312f2dull,   0, 173, 117,  23, 0, 0, 160, 0x80, 0, 0 },   // 0xda
    { 0x2006802621312f2dull, 118, 171, 115,  25, 0, 0, 160, 0x80, 0, 0 },   // 0xdb
    { 0x2006802621312f2dull, 174,  47,  26,  25, 0, 0, 160, 0x80, 0, 0 },   // 0xdc
    { 0x2006802621312f2dull,  68,   1, 104,  83, 0, 0, 160, 0x80, 0, 0 },   // 0xdd
    { 0x2006802621312f2dull,   1, 199,  86,  17, 0, 0, 160, 0x80, 0, 0 },   // 0xde
    { 0x2006802621312f2dull,  89, 199,  86,  17, 0, 0, 160, 0x80, 0, 0 },   // 0xdf
    { 0x2006802621312f2dull, 177, 199,  20,  17, 0, 0, 160, 0x80, 0, 0 },   // 0xe0
    { 0x2006802621312f2dull,   1,  86,  81,  85, 0, 0, 160, 0x80, 0, 0 },   // 0xe1
    { 0x2006802621312f2dull, 174,   1,  68,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xe2
    { 0x2006802621312f2dull,   1, 218,  68,  15, 0, 0, 160, 0x80, 0, 0 },   // 0xe3
    { 0x2006802621312f2dull,  71, 218,  16,  15, 0, 0, 160, 0x80, 0, 0 },   // 0xe4
    { 0x2006802621312f2dull,   1,   1,  65,  83, 0, 0, 160, 0x80, 0, 0 },   // 0xe5
    { 0x2006802621312f2dull, 174,  19,  55,  12, 0, 0, 160, 0x80, 0, 0 },   // 0xe6
    { 0x2006802621312f2dull, 174,  33,  55,  12, 0, 0, 160, 0x80, 0, 0 },   // 0xe7
    { 0x2006802621312f2dull, 231,  33,  14,  12, 0, 0, 160, 0x80, 0, 0 },   // 0xe8
    { 0x2005dba5dd40ad52ull,  96,  46,  31,  81, 169, 143, 160, 0x80, 0, 0 },   // 0xe9
    { 0x2005dba5dd40ad52ull,  96,  46,  31,  81, 169, 229, 160, 0x80, 1, 0 },   // 0xea
    { 0x2005dba5dd40ad52ull,  96,  46,  31,  81, 440, 143, 160, 0x80, 2, 0 },   // 0xeb
    { 0x2005dba5dd40ad52ull,  96,  46,  31,  81, 440, 229, 160, 0x80, 3, 0 },   // 0xec
    { 0x2005dba5dd40ad52ull,  70,  60,  24,  67, 159, 157, 160, 0x80, 0, 0 },   // 0xed
    { 0x2005dba5dd40ad52ull,  70,  60,  24,  67, 159, 224, 160, 0x80, 1, 0 },   // 0xee
    { 0x2005dba5dd40ad52ull,  70,  60,  24,  67, 457, 157, 160, 0x80, 2, 0 },   // 0xef
    { 0x2005dba5dd40ad52ull,  70,  60,  24,  67, 457, 224, 160, 0x80, 3, 0 },   // 0xf0
    { 0x20067fa5dd30aeedull,   1,   8,  31,  18, 0, 0, 160, 0x80, 0, 0 },   // 0xf1
    { 0x20067fa5dd30aeedull,  34,   8,  15,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xf2
    { 0x20067fa5dd30aeedull,  51,   8,  15,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xf3
    { 0x20067fa5dd30aeedull,  68,   8,  15,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xf4
    { 0x20067fa5dd30aeedull,  85,   8,  15,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xf5
    { 0x20067fa5dd30aeedull, 102,   8,  15,  16, 0, 0, 160, 0x80, 0, 0 },   // 0xf6
#endif
};

// ──────────────────────────────────────────────────────────────────────
// The viewfinder camera
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001b6f20
void FinderViewCameraCtrl(void)
{
    PLYR_WRK     *p = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR eye_ofs;
    sceVu0FVECTOR eye;
    sceVu0FVECTOR ang;
    sceVu0FVECTOR tgt;
    float         fov;

    if (p->_0e0 & 1)
    {
        CameraSetFromRotPair();
        return;
    }
    if (p->_0e0 & 2)
    {
        CatViewCameraSet();
        return;
    }

    eye_ofs[0] = 0.0f;
    eye_ofs[1] = p->crawl_h;
    eye_ofs[2] = 0.0f;
    eye_ofs[3] = 1.0f;
    RotVectorZXY(eye_ofs, eye_ofs, p->rot);
    sceVu0AddVector(eye, p->pos, eye_ofs);
    eye[3] = 1.0f;
    gra3dcamSetPosition(eye);

    if ((ingame_wrk.plyr_wrk->flags2 & 0x200000) == 0)
    {
        FinderViewTurnCtrl();
        FinderViewHook0(p, p);
        FinderViewHook1(p, p);
    }
    else if (p->_000 == 0.0f)
    {
        // the timed turn is over
        ingame_wrk.plyr_wrk->flags2 &= ~(u_long)0x200000;
        p->view_turn[3] = 0.0f;
        p->view_turn[0] = 0.0f;
        p->view_turn[1] = 0.0f;
        p->view_turn[2] = 0.0f;
    }
    else
    {
        p->_000 -= 1.0f;
        sceVu0AddVector(p->rot, p->rot, p->view_turn);
        RotLimitChk(&p->rot[1]);
    }

    tgt[0] = 0.0f;
    tgt[1] = 0.0f;
    tgt[2] = 1000.0f;
    tgt[3] = 1.0f;
    ang[0] = p->finder_pitch;
    ang[1] = p->rot[1];
    ang[2] = 0.0f;
    ang[3] = 0.0f;
    RotVectorZXY(tgt, tgt, ang);
    sceVu0AddVector(tgt, eye, tgt);
    tgt[3] = 1.0f;
    gra3dcamSetTarget(tgt, 1);

    PhotoShakeWork();
    fov = GetPhotoModeWrk()->GetZoom() * 0.835f;
    gra3dcamSetFov(fov * (1.85f - GetPhotoModeWrk1e8()->GetDispRate()));
    gra3dApplyCamera(nullptr, 1, 1);
}

// FF3 0x001b7160
void FinderViewHook1(PLYR_WRK *p, PLYR_WRK *q)
{
    (void)p;
    (void)q;
}

// FF3 0x001b7168
void FinderViewHook0(PLYR_WRK *p, PLYR_WRK *q)
{
    (void)p;
    (void)q;
}

// FF3 0x001b7170
void FinderViewSetTurnTime(float time)
{
    finder_view_turn_time = time;
}

// FF3 0x001b7178
void FinderViewTurnStart(void)
{
    PLYR_WRK *p = ingame_wrk.plyr_wrk;

    p->_000 = 10.0f;
    if (p->view_turn[1] != 0.0f)
    {
        p->view_turn[1] = p->view_turn[1] / 10.0f;
    }
}

// FF3 0x001b71b8
void FinderViewTurnCtrl(void)
{
    PLYR_WRK *p = ingame_wrk.plyr_wrk;
    float     acc;
    float     a;

    if ((p->flags & PLYR_FLG_VIEW_EASE) == 0)
    {
        if (p->_000 <= 0.0f)
        {
            return;
        }
        p->rot[1] += p->view_turn[1];
        RotLimitChk(&p->rot[1]);
        p->_000 -= 1.0f;
        if (p->_000 > 0.0f)
        {
            return;
        }
    }
    else
    {
        acc = p->view_turn[0] * 0.5f;
        p->_000 += 1.0f;
        a = acc * p->_000 * p->_000 + p->view_from[0];
        RotLimitChk(&a);
        p->finder_pitch = a;
        a = p->view_turn[1] * 0.5f * p->_000 * p->_000 + p->view_from[1];
        RotLimitChk(&a);
        p->rot[1] = a;
        if (p->_000 < finder_view_turn_time)
        {
            return;
        }
        p->flags &= ~(u_long)PLYR_FLG_VIEW_EASE;
        p->view_turn[0] = p->view_turn[1] = p->view_turn[2] = p->view_turn[3] = 0.0f;
        p->view_from[0] = p->view_from[1] = p->view_from[2] = p->view_from[3] = 0.0f;
        return;
    }
    p->view_turn[0] = p->view_turn[1] = p->view_turn[2] = p->view_turn[3] = 0.0f;
}

// FF3 0x001b7330
void FinderViewTurnReq(void)
{
    FinderViewTurnStart();
}

// FF3 0x001b7348
void FinderViewTurnMain(void)
{
    FinderViewTurnCtrl();
}

// ──────────────────────────────────────────────────────────────────────
// HUD sprite helpers
// ──────────────────────────────────────────────────────────────────────

// The table entry's alpha times a, clamped to 0..255 (inlined in FF3's
// HudSprtSet and HudSprtSetCenter).
static inline u_char HudSprtAlpha(int no, float a)
{
    int alpha = (int)((float)hud_sprt[no].alpha * a);

    if (alpha < 0)
    {
        return 0;
    }
    if (alpha > 0xff)
    {
        alpha = 0xff;
    }
    return (u_char)alpha;
}

// FF3 0x001b7360
void HudSprtSetBlend(DISP_SPRT *ds, int blend)
{
    ds->alphar = hud_sprt_alphar[blend];
}

// FF3 0x001b7380
void HudSprtSetOfs(float x, float y, float a, float sx, float sy, float rot,
                   DISP_SPRT *ds, int no)
{
    HudSprtSet((float)hud_sprt[no].x + x, (float)hud_sprt[no].y + y, a, sx, sy, rot, ds, no);
}

// FF3 0x001b73c8
void HudSprtDrawOfs(float x, float y, float a, float sx, float sy, float rot,
                    DISP_SPRT *ds, int no, int blend, u_char r, u_char g, u_char b)
{
    HudSprtSetOfs(x, y, a, sx, sy, rot, ds, no);
    HudSprtSetBlend(ds, blend);
    ds->r = r;
    ds->g = g;
    ds->b = b;
    DispSprD(ds);
}

// FF3 0x001b7438
void HudSprtSet(float x, float y, float a, float sx, float sy, float rot,
                DISP_SPRT *ds, int no)
{
    u_char alpha = HudSprtAlpha(no, a);

    CopySprDToSpr(ds, &hud_sprt[no]);
    ds->scw   = sx;
    ds->sch   = sy;
    ds->rot   = rot;
    ds->crx   = x;
    ds->cry   = y;
    ds->alpha = alpha;
    ds->x     = x;
    ds->y     = y;
    ds->csx   = x;
    ds->csy   = y;
    ds->tex1  = 0x161;
}

// FF3 0x001b7518
void HudSprtDraw(float x, float y, float a, float sx, float sy, float rot,
                 DISP_SPRT *ds, int no, int blend, u_char r, u_char g, u_char b)
{
    HudSprtSet(x, y, a, sx, sy, rot, ds, no);
    HudSprtSetBlend(ds, blend);
    ds->r = r;
    ds->g = g;
    ds->b = b;
    DispSprD(ds);
}

// FF3 0x001b7588
void HudSprtSetCenter(float x, float y, float a, float sx, float sy, float rot,
                      DISP_SPRT *ds, int no)
{
    float  top   = y - (float)hud_sprt[no].h * 0.5f;
    float  left  = x - (float)hud_sprt[no].w * 0.5f;
    u_char alpha = HudSprtAlpha(no, a);

    CopySprDToSpr(ds, &hud_sprt[no]);
    ds->x     = left;
    ds->y     = top;
    ds->scw   = sx;
    ds->sch   = sy;
    ds->rot   = rot;
    ds->crx   = x;
    ds->cry   = y;
    ds->alpha = alpha;
    ds->csx   = x;
    ds->csy   = y;
    ds->tex1  = 0x161;
}

// FF3 0x001b76a8
void HudSprtDrawCenter(float x, float y, float a, float sx, float sy, float rot,
                       DISP_SPRT *ds, int no, int blend, u_char r, u_char g, u_char b)
{
    HudSprtSetCenter(x, y, a, sx, sy, rot, ds, no);
    HudSprtSetBlend(ds, blend);
    ds->r = r;
    ds->g = g;
    ds->b = b;
    DispSprD(ds);
}

// FF3 0x001b7718
void HudSprtDrawMirror4(float x, float y, float a, float sx, float sy, float rot,
                        DISP_SPRT *ds, int no, int blend, u_char r, u_char g, u_char b)
{
    HudSprtSet(x, y, a, 1.0f, 1.0f, 0.0f, ds, no);
    HudSprtSetBlend(ds, blend);
    ds->rot = rot;
    // bottom-right corner on (x, y), the scale centre: the four scales mirror it
    ds->x += (float)-(int)hud_sprt[no].w;
    ds->y += (float)-(int)hud_sprt[no].h;
    ds->r = r;
    ds->g = g;
    ds->b = b;
    ds->scw = sx;
    ds->sch = sy;
    DispSprD(ds);
    ds->scw = -sx;
    ds->sch = sy;
    DispSprD(ds);
    ds->scw = sx;
    ds->sch = -sy;
    DispSprD(ds);
    ds->scw = -sx;
    ds->sch = -sy;
    DispSprD(ds);
}

// FF3 0x001b7868
int HudGetCamType(void)
{
    if (PlyrCamAble())
    {
        return 2;
    }
    return PlyrFlashAble() != 0;
}

// FF3 0x001b78a0
int HudIsViewOn(void)
{
    return GetPhotoModeWrk()->GetViewAlpha() != 0;
}

// FF3 0x001b78c8
void HudTexSend(int real)
{
    (void)real;                             // FF3 ignores it

    if (HudIsViewOn())
    {
        LangTexSend(0x20);
        LangTexSend(0x13);
        LangTexSend(0x15);
        LangTexSend(0x0d);
        LangTexSend(0x0e);
        LangTexSend(0x10);
        LangTexSend(0x12);
        LangTexSend(0x14);
        LangTexSend(0x1d);
        LangTexSend(0x1f);
        LangTexSend(0x23);
        LangTexSend(0x19);
        LangTexSend(0x1a);
        LangTexSend(0x1e);
        LangTexSend(0x18);
        LangTexSend(0x0c);
        LangTexSend(0x16);
        LangTexSend(0x17);
        LangTexSend(0x1b);
        LangTexSend(0x1c);
    }
    LangTexSend(0x21);
    LangTexSend(0x22);
}
