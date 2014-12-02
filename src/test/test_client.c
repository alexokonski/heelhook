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
#define TIME_RANGE_S 10

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_INFO,
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
static int g_mps = 0;
static char* g_body = NULL;
static unsigned int g_sleep_ms = 0;

static void usage(char* exec_name)
{
    printf("Usage:\n"
"%s [-a|--addr] [-p|--port] [-d|--debug] [-a|--autobahn] [-c|--chatserver]"
"[-n|--numclients]\n\n"
"-a, --addr\n"
"    Ip address to connect to as a client (default: localhost)\n"
"-p, --port\n"
"    Port to connect to as a client (default: 8080)\n"
"-r, --resource\n"
"    Resource to request from the server (default: \"/\")\n"
"-d, --debug\n"
"    Set logging to debug level (default: off)\n"
"-u, --autobahn\n"
"    Connect one client at a time to run tests against an autobahn\n"
"    'fuzzing server'. For testing client implementation correctness\n"
"    (default: off)\n"
"-c, --chatserver\n"
"    Stress test the chatserver implementation found in servers/ of heelhook\n"
"    (default:off)\n"
"-m <one_client_mps>, --mps_bench <one_client_mps>\n"
"    Simple 'message per second' benchmark, sends <one_client_mps> * num\n"
"    messages per second. If a message body is not specified, will send PING\n"
"    messages, otherwise, it will send the specified message body as a TEXT\n"
"    message (see --message-body). one_client_mps must be greater than 0 and\n"
"    less than\n"
"    or equal to 1000 (default:on with one_client_mps=10)\n"
" -t --timeout\n"
"    Test heelhook echoserver's handshake/heartbeat timeouts\n"
"-b <string>, --message-body <string>\n"
"    When in message per second benchmark mode, send <string> as the message\n"
"    payload.\n"
"-n, --num\n"
"    When in autobahn mode, run this many test cases against the fuzzing\n"
"    server. When not in autobahn mode, the number of concurrent client\n"
"    connections to make for stress/load testing of a websocket server\n"
"    (default: 1)\n"
"-s <ms>, --rampup-sleep <ms>\n"
"    When in 'message per second' mode, the amount to wait before creating\n"
"    each new client connection\n",
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

static void mps_write_time_proc(event_loop* loop, event_time_id id,
                                   void* data)
{
    hhunused(id);

    client* c = data;

    client_result r;
    if (g_body == NULL)
    {
        char msg [] = "{param: 'pong'}";
        r = client_send_ping(c, msg, (int)(sizeof(msg) - 1));
    }
    else
    {
        endpoint_msg msg;
        msg.is_text = true;
        msg.data = g_body;
        msg.msg_len = strlen(g_body);
        r = client_send_msg(c, &msg);
    }

    if (r != CLIENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "could not send client message: %d",
              client_fd(c));
        exit(1);
    }
    queue_write(c, loop);
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
    hhlog(HHLOG_LEVEL_DEBUG, "fd %d waiting for %dms", fd, random_ms);
}

static void queue_mps_write(client* c, event_loop* loop)
{
    event_add_time_event(loop, mps_write_time_proc, (MS_PER_S / g_mps), c);
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
        return;
    }

    hhassert(0);
}

static bool check_for_errors(int fd, client* c, event_loop* loop)
{
    int result = 0;
    socklen_t sizeval = sizeof(result);
    int r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, &sizeval);
    if (r < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "getsockopt failed: %d %s", r,
                  strerror(errno));
        teardown_client(c, loop);
        return false;
    }

    if (result != 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "non-blocking connect() failed: %d %s",
                  result, strerror(result));
        teardown_client(c, loop);
        return false;
    }

    return true;
}

static void test_client_connect(event_loop* loop, int fd, void* data)
{
    client* c = data;
    if (!check_for_errors(fd, c, loop))
    {
        exit(1);
    }

    event_result er;
    er = event_add_io_event(loop, fd, EVENT_READABLE,
                            test_client_read, c);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d, fd: %d", er, client_fd(c));
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

static void mps_client_connect(event_loop* loop, int fd, void* data)
{
    client* c = data;
    if (!check_for_errors(fd, c, loop))
    {
        exit(1);
    }

    event_result er;
    er = event_add_io_event(loop, fd, EVENT_READABLE,
                            test_client_read, c);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d, fd: %d", er, fd);
        exit(1);
    }
    queue_mps_write(c, loop);
    queue_write(c, loop);
}

static int random_socket_connect(const char* ip_addr, int port,
                                 event_loop* loop);

