/* server - handles client connections and sending/receiving data
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

#include "error_code.h"
#include "endpoint.h"
#include "event.h"
#include "iloop.h"
#include "loop_adapters/event_iface.h"
#include "inlist.h"
#include "hhassert.h"
#include "hhclock.h"
#include "hhlog.h"
#include "hhmemory.h"
#include "protocol.h"
#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SERVER_LISTEN_BACKLOG               512
#define SERVER_WATCHDOG_FREQ_MS             100
#define SERVER_HANDSHAKE_TIMEOUT_FREQ_MS    300
#define SERVER_HEARTBEAT_PENDING            0
#define SERVER_HEARTEAT_RECEIVED            ULLONG_MAX

#define COMMAND_SHUT_DOWN   ((char)1)

static char g_heartbeat_msg[] = "heartbeat";

struct server_conn
{
    int fd;
    endpoint endp;
    server* serv;
    void* userdata;
    uint64_t timeout;
    server_conn* next; /* in either active or free list */
    server_conn* prev; /* in either active or free list */
    server_conn* timeout_next; /* in either handshake or heartbeat list */
    server_conn* timeout_prev; /* in either handshake or heartbeat list */
};

struct server
{
    bool stopping;
    int fd;
    int num_connected;
    server_conn* connections;
    server_conn* active_head;
    server_conn* active_tail;
    server_conn* free_head;
    server_conn* free_tail;
    server_conn* handshake_head;
    server_conn* handshake_tail;
    server_conn* heartbeat_head;
    server_conn* heartbeat_tail;
    iloop loop;
    config_server_options options;
    server_callbacks cbs;
    void* userdata;
    int* pipes;
};

static void accept_callback(iloop* loop, int fd, void* data);
static void worker_pipe_callback(iloop* loop, int fd, void* data);
static void read_from_client_callback(iloop* loop, int fd, void* data);
static void write_to_client_callback(iloop* loop, int fd, void* data);

static iloop_io_callback* g_io_cbs[ILOOP_NUMBER_OF_IO_CB] =
{
    accept_callback, /* ILOOP_ACCEPT_CB */
    read_from_client_callback, /* ILOOP_READ_CB */
    write_to_client_callback, /* ILOOP_WRITE_CB */
    worker_pipe_callback /* ILOOP_WORKER_CB */
};

static void stop_watchdog(iloop* loop,iloop_time_cb_type type,void* data);
static void send_heartbeats(iloop* loop,iloop_time_cb_type type,void* data);
static void expire_heartbeats(iloop* loop,iloop_time_cb_type type,void* data);
static void timeout_handshakes(iloop* loop,iloop_time_cb_type type,void* data);

static iloop_time_callback* g_time_cbs[ILOOP_NUMBER_OF_TIME_CB] =
{
    stop_watchdog, /* ILOOP_WATCHDOG_CB */
    send_heartbeats, /* ILOOP_HEARTBEAT_CB */
    expire_heartbeats, /* ILOOP_HEARTBEAT_EXPIRE_CB */
    timeout_handshakes /* ILOOP_HANDSHAKE_TIMEOUT_CB */
};

/*
 * Required when including iloop.h
 */
iloop* iloop_from_io(iloop_cb_type type, void *data)
{
    switch (type)
    {
    case ILOOP_ACCEPT_CB:
    case ILOOP_WORKER_CB:
        return &(((server*)data)->loop);

    case ILOOP_READ_CB:
    case ILOOP_WRITE_CB:
        return &(((server_conn*)data)->serv->loop);

    case ILOOP_NUMBER_OF_IO_CB:
        hhassert(false);
        return NULL;
    }

    hhassert(false);
    return NULL;
}

iloop* iloop_from_time(iloop_time_cb_type type, void *data)
{
    hhunused(type);
    return &(((server*)data)->loop);
}

static bool server_on_connect_callback(endpoint* conn, protocol_conn* proto_conn,
                                    void* userdata);

static void server_on_message_callback(endpoint* conn, endpoint_msg* msg,
                                       void* userdata);

static void server_on_ping_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata);

static void server_on_pong_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata);

static void server_on_close_callback(endpoint* conn_info, int code,
                                     const char* reason, int reason_len,
                                     void* userdata);

static endpoint_callbacks g_server_cbs =
{
    .on_connect = server_on_connect_callback,
    .on_message = server_on_message_callback,
    .on_ping = server_on_ping_callback,
    .on_pong = server_on_pong_callback,
    .on_close = server_on_close_callback
};

