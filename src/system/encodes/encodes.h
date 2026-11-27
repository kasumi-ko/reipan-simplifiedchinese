/* ==========================================================================
 *  system/encodes/encodes.h
 *
 *  encodes.o -- the LZSS coder, five functions, FF3 0x2712a0..0x271b08
 *  (FF2 0x278428).
 *  It is the game's lossless compressor: `cmp.a` uses it for every block of a
 *  .cmp file marked ENCODE_TYPE_SLIDE, and photo_make.c tries it on a photo
 *  before falling back to the DCT codec in system/compress.
 *
 *  The algorithm is Haruhiko Okumura's LZSS.C, near-verbatim: a 4 KB ring
 *  buffer, matches of 3..18 bytes, and a binary search tree over the window so
 *  the encoder can find the longest match in log time.  The ROM's changes are
 *  small and listed in encodes.c.
 *
 *  Format, for anyone reading a .cmp block by hand: the stream is groups of
 *  one flag byte followed by up to eight items.  Flag bit set = the next item
 *  is one literal byte.  Flag bit clear = the next item is two bytes
 *  (position_low, ((position_high << 4) | (length - 3))), naming a match of
 *  length+3 bytes at `position` in the 4 KB window.
 *
 *  From MioPan's encodes.h (FF2's Feb 6 2004 prototype, SLES_523.84); FF3's
 *  code is the same, only the data moved.
 * ======================================================================== */

#ifndef _SYSTEM_ENCODES_ENCODES_H
#define _SYSTEM_ENCODES_ENCODES_H

#include <sys/types.h>

/* Okumura's four constants.  NIL is N, which is why lson/dad are N+1 entries
 * and rson is N+257 -- the top 256 slots of rson are the per-first-byte tree
 * roots. */
#define ENCODES_N           4096    /* ring-buffer size                       */
#define ENCODES_F           18      /* longest match                          */
#define ENCODES_THRESHOLD   2       /* shorter than this is cheaper as literals */
#define ENCODES_NIL         ENCODES_N

/* The search tree and the encoder's window.  All four are file-scope in the
 * ROM and all four are exported, so they are not static here either.  Note
 * SlideDecode() does NOT use `text` -- it keeps its own window on the stack,
 * which is what lets a decode run while an encode is half-finished. */
extern short  dad[ENCODES_N + 1];               /* FF3 data  0x333b08 */
extern short  lson[ENCODES_N + 1];              /* FF3 data  0x335b10 */
extern short  rson[ENCODES_N + 257];            /* FF3 data  0x337b18 */
extern u_char text[ENCODES_N + ENCODES_F - 1];  /* FF3 data  0x339d20 */

extern short  matchpos;                         /* FF3 sdata 0x372828 */
extern short  matchlen;                         /* FF3 sdata 0x37282a */

/* The tree.  Exported by the ROM although only SlideEncode() calls them. */
void init_tree(void);                                       // FF3 0x00271748
void insert_node(short r);                                  // FF3 0x002717b0
void delete_node(short p);                                  // FF3 0x00271970

/* Expand one LZSS block.  `size` is the COMPRESSED byte count; the caller
 * knows the expanded size from its own header (CMP_HEADER::div_size, or
 * SLIDE_ENCODE_HEADER for a photo). */
int SlideDecode(const u_char *base, u_char *addrs, int size,
                int capacity = 0x7fffffff);                // FF3 0x00271638

/* Compress `max_size` bytes at `base` into `addrs`, and answer how many bytes
 * were written.  Answers 0 for an empty input.  There is no bound on the
 * output: LZSS can expand incompressible data by roughly 1/8, so the
 * destination must have room for that. */
int  SlideEncode(u_char *base, u_char *addrs, int max_size); // FF3 0x002712a0

#endif /* _SYSTEM_ENCODES_ENCODES_H */