static void write_random_bytes(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    int bytes[4096 / sizeof(int)];
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

static bool ab_on_open(client* c, void* userdata)
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

static void mps_on_message(client* c, endpoint_msg* msg, void* userdata)
{
    hhunused(userdata);
    hhlog(HHLOG_LEVEL_DEBUG, "%p got message: \"%.*s\"", c, (int)msg->msg_len,
          msg->data);
}

static void ab_on_message(client* c, endpoint_msg* msg, void* userdata)
{
    event_loop* loop = userdata;
    hhlog(HHLOG_LEVEL_DEBUG, "%p got message: \"%.*s\"", c, (int)msg->msg_len,
          msg->data);

    /*char data[64];
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

static void mps_on_close(client* c, int code, const char* reason,
                         int reason_len, void* userdata)
{
    ab_on_close(c, code, reason, reason_len, userdata);
}

static void mps_on_pong(client* c, char* payload, int payload_len,
                        void* userdata)
{
    hhunused(c);
    hhunused(payload);
    hhunused(payload_len);
    hhunused(userdata);
    /*hhlog(HHLOG_LEVEL_DEBUG, "client %d got pong: %.*s", client_fd(c),
          payload_len, payload);*/
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
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d, fd: %d", er, s);
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
    chatty_cbs.on_open = cs_chatty_on_open;
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
    callbacks.on_open = ab_on_open;
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
        event_loop* loop = event_create_loop(num + 1024);
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
            hhlog(HHLOG_LEVEL_ERROR, "event fail: %d, fd: %d", er,
                  client_fd(&c));
            exit(1);
        }

        /* block and process all connections */
        event_pump_events(loop, 0);
        event_destroy_loop(loop);
    }

}

static void
connect_client(client* c, config_client_options* options,
               client_callbacks* cbs, const char* addr, int port,
               const char* resource, const char* host,
               const char** extra_headers,event_loop* loop, bool is_mps)
{
    client_result cr;
    event_result er;

    cr = client_connect_raw(c, options, cbs, addr, port, resource,
                            host, NULL, NULL, extra_headers,
                            loop);
    if (cr != CLIENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "connect fail: %d", cr);
        exit(1);
    }

    if (is_mps)
    {
        er = event_add_io_event(loop, client_fd(c), EVENT_WRITEABLE,
                                mps_client_connect, c);
    }
    else
    {
        er = event_add_io_event(loop, client_fd(c), EVENT_WRITEABLE,
                                test_client_connect, c);
    }

    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "event fail: %d, fd: %d", er, client_fd(c));
        exit(1);
    }
}

typedef struct
{
    config_client_options* options;
    client_callbacks* cbs;
    const char* addr;
    const char* host;
    const char* resource;
    int port;
    int num;
    const char** extra_headers;
    client* clients;
    int num_connected;
} mps_info;

static void mps_connect_proc(event_loop* loop, event_time_id id, void* data)
{
    mps_info* info = data;
    client* c = &info->clients[info->num_connected];

    /* connect a client */
    connect_client(c, info->options, info->cbs, info->addr, info->port,
                   info->resource, info->host, info->extra_headers, loop, true);
    info->num_connected++;

    /* if this was the last client, we're done */
    if (info->num == info->num_connected)
    {
        event_delete_time_event(loop, id);
    }
}

static void do_mps_test(const char* addr, const char* resource, int port,
                        int num)
{
    config_client_options options;
    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 16 * 1024;
    conn_settings->read_max_msg_size = 16 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->rand_func = random_callback;

    client_callbacks cbs;
    cbs.on_open = NULL;
    cbs.on_message = mps_on_message;
    cbs.on_ping = NULL;
    cbs.on_pong = mps_on_pong;
    cbs.on_close = mps_on_close;

    static const char* extra_headers[] =
    {
        "Origin", "localhost",
        NULL
    };

    client* clients = hhmalloc((size_t)num * sizeof(*clients));
    event_loop* loop = event_create_loop(num + 1024);
    char host[1024];
    snprintf(host, sizeof(host), "%s:%d", addr, port);
    mps_info info;

    if (g_sleep_ms == 0)
    {
        /*
         * if no sleep is specified, just connect all clients as fast as
         * possible
         */
        for (int i = 0; i < num; i++)
        {
            client* c = &clients[i];
            connect_client(c, &options, &cbs, addr, port, resource, host,
                               extra_headers, loop, true);
        }
    }
    else
    {
        /*
         * defer all client connecting to mps_connect_proc, which will be
         * called every g_sleep_ms
         */
        info.options = &options;
        info.cbs = &cbs;
        info.addr = addr;
        info.host = host;
        info.port = port;
        info.resource = resource;
        info.host = host;
        info.num = num;
        info.extra_headers = extra_headers;
        info.clients = clients;
        info.num_connected = 0;

        event_add_time_event(loop, mps_connect_proc, g_sleep_ms, &info);
    }

    event_pump_events(loop, 0);
    event_destroy_loop(loop);
    hhfree(clients);
}

