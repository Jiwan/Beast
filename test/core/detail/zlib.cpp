//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/detail/zlib/zlib.h>

#include <beast/unit_test/suite.hpp>
#include <cassert>
#include <memory>
#include <random>

namespace beast {
namespace detail {

class zlib_test : public beast::unit_test::suite
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
    make_source(std::size_t size)
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
    checkInflate(buffer const& input, buffer const& original)
    {
        for(std::size_t i = 0; i < input.size(); ++i)
        {
            buffer output(original.size());
            z_stream zs;
            zs.zalloc = Z_NULL;
            zs.zfree = Z_NULL;
            zs.opaque = Z_NULL;
            zs.avail_in = 0;
            zs.next_in = Z_NULL;
            expect(inflateInit2(&zs, 15) == Z_OK);
            zs.next_out = output.data();
            zs.avail_out = output.capacity();
            if(i > 0)
            {
                zs.next_in = (Bytef*)input.data();
                zs.avail_in = i;
                auto result = inflate(&zs, Z_FULL_FLUSH);
                expect(result == Z_OK);
            }
            zs.next_in = (Bytef*)input.data() + i;
            zs.avail_in = input.size() - i;
            auto result = inflate(&zs, Z_FULL_FLUSH);
            output.resize(output.capacity() - zs.avail_out);
            expect(result == Z_OK);
            expect(output.size() == original.size());
            expect(std::memcmp(
                output.data(), original.data(), original.size()) == 0);
            inflateEnd(&zs);
        }
    }

    void testCompress()
    {
        static std::size_t constexpr N = 2048;
        auto const original = make_source(N);
        for(int level = 0; level <= 9; ++level)
        {
            for(int strategy = 0; strategy <= 4; ++strategy)
            {
                z_stream zs;
                zs.zalloc = Z_NULL;
                zs.zfree = Z_NULL;
                zs.opaque = Z_NULL;
                zs.avail_in = 0;
                zs.next_in = Z_NULL;
                expect(deflateInit2(&zs, 
                    level,
                    Z_DEFLATED,
                    -15,
                    4,
                    strategy) == Z_OK);
                buffer output(deflateBound(&zs, original.size()));
                zs.next_in = (Bytef*)original.data();
                zs.avail_in = original.size();
                zs.next_out = output.data();
                zs.avail_out = output.capacity();
                auto result = deflate(&zs, Z_FULL_FLUSH);
                deflateEnd(&zs);
                expect(result == Z_OK);
                output.resize(output.capacity() - zs.avail_out);
                checkInflate(output, original);
            }
        }
    }

    void run() override
    {
        testCompress();
    }
};

BEAST_DEFINE_TESTSUITE(zlib,core,beast);

} // detail
} // beast

