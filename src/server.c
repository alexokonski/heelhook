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
#include "inlist.h"
#include "hhassert.h"
#include "hhlog.h"
#include "hhmemory.h"
#include "protocol.h"
#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SERVER_MAX_LOG_LENGTH 1024
#define SERVER_HEADER_READ_LENGTH (1024 * 2)
#define SERVER_MAX_READ_LENGTH (1024 * 64)
#define SERVER_MAX_WRITE_LENGTH (1024 * 64)
#define SERVER_LISTEN_BACKLOG 512
#define SERVER_WATCHDOG_FREQ_MS 5

struct server_conn
{
    int fd;
    endpoint endp;
    server* serv;
    server_conn* next;
    server_conn* prev;
};

struct server
{
    BOOL stopping;
    event_time_id watchdog_id;
    int fd;
    server_conn* connections;
    server_conn* active_head;
    server_conn* active_tail;
    server_conn* free_head;
    server_conn* free_tail;
    event_loop* loop;
    config_server_options options;
    server_callbacks callbacks;
};

static void write_to_client_callback(event_loop* loop, int fd, void* data);

static BOOL server_on_open_callback(
    endpoint* conn,
    protocol_conn* proto_conn,
    void* userdata
);

static void server_on_message_callback(
    endpoint* conn,
    endpoint_msg* msg,
    void* userdata
);

static void server_on_ping_callback(
    endpoint* conn_info,
    char* payload,
    int payload_len,
    void* userdata
);

static void server_on_close_callback(
    endpoint* conn_info,
    int code,
    const char* reason,
    int reason_len,
    void* userdata
);

static endpoint_callbacks g_endpoint_cbs =
{
    .on_open_callback = server_on_open_callback,
    .on_message_callback = server_on_message_callback,
    .on_ping_callback = server_on_ping_callback,
    .on_close_callback = server_on_close_callback
};

static int init_conn(server_conn* conn, server* serv)
{
    conn->serv = serv;
    int r = endpoint_init(
        &conn->endp,
        ENDPOINT_SERVER,
        &serv->options.endp_settings,
        &g_endpoint_cbs,
        conn
    );

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

    conn->fd = client_fd;
    endpoint_reset(&conn->endp);

    return conn;
}

static event_result queue_write(server_conn* conn)
{
    /* queue up writing the handshake response back */
    event_result er = event_add_io_event(
        conn->serv->loop,
        conn->fd,
        EVENT_WRITEABLE,
        write_to_client_callback,
        conn
    );

    return er;
}

static void server_on_ping_callback(
    endpoint* conn_info,
    char* payload,
    int payload_len,
    void* userdata
)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    if (serv->callbacks.on_ping_callback != NULL)
    {
        serv->callbacks.on_ping_callback(conn, payload, payload_len);
    }
}

static void server_on_close_callback(
    endpoint* conn_info,
    int code,
    const char* reason,
    int reason_len,
    void* userdata
)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    if (serv->callbacks.on_close_callback != NULL)
    {
        serv->callbacks.on_close_callback(conn, code, reason, reason_len);
    }

    /* take this client out of the event loop */
    event_delete_io_event(
        serv->loop,
        conn->fd,
        EVENT_READABLE | EVENT_WRITEABLE
    );

    /* close the socket */
    close(conn->fd);

    /* take this client out of the active list */
    INLIST_REMOVE(serv, conn, next, prev, active_head, active_tail);

    /* put it on the end of the free list */
    INLIST_APPEND(serv, conn, next, prev, free_head, free_tail);

    /*
     * if we're stopping, and we were the last connection, tear down the
     * event loop
     */
    if (serv->stopping && serv->active_head == NULL)
    {
        event_stop_loop(serv->loop);
    }
}