static int init_conn(server_conn* conn, server* serv)
{
    conn->serv = serv;
    conn->userdata = NULL;
    conn->timeout_next = NULL;
    conn->timeout_prev = NULL;
    conn->prev = NULL;
    conn->next = NULL;
    int r = endpoint_init(&conn->endp, ENDPOINT_SERVER,
                          &serv->options.endp_settings, &g_server_cbs, conn);

    return r;
}

static void deinit_conn(server_conn* conn)
{
    endpoint_deinit(&conn->endp);
}

static server_conn* activate_conn(server* serv, int client_fd)
{
    if (serv->free_head == NULL) return NULL;

    server_conn* conn = serv->free_head;

    /* pop the next free connection object off the head of the list */
    INLIST_REMOVE(serv, conn, next, prev, free_head, free_tail);

    /* push it to the back of the active list of connections */
    INLIST_APPEND(serv, conn, next, prev, active_head, active_tail);

    /* push it to the back of the list of hanshake timeouts */
    config_server_options* opt = &serv->options;
    if (opt->handshake_timeout_ms > 0)
    {
        INLIST_APPEND(serv, conn, timeout_next, timeout_prev, handshake_head,
                      handshake_tail);
        conn->timeout = hhclock_get_now_ms() + opt->handshake_timeout_ms;
    }

    conn->fd = client_fd;
    endpoint_reset(&conn->endp);
    serv->num_connected++;

    return conn;
}

static void deactivate_conn(server* serv, server_conn* conn)
{
    /* take this client out of the active list */
    INLIST_REMOVE(serv, conn, next, prev, active_head, active_tail);

    serv->num_connected--;

    /* put it on the end of the free list */
    INLIST_APPEND(serv, conn, next, prev, free_head, free_tail);

    /* remove from any timeout lists */
    switch (conn->endp.pconn.state)
    {
    case PROTOCOL_STATE_READ_HANDSHAKE:
    case PROTOCOL_STATE_WRITE_HANDSHAKE:
        INLIST_REMOVE(serv, conn, timeout_next, timeout_prev, handshake_head,
                      handshake_tail);
        break;

    case PROTOCOL_STATE_CONNECTED:
        INLIST_REMOVE(serv, conn, timeout_next, timeout_prev, heartbeat_head,
                      heartbeat_tail);
        break;
    }
}

static iloop_result queue_write(server_conn* conn)
{
    /* queue up writing response back */
    iloop_result er;
    iloop* loop = &conn->serv->loop;
    er = loop->add_io(loop, conn->fd, ILOOP_WRITEABLE, ILOOP_WRITE_CB, conn);

    return er;
}

static void server_on_ping_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    hhlog(HHLOG_LEVEL_DEBUG_2, "ping received from client %d: %.*s", conn->fd,
          (int)payload_len, payload);

    if (serv->cbs.on_ping != NULL)
    {
        serv->cbs.on_ping(conn, payload, payload_len, serv->userdata);
    }
    else
    {
        server_conn_send_pong(conn, payload, payload_len);
    }
}

static void server_on_pong_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    hhlog(HHLOG_LEVEL_DEBUG_2, "pong received from client %d: %.*s", conn->fd,
          (int)payload_len, payload);

    if (serv->options.heartbeat_interval_ms > 0 &&
        serv->options.heartbeat_ttl_ms > 0)
    {
        if (payload_len == sizeof(g_heartbeat_msg) - 1 &&
            memcmp(payload, g_heartbeat_msg, payload_len) == 0)
        {
            /* this conn is safe, move to the back of the list */
            conn->timeout = SERVER_HEARTEAT_RECEIVED;
            INLIST_MOVE_BACK(serv, conn, timeout_next, timeout_prev,
                             heartbeat_head, heartbeat_tail);
        }
    }

    if (serv->cbs.on_pong != NULL)
    {
        serv->cbs.on_pong(conn, payload, payload_len, serv->userdata);
    }
}

static void server_on_close_callback(endpoint* conn_info, int code,
                                     const char* reason, int reason_len,
                                     void* userdata)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;
    iloop* loop = &serv->loop;

    if (serv->cbs.on_close != NULL)
    {
        serv->cbs.on_close(conn, code, reason, reason_len, serv->userdata);
    }

    /* take this client out of the event loop */
    loop->delete_io(loop, conn->fd, ILOOP_READABLE | ILOOP_WRITEABLE);

    /* close the socket */
    close(conn->fd);

    /* set fd to -1 to mark socket dead */
    conn->fd = -1;

    /* remove from active list */
    deactivate_conn(serv, conn);

    /*
     * if we're stopping, and we were the last connection, tear down the
     * event loop
     */
    if (serv->stopping && serv->active_head == NULL && loop->stop != NULL)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "final client disconnected, stopping");
        loop->delete_time(loop, ILOOP_HEARTBEAT_CB);
        loop->delete_time(loop, ILOOP_HEARTBEAT_EXPIRE_CB);
        loop->delete_time(loop, ILOOP_HANDSHAKE_TIMEOUT_CB);
        loop->stop(loop);
    }
}

