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

#ifndef BEAST_ZLIB_IMPL_BASIC_DEFLATE_STREAM_IPP
#define BEAST_ZLIB_IMPL_BASIC_DEFLATE_STREAM_IPP

#include <beast/detail/zlib/detail/deflate.hpp>
#include <cassert>
#include <cstring>
#include <memory>

namespace beast {
namespace zlib {

/*
 *  ALGORITHM
 *
 *      The "deflation" process depends on being able to identify portions
 *      of the input text which are identical to earlier input (within a
 *      sliding window trailing behind the input currently being processed).
 *
 *      Each code tree is stored in a compressed form which is itself
 *      a Huffman encoding of the lengths of all the code strings (in
 *      ascending order by source values).  The actual code strings are
 *      reconstructed from the lengths in the inflate process, as described
 *      in the deflate specification.
 *
 *      The most straightforward technique turns out to be the fastest for
 *      most input files: try all possible matches and select the longest.
 *      The key feature of this algorithm is that insertions into the string
 *      dictionary are very simple and thus fast, and deletions are avoided
 *      completely. Insertions are performed at each input character, whereas
 *      string matches are performed only when the previous match ends. So it
 *      is preferable to spend more time in matches to allow very fast string
 *      insertions and avoid deletions. The matching algorithm for small
 *      strings is inspired from that of Rabin & Karp. A brute force approach
 *      is used to find longer strings when a small match has been found.
 *      A similar algorithm is used in comic (by Jan-Mark Wams) and freeze
 *      (by Leonid Broukhis).
 *         A previous version of this file used a more sophisticated algorithm
 *      (by Fiala and Greene) which is guaranteed to run in linear amortized
 *      time, but has a larger average cost, uses more memory and is patented.
 *      However the F&G algorithm may be faster for some highly redundant
 *      files if the parameter max_chain_length (described below) is too large.
 *
 *  ACKNOWLEDGEMENTS
 *
 *      The idea of lazy evaluation of matches is due to Jan-Mark Wams, and
 *      I found it in 'freeze' written by Leonid Broukhis.
 *      Thanks to many people for bug reports and testing.
 *
 *  REFERENCES
 *
 *      Deutsch, L.P.,"DEFLATE Compressed Data Format Specification".
 *      Available in http://tools.ietf.org/html/rfc1951
 *
 *      A description of the Rabin and Karp algorithm is given in the book
 *         "Algorithms" by R. Sedgewick, Addison-Wesley, p252.
 *
 *      Fiala,E.R., and Greene,D.H.
 *         Data Compression with Finite Windows, Comm.ACM, 32,4 (1989) 490-595
 *
 */

template<class Allocator>
basic_deflate_stream<Allocator>::
basic_deflate_stream()
    : lut_(detail::get_deflate_tables())
{
    // default level 6
    //deflateInit2(this, 6, Z_DEFLATED, 15, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);
}

template<class Allocator>
basic_deflate_stream<Allocator>::
~basic_deflate_stream()
{
    deflateEnd(this);
}
















/* Send a code of the given tree. c and tree must not have side effects */
#  define send_code(s, c, tree) send_bits(s, tree[c].fc, tree[c].dl)

/* ===========================================================================
 * Output a short LSB first on the stream.
 * IN assertion: there is enough room in pendingBuf.
 */
#define put_short(s, w) { \
    put_byte(s, (std::uint8_t)((w) & 0xff)); \
    put_byte(s, (std::uint8_t)((std::uint16_t)(w) >> 8)); \
}

/* ===========================================================================
 * Send a value on a given number of bits.
 * IN assertion: length <= 16 and value fits in length bits.
 */
#define send_bits(s, value, length) \
{ int len = length;\
  if (s->bi_valid_ > (int)Buf_size - len) {\
    int val = value;\
    s->bi_buf_ |= (std::uint16_t)val << s->bi_valid_;\
    put_short(s, s->bi_buf_);\
    s->bi_buf_ = (std::uint16_t)val >> (Buf_size - s->bi_valid_);\
    s->bi_valid_ += len - Buf_size;\
  } else {\
    s->bi_buf_ |= (std::uint16_t)(value) << s->bi_valid_;\
    s->bi_valid_ += len;\
  }\
}

/* ===========================================================================
 * Initialize the tree data structures for a new zlib stream.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
_tr_init(basic_deflate_stream *s)
{
    s->l_desc_.dyn_tree = s->dyn_ltree_;
    s->l_desc_.stat_desc = &s->lut_.l_desc;

    s->d_desc_.dyn_tree = s->dyn_dtree_;
    s->d_desc_.stat_desc = &s->lut_.d_desc;

    s->bl_desc_.dyn_tree = s->bl_tree_;
    s->bl_desc_.stat_desc = &s->lut_.bl_desc;

    s->bi_buf_ = 0;
    s->bi_valid_ = 0;

    /* Initialize the first block of the first file: */
    init_block(s);
}

/* ===========================================================================
 * Initialize a new block.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
init_block(basic_deflate_stream *s)
{
    int n; /* iterates over tree elements */

    /* Initialize the trees. */
    for (n = 0; n < limits::lCodes;  n++) s->dyn_ltree_[n].fc = 0;
    for (n = 0; n < limits::dCodes;  n++) s->dyn_dtree_[n].fc = 0;
    for (n = 0; n < limits::blCodes; n++) s->bl_tree_[n].fc = 0;

    s->dyn_ltree_[END_BLOCK].fc = 1;
    s->opt_len_ = s->static_len_ = 0L;
    s->last_lit_ = s->matches_ = 0;
}

#define SMALLEST 1
/* Index within the heap array of least frequent node in the Huffman tree */


/* ===========================================================================
 * Remove the smallest element from the heap and recreate the heap with
 * one less element. Updates heap and heap_len.
 */
#define pqremove(s, tree, top) \
{\
    top = s->heap_[SMALLEST]; \
    s->heap_[SMALLEST] = s->heap_[s->heap_len_--]; \
    pqdownheap(s, tree, SMALLEST); \
}

/* ===========================================================================
 * Compares to subtrees, using the tree depth as tie breaker when
 * the subtrees have equal frequency. This minimizes the worst case length.
 */
#define smaller(tree, n, m, depth) \
   (tree[n].fc < tree[m].fc || \
   (tree[n].fc == tree[m].fc && depth[n] <= depth[m]))

/* ===========================================================================
 * Restore the heap property by moving down the tree starting at node k,
 * exchanging a node with the smallest of its two sons if necessary, stopping
 * when the heap property is re-established (each father smaller than its
 * two sons).
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
pqdownheap(
    basic_deflate_stream *s,
    detail::ct_data *tree,  /* the tree to restore */
    int k)               /* node to move down */
{
    int v = s->heap_[k];
    int j = k << 1;  /* left son of k */
    while (j <= s->heap_len_) {
        /* Set j to the smallest of the two sons: */
        if (j < s->heap_len_ &&
            smaller(tree, s->heap_[j+1], s->heap_[j], s->depth_)) {
            j++;
        }
        /* Exit if v is smaller than both sons */
        if (smaller(tree, v, s->heap_[j], s->depth_)) break;

        /* Exchange v with the smallest son */
        s->heap_[k] = s->heap_[j];  k = j;

        /* And continue down the tree, setting j to the left son of k */
        j <<= 1;
    }
    s->heap_[k] = v;
}

/* ===========================================================================
 * Compute the optimal bit lengths for a tree and update the total bit length
 * for the current block.
 * IN assertion: the fields freq and dad are set, heap[heap_max] and
 *    above are the tree nodes sorted by increasing frequency.
 * OUT assertions: the field len is set to the optimal bit length, the
 *     array bl_count contains the frequencies for each bit length.
 *     The length opt_len is updated; static_len is also updated if stree is
 *     not null.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
gen_bitlen(
    basic_deflate_stream *s,
    tree_desc *desc)    /* the tree descriptor */
{
    detail::ct_data *tree        = desc->dyn_tree;
    int max_code         = desc->max_code;
    const detail::ct_data *stree = desc->stat_desc->static_tree;
    std::uint8_t const *extra    = desc->stat_desc->extra_bits;
    int base             = desc->stat_desc->extra_base;
    int max_length       = desc->stat_desc->max_length;
    int h;              /* heap index */
    int n, m;           /* iterate over the tree elements */
    int bits;           /* bit length */
    int xbits;          /* extra bits */
    std::uint16_t f;              /* frequency */
    int overflow = 0;   /* number of elements with bit length too large */

    for (bits = 0; bits <= limits::maxBits; bits++) s->bl_count_[bits] = 0;

    /* In a first pass, compute the optimal bit lengths (which may
     * overflow in the case of the bit length tree).
     */
    tree[s->heap_[s->heap_max_]].dl = 0; /* root of the heap */

    for (h = s->heap_max_+1; h < HEAP_SIZE; h++) {
        n = s->heap_[h];
        bits = tree[tree[n].dl].dl + 1;
        if (bits > max_length) bits = max_length, overflow++;
        tree[n].dl = (std::uint16_t)bits;
        /* We overwrite tree[n].dl which is no longer needed */

        if (n > max_code) continue; /* not a leaf node */

        s->bl_count_[bits]++;
        xbits = 0;
        if (n >= base) xbits = extra[n-base];
        f = tree[n].fc;
        s->opt_len_ += (std::uint32_t)f * (bits + xbits);
        if (stree) s->static_len_ += (std::uint32_t)f * (stree[n].dl + xbits);
    }
    if (overflow == 0) return;

    Trace((stderr,"\nbit length overflow\n"));
    /* This happens for example on obj2 and pic of the Calgary corpus */

    /* Find the first bit length which could increase: */
    do {
        bits = max_length-1;
        while (s->bl_count_[bits] == 0) bits--;
        s->bl_count_[bits]--;      /* move one leaf down the tree */
        s->bl_count_[bits+1] += 2; /* move one overflow item as its brother */
        s->bl_count_[max_length]--;
        /* The brother of the overflow item also moves one step up,
         * but this does not affect bl_count[max_length]
         */
        overflow -= 2;
    } while (overflow > 0);

    /* Now recompute all bit lengths, scanning in increasing frequency.
     * h is still equal to HEAP_SIZE. (It is simpler to reconstruct all
     * lengths instead of fixing only the wrong ones. This idea is taken
     * from 'ar' written by Haruhiko Okumura.)
     */
    for (bits = max_length; bits != 0; bits--) {
        n = s->bl_count_[bits];
        while (n != 0) {
            m = s->heap_[--h];
            if (m > max_code) continue;
            if ((unsigned) tree[m].dl != (unsigned) bits) {
                Trace((stderr,"code %d bits %d->%d\n", m, tree[m].dl, bits));
                s->opt_len_ += ((long)bits - (long)tree[m].dl)
                              *(long)tree[m].fc;
                tree[m].dl = (std::uint16_t)bits;
            }
            n--;
        }
    }
}

