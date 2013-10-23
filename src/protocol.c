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

#include "base64/cencode.h"
#include "error_code.h"
#include "hhassert.h"
#include "hhmemory.h"
#include "protocol.h"
#include "sha1/sha1.h"
#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>


static int is_valid_opcode(protocol_opcode opcode)
{
    switch (opcode)
    {
        case PROTOCOL_OPCODE_CONTINUATION:
        case PROTOCOL_OPCODE_TEXT:
        case PROTOCOL_OPCODE_BINARY:
        case PROTOCOL_OPCODE_CLOSE:
        case PROTOCOL_OPCODE_PING:
        case PROTOCOL_OPCODE_PONG:
            return 1;

        default:
            return 0;
    }
}

static int multiple_frames_allowed(protocol_opcode opcode)
{
    switch (opcode)
    {
        case PROTOCOL_OPCODE_CONTINUATION:
        case PROTOCOL_OPCODE_TEXT:
        case PROTOCOL_OPCODE_BINARY:
            return 1;

        case PROTOCOL_OPCODE_CLOSE:
        case PROTOCOL_OPCODE_PING:
        case PROTOCOL_OPCODE_PONG:
        default:
            return 0;
    }
}

static protocol_msg_type msg_type_from_opcode(protocol_opcode opcode)
{
    switch (opcode)
    {
        case PROTOCOL_OPCODE_TEXT:
            return PROTOCOL_MSG_TEXT;
        case PROTOCOL_OPCODE_BINARY:
            return PROTOCOL_MSG_BINARY;
        case PROTOCOL_OPCODE_CLOSE:
            return PROTOCOL_MSG_CLOSE;
        case PROTOCOL_OPCODE_PING:
            return PROTOCOL_MSG_PING;
        case PROTOCOL_OPCODE_PONG:
            return PROTOCOL_MSG_PONG;
        default:
            return PROTOCOL_MSG_NONE;
    }
}

static protocol_opcode opcode_from_msg_type(protocol_msg_type msg_type)
{
    switch (msg_type)
    {
        case PROTOCOL_MSG_NONE:
            return PROTOCOL_OPCODE_TEXT;
        case PROTOCOL_MSG_TEXT:
            return PROTOCOL_OPCODE_TEXT;
        case PROTOCOL_MSG_BINARY:
            return PROTOCOL_OPCODE_BINARY;
        case PROTOCOL_MSG_CLOSE:
            return PROTOCOL_OPCODE_CLOSE;
        case PROTOCOL_MSG_PING:
            return PROTOCOL_OPCODE_PING;
        case PROTOCOL_MSG_PONG:
            return PROTOCOL_OPCODE_PONG;
        default:
            return PROTOCOL_OPCODE_TEXT;
    }
}

static int get_num_extra_len_bytes(int64_t payload_len)
{
    if (payload_len <= 125)
    {
        return 0;
    }
    else if(payload_len <= UINT16_MAX)
    {
        return 2;
    }
    else
    {
        return 8;
    }
}

/* from http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

static HH_INLINE uint32_t utf8_decode(
    uint32_t* state,
    uint32_t* codep,
    uint32_t byte
)
{
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state*16 + type];
  return *state;
}

static BOOL is_valid_utf8(const char* str, int length)
{
    uint32_t codepoint, state = 0;
    const uint8_t* s = (const uint8_t*)str;
    const uint8_t* end = s + length;
    while (s != end)
    {
        utf8_decode(&state, &codepoint, *s++);
    }

    return state == UTF8_ACCEPT;
}

static void mask_data(
    char* data,
    size_t len,
    char* mask_key,
    int mask_index,
    BOOL validate_utf8,
    uint32_t* state,
    uint32_t* codepoint
)
{
    uint8_t* d = (uint8_t*)data;
    for (size_t i = 0; i < len; i++)
    {
        d[i] ^= mask_key[mask_index++ % 4];
        if (validate_utf8)
        {
            utf8_decode(state, codepoint, d[i]);
            if (*state == UTF8_REJECT) return;
        }
    }
}

static void mask_and_move_data(
    char* dest,
    char* src,
    size_t len,
    char* mask_key_ptr,
    int mask_index,
    BOOL validate_utf8,
    uint32_t* state,
    uint32_t* codepoint
)
{
    /*
     * the move we're about to do might overwrite the data at
     * mask_key_ptr... copy the key into another place
     */
    char mask_key[4];
    memcpy(mask_key, mask_key_ptr, sizeof(mask_key));

    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < len; i++)
    {
        d[i] = src[i] ^ mask_key[mask_index++ % 4];
        if (validate_utf8)
        {
            utf8_decode(state, codepoint, d[i]);
            if (*state == UTF8_REJECT) return;
        }
    }
}