static void write_to_client_callback(iloop* loop, int fd, void* data)
{
    server_conn* conn = data;

    endpoint_write_result r = endpoint_write(&conn->endp, fd);
    switch (r)
    {
    case ENDPOINT_WRITE_CONTINUE:
        return;
    case ENDPOINT_WRITE_DONE:
        /*
         * done writing to the client for now, don't call this again until
         * there's more data to be sent
         */
        loop->delete_io(loop, fd, ILOOP_WRITEABLE);
        return;
    case ENDPOINT_WRITE_ERROR:
    case ENDPOINT_WRITE_CLOSED:
        /* proper callback will have been called */
        return;
    }

    hhassert(0);
}

static bool
server_on_connect_callback(endpoint* conn_info,
                        protocol_conn* proto_conn, void* userdata)
{
    hhunused(conn_info);
    hhunused(proto_conn);

    server_conn* conn = userdata;
    server* serv = conn->serv;
    int* extensions_out = NULL;
    const char** extensions = NULL;
    const char* subprotocol = NULL;

    /* initialize output params */
    int subprotocol_out = -1;
    unsigned num_extensions = server_get_num_client_extensions(conn);
    if (num_extensions > 0)
    {
        extensions_out = hhmalloc(num_extensions * sizeof(*extensions_out));
        for (unsigned i = 0; i < num_extensions; i++)
        {
            extensions_out[i] = -1;
        }
    }

    /*
     * allow users of this interface to reject or pick
     * subprotocols/extensions
     */
    if (serv->cbs.on_connect != NULL &&
        !serv->cbs.on_connect(conn, &subprotocol_out, extensions_out,
                           serv->userdata))
    {
        goto reject_client;
    }

    if (subprotocol_out >= 0)
    {
        subprotocol =
            server_get_client_subprotocol(conn, (unsigned)subprotocol_out);
    }

    if (extensions_out != NULL && extensions_out[0] >= 0)
    {
        /* +1 for terminating NULL */
        extensions = hhmalloc((num_extensions + 1) * sizeof(*extensions));
        unsigned e = 0;
        for (e = 0; e < num_extensions; e++)
        {
            if (extensions_out[e] < 0) break;
            unsigned out = (unsigned)extensions_out[e];
            extensions[e] =
                (char*)server_get_client_extension(conn, out);
        }
        extensions[e] = NULL;
    }

    endpoint_result r;
    r = endpoint_send_handshake_response(&conn->endp, subprotocol, extensions);

    if (r != ENDPOINT_RESULT_SUCCESS)
    {
        goto reject_client;
    }

    if (extensions_out != NULL)
    {
        hhfree(extensions_out);
        extensions_out = NULL;
    }

    if (extensions != NULL)
    {
        hhfree(extensions);
        extensions = NULL;
    }

    iloop_result ir = queue_write(conn);
    if (ir != ILOOP_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "write_handshake event loop error: %d", ir);
        goto reject_client;
    }

    if (serv->cbs.on_open != NULL)
    {
        serv->cbs.on_open(conn, serv->userdata);
    }

    INLIST_REMOVE(serv, conn, timeout_next, timeout_prev, handshake_head,
                  handshake_tail);

    config_server_options* opt = &serv->options;
    uint64_t hb_interval = opt->heartbeat_interval_ms;
    if (hb_interval > 0)
    {
        conn->timeout = SERVER_HEARTEAT_RECEIVED;
        INLIST_APPEND(serv, conn, timeout_next, timeout_prev, heartbeat_head,
                      heartbeat_tail);
    }

    return true;

reject_client:
    if (extensions_out != NULL) hhfree(extensions_out);
    if (extensions != NULL) hhfree(extensions);
    return false;
}

static void server_on_message_callback(endpoint* conn_info, endpoint_msg* msg,
                                       void* userdata)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    hhlog(HHLOG_LEVEL_DEBUG_1, "msg received from client %d: %.*s", conn->fd,
          (int)msg->msg_len, msg->data);

    if (serv->cbs.on_message!= NULL)
    {
        serv->cbs.on_message(conn, msg, serv->userdata);
    }
}

