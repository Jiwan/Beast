//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/detail/zistream.hpp>

#include "../zlib/zlib.h"
#include <beast/unit_test/suite.hpp>
#include <cassert>
#include <random>

namespace beast {
namespace detail {

class zistream_test : public beast::unit_test::suite
{
public:
    class buffer
    {
        std::size_t size_ = 0;
        std::size_t capacity_;
        std::unique_ptr<std::uint8_t[]> p_;

    public:
        buffer(buffer&&) = default;

        explicit
        buffer(std::size_t capacity)
            : capacity_(capacity)
            , p_(new std::uint8_t[capacity_])
        {
        }

        std::size_t
        size() const
        {
            return size_;
        }

        std::size_t
        capacity() const
        {
            return capacity_;
        }

        std::uint8_t const*
        data() const
        {
            return p_.get();
        }

        std::uint8_t*
        data()
        {
            return p_.get();
        }

        void
        resize(std::size_t size)
        {
            assert(size <= capacity_);
            size_ = size;
        }
    };

    buffer
    make_rnd1(std::size_t size)
    {
        std::mt19937 rng;
        buffer b(size);
        auto p = b.data();
        std::size_t n = 0;
        static std::string const chars(
            "01234567890{}\"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            "{{{{{{{{{{}}}}}}}}}}  ");
        while(n < size)
        {
            *p++ = chars[rng()%chars.size()];
            ++n;
        }
        b.resize(n);
        return b;
    }

    void
    testStreaming()
    {
        auto constexpr N = 4 * 1024;
        auto bi = make_rnd1(N);
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = 0;
        zs.next_in = Z_NULL;
        expect(deflateInit2(&zs,
            Z_DEFAULT_COMPRESSION,
            Z_DEFLATED, -15,
            4, // memory level 1-9
            Z_FIXED
            ) == Z_OK);
        auto const avail_out = deflateBound(&zs, N);
        buffer bo(avail_out);
        zs.avail_in = bi.size();
        zs.next_in = (Bytef*)bi.data();
        zs.avail_out = bo.capacity();
        zs.next_out = (Bytef*)bo.data();
        deflate(&zs, Z_FULL_FLUSH);
        bo.resize(bo.capacity() - zs.avail_out);
#if 0
        {
            z_stream zsi;
            zsi.zalloc = Z_NULL;
            zsi.zfree = Z_NULL;
            zsi.opaque = Z_NULL;
            zsi.avail_in = 0;
            zsi.next_in = Z_NULL;
            expect(inflateInit2(&zsi, -1*15) == Z_OK);
            buffer bc(N);
            zsi.next_in = bo.data();
            zsi.avail_in = bo.size();
            zsi.next_out = bc.data();
            zsi.avail_out = bc.capacity();
            inflate(&zsi, Z_FULL_FLUSH);
            bc.resize(bc.capacity() - zsi.avail_out);
            if(expect(bc.size() == N))
                expect(std::memcmp(bc.data(), bi.data(), N) == 0);
            inflateEnd(&zsi);
        }
#endif
        for(std::size_t i = 0; i < bo.size(); ++i)
        {
            // decompress with input as two pieces
            buffer bc(N);
            error_code ec;
            zistream zi;
            zistream::params ps;
            ps.next_out = bc.data();
            ps.avail_out = bc.capacity();
            if(i > 0)
            {
                ps.next_in = bo.data();
                ps.avail_in = i;
                zi.write(ps, ec);
                if(! expect(! ec, ec.message()))
                    continue;
            }
            ps.next_in = bo.data() + i;
            ps.avail_in = bo.size() - i;
            zi.write(ps, ec);
            if(! expect(! ec, ec.message()))
                continue;
            bc.resize(ps.total_out);
            if(expect(bc.size() == N, "wrong size"))
                expect(std::memcmp(bc.data(), bi.data(), N) == 0,
                    "wrong data");
        }
        for(std::size_t i = 0; i < N; ++i)
        {
            // decompress with output as two pieces
            buffer bc(N);
            error_code ec;
            zistream zi;
            zistream::params ps;
            ps.next_in = bo.data();
            ps.avail_in = bo.size();
            if(i > 0)
            {
                ps.next_out = bc.data();
                ps.avail_out = i;
                zi.write(ps, ec);
                if(! expect(! ec, ec.message()))
                    continue;
            }
            ps.next_out = bc.data() + i;
            ps.avail_out = bc.capacity() - i;
            zi.write(ps, ec);
            if(! expect(! ec, ec.message()))
                continue;
            bc.resize(ps.total_out);
            if(expect(bc.size() == N, "wrong size"))
                expect(std::memcmp(bc.data(), bi.data(), N) == 0,
                    "wrong data");
        }
        deflateEnd(&zs);
    }

    void
    run()
    {
        testStreaming();
    }
};

BEAST_DEFINE_TESTSUITE(zistream,core,beast);

} // detail
} // beast
