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

#ifndef BEAST_ZLIB_DEFLATE_STREAM_HPP
#define BEAST_ZLIB_DEFLATE_STREAM_HPP

#include <beast/detail/zlib/zlib.hpp>
#include <beast/detail/zlib/detail/deflate.hpp>
#include <cstdlib>

namespace beast {

// maximum heap size
std::uint16_t constexpr HEAP_SIZE = 2 * limits::lCodes + 1;

// size of bit buffer in bi_buf
std::uint8_t constexpr Buf_size = 16;

// VFALCO THis might not be needed, e.g. for zip/gzip
enum StreamStatus
{
    EXTRA_STATE = 69,
    NAME_STATE = 73,
    COMMENT_STATE = 91,
    HCRC_STATE = 103,
    BUSY_STATE = 113,
    FINISH_STATE = 666
};

struct tree_desc
{
    detail::ct_data *dyn_tree;           /* the dynamic tree */
    int     max_code;            /* largest code with non zero frequency */
    detail::static_tree_desc const* stat_desc; /* the corresponding static tree */
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

//namespace detail {

template<class = void>
class deflate_stream_t : public z_stream
{
public:
    deflate_stream_t();

    ~deflate_stream_t();

    int deflate(int flush);

    int deflateSetDictionary(const Byte *dictionary, uInt  dictLength);

public:
    detail::deflate_tables const& lut_;

    int   status_;        /* as the name implies */
    Byte *pending_buf_;  /* output still pending */
    std::uint32_t   pending_buf_size_; /* size of pending_buf */
    Byte *pending_out_;  /* next pending byte to output to the stream */
    uInt   pending_;      /* nb of bytes in the pending buffer */
    int   last_flush_;    /* value of flush param for previous deflate call */

                /* used by deflate.c: */

    uInt  w_size_;        /* LZ77 window size (32K by default) */
    uInt  w_bits_;        /* log2(w_size)  (8..16) */
    uInt  w_mask_;        /* w_size - 1 */

    /* Sliding window. Input bytes are read into the second half of the window,
     * and move to the first half later to keep a dictionary of at least wSize
     * bytes. With this organization, matches are limited to a distance of
     * wSize-limits::maxMatch bytes, but this ensures that IO is always
     * performed with a length multiple of the block size. Also, it limits
     * the window size to 64K.
     * To do: use the user input buffer as sliding window.
     */
    Byte *window_ = nullptr;

    /* Actual size of window: 2*wSize, except when the user input buffer
     * is directly used as sliding window.
     */
    std::uint32_t window_size_;

    /* Link to older string with same hash index. To limit the size of this
     * array to 64K, this link is maintained only for the last 32K strings.
     * An index in this array is thus a window index modulo 32K.
     */
    std::uint16_t *prev_;

    std::uint16_t *head_; /* Heads of the hash chains or 0. */

    uInt  ins_h_;          /* hash index of string to be inserted */
    uInt  hash_size_;      /* number of elements in hash table */
    uInt  hash_bits_;      /* log2(hash_size) */
    uInt  hash_mask_;      /* hash_size-1 */

    /* Number of bits by which ins_h must be shifted at each input
     * step. It must be such that after limits::minMatch steps, the oldest
     * byte no longer takes part in the hash key, that is:
     *   hash_shift * limits::minMatch >= hash_bits
     */
    uInt  hash_shift_;

    long block_start_;
    /* Window position at the beginning of the current output block. Gets
     * negative when the window is moved backwards.
     */

    uInt match_length_;           /* length of best match */
    IPos prev_match_;             /* previous match */
    int match_available_;         /* set if previous match exists */
    uInt strstart_;               /* start of string to insert */
    uInt match_start_;            /* start of matching string */
    uInt lookahead_;              /* number of valid bytes ahead in window */

    uInt prev_length_;
    /* Length of the best match at previous step. Matches not greater than this
     * are discarded. This is used in the lazy match evaluation.
     */

    uInt max_chain_length_;
    /* To speed up deflation, hash chains are never searched beyond this
     * length.  A higher limit improves compression ratio but degrades the
     * speed.
     */

    uInt max_lazy_match_;
    /* Attempt to find a better match only when the current match is strictly
     * smaller than this value. This mechanism is used only for compression
     * levels >= 4.
     */
    /* OR Insert new strings in the hash table only if the match length is not
     * greater than this length. This saves time but degrades compression.
     * used only for compression levels <= 3.
     */