static int is_comma_delimited_header(const char* header)
{
    static const char* comma_headers[] =
    {
        "Sec-WebSocket-Protocol",
        "Sec-WebSocket-Extensions",
        "Accept-Encoding",
        "TE"
    };

    for (unsigned int i = 0; i < hhcountof(comma_headers); i++)
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
    protocol_handshake* info,
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

static void add_comma_delimited_token(protocol_header* header, char* value)
{
    value = eat_whitespace(value);
    char* value_end = eat_non_whitespace_or_comma(value);
    (*value_end) = '\0';
    darray_append(&header->values, &value, 1);
}

static void add_header(protocol_handshake* info, char* name, char* value)
{
    protocol_header* header = NULL;
    protocol_header* all_headers = darray_get_data(info->headers);
    int num_headers = darray_get_len(info->headers);

    for (int i = 0; i < num_headers; i++)
    {
        protocol_header* hdr = &all_headers[i];
        if (strcasecmp(hdr->name, name) == 0)
        {
            header = hdr;
            break;
        }
    }

    if (header == NULL )
    {
        /* first time this header has appeared, create a new header object */
        protocol_header new_header;
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
    protocol_handshake* info,
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

static void handle_violation(
    protocol_conn* conn,
    uint16_t code,
    const char* msg
)
{
    if (conn->should_close) return;

    conn->should_close = TRUE;
    conn->error_code = code;
    conn->error_msg = msg;
    conn->error_len = strlen(msg); /* terminator left out on purpose */
}

static protocol_result protocol_parse_frame_hdr(
    protocol_conn* conn,
    size_t* pos_ptr
)
{
    protocol_frame_hdr* hdr = &conn->frame_hdr;
    if (hdr->payload_len >= 0) return PROTOCOL_RESULT_FRAME_FINISHED;

    size_t pos = *pos_ptr;
    char* raw_buffer = darray_get_data(conn->read_buffer);
    char* data = &raw_buffer[pos];
    size_t data_len = darray_get_len(conn->read_buffer) - pos;

    char* data_end = data + data_len;

    if (data_len < 2) return PROTOCOL_RESULT_CONTINUE;

    unsigned char first = *data;
    int fin = (first & 0xf0);

    /* you're not allowed to have the RSV bits set  */
    if (fin != 0 && fin != 0x80)
    {
        handle_violation(conn, HH_ERROR_PROTOCOL, "RSV bit set");
        return PROTOCOL_RESULT_FAIL;
    }

    protocol_opcode opcode = (first & 0x0f);
    protocol_msg_type msg_type = msg_type_from_opcode(opcode);

    /* if this is a bogus opcode, fail */
    if (!is_valid_opcode(opcode))
    {
        handle_violation(conn, HH_ERROR_PROTOCOL, "Invalid opcode");
        return PROTOCOL_RESULT_FAIL;
    }

    /*
     * if this is a continuation frame, we need to have gotten
     * a non-continuation frame previously
     */
    if (opcode == PROTOCOL_OPCODE_CONTINUATION &&
        conn->frag_msg.type == PROTOCOL_MSG_NONE)

    {
        handle_violation(
            conn,
            HH_ERROR_PROTOCOL,
            "Out of band continuation frame"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    /*
     * We're currently processing a fragmented message, you can't send
     * text or binary frames with a non CONTINUATION opcode
     */
    if (conn->frag_msg.type != PROTOCOL_MSG_NONE &&
        (opcode == PROTOCOL_OPCODE_TEXT ||
         opcode == PROTOCOL_OPCODE_BINARY))
    {
        handle_violation(
            conn,
            HH_ERROR_PROTOCOL,
            "Out of band text or binary frame"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    /* control frames must not be fragmented */
    if (!fin &&
        !(opcode == PROTOCOL_OPCODE_TEXT ||
          opcode == PROTOCOL_OPCODE_BINARY ||
          opcode == PROTOCOL_OPCODE_CONTINUATION))

    {
        handle_violation(
            conn,
            HH_ERROR_PROTOCOL,
            "Control frames must not be fragmented"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    data++;
    unsigned char second = *data;
    int is_masked = (second & 0x80);

    /* all client frames must be masked */
    if (!is_masked)
    {
        handle_violation(
            conn,
            HH_ERROR_PROTOCOL,
            "All client frames must be masked"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    uint64_t payload_len = 0;
    unsigned char first_len = (second & 0x7f);

    /* control frames must be <=125 bytes */
    if (first_len > 125 &&
        !(opcode == PROTOCOL_OPCODE_TEXT ||
          opcode == PROTOCOL_OPCODE_BINARY ||
          opcode == PROTOCOL_OPCODE_CONTINUATION))
    {
        handle_violation(
            conn,
            HH_ERROR_PROTOCOL,
            "Control frames must be <=125 bytes"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    if (first_len <= 125)
    {
        payload_len = first_len;
        data++;
    }
    else if (first_len == 126)
    {
        if (data_len < (2 + sizeof(uint16_t)))
        {
            return PROTOCOL_RESULT_CONTINUE;
        }
        data++;
        payload_len = hh_ntohs(*((uint16_t*)data));
        data += sizeof(uint16_t);
    }
    else /* first_len == 127 */
    {
        hhassert(first_len == 127);
        if (data_len < (2 + sizeof(uint64_t)))
        {
            return PROTOCOL_RESULT_CONTINUE;
        }

        data++;
        payload_len = hh_ntohll(*((uint64_t*)data));
        data += sizeof(uint64_t);
    }

    if (data + sizeof(uint32_t) > data_end)
    {
        return PROTOCOL_RESULT_CONTINUE;
    }

    char* masking_key = data;
    data += sizeof(uint32_t);

    protocol_offset_msg* msg = &conn->frag_msg;
    if (conn->settings->read_max_num_frames != -1 &&
        conn->num_fragments_read >= conn->settings->read_max_num_frames)
    {
        handle_violation(
            conn,
            HH_ERROR_POLICY_VIOLATION,
            "client sent too many frames in one message"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    int64_t read_max_msg_size = conn->settings->read_max_msg_size;
    /*
     * cast below is okay because we know read_max_msg_size is
     * non-negative
     */
    if (read_max_msg_size >= 0 &&
        msg->msg_len + payload_len > (uint64_t)read_max_msg_size)

    {
        handle_violation(
            conn,
            HH_ERROR_LARGE_MESSAGE,
            "client sent message that was too large"
        );
        return PROTOCOL_RESULT_FAIL;
    }

    hdr->opcode = opcode;
    hdr->msg_type = msg_type;
    hdr->payload_processed = 0;
    hdr->payload_len = payload_len;
    memcpy(hdr->masking_key, masking_key, sizeof(hdr->masking_key));
    hdr->fin = (fin != 0);
    pos += data - &raw_buffer[pos];
    hdr->data_start_pos = pos;
    *pos_ptr = pos;

    return PROTOCOL_RESULT_FRAME_FINISHED;
}

/* create a protocol_conn on the heap, init buffers to init_buf_len bytes */
protocol_conn* protocol_create_conn(
    protocol_settings* settings,
    size_t init_buf_len
)
{
    protocol_conn* conn = hhmalloc(sizeof(*conn));
    if (conn == NULL || protocol_init_conn(conn, settings, init_buf_len) < 0)
    {
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
        if (conn != NULL)
        {
            hhfree(conn);
        }
        conn = NULL;
    }

    return conn;
}

/*
 * initialize an already allocated protocol_conn. allocates read and
 * write buffers
*/
int protocol_init_conn(
    protocol_conn* conn,
    protocol_settings* settings,
    size_t init_buf_len
)
{
    memset(conn, 0, sizeof(*conn));
    conn->settings = settings;
    conn->state = PROTOCOL_STATE_READ_HANDSHAKE;
    conn->info.resource_name = NULL;
    conn->info.headers = darray_create(sizeof(protocol_header), 8);
    if (conn->info.headers == NULL) return -1;
    conn->info.buffer = darray_create(sizeof(char), 1024);
    conn->read_buffer = darray_create(sizeof(char), init_buf_len);
    if (conn->read_buffer == NULL) return -1;
    conn->write_buffer = darray_create(sizeof(char), init_buf_len);
    if (conn->write_buffer == NULL) return -1;
    conn->frag_msg.type = PROTOCOL_MSG_NONE;
    conn->frag_msg.msg_len = 0;
    conn->frag_msg.start_pos = 0;
    conn->frame_hdr.payload_len = -1;
    conn->valid_state.state = 0;
    conn->valid_state.codepoint = 0;
    conn->should_close = FALSE;
    conn->error_msg = NULL;
    conn->error_code = 0;
    conn->error_len = 0;

    return 0;
}


/* free/destroy a protocol_conn */
void protocol_destroy_conn(protocol_conn* conn)
{
    protocol_deinit_conn(conn);
    hhfree(conn);
}

/* destroy everything in the conn but leave the conn intact */
void protocol_deinit_conn(protocol_conn* conn)
{
    if (conn->info.headers != NULL)
    {
        int num_headers = darray_get_len(conn->info.headers);
        protocol_header* headers = darray_get_data(conn->info.headers);
        for (int i = 0; i < num_headers; i++)
        {
            darray_destroy(headers[i].values);
        }
    }

    darray_destroy(conn->info.headers);
    darray_destroy(conn->info.buffer);
    darray_destroy(conn->read_buffer);
    darray_destroy(conn->write_buffer);
}

/*
 * reset all state on a connection
 */
void protocol_reset_conn(protocol_conn* conn)
{
    conn->state = PROTOCOL_STATE_READ_HANDSHAKE;
    conn->frag_msg.type = PROTOCOL_MSG_NONE;
    conn->frag_msg.msg_len = 0;
    conn->frag_msg.start_pos = 0;
    conn->num_fragments_read = 0;
    conn->info.resource_name = NULL;
    conn->frame_hdr.payload_len = -1;
    conn->valid_state.state = 0;
    conn->valid_state.codepoint = 0;
    conn->should_close = FALSE;
    conn->error_msg = NULL;
    conn->error_code = 0;
    conn->error_len = 0;

    int num_headers = darray_get_len(conn->info.headers);
    protocol_header* headers = darray_get_data(conn->info.headers);
    for (int i = 0; i < num_headers; i++)
    {
        headers[i].name = NULL;
        darray_clear(headers[i].values);
    }

    darray_clear(conn->info.headers);
    darray_clear(conn->info.buffer);
    darray_clear(conn->read_buffer);
    darray_clear(conn->write_buffer);
}

/*
 * parse the handshake from conn->info.buffer. On success, advances state from
 * PROTOCOL_STATE_READ_HANDSHAKE -> PROTOCOL_STATE_WRITE_HANDSHAKE
 */
protocol_handshake_result protocol_read_handshake(protocol_conn* conn)
{
    protocol_handshake* info = &conn->info;

    char* buf = darray_get_data(info->buffer);
    hhassert_pointer(buf);
    int length = darray_get_len(info->buffer);

    if (length < 4) return PROTOCOL_HANDSHAKE_CONTINUE;

    char* end_buf = buf + length;

    if (strncmp(&end_buf[-4], "\r\n\r\n", 4) != 0)
    {
        return PROTOCOL_HANDSHAKE_CONTINUE;
    }

    const char null_term = '\0';
    darray_append(&info->buffer, &null_term, 1);

    buf = parse_request_line(info, buf, end_buf);
    if (buf == NULL) goto read_err;

    buf = parse_headers(info, buf, end_buf);
    if (buf == NULL) goto read_err;

    if (strncmp(buf, "\r\n", 2) != 0) goto read_err;

    conn->state = PROTOCOL_STATE_WRITE_HANDSHAKE;
    return PROTOCOL_HANDSHAKE_SUCCESS;

read_err:
    return PROTOCOL_HANDSHAKE_FAIL;
}

/*
 * write the handshake response to conn->write_buffer.  must be called after
 * protocol_read_handshake.  on success, DOES NOT advance state.  This should
 * be done after the write buffer has actually be written to a socket.
 */
protocol_handshake_result protocol_write_handshake(
    protocol_conn* conn,
    const char* protocol,
    const char* extensions
)
{
    hhassert(conn->state == PROTOCOL_STATE_WRITE_HANDSHAKE);

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

    const char* upgrade = protocol_get_header_value(conn, "Upgrade", 0);
    if (upgrade == NULL || strcasecmp(upgrade, "websocket") != 0)
    {
        return PROTOCOL_HANDSHAKE_FAIL;
    }

    const char* connection = protocol_get_header_value(conn, "Connection", 0);
    if (connection == NULL || strcasecmp(connection, "Upgrade") != 0)
    {
        return PROTOCOL_HANDSHAKE_FAIL;
    }

    const char* web_sock_key = protocol_get_header_value(
        conn,
        "Sec-WebSocket-Key",
        0
    );
    if (web_sock_key == NULL) return PROTOCOL_HANDSHAKE_FAIL;
    if (strlen(web_sock_key) != 24) return PROTOCOL_HANDSHAKE_FAIL;

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
    num_encoded += base64_encode_blockend(
        &response_key[num_encoded],
        &encode_state
    );

    /* encode_blockend adds a \n... we want to null terminate */
    size_t response_key_len = num_encoded - 1;
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
     * written exactly total_len - 1 num
     */
    hhassert(num_written == (total_len - 1));
    darray_add_len(conn->write_buffer, total_len - 1);

    return PROTOCOL_HANDSHAKE_SUCCESS;
}

/*
 * Get the number of times this header appeared in the request
 */
int protocol_get_num_header_values(protocol_conn* conn, const char* name)
{
    protocol_header* headers = darray_get_data(conn->info.headers);
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
const char* protocol_get_header_value(
    protocol_conn* conn,
    const char* name,
    int index
)
{
    protocol_header* headers = darray_get_data(conn->info.headers);
    int len = darray_get_len(conn->info.headers);

    for (int i = 0; i < len; i++)
    {
        if (strcasecmp(name, headers[i].name) == 0)
        {
            hhassert(index >= 0);
            hhassert((size_t)index < darray_get_len(headers[i].values));
            char** values = darray_get_data(headers[i].values);
            return values[index];
        }
    }

    return NULL;
}

/*
 * Helper for easily getting the number of values the client sent in the
 * Sec-WebSocket-Protocol header
 */
int protocol_get_num_subprotocols(protocol_conn* conn)
{
    return protocol_get_num_header_values(conn, "Sec-WebSocket-Protocol");
}

/*
 * Helper for getting values of the Sec-WebSocket-Protocol header sent by
 * the client
 */
const char* protocol_get_subprotocol(protocol_conn* conn, int index)
{
    return protocol_get_header_value(conn, "Sec-WebSocket-Protocol", index);
}

/*
 * process a frame from the read buffer starting at start_pos into
 * conn->read_msg. read_msg will contain the read message if protocol_result
 * is PROTOCOL_RESULT_MESSAGE_FINISHED.  Otherwise, it is untouched.
 */
protocol_result protocol_read_msg(
    protocol_conn* conn,
    size_t* start_pos,
    protocol_msg* read_msg
)
{
    size_t pos = *start_pos;
    char* raw_buffer = darray_get_data(conn->read_buffer);
    char* data = &raw_buffer[pos];
    size_t data_len = darray_get_len(conn->read_buffer) - pos;
    char* data_end = data + data_len;

    protocol_result r = protocol_parse_frame_hdr(conn, &pos);
    if (r != PROTOCOL_RESULT_FRAME_FINISHED)
    {
        return r;
    }
    protocol_frame_hdr* hdr = &conn->frame_hdr;

    /*
     * protocol_parse_frame_hdr may haveadvanced buffer...
     * update data to reflect that
     */
    data = &raw_buffer[pos];

    BOOL fin = hdr->fin;
    BOOL msg_finished = FALSE;
    BOOL frame_finished = FALSE;
    protocol_opcode opcode = hdr->opcode;
    protocol_msg_type msg_type = hdr->msg_type;

    /*
     * read we're going to process either the rest of the message,
     * or an incomplete piece of the message
     */
    size_t len = hhmin(
        data_end - data,
        hdr->payload_len - hdr->payload_processed
    );

    protocol_offset_msg* msg = &conn->frag_msg;

    if (!fin)
    {
        /* this is a fragmented message */
        if (opcode != PROTOCOL_OPCODE_CONTINUATION)
        {
            /*
             * this is the first fragment in a fragmented message
             */
            if (hdr->payload_processed == 0)
            {
                hhassert(msg->msg_len == 0);

                msg->type = msg_type;
                msg->start_pos = pos;
                msg->msg_len = 0;
            }

            /*
             * mask data in place, no need to move it,
             * since this is the first fragment
             */
            mask_data(
                data,
                len,
                hdr->masking_key,
                hdr->payload_processed,
                (msg->type == PROTOCOL_MSG_TEXT),
                &conn->valid_state.state,
                &conn->valid_state.codepoint
            );
            hdr->payload_processed += len;
            msg->msg_len += len;
            pos += len;

            if (hdr->payload_len == hdr->payload_processed &&
                conn->valid_state.state != UTF8_REJECT)
            {
                hdr->payload_len = -1;
                frame_finished = TRUE;
            }
        }
        else
        {
            /*
             * this is a later fragment.
             */
            mask_and_move_data(
                &raw_buffer[msg->start_pos + msg->msg_len],
                data,
                len,
                hdr->masking_key,
                hdr->payload_processed,
                (msg->type == PROTOCOL_MSG_TEXT),
                &conn->valid_state.state,
                &conn->valid_state.codepoint
            );
            hdr->payload_processed += len;
            msg->msg_len += len;
            pos += len;

            if (hdr->payload_processed == hdr->payload_len &&
                conn->valid_state.state != UTF8_REJECT)
            {
                size_t end_pos = msg->start_pos + msg->msg_len;
                char* frame_end = &raw_buffer[end_pos];
                /*
                 * we just masked and moved the data from this frame in one
                 * step, now move the rest of the buffer
                 */
                char* remainder_start = data + len;
                size_t remainder_len = data_end - remainder_start;
                memmove(frame_end, remainder_start, remainder_len);

                /* our darray just got smaller */
                darray_add_len(
                    conn->read_buffer,
                    -(remainder_start - frame_end)
                );

                pos -= (remainder_start - frame_end);

                /* we just finished processing a frame... clear hdr */
                hdr->payload_len = -1;
                frame_finished = TRUE;
            }
        }

        (*start_pos) = pos;

        if (conn->valid_state.state == UTF8_REJECT)
        {
            handle_violation(
                conn,
                HH_ERROR_BAD_DATA,
                "text frame was not valid utf-8 text"
            );
            return PROTOCOL_RESULT_FAIL;
        }
        else if (frame_finished)
        {
            conn->num_fragments_read++;
            return PROTOCOL_RESULT_FRAME_FINISHED;
        }
        else
        {
            return PROTOCOL_RESULT_CONTINUE;
        }
    }
    else
    {
        if (msg->type != PROTOCOL_MSG_NONE &&
            opcode == PROTOCOL_OPCODE_CONTINUATION)
        {
            /*
             * this is the last fragment of a fragmented message.
             */
            mask_and_move_data(
                &raw_buffer[msg->start_pos + msg->msg_len],
                data,
                len,
                hdr->masking_key,
                hdr->payload_processed,
                (msg->type == PROTOCOL_MSG_TEXT),
                &conn->valid_state.state,
                &conn->valid_state.codepoint
            );
            msg->msg_len += len;
            hdr->payload_processed += len;
            pos += len;

            if (hdr->payload_processed == hdr->payload_len &&
                conn->valid_state.state != UTF8_REJECT)

            {
                size_t end_pos = msg->start_pos + msg->msg_len;
                char* frame_end = &raw_buffer[end_pos];

                /*
                 * we just masked and moved the data from this frame in one
                 * step, now move the rest of the buffer
                 */
                char* remainder_start = data + len;
                size_t remainder_len = data_end - remainder_start;
                memmove(frame_end, remainder_start, remainder_len);

                /* our darray just got smaller */
                darray_add_len(
                    conn->read_buffer,
                    -(remainder_start - frame_end)
                );

                pos -= (remainder_start - frame_end);

                /* we just finished processing a frame... clear hdr */
                hdr->payload_len = -1;

                /* we just processed a message, tell the caller */
                read_msg->type = msg->type;
                read_msg->msg_len = msg->msg_len;
                read_msg->data = &raw_buffer[msg->start_pos];

                /* we're no longer keeping track of a fragmented message */
                msg->type = PROTOCOL_MSG_NONE;
                msg->start_pos = 0;
                msg->msg_len = 0;

                /* we just read a full message, reset the fragment counter */
                conn->num_fragments_read = 0;

                /* reset utf8 validator state */
                conn->valid_state.state = 0;
                conn->valid_state.codepoint = 0;

                msg_finished = TRUE;
            }
        }
        else
        {
            /*
             * mask data in place, no need to move it,
             * since this is the only fragment
             */
            mask_data(
                data,
                len,
                hdr->masking_key,
                hdr->payload_processed,
                (msg_type == PROTOCOL_MSG_TEXT),
                &conn->valid_state.state,
                &conn->valid_state.codepoint
            );
            hdr->payload_processed += len;
            pos += len;

            if (hdr->payload_processed == hdr->payload_len &&
                conn->valid_state.state != UTF8_REJECT)
            {
                /* this is a non-fragmented message */
                read_msg->type = msg_type;
                read_msg->msg_len = hdr->payload_len;
                read_msg->data = &raw_buffer[hdr->data_start_pos];
                hdr->payload_len = -1;

                /* we just read a full message, reset the fragment counter */
                conn->num_fragments_read = 0;

                msg_finished = TRUE;
            }
        }

        (*start_pos) = pos;
        if (conn->valid_state.state == UTF8_REJECT ||
            (msg_finished && conn->valid_state.state != UTF8_ACCEPT))
        {
            handle_violation(
                conn,
                HH_ERROR_BAD_DATA,
                "text frame was not valid utf-8 text"
            );
            return PROTOCOL_RESULT_FAIL;
        }
        else if (msg_finished &&
                 read_msg->type == PROTOCOL_MSG_CLOSE &&
                 read_msg->msg_len >= 2)
        {
            if (!is_error_valid(hh_ntohs((*(uint16_t*)read_msg->data))))
            {
                handle_violation(
                    conn,
                    HH_ERROR_PROTOCOL,
                    "Invalid error code"
                );
                return PROTOCOL_RESULT_FAIL;
            }
            else if (read_msg->msg_len > 2 &&
                     !is_valid_utf8(&read_msg->data[2], read_msg->msg_len-2))
            {
                handle_violation(
                    conn,
                    HH_ERROR_PROTOCOL,
                    "Invalid utf-8 in close frame"
                );
                return PROTOCOL_RESULT_FAIL;
            }
            else
            {
                return PROTOCOL_RESULT_MESSAGE_FINISHED;
            }
        }
        else if (msg_finished)
        {
            /* reset utf8 validator state */
            conn->valid_state.state = 0;
            conn->valid_state.codepoint = 0;

            return PROTOCOL_RESULT_MESSAGE_FINISHED;
        }
        else
        {
            return PROTOCOL_RESULT_CONTINUE;
        }
    }
}

/*
 * put the message in write_msg on conn->write_buffer.
 * msg will be broken up into frames of size write_max_frame_size
 */
protocol_result protocol_write_msg(
    protocol_conn* conn,
    protocol_msg* write_msg
)
{
    protocol_msg_type msg_type = write_msg->type;
    int64_t msg_len = write_msg->msg_len;
    char* msg_data = write_msg->data;
    int64_t max_frame_size = conn->settings->write_max_frame_size;
    if (max_frame_size < 0) max_frame_size = INT64_MAX;

    protocol_opcode opcode = opcode_from_msg_type(msg_type);

    /* please provide valid inputs... */
    if (msg_len < 0)
    {
        return PROTOCOL_RESULT_FAIL;
    }

    if (msg_len > max_frame_size && !multiple_frames_allowed(opcode))
    {
        return PROTOCOL_RESULT_FAIL;
    }

    /* please provide valid inputs... */
    if (opcode == PROTOCOL_OPCODE_TEXT && !is_valid_utf8(msg_data, msg_len))
    {
        return PROTOCOL_RESULT_FAIL;
    }

    int64_t num_written = 0;
    int64_t payload_num_written = 0;
    do
    {
        int64_t num_remaining = msg_len - payload_num_written;
        int64_t payload_len = hhmin(num_remaining, max_frame_size);
        int num_extra_len_bytes = get_num_extra_len_bytes(payload_len);
        size_t total_frame_len = 2 + num_extra_len_bytes + payload_len;

        /* make sure there is enough room for this frame */
        darray_ensure(&conn->write_buffer, total_frame_len);

        /* get the data */
        char* data = darray_get_data(conn->write_buffer);
        data = &data[darray_get_len(conn->write_buffer)];
        const char* start_data = data;

        /* determine if this is the fin frame */
        char first_byte_mask = ((num_written + payload_len) >= msg_len) ?
            0x80 : 0x00;

        /* write the first byte to the buffer */
        char first_byte = first_byte_mask | opcode;
        *data = first_byte;
        data++;

        /* write the payload length to the buffer */
        uint16_t short_len;
        uint64_t long_len;
        uint16_t* short_data;
        uint64_t* long_data;
        switch (num_extra_len_bytes)
        {
        case 0:
            *data = (char)payload_len;
            data++;
            break;

        case 2:
            *data = 126;
            data++;
            short_len = hh_htons((uint16_t)payload_len);
            short_data = (uint16_t*)data;
            *short_data = short_len;
            data += sizeof(uint16_t);
            break;

        case 8:
            *data = 127;
            data++;
            long_len = hh_htonll((uint64_t)payload_len);
            long_data = (uint64_t*)data;
            *long_data = long_len;
            data += sizeof(uint64_t);
            break;

        default:
            hhassert(0);
            break;
        }

    #ifdef DEBUG
        hhassert(start_data + 2 + num_extra_len_bytes == data);
    #else
        hhunused(start_data);
    #endif

        /* write the payload to the buffer */
        memcpy(data, msg_data, payload_len);

        /* bookkeeping */
        darray_add_len(conn->write_buffer, total_frame_len);
        msg_data += payload_len;
        payload_num_written += payload_len;
        num_written += total_frame_len;
        opcode = PROTOCOL_OPCODE_CONTINUATION;
    } while (payload_num_written < msg_len);

    return PROTOCOL_RESULT_MESSAGE_FINISHED;
}

BOOL protocol_is_data(protocol_msg_type msg_type)
{
    switch (msg_type)
    {
        case PROTOCOL_MSG_NONE:
        case PROTOCOL_MSG_CLOSE:
        case PROTOCOL_MSG_PING:
        case PROTOCOL_MSG_PONG:
            return FALSE;
        case PROTOCOL_MSG_TEXT:
        case PROTOCOL_MSG_BINARY:
            return TRUE;
        default:
            return FALSE;
    }
}

BOOL protocol_is_control(protocol_msg_type msg_type)
{
    switch (msg_type)
    {
        case PROTOCOL_MSG_NONE:
        case PROTOCOL_MSG_TEXT:
        case PROTOCOL_MSG_BINARY:
            return FALSE;
        case PROTOCOL_MSG_CLOSE:
        case PROTOCOL_MSG_PING:
        case PROTOCOL_MSG_PONG:
            return TRUE;
        default:
            return FALSE;
    }
}

