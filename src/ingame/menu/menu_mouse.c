// ingame/menu/menu_mouse.c -- PC: the mouse on the game's cursors. See
// menu_mouse.h.

#include "ingame/menu/menu_mouse.h"

#include "common/cmn_fade_layer.h"      // GetCmnFadeLayer, CMN_FADE_LAYER_FILE
#include "graphics/graph2d/message.h"
#include "graphics/motion/anicode.h"    // SetDefaultGsDrawState
#include "system/b2d/BpCtl.h"           // BpCtlGetHandleByFileNo
#include "main/gphase.h"                // GetGPhaseCtrl
#include "system/os/system.h"           // GetSysWrk, SYS_WRK::GetLanguage
#include "system/pad/pad_ctrl.h"        // pad_key
#include "reipan/rendering/reipan_renderer.h"  // ReiPan_RendererGetViewBounds

#include <string.h>

// The move asked for last, until its cursor's next CsrPadCtrl. `read` keeps
// one that never landed -- the screen changed under it -- from landing on the
// cursor's next visit.
static CSR_ITEM *mm_pending_item;
static short     mm_pending_cell;
static int       mm_pending_read;

static void MenuMouseCsrAsk(CSR_ITEM *item, int cell)
{
    if (item->id == (short)cell)
        return;
    if (CsrItemFind(item, (short)cell) == -1)
        return;
    mm_pending_item = item;
    mm_pending_cell = (short)cell;
    mm_pending_read = ReiPan_MenuMouseReadCount();
}

static int MenuMouseCsrBox(CSR_ITEM *item, int cell, const ReiPan_MenuBox *box)
{
    // A target is a menu: the claim comes first, whatever else claims later.
    ReiPan_MenuMouseBegin(0, nullptr);
    if (!ReiPan_MenuMouseIn(box->x, box->y, box->w, box->h))
        return 0;
    if (CsrItemFind(item, (short)cell) == -1)
        return 1;
    if (ReiPan_MenuMouseClick(box->x, box->y, box->w, box->h))
    {
        MenuMouseCsrAsk(item, cell);
        ReiPan_MenuMousePress(REIPAN_MENU_CROSS);
    }
    else if (ReiPan_MenuMouseHover(box->x, box->y, box->w, box->h))
    {
        MenuMouseCsrAsk(item, cell);
    }
    return 1;
}

int MenuMouseCsrWork(MENU_CSR_WRK *wrk, int cell, const ReiPan_MenuBox *box)
{
    return MenuMouseCsrBox(&wrk->item, cell, box);
}

int MenuMouseCsr(int csr, int cell, const ReiPan_MenuBox *box)
{
    return MenuMouseCsrWork(MenuCsrGetWork(csr), cell, box);
}

int MenuMouseCsrWorkName(MENU_CSR_WRK *wrk, int cell, int handle, const char *name)
{
    ReiPan_MenuBox box;

    return ReiPan_MenuMouseNameBox(handle, name, &box) ? MenuMouseCsrWork(wrk, cell, &box) : 0;
}

int MenuMouseCsrWorkId(MENU_CSR_WRK *wrk, int cell, int handle, int id)
{
    ReiPan_MenuBox box;

    return ReiPan_MenuMouseItemBox(handle, id, &box) ? MenuMouseCsrWork(wrk, cell, &box) : 0;
}

int MenuMouseCsrName(int csr, int cell, int handle, const char *name)
{
    return MenuMouseCsrWorkName(MenuCsrGetWork(csr), cell, handle, name);
}

int MenuMouseCsrId(int csr, int cell, int handle, int id)
{
    return MenuMouseCsrWorkId(MenuCsrGetWork(csr), cell, handle, id);
}

static void MenuMouseCsrCells(MENU_CSR_WRK *wrk, const ReiPan_MenuBox *at, float dx, float dy,
                              int from_cursor)
{
    const CSR_ITEM *item = &wrk->item;
    const int view_x = wrk->vx.on != 0;
    const int view_y = wrk->vy.on != 0;
    const int cx = !from_cursor ? 0 : view_x ? wrk->vx.pos : item->x;
    const int cy = !from_cursor ? 0 : view_y ? wrk->vy.pos : item->y;
    const int tx = view_x ? wrk->vx.top : 0;
    const int ty = view_y ? wrk->vy.top : 0;
    int cols = view_x ? wrk->vx.size : item->w;
    int rows = view_y ? wrk->vy.size : item->h;

    if (cols > item->w - tx)
        cols = item->w - tx;
    if (rows > item->h - ty)
        rows = item->h - ty;

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            const int cell = item->tbl[(tx + c) + (ty + r) * item->w];

            if (cell < 0)
                continue;
            ReiPan_MenuBox box = *at;

            box.x += (float)(c - cx) * dx;
            box.y += (float)(r - cy) * dy;
            MenuMouseCsrWork(wrk, cell, &box);
        }
    }
}