static void timeout_on_ping(client* c, char* payload, int payload_len,
                            void* userdata)
{
    hhunused(c);
    hhunused(payload);
    hhunused(payload_len);
    hhunused(userdata);
    hhlog(HHLOG_LEVEL_INFO, "client %d got ping: %.*s", client_fd(c),
          payload_len, payload);
}

static void timeout_on_close(client* c, int code, const char* reason,
                             int reason_len, void* userdata)
{
    event_loop* loop = userdata;
    if (reason_len > 0)
    {
        hhlog(HHLOG_LEVEL_INFO, "client %d got close: %d %.*s",
                  client_fd(c), code, reason_len, reason);
    }
    else
    {
        hhlog(HHLOG_LEVEL_INFO, "client %d got reason-less close: %d",
                  client_fd(c), code);
    }

    event_delete_io_event(loop, client_fd(c),
                          EVENT_WRITEABLE | EVENT_READABLE);
    client_disconnect(c);
}

static void do_timeout_test(const char* addr, const char* resource, int port)
{
    config_client_options options;
    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 16 * 1024;
    conn_settings->read_max_msg_size = 16 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    conn_settings->rand_func = random_callback;

    client_callbacks cbs;
    cbs.on_open = NULL;
    cbs.on_message = NULL;
    cbs.on_ping = timeout_on_ping;
    cbs.on_pong = NULL;
    cbs.on_close = timeout_on_close;

    static const char* extra_headers[] =
    {
        "Origin", "localhost",
        NULL
    };

    char host[1024];
    snprintf(host, sizeof(host), "%s:%d", addr, port);

    client c;
    event_loop* loop = event_create_loop(1 + 1024);

    connect_client(&c, &options, &cbs, addr, port, resource, host,
                   extra_headers, loop, false);

    random_socket_connect(addr, port, loop);

    event_pump_events(loop, 0);
    event_destroy_loop(loop);
}

typedef enum
{
    AUTOBAHN,
    CHATSERVER,
    MPS,
    TIMEOUT
} client_mode;

int main(int argc, char** argv)
{
    const char* addr = "127.0.0.1";
    const char* port_str = "8080";
    const char* num_str = "1";
    const char* mps_str = "10";
    const char* resource = "/";
    const char* sleep_str = "0";
    bool debug = false;
    client_mode mode = MPS;

    static struct option long_options[] =
    {
        { "debug"       , no_argument      , NULL, 'd'},
        { "addr"        , required_argument, NULL, 'a'},
        { "port"        , required_argument, NULL, 'p'},
        { "resource"    , required_argument, NULL, 'r'},
        { "autobahn"    , no_argument      , NULL, 'u'},
        { "chatserver"  , no_argument      , NULL, 'c'},
        { "mps_bench"   , required_argument, NULL, 'm'},
        { "timeout"     , no_argument      , NULL, 't'},
        { "num"         , required_argument, NULL, 'n'},
        { "message-body", required_argument, NULL, 'b'},
        { "rampup-sleep", required_argument, NULL, 's'},
        { NULL          , 0                , NULL,  0 }
    };

    bool error_occurred = false;
    while (1)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "ucda:p:n:m:b:r:s:", long_options,
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
            mode = AUTOBAHN;
            break;

        case 'c':
            mode = CHATSERVER;
            break;

        case 'n':
            num_str = optarg;
            break;

        case 'm':
            mps_str = optarg;
            break;

        case 'b':
            g_body = optarg;
            break;

        case 'r':
            resource = optarg;
            break;

        case 's':
            sleep_str = optarg;
            break;

        case 't':
            mode = TIMEOUT;
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
    int mps = convert_to_int(mps_str, argv[0]);
    int sleep_ms = convert_to_int(sleep_str, argv[0]);
    if (mps < 0 || mps > 1000 || sleep_ms < 0)
    {
        usage(argv[0]);
        exit(1);
    }

    g_port = port;
    g_addr = addr;
    g_mps = mps;
    g_sleep_ms = sleep_ms;

    if (debug)
    {
        g_log_options.loglevel = HHLOG_LEVEL_DEBUG;
    }

    /* initialize needed state */
    srand(time(NULL));
    hhlog_set_options(&g_log_options);

    switch (mode)
    {
    case AUTOBAHN:
        /*
         * connect one client at a time running num test cases against an 
         * autobahn fuzzing server
         */
        do_autobahn_test(addr, port, num);
        break;
    case CHATSERVER:
        /* connect num clients in parallel and start sending stuff */
        do_chatserver_test(addr, port, num);
        break;
    case MPS:
        do_mps_test(addr, resource, port, num);
        break;
    case TIMEOUT:
        do_timeout_test(addr, resource, port);
        break;
    }

    exit(0);
}
