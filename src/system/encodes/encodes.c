/* ==========================================================================
 *  system/encodes/encodes.c
 *
 *  The LZSS coder (see encodes.h). FF3 0x2712a0..0x271b08, the same five
 *  functions as FF2's encodes.o in the same order; ported from MioPan and
 *  checked against every FF3 body: no change in the logic (FF3's GCC counts
 *  the window-clearing loops up where FF2's counted down). cmp.c expands
 *  every ENCODE_TYPE_SLIDE block through SlideDecode; photo_comp.c tries
 *  SlideEncode on a photo before the DCT codec.
 *
 *  The algorithm is Haruhiko Okumura's LZSS.C, near-verbatim: init_tree,
 *  insert_node, delete_node, code / text, N / F / THRESHOLD / NIL and the
 *  binary-search-tree window are all his. The ROM's deviations:
 *    - getc / putc are buffer cursors, and SlideEncode answers the
 *      compressed byte count instead of leaving it in a global;
 *    - every index is a short, so NIL is 0x1000 in a halfword;
 *    - the window is pre-filled with 0, not ' ' (both halves agree, so
 *      round trips are unaffected; a stock LZSS.C stream would not decode
 *      the same);
 *    - SlideEncode counts size++ inside the flush loops and accumulates
 *      incount, whose only trace is a guarded divide-by-zero trap (below).
 *
 *  No external calls: only init_tree / insert_node / delete_node.
 *  SlideEncode works in the file-scope text[] window, SlideDecode keeps its
 *  own window on the stack, so a decode can run while an encode is half
 *  done.
 *
 *  The tree arrays and matchpos / matchlen are all zero but live in .data /
 *  .sdata (FF3 0x333b08.. and 0x372828), not .bss: the source wrote explicit
 *  zero initialisers, kept here for that reason.
 * ======================================================================== */

#include "encodes.h"

#define N           ENCODES_N
#define F           ENCODES_F
#define THRESHOLD   ENCODES_THRESHOLD
#define NIL         ENCODES_NIL

/* --------------------------------------------------------------------------
 *  The search tree over the window, and the window itself.  `dad[i]` is the
 *  parent of node i, `lson`/`rson` its children; rson[N+1 .. N+256] are the
 *  256 tree roots, one per possible first byte.  See the banner for
 *  why these carry explicit zero initialisers.
 * ------------------------------------------------------------------------ */
short  dad[N + 1]      = {0};                               /* FF3 data  0x333b08 */
short  lson[N + 1]     = {0};                               /* FF3 data  0x335b10 */
short  rson[N + 257]   = {0};                               /* FF3 data  0x337b18 */
u_char text[N + F - 1] = {0};                               /* FF3 data  0x339d20 */

/* Where insert_node() left the longest match it found, and how long it was. */
short  matchpos = 0;                                        /* FF3 sdata 0x372828 */
short  matchlen = 0;                                        /* FF3 sdata 0x37282a */

