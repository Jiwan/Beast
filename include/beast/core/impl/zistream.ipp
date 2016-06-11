//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_ZISTREAM_IPP
#define BEAST_IMPL_ZISTREAM_IPP

#include <beast/core/consuming_buffers.hpp>
#include <cassert>
#include <memory>

#include <beast/unit_test/dstream.hpp>

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

template<class _>
void
zistream_t<_>::window::
reset(std::uint16_t capacity)
{
    if(capacity_ != capacity)
    {
        p_.reset();
        capacity_ = capacity;
    }
    i_ = 0;
    size_ = 0;
}

template<class _>
void
zistream_t<_>::window::
read(std::uint8_t* out, std::uint16_t pos, std::uint16_t n)
{
    std::uint16_t i = (i_ - pos + capacity_) % capacity_;
    while(n--)
    {
        *out++ = p_[i];
        i = (i + 1) % capacity_;
    }
}

template<class _>
void
zistream_t<_>::window::
write(std::uint8_t const* in, std::uint16_t n)
{
    if(! p_)
        p_.reset(new std::uint8_t[capacity_]);
    if(n >= capacity_)
    {
        i_ = 0;
        size_ = capacity_;
        std::memcpy(&p_[0], in + n - capacity_, capacity_);
        return;
    }
    if(n < capacity_ - size_)
        size_ += n;
    else
        size_ = capacity_;
    auto m = std::min<std::uint16_t>(n, capacity_ - i_);
    std::memcpy(&p_[i_], in, m);
    if(m == n)
    {
        i_ = (i_ + m) % capacity_;
        return;
    }
    in += m;
    m = n - m;
    std::memcpy(&p_[0], in, m);
    i_ = m;
}

//------------------------------------------------------------------------------

template<class _>
inline
void
zistream_t<_>::bitstream::
drop(std::uint8_t n)
{
    assert(n_ >= n);
    n_ -= n;
    v_ >>= n;
}

template<class _>
inline
void
zistream_t<_>::bitstream::
flush()
{
    n_ = 0;
    v_ = 0;
}

template<class _>
inline
void
zistream_t<_>::bitstream::
flush_byte()
{
    drop(n_ % 8);
}

template<class _>
template<class FwdIt>
inline
bool
zistream_t<_>::bitstream::
fill(std::uint8_t n, FwdIt& begin, FwdIt const& end)
{
    while(n_ < n)
    {
        if(begin == end)
            return false;
        v_ += static_cast<value_type>(*begin++) << n_;
        n_ += 8;
    }
    return true;
}

template<class _>
template<class Unsigned, class FwdIt>
inline
bool
zistream_t<_>::bitstream::
peek(Unsigned& value, std::uint8_t n, FwdIt& begin, FwdIt const& end)
{
    assert(n <= sizeof(value)*8);
    if(! fill(n, begin, end))
        return false;
    value = v_ & ((1ULL << n) - 1);
    return true;
}

template<class _>
template<class Unsigned, class FwdIt>
inline
bool
zistream_t<_>::bitstream::
read(Unsigned& value, std::uint8_t n, FwdIt& begin, FwdIt const& end)
{
    assert(n < sizeof(v_)*8);
    if(! peek(value, n, begin, end))
        return false;
    v_ >>= n;
    n_ -= n;
    return true;
}

//------------------------------------------------------------------------------

template<class _>
zistream_t<_>::
zistream_t()
{
    w_.reset(32 * 1024);
}

template<class _>
template<class DynamicBuffer, class ConstBufferSequence>
std::size_t
zistream_t<_>::
dwrite(DynamicBuffer& dynabuf,
    ConstBufferSequence const& in, error_code& ec)
{
    using boost::asio::buffer_size;
    std::size_t used = 0;
    auto cb = consumed_buffers(in, 0);
    for(;;)
    {
        auto const mb = dynabuf.prepare(4096);
        auto d = *mb.begin();
        auto const n = write(d, cb, ec);
        auto const m = buffer_size(*mb.begin()) - buffer_size(d);
        dynabuf.commit(m);
        used += n;
        if(ec)
            break;
        cb.consume(n);
        if(buffer_size(cb) == 0)
            break;
    }
    return used;
}

