/* util - helpful utilities 
 * 
 * Copyright (c) 2013, Alex O'Konski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of heelhook nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "util.h"

#include <arpa/inet.h>
#include <stdio.h>

typedef enum
{
    UTIL_UNKNOWN,
    UTIL_LITTLE_ENDIAN,
    UTIL_BIG_ENDIAN
} util_endian;

typedef union
{
    uint64_t i;
    char bytes[sizeof(uint64_t)];
} util_convert;

static util_endian g_system_endian = UTIL_UNKNOWN;

static void util_endian_test(void)
{
    if (g_system_endian == UTIL_UNKNOWN)
    {
        int test = 1;
        if (*((char*)&test) == 1)
        {
            g_system_endian = UTIL_LITTLE_ENDIAN;
        }
        else
        {
            g_system_endian = UTIL_BIG_ENDIAN;
        }
    }
}

static uint64_t util_longlong_swap(uint64_t longlong)
{
    util_endian_test();

    if (g_system_endian == UTIL_BIG_ENDIAN) return longlong;

    uint64_t result = 
        ((longlong & 0x00000000000000ff) << 56) |
        ((longlong & 0x000000000000ff00) << 40) |
        ((longlong & 0x0000000000ff0000) << 24) |
        ((longlong & 0x00000000ff000000) <<  8) |
        ((longlong & 0x000000ff00000000) >>  8) |
        ((longlong & 0x0000ff0000000000) >> 24) |
        ((longlong & 0x00ff000000000000) >> 40) |
        ((longlong & 0xff00000000000000) >> 56);
     
    return result;
}

uint32_t hh_htonl(uint32_t hostlong)
{
    return htonl(hostlong);
}

uint16_t hh_htons(uint16_t hostshort)
{
    return htons(hostshort);
}

uint32_t hh_ntohl(uint32_t netlong)
{
    return ntohl(netlong);
}

uint16_t hh_ntohs(uint16_t netshort)
{
    return ntohs(netshort);
}

uint64_t hh_htonll(uint64_t hostlonglong)
{
    return util_longlong_swap(hostlonglong);
}

uint64_t hh_ntohll(uint64_t netlonglong)
{
    return util_longlong_swap(netlonglong);
}


