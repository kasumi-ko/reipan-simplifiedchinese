/* ==========================================================================
 *  main/gphase.c
 *
 *  The game-phase engine (see gphase.h): the five phase-function tables and
 *  the tree definition (data 0x3275c0..0x3282e0), the per-phase dispatchers
 *  (GPhaseCall*), the node state machine (GPhaseNode*) and the controller
 *  (GPhaseCtrl*), plus its two singleton getters.
 *  FF3 0x1d3b40..0x1d4240, 0x2ab3e0, 0x2abe40. FF2 main/gphase.c is the
 *  ancestor in name only: FF2 ran fixed layers (DoJobPhase per layer), FF3
 *  keeps a node stack over a re-linkable tree.
 *
 *  FF3 reads the singletons through gp-relative cached pointers (if (!p)
 *  p = GetGPhaseCtrl();); nothing depends on the cache, so calls go to the
 *  getters directly. FF3 0x001d4210 is not written: a compiler-emitted
 *  static-destruction thunk (GPhaseTreeTerm on the tree singleton) that marks
 *  the end of the file.
 * ======================================================================== */
#include "main/gphase.h"

#include <stdio.h>                  // printf (PC: unwritten-phase report)
#include <stdlib.h>

#include "main/main.h"              // phase 0: *_super
#include "album/prg/album_edit.h"  // phases 8, 24..26, 52, 54, 78
#include "album/prg/album_phase.h" // phases 27, 28
#include "album/prg/album_zoom.h"  // phases 53, 79, 106
#include "graphics/movie/movie.h"   // phase 39
#include "graphics/movie/story_movie.h" // phases 67, 90
#include "graphics/scene/IngameScene.h" // phases 66, 89
#include "ingame/candle_ctrl.h"   // phase 48
#include "ingame/cine_menu.h"     // phase 47
#include "ingame/clear/ClearMenu.h"  // phases 43, 70
#include "ingame/event/prg/ev_command.h"  // phase 36
#include "ingame/gameover/prg/gameover.h"  // phases 10, 40
#include "ingame/ingame.h"          // phase 12
#include "ingame/ingame_wrk.h"      // phase 11
#include "ingame/menu/menu_cassette.h"  // phase 46
#include "ingame/menu/menu_file.h"  // phase 86
#include "ingame/menu/menu_file_list.h"  // phases 62, 87
#include "ingame/story_menu_ghost_list.h"  // phases 63, 88
#include "ingame/menu/start_menu.h"  // phases 55, 56, 81..83, 109..115, 119
#include "ingame/menu/start_menu_top.h"  // phases 80, 107, 108, 118
#include "ingame/mission/mission_end.h"  // phases 50, 77
#include "ingame/mission/mission_start.h"  // phase 31
#include "ingame/pause/prg/pause.h" // phase 44
#include "ingame/photo/photo.h"     // phase 45
#include "ingame/photo/photo_dat.h" // phase 33
#include "ingame/plyr/unit_ctl.h"   // phases 6, 18
#include "ingame/puzzle/kamon/kamon_pzl.h"  // phases 74, 97..99
#include "ingame/puzzle/kaza/kaza_pzl.h"    // phases 73, 95, 96
#include "ingame/puzzle/kusi/kusi_pzl.h"    // phases 72, 91..94
#include "ingame/puzzle/rousoku/rousoku_pzl.h" // phases 76, 104, 105
#include "ingame/puzzle/sizumedama/sizumedama_pzl.h" // phases 75, 100..103, 116, 117
#include "ingame/puzzle/puzzle.h"   // phases 49, 71
#include "ingame/story_load_mc.h"  // phase 68
#include "ingame/story_develop.h"  // phases 38, 65
#include "ingame/story_door.h"     // phase 34
#include "ingame/story_game_clear.h" // phase 42
#include "ingame/story_night.h"    // phase 35
#include "ingame/story_main.h"     // phases 9, 29, 30, 32
#include "ingame/story_menu.h"     // phase 37
#include "ingame/story_menu_camera.h" // phase 64
#include "ingame/story_menu_film.h"   // phase 60
#include "ingame/story_menu_map.h" // phase 58
#include "ingame/menu/menu_item.h"   // phase 59
#include "ingame/story_menu_photo.h" // phase 61
#include "ingame/story_menu_photo_view.h" // phases 84, 85
#include "ingame/story_menu_top.h"   // phase 57
#include "ingame/story_save.h"     // phase 41
#include "ingame/story_save_mc.h"  // phase 69
#include "outgame/boot.h"           // phases 3, 4, 13, 14
#include "outgame/lang_sel.h"       // phase 2
#include "outgame/option.h"         // phase 7
#include "outgame/option_top.h"     // phase 19
#include "outgame/option_system.h"  // phase 20
#include "outgame/option_akarusa.h" // phase 21
#include "outgame/option_sousa.h"   // phase 22
#include "outgame/option_button.h"  // phase 23
#include "outgame/pad_check.h"      // phase 1
#include "outgame/title_menu.h"     // phases 5, 15, 16, 17, 51
#include "outgame/title_room.h"     // phase 5 (PORT: the room backdrop wraps the title's callbacks)

// ──────────────────────────────────────────────────────────────────────
// Phase tables
// ──────────────────────────────────────────────────────────────────────

// PC: only the phases reconstructed so far are filled in; an empty init/one/
// end slot (never empty in FF3) is reported once and stands in as described
// at GPhaseCallMissing. Fill a slot when its phase's TU is written; the FF3
// address of every slot is in gphase_ff3_func below.
//
// PC: Title_Mode's (5) init / pre / end are outgame/title_room.c's wrappers,
// which run title_menu.c's own and put the title's room backdrop around them.

