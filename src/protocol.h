/* protocol - serialize messages conforming to RFC 6455
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

#ifndef __PROTOCOL_H_
#define __PROTOCOL_H_

#include "darray.h"
#include "util.h"

typedef enum
{
    PROTOCOL_MSG_NONE,   /* Not a real message */
    PROTOCOL_MSG_TEXT,
    PROTOCOL_MSG_BINARY,
    PROTOCOL_MSG_CLOSE,
    PROTOCOL_MSG_PING,
    PROTOCOL_MSG_PONG
} protocol_msg_type;

/* value of various opcodes defined in Section 5.2 (RFC 6455) */
typedef enum
{
    PROTOCOL_OPCODE_CONTINUATION, /* 0x00 */
    PROTOCOL_OPCODE_TEXT,         /* 0x01 */
    PROTOCOL_OPCODE_BINARY,       /* 0x02 */
    PROTOCOL_OPCODE_RESERVE3,     /* 0x03 */
    PROTOCOL_OPCODE_RESERVE4,     /* 0x04 */
    PROTOCOL_OPCODE_RESERVE5,     /* 0x05 */
    PROTOCOL_OPCODE_RESERVE6,     /* 0x06 */
    PROTOCOL_OPCODE_RESERVE7,     /* 0x07 */
    PROTOCOL_OPCODE_CLOSE,        /* 0x08 */
    PROTOCOL_OPCODE_PING,         /* 0x09 */
    PROTOCOL_OPCODE_PONG,         /* 0x0A */
    PROTOCOL_OPCODE_RESERVE11,    /* 0x0B */
    PROTOCOL_OPCODE_RESERVE12,    /* 0x0C */
    PROTOCOL_OPCODE_RESERVE13,    /* 0x0D */
    PROTOCOL_OPCODE_RESERVE14,    /* 0x0E */
    PROTOCOL_OPCODE_RESERVE15     /* 0x0F */
} protocol_opcode;

typedef enum
{
    PROTOCOL_STATE_READ_HANDSHAKE,
    PROTOCOL_STATE_WRITE_HANDSHAKE,
    PROTOCOL_STATE_CONNECTED,
} protocol_state;

typedef enum
{
    PROTOCOL_RESULT_MESSAGE_FINISHED,
    PROTOCOL_RESULT_FRAME_FINISHED,
    PROTOCOL_RESULT_CONTINUE,
    PROTOCOL_RESULT_FAIL,
} protocol_result;

typedef enum
{
    PROTOCOL_HANDSHAKE_SUCCESS,
    PROTOCOL_HANDSHAKE_CONTINUE,
    PROTOCOL_HANDSHAKE_FAIL_TOO_LARGE,
    PROTOCOL_HANDSHAKE_FAIL
} protocol_handshake_result;

/* represents a WebSocket message */
typedef struct
{
    protocol_msg_type type;
    int64_t msg_len;
    char* data;
} protocol_msg;

/* represents a WebSocket message, but the data is a pos in some buffer */
typedef struct
{
    size_t start_pos;
    int64_t msg_len;
    protocol_msg_type type;
} protocol_offset_msg;

/* represents a WebSocket frame header */
typedef struct
{
    protocol_opcode opcode;
    protocol_msg_type msg_type;
    int64_t payload_processed;
    int64_t payload_len; /* -1 means we're on a brand new frame */
    size_t data_start_pos;
    char masking_key[4];
    bool fin;
    bool masked;
} protocol_frame_hdr;

/* state needed across calls for ut8 validator */
typedef struct
{
    uint32_t state;
    uint32_t codepoint;
} protocol_utf8_valid_state;

/* represeents a single handshake header */
typedef struct
{
    char* name;
    darray* values; /* darray of char*, i.e. char** */
} protocol_header;

/* info about the connection obtained during the WebSocket handshake */
typedef struct
{
    char* resource_name;
    darray* headers; /* array of protocol_header that contains all headers */
    darray* buffer;  /* buffer that contains all headers and resource name */
} protocol_handshake;

typedef struct protocol_conn protocol_conn;
typedef uint32_t (random_func)(struct protocol_conn* conn);

/* settings for a connection */
typedef struct
{
    /*
     * written messages will be broken up into frames of this size,
     * -1 is no limit
     */
    int64_t write_max_frame_size;

    /* max size of the whole message (in bytes), -1 is no limit */
    int64_t read_max_msg_size;

    /* max frames allowed in a message, -1 is no limit */
    int64_t read_max_num_frames;

    /* max size of received handshake (in bytes), -1 is no limit */
    int max_handshake_size;

    /*
     * random number generator for creating client frames
     */
    random_func* rand_func;
} protocol_settings;

/* represents the buffers/info for a websocket connection */
struct protocol_conn
{
    /* settings for this connection */
    protocol_settings* settings;

    /* char* buffer used for reading from an endpoint */
    darray* read_buffer;

    /* char* buffer used for writing to a endpoint */
    darray* write_buffer;

    /* current state of this connection */
    protocol_state state;

    int error_len;

    /* fragmented msg we're currently reading from a endpoint */
    protocol_offset_msg frag_msg;

    /* current msg frame we're reading */
    protocol_frame_hdr frame_hdr;

    /* utf8 validator state for the current frame we're reading */
    protocol_utf8_valid_state valid_state;

    /*
     * number of message fragments read in the message we're currently
     * parsing
     */
    int64_t num_fragments_read;

    /* info about this connection from handshake */
    protocol_handshake info;

