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

#include "zutil.hpp"

#include <beast/core/detail/zlib/inflate_stream.hpp>
#include <cassert>
#include <cstring>

namespace beast {

inflate_stream::inflate_stream()
{
    reset(15);
}

inflate_stream::~inflate_stream()
{
    std::free(this->window);
}

void
inflate_stream::reset(std::uint8_t windowBits)
{
    if(windowBits < 8 || windowBits > 15)
        throw std::domain_error("windowBits out of range");
    if(window && wbits != windowBits)
    {
        std::free(window);
        window = nullptr;
    }

    // update state and reset the rest of it
    wbits = (unsigned)windowBits;
    wsize = 0;
    whave = 0;
    wnext = 0;

    resetKeep();
}

int
inflate_stream::write(int flush)
{
    return write(this, flush);
}

void
inflate_stream::resetKeep()
{
auto strm = this;
    strm->total_in = strm->total_out = strm->total = 0;
    strm->msg = Z_NULL;
    strm->mode = HEAD;
    strm->last = 0;
    strm->dmax = 32768U;
    strm->hold = 0;
    strm->bits = 0;
    strm->lencode = strm->distcode = strm->next = strm->codes;
    strm->sane = 1;
    strm->back = -1;
}

void
inflate_stream::fixedTables()
{
auto strm = this;
    auto const fc = get_fixed_tables();
    strm->lencode = fc.lencode;
    strm->lenbits = fc.lenbits;
    strm->distcode = fc.distcode;
    strm->distbits = fc.distbits;
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
int inflate_stream::updatewindow(const Byte *end, unsigned copy)
{
    auto strm = this;
    unsigned dist;

    /* if it hasn't been done already, allocate space for the window */
    if (strm->window == Z_NULL) {
        strm->window = (unsigned char *) std::malloc(1U << strm->wbits);
        if (strm->window == Z_NULL) return 1;
    }

    /* if window not in use yet, initialize */
    if (strm->wsize == 0) {
        strm->wsize = 1U << strm->wbits;
        strm->wnext = 0;
        strm->whave = 0;
    }

    /* copy strm->wsize or less output bytes into the circular window */
    if (copy >= strm->wsize) {
        std::memcpy(strm->window, end - strm->wsize, strm->wsize);
        strm->wnext = 0;
        strm->whave = strm->wsize;
    }
    else {
        dist = strm->wsize - strm->wnext;
        if (dist > copy) dist = copy;
        std::memcpy(strm->window + strm->wnext, end - copy, dist);
        copy -= dist;
        if (copy) {
            std::memcpy(strm->window, end - copy, copy);
            strm->wnext = copy;
            strm->whave = strm->wsize;
        }
        else {
            strm->wnext += dist;
            if (strm->wnext == strm->wsize) strm->wnext = 0;
            if (strm->whave < strm->wsize) strm->whave += dist;
        }
    }
    return 0;
}

/* Macros for inflate(): */

/* Clear the input bit accumulator */
#define INITBITS() \
    do { \
        strm->hold = 0; \
        strm->bits = 0; \
    } while (0)

/* Get a byte of input into the bit accumulator, or return from inflate()
   if there is no input available. */
#define PULLBYTE() \
    do { \
        if (strm->avail_in == 0) goto inf_leave; \
        strm->avail_in--; \
        auto next = reinterpret_cast<std::uint8_t const*>(strm->next_in); \
        strm->hold += (unsigned long)(*next++) << strm->bits; \
        strm->next_in = next; \
        strm->bits += 8; \
    } while (0)

/* Assure that there are at least n bits in the bit accumulator.  If there is
   not enough available input to do that, then return from inflate(). */
#define NEEDBITS(n) \
    do { \
        while (strm->bits < (unsigned)(n)) \
            PULLBYTE(); \
    } while (0)

/* Return the low n bits of the bit accumulator (n < 16) */
#define BITS(n) \
    ((unsigned)strm->hold & ((1U << (n)) - 1))

/* Remove n bits from the bit accumulator */
#define DROPBITS(n) \
    do { \
        strm->hold >>= (n); \
        strm->bits -= (unsigned)(n); \
    } while (0)

/* Remove zero to seven bits as needed to go to a byte boundary */
#define BYTEBITS() \
    do { \
        strm->hold >>= strm->bits & 7; \
        strm->bits -= strm->bits & 7; \
    } while (0)

int
inflate_stream::write(inflate_stream* strm, int flush)
{
    unsigned in, out;           /* save starting available input and output */
    unsigned copy;              /* number of stored or match bytes to copy */
    unsigned char *from;    /* where to copy match bytes from */
    code here;                  /* current decoding table entry */
    code last;                  /* parent table entry */
    unsigned len;               /* length to copy for repeats, bits to drop */
    int ret;                    /* return code */
    static const unsigned short order[19] = /* permutation of code lengths */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    if (strm->next_out == Z_NULL ||
            (strm->next_in == Z_NULL && strm->avail_in != 0))
        return Z_STREAM_ERROR;

    if (strm->mode == TYPE) strm->mode = TYPEDO;      /* skip check */
    in = strm->avail_in;
    out = strm->avail_out;
    ret = Z_OK;
    for (;;)
        switch (strm->mode) {
        case HEAD:
            strm->mode = TYPEDO;
            break;
        case TYPE:
            if (flush == Z_BLOCK || flush == Z_TREES) goto inf_leave;
        case TYPEDO:
            if (strm->last) {
                BYTEBITS();
                strm->mode = CHECK;
                break;
            }
            NEEDBITS(3);
            strm->last = BITS(1);
            DROPBITS(1);
            switch (BITS(2)) {
            case 0:                             /* stored block */
                Tracev((stderr, "inflate:     stored block%s\n",
                        strm->last ? " (last)" : ""));
                strm->mode = STORED;
                break;
            case 1:                             /* fixed block */
                strm->fixedTables();
                Tracev((stderr, "inflate:     fixed codes block%s\n",
                        strm->last ? " (last)" : ""));
                strm->mode = LEN_;             /* decode codes */
                if (flush == Z_TREES) {
                    DROPBITS(2);
                    goto inf_leave;
                }
                break;
            case 2:                             /* dynamic block */
                Tracev((stderr, "inflate:     dynamic codes block%s\n",
                        strm->last ? " (last)" : ""));
                strm->mode = TABLE;
                break;
            case 3:
                strm->msg = (char *)"invalid block type";
                strm->mode = BAD;
            }
            DROPBITS(2);
            break;
        case STORED:
            BYTEBITS();                         /* go to byte boundary */
            NEEDBITS(32);
            if ((strm->hold & 0xffff) != ((strm->hold >> 16) ^ 0xffff)) {
                strm->msg = (char *)"invalid stored block lengths";
                strm->mode = BAD;
                break;
            }
            strm->length = (unsigned)strm->hold & 0xffff;
            Tracev((stderr, "inflate:       stored length %u\n",
                    strm->length));
            INITBITS();
            strm->mode = COPY_;
            if (flush == Z_TREES) goto inf_leave;
        case COPY_:
            strm->mode = COPY;
        case COPY:
            copy = strm->length;
            if (copy) {
                if (copy > strm->avail_in) copy = strm->avail_in;
                if (copy > strm->avail_out) copy = strm->avail_out;
                if (copy == 0) goto inf_leave;
                std::memcpy(strm->next_out, strm->next_in, copy);
                strm->avail_in -= copy;
                strm->next_in += copy;
                strm->avail_out -= copy;
                strm->next_out += copy;
                strm->length -= copy;
                break;
            }
            Tracev((stderr, "inflate:       stored end\n"));
            strm->mode = TYPE;
            break;
        case TABLE:
            NEEDBITS(14);
            strm->nlen = BITS(5) + 257;
            DROPBITS(5);
            strm->ndist = BITS(5) + 1;
            DROPBITS(5);
            strm->ncode = BITS(4) + 4;
            DROPBITS(4);
#ifndef PKZIP_BUG_WORKAROUND
            if (strm->nlen > 286 || strm->ndist > 30) {
                strm->msg = (char *)"too many length or distance symbols";
                strm->mode = BAD;
                break;
            }
#endif
            Tracev((stderr, "inflate:       table sizes ok\n"));
            strm->have = 0;
            strm->mode = LENLENS;
        case LENLENS:
            while (strm->have < strm->ncode) {
                NEEDBITS(3);
                strm->lens[order[strm->have++]] = (unsigned short)BITS(3);
                DROPBITS(3);
            }
            while (strm->have < 19)
                strm->lens[order[strm->have++]] = 0;
            strm->next = strm->codes;
            strm->lencode = (const code *)(strm->next);
            strm->lenbits = 7;
            ret = inflate_table(CODES, strm->lens, 19, &(strm->next),
                                &(strm->lenbits), strm->work);
            if (ret) {
                strm->msg = (char *)"invalid code lengths set";
                strm->mode = BAD;
                break;
            }
            Tracev((stderr, "inflate:       code lengths ok\n"));
            strm->have = 0;
            strm->mode = CODELENS;
        case CODELENS:
            while (strm->have < strm->nlen + strm->ndist) {
                for (;;) {
                    here = strm->lencode[BITS(strm->lenbits)];
                    if ((unsigned)(here.bits) <= strm->bits) break;
                    PULLBYTE();
                }
                if (here.val < 16) {
                    DROPBITS(here.bits);
                    strm->lens[strm->have++] = here.val;
                }
                else {
                    if (here.val == 16) {
                        NEEDBITS(here.bits + 2);
                        DROPBITS(here.bits);
                        if (strm->have == 0) {
                            strm->msg = (char *)"invalid bit length repeat";
                            strm->mode = BAD;
                            break;
                        }
                        len = strm->lens[strm->have - 1];
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
                    if (strm->have + copy > strm->nlen + strm->ndist) {
                        strm->msg = (char *)"invalid bit length repeat";
                        strm->mode = BAD;
                        break;
                    }
                    while (copy--)
                        strm->lens[strm->have++] = (unsigned short)len;
                }
            }

            /* handle error breaks in while */
            if (strm->mode == BAD) break;

            /* check for end-of-block code (better have one) */
            if (strm->lens[256] == 0) {
                strm->msg = (char *)"invalid code -- missing end-of-block";
                strm->mode = BAD;
                break;
            }

            /* build code tables -- note: do not change the lenbits or distbits
               values here (9 and 6) without reading the comments in inftrees.hpp
               concerning the ENOUGH constants, which depend on those values */
            strm->next = strm->codes;
            strm->lencode = (const code *)(strm->next);
            strm->lenbits = 9;
            ret = inflate_table(LENS, strm->lens, strm->nlen, &(strm->next),
                                &(strm->lenbits), strm->work);
            if (ret) {
                strm->msg = (char *)"invalid literal/lengths set";
                strm->mode = BAD;
                break;
            }
            strm->distcode = (const code *)(strm->next);
            strm->distbits = 6;
            ret = inflate_table(DISTS, strm->lens + strm->nlen, strm->ndist,
                            &(strm->next), &(strm->distbits), strm->work);
            if (ret) {
                strm->msg = (char *)"invalid distances set";
                strm->mode = BAD;
                break;
            }
            Tracev((stderr, "inflate:       codes ok\n"));
            strm->mode = LEN_;
            if (flush == Z_TREES) goto inf_leave;
        case LEN_:
            strm->mode = LEN;
        case LEN:
            if (strm->avail_in >= 6 && strm->avail_out >= 258) {
                inflate_fast(strm, out);
                if (strm->mode == TYPE)
                    strm->back = -1;
                break;
            }
            strm->back = 0;
            for (;;) {
                here = strm->lencode[BITS(strm->lenbits)];
                if ((unsigned)(here.bits) <= strm->bits) break;
                PULLBYTE();
            }
            if (here.op && (here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = strm->lencode[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= strm->bits) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                strm->back += last.bits;
            }
            DROPBITS(here.bits);
            strm->back += here.bits;
            strm->length = (unsigned)here.val;
            if ((int)(here.op) == 0) {
                Tracevv((stderr, here.val >= 0x20 && here.val < 0x7f ?
                        "inflate:         literal '%c'\n" :
                        "inflate:         literal 0x%02x\n", here.val));
                strm->mode = LIT;
                break;
            }
            if (here.op & 32) {
                Tracevv((stderr, "inflate:         end of block\n"));
                strm->back = -1;
                strm->mode = TYPE;
                break;
            }
            if (here.op & 64) {
                strm->msg = (char *)"invalid literal/length code";
                strm->mode = BAD;
                break;
            }
            strm->extra = (unsigned)(here.op) & 15;
            strm->mode = LENEXT;
        case LENEXT:
            if (strm->extra) {
                NEEDBITS(strm->extra);
                strm->length += BITS(strm->extra);
                DROPBITS(strm->extra);
                strm->back += strm->extra;
            }
            Tracevv((stderr, "inflate:         length %u\n", strm->length));
            strm->was = strm->length;
            strm->mode = DIST;
        case DIST:
            for (;;) {
                here = strm->distcode[BITS(strm->distbits)];
                if ((unsigned)(here.bits) <= strm->bits) break;
                PULLBYTE();
            }
            if ((here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = strm->distcode[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= strm->bits) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                strm->back += last.bits;
            }
            DROPBITS(here.bits);
            strm->back += here.bits;
            if (here.op & 64) {
                strm->msg = (char *)"invalid distance code";
                strm->mode = BAD;
                break;
            }
            strm->offset = (unsigned)here.val;
            strm->extra = (unsigned)(here.op) & 15;
            strm->mode = DISTEXT;
        case DISTEXT:
            if (strm->extra) {
                NEEDBITS(strm->extra);
                strm->offset += BITS(strm->extra);
                DROPBITS(strm->extra);
                strm->back += strm->extra;
            }
#ifdef INFLATE_STRICT
            if (strm->offset > strm->dmax) {
                strm->msg = (char *)"invalid distance too far back";
                strm->mode = BAD;
                break;
            }
#endif
            Tracevv((stderr, "inflate:         distance %u\n", strm->offset));
            strm->mode = MATCH;
        case MATCH:
            if (strm->avail_out == 0) goto inf_leave;
            copy = out - strm->avail_out;
            if (strm->offset > copy) {         /* copy from window */
                copy = strm->offset - copy;
                if (copy > strm->whave) {
                    if (strm->sane) {
                        strm->msg = (char *)"invalid distance too far back";
                        strm->mode = BAD;
                        break;
                    }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
                    Trace((stderr, "inflate.c too far\n"));
                    copy -= strm->whave;
                    if (copy > strm->length) copy = strm->length;
                    if (copy > strm->avail_out) copy = strm->avail_out;
                    strm->avail_out -= copy;
                    strm->length -= copy;
                    do {
                        *strm->next_out++ = 0;
                    } while (--copy);
                    if (strm->length == 0) strm->mode = LEN;
                    break;
#endif
                }
                if (copy > strm->wnext) {
                    copy -= strm->wnext;
                    from = strm->window + (strm->wsize - copy);
                }
                else
                    from = strm->window + (strm->wnext - copy);
                if (copy > strm->length) copy = strm->length;
            }
            else {                              /* copy from output */
                from = strm->next_out - strm->offset;
                copy = strm->length;
            }
            if (copy > strm->avail_out) copy = strm->avail_out;
            strm->avail_out -= copy;
            strm->length -= copy;
            do {
                *strm->next_out++ = *from++;
            } while (--copy);
            if (strm->length == 0) strm->mode = LEN;
            break;
        case LIT:
            if (strm->avail_out == 0) goto inf_leave;
            *strm->next_out++ = (unsigned char)(strm->length);
            strm->avail_out--;
            strm->mode = LEN;
            break;
        case CHECK:
            strm->mode = DONE;
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
    if (strm->wsize || (out != strm->avail_out && strm->mode < BAD &&
            (strm->mode < CHECK || flush != Z_FINISH)))
        if (strm->updatewindow(strm->next_out, out - strm->avail_out)) {
            strm->mode = MEM;
            return Z_MEM_ERROR;
        }
    in -= strm->avail_in;
    out -= strm->avail_out;
    strm->total_in += in;
    strm->total_out += out;
    strm->total += out;
    strm->data_type = strm->bits + (strm->last ? 64 : 0) +
                      (strm->mode == TYPE ? 128 : 0) +
                      (strm->mode == LEN_ || strm->mode == COPY_ ? 256 : 0);
    if (((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
        ret = Z_BUF_ERROR;
    return ret;
}

} // beast