    int level_;    /* compression level (1..9) */
    int strategy_; /* favor or force Huffman coding*/

    uInt good_match_;
    /* Use a faster search when the previous match is longer than this */

    int nice_match_; /* Stop searching when current match exceeds this */

                /* used by trees.c: */
    /* Didn't use detail::ct_data typedef below to suppress compiler warning */
    detail::ct_data dyn_ltree_[HEAP_SIZE];   /* literal and length tree */
    detail::ct_data dyn_dtree_[2*limits::dCodes+1]; /* distance tree */
    detail::ct_data bl_tree_[2*limits::blCodes+1];  /* Huffman tree for bit lengths */

    tree_desc l_desc_;               /* desc. for literal tree */
    tree_desc d_desc_;               /* desc. for distance tree */
    tree_desc bl_desc_;              /* desc. for bit length tree */

    std::uint16_t bl_count_[limits::maxBits+1];
    /* number of codes at each bit length for an optimal tree */

    int heap_[2*limits::lCodes+1];      /* heap used to build the Huffman trees */
    int heap_len_;               /* number of elements in the heap */
    int heap_max_;               /* element of largest frequency */
    /* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
     * The same heap array is used to build all trees.
     */

    std::uint8_t depth_[2*limits::lCodes+1];
    /* Depth of each subtree used as tie breaker for trees of equal frequency
     */

    std::uint8_t *l_buf_;          /* buffer for literals or lengths */

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
    uInt  lit_bufsize_;

    uInt last_lit_;      /* running index in l_buf */

    std::uint16_t *d_buf_;
    /* Buffer for distances. To simplify the code, d_buf and l_buf have
     * the same number of elements. To use different lengths, an extra flag
     * array would be necessary.
     */

    std::uint32_t opt_len_;        /* bit length of current block with optimal trees */
    std::uint32_t static_len_;     /* bit length of current block with static trees */
    uInt matches_;       /* number of string matches in current block */
    uInt insert_;        /* bytes at end of window left to insert */

    /* Output buffer. bits are inserted starting at the bottom (least
     * significant bits).
     */
    std::uint16_t bi_buf_;

    /* Number of valid bits in bi_buf.  All bits above the last valid bit
     * are always zero.
     */
    int bi_valid_;

    /* High water mark offset in window for initialized bytes -- bytes above
     * this are set to zero in order to avoid memory check warnings when
     * longest match routines access bytes past the input.  This is then
     * updated to the new high water mark.
     */
    std::uint32_t high_water_;

    static void fill_window(deflate_stream_t *s);
    static block_state deflate_stored(deflate_stream_t *s, int flush);
    static block_state deflate_fast(deflate_stream_t *s, int flush);
    static block_state deflate_slow(deflate_stream_t *s, int flush);
    static block_state deflate_rle(deflate_stream_t *s, int flush);
    static block_state deflate_huff(deflate_stream_t *s, int flush);

    static void lm_init        (deflate_stream_t *s);
    static void flush_pending  (deflate_stream_t* strm);
    static int read_buf        (deflate_stream_t* strm, Byte *buf, unsigned size);
    static uInt longest_match  (deflate_stream_t *s, IPos cur_match);

    static int deflateEnd (deflate_stream_t* strm);
    static int deflateResetKeep (deflate_stream_t* strm);
    static int deflateReset (deflate_stream_t* strm);
    static int deflateParams (deflate_stream_t* strm, int level, int strategy);
    static int deflateTune (deflate_stream_t* strm,
        int good_length, int max_lazy, int nice_length, int max_chain);
    static uLong deflateBound (deflate_stream_t* strm, uLong sourceLen);
    static int deflatePending (deflate_stream_t* strm, unsigned *pending, int *bits);
    static int deflatePrime (deflate_stream_t* strm, int bits, int value);
    static int deflateInit (deflate_stream_t* strm, int level);
    static int deflateInit2 (deflate_stream_t* strm, int level, int  method,
        int windowBits, int memLevel, int strategy);

