/* ==========================================================================
 *  outgame/title_menu.c
 *
 *  The title (see title_menu.h). FF3 0x25d000..0x25e9e0, FF3's own code: FF2
 *  outgame/title.c's phases are the ancestor in name only (FF2 drew its
 *  title by hand; FF3 plays B2d labels of TITLE_PHF over the background
 *  movie and draws the menu entries from layout item hooks).
 *
 *  Flow: Title_Mode (5) loads the two layouts, the SE bank and (when another
 *  BGM played) the title BGM, starts the movie, then hands over to its
 *  children: Title_Top (15) waits for START / OK (-> 17) or 30 s (-> 16);
 *  Title_Move_Movie (16) plays movie 0x18 through phase 67 and comes back to
 *  15; Title_Menu (17) moves cursor 0x11 over new game / load game / album /
 *  option (-> 51, 9, 8, 7); Title_NewGame (51) picks the
 *  difficulty (-> 9 Story_Main).
 *
 *  FF3 reads the singletons through gp-relative cached pointers; nothing
 *  depends on the cache, so calls go to the getters directly. FF3 0x0025e9a8
 *  (GPhaseCtrl) and 0x0025e9c8 (the AlbumViewCtrl static) are the
 *  compiler's atexit thunks that end the file.
 * ======================================================================== */
#include "outgame/title_menu.h"

#include <new>                              // operator new / delete

#include "common/cmn_fade_layer.h"          // GetCmnFadeLayer, CmnFadeLayer*
#include "graphics/graph2d/g2d_debug.h"     // DispSqrRect
#include "graphics/motion/anicode.h"        // B2D_HOOK_REC, B2dSetHookList, SetDefaultGsDrawState
#include "ingame/event/prg/ev_main.h"       // EventSetMode
#include "ingame/ingame_wrk.h"              // title_start_flg, PlayTimeSetStop, IngameSetOutgame, TitleMenuIsItemOpen
#include "ingame/menu/menu_csr.h"
#include "ingame/menu/menu_draw_tex.h"      // TITLE_MENU_OBJ
#include "ingame/movie_room_menu/prg/movie_projecter.h"   // IsMoviePhase, MovieReq*
#include "outgame/title_obj.h"             // TITLE_OBJ
#include "outgame/title_room.h"            // PC: the room backdrop (TitleRoom*)
#include "reipan/reipan_ui/reipan_rml_options.h"  // PC: the RmlUi options screen
#include "ingame/sound/snd_ctrl.h"
#include "main/gphase.h"
#include "main/gphase_tree.h"               // GPHASE_TREE::Reset
#include "album/prg/album.h"                 // AlbumSetIngame
#include "system/b2d/B2dCtl.h"              // B2dCtlSetAlpha
#include "system/b2d/BpCtl.h"
#include "system/eeiop/cd_file_dat.h"       // TITLE_PHF, TITLEBG_PHF, OUTGAME_BG_PSS, ...
#include "system/os/system.h"               // GetSysWrk, SYS_WRK::GetLanguage, GetPALMode
#include "system/pad/pad.h"                 // pad_wrk
#include "system/pad/pad_ctrl.h"            // CheckPadTrgPlaySe, pad_key
#include "ingame/menu/menu_mouse.h"    // PC: the mouse in menus

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

// Menu cursors.
enum
{
    TITLE_CSR_MENU = 0x11,              // title menu: new game, load game, album, option
    TITLE_CSR_DIFF = 0x33,              // difficulty: easy, normal, hard, nightmare
};

static const char *const title_menu_sprites[] = { "R newgame", "R loadgame", "R albm", "R opt" };
static constexpr int title_menu_count = sizeof(title_menu_sprites) / sizeof(title_menu_sprites[0]);

// System SEs (SYS_BASE_BD) and the menu start bank's SE.
enum
{
    TITLE_SE_MOVE   = 0x1a9b0000,
    TITLE_SE_CANCEL = 0x1a9b0001,
    TITLE_SE_OK     = 0x1a9b0003,
    TITLE_SE_START  = MENU_START_BD_BD << 16,    // 0x1bd30000
};

