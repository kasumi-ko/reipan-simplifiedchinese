/* ==========================================================================
 *  ingame/radio.c
 *
 *  The radios (see radio.h). FF3-only, FF3 0x24cf18..0x24da20.
 *
 *  FF3 0x0024d9e8 is a compiler atexit thunk (GPhaseCtrlTerm(0x41e468), the
 *  GPhaseCtrl singleton's destructor, this file's copy); the host runs the
 *  static's destructor itself. FF3 0x0024cf18 is the base-object copy of the
 *  constructor FF3 0x0024cf60 (same body, no callers).
 * ======================================================================== */
#include "ingame/radio.h"

#include <math.h>                               // sqrtf

#include "graphics/movie/movie_title.h"         // MovieTitleDispMain
#include "ingame/event/prg/ev_chapter.h"        // EvChapterDispIsActive
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/menu/anim_2d.h"                // LayoutAnimIsAnyVisible
#include "ingame/plyr/player.h"                 // PLYR_WRK, PlyrGetCurWrk, PLYR_FLG_800000
#include "ingame/sound/snd_ctrl.h"              // SndSe*
#include "ingame/story_menu.h"                  // IsLockTblOn
#include "ingame/subtitle/subtitle.h"           // SubTitleIsEnd
#include "common/vu0math.h"                     // RotVectorY
#include "main/gphase.h"                        // GetGPhaseCtrl

#define RADIO_NOISE_BANK    0x1b75              // the static every radio plays under its broadcast
#define RADIO_LOCK          0x1a                // SET_RADIO_LOCK_FLG's lock table id

// data 0x362060
static const RADIO_DAT radio_dat[RADIO_DAT_NUM] = {
    {  0,  0, 0x1b870000, 1, 1000.0f, {   87.0f,    0.0f,  371.0f, 1.0f } },
    {  1,  1, 0x1b890000, 0,  200.0f, {   88.0f,  -30.0f,  357.0f, 1.0f } },
    {  2,  2, 0x1b8b0000, 0,  200.0f, {  137.0f,  -20.0f,   30.0f, 1.0f } },
    {  3,  3, 0x1b8d0000, 0,  300.0f, {  100.0f,  -10.0f,  100.0f, 1.0f } },
    {  4,  4, 0x1b410000, 0,  400.0f, {   80.0f,  -10.0f,  200.0f, 1.0f } },
    {  5,  5, 0x1b750000, 0,  500.0f, {  150.0f,  -10.0f,  300.0f, 1.0f } },
    {  6,  6, 0x1b8f0000, 0,  280.0f, {  117.0f,  -10.0f, 1386.0f, 1.0f } },
    {  7,  7, 0x1b910000, 0,  280.0f, {  131.0f, -135.0f, 1190.0f, 1.0f } },
    {  8,  8, 0x1b930000, 0,  280.0f, {  388.0f,  -17.0f, 1159.0f, 1.0f } },
    {  9,  9, 0x1b950000, 0,  280.0f, { -147.0f,  -29.0f,  853.0f, 1.0f } },
    { 10, 10, 0x1b970000, 0,  280.0f, { -192.0f,  -30.0f,  958.0f, 1.0f } },
    { 11, 11, 0x1b870000, 0,  500.0f, { -115.0f,  -15.0f,  100.0f, 1.0f } },
    { 12, 12, 0x1b8d0000, 0,  500.0f, {  487.0f,  -86.0f,  929.0f, 1.0f } },
    { 13, 13, 0x1b890000, 0,  500.0f, {  337.0f,  -30.0f,  496.0f, 1.0f } },
    { 14, -1, 0x1b870000, 1,  500.0f, {  153.0f,  -69.0f,   72.0f, 1.0f } },
    { 15, -1, 0x1b8b0000, 0,  500.0f, {  -49.0f,  -85.0f,  392.0f, 1.0f } },
    { 16, -1, 0x1b910000, 1,  350.0f, {  219.0f,  -72.0f,  121.0f, 1.0f } },
    { 17, -1, 0x1b910000, 1,  500.0f, {  102.0f, -104.0f,  166.0f, 1.0f } },
    { 18, -1, -1,         1,  350.0f, {   11.0f,  -90.0f,   78.0f, 1.0f } },
};

static Radio *radio_slot[RADIO_SLOT_NUM];       // bss 0x3f7a50
static short  radio_save[RADIO_SLOT_NUM];       // bss 0x3f7a68 (save_game_data entry 59)

// ──────────────────────────────────────────────────────────────────────
// One radio.
// ──────────────────────────────────────────────────────────────────────

// Starts asleep, so Resume loads its banks at once.
// FF3 0x0024cf60
Radio::Radio(short no)
{
    dist_rate = 0.0f;
    face_rate = 0.0f;
    noise     = -1;
    this->no  = no;
    asleep    = 1;
    voice     = -1;
    vol       = 0.0f;
    loudest   = 0;
    Resume();
}

// FF3 0x0024cfc0
Radio::~Radio()
{
    Suspend();
}