template<class _>
template<class ConstBufferSequence>
std::size_t
zistream_t<_>::
write(boost::asio::mutable_buffer& out,
    ConstBufferSequence const& in, error_code& ec)
{
    using boost::asio::buffer_size;
    std::size_t used = 0;
    for(auto it = in.begin(); it != in.end();)
    {
        auto cur = *it++;
        used += write_one(out, cur, ec);
        if(ec)
            break;
    }
    return used;
}

template<class _>
std::size_t
zistream_t<_>::
write_one(boost::asio::mutable_buffer& out,
    boost::asio::const_buffer const& in, error_code& ec)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    params ps;
    ps.next_in = buffer_cast<std::uint8_t const*>(in);
    ps.avail_in = buffer_size(in);
    ps.next_out = buffer_cast<std::uint8_t*>(out);
    ps.avail_out = buffer_size(out);
    write(ps, ec);
    out = out + (buffer_size(out) - ps.avail_out);
    return buffer_size(in) - ps.avail_in;
}

template<class _>
void
zistream_t<_>::
write(params& ps, error_code& ec)
{
    auto put = ps.next_out;
    auto next = ps.next_in;
    auto const outend = ps.next_out + ps.avail_out;
    auto const end = next + ps.avail_in;;
    auto const err =
        [&](z_error ev)
        {
            ec = ev;
            return;
        };
    auto const more =
        [&]
        {
            auto const nwritten = put - ps.next_out;
            // VFALCO TODO Don't update the window unless necessary
            {
                if(nwritten >= 32768)
                    w_.write(put - 32768, 32768);
                else
                    w_.write(ps.next_out,
                        static_cast<std::uint16_t>(nwritten));
            }
            ps.total_in += next - ps.next_in;
            ps.total_out += nwritten;
            ps.next_out = put;
            ps.avail_out = outend - put;
            ps.next_in = next;
            ps.avail_in = end - next;
        };
    for(;;)
    {
        switch(s_)
        {
        case s_head:
            s_ = s_typedo;
            break;

        case s_type:
            s_ = s_typedo;
            // fall through

        case s_typedo:
        {
            if(! bi_.fill(3, next, end))
                return more();
            std::uint8_t v;
            bi_.read(v, 1, next, end);
            last_ = v != 0;
            bi_.read(v, 2, next, end);
            switch(v)
            {
            case 0: // uncompressed
                s_ = s_stored;
                break;

            case 1: // fixed Huffman table
                setfixed();
                s_ = s_len;
                break;

            case 2: // dynamic Huffman table
                s_ = s_table;
                break;

            default:
                return err(z_error::invalid_block_type);
            }
            break;
        }

        case s_stored:
        {
            bi_.flush_byte();
            std::uint32_t v;
            if(! bi_.peek(v, 32, next, end))
                return more();
            // flush instead of advancing 32 bits, otherwise
            // undefined behavior from too much right shifting.
            bi_.flush();
            if((v & 0xffff) != ((v >> 16) ^ 0xffff))
                return err(z_error::invalid_stored_block_lengths);
            length_ = v & 0xffff;
            s_ = s_copy;
            // if (flush == Z_TREES) goto inf_leave;
            // fall through
        }

        case s_copy:
        {
            auto copy = length_;
            if(copy == 0)
            {
                s_ = s_type;
                break;
            }
            auto const have =
                static_cast<std::size_t>(end - next);
            copy = clamp(copy, have);
            auto const left =
                static_cast<std::size_t>(outend - put);
            copy = clamp(copy, left);
            if(copy == 0)
                return more();
            std::memcpy(put, next, copy);
            next += copy;
            put += copy;
            length_ -= copy;
            break;
        }

        case s_table:
            if(! bi_.fill(5 + 5 + 4, next, end))
                return more();
            bi_.read(nlen_, 5, next, end);
            nlen_ += 257;
            bi_.read(ndist_, 5, next, end);
            ndist_ += 1;
            bi_.read(ncode_, 4, next, end);
            ncode_ += 4;
            have_ = 0;
            s_ = s_lenlens;
            // fall through

        case s_lenlens:
        {
            static std::array<std::uint8_t, 19> constexpr si = {{
                16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}};
            while(have_ < ncode_)
            {
                if(! bi_.read(lens_[si[have_]], 3, next, end))
                    return more();
                ++have_;
            }
            while(have_ < si.size())
                lens_[si[have_++]] = 0;
            next_ = codes_.data();
            lencode_ = next_;
            lenbits_ = 7;
            make_table(CODES, &lens_[0], 19,
                &next_, &lenbits_, work_, ec);
            if(ec)
                return;
            have_ = 0;
            s_ = s_codelens;
            // fall through
        }

        case s_codelens:
        {
            while(have_ < nlen_ + ndist_)
            {
                std::uint16_t v;
                if(! bi_.peek(v, lenbits_, next, end))
                    return more();
                auto cp = &lencode_[v];
                if(cp->val < 16)
                {
                    bi_.drop(cp->bits);
                    lens_[have_++] = cp->val;
                }
                else
                {
                    std::uint16_t len;
                    std::uint16_t copy;
                    if(cp->val == 16)
                    {
                        if(! bi_.fill(cp->bits + 2, next, end))
                            return more();
                        bi_.drop(cp->bits);
                        if(have_ == 0)
                            return err(z_error::invalid_bitlen_repeat);
                        bi_.read(copy, 2, next, end);
                        len = lens_[have_ - 1];
                        copy += 3;

                    }
                    else if(cp->val == 17)
                    {
                        if(! bi_.fill(cp->bits + 3, next, end))
                            return more();
                        bi_.drop(cp->bits);
                        bi_.read(copy, 3, next, end);
                        len = 0;
                        copy += 3;
                    }
                    else
                    {
                        if(! bi_.fill(cp->bits + 7, next, end))
                            return more();
                        bi_.drop(cp->bits);
                        bi_.read(copy, 7, next, end);
                        len = 0;
                        copy += 11;
                    }
                    if(have_ + copy > nlen_ + ndist_)
                        return err(z_error::invalid_bitlen_repeat);
                    while(copy--)
                        lens_[have_++] = len;
                }
            }
            if(lens_[256] == 0)
                return err(z_error::invalid_code_no_block_end);
            next_ = codes_.data();
            lencode_ = next_;
            lenbits_ = 9;
            make_table(LENS, &lens_[0],
                nlen_, &next_, &lenbits_, work_, ec);
            if(ec)
                return;
            distcode_ = next_;
            distbits_ = 6;
            make_table(DISTS, lens_ + nlen_,
                ndist_, &next_, &distbits_, work_, ec);
            if(ec)
                return;
            s_ = s_len;
            // if (flush == Z_TREES) goto inf_leave;
            break;
        }

        //case s_len_
        case s_len:
        {
            std::uint16_t v;
            back_ = 0;
            if(! bi_.peek(v, lenbits_, next, end))
                return more();
            auto cp = &lencode_[v];
            if(cp->op && (cp->op & 0xf0) == 0)
            {
                auto prev = cp;
                if(! bi_.peek(v, prev->bits + prev->op, next, end))
                    return more();
                cp = &lencode_[prev->val + (v >> prev->bits)];
                bi_.drop(prev->bits + cp->bits);
                back_ += prev->bits;
            }
            else
            {
                bi_.drop(cp->bits);
            }
            back_ += cp->bits;
            length_ = cp->val;
            if(cp->op == 0)
            {
                s_ = s_lit;
                break;
            }
            if(cp->op & 32)
            {
                back_ = -1;
                s_ = s_type;
                break;
            }
            if(cp->op & 64)
                return err(z_error::invalid_len_code);
            extra_ = cp->op & 15;
            s_ = s_lenext;
            // fall through
        }

        case s_lenext:
            if(extra_)
            {
                std::uint16_t v;
                if(! bi_.read(v, extra_, next, end))
                    return more();
                length_ += v;
                back_ += extra_;
            }
            was_ = length_;
            s_ = s_dist;
            // fall through

        case s_dist:
        {
            std::uint16_t v;
            if(! bi_.peek(v, distbits_, next, end))
                return more();
            auto cp = &distcode_[v];
            if((cp->op & 0xf0) == 0)
            {
                auto prev = cp;
                if(! bi_.peek(v, prev->bits + prev->op, next, end))
                    return more();
                cp = &distcode_[prev->val + (v >> prev->bits)];
                bi_.drop(prev->bits + cp->bits);
                back_ += prev->bits;
            }
            else
            {
                bi_.drop(cp->bits);
            }
            if(cp->op & 64)
                return err(z_error::invalid_dist_code);
            offset_ = cp->val;
            extra_ = cp->op & 15;
            s_ = s_distext;
            // fall through
        }

        case s_distext:
            if(extra_)
            {
                std::uint16_t v;
                if(! bi_.read(v, extra_, next, end))
                    return more();
                offset_ += v;
                back_ += extra_;
            }
            s_ = s_match;
            // fall through

        case s_match:
        {
            if(put == outend)
                return more();
            auto const written =
                static_cast<std::size_t>(put - ps.next_out);
            if(offset_ > written)
            {
                // copy from window
                auto offset = static_cast<std::uint16_t>(
                    offset_ - written);
                if(offset > w_.size())
                {
                    ec = z_error::invalid_window_offset;
                    return;
                }
                auto const n = clamp(length_, offset);
                w_.read(put, offset, n);
                put += n;
                length_ -= n;
            }
            else
            {
                // copy from output
                auto from = put - offset_;
                auto n = clamp(length_,
                    ps.avail_out - (put - ps.next_out));
                length_ -= n;
                do
                {
                    *put++ = *from++;
                }
                while(--n);
            }
            if(length_ == 0)
                s_ = s_len;
            break;
        }

        case s_lit:
        {
            if(put == outend)
                return more();
            auto const v = static_cast<std::uint8_t>(length_);
            *put++ = v;
            s_ = s_len;
            break;
        }
        }
    }
}