// FF3 0x003275c0
static GPHASE_FUNC ini_func[GPHASE_NUM] =
{
    /*   0 */ init_super,
    /*   1 */ init_Boot_PadCheck, init_LangSel_Main, init_Boot_Main, init_Logo_Main, init_TitleRoom_Mode,
    /*   6 */ init_FrameRateSel_Main, init_Option_Main, init_Album_Main, init_Story_Main, init_GameOver,
    /*  11 */ init_Phase11,
    /*  12 */ init_Phase12,
    /*  13 */ init_Boot_Load, init_AutoLoad_Main, init_Title_Top, init_Title_Move_Movie, init_Title_Menu, init_Title_FrameRate_Sel, init_OptionTop,
    /*  20 */ init_OptionSystem, init_OptionAkarusa, init_OptionSousa, init_OptionButton, init_Album_Open, init_Album_Copy, init_Album_Menu, init_Album_Save, init_Album_Load, init_Story_Start,
    /*  30 */ init_Story_NowLoading, init_Story_Mission_St, init_Story_Normal, init_Story_Photo, init_Story_Door_Open, init_Story_NightChange, init_Story_RoomReload,
    /*  37 */ init_Story_Menu, init_Story_Develop, init_Story_SceneMovie,
    /*  40 */ init_Story_LoadGame, init_Story_SaveGame, init_Story_GameClear, init_Story_ClearMenu,
    /*  44 */ init_Story_Pause, init_Story_Peeping, init_Story_Cassette, init_Story_CineMenu, init_Story_CandleMono, init_Story_Puzzle,
    /*  50 */ init_Story_Mission_End, init_Title_NewGame, init_Album_Sort, init_Album_Zoom, init_Album_Help, init_StartMenu_Main, init_Story_Opening, init_StoryMenu_Top, init_StoryMenu_Map, init_StoryMenu_Item,
    /*  60 */ init_StoryMenu_Film, init_StoryMenu_Photo, init_StoryMenu_File, init_StoryMenu_GhostList, init_StoryMenu_Camera, init_Story_DevelopFilm, init_Story_Scene, init_Story_Movie,
    /*  68 */ init_Story_LoadGame_Mc, init_Story_SaveGame_Mc, init_Story_ClearMenu_Main, init_Story_PuzzleNoItem,
    /*  72 */ init_Story_PzlKusi, init_Story_PzlKaza, init_Story_PzlKamon, init_Story_PzlSizume, init_Story_PzlRousoku, init_Story_Mission_Result, init_Album_Confirm, init_Album_ZoomSort,
    /*  80 */ init_StartMenu_Top, init_StartMenu_Option, init_StartMenu_Gallery, init_StartMenu_Mission, init_StoryMenu_PhotoDetail, init_StoryMenu_PhotoMap, init_StoryMenu_FileDetail, init_StoryMenu_FileFirst, init_StoryMenu_GhostListBonus, init_Story_Scene_Main,
    /*  90 */ init_Story_Movie_Main, init_PzlKusi_Select, init_PzlKusi_Move, init_PzlKusi_Slide, init_PzlKusi_Retry, init_PzlKaza_Select, init_PzlKaza_Turn, init_PzlKamon_Select, init_PzlKamon_Move, init_PzlKamon_Retry,
    /* 100 */ init_PzlSizume_Daiza, init_PzlSizume_Stone, init_PzlSizume_Remove, init_PzlSizume_Play, init_PzlRousoku_Select, init_PzlRousoku_Check, init_Album_ZoomConfirm, init_StartMenu_Difficulty, init_StartMenu_Costume,
    /* 109 */ init_StartMenu_OptionMain, init_StartMenu_OptionSave, init_StartMenu_GalleryMain, init_StartMenu_GalleryView,
    /* 113 */ init_StartMenu_GalleryZero, init_StartMenu_MissionMain, init_StartMenu_MissionSave, init_PzlSizume_PlayClear, init_PzlSizume_PlayWrong, init_StartMenu_CostumeSel,
    /* 119 */ init_StartMenu_MissionSel,
};

// FF3 0x003277a0
static GPHASE_FUNC one_func[GPHASE_NUM] =
{
    /*   0 */ one_super,
    /*   1 */ one_Boot_PadCheck, one_LangSel_Main, one_Boot_Main, one_Logo_Main, one_Title_Mode,
    /*   6 */ one_FrameRateSel_Main, one_Option_Main, one_Album_Main, one_Story_Main, one_GameOver,
    /*  11 */ one_Phase11,
    /*  12 */ one_Phase12,
    /*  13 */ one_Boot_Load, one_AutoLoad_Main, one_Title_Top, one_Title_Move_Movie, one_Title_Menu, one_Title_FrameRate_Sel, one_OptionTop,
    /*  20 */ one_OptionSystem, one_OptionAkarusa, one_OptionSousa, one_OptionButton, one_Album_Open, one_Album_Copy, one_Album_Menu, one_Album_Save, one_Album_Load, one_Story_Start,
    /*  30 */ one_Story_NowLoading, one_Story_Mission_St, one_Story_Normal, one_Story_Photo, one_Story_Door_Open, one_Story_NightChange, one_Story_RoomReload,
    /*  37 */ one_Story_Menu, one_Story_Develop, one_Story_SceneMovie,
    /*  40 */ one_Story_LoadGame, one_Story_SaveGame, one_Story_GameClear, one_Story_ClearMenu,
    /*  44 */ one_Story_Pause, one_Story_Peeping, one_Story_Cassette, one_Story_CineMenu, one_Story_CandleMono, one_Story_Puzzle,
    /*  50 */ one_Story_Mission_End, one_Title_NewGame, one_Album_Sort, one_Album_Zoom, one_Album_Help, one_StartMenu_Main, one_Story_Opening, one_StoryMenu_Top, one_StoryMenu_Map, one_StoryMenu_Item,
    /*  60 */ one_StoryMenu_Film, one_StoryMenu_Photo, one_StoryMenu_File, one_StoryMenu_GhostList, one_StoryMenu_Camera, one_Story_DevelopFilm, one_Story_Scene, one_Story_Movie,
    /*  68 */ one_Story_LoadGame_Mc, one_Story_SaveGame_Mc, one_Story_ClearMenu_Main, one_Story_PuzzleNoItem,
    /*  72 */ one_Story_PzlKusi, one_Story_PzlKaza, one_Story_PzlKamon, one_Story_PzlSizume, one_Story_PzlRousoku, one_Story_Mission_Result, one_Album_Confirm, one_Album_ZoomSort,
    /*  80 */ one_StartMenu_Top, one_StartMenu_Option, one_StartMenu_Gallery, one_StartMenu_Mission, one_StoryMenu_PhotoDetail, one_StoryMenu_PhotoMap, one_StoryMenu_FileDetail, one_StoryMenu_FileFirst, one_StoryMenu_GhostListBonus, one_Story_Scene_Main,
    /*  90 */ one_Story_Movie_Main, one_PzlKusi_Select, one_PzlKusi_Move, one_PzlKusi_Slide, one_PzlKusi_Retry, one_PzlKaza_Select, one_PzlKaza_Turn, one_PzlKamon_Select, one_PzlKamon_Move, one_PzlKamon_Retry,
    /* 100 */ one_PzlSizume_Daiza, one_PzlSizume_Stone, one_PzlSizume_Remove, one_PzlSizume_Play, one_PzlRousoku_Select, one_PzlRousoku_Check, one_Album_ZoomConfirm, one_StartMenu_Difficulty, one_StartMenu_Costume,
    /* 109 */ one_StartMenu_OptionMain, one_StartMenu_OptionSave, one_StartMenu_GalleryMain, one_StartMenu_GalleryView,
    /* 113 */ one_StartMenu_GalleryZero, one_StartMenu_MissionMain, one_StartMenu_MissionSave, one_PzlSizume_PlayClear, one_PzlSizume_PlayWrong, one_StartMenu_CostumeSel,
    /* 119 */ one_StartMenu_MissionSel,
};