// --------------------------------------------------------------------------
// Compress `max_size` bytes at `base` into `addrs`; answer the byte count
// written.  Okumura's Encode().
//
// The shape: keep a 4 KB sliding window in text[], with `s` the oldest byte
// and `r` the insertion point F bytes ahead.  Each step asks the tree for the
// longest match at r, emits either one literal or a (position, length) pair,
// then slides the window that far -- deleting and re-inserting tree nodes as
// bytes leave and enter.  Output is buffered into groups of one flag byte plus
// up to eight items, which is what `code`/`codeptr`/`mask` are.
//
// FF3 0x002712a0; from MioPan encodes.c (unchanged).
int SlideEncode(u_char *base, u_char *addrs, int max_size)
{
    short  s;
    short  r;
    short  len;
    short  codeptr;
    short  i;
    short  lastmatchlen;
    u_char c;
    u_char code[17];
    u_char mask;
    u_int  incount;
    u_int  size;

    size = 0;

    init_tree();
    code[0] = 0;
    codeptr = mask = 1;
    s = 0;
    r = N - F;

    /* Okumura fills this with ' '.  This build fills it with 0 -- see the
     * banner; SlideDecode() does the same, so the two agree. */
    for (i = s; i < r; i++)
        text[i] = 0;

    /* Prime the look-ahead with up to F bytes. */
    for (len = 0; len < F; len++)
    {
        if (max_size == 0) break;

        c = *base++;
        max_size--;
        text[r + len] = c;
    }

    incount = len;
    if (incount == 0) return 0;

    /* Seed the tree with the F strings that end at r, then r itself. */
    for (i = 1; i <= F; i++)
        insert_node(r - i);

    insert_node(r);

    do
    {
        if (matchlen > len)
            matchlen = len;

        if (matchlen <= THRESHOLD)
        {
            matchlen = 1;
            code[0] |= mask;
            code[codeptr++] = text[r];
        }
        else
        {
            code[codeptr++] = (u_char)matchpos;
            code[codeptr++] = (u_char)(((matchpos >> 4) & 0xf0)
                                       | (matchlen - (THRESHOLD + 1)));
        }

        /* mask is a u_char, so the ninth shift is what flushes the group. */
        if ((mask <<= 1) == 0)
        {
            for (i = 0; i < codeptr; i++)
            {
                *addrs++ = code[i];
                size++;
            }

            code[0] = 0;
            codeptr = mask = 1;
        }

        lastmatchlen = matchlen;

        /* Slide the window forward by the match length, feeding new bytes in
         * at r and retiring old ones at s. */
        for (i = 0; i < lastmatchlen; i++)
        {
            if (max_size == 0) break;

            c = *base++;
            max_size--;
            delete_node(s);

            text[s] = c;
            if (s < F - 1)
                text[s + N] = c;

            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            insert_node(r);
        }

        incount += i;

        /* Ran out of input mid-match: drain the rest of the look-ahead. */
        while (i++ < lastmatchlen)
        {
            delete_node(s);
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            if (--len)
                insert_node(r);
        }
    }
    while (len > 0);

    if (codeptr > 1)
    {
        for (i = 0; i < codeptr; i++)
        {
            *addrs++ = code[i];
            size++;
        }
    }

    /* NOT REPRODUCED.  What is left here is
     * `if (incount != 0)` guarding an integer division by `incount` whose
     * quotient GCC folded away, leaving only the divide-by-zero trap check
     * (`beql` + `break 0x7` at FF3 0x2715b0, FF2 0x2787e0) -- which is the only reason the
     * divisor is recoverable at all.  It is what remains of Okumura's trailing
     * "Out/In" ratio report: the printf is gone (this object makes no external
     * calls of any kind), and the quotient's destination is not recoverable
     * from a trap alone.  Documented rather than invented; nothing observes
     * it, and the guard means it could never have trapped. */

    return size;
}

// --------------------------------------------------------------------------
// Expand one LZSS block.  Okumura's Decode(), with its own stack window.
//
// `size` is the compressed byte count and is the only terminator -- there is
// no end marker in the stream, so every read is guarded and the function
// simply returns when the input runs out.
//
// FF3 0x00271638; from MioPan encodes.c (unchanged). Like FF2, FF3 loads each
// input byte before it tests `rest` (a hoisted load, the byte unused); the port
// tests first so it never reads past the host buffer.
int SlideDecode(const u_char *base, u_char *addrs, int size, int capacity)
{
    u_char textbuf[N + F - 1] = {};
    u_char *const start = addrs;
    int    i;
    int    j;
    int    k;
    int    r;
    int    c;
    u_short flags;
    u_int   rest;

    if (size < 0 || capacity < 0)
        return -1;
    rest = (u_int)size;

    r = N - F;
    flags = 0;

    for (;;)
    {
        /* The flag byte is refilled every eight items; the 0xff00 makes the
         * count fall out of the shift reaching bit 8. */
        if (((flags >>= 1) & 0x100) == 0)
        {
            if (rest == 0) return (int)(addrs - start);

            c = *base++;
            rest--;
            flags = (u_short)(c | 0xff00);
        }

        if ((flags & 1) != 0)
        {
            /* One literal byte. */
            if (rest == 0) return (int)(addrs - start);
            if (addrs - start >= capacity) return -1;

            c = *base++;
            rest--;

            *addrs++ = (u_char)c;
            textbuf[r] = (u_char)c;
            r = (r + 1) & (N - 1);
        }
        else
        {
            /* A (position, length) pair.  Note the copy reads through the
             * window as it writes, so an overlapping match repeats -- which is
             * how a run of one byte is encoded. */
            if (rest == 0) return (int)(addrs - start);

            i = *base++;
            rest--;

            if (rest == 0) return -1;

            j = *base++;
            rest--;

            i |= (j & 0xf0) << 4;
            j = (j & 0x0f) + THRESHOLD;
            if (j + 1 > capacity - (addrs - start)) return -1;

            for (k = 0; k <= j; k++)
            {
                c = textbuf[(i + k) & (N - 1)];
                *addrs++ = (u_char)c;
                textbuf[r] = (u_char)c;
                r = (r + 1) & (N - 1);
            }
        }
    }
}

