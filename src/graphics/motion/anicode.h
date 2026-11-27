/* ==========================================================================
 *  graphics/motion/anicode.h
 *
 *  FF3 0x14cd30..0x1506d8, in FF2 anicode.c's slot of the link order (the
 *  file name is kept from there; none of it is FF2's ANI_CODE reader). FF3
 *  holds here:
 *    0x14cd30  a byte stack of pad_wrk[0].stick_dpad (layout animations
 *              push 1 while they run)
 *    0x14cd98  pad and layout-label helpers, item hook helpers
 *    0x14d228  the message viewer (layout types 0/1): a paged message with
 *              pictures beside it (CTutorial, MENU_PIC), work at data
 *              0x2f62e8
 *    0x14e470  the item-get window (layout type 2, ITEMGET_PHF)
 *    0x14e8b0  the Now Loading screens (CcmLoading, CcmLoadingNormal,
 *              CcmLoadingMission)
 *    0x14eef0  the yes/no dialog (layout types 6, 10, 11)
 *    0x14f348  the menu window (layout type 8)
 *    0x14f860  the pause menu (layout type 4)
 *    0x14ff58  the slot select (layout type 7)
 *  Its work areas sit in bss 0x391870..0x391984; 0x1502a0.. is anim_2d.c's
 *  HUD code.
 * ======================================================================== */
#ifndef _GRAPHICS_MOTION_ANICODE_H
#define _GRAPHICS_MOTION_ANICODE_H

#include <stddef.h>                     // offsetof

#include "sdk/scetypes.h"
#include "system/b2d/B2dCtl.h"          // B2D_ITEM_HOOK_FUNC, B2D_HOOK_ARGS
#include "ingame/story_menu_photo.h"    // MSG_PIC_KEY_DAT, msg_pic_key_dat

// ---- pad_wrk[0].stick_dpad stack (bss 0x391870, depth gp-0x5a88) ----
void PadStickDpadStackReset(void);              // Anim2dInit
void PadStickDpadPush(int value);               // LayoutAnimStart pushes 1
void PadStickDpadPop(void);                     // LayoutAnimReset

// 1 OK, 2 cancel, 3 left, 4 right, 5 up, 6 down (pad 0 triggers), else 0.
int PadTrgGetMenuInput(void);

int LabelAlignGetX(int type, const int *rect);  // 0/1: rect[0], 2: centre of rect[0], rect[6]; else -1
int GetLabelSuffixType(const char *label);      // alignment type from the label's suffix

void SetCurColorPacked(u_int rgba);             // SetCurColorRGB(r, g, b) of the low three bytes
void SetDefaultGsDrawState(void);               // after a hook's own drawing
// MsgPrintData(GetMsgDataAddr(msg0, msg1), x, y, 0x2d, a4, a5, a6, a7, a8).
void MsgPrintAt(int msg0, int msg1, int x, int y, int a4, int a5, int a6, int a7, int a8);

// One record of an item hook list (B2dSetHookList; EE 0x10 bytes, ROM lists
// at 0x350b00 / 0x350c20 / 0x3511d8 / 0x351238): the item by name, else by
// id; kind 0 sets the item draw hook (+0xc), other kinds the hook at +0x10.
// The list ends with a record whose func is nullptr.
// layout: host -- func and name are pointers.
struct B2D_HOOK_REC
{
    /* 0x00 */ B2D_ITEM_HOOK_FUNC func;
    /* 0x04 */ int                kind;
    /* 0x08 */ int                id;
    /* 0x0c */ const char        *name;     // nullptr: find by id
};
static_assert(offsetof(B2D_HOOK_REC, func) == 0x00, "B2D_HOOK_REC.func");

void B2dSetHookList(int handle, const B2D_HOOK_REC *recs);
int  B2dDrawItemDefault(const B2D_HOOK_ARGS *args);   // B2dGetSpriteFunc()(args)
int  B2dDrawQuadDefault(const B2D_HOOK_ARGS *args);   // B2dGetQuadFunc()(args)

// ---- the message viewer (layout types 0 and 1) ----
// Callers set the message, the mode and the pictures, then start layout
// type 0 or 1. The message (msg0, msg1) is the title, (msg0, msg1 + 2) the
// paged body; the pictures are MENU_PIC picture numbers the player
// pages through in the picture state.
enum MSG_VIEW_MODE
{
    MSG_VIEW_MODE_0 = 0,                // left/right page, cancel closes; OK on the last page buzzes
    MSG_VIEW_MODE_1 = 1,                // no paging arrows; OK on the last page closes, so does
                                        // any frame the page is not waiting; cancel buzzes
    MSG_VIEW_MODE_2 = 2,                // as 0, OK on the last page closes too; the fade layer
                                        // label is shown at once and left alone
};

