// FILE: ingame/subtitle/subtitle.c
//
// Spoken-line captions: the lines the event script, the NPCs, the telephone,
// the tapes and the ghosts' voices put on screen (FF2 subtitle.c, ported from
// MioPan and rewritten to FF3).
//
// One caption at a time. A request looks its row up in the subtitle file,
// stops whatever was running, queues the voice stream (StreamAutoPreload, at
// priority 0xe) and steps through the row's lines with the frame counter:
// PRELOAD waits on the CD, EXEC draws the current line and advances it. A
// row's line list ends in one of FF3's markers -- -1 holds a blank caption
// until the stream ends, -3 ends the caption at once, -4 loops it -- and the
// stream ending always ends the caption.
//
// FF3 against FF2: the file has five tables (SUBTITLE_KIND) instead of one,
// each drawn from its own message bank; rows may have no stream at all (the
// lines then run on the counter alone); a -2 line is a timed blank; the
// mouth anime (SubTitleMimReq / Stop) is gone; SubTitleCheckDispAccept also
// waits for message pages and layout animes; the header file is the stream
// + 1 (FF2: - 1); the ctrl grew Kind and its init clears ObjType / ObjId.
// The counter now counts up before the comparison, so a line of Frame
// frames is shown Frame frames (FF2 showed Frame + 1).

#include "ingame/subtitle/subtitle.h"

#include "common/heapctrl.h"                    // heapCtrlMalloc / Free
#include "graphics/graph2d/message.h"           // MsgIsPageWait
#include "graphics/movie/movie_title.h"         // MovieTitleDispMain
#ifdef REIPAN_SLPS_25544_CHS
#include "graphics/movie/slps_captions.h"
#endif
#include "ingame/event/prg/ev_chapter.h"        // GetObjectPos, EvChapterDispIsActive
#include "ingame/ingame_wrk.h"                  // ingame_wrk
#include "ingame/menu/anim_2d.h"                // LayoutAnimIsAnyVisible
#include "ingame/plyr/player.h"                 // PLYR_WRK, PLYR_FLG_800000
#include "system/eeiop/cd_file_dat.h"           // SUBTITLE_OBJ
#include "system/eeiop/cddat.h"                 // GetFileSize
#include "system/eeiop/snd_3d.h"                // SND_3D_SET
#include "system/eeiop/stream_auto.h"           // StreamAuto*
#include "system/os/eecdvd.h"                   // LoadReq
#include "system/os/sys_heap.h"                 // GetSystemHeapWrkP
#include "system/os/system.h"                   // GetPALMode

#include <string.h>                             // memcpy
#include <stdio.h>                              // printf
#include <stdlib.h>                             // getenv

// MovieTitleDispMain's position and colour for every caption.
#define SUBTITLE_DISP_Y         0x17c
#define SUBTITLE_MSG_COL        1
#define SUBTITLE_ALPHA          0x80

#ifdef REIPAN_SLPS_25544_CHS
// Fits two native caption lines above the gameplay dialogue window.
#define SUBTITLE_ABOVE_DIALOG_Y 0x120
#endif

// Voice streams: priority 0xe, full volume.
#define SUBTITLE_STREAM_PRIORITY    0xe
#define SUBTITLE_STREAM_VOL         0x3200

// The message bank of each kind (rodata 0x363018).
#ifdef REIPAN_SLPS_25544_CHS
static const int subtitle_msg_type[SUBTITLE_KIND_MAX] = { 0x72, 0x72, 0x72, 0x7f, 0x72 };
#else
static const int subtitle_msg_type[SUBTITLE_KIND_MAX] = { 0x7d, 0x7d, 0x7d, 0x80, 0x7d };
#endif

// The number of rows in each kind's table (rodata 0x363030).
static const int subtitle_data_num[SUBTITLE_KIND_MAX] = { 45, 40, 12, 7, 108 };

static int  SubTitleCheckDispAccept(void);
static void SubTitleReqSub(int kind, int no, int stream_id, int obj_type, int obj_id, float *pos);

static bool SubTitleTraceEnabled(void)
{
    static const bool enabled = getenv("REIPAN_TRACE_SUBTITLES") != nullptr;
    return enabled;
}

SUBTITLE_CTRL SubTitleCtrl;                     // sbss 0x3fd040
void *subtitle_data;                            // sdata 0x372604
static u_int subtitle_data_size;

// FF3 0x002590e0.

void SubTitleDataLoadReq(void)
{
    const u_int size = GetFileSize(SUBTITLE_OBJ);
    if (size < sizeof(SUBTITLE_FILE_HEAD) || size == (u_int)-1)
        return;
    subtitle_data = heapCtrlMalloc(GetSystemHeapWrkP(), size, 0, 0, 0);
    if (subtitle_data != nullptr)
    {
        if (LoadReq(SUBTITLE_OBJ, subtitle_data) < 0)
            SubTitleDataFree();
        else
            subtitle_data_size = size;
    }
}

