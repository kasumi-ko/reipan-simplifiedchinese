/* ==========================================================================
 *  ingame/menu/start_menu.h
 *
 *  FF3's start menu (phase 55 StartMenu_Main and its Option / Gallery /
 *  Mission phases, phase 56 Story_Opening), FF3 0x1a4be0..0x1aa9a8
 *  (FF3-only). The
 *  menu opens from Story_Start once a game has been cleared, and keeps two
 *  save works (Ghidra still calls them "Finder*", an early guess):
 *
 *    - FINDER_STATE_WRK 0x41e490 (save block 19): clear counts per
 *      difficulty; GAME_CLEAR_ adds one, the ABOVE_CLEAR_NUM /
 *      FOLLOW_CLEAR_NUM event conditions read the sum.
 *    - FINDER_SLOT_WRK  0x424b10 (save block 20): 33 unlock slots the clears
 *      open (costumes, lenses, extras), read by the Option menu.
 *
 *  The range holds four FF3 source files (a GPhaseCtrl atexit thunk ends
 *  each: 0x1a5d68, 0x1a80e0, 0x1a8a38, 0x1aa910): the bonus shop
 *  (StartMenu_Option 81 / OptionMain 109 / OptionSave 110), the save works
 *  with the gallery (StartMenu_Gallery 82 / GalleryMain 111 / GalleryView
 *  112, GalleryZero 113) and Story_Opening 56, StartMenu_Main 55, and the
 *  extra-mission menu (StartMenu_Mission 83 / MissionMain 114 / MissionSel
 *  119 / MissionSave 115; Ghidra called 83 "Album" until 2026-10-15). They
 *  stay in this one file until the TU is split (open-items).
 *
 *  Slot values (FINDER_SLOT_WRK): 0 locked, 1 for sale, 2 bought.
 * ======================================================================== */
#ifndef _INGAME_MENU_START_MENU_H
#define _INGAME_MENU_START_MENU_H

#include <stddef.h>                 // offsetof
#include "sdk/scetypes.h"

// Clear counts (0x41e490), cleared by Init and saved whole. A C++ object in
// FF3: the static's constructor runs Init (also called by the in-game init).
struct FINDER_STATE_WRK             /* 0x10 */
{
    FINDER_STATE_WRK();             // runs Init

    void Init();                            // FF3 0x001a5df8 (FinderStateWrkInit)
    void AddClear(int difficulty);          // FF3 0x001a5e30 (FinderStateAddClear): cnt[difficulty] + 1, 0..99
    int  FUN_001a5e60();                    // FF3 0x001a5e60: cnt[0..3] summed
    void SlotOpenFirstClear();              // FF3 0x001a5e90 (FinderSlotOpenFirstClear): what any clear unlocks (the object unused)
    void Clear(int difficulty);             // FF3 0x001a5f60 (FinderStateClear): GAME_CLEAR_ (count + unlocks)

    int ClearSaveFlag() const { return _0c & 0xff; }
    void SetClearSaveFlag(int value) { _0c = (_0c & ~0xff) | (value & 0xff); }
#ifdef REIPAN_SLPS_25544_CHS
    // The Japanese shop has one extra unlock. Keep the PC save blocks at their original size.
    int PromoMovieState() const { return (_0c >> 8) & 3; }
    void SetPromoMovieState(int value) { _0c = (_0c & ~0x300) | ((value & 3) << 8); }
#endif

    /* 0x00 */ s_char cnt[4];       // clears per difficulty, 0..99 (AddClear); FUN_001a5e60 sums them
    /* 0x04 */ int    _04;          // GAME_CLEAR_ 0 sets it; opens gallery entry 2 (StartMenuGalleryIsOpen)
    /* 0x08 */ int    _08;          // GAME_CLEAR_ non-zero sets it; opens gallery entry 3
    /* 0x0c */ int    _0c;          // copied into the save slot head (finder_flg); GAME_CLEAR_ sets 1
};
static_assert(sizeof(FINDER_STATE_WRK) == 0x10, "FINDER_STATE_WRK is 0x10 bytes");
static_assert(offsetof(FINDER_STATE_WRK, _04) == 0x04, "FINDER_STATE_WRK._04");
static_assert(offsetof(FINDER_STATE_WRK, _08) == 0x08, "FINDER_STATE_WRK._08");
static_assert(offsetof(FINDER_STATE_WRK, _0c) == 0x0c, "FINDER_STATE_WRK._0c");

#define FINDER_SLOT_MAX 33