static void read_from_client_callback(iloop* loop, int fd, void* data)
{
    hhunused(loop);
    server_conn* conn = data;

    iloop_result ir;
    endpoint_read_result r = endpoint_read(&conn->endp, fd);
    switch (r)
    {
    case ENDPOINT_READ_SUCCESS:
        return;
    case ENDPOINT_READ_SUCCESS_WROTE_DATA:
        ir = queue_write(conn);
        if (ir != ILOOP_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "read_from_client event loop error: %d",ir);
        }
        return;
    case ENDPOINT_READ_ERROR:
    case ENDPOINT_READ_CLOSED:
        /*
         * errors will have been logged and handled properly by on_close
         * callback
         */
        loop->delete_io(loop, fd, ILOOP_READABLE);
        return;
    }

    hhassert(0);
}

static void accept_callback(iloop* loop, int fd, void* data)
{
    server* serv = data;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr*)(&addr), &len);

    if (client_fd == -1)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            hhlog(HHLOG_LEVEL_ERROR,
                  "-1 fd when accepting socket, fd: %d, err: %d (%s)", fd,
                  errno, strerror(errno));
        }
        return;
    }

    hhlog(HHLOG_LEVEL_DEBUG, "client connected, fd: %d", client_fd);

    server_conn* conn = activate_conn(serv, client_fd);
    if (conn == NULL)
    {
        hhlog(HHLOG_LEVEL_ERROR, "Server at max client capacity: %d",
                  serv->options.max_clients);
        close(client_fd);
        return;
    }

    iloop_result r;
    r = loop->add_io(loop, client_fd, ILOOP_READABLE, ILOOP_READ_CB, conn);

    if (r != ILOOP_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "add client to event loop error: %d", r);
        loop->delete_io(loop, client_fd, ILOOP_READABLE);
    }
}

/*
 * Only used in worker process mode
 */
static void worker_pipe_callback(iloop* loop, int fd, void* data)
{
    hhunused(loop);

    server* serv = data;

    char cmd = 0;
    ssize_t num = read(fd, &cmd, sizeof(cmd));

    if (num <= 0)
    {
        /* error or eof from pipe, close and don't listen anymore */
        close(fd);
        loop->delete_io(loop, fd, ILOOP_READABLE | ILOOP_WRITEABLE);

        if (num < 0)
        {
            hhlog(HHLOG_LEVEL_ERROR,
                  "invalid worker_pipe_callback read, stopping worker process"
                  " err: %d (%s)", errno, strerror(errno));
            server_stop(serv);
            return;
        }
        else if (num == 0)
        {
            if (serv->stopping)
            {
                hhlog(HHLOG_LEVEL_DEBUG,
                   "pipe died but server is stopping, silently closed pipe");
            }
            else
            {
                hhlog(HHLOG_LEVEL_ERROR,
                        "master process pipe eof, stopping worker process");
                server_stop(serv);
            }
            return;
        }
    }

    switch (cmd)
    {
    case COMMAND_SHUT_DOWN:
            hhlog(HHLOG_LEVEL_DEBUG,
                  "worker received shut down command, shutting down");
        server_stop(serv);
        break;
    default:
        hhlog(HHLOG_LEVEL_ERROR, "invalid cmd: %c", cmd);
        break;
    }
}

server* server_create(config_server_options* options,
                      server_callbacks* callbacks, void* userdata)
{
    server* serv = server_create_detached(options, callbacks, userdata);
    if (serv == NULL) return NULL;

    if (!iloop_event_attach_internal(&serv->loop, &serv->options))
    {
        server_destroy(serv);
        return NULL;
    }

    return serv;
}

server* server_create_detached(config_server_options* options,
                               server_callbacks* callbacks, void* userdata)
{
    if (options->enable_workers && options->num_workers < 1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "Invalid number of workers: %d",
              options->num_workers);
    }

    int i = 0;
    server* serv = hhmalloc(sizeof(*serv));
    if (serv == NULL) return NULL;

    serv->stopping = false;
    serv->fd = -1;
    serv->num_connected = 0;
    serv->active_head = NULL;
    serv->active_tail = NULL;
    serv->free_head = NULL;
    serv->free_tail = NULL;
    serv->handshake_head = NULL;
    serv->handshake_tail = NULL;
    serv->heartbeat_head = NULL;
    serv->heartbeat_tail = NULL;
    serv->cbs = *callbacks;
    serv->options = *options;
    serv->userdata = userdata;
    serv->pipes = NULL;

    int max_clients = options->max_clients;
    hhassert(max_clients >= 0);
    serv->connections = hhmalloc((size_t)max_clients * sizeof(server_conn));
    if (serv->connections == NULL) goto err_create;

    memset(&serv->loop, 0, sizeof(serv->loop));
    serv->loop.userdata = NULL;
    serv->loop.io_cbs = g_io_cbs;
    serv->loop.time_cbs = g_time_cbs;

    /*
     * all available connection objects are 'free', initialize each and add it
     * to the free list
     */
    for (i = 0; i < max_clients; i++)
    {
        server_conn* conn = &serv->connections[i];
        if (init_conn(conn, serv) < 0)
        {
            i++; /* need to do this so loop in err_create works correctly */
            goto err_create;
        }
        INLIST_APPEND(serv, conn,  next, prev, free_head, free_tail);
    }

    return serv;

