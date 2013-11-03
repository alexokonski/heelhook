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
#include "event.h"
#include "inlist.h"
#include "hhassert.h"
#include "hhmemory.h"
#include "protocol.h"
#include "server.h"
#include "util.h"

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
#define SERVER_MAX_WRITE_LENGTH (1024 * 64)
#define SERVER_LISTEN_BACKLOG 512

struct server_conn
{
    int fd;
    BOOL close_received;
    BOOL close_sent;
    BOOL close_send_pending;
    BOOL should_fail;
    size_t write_pos;
    size_t read_pos;
    protocol_conn pconn;
    server* serv;
    server_conn* next;
    server_conn* prev;
};

struct server
{
    int stopping;
    int fd;
    server_conn* connections;
    server_conn* active_head;
    server_conn* active_tail;
    server_conn* free_head;
    server_conn* free_tail;
    event_loop* loop;
    server_callbacks callbacks;
    config_options options;
};

static server_result server_conn_send_pmsg(
    server_conn* conn,
    protocol_msg* pmsg
);

static void server_log(
    config_options* options,
    int level,
    const char* format,
    ...
)
{
    if (level < options->loglevel)
    {
        return;
    }

    FILE* fp = stdout;
    if (options->logfilepath != NULL)
    {
        fp = fopen(options->logfilepath, "a");
        if (fp == NULL) return;
    }

    char buffer[SERVER_MAX_LOG_LENGTH];
    va_list list;

    va_start(list, format);
    vsnprintf(buffer, sizeof(buffer), format, list);
    va_end(list);

    struct timeval tv;
    char time_buffer[64];
    gettimeofday(&tv, NULL);
    int num_written = strftime(
        time_buffer,
        sizeof(time_buffer),
        "%d %b %H:%M:%S.",
        localtime(&tv.tv_sec)
    );
    snprintf(
        time_buffer + num_written,
        sizeof(time_buffer) - num_written,
        "%03d",
        (int)tv.tv_usec / 1000
    );

    char* level_str = NULL;
    switch(level)
    {
        case CONFIG_LOG_LEVEL_DEBUG:
            level_str = CONFIG_LOG_LEVEL_DEBUG_STR;
            break;

        case CONFIG_LOG_LEVEL_WARN:
            level_str = CONFIG_LOG_LEVEL_WARN_STR;
            break;

        case CONFIG_LOG_LEVEL_ERROR:
            level_str = CONFIG_LOG_LEVEL_ERROR_STR;
            break;

        default:
            break;
    }

    if (level_str != NULL)
    {
        fprintf(fp, "%s %s %s\n", time_buffer, level_str, buffer);
    }
    else
    {
        fprintf(fp, "%s %d %s\n", time_buffer, level, buffer);
    }

    fflush(fp);
    if (options->logfilepath != NULL) fclose(fp);
}

static int init_conn(server_conn* conn, server* serv)
{
    int r = protocol_init_conn(
        &conn->pconn,
        &serv->options.conn_settings,
        serv->options.protocol_buf_init_len
    );
    conn->serv = serv;
    conn->write_pos = 0;
    conn->read_pos = 0;
    conn->close_received = FALSE;
    conn->close_sent = FALSE;
    conn->close_send_pending = FALSE;
    conn->should_fail = FALSE;

    /*return 0;*/
    return r;
}

static void deinit_conn(server_conn* conn)
{
    protocol_deinit_conn(&conn->pconn);
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
    conn->write_pos = 0;
    conn->read_pos = 0;
    conn->close_received = FALSE;
    conn->close_sent = FALSE;
    conn->close_send_pending = FALSE;
    conn->should_fail = FALSE;

    /*protocol_init_conn(
        &conn->pconn,
        &serv->options.conn_settings,
        serv->options.protocol_buf_init_len
    );*/

    protocol_reset_conn(&conn->pconn);

    return conn;
}