void MsgViewSetMode(int mode);                  // MSG_VIEW_MODE_*
void MsgViewSetMsg(int msg0, int msg1);         // also MsgPageReset
#define MSG_VIEW_PIC_MAX 8
void MsgViewSetPics(const int *pics, int n);    // copies n picture IDs; nullptr clears the list
void MsgViewBackToText(int handle);             // FF3 0x0014deb8: from the pictures back to the text

int  MsgViewOnOpen(int handle);                 // type 0 on_open
int  FUN_0014e2f8(int handle);                  // type 1 on_open (no Ghidra function)
int  FUN_0014e040(int handle);                  // types 0/1 on_update (no Ghidra function)
int  MsgViewOnClose(int handle);                // types 0/1 on_close

// ---- the item-get window (layout type 2, ITEMGET_PHF) ----
// Callers set the picture, the message and the flags, then start type 2;
// ItemGetReqClose(1) plays "out".
#define ITEM_GET_NO_MSG     0x1         // no name text; item 0x40d hidden, 0x40e moved up 0x1e
#define ITEM_GET_NO_3EC     0x2         // item 0x3ec hidden
#define ITEM_GET_NO_PIC     0x4         // no picture

void ItemGetSetPic(int pic);                    // MENU_PIC picture number
void ItemGetSetMsg(int msg0, int msg1);         // msg0 < 0 or msg1 < 0: no text
void ItemGetReqClose(int req);
void ItemGetSetFlags(u_int flags);              // ITEM_GET_*

int ItemGetOnUpdate(int handle);                // type 2 on_update; 1 when closing
int FUN_0014e710(int handle);                   // type 2 on_load_req (no Ghidra function)
int ItemGetOnOpen(int handle);                  // type 2 on_open
int ItemGetOnClose(int handle);                 // type 2 on_close

// ---- the Now Loading screens ----
// init_Story_NowLoading news one (CcmLoadingMission for MISSION_LOAD_PHF,
// else CcmLoadingNormal for LOADING_PHF; the file numbers + GetLanguage()).
// The ctor requests the layout; Main() draws a rect in the background colour
// under the layout and steps `state`: 0 waiting for the data (then
// OnReady), 1 showing, 2 leaving (after End()), 3 done. The dtor releases
// the layout. EE vtable slots in the comments (vptr at +0).
// layout: host -- the vptr is 4 bytes on the EE, 8 here.
class CcmLoading
{
public:
    explicit CcmLoading(int file);                          // FF3 0x14e8b0 (also 0x14e908)
    virtual void DrawBg(u_char r, u_char g, u_char b, u_char a);   // +0x00, FF3 0x14ea10
    virtual void OnReady(void) {}                           // +0x04, FF3 0x2adce0
    virtual void StepShow(void) {}                          // +0x08, FF3 0x2adce8
    virtual void StepLeave(void) {}                         // +0x0c, FF3 0x2adcf0
    virtual void StepDone(void) {}                          // +0x10, FF3 0x2adcf8
    virtual ~CcmLoading();                                  // +0x14 / +0x18, FF3 0x14e960 / 0x14e9d0
    virtual void Main(void);                                // +0x1c, FF3 0x14ea78
    virtual void End(void) {}                               // +0x20, FF3 0x2add00
    virtual void SetBgColor(u_char r, u_char g, u_char b, u_char a)  // +0x24, FF3 0x2add08
    {
        bg_r = r;
        bg_g = g;
        bg_b = b;
        bg_a = a;
    }

    /* 0x04 */ int    file;                 // CD_FILE_DAT number of the layout
    /* 0x08 */ u_char bg_r, bg_g, bg_b, bg_a;   // 0, 0, 0, 0x80
    /* 0x0c */ int    state;                // 0..3, see above

protected:
    int IsReady(void);                                      // FF3 0x14ea58
};

// Fades the layout in on "NewLabel_0", out after End(). EE 0x14 bytes.
// layout: host -- see CcmLoading.
class CcmLoadingNormal : public CcmLoading
{
public:
    explicit CcmLoadingNormal(int file);                    // FF3 0x14eb90 (also 0x14eb58)
    void OnReady(void) override;                            // FF3 0x14ec50
    void StepShow(void) override;                           // FF3 0x14ec98
    void StepLeave(void) override;                          // FF3 0x14ece8
    ~CcmLoadingNormal() override;                           // FF3 0x14ebc8 / 0x14ec18
    void End(void) override { state = 2; }                  // FF3 0x2add30

