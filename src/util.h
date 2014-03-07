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

#ifndef __UTIL_H_
#define __UTIL_H_

#include <stdint.h>
#include <stdbool.h>

/* min/max */
#define hhmin(x, y) (((x) <= (y)) ? (x) : (y))
#define hhmax(x, y) (((x) >= (y)) ? (x) : (y))

/* countof static array */
#define hhcountof(a) (sizeof(a)/sizeof(*(a)))
#define hhunused(s) ((void)s)

/* host <--> network byte order funcs, including uint64_t */
uint32_t hh_htonl(uint32_t hostlong);
uint16_t hh_htons(uint16_t hostshort);
uint32_t hh_ntohl(uint32_t netlong);
uint16_t hh_ntohs(uint16_t netshort);
uint64_t hh_htonll(uint64_t hostlonglong);
uint64_t hh_ntohll(uint64_t netlonglong);

/* use this so we never inline in debug builds */
#ifdef DEBUG
    #define HH_INLINE
#else
    #define HH_INLINE inline
#endif

#endif /* __UTIL_H_ */