void MenuMouseCsrFrom(MENU_CSR_WRK *wrk, const ReiPan_MenuBox *at, float dx, float dy)
{
    MenuMouseCsrCells(wrk, at, dx, dy, 1);
}

void MenuMouseCsrOrigin(MENU_CSR_WRK *wrk, const ReiPan_MenuBox *at, float dx, float dy)
{
    MenuMouseCsrCells(wrk, at, dx, dy, 0);
}

void MenuMouseCsrBar(MENU_CSR_WRK *wrk, int handle, int id, float dx, float dy)
{
    ReiPan_MenuBox at;

    ReiPan_MenuMouseBegin(0, nullptr);
    if (ReiPan_MenuMouseItemBox(handle, id, &at))
        MenuMouseCsrFrom(wrk, &at, dx, dy);
}

void MenuMouseCsrGrid(MENU_CSR_WRK *wrk, const B2D_GRID_SPR *spr, int handle)
{
    ReiPan_MenuBox at;

    if (spr != nullptr && spr->item != nullptr
        && ReiPan_MenuMouseItemBox(handle, spr->item->id, &at))
        MenuMouseCsrFrom(wrk, &at, (float)spr->dx, (float)spr->dy);
}

void MenuMouseCsrGridId(int csr, const B2D_GRID_SPR *spr, int handle)
{
    MenuMouseCsrGrid(MenuCsrGetWork(csr), spr, handle);
}

void MenuMouseCsrObj(MENU_CSR_WRK *wrk, BP_OBJ *obj, int id)
{
    for (const BP_OBJ_CSR *node = obj != nullptr ? obj->csr : nullptr; node != nullptr; node = node->next)
    {
        if (node->id == id)
        {
            MenuMouseCsrGrid(wrk, node->spr, obj->GetHandle());
            return;
        }
    }
}

void MenuMouseCsrObjId(int csr, BP_OBJ *obj, int id)
{
    MenuMouseCsrObj(MenuCsrGetWork(csr), obj, id);
}

int MenuMouseStepList(int cursor, const ReiPan_MenuBox *at, float dx, float dy, int count,
                      int (*can)(int index))
{
    ReiPan_MenuMouseBegin(0, nullptr);
    for (int i = 0; i < count; i++)
    {
        ReiPan_MenuBox box = *at;

        box.x += (float)(i - cursor) * dx;
        box.y += (float)(i - cursor) * dy;
        if (can != nullptr && can(i) == 0)
            continue;
        if (ReiPan_MenuMouseClick(box.x, box.y, box.w, box.h))
        {
            ReiPan_MenuMousePress(REIPAN_MENU_CROSS);
            return i;
        }
        if (ReiPan_MenuMouseHover(box.x, box.y, box.w, box.h))
            return i;
    }
    return cursor;
}

void MenuMouseCsrRows(int csr, int first_cell, const ReiPan_MenuBox *first, float w,
                      float dx, float dy, int count)
{
    for (int i = 0; i < count; i++)
    {
        ReiPan_MenuBox box = *first;

        box.x += dx * (float)i;
        box.y += dy * (float)i;
        if (w > 0.0f)
            box.w = w;
        MenuMouseCsr(csr, first_cell + i, &box);
    }
}

void MenuMouseTab(int cur, int index, const ReiPan_MenuBox *box, int dec, int inc)
{
    ReiPan_MenuMouseBegin(0, nullptr);
    if (index == cur || !ReiPan_MenuMouseClick(box->x, box->y, box->w, box->h))
        return;
    for (int i = cur; i != index; i += index > cur ? 1 : -1)
        ReiPan_MenuMousePress(index > cur ? inc : dec);
}

#ifdef REIPAN_SLPS_25544_CHS
struct PuzzleGuide
{
    const char *name;
    int key_strip;
    int first_key_y;
    int row_height;
    int buttons[3];
};

