//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_ZERROR_HPP
#define BEAST_DETAIL_ZERROR_HPP

#include <beast/core/error.hpp>

namespace beast {
namespace detail {

enum z_error
{
    end_of_stream = 1,
    invalid_block_type,
    invalid_len_code,
    invalid_dist_code,
    invalid_stored_block_lengths,
    invalid_bitlen_repeat,
    invalid_code_no_block_end,
    invalid_window_offset,
    table_oversubscribed,
    table_incomplete,
    table_overflow
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
        switch(static_cast<z_error>(ev))
        {
        case z_error::end_of_stream:
            return "end of deflate stream";

        case z_error::invalid_block_type:
            return "invalid block type";

        case z_error::invalid_len_code:
            return "invalid literal/length code";
            
        case z_error::invalid_dist_code:
            return "invalid distance code";

        case z_error::invalid_stored_block_lengths:
            return "invalid stored block lengths";

        case z_error::invalid_bitlen_repeat:
            return "invalid bit length repeat";

        case z_error::invalid_code_no_block_end:
            return "invalid code: no block end";

        case z_error::invalid_window_offset:
            return "invalid window offset";

        case z_error::table_oversubscribed:
            return "code table is oversubscribed";

        case z_error::table_incomplete:
            return "code table is incomplete";

        case z_error::table_overflow:
            return "table_overflow";

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
make_error_code(z_error ev)
{
    return error_code(static_cast<int>(ev),
        get_zcodec_error_category());
}

} // detail
} // beast

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::detail::z_error>
{
    static bool const value = true;
};
} // system
} // boost

#endif