// Unlock slots (0x424b10): one int each, Set raises them. A C++ object in
// FF3: the static's constructor runs Init (also called by the in-game init).
struct FINDER_SLOT_WRK              /* 0x84 */
{
    FINDER_SLOT_WRK();              // runs Init

    void Init();                                    // FF3 0x001a62e0 (FinderSlotWrkInit): every slot 0
    int  Count(int group, int value);               // FF3 0x001a6330 (FinderSlotCount): slots of a group (table 0x358540) equal to value (the object unused)
    void SetForce(int no, int value);               // FF3 0x001a63d0 (FinderSlotSetForce)
    void Set(int no, int value);                    // FF3 0x001a63e8 (FinderSlotSet): only raises
    void SetSub(int no, int value, int force);      // FF3 0x001a6400 (FinderSlotSetSub)

    /* 0x00 */ int slot[FINDER_SLOT_MAX];
};
static_assert(sizeof(FINDER_SLOT_WRK) == 0x84, "FINDER_SLOT_WRK is 0x84 bytes");
static_assert(offsetof(FINDER_SLOT_WRK, slot) == 0x00, "FINDER_SLOT_WRK.slot");

FINDER_STATE_WRK *GetFinderStateWrk(void);                  // FF3 0x002ab688
int   FinderStateSaveGet(void **addr);                      // FF3 0x001a5da0: {GetFinderStateWrk(), 0x10}
int   FinderStateHasAny(void);                              // FF3 0x001a8268: GetFinderStateWrk()->FUN_001a5e60() > 0

FINDER_SLOT_WRK  *GetFinderSlotWrk(void);                   // FF3 0x002b90b8
int   FinderSlotSaveGet(void **addr);                       // FF3 0x001a6288: {GetFinderSlotWrk(), 0x84}
void  FinderSlotOpenByCount(void);                          // FF3 0x001a60f8
void  FinderSlotOpenByMission(void);                        // FF3 0x001a61e0

struct FINDER_BANK_ENTRY;            // ingame/mission/mission_list.h
void  MissionItemReset(FINDER_BANK_ENTRY *entry);   // FF3 0x001a9c10: ITEM_RESET in start mode 2 (the mission's items)

// ---- the start menu ----

// Layout files (CD_FILE_DAT; + GetLanguage).
#define START_MENU_SHOP_FILE    0x1274  // bonus shop (phases 81 / 109)
#define START_MENU_GALLERY_FILE 0x1298  // gallery list (phase 111)
#define START_MENU_VIEW_FILE    0x129d  // gallery picture viewer (phase 112)
#define START_MENU_FADE_FILE    0x12a2  // GALLARY_ZERO1_00_PHF: phase 113's picture of gallery entry 4 (the hooked one)
#define START_MENU_ZERO2_FILE   0x12a7  // GALLARY_ZERO2_00_PHF: phase 113's picture of entry 5

// Menu cursors (MenuCsr ids).
#define START_MENU_CSR_TOP      0x50    // StartMenu_Top's cursor (the gallery's cancel puts it on 5)
#define START_MENU_CSR_SHOP     0x6d    // 33 bonus entries, six rows on screen
#define START_MENU_CSR_GALLERY  0x6f    // six gallery entries
#define START_MENU_CSR_DIFF     0x6b    // difficulties (StartMenu_Difficulty)
#define START_MENU_CSR_MISSION_SUB 0x77 // the mission list's two-entry sub menu (phase 119)

// The start menu's message window (sdata 0x370310, made by init_StartMenu_Main).
class CWindowMsg;
extern CWindowMsg *start_menu_sel_win;

// Phase 81 StartMenu_Option: the bonus shop (FF3 0x1a5560..)
int  init_StartMenu_Option(void);           // FF3 0x001a5560
void pre_StartMenu_Option(void);            // FF3 0x001a5628
int  one_StartMenu_Option(void);            // FF3 0x001a5630
void after_StartMenu_Option(void);          // FF3 0x001a5638
int  end_StartMenu_Option(void);            // FF3 0x001a5640
// Phase 109 StartMenu_OptionMain: the list, buying.
int  init_StartMenu_OptionMain(void);       // FF3 0x001a5678
int  one_StartMenu_OptionMain(void);        // FF3 0x001a57a0
int  end_StartMenu_OptionMain(void);        // FF3 0x001a5b70
// Phase 110 StartMenu_OptionSave: the save offered when leaving after a purchase.
int  init_StartMenu_OptionSave(void);       // FF3 0x001a5c50
int  one_StartMenu_OptionSave(void);        // FF3 0x001a5c58
int  end_StartMenu_OptionSave(void);        // FF3 0x001a5d58

