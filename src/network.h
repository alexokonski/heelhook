/* network - serialize messages conforming to RFC 6455 
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

#ifndef __NETWORK_H_
#define __NETWORK_H_

#include "darray.h"

typedef enum
{
    NETWORK_MSG_NONE,   /* Not a real message */
    NETWORK_MSG_TEXT,
    NETWORK_MSG_BINARY,
    NETWORK_MSG_CLOSE,
    NETWORK_MSG_PING,
    NETWORK_MSG_PONG
} network_msg_type;

typedef enum 
{
    NETWORK_STATE_READ_HANDSHAKE,
    NETWORK_STATE_WRITE_HANDSHAKE,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_CLOSED
} network_state;

typedef enum
{
    NETWORK_RESULT_SUCCESS,
    NETWORK_RESULT_CONTINUE,
    NETWORK_RESULT_FAIL
} network_result;

/* represents a WebSocket message */
typedef struct
{
    network_msg_type msg_type;
    int msg_length;
    char* data;
} network_msg;

/* represeents a single handshake header */
typedef struct
{
    char* name;
    darray* values; /* darray of char*, i.e. char** */
} network_header;

/* info about the connection obtained during the WebSocket handshake */
typedef struct
{
    char* resource_name;
    darray* headers; /* array of network_header that contains all headers */
    darray* buffer;  /* buffer that contains all headers and resource name */
} network_handshake;

/* represents a network connection */
typedef struct
{
    size_t out_frame_max;   /* maximum payload size for outgoing frames */
    network_state state;    /* current state of this connection */
    network_handshake info; /* info about this connection from handshake */
    darray* read_buffer;    /* char* buffer used for reading from a client */
    darray* write_buffer;   /* char* buffer used for writing to a client */
    network_msg read_msg;   /* msg we're currently reading from a client */
} network_conn;

/*
 * create a network_conn on the heap, init buffers to init_buf_len bytes 
 */
network_conn* network_create_conn(size_t out_frame_max, size_t init_buf_len);

/* 
 * free/destroy a network_conn
 */
void network_destroy_conn(network_conn* conn);

/* 
 * parse the handshake from buffer into conn->info.  re-uses buffer and 
 * assigns it to conn->info->buffer. on success, advances state from
 * NETWORK_STATE_READ_HANDSHAKE -> NETWORK_STATE_WRITE_HANDSHAKE
 */
network_result network_read_handshake(network_conn* conn, darray* buffer);

/* 
 * write the handshake response to conn->write_buffer.  must be called after
 * network_read_handshake.  on success, advances state from
 * NETWORK_STATE_WRITE_HANDSHAKE -> NETWORK_STATE_CONNECTED
 */
network_result network_write_handshake(
    network_conn* conn, 
    const char* protocol,
    const char* extensions
);

/*
 * Get the number of times this header appeared in the request
 */
int network_get_num_header_values(network_conn* conn, const char* name);

/*
 * Get one of the values retrieved for a header
 */
const char* network_get_header_value(
    network_conn* conn, 
    const char* name, 
    int index
);

/*
 * process a frame from the read buffer starting at start_pos into 
 * conn->read_msg.
 */
network_result network_read_msg(network_conn* conn, int start_pos);

/*
 * put the message in write_msg on conn->write_buffer.
 * msg will be broken up into frames of size out_frame_max
 */
network_result network_write_msg(network_conn* conn, network_msg write_msg);

#endif /* __NETWORK_H_ */