// Texel coordinates in the SLPS_255.44 guides. The navigation row and the
// lettering stay on the original sprites. The remaining rows draw live keys.
static const PuzzleGuide puzzle_guides[] = {
    { "R caption_in",   33, 45, 27, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R caption_next", 34, 46, 27, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, REIPAN_MENU_CIRCLE } },
    { "R caption",      34, 50, 27, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R cap_A",        32, 43, 24, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R cap_B",        32, 43, 24, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, REIPAN_MENU_CIRCLE } },
    { "R cap_C",        32, 43, 24, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, REIPAN_MENU_CIRCLE } },
    { "R cap_1",        32, 48, 25, { REIPAN_MENU_CROSS, REIPAN_MENU_SQUARE, REIPAN_MENU_TRIANGLE } },
    { "R cap_2",        32, 48, 25, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R cap_3",        32, 48, 25, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R cap_4",        32, 48, 25, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
    { "R_cap",          57, 68, 26, { REIPAN_MENU_CROSS, REIPAN_MENU_TRIANGLE, -1 } },
};

static const PuzzleGuide *FindPuzzleGuide(const char *name)
{
    for (const PuzzleGuide &guide : puzzle_guides)
        if (strcmp(name, guide.name) == 0)
            return &guide;
    return nullptr;
}

static int DrawPuzzleGuide(const B2D_HOOK_ARGS *args)
{
    const B2D_ITEM *item = B2dGetDrawItem();
    const PuzzleGuide *guide = item != nullptr && item->name != nullptr
        ? FindPuzzleGuide(item->name) : nullptr;
    if (guide == nullptr)
        return B2dDrawSpriteDirect(args);

    const int start = guide->first_key_y - guide->row_height / 2;
    const int end = guide->first_key_y + guide->row_height * (guide->buttons[2] >= 0 ? 2 : 1);
    if (args->a3 <= guide->key_strip || args->t0 <= end || (args->stack[3] & 0x3000))
        return B2dDrawSpriteDirect(args);

    B2D_HOOK_ARGS navigation = *args;
    navigation.t0 = start;
    B2dDrawSpriteDirect(&navigation);

    const int text_x = (guide->key_strip * args->t3 + 512) / 1024;
    const int text_y = (start * args->stack[0] + 512) / 1024;
    B2D_HOOK_ARGS lettering = *args;
    lettering.a1 += guide->key_strip;
    lettering.a2 += start;
    lettering.a3 -= guide->key_strip;
    lettering.t0 -= start;
    lettering.t1 += text_x;
    lettering.t2 += text_y;
    B2dDrawSpriteDirect(&lettering);

    const int height = (20 * args->stack[0] + 512) / 1024;
    for (int i = 0; i < 3 && guide->buttons[i] >= 0; ++i)
    {
        const int y = ((guide->first_key_y + i * guide->row_height) * args->stack[0] + 512) / 1024;
        const int width = guide->key_strip > 40 ? text_x / 2 : text_x - 5;
        MsgDrawButtonPrompt(guide->buttons[i], (int)args->t1 + text_x - width - 4,
                            (int)args->t2 + y - height / 2, width, height, (int)args->stack[1]);
    }
    SetDefaultGsDrawState();
    return 0;
}

static void SetPuzzleGuideHook(int handle, const char *name)
{
    if (FindPuzzleGuide(name) == nullptr)
        return;
    B2D_ITEM *item = B2dCtlFindItemByName(handle, name);
    if (item != nullptr)
    {
        static const B2D_ITEM_HOOK hook = { DrawPuzzleGuide };
        B2dCtlSetItemHook(item, &hook);
    }
}
#endif

void MenuMouseGuide(int handle, const char *name, const int *buttons, int rows)
{
    ReiPan_MenuBox box;

#ifdef REIPAN_SLPS_25544_CHS
    if (handle >= 0)
        SetPuzzleGuideHook(handle, name);
#endif
    ReiPan_MenuMouseBegin(0, nullptr);
    if (!ReiPan_MenuMouseNameBox(handle, name, &box))
        return;
    for (int i = 0; i < rows; i++)
    {
        if (buttons[i] >= 0)
            ReiPan_MenuMouseButton(box.x, box.y + box.h * (float)i / (float)rows, box.w,
                                   box.h / (float)rows, buttons[i]);
    }
}