void StartMenuOptionSetHooks(void);         // FF3 0x001a51f0 (init_StartMenu_Main)
void StartMenuOptionApplyBought(void);      // FF3 0x001a5308

// Phase 82 StartMenu_Gallery and its children 111 (list), 112 (picture viewer).
int  init_StartMenu_Gallery(void);          // FF3 0x001a70d0
void pre_StartMenu_Gallery(void);           // FF3 0x001a7120
int  one_StartMenu_Gallery(void);           // FF3 0x001a7128
void after_StartMenu_Gallery(void);         // FF3 0x001a7168
int  end_StartMenu_Gallery(void);           // FF3 0x001a7170
int  init_StartMenu_GalleryMain(void);      // FF3 0x001a71a0
int  one_StartMenu_GalleryMain(void);       // FF3 0x001a7368
int  end_StartMenu_GalleryMain(void);       // FF3 0x001a7538
int  init_StartMenu_GalleryView(void);      // FF3 0x001a76b0
int  one_StartMenu_GalleryView(void);       // FF3 0x001a7890
int  end_StartMenu_GalleryView(void);       // FF3 0x001a7930

int  StartMenuGalleryIsOpen(int no);        // FF3 0x001a65a8
void StartMenuGallerySetHooks(void);        // FF3 0x001a6ed0 (init_StartMenu_Main)
void StartMenuGalleryFadeSetHooks(void);    // FF3 0x001a6e40 (phase 113)

// Phase 113 StartMenu_GalleryZero: gallery entries 4 / 5, a full-screen
// GALLARY_ZERO layout.
int  init_StartMenu_GalleryZero(void);      // FF3 0x001a7a40
int  one_StartMenu_GalleryZero(void);       // FF3 0x001a7d10
int  end_StartMenu_GalleryZero(void);       // FF3 0x001a7d78

// Phase 56 Story_Opening: gallery entries 2 / 3, a movie through phase 67.
int  init_Story_Opening(void);              // FF3 0x001a7f20
int  one_Story_Opening(void);               // FF3 0x001a7f68
int  end_Story_Opening(void);               // FF3 0x001a80b8

// Phase 55 StartMenu_Main: loads the menu's layouts, owns the cursors and
// the message window.
int  init_StartMenu_Main(void);             // FF3 0x001a82a0
int  one_StartMenu_Main(void);              // FF3 0x001a8958
int  end_StartMenu_Main(void);              // FF3 0x001a8778
void pre_StartMenu_Main(void);              // FF3 0x001a8998
void after_StartMenu_Main(void);            // FF3 0x001a8a20

// Phase 83 StartMenu_Mission (the extra missions) and its children 114
// (list), 119 (sub menu under 114), 115 (save).
int  init_StartMenu_Mission(void);          // FF3 0x001a9e10
void pre_StartMenu_Mission(void);           // FF3 0x001a9f08
int  one_StartMenu_Mission(void);           // FF3 0x001a9f10
void after_StartMenu_Mission(void);         // FF3 0x001a9f50
int  end_StartMenu_Mission(void);           // FF3 0x001a9f58
int  init_StartMenu_MissionMain(void);      // FF3 0x001a9f80
void pre_StartMenu_MissionMain(void);       // FF3 0x001aa150
int  one_StartMenu_MissionMain(void);       // FF3 0x001aa158
void after_StartMenu_MissionMain(void);     // FF3 0x001aa380
int  end_StartMenu_MissionMain(void);       // FF3 0x001aa388
int  init_StartMenu_MissionSel(void);       // FF3 0x001aa4b0
int  one_StartMenu_MissionSel(void);        // FF3 0x001aa5b0
int  end_StartMenu_MissionSel(void);        // FF3 0x001aa680
int  init_StartMenu_MissionSave(void);      // FF3 0x001aa780
int  one_StartMenu_MissionSave(void);       // FF3 0x001aa788
int  end_StartMenu_MissionSave(void);       // FF3 0x001aa8f0

// hms {hours, minutes, seconds} as two-digit numbers with message (0x69, 0xb)
// between them from pos; hours only with with_hours.
void StartMenuDrawTime(const int *hms, const float *pos, int alpha, int with_hours);  // FF3 0x001a9c90
void StartMenuMissionDrawValue(int type, int value, const float *pos, int alpha);    // FF3 0x001a9a40: a value in its mission type's unit; -1 draws nothing

struct FINDER_MODE_WRK;
void FinderModeWrkStart(FINDER_MODE_WRK *wrk, int group, int no);  // FF3 0x001aa940

#endif /* _INGAME_MENU_START_MENU_H */
