/* ==========================================================================
 *  ingame/event/prg/ev_command.c
 *
 *  FF3's event script handlers, FF3 0x19e538..0x1a4be0 (see ev_command.h for
 *  the two name tables and the argument formats). Written so far:
 *
 *    - the handlers 0x19e538..0x1a0760: the item window, messages and
 *      choices, subtitles, event camera, player lock / finder / flashlight /
 *      look direct, IF_ .. ENDIF_, EV_STOP, doors, effects, fades, the FF2
 *      fog commands FF3 empties, locks;
 *    - AreaChange / AreaChangeEnd (lock ids) and the effect save blocks
 *      0x19fb18..0x19fcb8;
 *    - the event sound / stream control 0x1a2fb0..0x1a3c60 with its
 *      commands (FF2 ev_macro.c's ev_sound_ctrl / ev_stream_ctrl /
 *      ev_save_* code, from MioPan ev_macro.c; FF3 keys sounds by SE id,
 *      gives streams a state word and merges the three saved-stream tables);
 *    - the out-of-line stream starter 0x2b8f70 and the wakeup / sleep movie
 *      work's getter 0x2b8f28.
 *
 *    - the handlers 0x1a07a0..0x1a2df0 (worlds, research, radio, wakeup /
 *      sleep movies, missions, ghosts, items, files, memos, areas, the
 *      player's position / anime, NPCs, map object rows) and phase 36
 *      (Story_RoomReload) 0x1a2df8..0x1a2f08.
 *
 *    - the last commands 0x1a3c60..0x1a3e18 (BGM list, tape, telephone,
 *      event work) and every condition 0x1a3e38..0x1a4af0 with their
 *      helpers EvRotInRange / EvPosIntoSight.
 *
 *  Nothing calls the handlers yet: the dispatcher 0x19b5b8 that reaches
 *  them through the table is not written.
 * ======================================================================== */
#include "ingame/event/prg/ev_command.h"
#include "ingame/map/MapCtrl.h"              // MapAreaGetHeight
#include "ingame/map/BgSe.h"                    // GetLayoutSeWrk, CBgSe

#include <math.h>                               // sqrtf
#include <string.h>                             // memset, memcpy

#include "ingame/camera/camVci.h"              // camVci, GetEvCamVci
#include "ingame/cine_film.h"                   // CineFilmPlayReq, CineFilmSelect, CineFilmStop
#include "common/cmn_fade_layer.h"              // CmnFadeLayerIdle*
#include "common/cmn_util.h"                    // ClearRewardSetOpened
#include "common/get_byte.h"                    // Get1Byte / Get2Byte / Get4Byte
#include "ingame/event/prg/ev_get.h"            // EvGetRot360
#include "common/vu0math.h"                     // RotLimitChk
#include "graphics/scene/IngameScene.h"         // SleepFlgSet, SleepFlgClear, SleepFlgSideWake
#include "ingame/map/DoorStat.h"                 // DoorStatClearKnownAll, DoorStatSetKnownSys, DoorStatSetLock, DoorStatSetNoUse
#include "graphics/effect/effect.h"             // SetEffects, EffectSetScreenEffectNo
#include "graphics/effect/effect_butterfly.h"   // EffectButterfly*
#include "graphics/effect/effect_obj.h"         // EffThunderReq, EffSealDoorBurn*
#include "graphics/effect/effect_oth.h"         // EffectMonochromeSet
#include "ingame/enemy/enemy_act.h"             // EneActReqById
#include "graphics/motion/anicode.h"            // ItemGet*, MsgView*, msg_pic_key_dat
#include "system/eeiop/cd_file_dat.h"           // ITEM_000_TM2, EVENT_002_TM2, FG_VOICE_IKINOKORI_00_STR_STR
#include "ingame/memo/memo.h"            // MemoSetPage, MemoOpen, memo_flg
#include "ingame/candle_ctrl.h"                 // GetCandleControl, CandleControlSet*
#include "ingame/cat/cat_ctrl.h"                // the cat NPCs
#include "ingame/camera/event_camera.h"         // EventCamera*
#include "ingame/cine_menu.h"                    // CineFilmTblGetFile, CineMenuReq
#include "ingame/enemy/ene_main.h"              // ENE_WRK, EneGet*
#include "ingame/enemy/enemy.h"                 // EneLoadReq, EnemyStart, ...
#include "ingame/enemy/ene_spe_obj.h"             // GetEneSpeObjTbl, EneSpeObjTblStart
#include "ingame/event/telephone.h"         // the telephone, TelTalkClear
#include "ingame/event/ev_queue.h"              // EvQueue*
#include "ingame/event/prg/ev_change.h"         // the event states, the condition table
#include "ingame/event/prg/ev_exe.h"            // EV_EXE_CTRL, EvExeGetCtrl
#include "ingame/event/prg/ev_chapter.h"        // GetObjectPos, EvChapterDispReq, EvGhost*
#include "ingame/event/prg/ev_open.h"           // EventSetOpenCondition
#include "ingame/event/prg/ev_se.h"             // the event SE files
#include "ingame/event/prg/ev_timer.h"          // EvTimerRegist
#include "ingame/camera/finder_camera.h"        // FINDER_MODE_WRK
#include "ingame/story_menu_photo.h"            // DocFlagSet, DocFlagClear, DocGetMsgType
#include "ingame/story_develop.h"               // DevelopReq, DevelopFilm*
#include "ingame/story_night.h"                 // NightChangeReq, NightStartPosSet
#include "ingame/ingame.h"                      // GPhaseCtrlRequest, Ingame*MdlReset, SndVolume0Mute
#include "ingame/ingame_effect.h"               // IgEffectHighContrastFade
#include "ingame/item/prg/item.h"               // ItemGet, ItemUse, ItemLost, ItemGetCnt
#include "ingame/item/prg/item_tbl.h"           // ItemTblReset
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/clear/GameClear.h"             // GameClear*
#include "ingame/map/MarkEvent.h"               // MarkEventFlagSet
#include "ingame/map/MapDBuff.h"                // MapSpEventSet
#include "ingame/map/MapLoad.h"                 // MapLoad*
#include "ingame/map/MapPut.h"                  // MapPutSetDrawStop
#include "ingame/map/foot_se.h"                 // foot_seRelease
#include "ingame/map/map_rectangle.h"           // MrecSetCameraSet
#include "ingame/menu/menu_cassette.h"          // the cassette player (TAPE_START)
#include "ingame/menu/anim_2d.h"                // LayoutAnim*, Anim2dMsg*, LayoutMsg*
#include "ingame/photo/photo_obj.h"             // PhotoObjAreaOn / Off
#include "ingame/photo/photo_obj_dat.h"         // PhotoObjGetDat, PhotoObj*Photoable
#include "ingame/menu/start_menu.h"             // FINDER_STATE_WRK
#include "ingame/movie_room_menu/prg/movie_projecter.h"  // MovieReqPlay
#include "ingame/mission/mission_mode.h"         // FINDER_MODE_WRK, FINDER_BANK_ENTRY
#include "ingame/photo/finder_photo.h"          // GetFilmWrk, GetLensWrk, GetHudHpWrk, GetPhotoModeWrk
#include "ingame/photo/photo.h"                 // PeepingRequest
#include "ingame/photo/fndr_parts.h"            // Film*, Lens*, HudHpSetNow
#include "ingame/photo/n_equip_tray.h"          // PlyrCameraMotCall
#include "ingame/photo/n_plyr_camera.h"         // the player-camera NPCs
#include "ingame/plyr/player.h"                 // PLYR_WRK, Plyr*
#include "ingame/plyr/plyr_mdl.h"               // PlyrFinderMode*, PlyrLookDirect*
#include "ingame/plyr/unit_ctl.h"               // OutSightChk
#include "ingame/plyr/plyr_sub.h"               // PlyrMdlChange, AllPlyr*, PlyrSetCostumeSub
#include "ingame/story_door.h"          // MapAreaNoSet, MapAreaSideSet
#include "ingame/map/MapEff.h"                  // MapEffVisibleUpdate, MapEffBlazeUp, MapLightReg*
#include "ingame/map/MapSnap.h"                 // MapSnapSetUpdate, MapSnapClearFlagAll, MapSnapUpdateWorld
#include "ingame/map/MapDoor.h"      // door table, door motion pack
#include "ingame/puzzle/puzzle.h"               // PuzzleSet
#include "ingame/plyr/plyr_shot.h"           // PlyrGetFinderState
#include "ingame/radio.h"                       // RadioTurnOn, RadioTurnOff
#include "ingame/enemy/random_ghost.h"          // RandGhost*
#include "ingame/sound/snd_ctrl.h"              // SndSe*
#include "ingame/story_menu.h"                  // LockTblSet / Inc
#include "ingame/menu/menu_file_list.h"         // MemoMenuReq
#include "ingame/story_save.h"                  // StorySaveGameReq
#include "main/gphase.h"                        // GetGPhaseCtrl, GPHASE_CTRL::SetNext
#include "ingame/gameover/prg/gameover.h"       // GameOverReq
#include "ingame/subtitle/subtitle.h"           // SubTitle*
#include "ingame/ingame_wrk.h"                  // ingame_wrk, title_start_flg, TitleMenuIsItemOpen
#include "system/pad/pad_ctrl.h"                // PadLock*, CheckPadTrgPlaySe, pad_key
#include "system/eeiop/snd_3d.h"                // SND_3D_SET
#include "system/eeiop/snd_buffer.h"            // SndBufPause / Restart
#include "system/eeiop/stream_auto.h"           // StreamAuto*
#include "ingame/map/FurnSave.h"                // FURN_SAVE_ROW, FurnSaveFind*
#include "ingame/map/MapObjWrk.h"               // MapObjFindWrkByRow, MapObjSetAction, MapObjUpdate
#include "ingame/map/map_bgm.h"                 // map_bgmFadeIn / FadeOut / SetList / ChangeTbl
#include "ingame/npc/npc_act.h"                 // PlyrCameraSetPosLabel, PlyrCameraSightCheck, PlyrCameraDistCheck
#include "graphics/graph2d/fade.h"
#include "graphics/effect/effect_data.h"     // HandLightEffectSetDisp
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// unassigned@001cae98 (screen fade)

// unassigned@00158ed0 (effect switches)

// unassigned@0011cea8 (map hit)

// unassigned@00198e28 (event executor)



// ── state ──────────────────────────────────────────────────────────────

static void                 *ev_eff_dither_id[2];       /* sdata 0x3730d8 (EE: two words) */
static EV_SAVE_EFF_DITHER    ev_save_eff_dither;        /* sdata 0x3730e0 */
static EV_SAVE_SCREEN_EFFECT ev_save_screen_effect;     /* sdata 0x3730e8 */
static u_char                synchro_mode_flg;          /* sdata 0x370254 */

static int   ev_msg_anim;               /* sdata 0x370234: MSG_DISP's layout anim slot (EvMsgCtrlInit: -1) */
static int   ev_disp2d_anim;            /* sdata 0x370238: DISP2D_START's */
static int   ev_msg_choice_anim = -1;   /* sdata 0x370244: MSG_CHOICE's */
static short ev_msg_choice_no = -1;     /* sdata 0x370248: MSG_CHOICE's answer (condition SELECT_CHOICE) */

static EV_SOUND_CTRL  ev_sound_ctrl[EV_SOUND_CTRL_MAX];     /* bss 0x3e6850 */
static EV_SAVE_STREAM ev_save_stream[EV_SAVE_STREAM_MAX];   /* bss 0x3e69c0 */
static EV_STREAM_CTRL ev_stream_ctrl[EV_STREAM_CTRL_MAX];   /* bss 0x3e6a20 */

static int    ev_file_read_anim;        /* sdata 0x370270: FILE_READ's layout anim slot */
static u_char ev_plyr_mode_keep;        /* sdata 0x370274: the mode PLYR_MOTION_CALL left */
static int    ev_reload_area;           /* bss 0x3e6840: phase 36's area */
static int    ev_reload_mh;             /* bss 0x3e6844: its map hit object */
static int    ev_reload_ret_phase;      /* bss 0x3e6848: the phase to return to */

static void EvRoomReloadReq(int area, int mh);          // FF3 0x001a2df8

// ──────────────────────────────────────────────────────────────────────
// MOVIE_PLAY "112": play movie args[0] (Story_SceneMovie).
// FF3 0x0019e538