/* ===========================================================================
 * Construct one Huffman tree and assigns the code bit strings and lengths.
 * Update the total bit length for the current block.
 * IN assertion: the field freq is set for all tree elements.
 * OUT assertions: the fields len and code are set to the optimal bit length
 *     and corresponding code. The length opt_len is updated; static_len is
 *     also updated if stree is not null. The field max_code is set.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
build_tree(
    basic_deflate_stream *s,
    tree_desc *desc) /* the tree descriptor */
{
    detail::ct_data *tree         = desc->dyn_tree;
    const detail::ct_data *stree  = desc->stat_desc->static_tree;
    int elems             = desc->stat_desc->elems;
    int n, m;          /* iterate over heap elements */
    int max_code = -1; /* largest code with non zero frequency */
    int node;          /* new node being created */

    /* Construct the initial heap, with least frequent element in
     * heap[SMALLEST]. The sons of heap[n] are heap[2*n] and heap[2*n+1].
     * heap[0] is not used.
     */
    s->heap_len_ = 0, s->heap_max_ = HEAP_SIZE;

    for (n = 0; n < elems; n++) {
        if (tree[n].fc != 0) {
            s->heap_[++(s->heap_len_)] = max_code = n;
            s->depth_[n] = 0;
        } else {
            tree[n].dl = 0;
        }
    }

    /* The pkzip format requires that at least one distance code exists,
     * and that at least one bit should be sent even if there is only one
     * possible code. So to avoid special checks later on we force at least
     * two codes of non zero frequency.
     */
    while (s->heap_len_ < 2) {
        node = s->heap_[++(s->heap_len_)] = (max_code < 2 ? ++max_code : 0);
        tree[node].fc = 1;
        s->depth_[node] = 0;
        s->opt_len_--; if (stree) s->static_len_ -= stree[node].dl;
        /* node is 0 or 1 so it does not have extra bits */
    }
    desc->max_code = max_code;

    /* The elements heap[heap_len/2+1 .. heap_len] are leaves of the tree,
     * establish sub-heaps of increasing lengths:
     */
    for (n = s->heap_len_/2; n >= 1; n--) pqdownheap(s, tree, n);

    /* Construct the Huffman tree by repeatedly combining the least two
     * frequent nodes.
     */
    node = elems;              /* next internal node of the tree */
    do {
        pqremove(s, tree, n);  /* n = node of least frequency */
        m = s->heap_[SMALLEST]; /* m = node of next least frequency */

        s->heap_[--(s->heap_max_)] = n; /* keep the nodes sorted by frequency */
        s->heap_[--(s->heap_max_)] = m;

        /* Create a new node father of n and m */
        tree[node].fc = tree[n].fc + tree[m].fc;
        s->depth_[node] = (std::uint8_t)((s->depth_[n] >= s->depth_[m] ?
                                s->depth_[n] : s->depth_[m]) + 1);
        tree[n].dl = tree[m].dl = (std::uint16_t)node;
        /* and insert the new node in the heap */
        s->heap_[SMALLEST] = node++;
        pqdownheap(s, tree, SMALLEST);

    } while (s->heap_len_ >= 2);

    s->heap_[--(s->heap_max_)] = s->heap_[SMALLEST];

    /* At this point, the fields freq and dad are set. We can now
     * generate the bit lengths.
     */
    gen_bitlen(s, (tree_desc *)desc);

    /* The field len is now set, we can generate the bit codes */
    detail::gen_codes (tree, max_code, s->bl_count_);
}

/* ===========================================================================
 * Scan a literal or distance tree to determine the frequencies of the codes
 * in the bit length tree.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
scan_tree(
    basic_deflate_stream *s,
    detail::ct_data *tree,   /* the tree to be scanned */
    int max_code)    /* and its largest code of non zero frequency */
{
    int n;                     /* iterates over all tree elements */
    int prevlen = -1;          /* last emitted length */
    int curlen;                /* length of current code */
    int nextlen = tree[0].dl; /* length of next code */
    int count = 0;             /* repeat count of the current code */
    int max_count = 7;         /* max repeat count */
    int min_count = 4;         /* min repeat count */

    if (nextlen == 0) max_count = 138, min_count = 3;
    tree[max_code+1].dl = (std::uint16_t)0xffff; /* guard */

    for (n = 0; n <= max_code; n++) {
        curlen = nextlen; nextlen = tree[n+1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            s->bl_tree_[curlen].fc += count;
        } else if (curlen != 0) {
            if (curlen != prevlen) s->bl_tree_[curlen].fc++;
            s->bl_tree_[REP_3_6].fc++;
        } else if (count <= 10) {
            s->bl_tree_[REPZ_3_10].fc++;
        } else {
            s->bl_tree_[REPZ_11_138].fc++;
        }
        count = 0; prevlen = curlen;
        if (nextlen == 0) {
            max_count = 138, min_count = 3;
        } else if (curlen == nextlen) {
            max_count = 6, min_count = 3;
        } else {
            max_count = 7, min_count = 4;
        }
    }
}

/* ===========================================================================
 * Send a literal or distance tree in compressed form, using the codes in
 * bl_tree.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
send_tree (
    basic_deflate_stream *s,
    detail::ct_data *tree, /* the tree to be scanned */
    int max_code)       /* and its largest code of non zero frequency */
{
    int n;                     /* iterates over all tree elements */
    int prevlen = -1;          /* last emitted length */
    int curlen;                /* length of current code */
    int nextlen = tree[0].dl; /* length of next code */
    int count = 0;             /* repeat count of the current code */
    int max_count = 7;         /* max repeat count */
    int min_count = 4;         /* min repeat count */

    /* tree[max_code+1].dl = -1; */  /* guard already set */
    if (nextlen == 0) max_count = 138, min_count = 3;

    for (n = 0; n <= max_code; n++) {
        curlen = nextlen; nextlen = tree[n+1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            do { send_code(s, curlen, s->bl_tree_); } while (--count != 0);

        } else if (curlen != 0) {
            if (curlen != prevlen) {
                send_code(s, curlen, s->bl_tree_); count--;
            }
            Assert(count >= 3 && count <= 6, " 3_6?");
            send_code(s, REP_3_6, s->bl_tree_); send_bits(s, count-3, 2);

        } else if (count <= 10) {
            send_code(s, REPZ_3_10, s->bl_tree_); send_bits(s, count-3, 3);

        } else {
            send_code(s, REPZ_11_138, s->bl_tree_); send_bits(s, count-11, 7);
        }
        count = 0; prevlen = curlen;
        if (nextlen == 0) {
            max_count = 138, min_count = 3;
        } else if (curlen == nextlen) {
            max_count = 6, min_count = 3;
        } else {
            max_count = 7, min_count = 4;
        }
    }
}

/* ===========================================================================
 * Construct the Huffman tree for the bit lengths and return the index in
 * bl_order of the last bit length code to send.
 */
template<class Allocator>
int
basic_deflate_stream<Allocator>::
build_bl_tree(basic_deflate_stream *s)
{
    int max_blindex;  /* index of last bit length code of non zero freq */

    /* Determine the bit length frequencies for literal and distance trees */
    scan_tree(s, (detail::ct_data *)s->dyn_ltree_, s->l_desc_.max_code);
    scan_tree(s, (detail::ct_data *)s->dyn_dtree_, s->d_desc_.max_code);

    /* Build the bit length tree: */
    build_tree(s, (tree_desc *)(&(s->bl_desc_)));
    /* opt_len now includes the length of the tree representations, except
     * the lengths of the bit lengths codes and the 5+5+4 bits for the counts.
     */

    /* Determine the number of bit length codes to send. The pkzip format
     * requires that at least 4 bit length codes be sent. (appnote.txt says
     * 3 but the actual value used is 4.)
     */
    for (max_blindex = limits::blCodes-1; max_blindex >= 3; max_blindex--) {
        if (s->bl_tree_[s->lut_.bl_order[max_blindex]].dl != 0) break;
    }
    /* Update opt_len to include the bit length tree and counts */
    s->opt_len_ += 3*(max_blindex+1) + 5+5+4;
    Tracev((stderr, "\ndyn trees: dyn %ld, stat %ld",
            s->opt_len_, s->static_len_));

    return max_blindex;
}