static void write_to_client_callback(event_loop* loop, int fd, void* data)
{
    server_conn* conn = data;

    endpoint_write_result r = write_to_endpoint(&conn->endp, fd);
    switch (r)
    {
    case ENDPOINT_WRITE_CONTINUE:
        break;
    case ENDPOINT_WRITE_DONE:
        /*
         * done writing to the client for now, don't call this again until
         * there's more data to be sent
         */
        event_delete_io_event(loop, fd, EVENT_WRITEABLE);
        break;
    case ENDPOINT_WRITE_ERROR:
    case ENDPOINT_WRITE_CLOSED:
        /* proper callback will have been called */
        break;
    }
}

static BOOL server_on_open_callback(
    endpoint* conn_info,
    protocol_conn* proto_conn,
    void* userdata
)
{
    hhunused(conn_info);
    hhunused(proto_conn);

    server_conn* conn = userdata;
    server* serv = conn->serv;
    int* extensions_out = NULL;
    const char** extensions = NULL;

    /* initialize output params */
    int subprotocol_out = -1;
    int num_extensions = server_get_num_client_extensions(conn);
    if (num_extensions > 0)
    {
        extensions_out = hhmalloc(num_extensions * sizeof(*extensions_out));
        for (int i = 0; i < num_extensions; i++)
        {
            extensions_out[i] = -1;
        }
    }

    /*
     * allow users of this interface to reject or pick
     * subprotocols/extensions
     */
    if (serv->callbacks.on_open_callback != NULL &&
        !serv->callbacks.on_open_callback(
            conn,
            &subprotocol_out,
            extensions_out
        )
    )
    {
        goto reject_client;
    }

    char* subprotocol = NULL;
    if (subprotocol_out >= 0)
    {
        server_get_client_subprotocol(
            conn,
            subprotocol_out
        );
    }

    if (extensions_out != NULL && extensions_out[0] >= 0)
    {
        /* +1 for terminating NULL */
        extensions = hhmalloc((num_extensions + 1) * sizeof(*extensions));
        int e = 0;
        for (e = 0; e < num_extensions; e++)
        {
            if (extensions_out[e] < 0) break;
            extensions[e] = (char*)server_get_client_extension(
                conn,
                extensions_out[e]
            );
        }
        extensions[e] = NULL;
    }

    endpoint_result r = endpoint_send_handshake_response(
            &conn->endp,
            subprotocol,
            extensions
    );
    if (r != ENDPOINT_RESULT_SUCCESS)
    {
        goto reject_client;
    }

    if (extensions_out != NULL)
    {
        hhfree(extensions_out);
        extensions_out = NULL;
    }

    if (subprotocol != NULL)
    {
        hhfree(subprotocol);
        subprotocol = NULL;
    }

    if (extensions != NULL)
    {
        hhfree(extensions);
        extensions = NULL;
    }

    event_result er = queue_write(conn);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "write_handshake event loop error: %d",
            er
        );
        goto reject_client;
    }

    return TRUE;

reject_client:
    if (extensions_out != NULL) hhfree(extensions_out);
    if (subprotocol != NULL) hhfree(subprotocol);
    if (extensions != NULL) hhfree(extensions);
    return FALSE;
}

static void server_on_message_callback(
    endpoint* conn_info,
    endpoint_msg* msg,
    void* userdata
)
{
    hhunused(conn_info);

    server_conn* conn = userdata;
    server* serv = conn->serv;

    if (serv->callbacks.on_message_callback != NULL)
    {
        serv->callbacks.on_message_callback(conn, msg);
    }
}

static void read_from_client_callback(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    server_conn* conn = data;

    event_result er;
    endpoint_read_result r = read_from_endpoint(&conn->endp, fd);
    switch (r)
    {
    case ENDPOINT_READ_SUCCESS:
        break;
    case ENDPOINT_READ_SUCCESS_WROTE_DATA:
        er = queue_write(conn);
        if (er != EVENT_RESULT_SUCCESS)
        {
            hhlog_log(
                HHLOG_LEVEL_ERROR,
                "send_msg event loop error: %d",
                er
            );
        }
        break;
    case ENDPOINT_READ_ERROR:
    case ENDPOINT_READ_CLOSED:
        /*
         * errors will have been logged and handled properly by on_close
         * callback
         */
        event_delete_io_event(loop, fd, EVENT_READABLE);
        break;
    }
}