template<class _>
void
zistream_t<_>::
setfixed()
{
    static code const s_lenfix[512] = {
        {96,7,0},{0,8,80},{0,8,16},{20,8,115},{18,7,31},{0,8,112},{0,8,48},
        {0,9,192},{16,7,10},{0,8,96},{0,8,32},{0,9,160},{0,8,0},{0,8,128},
        {0,8,64},{0,9,224},{16,7,6},{0,8,88},{0,8,24},{0,9,144},{19,7,59},
        {0,8,120},{0,8,56},{0,9,208},{17,7,17},{0,8,104},{0,8,40},{0,9,176},
        {0,8,8},{0,8,136},{0,8,72},{0,9,240},{16,7,4},{0,8,84},{0,8,20},
        {21,8,227},{19,7,43},{0,8,116},{0,8,52},{0,9,200},{17,7,13},{0,8,100},
        {0,8,36},{0,9,168},{0,8,4},{0,8,132},{0,8,68},{0,9,232},{16,7,8},
        {0,8,92},{0,8,28},{0,9,152},{20,7,83},{0,8,124},{0,8,60},{0,9,216},
        {18,7,23},{0,8,108},{0,8,44},{0,9,184},{0,8,12},{0,8,140},{0,8,76},
        {0,9,248},{16,7,3},{0,8,82},{0,8,18},{21,8,163},{19,7,35},{0,8,114},
        {0,8,50},{0,9,196},{17,7,11},{0,8,98},{0,8,34},{0,9,164},{0,8,2},
        {0,8,130},{0,8,66},{0,9,228},{16,7,7},{0,8,90},{0,8,26},{0,9,148},
        {20,7,67},{0,8,122},{0,8,58},{0,9,212},{18,7,19},{0,8,106},{0,8,42},
        {0,9,180},{0,8,10},{0,8,138},{0,8,74},{0,9,244},{16,7,5},{0,8,86},
        {0,8,22},{64,8,0},{19,7,51},{0,8,118},{0,8,54},{0,9,204},{17,7,15},
        {0,8,102},{0,8,38},{0,9,172},{0,8,6},{0,8,134},{0,8,70},{0,9,236},
        {16,7,9},{0,8,94},{0,8,30},{0,9,156},{20,7,99},{0,8,126},{0,8,62},
        {0,9,220},{18,7,27},{0,8,110},{0,8,46},{0,9,188},{0,8,14},{0,8,142},
        {0,8,78},{0,9,252},{96,7,0},{0,8,81},{0,8,17},{21,8,131},{18,7,31},
        {0,8,113},{0,8,49},{0,9,194},{16,7,10},{0,8,97},{0,8,33},{0,9,162},
        {0,8,1},{0,8,129},{0,8,65},{0,9,226},{16,7,6},{0,8,89},{0,8,25},
        {0,9,146},{19,7,59},{0,8,121},{0,8,57},{0,9,210},{17,7,17},{0,8,105},
        {0,8,41},{0,9,178},{0,8,9},{0,8,137},{0,8,73},{0,9,242},{16,7,4},
        {0,8,85},{0,8,21},{16,8,258},{19,7,43},{0,8,117},{0,8,53},{0,9,202},
        {17,7,13},{0,8,101},{0,8,37},{0,9,170},{0,8,5},{0,8,133},{0,8,69},
        {0,9,234},{16,7,8},{0,8,93},{0,8,29},{0,9,154},{20,7,83},{0,8,125},
        {0,8,61},{0,9,218},{18,7,23},{0,8,109},{0,8,45},{0,9,186},{0,8,13},
        {0,8,141},{0,8,77},{0,9,250},{16,7,3},{0,8,83},{0,8,19},{21,8,195},
        {19,7,35},{0,8,115},{0,8,51},{0,9,198},{17,7,11},{0,8,99},{0,8,35},
        {0,9,166},{0,8,3},{0,8,131},{0,8,67},{0,9,230},{16,7,7},{0,8,91},
        {0,8,27},{0,9,150},{20,7,67},{0,8,123},{0,8,59},{0,9,214},{18,7,19},
        {0,8,107},{0,8,43},{0,9,182},{0,8,11},{0,8,139},{0,8,75},{0,9,246},
        {16,7,5},{0,8,87},{0,8,23},{64,8,0},{19,7,51},{0,8,119},{0,8,55},
        {0,9,206},{17,7,15},{0,8,103},{0,8,39},{0,9,174},{0,8,7},{0,8,135},
        {0,8,71},{0,9,238},{16,7,9},{0,8,95},{0,8,31},{0,9,158},{20,7,99},
        {0,8,127},{0,8,63},{0,9,222},{18,7,27},{0,8,111},{0,8,47},{0,9,190},
        {0,8,15},{0,8,143},{0,8,79},{0,9,254},{96,7,0},{0,8,80},{0,8,16},
        {20,8,115},{18,7,31},{0,8,112},{0,8,48},{0,9,193},{16,7,10},{0,8,96},
        {0,8,32},{0,9,161},{0,8,0},{0,8,128},{0,8,64},{0,9,225},{16,7,6},
        {0,8,88},{0,8,24},{0,9,145},{19,7,59},{0,8,120},{0,8,56},{0,9,209},
        {17,7,17},{0,8,104},{0,8,40},{0,9,177},{0,8,8},{0,8,136},{0,8,72},
        {0,9,241},{16,7,4},{0,8,84},{0,8,20},{21,8,227},{19,7,43},{0,8,116},
        {0,8,52},{0,9,201},{17,7,13},{0,8,100},{0,8,36},{0,9,169},{0,8,4},
        {0,8,132},{0,8,68},{0,9,233},{16,7,8},{0,8,92},{0,8,28},{0,9,153},
        {20,7,83},{0,8,124},{0,8,60},{0,9,217},{18,7,23},{0,8,108},{0,8,44},
        {0,9,185},{0,8,12},{0,8,140},{0,8,76},{0,9,249},{16,7,3},{0,8,82},
        {0,8,18},{21,8,163},{19,7,35},{0,8,114},{0,8,50},{0,9,197},{17,7,11},
        {0,8,98},{0,8,34},{0,9,165},{0,8,2},{0,8,130},{0,8,66},{0,9,229},
        {16,7,7},{0,8,90},{0,8,26},{0,9,149},{20,7,67},{0,8,122},{0,8,58},
        {0,9,213},{18,7,19},{0,8,106},{0,8,42},{0,9,181},{0,8,10},{0,8,138},
        {0,8,74},{0,9,245},{16,7,5},{0,8,86},{0,8,22},{64,8,0},{19,7,51},
        {0,8,118},{0,8,54},{0,9,205},{17,7,15},{0,8,102},{0,8,38},{0,9,173},
        {0,8,6},{0,8,134},{0,8,70},{0,9,237},{16,7,9},{0,8,94},{0,8,30},
        {0,9,157},{20,7,99},{0,8,126},{0,8,62},{0,9,221},{18,7,27},{0,8,110},
        {0,8,46},{0,9,189},{0,8,14},{0,8,142},{0,8,78},{0,9,253},{96,7,0},
        {0,8,81},{0,8,17},{21,8,131},{18,7,31},{0,8,113},{0,8,49},{0,9,195},
        {16,7,10},{0,8,97},{0,8,33},{0,9,163},{0,8,1},{0,8,129},{0,8,65},
        {0,9,227},{16,7,6},{0,8,89},{0,8,25},{0,9,147},{19,7,59},{0,8,121},
        {0,8,57},{0,9,211},{17,7,17},{0,8,105},{0,8,41},{0,9,179},{0,8,9},
        {0,8,137},{0,8,73},{0,9,243},{16,7,4},{0,8,85},{0,8,21},{16,8,258},
        {19,7,43},{0,8,117},{0,8,53},{0,9,203},{17,7,13},{0,8,101},{0,8,37},
        {0,9,171},{0,8,5},{0,8,133},{0,8,69},{0,9,235},{16,7,8},{0,8,93},
        {0,8,29},{0,9,155},{20,7,83},{0,8,125},{0,8,61},{0,9,219},{18,7,23},
        {0,8,109},{0,8,45},{0,9,187},{0,8,13},{0,8,141},{0,8,77},{0,9,251},
        {16,7,3},{0,8,83},{0,8,19},{21,8,195},{19,7,35},{0,8,115},{0,8,51},
        {0,9,199},{17,7,11},{0,8,99},{0,8,35},{0,9,167},{0,8,3},{0,8,131},
        {0,8,67},{0,9,231},{16,7,7},{0,8,91},{0,8,27},{0,9,151},{20,7,67},
        {0,8,123},{0,8,59},{0,9,215},{18,7,19},{0,8,107},{0,8,43},{0,9,183},
        {0,8,11},{0,8,139},{0,8,75},{0,9,247},{16,7,5},{0,8,87},{0,8,23},
        {64,8,0},{19,7,51},{0,8,119},{0,8,55},{0,9,207},{17,7,15},{0,8,103},
        {0,8,39},{0,9,175},{0,8,7},{0,8,135},{0,8,71},{0,9,239},{16,7,9},
        {0,8,95},{0,8,31},{0,9,159},{20,7,99},{0,8,127},{0,8,63},{0,9,223},
        {18,7,27},{0,8,111},{0,8,47},{0,9,191},{0,8,15},{0,8,143},{0,8,79},
        {0,9,255}
    };

    static code constexpr s_distfix[32] = {
        {16,5,1},{23,5,257},{19,5,17},{27,5,4097},{17,5,5},{25,5,1025},
        {21,5,65},{29,5,16385},{16,5,3},{24,5,513},{20,5,33},{28,5,8193},
        {18,5,9},{26,5,2049},{22,5,129},{64,5,0},{16,5,2},{23,5,385},
        {19,5,25},{27,5,6145},{17,5,7},{25,5,1537},{21,5,97},{29,5,24577},
        {16,5,4},{24,5,769},{20,5,49},{28,5,12289},{18,5,13},{26,5,3073},
        {22,5,193},{64,5,0}
    };

    lencode_ = s_lenfix;
    lenbits_ = 9;
    distcode_ = s_distfix;
    distbits_ = 5;
}