// FF3 0x00327980
static GPHASE_FUNC end_func[GPHASE_NUM] =
{
    /*   0 */ end_super,
    /*   1 */ end_Boot_PadCheck, end_LangSel_Main, end_Boot_Main, end_Logo_Main, end_TitleRoom_Mode,
    /*   6 */ end_FrameRateSel_Main, end_Option_Main, end_Album_Main, end_Story_Main, end_GameOver,
    /*  11 */ end_Phase11,
    /*  12 */ end_Phase12,
    /*  13 */ end_Boot_Load, end_AutoLoad_Main, end_Title_Top, end_Title_Move_Movie, end_Title_Menu, end_Title_FrameRate_Sel, end_OptionTop,
    /*  20 */ end_OptionSystem, end_OptionAkarusa, end_OptionSousa, end_OptionButton, end_Album_Open, end_Album_Copy, end_Album_Menu, end_Album_Save, end_Album_Load, end_Story_Start,
    /*  30 */ end_Story_NowLoading, end_Story_Mission_St, end_Story_Normal, end_Story_Photo, end_Story_Door_Open, end_Story_NightChange, end_Story_RoomReload,
    /*  37 */ end_Story_Menu, end_Story_Develop, end_Story_SceneMovie,
    /*  40 */ end_Story_LoadGame, end_Story_SaveGame, end_Story_GameClear, end_Story_ClearMenu,
    /*  44 */ end_Story_Pause, end_Story_Peeping, end_Story_Cassette, end_Story_CineMenu, end_Story_CandleMono, end_Story_Puzzle,
    /*  50 */ end_Story_Mission_End, end_Title_NewGame, end_Album_Sort, end_Album_Zoom, end_Album_Help, end_StartMenu_Main, end_Story_Opening, end_StoryMenu_Top, end_StoryMenu_Map, end_StoryMenu_Item,
    /*  60 */ end_StoryMenu_Film, end_StoryMenu_Photo, end_StoryMenu_File, end_StoryMenu_GhostList, end_StoryMenu_Camera, end_Story_DevelopFilm, end_Story_Scene, end_Story_Movie,
    /*  68 */ end_Story_LoadGame_Mc, end_Story_SaveGame_Mc, end_Story_ClearMenu_Main, end_Story_PuzzleNoItem,
    /*  72 */ end_Story_PzlKusi, end_Story_PzlKaza, end_Story_PzlKamon, end_Story_PzlSizume, end_Story_PzlRousoku, end_Story_Mission_Result, end_Album_Confirm, end_Album_ZoomSort,
    /*  80 */ end_StartMenu_Top, end_StartMenu_Option, end_StartMenu_Gallery, end_StartMenu_Mission, end_StoryMenu_PhotoDetail, end_StoryMenu_PhotoMap, end_StoryMenu_FileDetail, end_StoryMenu_FileFirst, end_StoryMenu_GhostListBonus, end_Story_Scene_Main,
    /*  90 */ end_Story_Movie_Main, end_PzlKusi_Select, end_PzlKusi_Move, end_PzlKusi_Slide, end_PzlKusi_Retry, end_PzlKaza_Select, end_PzlKaza_Turn, end_PzlKamon_Select, end_PzlKamon_Move, end_PzlKamon_Retry,
    /* 100 */ end_PzlSizume_Daiza, end_PzlSizume_Stone, end_PzlSizume_Remove, end_PzlSizume_Play, end_PzlRousoku_Select, end_PzlRousoku_Check, end_Album_ZoomConfirm, end_StartMenu_Difficulty, end_StartMenu_Costume,
    /* 109 */ end_StartMenu_OptionMain, end_StartMenu_OptionSave, end_StartMenu_GalleryMain, end_StartMenu_GalleryView,
    /* 113 */ end_StartMenu_GalleryZero, end_StartMenu_MissionMain, end_StartMenu_MissionSave, end_PzlSizume_PlayClear, end_PzlSizume_PlayWrong, end_StartMenu_CostumeSel,
    /* 119 */ end_StartMenu_MissionSel,
};

// FF3 0x00327b60 (nullptr in FF3 for phases without one)
static GPHASE_SUB_FUNC pre_func[GPHASE_NUM] =
{
    /*   0 */ pre_super,
    /*   1 */ nullptr, nullptr, pre_Boot_Main, nullptr, pre_TitleRoom_Mode,
    /*   6 */ pre_FrameRateSel_Main, pre_Option_Main, pre_Album_Main, pre_Story_Main, nullptr,
    /*  11 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, pre_Title_Menu, nullptr, nullptr,
    /*  20 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, pre_Album_Menu, nullptr, nullptr, pre_Story_Start,
    /*  30 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  37 */ pre_Story_Menu, pre_Story_Develop, pre_Story_SceneMovie,
    /*  40 */ pre_Story_LoadGame, pre_Story_SaveGame, nullptr, pre_Story_ClearMenu, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  49 */ pre_Story_Puzzle,
    /*  50 */ pre_Story_Mission_End, nullptr, pre_Album_Sort, pre_Album_Zoom, nullptr, pre_StartMenu_Main, nullptr, nullptr, nullptr, nullptr,
    /*  60 */ nullptr, pre_StoryMenu_Photo, pre_StoryMenu_File, pre_StoryMenu_GhostList, nullptr, nullptr, pre_Story_Scene, pre_Story_Movie, nullptr, nullptr,
    /*  70 */ nullptr, nullptr, pre_Story_PzlKusi, pre_Story_PzlKaza, pre_Story_PzlKamon, pre_Story_PzlSizume, pre_Story_PzlRousoku, nullptr, nullptr, pre_Album_ZoomSort,
    /*  80 */ pre_StartMenu_Top, pre_StartMenu_Option, pre_StartMenu_Gallery, pre_StartMenu_Mission, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  90 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 100 */ nullptr, nullptr, nullptr, pre_PzlSizume_Play, nullptr, nullptr, nullptr, nullptr, pre_StartMenu_Costume, nullptr,
    /* 110 */ nullptr, nullptr, nullptr, nullptr, pre_StartMenu_MissionMain,
};

// FF3 0x00327d40 (nullptr in FF3 for phases without one)
static GPHASE_SUB_FUNC after_func[GPHASE_NUM] =
{
    /*   0 */ after_super,
    /*   1 */ nullptr, nullptr, after_Boot_Main, nullptr, after_Title_Mode,
    /*   6 */ after_FrameRateSel_Main, after_Option_Main, after_Album_Main, after_Story_Main, nullptr,
    /*  11 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, after_Title_Menu, nullptr, nullptr,
    /*  20 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, after_Album_Menu, nullptr, nullptr, after_Story_Start,
    /*  30 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  37 */ after_Story_Menu, after_Story_Develop, after_Story_SceneMovie,
    /*  40 */ after_Story_LoadGame, after_Story_SaveGame, nullptr, after_Story_ClearMenu, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  49 */ after_Story_Puzzle,
    /*  50 */ after_Story_Mission_End, nullptr, after_Album_Sort, after_Album_Zoom, nullptr, after_StartMenu_Main, nullptr, nullptr, nullptr, nullptr,
    /*  60 */ nullptr, after_StoryMenu_Photo, after_StoryMenu_File, after_StoryMenu_GhostList, nullptr, nullptr, after_Story_Scene, after_Story_Movie, nullptr, nullptr,
    /*  70 */ nullptr, nullptr, after_Story_PzlKusi, after_Story_PzlKaza, after_Story_PzlKamon, after_Story_PzlSizume, after_Story_PzlRousoku, nullptr, nullptr, after_Album_ZoomSort,
    /*  80 */ after_StartMenu_Top, after_StartMenu_Option, after_StartMenu_Gallery, after_StartMenu_Mission, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  90 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 100 */ nullptr, nullptr, nullptr, after_PzlSizume_Play, nullptr, nullptr, nullptr, nullptr, after_StartMenu_Costume, nullptr,
    /* 110 */ nullptr, nullptr, nullptr, nullptr, after_StartMenu_MissionMain,
};