static TITLE_OBJ      *title_obj;           // sdata 0x3739e0
static int             title_bgm_load;      // sdata 0x3739e4: 1 when Title_Mode loads the title BGM itself
static int             title_top_cnt;       // sdata 0x3739e8: Title_Top's frames without input
static int             title_movie_step;    // sdata 0x3739ec
static int             title_menu_csr;      // sdata 0x3739f0: cursor 0x11 kept while 17 is left
static int             title_difficulty;    // sdata 0x3739f4
static TITLE_MENU_OBJ *title_menu_obj;      // sdata 0x3726b0
static float           title_diff_glow[4];  // data 0x333480: per difficulty 0..1, toward the cursor

static int TitleLayoutHandle(void)
{
    return BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + TITLE_PHF);
}

static int TitleLayoutIsLoadEnd(void)
{
    return BpCtlIsLoadEnd(GetSysWrk()->GetLanguage() + TITLE_PHF);
}

// ──────────────────────────────────────────────────────────────────────
// The background movie object
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025d000
// callcheck: operator_new, TitleObjCtor (new TITLE_OBJ)
void TitleObjCreate(void)
{
    TITLE_OBJ *obj = new TITLE_OBJ(OUTGAME_BG_PSS);
    title_obj = obj;
    obj->alpha_reg = 0x68;
}

// FF3 0x0025d040
// callcheck: operator_delete, TitleObjDtor (delete)
void TitleObjDelete(void)
{
    TITLE_OBJ *obj = title_obj;

    if (obj != nullptr)
    {
        delete obj;
    }
    title_obj = nullptr;
}

// FF3 0x0025d080
int TitleObjIsReady(void)
{
    if (title_obj != nullptr && title_obj->CheckReady() != 0)
        return 1;
    return 0;
}

// Kind 0 hook of TITLE_PHF's item "movie": the movie under the layout at the
// item's alpha, TITLEBG_PHF's layer at 0.6 of it. Words: stack[2] alpha.
// FF3 0x0025d0b8.
static int TitleObjDrawCallback(const B2D_HOOK_ARGS *args)
{
    int alpha = (int)args->stack[2];

    // PC: with a room behind the title (outgame/title_room.c) the movie and
    // the picture of Rei are left out; the logo and menus stay.
    if (TitleRoomHidesTitleArt())
    {
        B2dCtlSetAlpha(BpCtlGetHandleByFileNo(TITLEBG_PHF), 0);
        return 0;
    }

    if (alpha != 0)
    {
        if (title_obj != nullptr)
        {
            title_obj->alpha_reg = ((u_long)(u_int)alpha << 32) | 0x68;
            title_obj->Draw();
        }
        B2dCtlSetAlpha(BpCtlGetHandleByFileNo(TITLEBG_PHF), (int)((float)alpha * 0.6f));  /* sdata 0x3726b4 */
        SetDefaultGsDrawState();
    }
    return 0;
}

// FF3 0x0025d138 (the record list is built on the stack)
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (TitleLayoutHandle)
void TitleSetLayoutCallbacks(void)
{
    const B2D_HOOK_REC hooks[] =
    {
        { TitleObjDrawCallback, 0,  0, "movie" },
        { nullptr,             -1, -1, nullptr },
    };

    B2dSetHookList(TitleLayoutHandle(), hooks);
    BpPlayLabel(BpCtlGetHandleByFileNo(TITLEBG_PHF), "NewLabel_0");
    B2dCtlSetAlpha(BpCtlGetHandleByFileNo(TITLEBG_PHF), 0);
}

// ──────────────────────────────────────────────────────────────────────
// Menu helpers
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025d1f0
void TitleMenuDecide(int pos)
{
    title_start_flg.load = 0;
    EventSetMode(0);
    switch (pos)
    {
    case 0:                             // new game
        title_start_flg.mode = 1;
        break;
    case 1:                             // load game
        title_start_flg.load = 1;
        title_start_flg.mode = 1;
        break;
    case 2:                             // album
        title_start_flg.mode = 0;
        AlbumSetIngame(0, 0);
        break;
    case 3:                             // option
        title_start_flg.mode = 0;
        break;
    default:
        title_start_flg.mode = 1;
        break;
    }
}

// Cursor 0x33's filter (MENU_CSR_FILTER, a 4 x 1 grid): out[i] = i for an
// open difficulty, else -1.
// FF3 0x0025d2b0 (no Ghidra function before 2026-10-14: it follows
// TitleMenuDecide's jump table).
static void TitleNewGameCsrFilter(short *out, short w, short h)
{
    for (short i = 0; i < w; i++, out++)
        *out = TitleMenuIsItemOpen(i) ? i : -1;
}

