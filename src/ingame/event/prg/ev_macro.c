/* ==========================================================================
 *  ingame/event/prg/ev_macro.c
 *
 *  Event macros: FF3 0x19b4a8..0x19b768 (FF2 ev_macro.c's functions, minus
 *  the handlers, which FF3 keeps in ev_command.c): the macro-state reset,
 *  the load restore, and the interpreter. EventExeFuncCall steps a running
 *  program: it reads the opcode byte, runs the command table's row for it
 *  (arguments decoded by the row's format string, EvGetCmdArgs), and moves
 *  event_addr past the command when the handler is done. FF3 dropped FF2's
 *  "not registered" and ENDIF asserts and the null-handler check (every row
 *  has a handler).
 * ======================================================================== */
#include "ingame/event/prg/ev_macro.h"

#include "common/get_byte.h"                    // Get1Byte
#include "ingame/event/prg/ev_change.h"         // GetEvState, EV_STATE_*
#include "ingame/event/prg/ev_chapter.h"        // EvGhostTblInit
#include "ingame/event/prg/ev_command.h"        // the handlers, EvSoundCtrlInit, EvSave*Init, EventMacroLoad_*
#include "ingame/event/prg/ev_exe.h"            // EV_EXE_CTRL, EvExeRelease, SetEventExeStatus
#include "ingame/event/prg/ev_get.h"            // EvGetCmdArgs
#include "ingame/event/prg/ev_open.h"           // EventSetCloseCondition

// ══════════════════════════════════════════════════════════════════════
// The command table (data 0x3556b8, FF2 ev_exe_wrk). Row n is opcode n:
// {opcode, handler, the command's bytes, argument format, script keyword}.
// Read from the ELF by tools/reverse/evtbl.py cmd.
// ══════════════════════════════════════════════════════════════════════

