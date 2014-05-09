/* endpoint - handles client connections and sending/receiving data
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
#include "hhassert.h"
#include "hhlog.h"
#include "hhmemory.h"
#include "protocol.h"
#include "endpoint.h"

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

#define ENDPOINT_MAX_READ_LENGTH (1024 * 64)
#define ENDPOINT_MAX_WRITE_LENGTH (1024 * 64)

static endpoint_result endpoint_send_pmsg(endpoint* conn, protocol_msg* pmsg);

static void deactivate_conn(endpoint* conn)
{
    if (conn->callbacks->on_close != NULL)
    {
        conn->callbacks->on_close(conn, conn->pconn.error_code,
                                  conn->pconn.error_msg,
                                  conn->pconn.error_len,
                                  conn->userdata);
    }
}

endpoint_write_result endpoint_write(endpoint* conn, int fd)
{
    endpoint_write_result result = ENDPOINT_WRITE_CONTINUE;
    protocol_conn* pconn = &conn->pconn;

    char* out_buf = darray_get_data(pconn->write_buffer);
    size_t buf_len = darray_get_len(pconn->write_buffer);
    ssize_t num_written = 0;
    ssize_t total_written = 0;

    while (conn->write_pos < buf_len)
    {
        size_t len = buf_len - conn->write_pos;
        num_written = write(fd, &out_buf[conn->write_pos], len);
        if (num_written <= 0) break;

        if (num_written > 0)
        {
            /*printf("WRITTEN: %.*s\n", (int)len, &out_buf[conn->write_pos]);
            hhlog(HHLOG_LEVEL_DEBUG, "WROTE %lu bytes", buf_len -
                        conn->write_pos);*/
        }
        conn->write_pos += (size_t)num_written;
        total_written += num_written;

        /* don't want to block for too long here writing stuff... */
        if (total_written >= ENDPOINT_MAX_WRITE_LENGTH) break;
    }

    if (num_written == -1)
    {
        hhlog(HHLOG_LEVEL_WARNING,
              "Error writing to endpoint. fd: %d, error: %s", fd,
              strerror(errno));
        deactivate_conn(conn);
        return ENDPOINT_WRITE_ERROR;
    }
    else if (conn->write_pos == buf_len)
    {
        if (conn->close_send_pending) conn->close_sent = true;

        /* if (conn->close_received && conn->close_sent) */
        if (conn->close_sent && (conn->should_fail || conn->close_received))
        {
            /*
             * if we just sent a close message and we've received one already
             * the websocket connection is fully closed and we're done
             */
            deactivate_conn(conn);
            return ENDPOINT_WRITE_CLOSED;
        }
        else
        {
            /*
             * we've sent everything on our write buffer, clean up
             * and get rid of the write callback
             */
            darray_clear(pconn->write_buffer);
            conn->write_pos = 0;
            result = ENDPOINT_WRITE_DONE;
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

    return result;
}

typedef enum
{
    PARSE_CONTINUE,
    PARSE_CONTINUE_WROTE_DATA,
    PARSE_CLOSE
} parse_result;

static endpoint_read_result
parse_result_to_endpoint_read_result(parse_result pr)
{
    switch (pr)
    {
    case PARSE_CONTINUE:
        return ENDPOINT_READ_SUCCESS;
    case PARSE_CONTINUE_WROTE_DATA:
        return ENDPOINT_READ_SUCCESS_WROTE_DATA;
    case PARSE_CLOSE:
        return ENDPOINT_READ_CLOSED;
    }

    hhassert(0);
    return ENDPOINT_READ_ERROR;
}

static parse_result parse_endpoint_messages(endpoint* conn)
{
    protocol_result r = PROTOCOL_RESULT_MESSAGE_FINISHED;
    parse_result pr = PARSE_CONTINUE;
    protocol_msg msg;
    endpoint_msg smsg;
    do
    {
        switch (conn->type)
        {
        case ENDPOINT_CLIENT:
            r = protocol_read_server_msg(&conn->pconn, &conn->read_pos, &msg);
            break;
        case ENDPOINT_SERVER:
            r = protocol_read_client_msg(&conn->pconn, &conn->read_pos, &msg);
            break;
        }

        bool is_text = false;
        switch (r)
        {
        case PROTOCOL_RESULT_MESSAGE_FINISHED:
            switch (msg.type)
            {
            case PROTOCOL_MSG_NONE:
                hhassert(0);
                break;
            case PROTOCOL_MSG_TEXT:
                is_text = true; /* fallthru */
            case PROTOCOL_MSG_BINARY:
                smsg.data = msg.data;
                smsg.msg_len = msg.msg_len;
                smsg.is_text = is_text;
                if (conn->callbacks->on_message != NULL)
                {
                    conn->callbacks->on_message(conn, &smsg,
                                                conn->userdata);
                }
                break;
            case PROTOCOL_MSG_CLOSE:
                if (msg.msg_len >= (int)sizeof(uint16_t))
                {
                    uint16_t code;
                    memcpy(&code, msg.data, sizeof(code));
                    conn->pconn.error_code = hh_ntohs(code);

                    conn->pconn.error_msg =
                        (msg.msg_len > (int)sizeof(uint16_t))
                            ? (msg.data + sizeof(uint16_t))
                            : NULL;
                    conn->pconn.error_len =
                        (int)msg.msg_len - (int)sizeof(uint16_t);
                }

                if (conn->close_sent)
                {
                    deactivate_conn(conn);
                    return PARSE_CLOSE;
                }
                else
                {
                    endpoint_send_pmsg(conn, &msg);
                    pr = PARSE_CONTINUE_WROTE_DATA;
                    conn->close_send_pending = true;
                    conn->close_received = true;
                }
                break;
            case PROTOCOL_MSG_PING:
                if (conn->callbacks->on_ping != NULL)
                {
                    conn->callbacks->on_ping(conn, msg.data, (int)msg.msg_len,
                                             conn->userdata);
                }
                msg.type = PROTOCOL_MSG_PONG;
                endpoint_send_pmsg(conn, &msg);
                pr = PARSE_CONTINUE_WROTE_DATA;
                break;
            case PROTOCOL_MSG_PONG:
                if (conn->callbacks->on_pong != NULL)
                {
                    conn->callbacks->on_pong(conn, msg.data, (int)msg.msg_len,
                                             conn->userdata);
                }
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
                conn->should_fail = true;
                endpoint_close(conn, conn->pconn.error_code,
                               conn->pconn.error_msg, conn->pconn.error_len);
            }
            return PARSE_CONTINUE_WROTE_DATA;
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

    return pr;
}

static endpoint_read_result read_handshake(endpoint* conn, int fd)
{
    protocol_handshake_result hr = PROTOCOL_HANDSHAKE_FAIL;

    switch (conn->type)
    {
        case ENDPOINT_SERVER:
            hr = protocol_read_handshake_request(&conn->pconn);
            break;
        case ENDPOINT_CLIENT:
            hr = protocol_read_handshake_response(&conn->pconn);
            break;
    }

    switch (hr)
    {
    case PROTOCOL_HANDSHAKE_SUCCESS:
        break;
    case PROTOCOL_HANDSHAKE_CONTINUE:
        /*
         * apparently we didn't read the whole thing...
         * keep waiting
         */
        return ENDPOINT_READ_SUCCESS;
    case PROTOCOL_HANDSHAKE_FAIL:
        hhlog(HHLOG_LEVEL_DEBUG, "Read invalid handshake. fd: %d", fd);
        deactivate_conn(conn);
        return ENDPOINT_READ_ERROR;
    case PROTOCOL_HANDSHAKE_FAIL_TOO_LARGE:
        hhlog(HHLOG_LEVEL_DEBUG, "Read too large handshake. fd: %d", fd);
        deactivate_conn(conn);
        return ENDPOINT_READ_ERROR;
    default:
        hhassert(0);
        deactivate_conn(conn);
        return ENDPOINT_READ_ERROR;
    }

    /*
     * allow users of this interface to reject or pick
     * subprotocols/extensions
     */
    if (conn->callbacks->on_open != NULL &&
        !conn->callbacks->on_open(conn, &conn->pconn, conn->userdata)
    )
    {
        deactivate_conn(conn);
        return ENDPOINT_READ_CLOSED;
    }

    return ENDPOINT_READ_SUCCESS;
}

endpoint_read_result endpoint_read(endpoint* conn, int fd)
{
    protocol_conn* pconn = &conn->pconn;

    /*
     * we're trying to close the connection and we already heard the client
     * agrees... don't read anything more
     */
    /*if (conn->close_send_pending && conn->close_received)*/
    if (conn->close_received)
    {
        return ENDPOINT_READ_CLOSED;
    }

    size_t read_len = hhmin((size_t)pconn->settings->read_max_msg_size,
                            ENDPOINT_MAX_READ_LENGTH);
    char* buf = protocol_prepare_read(pconn, read_len);

    ssize_t num_read = read(fd, buf, read_len);

    if (num_read == -1)
    {
        hhlog(HHLOG_LEVEL_WARNING,
                  "Error reading from endpoint. fd: %d, error: %s",
                  fd, strerror(errno));
        deactivate_conn(conn);
        return ENDPOINT_READ_ERROR;
    }
    else if (num_read == 0)
    {
        hhlog(HHLOG_LEVEL_INFO, "Endpoint closed connection. fd: %d", fd);
        deactivate_conn(conn);
        return ENDPOINT_READ_ERROR;
    }
    /* else ... */

    /*hhlog(HHLOG_LEVEL_DEBUG, "READ %lu bytes: %.*s", num_read,
              (int)num_read, buf);*/

    hhassert(num_read > 0);
    protocol_update_read(pconn, (size_t)num_read);

    endpoint_read_result r = ENDPOINT_READ_SUCCESS;
    parse_result pr = PARSE_CONTINUE;
    switch (pconn->state)
    {
    case PROTOCOL_STATE_READ_HANDSHAKE:
        /* fd used only for logging, message is read off of buffers in conn */
        r = read_handshake(conn, fd);

        /*
         * if the handshake read caused us to become connected, it's possible
         * there are already messages to parse
         */
        if (pconn->state == PROTOCOL_STATE_CONNECTED)
        {
            pr = parse_endpoint_messages(conn);
            r = parse_result_to_endpoint_read_result(pr);
        }
        break;
    case PROTOCOL_STATE_WRITE_HANDSHAKE:
        hhlog(HHLOG_LEVEL_WARNING,
                  "tried to read when we were writing handshake: %d", fd);
        deactivate_conn(conn);
        r = ENDPOINT_READ_ERROR;
        break;
    case PROTOCOL_STATE_CONNECTED:
        /*hhlog(HHLOG_LEVEL_DEBUG, "read %lu msg bytes", num_read);*/
        pr = parse_endpoint_messages(conn);
        r = parse_result_to_endpoint_read_result(pr);
        break;
    }

    return r;
}

static void endpoint_state_clear(endpoint* conn)
{
    conn->write_pos = 0;
    conn->read_pos = 0;
    conn->close_received = false;
    conn->close_sent = false;
    conn->close_send_pending = false;
    conn->should_fail = false;
}

/* reset buffers etc but don't deallocate  */
void endpoint_reset(endpoint* conn)
{
    endpoint_state_clear(conn);
    protocol_reset_conn(&conn->pconn);
}

/* send a handshake reponse (only applies to server endpoints) */
endpoint_result
endpoint_send_handshake_response(
        endpoint* conn,
        const char* protocol, /* (optional) */
        const char** extensions /* NULL terminated (optional) */
)
{
    hhassert(conn->type == ENDPOINT_SERVER);

    protocol_handshake_result hr;
    hr = protocol_write_handshake_response(&conn->pconn, protocol,
                                           extensions);
    if (hr != PROTOCOL_HANDSHAKE_SUCCESS)
    {
       hhlog(HHLOG_LEVEL_ERROR,
                 "Error writing handshake. conn_ptr: %p, err: %d", conn, hr);
        return ENDPOINT_RESULT_FAIL;
    }
    return ENDPOINT_RESULT_SUCCESS;
}

/* send a handshake request (only applies to client endpoints) */
endpoint_result
endpoint_send_handshake_request(
    endpoint* conn,
    const char* resource,
    const char* host,
    const char** protocols, /* NULL terminated (optional) */
    const char** extensions, /* NULL terminated (optional) */
    const char** extra_headers /* NULL terminated, (optional) */
)
{
    hhassert(conn->type == ENDPOINT_CLIENT);

    protocol_handshake_result hr;
    hr = protocol_write_handshake_request(&conn->pconn, resource, host,
                                          protocols, extensions,
                                          extra_headers);

    if (hr != PROTOCOL_HANDSHAKE_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR,
                  "Error writing handshake. conn_ptr: %s, err: %d", conn, hr);
        return ENDPOINT_RESULT_FAIL;
    }
    return ENDPOINT_RESULT_SUCCESS;
}

int endpoint_init(endpoint* conn, endpoint_type type,
                  endpoint_settings* settings,
                  endpoint_callbacks* callbacks, void* userdata)
{
    conn->type = type;
    int r = protocol_init_conn(&conn->pconn, &(settings->conn_settings),
                               settings->protocol_buf_init_len, NULL);
    conn->callbacks = callbacks;
    conn->userdata = userdata;
    endpoint_state_clear(conn);
    return r;
}

void endpoint_deinit(endpoint* conn)
{
    protocol_deinit_conn(&conn->pconn);
}

static endpoint_result endpoint_send_pmsg(endpoint* conn, protocol_msg* pmsg)
{
    if (conn->close_send_pending)
    {
        return ENDPOINT_RESULT_SUCCESS;
    }

    protocol_result pr;

    switch(conn->type)
    {
    case ENDPOINT_SERVER:
        if ((pr = protocol_write_server_msg(&conn->pconn, pmsg)) !=
                PROTOCOL_RESULT_MESSAGE_FINISHED)
        {
            hhlog(HHLOG_LEVEL_ERROR, "protocol_write_server_msg error: %d",
                      pr);
            return ENDPOINT_RESULT_FAIL;
        }
        break;
    case ENDPOINT_CLIENT:
        if ((pr = protocol_write_client_msg(&conn->pconn, pmsg)) !=
                PROTOCOL_RESULT_MESSAGE_FINISHED)
        {
            hhlog(HHLOG_LEVEL_ERROR, "protocol_write_client_msg error: %d",
                      pr);
            return ENDPOINT_RESULT_FAIL;
        }
        break;
    }

    return ENDPOINT_RESULT_SUCCESS;
}

/* queue up a message to send on this connection */
endpoint_result endpoint_send_msg(endpoint* conn, endpoint_msg* msg)
{
    protocol_msg pmsg;
    pmsg.data = msg->data;
    pmsg.msg_len = msg->msg_len;
    pmsg.type = (msg->is_text) ? PROTOCOL_MSG_TEXT : PROTOCOL_MSG_BINARY;

    return endpoint_send_pmsg(conn, &pmsg);
}

/* send a ping with payload (NULL for no payload)*/
endpoint_result
endpoint_send_ping(endpoint* conn, char* payload, int payload_len)
{
    protocol_msg pmsg;
    pmsg.data = payload;
    pmsg.msg_len = payload_len;
    pmsg.type = PROTOCOL_MSG_PING;

    return endpoint_send_pmsg(conn, &pmsg);
}

/* send a pong with payload (NULL for no payload)*/
endpoint_result
endpoint_send_pong(endpoint* conn, char* payload, int payload_len)
{
    protocol_msg pmsg;
    pmsg.data = payload;
    pmsg.msg_len = payload_len;
    pmsg.type = PROTOCOL_MSG_PONG;

    return endpoint_send_pmsg(conn, &pmsg);
}

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
endpoint_result
endpoint_close(endpoint* conn, uint16_t code, const char* reason,
               int reason_len)
{
    protocol_msg msg;
    size_t data_len = 0;
    char* orig_data = NULL;
    char* data = NULL;
    endpoint_result r = ENDPOINT_RESULT_SUCCESS;

    switch (conn->pconn.state)
    {
    case PROTOCOL_STATE_READ_HANDSHAKE:
    case PROTOCOL_STATE_WRITE_HANDSHAKE:
        deactivate_conn(conn);
        return r;

    case PROTOCOL_STATE_CONNECTED:
        hhassert(reason_len >= 0);
        data_len = sizeof(code) + (size_t)reason_len;
        orig_data = hhmalloc(data_len);
        data = orig_data;
        code = hh_htons(code);
        memcpy(data, &code, sizeof(code));
        data += sizeof(code);

        /* purposely leave out terminator */
        memcpy(data, reason, (size_t)reason_len);

        msg.type = PROTOCOL_MSG_CLOSE;
        msg.data = orig_data;
        msg.msg_len = (int64_t)data_len;
        r = endpoint_send_pmsg(conn, &msg);
        conn->close_send_pending = true;

        hhfree(orig_data);
        break;
    }

    return r;
}