/* ===========================================================================
 * Send the header for a block using dynamic Huffman trees: the counts, the
 * lengths of the bit length codes, the literal tree and the distance tree.
 * IN assertion: lcodes >= 257, dcodes >= 1, blcodes >= 4.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
send_all_trees(
    basic_deflate_stream *s,
    int lcodes,
    int dcodes,
    int blcodes) /* number of codes for each tree */
{
    int rank;                    /* index in bl_order */

    Assert (lcodes >= 257 && dcodes >= 1 && blcodes >= 4, "not enough codes");
    Assert (lcodes <= limits::lCodes && dcodes <= limits::dCodes && blcodes <= limits::blCodes,
            "too many codes");
    Tracev((stderr, "\nbl counts: "));
    send_bits(s, lcodes-257, 5); /* not +255 as stated in appnote.txt */
    send_bits(s, dcodes-1,   5);
    send_bits(s, blcodes-4,  4); /* not -3 as stated in appnote.txt */
    for (rank = 0; rank < blcodes; rank++) {
        Tracev((stderr, "\nbl code %2d ", bl_order[rank]));
        send_bits(s, s->bl_tree_[s->lut_.bl_order[rank]].dl, 3);
    }
    Tracev((stderr, "\nbl tree: sent %ld", s->bits_sent_));

    send_tree(s, (detail::ct_data *)s->dyn_ltree_, lcodes-1); /* literal tree */
    Tracev((stderr, "\nlit tree: sent %ld", s->bits_sent_));

    send_tree(s, (detail::ct_data *)s->dyn_dtree_, dcodes-1); /* distance tree */
    Tracev((stderr, "\ndist tree: sent %ld", s->bits_sent_));
}

/* ===========================================================================
 * Send a stored block
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
_tr_stored_block(
    basic_deflate_stream *s,
    char *buf,       /* input block */
    std::uint32_t stored_len,   /* length of input block */
    int last)         /* one if this is the last block for a file */
{
    send_bits(s, (STORED_BLOCK<<1)+last, 3);    /* send block type */
    copy_block(s, buf, (unsigned)stored_len, 1); /* with header */
}

/* ===========================================================================
 * Flush the bits in the bit buffer to pending output (leaves at most 7 bits)
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
_tr_flush_bits(basic_deflate_stream *s)
{
    bi_flush(s);
}

/* ===========================================================================
 * Send one empty static block to give enough lookahead for inflate.
 * This takes 10 bits, of which 7 may remain in the bit buffer.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
_tr_align(basic_deflate_stream *s)
{
    send_bits(s, STATIC_TREES<<1, 3);
    send_code(s, END_BLOCK, s->lut_.ltree);
    bi_flush(s);
}

/* ===========================================================================
 * Determine the best encoding for the current block: dynamic trees, static
 * trees or store, and output the encoded block to the zip file.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
_tr_flush_block(
    basic_deflate_stream *s,
    char *buf,       /* input block, or NULL if too old */
    std::uint32_t stored_len,   /* length of input block */
    int last)         /* one if this is the last block for a file */
{
    std::uint32_t opt_lenb, static_lenb; /* opt_len and static_len in bytes */
    int max_blindex = 0;  /* index of last bit length code of non zero freq */

    /* Build the Huffman trees unless a stored block is forced */
    if (s->level_ > 0) {

        /* Check if the file is binary or text */
        if (s->data_type == Z_UNKNOWN)
            s->data_type = detect_data_type(s);

        /* Construct the literal and distance trees */
        build_tree(s, (tree_desc *)(&(s->l_desc_)));
        Tracev((stderr, "\nlit data: dyn %ld, stat %ld", s->opt_len_,
                s->static_len_));

        build_tree(s, (tree_desc *)(&(s->d_desc_)));
        Tracev((stderr, "\ndist data: dyn %ld, stat %ld", s->opt_len_,
                s->static_len_));
        /* At this point, opt_len and static_len are the total bit lengths of
         * the compressed block data, excluding the tree representations.
         */

        /* Build the bit length tree for the above two trees, and get the index
         * in bl_order of the last bit length code to send.
         */
        max_blindex = build_bl_tree(s);

        /* Determine the best encoding. Compute the block lengths in bytes. */
        opt_lenb = (s->opt_len_+3+7)>>3;
        static_lenb = (s->static_len_+3+7)>>3;

        Tracev((stderr, "\nopt %lu(%lu) stat %lu(%lu) stored %lu lit %u ",
                opt_lenb, s->opt_len_, static_lenb, s->static_len_, stored_len,
                s->last_lit_));

        if (static_lenb <= opt_lenb) opt_lenb = static_lenb;

    } else {
        Assert(buf != (char*)0, "lost buf");
        opt_lenb = static_lenb = stored_len + 5; /* force a stored block */
    }

#ifdef FORCE_STORED
    if (buf != (char*)0) { /* force stored block */
#else
    if (stored_len+4 <= opt_lenb && buf != (char*)0) {
                       /* 4: two words for the lengths */
#endif
        /* The test buf != NULL is only necessary if LIT_BUFSIZE > WSIZE.
         * Otherwise we can't have processed more than WSIZE input bytes since
         * the last block flush, because compression would have been
         * successful. If LIT_BUFSIZE <= WSIZE, it is never too late to
         * transform a block into a stored block.
         */
        _tr_stored_block(s, buf, stored_len, last);

#ifdef FORCE_STATIC
    } else if (static_lenb >= 0) { /* force static trees */
#else
    } else if (s->strategy_ == Z_FIXED || static_lenb == opt_lenb) {
#endif
        send_bits(s, (STATIC_TREES<<1)+last, 3);
        compress_block(s, s->lut_.ltree, s->lut_.dtree);
    } else {
        send_bits(s, (DYN_TREES<<1)+last, 3);
        send_all_trees(s, s->l_desc_.max_code+1, s->d_desc_.max_code+1,
                       max_blindex+1);
        compress_block(s, (const detail::ct_data *)s->dyn_ltree_,
                       (const detail::ct_data *)s->dyn_dtree_);
    }
    Assert (s->compressed_len_ == s->bits_sent_, "bad compressed size");
    /* The above check is made mod 2^32, for files larger than 512 MB
     * and uLong implemented on 32 bits.
     */
    init_block(s);

    if (last) {
        bi_windup(s);
    }
    Tracev((stderr,"\ncomprlen %lu(%lu) ", s->compressed_len_>>3,
           s->compressed_len_-7*last));
}

/* ===========================================================================
 * Save the match info and tally the frequency counts. Return true if
 * the current block must be flushed.
 */
template<class Allocator>
int
basic_deflate_stream<Allocator>::
_tr_tally (
    basic_deflate_stream *s,
    unsigned dist,  /* distance of matched string */
    unsigned lc)    /* match length-limits::minMatch or unmatched char (if dist==0) */
{
    s->d_buf_[s->last_lit_] = (std::uint16_t)dist;
    s->l_buf_[s->last_lit_++] = (std::uint8_t)lc;
    if (dist == 0) {
        /* lc is the unmatched char */
        s->dyn_ltree_[lc].fc++;
    } else {
        s->matches_++;
        /* Here, lc is the match length - limits::minMatch */
        dist--;             /* dist = match distance - 1 */
        Assert((std::uint16_t)dist < (std::uint16_t)MAX_DIST(s) &&
               (std::uint16_t)lc <= (std::uint16_t)(limits::maxMatch-limits::minMatch) &&
               (std::uint16_t)d_code(dist) < (std::uint16_t)limits::dCodes,  "_tr_tally: bad match");

        s->dyn_ltree_[s->lut_.length_code[lc]+limits::literals+1].fc++;
        s->dyn_dtree_[d_code(dist)].fc++;
    }

#ifdef TRUNCATE_BLOCK
    /* Try to guess if it is profitable to stop the current block here */
    if ((s->last_lit_ & 0x1fff) == 0 && s->level > 2) {
        /* Compute an upper bound for the compressed length */
        std::uint32_t out_length = (std::uint32_t)s->last_lit_*8L;
        std::uint32_t in_length = (std::uint32_t)((long)s->strstart - s->block_start);
        int dcode;
        for (dcode = 0; dcode < limits::dCodes; dcode++) {
            out_length += (std::uint32_t)s->dyn_dtree_[dcode].fc *
                (5L+extra_dbits[dcode]);
        }
        out_length >>= 3;
        Tracev((stderr,"\nlast_lit %u, in %ld, out ~%ld(%ld%%) ",
               s->last_lit_, in_length, out_length,
               100L - out_length*100L/in_length));
        if (s->matches_ < s->last_lit_/2 && out_length < in_length/2) return 1;
    }
#endif
    return (s->last_lit_ == s->lit_bufsize_-1);
    /* We avoid equality with lit_bufsize because of wraparound at 64K
     * on 16 bit machines and because stored blocks are restricted to
     * 64K-1 bytes.
     */
}

/* ===========================================================================
 * Send the block data compressed using the given Huffman trees
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
compress_block(
    basic_deflate_stream *s,
    const detail::ct_data *ltree, /* literal tree */
    const detail::ct_data *dtree) /* distance tree */
{
    unsigned dist;      /* distance of matched string */
    int lc;             /* match length or unmatched char (if dist == 0) */
    unsigned lx = 0;    /* running index in l_buf */
    unsigned code;      /* the code to send */
    int extra;          /* number of extra bits to send */

    if (s->last_lit_ != 0) do {
        dist = s->d_buf_[lx];
        lc = s->l_buf_[lx++];
        if (dist == 0) {
            send_code(s, lc, ltree); /* send a literal byte */
            Tracecv(isgraph(lc), (stderr," '%c' ", lc));
        } else {
            /* Here, lc is the match length - limits::minMatch */
            code = s->lut_.length_code[lc];
            send_code(s, code+limits::literals+1, ltree); /* send the length code */
            extra = s->lut_.extra_lbits[code];
            if (extra != 0) {
                lc -= s->lut_.base_length[code];
                send_bits(s, lc, extra);       /* send the extra length bits */
            }
            dist--; /* dist is now the match distance - 1 */
            code = d_code(dist);
            Assert (code < limits::dCodes, "bad d_code");

            send_code(s, code, dtree);       /* send the distance code */
            extra = s->lut_.extra_dbits[code];
            if (extra != 0) {
                dist -= s->lut_.base_dist[code];
                send_bits(s, dist, extra);   /* send the extra distance bits */
            }
        } /* literal or match pair ? */

        /* Check that the overlay between pending_buf and d_buf+l_buf is ok: */
        Assert((uInt)(s->pending_) < s->lit_bufsize_ + 2*lx,
               "pendingBuf overflow");

    } while (lx < s->last_lit_);

    send_code(s, END_BLOCK, ltree);
}

