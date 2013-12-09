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

#ifndef __SERVER_H_
#define __SERVER_H_

#include "config.h"
#include "protocol.h"
#include "util.h"
#include <stdint.h>

typedef struct server server;
typedef struct server_conn server_conn;
typedef struct
{
    BOOL is_text;
    char* data;
    int64_t msg_len;
} server_msg;

/*
 * return false if you want to reject this client
 * 
 * output params:
 *      - index of subprotocol you want to use (index determined by 
 *        server_get_client_subprotocol).  Set -1 or don't write to this to
 *        choose 0 subprotocols
 *
 *      - indices of extensions to use. extensions_out is an array with
 *        length equal to server_get_num_extensions. Fill in starting from
 *        index 0. Set all to -1 or don't write to this to choose 0 extensions
 */
typedef BOOL (server_on_open)(
    server_conn* conn,
    int* subprotocol_out,
    int* extensions_out
);
typedef void (server_on_message)(server_conn* conn, server_msg* msg);
typedef void (server_on_ping)(
    server_conn* conn_info,
    char* payload,
    int64_t payload_len
);

/* includes the close code and reason received from the client (if any) */
typedef void (server_on_close)(
    server_conn* conn_info,
    int code,
    const char* reason,
    int64_t reason_len
);

typedef enum
{
    SERVER_RESULT_SUCCESS,
    SERVER_RESULT_FAIL
} server_result;

typedef struct
{
    /* called when a handshake has just been completed */
    server_on_open* on_open_callback;

    /* called when a full message is received from a client */
    server_on_message* on_message_callback;

    /*
     * called when a ping was received.  a pong is always sent for you
     * automatically to conform to the RFC
     */
    server_on_ping* on_ping_callback;

    /* called when connection is about to terminate */
    server_on_close* on_close_callback;
} server_callbacks;

/*
 * create an instance of a 'server' options and callbacks must be valid
 * pointers until server_stop is called
 */
server* server_create(config_options* options, server_callbacks* callbacks);

/* queue up a message to send on this connection */
server_result server_conn_send_msg(server_conn* conn, server_msg* msg);

/* send a ping with payload (NULL for no payload)*/
server_result server_conn_send_ping(
    server_conn* conn,
    char* payload,
    int64_t payload_len
);

/* send a pong with payload (NULL for no payload)*/
server_result server_conn_send_pong(
    server_conn* conn,
    char* payload,
    int64_t payload_len
);

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
server_result server_conn_close(
    server_conn* conn,
    uint16_t code,
    const char* reason,
    int reason_len
);

/*
 * get number of subprotocols the client reported they support
 */
int server_get_num_client_subprotocols(server_conn* conn);

/*
 * get a subprotocol the client reported they support
 */
const char* server_get_client_subprotocol(server_conn* conn, int index);

/*
 * get number of extensions the client reported they support
 */
int server_get_num_client_extensions(server_conn* conn);

/*
 * get an extension the client reported they support
 */
const char* server_get_client_extension(server_conn* conn, int index);

/*
 * stop the server, close all connections. will cause server_listen
 * to return eventually. safe to call from a signal handler
 */
void server_stop(server* serv);

/*
 * blocks indefinitely listening to connections from clients
 */
server_result server_listen(server* serv);

#endif /* __SERVER_H_ */

