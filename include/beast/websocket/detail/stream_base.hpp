//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP
#define BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP

#include <beast/websocket/error.hpp>
#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/detail/decorator.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <beast/websocket/detail/invokable.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/websocket/detail/zstreams.hpp>
#include <beast/core/detail/zcodec.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <boost/asio/error.hpp>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace beast {
namespace websocket {
namespace detail {

template<class UInt>
static
std::size_t
clamp(UInt x)
{
    if(x >= std::numeric_limits<std::size_t>::max())
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(x);
}

template<class UInt>
static
std::size_t
clamp(UInt x, std::size_t limit)
{
    if(x >= limit)
        return limit;
    return static_cast<std::size_t>(x);
}

using pong_cb = std::function<void(ping_data const&)>;

/// Identifies the role of a WebSockets stream.
enum class role_type
{
    /// Stream is operating as a client.
    client,

    /// Stream is operating as a server.
    server
};

//------------------------------------------------------------------------------

struct stream_base
{
protected:
    friend class frame_test;

    struct op {};

    detail::maskgen maskgen_;                   // source of mask keys
    decorator_type d_;                          // adorns http messages
    pong_cb pong_cb_;                           // pong callback

    role_type role_;                            // server or client
    bool failed_;                               // the connection failed
    bool wr_close_;                             // sent close frame
    op* wr_block_;                              // op currenly writing
    ping_data* pong_data_;                      // where to put pong payload
    invokable rd_op_;                           // invoked after write completes
    invokable wr_op_;                           // invoked after read completes
    close_reason cr_;                           // set from received close frame

    struct opt_t
    {
        std::size_t msg_max = 16 * 1024 * 1024; // max message size
        std::uint16_t rd_buf_size = 4096;       // read buffer size
        std::uint16_t wr_buf_size = 4096;       // write buffer size
        opcode wr_opc = opcode::text;           // outgoing message type
        bool autofrag = true;                   // auto fragment
        bool keepalive = false;                 // close on failed upgrade
        bool pmd_enable = true;                 // if pmd extension is enabled
        bool compress = true;                   // if sent messages should be compressed
    };

    struct rd_t
    {
        detail::frame_header fh;                // current frame header
        detail::prepared_key_type key;          // prepared masking key
        detail::utf8_checker utf8_check;        // for current text msg
        std::unique_ptr<std::uint8_t[]> buf;    // read buffer storage
        std::uint64_t size;                     // size of the current message so far
        std::uint64_t need = 0;                 // bytes left in msg frame payload
        std::uint16_t max;                      // size of read buffer
        opcode opc;                             // opcode of current msg
        bool cont;                              // expecting a continuation frame
    };

    struct wr_t
    {
        std::size_t size;                       // amount stored in buffer
        std::unique_ptr<std::uint8_t[]> buf;    // write buffer storage
        std::uint16_t max;                      // size of write buffer
        bool cont;                              // next frame is continuation frame
        bool autofrag;                          // if this message is auto fragmented
    };

    struct pmd_t
    {
        bool rd_set;                            // if current read message is compressed
        bool wr_set;                            // if current write message is compressed
        zistream zi;
        zostream zo;
        beast::detail::z_istream z_i;
    };

    opt_t opt_;
    rd_t rd_;
    wr_t wr_;
    std::unique_ptr<pmd_t> pmd_;                // per-message deflate settings

    stream_base(stream_base&&) = default;
    stream_base(stream_base const&) = delete;
    stream_base& operator=(stream_base&&) = default;
    stream_base& operator=(stream_base const&) = delete;

    stream_base()
        : d_(new decorator<default_decorator>{})
    {
    }

    template<class = void>
    void
    open(role_type role);

    template<class = void>
    void
    close();

    template<class DynamicBuffer>
    std::size_t
    read_fh1(DynamicBuffer& db, close_code::value& code);

    template<class DynamicBuffer>
    void
    read_fh2(DynamicBuffer& db, close_code::value& code);

    template<class = void>
    void
    rd_prepare();

    template<class = void>
    void
    wr_prepare(bool compress);

