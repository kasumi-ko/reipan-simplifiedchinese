/* ==========================================================================
 *  system/encodes/cmp.c
 *
 *  The CMP archive block decoder (see cmp.h). FF3 0x271b08..0x271c30, the
 *  same three functions as FF2's cmp.o in the same order; ported from MioPan
 *  and checked against every FF3 body: no change in the logic.
 *
 *  Callers: FF3's own consumer is cmp_eeiop.c's decode thread
 *  (cmp_eeiopDecodeLoadMain 0x270470), which calls CMP_Init once and then
 *  drives CMP_DecodeOne block by block so it can wait for each IOP doorbell.
 *  CMP_Decode has no caller in the ROM. The port uses it: the host loader
 *  (reipan_fileload.cpp) reads a whole .cmp into a buffer and expands it in
 *  one go, and on PC the decode thread never starts (cmp_eeiop.c).
 *
 *  A block's two sizes: ENCODE_DIV_SECTION::size is the compressed extent and
 *  steps the input cursor (rounded with GetAlignUp(size, 4): a shift count,
 *  so blocks start on 16-byte boundaries); every block expands to exactly
 *  CMP_HEADER::div_size, which steps the output cursor.
 *
 *  PORT DEVIATION -- CMP_Init's in-place fixup. In the ROM `data_offset` and
 *  `div_p` arrive as byte offsets from the head of the header and CMP_Init
 *  rewrites both into absolute EE addresses. Both are 32-bit fields of an
 *  on-disc record, too small for a host pointer, so the port leaves them
 *  header-relative (what the file holds) and adds the base at each use
 *  (CmpDataTop / CmpDivTbl, not ROM functions). CMP_Init keeps its `mapping`
 *  latch. cmp_eeiopDecodeLoadMain reads data_offset as an address after
 *  CMP_Init in the ROM; on PC that code never runs (see cmp_eeiop.c).
 * ======================================================================== */

#include "cmp.h"

#include <string.h>                     // memcpy

#include "encodes.h"                    // SlideDecode
#include "../../common/utility2.h"      // GetAlignUp

// ──────────────────────────────────────────────────────────────────────
// PC: resolve the two header-relative fields (see the banner). Where these
// appear, the ROM read header->data_offset / header->div_p directly.

static ENCODE_DIV_SECTION *CmpDivTbl(const CMP_HEADER *header)
{
    return (ENCODE_DIV_SECTION *)((u_char *)header + header->div_p);
}

// ──────────────────────────────────────────────────────────────────────
// Expand block `no` from `from_adrs` to `to_adrs`: LZSS or a plain copy, as
// the block's div entry says; both take its compressed size.
// FF3 0x00271b08; from MioPan cmp.c (unchanged).

void CMP_DecodeOne(CMP_HEADER *header, int no, uintptr_t from_adrs, uintptr_t to_adrs)
{
    if (CmpDivTbl(header)[no].type == ENCODE_TYPE_SLIDE)
    {
        SlideDecode((u_char *)from_adrs, (u_char *)to_adrs, CmpDivTbl(header)[no].size);
    }
    else
    {
        memcpy((void *)to_adrs, (void *)from_adrs, CmpDivTbl(header)[no].size);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Decode into the file's aligned destination. Some final B2d blocks include
// their 16-byte alignment padding in the compressed stream.
// FF3 0x00271b60; from MioPan cmp.c (unchanged). No caller in the ROM.

int CMP_Decode(const CMP_HEADER *header, size_t stored_size, void *decode_buf,
               size_t output_size)
{
    if (stored_size < sizeof(*header) || header->size <= 0 ||
        (size_t)header->size > output_size || output_size - header->size >= 16 ||
        header->div_size <= 0 || header->div_num != 1 + (header->size - 1) / header->div_size ||
        header->div_p < (int)sizeof(*header) || header->data_offset < header->div_p ||
        (size_t)header->data_offset > stored_size ||
        (header->div_p % alignof(ENCODE_DIV_SECTION)) != 0 ||
        (size_t)header->div_num > (size_t)(header->data_offset - header->div_p) / sizeof(ENCODE_DIV_SECTION))
        return -1;

    const ENCODE_DIV_SECTION *blocks = CmpDivTbl(header);
    const u_char *input = (const u_char *)header;
    u_char *output = (u_char *)decode_buf;
    size_t offset = header->data_offset;
    int remaining = header->size;
    for (int i = 0; i < header->div_num; i++)
    {
        const ENCODE_DIV_SECTION &block = blocks[i];
        const size_t padded = GetAlignUp(block.size, 4);
        const int expected = remaining < header->div_size ? remaining : header->div_size;
        if (block.size == 0 || padded > stored_size - offset)
            return -1;
        if (block.type == ENCODE_TYPE_SLIDE)
        {
            const int capacity = remaining == expected
                ? expected + (int)(output_size - header->size) : expected;
            if (SlideDecode(input + offset, output, block.size, capacity) < expected)
                return -1;
        }
        else
        {
            if (block.type != ENCODE_TYPE_NONE || block.size != expected)
                return -1;
            memcpy(output, input + offset, block.size);
        }
        offset += padded;
        output += expected;
        remaining -= expected;
    }

    return header->size;
}

// ──────────────────────────────────────────────────────────────────────
// Resolve the header's two offsets, once.
// FF3 0x00271c00; from MioPan cmp.c.
// PC: the ROM does data_offset += (int)header and div_p += (int)header
// here; the port keeps both header-relative (banner), so only the latch is
// left.

void CMP_Init(CMP_HEADER *header)
{
    if (header->mapping == 0)
    {
        header->mapping = 1;
    }
}
