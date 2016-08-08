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

#ifndef BEAST_ZLIB_IMPL_BASIC_INFLATE_STREAM_IPP
#define BEAST_ZLIB_IMPL_BASIC_INFLATE_STREAM_IPP

#include <beast/detail/zlib/inflate_stream.hpp>
#include <cassert>
#include <cstring>

namespace beast {
namespace zlib {

template<class Allocator>
basic_inflate_stream<Allocator>::
basic_inflate_stream()
{
    reset(15);
}

template<class Allocator>
basic_inflate_stream<Allocator>::
~basic_inflate_stream()
{
    std::free(this->window_);
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
reset(std::uint8_t windowBits)
{
    if(windowBits < 8 || windowBits > 15)
        throw std::domain_error("windowBits out of range");
    if(window_ && wbits_ != windowBits)
    {
        std::free(window_);
        window_ = nullptr;
    }

    // update state and reset the rest of it
    wbits_ = (unsigned)windowBits;
    wsize_ = 0;
    whave_ = 0;
    wnext_ = 0;

    resetKeep();
}

template<class Allocator>
int
basic_inflate_stream<Allocator>::
write(int flush)
{
    return write(this, flush);
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
resetKeep()
{
auto strm = this;
    strm->total_in = strm->total_out = strm->total_ = 0;
    strm->msg = 0;
    strm->mode_ = HEAD;
    strm->last_ = 0;
    strm->dmax_ = 32768U;
    strm->hold_ = 0;
    strm->bits_ = 0;
    strm->lencode_ = strm->distcode_ = strm->next_ = strm->codes_;
    strm->sane_ = 1;
    strm->back_ = -1;
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
fixedTables()
{
auto strm = this;
    auto const fc = detail::get_fixed_tables();
    strm->lencode_ = fc.lencode;
    strm->lenbits_ = fc.lenbits;
    strm->distcode_ = fc.distcode;
    strm->distbits_ = fc.distbits;
}

//------------------------------------------------------------------------------

/*
   Update the window with the last wsize (normally 32K) bytes written before
   returning.  If window does not exist yet, create it.  This is only called
   when a window is already in use, or when output has been written during this
   inflate call, but the end of the deflate stream has not been reached yet.
   It is also called to create a window for dictionary data when a dictionary
   is loaded.

   Providing output buffers larger than 32K to inflate() should provide a speed
   advantage, since only the last 32K of output is copied to the sliding window
   upon return from inflate(), and since all distances after the first 32K of
   output will fall in the output data, making match copies simpler and faster.
   The advantage may be dependent on the size of the processor's data caches.
 */
template<class Allocator>
int
basic_inflate_stream<Allocator>::
updatewindow(const Byte *end, unsigned copy)
{
    auto strm = this;
    unsigned dist;

    /* if it hasn't been done already, allocate space for the window */
    if (strm->window_ == 0) {
        strm->window_ = (unsigned char *) std::malloc(1U << strm->wbits_);
        if (strm->window_ == 0) return 1;
    }

    /* if window not in use yet, initialize */
    if (strm->wsize_ == 0) {
        strm->wsize_ = 1U << strm->wbits_;
        strm->wnext_ = 0;
        strm->whave_ = 0;
    }

    /* copy strm->wsize_ or less output bytes into the circular window */
    if (copy >= strm->wsize_) {
        std::memcpy(strm->window_, end - strm->wsize_, strm->wsize_);
        strm->wnext_ = 0;
        strm->whave_ = strm->wsize_;
    }
    else {
        dist = strm->wsize_ - strm->wnext_;
        if (dist > copy) dist = copy;
        std::memcpy(strm->window_ + strm->wnext_, end - copy, dist);
        copy -= dist;
        if (copy) {
            std::memcpy(strm->window_, end - copy, copy);
            strm->wnext_ = copy;
            strm->whave_ = strm->wsize_;
        }
        else {
            strm->wnext_ += dist;
            if (strm->wnext_ == strm->wsize_) strm->wnext_ = 0;
            if (strm->whave_ < strm->wsize_) strm->whave_ += dist;
        }
    }
    return 0;
}

/* Macros for inflate(): */

/* Clear the input bit accumulator */
#define INITBITS() \
    do { \
        strm->hold_ = 0; \
        strm->bits_ = 0; \
    } while (0)

/* Get a byte of input into the bit accumulator, or return from inflate()
   if there is no input available. */
#define PULLBYTE() \
    do { \
        if (strm->avail_in == 0) goto inf_leave; \
        strm->avail_in--; \
        auto next = reinterpret_cast<std::uint8_t const*>(strm->next_in); \
        strm->hold_ += (unsigned long)(*next++) << strm->bits_; \
        strm->next_in = next; \
        strm->bits_ += 8; \
    } while (0)

/* Assure that there are at least n bits in the bit accumulator.  If there is
   not enough available input to do that, then return from inflate(). */
#define NEEDBITS(n) \
    do { \
        while (strm->bits_ < (unsigned)(n)) \
            PULLBYTE(); \
    } while (0)

/* Return the low n bits of the bit accumulator (n < 16) */
#define BITS(n) \
    ((unsigned)strm->hold_ & ((1U << (n)) - 1))

/* Remove n bits from the bit accumulator */
#define DROPBITS(n) \
    do { \
        strm->hold_ >>= (n); \
        strm->bits_ -= (unsigned)(n); \
    } while (0)

/* Remove zero to seven bits as needed to go to a byte boundary */
#define BYTEBITS() \
    do { \
        strm->hold_ >>= strm->bits_ & 7; \
        strm->bits_ -= strm->bits_ & 7; \
    } while (0)

template<class Allocator>
int
basic_inflate_stream<Allocator>::
write(basic_inflate_stream* strm, int flush)
{
    unsigned in, out;       // save starting available input and output
    unsigned copy;          // number of stored or match bytes to copy
    unsigned char *from;    // where to copy match bytes from
    detail::code here;      // current decoding table entry
    detail::code last;      // parent table entry
    unsigned len;           // length to copy for repeats, bits to drop
    int ret;                // return code
    static unsigned short constexpr order[19] = /* permutation of code lengths */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    if (strm->next_out == 0 ||
            (strm->next_in == 0 && strm->avail_in != 0))
        return Z_STREAM_ERROR;

    if (strm->mode_ == TYPE) strm->mode_ = TYPEDO;      /* skip check */
    in = strm->avail_in;
    out = strm->avail_out;
    ret = Z_OK;
    for (;;)
        switch (strm->mode_) {
        case HEAD:
            strm->mode_ = TYPEDO;
            break;
        case TYPE:
            if (flush == Z_BLOCK || flush == Z_TREES) goto inf_leave;
        case TYPEDO:
            if (strm->last_) {
                BYTEBITS();
                strm->mode_ = CHECK;
                break;
            }
            NEEDBITS(3);
            strm->last_ = BITS(1);
            DROPBITS(1);
            switch (BITS(2)) {
            case 0:                             /* stored block */
                strm->mode_ = STORED;
                break;
            case 1:                             /* fixed block */
                strm->fixedTables();
                strm->mode_ = LEN_;             /* decode codes */
                if (flush == Z_TREES) {
                    DROPBITS(2);
                    goto inf_leave;
                }
                break;
            case 2:                             /* dynamic block */
                strm->mode_ = TABLE;
                break;
            case 3:
                strm->msg = (char *)"invalid block type";
                strm->mode_ = BAD;
            }
            DROPBITS(2);
            break;
        case STORED:
            BYTEBITS();                         /* go to byte boundary */
            NEEDBITS(32);
            if ((strm->hold_ & 0xffff) != ((strm->hold_ >> 16) ^ 0xffff)) {
                strm->msg = (char *)"invalid stored block lengths";
                strm->mode_ = BAD;
                break;
            }
            strm->length_ = (unsigned)strm->hold_ & 0xffff;
            INITBITS();
            strm->mode_ = COPY_;
            if (flush == Z_TREES) goto inf_leave;
        case COPY_:
            strm->mode_ = COPY;
        case COPY:
            copy = strm->length_;
            if (copy) {
                if (copy > strm->avail_in) copy = strm->avail_in;
                if (copy > strm->avail_out) copy = strm->avail_out;
                if (copy == 0) goto inf_leave;
                std::memcpy(strm->next_out, strm->next_in, copy);
                strm->avail_in -= copy;
                strm->next_in += copy;
                strm->avail_out -= copy;
                strm->next_out += copy;
                strm->length_ -= copy;
                break;
            }
            strm->mode_ = TYPE;
            break;
        case TABLE:
            NEEDBITS(14);
            strm->nlen_ = BITS(5) + 257;
            DROPBITS(5);
            strm->ndist_ = BITS(5) + 1;
            DROPBITS(5);
            strm->ncode_ = BITS(4) + 4;
            DROPBITS(4);
#ifndef PKZIP_BUG_WORKAROUND
            if (strm->nlen_ > 286 || strm->ndist_ > 30) {
                strm->msg = (char *)"too many length or distance symbols";
                strm->mode_ = BAD;
                break;
            }
#endif
            strm->have_ = 0;
            strm->mode_ = LENLENS;
        case LENLENS:
            while (strm->have_ < strm->ncode_) {
                NEEDBITS(3);
                strm->lens_[order[strm->have_++]] = (unsigned short)BITS(3);
                DROPBITS(3);
            }
            while (strm->have_ < 19)
                strm->lens_[order[strm->have_++]] = 0;
            strm->next_ = strm->codes_;
            strm->lencode_ = (detail::code const*)(strm->next_);
            strm->lenbits_ = 7;
            ret = inflate_table(detail::CODES, strm->lens_, 19, &(strm->next_),
                                &(strm->lenbits_), strm->work_);
            if (ret) {
                strm->msg = (char *)"invalid code lengths set";
                strm->mode_ = BAD;
                break;
            }
            strm->have_ = 0;
            strm->mode_ = CODELENS;
        case CODELENS:
            while (strm->have_ < strm->nlen_ + strm->ndist_) {
                for (;;) {
                    here = strm->lencode_[BITS(strm->lenbits_)];
                    if ((unsigned)(here.bits) <= strm->bits_) break;
                    PULLBYTE();
                }
                if (here.val < 16) {
                    DROPBITS(here.bits);
                    strm->lens_[strm->have_++] = here.val;
                }
                else {
                    if (here.val == 16) {
                        NEEDBITS(here.bits + 2);
                        DROPBITS(here.bits);
                        if (strm->have_ == 0) {
                            strm->msg = (char *)"invalid bit length repeat";
                            strm->mode_ = BAD;
                            break;
                        }
                        len = strm->lens_[strm->have_ - 1];
                        copy = 3 + BITS(2);
                        DROPBITS(2);
                    }
                    else if (here.val == 17) {
                        NEEDBITS(here.bits + 3);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 3 + BITS(3);
                        DROPBITS(3);
                    }
                    else {
                        NEEDBITS(here.bits + 7);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 11 + BITS(7);
                        DROPBITS(7);
                    }
                    if (strm->have_ + copy > strm->nlen_ + strm->ndist_) {
                        strm->msg = (char *)"invalid bit length repeat";
                        strm->mode_ = BAD;
                        break;
                    }
                    while (copy--)
                        strm->lens_[strm->have_++] = (unsigned short)len;
                }
            }

            /* handle error breaks in while */
            if (strm->mode_ == BAD) break;

            /* check for end-of-block code (better have one) */
            if (strm->lens_[256] == 0) {
                strm->msg = (char *)"invalid code -- missing end-of-block";
                strm->mode_ = BAD;
                break;
            }

            /* build code tables -- note: do not change the lenbits or distbits
               values here (9 and 6) without reading the comments in inftrees.hpp
               concerning the ENOUGH constants, which depend on those values */
            strm->next_ = strm->codes_;
            strm->lencode_ = (detail::code const*)(strm->next_);
            strm->lenbits_ = 9;
            ret = inflate_table(detail::LENS, strm->lens_, strm->nlen_, &(strm->next_),
                                &(strm->lenbits_), strm->work_);
            if (ret) {
                strm->msg = (char *)"invalid literal/lengths set";
                strm->mode_ = BAD;
                break;
            }
            strm->distcode_ = (detail::code const *)(strm->next_);
            strm->distbits_ = 6;
            ret = inflate_table(detail::DISTS, strm->lens_ + strm->nlen_, strm->ndist_,
                            &(strm->next_), &(strm->distbits_), strm->work_);
            if (ret) {
                strm->msg = (char *)"invalid distances set";
                strm->mode_ = BAD;
                break;
            }
            strm->mode_ = LEN_;
            if (flush == Z_TREES) goto inf_leave;
        case LEN_:
            strm->mode_ = LEN;
        case LEN:
            if (strm->avail_in >= 6 && strm->avail_out >= 258) {
                inflate_fast(strm, out);
                if (strm->mode_ == TYPE)
                    strm->back_ = -1;
                break;
            }
            strm->back_ = 0;
            for (;;) {
                here = strm->lencode_[BITS(strm->lenbits_)];
                if ((unsigned)(here.bits) <= strm->bits_) break;
                PULLBYTE();
            }
            if (here.op && (here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = strm->lencode_[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= strm->bits_) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                strm->back_ += last.bits;
            }
            DROPBITS(here.bits);
            strm->back_ += here.bits;
            strm->length_ = (unsigned)here.val;
            if ((int)(here.op) == 0) {
                strm->mode_ = LIT;
                break;
            }
            if (here.op & 32) {
                strm->back_ = -1;
                strm->mode_ = TYPE;
                break;
            }
            if (here.op & 64) {
                strm->msg = (char *)"invalid literal/length code";
                strm->mode_ = BAD;
                break;
            }
            strm->extra_ = (unsigned)(here.op) & 15;
            strm->mode_ = LENEXT;
        case LENEXT:
            if (strm->extra_) {
                NEEDBITS(strm->extra_);
                strm->length_ += BITS(strm->extra_);
                DROPBITS(strm->extra_);
                strm->back_ += strm->extra_;
            }
            strm->was_ = strm->length_;
            strm->mode_ = DIST;
        case DIST:
            for (;;) {
                here = strm->distcode_[BITS(strm->distbits_)];
                if ((unsigned)(here.bits) <= strm->bits_) break;
                PULLBYTE();
            }
            if ((here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = strm->distcode_[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= strm->bits_) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                strm->back_ += last.bits;
            }
            DROPBITS(here.bits);
            strm->back_ += here.bits;
            if (here.op & 64) {
                strm->msg = (char *)"invalid distance code";
                strm->mode_ = BAD;
                break;
            }
            strm->offset_ = (unsigned)here.val;
            strm->extra_ = (unsigned)(here.op) & 15;
            strm->mode_ = DISTEXT;
        case DISTEXT:
            if (strm->extra_) {
                NEEDBITS(strm->extra_);
                strm->offset_ += BITS(strm->extra_);
                DROPBITS(strm->extra_);
                strm->back_ += strm->extra_;
            }
#ifdef INFLATE_STRICT
            if (strm->offset_ > strm->dmax_) {
                strm->msg = (char *)"invalid distance too far back";
                strm->mode_ = BAD;
                break;
            }
#endif
            strm->mode_ = MATCH;
        case MATCH:
            if (strm->avail_out == 0) goto inf_leave;
            copy = out - strm->avail_out;
            if (strm->offset_ > copy) {         /* copy from window */
                copy = strm->offset_ - copy;
                if (copy > strm->whave_) {
                    if (strm->sane_) {
                        strm->msg = (char *)"invalid distance too far back";
                        strm->mode_ = BAD;
                        break;
                    }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
                    Trace((stderr, "inflate.c too far\n"));
                    copy -= strm->whave_;
                    if (copy > strm->length_) copy = strm->length_;
                    if (copy > strm->avail_out) copy = strm->avail_out;
                    strm->avail_out -= copy;
                    strm->length_ -= copy;
                    do {
                        *strm->next_out++ = 0;
                    } while (--copy);
                    if (strm->length_ == 0) strm->mode_ = LEN;
                    break;
#endif
                }
                if (copy > strm->wnext_) {
                    copy -= strm->wnext_;
                    from = strm->window_ + (strm->wsize_ - copy);
                }
                else
                    from = strm->window_ + (strm->wnext_ - copy);
                if (copy > strm->length_) copy = strm->length_;
            }
            else {                              /* copy from output */
                from = strm->next_out - strm->offset_;
                copy = strm->length_;
            }
            if (copy > strm->avail_out) copy = strm->avail_out;
            strm->avail_out -= copy;
            strm->length_ -= copy;
            do {
                *strm->next_out++ = *from++;
            } while (--copy);
            if (strm->length_ == 0) strm->mode_ = LEN;
            break;
        case LIT:
            if (strm->avail_out == 0) goto inf_leave;
            *strm->next_out++ = (unsigned char)(strm->length_);
            strm->avail_out--;
            strm->mode_ = LEN;
            break;
        case CHECK:
            strm->mode_ = DONE;
        case DONE:
            ret = Z_STREAM_END;
            goto inf_leave;
        case BAD:
            ret = Z_DATA_ERROR;
            goto inf_leave;
        case MEM:
            return Z_MEM_ERROR;
        case SYNC:
        default:
            return Z_STREAM_ERROR;
        }

    /*
       Return from inflate(), updating the total counts and the check value.
       If there was no progress during the inflate() call, return a buffer
       error.  Call updatewindow() to create and/or update the window state.
       Note: a memory error from inflate() is non-recoverable.
     */
  inf_leave:
    if (strm->wsize_ || (out != strm->avail_out && strm->mode_ < BAD &&
            (strm->mode_ < CHECK || flush != Z_FINISH)))
        if (strm->updatewindow(strm->next_out, out - strm->avail_out)) {
            strm->mode_ = MEM;
            return Z_MEM_ERROR;
        }
    in -= strm->avail_in;
    out -= strm->avail_out;
    strm->total_in += in;
    strm->total_out += out;
    strm->total_ += out;
    strm->data_type = strm->bits_ + (strm->last_ ? 64 : 0) +
                      (strm->mode_ == TYPE ? 128 : 0) +
                      (strm->mode_ == LEN_ || strm->mode_ == COPY_ ? 256 : 0);
    if (((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
        ret = Z_BUF_ERROR;
    return ret;
}

/* Allow machine dependent optimization for post-increment or pre-increment.
   Based on testing to date,
   Pre-increment preferred for:
   - PowerPC G3 (Adler)
   - MIPS R5000 (Randers-Pehrson)
   Post-increment preferred for:
   - none
   No measurable difference:
   - Pentium III (Anderson)
   - M68060 (Nikl)
 */
#ifdef POSTINC
#  define OFF 0
#  define PUP(a) *(a)++
#else
#  define OFF 1
#  define PUP(a) *++(a)
#endif

/*
   Decode literal, length, and distance codes and write out the resulting
   literal and match bytes until either not enough input or output is
   available, an end-of-block is encountered, or a data error is encountered.
   When large enough input and output buffers are supplied to inflate(), for
   example, a 16K input buffer and a 64K output buffer, more than 95% of the
   inflate execution time is spent in this routine.

   Entry assumptions:

        state->mode_ == LEN
        strm->avail_in >= 6
        strm->avail_out >= 258
        start >= strm->avail_out
        state->bits_ < 8

   On return, state->mode_ is one of:

        LEN -- ran out of enough output space or enough available input
        TYPE -- reached end of block code, inflate() to interpret next block
        BAD -- error in block data

   Notes:

    - The maximum input bits used by a length/distance pair is 15 bits for the
      length code, 5 bits for the length extra, 15 bits for the distance code,
      and 13 bits for the distance extra.  This totals 48 bits, or six bytes.
      Therefore if strm->avail_in >= 6, then there is enough input to avoid
      checking for available input while decoding.

    - The maximum bytes that a single length/distance pair can output is 258
      bytes, which is the maximum length that can be coded.  inflate_fast()
      requires strm->avail_out >= 258 for each loop to avoid checking for
      output space.
 */
template<class Allocator>
void
basic_inflate_stream<Allocator>::
inflate_fast(
    basic_inflate_stream* strm,
    unsigned start)             // inflate()'s starting value for strm->avail_out
{
    const unsigned char *in;    // local strm->next_in
    const unsigned char *last;  // have enough input while in < last
    unsigned char *out;         // local strm->next_out
    unsigned char *beg;         // inflate()'s initial strm->next_out
    unsigned char *end;         // while out < end, enough space available
#ifdef INFLATE_STRICT
    unsigned dmax;              /* maximum distance from zlib header */
#endif
    unsigned wsize;             // window size or zero if not using window
    unsigned whave;             // valid bytes in the window
    unsigned wnext;             // window write index
    unsigned char *window;      // allocated sliding window, if wsize != 0
    unsigned long hold;         // local strm->hold_
    unsigned bits;              // local strm->bits_
    detail::code const *lcode;  // local strm->lencode_
    detail::code const *dcode;  // local strm->distcode_
    unsigned lmask;             // mask for first level of length codes
    unsigned dmask;             // mask for first level of distance codes
    detail::code here;          // retrieved table entry
    unsigned op;                // code bits, operation, extra bits, or window position, window bytes to copy
    unsigned len;               // match length, unused bytes
    unsigned dist;              // match distance
    unsigned char *from;        // where to copy match from

    /* copy state to local variables */
    auto state = strm;
    in = strm->next_in - OFF;
    last = in + (strm->avail_in - 5);
    out = strm->next_out - OFF;
    beg = out - (start - strm->avail_out);
    end = out + (strm->avail_out - 257);
#ifdef INFLATE_STRICT
    dmax = state->dmax_;
#endif
    wsize = state->wsize_;
    whave = state->whave_;
    wnext = state->wnext_;
    window = state->window_;
    hold = state->hold_;
    bits = state->bits_;
    lcode = state->lencode_;
    dcode = state->distcode_;
    lmask = (1U << state->lenbits_) - 1;
    dmask = (1U << state->distbits_) - 1;

    /* decode literals and length/distances until end-of-block or not enough
       input data or output space */
    do {
        if (bits < 15) {
            hold += (unsigned long)(PUP(in)) << bits;
            bits += 8;
            hold += (unsigned long)(PUP(in)) << bits;
            bits += 8;
        }
        here = lcode[hold & lmask];
      dolen:
        op = (unsigned)(here.bits);
        hold >>= op;
        bits -= op;
        op = (unsigned)(here.op);
        if (op == 0) {                          /* literal */
            PUP(out) = (unsigned char)(here.val);
        }
        else if (op & 16) {                     /* length base */
            len = (unsigned)(here.val);
            op &= 15;                           /* number of extra bits */
            if (op) {
                if (bits < op) {
                    hold += (unsigned long)(PUP(in)) << bits;
                    bits += 8;
                }
                len += (unsigned)hold & ((1U << op) - 1);
                hold >>= op;
                bits -= op;
            }
            if (bits < 15) {
                hold += (unsigned long)(PUP(in)) << bits;
                bits += 8;
                hold += (unsigned long)(PUP(in)) << bits;
                bits += 8;
            }
            here = dcode[hold & dmask];
          dodist:
            op = (unsigned)(here.bits);
            hold >>= op;
            bits -= op;
            op = (unsigned)(here.op);
            if (op & 16) {                      /* distance base */
                dist = (unsigned)(here.val);
                op &= 15;                       /* number of extra bits */
                if (bits < op) {
                    hold += (unsigned long)(PUP(in)) << bits;
                    bits += 8;
                    if (bits < op) {
                        hold += (unsigned long)(PUP(in)) << bits;
                        bits += 8;
                    }
                }
                dist += (unsigned)hold & ((1U << op) - 1);
#ifdef INFLATE_STRICT
                if (dist > dmax) {
                    strm->msg = (char *)"invalid distance too far back";
                    state->mode_ = BAD;
                    break;
                }
#endif
                hold >>= op;
                bits -= op;
                op = (unsigned)(out - beg);     /* max distance in output */
                if (dist > op) {                /* see if copy from window */
                    op = dist - op;             /* distance back in window */
                    if (op > whave) {
                        if (state->sane_) {
                            strm->msg =
                                (char *)"invalid distance too far back";
                            state->mode_ = BAD;
                            break;
                        }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
                        if (len <= op - whave) {
                            do {
                                PUP(out) = 0;
                            } while (--len);
                            continue;
                        }
                        len -= op - whave;
                        do {
                            PUP(out) = 0;
                        } while (--op > whave);
                        if (op == 0) {
                            from = out - dist;
                            do {
                                PUP(out) = PUP(from);
                            } while (--len);
                            continue;
                        }
#endif
                    }
                    from = window - OFF;
                    if (wnext == 0) {           /* very common case */
                        from += wsize - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    else if (wnext < op) {      /* wrap around window */
                        from += wsize + wnext - op;
                        op -= wnext;
                        if (op < len) {         /* some from end of window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = window - OFF;
                            if (wnext < len) {  /* some from start of window */
                                op = wnext;
                                len -= op;
                                do {
                                    PUP(out) = PUP(from);
                                } while (--op);
                                from = out - dist;      /* rest from output */
                            }
                        }
                    }
                    else {                      /* contiguous in window */
                        from += wnext - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                PUP(out) = PUP(from);
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    while (len > 2) {
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        len -= 3;
                    }
                    if (len) {
                        PUP(out) = PUP(from);
                        if (len > 1)
                            PUP(out) = PUP(from);
                    }
                }
                else {
                    from = out - dist;          /* copy direct from output */
                    do {                        /* minimum length is three */
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        PUP(out) = PUP(from);
                        len -= 3;
                    } while (len > 2);
                    if (len) {
                        PUP(out) = PUP(from);
                        if (len > 1)
                            PUP(out) = PUP(from);
                    }
                }
            }
            else if ((op & 64) == 0) {          /* 2nd level distance code */
                here = dcode[here.val + (hold & ((1U << op) - 1))];
                goto dodist;
            }
            else {
                strm->msg = (char *)"invalid distance code";
                state->mode_ = BAD;
                break;
            }
        }
        else if ((op & 64) == 0) {              /* 2nd level length code */
            here = lcode[here.val + (hold & ((1U << op) - 1))];
            goto dolen;
        }
        else if (op & 32) {                     /* end-of-block */
            state->mode_ = TYPE;
            break;
        }
        else {
            strm->msg = (char *)"invalid literal/length code";
            state->mode_ = BAD;
            break;
        }
    } while (in < last && out < end);

    /* return unused bytes (on entry, bits < 8, so in won't go too far back) */
    len = bits >> 3;
    in -= len;
    bits -= len << 3;
    hold &= (1U << bits) - 1;

    /* update state and return */
    strm->next_in = in + OFF;
    strm->next_out = out + OFF;
    strm->avail_in = (unsigned)(in < last ? 5 + (last - in) : 5 - (in - last));
    strm->avail_out = (unsigned)(out < end ?
                                 257 + (end - out) : 257 - (out - end));
    state->hold_ = hold;
    state->bits_ = bits;
    return;
}

/*
   inflate_fast() speedups that turned out slower (on a PowerPC G3 750CXe):
   - Using bit fields for code structure
   - Different op definition to avoid & for extra bits (do & for table bits)
   - Three separate decoding do-loops for direct, window, and wnext == 0
   - Special case for distance > 1 copies to do overlapped load and store copy
   - Explicit branch predictions (based on measured branch probabilities)
   - Deferring match copy and interspersed it with decoding subsequent codes
   - Swapping literal/length else
   - Swapping window/direct else
   - Larger unrolled copy loops (three is about right)
   - Moving len -= 3 statement into middle of loop
 */

} // zlib
} // beast

#endif
