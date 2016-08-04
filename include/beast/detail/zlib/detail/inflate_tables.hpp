//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// This is a derivative work based on Zlib, copyright below:
/*
    Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Jean-loup Gailly        Mark Adler
    jloup@gzip.org          madler@alumni.caltech.edu

    The data format used by the zlib library is described by RFCs (Request for
    Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
    (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/

#ifndef BEAST_CORE_DETAIL_ZLIB_DETAIL_INFLATE_TABLES_HPP
#define BEAST_CORE_DETAIL_ZLIB_DETAIL_INFLATE_TABLES_HPP

#include <cstdint>

namespace beast {

/*
    Structure for decoding tables.  Each entry provides either the
    information needed to do the operation requested by the code that
    indexed that table entry, or it provides a pointer to another
    table that indexes more bits of the code.  op indicates whether
    the entry is a pointer to another table, a literal, a length or
    distance, an end-of-block, or an invalid code.  For a table
    pointer, the low four bits of op is the number of index bits of
    that table.  For a length or distance, the low four bits of op
    is the number of extra bits to get after the code.  bits is
    the number of bits in this code or part of the code to drop off
    of the bit buffer.  val is the actual byte to output in the case
    of a literal, the base length or distance, or the offset from
    the current table to the next table.  Each entry is four bytes.

    op values as set by inflate_table():

    00000000 - literal
    0000tttt - table link, tttt != 0 is the number of table index bits
    0001eeee - length or distance, eeee is the number of extra bits
    01100000 - end of block
    01000000 - invalid code
*/
struct code
{
    std::uint8_t op;           // operation, extra bits, table bits
    std::uint8_t bits;         // bits in this part of the code
    std::uint16_t val;         // offset in table or code value

    bool operator==(code const& other) const
    {
        return op == other.op && bits == other.bits && val == other.val;
    }
};

/* Maximum size of the dynamic table.  The maximum number of code structures is
   1444, which is the sum of 852 for literal/length codes and 592 for distance
   codes.  These values were found by exhaustive searches using the program
   examples/enough.c found in the zlib distribtution.  The arguments to that
   program are the number of symbols, the initial root table size, and the
   maximum bit length of a code.  "enough 286 9 15" for literal/length codes
   returns returns 852, and "enough 30 6 15" for distance codes returns 592.
   The initial root table size (9 or 6) is found in the fifth argument of the
   inflate_table() calls in inflate.c and infback.c.  If the root table size is
   changed, then these maximum sizes would be need to be recalculated and
   updated. */
std::uint16_t constexpr ENOUGH_LENS = 852;
std::uint16_t constexpr ENOUGH_DISTS = 592;
std::uint16_t constexpr ENOUGH = ENOUGH_LENS + ENOUGH_DISTS;

struct codes
{
    code const* lencode;
    code const* distcode;
    unsigned lenbits; // VFALCO use std::uint8_t
    unsigned distbits;
};

/* Type of code to build for inflate_table() */
enum codetype
{
    CODES,
    LENS,
    DISTS
};

/*
   Build a set of tables to decode the provided canonical Huffman code.
   The code lengths are lens[0..codes-1].  The result starts at *table,
   whose indices are 0..2^bits-1.  work is a writable array of at least
   lens shorts, which is used as a work area.  type is the type of code
   to be generated, CODES, LENS, or DISTS.  On return, zero is success,
   -1 is an invalid code, and +1 means that ENOUGH isn't enough.  table
   on return points to the next available entry's address.  bits is the
   requested root table index bits, and on return it is the actual root
   table index bits.  It will differ if the request is greater than the
   longest code or if it is less than the shortest code.
 */