static void accept_callback(event_loop* loop, int fd, void* data)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr*)(&addr), &len);
    server* serv = data;

    if (client_fd == -1)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "-1 fd when accepting socket, fd: %d",
            fd
        );
        return;
    }

    server_conn* conn = activate_conn(serv, client_fd);
    if (conn == NULL)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "Server at max client capacity (%d)",
            serv->options.max_clients
        );
        close(client_fd);
        return;
    }

    event_result r = event_add_io_event(
        loop,
        client_fd,
        EVENT_READABLE,
        read_from_client_callback,
        conn
    );

    if (r != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "add client to event loop error: %d",
            r
        );
        event_delete_io_event(loop, client_fd, EVENT_READABLE);
    }
}

server* server_create(
    config_server_options* options,
    server_callbacks* callbacks
)
{
    int i = 0;
    server* serv = hhmalloc(sizeof(*serv));
    if (serv == NULL) return NULL;

    serv->stopping = FALSE;
    serv->active_head = NULL;
    serv->active_tail = NULL;
    serv->free_head = NULL;
    serv->free_tail = NULL;
    serv->callbacks = *callbacks;
    serv->options = *options;
    int max_clients = options->max_clients;
    serv->connections = hhmalloc(max_clients * sizeof(server_conn));
    if (serv->connections == NULL) goto err_create;

    serv->loop = event_create_loop(options->max_clients + 1024);
    if (serv->loop == NULL) goto err_create;

    /*
     * all available connection objects are 'free', initialize each and add it
     * to the free list
     */
    for (i = 0; i < max_clients; i++)
    {
        server_conn* conn = &serv->connections[i];
        INLIST_APPEND(serv, conn,  next, prev, free_head, free_tail);
        if (init_conn(conn, serv) < 0)
        {
            i++; /* need to do this so loop in err_create works correctly */
            goto err_create;
        }
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
    if (serv->loop != NULL) event_destroy_loop(serv->loop);
    hhfree(serv);
    return NULL;
}

void server_destroy(server* serv)
{
    event_destroy_loop(serv->loop);
    int max_clients = serv->options.max_clients;
    for (int i = 0; i < max_clients; i++)
    {
        deinit_conn(&serv->connections[i]);
    }

    hhfree(serv->connections);
    hhfree(serv);
}

static void server_teardown(server* serv)
{
    /* stop listening for new connections */
    event_delete_io_event(
        serv->loop,
        serv->fd,
        EVENT_READABLE | EVENT_WRITEABLE
    );

    /* stop accepting connections to the server */
    close(serv->fd);

    /*
     * if we don't currently have any client connections, we're pretty
     * much done
     */
    if (serv->active_head == NULL)
    {
        event_stop_loop(serv->loop);
        return;
    }

    /* close each connection */
    INLIST_FOREACH(
        serv,
        server_conn,
        conn,
        next,
        prev,
        active_head,
        active_tail
    )
    {
        const char msg[] = "server shutting down";
        server_conn_close(conn, HH_ERROR_GOING_AWAY, msg, sizeof(msg)-1);
    }
}

/* check if we've been asked to stop, and stop */
static void stop_watchdog(event_loop* loop, event_time_id id, void* data)
{
    server* serv = data;
    if (serv->stopping)
    {
        /* we aren't needed any more */
        event_delete_time_event(loop, id);

        /* tear down everything else */
        server_teardown(serv);
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

/* queue up a message to send on this connection */
server_result server_conn_send_msg(server_conn* conn, endpoint_msg* msg)
{
    endpoint_result r = endpoint_send_msg(&conn->endp, msg);

    event_result er = queue_write(conn);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "send_msg event loop error: %d",
            er
        );
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/* send a ping with payload (NULL for no payload)*/
server_result server_conn_send_ping(
    server_conn* conn,
    char* payload,
    int payload_len
)
{
    endpoint_result r = endpoint_send_ping(&conn->endp, payload, payload_len);

    event_result er = queue_write(conn);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "send_ping event loop error: %d",
            er
        );
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/* send a pong with payload (NULL for no payload)*/
server_result server_conn_send_pong(
    server_conn* conn,
    char* payload,
    int payload_len
)
{
    endpoint_result r = endpoint_send_pong(&conn->endp, payload, payload_len);

    event_result er = queue_write(conn);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "send_pong event loop error: %d",
            er
        );
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
server_result server_conn_close(
    server_conn* conn,
    uint16_t code,
    const char* reason,
    int reason_len
)
{
    endpoint_result r = endpoint_close(&conn->endp, code, reason, reason_len);

    event_result er = queue_write(conn);
    if (er != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "close event loop error: %d",
            er
        );
        return SERVER_RESULT_FAIL;
    }

    return endpoint_result_to_server_result(r);
}

/*
 * get number of subprotocols the client reported they support
 */
int server_get_num_client_subprotocols(server_conn* conn)
{
    return protocol_get_num_subprotocols(&conn->endp.pconn);
}

/*
 * get a subprotocol the client reported they support
 */
const char* server_get_client_subprotocol(server_conn* conn, int index)
{
    return protocol_get_subprotocol(&conn->endp.pconn, index);
}

/*
 * get number of extensions the client reported they support
 */
int server_get_num_client_extensions(server_conn* conn)
{
    return protocol_get_num_extensions(&conn->endp.pconn);
}

/*
 * get an extension the client reported they support
 */
const char* server_get_client_extension(server_conn* conn, int index)
{
    return protocol_get_extension(&conn->endp.pconn, index);
}

/*
 * stop the server, close all connections. will cause server_listen
 * to return eventually. safe to call from a signal handler
 */
void server_stop(server* serv)
{
    serv->stopping = TRUE;
}

/*
 * blocks indefinitely listening to connections from clients
 */
server_result server_listen(server* serv)
{
    config_server_options* opt = &serv->options;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "failed to create socket: %s",
            strerror(errno)
        );
        return SERVER_RESULT_FAIL;
    }
    serv->fd = s;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opt->port);
    addr.sin_addr.s_addr = hh_htonl(INADDR_ANY);
    if (opt->bindaddr != NULL &&
        inet_aton(opt->bindaddr, &addr.sin_addr) != 0)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "invalid bind address: %s",
            opt->bindaddr
        );
        return SERVER_RESULT_FAIL;
    }

    if (bind(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "failed to bind socket: %s",
            strerror(errno)
        );
        return SERVER_RESULT_FAIL;
    }

    if (listen(s, SERVER_LISTEN_BACKLOG) == -1)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "failed to listen on socket: %s",
            strerror(errno)
        );
        return SERVER_RESULT_FAIL;
    }

    event_result r = event_add_io_event(
        serv->loop,
        s,
        EVENT_READABLE,
        accept_callback,
        serv
    );

    if (r != EVENT_RESULT_SUCCESS)
    {
        hhlog_log(
            HHLOG_LEVEL_ERROR,
            "error adding accept callback to event loop: %d",
            r
        );
        return SERVER_RESULT_FAIL;
    }

    /*
     * add watchdog so we know when we're being asked to shut down
     * the server
     */
    serv->watchdog_id = event_add_time_event(
        serv->loop,
        stop_watchdog,
        SERVER_WATCHDOG_FREQ_MS,
        serv
    );

    /* block and process all connections */
    event_pump_events(serv->loop, 0);

    return SERVER_RESULT_SUCCESS;
}

