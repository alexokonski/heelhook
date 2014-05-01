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
#include "../hhmemory.h"
#include "../hhlog.h"
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define RANDOM_RATIO 1.0
#define CHATTY_RATIO 0.0
#define MS_PER_S 1000
#define TIME_RANGE_S 2

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_DEBUG,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = true,
    .log_location = true
};

typedef union
{
    int fd;
    void* ptr;
} int_ptr;

static const char* g_addr = NULL;
static int g_port = 0;

static void usage(char* exec_name)
{
    printf("Usage:\n"
"%s [-a|--addr] [-p|--port] [-d|--debug] [-a|--autobahn] [-c|--chatserver]"
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
"-c, --chatserver\n"
"    Stress test the chatserver implementation found in servers/ of heelhook\n"
"    (default:on)\n"
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

static void write_random_bytes(event_loop* loop, int fd, void* data);
static void random_write_time_proc(event_loop* loop, event_time_id id,
                                   void* data)
{
    int_ptr d;
    d.ptr = data;
    int fd = d.fd;
    event_result er;
    er = event_add_io_event(loop, fd, EVENT_WRITEABLE,
                            write_random_bytes, NULL);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "add io event failed: %d", er);
        exit(1);
    }
    event_delete_time_event(loop, id);
}

static void queue_random_write(int fd, event_loop* loop)
{
    event_delete_io_event(loop, fd, EVENT_WRITEABLE);

    /* make a random write from 0 to TIME_RANGE_S seconds from now */
    int random_ms = random() % (TIME_RANGE_S * MS_PER_S);
    int_ptr data;
    data.fd = fd;
    event_add_time_event(loop, random_write_time_proc,
                         random_ms, data.ptr);
    hhlog(HHLOG_LEVEL_ERROR, "fd %d waiting for %dms", fd, random_ms);
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

static void random_client_connect(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    int result = 0;
    socklen_t sizeval = sizeof(result);
    int r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &sizeval);
    if (r < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "random: getsockopt failed: %d %s", r,
              strerror(errno));
        exit(1);
    }

    if (result != 0)
    {
        hhlog(HHLOG_LEVEL_ERROR,
              "random: non-blocking connect() failed: %d %s",
              result, strerror(result));
        exit(1);
    }

    queue_random_write(fd, loop);
}

static int random_socket_connect(const char* ip_addr, int port,
                                 event_loop* loop);

static void write_random_bytes(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    int bytes[1024 / sizeof(int)];
    for (size_t i = 0; i < hhcountof(bytes); i++)
    {
        bytes[i] = random();
    }

    int result = write(fd, bytes, sizeof(bytes));
    if (result < 0)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "random: write failed for fd: %d, err: %s",
              fd, strerror(errno));
        close(fd);
        event_delete_io_event(loop, fd, EVENT_WRITEABLE | EVENT_READABLE);
        if (random_socket_connect(g_addr, g_port, loop) < 0)
        {
            exit(1);
        }
        return;
    }
    queue_random_write(fd, loop);
}

static bool ab_on_connect(client* c, void* userdata)
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

static void ab_on_message(client* c, endpoint_msg* msg, void* userdata)
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

static void ab_on_close(client* c, int code, const char* reason,
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

static int random_socket_connect(const char* ip_addr, int port,
                                 event_loop* loop)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "failed to create socket: %s",
              strerror(errno));
        return -1;
    }

    if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "fcntl failed on socket: %s",
              strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = hh_htonl(INADDR_ANY);
    if (inet_aton(ip_addr, &addr.sin_addr) == 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "invalid bind address: %s",
              strerror(errno));
        return -1;
    }

    if (connect(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1 &&
        errno != EINPROGRESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "connect failed on socket: %s",
              strerror(errno));
        return -1;
    }

    event_result er;
    er = event_add_io_event(loop, s, EVENT_WRITEABLE,
                            random_client_connect, NULL);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d", er);
        exit(1);
    }
    return 0;
}

static void do_chatserver_test(const char* addr, int port, int num)
{
    config_client_options options;
    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 16 * 1024;
    conn_settings->read_max_msg_size = 16 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->rand_func = random_callback;

    /*client_callbacks chatty_cbs;
    chatty_cbs.on_connect = cs_chatty_on_connect;
    chatty_cbs.on_message = cs_chatty_on_message;
    chatty_cbs.on_ping = NULL;
    chatty_cbs.on_close = cs_chatty_on_close;

    static const char* extra_headers[] =
    {
        "Origin", "localhost",
        NULL
    };*/

    client* clients = hhmalloc((size_t)num * sizeof(*clients));
    event_loop* loop = event_create_loop(num + 1024);

    int num_random = RANDOM_RATIO * num;
    int chatty_start = num_random;
    if (num_random == 0)
    {
        num_random = 1;
        chatty_start = num_random;
    }

    for (int i = 0; i < num; i++)
    {
        /*client* c = &clients[i];
        client_result cr;
        event_result er;*/
        if (i < chatty_start)
        {
            if (random_socket_connect(addr, port, loop) < 0)
            {
                exit(1);
            }
        }
        else
        {
            /*cr = client_connect_raw(c, &options, &chatty_cbs, addr, port, "/",
                               "localhost:9001", "chatserver", NULL,
                               extra_headers, loop);
            if (cr != CLIENT_RESULT_SUCCESS)
            {
                hhlog(HHLOG_LEVEL_ERROR, "connect fail: %d", cr);
                exit(1);
            }
            er = event_add_io_event(loop, client_fd(c), EVENT_WRITEABLE,
                                    test_client_connect, c);*/
        }
        /*if (er != EVENT_RESULT_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "event fail: %d", er);
            exit(1);
        }*/
    }

    event_pump_events(loop, 0);
    event_destroy_loop(loop);
}

static void do_autobahn_test(const char* addr, int port, int num)
{
    config_client_options options;
    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 20 * 1024 * 1024;
    conn_settings->read_max_msg_size = 20 * 1024 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->rand_func = random_callback;

    client_callbacks callbacks;
    callbacks.on_connect = ab_on_connect;
    callbacks.on_message = ab_on_message;
    callbacks.on_ping = NULL;
    callbacks.on_close = ab_on_close;

    static const char* extra_headers[] =
    {
        "Origin", "localhost",
        NULL
    };

    client c;
    for (int i = 0; i < num; i++)
    {
        event_loop* loop = event_create_loop(1 + 1024);
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
        event_destroy_loop(loop);
    }

}

int main(int argc, char** argv)
{
    const char* addr = "127.0.0.1";
    const char* port_str = "8080";
    const char* num_str = "1";
    bool autobahn_mode = false;
    bool chatserver_mode = false;
    bool debug = false;

    static struct option long_options[] =
    {
        { "debug"     , no_argument      , NULL, 'd'},
        { "addr"      , required_argument, NULL, 'a'},
        { "port"      , required_argument, NULL, 'p'},
        { "autobahn"  , no_argument      , NULL, 'u'},
        { "chatserver", no_argument      , NULL, 'c'},
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

        case 'c':
            chatserver_mode = true;
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

    signal(SIGPIPE, SIG_IGN);

    /* convert cmd line args to int */
    int port = convert_to_int(port_str, argv[0]);
    int num = convert_to_int(num_str, argv[0]);

    g_port = port;
    g_addr = addr;

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
        hhunused(chatserver_mode);

        /* connect num clients in parallel and start sending stuff */
        do_chatserver_test(addr, port, num);
    }

    exit(0);
}