// FF3 0x0024cfd8
void Radio::BankLoad()
{
    if (radio_dat[no].se >= 0)
        SndSeBankLoad((u_int)radio_dat[no].se >> 16);
    SndSeBankLoad(RADIO_NOISE_BANK);
}

// FF3 0x0024d018
void Radio::BankRelease()
{
    if (voice >= 0)
    {
        SndSeFadeStop(voice, 0);
        voice = -1;
    }
    if (noise >= 0)
    {
        SndSeFadeStop(noise, 0);
        noise = -1;
    }
    if (radio_dat[no].se >= 0)
        SndSeBankRelease((u_int)radio_dat[no].se >> 16, 0);
    SndSeBankRelease(RADIO_NOISE_BANK, 0);
}

// FF3 0x0024d0a0
void Radio::Suspend()
{
    if (asleep == 0)
    {
        asleep = 1;
        BankRelease();
    }
}

// FF3 0x0024d0d0
void Radio::Resume()
{
    if (asleep != 0)
    {
        asleep = 0;
        BankLoad();
    }
}

// 1 at the radio, falling to 0 at its range (3D distance from listen).
// Clears the facing rate; 0 while asleep.
// FF3 0x0024d0f8
float Radio::CalcDistRate(const float *listen)
{
    const float *pos = radio_dat[no].pos;
    float dx, dy, dz, dist;

    if (asleep != 0)
        return 0.0f;

    dx   = listen[0] - pos[0];
    dy   = listen[1] - pos[1];
    dz   = listen[2] - pos[2];
    dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist <= 0.0f)
    {
        dist_rate = 1.0f;
    }
    else
    {
        dist_rate = (radio_dat[no].range - dist) / radio_dat[no].range;
        if (dist_rate < 0.0f)
            dist_rate = 0.0f;
    }
    face_rate = 0.0f;
    return dist_rate;
}

// 0.5 with the radio behind the player .. 1 straight ahead: the dot of the
// player's facing (rot[1] turning +z) with the direction to the radio.
// FF3 0x0024d1a8
float Radio::CalcFaceRate(const float *listen, const float *rot)
{
    static const sceVu0FVECTOR z_axis = { 0.0f, 0.0f, 1.0f, 0.0f };   // data 0x329460
    const float  *pos = radio_dat[no].pos;
    sceVu0FVECTOR dir;
    sceVu0FVECTOR face;
    float         q;

    if (asleep != 0)
        return 0.0f;

    // vsub; vrsqrt of the xyz length: the unit direction to the radio.
    dir[0] = pos[0] - listen[0];
    dir[1] = pos[1] - listen[1];
    dir[2] = pos[2] - listen[2];
    dir[3] = pos[3] - listen[3];
    q      = Vu0Rsqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);   // PC: PS2 rsqrt
    dir[0] *= q;
    dir[1] *= q;
    dir[2] *= q;

    RotVectorY(rot[1], face, (float *)z_axis);
    face_rate = (face[0] * dir[0] + face[1] * dir[1] + face[2] * dir[2] + 1.0f) * 0.25f + 0.5f;
    return face_rate;
}

// Starts the broadcast and the static once their banks are in and sets
// both volumes: the broadcast vol * 100, the static rising with the
// distance rate up to 0.8 (full beyond), times the facing rate.
// FF3 0x0024d298
void Radio::Play(int loudest)
{
    float noise_rate;

    if (asleep != 0)
        return;

    if (voice < 0 && radio_dat[no].se >= 0
        && SndSeBankIsLoaded((u_int)radio_dat[no].se >> 16) != 0)
    {
        voice = SndSePlay((u_int)radio_dat[no].se, 0, 0x1000, 0, nullptr, 0);
    }
    if (noise < 0 && SndSeBankIsLoaded(RADIO_NOISE_BANK) != 0)
        noise = SndSePlay(RADIO_NOISE_BANK << 16, 0, 0x1000, 0, nullptr, 0);

    this->loudest = loudest;
    vol           = dist_rate * face_rate;
    noise_rate    = 1.0f;
    if (dist_rate < 0.8f)
        noise_rate = dist_rate / 0.8f;
    noise_rate *= 100.0f;

    if (voice >= 0)
        SndSeVolFade(voice, (int)(vol * 100.0f), 0);
    if (noise >= 0)
        SndSeVolFade(noise, (int)(noise_rate * face_rate), 0);
}

// The caption of the loudest radio while it is heard above 0.25, fading
// in up to 0.5.
// FF3 0x0024d408
void Radio::DrawCaption()
{
    u_char alpha;
    int caption = radio_dat[no].caption;

    if (caption < 0 || loudest == 0 || !(vol > 0.25f))
        return;

#ifdef REIPAN_SLPS_25544_CHS
    // The same broadcasts have authored text at their alternate placements.
    if (caption < 6)
    {
        static constexpr int captions[] = {11, 13, -1, 12, -1, -1};
        caption = captions[caption];
        if (caption < 0)
            return;
    }
#endif
    alpha = 0x80;
    if (vol < 0.5f)
        alpha = (u_char)(int)((vol - 0.25f) * 4.0f * 128.0f);
    MovieTitleDispMain(0x7c, caption, 0x17c, 0xc, 1, alpha, 1);
}