    static void init_block     (deflate_stream_t *s);
    static void pqdownheap     (deflate_stream_t *s, detail::ct_data *tree, int k);
    static void gen_bitlen     (deflate_stream_t *s, tree_desc *desc);
    static void build_tree     (deflate_stream_t *s, tree_desc *desc);
    static void scan_tree      (deflate_stream_t *s, detail::ct_data *tree, int max_code);
    static void send_tree      (deflate_stream_t *s, detail::ct_data *tree, int max_code);
    static int  build_bl_tree  (deflate_stream_t *s);
    static void send_all_trees (deflate_stream_t *s, int lcodes, int dcodes,
                                int blcodes);

    static void _tr_init (deflate_stream_t *s);
    static int _tr_tally (deflate_stream_t *s, unsigned dist, unsigned lc);
    static void _tr_flush_block (deflate_stream_t *s, char *buf,
                            std::uint32_t stored_len, int last);
    static void _tr_flush_bits (deflate_stream_t *s);
    static void _tr_align (deflate_stream_t *s);
    static void _tr_stored_block (deflate_stream_t *s, char *bu,
                            std::uint32_t stored_len, int last);

    static void compress_block (deflate_stream_t *s, const detail::ct_data *ltree,
                                const detail::ct_data *dtree);
    static int  detect_data_type (deflate_stream_t *s);
    static void bi_flush       (deflate_stream_t *s);
    static void bi_windup      (deflate_stream_t *s);
    static void copy_block     (deflate_stream_t *s, char *buf, unsigned len,
                                  int header);

    using compress_func = block_state(*)(deflate_stream_t*, int flush);

    /* Values for max_lazy_match, good_match and max_chain_length, depending on
     * the desired pack level (0..9). The values given below have been tuned to
     * exclude worst case performance for pathological files. Better values may be
     * found for specific files.
     */
    struct config {
       std::uint16_t good_length; /* reduce lazy search above this match length */
       std::uint16_t max_lazy;    /* do not perform lazy search above this match length */
       std::uint16_t nice_length; /* quit search above this match length */
       std::uint16_t max_chain;
       compress_func func;

       config(
               std::uint16_t good_length_,
               std::uint16_t max_lazy_,
               std::uint16_t nice_length_,
               std::uint16_t max_chain_,
               compress_func func_)
           : good_length(good_length_)
           , max_lazy(max_lazy_)
           , nice_length(nice_length_)
           , max_chain(max_chain_)
           , func(func_)
       {
       }
    };

    static
    config
    get_config(std::size_t level)
    {
        switch(level)
        {
        //      good lazy nice chain 
        case 0: return {  0,   0,   0,    0, &deflate_stored}; // store only 
        case 1: return {  4,   4,   8,    4, &deflate_fast};   // max speed, no lazy matches
        case 2: return {  4,   5,  16,    8, &deflate_fast};
        case 3: return {  4,   6,  32,   32, &deflate_fast};
        case 4: return {  4,   4,  16,   16, &deflate_slow};   // lazy matches
        case 5: return {  8,  16,  32,   32, &deflate_slow};
        case 6: return {  8,  16, 128,  128, &deflate_slow};
        case 7: return {  8,  32, 128,  256, &deflate_slow};
        case 8: return { 32, 128, 258, 1024, &deflate_slow};
        default:
        case 9: return { 32, 258, 258, 4096, &deflate_slow};    // max compression
        }
    }
};

//} // detail

using deflate_stream = deflate_stream_t<>;

/* Output a byte on the stream.
 * IN assertion: there is enough room in pending_buf.
 */
#define put_byte(s, c) {s->pending_buf_[s->pending_++] = (c);}


#define MIN_LOOKAHEAD (limits::maxMatch+limits::minMatch+1)
/* Minimum amount of lookahead, except at the end of the input file.
 * See deflate.c for comments about the limits::minMatch+1.
 */

#define MAX_DIST(s)  ((s)->w_size_-MIN_LOOKAHEAD)
/* In order to simplify the code, particularly on 16 bit machines, match
 * distances are limited to MAX_DIST instead of WSIZE.
 */

#define WIN_INIT limits::maxMatch
/* Number of bytes after end of data in window to initialize in order to avoid
   memory checker errors from longest match routines */


/* Mapping from a distance to a distance code. dist is the distance - 1 and
 * must not have side effects. _dist_code[256] and _dist_code[257] are never
 * used.
 */
#define d_code(dist) \
   ((dist) < 256 ? s->lut_.dist_code[dist] : s->lut_.dist_code[256+((dist)>>7)])

} // beast

#include <beast/detail/zlib/impl/deflate_stream.ipp>

#endif
