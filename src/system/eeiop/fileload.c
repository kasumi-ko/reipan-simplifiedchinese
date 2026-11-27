/* ==========================================================================
 *  system/eeiop/fileload.c
 *
 *  EE-side asynchronous file loader (FF3 0x268e70..0x269fa0). A dedicated
 *  thread (thFileLoad) owns a queue of FILE_LOAD_WRK requests; each pass sorts
 *  it by priority (descending) and, within a priority, by start sector, then
 *  fires the tail request -- the lowest priority value, the most urgent -- at
 *  the IOP file server over SIF RPC (rpcFileLoadReqSub), waits for the
 *  transfer and, for a compressed file, the EE-side decode (cmp_eeiop.c).
 *
 *    * Queue    - FileLoadReqEE (raw, or decode for a compressed file),
 *                 FileLoadReqSPU, FileDecodeLoadReqEE, FileLoadReqEEWait.
 *    * Poll     - FileLoadIsEnd (by packed id), FileLoadIsEnd2 (by file and
 *                 buffer), AllFileLoadIsEnd.
 *    * Cancel   - FileLoadCancel[2/SPU2] (callback on withdrawal), the *Wait
 *                 variants (block until withdrawn) and FileLoadCancelAll.
 *    * Bring-up - FileLoadGetNeedSize / FileLoadInit (from ee_iopInitSub).
 *
 *  A request id is packed as (wrk.id << 16) | wrk.temp_id: id is a slot
 *  number that travels with the entry when slots are swapped, temp_id a
 *  rolling serial.
 *
 *  Ported from MioPan system/eeiop/fileload.c (FF2), checked against FF3:
 *  FF3 dropped every printf and assert, PrintFileLoadWrk, and the file name
 *  of the IOP command (the IOP reads by sector only); it added the
 *  "data image in use" flag that decides whether FileLoadReq asks the table
 *  for a start sector, and sizes a raw load with GetFileSize.
 *
 *  PC: there is no IOP file server. As in MioPan, rpcFileLoadReqSub hands
 *  each request to the platform's host loader (ReiPan_FileLoadServe), which
 *  reads -- and for a compressed file decodes -- it on the loader thread.
 * ======================================================================== */
#include "system/eeiop/fileload.h"

#include <eekernel.h>                   // threads / semaphores / FlushCache
#include <sifrpc.h>                     // sceSifClientData / sceSifBindRpc

#include <stdio.h>                      // printf (PC: load log)
#include <stdlib.h>                     // qsort

#include "common/utility2.h"            // GetAlignUp
#include "system/eeiop/cddat.h"         // GetFileStartSector / GetFileCmpSize / GetFileSize / cddatIsCmpFile
#include "system/eeiop/cmp_eeiop.h"     // EE-side decode of compressed files
#include "system/eeiop/ee_iop_q.h"      // ReqQueryLoadCancel

#include "reipan/io/reipan_fileload.h"  // PC: ReiPan_FileLoadServe

// ──────────────────────────────────────────────────────────────────────
// The per-request work block and the loader-wide state.

enum LOAD_CANCEL_TYPE
{
    LOAD_CANCEL_NONE   = 0,         // nothing pending
    LOAD_CANCEL_STORE  = 1,         // a more urgent request came in: stop, keep what was read
    LOAD_CANCEL_NORMAL = 2,         // FileLoadCancel*: drop it, fire cancel_func
    LOAD_CANCEL_WAIT   = 3,         // FileLoadCancelWait* / All: drop it, wake the master thread
};

// 0x28; the work array is FileLoadInit's wrk_buffer.
// layout: host -- buffer / func / arg are pointers.
struct FILE_LOAD_WRK
{
    /* 0x00 */ int                file_no;
    /* 0x04 */ int                start_sector;
    /* 0x08 */ char              *buffer;
    /* 0x0c */ FILE_LOAD_CALLBACK func;
    /* 0x10 */ int                type;         // FILE_LOAD_TYPE
    /* 0x14 */ int                priority;     // lower is served first
    /* 0x18 */ void              *arg;
    /* 0x1c */ int                read_size;    // bytes already read (a stored cancel)
    /* 0x20 */ int                size;         // bytes to read
    /* 0x24 */ u_short            temp_id;
    /* 0x26 */ short              id;
};
static_assert(offsetof(FILE_LOAD_WRK, file_no) == 0x00, "FILE_LOAD_WRK.file_no");
static_assert(offsetof(FILE_LOAD_WRK, start_sector) == 0x04, "FILE_LOAD_WRK.start_sector");

