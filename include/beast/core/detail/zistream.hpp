//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_ZISTREAM_HPP
#define BEAST_DETAIL_ZISTREAM_HPP

#include <beast/core/error.hpp>
#include <beast/core/detail/zerror.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

// This is a modified work, with code and ideas taken from ZLib:
//
/*  Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler
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

namespace beast {
namespace detail {

template<class = void>
class zistream_t
{
    class window
    {
        std::uint16_t i_ = 0;
        std::uint16_t size_ = 0;
        std::uint16_t capacity_ = 32768;
        std::unique_ptr<std::uint8_t[]> p_;

    public:
        std::uint16_t
        capacity() const
        {
            return capacity_;
        }

        std::uint16_t
        size() const
        {
            return size_;
        }

        void
        reset(std::uint16_t capacity);

        void
        read(std::uint8_t* out,
            std::uint16_t pos, std::uint16_t n);

        void
        write(std::uint8_t const* in, std::uint16_t n);
    };

    class bitstream
    {
        using value_type = std::uint32_t;

        value_type v_ = 0;
        std::uint8_t n_ = 0;

    public:
        // discard n bits
        void
        drop(std::uint8_t n);

        // flush everything
        void
        flush();

        // flush to the next byte boundary
        void
        flush_byte();

        // ensure at least n bits
        template<class FwdIt>
        bool
        fill(std::uint8_t n, FwdIt& begin, FwdIt const& end);

        // return n bits
        template<class Unsigned, class FwdIt>
        bool
        peek(Unsigned& value, std::uint8_t n, FwdIt& begin, FwdIt const& end);

        // return n bits, and consume
        template<class Unsigned, class FwdIt>
        bool
        read(Unsigned& value, std::uint8_t n, FwdIt& begin, FwdIt const& end);
    };

    enum state
    {
        s_head,
        s_type,
        s_typedo,
        
        s_stored,
        s_copy,

        s_table,
        s_lenlens,
        s_codelens,

        s_len,
        s_lenext,
        s_dist,
        s_distext,
        s_match,
        s_lit
    };

    enum codetype
    {
        CODES,
        LENS,
        DISTS
    };

    struct code
    {
        std::uint8_t op;
        std::uint8_t bits;
        std::uint16_t val;
    };

    static std::size_t constexpr ENOUGH_LENS = 852;
    static std::size_t constexpr ENOUGH_DISTS = 592;
    static std::size_t constexpr ENOUGH = ENOUGH_LENS+ENOUGH_DISTS;

    bitstream bi_;
    std::uint16_t nlen_;
    std::uint16_t have_;
    state s_ = s_head;
    std::uint8_t ndist_;
    std::uint8_t ncode_;
    code* next_;
    std::uint16_t lens_[320];
    std::uint16_t work_[288];
    std::array<code, ENOUGH> codes_;
    bool last_;

    window w_;
    code const* lencode_;
    code const* distcode_;
    std::uint8_t lenbits_;
    std::uint8_t distbits_;
    std::int8_t back_;
    std::uint8_t extra_;
    std::uint16_t length_;
    std::size_t was_;
    std::size_t offset_;

public:
    struct params
    {
        std::uint8_t const* next_in;
        std::size_t avail_in;
        std::size_t total_in = 0;

        std::uint8_t* next_out;
        std::size_t avail_out;
        std::size_t total_out = 0;
    };

    zistream_t();

    template<class DynamicBuffer, class ConstBufferSequence>
    std::size_t
    dwrite(DynamicBuffer& dynabuf,
        ConstBufferSequence const& in, error_code& ec);

    template<class ConstBufferSequence>
    std::size_t
    write(boost::asio::mutable_buffer& out,
        ConstBufferSequence const& in, error_code& ec);

    std::size_t
    write_one(boost::asio::mutable_buffer& out,
        boost::asio::const_buffer const& in, error_code& ec);

    void
    write(params& ps, error_code& ec);

private:
    template<class U1, class U2>
    static
    U1
    clamp(U1 u1, U2 u2)
    {
        if(u1 > u2)
            u1 = static_cast<U1>(u2);
        return u1;
    }

    void
    setfixed();

    void
    make_table(int type, std::uint16_t* lens, std::uint16_t codes,
        code** table, std::uint8_t* bits, std::uint16_t* work,
            error_code& ec);
};

using zistream = zistream_t<>;

} // detail
} // beast

#include <beast/core/impl/zistream.ipp>

#endif
