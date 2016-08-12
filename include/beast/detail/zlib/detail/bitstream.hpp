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

#ifndef BEAST_ZLIB_DETAIL_BITSTREAM_HPP
#define BEAST_ZLIB_DETAIL_BITSTREAM_HPP

#include <cassert>

namespace beast {
namespace zlib {
namespace detail {

class bitstream
{
    using value_type = std::uint32_t;

    value_type v_ = 0;
    std::uint8_t n_ = 0;

public:
    // discard n bits
    void
    drop(std::uint8_t n)
    {
        assert(n_ >= n);
        n_ -= n;
        v_ >>= n;
    }

    // flush everything
    void
    flush()
    {
        n_ = 0;
        v_ = 0;
    }

    // flush to the next byte boundary
    void
    flush_byte()
    {
        drop(n_ % 8);
    }

    // ensure at least n bits
    template<class FwdIt>
    bool
    fill(std::uint8_t n, FwdIt& first, FwdIt const& last);

    // return n bits
    template<class Unsigned, class FwdIt>
    bool
    peek(Unsigned& value, std::uint8_t n, FwdIt& first, FwdIt const& last);

    // return n bits, and consume
    template<class Unsigned, class FwdIt>
    bool
    read(Unsigned& value, std::uint8_t n, FwdIt& first, FwdIt const& last);
};

template<class FwdIt>
bool
bitstream::
fill(std::uint8_t n, FwdIt& first, FwdIt const& last)
{
    while(n_ < n)
    {
        if(first == last)
            return false;
        v_ += static_cast<value_type>(*first++) << n_;
        n_ += 8;
    }
    return true;
}

template<class Unsigned, class FwdIt>
bool
bitstream::
peek(Unsigned& value, std::uint8_t n, FwdIt& first, FwdIt const& last)
{
    assert(n <= sizeof(value)*8);
    if(! fill(n, first, last))
        return false;
    value = v_ & ((1ULL << n) - 1);
    return true;
}

template<class Unsigned, class FwdIt>
bool
bitstream::
read(Unsigned& value, std::uint8_t n, FwdIt& first, FwdIt const& last)
{
    assert(n < sizeof(v_)*8);
    if(! peek(value, n, first, last))
        return false;
    v_ >>= n;
    n_ -= n;
    return true;
}

} // detail
} // zlib
} // beast

#endif