ReiPan_MenuBox MenuMouseBoxGrow(ReiPan_MenuBox box, float mx, float my)
{
    box.x -= mx;
    box.y -= my;
    box.w += mx * 2.0f;
    box.h += my * 2.0f;
    return box;
}

ReiPan_MenuBox MenuMouseBoxUnion(ReiPan_MenuBox a, ReiPan_MenuBox b)
{
    const float x0 = a.x < b.x ? a.x : b.x;
    const float y0 = a.y < b.y ? a.y : b.y;
    const float x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    const float y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    ReiPan_MenuBox box = { x0, y0, x1 - x0, y1 - y0 };

    return box;
}

// ──────────────────────────────────────────────────────────────────────
// The button guide: caption.phf's icons, by what each sprite shows. The names
// are the Japanese release's: this disc's atlas has the cross where "maru"
// points, the triangle at "batu", the circle at "sankaku".
// ──────────────────────────────────────────────────────────────────────

struct MM_HINT_ICON
{
    int id;                             // the sprite
    int left;                           // the button of its left half, or all of it
    int right;                          // the button of its right half, -1 one button
};

static const MM_HINT_ICON mm_hint_icons[] =
{
    { 0x3e8, REIPAN_MENU_CROSS,    -1 },                // R_bottun_maru
    { 0x3e9, REIPAN_MENU_TRIANGLE, -1 },                // R_bottun_batu
    { 0x3ea, REIPAN_MENU_CIRCLE,   -1 },                // R_bottun_sankaku
    { 0x3eb, REIPAN_MENU_SQUARE,   -1 },                // R_bottun_sikaku
    { 0x3ec, REIPAN_MENU_L1,       REIPAN_MENU_R1 },    // R_bottun_L1R1
    { 0x3ed, REIPAN_MENU_L2,       REIPAN_MENU_R2 },    // R_bottun_L2R2
    { 0x3ee, REIPAN_MENU_LEFT,     -1 },                // R_bottun_jyuuji_L
    { 0x3ef, REIPAN_MENU_RIGHT,    -1 },                // R_bottun_jyuuji_R
    { 0x3f0, REIPAN_MENU_SELECT,   -1 },                // R_bottun_serect01
    { 0x3f1, REIPAN_MENU_SELECT,   -1 },                // R_bottun_serect02
    { 0x3f2, REIPAN_MENU_L1,       -1 },                // R_bottun_L1
    { 0x408, REIPAN_MENU_R1,       -1 },                // R R1
};

#define MM_HINT_MAX       16            // on screen at once
#define MM_HINT_ALPHA_MIN 0x40          // fading in or out is not there yet
#define MM_HINT_WORD_FIRST 0x3f3        // R_kettei ..
#define MM_HINT_WORD_LAST  0x407        // .. R_gazou

static const MM_HINT_ICON *MenuMouseHintIcon(int id)
{
    for (const MM_HINT_ICON &icon : mm_hint_icons)
    {
        if (icon.id == id)
            return &icon;
    }
    return nullptr;
}

static int MenuMouseBoxOnScreen(const ReiPan_MenuBox *box)
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 640.0f;
    float y1 = 448.0f;

    ReiPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);
    return box->w > 0.0f && box->h > 0.0f && box->x < x1 && box->x + box->w > x0
           && box->y < y1 && box->y + box->h > y0;
}

