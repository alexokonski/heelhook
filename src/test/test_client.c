/* test_client - general test client, can test both client functionality and
 *               server functionality
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

#include "../client.h"
#include "../event.h"
#include "../error_code.h"
#include "../util.h"
#include "../hhassert.h"
#include "../hhlog.h"
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_INFO,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = true,
    .log_location = true
};

static void usage(char* exec_name)
{
    printf("Usage:\n"
"%s [-a|--addr] [-p|--port] [-d|--debug] [-a|--autobahn] "
"[-n|--numclients]\n\n"
"-a, --addr\n"
"    Ip address to connect to as a client (default: localhost)\n"
"-p, --port\n"
"    Port to connect to as a client (default: 8080)\n"
"-d, --debug\n"
"    Set logging to debug level (default: off)\n"
"-u, --autobahn\n"
"    Connect one client at a time to run tests against an autobahn\n"
"    'fuzzing server'. For testing client implementation correctness\n"
"    (default: off)\n"
"-n, --num\n"
"    When in autobahn mode, run this many test cases against the fuzzing\n"
"    server. When not in autobahn mode, the number of concurrent client\n"
"    connections to make for stress/load testing of a websocket echoserver\n"
"    (default: 1)\n",
     exec_name);
}

static uint32_t random_callback(protocol_conn* conn)
{
    hhunused(conn);
    return random();
}

static void teardown_client(client* c, event_loop* loop)
{
    event_delete_io_event(loop, client_fd(c),
                          EVENT_WRITEABLE | EVENT_READABLE);
    client_disconnect(c);
}

static void test_client_write(event_loop* loop, int fd, void* data);
static event_result queue_write(client* c, event_loop* loop)
{
    /* queue up writing */
    event_result er;
    er = event_add_io_event(loop, client_fd(c), EVENT_WRITEABLE,
                            test_client_write, c);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "add io event failed: %d", er);
        exit(1);
    }

    return er;
}

static void test_client_write(event_loop* loop, int fd, void* data)
{
    client* c = data;

    client_write_result r = client_write(c, fd);
    switch (r)
    {
    case CLIENT_WRITE_CONTINUE:
        return;
    case CLIENT_WRITE_DONE:
        /*
         * done writing to the client for now, don't call this again until
         * there's more data to be sent
         */
        event_delete_io_event(loop, fd, EVENT_WRITEABLE);
        return;
    case CLIENT_WRITE_ERROR:
    case CLIENT_WRITE_CLOSED:
        /* proper callback will have been called */
        return;
    }

    hhassert(0);
}

static void test_client_read(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    client* c = data;

    client_read_result r = client_read(c, fd);
    switch (r)
    {
    case CLIENT_READ_SUCCESS:
        return;
    case CLIENT_READ_SUCCESS_WROTE_DATA:
        queue_write(c, loop);
        return;
    case CLIENT_READ_ERROR:
    case CLIENT_READ_CLOSED:
        /*
         * errors will have been logged and handled properly by on_close
         * callback
         */
        event_delete_io_event(loop, fd, EVENT_READABLE);
        return;
    }

    hhassert(0);
}

static void test_client_connect(event_loop* loop, int fd, void* data)
{
    client* c = data;
    int result = 0;
    socklen_t sizeval = sizeof(result);
    int r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &sizeval);
    if (r < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "getsockopt failed: %d %s", r,
                  strerror(errno));
        teardown_client(c, loop);
        return;
    }

    if (result != 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "non-blocking connect() failed: %d %s",
                  result, strerror(result));
        teardown_client(c, loop);
        return;
    }

    event_result er;
    er = event_add_io_event(loop, fd, EVENT_READABLE,
                            test_client_read, c);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d", er);
        exit(1);
    }
    queue_write(c, loop);
}

static bool on_open(client* c, void* userdata)
{
    hhunused(userdata);
    /*event_loop* loop = userdata;*/
    hhlog(HHLOG_LEVEL_DEBUG, "connection opened: %d", client_fd(c));

    /*endpoint_msg msg;
    msg.is_text = true;
    char data[64];
    int len = snprintf(data, sizeof(data), "client %d says hello",
                       client_fd(c));
    msg.data = data;
    msg.msg_len = len;
    client_send_msg(c, &msg);
    queue_write(c, loop);*/

    return true;
}

