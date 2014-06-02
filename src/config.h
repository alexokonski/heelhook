/* config - configuration options for heelhook
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

#ifndef __CONFIG_H_
#define __CONFIG_H_

#include <stdint.h>
#include "endpoint.h"

typedef struct
{
    /* addr to bind to, if NULL, all interfaces */
    char* bindaddr;

    /* port the server will listen on */
    uint16_t port;

    /* set to 0 for none */
    uint64_t heartbeat_interval_ms;

    /*
     * how long to wait for a heartbeat response. if this is 0 , but
     * heartbeat_interval_ms is not 0, heartbeats will be pong frames
     * and won't be checked for acks
     */
    uint64_t heartbeat_ttl_ms;

    /*
     * will close the socket if a handshake takes longer than this (slowloris
     * attack mitigation). set to 0 for none
     */
    uint64_t handshake_timeout_ms;

    /* endpoint settings */
    endpoint_settings endp_settings;

    /* max clients we allow connected */
    int max_clients;
} config_server_options;

typedef struct
{
    endpoint_settings endp_settings; /* endpoint settings */
} config_client_options;

/* Parse settings from config_str into options */
/*void config_parse_from_string(const char* config_str, config_options*
                                options);*/

#endif /* __CONFIG_H_ */

