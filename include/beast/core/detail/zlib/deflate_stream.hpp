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

#ifndef BEAST_CORE_DETAIL_DEFLATE_STREAM_HPP
#define BEAST_CORE_DETAIL_DEFLATE_STREAM_HPP

#include "zutil.hpp"
#include <cstdlib>

namespace beast {

// number of length codes, not counting the special END_BLOCK code
std::uint16_t constexpr LENGTH_CODES = 29;

// number of literal bytes 0..255
std::uint16_t constexpr LITERALS = 256;

// number of Literal or Length codes, including the END_BLOCK code
std::uint16_t constexpr L_CODES = LITERALS + 1 + LENGTH_CODES;

// number of distance codes
std::uint16_t constexpr D_CODES = 30;

// number of codes used to transfer the bit lengths
std::uint16_t constexpr BL_CODES = 19;

// maximum heap size
std::uint16_t constexpr HEAP_SIZE = 2 * L_CODES + 1;

// All codes must not exceed MAX_BITS bits
std::uint8_t constexpr MAX_BITS = 15;

// size of bit buffer in bi_buf
std::uint8_t constexpr Buf_size = 16;

enum StreamStatus
{
    EXTRA_STATE = 69,
    NAME_STATE = 73,
    COMMENT_STATE = 91,
    HCRC_STATE = 103,
    BUSY_STATE = 113,
    FINISH_STATE = 666
};


/* Data structure describing a single value and its code string. */
struct ct_data
{
    union fc_t
    {
        std::uint16_t  freq;       /* frequency count */
        std::uint16_t  code;       /* bit string */
    };
    
    fc_t fc;

    union dl_t
    {
        std::uint16_t  dad;        /* father node in Huffman tree */
        std::uint16_t  len;        /* length of bit string */
    };
    
    dl_t dl;
};

struct static_tree_desc;

struct tree_desc
{
    ct_data *dyn_tree;           /* the dynamic tree */
    int     max_code;            /* largest code with non zero frequency */
    static_tree_desc* stat_desc; /* the corresponding static tree */
};

/* A std::uint16_t is an index in the character window. We use short instead of int to
 * save space in the various tables. IPos is used only for parameter passing.
 */
using IPos = unsigned;

enum block_state
{
    need_more,      /* block not completed, need more input or more output */
    block_done,     /* block flush performed */
    finish_started, /* finish started, need only more output at next deflate */
    finish_done     /* finish done, accept no more input or output */
};

class deflate_stream : public z_stream
{
public:
    deflate_stream();

    ~deflate_stream();

    int deflate(int flush);

    int deflateSetDictionary(const Byte *dictionary, uInt  dictLength);

public:
    int   status;        /* as the name implies */
    Byte *pending_buf;  /* output still pending */
    std::uint32_t   pending_buf_size; /* size of pending_buf */
    Byte *pending_out;  /* next pending byte to output to the stream */
    uInt   pending;      /* nb of bytes in the pending buffer */
    uInt   gzindex;      /* where in extra, name, or comment */
    Byte  method;        /* can only be DEFLATED */
    int   last_flush;    /* value of flush param for previous deflate call */

                /* used by deflate.c: */

    uInt  w_size;        /* LZ77 window size (32K by default) */
    uInt  w_bits;        /* log2(w_size)  (8..16) */
    uInt  w_mask;        /* w_size - 1 */

    Byte *window = nullptr;
    /* Sliding window. Input bytes are read into the second half of the window,
     * and move to the first half later to keep a dictionary of at least wSize
     * bytes. With this organization, matches are limited to a distance of
     * wSize-MAX_MATCH bytes, but this ensures that IO is always
     * performed with a length multiple of the block size. Also, it limits
     * the window size to 64K.
     * To do: use the user input buffer as sliding window.
     */

    std::uint32_t window_size;
    /* Actual size of window: 2*wSize, except when the user input buffer
     * is directly used as sliding window.
     */

    std::uint16_t *prev;
    /* Link to older string with same hash index. To limit the size of this
     * array to 64K, this link is maintained only for the last 32K strings.
     * An index in this array is thus a window index modulo 32K.
     */

    std::uint16_t *head; /* Heads of the hash chains or NIL. */