static void deactivate_conn(server_conn* conn)
{
    server* serv = conn->serv;

    if (serv->callbacks.on_close_callback != NULL)
    {
        serv->callbacks.on_close_callback(
            conn,
            conn->pconn.error_code,
            conn->pconn.error_msg,
            conn->pconn.error_len
        );
    }

    /* take this client out of the event loop */
    event_delete_io_event(
        serv->loop,
        conn->fd,
        EVENT_READABLE | EVENT_WRITEABLE
    );

    /* close the socket */
    close(conn->fd);

    /*protocol_deinit_conn(&conn->pconn);*/

    /* take this client out of the active list */
    INLIST_REMOVE(serv, conn, next, prev, active_head, active_tail);

    /* put it on the end of the free list */
    INLIST_APPEND(serv, conn, next, prev, free_head, free_tail);
}

static void write_to_client_callback(event_loop* loop, int fd, void* data)
{
    server_conn* conn = data;
    protocol_conn* pconn = &conn->pconn;

    char* out_buf = darray_get_data(pconn->write_buffer);
    size_t buf_len = darray_get_len(pconn->write_buffer);
    ssize_t num_written = 0;
    ssize_t total_written = 0;

    while (conn->write_pos < buf_len)
    {
        num_written = write(
            fd,
            &out_buf[conn->write_pos],
            buf_len - conn->write_pos
        );
        if (num_written <= 0) break;

        if (num_written > 0)
        {
            /*server_log(
                &conn->serv->options,
                CONFIG_LOG_LEVEL_DEBUG,
                "WROTE %lu bytes",
                buf_len - conn->write_pos
            );*/
        }
        conn->write_pos += num_written;
        total_written += num_written;

        /* don't want to block for too long here writing stuff... */
        if (total_written >= SERVER_MAX_WRITE_LENGTH) break;
    }

    if (num_written == -1)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "Error writing to client. fd: %d, error: %s",
            fd,
            strerror(errno)
        );
        deactivate_conn(conn);
    }
    else if (conn->write_pos == buf_len)
    {
        if (conn->close_send_pending) conn->close_sent = TRUE;

        /* if (conn->close_received && conn->close_sent) */
        if (conn->close_sent && (conn->should_fail || conn->close_received))
        {
            /*
             * if we just sent a close message and we've received one already
             * the websocket connection is fully closed and we're done
             */
            deactivate_conn(conn);
        }
        else
        {
            /*
             * we've sent everything on our write buffer, clean up
             * and get rid of the write callback
             */
            darray_clear(pconn->write_buffer);
            conn->write_pos = 0;
            event_delete_io_event(loop, fd, EVENT_WRITEABLE);

            /*
             * if we're in the write handshake state, we just wrote
             * the handshake and we're now connected
             */
            if (conn->pconn.state == PROTOCOL_STATE_WRITE_HANDSHAKE)
            {
                conn->pconn.state = PROTOCOL_STATE_CONNECTED;
            }
        }
    }

    if (pconn->settings->read_max_msg_size > 0 &&
        darray_get_len(pconn->write_buffer) >
        (uint64_t)pconn->settings->read_max_msg_size)
    {
        /*
         * don't let the write buffer get above the max the read buffer len
         */
        darray_slice(pconn->write_buffer, conn->write_pos, -1);
        conn->write_pos = 0;
    }
}