static EV_EXE_WRK ev_exe_wrk[EV_EXE_WRK_NUM] = {    // data 0x3556b8
    {   0, EvGhostPreloadRegist,      8, "1122",    "GHOST_PRELOAD_REGIST" },
    {   1, EvPlyrLock,                4, "211",     "PLYR_LOCK" },
    {   2, EvPlyrPosSet,              8, "222",     "PLYR_POS_SET" },
    {   3, EvPlyrHeightSet,           4, "22",      "PLYR_HEIGHT_SET" },
    {   4, EvPlyrRotSet,              4, "22",      "PLYR_ROT_SET" },
    {   5, EvPlyrPosMove,            12, "1122222", "PLYR_POS_MOVE" },
    {   6, EvPlyrDisp,                4, "1111",    "PLYR_DISP" },
    {   7, EvPlyrPad,                 4, "22",      "PLYR_PAD" },
    {   8, EvPlyrFloorChange,         4, "22",      "PLYR_FLOOR_CHANGE" },
    {   9, EvPlyrFinderMode,          4, "1111",    "PLYR_FINDER_MODE" },
    {  10, EvPlyrMotionCall,          4, "1111",    "PLYR_MOTION_CALL" },
    {  11, EvPlyrFacialCall,          4, "112",     "PLYR_FACIAL_CALL" },
    {  12, EvPlyrDamageRequest,       4, "1111",    "PLYR_DAMAGE_REQUEST" },
    {  13, EvPlyrMotionChange,        4, "112",     "PLYR_MOTION_CHANGE" },
    {  14, EvPlyrModelChange,         4, "112",     "PLYR_MODEL_CHANGE" },
    {  15, EvPlyrFlashLightSet,       4, "112",     "PLYR_FLASHLIGHT_SET" },
    {  16, EvPlyrGazePointObjSet,     8, "1124",    "PLYR_GAZE_POINT_OBJ_SET" },
    {  17, EvPlyrGazePointPosSet,     8, "11222",   "PLYR_GAZE_POINT_POS_SET" },
    {  18, EvPlyrGazePointPosSet,     4, "112",     "PLYR_GAZE_POINT_DEF_SET" },
    {  19, EvGhostAppear,             4, "112",     "GHOST_APPEAR" },
    {  20, EvGhostDisappear,          4, "112",     "GHOST_DISAPPEAR" },
    {  21, EvGhostRegist,             8, "11222",   "GHOST_REGIST" },
    {  22, EvGhostDelete,             8, "11222",   "GHOST_DELETE" },
    {  23, EvRandomGhostOn,           8, "1124",    "RANDOM_GHOST_ON" },
    {  24, EvRandomGhostOff,          4, "112",     "RANDOM_GHOST_OFF" },
    {  25, EvRandomGhostAllOff,       4, "22",      "RANDOM_GHOST_ALL_OFF" },
    {  26, EvRandomGhostReq,          4, "22",      "RANDOM_GHOST_REQ" },
    {  27, EvSetObjHitCheck,          8, "1124",    "SET_OBJ_HITCHECK" },
    {  28, EvSetObjEffect,            8, "1124",    "SET_OBJ_EFFECT" },
    {  29, EvSetObjVisible,           8, "1124",    "SET_OBJ_VISIBLE" },
    {  30, EvSetObjAlpha,             8, "1124",    "SET_OBJ_ALPHA" },
    {  31, EvSetObjReqAction,         8, "1124",    "SET_OBJ_REQ_ACTION" },
    {  32, EvSetObjActionType,        8, "1124",    "SET_OBJ_ACTIONTYPE" },
    {  33, EvPhotoLock,               4, "4",       "PHOTO_LOCK" },
    {  34, EvPhotoUnlock,             4, "4",       "PHOTO_UNLOCK" },
    {  35, EvItemGet,                 4, "1111",    "ITEM_GET" },
    {  36, EvItemUse,                 4, "1111",    "ITEM_USE" },
    {  37, EvItemLost,                4, "211",     "ITEM_LOST" },
    {  38, EvFileGet,                 4, "112",     "FILE_GET" },
    {  39, EvFileLost,                4, "112",     "FILE_LOST" },
    {  40, EvFileRead,                4, "112",     "FILE_READ" },
    {  41, EvSet2dmapDraw,            4, "112",     "SET_2DMAP_DRAW" },
    {  42, EvCrystalLost,             4, "112",     "CRYSTAL_LOST" },
    {  43, EvLevelGemGet,             4, "112",     "LEVELGEM_GET" },
    {  44, EvLevelGemLost,            4, "112",     "LEVELGEM_LOST" },
    {  45, EvCamSpecialShotGet,       4, "112",     "CAM_SPECIALSHOT_GET" },
    {  46, EvGetCharStat,             4, "112",     "GET_CHAR_STAT" },
    {  47, EvCamEquipFunctionGet,     4, "112",     "CAM_EQUIP_FUNCTION_GET" },
    {  48, EvMemoOpen,                4, "112",     "MEMO_OPEN" },
    {  49, EvMemoUpdate,              4, "112",     "MEMO_UPDATE" },
    {  50, EvPuzzleStart,             4, "112",     "PUZZLE_START" },
    {  51, EvEvCam,                   4, "211",     "EV_CAM" },
    {  52, EvSetEvCamVCI,             4, "112",     "SET_EV_CAM_VCI" },
    {  53, EvDeleteEvCamVci,          4, "4",       "DELETE_EV_CAM_VCI" },
    {  54, EvSetEvCamVP,              8, "2222",    "SET_EV_CAM_VP" },
    {  55, EvSetEvCamVR,              8, "2222",    "SET_EV_CAM_VR" },
    {  56, EvSetEvCamRot,             4, "22",      "SET_EV_CAM_ROT" },
    {  57, EvSetEvCamProj,            4, "22",      "SET_EV_CAM_PROJ" },
    {  58, EvCamVPSetObj,            16, "11222224", "EV_CAM_VP_SET_OBJ" },
    {  59, EvCamVRSetObj,            16, "11222224", "EV_CAM_VR_SET_OBJ" },
    {  60, EvCamSetWorldSwitch,       4, "112",     "EV_CAM_SET_WORLD_SWITCH" },
    {  61, EvMonoDisp,                4, "1111",    "MONO_DISP" },
    {  62, EvFadeIn,                  8, "11114",   "FADE_IN" },
    {  63, EvFadeOut,                 8, "11114",   "FADE_OUT" },
    {  64, EvSoundLoad,               4, "112",     "SOUND_LOAD" },
    {  65, EvSoundPreloadRegist,      4, "112",     "SOUND_PRELOAD_REGIST" },
    {  66, EvSoundRelease,            4, "112",     "EV_SOUND_RELEASE" },
    {  67, EvSoundPlay,               8, "224",     "SOUND_PLAY" },
    {  68, EvSoundStop,               8, "1124",    "SOUND_STOP" },
    {  69, EvSound3dObjPlay,         12, "11244",   "SOUND3D_OBJ_PLAY" },
    {  70, EvSoundStop,               8, "1124",    "SOUND3D_OBJ_STOP" },
    {  71, EvSound3dPosPlay,         12, "112224",  "SOUND3D_POS_PLAY" },
    {  72, EvSoundStop,               8, "1124",    "SOUND3D_POS_STOP" },
    {  73, EvStreamPlay,             12, "1122222", "STREAM_PLAY" },
    {  74, EvStreamPreload,           4, "112",     "STREAM_PRELOAD" },
    {  75, EvStreamStop,              8, "2222",    "STREAM_STOP" },
    {  76, EvMapStreamPlay,           4, "22",      "MAP_STREAM_PLAY" },
    {  77, EvMapStreamStop,           4, "22",      "MAP_STREAM_STOP" },
    {  78, EvMapStreamChange,         8, "224",     "EV_MAP_STREAM_CHAHGE" },
    {  79, EvStream3dObjPlay,        16, "112222114", "STREAM3D_OBJ_PLAY" },
    {  80, EvStreamStop,              8, "2222",    "STREAM3D_OBJ_STOP" },
    {  81, EvStream3dPosPlay,        16, "112222222", "STREAM3D_POS_PLAY" },
    {  82, EvStreamStop,              8, "2222",    "STREAM3D_POS_STOP" },
    {  83, EvStreamAllStop,           4, "4",       "STREAM_ALL_STOP" },
    {  84, EvDoorOpen,                4, "112",     "DOOR_OPEN" },
    {  85, EvDoorClose,               4, "112",     "DOOR_CLOSE" },
    {  86, EvPrivateDoorLock,         8, "1124",    "PRIVATE_DOOR_LOCK" },
    {  87, EvPrivateDoorUnlock,       8, "224",     "PRIVATE_DOOR_UNLOCK" },
    {  88, EvLoadRequest,             8, "11222",   "LOAD_REQUEST" },
    {  89, EvReleaseRequest,          8, "11222",   "RELEASE_REQUEST" },
    {  90, EvAreaChange,              8, "2222",    "AREA_CHANGE" },
    {  91, EvBackGroundLoad,          4, "112",     "BACK_GROUND_LOAD" },
    {  92, EvAreaSwitch,              4, "112",     "AREA_SWITCH" },
    {  93, EvDispMsg,                 4, "112",     "MSG_DISP" },
    {  94, EvMsgDispSel,              8, "11222",   "MSG_DISP_SEL" },
    {  95, EvMsgChoice,              12, "222222",  "MSG_CHOICE" },
    {  96, EvMoviePlay,               4, "112",     "MOVIE_PLAY" },
    {  97, EvDisp2DStart,            12, "1122222", "DISP2D_START" },
    {  98, EvDisp2DEnd,               4, "112",     "DISP2D_END" },
    {  99, EvChapterDispStart,        4, "112",     "CHAPTER_DISP_START" },
    { 100, EvEvFog,                   4, "1111",    "EV_FOG" },
    { 101, EvSetEvFogColor,           8, "111122",  "SET_EV_FOG_COLOR" },
    { 102, EvSetEvFogDistNear,        8, "11222",   "SET_EV_FOG_DIST_NEAR" },
    { 103, EvSetEvFogDistFar,         8, "11222",   "SET_EV_FOG_DIST_FAR" },
    { 104, EvSetEvFogDist,            8, "11222",   "SET_EV_FOG_DIST" },
    { 105, EvOverLapStart,            4, "112",     "EV_OVER_LAP_START" },
    { 106, EvOverLapEnd,              4, "112",     "EV_OVER_LAP_END" },
    { 107, EvSetEffVisible,           8, "11222",   "SET_EFF_VISIBLE" },
    { 108, EvFilamentTimerCall,       4, "112",     "FILAMENT_TIMER_CALL" },
    { 109, EvFilamentCall,            4, "4",       "FILAMENT_CALL" },
    { 110, EvFilamentRelease,         4, "4",       "FILAMENT_RELEASE" },
    { 111, EvSetObjAlgorithm,         8, "1124",    "SET_OBJ_ALGORITHM" },
    { 112, EvEvSetState,              4, "112",     "EV_SET_STATE" },
    { 113, EvEvSetTimer,              8, "1124",    "EV_SET_TIMER" },
    { 114, EvEvStop,                  8, "44",      "EV_STOP" },
    { 115, EvChapterLoadRequest,      4, "112",     "CHAPTER_LOAD_REQUEST" },
    { 116, EvChangeChapter,           4, "1111",    "CHANGE_CHAPTER" },
    { 117, EvGameDataSave,            4, "112",     "GAME_DATA_SAVE" },
    { 118, EvGameClear,               4, "112",     "GAME_CLEAR_" },
    { 119, EvSetConditionCheck,       4, "1111",    "SET_CONDITION_CHECK" },
    { 120, EvSetButterfly,           16, "1111444", "SET_BUTTERFLY" },
    { 121, EvMoveButterfly,          16, "112444",  "MOVE_BUTTERFLY" },
    { 122, EvReleaseButterfly,        4, "112",     "RELEASE_BUTTERFLY" },
    { 123, EvEffDitherStart,          8, "1111112", "EV_EFF_DITHER_START" },
    { 124, EvEffDitherEnd,            4, "112",     "EV_EFF_DITHER_END" },
    { 125, EvEffThunderReq,          28, "112444444", "EV_EFF_THUNDER_REQ" },
    { 126, EvSetScreenEffect,         4, "112",     "EV_SET_SCREEN_EFFECT" },
    { 127, EvSynchroModeStart,        4, "112",     "SYNCHRO_MODE_START" },
    { 128, EvSynchroModeEnd,          4, "112",     "SYNCHRO_MODE_END" },
    { 129, EvMenuLock,                4, "112",     "MENU_LOCK" },
    { 130, EvMenuUnlock,              4, "112",     "MENU_UNLOCK" },
    { 131, EvPauseLock,               4, "112",     "PAUSE_LOCK" },
    { 132, EvPauseUnlock,             4, "112",     "PAUSE_UNLOCK" },
    { 133, EvSetPhotoCurse,           4, "112",     "SET_PHOTO_CURSE" },
    { 134, EvPadWait,                 4, "22",      "EV_PAD_WAIT" },
    { 135, EvGameOverRequest,         4, "112",     "GAMEOVER_REQUEST" },
    { 136, EvSubTitleDispReq,         4, "112",     "SUBTITLE_DISP_REQ" },
#ifdef REIPAN_SLPS_25544_CHS
    { 137, EvSubTitle3DObjDispReq,    8, "1124",    "SUBTITLE3D_OBJ_DISP_REQ" },
#else
    { 137, EvSubTitle3DObjDispReq,    8, "11222",   "SUBTITLE3D_OBJ_DISP_REQ" },
#endif
    { 138, EvSubTitle3DPosDispReq,   16, "112444",  "SUBTITLE3D_POS_DISP_REQ" },
    { 139, EvSubTitleStop,            4, "112",     "SUBTITLE_STOP" },
    { 140, EvSetGhostSealDoor,        4, "112",     "SET_GHOST_SEAL_DOOR" },
    { 141, EvReleaseGhostSealDoor,    4, "112",     "RELEASE_GHOST_SEAL_DOOR" },
    { 142, EvCallGhostSealDoorBurn,   4, "112",     "CALL_GHOST_SEAL_DOOR_BURN" },
    { 143, EvMissionStart,            4, "112",     "EV_MISSION_START" },
    { 144, EvMissionClear,            4, "112",     "EV_MISSION_CLEAR" },
    { 145, EvMissionFailed,           4, "112",     "EV_MISSION_FAILED" },
    { 146, EvChapterSelInit,          8, "11222",   "CHAPTER_SEL_INIT" },
    { 147, EvIf,                      4, "4",       "IF_" },
    { 148, EvElse,                    4, "4",       "ELSE_" },
    { 149, EvElseif,                  4, "4",       "ELSEIF_" },
    { 150, EvEndif,                   4, "4",       "ENDIF_" },
    { 151, EvItemReset,               4, "4",       "ITEM_RESET" },
    { 152, EvItemTableChange,         4, "112",     "ITEM_TABLE_CHANGE_" },
    { 153, EvEventReboot,             4, "112",     "EVENT_REBOOT" },
    { 154, EvSetEventRecHit,          8, "1124",    "SET_EVENT_REC_HIT" },
    { 155, EvChangeWorld,             4, "112",     "EV_CHANGE_WORLD" },
    { 156, EvHide,                    4, "112",     "HIDE" },
    { 157, EvSetCandlePower,          4, "112",     "SET_CANDLE_POWER" },
    { 158, EvSetSleepFlg,             8, "1124",    "SET_SLEEP_FLG_" },
    { 159, EvSetNightFlg,             4, "112",     "SET_NIGHT_FLG" },
    { 160, EvSetNightStartPos,       12, "222222",  "SET_NIGHT_START_POS" },
    { 161, EvSetNightLockFlg,         4, "22",      "SET_NIGHT_LOCK_FLG" },
    { 162, EvSetCandleEnable,         4, "112",     "SET_CANDLE_ENABLE" },
    { 163, EvGoDreamWorld,            4, "4",       "GO_DREAM_WORLD" },
    { 164, EvGoRealWorld,             4, "4",       "GO_REAL_WORLD" },
    { 165, EvDevelopRequest,          4, "4",       "DEVELOP_REQUEST" },
    { 166, EvSetPhotoable,            4, "112",     "SET_PHOTOABLE_" },
    { 167, EvShootDevelopFilm,        4, "112",     "SHOOT_DEVELOP_FILM" },
    { 168, EvGetDevelopFilm,          4, "4",       "GET_DEVELOP_FILM" },
    { 169, EvResearchPermit,          4, "112",     "RESEARCH_PERMIT" },
    { 170, EvResearchStart,           4, "112",     "RESEARCH_START" },
    { 171, EvPeepingRequest,          4, "112",     "PEEPING_REQUEST" },
    { 172, EvRadioTurnOn,             4, "112",     "RADIO_TURN_ON" },
    { 173, EvRadioTurnOff,            4, "112",     "RADIO_TURN_OFF" },
    { 174, EvTelStart,                4, "1111",    "TEL_START" },
    { 175, EvTelStop,                 4, "4",       "TEL_STOP" },
    { 176, EvTelVolOff,               4, "4",       "TEL_VOL_OFF" },
    { 177, EvTelVolOn,                4, "4",       "TEL_VOL_ON" },
    { 178, EvTelTalkStart,            4, "112",     "TEL_TALK_START" },
    { 179, EvTelTalkStop,             4, "4",       "TEL_TALK_STOP" },
    { 180, EvTapeStart,               4, "4",       "TAPE_START" },
    { 181, EvCineMenuStart,           4, "4",       "CINE_MENU_START" },
    { 182, EvCineFilmPlay,            4, "112",     "CINE_FILM_PLAY" },
    { 183, EvCineFilmStop,            4, "4",       "CINE_FILM_STOP" },
    { 184, EvCamChangeType,           4, "112",     "CAM_CHANGE_TYPE" },
    { 185, EvSetObjPhotoAble,         8, "1124",    "SET_OBJ_PHOTOABLE" },
    { 186, EvSetHaichiId,             4, "112",     "SET_HAICHI_ID" },
    { 187, EvWakeupMoviePlay,         4, "4",       "WAKEUP_MOVIE_PLAY" },
    { 188, EvSetWakeupMovie,          4, "112",     "SET_WAKEUP_MOVIE" },
    { 189, EvSetWork,                 4, "112",     "EV_SET_WORK" },
    { 190, EvAddWork,                 4, "112",     "EV_ADD_WORK" },
    { 191, EvSubWork,                 4, "112",     "EV_SUB_WORK" },
    { 192, EvSleepMoviePlay,          4, "4",       "SLEEP_MOVIE_PLAY" },
    { 193, EvSetSleepMovie,           4, "112",     "SET_SLEEP_MOVIE" },
    { 194, EvPlayerActFours,          4, "112",     "PLAYER_ACT_FOURS" },
    { 195, EvSetDreamPlayer,          4, "112",     "SET_DREAM_PLAYER" },
    { 196, EvSetSpGameover,           4, "112",     "SET_SP_GAMEOVER_" },
    { 197, EvSetRealCamera,           4, "112",     "SET_REAL_CAMERA" },
    { 198, EvSetBgmList,              4, "22",      "SET_BGM_LIST" },
    { 199, EvGoUpLadder,              4, "22",      "GO_UP_LADDER" },
    { 200, EvGoDownLadder,            4, "22",      "GO_DOWN_LADDER" },
    { 201, EvSetPlayerHit,            4, "22",      "SET_PLAYER_HIT" },
    { 202, EvNpcSerifTbl,             4, "112",     "NPC_SERIF_TBL" },
    { 203, EvNpcFacialTbl,            4, "112",     "NPC_FACIAL_TBL" },
    { 204, EvNpcFixEvent,             4, "1111",    "NPC_FIX_EVENT" },
    { 205, EvNpcSetPos,              12, "11112222", "NPC_SET_POS" },
    { 206, EvNpcSetPosLabel,          4, "1111",    "NPC_SET_POS_LABEL" },
    { 207, EvNpcMotionCall,           8, "11222",   "NPC_MOTION_CALL" },
    { 208, EvNpcFacialCall,           4, "112",     "NPC_FACIAL_CALL" },
    { 209, EvNpcActiveFlg,            4, "1111",    "NPC_ACTIVE_FLG" },
    { 210, EvNpcModelChange,          4, "1111",    "NPC_MODEL_CHANGE" },
    { 211, EvNpcEyeTarget,           12, "11112222", "NPC_EYE_TARGET" },
    { 212, EvNpcLockPos,              4, "1111",    "NPC_LOCK_POS" },
    { 213, EvChangeGhostAttr,        12, "11244",   "CHANGE_GHOST_ATTR" },
    { 214, EvMapUpdate,               4, "22",      "MAP_UPDATE" },
    { 215, EvSetPlyrLookFurn,         8, "1124",    "SET_PLYR_LOOK_FURN" },
    { 216, EvSetPlyrLookDirect,       8, "11222",   "SET_PLYR_LOOK_DIRECT" },
    { 217, EvStopPlyrLookDirect,      4, "4",       "STOP_PLYR_LOOK_DIRECT" },
    { 218, EvSetFurnWind,             8, "112211",  "SET_FURN_WIND" },
    { 219, EvFurnCandleBlazeUp,       4, "112",     "FURN_CANDLE_BLAZE_UP" },
    { 220, EvReqPlSurprise,           4, "4",       "EV_REQ_PL_SURPRISE" },
    { 221, EvSetHandlighteffectDisp,  4, "22",      "SET_HANDLIGHTEFFECT_DISP" },
    { 222, EvSetMemoOpenFalg,         4, "22",      "SET_MEMO_OPEN_FALG" },
    { 223, EvSetSpEvent,              4, "22",      "SET_SP_EVENT" },
    { 224, EvChangeTattooLevel,       4, "211",     "CHANGE_TATTOO_LEVEL" },
    { 225, EvMissionSetup,            4, "4",       "MISSION_SETUP" },
    { 226, EvPlyrMotionModeEnd,       4, "4",       "PLYR_MOTION_MODE_END" },
    { 227, EvSetRadioLockFlg,         4, "22",      "SET_RADIO_LOCK_FLG" },
    { 228, EvMemoMenuOpen,            4, "22",      "MEMO_MENU_OPEN" },
    { 229, EvCaptionSet,              4, "211",     "CAPTION_SET" },
    { 230, EvSetMapInLockFlg,         4, "22",      "SET_MAP_IN_LOCK_FLG" },
    { 231, EvFadeHighcontrast,        4, "211",     "FADE_HIGHCONTRAST" },
    { 232, EvSetLanterneffectEnable,  4, "22",      "SET_LANTERNEFFECT_ENABLE" },
    { 233, EvSetMuteSeVolume,         4, "22",      "SET_MUTE_SE_VOLUME" },
    { 234, EvBgSeStop,                4, "4",       "BG_SE_STOP" },
};