// --------------------------------------------------------------------------
// Empty the search tree.  The 256 roots go in rson[N+1 .. N+256]; a dad[] of
// NIL marks a window slot as not currently in any tree.
//
// FF3 0x00271748; from MioPan encodes.c (unchanged).
void init_tree(void)
{
    short i;

    for (i = N + 1; i <= N + 256; i++)
        rson[i] = NIL;

    for (i = 0; i < N; i++)
        dad[i] = NIL;
}

// --------------------------------------------------------------------------
// Insert the string at text[r] into the tree, and leave the longest match it
// passed on the way down in matchpos/matchlen.  Finding the match and doing
// the insert are the same descent, which is the point of the structure.
//
// If a full-length (F byte) match turns up, the old node is *replaced* rather
// than a new one added -- the tail of the function is that surgery.
//
// FF3 0x002717b0; from MioPan encodes.c (unchanged).
void insert_node(short r)
{
    short   p;
    short   cmp;
    short   i;
    u_char *key;

    cmp = 1;
    key = &text[r];
    p = N + 1 + key[0];
    rson[r] = lson[r] = NIL;
    matchlen = 0;

    for (;;)
    {
        if (cmp >= 0)
        {
            if (rson[p] != NIL)
            {
                p = rson[p];
            }
            else
            {
                rson[p] = r;
                dad[r] = p;
                return;
            }
        }
        else
        {
            if (lson[p] != NIL)
            {
                p = lson[p];
            }
            else
            {
                lson[p] = r;
                dad[r] = p;
                return;
            }
        }

        for (i = 1; i < F; i++)
            if ((cmp = key[i] - text[p + i]) != 0) break;

        if (i > matchlen)
        {
            matchpos = p;
            if ((matchlen = i) >= F) break;
        }
    }

    /* A full-length match: put r where p was and drop p out of the tree. */
    dad[r]  = dad[p];
    lson[r] = lson[p];
    rson[r] = rson[p];
    dad[lson[p]] = r;
    dad[rson[p]] = r;

    if (rson[dad[p]] == p) rson[dad[p]] = r;
    else                   lson[dad[p]] = r;

    dad[p] = NIL;
}

// --------------------------------------------------------------------------
// Remove node p from the tree.  Standard binary-tree deletion: splice out the
// child if there is only one, otherwise promote the right-most descendant of
// the left subtree.
//
// FF3 0x00271970; from MioPan encodes.c (unchanged).
void delete_node(short p)
{
    short q;

    if (dad[p] == NIL) return;      /* not in a tree */

    if (rson[p] == NIL)
    {
        q = lson[p];
    }
    else if (lson[p] == NIL)
    {
        q = rson[p];
    }
    else
    {
        q = lson[p];
        if (rson[q] != NIL)
        {
            do
            {
                q = rson[q];
            }
            while (rson[q] != NIL);

            rson[dad[q]] = lson[q];
            dad[lson[q]] = dad[q];
            lson[q] = lson[p];
            dad[lson[p]] = q;
        }

        rson[q] = rson[p];
        dad[rson[p]] = q;
    }

    dad[q] = dad[p];

    if (rson[dad[p]] == p) rson[dad[p]] = q;
    else                   lson[dad[p]] = q;

    dad[p] = NIL;
}