// FF3 0x00259148.

void SubTitleDataFree(void)
{
    subtitle_data_size = 0;
    SubTitleCtrlStructInit();
    if (subtitle_data != nullptr)
    {
        heapCtrlFree(GetSystemHeapWrkP(), subtitle_data, 0, 0, 0);
        subtitle_data = nullptr;
    }
}

// Row no of table kind, nullptr when either is out of range (FF2
// SubTitleDataPtrGet; FF3 inlines it into its two callers).
// PORT: the offsets are added to the host pointer, not as 32-bit integers.

static SUBTITLE_DATA *SubTitleDataPtrGet(int kind, int no)
{
    if ((u_int)kind >= SUBTITLE_KIND_MAX || no < 0 || no >= subtitle_data_num[kind] ||
        subtitle_data == nullptr || subtitle_data_size < sizeof(SUBTITLE_FILE_HEAD))
        return nullptr;

    auto *top = (u_char *)subtitle_data;
    auto *head = (const SUBTITLE_FILE_HEAD *)top;
    const u_int table = head->KindTbl[kind];
    const u_int kind_end = kind + 1 < SUBTITLE_KIND_MAX ?
        head->KindTbl[kind + 1] : subtitle_data_size;
    const u_int table_bytes = subtitle_data_num[kind] * sizeof(u_int);
    if (table < sizeof(SUBTITLE_FILE_HEAD) || table % alignof(u_int) != 0 ||
        kind_end > subtitle_data_size || kind_end < table || kind_end - table < table_bytes)
        return nullptr;

    const auto *offsets = (const u_int *)(top + table);
    const u_int first = offsets[no];
    u_int end = kind_end;
    // Missing rows in the compatibility table can share one empty record.
    for (int next = no + 1; next < subtitle_data_num[kind]; next++)
    {
        if (offsets[next] == first)
            continue;
        if (offsets[next] < first || offsets[next] > kind_end)
            return nullptr;
        end = offsets[next];
        break;
    }
    if (first < table + table_bytes || first % alignof(SUBTITLE_DATA) != 0 ||
        first > end || end - first < sizeof(SUBTITLE_DATA))
        return nullptr;

    for (u_int pos = first + offsetof(SUBTITLE_DATA, MsgData);
         pos <= end - sizeof(SUBTITLE_MSG_DATA); pos += sizeof(SUBTITLE_MSG_DATA))
    {
        const auto *line = (const SUBTITLE_MSG_DATA *)(top + pos);
        if (line->MsgId == SUBTITLE_MSG_HOLD || line->MsgId == SUBTITLE_MSG_END ||
            line->MsgId == SUBTITLE_MSG_LOOP)
            return (SUBTITLE_DATA *)(top + first);
    }
    return nullptr;
}

// FF3 0x00259190.

void SubTitleCtrlStructInit(void)
{
    SUBTITLE_CTRL *ctrl = &SubTitleCtrl;

    ctrl->pSubTitleData = nullptr;
    ctrl->StreamId      = -1;
    ctrl->Status        = SUBTITLE_STATUS_IDLE;
    ctrl->Counter       = 0;
    ctrl->MsgDataNo     = 0;
    ctrl->ObjType       = 0;
    ctrl->ObjId         = 0;
    ctrl->Kind          = -1;
}

// FF2 SubTitleUpdateStreamPosition, inlined: a voice bound to an object
// follows it (only SubTitleReq3DObj leaves ObjType / ObjId set).

static void SubTitleUpdateStreamPosition(int stream_id, int obj_type, int obj_id)
{
    sceVu0FVECTOR pos;

    if (stream_id == -1 || obj_type == -1 || obj_id == -1)
    {
        return;
    }
    if (GetObjectPos(pos, (u_char)obj_type, obj_id) != 0)
    {
        StreamAutoSetPosition(stream_id, pos);
    }
}

// FF3 0x002591c0.
// callcheck: GetObjectPos, StreamAutoSetPosition (in SubTitleUpdateStreamPosition, which FF3 inlines)