    /*
     * if protocol_read_msg returns PROTOCOL_RESULT_FAIL, 
     * contains a message to send to endpoint upon closing
     */
    const char* error_msg;

    /*
     * if protocol_read_msg returns PROTOCOL_RESULT_FAIL, 
     * contains the error code to send to endpoints upon closing
     */
    uint16_t error_code;

    /*
     * arbitrary data to associate with this object
     */
    void* userdata;
};

/*
 * create a protocol_conn on the heap, init buffers to init_buf_len bytes
 * the caller is responsible for the settings object
 */
protocol_conn* protocol_create_conn(protocol_settings* settings,
                                    size_t init_buf_len, void* userdata);

/*
 * initialize an already allocated protocol_conn. allocates read and
 * write buffers
*/
int protocol_init_conn(protocol_conn* conn, protocol_settings* settings,
                       size_t init_buf_len, void* userdata);

/*
 * free/destroy a protocol_conn
 */
void protocol_destroy_conn(protocol_conn* conn);

/*
 * destroy everything in the conn but leave the conn intact
 */
void protocol_deinit_conn(protocol_conn* conn);

/*
 * reset all state on a connection
 */
void protocol_reset_conn(protocol_conn* conn);

/*
 * parse the handshake from the client from conn->info.buffer. On success, 
 * advances state from
 * PROTOCOL_STATE_READ_HANDSHAKE -> PROTOCOL_STATE_WRITE_HANDSHAKE
 */
protocol_handshake_result
protocol_read_handshake_request(protocol_conn* conn);

/*
 * write the handshake response to the client to conn->info.buffer. 
 * must be called after protocol_read_handshake_request.  on success,
 * advances state from 
 * PROTOCOL_STATE_WRITE_HANDSHAKE -> PROTOCOL_STATE_CONNECTED
 */
protocol_handshake_result
protocol_write_handshake_response(
    protocol_conn* conn,
    const char* protocol, /* (optional) */
    const char** extensions /* NULL terminated (optional) */
);

/*
 * parse the handshake response from the server from conn->info.buffer
 * On success, advances state from
 * PROTOCOL_STATE_READ_HANDSHAKE -> PROTOCOL_STATE_CONNECTED
 */
protocol_handshake_result
protocol_read_handshake_response(protocol_conn* conn);

/*
 * write the client handshake request to conn->write_buffer. On success,
 * advances state to PROTOCOL_STATE_READ_HANDSHAKE.  extra_headers is 
 * repeating (key,value) pairs with a terminating NULL
 */
protocol_handshake_result
protocol_write_handshake_request(
    protocol_conn* conn,
    const char* resource,
    const char* host,
    const char** protocols, /* NULL terminated (optional) */
    const char** extensions, /* NULL terminated (optional) */
    const char** extra_headers /* NULL terminated, (optional) */
);

/*
 * Get the number of times this header appeared in the request
 */
unsigned protocol_get_num_header_values(protocol_conn* conn, const char* name);

/*
 * Get one of the values retrieved for a header
 */
const char* protocol_get_header_value(protocol_conn* conn, const char* name,
                                      unsigned index);

/*
 * Helper for easily getting the number of values the endpoint sent in the
 * Sec-WebSocket-Protocol header
 */
unsigned protocol_get_num_subprotocols(protocol_conn* conn);

/*
 * Helper for getting values of the Sec-WebSocket-Protocol header sent by
 * the endpoint
 */
const char* protocol_get_subprotocol(protocol_conn* conn, unsigned index);

/*
 * Helper for easily getting the number of values the client sent in the
 * Sec-WebSocket-Extensions header
 */
unsigned protocol_get_num_extensions(protocol_conn* conn);

/*
 * Helper for getting values of the Sec-WebSocket-Extensions header sent by
 * the client
 */
const char* protocol_get_extension(protocol_conn* conn, unsigned index);

/*
 * process a frame sent by a client from the read buffer starting at start_pos
 * into conn->read_msg. read_msg will contain the read message if
 * protocol_result is PROTOCOL_RESULT_MESSAGE_FINISHED.  Otherwise, it is
 * untouched.  The data pointer in read_msg will be a pointer into
 * conn->read_buffer, and as such will NOT be valid after any changes to
 * conn->read_buffer
 */
protocol_result
protocol_read_client_msg(protocol_conn* conn, size_t* start_pos,
                         protocol_msg* read_msg);

/*
 * process a frame sent by a server from the read buffer starting at start_pos
 * into conn->read_msg. read_msg will contain the read message if
 * protocol_result is PROTOCOL_RESULT_MESSAGE_FINISHED.  Otherwise, it is
 * untouched.  The data pointer in read_msg will be a pointer into
 * conn->read_buffer, and as such will NOT be valid after any changes to
 * conn->read_buffer
 */
protocol_result
protocol_read_server_msg(protocol_conn* conn, size_t* start_pos,
                         protocol_msg* read_msg);

/*
 * write write_msg to conn->write_buffer.  must be called after
 * protocol_read_handshake_request.
 */
protocol_result
protocol_write_server_msg(protocol_conn* conn, protocol_msg* write_msg);

/*
 * write write_msg to conn->write_buffer.  must be called after
 * protocol_read_handshake_request.
 */
protocol_result
protocol_write_client_msg(protocol_conn* conn, protocol_msg* write_msg);

/* Convienence functions */
bool protocol_is_data(protocol_msg_type msg_type);
bool protocol_is_control(protocol_msg_type msg_type);

#endif /* __PROTOCOL_H_ */

