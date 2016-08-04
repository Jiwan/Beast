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

#ifndef BEAST_ZLIB_INFLATE_STREAM_HPP
#define BEAST_ZLIB_INFLATE_STREAM_HPP

#include <beast/detail/zlib/zlib.hpp>
#include <beast/detail/zlib/detail/inflate_tables.hpp>
#include <beast/core/error.hpp>
#include <cstdint>
#include <cstdlib>

namespace beast {

/** Raw deflate decompressor.

    This is a port of zlib's "inflate" functionality to C++.
*/
class inflate_stream : public z_stream
{
public:
    struct params
    {
        void const* next_in;        // next input byte
        std::size_t avail_in;       // number of bytes available at next_in
        std::size_t total_in = 0;   // total number of input bytes read so far

        void*       next_out;       // next output byte should be put there
        std::size_t avail_out;      // remaining free space at next_out
        std::size_t total_out = 0;  // total number of bytes output so far
    };

    /** Construct a raw deflate decompression stream.

        The window size is set to the default of 15 bits.
    */
    inflate_stream();

    /// Destructor.
    ~inflate_stream();

    /** Reset the stream.

        This puts the stream in a newly constructed state with the
        specified window size, but without de-allocating any dynamically
        created structures.
    */
    void
    reset(std::uint8_t windowBits);

    /** Write compressed data to the stream.

        @return `true` if the end of stream is reached.
    */
    bool
    write(params& ps, error_code& ec);

    int
    write(int flush);

private:
    enum inflate_mode
    {
        HEAD,       // i: waiting for magic header
        FLAGS,      // i: waiting for method and flags (gzip)
        TIME,       // i: waiting for modification time (gzip)
        OS,         // i: waiting for extra flags and operating system (gzip)
        EXLEN,      // i: waiting for extra length (gzip)
        EXTRA,      // i: waiting for extra bytes (gzip)
        NAME,       // i: waiting for end of file name (gzip)
        COMMENT,    // i: waiting for end of comment (gzip)
        HCRC,       // i: waiting for header crc (gzip)
        TYPE,       // i: waiting for type bits, including last-flag bit
        TYPEDO,     // i: same, but skip check to exit inflate on new block
        STORED,     // i: waiting for stored size (length and complement)
        COPY_,      // i/o: same as COPY below, but only first time in
        COPY,       // i/o: waiting for input or output to copy stored block
        TABLE,      // i: waiting for dynamic block table lengths
        LENLENS,    // i: waiting for code length code lengths
        CODELENS,   // i: waiting for length/lit and distance code lengths
            LEN_,   // i: same as LEN below, but only first time in
            LEN,    // i: waiting for length/lit/eob code
            LENEXT, // i: waiting for length extra bits
            DIST,   // i: waiting for distance code
            DISTEXT,// i: waiting for distance extra bits
            MATCH,  // o: waiting for output space to copy string
            LIT,    // o: waiting for output space to write literal
        CHECK,      // i: waiting for 32-bit check value
        LENGTH,     // i: waiting for 32-bit length (gzip)
        DONE,       // finished check, done -- remain here until reset
        BAD,        // got a data error -- remain here until reset
        MEM,        // got an inflate() memory error -- remain here until reset
        SYNC        // looking for synchronization bytes to restart inflate()
    };

    inflate_mode mode;          // current inflate mode
    int last;                   // true if processing last block
    unsigned dmax;              // zlib header max distance (INFLATE_STRICT)
    unsigned long total;        // protected copy of output count

    // sliding window
    unsigned wbits;             // log base 2 of requested window size
    unsigned wsize;             // window size or zero if not using window
    unsigned whave;             // valid bytes in the window
    unsigned wnext;             // window write index
    unsigned char *window =
        nullptr;                // allocated sliding window, if needed

    // bit accumulator
    unsigned long hold;         // input bit accumulator
    unsigned bits;              // number of bits in "in"

    // for string and stored block copying
    unsigned length;            // literal or length of data to copy
    unsigned offset;            // distance back to copy string from

    // for table and code decoding
    unsigned extra;             // extra bits needed
    
    // fixed and dynamic code tables
    code const *lencode;        // starting table for length/literal codes
    code const *distcode;       // starting table for distance codes
    unsigned lenbits;           // index bits for lencode
    unsigned distbits;          // index bits for distcode

    // dynamic table building
    unsigned ncode;             // number of code length code lengths
    unsigned nlen;              // number of length code lengths
    unsigned ndist;             // number of distance code lengths
    unsigned have;              // number of code lengths in lens[]
    code *next;                 // next available space in codes[]
    unsigned short lens[320];   // temporary storage for code lengths
    unsigned short work[288];   // work area for code table building
    code codes[ENOUGH];         // space for code tables
    int sane;                   // if false, allow invalid distance too far
    int back;                   // bits back of last unprocessed length/lit
    unsigned was;               // initial length of match

private:
    static
    void
    inflate_fast(inflate_stream* strm, unsigned start);

    static
    int
    write(inflate_stream* strm, int flush);

    void
    resetKeep();

    void
    fixedTables();

    int
    updatewindow(const Byte *end, unsigned copy);
};

} // beast

#endif