void SubTitleMain(int subtitle_on, int check_disp)
{
    SUBTITLE_CTRL *ctrl = &SubTitleCtrl;

#ifdef REIPAN_SLPS_25544_CHS
    // Leave rows with authored captions, including the tape pauses, to the disc table.
    const bool audio_only = ctrl->Status == SUBTITLE_STATUS_IDLE ||
        (ctrl->Status == SUBTITLE_STATUS_EXEC && ctrl->pSubTitleData != nullptr &&
         ctrl->pSubTitleData->MsgData[0].MsgId == SUBTITLE_MSG_HOLD);
    if (subtitle_on && audio_only)
    {
        const bool dialogue = check_disp != 0 && MsgIsPageWait() != 0;
        SlpsDrawVoiceCaption(dialogue ? SUBTITLE_ABOVE_DIALOG_Y : SUBTITLE_DISP_Y, SUBTITLE_MSG_COL);
    }
#endif

    if (ctrl->Status == SUBTITLE_STATUS_PRELOAD)
    {
        if (ctrl->StreamId != -1)
        {
            if (StreamAutoIsPreload(ctrl->StreamId) == 0)
            {
                return;
            }
            StreamAutoPreloadPlay(ctrl->StreamId);
        }
        ctrl->Status = SUBTITLE_STATUS_EXEC;
        return;
    }
    if (ctrl->Status != SUBTITLE_STATUS_EXEC)
    {
        return;
    }

    SUBTITLE_DATA *data = ctrl->pSubTitleData;
    if (data == nullptr || (u_int)ctrl->Kind >= SUBTITLE_KIND_MAX)
    {
        SubTitleCtrlStructInit();
        return;
    }
    SUBTITLE_MSG_DATA *msg = &data->MsgData[ctrl->MsgDataNo];

    // The stream ending ends the caption, wherever its lines are.
    if (ctrl->StreamId != -1 && StreamAutoIsPlaying(ctrl->StreamId) == 0)
    {
        SubTitleCtrlStructInit();
        return;
    }

    // GetObjectPos / StreamAutoSetPosition: inlined in the ROM.
    SubTitleUpdateStreamPosition(ctrl->StreamId, ctrl->ObjType, ctrl->ObjId);

    if (msg->MsgId == SUBTITLE_MSG_END || msg->MsgId == SUBTITLE_MSG_LOOP)
    {
        SubTitleCtrlStructInit();
        return;
    }
    // Held: the counter stops, the stream finishes on its own.
    if (msg->MsgId == SUBTITLE_MSG_HOLD)
    {
        return;
    }

    if (msg->MsgId >= 0)
    {
        const bool visible = check_disp == 0 || SubTitleCheckDispAccept() != 0;
        if (ctrl->Counter == 0 && SubTitleTraceEnabled())
            printf("[subtitle] kind=%d bank=%02x id=%d frames=%d stream=%d visible=%d enabled=%d\n",
                   ctrl->Kind, subtitle_msg_type[ctrl->Kind], msg->MsgId, msg->Frame, ctrl->StreamId,
                   visible, subtitle_on);
        if (visible)
            MovieTitleDispMain(subtitle_msg_type[ctrl->Kind], msg->MsgId, SUBTITLE_DISP_Y,
                               SUBTITLE_MSG_COL, 1, SUBTITLE_ALPHA, subtitle_on);
    }

    // The frame counts are authored at 60 Hz: PAL holds a line 5/6 as long.
    ctrl->Counter++;
    if (GetPALMode() != 0)
    {
        if (ctrl->Counter >= msg->Frame * 5 / 6)
        {
            ctrl->Counter = 0;
            ctrl->MsgDataNo++;
        }
    }
    else
    {
        if (ctrl->Counter >= msg->Frame)
        {
            ctrl->Counter = 0;
            ctrl->MsgDataNo++;
        }
    }

    switch (data->MsgData[ctrl->MsgDataNo].MsgId)
    {
        case SUBTITLE_MSG_LOOP:
            ctrl->MsgDataNo = 0;
            ctrl->Counter   = 0;
            break;

        case SUBTITLE_MSG_END:
            SubTitleCtrlStructInit();
            break;
    }
}

// The one request path. stream_id >= 0: the caller owns the stream and the
// caption rides on it; else the row's stream is queued here, at pos when
// given (obj_type / obj_id then keep it on that object), and a row without a
// stream runs its lines on the counter alone.
// FF3 0x002bf0b8 (FF2's static, emitted out of line).

