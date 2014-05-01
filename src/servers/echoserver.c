/* echoserver - implements a server that simply echos any message sent to it
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

#include "../server.h"
#include "../hhlog.h"
#include "../util.h"

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

static server* g_serv = NULL;

static void on_message_received(server_conn* conn, endpoint_msg* msg,
                                void* userdata)
{
    hhunused(userdata);
    server_conn_send_msg(conn, msg);
}

static bool
on_open(server_conn* conn, int* subprotocol_out, int* extensions_out,
        void* userdata)
{
    hhunused(subprotocol_out);
    hhunused(extensions_out);
    hhunused(conn);
    hhunused(userdata);
    unsigned num_protocols = server_get_num_client_subprotocols(conn);
    hhlog(HHLOG_LEVEL_DEBUG, "Got subprotocols [");
    for (unsigned i = 0; i < num_protocols; i++)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "    %s",
              server_get_client_subprotocol(conn, i));
    }
    hhlog(HHLOG_LEVEL_DEBUG, "]");

    return true;
}

static void
on_close(server_conn* conn, int code, const char* reason, int reason_len,
         void* userdata)
{
    hhunused(conn);
    hhunused(code);
    hhunused(reason);
    hhunused(reason_len);
    hhunused(userdata);
    hhlog(HHLOG_LEVEL_DEBUG, "Got close: (%d, %.*s)\n", code,
          (int)reason_len, reason);
}

static void signal_handler(int sig)
{
    hhunused(sig);
    /* signal server to stop */
    if (g_serv != NULL)
    {
        server_stop(g_serv);
    }
}

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_DEBUG,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = true,
    .log_location = true
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s port\n", argv[0]);
        exit(1);
    }

    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = signal_handler;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    config_server_options options;
    options.bindaddr = NULL;
    options.max_clients = 10;

    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 20 * 1024 * 1024;
    conn_settings->read_max_msg_size = 20 * 1024 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->max_handshake_size = 2048;
    conn_settings->rand_func = NULL;
    options.port = (uint16_t)atoi(argv[1]);

    server_callbacks callbacks;
    callbacks.on_open = on_open;
    callbacks.on_message = on_message_received;
    callbacks.on_ping = NULL;
    callbacks.on_close = on_close;

    hhlog_set_options(&g_log_options);

    g_serv = server_create(&options, &callbacks, NULL);

    server_listen(g_serv);

    server_destroy(g_serv);
    g_serv = NULL;
    exit(0);
}