/* ===========================================================================
 * Check if the data type is TEXT or BINARY, using the following algorithm:
 * - TEXT if the two conditions below are satisfied:
 *    a) There are no non-portable control characters belonging to the
 *       "black list" (0..6, 14..25, 28..31).
 *    b) There is at least one printable character belonging to the
 *       "white list" (9 {TAB}, 10 {LF}, 13 {CR}, 32..255).
 * - BINARY otherwise.
 * - The following partially-portable control characters form a
 *   "gray list" that is ignored in this detection algorithm:
 *   (7 {BEL}, 8 {BS}, 11 {VT}, 12 {FF}, 26 {SUB}, 27 {ESC}).
 * IN assertion: the fields fc of dyn_ltree are set.
 */
template<class Allocator>
int
basic_deflate_stream<Allocator>::
detect_data_type(basic_deflate_stream *s)
{
    /* black_mask is the bit mask of black-listed bytes
     * set bits 0..6, 14..25, and 28..31
     * 0xf3ffc07f = binary 11110011111111111100000001111111
     */
    unsigned long black_mask = 0xf3ffc07fUL;
    int n;

    /* Check for non-textual ("black-listed") bytes. */
    for (n = 0; n <= 31; n++, black_mask >>= 1)
        if ((black_mask & 1) && (s->dyn_ltree_[n].fc != 0))
            return Z_BINARY;

    /* Check for textual ("white-listed") bytes. */
    if (s->dyn_ltree_[9].fc != 0 || s->dyn_ltree_[10].fc != 0
            || s->dyn_ltree_[13].fc != 0)
        return Z_TEXT;
    for (n = 32; n < limits::literals; n++)
        if (s->dyn_ltree_[n].fc != 0)
            return Z_TEXT;

    /* There are no "black-listed" or "white-listed" bytes:
     * this stream either is empty or has tolerated ("gray-listed") bytes only.
     */
    return Z_BINARY;
}

/* ===========================================================================
 * Flush the bit buffer, keeping at most 7 bits in it.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
bi_flush(
    basic_deflate_stream *s)
{
    if (s->bi_valid_ == 16) {
        put_short(s, s->bi_buf_);
        s->bi_buf_ = 0;
        s->bi_valid_ = 0;
    } else if (s->bi_valid_ >= 8) {
        put_byte(s, (Byte)s->bi_buf_);
        s->bi_buf_ >>= 8;
        s->bi_valid_ -= 8;
    }
}

/* ===========================================================================
 * Flush the bit buffer and align the output on a byte boundary
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
bi_windup(basic_deflate_stream *s)
{
    if (s->bi_valid_ > 8) {
        put_short(s, s->bi_buf_);
    } else if (s->bi_valid_ > 0) {
        put_byte(s, (Byte)s->bi_buf_);
    }
    s->bi_buf_ = 0;
    s->bi_valid_ = 0;
}

/* ===========================================================================
 * Copy a stored block, storing first the length and its
 * one's complement if requested.
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
copy_block(
    basic_deflate_stream *s,
    char    *buf,    /* the input data */
    unsigned len,     /* its length */
    int      header)  /* true if block header must be written */
{
    bi_windup(s);        /* align on byte boundary */

    if (header) {
        put_short(s, (std::uint16_t)len);
        put_short(s, (std::uint16_t)~len);
    }
    while (len--) {
        put_byte(s, *buf++);
    }
}

# define _tr_tally_lit(s, c, flush) \
  { std::uint8_t cc = (c); \
    s->d_buf_[s->last_lit_] = 0; \
    s->l_buf_[s->last_lit_++] = cc; \
    s->dyn_ltree_[cc].fc++; \
    flush = (s->last_lit_ == s->lit_bufsize_-1); \
   }
# define _tr_tally_dist(s, distance, length, flush) \
  { std::uint8_t len = (length); \
    std::uint16_t dist = (distance); \
    s->d_buf_[s->last_lit_] = dist; \
    s->l_buf_[s->last_lit_++] = len; \
    dist--; \
    s->dyn_ltree_[s->lut_.length_code[len]+limits::literals+1].fc++; \
    s->dyn_dtree_[d_code(dist)].fc++; \
    flush = (s->last_lit_ == s->lit_bufsize_-1); \
  }

/* To be used only when the state is known to be valid */
#define ERR_RETURN(strm,err) \
  return (strm->msg = "unspecified zlib error", (err))

/* Matches of length 3 are discarded if their distance exceeds TOO_FAR */
#ifndef TOO_FAR
#  define TOO_FAR 4096
#endif

/* Note: the deflate() code requires max_lazy >= limits::minMatch and max_chain >= 4
 * For deflate_fast() (levels <= 3) good is ignored and lazy has a different
 * meaning.
 */

/* rank Z_BLOCK between Z_NO_FLUSH and Z_PARTIAL_FLUSH */
#define RANK(f) (((f) << 1) - ((f) > 4 ? 9 : 0))

/* ===========================================================================
 * Update a hash value with the given input byte
 * IN  assertion: all calls to to UPDATE_HASH are made with consecutive
 *    input characters, so that a running hash key can be computed from the
 *    previous key instead of complete recalculation each time.
 */
#define UPDATE_HASH(s,h,c) (h = (((h)<<s->hash_shift_) ^ (c)) & s->hash_mask_)


/* ===========================================================================
 * Insert string str in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * If this file is compiled with -DFASTEST, the compression level is forced
 * to 1, and no hash chains are maintained.
 * IN  assertion: all calls to to INSERT_STRING are made with consecutive
 *    input characters and the first limits::minMatch bytes of str are valid
 *    (except for the last limits::minMatch-1 bytes of the input file).
 */
#define INSERT_STRING(s, str, match_head) \
   (UPDATE_HASH(s, s->ins_h_, s->window_[(str) + (limits::minMatch-1)]), \
    match_head = s->prev_[(str) & s->w_mask_] = s->head_[s->ins_h_], \
    s->head_[s->ins_h_] = (std::uint16_t)(str))

/* ===========================================================================
 * Initialize the hash table (avoiding 64K overflow for 16 bit systems).
 * prev[] will be initialized on the fly.
 */
#define CLEAR_HASH(s) \
    s->head_[s->hash_size_-1] = 0; \
    std::memset((Byte *)s->head_, 0, (unsigned)(s->hash_size_-1)*sizeof(*s->head_));

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateInit(basic_deflate_stream* strm, int level)
{
    return deflateInit2(strm, level, Z_DEFLATED, 15, DEF_MEM_LEVEL,
                         Z_DEFAULT_STRATEGY);
    /* To do: ignore strm->next_in if we use it as window */
}