// ──────────────────────────────────────────────────────────────────────
// Every macro control back to its start (EventExeInit). FF3 0x0019b4a8;
// FF2 EventMacroInit (FF3 adds the ghost tables).

void EventMacroInit(void)
{
    EvSoundCtrlInit();
    EvStreamCtrlInit();
    EvEffCtrlInit();
    EvSaveStreamInit();
    EvSaveEffDitherInit();
    EvSaveScreenEffectInit();
    EvGhostTblInit();
}

// ──────────────────────────────────────────────────────────────────────
// After a game file was loaded (IngameLoadRestore): the saved dither and
// screen effect started again. FF3 0x0019b4f0; FF2 EventMacroLoadInit's
// last two calls (FF3 restarts the saved streams elsewhere).

void EventLoadRestore(void)
{
    EventMacroLoad_EffDitherInit();
    EventMacroLoad_ScreenEffectInit();
}

// ──────────────────────────────────────────────────────────────────────
// EV_END: the program is over. Its slot is freed; an init program ending
// queues the main program, a main program ending posts the close
// conditions. FF3 0x0019b510; FF2 Event_EvEnd without its asserts (a
// missing ENDIF_, an unknown state).

static void Event_EvEnd(EV_EXE_CTRL *ctrl)
{
    int event_id = ctrl->event_id;

    EvExeRelease(event_id);
    switch (GetEvState(event_id)) {
    case EV_STATE_INIT:
        SetEventExeStatus(event_id);
        break;
    case EV_STATE_EXE:
        EventSetCloseCondition(event_id);
        break;
    }
}

