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

#include "protocol.h"

#define CONFIG_LOG_LEVEL_DEBUG  0
#define CONFIG_LOG_LEVEL_WARN   10
#define CONFIG_LOG_LEVEL_ERROR  20

#define CONFIG_LOG_LEVEL_DEBUG_STR "DEBUG"
#define CONFIG_LOG_LEVEL_WARN_STR  "WARN"
#define CONFIG_LOG_LEVEL_ERROR_STR "ERROR"

typedef struct
{
    char* bindaddr; /* addr to bind to, if NULL, all interfaces */
    char* logfilepath; /* path to log file, NULL for stdout */
    int port; /* port the server will listen on */
    size_t protocol_buf_init_len; /* initital length for read/write buffers */
    int max_clients; /* max clients we allow connected */
    protocol_settings conn_settings; /* settings for each connection */
    int loglevel;
} config_options;

/* Parse settings from config_str into options */
void config_parse_from_string(
    const char* config_str,
    config_options* options
);

