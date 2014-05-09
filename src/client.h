/* client - a single connection to a client
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

#ifndef CLIENT_H__
#define CLIENT_H__

#include "config.h"
#include "endpoint.h"
#include <sys/types.h>

typedef enum
{
    CLIENT_STATE_CONNECTING,
    CLIENT_STATE_WRITE_HANDSHAKE,
    CLIENT_STATE_READ_HANDSHAKE,
    CLIENT_STATE_CONNECTED
} client_state;

typedef enum
{
    CLIENT_WRITE_CONTINUE,
    CLIENT_WRITE_DONE,
    CLIENT_WRITE_ERROR,
    CLIENT_WRITE_CLOSED
} client_write_result;

typedef enum
{
    /* read succeeded */
    CLIENT_READ_SUCCESS,

    /* this read caused data to be written to the write buffer */
    CLIENT_READ_SUCCESS_WROTE_DATA,

    /* error occured during read */
    CLIENT_READ_ERROR,

    /* the connect was closed during this read or a previous read*/
    CLIENT_READ_CLOSED
} client_read_result;

typedef enum
{
    CLIENT_RESULT_SUCCESS,
    CLIENT_RESULT_FAIL
} client_result;

typedef struct client client;
typedef bool (client_on_connect)(client* c, void* userdata);
typedef void (client_on_message)(client* c, endpoint_msg* msg, void* userdata);
typedef void (client_on_ping)(client* c, char* payload, int payload_len,
                              void* userdata);
typedef void (client_on_pong)(client* c, char* payload, int payload_len,
                              void* userdata);

/* includes the close code and reason received from the server (if any) */
typedef void (client_on_close)(client* c, int code, const char* reason,
                               int reason_len, void* userdata);

typedef struct
{
    /* called when a handshake has just been completed */
    client_on_connect* on_connect;

    /* called when a full message is received from a client */
    client_on_message* on_message;

    /*
     * called when a ping was received.  a pong is always sent for you
     * automatically to conform to the RFC
     */
    client_on_ping* on_ping;

    /*
     * called when a pong was received
     */
    client_on_pong* on_pong;

    /* called when connection is about to terminate */
    client_on_close* on_close;
} client_callbacks;

struct client
{
    int fd;
    client_state state;
    endpoint endp;
    client_callbacks* cbs;
    void* userdata;
};

/* Opens a non-blocking socket to addr, port, and puts a handshake on the write
 * buffer. Since this is non-blocking, you will have to use client_get_fd and
 * select() (or some such) before calling client_write_data()
 *
 * TODO: make a version of client_connect where you pass in an actual uri
 * rather than addr, port, resource
 */
client_result
client_connect_raw(client* c, config_client_options* opt,
                   client_callbacks* cbs, const char* ip_addr, uint16_t port,
                   const char* resource, const char* host,
                   const char** subprotocols, const char** extensions,
                   const char** extra_headers, void* userdata);

/* forcibly closes the socket and frees all resources (does NOT free c)*/
void client_disconnect(client* c);

/* get the file descriptor for this client */
int client_fd(client* c);

/* get the current state of this client */
client_state client_get_state(client* c);

/* queue up a message to send on this connection */
client_result client_send_msg(client* c, endpoint_msg* msg);

/* queue up a ping with payload (NULL for no payload)*/
client_result
client_send_ping(client* c, char* payload, int payload_len);

/* queue up a pong with payload (NULL for no payload)*/
client_result
client_send_pong(client* c, char* payload, int payload_len);

/*
 * close this connection. queues up a close message with the error
 * code and reason
 */
client_result client_close(client* c, uint16_t code, const char* reason,
                                int reason_len);

/*
 * get the subprotocol the client chose
 */
const char* client_get_client_subprotocol(client* c);

/*
 * get number of extensions the client chose
 */
unsigned client_get_num_extensions(client* c);

/*
 * get an extension the client chose
 */
const char* client_get_extension(client* c, unsigned index);

/*
 * write data from client to a ready socket
 */
client_write_result client_write(client* c, int fd);

/*
 * read data from a ready socket into the endpoint
 */
client_read_result client_read(client* c, int fd);

#endif /* CLIENT_H__ */