// The tree as the ROM defines it (data 0x327f20); GameSystemStart hands it to
// GPhaseTreeInit. Comments: phase id, name where known (architecture.md).
GPHASE_TREE_DEF gphase_tree_def =
{{
    // depth, parent, child, child_num
    { 0,  -1,   1, 12 }, //   0 super
    { 1,   0,  -1,  0 }, //   1 Boot_PadCheck
    { 1,   0,  -1,  0 }, //   2 LangSel_Main
    { 1,   0,  13,  2 }, //   3 Boot_Main
    { 1,   0,  -1,  0 }, //   4 Logo_Main
    { 1,   0,  15,  3 }, //   5 Title_Mode
    { 1,   0,  18,  1 }, //   6 FrameRateSel_Main
    { 1,   0,  19,  5 }, //   7 Option_Main
    { 1,   0,  24,  5 }, //   8
    { 1,   0,  29, 22 }, //   9 Story_Main
    { 1,   0,  -1,  0 }, //  10
    { 1,   0,  -1,  0 }, //  11
    { 1,   0,  -1,  0 }, //  12
    { 2,   3,  -1,  0 }, //  13 Boot_Load
    { 2,   3,  -1,  0 }, //  14 AutoLoad_Main
    { 2,   5,  -1,  0 }, //  15 Title_Top
    { 2,   5,  -1,  0 }, //  16 Title_Move_Movie
    { 2,   5,  51,  1 }, //  17 Title_Menu
    { 2,   6,  -1,  0 }, //  18 Title_FrameRate_Sel
    { 2,   7,  -1,  0 }, //  19
    { 2,   7,  -1,  0 }, //  20
    { 2,   7,  -1,  0 }, //  21
    { 2,   7,  -1,  0 }, //  22
    { 2,   7,  -1,  0 }, //  23
    { 2,   8,  -1,  0 }, //  24
    { 2,   8,  -1,  0 }, //  25
    { 2,   8,  52,  3 }, //  26
    { 2,   8,  -1,  0 }, //  27
    { 2,   8,  -1,  0 }, //  28
    { 2,   9,  55,  2 }, //  29 Story_Start
    { 2,   9,  -1,  0 }, //  30 Story_NowLoading
    { 2,   9,  -1,  0 }, //  31 Story_Mission_St
    { 2,   9,  -1,  0 }, //  32 Story_Normal
    { 2,   9,  -1,  0 }, //  33 Story_Photo
    { 2,   9,  -1,  0 }, //  34 Story_Door_Open
    { 2,   9,  -1,  0 }, //  35
    { 2,   9,  -1,  0 }, //  36
    { 2,   9,  57,  8 }, //  37 Story_Menu
    { 2,   9,  65,  1 }, //  38
    { 2,   9,  66,  2 }, //  39 Story_SceneMovie
    { 2,   9,  68,  1 }, //  40 Story_LoadGame
    { 2,   9,  69,  1 }, //  41 Story_SaveGame
    { 2,   9,  -1,  0 }, //  42
    { 2,   9,  70,  1 }, //  43
    { 2,   9,  -1,  0 }, //  44 Story_Pause
    { 2,   9,  -1,  0 }, //  45
    { 2,   9,  -1,  0 }, //  46 Story_Cassette
    { 2,   9,  -1,  0 }, //  47 Story_CineMenu
    { 2,   9,  -1,  0 }, //  48
    { 2,   9,  71,  6 }, //  49 Story_Puzzle (was Story_Overlay)
    { 2,   9,  77,  1 }, //  50 Story_Mission_End
    { 3,  17,  -1,  0 }, //  51 Title_NewGame
    { 3,  26,  78,  1 }, //  52
    { 3,  26,  79,  1 }, //  53 Album_Zoom
    { 3,  26,  -1,  0 }, //  54
    { 3,  29,  80,  4 }, //  55 StartMenu_Main
    { 3,  29,  -1,  0 }, //  56 Story_Opening
    { 3,  37,  -1,  0 }, //  57 StoryMenu_Top
    { 3,  37,  -1,  0 }, //  58 StoryMenu_Map
    { 3,  37,  -1,  0 }, //  59 StoryMenu_Item
    { 3,  37,  -1,  0 }, //  60 StoryMenu_Film
    { 3,  37,  84,  2 }, //  61 StoryMenu_Photo
    { 3,  37,  86,  2 }, //  62 StoryMenu_File
    { 3,  37,  88,  1 }, //  63 StoryMenu_GhostList
    { 3,  37,  -1,  0 }, //  64 StoryMenu_Camera
    { 3,  38,  -1,  0 }, //  65
    { 3,  39,  89,  1 }, //  66 Story_Scene
    { 3,  39,  90,  1 }, //  67 Story_Movie
    { 3,  40,  -1,  0 }, //  68 Story_LoadGame_Mc
    { 3,  41,  -1,  0 }, //  69 Story_SaveGame_Mc
    { 3,  43,  -1,  0 }, //  70
    { 3,  49,  -1,  0 }, //  71 Story_PuzzleNoItem
    { 3,  49,  91,  4 }, //  72 Story_PzlKusi
    { 3,  49,  95,  2 }, //  73 Story_PzlKaza
    { 3,  49,  97,  3 }, //  74 Story_PzlKamon
    { 3,  49, 100,  4 }, //  75 Story_PzlSizume
    { 3,  49, 104,  2 }, //  76 Story_PzlRousoku
    { 3,  50,  -1,  0 }, //  77 Story_Mission_Result
    { 4,  52,  -1,  0 }, //  78
    { 4,  53, 106,  1 }, //  79 Album_ZoomSort
    { 4,  55, 107,  2 }, //  80 StartMenu_Top
    { 4,  55, 109,  2 }, //  81 StartMenu_Option
    { 4,  55, 111,  3 }, //  82 StartMenu_Gallery
    { 4,  55, 114,  2 }, //  83 StartMenu_Mission
    { 4,  61,  -1,  0 }, //  84 StoryMenu_PhotoDetail
    { 4,  61,  -1,  0 }, //  85 StoryMenu_PhotoMap
    { 4,  62,  -1,  0 }, //  86 StoryMenu_FileDetail
    { 4,  62,  -1,  0 }, //  87 StoryMenu_FileFirst
    { 4,  63,  -1,  0 }, //  88 StoryMenu_GhostListBonus
    { 4,  66,  -1,  0 }, //  89 Story_Scene_Main
    { 4,  67,  -1,  0 }, //  90 Story_Movie_Main
    { 4,  72,  -1,  0 }, //  91 PzlKusi_Select
    { 4,  72,  -1,  0 }, //  92 PzlKusi_Move
    { 4,  72,  -1,  0 }, //  93 PzlKusi_Slide
    { 4,  72,  -1,  0 }, //  94 PzlKusi_Retry
    { 4,  73,  -1,  0 }, //  95 PzlKaza_Select
    { 4,  73,  -1,  0 }, //  96 PzlKaza_Turn
    { 4,  74,  -1,  0 }, //  97 PzlKamon_Select
    { 4,  74,  -1,  0 }, //  98 PzlKamon_Move
    { 4,  74,  -1,  0 }, //  99 PzlKamon_Retry
    { 4,  75,  -1,  0 }, // 100 PzlSizume_Daiza
    { 4,  75,  -1,  0 }, // 101 PzlSizume_Stone
    { 4,  75,  -1,  0 }, // 102 PzlSizume_Remove
    { 4,  75, 116,  2 }, // 103 PzlSizume_Play
    { 4,  76,  -1,  0 }, // 104 PzlRousoku_Select
    { 4,  76,  -1,  0 }, // 105 PzlRousoku_Check
    { 5,  79,  -1,  0 }, // 106 Album_ZoomConfirm
    { 5,  80,  -1,  0 }, // 107 StartMenu_Difficulty
    { 5,  80, 118,  1 }, // 108 StartMenu_Costume
    { 5,  81,  -1,  0 }, // 109 StartMenu_OptionMain
    { 5,  81,  -1,  0 }, // 110 StartMenu_OptionSave
    { 5,  82,  -1,  0 }, // 111 StartMenu_GalleryMain
    { 5,  82,  -1,  0 }, // 112 StartMenu_GalleryView
    { 5,  82,  -1,  0 }, // 113 StartMenu_GalleryZero
    { 5,  83, 119,  1 }, // 114 StartMenu_MissionMain
    { 5,  83,  -1,  0 }, // 115 StartMenu_MissionSave
    { 5, 103,  -1,  0 }, // 116 PzlSizume_PlayClear
    { 5, 103,  -1,  0 }, // 117 PzlSizume_PlayWrong
    { 6, 108,  -1,  0 }, // 118 StartMenu_CostumeSel
    { 6, 114,  -1,  0 }, // 119 StartMenu_MissionSel
}};