    template<class DynamicBuffer>
    void
    write_close(DynamicBuffer& db, close_reason const& rc);

    template<class DynamicBuffer>
    void
    write_ping(DynamicBuffer& db, opcode op, ping_data const& data);
};

template<class _>
void
stream_base::
open(role_type role)
{
    // VFALCO TODO analyze and remove dupe code in reset()
    role_ = role;
    failed_ = false;
    rd_.need = 0;
    rd_.cont = false;
    wr_close_ = false;
    wr_block_ = nullptr;    // should be nullptr on close anyway
    pong_data_ = nullptr;   // should be nullptr on close anyway

    if(pmd_)
    {
        pmd_->zi.init();
        pmd_->zo.init();
    }

    wr_.cont = false;
    wr_.size = 0;
}

template<class _>
void
stream_base::
close()
{
    rd_.buf.reset();
    wr_.buf.reset();

    if(pmd_)
        pmd_.reset();
}

// Read fixed frame header
// Requires at least 2 bytes
//
template<class DynamicBuffer>
std::size_t
stream_base::
read_fh1(DynamicBuffer& db, close_code::value& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    auto const err =
        [&](close_code::value cv)
        {
            code = cv;
            return 0;
        };
    std::uint8_t b[2];
    assert(buffer_size(db.data()) >= sizeof(b));
    db.consume(buffer_copy(buffer(b), db.data()));
    std::size_t need;
    rd_.fh.len = b[1] & 0x7f;
    switch(rd_.fh.len)
    {
        case 126: need = 2; break;
        case 127: need = 8; break;
        default:
            need = 0;
    }
    rd_.fh.mask = (b[1] & 0x80) != 0;
    if(rd_.fh.mask)
        need += 4;
    rd_.fh.op   = static_cast<opcode>(b[0] & 0x0f);
    rd_.fh.fin  = (b[0] & 0x80) != 0;
    rd_.fh.rsv1 = (b[0] & 0x40) != 0;
    rd_.fh.rsv2 = (b[0] & 0x20) != 0;
    rd_.fh.rsv3 = (b[0] & 0x10) != 0;
    switch(rd_.fh.op)
    {
    case opcode::binary:
    case opcode::text:
        if(rd_.cont)
        {
            // new data frame when continuation expected
            return err(close_code::protocol_error);
        }
        if((rd_.fh.rsv1 & ! pmd_) ||
            rd_.fh.rsv2 || rd_.fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        if(pmd_)
            pmd_->rd_set = rd_.fh.rsv1;
        break;

    case opcode::cont:
        if(! rd_.cont)
        {
            // continuation without an active message
            return err(close_code::protocol_error);
        }
        if(rd_.fh.rsv1 || rd_.fh.rsv2 || rd_.fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        break;

    default:
        if(is_reserved(rd_.fh.op))
        {
            // reserved opcode
            return err(close_code::protocol_error);
        }
        if(! rd_.fh.fin)
        {
            // fragmented control message
            return err(close_code::protocol_error);
        }
        if(rd_.fh.len > 125)
        {
            // invalid length for control message
            return err(close_code::protocol_error);
        }
        if(rd_.fh.rsv1 || rd_.fh.rsv2 || rd_.fh.rsv3)
        {
            // reserved bits not cleared
            return err(close_code::protocol_error);
        }
        break;
    }
    // unmasked frame from client
    if(role_ == role_type::server && ! rd_.fh.mask)
    {
        code = close_code::protocol_error;
        return 0;
    }
    // masked frame from server
    if(role_ == role_type::client && rd_.fh.mask)
    {
        code = close_code::protocol_error;
        return 0;
    }
    code = close_code::none;
    return need;
}

// Decode variable frame header from stream
//
template<class DynamicBuffer>
void
stream_base::
read_fh2(DynamicBuffer& db, close_code::value& code)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    using namespace boost::endian;
    switch(rd_.fh.len)
    {
    case 126:
    {
        std::uint8_t b[2];
        assert(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        rd_.fh.len = big_uint16_to_native(&b[0]);
        // length not canonical
        if(rd_.fh.len < 126)
        {
            code = close_code::protocol_error;
            return;
        }
        break;
    }
    case 127:
    {
        std::uint8_t b[8];
        assert(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        rd_.fh.len = big_uint64_to_native(&b[0]);
        // length not canonical
        if(rd_.fh.len < 65536)
        {
            code = close_code::protocol_error;
            return;
        }
        break;
    }
    }
    if(rd_.fh.mask)
    {
        std::uint8_t b[4];
        assert(buffer_size(db.data()) >= sizeof(b));
        db.consume(buffer_copy(buffer(b), db.data()));
        rd_.fh.key = little_uint32_to_native(&b[0]);
    }
    else
    {
        // initialize this otherwise operator== breaks
        rd_.fh.key = 0;
    }
    if(rd_.fh.mask)
        prepare_key(rd_.key, rd_.fh.key);
    if(! is_control(rd_.fh.op))
    {
        if(rd_.fh.op != opcode::cont)
        {
            rd_.size = rd_.fh.len;
            rd_.opc = rd_.fh.op;
        }
        else
        {
            if(rd_.size > std::numeric_limits<
                std::uint64_t>::max() - rd_.fh.len)
            {
                code = close_code::too_big;
                return;
            }
            rd_.size += rd_.fh.len;
        }
        if(opt_.msg_max && rd_.size > opt_.msg_max)
        {
            code = close_code::too_big;
            return;
        }
        rd_.need = rd_.fh.len;
        rd_.cont = ! rd_.fh.fin;
    }
    code = close_code::none;
}

template<class _>
void
stream_base::
rd_prepare()
{
    if(rd_.need == rd_.fh.len)
    {
        if(! rd_.buf || rd_.max != opt_.rd_buf_size)
        {
            rd_.max = opt_.rd_buf_size;
            rd_.buf.reset(new std::uint8_t[rd_.max]);
        }
    }
}

template<class _>
void
stream_base::
wr_prepare(bool)
{
}

template<class DynamicBuffer>
void
stream_base::
write_close(DynamicBuffer& db, close_reason const& cr)
{
    using namespace boost::endian;
    frame_header fh;
    fh.op = opcode::close;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = cr.code == close_code::none ?
        0 : 2 + cr.reason.size();
    fh.mask = role_ == detail::role_type::client;
    if(fh.mask)
        fh.key = maskgen_();
    detail::write(db, fh);
    if(cr.code != close_code::none)
    {
        detail::prepared_key_type key;
        if(fh.mask)
            detail::prepare_key(key, fh.key);
        {
            std::uint8_t b[2];
            ::new(&b[0]) big_uint16_buf_t{
                (std::uint16_t)cr.code};
            auto d = db.prepare(2);
            boost::asio::buffer_copy(d,
                boost::asio::buffer(b));
            if(fh.mask)
                detail::mask_inplace(d, key);
            db.commit(2);
        }
        if(! cr.reason.empty())
        {
            auto d = db.prepare(cr.reason.size());
            boost::asio::buffer_copy(d,
                boost::asio::const_buffer(
                    cr.reason.data(), cr.reason.size()));
            if(fh.mask)
                detail::mask_inplace(d, key);
            db.commit(cr.reason.size());
        }
    }
}

template<class DynamicBuffer>
void
stream_base::
write_ping(
    DynamicBuffer& db, opcode op, ping_data const& data)
{
    frame_header fh;
    fh.op = op;
    fh.fin = true;
    fh.rsv1 = false;
    fh.rsv2 = false;
    fh.rsv3 = false;
    fh.len = data.size();
    fh.mask = role_ == role_type::client;
    if(fh.mask)
        fh.key = maskgen_();
    detail::write(db, fh);
    if(data.empty())
        return;
    detail::prepared_key_type key;
    if(fh.mask)
        detail::prepare_key(key, fh.key);
    auto d = db.prepare(data.size());
    boost::asio::buffer_copy(d,
        boost::asio::const_buffers_1(
            data.data(), data.size()));
    if(fh.mask)
        detail::mask_inplace(d, key);
    db.commit(data.size());
}

} // detail
} // websocket
} // beast

#endif
