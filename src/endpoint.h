/* endpoint - send/receive data as a client or a server
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

#ifndef __ENDPOINT_H_
#define __ENDPOINT_H_

#include "protocol.h"
#include "util.h"
#include <stdint.h>

typedef enum
{
    ENDPOINT_CLIENT,
    ENDPOINT_SERVER
} endpoint_type;

typedef struct
{
    size_t protocol_buf_init_len; /* initial length for read/write buffers */
    protocol_settings conn_settings; /* settings for a connection */
} endpoint_settings;

typedef struct endpoint_callbacks endpoint_callbacks;

typedef struct
{
    endpoint_type type;
    endpoint_callbacks* callbacks;
    size_t write_pos;
    size_t read_pos;
    protocol_conn pconn;
    bool close_received;
    bool close_sent;
    bool close_send_pending;
    bool should_fail;
    void* userdata;
} endpoint;

typedef enum
{
    ENDPOINT_WRITE_CONTINUE,
    ENDPOINT_WRITE_DONE,
    ENDPOINT_WRITE_ERROR,
    ENDPOINT_WRITE_CLOSED
} endpoint_write_result;

typedef enum
{
    /* read succeeded */
    ENDPOINT_READ_SUCCESS,

    /* this read caused data to be written to the write buffer */
    ENDPOINT_READ_SUCCESS_WROTE_DATA,

    /* error occured during read */
    ENDPOINT_READ_ERROR,

    /* the connect was closed during this read or a previous read*/
    ENDPOINT_READ_CLOSED
} endpoint_read_result;

typedef struct
{
    bool is_text;
    char* data;
    int64_t msg_len;
} endpoint_msg;

typedef bool (endpoint_on_open)(endpoint* conn, protocol_conn* proto_conn,
                                  void* userdata);
typedef void (endpoint_on_message)(endpoint* conn, endpoint_msg* msg,
                                   void* userdata);
typedef void (endpoint_on_ping)(endpoint* conn_info, char* payload,
                                int payload_len, void* userdata);

/* includes the close code and reason received from the client (if any) */
typedef void (endpoint_on_close)(endpoint* conn_info, int code,
                                const char* reason, int reason_len,
                                void* userdata);

typedef enum
{
    ENDPOINT_RESULT_SUCCESS,
    ENDPOINT_RESULT_FAIL
} endpoint_result;

struct endpoint_callbacks
{
    /* 
     * for servers, called when a client has sent his opening handshake.
     * as a server, you must call endpoint_send_handshake_response with
     * the selected protocol/extensions in this callback
     *
     * for clients, called when the server has sent the handshake response 
     */
    endpoint_on_open* on_open_callback;

    /* called when a full message is received from a client */
    endpoint_on_message* on_message_callback;

    /*
     * called when a ping was received.  a pong is always sent for you
     * automatically to conform to the RFC
     */
    endpoint_on_ping* on_ping_callback;

    /* called when connection is about to terminate */
    endpoint_on_close* on_close_callback;
};

/*
 * initialize an instance of a 'endpoint' settings and callbacks must be valid
 * pointers until endpoint_deinit is called.  will initialize the internal
 * protocol_conn
 */
int endpoint_init(endpoint* conn, endpoint_type type,
                  endpoint_settings* settings, endpoint_callbacks* callbacks,
                  void* userdata);

/* deinitialize the endpoint */
void endpoint_deinit(endpoint* conn);

/* reset buffers etc but don't deallocate  */
void endpoint_reset(endpoint* conn);

/* send a handshake reponse (only applies to server endpoints) */
endpoint_result
endpoint_send_handshake_response(
    endpoint* conn,
    const char* protocol, /* (optional) */
    const char** extensions /* NULL terminated (optional) */
);

/* send a handshake request (only applies to client endpoints) */
endpoint_result
endpoint_send_handshake_request(
    endpoint* conn,
    const char* resource,
    const char* host,
    const char** protocols, /* NULL terminated (optional) */
    const char** extensions, /* NULL terminated (optional) */
    const char** extra_headers /* NULL terminated, (optional) */
);

/* queue up a message to send on this connection */
endpoint_result endpoint_send_msg(endpoint* conn, endpoint_msg* msg);

/* send a ping with payload (NULL for no payload)*/
endpoint_result
endpoint_send_ping(endpoint* conn, char* payload, int payload_len);

/* send a pong with payload (NULL for no payload)*/
endpoint_result
endpoint_send_pong(endpoint* conn, char* payload, int payload_len);

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
endpoint_result
endpoint_close(endpoint* conn, uint16_t code, const char* reason,
               int reason_len);

/*
 * write data from endpoint to a ready socket
 */
endpoint_write_result endpoint_write(endpoint* conn, int fd);

/*
 * read data from a ready socket into the endpoint
 */
endpoint_read_result endpoint_read(endpoint* conn, int fd);

#endif /* __ENDPOINT_H_ */