static void on_message(client* c, endpoint_msg* msg, void* userdata)
{
    event_loop* loop = userdata;
    /*hhlog(HHLOG_LEVEL_DEBUG, "got message: \"%.*s\"", (int)msg->msg_len,
              msg->data);

    char data[64];
    int len = snprintf(data, sizeof(data), "client %d says bye-bye",
                       client_fd(c));

    client_close(c, HH_ERROR_NORMAL, data, len);*/
    client_send_msg(c, msg);
    queue_write(c, loop);
}

static void on_close(client* c, int code, const char* reason,
                     int reason_len, void* userdata)
{
    event_loop* loop = userdata;
    if (reason_len > 0)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "client %d got close: %d %.*s",
                  client_fd(c), code, reason_len, reason);
    }
    else
    {
        hhlog(HHLOG_LEVEL_DEBUG, "client %d got reason-less close: %d",
                  client_fd(c), code);
    }

    event_delete_io_event(loop, client_fd(c),
                          EVENT_WRITEABLE | EVENT_READABLE);
    client_disconnect(c);

    event_stop_loop(loop);
}

static int convert_to_int(const char* str, char* exec_name)
{
    errno = 0;
    char* result = NULL;
    int num = strtol(str, &result, 10);
    if (errno != 0 || result == NULL || (*result) != '\0')
    {
        usage(exec_name);
        exit(1);
    }

    return num;
}

static void do_autobahn_test(const char* addr, int port, int num)
{
    event_loop* loop = event_create_loop(1 + 1024);

    config_client_options options;
    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 20 * 1024 * 1024;
    conn_settings->read_max_msg_size = 20 * 1024 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->rand_func = random_callback;

    client_callbacks callbacks;
    callbacks.on_open_callback = on_open;
    callbacks.on_message_callback = on_message;
    callbacks.on_ping_callback = NULL;
    callbacks.on_close_callback = on_close;

    static const char* extra_headers[] =
    {
        "Origin", "localhost",
        NULL
    };

    client c;
    for (int i = 0; i < num; i++)
    {
        client_result cr;
        char resource[64];
        snprintf(resource, sizeof(resource),
                 "/runCase?case=%d&agent=heelhook", i+1);
        cr = client_connect_raw(&c, &options, &callbacks, addr, port,
                                resource,
                                "localhost:9001", NULL, NULL, extra_headers,
                                loop);

        if (cr != CLIENT_RESULT_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "connect fail: %d", cr);
            exit(1);
        }

        event_result er;
        er = event_add_io_event(loop, client_fd(&c), EVENT_WRITEABLE,
                                test_client_connect, &c);
        if (er != EVENT_RESULT_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "event fail: %d", er);
            exit(1);
        }

        /* block and process all connections */
        event_pump_events(loop, 0);
    }

    event_destroy_loop(loop);
}

int main(int argc, char** argv)
{
    const char* addr = "127.0.0.1";
    const char* port_str = "8080";
    const char* num_str = "1";
    bool autobahn_mode = false;
    bool debug = false;

    static struct option long_options[] =
    {
        { "debug"     , no_argument      , NULL, 'd'},
        { "addr"      , required_argument, NULL, 'a'},
        { "port"      , required_argument, NULL, 'p'},
        { "autobahn"  , no_argument      , NULL, 'u'},
        { "num"       , required_argument, NULL, 'n'},
        { NULL        , 0                , NULL,  0 }
    };

    bool error_occurred = false;
    while (1)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "uda:p:n:", long_options,
                            &option_index);
        if (c == -1)
        {
           break;
        }

        switch (c)
        {
        case 'd':
            debug = true;
            break;

        case 'a':
            addr = optarg;
            break;

        case 'p':
            port_str = optarg;
            break;

        case 'u':
            autobahn_mode = true;
            break;

        case 'n':
            num_str = optarg;
            break;

        case '?':
        default:
            error_occurred = true;
            break;
        }

        if (error_occurred)
        {
            break;
        }
    }

    if (error_occurred || optind < argc)
    {
        usage(argv[0]);
        exit(1);
    }

    /* convert cmd line args to int */
    int port = convert_to_int(port_str, argv[0]);
    int num = convert_to_int(num_str, argv[0]);

    if (debug)
    {
        g_log_options.loglevel = HHLOG_LEVEL_DEBUG;
    }

    /* initialize needed state */
    srand(time(NULL));
    hhlog_set_options(&g_log_options);

    if (autobahn_mode)
    {
        /*
         * connect one client at a time running num test cases against an 
         * autobahn fuzzing server
         */
        do_autobahn_test(addr, port, num);
    }
    else
    {
        /* connect num clients in parallel and start sending stuff */
        printf("non-autobahn not implemented yet!\n");
    }

    exit(0);
}