    /* 0x10 */ int alpha;                   // layout alpha, 0..0x80 in steps of 8
};

// Plays "in" with the mission layout's hooks, "out" after End(). EE 0x10 bytes.
// layout: host -- see CcmLoading.
class CcmLoadingMission : public CcmLoading
{
public:
    explicit CcmLoadingMission(int file);                   // FF3 0x14ed68 (also 0x14ed38)
    void OnReady(void) override;                            // FF3 0x14ee20
    void StepShow(void) override;                           // FF3 0x14ee50
    void StepLeave(void) override;                          // FF3 0x14ee68
    ~CcmLoadingMission() override;                          // FF3 0x14ed98 / 0x14ede8
    void End(void) override;                                // FF3 0x14eea0
};

// ---- the yes/no dialog (layout types 6, 10, 11) ----
// The question is message (msg0, msg1) (item 0x415); the choices are messages
// (1, 1) and (1, 2) on items 0x416 / 0x417, the cursor sprites 0x3f4 / 0x3f5
// swap their textures by the selection. The owner moves the selection and
// requests the close.
void YesNoSetMsg(int msg0, int msg1);           // msg0 < 0 or msg1 < 0: no question
void YesNoSetSel(int sel);                      // 0 first, 1 second, 2 none shown
void YesNoReqClose(int req);                    // != 0: play "out" and close

int YesNoOnOpen(int handle);                    // types 6/10/11 on_open
int YesNoOnUpdate(int handle);                  // types 6/10/11 on_update; 1 when closing
int FUN_0014f338(int handle);                   // types 6/10/11 on_close (no Ghidra function)

// ---- message picture keys (unassigned@001b1648's data; move with it) ----
// The message picture keys (MSG_PIC_KEY_DAT, msg_pic_key_dat): the document
// table of ingame/story_menu_photo.h.

// The key of message (msg0, msg1), -1 when it has none (a satellite of this
// TU, FF3 0x2adbf0).
int MsgGetPicKey(int msg0, int msg1);

// ---- the menu window (layout type 8, MENU_WDW_PHF) ----
// A message (item 0x415) with an optional yes/no under it; left/right move
// between yes and no. OK / cancel set the result and, unless the owner keeps
// the window open, play "out" and close it. The story menu's item and photo
// pages use it.
enum MENU_WDW_RESULT
{
    MENU_WDW_RESULT_NONE   = 0,
    MENU_WDW_RESULT_YES    = 1,         // OK with yes selected
    MENU_WDW_RESULT_NO     = 2,         // OK with no selected
    MENU_WDW_RESULT_CANCEL = 3,
};

void MenuWdwSetKeepOpen(int keep);              // != 0: OK / cancel only set the result
int  MenuWdwGetResult(void);                    // MENU_WDW_RESULT_* (only while the yes/no is shown)
void MenuWdwSetYesNo(int show, int yes);        // also clears the result
void MenuWdwSetMsg(int msg0, int msg1);         // msg0 < 0 or msg1 < 0: no text
int  FUN_0014f348(void);                        // last PadTrgGetMenuInput value (no Ghidra function, no caller)

int MenuWdwOnUpdate(int handle);                // type 8 on_update
int FUN_0014f798(int handle);                   // type 8 on_open (no Ghidra function)
int FUN_0014f850(int handle);                   // type 8 on_close (no Ghidra function)

// ---- the pause menu (layout type 4, PAUSE_PHF) ----
// The choice (word 0x3918e8): 0 continue, 1 options, 2 quit. OK on quit asks
// again through the layout message (type 5); ingame/pause reads the choice
// once the layout has closed.
int  PauseMenuGetSel(void);
void PauseMenuSetSel(int sel);

int PauseMenuOnOpen(int handle);                // type 4 on_open
int PauseMenuOnUpdate(int handle);              // type 4 on_update; 1 when closing
int PauseMenuOnClose(int handle);               // type 4 on_close

// ---- the slot select (layout type 7, SLOTSELECT_PHF) ----
// Two choices, messages (0x59, 9) / (0x59, 10) on items 0x3e9 / 0x3ed and a
// cursor item 0x3ee; the owner moves the choice and requests the close.
void SlotSelSetSel(int sel);
void SlotSelMouse(int sel);
int  SlotSelIsSel(int sel);
void SlotSelReqClose(int req);                  // != 0: play "out" and close

int FUN_00150190(int handle);                   // type 7 on_open (no Ghidra function)
int SlotSelOnUpdate(int handle);                // type 7 on_update; 1 when closing
int FUN_00150290(int handle);                   // type 7 on_close (no Ghidra function)

#endif /* _GRAPHICS_MOTION_ANICODE_H */
