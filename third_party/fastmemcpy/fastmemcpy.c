/*
   (c) Copyright 2000-2002  convergence integrated media GmbH.
   (c) Copyright 2002       convergence GmbH.

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de> and
              Sven Neumann <sven@convergence.de>.
              Silvano Galliani aka kysucix <kysucix@dyne.org> sse2 version

   Fast memcpy code was taken from xine (see below).

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

/*
 * Copyright (C) 2001 the xine project
 *
 * This file is part of xine, a unix video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * These are the MMX/MMX2/SSE optimized versions of memcpy
 *
 * This code was adapted from Linux Kernel sources by Nick Kurshev to
 * the mplayer program. (http://mplayer.sourceforge.net)
 *
 * Miguel Freitas split the #ifdefs into several specialized functions that
 * are benchmarked at runtime by xine. Some original comments from Nick
 * have been preserved documenting some MMX/SSE oddities.
 * Also added kernel memcpy function that seems faster than glibc one.
 *
 */

/* Original comments from mplayer (file: aclib.c) This part of code
   was taken by me from Linux-2.4.3 and slightly modified for MMX, MMX2,
   SSE instruction set. I have done it since linux uses page aligned
   blocks but mplayer uses weakly ordered data and original sources can
   not speedup them. Only using PREFETCHNTA and MOVNTQ together have
   effect!

   From IA-32 Intel Architecture Software Developer's Manual Volume 1,

  Order Number 245470:
  "10.4.6. Cacheability Control, Prefetch, and Memory Ordering Instructions"

  Data referenced by a program can be temporal (data will be used
  again) or non-temporal (data will be referenced once and not reused
  in the immediate future). To make efficient use of the processor's
  caches, it is generally desirable to cache temporal data and not
  cache non-temporal data. Overloading the processor's caches with
  non-temporal data is sometimes referred to as "polluting the
  caches".  The non-temporal data is written to memory with
  Write-Combining semantics.

  The PREFETCHh instructions permits a program to load data into the
  processor at a suggested cache level, so that it is closer to the
  processors load and store unit when it is needed. If the data is
  already present in a level of the cache hierarchy that is closer to
  the processor, the PREFETCHh instruction will not result in any data
  movement.  But we should you PREFETCHNTA: Non-temporal data fetch
  data into location close to the processor, minimizing cache
  pollution.

  The MOVNTQ (store quadword using non-temporal hint) instruction
  stores packed integer data from an MMX register to memory, using a
  non-temporal hint.  The MOVNTPS (store packed single-precision
  floating-point values using non-temporal hint) instruction stores
  packed floating-point data from an XMM register to memory, using a
  non-temporal hint.

  The SFENCE (Store Fence) instruction controls write ordering by
  creating a fence for memory store operations. This instruction
  guarantees that the results of every store instruction that precedes
  the store fence in program order is globally visible before any
  store instruction that follows the fence. The SFENCE instruction
  provides an efficient way of ensuring ordering between procedures
  that produce weakly-ordered data and procedures that consume that
  data.

  If you have questions please contact with me: Nick Kurshev:
  nickols_k@mail.ru.
*/

/*  mmx v.1 Note: Since we added alignment of destinition it speedups
    of memory copying on PentMMX, Celeron-1 and P2 upto 12% versus
    standard (non MMX-optimized) version.
    Note: on K6-2+ it speedups memory copying upto 25% and
          on K7 and P3 about 500% (5 times).
*/

/* Additional notes on gcc assembly and processors: [MF]
   prefetch is specific for AMD processors, the intel ones should be
   prefetch0, prefetch1, prefetch2 which are not recognized by my gcc.
   prefetchnta is supported both on athlon and pentium 3.

   therefore i will take off prefetchnta instructions from the mmx1
   version to avoid problems on pentium mmx and k6-2.

   quote of the day:
    "Using prefetches efficiently is more of an art than a science"
*/

#include <stddef.h>

/* for small memory blocks (<256 bytes) this version is faster */
#define small_memcpy(to, from, n)                               \
  {                                                             \
    register unsigned long int dummy;                           \
    __asm__ __volatile__("rep; movsb"                           \
                         : "=&D"(to), "=&S"(from), "=&c"(dummy) \
                         : "0"(to), "1"(from), "2"(n)           \
                         : "memory");                           \
  }

/* linux kernel __memcpy (from: /include/asm/string.h) */
static inline void* __memcpy(void* to, const void* from, size_t n) {
  int d0, d1, d2;

  if (n < 4) {
    small_memcpy(to, from, n);
  } else
    __asm__ __volatile__(
        "rep ; movsl\n\t"
        "testb $2,%b4\n\t"
        "je 1f\n\t"
        "movsw\n"
        "1:\ttestb $1,%b4\n\t"
        "je 2f\n\t"
        "movsb\n"
        "2:"
        : "=&c"(d0), "=&D"(d1), "=&S"(d2)
        : "0"(n / 4), "q"(n), "1"((long)to), "2"((long)from)
        : "memory");

  return (to);
}

#define MMX_MMREG_SIZE 8

#define MMX1_MIN_LEN 0x800 /* 2K blocks */

void* mmx_memcpy(void* to, const void* from, size_t len) {
  void* retval;
  size_t i;
  retval = to;

  if (len >= MMX1_MIN_LEN) {
    register unsigned long int delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((unsigned long int)to) & (MMX_MMREG_SIZE - 1);
    if (delta) {
      delta = MMX_MMREG_SIZE - delta;
      len -= delta;
      small_memcpy(to, from, delta);
    }
    i = len >> 6; /* len/64 */
    len &= 63;
    for (; i > 0; i--) {
      __asm__ __volatile__(
          "movq (%0), %%mm0\n"
          "movq 8(%0), %%mm1\n"
          "movq 16(%0), %%mm2\n"
          "movq 24(%0), %%mm3\n"
          "movq 32(%0), %%mm4\n"
          "movq 40(%0), %%mm5\n"
          "movq 48(%0), %%mm6\n"
          "movq 56(%0), %%mm7\n"
          "movq %%mm0, (%1)\n"
          "movq %%mm1, 8(%1)\n"
          "movq %%mm2, 16(%1)\n"
          "movq %%mm3, 24(%1)\n"
          "movq %%mm4, 32(%1)\n"
          "movq %%mm5, 40(%1)\n"
          "movq %%mm6, 48(%1)\n"
          "movq %%mm7, 56(%1)\n" ::"r"(from),
          "r"(to)
          : "memory");
      from = ((const unsigned char*)from) + 64;
      to = ((unsigned char*)to) + 64;
    }
    __asm__ __volatile__("emms" ::: "memory");
  }
  /*
   * Now do the tail of the block
   */
  if (len) __memcpy(to, from, len);
  return retval;
}