// ──────────────────────────────────────────────────────────────────────
// The slots.
// ──────────────────────────────────────────────────────────────────────

// FF3 0x0024da08
static void RadioWrkInit(void)
{
}

// FF3 0x0024d918
static void RadioDispInit(void)
{
}

// FF3 0x0024d920
static void RadioDispEnd(void)
{
}

// FF3 0x0024d4c8
void RadioInit(void)
{
    int i;

    RadioWrkInit();
    RadioDispInit();
    for (i = 0; i < RADIO_SLOT_NUM; i++)
        radio_slot[i] = nullptr;
}

// FF3 0x0024d518
void RadioEnd(void)
{
    int i;

    RadioTurnOff(-1);
    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        if (radio_slot[i] != nullptr)
        {
            delete radio_slot[i];       // callcheck: RadioDtor, operator_delete (the delete expression)
            radio_slot[i] = nullptr;
        }
    }
    RadioDispEnd();
}

// Every radio of the current world measures how well the player hears it
// (their listen position and facing); the loudest one shows its caption.
// The others, and all under the radio lock, are suspended.
// FF3 0x0024d590
void RadioMain(void)
{
    PLYR_WRK     *plyr = PlyrGetCurWrk();
    sceVu0FVECTOR listen;
    sceVu0FVECTOR rot;
    float         best_rate = 0.0f;
    float         rate;
    int           best = -1;
    int           i;

    listen[0] = plyr->listen_pos[0];
    listen[1] = plyr->listen_pos[1];
    listen[2] = plyr->listen_pos[2];
    listen[3] = plyr->listen_pos[3];
    rot[0]    = plyr->rot[0];
    rot[1]    = plyr->rot[1];
    rot[2]    = plyr->rot[2];
    rot[3]    = plyr->rot[3];

    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        Radio *radio = radio_slot[i];

        if (radio == nullptr)
            continue;
        if (IsLockTblOn(RADIO_LOCK) == 0 && (s_char)ingame_wrk.b03 == radio_dat[radio->no].world)
        {
            radio->Resume();
            rate = radio->CalcDistRate(listen);
            if (0.0f < rate)
                rate *= radio->CalcFaceRate(listen, rot);
            if (best_rate < rate)
            {
                best      = i;
                best_rate = rate;
            }
        }
        else
        {
            radio->Suspend();
        }
    }

    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        if (radio_slot[i] != nullptr)
            radio_slot[i]->Play(best == i);
    }
}

// The captions, unless the radio lock, phase 0x21, the viewfinder, a
// chapter title, a layout animation or a subtitle is up.
// FF3 0x0024d718
void RadioDraw(void)
{
    static GPHASE_CTRL *gphase_ctrl;            // FF3: the shared inline cache (sdata gp-0xca4)
    int i;

    if (IsLockTblOn(RADIO_LOCK) != 0)
        return;
    if (gphase_ctrl == nullptr)
        gphase_ctrl = GetGPhaseCtrl();
    if (gphase_ctrl->leaf->id == 0x21)
        return;
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
        return;
    if (EvChapterDispIsActive() != 0 || LayoutAnimIsAnyVisible() != 0 || SubTitleIsEnd() == 0)
        return;

    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        if (radio_slot[i] != nullptr)
            radio_slot[i]->DrawCaption();
    }
}

// FF3 0x0024d7f0
int RadioTurnOn(short no)
{
    int i;

    if ((u_short)no >= RADIO_DAT_NUM)
        return 0;
    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        if (radio_slot[i] == nullptr)
        {
            radio_slot[i] = new Radio(no);      // callcheck: operator_new, RadioCtor (the new expression)
            return 1;
        }
        if (radio_slot[i]->no == no)
            return 0;
    }
    return 0;
}

// FF3 0x0024d890
void RadioTurnOff(short no)
{
    int i;

    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        Radio *radio = radio_slot[i];

        if (radio != nullptr && (no < 0 || radio->no == no))
        {
            delete radio;               // callcheck: RadioDtor, operator_delete (the delete expression)
            radio_slot[i] = nullptr;
        }
    }
}

// FF3 0x0024d928
void RadioSaveStore(void)
{
    int i;

    for (i = 0; i < RADIO_SLOT_NUM; i++)
        radio_save[i] = radio_slot[i] == nullptr ? -1 : radio_slot[i]->no;
}

// FF3 0x0024d978
void RadioLoadRestore(void)
{
    int i;

    for (i = 0; i < RADIO_SLOT_NUM; i++)
    {
        if ((u_short)radio_save[i] < RADIO_DAT_NUM)
            RadioTurnOn(radio_save[i]);
    }
}

// FF3 0x0024d9d0
int RadioSaveGet(void **addr)
{
    *addr = radio_save;
    return sizeof(radio_save);
}
