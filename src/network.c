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

#include "base64/cencode.h"
#include "hhassert.h"
#include "hhmemory.h"
#include "network.h"
#include "sha1/sha1.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX_RESPONSE_LENGTH 1024

/* value of various opcodes defined in Section 5.2 (RFC 6455) */
typedef enum
{
    NETWORK_OPCODE_CONTINUATION, /* 0x00 */
    NETWORK_OPCODE_TEXT,         /* 0x01 */
    NETWORK_OPCODE_BINARY,       /* 0x02 */
    NETWORK_OPCODE_RESERVE3,     /* 0x03 */
    NETWORK_OPCODE_RESERVE4,     /* 0x04 */
    NETWORK_OPCODE_RESERVE5,     /* 0x05 */
    NETWORK_OPCODE_RESERVE6,     /* 0x06 */
    NETWORK_OPCODE_RESERVE7,     /* 0x07 */
    NETWORK_OPCODE_CLOSE,        /* 0x08 */
    NETWORK_OPCODE_PING,         /* 0x09 */
    NETWORK_OPCODE_PONG,         /* 0x0A */
    NETWORK_OPCODE_RESERVE11,    /* 0x0B */
    NETWORK_OPCODE_RESERVE12,    /* 0x0C */
    NETWORK_OPCODE_RESERVE13,    /* 0x0D */
    NETWORK_OPCODE_RESERVE14,    /* 0x0E */
    NETWORK_OPCODE_RESERVE15     /* 0x0F */
} network_opcode;

