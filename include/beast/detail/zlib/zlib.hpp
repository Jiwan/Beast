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

#ifndef BEAST_ZLIB_ZLIB_HPP
#define BEAST_ZLIB_ZLIB_HPP

#include <cstdint>
#include <cstdlib>

namespace beast {
namespace zlib {

#if !defined(__MACTYPES__)
typedef unsigned char  Byte;  /* 8 bits */
#endif
typedef unsigned int   uInt;  /* 16 bits or more */
typedef unsigned long  uLong; /* 32 bits or more */

/*
     The application must update next_in and avail_in when avail_in has dropped
   to zero.  It must update next_out and avail_out when avail_out has dropped
   to zero.  The application must initialize zalloc, zfree and opaque before
   calling the init function.  All other fields are set by the compression
   library and must not be updated by the application.

     The opaque value provided by the application will be passed as the first
   parameter for calls of zalloc and zfree.  This can be useful for custom
   memory management.  The compression library attaches no meaning to the
   opaque value.

     zalloc must return 0 if there is not enough memory for the object.
   If zlib is used in a multi-threaded application, zalloc and zfree must be
   thread safe.

     On 16-bit systems, the functions zalloc and zfree must be able to allocate
   exactly 65536 bytes, but will not be required to allocate more than this if
   the symbol MAXSEG_64K is defined (see zconf.hpp).  To reduce memory requirements
   and avoid any allocation of 64K objects, at the expense of compression ratio,
   compile the library with -DMAX_WBITS=14 (see zconf.hpp).

     The fields total_in and total_out can be used for statistics or progress
   reports.  After compression, total_in holds the total size of the
   uncompressed data and may be saved for use in the decompressor (particularly
   if the decompressor wants to decompress everything in a single step).
*/
struct z_stream
{
    Byte const*     next_in;    // next input byte
    std::size_t     avail_in;   // number of bytes available at next_in
    std::size_t     total_in;   // total number of input bytes read so far

    Byte*           next_out;   // next output byte should be put there
    std::size_t     avail_out;  // remaining free space at next_out
    std::size_t     total_out;  // total number of bytes output so far

    const char *msg = nullptr;  // last error message, NULL if no error

    int     data_type;          // best guess about the data type: binary or text
};


/* constants */

/* Allowed flush values; see deflate() and inflate() below for details */
enum z_Flush
{
    Z_NO_FLUSH      = 0,
    Z_PARTIAL_FLUSH = 1,
    Z_SYNC_FLUSH    = 2,
    Z_FULL_FLUSH    = 3,
    Z_FINISH        = 4,
    Z_BLOCK         = 5,
    Z_TREES         = 6
};

/* Return codes for the compression/decompression functions. Negative values
 * are errors, positive values are used for special but normal events.
 */
enum z_Result
{
    Z_OK            =  0,
    Z_STREAM_END    =  1,
    Z_NEED_DICT     =  2,
    Z_ERRNO         = -1,
    Z_STREAM_ERROR  = -2,
    Z_DATA_ERROR    = -3,
    Z_MEM_ERROR     = -4,
    Z_BUF_ERROR     = -5,
    Z_VERSION_ERROR = -6
};

/* compression levels */
enum z_Compression
{
    Z_NO_COMPRESSION        =  0,
    Z_BEST_SPEED            =  1,
    Z_BEST_COMPRESSION      =  9,
    Z_DEFAULT_COMPRESSION   = -1
};

/* compression strategy; see deflateInit2() below for details */
enum z_Strategy
{
    Z_FILTERED          = 1,
    Z_HUFFMAN_ONLY      = 2,
    Z_RLE               = 3,
    Z_FIXED             = 4,
    Z_DEFAULT_STRATEGY  = 0
};

/* Possible values of the data_type field (though see inflate()) */
enum z_Type
{
    Z_BINARY    = 0,
    Z_TEXT      = 1,
    Z_UNKNOWN   = 2
};

std::uint8_t constexpr Z_DEFLATED = 8; // VFALCO Useless, remove

} // zlib
} // beast

#endif

