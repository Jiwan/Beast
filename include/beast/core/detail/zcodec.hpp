//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_ZCODEC_HPP
#define BEAST_DETAIL_ZCODEC_HPP

#include <beast/core/error.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

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

enum zcodec_error
{
    end_of_stream = 1,
    invalid_block_type,
    invalid_len_code,
    invalid_dist_code,
    invalid_stored_block_lengths
};

class zcodec_error_category : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "zcodec";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<zcodec_error>(ev))
        {
        case zcodec_error::end_of_stream:
            return "end of deflate stream";

        case zcodec_error::invalid_block_type:
            return "invalid block type";

        case zcodec_error::invalid_len_code:
            return "invalid literal/length code";
            
        case zcodec_error::invalid_dist_code:
            return "invalid distance code";

        case zcodec_error::invalid_stored_block_lengths:
            return "invalid stored block lengths";

        default:
            return "deflate error";
        }
    }

    boost::system::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return boost::system::error_condition(ev, *this);
    }

    bool
    equivalent(int ev,
        boost::system::error_condition const& condition
            ) const noexcept override
    {
        return condition.value() == ev &&
            &condition.category() == this;
    }

    bool
    equivalent(error_code const& error, int ev) const noexcept override
    {
        return error.value() == ev && &error.category() == this;
    }
};

inline
boost::system::error_category const&
get_zcodec_error_category()
{
    static zcodec_error_category const cat{};
    return cat;
}

inline
boost::system::error_code
make_error_code(zcodec_error ev)
{
    return error_code(static_cast<int>(ev),
        get_zcodec_error_category());
}

} // beast

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::zcodec_error>
{
    static bool const value = true;
};
} // system
} // boost

namespace beast {
namespace detail {

template<class = void>
class z_istream_t
{
    class window
    {
        std::uint16_t i_;
        std::uint16_t size_ = 0;
        std::unique_ptr<std::uint8_t[]> p_;

    public:
        std::uint16_t
        size() const
        {
            return size_;
        }

        void
        reset(std::uint16_t size);

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
        peek(std::uint8_t n, Unsigned& value, FwdIt& begin, FwdIt const& end);

        // return n bits, and consume
        template<class Unsigned, class FwdIt>
        bool
        read(std::uint8_t n, Unsigned& value, FwdIt& begin, FwdIt const& end);
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
    std::uint16_t i_;
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
    std::size_t length_;
    std::size_t was_;
    std::size_t offset_;

    int hrv_;
    int hrl_;

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

    z_istream_t();

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
    void
    setfixed();

    int
    mktab(int type, std::uint16_t* lens, std::uint16_t codes,
        code** table, std::uint8_t* bits, std::uint16_t* work);
};

using z_istream = z_istream_t<>;

} // detail
} // beast

#include <beast/core/impl/zcodec.ipp>

#endif
