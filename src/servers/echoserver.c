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
#include "../hhclock.h"

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#ifdef HH_WITH_LIBEVENT
#include "../loop_adapters/libevent_iface.h"
#endif

static server* g_serv = NULL;
static uint64_t g_pings_received = 0;
static uint64_t last_sample_time = 0;

#define MS_PER_S 1000
#define SAMPLE_RATE 10

static uint64_t event_get_now_ms(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint64_t)(now.tv_sec * 1000 + now.tv_usec / 1000);
}

static void on_message_received(server_conn* conn, endpoint_msg* msg,
                                void* userdata)
{
    hhunused(userdata);
    /*hhlog(HHLOG_LEVEL_DEBUG, "%p got message: \"%.*s\"", conn,
          (int)msg->msg_len, msg->data);*/
    server_conn_send_msg(conn, msg);
}

static void on_ping(server_conn* conn, char* payload,
                    int payload_len, void* userdata)
{
    hhunused(payload);
    hhunused(payload_len);
    hhunused(userdata);

    g_pings_received++;

    uint64_t now = event_get_now_ms();
    if (now - last_sample_time >= (SAMPLE_RATE * MS_PER_S))
    {
        float secs_elapsed = (now - last_sample_time) / MS_PER_S;
        hhlog(HHLOG_LEVEL_INFO, "current messages per second: %f",
              ((float)g_pings_received / (float)secs_elapsed));
        g_pings_received = 0;
        last_sample_time = now;
    }

    server_conn_send_pong(conn, payload, payload_len);
}

static bool
on_connect(server_conn* conn, int* subprotocol_out, int* extensions_out,
        void* userdata)
{
    hhunused(subprotocol_out);
    hhunused(extensions_out);
    hhunused(conn);
    hhunused(userdata);
    unsigned num_protocols = server_get_num_client_subprotocols(conn);
    hhlog(HHLOG_LEVEL_DEBUG, "%p: Got subprotocols [", conn);
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
    hhlog(HHLOG_LEVEL_DEBUG, "Got close: (%d, %.*s)", code,
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

#ifdef HH_WITH_LIBEVENT
static struct event_base* g_base = NULL;

static bool attach_libevent(server* serv, iloop* loop,
                            config_server_options* options, void* userdata)
{
    hhunused(serv);
    hhunused(userdata);

    g_base = event_base_new();
    return iloop_attach_libevent(loop, g_base, options);
}
#endif

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
        fprintf(stderr,
"usage: %s port [event loop]\n"
"    event loop is one of: libevent, libuv, libev. echoserver must have been\n"
"    built with the proper event support", argv[0]);
        exit(1);
    }

    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = signal_handler;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    config_server_options options =
    {
        .bindaddr = NULL,
        .max_clients = 2001,
        .heartbeat_interval_ms = 0,
        .heartbeat_ttl_ms = 0,
        .handshake_timeout_ms = 0,
        .enable_workers = false,

        .endp_settings =
        {
            .conn_settings =
            {
                .write_max_frame_size = 20 * 1024 * 1024,
                .read_max_msg_size = 20 * 1024 * 1024,
                .read_max_num_frames = 20 * 1024 * 1024,
                .max_handshake_size = 4 * 1024,
                .init_buf_len = 4 * 1024,
                .rand_func = NULL
            }
        }
    };

    options.port = (uint16_t)atoi(argv[1]);

    server_callbacks callbacks =
    {
        .on_connect = on_connect,
        .on_open = NULL,
        .on_message = on_message_received,
        .on_ping = on_ping,
        .on_close = on_close
    };

    hhlog_set_options(&g_log_options);

    char* eventloop = NULL;
    if (argc > 2) eventloop = argv[2];

    if (eventloop == NULL)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "Starting with built-in event loop");

        g_serv = server_create(&options, &callbacks, NULL);

        /* use default event loop */
        server_listen(g_serv);
        server_destroy(g_serv);

        goto done;
    }
    else if (strcmp(eventloop, "libevent") == 0)
    {
#ifdef HH_WITH_LIBEVENT

        hhlog(HHLOG_LEVEL_DEBUG, "Starting with libevent");

        /* use libevent as event loop */
        callbacks.attach_loop = attach_libevent;

        g_serv = server_create_detached(&options, &callbacks, NULL);

        server_init_result r;
        r = server_init(g_serv);
        switch (r)
        {
        case SERVER_INIT_WORKER:
            /* block waiting for events */
            event_base_dispatch(g_base);
            server_destroy(g_serv);

            event_base_free(g_base);
            break;
        case SERVER_INIT_PARENT:
            pause();
            break;
        case SERVER_INIT_FAIL:
            break;
        }

#else
        fprintf(stderr, "you must build this file with HH_WITH_LIBEVENT\n");
#endif
        goto done;
    }
    else
    {
        fprintf(stderr, "unknown eventloop: %s. option(s) are: libevent",
                eventloop);
        goto done;
    }

done:
    g_serv = NULL;
    exit(0);
}