template<class Allocator>
void
basic_deflate_stream<Allocator>::
fill_window(basic_deflate_stream *s)
{
    unsigned n, m;
    std::uint16_t *p;
    unsigned more;    /* Amount of free space at the end of the window. */
    uInt wsize = s->w_size_;

    Assert(s->lookahead_ < MIN_LOOKAHEAD, "already enough lookahead");

    do {
        more = (unsigned)(s->window_size_ -(std::uint32_t)s->lookahead_ -(std::uint32_t)s->strstart_);

        /* Deal with !@#$% 64K limit: */
        if (sizeof(int) <= 2) {
            if (more == 0 && s->strstart_ == 0 && s->lookahead_ == 0) {
                more = wsize;

            } else if (more == (unsigned)(-1)) {
                /* Very unlikely, but possible on 16 bit machine if
                 * strstart == 0 && lookahead == 1 (input done a byte at time)
                 */
                more--;
            }
        }

        /* If the window is almost full and there is insufficient lookahead,
         * move the upper half to the lower one to make room in the upper half.
         */
        if (s->strstart_ >= wsize+MAX_DIST(s)) {

            std::memcpy(s->window_, s->window_+wsize, (unsigned)wsize);
            s->match_start_ -= wsize;
            s->strstart_    -= wsize; /* we now have strstart >= MAX_DIST */
            s->block_start_ -= (long) wsize;

            /* Slide the hash table (could be avoided with 32 bit values
               at the expense of memory usage). We slide even when level == 0
               to keep the hash table consistent if we switch back to level > 0
               later. (Using level 0 permanently is not an optimal usage of
               zlib, so we don't care about this pathological case.)
             */
            n = s->hash_size_;
            p = &s->head_[n];
            do {
                m = *--p;
                *p = (std::uint16_t)(m >= wsize ? m-wsize : 0);
            } while (--n);

            n = wsize;
            p = &s->prev_[n];
            do {
                m = *--p;
                *p = (std::uint16_t)(m >= wsize ? m-wsize : 0);
                /* If n is not on any hash chain, prev[n] is garbage but
                 * its value will never be used.
                 */
            } while (--n);
            more += wsize;
        }
        if (s->avail_in == 0) break;

        /* If there was no sliding:
         *    strstart <= WSIZE+MAX_DIST-1 && lookahead <= MIN_LOOKAHEAD - 1 &&
         *    more == window_size - lookahead - strstart
         * => more >= window_size - (MIN_LOOKAHEAD-1 + WSIZE + MAX_DIST-1)
         * => more >= window_size - 2*WSIZE + 2
         * In the BIG_MEM or MMAP case (not yet supported),
         *   window_size == input_size + MIN_LOOKAHEAD  &&
         *   strstart + s->lookahead_ <= input_size => more >= MIN_LOOKAHEAD.
         * Otherwise, window_size == 2*WSIZE so more >= 2.
         * If there was sliding, more >= WSIZE. So in all cases, more >= 2.
         */
        Assert(more >= 2, "more < 2");

        n = read_buf(s, s->window_ + s->strstart_ + s->lookahead_, more);
        s->lookahead_ += n;

        /* Initialize the hash value now that we have some input: */
        if (s->lookahead_ + s->insert_ >= limits::minMatch) {
            uInt str = s->strstart_ - s->insert_;
            s->ins_h_ = s->window_[str];
            UPDATE_HASH(s, s->ins_h_, s->window_[str + 1]);
            while (s->insert_) {
                UPDATE_HASH(s, s->ins_h_, s->window_[str + limits::minMatch-1]);
                s->prev_[str & s->w_mask_] = s->head_[s->ins_h_];
                s->head_[s->ins_h_] = (std::uint16_t)str;
                str++;
                s->insert_--;
                if (s->lookahead_ + s->insert_ < limits::minMatch)
                    break;
            }
        }
        /* If the whole input has less than limits::minMatch bytes, ins_h is garbage,
         * but this is not important since only literal bytes will be emitted.
         */

    } while (s->lookahead_ < MIN_LOOKAHEAD && s->avail_in != 0);

    /* If the WIN_INIT bytes after the end of the current data have never been
     * written, then zero those bytes in order to avoid memory check reports of
     * the use of uninitialized (or uninitialised as Julian writes) bytes by
     * the longest match routines.  Update the high water mark for the next
     * time through here.  WIN_INIT is set to limits::maxMatch since the longest match
     * routines allow scanning to strstart + limits::maxMatch, ignoring lookahead.
     */
    if (s->high_water_ < s->window_size_) {
        std::uint32_t curr = s->strstart_ + (std::uint32_t)(s->lookahead_);
        std::uint32_t init;

        if (s->high_water_ < curr) {
            /* Previous high water mark below current data -- zero WIN_INIT
             * bytes or up to end of window, whichever is less.
             */
            init = s->window_size_ - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            std::memset(s->window_ + curr, 0, (unsigned)init);
            s->high_water_ = curr + init;
        }
        else if (s->high_water_ < (std::uint32_t)curr + WIN_INIT) {
            /* High water mark at or above current data, but below current data
             * plus WIN_INIT -- zero out to current data plus WIN_INIT, or up
             * to end of window, whichever is less.
             */
            init = (std::uint32_t)curr + WIN_INIT - s->high_water_;
            if (init > s->window_size_ - s->high_water_)
                init = s->window_size_ - s->high_water_;
            std::memset(s->window_ + s->high_water_, 0, (unsigned)init);
            s->high_water_ += init;
        }
    }

    Assert((std::uint32_t)s->strstart_ <= s->window_size_ - MIN_LOOKAHEAD,
           "not enough room for search");
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateInit2(
    basic_deflate_stream* strm,
    int  level,
    int  method,
    int  windowBits,
    int  memLevel,
    int  strategy)
{
    std::uint16_t *overlay;
    /* We overlay pending_buf and d_buf+l_buf. This works since the average
     * output size for (length,distance) codes is <= 24 bits.
     */

    if (strm == 0)
        return Z_STREAM_ERROR;

    strm->msg = 0;

    if (level == Z_DEFAULT_COMPRESSION) level = 6;

    assert(windowBits >= 0);
    if (memLevel < 1 || memLevel > MAX_MEM_LEVEL || method != Z_DEFLATED ||
        windowBits < 8 || windowBits > 15 || level < 0 || level > 9 ||
        strategy < 0 || strategy > Z_FIXED) {
        return Z_STREAM_ERROR;
    }
    if (windowBits == 8) windowBits = 9;  /* until 256-byte window bug fixed */
    auto s = strm;

    s->w_bits_ = windowBits;
    s->w_size_ = 1 << s->w_bits_;
    s->w_mask_ = s->w_size_ - 1;

    s->hash_bits_ = memLevel + 7;
    s->hash_size_ = 1 << s->hash_bits_;
    s->hash_mask_ = s->hash_size_ - 1;
    s->hash_shift_ =  ((s->hash_bits_+limits::minMatch-1)/limits::minMatch);

    s->window_ = (Byte *) std::malloc(s->w_size_ * 2*sizeof(Byte));
    s->prev_   = (std::uint16_t *)  std::malloc(s->w_size_ * sizeof(std::uint16_t));
    s->head_   = (std::uint16_t *)  std::malloc(s->hash_size_ * sizeof(std::uint16_t));

    s->high_water_ = 0;      /* nothing written to s->window_ yet */

    s->lit_bufsize_ = 1 << (memLevel + 6); /* 16K elements by default */

    overlay = (std::uint16_t *) std::malloc(s->lit_bufsize_ * (sizeof(std::uint16_t)+2));
    s->pending_buf_ = (std::uint8_t *) overlay;
    s->pending_buf_size_ = (std::uint32_t)s->lit_bufsize_ * (sizeof(std::uint16_t)+2L);

    if (s->window_ == 0 || s->prev_ == 0 || s->head_ == 0 ||
        s->pending_buf_ == 0) {
        s->status_ = FINISH_STATE;
        strm->msg = "unspecified zlib error";
        deflateEnd (strm);
        return Z_MEM_ERROR;
    }
    s->d_buf_ = overlay + s->lit_bufsize_/sizeof(std::uint16_t);
    s->l_buf_ = s->pending_buf_ + (1+sizeof(std::uint16_t))*s->lit_bufsize_;

    s->level_ = level;
    s->strategy_ = strategy;

    return deflateReset(strm);
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateSetDictionary (
    const Byte *dictionary,
    uInt  dictLength)
{
auto strm = this;
    uInt str, n;
    unsigned avail;
    const unsigned char *next;

    auto s = strm;

    if (s->lookahead_)
        return Z_STREAM_ERROR;

    /* if dictionary would fill window, just replace the history */
    if (dictLength >= s->w_size_) {
        CLEAR_HASH(s);
        s->strstart_ = 0;
        s->block_start_ = 0L;
        s->insert_ = 0;
        dictionary += dictLength - s->w_size_;  /* use the tail */
        dictLength = s->w_size_;
    }

    /* insert dictionary into window and hash */
    avail = strm->avail_in;
    next = strm->next_in;
    strm->avail_in = dictLength;
    strm->next_in = (const Byte *)dictionary;
    fill_window(s);
    while (s->lookahead_ >= limits::minMatch) {
        str = s->strstart_;
        n = s->lookahead_ - (limits::minMatch-1);
        do {
            UPDATE_HASH(s, s->ins_h_, s->window_[str + limits::minMatch-1]);
            s->prev_[str & s->w_mask_] = s->head_[s->ins_h_];
            s->head_[s->ins_h_] = (std::uint16_t)str;
            str++;
        } while (--n);
        s->strstart_ = str;
        s->lookahead_ = limits::minMatch-1;
        fill_window(s);
    }
    s->strstart_ += s->lookahead_;
    s->block_start_ = (long)s->strstart_;
    s->insert_ = s->lookahead_;
    s->lookahead_ = 0;
    s->match_length_ = s->prev_length_ = limits::minMatch-1;
    s->match_available_ = 0;
    strm->next_in = next;
    strm->avail_in = avail;
    return Z_OK;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateResetKeep(basic_deflate_stream* strm)
{
    strm->total_in = strm->total_out = 0;
    strm->msg = 0;
    strm->data_type = Z_UNKNOWN;

    auto s = strm;
    s->pending_ = 0;
    s->pending_out_ = s->pending_buf_;

    s->status_ = BUSY_STATE;
    s->last_flush_ = Z_NO_FLUSH;

    _tr_init(s);

    return Z_OK;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateReset(basic_deflate_stream* strm)
{
    int ret;

    ret = deflateResetKeep(strm);
    if (ret == Z_OK)
        lm_init(strm);
    return ret;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflatePending (
    basic_deflate_stream* strm,
    unsigned *pending,
    int *bits)
{
    if (pending != 0)
        *pending = strm->pending_;
    if (bits != 0)
        *bits = strm->bi_valid_;
    return Z_OK;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflatePrime(basic_deflate_stream* strm, int bits, int value)
{
    int put;

    auto s = strm;
    if ((Byte *)(s->d_buf_) < s->pending_out_ + ((Buf_size + 7) >> 3))
        return Z_BUF_ERROR;
    do {
        put = Buf_size - s->bi_valid_;
        if (put > bits)
            put = bits;
        s->bi_buf_ |= (std::uint16_t)((value & ((1 << put) - 1)) << s->bi_valid_);
        s->bi_valid_ += put;
        _tr_flush_bits(s);
        value >>= put;
        bits -= put;
    } while (bits);
    return Z_OK;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateParams(basic_deflate_stream* strm, int level, int strategy)
{
    compress_func func;
    int err = Z_OK;

    if (strm == 0 || strm == 0) return Z_STREAM_ERROR;
    auto s = strm;

    if (level == Z_DEFAULT_COMPRESSION) level = 6;
    if (level < 0 || level > 9 || strategy < 0 || strategy > Z_FIXED) {
        return Z_STREAM_ERROR;
    }
    func = get_config(s->level_).func;

    if ((strategy != s->strategy_ || func != get_config(level).func) &&
        strm->total_in != 0) {
        /* Flush the last buffer: */
        err = strm->deflate(Z_BLOCK);
        if (err == Z_BUF_ERROR && s->pending_ == 0)
            err = Z_OK;
    }
    if (s->level_ != level) {
        s->level_ = level;
        s->max_lazy_match_   = get_config(level).max_lazy;
        s->good_match_       = get_config(level).good_length;
        s->nice_match_       = get_config(level).nice_length;
        s->max_chain_length_ = get_config(level).max_chain;
    }
    s->strategy_ = strategy;
    return err;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateTune(
    basic_deflate_stream* strm,
    int good_length,
    int max_lazy,
    int nice_length,
    int max_chain)
{
    auto s = strm;
    s->good_match_ = good_length;
    s->max_lazy_match_ = max_lazy;
    s->nice_match_ = nice_length;
    s->max_chain_length_ = max_chain;
    return Z_OK;
}

/* =========================================================================
 * For the default windowBits of 15 and memLevel of 8, this function returns
 * a close to exact, as well as small, upper bound on the compressed size.
 * They are coded as constants here for a reason--if the #define's are
 * changed, then this function needs to be changed as well.  The return
 * value for 15 and 8 only works for those exact settings.
 *
 * For any setting other than those defaults for windowBits and memLevel,
 * the value returned is a conservative worst case for the maximum expansion
 * resulting from using fixed blocks instead of stored blocks, which deflate
 * can emit on compressed data for some combinations of the parameters.
 *
 * This function could be more sophisticated to provide closer upper bounds for
 * every combination of windowBits and memLevel.  But even the conservative
 * upper bound of about 14% expansion does not seem onerous for output buffer
 * allocation.
 */
template<class Allocator>
uLong
basic_deflate_stream<Allocator>::
deflateBound(
    basic_deflate_stream* strm,
    uLong sourceLen)
{
    uLong complen, wraplen;

    /* conservative upper bound for compressed data */
    complen = sourceLen +
              ((sourceLen + 7) >> 3) + ((sourceLen + 63) >> 6) + 5;

    /* if can't get parameters, return conservative bound plus zlib wrapper */
    if (strm == 0 || strm == 0)
        return complen + 6;

    /* compute wrapper length */
    auto s = strm;
    wraplen = 0;

    /* if not default parameters, return conservative bound */
    if (s->w_bits_ != 15 || s->hash_bits_ != 8 + 7)
        return complen + wraplen;

    /* default settings: return tight bound for that case */
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) +
           (sourceLen >> 25) + 13 - 6 + wraplen;
}

/* =========================================================================
 * Flush as much pending output as possible. All deflate() output goes
 * through this function so some applications may wish to modify it
 * to avoid allocating a large strm->next_out buffer and copying into it.
 * (See also read_buf()).
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
flush_pending(basic_deflate_stream* strm)
{
    unsigned len;
    auto s = strm;

    _tr_flush_bits(s);
    len = s->pending_;
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;

    std::memcpy(strm->next_out, s->pending_out_, len);
    strm->next_out  += len;
    s->pending_out_  += len;
    strm->total_out += len;
    strm->avail_out  -= len;
    s->pending_ -= len;
    if (s->pending_ == 0) {
        s->pending_out_ = s->pending_buf_;
    }
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflate(int flush)
{
auto strm = this;
    int old_flush; /* value of flush param for previous deflate call */

    if (strm == 0 || strm == 0 ||
        flush > Z_BLOCK || flush < 0) {
        return Z_STREAM_ERROR;
    }
    auto s = strm;

    if (strm->next_out == 0 ||
        (strm->next_in == 0 && strm->avail_in != 0) ||
        (s->status_ == FINISH_STATE && flush != Z_FINISH)) {
        ERR_RETURN(strm, Z_STREAM_ERROR);
    }
    if (strm->avail_out == 0) ERR_RETURN(strm, Z_BUF_ERROR);

    old_flush = s->last_flush_;
    s->last_flush_ = flush;

    /* Flush as much pending output as possible */
    if (s->pending_ != 0) {
        flush_pending(strm);
        if (strm->avail_out == 0) {
            /* Since avail_out is 0, deflate will be called again with
             * more output space, but possibly with both pending and
             * avail_in equal to zero. There won't be anything to do,
             * but this is not an error situation so make sure we
             * return OK instead of BUF_ERROR at next call of deflate:
             */
            s->last_flush_ = -1;
            return Z_OK;
        }

    /* Make sure there is something to do and avoid duplicate consecutive
     * flushes. For repeated and useless calls with Z_FINISH, we keep
     * returning Z_STREAM_END instead of Z_BUF_ERROR.
     */
    } else if (strm->avail_in == 0 && RANK(flush) <= RANK(old_flush) &&
               flush != Z_FINISH) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* User must not provide more input after the first FINISH: */
    if (s->status_ == FINISH_STATE && strm->avail_in != 0) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }

    /* Start a new block or continue the current one.
     */
    if (strm->avail_in != 0 || s->lookahead_ != 0 ||
        (flush != Z_NO_FLUSH && s->status_ != FINISH_STATE)) {
        block_state bstate;

        bstate = s->strategy_ == Z_HUFFMAN_ONLY ? deflate_huff(s, flush) :
                    (s->strategy_ == Z_RLE ? deflate_rle(s, flush) :
                        (*(get_config(s->level_).func))(s, flush));

        if (bstate == finish_started || bstate == finish_done) {
            s->status_ = FINISH_STATE;
        }
        if (bstate == need_more || bstate == finish_started) {
            if (strm->avail_out == 0) {
                s->last_flush_ = -1; /* avoid BUF_ERROR next call, see above */
            }
            return Z_OK;
            /* If flush != Z_NO_FLUSH && avail_out == 0, the next call
             * of deflate should use the same flush parameter to make sure
             * that the flush is complete. So we don't have to output an
             * empty block here, this will be done at next call. This also
             * ensures that for a very small output buffer, we emit at most
             * one empty block.
             */
        }
        if (bstate == block_done) {
            if (flush == Z_PARTIAL_FLUSH) {
                _tr_align(s);
            } else if (flush != Z_BLOCK) { /* FULL_FLUSH or SYNC_FLUSH */
                _tr_stored_block(s, (char*)0, 0L, 0);
                /* For a full flush, this empty block will be recognized
                 * as a special marker by inflate_sync().
                 */
                if (flush == Z_FULL_FLUSH) {
                    CLEAR_HASH(s);             /* forget history */
                    if (s->lookahead_ == 0) {
                        s->strstart_ = 0;
                        s->block_start_ = 0L;
                        s->insert_ = 0;
                    }
                }
            }
            flush_pending(strm);
            if (strm->avail_out == 0) {
              s->last_flush_ = -1; /* avoid BUF_ERROR at next call, see above */
              return Z_OK;
            }
        }
    }
    Assert(strm->avail_out > 0, "bug2");

    if (flush != Z_FINISH) return Z_OK;
    return Z_STREAM_END;
}

/* ========================================================================= */

template<class Allocator>
int
basic_deflate_stream<Allocator>::
deflateEnd(basic_deflate_stream* strm)
{
    int status;

    if (strm == 0 || strm == 0) return Z_STREAM_ERROR;

    status = strm->status_;
    if (status != EXTRA_STATE &&
        status != NAME_STATE &&
        status != COMMENT_STATE &&
        status != HCRC_STATE &&
        status != BUSY_STATE &&
        status != FINISH_STATE) {
      return Z_STREAM_ERROR;
    }

    /* Deallocate in reverse order of allocations: */
    std::free(strm->pending_buf_);
    std::free(strm->head_);
    std::free(strm->prev_);
    std::free(strm->window_);
    strm = 0;

    return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK;
}

/* ===========================================================================
 * Read a new buffer from the current input stream, update the adler32
 * and total number of bytes read.  All deflate() input goes through
 * this function so some applications may wish to modify it to avoid
 * allocating a large strm->next_in buffer and copying from it.
 * (See also flush_pending()).
 */
template<class Allocator>
int
basic_deflate_stream<Allocator>::
read_buf(basic_deflate_stream* strm, Byte *buf, unsigned size)
{
    unsigned len = strm->avail_in;

    if (len > size) len = size;
    if (len == 0) return 0;

    strm->avail_in  -= len;

    std::memcpy(buf, strm->next_in, len);
    strm->next_in  += len;
    strm->total_in += len;

    return (int)len;
}

/* ===========================================================================
 * Initialize the "longest match" routines for a new zlib stream
 */
template<class Allocator>
void
basic_deflate_stream<Allocator>::
lm_init(basic_deflate_stream *s)
{
    s->window_size_ = (std::uint32_t)2L*s->w_size_;

    CLEAR_HASH(s);

    /* Set the default configuration parameters:
     */
    // VFALCO TODO just copy the config struct
    s->max_lazy_match_   = get_config(s->level_).max_lazy;
    s->good_match_       = get_config(s->level_).good_length;
    s->nice_match_       = get_config(s->level_).nice_length;
    s->max_chain_length_ = get_config(s->level_).max_chain;

    s->strstart_ = 0;
    s->block_start_ = 0L;
    s->lookahead_ = 0;
    s->insert_ = 0;
    s->match_length_ = s->prev_length_ = limits::minMatch-1;
    s->match_available_ = 0;
    s->ins_h_ = 0;
}

/* ===========================================================================
 * Set match_start to the longest match starting at the given string and
 * return its length. Matches shorter or equal to prev_length are discarded,
 * in which case the result is equal to prev_length and match_start is
 * garbage.
 * IN assertions: cur_match is the head of the hash chain for the current
 *   string (strstart) and its distance is <= MAX_DIST, and prev_length >= 1
 * OUT assertion: the match length is not greater than s->lookahead_.
 */
/* For 80x86 and 680x0, an optimized version will be provided in match.asm or
 * match.S. The code will be functionally equivalent.
 */
template<class Allocator>
uInt
basic_deflate_stream<Allocator>::
longest_match(basic_deflate_stream *s, IPos cur_match)
{
    unsigned chain_length = s->max_chain_length_;/* max hash chain length */
    Byte *scan = s->window_ + s->strstart_; /* current string */
    Byte *match;                       /* matched string */
    int len;                           /* length of current match */
    int best_len = s->prev_length_;              /* best match length so far */
    int nice_match = s->nice_match_;             /* stop if match long enough */
    IPos limit = s->strstart_ > (IPos)MAX_DIST(s) ?
        s->strstart_ - (IPos)MAX_DIST(s) : 0;
    /* Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0.
     */
    std::uint16_t *prev = s->prev_;
    uInt wmask = s->w_mask_;

    Byte *strend = s->window_ + s->strstart_ + limits::maxMatch;
    Byte scan_end1  = scan[best_len-1];
    Byte scan_end   = scan[best_len];

    /* The code is optimized for HASH_BITS >= 8 and limits::maxMatch-2 multiple of 16.
     * It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits_ >= 8 && limits::maxMatch == 258, "fc too clever");

    /* Do not waste too much time if we already have a good match: */
    if (s->prev_length_ >= s->good_match_) {
        chain_length >>= 2;
    }
    /* Do not look for matches beyond the end of the input. This is necessary
     * to make deflate deterministic.
     */
    if ((uInt)nice_match > s->lookahead_) nice_match = s->lookahead_;

    Assert((std::uint32_t)s->strstart_ <= s->window_size_-MIN_LOOKAHEAD, "need lookahead");

    do {
        Assert(cur_match < s->strstart_, "no future");
        match = s->window_ + cur_match;

        /* Skip to next match if the match length cannot increase
         * or if the match length is less than 2.  Note that the checks below
         * for insufficient lookahead only occur occasionally for performance
         * reasons.  Therefore uninitialized memory will be accessed, and
         * conditional jumps will be made that depend on those values.
         * However the length of the match is limited to the lookahead, so
         * the output of deflate is not affected by the uninitialized values.
         */
        if (match[best_len]   != scan_end  ||
            match[best_len-1] != scan_end1 ||
            *match            != *scan     ||
            *++match          != scan[1])      continue;

        /* The check at best_len-1 can be removed because it will be made
         * again later. (This heuristic is not always a win.)
         * It is not necessary to compare scan[2] and match[2] since they
         * are always equal when the other bytes match, given that
         * the hash keys are equal and that HASH_BITS >= 8.
         */
        scan += 2, match++;
        Assert(*scan == *match, "match[2]?");

        /* We check for insufficient lookahead only every 8th comparison;
         * the 256th check will be made at strstart+258.
         */
        do {
        } while (*++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 scan < strend);

        Assert(scan <= s->window_+(unsigned)(s->window_size_-1), "wild scan");

        len = limits::maxMatch - (int)(strend - scan);
        scan = strend - limits::maxMatch;

        if (len > best_len) {
            s->match_start_ = cur_match;
            best_len = len;
            if (len >= nice_match) break;
            scan_end1  = scan[best_len-1];
            scan_end   = scan[best_len];
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit
             && --chain_length != 0);

    if ((uInt)best_len <= s->lookahead_) return (uInt)best_len;
    return s->lookahead_;
}

#  define check_match(s, start, match, length)

/* ===========================================================================
 * Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match.
 */
#define FLUSH_BLOCK_ONLY(s, last) { \
   _tr_flush_block(s, (s->block_start_ >= 0L ? \
                   (char *)&s->window_[(unsigned)s->block_start_] : \
                   (char *)0), \
                (std::uint32_t)((long)s->strstart_ - s->block_start_), \
                (last)); \
   s->block_start_ = s->strstart_; \
   flush_pending(s); \
   Tracev((stderr,"[FLUSH]")); \
}

/* Same but force premature exit if necessary. */
#define FLUSH_BLOCK(s, last) { \
   FLUSH_BLOCK_ONLY(s, last); \
   if (s->avail_out == 0) return (last) ? finish_started : need_more; \
}

/* ===========================================================================
 * Copy without compression as much as possible from the input stream, return
 * the current block state.
 * This function does not insert new strings in the dictionary since
 * uncompressible data is probably not useful. This function is used
 * only for the level=0 compression option.
 * NOTE: this function should be optimized to avoid extra copying from
 * window to pending_buf.
 */
template<class Allocator>
block_state
basic_deflate_stream<Allocator>::
deflate_stored(
    basic_deflate_stream *s,
    int flush)
{
    /* Stored blocks are limited to 0xffff bytes, pending_buf is limited
     * to pending_buf_size, and each stored block has a 5 byte header:
     */
    std::uint32_t max_block_size = 0xffff;
    std::uint32_t max_start;

    if (max_block_size > s->pending_buf_size_ - 5) {
        max_block_size = s->pending_buf_size_ - 5;
    }

    /* Copy as much as possible from input to output: */
    for (;;) {
        /* Fill the window as much as possible: */
        if (s->lookahead_ <= 1) {

            Assert(s->strstart_ < s->w_size_+MAX_DIST(s) ||
                   s->block_start_ >= (long)s->w_size_, "slide too late");

            fill_window(s);
            if (s->lookahead_ == 0 && flush == Z_NO_FLUSH) return need_more;

            if (s->lookahead_ == 0) break; /* flush the current block */
        }
        Assert(s->block_start_ >= 0L, "block gone");

        s->strstart_ += s->lookahead_;
        s->lookahead_ = 0;

        /* Emit a stored block if pending_buf will be full: */
        max_start = s->block_start_ + max_block_size;
        if (s->strstart_ == 0 || (std::uint32_t)s->strstart_ >= max_start) {
            /* strstart == 0 is possible when wraparound on 16-bit machine */
            s->lookahead_ = (uInt)(s->strstart_ - max_start);
            s->strstart_ = (uInt)max_start;
            FLUSH_BLOCK(s, 0);
        }
        /* Flush if we may have to slide, otherwise block_start may become
         * negative and the data will be gone:
         */
        if (s->strstart_ - (uInt)s->block_start_ >= MAX_DIST(s)) {
            FLUSH_BLOCK(s, 0);
        }
    }
    s->insert_ = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if ((long)s->strstart_ > s->block_start_)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * Compress as much as possible from the input stream, return the current
 * block state.
 * This function does not perform lazy evaluation of matches and inserts
 * new strings in the dictionary only for unmatched strings or for short
 * matches. It is used only for the fast compression options.
 */
template<class Allocator>
block_state
basic_deflate_stream<Allocator>::
deflate_fast(basic_deflate_stream *s, int flush)
{
    IPos hash_head;       /* head of the hash chain */
    int bflush;           /* set if current block must be flushed */

    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need limits::maxMatch bytes
         * for the next match, plus limits::minMatch bytes to insert the
         * string following the next match.
         */
        if (s->lookahead_ < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead_ < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead_ == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = 0;
        if (s->lookahead_ >= limits::minMatch) {
            INSERT_STRING(s, s->strstart_, hash_head);
        }

        /* Find the longest match, discarding those <= prev_length.
         * At this point we have always match_length < limits::minMatch
         */
        if (hash_head != 0 && s->strstart_ - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length_ = longest_match (s, hash_head);
            /* longest_match() sets match_start */
        }
        if (s->match_length_ >= limits::minMatch) {
            check_match(s, s->strstart_, s->match_start_, s->match_length_);

            _tr_tally_dist(s, s->strstart_ - s->match_start_,
                           s->match_length_ - limits::minMatch, bflush);

            s->lookahead_ -= s->match_length_;

            /* Insert new strings in the hash table only if the match length
             * is not too large. This saves time but degrades compression.
             */
            if (s->match_length_ <= s->max_lazy_match_ &&
                s->lookahead_ >= limits::minMatch) {
                s->match_length_--; /* string at strstart already in table */
                do {
                    s->strstart_++;
                    INSERT_STRING(s, s->strstart_, hash_head);
                    /* strstart never exceeds WSIZE-limits::maxMatch, so there are
                     * always limits::minMatch bytes ahead.
                     */
                } while (--s->match_length_ != 0);
                s->strstart_++;
            } else
            {
                s->strstart_ += s->match_length_;
                s->match_length_ = 0;
                s->ins_h_ = s->window_[s->strstart_];
                UPDATE_HASH(s, s->ins_h_, s->window_[s->strstart_+1]);
                /* If lookahead < limits::minMatch, ins_h is garbage, but it does not
                 * matter since it will be recomputed at next deflate call.
                 */
            }
        } else {
            /* No match, output a literal byte */
            Tracevv((stderr,"%c", s->window_[s->strstart_]));
            _tr_tally_lit (s, s->window_[s->strstart_], bflush);
            s->lookahead_--;
            s->strstart_++;
        }
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert_ = s->strstart_ < limits::minMatch-1 ? s->strstart_ : limits::minMatch-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit_)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * Same as above, but achieves better compression. We use a lazy
 * evaluation for matches: a match is finally adopted only if there is
 * no better match at the next window position.
 */
template<class Allocator>
block_state
basic_deflate_stream<Allocator>::
deflate_slow(basic_deflate_stream *s, int flush)
{
    IPos hash_head;          /* head of hash chain */
    int bflush;              /* set if current block must be flushed */

    /* Process the input block. */
    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need limits::maxMatch bytes
         * for the next match, plus limits::minMatch bytes to insert the
         * string following the next match.
         */
        if (s->lookahead_ < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead_ < MIN_LOOKAHEAD && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead_ == 0) break; /* flush the current block */
        }

        /* Insert the string window[strstart .. strstart+2] in the
         * dictionary, and set hash_head to the head of the hash chain:
         */
        hash_head = 0;
        if (s->lookahead_ >= limits::minMatch) {
            INSERT_STRING(s, s->strstart_, hash_head);
        }

        /* Find the longest match, discarding those <= prev_length.
         */
        s->prev_length_ = s->match_length_, s->prev_match_ = s->match_start_;
        s->match_length_ = limits::minMatch-1;

        if (hash_head != 0 && s->prev_length_ < s->max_lazy_match_ &&
            s->strstart_ - hash_head <= MAX_DIST(s)) {
            /* To simplify the code, we prevent matches with the string
             * of window index 0 (in particular we have to avoid a match
             * of the string with itself at the start of the input file).
             */
            s->match_length_ = longest_match (s, hash_head);
            /* longest_match() sets match_start */

            if (s->match_length_ <= 5 && (s->strategy_ == Z_FILTERED
#if TOO_FAR <= 32767
                || (s->match_length_ == limits::minMatch &&
                    s->strstart_ - s->match_start_ > TOO_FAR)
#endif
                )) {

                /* If prev_match is also limits::minMatch, match_start is garbage
                 * but we will ignore the current match anyway.
                 */
                s->match_length_ = limits::minMatch-1;
            }
        }
        /* If there was a match at the previous step and the current
         * match is not better, output the previous match:
         */
        if (s->prev_length_ >= limits::minMatch && s->match_length_ <= s->prev_length_) {
            uInt max_insert = s->strstart_ + s->lookahead_ - limits::minMatch;
            /* Do not insert strings in hash table beyond this. */

            check_match(s, s->strstart_-1, s->prev_match_, s->prev_length_);

            _tr_tally_dist(s, s->strstart_ -1 - s->prev_match_,
                           s->prev_length_ - limits::minMatch, bflush);

            /* Insert in hash table all strings up to the end of the match.
             * strstart-1 and strstart are already inserted. If there is not
             * enough lookahead, the last two strings are not inserted in
             * the hash table.
             */
            s->lookahead_ -= s->prev_length_-1;
            s->prev_length_ -= 2;
            do {
                if (++s->strstart_ <= max_insert) {
                    INSERT_STRING(s, s->strstart_, hash_head);
                }
            } while (--s->prev_length_ != 0);
            s->match_available_ = 0;
            s->match_length_ = limits::minMatch-1;
            s->strstart_++;

            if (bflush) FLUSH_BLOCK(s, 0);

        } else if (s->match_available_) {
            /* If there was no match at the previous position, output a
             * single literal. If there was a match but the current match
             * is longer, truncate the previous match to a single literal.
             */
            Tracevv((stderr,"%c", s->window_[s->strstart_-1]));
            _tr_tally_lit(s, s->window_[s->strstart_-1], bflush);
            if (bflush) {
                FLUSH_BLOCK_ONLY(s, 0);
            }
            s->strstart_++;
            s->lookahead_--;
            if (s->avail_out == 0) return need_more;
        } else {
            /* There is no previous match to compare with, wait for
             * the next step to decide.
             */
            s->match_available_ = 1;
            s->strstart_++;
            s->lookahead_--;
        }
    }
    Assert (flush != Z_NO_FLUSH, "no flush?");
    if (s->match_available_) {
        Tracevv((stderr,"%c", s->window_[s->strstart_-1]));
        _tr_tally_lit(s, s->window_[s->strstart_-1], bflush);
        s->match_available_ = 0;
    }
    s->insert_ = s->strstart_ < limits::minMatch-1 ? s->strstart_ : limits::minMatch-1;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit_)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * For Z_RLE, simply look for runs of bytes, generate matches only of distance
 * one.  Do not maintain a hash table.  (It will be regenerated if this run of
 * deflate switches away from Z_RLE.)
 */
template<class Allocator>
block_state
basic_deflate_stream<Allocator>::
deflate_rle(basic_deflate_stream *s, int flush)
{
    int bflush;             /* set if current block must be flushed */
    uInt prev;              /* byte at distance one to match */
    Byte *scan, *strend;   /* scan goes up to strend for length of run */

    for (;;) {
        /* Make sure that we always have enough lookahead, except
         * at the end of the input file. We need limits::maxMatch bytes
         * for the longest run, plus one for the unrolled loop.
         */
        if (s->lookahead_ <= limits::maxMatch) {
            fill_window(s);
            if (s->lookahead_ <= limits::maxMatch && flush == Z_NO_FLUSH) {
                return need_more;
            }
            if (s->lookahead_ == 0) break; /* flush the current block */
        }

        /* See how many times the previous byte repeats */
        s->match_length_ = 0;
        if (s->lookahead_ >= limits::minMatch && s->strstart_ > 0) {
            scan = s->window_ + s->strstart_ - 1;
            prev = *scan;
            if (prev == *++scan && prev == *++scan && prev == *++scan) {
                strend = s->window_ + s->strstart_ + limits::maxMatch;
                do {
                } while (prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan &&
                         scan < strend);
                s->match_length_ = limits::maxMatch - (int)(strend - scan);
                if (s->match_length_ > s->lookahead_)
                    s->match_length_ = s->lookahead_;
            }
            Assert(scan <= s->window_+(uInt)(s->window_size_-1), "wild scan");
        }

        /* Emit match if have run of limits::minMatch or longer, else emit literal */
        if (s->match_length_ >= limits::minMatch) {
            check_match(s, s->strstart_, s->strstart_ - 1, s->match_length_);

            _tr_tally_dist(s, 1, s->match_length_ - limits::minMatch, bflush);

            s->lookahead_ -= s->match_length_;
            s->strstart_ += s->match_length_;
            s->match_length_ = 0;
        } else {
            /* No match, output a literal byte */
            Tracevv((stderr,"%c", s->window_[s->strstart_]));
            _tr_tally_lit (s, s->window_[s->strstart_], bflush);
            s->lookahead_--;
            s->strstart_++;
        }
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert_ = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit_)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

/* ===========================================================================
 * For Z_HUFFMAN_ONLY, do not look for matches.  Do not maintain a hash table.
 * (It will be regenerated if this run of deflate switches away from Huffman.)
 */
template<class Allocator>
block_state
basic_deflate_stream<Allocator>::
deflate_huff(basic_deflate_stream *s, int flush)
{
    int bflush;             /* set if current block must be flushed */

    for (;;) {
        /* Make sure that we have a literal to write. */
        if (s->lookahead_ == 0) {
            fill_window(s);
            if (s->lookahead_ == 0) {
                if (flush == Z_NO_FLUSH)
                    return need_more;
                break;      /* flush the current block */
            }
        }

        /* Output a literal byte */
        s->match_length_ = 0;
        Tracevv((stderr,"%c", s->window_[s->strstart_]));
        _tr_tally_lit (s, s->window_[s->strstart_], bflush);
        s->lookahead_--;
        s->strstart_++;
        if (bflush) FLUSH_BLOCK(s, 0);
    }
    s->insert_ = 0;
    if (flush == Z_FINISH) {
        FLUSH_BLOCK(s, 1);
        return finish_done;
    }
    if (s->last_lit_)
        FLUSH_BLOCK(s, 0);
    return block_done;
}

} // zlib
} // beast

#endif
