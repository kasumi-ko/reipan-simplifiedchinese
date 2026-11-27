#include "reipan/rendering/reipan_font_textures.h"
/* ==========================================================================
 *  system/eeiop/cddat.c
 *
 *  The disc's file table. ee_iopInit reads ZERO3.FHD whole and hands it to
 *  cddatInit together with the PC path prefix ("host0:"); the Get* accessors
 *  map a file number (enum CD_FILE_DAT) onto the FHD's tables. With a data
 *  image (CDDAT_HEADER.use_cd_image, set on the disc) sizes and sectors come
 *  from the tables; without one the debug build asked the host for sizes
 *  (QueryFileSize, empty in retail).
 *
 *  From MioPan system/eeiop/cddat.c (FF2's cddat.c), changed to FF3's shape:
 *  the four separate FF2 arrays became one loaded image with a header, every
 *  lookup checks that its table exists, cddatSearchFileName is new, and FF2's
 *  asserts, prints and cddatCompressFileNoUse[No] are gone.
 * ======================================================================== */
#include "system/eeiop/cddat.h"

#include <stdio.h>                      // snprintf (ReiPan_CdFilePath)
#include <string.h>                     // strcpy / strcat / strcmp

#include "common/utility2.h"            // GetAlignUp
#include "system/eeiop/ee_iop_q.h"      // QueryFileSize

static CDDAT_HEADER *p_cd_dat;          /* sbss gp-0xce0 */
static char file_full_name[256];        /* bss 0x410eb0: GetFileName's result */
static char title_root_path[200];       /* bss 0x410fb0: cddatInit's pc_path */

static bool ValidFileNumber(int file_no)
{
    return p_cd_dat != nullptr && file_no >= 0 && file_no < p_cd_dat->file_num;
}

// ──────────────────────────────────────────────────────────────────────
// PC: FF3 rewrites the header's table offsets into EE pointers in place
// (cddatSetHeader). A host pointer does not fit the 32-bit slots, so ReiPan
// keeps the offsets and resolves them here at each use. An offset of 0 stays
// "no table" either way, so every FF3 null check reads the same.

template <typename T>
static T *CdDatTbl(u_int offset)
{
    return (T *)((u_char *)p_cd_dat + offset);
}

static const char *CdDatStr(u_int offset)
{
    return CdDatTbl<const char>(offset);
}

// ──────────────────────────────────────────────────────────────────────
// Latch the loaded FHD image. FF3 also adds the image address to each table
// offset (size_tbl always, the others when non-zero, and both string offsets
// of every name entry).
// FF3 0x0026f340; no FF2 counterpart (FF2 passed the tables to cddatInit).

void cddatSetHeader(CDDAT_HEADER *hdr)
{
    // PC: the relocation is left out; CdDatTbl resolves the offsets instead.
    p_cd_dat = hdr;
}

// ──────────────────────────────────────────────────────────────────────
// Remember the PC path prefix and latch the file table. FF2's version also
// sent the image name to the IOP (REQ_SET_CD_DAT); FF3's ee_iopInit does that
// itself before reading the FHD.
// FF3 0x0026f3d8; from MioPan cddat.c cddatInit (tables now in one image).

void cddatInit(const char *pc_path, void *fhd)
{
    strcpy(title_root_path, pc_path);
    cddatSetHeader((CDDAT_HEADER *)fhd);
    ReiPanFonts::Prepare();
}

// ──────────────────────────────────────────────────────────────────────
// A file's extension label byte, 0 when the FHD has no label table.
// FF3 0x0026f410; from MioPan cddat.c GetFileExtLabel (table check added).