// bss 0x40e1c0.
// layout: host -- cancel_func / cancel_arg are pointers.
struct FILE_LOAD_SYS
{
    /* 0x00 */ int                yet_files;            // requests still queued
    /* 0x04 */ int                cancel_type;          // LOAD_CANCEL_TYPE
    /* 0x08 */ FILE_LOAD_CALLBACK cancel_func;          // fired when a cancel completes
    /* 0x0c */ void              *cancel_arg;
    /* 0x10 */ int                master_thread_id;     // FileLoadInit's caller
    /* 0x14 */ int                ring_buf_num;         // IOP ring buffer: segments
    /* 0x18 */ int                one_buf_size;         // IOP ring buffer: segment size
    /* 0x1c */ int                file_load_wrk_max;
    /* 0x20 */ int                load_th_priority;
    /* 0x24 */ int                decode_th_priority;
    /* 0x28 */ int                use_cd_image;         // FF3: EEIOP_DEF.cd_dat_name set
};
static_assert(offsetof(FILE_LOAD_SYS, yet_files) == 0x00, "FILE_LOAD_SYS.yet_files");
static_assert(offsetof(FILE_LOAD_SYS, cancel_type) == 0x04, "FILE_LOAD_SYS.cancel_type");

// The IOP's reply to a load command. 0x8.
struct FILE_LOAD_RET
{
    /* 0x0 */ int read_size;
    /* 0x4 */ int cancel_flg;
};
static_assert(sizeof(FILE_LOAD_RET) == 0x8, "FILE_LOAD_RET size");

// ──────────────────────────────────────────────────────────────────────
// Statics.

static u_short          file_load_wrk_id;           // sbss gp+0x528: rolling temp_id serial
static FILE_LOAD_WRK   *file_load_wrk;              // sbss gp+0x52c: the request array
static int              load_th_idx;                // sbss gp+0x530: loader thread
static int              load_req_sema_id;           // sbss gp+0x534: counts queued requests
static int              rpc_sema_id;                // sbss gp+0x538: RPC completion
static FILE_LOAD_SYS    file_load_sys;              // bss 0x40e1c0
static FILE_LOAD_RET    iop_fileload_ret[8];        // bss 0x40e200: RPC receive buffer (0x40)
static LOAD_REQ_NEW     iop_fileload_cmd;           // bss 0x40e240: RPC send buffer (0x40)
static sceSifClientData sif_cli_data_f;             // bss 0x40e280: IOP file server binding
static char             file_load_stack[0x1000];    // bss 0x40d1c0: loader thread stack

static int  FileLoadReq(int file_no, char *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg, int type);
static void SwapFileLoadWrk(int one, int two);
static void thFileLoad(void *dummy);
static void intr_SifEnd_FileLoad(void *data);

// ──────────────────────────────────────────────────────────────────────
// Queue an SPU-RAM load (FF2 asserted the file is not compressed).
// FF3 0x00268e70

int FileLoadReqSPU(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_SPU);
}

// ──────────────────────────────────────────────────────────────────────
// Queue a raw EE load of file_no to adrs, or a decode load when the file is
// stored compressed. Returns the packed load id.
// FF3 0x00268e88

int FileLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    return FileLoadReq(file_no, (char *)adrs, priority, func, arg,
                       cddatIsCmpFile(file_no) != 0 ? FILE_LOAD_TYPE_DECODE_EE : FILE_LOAD_TYPE_EE);
}

// ──────────────────────────────────────────────────────────────────────
// Queue an explicit EE decode load.
// FF3 0x00268f18

int FileDecodeLoadReqEE(int file_no, void *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg)
{
    return FileLoadReq(file_no, (char *)adrs, priority, func, arg, FILE_LOAD_TYPE_DECODE_EE);
}

// ──────────────────────────────────────────────────────────────────────
// FileLoadReqEEWait's completion callback: wake the requesting thread.
// FF3 0x00268f30

static void _intr_wait_load_func(void *buffer, void *arg)
{
    (void)buffer;
    WakeupThread(*(int *)arg);
}

// ──────────────────────────────────────────────────────────────────────
// Queue a load at priority 10 and sleep until it finished.
// FF3 0x00268f48