    uInt  ins_h;          /* hash index of string to be inserted */
    uInt  hash_size;      /* number of elements in hash table */
    uInt  hash_bits;      /* log2(hash_size) */
    uInt  hash_mask;      /* hash_size-1 */

    uInt  hash_shift;
    /* Number of bits by which ins_h must be shifted at each input
     * step. It must be such that after MIN_MATCH steps, the oldest
     * byte no longer takes part in the hash key, that is:
     *   hash_shift * MIN_MATCH >= hash_bits
     */

    long block_start;
    /* Window position at the beginning of the current output block. Gets
     * negative when the window is moved backwards.
     */

    uInt match_length;           /* length of best match */
    IPos prev_match;             /* previous match */
    int match_available;         /* set if previous match exists */
    uInt strstart;               /* start of string to insert */
    uInt match_start;            /* start of matching string */
    uInt lookahead;              /* number of valid bytes ahead in window */

    uInt prev_length;
    /* Length of the best match at previous step. Matches not greater than this
     * are discarded. This is used in the lazy match evaluation.
     */

    uInt max_chain_length;
    /* To speed up deflation, hash chains are never searched beyond this
     * length.  A higher limit improves compression ratio but degrades the
     * speed.
     */

    uInt max_lazy_match;
    /* Attempt to find a better match only when the current match is strictly
     * smaller than this value. This mechanism is used only for compression
     * levels >= 4.
     */
#   define max_insert_length  max_lazy_match
    /* Insert new strings in the hash table only if the match length is not
     * greater than this length. This saves time but degrades compression.
     * max_insert_length is used only for compression levels <= 3.
     */

    int level;    /* compression level (1..9) */
    int strategy; /* favor or force Huffman coding*/

    uInt good_match;
    /* Use a faster search when the previous match is longer than this */

    int nice_match; /* Stop searching when current match exceeds this */

                /* used by trees.c: */
    /* Didn't use ct_data typedef below to suppress compiler warning */
    ct_data dyn_ltree[HEAP_SIZE];   /* literal and length tree */
    ct_data dyn_dtree[2*D_CODES+1]; /* distance tree */
    ct_data bl_tree[2*BL_CODES+1];  /* Huffman tree for bit lengths */

    tree_desc l_desc;               /* desc. for literal tree */
    tree_desc d_desc;               /* desc. for distance tree */
    tree_desc bl_desc;              /* desc. for bit length tree */

    std::uint16_t bl_count[MAX_BITS+1];
    /* number of codes at each bit length for an optimal tree */

    int heap[2*L_CODES+1];      /* heap used to build the Huffman trees */
    int heap_len;               /* number of elements in the heap */
    int heap_max;               /* element of largest frequency */
    /* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
     * The same heap array is used to build all trees.
     */

    std::uint8_t depth[2*L_CODES+1];
    /* Depth of each subtree used as tie breaker for trees of equal frequency
     */

    std::uint8_t *l_buf;          /* buffer for literals or lengths */

    uInt  lit_bufsize;
    /* Size of match buffer for literals/lengths.  There are 4 reasons for
     * limiting lit_bufsize to 64K:
     *   - frequencies can be kept in 16 bit counters
     *   - if compression is not successful for the first block, all input
     *     data is still in the window so we can still emit a stored block even
     *     when input comes from standard input.  (This can also be done for
     *     all blocks if lit_bufsize is not greater than 32K.)
     *   - if compression is not successful for a file smaller than 64K, we can
     *     even emit a stored file instead of a stored block (saving 5 bytes).
     *     This is applicable only for zip (not gzip or zlib).
     *   - creating new Huffman trees less frequently may not provide fast
     *     adaptation to changes in the input data statistics. (Take for
     *     example a binary file with poorly compressible code followed by
     *     a highly compressible string table.) Smaller buffer sizes give
     *     fast adaptation but have of course the overhead of transmitting
     *     trees more frequently.
     *   - I can't count above 4
     */

    uInt last_lit;      /* running index in l_buf */

    std::uint16_t *d_buf;
    /* Buffer for distances. To simplify the code, d_buf and l_buf have
     * the same number of elements. To use different lengths, an extra flag
     * array would be necessary.
     */

