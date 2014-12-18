/* random.c - non-cryptographic fast PRNG
 *
 * (c) 2013-2014 Nicholas J. Kain <njkain at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "nk/random.h"
#include "nk/log.h"
#include "nk/io.h"

#ifdef NK_USE_GETRANDOM_SYSCALL
#include <unistd.h>
#include <sys/syscall.h>
#include <asm-generic/unistd.h>
#include <linux/random.h>
static void nk_get_urandom(char *seed, size_t len)
{
    int err;
retry:
    //err = getrandom(seed, len, 0);
    err = syscall(__NR_getrandom, seed, len, 0);
    if (err < 0) {
        switch (errno) {
        case EINTR: goto retry; break;
        default: suicide("%s: getrandom() failed: %s",
                         __func__, strerror(errno));
        }
    }
}
#else
static void nk_get_rnd_clk(char *seed, size_t len)
{
    struct timespec ts;
    for (size_t i = 0; i < len; ++i) {
        int r = clock_gettime(CLOCK_REALTIME, &ts);
        if (r < 0)
            suicide("%s: Could not call clock_gettime(CLOCK_REALTIME): %s",
                    __func__, strerror(errno));
        char *p = (char *)&ts.tv_sec;
        char *q = (char *)&ts.tv_nsec;
        for (size_t j = 0; j < sizeof ts.tv_sec; ++j)
            seed[i] ^= p[j];
        for (size_t j = 0; j < sizeof ts.tv_nsec; ++j)
            seed[i] ^= q[j];
        // Force some scheduler jitter.
        static const struct timespec st = { .tv_sec=0, .tv_nsec=1 };
        nanosleep(&st, NULL);
    }
}

static void nk_get_urandom(char *seed, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        int r = safe_read(fd, seed, len);
        if (r < 0) {
            log_warning("%s: Could not read /dev/urandom: %s",
                        __func__, strerror(errno));
            goto fail;
        }
    } else {
        log_warning("%s: Could not open /dev/urandom: %s", __func__,
                    strerror(errno));
        goto fail;
    }
    close(fd);
    return;
  fail:
    log_warning("%s: Seeding PRNG via system clock.  May be predictable.",
                __func__);
    nk_get_rnd_clk(seed, len);
}
#endif

static void nk_get_urandom_u32(uint32_t *seed)
{
    nk_get_urandom((char *)seed, sizeof *seed);
}

static void nk_get_urandom_u64(uint64_t *seed)
{
    nk_get_urandom((char *)seed, sizeof *seed);
}

// 32-bit specific code implements the combined Tausworthe generator described
// by L'Ecuyer.  It is a fast PRNG that produces results of good quality.
//
// For more details, see
// P. L'Ecuyer, “Maximally Equidistributed Combined Tausworthe Generators”,
// Mathematics of Computation, 65, 213 (1996), 203–213.
//
// Initial seed mixing is done by using a single step of a linear
// congruential generator.  Parameters are taken from those commonly used
// in rand() implementations.
void nk_random_u32_init(struct nk_random_state_u32 *s)
{
    uint32_t seed;
    nk_get_urandom_u32(&seed);

    s->s1 = seed * 1664525u + (1013904223u|0x10);
    s->s2 = seed * 1103515245u + (12345u|0x1000);
    s->s3 = seed * 214013u + (2531011u|0x100000);
    s->s4 = seed * 2147483629u + (2147483587u|0x10000000);
}

uint32_t nk_random_u32(struct nk_random_state_u32 *s)
{
    s->s1 = ((s->s1 & 0xfffffffe) << 18) ^ (((s->s1 << 6)  ^ s->s1) >> 18);
    s->s2 = ((s->s2 & 0xfffffff8) << 2)  ^ (((s->s2 << 2)  ^ s->s2) >> 27);
    s->s3 = ((s->s3 & 0xfffffff0) << 7)  ^ (((s->s3 << 13) ^ s->s3) >> 21);
    s->s4 = ((s->s4 & 0xffffff80) << 13) ^ (((s->s4 << 3)  ^ s->s4) >> 12);
    return s->s1 ^ s->s2 ^ s->s3 ^ s->s4;
}

// 64-bit specific code implements the xorshift64* generator described by
// Vigna.  It is an extremely fast but high-quality PRNG.
//
// For more details, see
// S. Vigna, "An experimental exploration of Marsaglia's xorshift generators,
//            scrambled".
void nk_random_u64_init(struct nk_random_state_u64 *s)
{
    nk_get_urandom_u64(&s->s1);
}

uint64_t nk_random_u64(struct nk_random_state_u64 *s)
{
    s->s1 ^= s->s1 >> 12;
    s->s1 ^= s->s1 << 25;
    s->s1 ^= s->s1 >> 27;
    return s->s1 * 2685821657736338717ull;
}