static void SubTitleReqSub(int kind, int no, int stream_id, int obj_type, int obj_id, float *pos)
{
    SUBTITLE_CTRL *ctrl = &SubTitleCtrl;
    SUBTITLE_DATA *data = SubTitleDataPtrGet(kind, no);
    if (SubTitleTraceEnabled())
        printf("[subtitle] request kind=%d row=%d file=%04x valid=%d\n",
               kind, no, data == nullptr ? -1 : data->StreamFile, data != nullptr);
    if (data == nullptr)
    {
        return;
    }

    // Anything running is stopped, its stream included.
    if (ctrl->StreamId == -1)
    {
        SubTitleCtrlStructInit();
    }
    else
    {
        SubTitleStop();
    }
    ctrl->pSubTitleData = data;

    if (stream_id >= 0)
    {
        ctrl->StreamId = stream_id;
        ctrl->ObjType  = -1;
        ctrl->ObjId    = -1;
    }
    else if (pos != nullptr)
    {
        SND_3D_SET set = { pos, nullptr, nullptr };

        if (data->StreamFile < 0)
        {
            ctrl->StreamId = -1;
        }
        else
        {
            ctrl->StreamId = StreamAutoPreload(data->StreamFile, data->StreamFile + 1,
                                               SUBTITLE_STREAM_PRIORITY, 0, 0,
                                               SUBTITLE_STREAM_VOL, 0, &set);
        }
        ctrl->ObjType = obj_type;
        ctrl->ObjId   = obj_id;
    }
    else
    {
        if (data->StreamFile < 0)
        {
            ctrl->StreamId = -1;
        }
        else
        {
            ctrl->StreamId = StreamAutoPreload(data->StreamFile, data->StreamFile + 1,
                                               SUBTITLE_STREAM_PRIORITY, 0, 0,
                                               SUBTITLE_STREAM_VOL, 0, nullptr);
        }
        ctrl->ObjType = -1;
        ctrl->ObjId   = -1;
    }

    ctrl->Kind      = kind;
    ctrl->Status    = SUBTITLE_STATUS_PRELOAD;
    ctrl->Counter   = 0;
    ctrl->MsgDataNo = 0;
}

// FF3 0x00259430.

void SubTitleReq(int kind, int no)
{
    SubTitleReqSub(kind, no, -1, -1, -1, nullptr);
}

// The point is copied: the caller's vector may be a script operand.
// FF3 0x00259458.

void SubTitleReq3D(int kind, int no, float *pos)
{
    sceVu0FVECTOR tmp;

    memcpy(tmp, pos, sizeof(tmp));
    SubTitleReqSub(kind, no, -1, -1, -1, tmp);
}

// Nothing when the object is not found.
// FF3 0x00259490.

void SubTitleReq3DObj(int kind, int no, int obj_type, int obj_id)
{
    sceVu0FVECTOR tmp;

    if (GetObjectPos(tmp, (u_char)obj_type, obj_id) != 0)
    {
        SubTitleReqSub(kind, no, -1, obj_type, obj_id, tmp);
    }
}

// FF2 SubTitleReqAutoEnemy.
// FF3 0x00259508.

void SubTitleReqEne(int no, int stream)
{
    SubTitleReqSub(SUBTITLE_KIND_ENEMY, no, stream, -1, -1, nullptr);
}

// An immediate fade-out of the voice and a cleared slot; nothing when idle
// (FF2 stopped unconditionally).
// FF3 0x00259538.

void SubTitleStop(void)
{
    SUBTITLE_CTRL *ctrl = &SubTitleCtrl;

    if (SubTitleIsEnd() == 0)
    {
        StreamAutoFadeOut(ctrl->StreamId, 0);
        SubTitleCtrlStructInit();
    }
}

// FF3 0x002595a0.

int SubTitleIsEnd(void)
{
    return SubTitleCtrl.Status == SUBTITLE_STATUS_IDLE;
}

// FF2 SubTitleStreamFileNoGet.
// PC: the ROM reads the halfword at address 0 for a row out of range; -1
// (no stream) here.
// FF3 0x002595b0.

int SubTitleGetStreamFile(int kind, int no)
{
    SUBTITLE_DATA *data = SubTitleDataPtrGet(kind, no);

    if (data == nullptr)
    {
        return -1;
    }
    return data->StreamFile;
}

// FF3 0x00259610.

int SubTitleGetMsgId(void)
{
    SUBTITLE_CTRL *ctrl = &SubTitleCtrl;

    if ((u_int)ctrl->Kind < SUBTITLE_KIND_MAX && ctrl->Status == SUBTITLE_STATUS_EXEC)
    {
        return ctrl->pSubTitleData->MsgData[ctrl->MsgDataNo].MsgId;
    }
    return SUBTITLE_MSG_BLANK;
}

// No callers in the ROM.
// FF3 0x00259658.

int SubTitleGetCounter(void)
{
    return SubTitleCtrl.Counter;
}

// FF3 0x00259668.

int SubTitleIsExec(void)
{
    return SubTitleCtrl.Status == SUBTITLE_STATUS_EXEC;
}

// Whether the caption may be drawn this frame: not while a message page
// waits, the viewfinder is up, the chapter title shows or a layout anime is
// visible.
// FF3 0x002bf058 (FF2's static, emitted out of line).

static int SubTitleCheckDispAccept(void)
{
    if (MsgIsPageWait() != 0)
    {
        return 0;
    }
    if ((ingame_wrk.plyr_wrk->flags & PLYR_FLG_800000) != 0)
    {
        return 0;
    }
    if (EvChapterDispIsActive() != 0)
    {
        return 0;
    }
    return LayoutAnimIsAnyVisible() == 0;
}