template<class = void>
int
inflate_table(
    codetype type,
    std::uint16_t *lens,
    unsigned codes,
    code * *table,
    unsigned *bits,
    std::uint16_t *work)
{
    unsigned len;                   // a code's length in bits
    unsigned sym;                   // index of code symbols
    unsigned min, max;              // minimum and maximum code lengths
    unsigned root;                  // number of index bits for root table
    unsigned curr;                  // number of index bits for current table
    unsigned drop;                  // code bits to drop for sub-table
    int left;                       // number of prefix codes available
    unsigned used;                  // code entries in table used
    unsigned huff;                  // Huffman code
    unsigned incr;                  // for incrementing code, index
    unsigned fill;                  // index for replicating entries
    unsigned low;                   // low bits for current root entry
    unsigned mask;                  // mask for low root bits
    code here;                      // table entry for duplication
    code *next;                     // next available space in table
    std::uint16_t const* base;      // base value table to use
    std::uint16_t const* extra;     // extra bits table to use
    int end;                        // use base and extra for symbol > end
    std::uint16_t count[15+1];      // number of codes of each length
    std::uint16_t offs[15+1];       // offsets in table for each length
    // Length codes 257..285 base
    static std::uint16_t constexpr lbase[31] = { 
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
    // Length codes 257..285 extra
    static std::uint16_t constexpr lext[31] = {
        16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18,
        19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 16, 72, 78};
    // Distance codes 0..29 base
    static std::uint16_t constexpr dbase[32] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577, 0, 0};
    // Distance codes 0..29 extra
    static std::uint16_t constexpr dext[32] = {
        16, 16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
        23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
        28, 28, 29, 29, 64, 64};

    /*
       Process a set of code lengths to create a canonical Huffman code.  The
       code lengths are lens[0..codes-1].  Each length corresponds to the
       symbols 0..codes-1.  The Huffman code is generated by first sorting the
       symbols by length from short to long, and retaining the symbol order
       for codes with equal lengths.  Then the code starts with all zero bits
       for the first code of the shortest length, and the codes are integer
       increments for the same length, and zeros are appended as the length
       increases.  For the deflate format, these bits are stored backwards
       from their more natural integer increment ordering, and so when the
       decoding tables are built in the large loop below, the integer codes
       are incremented backwards.

       This routine assumes, but does not check, that all of the entries in
       lens[] are in the range 0..15.  The caller must assure this.
       1..15 is interpreted as that code length.  zero means that that
       symbol does not occur in this code.

       The codes are sorted by computing a count of codes for each length,
       creating from that a table of starting indices for each length in the
       sorted table, and then entering the symbols in order in the sorted
       table.  The sorted table is work[], with that space being provided by
       the caller.

       The length counts are used for other purposes as well, i.e. finding
       the minimum and maximum length codes, determining if there are any
       codes at all, checking for a valid set of lengths, and looking ahead
       at length counts to determine sub-table sizes when building the
       decoding tables.
     */

    /* accumulate lengths for codes (assumes lens[] all in 0..15) */
    for (len = 0; len <= 15; len++)
        count[len] = 0;
    for (sym = 0; sym < codes; sym++)
        count[lens[sym]]++;

    /* bound code lengths, force root to be within code lengths */
    root = *bits;
    for (max = 15; max >= 1; max--)
        if (count[max] != 0)
            break;
    if (root > max)
        root = max;
    if (max == 0)
    {                     /* no symbols to code at all */
        here.op = (std::uint8_t)64;    /* invalid code marker */
        here.bits = (std::uint8_t)1;
        here.val = (std::uint16_t)0;
        *(*table)++ = here;             /* make a table to force an error */
        *(*table)++ = here;
        *bits = 1;
        return 0;     /* no symbols, but wait for decoding to report error */
    }
    for (min = 1; min < max; min++)
        if (count[min] != 0)
            break;
    if (root < min)
        root = min;

    /* check for an over-subscribed or incomplete set of lengths */
    left = 1;
    for (len = 1; len <= 15; len++)
    {
        left <<= 1;
        left -= count[len];
        if (left < 0)
            return -1;        /* over-subscribed */
    }
    if (left > 0 && (type == CODES || max != 1))
        return -1;                      /* incomplete set */

    /* generate offsets into symbol table for each length for sorting */
    offs[1] = 0;
    for (len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + count[len];

    /* sort symbols by length, by symbol order within each length */
    for (sym = 0; sym < codes; sym++)
        if (lens[sym] != 0)
            work[offs[lens[sym]]++] = (std::uint16_t)sym;

    /*
       Create and fill in decoding tables.  In this loop, the table being
       filled is at next and has curr index bits.  The code being used is huff
       with length len.  That code is converted to an index by dropping drop
       bits off of the bottom.  For codes where len is less than drop + curr,
       those top drop + curr - len bits are incremented through all values to
       fill the table with replicated entries.

       root is the number of index bits for the root table.  When len exceeds
       root, sub-tables are created pointed to by the root entry with an index
       of the low root bits of huff.  This is saved in low to check for when a
       new sub-table should be started.  drop is zero when the root table is
       being filled, and drop is root when sub-tables are being filled.

       When a new sub-table is needed, it is necessary to look ahead in the
       code lengths to determine what size sub-table is needed.  The length
       counts are used for this, and so count[] is decremented as codes are
       entered in the tables.

       used keeps track of how many table entries have been allocated from the
       provided *table space.  It is checked for LENS and DIST tables against
       the constants ENOUGH_LENS and ENOUGH_DISTS to guard against changes in
       the initial root table size constants.  See the comments in inftrees.hpp
       for more information.

       sym increments through all symbols, and the loop terminates when
       all codes of length max, i.e. all codes, have been processed.  This
       routine permits incomplete codes, so another loop after this one fills
       in the rest of the decoding tables with invalid code markers.
     */

    /* set up for code type */
    switch (type)
    {
    case CODES:
        base = extra = work;    /* dummy value--not used */
        end = 19;
        break;
    case LENS:
        base = lbase;
        base -= 257;
        extra = lext;
        extra -= 257;
        end = 256;
        break;
    default:            /* DISTS */
        base = dbase;
        extra = dext;
        end = -1;
    }

    /* initialize state for loop */
    huff = 0;                   /* starting code */
    sym = 0;                    /* starting code symbol */
    len = min;                  /* starting code length */
    next = *table;              /* current table to fill in */
    curr = root;                /* current table index bits */
    drop = 0;                   /* current bits to drop from code for index */
    low = (unsigned)(-1);       /* trigger new sub-table when len > root */
    used = 1U << root;          /* use root table entries */
    mask = used - 1;            /* mask for comparing low */

    /* check available table space */
    if ((type == LENS && used > ENOUGH_LENS) ||
            (type == DISTS && used > ENOUGH_DISTS))
        return 1;

    /* process all codes and make table entries */
    for (;;)
    {
        /* create table entry */
        here.bits = (std::uint8_t)(len - drop);
        if ((int)(work[sym]) < end)
        {
            here.op = (std::uint8_t)0;
            here.val = work[sym];
        }
        else if ((int)(work[sym]) > end)
        {
            here.op = (std::uint8_t)(extra[work[sym]]);
            here.val = base[work[sym]];
        }
        else
        {
            here.op = (std::uint8_t)(32 + 64);         /* end of block */
            here.val = 0;
        }

        /* replicate for those indices with low len bits equal to huff */
        incr = 1U << (len - drop);
        fill = 1U << curr;
        min = fill;                 /* save offset to next table */
        do
        {
            fill -= incr;
            next[(huff >> drop) + fill] = here;
        } while (fill != 0);

        /* backwards increment the len-bit code huff */
        incr = 1U << (len - 1);
        while (huff & incr)
            incr >>= 1;
        if (incr != 0)
        {
            huff &= incr - 1;
            huff += incr;
        }
        else
            huff = 0;

        /* go to next symbol, update count, len */
        sym++;
        if (--(count[len]) == 0)
        {
            if (len == max) break;
            len = lens[work[sym]];
        }

        /* create new sub-table if needed */
        if (len > root && (huff & mask) != low)
        {
            /* if first time, transition to sub-tables */
            if (drop == 0)
                drop = root;

            /* increment past last table */
            next += min;            /* here min is 1 << curr */

            /* determine length of next table */
            curr = len - drop;
            left = (int)(1 << curr);
            while (curr + drop < max)
            {
                left -= count[curr + drop];
                if (left <= 0) break;
                curr++;
                left <<= 1;
            }

            /* check for enough space */
            used += 1U << curr;
            if ((type == LENS && used > ENOUGH_LENS) ||
                (type == DISTS && used > ENOUGH_DISTS))
                return 1;

            /* point entry in root table to sub-table */
            low = huff & mask;
            (*table)[low].op = (std::uint8_t)curr;
            (*table)[low].bits = (std::uint8_t)root;
            (*table)[low].val = (std::uint16_t)(next - *table);
        }
    }

    /* fill in remaining table entry if code is incomplete (guaranteed to have
       at most one remaining entry, since if the code is incomplete, the
       maximum code length that was allowed to get this far is one bit) */
    if (huff != 0)
    {
        here.op = (std::uint8_t)64;            /* invalid code marker */
        here.bits = (std::uint8_t)(len - drop);
        here.val = (std::uint16_t)0;
        next[huff] = here;
    }

    /* set return parameters */
    *table += used;
    *bits = root;
    return 0;
}

template<class = void>
codes const&
get_fixed_tables()
{
    struct fixed_codes : codes
    {
        code len[512];
        code dist[32];

        fixed_codes()
        {
            lencode = len;
            distcode = dist;
            lenbits = 9;
            distbits = 5;

            code* next;
            unsigned sym;
            std::uint16_t lens[320];
            std::uint16_t work[288];

            sym = 0;
            while(sym < 144)
                lens[sym++] = 8;
            while(sym < 256)
                lens[sym++] = 9;
            while(sym < 280)
                lens[sym++] = 7;
            while(sym < 288)
                lens[sym++] = 8;
            next = &len[0];
            inflate_table(LENS, lens, 288, &next, &lenbits, work);

            sym = 0;
            while(sym < 32)
                lens[sym++] = 5;
            next = &dist[0];
            inflate_table(DISTS, lens, 32, &next, &distbits, work);

            // VFALCO These hacks work around a bug in Zlib
            len[99].op = 64;
            len[227].op = 64;
            len[355].op = 64;
            len[483].op = 64;
        }
    };

    static fixed_codes const fc;
    return fc;
}

} // beast

#endif