static void parse_client_messages(server_conn* conn)
{
    protocol_result r;
    protocol_msg msg;
    server_msg smsg;
    do
    {
        r = protocol_read_msg(&conn->pconn, &conn->read_pos, &msg);

        BOOL is_text = FALSE;
        switch (r)
        {
        case PROTOCOL_RESULT_MESSAGE_FINISHED:
            switch (msg.type)
            {
            case PROTOCOL_MSG_NONE:
                hhassert(0);
                break;
            case PROTOCOL_MSG_TEXT:
                is_text = TRUE; /* fallthru */
            case PROTOCOL_MSG_BINARY:
                smsg.data = msg.data;
                smsg.msg_len = msg.msg_len;
                smsg.is_text = is_text;
                if (conn->serv->callbacks.on_message_callback != NULL)
                {
                    conn->serv->callbacks.on_message_callback(
                        conn,
                        &smsg
                    );
                }
                break;
            case PROTOCOL_MSG_CLOSE:
                if (conn->close_sent)
                {
                    deactivate_conn(conn);
                    return;
                }
                else
                {
                    if (msg.msg_len >= (int)sizeof(uint16_t))
                    {
                        uint16_t code = *((uint16_t*)msg.data);
                        conn->pconn.error_code = hh_ntohs(code);

                        conn->pconn.error_msg =
                            (msg.data != NULL)
                                ? (msg.data + sizeof(uint16_t))
                                : NULL;
                        conn->pconn.error_len =
                            msg.msg_len - sizeof(uint16_t);
                    }
                    server_conn_send_pmsg(conn, &msg);
                    conn->close_send_pending = TRUE;
                    conn->close_received = TRUE;
                }
                break;
            case PROTOCOL_MSG_PING:
                if (conn->serv->callbacks.on_ping_callback != NULL)
                {
                    conn->serv->callbacks.on_ping_callback(
                        conn,
                        msg.data,
                        msg.msg_len
                    );
                }
                msg.type = PROTOCOL_MSG_PONG;
                server_conn_send_pmsg(conn, &msg);
                break;
            case PROTOCOL_MSG_PONG:
                break;
            }
            break;
        case PROTOCOL_RESULT_CONTINUE:
        case PROTOCOL_RESULT_FRAME_FINISHED:
            /* we're still reading this message */
            break;
        case PROTOCOL_RESULT_FAIL:
            if (!conn->close_send_pending)
            {
                conn->should_fail = TRUE;
                server_conn_close(
                    conn,
                    conn->pconn.error_code,
                    conn->pconn.error_msg,
                    conn->pconn.error_len
                );
                return;
            }
            break;
        }
    } while (r != PROTOCOL_RESULT_CONTINUE &&
             conn->read_pos < darray_get_len(conn->pconn.read_buffer));

    /*
     * we just finished parsing a data message.  we don't need the data
     * for it anymore, so slice it off the buffer
     */
    if (r == PROTOCOL_RESULT_MESSAGE_FINISHED && protocol_is_data(msg.type))
    {
        darray_slice(conn->pconn.read_buffer, conn->read_pos, -1);
        conn->read_pos = darray_get_len(conn->pconn.read_buffer);
    }
}

static void read_client_handshake(server_conn* conn)
{
    protocol_handshake_result hr = protocol_read_handshake(&conn->pconn);
    server* serv = conn->serv;

    switch (hr)
    {
    case PROTOCOL_HANDSHAKE_SUCCESS:
        break;
    case PROTOCOL_HANDSHAKE_CONTINUE:
        /*
         * apparently we didn't read the whole thing...
         * keep waiting
         */
        return;
    case PROTOCOL_HANDSHAKE_FAIL:
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "Read invalid handshake. fd: %d",
            conn->fd
        );
        deactivate_conn(conn);
        return;
    default:
        hhassert(0);
        return;
    }

    if (serv->callbacks.on_open_callback != NULL &&
        !serv->callbacks.on_open_callback(conn))
    {
        deactivate_conn(conn);
        return;
    }

    /* TODO: allow server to provide subprotocols/extensions desired... */
    if ((hr = protocol_write_handshake(&conn->pconn, NULL, NULL)) !=
            PROTOCOL_HANDSHAKE_SUCCESS)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "Error writing handshake. fd: %d, err: %d",
            conn->fd,
            hr
        );
        deactivate_conn(conn);
        return;
    }

    /* queue up writing the handshake response back */
    event_result er = event_add_io_event(
        conn->serv->loop,
        conn->fd,
        EVENT_WRITEABLE,
        write_to_client_callback,
        conn
    );

    if (er != EVENT_RESULT_SUCCESS)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "write_handshake event loop error: %d!",
            er
        );
        deactivate_conn(conn);
        return;
    }
}