err_create:
    if (serv->connections != NULL)
    {
        /* have to clean up already allocated connections */
        for(int j = 0; j < i; j++)
        {
           deinit_conn(&serv->connections[j]);
        }
        hhfree(serv->connections);
    }
    hhfree(serv);
    return NULL;
}

void server_destroy(server* serv)
{
    if (serv->loop.cleanup != NULL)
    {
        serv->loop.cleanup(&serv->loop);
    }

    if (serv->connections != NULL)
    {
        int max_clients = serv->options.max_clients;
        for (int i = 0; i < max_clients; i++)
        {
            deinit_conn(&serv->connections[i]);
        }
    }

    if(serv->pipes != NULL)
    {
        hhfree(serv->pipes);
    }

    hhfree(serv->connections);
    hhfree(serv);
}

/*
 * Returns true if this is the master process, false if this is a worker
 * process. It multi-process is not enabled, will return false
 */
server_process_type server_get_process_type(server* serv)
{
    config_server_options* opt = &serv->options;

    if (!opt->enable_workers)
    {
        return SERVER_PROCESS_SINGLETON;
    }
    else if (serv->pipes != NULL)
    {
        return SERVER_PROCESS_MASTER;
    }
    else
    {
        return SERVER_PROCESS_WORKER;
    }
}

bool server_is_master(server* serv)
{
    return (server_get_process_type(serv) == SERVER_PROCESS_MASTER);
}

/* set per-connection userdata */
void server_conn_set_userdata(server_conn* conn, void* userdata)
{
    conn->userdata = userdata;
}

/* get per-connection userdata */
void* server_conn_get_userdata(server_conn* conn)
{
    return conn->userdata;
}

static void server_teardown(server* serv)
{
    iloop* loop = &serv->loop;

    /* stop listening for new connections */
    loop->delete_io(loop, serv->fd, ILOOP_READABLE | ILOOP_WRITEABLE);

    /* stop accepting connections to the server */
    close(serv->fd);

    /*
     * if we don't currently have any client connections, we're pretty
     * much done
     */
    if (serv->active_head == NULL)
    {
        loop->delete_time(loop, ILOOP_HEARTBEAT_CB);
        loop->delete_time(loop, ILOOP_HEARTBEAT_EXPIRE_CB);
        loop->delete_time(loop, ILOOP_HANDSHAKE_TIMEOUT_CB);

        if (loop->stop != NULL)
        {
            loop->stop(loop);
        }
        return;
    }

    /* close each connection */
    INLIST_FOREACH(serv,server_conn,conn,next,prev,active_head,active_tail)
    {
        static const char msg[] = "server shutting down";
        server_conn_close(conn, HH_ERROR_GOING_AWAY, msg, sizeof(msg)-1);
    }
}

static void kill_workers(server* serv)
{
    config_server_options* opt = &serv->options;
    for (int i = 0; i < opt->num_workers; i++)
    {
        char cmd = COMMAND_SHUT_DOWN;
        ssize_t num = write(serv->pipes[i], &cmd, sizeof(cmd));
        if (num < 0)
        {
            hhlog(HHLOG_LEVEL_ERROR,
                  "error sending cmd to worker: %d (%s)", errno,
                  strerror(errno));
        }
    }
}

/* check if we've been asked to stop, and stop */
static void stop_watchdog(iloop* loop, iloop_time_cb_type type, void* data)
{
    server* serv = data;

    if (hhlog_get_level() == HHLOG_LEVEL_DEBUG_4 && !server_is_master(serv))
    {
        hhlog(HHLOG_LEVEL_DEBUG_4, "current_connected: %d",
                serv->num_connected);
    }

    if (serv->cbs.should_stop != NULL &&
        serv->cbs.should_stop(serv, serv->userdata))
    {
        serv->stopping = true;
    }

    if (serv->stopping)
    {
        hhlog(HHLOG_LEVEL_INFO, "received stop, sending close to all clients");

        server_process_type ptype = server_get_process_type(serv);
        if (ptype == SERVER_PROCESS_MASTER)
        {
            kill_workers(serv);
        }

        /* we aren't needed any more */
        loop->delete_time(loop, type);

        /* tear down everything else */
        server_teardown(serv);
    }
}