void FileLoadReqEEWait(int file_no, void *adrs)
{
    int this_thread_id = GetThreadId();
    if (FileLoadReqEE(file_no, adrs, 10, _intr_wait_load_func, &this_thread_id) >= 0)
        SleepThread();
}

// ──────────────────────────────────────────────────────────────────────
// Withdraw slot i and block until it is gone. The tail is in flight: arm a
// waiting cancel and sleep until the loader thread drops it. Any other slot
// slides to the tail and is dropped here. The caller suspended the loader.
// FF3 0x00268f98

static void FileLoadCancelWaitSub(int i)
{
    if (i == file_load_sys.yet_files - 1)
    {
        file_load_sys.cancel_type = LOAD_CANCEL_WAIT;
        ReqQueryLoadCancel();
        if (file_load_wrk[i].type == FILE_LOAD_TYPE_DECODE_EE)
        {
            cmp_eeiopCancel();
        }
        ResumeThread(load_th_idx);
        SleepThread();
        return;
    }

    while (i < file_load_sys.yet_files - 1)
    {
        SwapFileLoadWrk(i, i + 1);
        i++;
    }
    WaitSema(load_req_sema_id);
    file_load_sys.yet_files--;
    ResumeThread(load_th_idx);
}

// ──────────────────────────────────────────────────────────────────────
// Cancel-and-wait by file and buffer (not SPU loads).
// FF3 0x00269078

FILE_LOAD_ENUM FileLoadCancelWait2(int file_no, void *adrs)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->file_no == file_no && wrk->buffer == (char *)adrs && wrk->type != FILE_LOAD_TYPE_SPU)
        {
            FileLoadCancelWaitSub(i);
            return FILE_LOAD_OK;
        }
    }
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel-and-wait by packed id.
// FF3 0x00269118

FILE_LOAD_ENUM FileLoadCancelWait(int id)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->id == (id >> 16) && wrk->temp_id == (u_int)(id & 0xffff))
        {
            FileLoadCancelWaitSub(i);
            return FILE_LOAD_OK;
        }
    }
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Withdraw slot i without waiting. The tail is in flight: arm a normal
// cancel that fires func(buffer, arg) once the loader drops it. Any other
// slot fires func now, slides to the tail and is dropped. Resumes the loader
// its caller suspended.
// FF3 0x002691a8

static void FileLoadCancelSub(int i, FILE_LOAD_CALLBACK func, void *arg)
{
    if (i == file_load_sys.yet_files - 1)
    {
        file_load_sys.cancel_type = LOAD_CANCEL_NORMAL;
        file_load_sys.cancel_func = func;
        file_load_sys.cancel_arg  = arg;
        ReqQueryLoadCancel();
        if (file_load_wrk[i].type == FILE_LOAD_TYPE_DECODE_EE)
        {
            cmp_eeiopCancel();
        }
    }
    else
    {
        if (func != nullptr)
        {
            func(file_load_wrk[i].buffer, arg);
        }
        while (i < file_load_sys.yet_files - 1)
        {
            SwapFileLoadWrk(i, i + 1);
            i++;
        }
        WaitSema(load_req_sema_id);
        file_load_sys.yet_files--;
    }
    ResumeThread(load_th_idx);
}

// ──────────────────────────────────────────────────────────────────────
// Cancel by file and buffer (not SPU loads).
// FF3 0x00269298

FILE_LOAD_ENUM FileLoadCancel2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->file_no == file_no && wrk->buffer == (char *)adrs && wrk->type != FILE_LOAD_TYPE_SPU)
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
    }
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel an SPU load by file and address.
// FF3 0x00269350

FILE_LOAD_ENUM FileLoadCancelSPU2(int file_no, void *adrs, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->file_no == file_no && wrk->buffer == (char *)adrs && wrk->type == FILE_LOAD_TYPE_SPU)
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
    }
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel by packed id.
// FF3 0x00269408

FILE_LOAD_ENUM FileLoadCancel(int id, FILE_LOAD_CALLBACK func, void *arg)
{
    FILE_LOAD_WRK *wrk;
    int            i;

    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->id == (id >> 16) && wrk->temp_id == (u_int)(id & 0xffff))
        {
            FileLoadCancelSub(i, func, arg);
            return FILE_LOAD_OK;
        }
    }
    ResumeThread(load_th_idx);
    return FILE_LOAD_ID_NOT_EXIST;
}