// (0x19b580..0x19b5b8: a busy-wait loop, a0 times four nops, that nothing
// calls or points at; FF2 has no counterpart. Not reconstructed.)

// ──────────────────────────────────────────────────────────────────────
// Runs command com: its arguments decoded into an array on this frame
// (ctrl->args points at it for the handler), then its handler. Returns the
// handler's result as a byte. FF3 0x0019b5b8; FF2 inlined the call in
// EventExeFuncCall and had no argument decoding.

static u_char EventExeCmdCall(int com, EV_EXE_CTRL *ctrl)
{
    int args[0x20];                     // 0x80 bytes of stack on the EE

    ctrl->args = args;
    EvGetCmdArgs(args, ctrl->event_addr, ev_exe_wrk[com].fmt);
    return (u_char)ev_exe_wrk[com].func(ctrl);
}

// ──────────────────────────────────────────────────────────────────────
// One frame of a running program (EventExe; SetEventInitStatus runs an init
// program through it at once): commands one after the other until one
// wants another frame or the program ends. FF3 0x0019b618; FF2
// EventExeFuncCall (opcodes renumbered: IF_ .. ENDIF_ are 0x93..0x96).
//
// IF_ .. ENDIF_ run even inside a skipped branch, since they are what
// moves if_state; their result is not looked at, and they step over their
// condition too (cond_size, which IF_ / ELSEIF_ set). Any other command in a
// skipped branch is stepped over unrun. A handler returning neither 0 nor 1
// is called again at once. Opcodes 0xeb..0xfe have no row (as in FF3, not
// checked).