char GetFileExtLabel(int file_no)
{
    if (ValidFileNumber(file_no) && p_cd_dat->ext_lbl != 0)
    {
        return CdDatTbl<s_char>(p_cd_dat->ext_lbl)[file_no];
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Number of entries in the table.
// FF3 0x0026f430; from MioPan cddat.c GetFileNum.

int GetFileNum(void)
{
    return p_cd_dat != nullptr ? p_cd_dat->file_num : 0;
}

// ──────────────────────────────────────────────────────────────────────
// Start sector of a file in the data image; 0 when files are not read from
// an image (FF2 asserted instead when the file was not on the disc).
// FF3 0x0026f440; from MioPan cddat.c GetFileStartSector.

int GetFileStartSector(int file_no)
{
    if (!ValidFileNumber(file_no))
        return -1;
    if (p_cd_dat->use_cd_image != 0 && p_cd_dat->lba_tbl != 0)
    {
        return CdDatTbl<int>(p_cd_dat->lba_tbl)[file_no];
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// File size in 2048-byte sectors, rounded up.
// FF3 0x0026f470; from MioPan cddat.c GetFileSectorSize.

int GetFileSectorSize(int file_no)
{
    return (int)(GetFileSize(file_no) + 0x7ff) >> 0xb;
}

// ──────────────────────────────────────────────────────────────────────
// Unpacked size rounded up to 16, or (u_int)-1 for an entry without a file.
// Without a data image but with names (the debug build on host0:) the size
// comes from the host instead.
// FF3 0x0026f498; from MioPan cddat.c GetFileSize (host branch and the -1
// label check are FF3's).

u_int GetFileSize(int file_no)
{
    if (!ValidFileNumber(file_no))
        return (u_int)-1;

    // PC: QueryFileSize is empty in retail and leaves size unset; start from
    // "no file" so that branch answers something defined.
    int size = -1;

    if (p_cd_dat->use_cd_image == 0 && p_cd_dat->name_tbl != 0)
    {
        QueryFileSize(file_no, (u_int *)&size);
    }
    else
    {
        size = CdDatTbl<int>(p_cd_dat->size_tbl)[file_no];
    }

    if (size == -1)
    {
        return (u_int)-1;
    }
    return GetAlignUp((u_int)size, 4);
}

// ──────────────────────────────────────────────────────────────────────
// Stored (compressed) size of a file, 0 without a stored-size table. The
// table value is returned as it is (FF2 rounded it up to 16); the host branch
// rounds the host's answer.
// FF3 0x0026f518; from MioPan cddat.c GetFileCmpSize.

u_int GetFileCmpSize(int file_no)
{
    if (!ValidFileNumber(file_no) || p_cd_dat->cmp_tbl == 0)
    {
        return 0;
    }

    if (p_cd_dat->use_cd_image == 0 && p_cd_dat->name_tbl != 0)
    {
        u_int size = 0;                 // PC: QueryFileSize is empty in retail (see GetFileSize)

        QueryFileSize(file_no, &size);
        return GetAlignUp(size, 4);
    }
    return CdDatTbl<u_int>(p_cd_dat->cmp_tbl)[file_no] >> 1;
}

// ──────────────────────────────────────────────────────────────────────
// 1 when the file is stored compressed (bit 0 of its stored-size word).
// FF3 0x0026f580; from MioPan cddat.c cddatIsCmpFile.

int cddatIsCmpFile(int file_no)
{
    if (ValidFileNumber(file_no) && p_cd_dat->cmp_tbl != 0)
    {
        return (int)(CdDatTbl<u_int>(p_cd_dat->cmp_tbl)[file_no] & 1);
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// File number of the entry whose file name (not folder) equals name, or -1
// (also -1 without a name table). No caller in FF3 retail.
// FF3 0x0026f5a8; no FF2 counterpart.

int cddatSearchFileName(const char *name)
{
    if (p_cd_dat != nullptr && p_cd_dat->name_tbl != 0 && p_cd_dat->file_num > 0)
    {
        for (int i = 0; i < p_cd_dat->file_num; i++)
        {
            if (strcmp(name, CdDatStr(CdDatTbl<CDDAT_NAME>(p_cd_dat->name_tbl)[i].name)) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// Full PC path of a file (root + folder + name) in a static buffer, "error"
// without a name table. FF2 range-checked file_no; FF3 does not.
// FF3 0x0026f628; from MioPan cddat.c GetFileName.

char *GetFileName(int file_no)
{
    if (!ValidFileNumber(file_no) || p_cd_dat->name_tbl == 0)
    {
        return (char *)"error";
    }

    const CDDAT_NAME *ent = &CdDatTbl<CDDAT_NAME>(p_cd_dat->name_tbl)[file_no];

    strcpy(file_full_name, title_root_path);
    strcat(file_full_name, CdDatStr(ent->folder));
    strcat(file_full_name, CdDatStr(ent->name));
    return file_full_name;
}

// ──────────────────────────────────────────────────────────────────────
// As GetFileName, into the caller's buffer ("error" without a name table).
// FF3 0x0026f6b8; from MioPan cddat.c GetFileNameBuffer.

void GetFileNameBuffer(int file_no, char *buf)
{
    if (!ValidFileNumber(file_no) || p_cd_dat->name_tbl == 0)
    {
        strcpy(buf, "error");
        return;
    }

    const CDDAT_NAME *ent = &CdDatTbl<CDDAT_NAME>(p_cd_dat->name_tbl)[file_no];

    strcpy(buf, title_root_path);
    strcat(buf, CdDatStr(ent->folder));
    strcat(buf, CdDatStr(ent->name));
}

// ──────────────────────────────────────────────────────────────────────
// PC: folder + name of a file without the "host0:" root, for the host
// loader's loose-file override (fileload.c); "" when there is no table yet,
// no name table, or the number is out of range.

void ReiPan_CdFilePath(int file, char *buf, int size)
{
    if (size <= 0)
    {
        return;
    }
    buf[0] = '\0';
    if (p_cd_dat == nullptr || p_cd_dat->name_tbl == 0 || file < 0 || file >= p_cd_dat->file_num)
    {
        return;
    }

    const CDDAT_NAME *ent = &CdDatTbl<CDDAT_NAME>(p_cd_dat->name_tbl)[file];

    snprintf(buf, (size_t)size, "%s%s", CdDatStr(ent->folder), CdDatStr(ent->name));
}