static void MenuMouseHints(void)
{
    const capCtl *cap = GetCmnFadeLayer();

    // capCtl's idle label is the free-play prompt (peep, push ...): not a menu.
    if (cap->label < 0 || cap->alpha < MM_HINT_ALPHA_MIN)
        return;

    const int handle = BpCtlGetHandleByFileNo(GetSysWrk()->GetLanguage() + CMN_FADE_LAYER_FILE);

    if (handle < 0)
        return;

    ReiPan_MenuBox icon_box[MM_HINT_MAX];
    ReiPan_MenuBox word_box[MM_HINT_MAX];
    const MM_HINT_ICON *icon[MM_HINT_MAX];
    int icons = 0;
    int words = 0;
    int h;
    int id;
    int alpha;
    ReiPan_MenuBox box;

    for (int i = 0; ReiPan_MenuMouseDrawnItem(i, &h, &id, nullptr, &box, &alpha); i++)
    {
        if (h != handle || alpha < MM_HINT_ALPHA_MIN || !MenuMouseBoxOnScreen(&box))
            continue;
        const MM_HINT_ICON *found = MenuMouseHintIcon(id);

        if (found != nullptr && icons < MM_HINT_MAX)
        {
            icon[icons] = found;
            icon_box[icons++] = box;
        }
        else if (id >= MM_HINT_WORD_FIRST && id <= MM_HINT_WORD_LAST && words < MM_HINT_MAX)
        {
            word_box[words++] = box;
        }
    }
    if (icons == 0)
        return;

    // A screen that shows hints is a menu, hook of its own or not.
    ReiPan_MenuMouseBegin(0, nullptr);

    for (int i = 0; i < icons; i++)
    {
        const ReiPan_MenuBox *ib = &icon_box[i];

        // The word a hint is about starts where its icon ends, give or take a
        // pixel or two: a click on the word is a click on the hint. The d-pad's
        // halves share one word, so they keep to their own half.
        if (icon[i]->right >= 0)
        {
            const float half = ib->w * 0.5f;

            ReiPan_MenuMouseButton(ib->x, ib->y, half, ib->h, icon[i]->left);
            ReiPan_MenuMouseButton(ib->x + half, ib->y, half, ib->h, icon[i]->right);
            continue;
        }

        ReiPan_MenuBox hit = *ib;

        if (icon[i]->left != REIPAN_MENU_LEFT && icon[i]->left != REIPAN_MENU_RIGHT)
        {
            for (int j = 0; j < words; j++)
            {
                const float gap = word_box[j].x - (ib->x + ib->w);

                if (gap >= -4.0f && gap <= 8.0f && word_box[j].y < ib->y + ib->h
                    && word_box[j].y + word_box[j].h > ib->y)
                {
                    hit = MenuMouseBoxUnion(hit, word_box[j]);
                    break;
                }
            }
        }
        ReiPan_MenuMouseButton(hit.x, hit.y, hit.w, hit.h, icon[i]->left);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Yes / No
// ──────────────────────────────────────────────────────────────────────

#define MM_CHOICE_W 116.0f              // the plates ("R YES" / "R NO")
#define MM_CHOICE_H 26.0f

static int mm_choice_drawing = -1;      // what the hooks noted drawing this frame
static int mm_choice_handle = -1;

void MenuMouseNoteChoice(int sel)
{
    mm_choice_drawing = sel;
    mm_choice_handle = BpGetDrawHandle();
}

// The plate around a word. The window type 8 swaps its two plate sprites to
// light the answer, so the plates themselves do not say which is which; the
// words stay put, centred on them.
static int MenuMouseChoiceBox(int handle, const char *name, ReiPan_MenuBox *out)
{
    ReiPan_MenuBox word;

    if (!ReiPan_MenuMouseNameBox(handle, name, &word))
        return 0;
    out->x = word.x + word.w * 0.5f - MM_CHOICE_W * 0.5f;
    out->y = word.y - 1.0f;
    out->w = MM_CHOICE_W;
    out->h = MM_CHOICE_H;
    return 1;
}

static void MenuMouseChoices(int sel, int handle)
{
    ReiPan_MenuBox yes;
    ReiPan_MenuBox no;

    if ((sel != 0 && sel != 1) || !MenuMouseChoiceBox(handle, "yes_on", &yes)
        || !MenuMouseChoiceBox(handle, "no_on", &no))
        return;

    ReiPan_MenuMouseBegin(0, nullptr);
    ReiPan_MenuMouseSeekBox(sel, 0, &yes, REIPAN_MENU_LEFT, REIPAN_MENU_RIGHT, REIPAN_MENU_CROSS);
    ReiPan_MenuMouseSeekBox(sel, 1, &no, REIPAN_MENU_LEFT, REIPAN_MENU_RIGHT, REIPAN_MENU_CROSS);
}

#define MM_PHASE_NORMAL 32              // Story_Normal: Rei walks
#define MM_PHASE_PHOTO  33              // Story_Photo: the shot just taken

void MenuMouseAskedPad(u_int mask)
{
    if ((mask & (pad_key[PAD_KEY_OK] | pad_key[PAD_KEY_CANCEL])) == 0)
        return;

    const GPHASE_CTRL *ctrl = GetGPhaseCtrl();

    if (ctrl->leaf != nullptr && ctrl->leaf->id != MM_PHASE_NORMAL && ctrl->leaf->id != MM_PHASE_PHOTO)
        ReiPan_MenuMouseBegin(0, nullptr);
}

void MenuMouseClickMask(u_int mask)
{
    static const struct { u_int bit; int button; } bits[] =
    {
        { 0x0040, REIPAN_MENU_CROSS }, { 0x0010, REIPAN_MENU_TRIANGLE },
        { 0x0020, REIPAN_MENU_CIRCLE }, { 0x0080, REIPAN_MENU_SQUARE },
        { 0x0800, REIPAN_MENU_START }, { 0x0100, REIPAN_MENU_SELECT },
        { 0x0004, REIPAN_MENU_L1 }, { 0x0008, REIPAN_MENU_R1 },
        { 0x0001, REIPAN_MENU_L2 }, { 0x0002, REIPAN_MENU_R2 },
    };

    if (!ReiPan_MenuMouseBegin(0, nullptr) || !ReiPan_MenuMouseClicked())
        return;
    for (const auto &entry : bits)
    {
        if (mask & entry.bit)
        {
            ReiPan_MenuMousePress(entry.button);
            return;
        }
    }
}

void MenuMousePoll(void)
{
    const int choice = mm_choice_drawing;
    const int handle = mm_choice_handle;

    mm_choice_drawing = -1;
    mm_choice_handle = -1;
    MenuMouseHints();
    MenuMouseChoices(choice, handle);
}

// ──────────────────────────────────────────────────────────────────────
// Cursors
// ──────────────────────────────────────────────────────────────────────

static CSR_GRID *mm_grid_item;
static int       mm_grid_axis;
static int       mm_grid_pos;
static int       mm_grid_read;

int MenuMouseGrid(CSR_GRID *g, int axis, int pos, const ReiPan_MenuBox *box)
{
    ReiPan_MenuMouseBegin(0, nullptr);
    if (!ReiPan_MenuMouseIn(box->x, box->y, box->w, box->h))
        return 0;

    const int click = ReiPan_MenuMouseClick(box->x, box->y, box->w, box->h);

    if (click)
        ReiPan_MenuMousePress(REIPAN_MENU_CROSS);
    if ((click || ReiPan_MenuMouseHover(box->x, box->y, box->w, box->h)) && g->axis[axis].cur != pos)
    {
        mm_grid_item = g;
        mm_grid_axis = axis;
        mm_grid_pos  = pos;
        mm_grid_read = ReiPan_MenuMouseReadCount();
    }
    return 1;
}

void MenuMouseGridClaim(const CSR_GRID *g)
{
    ReiPan_MenuMouseBegin(g->axis[1].max > g->axis[1].min   ? REIPAN_MM_WHEEL_VERTICAL
                          : g->axis[0].max > g->axis[0].min ? REIPAN_MM_WHEEL_HORIZONTAL
                                                            : 0,
                          nullptr);
}

int MenuMouseGridApply(CSR_GRID *g, int *axis)
{
    if (mm_grid_item != g)
        return 0;
    mm_grid_item = nullptr;
    if (ReiPan_MenuMouseReadCount() - mm_grid_read > 1)
        return 0;

    CSR_AXIS *a = &g->axis[mm_grid_axis];
    const int step = mm_grid_pos > a->cur ? 1 : -1;

    a->cur = mm_grid_pos;
    *axis = mm_grid_axis;
    return step;
}

void MenuMouseCsrClaim(const CSR_ITEM *item)
{
    ReiPan_MenuMouseBegin(item->h > 1   ? REIPAN_MM_WHEEL_VERTICAL
                          : item->w > 1 ? REIPAN_MM_WHEEL_HORIZONTAL
                                        : 0,
                          nullptr);
}

void MenuMouseCsrApply(CSR_ITEM *item, CSR_VIEW *vx, CSR_VIEW *vy)
{
    if (mm_pending_item != item)
        return;
    mm_pending_item = nullptr;
    if (ReiPan_MenuMouseReadCount() - mm_pending_read > 1)
        return;

    // Set by item id, as the screens do. The views stay where they are: the
    // cell was on screen to be pointed at, so it is a place within the view.
    const short vx_top = vx->top;
    const short vy_top = vy->top;

    if (CsrItemSetId(item, mm_pending_cell) == -1)
        return;
    CsrViewSetPosX(item, vx, (short)(item->x - vx_top));
    CsrViewSetPosY(item, vy, (short)(item->y - vy_top));
    item->flags |= CSR_EV_MOVED;
}