// PC: FF3 address of every table slot, {init, one, end, pre, after}, 0 where
// FF3's slot is empty (tools/reverse/phases.py --c). Lets an unwritten phase
// report itself like a stub, so tus.py ranks its TU.
enum { GPHASE_INIT, GPHASE_ONE, GPHASE_END, GPHASE_PRE, GPHASE_AFTER, GPHASE_KIND_NUM };

static const unsigned int gphase_ff3_func[GPHASE_NUM][GPHASE_KIND_NUM] =
{
    { 0x001d2948, 0x001d2980, 0x001d29e0, 0x001d2950, 0x001d29c0 }, //   0
    { 0x00225b38, 0x00225d18, 0x00225cc8, 0x00000000, 0x00000000 }, //   1
    { 0x001ed590, 0x001ed5d8, 0x001ed5f8, 0x00000000, 0x00000000 }, //   2
    { 0x00100388, 0x001003a8, 0x00100390, 0x00100398, 0x001003a0 }, //   3
    { 0x00100758, 0x001008d8, 0x00100b38, 0x00000000, 0x00000000 }, //   4
    { 0x0025d388, 0x0025d820, 0x0025d6e8, 0x0025d860, 0x0025d8d8 }, //   5
    { 0x0025f528, 0x0025f820, 0x0025f6f8, 0x0025f828, 0x0025f830 }, //   6
    { 0x00224f78, 0x00225270, 0x002252c8, 0x00225208, 0x002252b0 }, //   7
    { 0x00123480, 0x00123ce0, 0x001239c8, 0x00124050, 0x00124080 }, //   8
    { 0x00120b18, 0x00120b50, 0x00120b30, 0x00120b90, 0x00120ba8 }, //   9
    { 0x001d7ab0, 0x001d7b70, 0x001d7b68, 0x00000000, 0x00000000 }, //  10
    { 0x001d06a0, 0x001d06b0, 0x001d06a8, 0x00000000, 0x00000000 }, //  11
    { 0x001d0780, 0x001d0930, 0x001d0be0, 0x00000000, 0x00000000 }, //  12
    { 0x001003e8, 0x001003f8, 0x001003f0, 0x00000000, 0x00000000 }, //  13
    { 0x001005b8, 0x00100600, 0x001005e0, 0x00000000, 0x00000000 }, //  14
    { 0x0025d8e0, 0x0025da20, 0x0025da18, 0x00000000, 0x00000000 }, //  15
    { 0x0025daf0, 0x0025dbe0, 0x0025dbd8, 0x00000000, 0x00000000 }, //  16
    { 0x0025dd50, 0x0025df10, 0x0025ded8, 0x0025e110, 0x0025e118 }, //  17
    { 0x0025f848, 0x0025f908, 0x0025f8e8, 0x00000000, 0x00000000 }, //  18
    { 0x00224150, 0x002242b0, 0x002244d0, 0x00000000, 0x00000000 }, //  19
    { 0x00223b80, 0x00223bd0, 0x00223dc0, 0x00000000, 0x00000000 }, //  20
    { 0x00222430, 0x00222480, 0x002225a0, 0x00000000, 0x00000000 }, //  21
    { 0x00222af0, 0x00222b40, 0x00222cb0, 0x00000000, 0x00000000 }, //  22
    { 0x00223280, 0x002232d8, 0x002234c8, 0x00000000, 0x00000000 }, //  23
    { 0x001240b8, 0x001240f0, 0x001240e8, 0x00000000, 0x00000000 }, //  24
    { 0x00124490, 0x00124580, 0x00124508, 0x00000000, 0x00000000 }, //  25
    { 0x00124778, 0x001248e8, 0x00124890, 0x001248e0, 0x00124ce0 }, //  26
    { 0x00128a68, 0x00128ab8, 0x00128aa8, 0x00000000, 0x00000000 }, //  27
    { 0x00128c28, 0x00128c78, 0x00128c68, 0x00000000, 0x00000000 }, //  28
    { 0x00120bc8, 0x00120c08, 0x00120be0, 0x00120bf8, 0x00120c00 }, //  29
    { 0x00120d18, 0x00120e30, 0x00120dd0, 0x00000000, 0x00000000 }, //  30
    { 0x001ae918, 0x001aea18, 0x001aed20, 0x00000000, 0x00000000 }, //  31
    { 0x00120f78, 0x00120fa0, 0x00120f98, 0x00000000, 0x00000000 }, //  32
    { 0x00228638, 0x00228690, 0x00228688, 0x00000000, 0x00000000 }, //  33
    { 0x001f33f0, 0x001f36e8, 0x001f3680, 0x00000000, 0x00000000 }, //  34
    { 0x00219e60, 0x00219f38, 0x00219ee8, 0x00000000, 0x00000000 }, //  35
    { 0x001a2e40, 0x001a2ee0, 0x001a2eb8, 0x00000000, 0x00000000 }, //  36
    { 0x001f1d08, 0x001f2038, 0x001f1f38, 0x001f2060, 0x001f2088 }, //  37
    { 0x001b3580, 0x001b3860, 0x001b3748, 0x001b3850, 0x001b3858 }, //  38
    { 0x002185a0, 0x002185d0, 0x002185c8, 0x00218668, 0x00218670 }, //  39
    { 0x001d7c68, 0x001d7f68, 0x001d7e88, 0x001d8178, 0x001d81a0 }, //  40
    { 0x00250878, 0x00250b28, 0x00250a50, 0x00250cb0, 0x00250cd8 }, //  41
    { 0x00258a68, 0x00258ad8, 0x00258aa0, 0x00000000, 0x00000000 }, //  42
    { 0x0014c7e0, 0x0014ca90, 0x0014c9b8, 0x0014ccb0, 0x0014ccc8 }, //  43
    { 0x00225e00, 0x00225ea8, 0x00225e68, 0x00000000, 0x00000000 }, //  44
    { 0x00226ff0, 0x00227080, 0x00227430, 0x00000000, 0x00000000 }, //  45
    { 0x0025b790, 0x0025b810, 0x0025ba68, 0x00000000, 0x00000000 }, //  46
    { 0x0014ab10, 0x0014ab78, 0x0014abf0, 0x00000000, 0x00000000 }, //  47
    { 0x0013d5a8, 0x0013d678, 0x0013d700, 0x00000000, 0x00000000 }, //  48
    { 0x00244228, 0x002444a8, 0x00244348, 0x00244648, 0x00244650 }, //  49
    { 0x001ae040, 0x001ae318, 0x001ae3d0, 0x001ae310, 0x001ae358 }, //  50
    { 0x0025e138, 0x0025e290, 0x0025e268, 0x00000000, 0x00000000 }, //  51
    { 0x00124d88, 0x00124ea0, 0x00124e58, 0x00124e98, 0x00124f80 }, //  52
    { 0x001267f8, 0x00126938, 0x00126900, 0x00126930, 0x00126b20 }, //  53
    { 0x00124f88, 0x00124ff8, 0x00124ff0, 0x00000000, 0x00000000 }, //  54
    { 0x001a82a0, 0x001a8958, 0x001a8778, 0x001a8998, 0x001a8a20 }, //  55
    { 0x001a7f20, 0x001a7f68, 0x001a80b8, 0x00000000, 0x00000000 }, //  56
    { 0x0020bd28, 0x0020c340, 0x0020c1a8, 0x00000000, 0x00000000 }, //  57
    { 0x001ff310, 0x001ff5b0, 0x001ff548, 0x00000000, 0x00000000 }, //  58
    { 0x001eadd0, 0x001eb310, 0x001eb108, 0x00000000, 0x00000000 }, //  59
    { 0x0022ec88, 0x0022f0b8, 0x0022ef30, 0x00000000, 0x00000000 }, //  60
    { 0x001b0930, 0x001b0d10, 0x001b0ba0, 0x001b0d58, 0x001b0d60 }, //  61
    { 0x00204810, 0x00204c00, 0x00204a80, 0x00204c48, 0x00204c50 }, //  62
    { 0x001d9860, 0x001d9bf0, 0x001d9d88, 0x001d9eb0, 0x001d9eb8 }, //  63
    { 0x00208238, 0x002082d0, 0x00208320, 0x00000000, 0x00000000 }, //  64
    { 0x001b5220, 0x001b55c8, 0x001b5538, 0x00000000, 0x00000000 }, //  65
    { 0x002574c8, 0x00257548, 0x00257540, 0x00257630, 0x00257638 }, //  66
    { 0x00218b18, 0x00218c88, 0x00218c30, 0x00218d50, 0x00218d58 }, //  67
    { 0x001f0e08, 0x001f11c8, 0x001f1008, 0x00000000, 0x00000000 }, //  68
    { 0x00250268, 0x00250608, 0x00250440, 0x00000000, 0x00000000 }, //  69
    { 0x0014c7c8, 0x0014c7d8, 0x0014c7d0, 0x00000000, 0x00000000 }, //  70
    { 0x002446c8, 0x002446d8, 0x002446d0, 0x00000000, 0x00000000 }, //  71
    { 0x00246108, 0x00246110, 0x00246198, 0x002461a0, 0x002461a8 }, //  72
    { 0x0024a808, 0x0024a840, 0x0024a8c8, 0x0024a8d0, 0x0024a8d8 }, //  73
    { 0x00248758, 0x00248760, 0x002487e8, 0x002487f0, 0x002487f8 }, //  74
    { 0x0024b5e0, 0x0024b6b8, 0x0024b740, 0x0024b860, 0x0024b868 }, //  75
    { 0x00244890, 0x00244928, 0x002449b0, 0x00244a80, 0x00244a88 }, //  76
    { 0x001ae598, 0x001ae700, 0x001ae7d0, 0x00000000, 0x00000000 }, //  77
    { 0x001250d0, 0x00125158, 0x00125120, 0x00000000, 0x00000000 }, //  78
    { 0x00126bd0, 0x00126cf8, 0x00126cb8, 0x00126cf0, 0x00126de0 }, //  79
    { 0x001af878, 0x001afa58, 0x001afc08, 0x001afa50, 0x001afc00 }, //  80
    { 0x001a5560, 0x001a5630, 0x001a5640, 0x001a5628, 0x001a5638 }, //  81
    { 0x001a70d0, 0x001a7128, 0x001a7170, 0x001a7120, 0x001a7168 }, //  82
    { 0x001a9e10, 0x001a9f10, 0x001a9f58, 0x001a9f08, 0x001a9f50 }, //  83
    { 0x001b24d8, 0x001b2528, 0x001b2500, 0x00000000, 0x00000000 }, //  84
    { 0x001b28a8, 0x001b2af8, 0x001b2a30, 0x00000000, 0x00000000 }, //  85
    { 0x00207490, 0x002076d0, 0x002075c8, 0x00000000, 0x00000000 }, //  86
    { 0x00207290, 0x002072a0, 0x00207298, 0x00000000, 0x00000000 }, //  87
    { 0x001d9f38, 0x001da1b0, 0x001da220, 0x00000000, 0x00000000 }, //  88
    { 0x00257268, 0x002572e8, 0x002572c0, 0x00000000, 0x00000000 }, //  89
    { 0x002188f8, 0x00218958, 0x00218930, 0x00000000, 0x00000000 }, //  90
    { 0x002461c0, 0x002461e0, 0x00246440, 0x00000000, 0x00000000 }, //  91
    { 0x00246448, 0x00246450, 0x002465e8, 0x00000000, 0x00000000 }, //  92
    { 0x00246608, 0x002466d8, 0x00246868, 0x00000000, 0x00000000 }, //  93
    { 0x00246870, 0x002468a8, 0x00246928, 0x00000000, 0x00000000 }, //  94
    { 0x0024a8f0, 0x0024a8f8, 0x0024aa60, 0x00000000, 0x00000000 }, //  95
    { 0x0024aa68, 0x0024ab48, 0x0024aca0, 0x00000000, 0x00000000 }, //  96
    { 0x00248810, 0x00248818, 0x00248a30, 0x00000000, 0x00000000 }, //  97
    { 0x00248a38, 0x00248d40, 0x00248e98, 0x00000000, 0x00000000 }, //  98
    { 0x00248ea0, 0x00248ed8, 0x00248f58, 0x00000000, 0x00000000 }, //  99
    { 0x0024b8c8, 0x0024b8e8, 0x0024bb98, 0x00000000, 0x00000000 }, // 100
    { 0x0024bba0, 0x0024bc70, 0x0024bdd0, 0x00000000, 0x00000000 }, // 101
    { 0x0024bdd8, 0x0024bde0, 0x0024be30, 0x00000000, 0x00000000 }, // 102
    { 0x0024be38, 0x0024be40, 0x0024beb0, 0x0024bef0, 0x0024bef8 }, // 103
    { 0x00244aa0, 0x00244aa8, 0x00244c80, 0x00000000, 0x00000000 }, // 104
    { 0x00244c88, 0x00244c90, 0x00244e10, 0x00000000, 0x00000000 }, // 105
    { 0x00126de8, 0x00126eb8, 0x00126e90, 0x00000000, 0x00000000 }, // 106
    { 0x001afd38, 0x001afe00, 0x001aff40, 0x00000000, 0x00000000 }, // 107
    { 0x001b0008, 0x001b00d8, 0x001b01b0, 0x001b00d0, 0x001b01a8 }, // 108
    { 0x001a5678, 0x001a57a0, 0x001a5b70, 0x00000000, 0x00000000 }, // 109
    { 0x001a5c50, 0x001a5c58, 0x001a5d58, 0x00000000, 0x00000000 }, // 110
    { 0x001a71a0, 0x001a7368, 0x001a7538, 0x00000000, 0x00000000 }, // 111
    { 0x001a76b0, 0x001a7890, 0x001a7930, 0x00000000, 0x00000000 }, // 112
    { 0x001a7a40, 0x001a7d10, 0x001a7d78, 0x00000000, 0x00000000 }, // 113
    { 0x001a9f80, 0x001aa158, 0x001aa388, 0x001aa150, 0x001aa380 }, // 114
    { 0x001aa780, 0x001aa788, 0x001aa8f0, 0x00000000, 0x00000000 }, // 115
    { 0x0024bf00, 0x0024bf48, 0x0024c0a0, 0x00000000, 0x00000000 }, // 116
    { 0x0024c0a8, 0x0024c0f0, 0x0024c198, 0x00000000, 0x00000000 }, // 117
    { 0x001b04b0, 0x001b0508, 0x001b05d8, 0x00000000, 0x00000000 }, // 118
    { 0x001aa4b0, 0x001aa5b0, 0x001aa680, 0x00000000, 0x00000000 }, // 119
};