    std::uint32_t opt_len;        /* bit length of current block with optimal trees */
    std::uint32_t static_len;     /* bit length of current block with static trees */
    uInt matches;       /* number of string matches in current block */
    uInt insert;        /* bytes at end of window left to insert */

#ifdef DEBUG
    std::uint32_t compressed_len; /* total bit length of compressed file mod 2^32 */
    std::uint32_t bits_sent;      /* bit length of compressed data sent mod 2^32 */
#endif

    std::uint16_t bi_buf;
    /* Output buffer. bits are inserted starting at the bottom (least
     * significant bits).
     */
    int bi_valid;
    /* Number of valid bits in bi_buf.  All bits above the last valid bit
     * are always zero.
     */

    std::uint32_t high_water;
    /* High water mark offset in window for initialized bytes -- bytes above
     * this are set to zero in order to avoid memory check warnings when
     * longest match routines access bytes past the input.  This is then
     * updated to the new high water mark.
     */

    static void fill_window(deflate_stream *s);
    static block_state deflate_stored(deflate_stream *s, int flush);
    static block_state deflate_fast(deflate_stream *s, int flush);
    static block_state deflate_slow(deflate_stream *s, int flush);
    static block_state deflate_rle(deflate_stream *s, int flush);
    static block_state deflate_huff(deflate_stream *s, int flush);

    static void lm_init        (deflate_stream *s);
    static void flush_pending  (deflate_stream* strm);
    static int read_buf        (deflate_stream* strm, Byte *buf, unsigned size);
    static uInt longest_match  (deflate_stream *s, IPos cur_match);

    static int deflateEnd (deflate_stream* strm);
    static int deflateResetKeep (deflate_stream* strm);
    static int deflateReset (deflate_stream* strm);
    static int deflateParams (deflate_stream* strm, int level, int strategy);
    static int deflateTune (deflate_stream* strm,
        int good_length, int max_lazy, int nice_length, int max_chain);
    static uLong deflateBound (deflate_stream* strm, uLong sourceLen);
    static int deflatePending (deflate_stream* strm, unsigned *pending, int *bits);
    static int deflatePrime (deflate_stream* strm, int bits, int value);
    static int deflateInit (deflate_stream* strm, int level);
    static int deflateInit2 (deflate_stream* strm, int level, int  method,
        int windowBits, int memLevel, int strategy);
};

/* Output a byte on the stream.
 * IN assertion: there is enough room in pending_buf.
 */
#define put_byte(s, c) {s->pending_buf[s->pending++] = (c);}


#define MIN_LOOKAHEAD (MAX_MATCH+MIN_MATCH+1)
/* Minimum amount of lookahead, except at the end of the input file.
 * See deflate.c for comments about the MIN_MATCH+1.
 */

#define MAX_DIST(s)  ((s)->w_size-MIN_LOOKAHEAD)
/* In order to simplify the code, particularly on 16 bit machines, match
 * distances are limited to MAX_DIST instead of WSIZE.
 */

#define WIN_INIT MAX_MATCH
/* Number of bytes after end of data in window to initialize in order to avoid
   memory checker errors from longest match routines */

        /* in trees.c */
void _tr_init (deflate_stream *s);
int _tr_tally (deflate_stream *s, unsigned dist, unsigned lc);
void _tr_flush_block (deflate_stream *s, char *buf,
                        std::uint32_t stored_len, int last);
void _tr_flush_bits (deflate_stream *s);
void _tr_align (deflate_stream *s);
void _tr_stored_block (deflate_stream *s, char *bu,
                        std::uint32_t stored_len, int last);

#define d_code(dist) \
   ((dist) < 256 ? _dist_code[dist] : _dist_code[256+((dist)>>7)])
/* Mapping from a distance to a distance code. dist is the distance - 1 and
 * must not have side effects. _dist_code[256] and _dist_code[257] are never
 * used.
 */

#ifndef DEBUG
/* Inline versions of _tr_tally for speed: */
#if defined(GEN_TREES_H)
  extern std::uint8_t _length_code[];
  extern std::uint8_t _dist_code[];
#else
  extern const std::uint8_t _length_code[];
  extern const std::uint8_t _dist_code[];
#endif
#endif

} // beast

#endif