static int is_comma_delimited_header(const char* header)
{
    static const char* comma_headers[] = 
    {
        "Sec-WebSocket-Protocol",
        "Sec-WebSocket-Extensions"
    };

    for (int i = 0; i < hhcountof(comma_headers); i++)
    {
        if (strcmp(header, comma_headers[i]) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static char* eat_non_whitespace_or_comma(char* buf)
{
    while (*buf != '\0' && !isspace(*buf) && *buf != ',') buf++;
    return buf;
}

static char* eat_whitespace(char* buf)
{
    while(*buf != '\0' && isspace(*buf)) buf++;
    return buf;
}

static char* eat_digits(char* buf)
{
    while(*buf != '\0' && isdigit(*buf)) buf++;
    return buf;
}

static char* parse_request_line(
    network_handshake* info, 
    char* buf, 
    char* end_buf)
{
    const int get_len = 4;
    const int http_len = 5;

    buf = eat_whitespace(buf);
    if ((buf + get_len) >= end_buf) return NULL;

    /* ALL WebSocket handshakes are HTTP 'GETs' */
    if (strncmp(buf, "GET ", 4) != 0) return NULL;
    buf += get_len;

    char* end_uri = strchr(buf, ' ');
    if (end_uri == NULL) return NULL;
    *end_uri = '\0'; /* make this a string */
    info->resource_name = buf;
    
    buf = end_uri + 1;
    if ((buf + http_len) >= end_buf) return NULL;
    if (strncmp(buf, "HTTP/", http_len) != 0) return NULL;
    buf += http_len;

    char* old_buf = buf;
    buf = eat_digits(buf);
    if (buf == old_buf) return NULL;
    if (*buf != '.') return NULL;
    buf++;
    old_buf = buf;
    buf = eat_digits(buf);
    if (buf == old_buf) return NULL;
    if ((buf + 2) >= end_buf) return NULL;
    if (strncmp(buf, "\r\n", 2) != 0) return NULL;
    buf += 2;

    return buf;
}

static void add_comma_delimited_token(network_header* header, char* value)
{
    value = eat_whitespace(value);
    char* value_end = eat_non_whitespace_or_comma(value);
    (*value_end) = '\0';
    darray_append(&header->values, &value, 1);
}

static void add_header(network_handshake* info, char* name, char* value)
{
    network_header* header = NULL;
    network_header* all_headers = darray_get_data(info->headers);
    int num_headers = darray_get_len(info->headers);

    for (int i = 0; i < num_headers; i++)
    {
        network_header* hdr = &all_headers[i];
        if (strcasecmp(hdr->name, name) == 0)
        {
            header = hdr;
            break;
        }
    }

    if (header == NULL )
    {
        /* first time this header has appeared, create a new header object */
        network_header new_header;
        new_header.name = name;
        new_header.values = darray_create(sizeof(char*), 1); 

        /* now add it to our list of headers */
        darray_append(&info->headers, &new_header, 1);

        /* retrieve it */
        header = darray_get_last(info->headers);        
    }

    if (is_comma_delimited_header(name))
    {
        /* separate each comma-delimited value */
        char* value_comma = strchr(value, ',');
        while (value_comma != NULL && *value != '\0')
        {
            add_comma_delimited_token(header, value);
            value = value_comma + 1;
            value_comma = strchr(value, ',');
        }

        /* add last value */
        add_comma_delimited_token(header, value);
    }
    else
    {
        darray_append(&header->values, &value, 1); 
    }
}

static char* parse_headers(
    network_handshake* info, 
    char* buf, 
    char* end_buf)
{
    while (buf != end_buf && !isspace(*buf))
    {
        char* end_name = strchr(buf, ':');
        if (end_name == NULL) return NULL;
        *end_name = '\0'; /* make buf up to here a string */
        char* name = buf;
        buf = end_name + 1;
        buf = eat_whitespace(buf);
        if (buf >= end_buf) return NULL;

        char* end_value = strstr(buf, "\r\n");
        if (end_value == NULL) return NULL;
        *end_value = '\0'; /* make buf up to here a string */
        char* value = buf;

        /* we have our name and value, add this header to the list */
        add_header(info, name, value);

        buf = end_value + 2;
    }

    return buf;
}
 
/* create a network_conn on the heap, init buffers to init_buf_len bytes */
network_conn* network_create_conn(size_t out_frame_max, size_t init_buf_len)
{
    network_conn* conn = hhmalloc(sizeof(*conn));
    if (conn == NULL) goto create_err;

    conn->out_frame_max = out_frame_max;
    conn->state = NETWORK_STATE_READ_HANDSHAKE;
    conn->info.headers = darray_create(sizeof(network_header), 8);
    if (conn->info.headers == NULL) goto create_err; 
    conn->info.buffer = NULL;
    conn->read_buffer = darray_create(sizeof(char), init_buf_len);
    if (conn->read_buffer == NULL) goto create_err;
    conn->write_buffer = darray_create(sizeof(char), init_buf_len);
    if (conn->write_buffer == NULL) goto create_err;
    conn->read_msg.msg_type = NETWORK_MSG_NONE;

    return conn;

create_err:
    if (conn != NULL && conn->info.headers != NULL)
    {
        darray_destroy(conn->info.headers);
    }
    
    if (conn != NULL && conn->read_buffer != NULL) 
    {
        darray_destroy(conn->read_buffer);
    }

    if (conn != NULL && conn->write_buffer != NULL)
    {
        darray_destroy(conn->write_buffer);
    }

    if (conn != NULL) hhfree(conn);
    return NULL;
}

/* free/destroy a network_conn */
void network_destroy_conn(network_conn* conn)
{
    if (conn->info.headers != NULL)
    {
        int num_headers = darray_get_len(conn->info.headers);
        network_header* headers = darray_get_data(conn->info.headers);
        for (int i = 0; i < num_headers; i++)
        {
            darray_destroy(headers[i].values);
        }
    }

    darray_destroy(conn->info.headers);
    darray_destroy(conn->info.buffer);
    darray_destroy(conn->read_buffer);
    darray_destroy(conn->write_buffer);
    hhfree(conn);
}

/* 
 * parse the handshake from buffer into conn->info.  re-uses buffer and 
 * assigns it to conn->info->buffer. on success, advances state from
 * NETWORK_STATE_READ_HANDSHAKE -> NETWORK_STATE_WRITE_HANDSHAKE
 */
network_result network_read_handshake(network_conn* conn, darray* buffer)
{
    network_handshake* info = &conn->info;

    char* buf = darray_get_data(buffer);
    hhassert_pointer(buf);
    int length = darray_get_len(buffer);
    char* end_buf = buf + length;

    buf = parse_request_line(info, buf, end_buf);
    if (buf == NULL) goto read_err;

    buf = parse_headers(info, buf, end_buf);
    if (buf == NULL) goto read_err;

    if (strncmp(buf, "\r\n", 2) != 0) goto read_err;

    conn->state = NETWORK_STATE_WRITE_HANDSHAKE;
    return NETWORK_RESULT_SUCCESS;

read_err:
    conn->state = NETWORK_STATE_CLOSED;
    return NETWORK_RESULT_FAIL;
}

/* 
 * write the handshake response to conn->write_buffer.  must be called after
 * network_read_handshake.  on success, advances state from
 * NETWORK_STATE_WRITE_HANDSHAKE -> NETWORK_STATE_CONNECTED
 */
network_result network_write_handshake(
    network_conn* conn, 
    const char* protocol,
    const char* extensions
)
{
    hhassert(conn->state == NETWORK_STATE_WRITE_HANDSHAKE);

    /* this constant comes straight from RFC 6455 */
    static const char key_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    static const char protocol_template[] = "Sec-WebSocket-Protocol: %s\r\n";
    static const char extension_template[] = 
        "Sec-WebSocket-Extensions: %s\r\n";
    static const char response_template[] = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n";

    const char* upgrade = network_get_header_value(conn, "Upgrade", 0);
    if (upgrade == NULL || strcmp(upgrade, "websocket") != 0) 
    {
        return NETWORK_RESULT_FAIL;
    }

    const char* connection = network_get_header_value(conn, "Connection", 0);
    if (connection == NULL || strcmp(connection, "Upgrade") != 0)
    {
        return NETWORK_RESULT_FAIL;
    }

    const char* web_sock_key = network_get_header_value(
        conn,
        "Sec-WebSocket-Key",
        0
    );
    if (web_sock_key == NULL) return NETWORK_RESULT_FAIL;
    if (strlen(web_sock_key) != 24) return NETWORK_RESULT_FAIL;

    char sha_buf[64];
    int num_written = snprintf(
        sha_buf, 
        sizeof(sha_buf), 
        "%s%s", 
        web_sock_key, 
        key_guid
    ); 
    
    /* create our response SHA1 */
    char sha_result[SHA1HashSize];
    SHA1Context context;
    SHA1Reset(&context);
    SHA1Input(&context, (uint8_t*)sha_buf, num_written);
    SHA1Result(&context, (uint8_t*)sha_result);

    /* encode our SHA1 as base64 */
    char response_key[(4 * ((SHA1HashSize + 3) / 3)) + 1];
    base64_encodestate encode_state;
    base64_init_encodestate(&encode_state);
    int num_encoded = base64_encode_block(
        sha_result, 
        SHA1HashSize, 
        response_key,
        &encode_state
    );
    int num_end = base64_encode_blockend(
        &response_key[num_encoded], 
        &encode_state
    );

    /* encode_blockend adds a \n... we want to null terminate */
    int response_key_len = num_encoded + num_end - 1;
    hhassert(response_key_len <= sizeof(response_key));
    response_key[response_key_len] = '\0';

    /* 
     * put the whole response on the connection's write buffer 
     */

    /* this should be the first thing ever written to a client */
    hhassert(darray_get_len(conn->write_buffer) == 0); 

    /* -2 because of %s in response_template, +2 because of trailing \r\n */
    int total_len = (sizeof(response_template) - 2) + (response_key_len) + 2;

    if (protocol != NULL)
    {
        total_len += strlen(protocol);

        /* -1 for terminator, -2 for %s in protocol_template */
        total_len += sizeof(protocol_template) - 1 - 2; 
    }

    if (extensions != NULL)
    {
        total_len += strlen(extensions);

        /* -1 for terminator, -2 for %s in extension_template */
        total_len += sizeof(extension_template) - 1 - 2;
    }

    darray_ensure(&conn->write_buffer, total_len);
    char* buf = darray_get_data(conn->write_buffer);
    
    /* write out the mandatory stuff */
    num_written = 0;
    num_written = snprintf(buf, total_len, response_template, response_key);

    /* write out protocol header, if necessary */
    if (protocol != NULL)
    {
        num_written += snprintf(
            &buf[num_written], 
            total_len - num_written,
            protocol_template,
            protocol
        );
    }

    /* write out the extension header, if necessary */
    if (extensions != NULL)
    {
        num_written += snprintf(
            &buf[num_written],
            total_len - num_written,
            extension_template,
            extensions
        );
    }

    /* write out the closing CRLF */
    num_written += snprintf(
        &buf[num_written], 
        total_len - num_written, 
        "\r\n"
    );

    /* 
     * num_written doesn't include the null terminator, so we should have 
     * written exactly total_len - 1 bytes
     */
    hhassert(num_written == (total_len - 1));
    darray_add_len(conn->write_buffer, total_len - 1);

    conn->state = NETWORK_STATE_CONNECTED;
    return NETWORK_RESULT_SUCCESS;
}

/*
 * Get the number of times this header appeared in the request
 */
int network_get_num_header_values(network_conn* conn, const char* name)
{
    network_header* headers = darray_get_data(conn->info.headers);
    int len = darray_get_len(conn->info.headers);

    for (int i = 0; i < len; i++)
    {
        if (strcasecmp(name, headers[i].name) == 0)
        {
            return darray_get_len(headers[i].values);
        }
    }

    return 0;
}

/*
 * Get one of the values retrieved for a header
 */
const char* network_get_header_value(
    network_conn* conn, 
    const char* name, 
    int index
)
{
    network_header* headers = darray_get_data(conn->info.headers);
    int len = darray_get_len(conn->info.headers);

    for (int i = 0; i < len; i++)
    {
        if (strcasecmp(name, headers[i].name) == 0)
        {
            hhassert(index >= 0 && index < darray_get_len(headers[i].values));
            char** values = darray_get_data(headers[i].values);
            return values[index];
        }
    }

    return NULL;
}

/*
 * process a frame from the read buffer starting at start_pos into 
 * conn->read_msg. 
 */
network_result network_read_msg(network_conn* conn, int start_pos)
{

}

/*
 * put the message in write_msg on conn->write_buffer.
 * msg will be broken up into frames of size out_frame_max
 */
network_result network_write_msg(network_conn* conn, network_msg* write_msg)
{

}