// PC: an empty slot whose FF3 function exists. Reported once per slot in the
// stub format ("[stub] <name> (FF3 0x...) reached") so the smoke run ranks the
// phase's TU; returns what lets the engine go on: init/end 1 (done, so the
// node moves on), one 0 (stay in the phase; FF3's one functions request every
// change themselves).
static int GPhaseCallMissing(int kind, short id)
{
    static const char *const kind_name[GPHASE_KIND_NUM] = { "init", "one", "end", "pre", "after" };
    static unsigned char     seen[GPHASE_NUM][GPHASE_KIND_NUM];

    if (gphase_ff3_func[id][kind] != 0 && !seen[id][kind])
    {
        seen[id][kind] = 1;
        printf("[stub] phase%d_%s (FF3 0x%08x) reached\n", id, kind_name[kind], gphase_ff3_func[id][kind]);
    }
    return kind != GPHASE_ONE;
}

// ──────────────────────────────────────────────────────────────────────
// Dispatch to one phase's functions
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001d3b40.

static int GPhaseCallInit(short id)
{
    if (ini_func[id] == nullptr)
    {
        return GPhaseCallMissing(GPHASE_INIT, id);      // PC: unwritten phase
    }
    const int result = ini_func[id]();
    static const bool trace = getenv("REIPAN_TRACE_PHASES") != nullptr;
    if (result != 0 && trace)
        printf("[phase] entered %d from %d\n", id, GetGPhaseCtrl()->from);
    return result;
}