static void read_from_client_callback(event_loop* loop, int fd, void* data)
{
    hhunused(loop);

    server_conn* conn = data;
    protocol_conn* pconn = &conn->pconn;

    /*
     * we're trying to close the connection and we already heard the client
     * agrees... don't read anything more
     */
    /*if (conn->close_send_pending && conn->close_received)*/
    if (conn->close_received)
    {
        return;
    }

    /*
     * figure out which buffer to read into.  We have a separate buffer for
     * handshake info we want to keep around for the duration of the connection
     */
    size_t read_len;
    darray** read_buffer;
    if (pconn->state == PROTOCOL_STATE_READ_HANDSHAKE)
    {
        read_len = SERVER_HEADER_READ_LENGTH;
        read_buffer = &pconn->info.buffer;
    }
    else
    {
        read_len = pconn->settings->read_max_msg_size;
        read_buffer = &pconn->read_buffer;
    }

    ssize_t num_read = 0;
    int end_pos = darray_get_len((*read_buffer));

    darray_ensure(read_buffer, read_len);
    char* buf = darray_get_data((*read_buffer));
    buf = &buf[end_pos];

    num_read = read(fd, buf, read_len);

    if (num_read == -1)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "Error reading from client. fd: %d, error: %s",
            fd,
            strerror(errno)
        );
        deactivate_conn(conn);
        return;
    }
    else if (num_read == 0)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "Client closed connection. fd: %d",
            fd
        );
        deactivate_conn(conn);
        return;
    }
    /* else ... */

    darray_add_len((*read_buffer), num_read);

    switch (pconn->state)
    {
    case PROTOCOL_STATE_READ_HANDSHAKE:
        read_client_handshake(conn);
        break;
    case PROTOCOL_STATE_WRITE_HANDSHAKE:
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_WARN,
            "client tried to send something before handshake was written: %d",
            fd
        );
        deactivate_conn(conn);
        break;
    case PROTOCOL_STATE_CONNECTED:
        /*server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_DEBUG,
            "read %lu msg bytes",
            num_read
        );*/
        parse_client_messages(conn);
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
        server_log(
            &serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "-1 fd when accepting socket, fd: %d",
            fd
        );
        return;
    }

    server_conn* conn = activate_conn(serv, client_fd);
    if (conn == NULL)
    {
        server_log(
            &serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "Server at max client capacity (%d)",
            serv->options.max_clients
        );
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
        server_log(
            &serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "add client to event loop error: %d",
            r
        );
        event_delete_io_event(loop, client_fd, EVENT_READABLE);
    }
}

server* server_create(config_options* options, server_callbacks* callbacks)
{
    int i = 0;
    server* serv = hhmalloc(sizeof(*serv));
    if (serv == NULL) return NULL;

    serv->stopping = 0;
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

static server_result server_conn_send_pmsg(
    server_conn* conn,
    protocol_msg* pmsg
)
{
    if (conn->close_send_pending)
    {
        return SERVER_RESULT_SUCCESS;
    }

    protocol_result pr;
    if ((pr = protocol_write_msg(&conn->pconn, pmsg)) !=
            PROTOCOL_RESULT_MESSAGE_FINISHED)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "protocol_write_msg error: %d",
            pr
        );
        return SERVER_RESULT_FAIL;
    }

    event_result r = event_add_io_event(
        conn->serv->loop,
        conn->fd,
        EVENT_WRITEABLE,
        write_to_client_callback,
        conn
    );

    if(r != EVENT_RESULT_SUCCESS)
    {
        server_log(
            &conn->serv->options,
            CONFIG_LOG_LEVEL_ERROR,
            "send_msg event loop error: %d",
            r
        );
        return SERVER_RESULT_FAIL;
    }

    return SERVER_RESULT_SUCCESS;
}