static void send_heartbeats(iloop* loop, iloop_time_cb_type type, void* data)
{
    hhunused(loop);
    hhunused(type);

    server* serv = data;

    uint64_t hb_ttl = serv->options.heartbeat_ttl_ms;
    bool send_ping = (hb_ttl > 0);
    INLIST_FOREACH(serv, server_conn, conn, timeout_next, timeout_prev,
                   heartbeat_head, heartbeat_tail)
    {
        if (send_ping)
        {
            server_conn_send_ping(conn, g_heartbeat_msg,
                                  sizeof(g_heartbeat_msg)-1);
            conn->timeout = SERVER_HEARTBEAT_PENDING;
        }
        else
        {
            server_conn_send_pong(conn, g_heartbeat_msg,
                                  sizeof(g_heartbeat_msg)-1);
        }
    }
}

static void expire_heartbeats(iloop* loop, iloop_time_cb_type type, void* data)
{
    hhunused(loop);
    hhunused(type);

    server* serv = data;
    INLIST_FOREACH(serv, server_conn, conn, timeout_next, timeout_prev,
                   heartbeat_head, heartbeat_tail)
    {
        if (conn->timeout != SERVER_HEARTEAT_RECEIVED)
        {
            hhlog(HHLOG_LEVEL_DEBUG, "closing, heartbeat expired for: %d",
                  conn->fd);
            server_on_close_callback(&conn->endp, 0, NULL, 0, conn);
        }
        else
        {
            /* this list is sorted by failed to not failed, so we're done */
            break;
        }
    }
}

static void timeout_handshakes(iloop* loop, iloop_time_cb_type type, void* data)
{
    hhunused(loop);
    hhunused(type);

    server* serv = data;
    uint64_t now = hhclock_get_now_ms();
    INLIST_FOREACH(serv, server_conn, conn, timeout_next, timeout_prev,
                   handshake_head, handshake_tail)
    {
        if (now >= conn->timeout)
        {
            hhlog(HHLOG_LEVEL_DEBUG,
                "closing, handshake timed out %"PRIu64" >= %"PRIu64" (%d, %p)",
                 conn->timeout, now, conn->fd, conn);
            server_on_close_callback(&conn->endp, 0, NULL, 0, conn);
        }
        else
        {
            /* this list is sorted by time, so we're done */
            break;
        }
    }
}

static server_result endpoint_result_to_server_result(endpoint_result r)
{
    switch (r)
    {
    case ENDPOINT_RESULT_SUCCESS:
        return SERVER_RESULT_SUCCESS;
    case ENDPOINT_RESULT_FAIL:
        return SERVER_RESULT_FAIL;
    }

    hhassert(0);
    return SERVER_RESULT_FAIL;
}

static int fork_workers(server* serv)
{
    config_server_options* opt = &serv->options;

    int fd = serv->fd;
    serv->pipes = hhcalloc(opt->num_workers, sizeof(int));

    for (int i = 0; i < opt->num_workers; i++)
    {
        int p[2];
        if (pipe(p) == -1)
        {
            hhlog(HHLOG_LEVEL_ERROR, "failed to create pipe: %s",
                    strerror(errno));
            hhfree(serv->pipes);
            serv->pipes = NULL;
            return -1;
        }

        if (fcntl(p[0], F_SETFL, O_NONBLOCK) == -1 ||
            fcntl(p[1], F_SETFL, O_NONBLOCK) == -1)
        {
            hhlog(HHLOG_LEVEL_ERROR, "fcntl failed on read pipe: %s",
                  strerror(errno));
            close(p[0]);
            close(p[1]);
            hhfree(serv->pipes);
            serv->pipes = NULL;
            return -1;
        }

        pid_t pid = fork();

        switch (pid)
        {
        case -1:
            hhlog(HHLOG_LEVEL_ERROR, "fork failed: %s", strerror(errno));
            return -1;
        case 0:
            hhlog(HHLOG_LEVEL_DEBUG, "forked child %d, parent: %d",
                    getpid(), getppid());
            hhfree(serv->pipes);
            serv->pipes = NULL;

            /* close write end */
            close(p[1]);
            /*
             * we're going to get notified about new client sockets
             * from the read end
             */
            fd = p[0];
            goto child_done;
        default:
            /* close the read end */
            close(p[0]);
            /* we're going to write new client sockets to the write end */
            serv->pipes[i] = p[1];
            break;
        }
    }

child_done:
    return fd;
}