void EventExeFuncCall(EV_EXE_CTRL *ctrl)
{
    int    loop_flg = 1;
    u_char com;
    u_char ret;

    do {
        com = Get1Byte(ctrl->event_addr);

        if (com >= EV_COM_IF && com <= EV_COM_ENDIF) {
            ctrl->cond_size = 0;
            EventExeCmdCall(com, ctrl);
            ctrl->event_addr += ctrl->cond_size + ev_exe_wrk[com].size;
        }
        else if (com == EV_COM_END) {
            loop_flg = 0;
            Event_EvEnd(ctrl);
        }
        else if (ctrl->if_state == EV_IF_FALSE || ctrl->if_state == EV_IF_DONE) {
            ctrl->event_addr += ev_exe_wrk[com].size;
        }
        else {
            ret = EventExeCmdCall(com, ctrl);
            if (ret == 0) {
                loop_flg = 0;           // again next frame
            }
            else if (ret == 1) {
                ctrl->process = 0;
                ctrl->event_addr += ev_exe_wrk[com].size;
            }
        }
    } while (loop_flg);
}

// ──────────────────────────────────────────────────────────────────────
// PC: a row of the command table for the Debug menu's event browser, which
// names and steps over the commands of programs it describes. Not in FF3.

const EV_EXE_WRK *EvMacroGetWrk(int com)
{
    return com >= 0 && com < EV_EXE_WRK_NUM ? &ev_exe_wrk[com] : nullptr;
}
