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

#ifndef BEAST_CORE_DETAIL_ZLIB_DETAIL_DEFLATE_HPP
#define BEAST_CORE_DETAIL_ZLIB_DETAIL_DEFLATE_HPP

#include <cassert>
#include <cstdint>

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

std::uint16_t constexpr DIST_CODE_LEN = 512;

std::uint16_t constexpr MIN_MATCH = 3;

// Can't change MIN_MATCH without also changing code, see original zlib
static_assert(MIN_MATCH==3, "");

std::uint16_t constexpr MAX_MATCH = 258;



namespace detail {

/* Data structure describing a single value and its code string. */
struct ct_data
{
    std::uint16_t fc; // frequency count or bit string    
    std::uint16_t dl; // parent node in tree or length of bit string
};

struct deflate_tables
{
    // Number of extra bits for each length code
    std::uint8_t extra_lbits[LENGTH_CODES] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };

    // Number of extra bits for each distance code
    std::uint8_t extra_dbits[D_CODES] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    // Number of extra bits for each bit length code
    std::uint8_t extra_blbits[BL_CODES] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,7
    };

    // The lengths of the bit length codes are sent in order
    // of decreasing probability, to avoid transmitting the
    // lengths for unused bit length codes.
    std::uint8_t bl_order[BL_CODES] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    ct_data ltree[L_CODES + 2];
    ct_data dtree[D_CODES];
    std::uint8_t dist_code[DIST_CODE_LEN];
    std::uint8_t length_code[MAX_MATCH-MIN_MATCH+1];
    std::uint8_t base_length[LENGTH_CODES];
    std::uint16_t base_dist[D_CODES];
};

template<class = void>
deflate_tables const&
get_deflate_tables()
{
    struct init
    {
        deflate_tables tables;

        init()
        {
            // number of codes at each bit length for an optimal tree
            //std::uint16_t bl_count[MAX_BITS+1];

            // Initialize the mapping length (0..255) -> length code (0..28)
            std::uint16_t length = 0;
            for(std::uint8_t code = 0; code < LENGTH_CODES-1; ++code)
            {
                tables.base_length[code] = length;
                for(std::size_t n = 0; n < (1U<<tables.extra_lbits[code]); n++)
                    tables.length_code[length++] = code;
            }
            assert(length == 256);
            // Note that the length 255 (match length 258) can be represented
            // in two different ways: code 284 + 5 bits or code 285, so we
            // overwrite length_code[255] to use the best encoding:
            tables.length_code[length-1] = LENGTH_CODES-1;


#if 0
            /* Initialize the mapping dist (0..32K) -> dist code (0..29) */
            dist = 0;
            for (code = 0 ; code < 16; code++) {
                base_dist[code] = dist;
                for (n = 0; n < (1<<extra_dbits[code]); n++) {
                    _dist_code[dist++] = (std::uint8_t)code;
                }
            }
            Assert (dist == 256, "tr_static_init: dist != 256");
            dist >>= 7; /* from now on, all distances are divided by 128 */
            for ( ; code < D_CODES; code++) {
                base_dist[code] = dist << 7;
                for (n = 0; n < (1<<(extra_dbits[code]-7)); n++) {
                    _dist_code[256 + dist++] = (std::uint8_t)code;
                }
            }
            Assert (dist == 256, "tr_static_init: 256+dist != 512");

            /* Construct the codes of the static literal tree */
            for (bits = 0; bits <= MAX_BITS; bits++) bl_count[bits] = 0;
            n = 0;
            while (n <= 143) static_ltree[n++].dl = 8, bl_count[8]++;
            while (n <= 255) static_ltree[n++].dl = 9, bl_count[9]++;
            while (n <= 279) static_ltree[n++].dl = 7, bl_count[7]++;
            while (n <= 287) static_ltree[n++].dl = 8, bl_count[8]++;
            /* Codes 286 and 287 do not exist, but we must include them in the
             * tree construction to get a canonical Huffman tree (longest code
             * all ones)
             */
            gen_codes((detail::ct_data *)static_ltree, L_CODES+1, bl_count);

            /* The static distance tree is trivial: */
            for (n = 0; n < D_CODES; n++) {
                static_dtree[n].dl = 5;
                static_dtree[n].fc = bi_reverse((unsigned)n, 5);
            }
            static_init_done = 1;
#endif
        }
    };
    static init const data;
    return data.tables;
}

} // detail
} // beast

#endif