/* queue up a message to send on this connection */
server_result server_conn_send_msg(server_conn* conn, endpoint_msg* msg)
{
    hhassert(conn->fd != -1);

    hhlog(HHLOG_LEVEL_DEBUG_1, "sending msg to client %d (%zu bytes): %.*s",
          conn->fd, msg->msg_len, (int)msg->msg_len, msg->data);

    endpoint_result r = endpoint_send_msg(&conn->endp, msg);

    iloop_result ir = queue_write(conn);
    if (ir != ILOOP_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "send_msg event loop error: %d", ir);
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/* send a ping with payload (NULL for no payload)*/
server_result server_conn_send_ping(server_conn* conn, char* payload,
                                    int payload_len)
{
    hhassert(conn->fd != -1);

    hhlog(HHLOG_LEVEL_DEBUG_2, "sending ping to client %d (%d bytes): %.*s",
          conn->fd, payload_len, payload_len, payload);

    endpoint_result r = endpoint_send_ping(&conn->endp, payload, payload_len);

    iloop_result ir = queue_write(conn);
    if (ir != ILOOP_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "send_ping event loop error: %d", ir);
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/* send a pong with payload (NULL for no payload)*/
server_result server_conn_send_pong(server_conn* conn, char* payload,
                                    int payload_len)
{
    hhassert(conn->fd != -1);

    hhlog(HHLOG_LEVEL_DEBUG_2, "sending pong to client %d (%d bytes): %.*s",
          conn->fd, payload_len, payload_len, payload);

    endpoint_result r = endpoint_send_pong(&conn->endp, payload, payload_len);

    iloop_result ir = queue_write(conn);
    if (ir != ILOOP_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "send_pong event loop error: %d", ir);
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
server_result server_conn_close(server_conn* conn, uint16_t code,
                                const char* reason, int reason_len)
{
    hhassert(conn->fd != -1);

    hhlog(HHLOG_LEVEL_DEBUG_2, "sending close to client %d (%d bytes): %d %.*s",
          conn->fd, reason_len, (int)code, reason_len, reason);

    endpoint_result r = endpoint_close(&conn->endp, code, reason, reason_len);

    /* it's possible for endpoint_close to directly close the client socket */
    if (conn->fd != -1)
    {
        iloop_result ir = queue_write(conn);
        if (ir != ILOOP_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "close event loop error: %d", ir);
            return SERVER_RESULT_FAIL;
        }
    }

    return endpoint_result_to_server_result(r);
}

/*
 * get the total number of headers the client sent
 */
unsigned server_get_num_client_headers(server_conn* conn)
{
    return protocol_get_num_headers(&conn->endp.pconn);
}

/*
 * Get the the field name of one of the headers sent by the client
 */
const char* server_get_header_name(server_conn* conn, unsigned index)
{
    return protocol_get_header_name(&conn->endp.pconn, index);
}

/*
 * Get the darray of values (type char*) for a given header index
 */
const darray* server_get_header_values(server_conn* conn, unsigned index)
{
    return protocol_get_header_values(&conn->endp.pconn, index);
}

/*
 * get number of subprotocols the client reported they support
 */
unsigned server_get_num_client_subprotocols(server_conn* conn)
{
    return protocol_get_num_subprotocols(&conn->endp.pconn);
}

/*
 * get a subprotocol the client reported they support
 */
const char* server_get_client_subprotocol(server_conn* conn, unsigned index)
{
    return protocol_get_subprotocol(&conn->endp.pconn, index);
}

/*
 * get number of extensions the client reported they support
 */
unsigned server_get_num_client_extensions(server_conn* conn)
{
    return protocol_get_num_extensions(&conn->endp.pconn);
}

/*
 * get an extension the client reported they support
 */
const char* server_get_client_extension(server_conn* conn, unsigned index)
{
    return protocol_get_extension(&conn->endp.pconn, index);
}

/*
 * Get the resource requested by the client
 */
const char* server_get_resource(server_conn* conn)
{
    return protocol_get_resource(&conn->endp.pconn);
}

/*
 * stop the server, close all connections. will cause server_listen
 * to return eventually. safe to call from a signal handler
 */
void server_stop(server* serv)
{
    serv->stopping = true;
}

server_result server_init(server* serv)
{
    config_server_options* opt = &serv->options;
    int pipefd = -1;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "failed to create socket: %s",
                  strerror(errno));
        goto fail;
    }

    if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "fcntl failed on socket: %s",
              strerror(errno));
        goto fail;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opt->port);
    addr.sin_addr.s_addr = hh_htonl(INADDR_ANY);
    if (opt->bindaddr != NULL &&
        inet_aton(opt->bindaddr, &addr.sin_addr) == 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "invalid bind address: %s",
                  opt->bindaddr);
        goto fail;
        return SERVER_RESULT_FAIL;
    }

    if (bind(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "failed to bind socket: %s",
                  strerror(errno));
        goto fail;
        return SERVER_RESULT_FAIL;
    }

    if (listen(s, SERVER_LISTEN_BACKLOG) == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "failed to listen on socket: %s",
                  strerror(errno));
        goto fail;
        return SERVER_RESULT_FAIL;
    }

    /* socket was set up successfully */
    serv->fd = s;
    iloop* loop = &serv->loop;

    if (opt->enable_workers && opt->num_workers >= 1)
    {
        if ((pipefd = fork_workers(serv)) == -1)
        {
            goto fail;
        }
    }

    /* Now that we've forked, we can start the event loop */

    /* call attach callback, or attach default event loop if no cb specified */
    if (serv->cbs.attach_loop != NULL &&
        !serv->cbs.attach_loop(serv, &serv->loop, &serv->options,
                               serv->userdata))
    {
        goto fail;
    }
    else
    {
        if (!iloop_event_attach_internal(&serv->loop, &serv->options))
        {
            goto fail;
        }
    }

    iloop_result r;
    server_process_type type = server_get_process_type(serv);
    if (type == SERVER_PROCESS_WORKER || type == SERVER_PROCESS_SINGLETON)
    {
        r = loop->add_io(loop, serv->fd, ILOOP_READABLE, ILOOP_ACCEPT_CB, serv);

        if (r != ILOOP_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "error adding accept callback to event"
                      "loop: %d (err: %s)", r, strerror(errno));
            goto fail;
        }
    }

    /*
     * add watchdog so we know when we're being asked to shut down
     * the server
     */
    loop->add_time(loop, ILOOP_WATCHDOG_CB, SERVER_WATCHDOG_FREQ_MS, 0, serv);

    switch (type)
    {
    case SERVER_PROCESS_MASTER:
        /* we are the master process, we don't need connections */
        hhfree(serv->connections);
        serv->connections = NULL;
        break;
    case SERVER_PROCESS_WORKER:
        hhassert(pipefd != -1);

        /*
         * the master has opened a pipe to communicate with us, we need to
         * listen for activity on it
         */
        r = loop->add_io(loop, pipefd, ILOOP_READABLE, ILOOP_WORKER_CB,
                         serv);

        if (r != ILOOP_SUCCESS)
        {
            hhlog(HHLOG_LEVEL_ERROR, "error adding worker callback to event"
                      "loop: %d (err: %s)", r, strerror(errno));
            goto fail;
        }
        /*
         * fallthru
         */
    case SERVER_PROCESS_SINGLETON:
    {
        uint64_t hb_interval = serv->options.heartbeat_interval_ms;
        uint64_t hb_ttl = serv->options.heartbeat_ttl_ms;
        uint64_t handshake_timeout = serv->options.handshake_timeout_ms;
        if (hb_interval > 0)
        {
            /* send a ping every hb_interval */
            loop->add_time(loop, ILOOP_HEARTBEAT_CB, hb_interval, 0,
                           serv);

            if (hb_ttl > 0)
            {
                /*
                 * check for responses every hb_interval, hb_ttl seconds
                 * after
                 */
                loop->add_time(loop, ILOOP_HEARTBEAT_EXPIRE_CB, hb_interval,
                               hb_ttl, serv);
            }
        }

        if (handshake_timeout > 0)
        {
            loop->add_time(loop, ILOOP_HANDSHAKE_TIMEOUT_CB,
                           SERVER_HANDSHAKE_TIMEOUT_FREQ_MS, 0, serv);
        }
        break;
    }
    }

    return SERVER_RESULT_SUCCESS;

fail:
    if (s != -1) close(s);
    if (pipefd != -1) close(pipefd);
    return SERVER_RESULT_FAIL;
}

/*
 * Get internal iloop to attach this server to another event loop
 */
iloop* server_get_iloop(server* serv)
{
    return &serv->loop;
}

/*
 * blocks indefinitely listening to connections from clients
 */
server_result server_listen(server* serv)
{
    /* start the server, if it hasn't been already */
    if (serv->fd == -1)
    {
        server_result r = server_init(serv);
        if (r != SERVER_RESULT_SUCCESS)
        {
            return r;
        }
    }

    hhassert(serv->loop.listen != NULL);

    /* block and process all connections */
    serv->loop.listen(&serv->loop);

    return SERVER_RESULT_SUCCESS;
}