// FF3 0x0025d320
int TitleMenuGetEnableItems(int *items)
{
    int n = 0;

    for (int i = 0; i < 4; i++)
    {
        if (TitleMenuIsItemOpen(i))
        {
            *items++ = i;
            n++;
        }
    }
    return n;
}

// The difficulty glows step toward the cursor: +0.125 for `pos`, -0.125 for
// the others, held in [0, 1]. FF3 0x002bf270 (emitted among the out-of-line
// code at the end of .text, but on this file's data: title_diff_glow, sdata
// 0x37274c / 0x372750).
// callcheck: FUN_002603a8 (not called: the code at 0x2bf308 after this function's jr ra is a separate
// hidden destructor, vtable 0x3425a8, that Ghidra's call list counts in)
void TitleDifficultyGlowUpdate(int pos)
{
    for (int i = 0; i < 4; i++)
    {
        if (i == pos)
            title_diff_glow[i] += 0.125f;
        else
            title_diff_glow[i] -= 0.125f;
        if (title_diff_glow[i] > 1.0f)
            title_diff_glow[i] = 1.0f;
        else if (title_diff_glow[i] < 0.0f)
            title_diff_glow[i] = 0.0f;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Item hooks of TITLE_PHF (data 0x3633c8, copied to the stack before
// B2dSetHookList). Words: t1 xy (the item rectangle: [0] x, [1] y, [6]
// right x), t3 colour, stack[2] alpha.
// ──────────────────────────────────────────────────────────────────────

static void TitleDrawSprite(int handle, const char *name, int x, int y, u_int rgb, float alpha)
{
    B2dDrawSpriteByName(handle, name, x, y, rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff,
                        alpha, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
}

// Item "menu": the title menu entry under cursor 0x11, cross-faded with the
// one before through title_menu_obj. FF3 0x0025e3f0 (no Ghidra function
// before 2026-10-14: it follows one_Title_NewGame).
// callcheck: B2dDrawSpriteByName (TitleDrawSprite)
static int TitleMenuItemDrawCallback(const B2D_HOOK_ARGS *args)
{
    const int *xy     = (const int *)args->t1;
    u_int      rgb    = (u_int)args->t3;
    float      alpha  = (float)(int)args->stack[2] * (1.0f / 128.0f);   /* sdata 0x372708 */
    int        handle = BpGetDrawHandle();
    TITLE_MENU_OBJ *obj;
    int        item;

    title_menu_obj->SetItem(MenuCsrGetPos(TITLE_CSR_MENU));
    title_menu_obj->scale = alpha;
    title_menu_obj->Step();

    obj  = title_menu_obj;
    item = obj->slot[obj->cur];
    if ((u_int)item < title_menu_count)
    {
        int fade = (int)((float)obj->GetFade() * obj->scale);
        TitleDrawSprite(handle, title_menu_sprites[item], xy[0], xy[1], rgb, (float)fade * (1.0f / 128.0f) * alpha);
        obj = title_menu_obj;
    }

    item = obj->slot[obj->cur ^ 1];
    if ((u_int)item < title_menu_count)
    {
        int fade = (int)((float)(obj->fade_max - obj->GetFade()) * obj->scale);
        TitleDrawSprite(handle, title_menu_sprites[item], xy[0], xy[1], rgb, (float)fade * (1.0f / 128.0f) * alpha);
    }

    SetDefaultGsDrawState();
    return 0;
}

// Item "arw": the cursor arrow. FF3 0x0025e630.
// callcheck: B2dDrawSpriteByName (TitleDrawSprite)
static int TitleMenuArrowDrawCallback(const B2D_HOOK_ARGS *args)
{
    const int *xy    = (const int *)args->t1;
    u_int      rgb   = (u_int)args->t3;
    float      alpha = (float)(int)args->stack[2] * (1.0f / 128.0f);    /* sdata 0x372718 */

    TitleDrawSprite(BpGetDrawHandle(), "R arw", xy[0], xy[1], rgb, alpha);
    SetDefaultGsDrawState();
    return 0;
}

// Item "menu_diffuculty": the open difficulties side by side (150 px apart,
// centred in the item), each a frame, its name and an arrow lit by its glow.
// FF3 0x0025e6e0.
// callcheck: B2dDrawSpriteByName (TitleDrawSprite)
static int TitleMenuDrawCallback(const B2D_HOOK_ARGS *args)
{
    static const char *const names[4] =     // data 0x3634d0
    {
        "R esy", "R nml", "R hrd", "R nightmare",
    };
    int        items[4] = { -1, 0, 0, 0 };  // data 0x3634e0
    const int *xy     = (const int *)args->t1;
    u_int      rgb    = (u_int)args->t3;
    float      alpha  = (float)(int)args->stack[2] * (1.0f / 128.0f);   /* sdata 0x372738 */
    int        handle = BpGetDrawHandle();
    int        pos    = MenuCsrGetPos(TITLE_CSR_DIFF);
    int        n      = TitleMenuGetEnableItems(items);
    int        left   = (xy[6] + xy[0] - n * 150) / 2;

    TitleDifficultyGlowUpdate(pos);
    for (int i = 0; i < n; i++)
    {
        int item = items[i];
        if ((u_int)item < 4)
        {
            float glow = title_diff_glow[item];
            int   x    = left + i * 150;
            int   y    = xy[1];

            TitleDrawSprite(handle, "R csl", x - 5, y - 6, rgb, alpha * glow);
            TitleDrawSprite(handle, names[item], x - 7, y + 1, rgb,
                            alpha * (glow * 0.3f + 0.7f));         /* sdata 0x372744, 0x372748 */
            TitleDrawSprite(handle, "R arw", x, y, rgb,
                            alpha * (glow * 0.5f + 0.5f));         /* sdata 0x372740 */
        }
    }
    SetDefaultGsDrawState();
    return 0;
}

static const B2D_HOOK_REC title_menu_hooks[] =
{
    { TitleMenuItemDrawCallback,  0,  0, "menu" },
    { TitleMenuArrowDrawCallback, 0,  0, "arw" },
    { TitleMenuDrawCallback,      0,  0, "menu_diffuculty" },
    { nullptr,                   -1, -1, nullptr },
};

// ──────────────────────────────────────────────────────────────────────
// Phase 5, Title_Mode
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025d388
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, operator_new, TitleMenuObjCtor (TitleLayoutHandle, new TITLE_MENU_OBJ)
int init_Title_Mode(void)
{
    GPHASE_NODE *leaf = GetGPhaseCtrl()->leaf;

    switch (leaf->state_cnt)
    {
    case 0:
        PlayTimeSetStop(1);
        IngameSetOutgame(1);
        SndSeReset();
        GetCmnFadeLayer()->Term();
        GetCmnFadeLayer()->Reset();
        title_bgm_load = 0;
        title_start_flg.mode = 0;
        // PC: the stream is the title theme, or the track chosen for the title
        // (outgame/title_room.c, TitleRoomTitleBgm).
        if (!SndBgmIsFileSet(TitleRoomTitleBgm()))
        {
            SndBgmStopAll();
            title_bgm_load = 1;
        }
        pad_wrk[0].stick_dpad = 1;
        MenuCsrInit(TITLE_CSR_MENU, title_menu_count, 1, 0);
        MenuCsrInitWithFilter(TitleNewGameCsrFilter, TITLE_CSR_DIFF, 4, 1, 3);
        title_menu_obj = new TITLE_MENU_OBJ;
        title_menu_obj->len    = 8;
        title_menu_obj->enable = 1;
        for (int i = 3; i >= 0; i--)
            title_diff_glow[i] = 0.0f;
        BpCtlLoadReq(TITLEBG_PHF, nullptr);
        BpCtlLoadReq(GetSysWrk()->GetLanguage() + TITLE_PHF, nullptr);
        SndSeBankLoad(MENU_START_BD_BD);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
        return 0;

    case 1:
    {
        int ready;

        if (BpCtlIsDataReady(TitleLayoutHandle()) < 1)
            return 0;
        if (BpCtlIsDataReady(BpCtlGetHandleByFileNo(TITLEBG_PHF)) < 1)
            return 0;
        if (!SndSeBankIsLoaded(MENU_START_BD_BD))
            return 0;
        ready = 1;
        if (title_bgm_load)
        {
            ready = 0;
            if (SndBgmIsAllIdle())
            {
                SndBgmPreload(TitleRoomTitleBgm(), 100, 0x1000, 0, nullptr, 1, 0);
                ready = 1;
            }
        }
        if (!ready)
            return 0;
        TitleObjCreate();
        GetGPhaseCtrl()->leaf->state_cnt = 2;
        return 0;
    }

    case 2:
    {
        int handle;

        if (!TitleObjIsReady())
            return 0;
        if (title_bgm_load && !SndBgmIsPreloaded(0))
            return 0;
        TitleSetLayoutCallbacks();
        if (title_bgm_load)
            SndBgmStartPreloaded(0);
        handle = TitleLayoutHandle();
        B2dCtlSetItemDispByName(handle, "R csl", 0);
        B2dSetHookList(handle, title_menu_hooks);
        return 1;
    }

    default:
        return 0;
    }
}

// FF3 0x0025d6e8
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, BpCtlIsLoadEnd, operator_delete, TitleMenuObjDtor (TitleLayoutHandle, TitleLayoutIsLoadEnd, delete)
int end_Title_Mode(void)
{
    if (GetGPhaseCtrl()->leaf->state_cnt != 0)
        return 0;
    if (!TitleLayoutIsLoadEnd())
        return 0;

    BpCtlRelease(TitleLayoutHandle());
    BpCtlRelease(BpCtlGetHandleByFileNo(TITLEBG_PHF));
    if (title_menu_obj != nullptr)
    {
        delete title_menu_obj;
    }
    // Options keeps the title BGM playing.
    if (MenuCsrGetPos(TITLE_CSR_MENU) != 3)
        SndBgmStop(GetPALMode() ? 25 : 30, 0);
    TitleObjDelete();
    MenuCsrRelease(TITLE_CSR_DIFF);
    MenuCsrRelease(TITLE_CSR_MENU);
    GetCmnFadeLayer()->Term();
    pad_wrk[0].stick_dpad = 0;
    return 1;
}

// FF3 0x0025d820
int one_Title_Mode(void)
{
    GetGPhaseCtrl()->SetNext(15, 0);      // Title_Top
    return 1;
}

// The black backdrop, the fade layer and the layouts, unless a movie plays.
// FF3 0x0025d860
void pre_Title_Mode(void)
{
    if (IsMoviePhase())
        return;
    DispSqrRect(0.0f, 0.0f, 640.0f, 448.0f, 0, 0, 0, 0, 0x80);    /* sdata 0x3726d8, 0x3726dc */
    TitleRoomDrawBackdrop();            // PC: the room backdrop, under the layouts (outgame/title_room.c)
    GetCmnFadeLayer()->Update();
    BpCtlMain();
}

// FF3 0x0025d8d8
void after_Title_Mode(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Phase 15, Title_Top
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025d8e0
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, BpCtlIsLoadEnd (TitleLayoutHandle, TitleLayoutIsLoadEnd)
int init_Title_Top(void)
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    if (ctrl->leaf->state_cnt == 0)
    {
        title_top_cnt = 0;
        // Back from the menu, or in from the boot / the attract movie.
        BpPlayLabel(TitleLayoutHandle(), GetGPhaseCtrl()->from == 17 ? "START_back" : "START_in");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    else if (ctrl->leaf->state_cnt == 1)
    {
        return TitleLayoutIsLoadEnd() ? 1 : 0;
    }
    return 0;
}

// FF3 0x0025da18
int end_Title_Top(void)
{
    return 1;
}

// FF3 0x0025da20
int one_Title_Top(void)
{
    title_top_cnt++;
    // PC: the room backdrop holds the attract movie off unless asked for
    // (outgame/title_room.c; the movie player borrows the room's memory).
    if (TitleRoomHoldsAttract())
        title_top_cnt = 0;
    ReiPan_MenuMouseConfirm();          // PC: a click is "press START"
    if (CheckPadTrgPlaySe((u_short)(pad_key[PAD_KEY_OK] | 0x800), TITLE_SE_OK, 0))
    {
        GetGPhaseCtrl()->SetNext(17, 0);  // Title_Menu
        return 1;
    }
    // 30 seconds idle: the attract movie.
    if (!GetPALMode())
    {
        if (title_top_cnt < 900)
            return 0;
    }
    else if (title_top_cnt < 750)
        return 0;
    title_top_cnt = 0;
    GetGPhaseCtrl()->SetNext(16, 0);      // Title_Move_Movie
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 16, Title_Move_Movie
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025daf0
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, BpCtlIsLoadEnd (TitleLayoutHandle, TitleLayoutIsLoadEnd)
int init_Title_Move_Movie(void)
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    if (ctrl->leaf->state_cnt == 0)
    {
        title_movie_step = 0;
        BpPlayLabelEx(TitleLayoutHandle(), "START_out", 0);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    else if (ctrl->leaf->state_cnt == 1)
    {
        return TitleLayoutIsLoadEnd() ? 1 : 0;
    }
    return 0;
}

// FF3 0x0025dbd8
int end_Title_Move_Movie(void)
{
    return 1;
}

// FF3 0x0025dbe0
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (TitleLayoutHandle)
int one_Title_Move_Movie(void)
{
    switch (title_movie_step)
    {
    case 0:
        // Movie 0x18 through Story_SceneMovie, under phase 67 linked here;
        // OK, cancel or START skip it.
        TitleObjDelete();
        MovieReqPlay(0x18, 1);
        MovieReqSetFilmMode(0);
        MovieReqSetSkipMask(pad_key[PAD_KEY_OK] | pad_key[PAD_KEY_CANCEL] | 0x800);
        GetGPhaseCtrl()->SetNext(0x43, 1);
        title_movie_step = 1;
        return 1;

    case 1:
        // Back from the movie: the tree as the ROM defines it, the background
        // movie again, the menu cursor on its first item.
        GetGPhaseTree()->Reset();
        TitleMovieFlagClear();
        TitleObjCreate();
        title_movie_step = 2;
        return 0;

    case 2:
        if (!TitleObjIsReady())
            return 0;
        BpPlayLabel(TitleLayoutHandle(), "START_in");
        GetGPhaseCtrl()->SetNext(15, 0);  // Title_Top
        return 1;

    default:
        return 0;
    }
}

// FF3 0x0025dd48
void TitleMovieFlagClear(void)
{
    title_menu_csr = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 17, Title_Menu
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025dd50
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, BpCtlIsLoadEnd (TitleLayoutHandle, TitleLayoutIsLoadEnd)
int init_Title_Menu(void)
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    if (ctrl->leaf->state_cnt == 0)
    {
        capCtl *fade;

        MenuCsrSetPos(TITLE_CSR_MENU, title_menu_csr);
        title_menu_obj->Reset();
        // In from the top screen, or back from a sub-menu.
        BpPlayLabel(TitleLayoutHandle(), GetGPhaseCtrl()->from == 15 ? "START_next" : "back");
        fade = GetCmnFadeLayer();
        fade->base_alpha = 0x80;
        GetCmnFadeLayer()->SetItemHandle(nullptr);
        GetCmnFadeLayer()->Show(0);
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    else if (ctrl->leaf->state_cnt == 1)
    {
        return TitleLayoutIsLoadEnd() ? 1 : 0;
    }
    return 0;
}

// FF3 0x0025ded8
int end_Title_Menu(void)
{
    title_menu_csr = MenuCsrGetPos(TITLE_CSR_MENU);
    GetCmnFadeLayer()->Hide(0x1e);
    return 1;
}

// PC: the mouse. The menu is one item between two arrows ("arw"): the arrows
// and the wheel step it, a click on the item chooses it.
static void TitleMenuMouse(const char *arrows)
{
    ReiPan_MenuBox box;

    ReiPan_MenuMouseBegin(REIPAN_MM_WHEEL_HORIZONTAL, nullptr);
    if (ReiPan_MenuMouseNameBox(TitleLayoutHandle(), arrows, &box))
    {
        const float arrow = 28.0f;

        ReiPan_MenuMouseButton(box.x - 8.0f, box.y, arrow, box.h, REIPAN_MENU_LEFT);
        ReiPan_MenuMouseButton(box.x + box.w + 8.0f - arrow, box.y, arrow, box.h, REIPAN_MENU_RIGHT);
        ReiPan_MenuMouseButton(box.x, box.y, box.w, box.h, REIPAN_MENU_CROSS);
    }
}

// FF3 0x0025df10
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (TitleLayoutHandle)
int one_Title_Menu(void)
{
    // Title_NewGame, Story_Main (load game), album and Option_Main.
    static const short next[title_menu_count] = { 51, 9, 8, 7 };
    int pos;

    // PC: Options is the port's RmlUi options screen (reipan_rml_options.h)
    // when it can open, over this menu, which waits under it and takes the
    // controls back when the player backs out; Option_Main only otherwise.
    if (ReiPan_RmlOptionsIsOpen())
        return 0;

    TitleMenuMouse("arw");              // PC
    MenuCsrUpdate(TITLE_CSR_MENU, 0);
    if (MenuCsrIsMovedSe(TITLE_CSR_MENU, TITLE_SE_MOVE))
        return 0;

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0))
    {
        pos = MenuCsrGetPos(TITLE_CSR_MENU);
        if ((u_int)pos >= title_menu_count)
            return 0;
        if (pos == 3 && ReiPan_RmlOptionsOpen(REIPAN_RML_OPTIONS_FROM_TITLE))  // PC
            return 0;
        GetGPhaseCtrl()->SetNext(next[pos], 0);
        TitleMenuDecide(pos);
        if (pos != 0)
            BpPlayLabel(TitleLayoutHandle(), "next");
        if (pos == 1)
            SndSeBankReleaseAfterPlay(SndSePlay(TITLE_SE_START, 100, 0x1000, 0, nullptr, 0),
                                      MENU_START_BD_BD, 1);
        else
            SndSePlay(TITLE_SE_OK, 100, 0x1000, 0, nullptr, 0);
        // New game keeps the bank for its own OK, load game frees it after its SE.
        if ((u_int)pos > 1)
            SndSeBankRelease(MENU_START_BD_BD, 0);
        return 1;
    }

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], TITLE_SE_CANCEL, 0))
    {
        BpPlayLabel(TitleLayoutHandle(), "START_back");
        GetGPhaseCtrl()->SetNext(15, 0);  // Title_Top
        return 1;
    }
    return 0;
}

// FF3 0x0025e110
void pre_Title_Menu(void)
{
}

// FF3 0x0025e118
void after_Title_Menu(void)
{
}

// FF3 0x0025e120
void TitleMenuFlagSet(void)
{
    title_difficulty = 1;
}

// FF3 0x0025e130
int TitleMenuCtrlInit(void)
{
    return title_difficulty;
}

// ──────────────────────────────────────────────────────────────────────
// Phase 51, Title_NewGame (the difficulty select)
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0025e138
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo, BpCtlIsLoadEnd (TitleLayoutHandle, TitleLayoutIsLoadEnd)
int init_Title_NewGame(void)
{
    GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    if (ctrl->leaf->state_cnt == 0)
    {
        TitleMenuFlagSet();
        MenuCsrSetPos(TITLE_CSR_DIFF, TitleMenuCtrlInit());
        for (int i = 3; i >= 0; i--)
            title_diff_glow[i] = 0.0f;
        BpPlayLabel(TitleLayoutHandle(), "difficulty_in");
        GetGPhaseCtrl()->leaf->state_cnt = 1;
    }
    else if (ctrl->leaf->state_cnt == 1)
    {
        return TitleLayoutIsLoadEnd() ? 1 : 0;
    }
    return 0;
}

// FF3 0x0025e268
int end_Title_NewGame(void)
{
    title_difficulty = MenuCsrGetPos(TITLE_CSR_DIFF);
    return 1;
}

// FF3 0x0025e290
// callcheck: GetLanguage, GetSysWrk, BpCtlGetHandleByFileNo (TitleLayoutHandle)
int one_Title_NewGame(void)
{
    TitleMenuMouse("arw_diffuculty");   // PC
    MenuCsrUpdate(TITLE_CSR_DIFF, 0);
    if (MenuCsrIsMovedSe(TITLE_CSR_DIFF, TITLE_SE_MOVE))
        return 1;

    if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_OK], -1, 0))
    {
        SndSeBankReleaseAfterPlay(SndSePlay(TITLE_SE_START, 100, 0x1000, 0, nullptr, 0),
                                  MENU_START_BD_BD, 1);
        BpPlayLabel(TitleLayoutHandle(), "diffuculty_next");
        GetGPhaseCtrl()->SetNext(9, 0);   // Story_Main
    }
    else if (CheckPadTrgPlaySe((u_short)pad_key[PAD_KEY_CANCEL], TITLE_SE_CANCEL, 0))
    {
        BpPlayLabel(TitleLayoutHandle(), "difficulty_back");
        GetGPhaseCtrl()->SetNext(17, 0);  // Title_Menu
    }
    return 1;
}
