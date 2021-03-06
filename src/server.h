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

#include "endpoint.h"
#include "iloop.h"
#include "config.h"
#include "util.h"
#include <stdint.h>

typedef struct server_conn server_conn;
typedef struct server server;

/* on_connect is called when a client has sent their side of the handshake, but
 * the server has not yet responded
 *
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
typedef bool (server_on_connect)(server_conn* conn, int* subprotocol_out,
                              int* extensions_out, void* userdata);
/*
 * on_open is called right after the server has sent its opening handshake
 * and it's now okay to send and receive normal websocket messages
 */
typedef void (server_on_open)(server_conn* conn, void* userdata);

/* on_message is called when the client sends the server a message */
typedef void (server_on_message)(server_conn* conn, endpoint_msg* msg,
                                 void* userdata);

/*
 * on_ping is called when the client sends the server a ping. If you specify
 * this, you'll have to send a pong yourself to conform to the RFC
 */
typedef void (server_on_ping)(server_conn* conn_info, char* payload,
                              int payload_len, void* userdata);

typedef void (server_on_pong)(server_conn* conn_info, char* payload,
                              int payload_len, void* userdata);
/*
 * on_close is called whenever the server is closing the connection for any
 * reason includes the close code and reason received from the client (if any)
 */
typedef void (server_on_close)(server_conn* conn_info, int code,
                               const char* reason, int reason_len,
                               void* userdata);

/*
 * should_stop will be called periodically, if you return 'true', the server
 * will stop itself (identical to calling server_stop)
 */
typedef bool (server_should_stop)(server* serv, void* userdata);

/*
 * will be called when the iloop for this server should be filled in
 */
typedef bool (server_attach_iloop)(server* serv, iloop* loop,
                                   config_server_options* options,
                                   void* userdata);

typedef enum
{
    SERVER_RESULT_SUCCESS,
    SERVER_RESULT_FAIL
} server_result;

typedef struct
{
    server_on_open* on_open;
    server_on_connect* on_connect;
    server_on_message* on_message;
    server_on_ping* on_ping;
    server_on_pong* on_pong;
    server_on_close* on_close;
    server_should_stop* should_stop;

    /* NULL will cause default loop to be attached */
    server_attach_iloop* attach_loop;
} server_callbacks;

typedef enum
{
    SERVER_PROCESS_SINGLETON,
    SERVER_PROCESS_MASTER,
    SERVER_PROCESS_WORKER
} server_process_type;

/*
 * create an instance of a 'server'. options and callbacks must be valid
 * pointers until server_stop is called and server_listen returns
 */
server* server_create(config_server_options* options,
                      server_callbacks* callbacks, void* userdata);

/*
 * create an instance of a 'server'. options and callbacks must be valid
 * pointers until server_stop is called and server_listen returns. Does not
 * attach the default event loop. An attachment function from the adapters/
 * directory must be called before using.
 */
server* server_create_detached(config_server_options* options,
                      server_callbacks* callbacks, void* userdata);

/*
 * destroy an instance of a server
 */
void server_destroy(server* serv);

/*
 * Returns true if this is the master process, false if this is a worker
 * process. It multi-process is not enabled, will return false. It's only
 * valid to call this after server_init or server_listen has been called.
 */
server_process_type server_get_process_type(server* serv);

/*
 * convenience function, returns true if the server is the master
 */
bool server_is_master(server* serv);

/* set per-connection userdata */
void server_conn_set_userdata(server_conn* conn, void* userdata);

/* get per-connection userdata */
void* server_conn_get_userdata(server_conn* conn);

/* queue up a message to send on this connection */
server_result server_conn_send_msg(server_conn* conn, endpoint_msg* msg);

/* send a ping with payload (NULL for no payload)*/
server_result
server_conn_send_ping(server_conn* conn, char* payload, int payload_len);

/* send a pong with payload (NULL for no payload)*/
server_result
server_conn_send_pong(server_conn* conn, char* payload, int payload_len);

/*
 * close this connection. sends a close message with the error
 * code and reason
 */
server_result server_conn_close(server_conn* conn, uint16_t code,
                                const char* reason, int reason_len);

/*
 * get the total number of headers the client sent
 */
unsigned server_get_num_client_headers(server_conn* conn);

/*
 * Get the the field name of one of the headers sent by the client
 */
const char* server_get_header_name(server_conn* conn, unsigned index);

/*
 * Get the darray of values (type char*) for a given header index
 */
const darray* server_get_header_values(server_conn* conn, unsigned index);

/*
 * get number of subprotocols the client reported they support
 */
unsigned server_get_num_client_subprotocols(server_conn* conn);

/*
 * get a subprotocol the client reported they support
 */
const char* server_get_client_subprotocol(server_conn* conn, unsigned index);

/*
 * get number of extensions the client reported they support
 */
unsigned server_get_num_client_extensions(server_conn* conn);

/*
 * get an extension the client reported they support
 */
const char* server_get_client_extension(server_conn* conn, unsigned index);

/*
 * Get the resource requested by the client
 */
const char* server_get_resource(server_conn* conn);

/*
 * stop the server, close all connections. will cause server_listen
 * to return eventually. safe to call from a signal handler
 */
void server_stop(server* serv);

/*
 * Initiates the server. Binds port and calls listen() on socket. Only call
 * this directly if you are going to use your own event loop instead of
 * calling server_listen
 */
 server_result server_init(server* serv);

/*
 * Calls server_init, then blocks indefinitely listening for connections from
 * clients
 */
server_result server_listen(server* serv);

#endif /* __SERVER_H_ */