// ──────────────────────────────────────────────────────────────────────
// Run the phase's one; non-zero means it requested a change (SetNext), which
// is applied at once.
// FF3 0x001d3b78.

static int GPhaseCallOne(short id)
{
    int ret;

    if (one_func[id] == nullptr)
    {
        return GPhaseCallMissing(GPHASE_ONE, id);       // PC: unwritten phase
    }

    ret = one_func[id]();
    if (ret != 0)
    {
        GetGPhaseCtrl()->Change();
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d3be8.

static int GPhaseCallEnd(short id)
{
    if (end_func[id] == nullptr)
    {
        return GPhaseCallMissing(GPHASE_END, id);       // PC: unwritten phase
    }
    return end_func[id]();
}

// ──────────────────────────────────────────────────────────────────────
// Load wait between init and run: always done in FF3.
// FF3 0x001d3c20.

static int GPhaseCallLoadWait(short id)
{
    (void)id;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Wait before end: always done in FF3.
// FF3 0x001d3c28.

static int GPhaseCallEndWait(short id)
{
    (void)id;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d3c30.

static void GPhaseCallPre(short id)
{
    if (pre_func[id] != nullptr)
    {
        pre_func[id]();
    }
    else
    {
        GPhaseCallMissing(GPHASE_PRE, id);              // PC: reports unwritten ones only
    }
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d3c70.

static void GPhaseCallAfter(short id)
{
    if (after_func[id] != nullptr)
    {
        after_func[id]();
    }
    else
    {
        GPhaseCallMissing(GPHASE_AFTER, id);            // PC: reports unwritten ones only
    }
}

// ──────────────────────────────────────────────────────────────────────
// Nodes
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001d3cd8.

GPHASE_NODE::GPHASE_NODE(GPHASE_NODE *parent, short id)
{
    this->parent = parent;
    state_wrk    = 0;
    this->id     = id;
    child        = nullptr;
    state        = GPHASE_STATE_INIT;
    state_cnt    = 0;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d3d00.

void GPHASE_NODE::SetState(short state)
{
    state_wrk   = 0;
    state_cnt   = 0;
    this->state = state;
}

// ──────────────────────────────────────────────────────────────────────
// One frame of a node and, recursively, of the nodes below it: init -> load
// wait -> run (pre, the child or the phase's one, after) -> end wait -> end.
// A running node ends once its phase is off the current phase's root path.
// FF3 0x001d3d18.

void GPHASE_NODE::Exec()
{
    GPHASE_CTRL *ctrl;
    int          check_end;

    if (state == GPHASE_STATE_INIT)
    {
        if (GPhaseCallInit(id) != 0)
        {
            SetState(GPHASE_STATE_LOAD);
        }
    }

    if (state == GPHASE_STATE_LOAD)
    {
        if (GPhaseCallLoadWait(id) != 0)
        {
            SetState(GPHASE_STATE_RUN);
            ctrl = GetGPhaseCtrl();
            if (ctrl->now == id)
            {
                ctrl->SetNext(-1, 0);         // the requested phase is up
            }
        }
    }

    GPhaseCallPre(id);

    if (state == GPHASE_STATE_RUN)
    {
        if (child == nullptr)
        {
            // Deepest node: while the current phase is below it, wait for
            // GPhaseCtrlBuildStack to push it; otherwise run the phase.
            check_end = !GetGPhaseTree()->IsAncestor(id, GetGPhaseCtrl()->now)
                     && GPhaseCallOne(id) != 0;
        }
        else
        {
            child->Exec();
            check_end = child->state == GPHASE_STATE_DONE;
        }

        if (check_end && GetGPhaseTree()->IsOffPath(id, GetGPhaseCtrl()->now))
        {
            SetState(GPHASE_STATE_END_WAIT);
        }
    }

    GPhaseCallAfter(id);

    if (state == GPHASE_STATE_END_WAIT && GPhaseCallEndWait(id) != 0)
    {
        SetState(GPHASE_STATE_END);
        GetGPhaseCtrl()->SetNext(-1, 0);
    }

    if (state == GPHASE_STATE_END && GPhaseCallEnd(id) != 0)
    {
        SetState(GPHASE_STATE_DONE);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Controller
// ──────────────────────────────────────────────────────────────────────

// FF3 0x001d3fa0; GPHASE_CTRL's constructor.

void GPHASE_CTRL::Init()
{
    root      = nullptr;
    next      = -1;
    from      = -1;
    now       = -1;
    next_link = 0;
    leaf      = nullptr;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x001d3fc0. The node stack is not freed: GameSystemEnd rebuilds the
// C++ heap right after (on PC the nodes of a session leak at soft reset).

void GPHASE_CTRL::Term()
{
    Init();
}

// ──────────────────────────────────────────────────────────────────────
// Request phase id (-1: clear the request); link: on change, hang id under
// the requesting phase in the tree.
// FF3 0x001d3fd8.

void GPHASE_CTRL::SetNext(int id, int link)
{
    next_link = (short)link;
    next      = (short)id;
}

// ──────────────────────────────────────────────────────────────────────
// Make the requested phase current; the next GPhaseCtrlMain ends the nodes
// off its path and pushes the ones on it.
// FF3 0x001d3ff0.

void GPHASE_CTRL::Change()
{
    now = next;
    if (next != -1)
    {
        if (leaf == nullptr)
        {
            from = -1;
        }
        else
        {
            from = leaf->id;
            if (next_link != 0)
            {
                GetGPhaseTree()->Link(now, from);
            }
        }
    }
}


// ──────────────────────────────────────────────────────────────────────
// One frame of the phase engine (GameSystemFrame).
// FF3 0x001d4070.

void GPHASE_CTRL::Main()
{
    BuildStack();
    root->Exec();
    FreeEndedNodes();
}

// ──────────────────────────────────────────────────────────────────────
// Push nodes from the leaf toward the current phase, one per tree level,
// while the leaf is running and an ancestor of it (from the root on the
// first frame).
// FF3 0x001d40a0 (operator_new(0x14) is the new expression).
// callcheck: operator_new, GPhaseNodeInit (new GPHASE_NODE and its constructor)

void GPHASE_CTRL::BuildStack()
{
    GPHASE_TREE *tree;
    GPHASE_NODE *leaf;
    GPHASE_NODE *parent;
    short        now;
    short        id;
    short        up;

    tree = GetGPhaseTree();
    now  = this->now;
    leaf = this->leaf;
    if (now == -1)
    {
        return;
    }

    for (;;)
    {
        if (leaf != nullptr)
        {
            if (!tree->IsAncestor(leaf->id, now))
            {
                return;
            }
            if (leaf->state != GPHASE_STATE_RUN)
            {
                return;
            }
        }

        // The child of the leaf's phase on the path to now.
        parent = this->leaf;
        id     = now;
        if (leaf != nullptr)
        {
            up = now;
            do
            {
                id = up;
                up = tree->GetParent(id);
            } while (up != leaf->id);
        }

        leaf = new GPHASE_NODE(parent, id);
        this->leaf = leaf;
        if (parent == nullptr)
        {
            break;
        }
        leaf->parent  = parent;
        parent->child = leaf;
    }
    root = leaf;
}

// ──────────────────────────────────────────────────────────────────────
// Pop and delete ended nodes from the leaf up.
// FF3 0x001d41b0 (FF2-matched as GetGameDataSaveDataMem, wrongly; operator_delete
// is the delete expression). Like FF3,
// assumes the root (super) never ends.
// callcheck: operator_delete (delete leaf)

void GPHASE_CTRL::FreeEndedNodes()
{
    GPHASE_NODE *leaf;
    GPHASE_NODE *parent;

    for (;;)
    {
        leaf   = this->leaf;
        parent = leaf->parent;
        if (leaf->state != GPHASE_STATE_DONE)
        {
            break;
        }
        delete leaf;
        this->leaf    = parent;
        parent->child = nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Singletons (FF3 emits these getters with the template instances at 0x2ab000+)
// ──────────────────────────────────────────────────────────────────────

// FF3 0x002ab3e0: built on first use (GPHASE_CTRL's constructor runs Init);
// the guard and atexit come from the compiler.
// callcheck: GPhaseCtrlInit, atexit (the static's constructor and destructor, implicit)

GPHASE_CTRL *GetGPhaseCtrl(void)
{
    static GPHASE_CTRL gphase_ctrl;         // bss 0x41e468

    return &gphase_ctrl;
}

// ──────────────────────────────────────────────────────────────────────
// FF3 0x002abe40: zeroed on first use; GameSystemStart fills it.
// callcheck: atexit (the static's destructor, registered by the compiler)

GPHASE_TREE *GetGPhaseTree(void)
{
    static GPHASE_TREE gphase_tree;         // sbss gp+0x6a0

    return &gphase_tree;
}
