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

#include <cstdint>

namespace beast {
namespace detail {

/* Data structure describing a single value and its code string. */
struct ct_data
{
    union fc_t
    {
        std::uint16_t  freq;       /* frequency count */
        std::uint16_t  code;       /* bit string */
    };
    
    fc_t fc; // frequency count or bit string

    union dl_t
    {
        std::uint16_t  dad;        /* father node in Huffman tree */
        std::uint16_t  len;        /* length of bit string */
    };
    
    dl_t dl; // parent node in tree or length of bit string
};

} // detail
} // beast

#endif