int EvMoviePlay(EV_EXE_CTRL *ctrl)
{
    MovieReqPlay(ctrl->args[0], 1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// The adapted file table keeps the port's named file IDs. EVENT_OBJ and
// EV_MISSION_OBJ still hold the SLPS_255.44 file IDs.
static int EventPictureFile(int file)
{
#ifdef REIPAN_SLPS_25544_CHS
    if (file >= 0x0024 && file <= 0x054f)
        return ITEM_000_TM2 + file - 0x0024;
    if (file >= 0x07c3 && file <= 0x07cf)
        return EVENT_002_TM2 + file - 0x07c3;
    if (file >= 0)
        return -1;
#endif
    return file;
}

static int EventStreamFile(int file)
{
#ifdef REIPAN_SLPS_25544_CHS
    // Each stream in this SLPS_255.44 span is followed by its HXD.
    if (file >= 0x0f40 && file <= 0x1120 && (file & 1) == 0)
        return FG_VOICE_IKINOKORI_00_STR_STR + file - 0x0f40;
    if (file >= 0)
        return -1;
#endif
    return file;
}

static int EventSoundBankFile(int file)
{
#ifdef REIPAN_SLPS_25544_CHS
    if (file >= 0x1911 && file <= 0x1af3 && (file & 1) != 0)
        return SHOCK_00_BD_BD + file - 0x1911;
    if (file >= 0)
        return -1;
#endif
    return file;
}

// DISP2D_START "1122222": the item-get window with picture args[1] and the
// texts args[4] / args[5] (either < 0: no text, flag 1). Done once the
// window's layout anim reaches state 2 (open).
// FF3 0x0019e560

int EvDisp2DStart(EV_EXE_CTRL *ctrl)
{
    int msg0 = ctrl->args[4];
    int msg1 = ctrl->args[5];

    if (ctrl->process == 0) {
        ev_disp2d_anim = LayoutAnimStart(2, 0);
        ItemGetSetPic(EventPictureFile(ctrl->args[1]));
        ItemGetSetMsg(msg0, msg1);
        ItemGetSetFlags((msg0 < 0 || msg1 < 0) ? 1 : 0);
        LockTblInc(9);
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (LayoutAnimIsStateChanged(ev_disp2d_anim) == 0) {
            return 0;
        }
        return LayoutAnimGetState(ev_disp2d_anim) == 2;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// DISP2D_END "112": close the item-get window; done at state 4 (closed).
// FF3 0x0019e650

int EvDisp2DEnd(EV_EXE_CTRL *ctrl)
{
    if (ctrl->process == 0) {
        ItemGetReqClose(1);
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (LayoutAnimIsStateChanged(ev_disp2d_anim) == 0) {
            return 0;
        }
        if (LayoutAnimGetState(ev_disp2d_anim) == 4) {
            LockTblDec(9);
            return 1;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// CHAPTER_DISP_START "112": the chapter title of args[0], which becomes the
// game work's chapter byte; chapter 1 restarts the night count.
// FF3 0x0019e6d8

int EvChapterDispStart(EV_EXE_CTRL *ctrl)
{
    u_int chapter = (u_int)ctrl->args[0];

    EvChapterDispReq((u_char)chapter);
    ingame_wrk.b02 = (u_char)chapter;
    if (chapter == 1) {
        ingame_wrk.b04 = 0;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MSG_DISP / MSG_DISP_SEL's body: message window (layout anim 5) with
// Anim2dMsgSet(args[0], args[1 + row]); done when the window is closed
// (state 0 or 4) or could not open. The event executor pauses meanwhile.
// FF3 0x0019e720

static int EvMsgDispSub(EV_EXE_CTRL *ctrl, int row)
{
    int state;

    SetEventWaitFlg(1);
    if (ctrl->process == 0) {
        SetEventWaitFlg(1);
        ev_msg_anim = LayoutAnimStart(5, 0);
        Anim2dMsgSet((u_char)ctrl->args[0], ctrl->args[row + 1]);
        Anim2dMsgSetType(2);
        LayoutMsgSetKeepOpen(0);
        LockTblInc(9);
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (ev_msg_anim < 0) {
            LockTblDec(9);
            SetEventWaitFlg(0);
            return 1;
        }
        state = LayoutAnimGetState(ev_msg_anim);
        if (state != 0 && state != 4) {
            return 0;
        }
        SetEventWaitFlg(0);
        LayoutAnimReset(ev_msg_anim);
        ev_msg_anim = -1;
        LockTblDec(9);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// MSG_DISP "112": message args[1].
// FF3 0x0019e820

int EvDispMsg(EV_EXE_CTRL *ctrl)
{
    return EvMsgDispSub(ctrl, 0);
}

// ──────────────────────────────────────────────────────────────────────
// MSG_DISP_SEL "11222": message args[1..3] by how often the event ran
// (1st, 2nd, 3rd and later; a first run that counted 0 reads args[0]).
// FF3 0x0019e838

int EvMsgDispSel(EV_EXE_CTRL *ctrl)
{
    int row = GetEvExeCount(ctrl->event_id) - 1;

    if (row > 2) {
        row = 2;
    }
    return EvMsgDispSub(ctrl, row);
}

// ──────────────────────────────────────────────────────────────────────
// SUBTITLE_DISP_REQ "112": subtitle args[0] with stream args[1].
// FF3 0x0019e878

int EvSubTitleDispReq(EV_EXE_CTRL *ctrl)
{
    SubTitleReq(ctrl->args[0], ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SUBTITLE3D_OBJ_DISP_REQ "11222": at object (args[2], args[3]).
// FF3 0x0019e8a0

int EvSubTitle3DObjDispReq(EV_EXE_CTRL *ctrl)
{
#ifdef REIPAN_SLPS_25544_CHS
    // This handler is a no-op in SLPS_255.44 (0x0019d1d0).
    (void)ctrl;
#else
    int *args = ctrl->args;

    SubTitleReq3DObj(args[0], args[1], args[2], args[3]);
#endif
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SUBTITLE3D_POS_DISP_REQ "112444": at position args[2..4].
// FF3 0x0019e8d0

int EvSubTitle3DPosDispReq(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)args[2];
    pos[1] = (float)args[3];
    pos[2] = (float)args[4];
    pos[3] = 0.0f;
    SubTitleReq3D(args[0], args[1], pos);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SUBTITLE_STOP "112".
// FF3 0x0019e930

int EvSubTitleStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    SubTitleStop();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EventInit: no message window yet. Returns 0.
// FF3 0x0019e950

int EvMsgCtrlInit(void)
{
    ev_msg_anim = -1;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019e960

void EvMsgCtrlRelease(void)
{
    EvMsgCtrlInit();
}

// ──────────────────────────────────────────────────────────────────────
// Leave the viewfinder before an event takes the camera: step 0 starts
// leaving (0), step 1 is 1 once out.
// FF3 0x0019e978

static int EvFinderModeExitWait(int step)
{
    if (step == 0) {
        PlyrFinderModeEnd();
        return 0;
    }
    if (step == 1) {
        return PlyrIsFinderModeOff() != 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// EV_CAM "211": args[0] 0 back to the map camera, 1 the event camera (after
// the viewfinder is left).
// FF3 0x0019e9c8

int EvEvCam(EV_EXE_CTRL *ctrl)
{
    u_char step = ctrl->process;

    if (ctrl->args[0] == 0) {
        EventCameraCut();
        return 1;
    }
    if (ctrl->args[0] == 1) {
        if (EvFinderModeExitWait(step) == 0) {
            if (step == 0) {
                ctrl->process = 1;
            }
            return 0;
        }
        EventCameraReq();
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EV_CAM_VCI "112": the camera of VCI member args[1] (after the
// viewfinder is left).
// FF3 0x0019ea50

int EvSetEvCamVCI(EV_EXE_CTRL *ctrl)
{
    u_char step = ctrl->process;

    if (EvFinderModeExitWait(step) == 0) {
        if (step == 0) {
            ctrl->process = 1;
        }
        return 0;
    }
    GetEvCamVci()->SetEvCam(ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// DELETE_EV_CAM_VCI "4".
// FF3 0x0019eac0

int EvDeleteEvCamVci(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    GetEvCamVci()->DelEvCam();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EV_CAM_VP "2222": camera position args[0..2].
// FF3 0x0019eae8

int EvSetEvCamVP(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)args[0];
    pos[1] = (float)args[1];
    pos[2] = (float)args[2];
    pos[3] = 0.0f;
    EventCameraSetPosition(pos);
    EventCameraSetChgFlg(1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EV_CAM_VR "2222": camera target args[0..2].
// FF3 0x0019eb48

int EvSetEvCamVR(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)args[0];
    pos[1] = (float)args[1];
    pos[2] = (float)args[2];
    pos[3] = 0.0f;
    EventCameraSetTarget(pos);
    EventCameraSetChgFlg(1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EV_CAM_ROT "22": roll args[0] / 100 (sdata 0x37023c).
// FF3 0x0019eba8

int EvSetEvCamRot(EV_EXE_CTRL *ctrl)
{
    EventCameraSetRoll((float)ctrl->args[0] * 0.01f);
    EventCameraSetChgFlg(1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EV_CAM_PROJ "22": fov args[0] / 100 (sdata 0x370240).
// FF3 0x0019ebe8

int EvSetEvCamProj(EV_EXE_CTRL *ctrl)
{
    EventCameraSetFov((float)ctrl->args[0] * 0.01f);
    EventCameraSetChgFlg(1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_CAM_VP_SET_OBJ / EV_CAM_VR_SET_OBJ "11222224", read from the command
// bytes: +1 object type, +2/+4/+6 the offset (signed), +8 the margin
// (unsigned), +0xc the object id.
// FF3 0x0019ec28 / 0x0019ed18

int EvCamVPSetObj(EV_EXE_CTRL *ctrl)
{
    u_char       *addr = ctrl->event_addr;
    u_char        type;
    int           margin;
    int           id;
    sceVu0FVECTOR ofs;

    type   = Get1Byte(addr + 1);
    ofs[0] = (float)(short)Get2Byte(addr + 2);
    ofs[1] = (float)(short)Get2Byte(addr + 4);
    ofs[2] = (float)(short)Get2Byte(addr + 6);
    ofs[3] = 0.0f;
    margin = Get2Byte(addr + 8);
    id     = (int)Get4Byte(addr + 0xc);
    EventCameraSetPositionObjId(type, id);
    EventCameraSetPositionOffset(ofs);
    EventCameraSetMargin((float)margin);
    EventCameraSetChgFlg(1);
    return 1;
}

// FF3 0x0019ed18
int EvCamVRSetObj(EV_EXE_CTRL *ctrl)
{
    u_char       *addr = ctrl->event_addr;
    u_char        type;
    int           margin;
    int           id;
    sceVu0FVECTOR ofs;

    type   = Get1Byte(addr + 1);
    ofs[0] = (float)(short)Get2Byte(addr + 2);
    ofs[1] = (float)(short)Get2Byte(addr + 4);
    ofs[2] = (float)(short)Get2Byte(addr + 6);
    ofs[3] = 0.0f;
    margin = Get2Byte(addr + 8);
    id     = (int)Get4Byte(addr + 0xc);
    EventCameraSetTargetObjId(type, id);
    EventCameraSetTargetOffset(ofs);
    EventCameraSetMargin((float)margin);
    EventCameraSetChgFlg(1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_CAM_SET_WORLD_SWITCH "112".
// FF3 0x0019ee08

int EvCamSetWorldSwitch(EV_EXE_CTRL *ctrl)
{
    EventCameraSetWorldFlg(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CAM_CHANGE_TYPE "112": area args[0]'s camera type becomes args[1], at
// once when the player is in that area.
// FF3 0x0019ee30

int EvCamChangeType(EV_EXE_CTRL *ctrl)
{
    int     area = ctrl->args[0];
    int     type = ctrl->args[1];
    MAP_AREA_DAT *row = MapAreaDataGet((s_char)area);

    if (row != nullptr) {               // PC: the EE wrote to address 2 when the area was missing
        row->cam_type = (u_char)type;
    }
    if (area == GetPlyrAreaNo()) {
        MrecSetCameraSet(type);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// No command row and no caller: reads two shorts and returns 0 (a dropped
// handler, kept for the address map).
// FF3 0x0019ee88

[[maybe_unused]] static int EvReadTwoShorts(u_char *addr)
{
    Get2Byte(addr);
    Get2Byte(addr + 2);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// MSG_CHOICE "222222": message args[0] with a Yes / No choice; the event
// id of the answer (args[1] for cursor 0 / 2, args[2] for 1 / 3) is kept in
// ev_msg_choice_no for the condition SELECT_CHOICE.
// callcheck: Get2Byte is SELECT_CHOICE's (0x19f008, inside this body in the cached features until the mkfunc)
// FF3 0x0019eee0

int EvMsgChoice(EV_EXE_CTRL *ctrl)
{
    int csr;
    int sel;

    if (ctrl->process == 0) {
        SetEventWaitFlg(1);
        ev_msg_choice_anim = LayoutAnimStart(5, 0);
        Anim2dMsgSet(1, ctrl->args[0]);
        Anim2dMsgSetType(1);
        LayoutMsgSetFlags(0);
        LayoutMsgSetKeepOpen(0);
        LayoutMsgSetArg(1);
        LockTblInc(9);
        ctrl->process = 1;
        return 0;
    }
    if (ctrl->process != 1) {
        return 0;
    }
    if (LayoutAnimGetState(ev_msg_choice_anim) != 4) {
        return 0;
    }

    SetEventWaitFlg(0);
    csr = Anim2dMsgGetCsr();
    sel = (csr == 1 || csr == 3) ? 1 : 0;
    ev_msg_choice_no = (short)ctrl->args[sel + 1];
    LockTblDec(9);
    LayoutAnimReset(ev_msg_choice_anim);
    ev_msg_choice_anim = -1;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Condition SELECT_CHOICE (row 52, "11"): MSG_CHOICE's answer is the short
// at +2. Was hidden after EvMsgChoice (no Ghidra function until 2026-10-14).
// FF3 0x0019f008

int EvSelectChoice(int event_id, u_char *addr)
{
    (void)event_id;
    return ev_msg_choice_no == (short)Get2Byte(addr + 2);
}

// ──────────────────────────────────────────────────────────────────────
// SET_PLAYER_HIT "22": 0 the player's +0x264 to 0, 1 back to 4.8 (sdata
// 0x37024c, its value after init).
// FF3 0x0019f040

int EvSetPlayerHit(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        PlyrSet264(0.0f);
    } else if (ctrl->args[0] == 1) {
        PlyrSet264(4.8f);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_DREAM_PLAYER "112".
// FF3 0x0019f090

int EvSetDreamPlayer(EV_EXE_CTRL *ctrl)
{
    int  no = ctrl->args[0];
    int *p  = GetEvDreamPlayerWrk();

    if (p != nullptr) {                 // PC: a stub returns nullptr
        *p = no;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_PAD "22": 0 locks pad 0, 1 unlocks it.
// FF3 0x0019f0c0

int EvPlyrPad(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        PadLockSet(0);
    } else if (ctrl->args[0] == 1) {
        PadLockReset(0);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Takes (arg1 == 0: sets, else counts up) the lock ids 0xc, 3, 5, 0xe, 0xf
// and 0x10, and 9 too when arg0 is set. Story_Main's area change calls it
// with (1, 1).
// FF3 0x0019f110

void AreaChange(int arg0, int arg1)
{
    static const int lock_id[6] = { 0xc, 3, 5, 0xe, 0xf, 0x10 };
    int i;

    for (i = 0; i < 6; i++) {
        if (arg1 == 0) {
            LockTblSet((u_char)lock_id[i]);
        } else {
            LockTblInc((u_char)lock_id[i]);
        }
    }

    if (arg0 != 0) {
        if (arg1 == 0) {
            LockTblSet(9);
        } else {
            LockTblInc(9);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// AreaChange's undo: (dec == 0: resets, else counts down) the same lock ids
// and 9.
// FF3 0x0019f1e0

void AreaChangeEnd(int dec)
{
    static const int lock_id[7] = { 0xc, 3, 5, 0xe, 0xf, 0x10, 9 };
    int i;

    for (i = 0; i < 7; i++) {
        if (dec == 0) {
            LockTblReset((u_char)lock_id[i]);
        } else {
            LockTblDec((u_char)lock_id[i]);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_LOCK "211": args[0] 0 frees the player (locks reset, flag 1 off), 1
// locks it (AreaChange's locks, with 9 when args[1] is 1; flag 1 on, the
// anime number kept, the turn flags cleared).
// FF3 0x0019f288

int EvPlyrLock(EV_EXE_CTRL *ctrl)
{
    PLYR_WRK *plyr;

    if (ctrl->args[0] == 0) {
        AreaChangeEnd(0);
        plyr = ingame_wrk.plyr_wrk;
        plyr->flags &= ~(u_long)1;
    } else if (ctrl->args[0] == 1) {
        AreaChange(ctrl->args[1] == 1, 0);
        plyr = ingame_wrk.plyr_wrk;
        plyr->flags |= 1;
        plyr->_193 = plyr->anime_no;
        PlyrTurnFlagClear();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_FLOOR_CHANGE "22".
// FF3 0x0019f328

int EvPlyrFloorChange(EV_EXE_CTRL *ctrl)
{
    MapAreaFloorChange(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// HIDE "112": the current player into mode 10.
// FF3 0x0019f350

int EvHide(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    PlyrMode10Start(PlyrGetCurWrk());
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_CANDLE_POWER "112": the candle's power args[1] (converted unsigned,
// so a negative one clamps to 100). The ROM caches GetCandleControl's
// static in sbss 0x372960 (shared with 0x1506f0 and GO_REAL_WORLD); the
// getter returns the same object every time, so the port calls it.
// FF3 0x0019f378

int EvSetCandlePower(EV_EXE_CTRL *ctrl)
{
    u_int power = (u_int)ctrl->args[1];

    GetCandleControl()->SetPower((float)power);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_CANDLE_ENABLE "112".
// FF3 0x0019f3e8

int EvSetCandleEnable(EV_EXE_CTRL *ctrl)
{
    GetCandleControl()->SetEnable(ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_FINDER_MODE "1111": args[1] 1 into the viewfinder, 0 out of it;
// done at once when the player is already there, else when it arrived.
// FF3 0x0019f430

int EvPlyrFinderMode(EV_EXE_CTRL *ctrl)
{
    switch (ctrl->process) {
    case 0:
        if ((ctrl->args[1] == 1) == (PlyrGetFinderState() != 0)) {
            return 1;
        }
        if (ctrl->args[1] == 0) {
            PlyrFinderModeEnd();
            ctrl->process = 2;
        } else if (ctrl->args[1] == 1) {
            PlyrFinderModeStart();
            ctrl->process = 1;
        }
        return 0;
    case 1:
        return (ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0;
    case 2:
        return PlyrIsFinderModeOff() != 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_DAMAGE_REQUEST "1111": nothing in FF3.
// FF3 0x0019f528

int EvPlyrDamageRequest(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_FLASHLIGHT_SET "112": the current player's model of kind 2 with
// motion args[0] != 0 (nothing while ingame_wrk.b03, the world byte, is 1; a
// player other than 0 asked for 0 only gets anime 0). Lock 6 is held while
// the model loads: step 0 takes it, 4 waits a frame, 1 changes the model, 2
// waits until every model is ready.
// FF3 0x0019f530

int EvPlyrFlashLightSet(EV_EXE_CTRL *ctrl)
{
    u_char cur;

    if ((s_char)ingame_wrk.b03 == 1) {
        return 1;
    }

    cur = PlyrGetCurNo();
    switch (ctrl->process) {
    case 0:
        if (cur != 0 && ctrl->args[0] == 0) {
            SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
            return 1;
        }
        LockTblInc(6);
        ctrl->process = 4;
        return 0;
    case 1:
        if (PlyrMdlChange(cur, 2, (s_char)(ctrl->args[0] != 0)) != 0) {
            ctrl->process = 2;
            return 0;
        }
        SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
        LockTblDec(6);
        return 1;
    case 2:
        if (AllPlyrIsReady() == 0) {
            return 0;
        }
        AllPlyrMdlIsLoaded();
        LockTblDec(6);
        return 1;
    case 4:
        ctrl->process = 1;
        return 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_GAZE_POINT_OBJ_SET "1124", PLYR_GAZE_POINT_POS_SET "11222" and
// PLYR_GAZE_POINT_DEF_SET "112" (one handler for both last two): nothing
// in FF3.
// FF3 0x0019f658 / 0x0019f660

int EvPlyrGazePointObjSet(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x0019f660
int EvPlyrGazePointPosSet(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_PLYR_LOOK_DIRECT "11222": look at args[1..3] for args[0] seconds.
// FF3 0x0019f670

int EvSetPlyrLookDirect(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)(short)args[1];
    pos[2] = (float)(short)args[3];
    pos[1] = (float)(short)args[2];
    pos[3] = 1.0f;                      // sdata 0x370250
    PlyrLookDirectSet(pos, (u_char)args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STOP_PLYR_LOOK_DIRECT "4".
// FF3 0x0019f6d8

int EvStopPlyrLookDirect(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    PlyrLookDirectStop();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_REQ_PL_SURPRISE "4": the player's surprise (mode 11).
// FF3 0x0019f6f8

int EvReqPlSurprise(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    PlyrMode11Start();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CHANGE_TATTOO_LEVEL "211": player args[0]'s costume value args[1].
// FF3 0x0019f718

int EvChangeTattooLevel(EV_EXE_CTRL *ctrl)
{
    PlyrSetCostumeSub((s_char)ctrl->args[0], (u_char)ctrl->args[1]);
    return 1;
}

// FF3 0x0019f740 is a compiler atexit thunk (CandleControl's destructor
// 0x2ad380 on GetCandleControl's static 0x41e540); the host runs the static's
// destructor itself.

// ──────────────────────────────────────────────────────────────────────
// CINE_MENU_START "4".
// FF3 0x0019f760

int EvCineMenuStart(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    CineMenuReq();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CINE_FILM_PLAY "112": the projector of area args[0], loaded with the film
// of row args[1] (film 6 reads as 7 once 6 was played through).
// FF3 0x0019f780

int EvCineFilmPlay(EV_EXE_CTRL *ctrl)
{
    int area = ctrl->args[0];

    CineFilmPlayReq(area, CineFilmTblGetFile(CineFilmSelect(ctrl->args[1])));
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CINE_FILM_STOP "4".
// FF3 0x0019f7c8

int EvCineFilmStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    CineFilmStop();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_SET_STATE "112": event args[1] to state args[0].
// FF3 0x0019f7e8

int EvEvSetState(EV_EXE_CTRL *ctrl)
{
    Req_CompulsionSetEventState(ctrl->event_id, ctrl->args[1], (u_char)ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_SET_TIMER "1124": after args[2], event args[1] to state args[0].
// FF3 0x0019f818

int EvEvSetTimer(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;

    EvTimerRegist(ctrl->event_id, args[2], args[1], (u_char)args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_STOP "44": wait args[0] frames (not counted while the executor is
// paused).
// FF3 0x0019f848

int EvEvStop(EV_EXE_CTRL *ctrl)
{
    u_int left;

    if (ctrl->stop_timer == 0) {
        left = (u_int)ctrl->args[0];
    } else {
        if (GetEvWrkWaitFlg() != 0) {
            return 0;
        }
        left = ctrl->stop_timer - 1;
    }
    ctrl->stop_timer = left;
    return left == 0;
}

// ──────────────────────────────────────────────────────────────────────
// SET_CONDITION_CHECK "1111": args[1] switches the open conditions
// (args[0] 0) or the close conditions (else) on or off.
// FF3 0x0019f8a8

int EvSetConditionCheck(EV_EXE_CTRL *ctrl)
{
    u_char arg = (u_char)ctrl->args[1];

    if ((u_char)ctrl->args[0] == 0) {
        SetOpenCondSwitch(arg);
    } else {
        SetEndCondSwitch(arg);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// IF_ "4": the condition after the opcode (its row id at +4) decides the
// branch; its size lets the executor skip it.
// FF3 0x0019f8f0

int EvIf(EV_EXE_CTRL *ctrl)
{
    u_char cond = ctrl->event_addr[4];
    int    ret  = EvCondCallJudge(cond, ctrl->event_id, ctrl->event_addr + 4);

    ctrl->cond_size = EvCondGetSize(cond);
    ctrl->if_state  = (ret != 1) ? EV_IF_FALSE : EV_IF_TRUE;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// ELSE_ "4".
// FF3 0x0019f960

int EvElse(EV_EXE_CTRL *ctrl)
{
    if (ctrl->if_state == EV_IF_TRUE || ctrl->if_state == EV_IF_DONE) {
        ctrl->if_state = EV_IF_DONE;
    } else {
        ctrl->if_state = EV_IF_TRUE;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// ELSEIF_ "4": skipped once a branch was taken, else an IF_.
// FF3 0x0019f988

int EvElseif(EV_EXE_CTRL *ctrl)
{
    if (ctrl->if_state == EV_IF_TRUE || ctrl->if_state == EV_IF_DONE) {
        ctrl->cond_size = EvCondGetSize(ctrl->event_addr[4]);
        ctrl->if_state  = EV_IF_DONE;
    } else {
        EvIf(ctrl);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// ENDIF_ "4".
// FF3 0x0019f9f0

int EvEndif(EV_EXE_CTRL *ctrl)
{
    ctrl->if_state = EV_IF_NONE;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EVENT_REBOOT "112": every event in state args[0] back to state 0,
// waiting for its open condition.
// FF3 0x0019fa00

int EvEventReboot(EV_EXE_CTRL *ctrl)
{
    int state = ctrl->args[0];
    int i;

    for (i = 0; i < EV_EVENT_NUM; i++) {
        if (GetEvState(i) == state) {
            SetEventState(i, 0);
            EventSetOpenCondition(i);
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// DOOR_OPEN / DOOR_CLOSE "112": nothing in FF3.
// FF3 0x0019fa68 / 0x0019fa70

int EvDoorOpen(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x0019fa70
int EvDoorClose(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PRIVATE_DOOR_LOCK "1124": door args[2]; args[0] 0..4 its lock state, 5 /
// 6 sets / clears its no-use flag (the door action check skips it).
// FF3 0x0019fa78

int EvPrivateDoorLock(EV_EXE_CTRL *ctrl)
{
    int door = ctrl->args[2];
    int type = ctrl->args[0];

    if (type == 5) {
        DoorStatSetNoUse(door, 1);
    } else if (type == 6) {
        DoorStatSetNoUse(door, 0);
    } else if (type >= 0 && type < 5) {
        DoorStatSetLock(door, (u_int)type);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PRIVATE_DOOR_UNLOCK "224": door args[1] to lock state 0.
// FF3 0x0019faf0

int EvPrivateDoorUnlock(EV_EXE_CTRL *ctrl)
{
    DoorStatSetLock(ctrl->args[1], 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Save block 14: the screen effect (and fade lock) to restore after a load.
// FF3 0x0019fb18

int EvSaveScreenEffectSaveGet(void **addr)
{
    *addr = &ev_save_screen_effect;
    return 8;
}

// ──────────────────────────────────────────────────────────────────────
// Save block 15: the dither effect to restart after a load.
// FF3 0x0019fb28

int EvSaveEffDitherSaveGet(void **addr)
{
    *addr = &ev_save_eff_dither;
    return 6;
}

// ──────────────────────────────────────────────────────────────────────
// FF2 EvEffCtrlInit, plus EventMacroInit's synchro_mode_flg = 0.
// FF3 0x0019fb38; FF2 EvEffCtrlInit 0x00180e48

void EvEffCtrlInit(void)
{
    DelEvEffDitherID(0);
    synchro_mode_flg = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fb58; FF2 SetEvEffDitherID (FF3 indexes two slots)

void SetEvEffDitherID(int no, void *dither_id)
{
    ev_eff_dither_id[no] = dither_id;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fb70; FF2 DelEvEffDitherID

void DelEvEffDitherID(int no)
{
    ev_eff_dither_id[no] = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fb88; FF2 GetEvEffDitherID

void *GetEvEffDitherID(int no)
{
    return ev_eff_dither_id[no];
}

// ──────────────────────────────────────────────────────────────────────
// After a load: the saved screen effect again (FF3 dropped FF2's test for 0;
// 0 is a valid "no effect").
// FF3 0x0019fba0; FF2 EventMacroLoad_ScreenEffectInit

void EventMacroLoad_ScreenEffectInit(void)
{
    EffectSetScreenEffectNo(ev_save_screen_effect.eff_number);
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fbc0; FF2 EvSaveScreenEffectInit (FF3 clears the fade lock too)

void EvSaveScreenEffectInit(void)
{
    ev_save_screen_effect.fade_lock = 0;
    ev_save_screen_effect.eff_number = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fbd0; FF2 SetEvSaveScreenEffect

void SetEvSaveScreenEffect(int eff_number)
{
    ev_save_screen_effect.eff_number = eff_number;
}

// ──────────────────────────────────────────────────────────────────────
// After a load: the saved dither effect again. The ROM passes the saved
// bytes as plain ints where EV_EFF_DITHER_START passes alpha and speed as
// doubles, so the effect read the alpha / speed slots as doubles whose bit
// patterns are small integers: denormals that convert to 0.0f. The port
// passes those 0.0 doubles (MioPan does the same for FF2's identical code).
// FF3 0x0019fbe0; FF2 EventMacroLoad_EffDitherInit 0x0017fb70

void EventMacroLoad_EffDitherInit(void)
{
    void *dither_id;

    if (ev_save_eff_dither.set_flg == 1) {
        // ROM: SetEffects(2, 2, type, alpha, speed, alpha_max, col_max)
        dither_id = SetEffects(2, 2, (int)ev_save_eff_dither.type, 0.0, 0.0,
                               (int)ev_save_eff_dither.alpha_max,
                               (int)ev_save_eff_dither.col_max);
        SetEvEffDitherID(0, dither_id);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fc48; FF2 SetEvSaveEffDither

void SetEvSaveEffDither(u_char type, u_char speed, u_char alpha, u_char alpha_max, u_char col_max)
{
    ev_save_eff_dither.type = type;
    ev_save_eff_dither.speed = speed;
    ev_save_eff_dither.alpha = alpha;
    ev_save_eff_dither.alpha_max = alpha_max;
    ev_save_eff_dither.col_max = col_max;
    ev_save_eff_dither.set_flg = 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x0019fc90; FF2 EvSaveEffDitherInit (EV_EFF_DITHER_END calls it too)

void EvSaveEffDitherInit(void)
{
    memset(&ev_save_eff_dither, 0, sizeof(ev_save_eff_dither));
}

// ──────────────────────────────────────────────────────────────────────
// SET_EFF_VISIBLE "11222": the map object (args[0], args[1], args[3]) shows
// the parts of visibility pattern args[2] (a bit mask 0..7 by index) and
// hides the others; then the candle effects follow.
// FF3 0x0019fcb8

int EvSetEffVisible(EV_EXE_CTRL *ctrl)
{
    static const u_int vis_tbl[6] = { 0, 7, 2, 5, 4, 3 };
    int         *args = ctrl->args;
    FURN_SAVE_ROW *obj;
    u_int        vis;

    obj = FurnSaveFind(args[0], 0, (short)args[1], args[3]);
    vis = vis_tbl[args[2]];
    if (vis != 0) {
        FurnSaveSetVisible(obj, vis, 1);
    }
    if ((vis ^ 7) != 0) {
        FurnSaveSetVisible(obj, vis ^ 7, 0);
    }
    MapEffVisibleUpdate();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_EFF_THUNDER_REQ "112444444", read from the command bytes: +2 the time,
// +4..+0xc the position, +0x10..+0x18 a second vector.
// FF3 0x0019fd80

int EvEffThunderReq(EV_EXE_CTRL *ctrl)
{
    u_char       *addr = ctrl->event_addr;
    sceVu0FVECTOR pos  = { 0.0f, 0.0f, 0.0f, 0.0f };
    sceVu0FVECTOR v    = { 0.0f, 0.0f, 0.0f, 0.0f };
    int           time;

    time   = Get2Byte(addr + 2);
    pos[0] = (float)(int)Get4Byte(addr + 4);
    pos[1] = (float)(int)Get4Byte(addr + 8);
    pos[2] = (float)(int)Get4Byte(addr + 0xc);
    v[0]   = (float)(int)Get4Byte(addr + 0x10);
    v[1]   = (float)(int)Get4Byte(addr + 0x14);
    v[2]   = (float)(int)Get4Byte(addr + 0x18);
    EffThunderReq(pos, time, v);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_SET_SCREEN_EFFECT "112": screen effect args[1], saved for a load.
// FF3 0x0019fe70

int EvSetScreenEffect(EV_EXE_CTRL *ctrl)
{
    int no = ctrl->args[1];

    EffectSetScreenEffectNo((u_int)no);
    SetEvSaveScreenEffect(no);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_BUTTERFLY "1111444": butterfly args[0] of type args[1] at args[3..5]
// (also its first target).
// FF3 0x0019fea8

int EvSetButterfly(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)args[3];
    pos[1] = (float)args[4];
    pos[2] = (float)args[5];
    pos[3] = 1.0f;                      // sdata 0x370258
    EffectButterflyReqTarget(args[0], args[1], pos, pos);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MOVE_BUTTERFLY "112444": butterfly args[0] flies to args[2..4].
// FF3 0x0019ff10

int EvMoveButterfly(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    sceVu0FVECTOR pos;

    pos[0] = (float)args[2];
    pos[1] = (float)args[3];
    pos[2] = (float)args[4];
    pos[3] = 1.0f;                      // sdata 0x37025c
    EffectButterflyChangeTarget(args[0], pos);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// RELEASE_BUTTERFLY "112".
// FF3 0x0019ff70

int EvReleaseButterfly(EV_EXE_CTRL *ctrl)
{
    EffectButterflyFadeOut(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_EFF_DITHER_START "1111112", read from the command bytes: +1 type, +2
// speed, +3 alpha, +4 alpha max, +5 colour max; alpha and speed go to the
// effect as doubles (variadic floats). Saved for a load.
// callcheck: FUN_0019fc48, fptodp (SetEvSaveEffDither; the double casts)
// FF3 0x0019ff98; FF2 Event_EvEffDitherStart

int EvEffDitherStart(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;
    u_char  type, speed, alpha, alpha_max, col_max;
    void   *dither_id;

    type      = Get1Byte(addr + 1);
    speed     = Get1Byte(addr + 2);
    alpha     = Get1Byte(addr + 3);
    alpha_max = Get1Byte(addr + 4);
    col_max   = Get1Byte(addr + 5);
    dither_id = SetEffects(2, 2, (int)type, (double)(float)alpha, (double)(float)speed,
                           (int)alpha_max, (int)col_max);
    SetEvEffDitherID(0, dither_id);
    SetEvSaveEffDither(type, speed, alpha, alpha_max, col_max);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_EFF_DITHER_END "112".
// FF3 0x001a0098; FF2 Event_EvEffDitherEnd

int EvEffDitherEnd(EV_EXE_CTRL *ctrl)
{
    void *dither_id = GetEvEffDitherID(0);

    (void)ctrl;
    if (dither_id != nullptr) {
        ResetEffects(dither_id);
        DelEvEffDitherID(0);
        EvSaveEffDitherInit();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SYNCHRO_MODE_START / SYNCHRO_MODE_END "112": nothing in FF3 (FF2 set
// synchro_mode_flg; FF3 only clears it in EvEffCtrlInit).
// FF3 0x001a00d8 / 0x001a00e0

int EvSynchroModeStart(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a00e0
int EvSynchroModeEnd(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_OVER_LAP_START "112": the overlap effect (id 6) for the time at +2.
// FF3 0x001a00e8

int EvOverLapStart(EV_EXE_CTRL *ctrl)
{
    SetEffects(6, 2, (int)Get2Byte(ctrl->event_addr + 2));
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_OVER_LAP_END "112".
// FF3 0x001a0120

int EvOverLapEnd(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    CutEffects(6);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FADE_IN "11114": colour args[0..2] over args[3] frames; releases the fade
// lock (lock 1) FADE_OUT took.
// FF3 0x001a0140

int EvFadeIn(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;

    FadeInReq((u_char)args[0], (u_char)args[1], (u_char)args[2], args[3]);
    if (ev_save_screen_effect.fade_lock != 0) {
        ev_save_screen_effect.fade_lock = 0;
        LockTblDec(1);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FADE_OUT "11114": and holds lock 1 until the next FADE_IN.
// FF3 0x001a0188

int EvFadeOut(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;

    FadeOutReq((u_char)args[0], (u_char)args[1], (u_char)args[2], args[3]);
    if (ev_save_screen_effect.fade_lock == 0) {
        ev_save_screen_effect.fade_lock = 1;
        LockTblInc(1);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MONO_DISP "1111": the byte at +2, 1 on / 0 off (others: nothing).
// FF3 0x001a01d0

int EvMonoDisp(EV_EXE_CTRL *ctrl)
{
    u_char on = Get1Byte(ctrl->event_addr + 2);

    if (on == 1) {
        EffectMonochromeSet(1, 1);
    } else if (on == 0) {
        EffectMonochromeSet(0, 1);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FURN_CANDLE_BLAZE_UP "112".
// FF3 0x001a0220

int EvFurnCandleBlazeUp(EV_EXE_CTRL *ctrl)
{
    MapEffBlazeUp(ctrl->args[0], ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_HANDLIGHTEFFECT_DISP "22".
// FF3 0x001a0248

int EvSetHandlighteffectDisp(EV_EXE_CTRL *ctrl)
{
    HandLightEffectSetDisp(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FADE_HIGHCONTRAST "211": to level args[0] over args[1].
// FF3 0x001a0270

int EvFadeHighcontrast(EV_EXE_CTRL *ctrl)
{
    IgEffectHighContrastFade((short)ctrl->args[0], (u_short)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_LANTERNEFFECT_ENABLE "22".
// FF3 0x001a0298

int EvSetLanterneffectEnable(EV_EXE_CTRL *ctrl)
{
    EffLanternEffectEnable(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_FOG "1111", SET_EV_FOG_COLOR "111122", SET_EV_FOG_DIST_NEAR /
// _FAR / SET_EV_FOG_DIST "11222": FF3 reads the bytes and drops them (FF2
// set the event fog).
// FF3 0x001a02c0 / 0x001a02e8 / 0x001a0340 / 0x001a0380 / 0x001a03c0

int EvEvFog(EV_EXE_CTRL *ctrl)
{
    Get1Byte(ctrl->event_addr + 2);
    return 1;
}

// FF3 0x001a02e8
int EvSetEvFogColor(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;

    Get1Byte(addr + 1);
    Get1Byte(addr + 2);
    Get1Byte(addr + 3);
    Get2Byte(addr + 4);
    return 1;
}

// FF3 0x001a0340
int EvSetEvFogDistNear(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;

    Get2Byte(addr + 2);
    Get2Byte(addr + 4);
    return 1;
}

// FF3 0x001a0380
int EvSetEvFogDistFar(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;

    Get2Byte(addr + 2);
    Get2Byte(addr + 4);
    return 1;
}

// FF3 0x001a03c0
int EvSetEvFogDist(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;

    Get2Byte(addr + 2);
    Get2Byte(addr + 4);
    Get2Byte(addr + 6);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PHOTO_LOCK / PHOTO_UNLOCK "4", MENU_LOCK / MENU_UNLOCK and PAUSE_LOCK /
// PAUSE_UNLOCK "112": lock ids 0xc, 0xf and 0x10.
// FF3 0x001a0408 / 0x001a0428 / 0x001a0448 / 0x001a0468 /
// FF3 0x001a0488 / 0x001a04a8

// FF3 0x001a0408
int EvPhotoLock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblSet(0xc);
    return 1;
}

// FF3 0x001a0428
int EvPhotoUnlock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblReset(0xc);
    return 1;
}

// FF3 0x001a0448
int EvMenuLock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblSet(0xf);
    return 1;
}

// FF3 0x001a0468
int EvMenuUnlock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblReset(0xf);
    return 1;
}

// FF3 0x001a0488
int EvPauseLock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblSet(0x10);
    return 1;
}

// FF3 0x001a04a8
int EvPauseUnlock(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    LockTblReset(0x10);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_SP_GAMEOVER_ "112", SET_NIGHT_LOCK_FLG / SET_RADIO_LOCK_FLG /
// SET_MAP_IN_LOCK_FLG "22": args[0] 1 sets, 0 resets lock 0x18 / 0x13 /
// 0x1a / 0x1b.
// FF3 0x001a04c8 / 0x001a0518 / 0x001a0568 / 0x001a05b8

// FF3 0x001a04c8
int EvSetSpGameover(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        LockTblReset(0x18);
    } else if (ctrl->args[0] == 1) {
        LockTblSet(0x18);
    }
    return 1;
}

// FF3 0x001a0518
int EvSetNightLockFlg(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        LockTblReset(0x13);
    } else if (ctrl->args[0] == 1) {
        LockTblSet(0x13);
    }
    return 1;
}

// FF3 0x001a0568
int EvSetRadioLockFlg(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        LockTblReset(0x1a);
    } else if (ctrl->args[0] == 1) {
        LockTblSet(0x1a);
    }
    return 1;
}

// FF3 0x001a05b8
int EvSetMapInLockFlg(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        LockTblReset(0x1b);
    } else if (ctrl->args[0] == 1) {
        LockTblSet(0x1b);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_PHOTO_CURSE, FILAMENT_TIMER_CALL "112", FILAMENT_CALL /
// FILAMENT_RELEASE "4": nothing in FF3.
// FF3 0x001a0608 / 0x001a0610 / 0x001a0618 / 0x001a0620

int EvSetPhotoCurse(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a0610
int EvFilamentTimerCall(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a0618
int EvFilamentCall(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a0620
int EvFilamentRelease(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_OBJ_ALGORITHM "1124": algorithm kind args[0] (0..2, 5; others and
// negatives: nothing) for object args[2] with args[1]; waits until it is
// accepted.
// FF3 0x001a0628

int EvSetObjAlgorithm(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;
    int  kind = args[0];

    if (kind >= 0) {
        if (kind > 2 && kind != 5) {
            return 1;
        }
        if (EneActReqById(kind, args[2], args[1]) == 0) {
            return 0;
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_GHOST_SEAL_DOOR / RELEASE_GHOST_SEAL_DOOR "112": nothing in FF3.
// FF3 0x001a0680 / 0x001a0688

int EvSetGhostSealDoor(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a0688
int EvReleaseGhostSealDoor(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CALL_GHOST_SEAL_DOOR_BURN "112": loads burn file args[1] with the player
// locked (15 lock ids, rodata 0x358318), then frees it and the locks.
// FF3 0x001a0690

int EvCallGhostSealDoorBurn(EV_EXE_CTRL *ctrl)
{
    static const int lock_id[15] = { 2, 3, 4, 5, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x14, 0x15, 0x16, 0x1a };
    int i;

    if (ctrl->process == 0) {
        int no = ctrl->args[1];

        for (i = 0; i < 15; i++) {
            LockTblInc(lock_id[i]);
        }
        EffSealDoorBurnLoad(no);
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (EffSealDoorBurnIsEnd() == 0) {
            return 0;
        }
        EffSealDoorBurnRelease();
        for (i = 0; i < 15; i++) {
            LockTblDec(lock_id[i]);
        }
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// EV_PAD_WAIT "22": done when the button of game action args[0] is pressed.
// FF3 0x001a0760

int EvPadWait(EV_EXE_CTRL *ctrl)
{
    MenuMouseClickMask(pad_key[ctrl->args[0]]);     // PC: a click is the press it waits for
    return CheckPadTrgPlaySe((u_short)pad_key[ctrl->args[0]], -1, 0) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// EV_CHANGE_WORLD "112": the world byte ingame_wrk.b03 = args[0].
// FF3 0x001a07a0

int EvChangeWorld(EV_EXE_CTRL *ctrl)
{
    ingame_wrk.b03 = (u_char)ctrl->args[0];
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_SLEEP_FLG_ "1124": args[0] 0 / 1 picks the scene-test value control
// C / I with (args[1], args[2]).
// FF3 0x001a07c0

int EvSetSleepFlg(EV_EXE_CTRL *ctrl)
{
    int  *args = ctrl->args;
    short a    = (short)args[1];
    short b    = (short)args[2];

    if (args[0] == 0) {
        SleepFlgClear(a, b);
    } else if (args[0] == 1) {
        SleepFlgSet(a, b);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_NIGHT_FLG "112": the world side args[0] (0 / 1; others -1) when it is
// not the current one: at once (args[1] 0) or through phase 0x23
// (NightChangeReq, without a fade).
// FF3 0x001a0820

int EvSetNightFlg(EV_EXE_CTRL *ctrl)
{
    int  on   = ctrl->args[0];
    int  how  = ctrl->args[1];
    int  side = -1;

    if (on == 0) {
        side = 0;
    } else if (on == 1) {
        side = 1;
    }
    if (MapAreaSideGet() != side) {
        if (how == 0) {
            MapAreaSideSet((u_int)side);
        } else {
            NightChangeReq(side, 0);
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_NIGHT_START_POS "222222": the room args[0], x / z args[1..2], the
// facing in degrees args[3]; args[4] is passed but unused.
// FF3 0x001a08a0

int EvSetNightStartPos(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;

    NightStartPosSet(args[0], args[4], (float)args[1], (float)args[2], (float)args[3]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GO_DREAM_WORLD / GO_REAL_WORLD / PLYR_MODEL_CHANGE: the player model
// change around a world switch. Start: PlyrMdlChange (kind) with lock 6 held
// and the new player's door motion pack loading (MapDoorLoadReq); refused: the player
// back to anime 0 and 0 returned.
// FF3 0x001a1f58

static int EvPlyrMdlChangeStart(int no, int kind)
{
    int ok = PlyrMdlChange((u_char)no, (u_char)kind, -1);

    if (ok == 0) {
        SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
    } else {
        LockTblInc(6);
        MapPutSetDrawStop(1);
        MapDoorLoadReq(no);
    }
    return ok != 0;
}

// The change's wait: 1 once every model is ready and the data loaded; then
// lock 6 off and the map objects' models again (MapObjDoorMdlRebuildAll).
// FF3 0x001a1fd0

static int EvPlyrMdlChangeWait(void)
{
    if (AllPlyrIsReady() == 0) {
        return 0;
    }
    if (MapDoorCheckLoad() == 0) {
        return 0;
    }
    AllPlyrMdlIsLoaded();
    LockTblDec(6);
    MapPutSetDrawStop(0);
    MapObjDoorMdlRebuildAll();
    return 1;
}

// Locks held while the world changes (rodata 0x358358).
static const int ev_world_lock_id[14] = {
    1, 3, 6, 0xb, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x19, 0x1a,
};

static void EvWorldLockInc(void)
{
    int i;

    for (i = 0; i < 14; i++) {
        LockTblInc(ev_world_lock_id[i]);
    }
}

static void EvWorldLockDec(void)
{
    int i;

    for (i = 0; i < 14; i++) {
        LockTblDec(ev_world_lock_id[i]);
    }
}

// ──────────────────────────────────────────────────────────────────────
// GO_DREAM_WORLD "4": steps 0 (locks) -> 4 (a frame) -> 1 (the switch)
// -> 2 (the model change). The switch counts the dream visits (b04, up to
// 98), resets the candle, HP, film and flash, drops the real world's map
// effects, special enemy objects, SE banks and camera models, then changes
// the current player's model to kind 2.
// callcheck: LockTblInc, LockTblDec (through EvWorldLockInc / EvWorldLockDec)
// FF3 0x001a08e0

int EvGoDreamWorld(EV_EXE_CTRL *ctrl)
{
    int no;

    switch (ctrl->process) {
    case 0:
        EvWorldLockInc();
        ctrl->process = 4;
        return 0;
    case 4:
        ctrl->process = 1;
        return 0;
    case 1:
        no = PlyrGetCurNo();
        if ((s_char)ingame_wrk.b04 >= 0 && (s_char)ingame_wrk.b04 < 98) {
            ingame_wrk.b04++;
        }
        GetCandleControl()->Reset();
        PlyrHpRestoreAll();
        FilmInit(GetFilmWrk());
        PlyrFlashNumInit();
        MapSnapClearFlagAll(0x11);
        DoorStatClearKnownAll();
        DoorStatSetKnownSys();
        MapSnapUpdateWorld();
        GetEneSpeObjTbl()->Release();
        SndSeBankRelease(0x1b7d, 0);
        SndSeBankRelease(0x1bf5, 0);
        SndSeBankRelease(0x1bf7, 0);
        IngamePlyrCamMdlReset(0);
        CatMdlDelete(0);
        PlyrCameraMdlRelease(1);
        PlyrCameraMdlRelease(0);
        if (EvPlyrMdlChangeStart(no, 2) == 0) {
            EvWorldLockDec();
            return 1;
        }
        ctrl->process = 2;
        return 0;
    case 2:
        if (EvPlyrMdlChangeWait() == 0) {
            return 0;
        }
        EvWorldLockDec();
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// GO_REAL_WORLD "4": the same steps back. The switch releases the event
// ghosts, enemies, footstep and layout SEs, puts the candle out, loads the
// real world's SE banks and special enemy objects, creates the layer and
// camera models (the night side, MapAreaSideGet 1, picks their kind; a
// locked camera 0 keeps its default) and changes the player's model.
// callcheck: LockTblInc, LockTblDec (through EvWorldLockInc / EvWorldLockDec)
// FF3 0x001a0ae0

int EvGoRealWorld(EV_EXE_CTRL *ctrl)
{
    CandleControl *candle;
    int            no;
    int            night;
    int            type;

    switch (ctrl->process) {
    case 0:
        EvWorldLockInc();
        ctrl->process = 4;
        return 0;
    case 4:
        ctrl->process = 1;
        return 0;
    case 1:
        no = PlyrGetCurNo();
        EvGhostCtrlReleaseAll();
        EnemyReleaseAll();
        foot_seRelease();
        GetLayoutSeWrk()->StopAll();
        candle = GetCandleControl();
        candle->SetLit(0);
        candle->out = 0;
        EffectMonochromeSet(0, 0);
        MapSnapClearFlagAll(0x11);
        DoorStatClearKnownAll();
        DoorStatSetKnownSys();
        MapSnapUpdateWorld();
        PlyrHpRestoreAll();
        SndSeBankLoad(0x1b7d);
        SndSeBankLoad(0x1bf5);
        SndSeBankLoad(0x1bf7);
        GetEneSpeObjTbl()->Start();
        CatMdlCreate(0, -1, 1);
        SleepFlgSideWake();
        night = MapAreaSideGet() == 1;
        IngamePlyrMdlReset(0);
        if (no != 1) {
            type = night;
            if (PlyrCameraIsFixed(0) != 0 && PlyrCameraIsCatMdl(GetPlyrCameraWrk(0)) != 0) {
                type = -1;
            }
            PlyrCameraMdlCreate(0, (signed char)type, 1);
        }
        if (no != 2) {
            PlyrCameraMdlCreate(1, (signed char)night, 1);
        }
        if (EvPlyrMdlChangeStart(no, night) == 0) {
            EvWorldLockDec();
            return 1;
        }
        ctrl->process = 2;
        return 0;
    case 2:
        if (EvPlyrMdlChangeWait() == 0) {
            return 0;
        }
        EvWorldLockDec();
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// DEVELOP_REQUEST "4": the film developing phase (DevelopReq: phase 0x26).
// FF3 0x001a0d58

int EvDevelopRequest(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    DevelopReq();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// RESEARCH_PERMIT "112": queue entry (kind args[0], index args[1]).
// RESEARCH_START "112": play the queue of kind args[0] with the event
// paused; done when none is left.
// FF3 0x001a0d78 / 0x001a0da0

int EvResearchPermit(EV_EXE_CTRL *ctrl)
{
    EvQueueAdd(ctrl->args[0], ctrl->args[1]);
    return 1;
}

// FF3 0x001a0da0
int EvResearchStart(EV_EXE_CTRL *ctrl)
{
    if (ctrl->process == 0) {
        SetEventWaitFlg(1);
        EvQueueStart(ctrl->args[0]);
        ctrl->process = 1;
    } else if (ctrl->process == 1 && EvQueueMain() < 0) {
        SetEventWaitFlg(0);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// RADIO_TURN_ON / RADIO_TURN_OFF "112": radio args[1].
// FF3 0x001a0e20 / 0x001a0e48

int EvRadioTurnOn(EV_EXE_CTRL *ctrl)
{
    RadioTurnOn((short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a0e48
int EvRadioTurnOff(EV_EXE_CTRL *ctrl)
{
    RadioTurnOff((short)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_WAKEUP_MOVIE / SET_SLEEP_MOVIE "112": the movie args[1], not played.
// WAKEUP_MOVIE_PLAY / SLEEP_MOVIE_PLAY "4": play it once (the sleep movie
// drops the special enemy objects first); done the frame after.
// FF3 0x001a0e70 / 0x001a0ed0 / 0x001a0f50 / 0x001a0fb0

int EvSetWakeupMovie(EV_EXE_CTRL *ctrl)
{
    FINDER_BANK_WRK *wrk = GetFinderBankWrk();

    wrk->wakeup.no = ctrl->args[1];
    wrk->wakeup.played = 0;
    return 1;
}

// FF3 0x001a0ed0
int EvWakeupMoviePlay(EV_EXE_CTRL *ctrl)
{
    FINDER_BANK_WRK *wrk;

    if (ctrl->process != 0) {
        return 1;
    }
    wrk = GetFinderBankWrk();
    wrk->MoviePlay(0);
    wrk->wakeup.played = 1;
    ctrl->process = 1;
    return 0;
}

// FF3 0x001a0f50
int EvSetSleepMovie(EV_EXE_CTRL *ctrl)
{
    FINDER_BANK_WRK *wrk = GetFinderBankWrk();

    wrk->sleep.no = ctrl->args[1];
    wrk->sleep.played = 0;
    return 1;
}

// FF3 0x001a0fb0
int EvSleepMoviePlay(EV_EXE_CTRL *ctrl)
{
    FINDER_BANK_WRK *wrk;

    if (ctrl->process != 0) {
        return 1;
    }
    GetEneSpeObjTbl()->Release();
    wrk = GetFinderBankWrk();
    wrk->MoviePlay(1);
    wrk->sleep.played = 1;
    ctrl->process = 1;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// SET_REAL_CAMERA "112": the photo mode 0 (args[0] 0) or 1, when the
// player holds the camera item for it (item 8 / 10).
// FF3 0x001a1040

int EvSetRealCamera(EV_EXE_CTRL *ctrl)
{
    u_char mode;

    if (ctrl->args[0] == 0) {
        if (*ItemGetCnt(8) == 0) {
            return 1;
        }
        mode = 0;
    } else {
        if (*ItemGetCnt(10) == 0) {
            return 1;
        }
        mode = 1;
    }
    GetPhotoModeWrk()->SetMode(mode, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MAP_UPDATE "22": MapSnapSetUpdate(args[0]) (word 0x3f16dc).
// SET_SP_EVENT "22": args[0] 0: MapSpEventSet.
// FF3 0x001a10c0 / 0x001a10e8

int EvMapUpdate(EV_EXE_CTRL *ctrl)
{
    MapSnapSetUpdate(ctrl->args[0]);
    return 1;
}

// FF3 0x001a10e8
int EvSetSpEvent(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        MapSpEventSet();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MISSION_SETUP "4": the mission's film and flash; every player's HP scaled
// by the mission's HP rate (percent, 1..100 %).
// FF3 0x001a1118

int EvMissionSetup(EV_EXE_CTRL *ctrl)
{
    FINDER_BANK_ENTRY *entry;
    float              rate;
    int                i;

    (void)ctrl;
    entry = GetFinderModeWrk()->GetDef();
    FilmSetByItem(GetFilmWrk(), 1);
    PlyrFlashNumInit();
    rate = (entry != nullptr) ? (float)entry->hp_rate / 100.0f : 1.0f;  // PC: stub guard
    if (rate < 0.01f) {
        rate = 0.01f;
    } else if (rate > 1.0f) {
        rate = 1.0f;
    }
    for (i = 0; i < 3; i++) {
        PLYR_WRK *plyr = GetPlyrWrk((s_char)i);

        plyr->hp = (int)((float)plyr->hp * rate);
    }
    HudHpSetNow(GetHudHpWrk(), rate);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CAPTION_SET "211": args[0] 0 hides, 1 shows caption args[1] on the fade
// layer.
// FF3 0x001a1218

int EvCaptionSet(EV_EXE_CTRL *ctrl)
{
    int on    = ctrl->args[0];
    int label = ctrl->args[1];

    if (on == 0) {
        GetCmnFadeLayer()->IdleHide();
    } else if (on == 1) {
        GetCmnFadeLayer()->IdleShow(label);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_MUTE_SE_VOLUME "22".
// FF3 0x001a1280

int EvSetMuteSeVolume(EV_EXE_CTRL *ctrl)
{
    SndVolume0Mute(ctrl->args[0]);
    return 1;
}

// FF3 0x001a12a8 is a compiler atexit thunk again (CandleControlStaticDtor2:
// the CandleControl destructor 0x2ad380 on GetCandleControl's static
// 0x41e540), and 0x1a12c8 an empty destructor; nothing to write.

// ──────────────────────────────────────────────────────────────────────
// GHOST_APPEAR "112": ghost (type args[0], id args[1]) loaded and started;
// waits while it loads, done once it acts (state 2 starts it, 3 runs).
// GHOST_DISAPPEAR "112": its release.
// FF3 0x001a12d8 / 0x001a1378

int EvGhostAppear(EV_EXE_CTRL *ctrl)
{
    u_char type = (u_char)ctrl->args[0];
    short  id   = (short)ctrl->args[1];

    switch (EneGetState(type, id)) {
    case 0:
        EneLoadReq(type, id);
        return 0;
    case 2:
        EnemyStart(SearchEneWrkNo(type, id));
        return 1;
    case 3:
        return 1;
    }
    return 0;
}

// FF3 0x001a1378
int EvGhostDisappear(EV_EXE_CTRL *ctrl)
{
    EneReleaseReq((u_char)ctrl->args[0], (short)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GHOST_REGIST / GHOST_PRELOAD_REGIST "11222", GHOST_DELETE "11222": ghost
// (type args[0], id args[1]) into / out of area args[2]'s event ghost table
// (preload 0 / 1).
// FF3 0x001a13a0 / 0x001a13d0 / 0x001a13e8 / 0x001a1400

static int EvGhostRegistSub(EV_EXE_CTRL *ctrl, int preload)
{
    int *args = ctrl->args;

    EvGhostTblRegist(args[2], args[0], args[1], preload);
    return 1;
}

// FF3 0x001a13d0
int EvGhostRegist(EV_EXE_CTRL *ctrl)
{
    return EvGhostRegistSub(ctrl, 0);
}

// FF3 0x001a13e8
int EvGhostPreloadRegist(EV_EXE_CTRL *ctrl)
{
    return EvGhostRegistSub(ctrl, 1);
}

// FF3 0x001a1400
int EvGhostDelete(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;

    EvGhostTblDelete(args[2], args[0], args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// RANDOM_GHOST_ON "1124" (args[1], args[2]), RANDOM_GHOST_OFF "112"
// (args[1]), RANDOM_GHOST_ALL_OFF / RANDOM_GHOST_REQ "22": the random ghost
// table (0x3f7ab0, 60 rows) of ingame/enemy/random_ghost.c.
// FF3 0x001a1430 / 0x001a1458 / 0x001a1480 / 0x001a14a0

int EvRandomGhostOn(EV_EXE_CTRL *ctrl)
{
    RandGhostOn(ctrl->args[1], ctrl->args[2]);
    return 1;
}

// FF3 0x001a1458
int EvRandomGhostOff(EV_EXE_CTRL *ctrl)
{
    RandGhostOff(ctrl->args[1]);
    return 1;
}

// FF3 0x001a1480
int EvRandomGhostAllOff(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    RandGhostAllOff();
    return 1;
}

// FF3 0x001a14a0
int EvRandomGhostReq(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    RandGhostReq();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CHANGE_GHOST_ATTR "11244": ghost (args[0], args[1])'s flags2 bits args[2]
// set (args[3] != 0) or cleared.
// FF3 0x001a14c0

int EvChangeGhostAttr(EV_EXE_CTRL *ctrl)
{
    int     *args = ctrl->args;
    u_int    bits = (u_int)args[2];
    ENE_WRK *ene  = EneGetWrk((u_char)args[0], (short)args[1]);

    if (ene != nullptr) {
        if (args[3] == 0) {
            ene->flags2 &= ~bits;
        } else {
            ene->flags2 |= bits;
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// ITEM_GET / ITEM_USE "1111", ITEM_LOST "211": item args[0], count args[1].
// FF3 0x001a1528 / 0x001a1550 / 0x001a1578

int EvItemGet(EV_EXE_CTRL *ctrl)
{
    ItemGet((short)ctrl->args[0], (short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a1550
int EvItemUse(EV_EXE_CTRL *ctrl)
{
    ItemUse((short)ctrl->args[0], (short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a1578
int EvItemLost(EV_EXE_CTRL *ctrl)
{
    ItemLost((short)ctrl->args[0], (short)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// ITEM_RESET "4": the item set of the finder bank's mission (start mode 2)
// or of item table ingame_wrk._24 at the difficulty.
// ITEM_TABLE_CHANGE_ "112": that table = args[0].
// FF3 0x001a15a0 / 0x001a1620

int EvItemReset(EV_EXE_CTRL *ctrl)
{
    int tbl = ingame_wrk._24;

    (void)ctrl;
    if (title_start_flg.mode == 2) {
        MissionItemReset(GetFinderModeWrk()->GetDef());
    } else {
        ItemTblReset(tbl, (u_int)IngameWrkGetDifficulty(1));
    }
    return 1;
}

// FF3 0x001a1620
int EvItemTableChange(EV_EXE_CTRL *ctrl)
{
    ingame_wrk._24 = ctrl->args[0];
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FILE_GET / FILE_LOST "112": file args[1]'s flag set / cleared.
// FF3 0x001a1638 / 0x001a1660

int EvFileGet(EV_EXE_CTRL *ctrl)
{
    DocFlagSet((short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a1660
int EvFileLost(EV_EXE_CTRL *ctrl)
{
    DocFlagClear((short)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FILE_READ "112": the message viewer with picture key args[1]'s message,
// the event paused until the viewer closed (layout anim state 4).
// FF3 0x001a1688

int EvFileRead(EV_EXE_CTRL *ctrl)
{
    short key = (short)ctrl->args[1];

    SetEventWaitFlg(1);
    if (ctrl->process == 0) {
        ev_file_read_anim = LayoutAnimStart(1, 0);
        MsgViewSetMsg(DocGetMsgType(key, 0), msg_pic_key_dat[key].msg1);
        MsgViewSetMode(MSG_VIEW_MODE_1);
        ctrl->process = 1;
    } else if (ctrl->process == 1 && LayoutAnimGetState(ev_file_read_anim) == 4) {
        SetEventWaitFlg(0);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// CRYSTAL_LOST / LEVELGEM_GET / LEVELGEM_LOST "112": nothing in FF3.
// FF3 0x001a1758 / 0x001a1760 / 0x001a1768

int EvCrystalLost(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a1760
int EvLevelGemGet(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a1768
int EvLevelGemLost(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CAM_SPECIALSHOT_GET "112": lens args[1] for character args[0].
// GET_CHAR_STAT "112": equipment 0 / 2 / 5 for args[1] 0 / 2 / 3 (else 0xff).
// CAM_EQUIP_FUNCTION_GET "112": lens function args[1].
// FF3 0x001a1770 / 0x001a17c0 / 0x001a1828

int EvCamSpecialShotGet(EV_EXE_CTRL *ctrl)
{
    GetLensWrk()->AddHave((u_char)ctrl->args[1], (u_char)ctrl->args[0]);
    return 1;
}

// FF3 0x001a17c0
int EvGetCharStat(EV_EXE_CTRL *ctrl)
{
    u_char no = 0xff;

    switch (ctrl->args[1]) {
    case 0:
        no = 0;
        break;
    case 2:
        no = 2;
        break;
    case 3:
        no = 5;
        break;
    }
    PlyrEquipObtain(no);
    return 1;
}

// FF3 0x001a1828
int EvCamEquipFunctionGet(EV_EXE_CTRL *ctrl)
{
    GetLensWrk()->AddFunc((u_char)ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MEMO_UPDATE "112": memo args[1] to page args[0], with the notice.
// MEMO_OPEN "112": memo args[1] found (page 0, with the notice).
// SET_MEMO_OPEN_FALG "22": the new-memo notice on (args[0] == 1) or off.
// MEMO_MENU_OPEN "22": the memo menu on page args[0] (-1: none).
// FF3 0x001a1868 / 0x001a1898 / 0x001a18c0 / 0x001a18e0

int EvMemoUpdate(EV_EXE_CTRL *ctrl)
{
    MemoSetPage((short)ctrl->args[1], (s_char)ctrl->args[0], 1);
    return 1;
}

// FF3 0x001a1898
int EvMemoOpen(EV_EXE_CTRL *ctrl)
{
    MemoOpen((short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a18c0
int EvSetMemoOpenFalg(EV_EXE_CTRL *ctrl)
{
    memo_flg.open_flg = ctrl->args[0] == 1;
    return 1;
}

// FF3 0x001a18e0
int EvMemoMenuOpen(EV_EXE_CTRL *ctrl)
{
    short page = (short)ctrl->args[0];

    MemoMenuReq(page, page != -1, 0, 1);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SHOOT_DEVELOP_FILM "112": film args[1] shot in the player's area (with
// ingame_wrk._28). GET_DEVELOP_FILM "4": the developed films.
// FF3 0x001a1918 / 0x001a1970

int EvShootDevelopFilm(EV_EXE_CTRL *ctrl)
{
    int arg = ingame_wrk._28;

    DevelopFilmShoot((short)ctrl->args[1], GetPlyrAreaNo(), (char)arg);
    return 1;
}

// FF3 0x001a1970
int EvGetDevelopFilm(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    DevelopFilmGetAll();
    DevelopFilmItemUpdate();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SOUND_LOAD / SOUND_PRELOAD_REGIST "112" (FF3 runs both the same way) and
// EV_SOUND_RELEASE "112": event SE file (args[0], args[1]).
// FF3 0x001a1998 / 0x001a19c8 / 0x001a19f8

int EvSoundLoad(EV_EXE_CTRL *ctrl)
{
    const int file = EventSoundBankFile(ctrl->args[1]);
    if (file >= 0)
        ev_seRegisterFile(ctrl->args[0], file, 1);
    return 1;
}

// FF3 0x001a19c8
int EvSoundPreloadRegist(EV_EXE_CTRL *ctrl)
{
    return EvSoundLoad(ctrl);
}

// FF3 0x001a19f8
int EvSoundRelease(EV_EXE_CTRL *ctrl)
{
    const int file = EventSoundBankFile(ctrl->args[1]);
    if (file >= 0)
        ev_seDeleteFile(ctrl->args[0], file);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// LOAD_REQUEST, RELEASE_REQUEST "11222", CHAPTER_LOAD_REQUEST "112": FF2's
// load requests; FF3 only reads the command's shorts.
// FF3 0x001a1a20 / 0x001a1a28 / 0x001a1a68

int EvLoadRequest(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a1a28
int EvReleaseRequest(EV_EXE_CTRL *ctrl)
{
    u_char *addr = ctrl->event_addr;

    (void)Get2Byte(addr + 2);
    (void)Get2Byte(addr + 4);
    return 1;
}

// FF3 0x001a1a68
int EvChapterLoadRequest(EV_EXE_CTRL *ctrl)
{
    (void)Get2Byte(ctrl->event_addr + 2);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_HAICHI_ID "112": area args[0]'s placement (haichi) id = args[1]; the
// loaded area reloads its room (phase 36), another area's buffered room is
// dropped.
// FF3 0x001a1a90

int EvSetHaichiId(EV_EXE_CTRL *ctrl)
{
    int     area = ctrl->args[0];
    int     id   = ctrl->args[1];
    MAP_AREA_DAT *row = MapAreaDataGet((s_char)area);
    int     room;

    if (row != nullptr) {
        row->side = (u_char)id;
        if (MapLoadGetRoomNoNow() == area) {
            EvRoomReloadReq(area, ingame_wrk.plyr_wrk->mh_no);
        } else {
            room = MapLoadGetBuffID(area);
            if (room >= 0) {
                MapLoadDeleteRoom(room);
            }
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_EVENT_REC_HIT "1124": args[1] 0 / 1 takes RegDat label args[2] out
// of / into the point-light table (MapLightRegDel / MapLightRegAdd) with
// light flags args[0] (1, 2; else 0).
// FF3 0x001a1b18

int EvSetEventRecHit(EV_EXE_CTRL *ctrl)
{
    int *args = ctrl->args;
    int  kind = 0;

    if (args[0] == 1) {
        kind = 1;
    } else if (args[0] == 2) {
        kind = 2;
    }
    if (args[1] == 0) {
        MapLightRegDel(args[2]);
    } else if (args[1] == 1) {
        MapLightRegAdd(args[2], kind);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// AREA_CHANGE "2222": move to area args[0] (map hit object args[1]); when
// it is loaded, the play data saves the area and the other buffer's area.
// FF3 0x001a1b90

int EvAreaChange(EV_EXE_CTRL *ctrl)
{
    int area = ctrl->args[0];
    int mh   = ctrl->args[1];

    if (ctrl->process == 0) {
        MapAreaMoveReq(area, mh);
        ctrl->process = 1;
    } else if (ctrl->process == 1 && MapAreaChange(area, mh) == 0) {
        PhotoObjAreaOn(area);
        PhotoObjAreaOff(MapLoadGetRoomArea(MapLoadGetBuffID(area) ^ 1));
        RandGhostAreaChange((short)area);
        MapLoadSetDrawFlg(2, 2);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// SET_2DMAP_DRAW "112": MarkEventFlagSet(args[1], args[0]).
// BACK_GROUND_LOAD / AREA_SWITCH "112": nothing in FF3.
// FF3 0x001a1c48 / 0x001a1c70 / 0x001a1c78

int EvSet2dmapDraw(EV_EXE_CTRL *ctrl)
{
    MarkEventFlagSet(ctrl->args[1], (u_char)ctrl->args[0]);
    return 1;
}

// FF3 0x001a1c70
int EvBackGroundLoad(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a1c78
int EvAreaSwitch(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PUZZLE_START "112": puzzle (args[0], args[1]), then phase 49.
// GAME_DATA_SAVE "112": the save point phase, unless an enemy is alive.
// FF3 0x001a1c80 / 0x001a1cb8

int EvPuzzleStart(EV_EXE_CTRL *ctrl)
{
    PuzzleSet(ctrl->args[0], ctrl->args[1]);
    GPhaseCtrlRequest(0x31, 0);
    return 1;
}

// FF3 0x001a1cb8
int EvGameDataSave(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    if (EneIsAnyAlive() == 0) {
        StorySaveGameReq();
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GAME_CLEAR_ "112": the clear: the results page's collection count and
// finder flags (GameClear.c), the title menu items 2 / 3 kept for the next
// game, the clear flag args[0] (0: _04, else _08), the clear count at the
// difficulty (FinderStateClear), then phase 42 (the game-clear sequence)
// in mode difficulty.
// FF3 0x001a1ce8

int EvGameClear(EV_EXE_CTRL *ctrl)
{
    FINDER_STATE_WRK *wrk;
    int               ending = ctrl->args[0] != 0;
    int               difficulty;
    int               find;

    difficulty = IngameWrkGetDifficulty(1);
    find = (difficulty >= 0 && difficulty <= 3) ? difficulty : -1;
    GameClearCountCollection();
    GameClearKeepFinderFlags();
    ClearRewardSetOpened(TitleMenuIsItemOpen(2), TitleMenuIsItemOpen(3));
    wrk = GetFinderStateWrk();
    if (ending) {
        wrk->_08 = 1;
    } else {
        wrk->_04 = 1;
    }
    wrk->Clear(difficulty);
    wrk->SetClearSaveFlag(1);
    GameClearSetMode(find, ending);
    GameClearRequest();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GAMEOVER_REQUEST "112", EV_MISSION_START "112" (phase 31),
// EV_MISSION_CLEAR / EV_MISSION_FAILED "112" (game-clear mode 4 / 5,
// phase 50; the clear is a failure when the time is up, FINDER_MODE_WRK::IsTimeUp).
// FF3 0x001a1e28 / 0x001a1e48 / 0x001a1e70 / 0x001a1f00

int EvGameOverRequest(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    GameOverReq();
    return 1;
}

// FF3 0x001a1e48
int EvMissionStart(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    GPhaseCtrlRequest(0x1f, 0);
    return 1;
}

// FF3 0x001a1e70
int EvMissionClear(EV_EXE_CTRL *ctrl)
{
    FINDER_MODE_WRK *wrk = GetFinderModeWrk();

    if (wrk->IsTimeUp() != 0) {
        return EvMissionFailed(ctrl);
    }
    GameClearSetMode(GAME_CLEAR_MODE_MISSION_CLEAR, 0);
    if (wrk != nullptr) {               // PC: stub guard
        wrk->result = 1;
    }
    GPhaseCtrlRequest(0x32, 0);
    return 1;
}

// FF3 0x001a1f00
int EvMissionFailed(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    GameClearSetMode(GAME_CLEAR_MODE_MISSION_FAILED, 0);
    GPhaseCtrlRequest(0x32, 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// CHANGE_CHAPTER "1111": ingame_wrk.b01 = args[1].
// CHAPTER_SEL_INIT "11222": never done in FF3 (returns 0).
// FF3 0x001a1f30 / 0x001a1f50

int EvChangeChapter(EV_EXE_CTRL *ctrl)
{
    ingame_wrk.b01 = (u_char)ctrl->args[1];
    return 1;
}

// FF3 0x001a1f50
int EvChapterSelInit(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_POS_SET "222": the player to (args[0], args[1]) on the room's floor
// plane (x / z from the room origin, the height of the map at that point;
// keeps y when there is none), flag 0x1000000 off, anime 0.
// FF3 0x001a2028

int EvPlyrPosSet(EV_EXE_CTRL *ctrl)
{
    PLYR_WRK     *plyr;
    float        *org;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR ground;

    org  = MapLoadGetOffset(MapLoadGetBuffID(GetPlyrAreaNo()));
    plyr = ingame_wrk.plyr_wrk;
    pos[0] = (float)ctrl->args[0] + (org != nullptr ? org[0] : 0.0f);  // PC: stub guard
    pos[1] = plyr->pos[1];
    pos[2] = (float)ctrl->args[1] + (org != nullptr ? org[2] : 0.0f);
    pos[3] = 0.0f;
    if (MapAreaGetHeight(ground, pos, GetPlyrAreaNo(), 0) < 0) {
        memcpy(ground, pos, sizeof(ground));
    }
    PlyrSetPos(ground);
    plyr = ingame_wrk.plyr_wrk;
    if ((plyr->flags & PLYR_FLG_1000000) != 0) {
        plyr->flags &= ~(u_long)PLYR_FLG_1000000;
    }
    SetPlyrAnime(0, 0, 0xff, 0xff, 0, 0.0f);
    PlyrMdlMotReset();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_HEIGHT_SET "22": the player's y = args[0] above the room origin.
// PLYR_ROT_SET "22": the player's facing = args[0] degrees.
// FF3 0x001a2130 / 0x001a21b8

int EvPlyrHeightSet(EV_EXE_CTRL *ctrl)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    float        *org;
    sceVu0FVECTOR pos;

    org = MapLoadGetOffset(MapLoadGetBuffID(GetPlyrAreaNo()));
    pos[0] = plyr->pos[0];
    pos[1] = (float)ctrl->args[0] + (org != nullptr ? org[1] : 0.0f);  // PC: stub guard
    pos[2] = plyr->pos[2];
    pos[3] = 0.0f;
    PlyrSetPos(pos);
    PlyrMdlMotReset();
    return 1;
}

// FF3 0x001a21b8
int EvPlyrRotSet(EV_EXE_CTRL *ctrl)
{
    PLYR_WRK     *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR rot;

    rot[0] = plyr->rot[0];
    rot[1] = EvGetRot360((short)ctrl->args[0]);
    rot[2] = plyr->rot[2];
    rot[3] = 1.0f;
    PlyrSetRot(rot);
    PlyrMdlMotReset();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_POS_MOVE "1122222": never done in FF3 (returns 0).
// PLYR_DISP "1111": args[0] 0 hides the player (lock 6), 1 shows it.
// FF3 0x001a2230 / 0x001a2238

int EvPlyrPosMove(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 0;
}

// FF3 0x001a2238
int EvPlyrDisp(EV_EXE_CTRL *ctrl)
{
    if (ctrl->args[0] == 0) {
        LockTblSet(6);
    } else if (ctrl->args[0] == 1) {
        LockTblReset(6);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_MOTION_CALL "1111": anime args[0] (blend args[1]) in the event mode
// 9, keeping the mode it left; args[2] != 0 waits for the anime and goes
// back. PLYR_MOTION_MODE_END "4": back to the kept mode.
// FF3 0x001a2288 / 0x001a2350

int EvPlyrMotionCall(EV_EXE_CTRL *ctrl)
{
    int   *args  = ctrl->args;
    u_char anime = (u_char)args[0];

    if (ctrl->process == 0) {
        SetPlyrAnime(anime, (u_char)args[1], 0xff, 0xff, 0, 0.0f);
        if (ingame_wrk.plyr_wrk->mode != 9) {
            ev_plyr_mode_keep = ingame_wrk.plyr_wrk->mode;
        }
        PlyrSetMode(9, nullptr);
        if ((u_char)args[2] == 0) {
            return 1;
        }
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (CheckPlyrAnimeEnd(anime) == 0) {
            return 0;
        }
        PlyrSetMode(ev_plyr_mode_keep, nullptr);
        return 1;
    }
    return 0;
}

// FF3 0x001a2350
int EvPlyrMotionModeEnd(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    PlyrSetMode(ev_plyr_mode_keep, nullptr);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_FACIAL_CALL / PLYR_MOTION_CHANGE "112": nothing in FF3.
// FF3 0x001a2378 / 0x001a2380

int EvPlyrFacialCall(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a2380
int EvPlyrMotionChange(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PLYR_MODEL_CHANGE "112": player args[1] becomes current with model kind
// 2, under lock 6 (steps 0 -> 4 -> 1 -> 2 as GO_DREAM_WORLD).
// FF3 0x001a2388

int EvPlyrModelChange(EV_EXE_CTRL *ctrl)
{
    switch (ctrl->process) {
    case 0:
        LockTblInc(6);
        ctrl->process = 4;
        break;
    case 4:
        ctrl->process = 1;
        break;
    case 1:
        if (EvPlyrMdlChangeStart(ctrl->args[1], 2) == 0) {
            LockTblDec(6);
            return 1;
        }
        ctrl->process = 2;
        break;
    case 2:
        if (EvPlyrMdlChangeWait() == 0) {
            return 0;
        }
        LockTblDec(6);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// PLAYER_ACT_FOURS "112": the crawl by (args[0] << 1 | args[1]): 0 cancel,
// 1 end request, 2 set, 3 request.
// FF3 0x001a2438

int EvPlayerActFours(EV_EXE_CTRL *ctrl)
{
    switch (ctrl->args[0] << 1 | ctrl->args[1]) {
    case 0:
        PlyrCrawlCancel();
        break;
    case 1:
        PlyrCrawlEndReq();
        break;
    case 2:
        PlyrCrawlSet();
        break;
    case 3:
        PlyrCrawlReq();
        break;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GO_UP_LADDER / GO_DOWN_LADDER "22": ladder args[0] from the bottom / top.
// FF3 0x001a24d0 / 0x001a24f8

int EvGoUpLadder(EV_EXE_CTRL *ctrl)
{
    PlyrLadderStartBottom((u_char)ctrl->args[0]);
    return 1;
}

// FF3 0x001a24f8
int EvGoDownLadder(EV_EXE_CTRL *ctrl)
{
    PlyrLadderStartTop((u_char)ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC commands. An NPC number below 2 is one of the two player cameras'
// characters (n_plyr_camera.c), 2 and up a cat (cat_ctrl.c, number - 2).
// NPC_SERIF_TBL / NPC_FACIAL_TBL "112": the speech / face table args[0].
// FF3 0x001a2520 / 0x001a2548

int EvNpcSerifTbl(EV_EXE_CTRL *ctrl)
{
    PlyrCameraSetSerifTbl((u_char)ctrl->args[0]);
    return 1;
}

// FF3 0x001a2548
int EvNpcFacialTbl(EV_EXE_CTRL *ctrl)
{
    PlyrCameraSetFacialTbl((u_char)ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_FIX_EVENT "1111": NPC args[0] fixed (args[1] != 0) with args[2].
// FF3 0x001a2570

int EvNpcFixEvent(EV_EXE_CTRL *ctrl)
{
    int   *args = ctrl->args;
    s_char npc  = (s_char)args[0];
    int    fix  = (u_char)args[1] != 0;

    if (npc < 2) {
        PlyrCameraFixEvent(npc, fix, (s_char)args[2]);
    } else {
        CatFixEvent((s_char)(npc - 2), fix, (s_char)args[2]);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_SET_POS "11112222": NPC args[1] to (args[3], args[4], args[5]) facing
// args[6] degrees, with args[2].
// NPC_SET_POS_LABEL "1111": NPC args[1] to position label args[2].
// FF3 0x001a25f0 / 0x001a26e0

int EvNpcSetPos(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    s_char        npc  = (s_char)args[1];
    u_char        arg  = (u_char)args[2];
    sceVu0FVECTOR pos;
    float         rot[4];

    pos[0] = (float)args[3];
    pos[1] = (float)args[4];
    pos[2] = (float)args[5];
    pos[3] = 1.0f;
    rot[0] = ((float)args[6] * 3.1415927f) / 180.0f;
    RotLimitChk(rot);
    if (npc < 2) {
        PlyrCameraSetPos(npc, arg, pos, rot[0]);
    } else {
        CatSetPos((s_char)(npc - 2), arg, pos, rot[0]);
    }
    return 1;
}

// FF3 0x001a26e0
int EvNpcSetPosLabel(EV_EXE_CTRL *ctrl)
{
    s_char npc   = (s_char)ctrl->args[1];
    u_char label = (u_char)ctrl->args[2];

    if (npc < 2) {
        PlyrCameraSetPosLabel(npc, label);
    } else {
        CatSetPosLabel((s_char)(npc - 2), label);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_MOTION_CALL "11222": NPC args[0] plays motion args[1] (a cat with
// args[2]); args[3] != 0 waits for its end.
// FF3 0x001a2740

int EvNpcMotionCall(EV_EXE_CTRL *ctrl)
{
    int   *args = ctrl->args;
    s_char npc  = (s_char)args[0];
    s_char cat  = (s_char)(npc - 2);
    int    end;

    if (ctrl->process == 0) {
        if (npc < 2) {
            PlyrCameraMotCall(npc, (s_char)args[1], args[2]);
        } else {
            CatMotionCall(cat, (s_char)args[1], (s_char)args[2]);
        }
        if ((short)args[3] == 0) {
            return 1;
        }
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        end = (npc < 2) ? PlyrCameraMotIsEnd(npc) : CatMotionIsEnd(cat);
        return end != 0;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_FACIAL_CALL "112": face args[1] (player-camera NPCs only).
// NPC_ACTIVE_FLG "1111": NPC args[1] active = args[2].
// FF3 0x001a2818 / 0x001a2858

int EvNpcFacialCall(EV_EXE_CTRL *ctrl)
{
    s_char npc = (s_char)ctrl->args[0];

    if (npc < 2) {
        PlyrCameraFacialCall(npc, (s_char)ctrl->args[1]);
    }
    return 1;
}

// FF3 0x001a2858
int EvNpcActiveFlg(EV_EXE_CTRL *ctrl)
{
    s_char npc = (s_char)ctrl->args[1];
    u_char on  = (u_char)ctrl->args[2];

    if (npc < 2) {
        PlyrCameraSetActive(npc, on);
    } else {
        CatSetActive((s_char)(npc - 2), on);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_MODEL_CHANGE "1111": NPC args[1] gets model args[2], a frame later.
// FF3 0x001a28b8

int EvNpcModelChange(EV_EXE_CTRL *ctrl)
{
    s_char npc = (s_char)ctrl->args[1];
    u_char mdl = (u_char)ctrl->args[2];

    if (ctrl->process == 0) {
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (npc > 1) {
            CatMdlChange((s_char)(npc - 2), mdl, 0);
        } else {
            PlyrCameraMdlChange(npc, mdl, 0);
        }
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_EYE_TARGET "11112222": NPC args[1] looks at target args[2], or at the
// point (args[3], args[4], args[5]) when any of it is non-zero.
// FF3 0x001a2938

int EvNpcEyeTarget(EV_EXE_CTRL *ctrl)
{
    int          *args = ctrl->args;
    s_char        npc  = (s_char)args[1];
    s_char        cat  = (s_char)(npc - 2);
    float        *at   = nullptr;
    sceVu0FVECTOR pos;

    if (args[3] != 0 || args[4] != 0 || args[5] != 0) {
        pos[0] = (float)args[3];
        pos[1] = (float)args[4];
        pos[2] = (float)args[5];
        pos[3] = 1.0f;
        at = pos;
    }
    if (npc < 2) {
        PlyrCameraSetEyeTarget(npc, (u_char)args[2]);
        PlyrCameraSetEyePos(npc, at, 0);
    } else {
        CatSetEyeOn(cat, (u_char)args[2]);
        CatSetEyePos(cat, at);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// NPC_LOCK_POS "1111": NPC args[0] with (args[1], args[2]).
// FF3 0x001a2a30

int EvNpcLockPos(EV_EXE_CTRL *ctrl)
{
    int   *args = ctrl->args;
    s_char npc  = (s_char)args[0];

    if (npc < 2) {
        PlyrCameraSetLockPos(npc, (s_char)args[1], (s_char)args[2]);
    } else {
        CatSetLabelLock((s_char)(npc - 2), (s_char)args[1], (s_char)args[2]);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Map object rows (FURN_SAVE_ROW, kind 1: furniture) by (area args[0], id
// args[1]); SET_OBJ_HITCHECK searches every area. "1124" unless noted.
// SET_OBJ_HITCHECK: hit check args[0] (0 / 1).
// FF3 0x001a2a98

int EvSetObjHitCheck(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    u_int        on   = (u_int)args[0];
    FURN_SAVE_ROW *row  = FurnSaveFindId(-1, 1, args[2]);

    if (on < 2 && row != nullptr) {     // PC: stub guard
        row->hit_check = (u_char)on;
    }
    return 1;
}

// SET_OBJ_VISIBLE: the parts of visibility pattern args[2] shown, the
// others hidden (as SET_EFF_VISIBLE), then the furniture redrawn.
// FF3 0x001a2ae0

int EvSetObjVisible(EV_EXE_CTRL *ctrl)
{
    static const u_int vis_tbl[6] = { 0, 7, 2, 5, 4, 3 };
    int         *args = ctrl->args;
    FURN_SAVE_ROW *row  = FurnSaveFindId(args[0], 1, args[1]);
    u_int        vis  = vis_tbl[args[2]];

    if (vis != 0) {
        FurnSaveSetVisible(row, vis, 1);
    }
    if ((vis ^ 7) != 0) {
        FurnSaveSetVisible(row, vis ^ 7, 0);
    }
    MapObjUpdate(-1);
    return 1;
}

// SET_OBJ_ALPHA: alpha args[2]. SET_OBJ_REQ_ACTION: action args[2].
// FF3 0x001a2ba0 / 0x001a2be0

int EvSetObjAlpha(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    FURN_SAVE_ROW *row  = FurnSaveFindId(args[0], 1, args[1]);

    if (row != nullptr) {               // PC: stub guard
        row->alpha = (u_char)args[2];
    }
    return 1;
}

// FF3 0x001a2be0
int EvSetObjReqAction(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    FURN_SAVE_ROW *row  = FurnSaveFindId(args[0], 1, args[1]);

    if (row != nullptr) {               // PC: stub guard
        row->req_action = (short)args[2];
    }
    return 1;
}

// SET_OBJ_ACTIONTYPE: action type args[2]; in the player's area a
// furniture work that shows the row takes it at once.
// FF3 0x001a2c20

int EvSetObjActionType(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    int          area = args[0];
    FURN_SAVE_ROW *row  = FurnSaveFindId(area, 1, args[1]);

    if (row == nullptr) {               // PC: stub guard
        return 1;
    }
    row->action_type = (u_char)args[2];
    if (area == GetPlyrAreaNo() && MapObjFindWrkByRow(row) != nullptr) {
        MapObjSetAction(row);
    }
    return 1;
}

// SET_FURN_WIND "112211": row (args[0], args[1], label args[2]) sways by
// (args[3], args[4]).
// FF3 0x001a2c98

int EvSetFurnWind(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    FURN_SAVE_ROW *row  = FurnSaveFind(args[0], 1, (short)args[1], args[2]);

    if (row != nullptr) {               // PC: stub guard
        row->wind[1] = (s_char)args[4];
        row->wind[0] = (s_char)args[3];
    }
    MapObjUpdate(-1);
    return 1;
}

// SET_PLYR_LOOK_FURN: the player looks at the row (args[2] != 0).
// FF3 0x001a2cf0

int EvSetPlyrLookFurn(EV_EXE_CTRL *ctrl)
{
    int         *args = ctrl->args;
    FURN_SAVE_ROW *row  = FurnSaveFindId(args[0], 1, args[1]);

    if (row != nullptr) {               // PC: stub guard
        row->look = args[2] != 0;
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// PEEPING_REQUEST "112": the peeping view (phase 45) on record args[1]; done when it ran.
// FF3 0x001a2d40

int EvPeepingRequest(EV_EXE_CTRL *ctrl)
{
    return PeepingRequest(ctrl->args[1]) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// SET_PHOTOABLE_ "112": photo object args[1] photoable (args[0] 1) or not
// (0); the player's area then sets its glow slots again (photo_obj.c).
// FF3 0x001a2d68

int EvSetPhotoable(EV_EXE_CTRL *ctrl)
{
    int on = ctrl->args[0];
    int no = ctrl->args[1];

    if (PhotoObjGetDat(no) != nullptr) {
        if (on == 0) {
            PhotoObjClrPhotoable(no);
        } else if (on == 1) {
            PhotoObjSetPhotoable(no);
        }
        PhotoObjAreaOn(GetPlyrAreaNo());
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_OBJ_EFFECT / SET_OBJ_PHOTOABLE "1124": nothing in FF3.
// FF3 0x001a2de8 / 0x001a2df0

int EvSetObjEffect(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// FF3 0x001a2df0
int EvSetObjPhotoAble(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 36 (Story_RoomReload, under Story_Main): SET_HAICHI_ID on the loaded
// area reloads its room. The request keeps (area, map hit object) and the
// phase to return to.
// FF3 0x001a2df8

static void EvRoomReloadReq(int area, int mh)
{
    ev_reload_area = area;
    ev_reload_mh = mh;
    GetGPhaseCtrl()->SetNext(0x24, 0);
}

// The room's buffer dropped, the area cleared, the phase that asked kept,
// the room loaded again.
// FF3 0x001a2e40

int init_Story_RoomReload(void)
{
    int room = MapLoadGetBuffID(ev_reload_area);

    if (room >= 0) {
        MapLoadDeleteRoom(room);
        MapAreaNoSet(-1);
    }
    ev_reload_ret_phase = GetGPhaseCtrl()->from;
    MapLoadMoveRoom(ev_reload_area);
    return 1;
}

// FF3 0x001a2eb8

int end_Story_RoomReload(void)
{
    MapAreaNoSet(ev_reload_area);
    return 1;
}

// Waits for the area as AREA_CHANGE does, the same saves, then back.
// FF3 0x001a2ee0

int one_Story_RoomReload(void)
{
    if (MapAreaChange(ev_reload_area, ev_reload_mh) != 0) {
        return 0;
    }
    PhotoObjAreaOn(ev_reload_area);
    PhotoObjAreaOff(MapLoadGetRoomArea(MapLoadGetBuffID(ev_reload_area) ^ 1));
    RandGhostAreaChange((short)ev_reload_area);
    MapLoadSetDrawFlg(2, 2);
    GetGPhaseCtrl()->SetNext((short)ev_reload_ret_phase, 0);
    return 1;
}

// FF3 0x001a2f88 (EvCommandGPhaseCtrlAtExit) is compiler output: the atexit
// thunk of the GPhaseCtrl singleton (GPhaseCtrlTerm(0x41e468)), whose C++
// static gphase.c owns here.

// ──────────────────────────────────────────────────────────────────────
// Every event sound row free.
// FF3 0x001a2fb0; FF2 EvSoundCtrlInit (FF3 clears pos to 0, not -1)

void EvSoundCtrlInit(void)
{
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        ev_sound_ctrl[i].sound_id = -1;
        ev_sound_ctrl[i].se = -1;
        ev_sound_ctrl[i].pos = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// SOUND_STOP's worker: fade out (time 0) every looping instance of the SE
// and free its row. FF2 stopped one sound id and called Del_SoundID.
// FF3 0x001a2fe8

static void EvSoundStopSub(int se)
{
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].se == se) {
            if (ev_sound_ctrl[i].sound_id != -1) {
                SndSeFadeStop(ev_sound_ctrl[i].sound_id, 0);
            }
            ev_sound_ctrl[i].se = -1;
            ev_sound_ctrl[i].sound_id = -1;
            ev_sound_ctrl[i].pos = 0;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Remember a looping SE in the first free row (silently dropped when full;
// FF2 printed and asserted).
// FF3 0x001a3070; FF2 Regist_SoundID 0x00188f10 (argument order changed)

static void Regist_SoundID(int sound_id, int se, int pos)
{
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id == -1) {
            ev_sound_ctrl[i].pos = pos;
            ev_sound_ctrl[i].sound_id = sound_id;
            ev_sound_ctrl[i].se = se;
            return;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The menu opens: pause every event SE. From MioPan ev_macro.c EvSoundPause.
// FF3 0x001a30c8; FF2 EvSoundPause 0x00187c60

void EvSoundPause(void)
{
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id != -1) {
            SndBufPause(ev_sound_ctrl[i].sound_id);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3120; FF2 EvSoundRestart 0x00187ce0

void EvSoundRestart(void)
{
    int i;

    for (i = 0; i < EV_SOUND_CTRL_MAX; i++) {
        if (ev_sound_ctrl[i].sound_id != -1) {
            SndBufRestart(ev_sound_ctrl[i].sound_id);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Play an event SE (bank = high 16 bits of the id). A resident bank plays
// through SndSePlay and loops are registered for SOUND_STOP; otherwise the
// bank is streamed (SndSeAutoPlay). FF2 picked the system / player / sister
// banks by file; FF3's SE ids carry their bank.
// FF3 0x001a3178; FF2 EvBankSoundSub 0x00185d48

static void EvBankSoundSub(u_int se, SND_3D_SET *set)
{
    int bank;
    int sound_id;

    bank = (int)(se >> 16);
    if (SndSeBankIsEntry(bank) != 0 && SndSeBankIsLoaded(bank) != 0) {
        sound_id = SndSePlay(se, 100, 0x1000, 0, set, 0);
        if (SndSeIsLoop(se) != 0) {
            Regist_SoundID(sound_id, (int)se, 0);
        }
        return;
    }

    SndSeAutoPlay(se, 100, 0x1000, 0, set, 0);
}

// ──────────────────────────────────────────────────────────────────────
// SOUND_PLAY "224": args[1] the SE id.
// FF3 0x001a3250

int EvSoundPlay(EV_EXE_CTRL *ctrl)
{
    EvBankSoundSub((u_int)ctrl->args[1], nullptr);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SOUND_STOP / SOUND3D_OBJ_STOP / SOUND3D_POS_STOP "1124": args[2] the SE id.
// FF3 0x001a3278

int EvSoundStop(EV_EXE_CTRL *ctrl)
{
    EvSoundStopSub(ctrl->args[2]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SOUND3D_OBJ_PLAY "11244": args[0] object type, args[3] object id, args[2]
// the SE, played at the object (GetObjectPos's answer is not checked).
// FF3 0x001a32a0

int EvSound3dObjPlay(EV_EXE_CTRL *ctrl)
{
    sceVu0FVECTOR pos = { 0.0f, 0.0f, 0.0f, 0.0f };
    SND_3D_SET    set;

    GetObjectPos(pos, (u_char)ctrl->args[0], ctrl->args[3]);
    memset(&set, 0, sizeof(set));
    set.pos = pos;
    EvBankSoundSub((u_int)ctrl->args[2], &set);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SOUND3D_POS_PLAY "112224": args[1..3] the position, args[4] the SE.
// FF3 0x001a3310

int EvSound3dPosPlay(EV_EXE_CTRL *ctrl)
{
    sceVu0FVECTOR pos;
    SND_3D_SET    set;
    int          *args;

    args = ctrl->args;
    pos[0] = (float)args[1];
    pos[1] = (float)args[2];
    pos[2] = (float)args[3];
    pos[3] = 0.0f;
    memset(&set, 0, sizeof(set));
    set.pos = pos;
    EvBankSoundSub((u_int)args[4], &set);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// BG_SE_STOP: stop the map's ambient SEs.
// FF3 0x001a3390

int EvBgSeStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    GetLayoutSeWrk()->StopAll();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Save block 12: the two saved-stream rows.
// FF3 0x001a33b8

int EvSaveStreamSaveGet(void **addr)
{
    *addr = ev_save_stream;
    return 0x60;
}

// ──────────────────────────────────────────────────────────────────────
// The stream id an event registered for a file, -1 if none.
// FF3 0x001a33d0

static int Get_StreamID(int file_label)
{
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].file_label == file_label) {
            return ev_stream_ctrl[i].stream_id;
        }
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Register a stream once (ids already present and -1 are ignored; a full
// table drops it, FF2 asserted).
// FF3 0x001a3408; FF2 Regist_StreamID 0x00188fa0 (+ state)

static void Regist_StreamID(int stream_id, int file_label, int state)
{
    int i;

    if (stream_id == -1) {
        return;
    }

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id == stream_id) {
            return;
        }
        if (ev_stream_ctrl[i].stream_id == -1) {
            ev_stream_ctrl[i].state = state;
            ev_stream_ctrl[i].stream_id = stream_id;
            ev_stream_ctrl[i].file_label = file_label;
            return;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3460; FF2 Del_StreamID 0x00189070

static void Del_StreamID(int stream_id)
{
    int i;

    if (stream_id == -1) {
        return;
    }

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id == stream_id) {
            ev_stream_ctrl[i].file_label = -1;
            ev_stream_ctrl[i].state = 0;
            ev_stream_ctrl[i].stream_id = -1;
            return;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a34b0; FF2 IsRegist_StreamID 0x001890e0

static int IsRegist_StreamID(int file_label)
{
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].file_label == file_label) {
            return 1;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// EV_STREAM_ST_* of a file's row, 0 when not registered.
// FF3 0x001a34f0

static int Get_StreamState(int file_label)
{
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].file_label == file_label) {
            return ev_stream_ctrl[i].state;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Stop (time 0) and forget every event stream. No caller found in FF3.
// FF3 0x001a3528

static void EvStreamCtrlFadeOutAll(void)
{
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        if (ev_stream_ctrl[i].stream_id != -1) {
            StreamAutoFadeOut(ev_stream_ctrl[i].stream_id, 0);
            ev_stream_ctrl[i].stream_id = -1;
            ev_stream_ctrl[i].file_label = -1;
            ev_stream_ctrl[i].state = 0;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3598; FF2 EvStreamCtrlInit 0x00180dc8

void EvStreamCtrlInit(void)
{
    int i;

    for (i = 0; i < EV_STREAM_CTRL_MAX; i++) {
        ev_stream_ctrl[i].stream_id = -1;
        ev_stream_ctrl[i].file_label = -1;
        ev_stream_ctrl[i].state = 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Whether a file an event preloaded is ready (an event condition waits on
// it).
// FF3 0x001a35d0

int EvStreamIsPreload(int file_label)
{
    int id;

    id = Get_StreamID(file_label);
    if (id == -1) {
        return 0;
    }
    return StreamAutoIsPreload(id) != 0;
}

// ──────────────────────────────────────────────────────────────────────
// After a load: start every saved stream again -- in 2D, at its object (2D
// when the object is gone) or at its position -- and register it as
// playing. FF2 had one loop per table (EventMacroLoad_StreamInit,
// _ObjStreamInit, _PosStreamInit); end_Story_NowLoading calls this.
// FF3 0x001a3610

void EventMacroLoad_StreamInit(void)
{
    int i;

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        EV_SAVE_STREAM *row = &ev_save_stream[i];
        sceVu0FVECTOR   obj_pos = { 0.0f, 0.0f, 0.0f, 0.0f };
        float          *pos;
        int             id;

        if (row->set_flg != 1) {
            continue;
        }

        pos = nullptr;
        if (row->type == EV_SAVE_STREAM_OBJ) {
            if (GetObjectPos(obj_pos, (u_char)row->obj_type, row->obj_id) != 0) {
                pos = obj_pos;
            }
        } else if (row->type == EV_SAVE_STREAM_POS) {
            pos = row->pos;
        }

        id = EvStreamPlaySub(row->file_label, 0xe, 0, 1, row->volume, 5, pos, 0x1000);
        row->stream_id = id;
        Regist_StreamID(id, row->file_label, EV_STREAM_ST_PLAY);
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3700; FF2 EvSaveStreamInit (+ the Obj / Pos tables)

void EvSaveStreamInit(void)
{
    memset(ev_save_stream, 0, sizeof(ev_save_stream));
}

// ──────────────────────────────────────────────────────────────────────
// The saved row of a stream: the one already holding it, else the first free
// one (filled as a 2D stream). nullptr when both are taken.
// FF3 0x001a3728; FF2 SetEvSaveStream 0x00189200

static EV_SAVE_STREAM *SetEvSaveStream(int stream_id, int file_label, int volume)
{
    int i;

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].set_flg != 0 && ev_save_stream[i].stream_id == stream_id) {
            return &ev_save_stream[i];
        }
    }

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].set_flg == 0) {
            ev_save_stream[i].stream_id = stream_id;
            ev_save_stream[i].set_flg = 1;
            ev_save_stream[i].file_label = file_label;
            ev_save_stream[i].volume = volume;
            ev_save_stream[i].type = EV_SAVE_STREAM_2D;
            return &ev_save_stream[i];
        }
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a37b0; FF2 SetEvSaveObjStream (merged table)

static void SetEvSaveObjStream(int stream_id, int obj_type, int obj_id, int file_label, int volume)
{
    EV_SAVE_STREAM *row;

    row = SetEvSaveStream(stream_id, file_label, volume);
    if (row != nullptr) {
        row->obj_id = obj_id;
        row->type = EV_SAVE_STREAM_OBJ;
        row->obj_type = obj_type;
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3800; FF2 SetEvSavePosStream (merged table)

static void SetEvSavePosStream(int stream_id, float *pos, int file_label, int volume)
{
    EV_SAVE_STREAM *row;

    row = SetEvSaveStream(stream_id, file_label, volume);
    if (row != nullptr) {
        row->type = EV_SAVE_STREAM_POS;
        memcpy(row->pos, pos, sizeof(row->pos));
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001a3848; FF2 DelEvSaveStream (covers the Obj / Pos variants)

static void DelEvSaveStream(int stream_id)
{
    int i;

    for (i = 0; i < EV_SAVE_STREAM_MAX; i++) {
        if (ev_save_stream[i].stream_id == stream_id) {
            ev_save_stream[i].set_flg = 0;
            ev_save_stream[i].stream_id = -1;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Stop an event stream by file and forget it everywhere.
// FF3 0x001a3888; FF2 EvStreamStopSub 0x00186040

static void EvStreamStopSub(int file_label, int fade_out_time)
{
    int id;

    id = Get_StreamID(file_label);
    StreamAutoFadeOut(id, fade_out_time);
    Del_StreamID(id);
    DelEvSaveStream(id);
}

// ──────────────────────────────────────────────────────────────────────
// STREAM_PRELOAD "112": args[0] loop, args[1] file. Preloads the file
// (volume 0x3200, pitch 0x1000) unless an event already holds it, and
// registers the result as preloaded (a -1 id is not registered).
// FF3 0x001a38d0

int EvStreamPreload(EV_EXE_CTRL *ctrl)
{
    int file_label;
    int loop;
    int id;

    file_label = ctrl->args[1];
    loop = ctrl->args[0];

    const int file = EventStreamFile(file_label);
    if (file < 0)
        return 1;

    id = -1;
    if (IsRegist_StreamID(file_label) == 0) {
        id = StreamAutoPreloadPitch(file, file + 1, 0xe, 0, loop, 0x3200, 0, nullptr, 0x1000);
    }
    Regist_StreamID(id, file_label, EV_STREAM_ST_PRELOAD);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STREAM_PLAY "1122222": args[0] loop, args[1] file, args[2] fade-in time,
// args[4] volume. A looping stream is registered and saved.
// FF3 0x001a3958

int EvStreamPlay(EV_EXE_CTRL *ctrl)
{
    int *args;
    int  file_label;
    int  vol;
    int  loop;
    int  id;

    args = ctrl->args;
    file_label = args[1];
    vol = args[4];
    loop = args[0];

    id = EvStreamPlaySub(file_label, 0xe, 0, loop, vol, args[2], nullptr, 0x1000);
    if (loop == 1) {
        Regist_StreamID(id, file_label, EV_STREAM_ST_PLAY);
        SetEvSaveStream(id, file_label, vol);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STREAM_STOP / STREAM3D_OBJ_STOP / STREAM3D_POS_STOP "2222": args[0] file,
// args[1] fade-out time.
// FF3 0x001a39f8

int EvStreamStop(EV_EXE_CTRL *ctrl)
{
    EvStreamStopSub(ctrl->args[0], ctrl->args[1]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MAP_STREAM_PLAY "22": the map BGM back over args[0] frames.
// FF3 0x001a3a20

int EvMapStreamPlay(EV_EXE_CTRL *ctrl)
{
    map_bgmFadeIn(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// MAP_STREAM_STOP "22": the map BGM out over args[0] frames.
// FF3 0x001a3a48

int EvMapStreamStop(EV_EXE_CTRL *ctrl)
{
    map_bgmFadeOut(ctrl->args[0], 0);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_MAP_STREAM_CHAHGE "224" (sic): map BGM slot args[0] to file args[1].
// FF3 0x001a3a70

int EvMapStreamChange(EV_EXE_CTRL *ctrl)
{
    const int file = EventStreamFile(ctrl->args[1]);
    if (file >= 0 || ctrl->args[1] < 0)
        map_bgmChangeTbl(ctrl->args[0], file);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STREAM3D_OBJ_PLAY "112222114": args[0] object type, args[7] object id,
// args[1] file, args[2] fade-in time, args[4] volume, args[5] loop. Nothing
// plays when the object is not found.
// FF3 0x001a3a98

int EvStream3dObjPlay(EV_EXE_CTRL *ctrl)
{
    sceVu0FVECTOR pos;
    int          *args;
    int           obj_type;
    int           obj_id;
    int           loop;
    int           file_label;
    int           vol;
    int           id;

    args = ctrl->args;
    obj_type = args[0];
    obj_id = args[7];
    loop = args[5];
    file_label = args[1];
    vol = args[4];

    if (GetObjectPos(pos, (u_char)obj_type, obj_id) != 0) {
        id = EvStreamPlaySub(file_label, 0xe, 0, loop, vol, args[2], pos, 0x1000);
        if (loop == 1) {
            Regist_StreamID(id, file_label, EV_STREAM_ST_PLAY);
            SetEvSaveObjStream(id, obj_type, obj_id, file_label, vol);
        }
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STREAM3D_POS_PLAY "112222222": args[0] loop, args[1] file, args[2] fade-in
// time, args[3..5] the position, args[7] volume.
// FF3 0x001a3b70

int EvStream3dPosPlay(EV_EXE_CTRL *ctrl)
{
    sceVu0FVECTOR pos;
    int          *args;
    int           file_label;
    int           vol;
    int           loop;
    int           id;

    args = ctrl->args;
    file_label = args[1];
    pos[0] = (float)args[3];
    vol = args[7];
    loop = args[0];
    pos[1] = (float)args[4];
    pos[2] = (float)args[5];
    pos[3] = 0.0f;

    id = EvStreamPlaySub(file_label, 0xe, 0, loop, vol, args[2], pos, 0x1000);
    if (loop == 1) {
        Regist_StreamID(id, file_label, EV_STREAM_ST_PLAY);
        SetEvSavePosStream(id, pos, file_label, vol);
    }
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// STREAM_ALL_STOP: every stream stops, the event's table is emptied (the
// saved rows stay).
// FF3 0x001a3c38

int EvStreamAllStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    StreamAutoAllStop();
    EvStreamCtrlInit();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// SET_BGM_LIST "14": the map BGM list (word 0x3f3230).
// FF3 0x001a3c60

int EvSetBgmList(EV_EXE_CTRL *ctrl)
{
    map_bgmSetList(ctrl->args[0]);
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// TAPE_START: opens the cassette player with the event paused, then waits
// until it closes.
// FF3 0x001a3c88

int EvTapeStart(EV_EXE_CTRL *ctrl)
{
    if (ctrl->process == 0) {
        SetEventWaitFlg(1);
        init_Story_Cassette();
        ctrl->process = 1;
    } else if (ctrl->process == 1) {
        if (one_Story_Cassette() == 0) {
            return 0;
        }
        end_Story_Cassette();
        SetEventWaitFlg(0);
        return 1;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// The telephone: TEL_START (args[1], args[2]), TEL_STOP, TEL_VOL_OFF /
// TEL_VOL_ON (the ambient level of no area / the player's), TEL_TALK_START
// (args[1]) and TEL_TALK_STOP.
// FF3 0x001a3d08 / 0x001a3d30 / 0x001a3d50 / 0x001a3d70 /
// FF3 0x001a3d98 / 0x001a3dc0

// FF3 0x001a3d08
int EvTelStart(EV_EXE_CTRL *ctrl)
{
    TelStart(ctrl->args[1], ctrl->args[2]);
    return 1;
}

// FF3 0x001a3d30
int EvTelStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    TelStop();
    return 1;
}

// FF3 0x001a3d50
int EvTelVolOff(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    TelRingSetArea(-1);
    return 1;
}

// FF3 0x001a3d70
int EvTelVolOn(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    TelRingSetArea(GetPlyrAreaNo());
    return 1;
}

// FF3 0x001a3d98
int EvTelTalkStart(EV_EXE_CTRL *ctrl)
{
    TelTalkStart(ctrl->args[1]);
    return 1;
}

// FF3 0x001a3dc0
int EvTelTalkStop(EV_EXE_CTRL *ctrl)
{
    (void)ctrl;
    TelTalkClear();
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// EV_SET_WORK / EV_ADD_WORK / EV_SUB_WORK: the event's work short
// (EV_WORK_CHECK compares it) set to, raised or lowered by args[1].
// FF3 0x001a3de0 / 0x001a3df8 / 0x001a3e18

int EvSetWork(EV_EXE_CTRL *ctrl)
{
    ctrl->work = (short)ctrl->args[1];
    return 1;
}

// FF3 0x001a3df8
int EvAddWork(EV_EXE_CTRL *ctrl)
{
    ctrl->work = (short)(ctrl->work + (short)ctrl->args[1]);
    return 1;
}

// FF3 0x001a3e18
int EvSubWork(EV_EXE_CTRL *ctrl)
{
    ctrl->work = (short)(ctrl->work - (short)ctrl->args[1]);
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// Conditions (table 0x3242e8): (event id, the condition's bytes: +0 the
// opcode, then its arguments) -> 1 when true. The comparing ones take an
// operator byte: 8 ">=", 9 "<=", 10 "==" (anything else: false).
// ══════════════════════════════════════════════════════════════════════

// NPC_INTO_PLYR_SIGHT "1122": the player sees NPC +1 (bit 1 of the sight
// test) within distance +2 * 0.2 and angle +4.
// FF3 0x001a3e38

int EvNpcIntoPlyrSight(int event_id, u_char *addr)
{
    struct { u_char code; s_char npc; short dist; short sight; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return (PlyrCameraSightCheck(a.npc, (float)a.dist * 0.2f, (float)a.sight) & 2) != 0;
}

// NPC_DISTANCE "11114": NPC +2's distance to the player against +4 * 0.2 by
// the operator +3 (0 ">=", 1 "==", 2 "<=").
// FF3 0x001a3e90

int EvNpcDistance(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; s_char npc; s_char cmp; short dist; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return PlyrCameraDistCheck(a.npc, a.cmp, (float)a.dist * 0.2f) != 0;
}

// NPC_AREAOUT / NPC_AREAOUT_DEF: always true in FF3.
// FF3 0x001a3ee8

int EvNpcAreaOut(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    return 1;
}

// EV_CNT_EXEC "1124": how often event +4 ran against +2 by the operator +1.
// FF3 0x001a3ef0

int EvEvCntExec(int event_id, u_char *addr)
{
    struct { u_char code; s_char op; short num; int event; } a;
    short cnt;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    cnt = GetEvExeCount(a.event);
    switch (a.op) {
    case 8:  return cnt >= a.num;
    case 9:  return cnt <= a.num;
    case 10: return cnt == a.num;
    }
    return 0;
}

// EV_STATE / EV_NOT_STATE "112": event +2's state byte is (is not) +1.
// FF3 0x001a3f90 / 0x001a3fd0

int EvEvState(int event_id, u_char *addr)
{
    struct { u_char code; s_char state; short event; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return a.state == GetEvState(a.event);
}

// FF3 0x001a3fd0
int EvEvNotState(int event_id, u_char *addr)
{
    return EvEvState(event_id, addr) != 1;
}

// EV_FAILURE: always false.
// FF3 0x001a3ff8

int EvEvFailure(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    return 0;
}

// EV_WORK_CHECK "112": this event's work short (EV_SET_WORK) against +2 by
// the operator +1.
// FF3 0x001a4000

int EvEvWorkCheck(int event_id, u_char *addr)
{
    struct { u_char code; s_char op; short num; } a;
    EV_EXE_CTRL *ctrl;

    memcpy(&a, addr, sizeof(a));
    ctrl = EvExeGetCtrl(event_id);
    if (ctrl == nullptr) {              // PC: a stub returns nullptr (FF3 reads it unchecked)
        return 0;
    }
    switch (a.op) {
    case 8:  return ctrl->work >= a.num;
    case 9:  return ctrl->work <= a.num;
    case 10: return ctrl->work == a.num;
    }
    return 0;
}

// DOOR_STAT_CHECK "1124": with no door current (MapDoorIdGet < 0) true for
// state 5 (MAP_DOOR_MODE_NONE); else door +4 must be the current one and
// MapDoorModeGet equal +1.
// FF3 0x001a40a0

int EvDoorStatCheck(int event_id, u_char *addr)
{
    struct { u_char code; s_char state; short _2; int door; } a;
    MAP_DOOR_DAT *cur;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    if (MapDoorIdGet() < 0) {
        return a.state == 5;
    }
    cur = MapDoorDataGet();
    if (cur == nullptr || cur->id != a.door) {  // PC: guard on cur (FF3 read address 0)
        return 0;
    }
    return (s_char)MapDoorModeGet() == a.state;
}

// ──────────────────────────────────────────────────────────────────────
// Start an event stream. A file STREAM_PRELOAD readied starts from its
// preload, faded to vol / pitch over in_time and moved to pos (the row is
// dropped; the caller registers it again as playing); a file already
// playing is left alone; any other starts fresh with its header file + 1,
// 3D when pos is given. Returns the stream id or -1.
// FF3 0x002b8f70 (out-of-line, after the singleton getters)

int EvStreamPlaySub(int file_label, int priority, int effect, int loop, int vol,
                    int in_time, float *pos, int pitch)
{
    SND_3D_SET  set;
    SND_3D_SET *s3s;
    int         state;
    int         id;

    state = Get_StreamState(file_label);
    if (state == EV_STREAM_ST_PLAY) {
        return -1;
    }

    if (state == EV_STREAM_ST_PRELOAD) {
        id = Get_StreamID(file_label);
        if (StreamAutoPreloadPlay(id) == 0) {
            return -1;
        }
        StreamAutoFade(id, vol, in_time);
        StreamAutoFadePitch(id, pitch, in_time);
        if (pos != nullptr) {
            StreamAutoSetPosition(id, pos);
        }
        Del_StreamID(id);
        return id;
    }

    memset(&set, 0, sizeof(set));
    s3s = (pos != nullptr) ? &set : nullptr;
    set.pos = pos;
    const int file = EventStreamFile(file_label);
    if (file < 0)
        return -1;
    return StreamAutoPlayPitch(file, file + 1, priority, effect, loop, vol, in_time, s3s, pitch);
}

// ──────────────────────────────────────────────────────────────────────
// The wakeup / sleep movie singleton (bss 0x424b00, guard sbss gp+0x788;
// FF3 registers an empty destructor 0x1a12c8 with atexit). FinderBankWrkInit
// runs in FINDER_BANK_WRK's constructor; the guard and atexit come from the
// compiler.
// callcheck: FinderBankWrkInit, atexit (the constructor; the static's guard)
// FF3 0x002b8f28

FINDER_BANK_WRK *GetFinderBankWrk(void)
{
    static FINDER_BANK_WRK wrk;         /* bss 0x424b00 */
    return &wrk;
}

FINDER_BANK_WRK::FINDER_BANK_WRK()
{
    Init();
}

// ──────────────────────────────────────────────────────────────────────
// Condition SP_GAMEOVER_ (row 65): true once the player came back from the
// special game over (SET_SP_GAMEOVER_'s lock 0x18); then back to mode 0 and
// PLYR_FLG_REVIVED cleared. Was hidden (no Ghidra function until 2026-10-14).
// FF3 0x001a4108

int EvCondSpGameover(int event_id, u_char *addr)
{
    int revived = PlyrDeathIsRevived();

    (void)event_id;
    (void)addr;
    if (revived != 0) {
        PlyrSetMode(0, nullptr);
        ingame_wrk.plyr_wrk->flags &= ~(u_long)PLYR_FLG_REVIVED;
    }
    return revived != 0;
}

// ──────────────────────────────────────────────────────────────────────
// SAVE_IS_OK: no enemy alive.
// FF3 0x001a4160

int EvSaveIsOk(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    return EneIsAnyAlive() == 0;
}

// CHECK_GAME_DIFFICULTY "21": the difficulty is +2.
// FF3 0x001a4180

int EvCheckGameDifficulty(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; s_char difficulty; u_char _3; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return (s_char)IngameWrkGetDifficulty(1) == a.difficulty;
}

// CHECK_MISSION "22": the mission (group * 5 + number) is +2.
// FF3 0x001a41b8 (FF3 caches the work pointer in sdata 0x372a40)

int EvCheckMission(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; short no; } a;
    FINDER_MODE_WRK *wrk;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    wrk = GetFinderModeWrk();
    if (wrk == nullptr) {               // PC: a stub returns nullptr
        return 0;
    }
    return a.no == wrk->mission_grp * 5 + wrk->mission_no;
}

// ABOVE_CLEAR_NUM / FOLLOW_CLEAR_NUM "22": the clear count (FUN_001a5e60) is
// at least / at most +2.
// FF3 0x001a4218 / 0x001a4268 (FF3 caches GetFinderStateWrk() in sdata
// 0x372870)

int EvAboveClearNum(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_short num; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return (int)a.num <= GetFinderStateWrk()->FUN_001a5e60();
}

// FF3 0x001a4268
int EvFollowClearNum(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_short num; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return GetFinderStateWrk()->FUN_001a5e60() <= (int)a.num;
}

// EV_NOW_CHAPTER / EV_NOT_NOW_CHAPTER "1111": the chapter (ingame_wrk.b01)
// against +1 by the operator +2.
// FF3 0x001a42b8 / 0x001a4358

int EvNowChapter(int event_id, u_char *addr)
{
    struct { u_char code; s_char chapter; s_char op; } a;
    int cur = ingame_wrk.b01;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    switch (a.op) {
    case 8:  return cur >= a.chapter;
    case 9:  return cur <= a.chapter;
    case 10: return cur == a.chapter;
    }
    return 0;
}

// FF3 0x001a4358
int EvNotNowChapter(int event_id, u_char *addr)
{
    return EvNowChapter(event_id, addr) != 1;
}

// GHOST_EXIST / GHOST_LOST "112": enemy (+1 type, +2 id) is (is not) in
// state 3.
// FF3 0x001a4380 / 0x001a43b8

int EvGhostExist(int event_id, u_char *addr)
{
    struct { u_char code; u_char type; short id; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return EneGetState(a.type, a.id) == 3;
}

// FF3 0x001a43b8
int EvGhostLost(int event_id, u_char *addr)
{
    return EvGhostExist(event_id, addr) != 1;
}

// GHOST_RELEASE_TYPE "1122": enemy (+1 type, +2 id) has a flag of mask +4.
// FF3 0x001a43e0

int EvGhostReleaseType(int event_id, u_char *addr)
{
    struct { u_char code; u_char type; u_short id; u_short mask; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return EneIdFlgCheck(a.type, a.id, a.mask) != 0;
}

// BATTLE_MODE / BATTLE_END: an enemy is alive or the player fights (not).
// FF3 0x001a4418 / 0x001a4458

int EvBattleMode(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    if (EneIsAnyAlive() != 0) {
        return 1;
    }
    return PlyrIsBattle() != 0;
}

// FF3 0x001a4458
int EvBattleEnd(int event_id, u_char *addr)
{
    return EvBattleMode(event_id, addr) != 1;
}

// PLYR_ITEM_HAVE "1111": at least +1 of item +2. EV_NOT_HAVE_ITEM "21":
// none of item +2. Items 0x45 and up count as none.
// FF3 0x001a4480 / 0x001a44d0

int EvPlyrItemHave(int event_id, u_char *addr)
{
    struct { u_char code; u_char num; u_char item; } a;
    short have = 0;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    if (a.item < 0x45) {
        have = *ItemGetCnt(a.item);
    }
    return (short)a.num <= have;
}

// FF3 0x001a44d0
int EvNotHaveItem(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_char item; u_char _3; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    if (a.item < 0x45) {
        return *ItemGetCnt(a.item) < 1;
    }
    return 1;
}

// PLYR_ITEM_USE: always true in FF3.
// FF3 0x001a4528

int EvPlyrItemUse(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    return 1;
}

// GET_CAM_LENS_EQUIP / GET_CAM_PARTS_EQUIP "22": lens +2 equipped (any
// character) / camera function +2 on.
// FF3 0x001a4530 / 0x001a4570

int EvGetCamLensEquip(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_char no; u_char _3; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return GetLensWrk()->IsEquip(a.no, 0xff) != 0;
}

// FF3 0x001a4570
int EvGetCamPartsEquip(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_char func; u_char _3; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return GetLensWrk()->GetFunc(a.func) != 0;
}

// REMAIN_UNDEVELOP_FILMS: a film waits to be developed (state 2).
// FF3 0x001a45a8

int EvRemainUndevelopFilms(int event_id, u_char *addr)
{
    (void)event_id;
    (void)addr;
    return DevelopFilmIsRemain() != 0;
}

// ──────────────────────────────────────────────────────────────────────
// rot (degrees, turned by 180) inside the arc center +- width (degrees,
// both taken mod 360 above 360), wrapping at 0 / 360. A width of 180 or
// more holds every rot. PLYR_ROT and the map area table call it.
// FF3 0x001a45c8

int EvRotInRange(u_short center, u_short width, short rot)
{
    int lo;
    int hi;

    if (center > 360) {
        center = (u_short)(center % 360);
    }
    if (width > 360) {
        width = (u_short)(width % 360);
    }
    lo = (short)(center - width);
    hi = (short)(center + width);
    if ((u_short)rot < 180) {
        rot = (short)(rot + 180);
    } else if ((u_short)(rot - 180) < 181) {
        rot = (short)(rot - 180);
    }
    if (width >= 180) {
        return 1;
    }
    if (lo < 0) {
        if (rot >= lo + 360 && rot < 361) {
            return 1;
        }
    } else if (hi < 361) {
        return rot >= lo && rot <= hi;
    } else {
        if (rot >= lo && rot < 361) {
            return 1;
        }
        if (rot < 0) {
            return 0;
        }
        hi -= 360;
    }
    if (rot < 0) {
        return 0;
    }
    return rot <= hi;
}

// tp in sight from vp facing rot: no farther than max_dist on the XZ plane,
// and inside OutSightChk's cone, whose angle narrows from sight_near to
// sight_far and whose reach grows from dist_near to max_dist with the
// distance.
// FF3 0x001a4708

int EvPosIntoSight(float *tp, float *vp, float rot, float max_dist, float sight_far,
                   float dist_near, float sight_near)
{
    float dx = tp[0] - vp[0];
    float dz = tp[2] - vp[2];
    float d  = sqrtf(dx * dx + dz * dz);
    float t;

    if (max_dist < d) {
        return 0;
    }
    t = d / max_dist;
    return OutSightChk(tp, vp, rot, (sight_far - sight_near) * t + sight_near,
                       (max_dist - dist_near) * t + dist_near) == 0;
}

// ──────────────────────────────────────────────────────────────────────
// GET_DREAM_PLAYER "112": the dream-world player (SET_DREAM_PLAYER) is +1.
// PLYR_IS_SELECT "112": the current player is +1. Players 0..2.
// FF3 0x001a4780 / 0x001a47d0

int EvGetDreamPlayer(int event_id, u_char *addr)
{
    struct { u_char code; u_char no; } a;
    int *p;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    if (a.no >= 3) {
        return 0;
    }
    p = GetEvDreamPlayerWrk();
    if (p == nullptr) {                 // PC: a stub returns nullptr
        return 0;
    }
    return *p == (s_char)a.no;
}

// FF3 0x001a47d0
int EvPlyrIsSelect(int event_id, u_char *addr)
{
    struct { u_char code; u_char no; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    if (a.no >= 3) {
        return 0;
    }
    return PlyrGetCurNo() == a.no;
}

// GET_HIDE_STATUS_ "11": +1 0 is true while the player hides (mode 10),
// any other value while not.
// FF3 0x001a4820

int EvGetHideStatus(int event_id, u_char *addr)
{
    struct { u_char code; s_char st; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return (PlyrIsMode10(PlyrGetCurWrk()) != 0) == (a.st == 0);
}

// PLAYER_STAT_CHECK "112": the player's state +1 (0 crawling, 1 mode 3,
// 2 mode 5, 3 hiding in mode 10; else 0) is +2 (0 / 1).
// FF3 0x001a4868

int EvPlayerStatCheck(int event_id, u_char *addr)
{
    struct { u_char code; s_char kind; short want; } a;
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int       st   = 0;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    switch (a.kind) {
    case 0:
        st = (plyr->flags & (u_long)PLYR_FLG_PZL) != 0;
        break;
    case 1:
        st = plyr->mode == 3;
        break;
    case 2:
        st = plyr->mode == 5;
        break;
    case 3:
        st = PlyrIsMode10(PlyrGetCurWrk()) != 0;
        break;
    }
    return a.want == st;
}

// PLYR_AREAIN / PLYR_AREAIN_DEF and PLYR_AREAOUT / PLYR_AREAOUT_DEF "1124":
// the player is inside (outside) rectangle +4; when true for an event in
// state 0 the event gets priority +1.
// FF3 0x001a4940 / 0x001a49b0

int EvPlyrAreaIn(int event_id, u_char *addr)
{
    struct { u_char code; u_char priority; short _2; int rect; } a;
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    memcpy(&a, addr, sizeof(a));
    if (MrecIsInEvent(plyr->pos, a.rect, plyr->floor) == 0) {
        return 0;
    }
    if (GetEvState(event_id) == 0) {
        SetEventPriority(event_id, a.priority);
    }
    return 1;
}

// FF3 0x001a49b0
int EvPlyrAreaOut(int event_id, u_char *addr)
{
    struct { u_char code; u_char priority; short _2; int rect; } a;
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;

    memcpy(&a, addr, sizeof(a));
    if (MrecIsInEvent(plyr->pos, a.rect, plyr->floor) != 0) {
        return 0;
    }
    if (GetEvState(event_id) == 0) {
        SetEventPriority(event_id, a.priority);
    }
    return 1;
}

// PLYR_ROOMIN / PLYR_ROOMOUT "22": the player's area is (is not) +2.
// FF3 0x001a4a20 / 0x001a4a58

int EvPlyrRoomIn(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; short area; } a;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    return (short)GetPlyrAreaNo() == a.area;
}

// FF3 0x001a4a58
int EvPlyrRoomOut(int event_id, u_char *addr)
{
    return EvPlyrRoomIn(event_id, addr) != 1;
}

// PLYR_ROT "222": the player faces into the arc +2 +- +4 (degrees).
// FF3 0x001a4a80

int EvPlyrRot(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; u_short center; u_short width; } a;
    float rot;

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    rot = PlyrGetJointRotY(3);
    return EvRotInRange(a.center, a.width, (short)((int)(rot * 180.0f / 3.1415927f) + 180));
}

// PLYR_INTO_SIGHT "2222": the player sees the point (+2, +4, +6) of the
// current room (room origin added): within 40, the cone narrowing from PI
// to PI / 3 while its reach grows from 4.
// FF3 0x001a4af0

int EvPlyrIntoSight(int event_id, u_char *addr)
{
    struct { u_char code; u_char _1; short x; short y; short z; } a;
    float *org;
    float  tp[4];

    (void)event_id;
    memcpy(&a, addr, sizeof(a));
    org = MapLoadGetOffset(MapLoadGetBuffID(GetPlyrAreaNo()));
    if (org == nullptr) {
        return 0;
    }
    tp[0] = (float)a.x + org[0];
    tp[1] = (float)a.y + org[1];
    tp[2] = (float)a.z + org[2];
    tp[3] = 0.0f;
    return EvPosIntoSight(tp, ingame_wrk.plyr_wrk->pos, PlyrGetJointRotY(3),
                          40.0f, 1.0471976f, 4.0f, 3.1415927f) != 0;
}