/* queue up a message to send on this connection */
server_result server_conn_send_msg(server_conn* conn, server_msg* msg)
{
    protocol_msg pmsg;
    pmsg.data = msg->data;
    pmsg.msg_len = msg->msg_len;
    pmsg.type = (msg->is_text) ? PROTOCOL_MSG_TEXT : PROTOCOL_MSG_BINARY;

    return server_conn_send_pmsg(conn, &pmsg);
}

/* send a ping with payload (NULL for no payload)*/
server_result server_conn_send_ping(
    server_conn* conn,
    char* payload,
    int64_t payload_len
)
{
    protocol_msg pmsg;
    pmsg.data = payload;
    pmsg.msg_len = payload_len;
    pmsg.type = PROTOCOL_MSG_PING;

    return server_conn_send_pmsg(conn, &pmsg);
}

/* send a pong with payload (NULL for no payload)*/
server_result server_conn_send_pong(
    server_conn* conn,
    char* payload,
    int64_t payload_len
)
{
    protocol_msg pmsg;
    pmsg.data = payload;
    pmsg.msg_len = payload_len;
    pmsg.type = PROTOCOL_MSG_PONG;

    return server_conn_send_pmsg(conn, &pmsg);
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
    protocol_msg msg;
    size_t data_len = 0;
    char* orig_data = NULL;
    char* data = NULL;
    uint16_t* code_data = NULL;
    server_result r = SERVER_RESULT_SUCCESS;

    switch (conn->pconn.state)
    {
    case PROTOCOL_STATE_READ_HANDSHAKE:
    case PROTOCOL_STATE_WRITE_HANDSHAKE:
        deactivate_conn(conn);
        return r;

    case PROTOCOL_STATE_CONNECTED:
        data_len = sizeof(code) + reason_len;
        orig_data = hhmalloc(data_len);
        data = orig_data;
        code_data = (uint16_t*)data;
        *code_data = hh_htons(code);
        data += sizeof(code);

        /* purposely leave out terminator */
        memcpy(data, reason, reason_len);

        msg.type = PROTOCOL_MSG_CLOSE;
        msg.data = orig_data;
        msg.msg_len = data_len;
        r = server_conn_send_pmsg(conn, &msg);
        conn->close_send_pending = TRUE;

        hhfree(orig_data);
        break;
    }

    return r;
}

/*
 * get number of subprotocols the client reported they support
 */
int server_get_num_client_subprotocols(server_conn* conn)
{
    return protocol_get_num_subprotocols(&conn->pconn);
}

/*
 * stop the server, close all connections. will cause server_listen
 * to return eventually
 */
const char* server_get_client_subprotocol(server_conn* conn, int index)
{
    return protocol_get_subprotocol(&conn->pconn, index);
}

void server_stop(server* serv)
{
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
    serv->stopping = 1;
}

server_result server_listen(server* serv)
{
    config_options* opt = &serv->options;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1)
    {
        server_log(
            opt,
            CONFIG_LOG_LEVEL_ERROR,
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
        server_log(
            opt,
            CONFIG_LOG_LEVEL_ERROR,
            "invalid bind address: %s",
            opt->bindaddr
        );
        return SERVER_RESULT_FAIL;
    }

    if (bind(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
    {
        server_log(
            opt,
            CONFIG_LOG_LEVEL_ERROR,
            "failed to bind socket: %s",
            strerror(errno)
        );
        return SERVER_RESULT_FAIL;
    }

    if (listen(s, SERVER_LISTEN_BACKLOG) == -1)
    {
        server_log(
            opt,
            CONFIG_LOG_LEVEL_ERROR,
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
        server_log(
            opt,
            CONFIG_LOG_LEVEL_ERROR,
            "error adding accept callback to event loop: %d",
            r
        );
        return SERVER_RESULT_FAIL;
    }

    event_pump_events(serv->loop, 0);

    /* take the server out of the event loop */
    event_delete_io_event(
        serv->loop,
        s,
        EVENT_READABLE | EVENT_WRITEABLE
    );

    return SERVER_RESULT_SUCCESS;
}