// ──────────────────────────────────────────────────────────────────────
// Cancel every queued load and sleep until the one in flight is dropped:
// slot 0 goes to the tail, the others are counted off the request sema, and
// the loader wakes this thread when its current transfer ends.
// FF3 0x002694b0

FILE_LOAD_ENUM FileLoadCancelAll(void)
{
    if (file_load_sys.yet_files == 0)
    {
        return FILE_LOAD_ALREADY_ALL_LOAD;
    }

    SuspendThread(load_th_idx);
    file_load_sys.cancel_type = LOAD_CANCEL_WAIT;
    SwapFileLoadWrk(0, file_load_sys.yet_files - 1);
    for (; file_load_sys.yet_files > 1; file_load_sys.yet_files--)
    {
        WaitSema(load_req_sema_id);
    }
    ReqQueryLoadCancel();
    if (file_load_wrk[0].type == FILE_LOAD_TYPE_DECODE_EE)
    {
        cmp_eeiopCancel();
    }
    ResumeThread(load_th_idx);
    SleepThread();
    return FILE_LOAD_OK;
}

// ──────────────────────────────────────────────────────────────────────
// qsort comparators: priority descending, then start sector descending (the
// tail, served first, is the lowest priority value and sector).
// FF3 0x00269570

static int cmpFileWrkPri(const void *a, const void *b)
{
    return ((const FILE_LOAD_WRK *)b)->priority - ((const FILE_LOAD_WRK *)a)->priority;
}

// FF3 0x00269580

static int cmpFileWrkSector(const void *a, const void *b)
{
    return ((const FILE_LOAD_WRK *)b)->start_sector - ((const FILE_LOAD_WRK *)a)->start_sector;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when no non-SPU load of file_no to adrs is queued.
// FF3 0x00269590

int FileLoadIsEnd2(int file_no, void *adrs)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            ret;

    ret = 1;
    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->file_no == file_no && wrk->buffer == (char *)adrs && wrk->type != FILE_LOAD_TYPE_SPU)
        {
            ret = 0;
        }
    }
    ResumeThread(load_th_idx);
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the load with this packed id is no longer queued.
// FF3 0x00269620

int FileLoadIsEnd(int id)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            ret;

    ret = 1;
    SuspendThread(load_th_idx);
    wrk = file_load_wrk;
    for (i = 0; i < file_load_sys.yet_files; i++, wrk++)
    {
        if (wrk->id == (id >> 16) && wrk->temp_id == (u_int)(id & 0xffff))
        {
            ret = 0;
        }
    }
    ResumeThread(load_th_idx);
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// 1 once the queue is empty.
// FF3 0x002696a0

int AllFileLoadIsEnd(void)
{
    int yet;

    SuspendThread(load_th_idx);
    yet = file_load_sys.yet_files;
    ResumeThread(load_th_idx);
    return yet < 1;
}

// ──────────────────────────────────────────────────────────────────────
// Work-buffer bytes FileLoadInit takes: the request array (64-aligned) plus
// the decode work when compressed files are on.
// FF3 0x002696e0

int FileLoadGetNeedSize(EEIOP_DEF *def)
{
    int size;

    size = (int)GetAlignUp(def->file_load_wrk_num * sizeof(FILE_LOAD_WRK), 6);
    if (def->cmp_use_flg != 0)
    {
        size += cmp_eeiopGetWrkSize();
    }
    return size;
}

// ──────────────────────────────────────────────────────────────────────
// Lay the request array (and the decode work) out of wrk_buffer, bind the
// IOP file server, create the semaphores and the loader thread -- one
// priority above its creator, the decode thread one below -- and return the
// next free byte of the buffer.
// FF3 0x00269738