/*
    Build a set of tables to decode the provided canonical Huffman code.
    The code lengths are lens[0..codes-1].  The result starts at *table,
    whose indices are 0..2^bits-1.  work is a writable array of at least
    lens shorts, which is used as a work area.  type is the type of code
    to be generated, CODES, LENS, or DISTS.  On return, zero is success,
    -1 is an invalid code, and +1 means that ENOUGH isn't enough.  table
    on return points to the next available entry's address.  bits is the
    requested root table index bits, and on return it is the actual root
    table index bits.  It will differ if the request is greater than the
    longest code or if it is less than the shortest code.
*/
template<class _>
void
zistream_t<_>::
make_table(int type, std::uint16_t* lens, std::uint16_t codes,
    code** table, std::uint8_t* bits, std::uint16_t* work,
        error_code& ec)
{
    auto constexpr MAXBITS = 15;
    unsigned len;                       /* a code's length in bits */
    unsigned sym;                       /* index of code symbols */
    unsigned min, max;                  /* minimum and maximum code lengths */
    unsigned root;                      /* number of index bits for root table */
    unsigned curr;                      /* number of index bits for current table */
    unsigned drop;                      /* code bits to drop for sub-table */
    int left;                           /* number of prefix codes available */
    unsigned used;                      /* code entries in table used */
    unsigned huff;                      /* Huffman code */
    unsigned incr;                      /* for incrementing code, index */
    unsigned fill;                      /* index for replicating entries */
    unsigned low;                       /* low bits for current root entry */
    unsigned mask;                      /* mask for low root bits */
    code here;                          /* table entry for duplication */
    code *next;                         /* next available space in table */
    const unsigned short *base;         /* base value table to use */
    const unsigned short *extra;        /* extra bits table to use */
    int end;                            /* use base and extra for symbol > end */
    unsigned short count[MAXBITS+1];    /* number of codes of each length */
    unsigned short offs[MAXBITS+1];     /* offsets in table for each length */
    /* Length codes 257..285 base */
    static std::uint16_t constexpr lbase[31] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
    /* Length codes 257..285 extra */
    static std::uint16_t constexpr lext[31] = {
        16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18,
        19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 16, 72, 78};
    /* Distance codes 0..29 base */
    static std::uint16_t constexpr dbase[32] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577, 0, 0};
    /* Distance codes 0..29 extra */
    static std::uint16_t constexpr dext[32] = {
        16, 16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
        23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
        28, 28, 29, 29, 64, 64};

    /*
        Process a set of code lengths to create a canonical Huffman code.  The
        code lengths are lens[0..codes-1].  Each length corresponds to the
        symbols 0..codes-1.  The Huffman code is generated by first sorting the
        symbols by length from short to long, and retaining the symbol order
        for codes with equal lengths.  Then the code starts with all zero bits
        for the first code of the shortest length, and the codes are integer
        increments for the same length, and zeros are appended as the length
        increases.  For the deflate format, these bits are stored backwards
        from their more natural integer increment ordering, and so when the
        decoding tables are built in the large loop below, the integer codes
        are incremented backwards.
        This routine assumes, but does not check, that all of the entries in
        lens[] are in the range 0..MAXBITS.  The caller must assure this.
        1..MAXBITS is interpreted as that code length.  zero means that that
        symbol does not occur in this code.
        The codes are sorted by computing a count of codes for each length,
        creating from that a table of starting indices for each length in the
        sorted table, and then entering the symbols in order in the sorted
        table.  The sorted table is work[], with that space being provided by
        the caller.
        The length counts are used for other purposes as well, i.e. finding
        the minimum and maximum length codes, determining if there are any
        codes at all, checking for a valid set of lengths, and looking ahead
        at length counts to determine sub-table sizes when building the
        decoding tables.
    */
    /* accumulate lengths for codes (assumes lens[] all in 0..MAXBITS) */
    for (len = 0; len <= MAXBITS; len++)
        count[len] = 0;
    for (sym = 0; sym < codes; sym++)
        count[lens[sym]]++;

    /* bound code lengths, force root to be within code lengths */
    root = *bits;
    for (max = MAXBITS; max >= 1; max--)
        if (count[max] != 0)
            break;
    if (root > max)
        root = max;
    if (max == 0)
    {
        // no symbols to code at all
        here.op = (unsigned char)64;    /* invalid code marker */
        here.bits = (unsigned char)1;
        here.val = (unsigned short)0;
        *(*table)++ = here;             /* make a table to force an error */
        *(*table)++ = here;
        *bits = 1;
        return;     /* no symbols, but wait for decoding to report error */
    }
    for (min = 1; min < max; min++)
        if (count[min] != 0)
            break;
    if (root < min)
        root = min;

    /* check for an over-subscribed or incomplete set of lengths */
    left = 1;
    for (len = 1; len <= MAXBITS; len++)
    {
        left <<= 1;
        left -= count[len];
        if (left < 0)
        {
            ec = z_error::table_oversubscribed;
            return;
        }
    }
    if (left > 0 && (type == CODES || max != 1))
    {
        /* incomplete set */
        ec = z_error::table_incomplete;
        return;
    }

    /* generate offsets into symbol table for each length for sorting */
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + count[len];

    /* sort symbols by length, by symbol order within each length */
    for (sym = 0; sym < codes; sym++)
        if (lens[sym] != 0)
            work[offs[lens[sym]]++] = (unsigned short)sym;

    /*
        Create and fill in decoding tables.  In this loop, the table being
        filled is at next and has curr index bits.  The code being used is huff
        with length len.  That code is converted to an index by dropping drop
        bits off of the bottom.  For codes where len is less than drop + curr,
        those top drop + curr - len bits are incremented through all values to
        fill the table with replicated entries.
        root is the number of index bits for the root table.  When len exceeds
        root, sub-tables are created pointed to by the root entry with an index
        of the low root bits of huff.  This is saved in low to check for when a
        new sub-table should be started.  drop is zero when the root table is
        being filled, and drop is root when sub-tables are being filled.
        When a new sub-table is needed, it is necessary to look ahead in the
        code lengths to determine what size sub-table is needed.  The length
        counts are used for this, and so count[] is decremented as codes are
        entered in the tables.
        used keeps track of how many table entries have been allocated from the
        provided *table space.  It is checked for LENS and DIST tables against
        the constants ENOUGH_LENS and ENOUGH_DISTS to guard against changes in
        the initial root table size constants.  See the comments in inftrees.h
        for more information.
        sym increments through all symbols, and the loop terminates when
        all codes of length max, i.e. all codes, have been processed.  This
        routine permits incomplete codes, so another loop after this one fills
        in the rest of the decoding tables with invalid code markers.
        */

    /* set up for code type */
    switch (type)
    {
    case CODES:
        base = extra = work;    /* dummy value--not used */
        end = 19;
        break;
    case LENS:
        base = lbase;
        base -= 257;
        extra = lext;
        extra -= 257;
        end = 256;
        break;
    default:            /* DISTS */
        base = dbase;
        extra = dext;
        end = -1;
    }

    /* initialize state for loop */
    huff = 0;                   /* starting code */
    sym = 0;                    /* starting code symbol */
    len = min;                  /* starting code length */
    next = *table;              /* current table to fill in */
    curr = root;                /* current table index bits */
    drop = 0;                   /* current bits to drop from code for index */
    low = (unsigned)(-1);       /* trigger new sub-table when len > root */
    used = 1U << root;          /* use root table entries */
    mask = used - 1;            /* mask for comparing low */

    /* check available table space */
    if ((type == LENS && used > ENOUGH_LENS) ||
            (type == DISTS && used > ENOUGH_DISTS))
    {
        ec = z_error::table_overflow;
        return;
    }

    /* process all codes and make table entries */
    for (;;)
    {
        /* create table entry */
        here.bits = (unsigned char)(len - drop);
        if ((int)(work[sym]) < end)
        {
            here.op = (unsigned char)0;
            here.val = work[sym];
        }
        else if ((int)(work[sym]) > end)
        {
            here.op = (unsigned char)(extra[work[sym]]);
            here.val = base[work[sym]];
        }
        else
        {
            here.op = (unsigned char)(32 + 64);         /* end of block */
            here.val = 0;
        }

        /* replicate for those indices with low len bits equal to huff */
        incr = 1U << (len - drop);
        fill = 1U << curr;
        min = fill;                 /* save offset to next table */
        do
        {
            fill -= incr;
            next[(huff >> drop) + fill] = here;
        }
        while (fill != 0);

        /* backwards increment the len-bit code huff */
        incr = 1U << (len - 1);
        while (huff & incr)
            incr >>= 1;
        if (incr != 0)
        {
            huff &= incr - 1;
            huff += incr;
        }
        else
            huff = 0;

        /* go to next symbol, update count, len */
        sym++;
        if (--(count[len]) == 0)
        {
            if (len == max)
                break;
            len = lens[work[sym]];
        }

        /* create new sub-table if needed */
        if (len > root && (huff & mask) != low)
        {
            /* if first time, transition to sub-tables */
            if (drop == 0)
                drop = root;

            /* increment past last table */
            next += min;            /* here min is 1 << curr */

            /* determine length of next table */
            curr = len - drop;
            left = (int)(1 << curr);
            while (curr + drop < max)
            {
                left -= count[curr + drop];
                if (left <= 0) break;
                curr++;
                left <<= 1;
            }

            /* check for enough space */
            used += 1U << curr;
            if ((type == LENS && used > ENOUGH_LENS) ||
                    (type == DISTS && used > ENOUGH_DISTS))
            {
                ec = z_error::table_overflow;
                return;
            }

            /* point entry in root table to sub-table */
            low = huff & mask;
            (*table)[low].op = (unsigned char)curr;
            (*table)[low].bits = (unsigned char)root;
            (*table)[low].val = (unsigned short)(next - *table);
        }
    }

    /* fill in remaining table entry if code is incomplete (guaranteed to have
        at most one remaining entry, since if the code is incomplete, the
        maximum code length that was allowed to get this far is one bit) */
    if (huff != 0)
    {
        here.op = (unsigned char)64;            /* invalid code marker */
        here.bits = (unsigned char)(len - drop);
        here.val = (unsigned short)0;
        next[huff] = here;
    }

    /* set return parameters */
    *table += used;
    *bits = static_cast<std::uint8_t>(root);
}

} // detail
} // beast

#endif
