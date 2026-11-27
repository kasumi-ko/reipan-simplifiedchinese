/* ==========================================================================
 *  ingame/map/MapPhf.c
 *
 *  The PHF pack reader (see MapPhf.h). FF3 0x11d648..0x11d890; FF3-only,
 *  written from the FF3 decompilation and checked against retail packs.
 *  Every accessor first checks the magic, so a lone .b2d or a null pointer
 *  reads as an empty pack.
 * ======================================================================== */
#include "ingame/map/MapPhf.h"

#include <string.h>                     // strcmp

// ──────────────────────────────────────────────────────────────────────
// 1 when `pack` starts with the "phf" magic. BpCtlStepLoad uses it to tell
// a pack from a lone .b2d file.
// FF3 0x0011d648.

int PhfCheck(const void *pack)
{
    int ret = 0;

    if (pack != nullptr)
    {
        ret = memcmp(pack, "phf", 4) == 0;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The number of members, 0 when `pack` is not a pack.
// FF3 0x0011d678.

int PhfGetFileNum(const void *pack)
{
    int num = 0;

    if (PhfCheck(pack))
    {
        num = ((const PHF_HEAD *)pack)->num;
    }
    return num;
}

// ──────────────────────────────────────────────────────────────────────
// Size of the member called `name`, 0 when there is none.
// FF3 0x0011d6b0 (MapDrawInitFurn).

int PhfFindFileSize(void *pack, const char *name)
{
    PHF_FILE *file = PhfFindFileHead(pack, name);
    int       size = 0;

    if (file != nullptr)
    {
        size = file->size;
    }
    return size;
}

// ──────────────────────────────────────────────────────────────────────
// Data of the member called `name`, null when there is none.
// FF3 0x0011d6e0 (BpTex's texture-load hook).

void *PhfFindFileData(void *pack, const char *name)
{
    PHF_FILE *file = PhfFindFileHead(pack, name);

    if (file == nullptr)
    {
        return nullptr;
    }
    return (u_char *)pack + ((PHF_HEAD *)pack)->data_ofs + file->ofs;
}

// ──────────────────────────────────────────────────────────────────────
// Data of member `no`, null when `pack` is not a pack or `no` is out of
// range. FF3 inlines the entry lookup of PhfGetFileHead here.
// FF3 0x0011d720.

void *PhfGetFileData(void *pack, int no)
{
    PHF_FILE *file = nullptr;

    if (PhfCheck(pack) && no >= 0)
    {
        int num = PhfGetFileNum(pack);

        file = (PHF_FILE *)((u_char *)pack + sizeof(PHF_HEAD)) + no;
        if (no >= num)
        {
            file = nullptr;
        }
    }
    if (file == nullptr)
    {
        return nullptr;
    }
    return (u_char *)pack + ((PHF_HEAD *)pack)->data_ofs + file->ofs;
}

// ──────────────────────────────────────────────────────────────────────
// Entry of member `no` (its name first), null when `pack` is not a pack or
// `no` is out of range.
// FF3 0x0011d798.

PHF_FILE *PhfGetFileHead(void *pack, int no)
{
    PHF_FILE *file = nullptr;

    if (PhfCheck(pack) && no >= 0)
    {
        int num = PhfGetFileNum(pack);

        file = (PHF_FILE *)((u_char *)pack + sizeof(PHF_HEAD)) + no;
        if (no >= num)
        {
            file = nullptr;
        }
    }
    return file;
}

// ──────────────────────────────────────────────────────────────────────
// Entry of the member called `name` (strcmp, first match), null when there
// is none or `pack` is not a pack.
// FF3 0x0011d800.

PHF_FILE *PhfFindFileHead(void *pack, const char *name)
{
    if (PhfCheck(pack))
    {
        PHF_HEAD *head = (PHF_HEAD *)pack;
        PHF_FILE *file = (PHF_FILE *)((u_char *)pack + sizeof(PHF_HEAD));

        for (int i = 0; i < head->num; i++, file++)
        {
            if (strcmp(file->name, name) == 0)
            {
                return file;
            }
        }
    }
    return nullptr;
}