void *FileLoadInit(EEIOP_DEF *def, void *wrk_buffer)
{
    struct SemaParam   semap;
    struct ThreadParam thp;
    char              *next;
    int                i;

    file_load_sys.file_load_wrk_max = def->file_load_wrk_num;
    file_load_wrk = (FILE_LOAD_WRK *)wrk_buffer;
    next = (char *)wrk_buffer + GetAlignUp(def->file_load_wrk_num * sizeof(FILE_LOAD_WRK), 6);

    // Each slot's fixed id.
    for (i = 0; i < file_load_sys.file_load_wrk_max; i++)
    {
        file_load_wrk[i].id = (short)i;
    }

    file_load_sys.yet_files        = 0;
    file_load_sys.master_thread_id = GetThreadId();
    file_load_sys.one_buf_size     = 0xa000;
    file_load_sys.cancel_type      = LOAD_CANCEL_NONE;
    file_load_sys.ring_buf_num     = 4;

    // FF3 spins here until the IOP's file service (RPC 3) has registered:
    //   do { if (bind < 0) for (;;); delay; } while (sif_cli_data_f.serve == NULL);
    // PC: bind once and go on. No request goes over SIF (rpcFileLoadReqSub
    // serves it on the host) and the IOP module registers no file service,
    // so serve would stay NULL for ever.
    if (sceSifBindRpc(&sif_cli_data_f, 3, 0) < 0)
    {
        for (;;)
        {
        }
    }

    semap.initCount = 0;
    semap.maxCount  = 1;
    rpc_sema_id = CreateSema(&semap);

    semap.initCount = 0;
    semap.maxCount  = file_load_sys.file_load_wrk_max;
    load_req_sema_id = CreateSema(&semap);

    if (def->cmp_use_flg != 0)
    {
        next = (char *)cmp_eeiopInit(next);
    }
    file_load_sys.use_cd_image = def->cd_dat_name != nullptr;

    ReferThreadStatus(file_load_sys.master_thread_id, &thp);
    file_load_sys.decode_th_priority = thp.currentPriority + 1;
    file_load_sys.load_th_priority   = thp.currentPriority - 1;

    thp.entry        = thFileLoad;
    thp.stack        = file_load_stack;
    thp.stackSize    = sizeof(file_load_stack);
    thp.gpReg        = nullptr;     // PC: FF3 passes &_gp; no $gp on the host
    thp.initPriority = file_load_sys.load_th_priority;
    thp.option       = 0;
    load_th_idx = CreateThread(&thp);
    StartThread(load_th_idx, nullptr);

    return next;
}

// ──────────────────────────────────────────────────────────────────────
// Swap request slots one and two (ids included).
// FF3 0x00269980

static void SwapFileLoadWrk(int one, int two)
{
    FILE_LOAD_WRK temp;

    temp               = file_load_wrk[one];
    file_load_wrk[one] = file_load_wrk[two];
    file_load_wrk[two] = temp;
}

// ──────────────────────────────────────────────────────────────────────
// Describe the transfer of wrk's file -- from read_size on -- and send it to
// the IOP file server; intr_SifEnd_FileLoad signals rpc_sema_id when it ended.
// A decode load is described by cmp_eeiop, which also restarts its thread.
// FF3 0x00269a88

static void rpcFileLoadReqSub(FILE_LOAD_WRK *wrk)
{
    LOAD_REQ_NEW *cmd = &iop_fileload_cmd;

    if (wrk->type == FILE_LOAD_TYPE_DECODE_EE)
    {
        *cmd = cmp_eeiopCreateDecodeThread(wrk->size, (intptr_t)wrk->buffer, wrk->start_sector,
                                           file_load_sys.decode_th_priority);
    }
    else
    {
        cmd->adrs            = (intptr_t)wrk->buffer + wrk->read_size;
        cmd->ld.size         = wrk->size - wrk->read_size;
        cmd->ld.start_sector = wrk->start_sector + (wrk->read_size >> 11);
        cmd->type            = wrk->type;
    }
    cmd->ld.one_buf_size = file_load_sys.one_buf_size;
    cmd->ld.ring_buf_num = file_load_sys.ring_buf_num;
    FlushCache(0);

    // FF3: sceSifCallRpc(&sif_cli_data_f, 0, SIF_RPCM_NOWAIT, cmd, 0x40,
    //                    iop_fileload_ret, 0x40, intr_SifEnd_FileLoad, 0);
    // PC: the host loader serves the request here, on the loader thread, and
    // the end function runs as the RPC's would (MioPan does the same). The
    // decode description from cmp_eeiop is not used: the host loader decodes a
    // compressed file itself, so the request is described from wrk.
    // callcheck: sceSifCallRpc (replaced by ReiPan_FileLoadServe on PC)
    {
        ReiPan_FileLoadReq req;
        char               path[256];
        const int          is_cmp = cddatIsCmpFile(wrk->file_no);

        ReiPan_CdFilePath(wrk->file_no, path, sizeof(path));
        req.ee_adrs       = (uintptr_t)wrk->buffer + (uintptr_t)wrk->read_size;
        req.request_size  = wrk->size - wrk->read_size;
        req.start_sector  = wrk->start_sector + (wrk->read_size >> 11);
        req.cmp_file_name = is_cmp ? "" : path;     // a loose file as stored
        req.raw_file_name = is_cmp ? path : "";     // a loose unpacked file
        req.raw_size      = GetFileSize(wrk->file_no);
        req.read_offset   = wrk->read_size;
        req.wrk_size      = wrk->size;
        req.is_cmp        = is_cmp;
        req.is_spu        = wrk->type == FILE_LOAD_TYPE_SPU;
        req.file_no       = wrk->file_no;

        iop_fileload_ret[0].cancel_flg = 0;
        iop_fileload_ret[0].read_size  = ReiPan_FileLoadServe(&req);
        printf("[load] %04x %s -> %p (%u bytes%s)\n", wrk->file_no, path, (void *)wrk->buffer,
               req.raw_size, is_cmp ? ", compressed" : "");
        FlushCache(0);
        intr_SifEnd_FileLoad(nullptr);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Register a request. With a load in flight (the tail), the in-flight entry
// moves one slot up and the new request takes its place, so the tail stays
// the in-flight load once yet_files grows. A new request more urgent than a
// raw in-flight load stops that load (LOAD_CANCEL_STORE): what it read is
// kept and it is queued again. Returns (slot id << 16) | temp_id.
// FF3 0x00269b78

static int FileLoadReq(int file_no, char *adrs, int priority, FILE_LOAD_CALLBACK func, void *arg, int type)
{
    FILE_LOAD_WRK *now_load_wrk;
    FILE_LOAD_WRK *wrk;
    int            ret;

    const u_int bytes = GetFileSize(file_no);
    const u_int transfer = cddatIsCmpFile(file_no) ? GetFileCmpSize(file_no) : bytes;
    if (bytes == 0 || bytes == (u_int)-1 || transfer == 0 || transfer > 0x7fffffffu ||
        (adrs == nullptr && type != FILE_LOAD_TYPE_SPU))
        return -1;

    SuspendThread(load_th_idx);
    if (file_load_sys.yet_files >= file_load_sys.file_load_wrk_max)
    {
        ResumeThread(load_th_idx);
        printf("ReiPan: file queue full while requesting %04x\n", file_no);
        return -1;
    }

    if (file_load_sys.yet_files == 0)
    {
        now_load_wrk = nullptr;
        wrk          = file_load_wrk;
    }
    else
    {
        SwapFileLoadWrk(file_load_sys.yet_files, file_load_sys.yet_files - 1);
        now_load_wrk = file_load_wrk + file_load_sys.yet_files;
        wrk          = now_load_wrk - 1;
    }

    wrk->file_no      = file_no;
    wrk->buffer       = adrs;
    wrk->func         = func;
    wrk->arg          = arg;
    wrk->start_sector = file_load_sys.use_cd_image != 0 ? GetFileStartSector(file_no) : 0;
    wrk->type         = type;
    wrk->priority     = priority;
    wrk->read_size    = 0;
    wrk->size         = (int)transfer;

    file_load_sys.yet_files++;
    wrk->temp_id = file_load_wrk_id++;
    SignalSema(load_req_sema_id);

    ret = wrk->id;
    if (now_load_wrk != nullptr && file_load_sys.cancel_type == LOAD_CANCEL_NONE &&
        (u_int)now_load_wrk->type < FILE_LOAD_TYPE_DECODE_EE && wrk->priority < now_load_wrk->priority)
    {
        ReqQueryLoadCancel();
        file_load_sys.cancel_type = LOAD_CANCEL_STORE;
    }

    ResumeThread(load_th_idx);
    return (ret << 16) | wrk->temp_id;
}

// ──────────────────────────────────────────────────────────────────────
// SIF RPC end function: the load command finished.
// FF3 0x00269d18

static void intr_SifEnd_FileLoad(void *data)
{
    (void)data;
    iSignalSema(rpc_sema_id);
    SYNC(0);
    EI();
}

// ──────────────────────────────────────────────────────────────────────
// The loader thread. Each pass: wait for a request, sort the queue (priority,
// then sector within each priority), fire the tail, wait for the transfer
// (and the decode), then settle a pending cancel or, on completion, drop the
// entry and run its callback.
// FF3 0x00269d40

static void thFileLoad(void *dummy)
{
    FILE_LOAD_WRK *wrk;
    int            i;
    int            end_flg;
    int            part_sort_start;
    int            priority;

    (void)dummy;
    for (;;)
    {
        WaitSema(load_req_sema_id);

        // PC: SuspendThread() froze this thread on the PS2; here it is a mutex
        // the loader has to hold too while it reads queue entries, or a
        // FileLoadReq() swap moves the entry being described (MioPan).
        ReiPan_LoaderCriticalEnter();

        qsort(file_load_wrk, file_load_sys.yet_files, sizeof(FILE_LOAD_WRK), cmpFileWrkPri);
        part_sort_start = 0;
        priority        = file_load_wrk[0].priority;
        for (i = 0; i < file_load_sys.yet_files; i++)
        {
            if (file_load_wrk[i].priority != priority)
            {
                qsort(file_load_wrk + part_sort_start, i - part_sort_start, sizeof(FILE_LOAD_WRK),
                      cmpFileWrkSector);
                part_sort_start = i;
                priority        = file_load_wrk[i].priority;
            }
        }
        qsort(file_load_wrk + part_sort_start, i - part_sort_start, sizeof(FILE_LOAD_WRK), cmpFileWrkSector);

        rpcFileLoadReqSub(file_load_wrk + file_load_sys.yet_files - 1);
        const int decode = file_load_wrk[file_load_sys.yet_files - 1].type == FILE_LOAD_TYPE_DECODE_EE;
        ReiPan_LoaderCriticalLeave();
        if (decode)
        {
            cmp_eeiopWaitSema();
        }

        end_flg = 0;
        WaitSema(rpc_sema_id);

        // PC: claim the mutex again before finding the in-flight entry; on the
        // PS2 nothing could run FileLoadReq() in between. Callbacks are taken
        // under it and run after it is released (they run game code).
        ReiPan_LoaderCriticalEnter();
        wrk = file_load_wrk + file_load_sys.yet_files - 1;

        FILE_LOAD_CALLBACK cxl_func    = nullptr;
        void              *cxl_buffer  = nullptr;
        void              *cxl_arg     = nullptr;
        FILE_LOAD_CALLBACK done_func   = nullptr;
        void              *done_buffer = nullptr;
        void              *done_arg    = nullptr;

        if (file_load_sys.cancel_type == LOAD_CANCEL_STORE)
        {
            // Stopped for a more urgent request: keep what was read, queue
            // the rest again.
            wrk->read_size += iop_fileload_ret[0].read_size;
            if (wrk->read_size < wrk->size)
            {
                SignalSema(load_req_sema_id);
            }
            else
            {
                end_flg = 1;
            }
        }
        else if (file_load_sys.cancel_type == LOAD_CANCEL_NONE)
        {
            end_flg = 1;
        }
        else if (file_load_sys.cancel_type == LOAD_CANCEL_NORMAL || file_load_sys.cancel_type == LOAD_CANCEL_WAIT)
        {
            if (file_load_sys.cancel_type == LOAD_CANCEL_WAIT)
            {
                WakeupThread(file_load_sys.master_thread_id);
            }
            // FF3 fires cancel_func for a waiting cancel too (whatever the
            // last normal cancel left there).
            if (file_load_sys.cancel_func != nullptr)
            {
                cxl_func   = file_load_sys.cancel_func;
                cxl_buffer = wrk->buffer;
                cxl_arg    = file_load_sys.cancel_arg;
            }
            file_load_sys.yet_files--;
        }

        file_load_sys.cancel_type = LOAD_CANCEL_NONE;
        if (end_flg != 0)
        {
            file_load_sys.yet_files--;
            done_func   = wrk->func;
            done_buffer = wrk->buffer;
            done_arg    = wrk->arg;
        }
        ReiPan_LoaderCriticalLeave();

        if (cxl_func != nullptr)
        {
            cxl_func(cxl_buffer, cxl_arg);
        }
        if (done_func != nullptr)
        {
            done_func(done_buffer, done_arg);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Change the loader and decode threads' priorities.
// FF3 0x00269f48

void FileLoadChangeThreadPriority(int load_th_priority, int decode_th_priority)
{
    ChangeThreadPriority(load_th_idx, load_th_priority);
    file_load_sys.load_th_priority = load_th_priority;
    cmp_eeiopChangePriority(decode_th_priority);
    file_load_sys.decode_th_priority = decode_th_priority;
}
