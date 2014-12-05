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

typedef enum
{
    PROTOCOL_ENDPOINT_CLIENT,
    PROTOCOL_ENDPOINT_SERVER
} protocol_endpoint;

#define HEADER_PROTOCOL "Sec-WebSocket-Protocol"
#define HEADER_EXTENSION "Sec-WebSocket-Extensions"
#define HEADER_KEY "Sec-WebSocket-Key"
#define KEY_LEN 16
#define BASE64_MAX_OUTPUT_LEN(n) ((4 * (((n) + 3) / 3)) + 1)

static const char g_header_template[] = "%s: %s\r\n";
#define HEADER_TEMPLATE_LEN 5 /* colon,<space>\r,\n,null  */

static bool is_valid_opcode(protocol_opcode opcode)
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

static bool multiple_frames_allowed(protocol_opcode opcode)
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
    }

    hhassert(0);
    return PROTOCOL_OPCODE_TEXT;
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

/*
 * Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 * See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 */

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint32_t utf8d[] = {
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

static HH_INLINE uint32_t utf8_decode(uint32_t* state, uint32_t* codep,
                                      uint32_t byte)
{
    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ?
        (byte & 0x3fu) | (*codep << 6) :
        (0xff >> type) & (byte);

    *state = utf8d[256 + *state*16 + type];
    return *state;
}

static bool is_valid_utf8(const char* str, int64_t length)
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

static void mask_data(char* data, size_t len, char* mask_key,
                      int64_t mask_index, bool validate_utf8, uint32_t* state,
                      uint32_t* codepoint)
{
    uint8_t* d = (uint8_t*)data;

    for (size_t i = 0; i < len; i++)
    {
        if (mask_key != NULL)
        {
            d[i] ^= mask_key[mask_index++ % 4];
        }
        if (validate_utf8)
        {
            utf8_decode(state, codepoint, d[i]);
            if (*state == UTF8_REJECT) return;
        }
    }
}

static void mask_and_move_data(char* dest, char* src, size_t len,
                               char* mask_key_ptr, int64_t mask_index,
                               bool validate_utf8, uint32_t* state,
                               uint32_t* codepoint)
{
    /*
     * the move we're about to do might overwrite the data at
     * mask_key_ptr... copy the key into another place
     */
    char mask_key[4];
    if(mask_key_ptr != NULL)
    {
        memcpy(mask_key, mask_key_ptr, sizeof(mask_key));
    }

    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < len; i++)
    {
        d[i] = (uint8_t)src[i];
        if (mask_key_ptr != NULL)
        {
            d[i] ^= mask_key[mask_index++ % 4];
        }
        if (validate_utf8)
        {
            utf8_decode(state, codepoint, d[i]);
            if (*state == UTF8_REJECT) return;
        }
    }
}

static bool is_comma_delimited_header(const char* header)
{
    static const char* comma_headers[] =
    {
        HEADER_PROTOCOL,
        HEADER_EXTENSION,
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

static char* parse_http_version(char* buf, char* end_buf)
{
    const int http_len = 5;
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

    return buf;
}

static char* parse_http_line(protocol_handshake* info, char* buf,
                             char* end_buf, protocol_endpoint type)
{
    const int get_len = 4;

    buf = eat_whitespace(buf);
    if ((buf + get_len) >= end_buf) return NULL;

    char* word_end;
    char* end_uri;
    switch (type)
    {
    case PROTOCOL_ENDPOINT_SERVER:
        if (strncmp(buf, "GET ", get_len) != 0) return NULL;
        buf += get_len;
        end_uri = strchr(buf, ' ');
        if (end_uri == NULL) return NULL;
        *end_uri = '\0'; /* make this a string */
        info->resource = buf;
        buf = end_uri + 1;
        buf = parse_http_version(buf, end_buf);
        if (buf == NULL) return NULL;
        if ((buf + 2) >= end_buf) return NULL;
        if (strncmp(buf, "\r\n", 2) != 0) return NULL;
        buf += 2;
        break;
    case PROTOCOL_ENDPOINT_CLIENT:
        buf = parse_http_version(buf, end_buf);
        buf++;
        if (buf >= end_buf) return NULL;
        word_end = strchr(buf, ' ');
        if (word_end == NULL) return NULL;
        *word_end = '\0'; /* make this a string */
        if ((word_end - buf != 3) || strncmp(buf, "101", 3) != 0) return NULL;
        buf = word_end + 1;
        char* end_line = strstr(buf, "\r\n");
        if (end_line == NULL) return NULL;
        buf = end_line + 2;
        break;
    }

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
    size_t num_headers = darray_get_len(info->headers);

    for (unsigned i = 0; i < num_headers; i++)
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
        header = darray_get_last_addr(info->headers);
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

static char* parse_headers(protocol_handshake* info, char* buf, char* end_buf)
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

static void handle_violation(protocol_conn* conn, uint16_t code,
                             const char* msg)
{
    conn->error_code = code;
    conn->error_msg = msg;
    conn->error_len = (int)strlen(msg); /* terminator left out on purpose */
}

static protocol_result
protocol_parse_frame_hdr(protocol_conn* conn, bool expect_mask,
                         size_t* pos_ptr)
{
    protocol_frame_hdr* hdr = &conn->frame_hdr;
    if (hdr->payload_len >= 0) return PROTOCOL_RESULT_FRAME_FINISHED;

    size_t pos = *pos_ptr;
    char* raw_buffer = darray_get_data(conn->read_buffer);
    char* data = &raw_buffer[pos];
    size_t data_len = darray_get_len(conn->read_buffer) - pos;

    char* data_end = data + data_len;

    if (data_len < 2) return PROTOCOL_RESULT_CONTINUE;

    unsigned char first = (unsigned char)(*data);
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
        handle_violation(conn, HH_ERROR_PROTOCOL,
                         "Out of band continuation frame");
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
        handle_violation(conn, HH_ERROR_PROTOCOL,
                         "Out of band text or binary frame");
        return PROTOCOL_RESULT_FAIL;
    }

    /* control frames must not be fragmented */
    if (!fin &&
        !(opcode == PROTOCOL_OPCODE_TEXT ||
          opcode == PROTOCOL_OPCODE_BINARY ||
          opcode == PROTOCOL_OPCODE_CONTINUATION))

    {
        handle_violation(conn, HH_ERROR_PROTOCOL, "Control frames must not be"
                         "fragmented");
        return PROTOCOL_RESULT_FAIL;
    }

    data++;
    unsigned char second = (unsigned char)(*data);
    bool is_masked = ((second & 0x80) != 0);

    /* all client frames must be masked */
    if (is_masked != expect_mask)
    {
        const char* msg = (expect_mask) ? "All client frames must be masked"
                                        : "Server frames must not be masked";
        handle_violation(conn, HH_ERROR_PROTOCOL, msg);
        return PROTOCOL_RESULT_FAIL;
    }

    int64_t payload_len = 0;
    unsigned char first_len = (second & 0x7f);

    /* control frames must be <=125 bytes */
    if (first_len > 125 &&
        !(opcode == PROTOCOL_OPCODE_TEXT ||
          opcode == PROTOCOL_OPCODE_BINARY ||
          opcode == PROTOCOL_OPCODE_CONTINUATION))
    {
        handle_violation(conn, HH_ERROR_PROTOCOL, "Control frames must be"
                         "<=125 bytes");
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
        uint16_t temp;
        memcpy(&temp, data, sizeof(temp));
        payload_len = hh_ntohs(temp);
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
        uint64_t temp;
        memcpy(&temp, data, sizeof(temp));
        payload_len = (int64_t)hh_ntohll(temp);
        data += sizeof(int64_t);
    }

    if (is_masked && data + sizeof(uint32_t) > data_end)
    {
        return PROTOCOL_RESULT_CONTINUE;
    }

    char* masking_key = NULL;

    if (is_masked)
    {
        masking_key = data;
        data += sizeof(uint32_t);
    }

    protocol_offset_msg* msg = &conn->frag_msg;
    if (conn->settings->read_max_num_frames != -1 &&
        conn->num_fragments_read >= conn->settings->read_max_num_frames)
    {
        handle_violation(conn, HH_ERROR_POLICY_VIOLATION,
                         "client sent too many frames in one message");
        return PROTOCOL_RESULT_FAIL;
    }

    int64_t read_max_msg_size = conn->settings->read_max_msg_size;
    /*
     * cast below is okay because we know read_max_msg_size is
     * non-negative
     */
    if (read_max_msg_size >= 0 &&
        msg->msg_len + payload_len > read_max_msg_size)

    {
        handle_violation(conn, HH_ERROR_LARGE_MESSAGE, "client sent message"
                         "that was too large");
        return PROTOCOL_RESULT_FAIL;
    }

    hdr->opcode = opcode;
    hdr->msg_type = msg_type;
    hdr->payload_processed = 0;
    hdr->payload_len = payload_len;
    if (masking_key != NULL)
    {
        memcpy(hdr->masking_key, masking_key, sizeof(hdr->masking_key));
    }
    hdr->masked = is_masked;
    hdr->fin = (fin != 0);
    pos += (size_t)(data - &raw_buffer[pos]);
    hdr->data_start_pos = pos;
    *pos_ptr = pos;

    return PROTOCOL_RESULT_FRAME_FINISHED;
}

/* create a protocol_conn on the heap */
protocol_conn* protocol_create_conn(protocol_settings* settings, void* userdata)
{
    protocol_conn* conn = hhmalloc(sizeof(*conn));
    if (conn == NULL ||
        protocol_init_conn(conn, settings, userdata) < 0)
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
int protocol_init_conn(protocol_conn* conn, protocol_settings* settings,
                       void* userdata)
{
    size_t init_buf_len = settings->init_buf_len;
    memset(conn, 0, sizeof(*conn));
    conn->settings = settings;
    conn->state = PROTOCOL_STATE_READ_HANDSHAKE;
    conn->info.resource = NULL;
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
    conn->error_msg = NULL;
    conn->error_code = 0;
    conn->error_len = 0;
    conn->userdata = userdata;

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
        size_t num_headers = darray_get_len(conn->info.headers);
        protocol_header* headers = darray_get_data(conn->info.headers);
        for (unsigned i = 0; i < num_headers; i++)
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
    conn->info.resource = NULL;
    conn->frame_hdr.payload_len = -1;
    conn->valid_state.state = 0;
    conn->valid_state.codepoint = 0;
    conn->error_msg = NULL;
    conn->error_code = 0;
    conn->error_len = 0;

    size_t num_headers = darray_get_len(conn->info.headers);
    protocol_header* headers = darray_get_data(conn->info.headers);
    for (unsigned i = 0; i < num_headers; i++)
    {
        headers[i].name = NULL;
        /*
         * destroy here instead of clear.  If we just clear, we can
         * end up leaking values because num_headers will change
         * and the loop in protocol_deinit_conn won't free some values
         */
        darray_destroy(headers[i].values);
        headers[i].values = NULL;
    }

    darray_clear(conn->info.headers);
    darray_clear(conn->info.buffer);
    darray_clear(conn->read_buffer);
    darray_clear(conn->write_buffer);
}

static protocol_handshake_result
protocol_read_handshake(protocol_conn* conn, protocol_endpoint type)
{
    protocol_handshake* info = &conn->info;

    char* buf = darray_get_data(info->buffer);
    hhassert_pointer(buf);
    size_t length = darray_get_len(info->buffer);

    if (length < 4) return PROTOCOL_HANDSHAKE_CONTINUE;

    if (type == PROTOCOL_ENDPOINT_SERVER &&
        conn->settings->max_handshake_size > 0 &&
        length > (size_t)conn->settings->max_handshake_size)
    {
        return PROTOCOL_HANDSHAKE_FAIL_TOO_LARGE;
    }

    bool found_end = false;
    for (size_t i = 0; i < length-3; i++)
    {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
        {
            found_end = true;
            break;
        }
    }

    if (!found_end)
    {
        return PROTOCOL_HANDSHAKE_CONTINUE;
    }

    const char null_term = '\0';
    buf = darray_append(&info->buffer, &null_term, 1);

    /* end_buf does not include the null_term */
    char* end_buf = buf + length;

    buf = parse_http_line(info, buf, end_buf, type);
    if (buf == NULL) goto read_err;

    buf = parse_headers(info, buf, end_buf);
    if (buf == NULL) goto read_err;

    if (strncmp(buf, "\r\n", 2) != 0) goto read_err;

    switch (type)
    {
    case PROTOCOL_ENDPOINT_SERVER:
        hhassert(conn->state == PROTOCOL_STATE_READ_HANDSHAKE);
        conn->state = PROTOCOL_STATE_WRITE_HANDSHAKE;
        break;
    case PROTOCOL_ENDPOINT_CLIENT:
        hhassert(conn->state == PROTOCOL_STATE_READ_HANDSHAKE);
        conn->state = PROTOCOL_STATE_CONNECTED;
        buf += 2;
        hhassert(buf <= end_buf);

        /*
         * for clients, it's legal for the server to have a message
         * immediately following the handshake. If the server has done this,
         * just copy the data directly to the read buffer.
         */
        if (buf < end_buf)
        {
            size_t len = (size_t)(end_buf - buf);
            darray_append(&conn->read_buffer, buf, len);
        }
        break;
    }
    return PROTOCOL_HANDSHAKE_SUCCESS;

read_err:
    return PROTOCOL_HANDSHAKE_FAIL;
}

/*
 * parse the handshake from conn->info.buffer. On success, advances state from
 * PROTOCOL_STATE_READ_HANDSHAKE -> PROTOCOL_STATE_WRITE_HANDSHAKE
 */
protocol_handshake_result protocol_read_handshake_request(protocol_conn* conn)
{
    return protocol_read_handshake(conn, PROTOCOL_ENDPOINT_SERVER);
}

/*
 * write the handshake response to conn->write_buffer.  must be called after
 * protocol_read_handshake_request.  on success, DOES NOT advance state.  This should
 * be done after the write buffer has actually be written to a socket.
 */
protocol_handshake_result
protocol_write_handshake_response(protocol_conn* conn, const char* protocol,
                                  const char** extensions)
{
    hhassert(conn->state == PROTOCOL_STATE_WRITE_HANDSHAKE);

    /* this constant comes straight from RFC 6455 */
    static const char key_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    static const char protocol_template[] = HEADER_PROTOCOL ": %s\r\n";
    static const char extension_template[] =
        HEADER_EXTENSION ": %s\r\n";
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

    const char* web_sock_key =
        protocol_get_header_value(conn, "Sec-WebSocket-Key", 0);
    if (web_sock_key == NULL) return PROTOCOL_HANDSHAKE_FAIL;
    if (strlen(web_sock_key) != 24) return PROTOCOL_HANDSHAKE_FAIL;

    if (protocol_get_header_value(conn, "Host", 0) == NULL)
    {
        return PROTOCOL_HANDSHAKE_FAIL;
    }

    char sha_buf[64];
    int num_written = snprintf(sha_buf, sizeof(sha_buf),
        "%s%s",
        web_sock_key,
        key_guid
    );

    hhassert(num_written >= 0);

    /* create our response SHA1 */
    char sha_result[SHA1HashSize];
    SHA1Context context;
    SHA1Reset(&context);
    SHA1Input(&context, (uint8_t*)sha_buf, (unsigned)num_written);
    SHA1Result(&context, (uint8_t*)sha_result);

    /* encode our SHA1 as base64 */
    char response_key[BASE64_MAX_OUTPUT_LEN(SHA1HashSize)];
    base64_encodestate encode_state;
    base64_init_encodestate(&encode_state);
    int num_encoded = base64_encode_block(sha_result, SHA1HashSize,
                                          response_key, &encode_state);
    num_encoded += base64_encode_blockend(&response_key[num_encoded],
                                          &encode_state);
    size_t response_key_len = (size_t)num_encoded - 1;

    /*
     * put the whole response on the connection's write buffer
     */

    /* this should be the first thing ever written to a client */
    hhassert(darray_get_len(conn->write_buffer) == 0);

    /* -2 because of %s in response_template, +2 because of trailing \r\n */
    unsigned total_len =
        (unsigned)((sizeof(response_template)-2) + (response_key_len)+2);

    if (protocol != NULL)
    {
        total_len += strlen(protocol);

        /* -1 for terminator, -2 for %s in protocol_template */
        total_len += sizeof(protocol_template) - 1 - 2;
    }

    if (extensions != NULL)
    {
        const char** exts = extensions;
        while ((*exts) != NULL)
        {
            total_len += strlen(*exts);

            /* -1 for terminator, -2 for %s in extension_template */
            total_len += sizeof(extension_template) - 1 - 2;
            exts++;
        }
    }

    char* buf = darray_ensure(&conn->write_buffer, total_len);

    /* write out the mandatory stuff */
    num_written = 0;
    num_written = snprintf(buf, total_len, response_template, response_key);

    /* write out protocol header, if necessary */
    if (protocol != NULL)
    {
        num_written += snprintf(&buf[num_written],
                                total_len - (unsigned)num_written,
                                protocol_template, protocol);
    }

    /* write out the extension header, if necessary */
    if (extensions != NULL)
    {
        const char** exts = extensions;
        while ((*exts) != NULL)
        {
            num_written += snprintf(&buf[num_written],
                                    total_len - (unsigned)num_written,
                                    extension_template, *exts);
            exts++;
        }
    }

    /* write out the closing CRLF */
    num_written += snprintf(&buf[num_written],
                            total_len - (unsigned)num_written, "\r\n");

    /*
     * num_written doesn't include the null terminator, so we should have
     * written exactly total_len - 1 num
     */
    hhassert((unsigned)num_written == (total_len - 1));
    darray_add_len(conn->write_buffer, total_len - 1);

    conn->state = PROTOCOL_STATE_CONNECTED;

    return PROTOCOL_HANDSHAKE_SUCCESS;
}

/*
 * parse the handshake response from the server from conn->info.buffer
 * On success, advances state from
 * PROTOCOL_STATE_READ_HANDSHAKE -> PROTOCOL_STATE_CONNECTED
 */
protocol_handshake_result
protocol_read_handshake_response(protocol_conn* conn)
{
    return protocol_read_handshake(conn, PROTOCOL_ENDPOINT_CLIENT);
}

static size_t append_key_header(darray** array, protocol_conn* conn)
{
    hhassert(conn->settings->rand_func != NULL);

    /* generate random 16 byte value */
    char request_key[KEY_LEN];
    for (unsigned int i = 0; i < (KEY_LEN / sizeof(uint32_t)); i++)
    {
        uint32_t val = conn->settings->rand_func(conn);
        memcpy(request_key + (i * sizeof(uint32_t)), &val, sizeof(val));
    }

    /* encode value as base64  */
    char request_key_base64[BASE64_MAX_OUTPUT_LEN(KEY_LEN)];
    base64_encodestate encode_state;
    base64_init_encodestate(&encode_state);
    int num_encoded = base64_encode_block(request_key, KEY_LEN,
                                          request_key_base64, &encode_state);
    num_encoded += base64_encode_blockend(&request_key_base64[num_encoded],
                                          &encode_state);

    /* write out header value  */
    size_t header_len =
        strlen(HEADER_KEY) + strlen(request_key_base64) + HEADER_TEMPLATE_LEN;
    char* buf = darray_ensure(array, (unsigned)header_len);
    size_t write_pos = darray_get_len((*array));
    int num_written = snprintf(&buf[write_pos], header_len,
                                  g_header_template, HEADER_KEY,
                                  request_key_base64);
    hhassert(num_written >= 0);
    darray_add_len((*array), (unsigned)num_written);

    return (size_t)num_written;
}

static size_t append_headers_list(darray** array, const char* key,
                                  const char** values)
{
    size_t write_pos = darray_get_len((*array));
    size_t total_written = 0;
    size_t key_len = strlen(key);
    while ((*values) != NULL)
    {
        /* 4 for colon and newlines in header_template, and null term */
        size_t header_len = key_len + strlen(*values) + HEADER_TEMPLATE_LEN;
        char* buf = darray_ensure(array, (unsigned)header_len);
        int num_written = snprintf(&buf[write_pos], header_len,
                                      g_header_template, key, *values);
        hhassert(num_written >= 0);
        values++;
        darray_add_len((*array), (unsigned)num_written);
        write_pos += (unsigned)num_written;
        total_written += (unsigned)num_written;
    }

    return total_written;
}

/*
 * write the client handshake request to conn->write_buffer. On success,
 * advances state to PROTOCOL_STATE_READ_HANDSHAKE
 */
protocol_handshake_result
protocol_write_handshake_request(
        protocol_conn* conn,
        const char* resource,
        const char* host,
        const char** protocols, /* NULL terminated (optional) */
        const char** extensions, /* NULL terminated (optional) */
        const char** extra_headers /* NULL terminated, (optional) */
)
{
    static const char http_line[] =
        "GET %s HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Host: %s\r\n";

    hhassert(darray_get_len(conn->write_buffer) == 0);

    /* -4 for %s */
    size_t http_line_size =
        sizeof(http_line) - 4 + strlen(resource) + strlen(host);
    char* buf = darray_ensure(&conn->write_buffer, (unsigned)http_line_size);

    int total_written = 0;
    total_written += snprintf(&buf[total_written], http_line_size, http_line,
                              resource, host);
    hhassert(total_written >= 0);
    darray_add_len(conn->write_buffer, (unsigned)total_written);

    /* write Sec-WebSocket-Key header */
    total_written += append_key_header(&conn->write_buffer, conn);

    if (protocols != NULL)
    {
        /* write Sec-WebSocket-Protocol header */
        total_written += append_headers_list(&conn->write_buffer,
                                             HEADER_PROTOCOL,
                                             protocols);
    }

    if (extensions != NULL)
    {
        /* write Sec-WebSocket-Extensions header  */
        total_written += append_headers_list(&conn->write_buffer,
                                             HEADER_EXTENSION, extensions);
    }

    /* buf could have changed */
    buf = darray_get_data(conn->write_buffer);

    /* write any other headers the user provided */
    /* TODO: prettify this, combine with append_header_list */
    while (extra_headers != NULL && (*extra_headers) != NULL)
    {
        size_t key_len = strlen(*extra_headers);
        const char* key = *extra_headers;
        extra_headers++;
        hhassert((*extra_headers) != NULL);
        const char* value = *extra_headers;
        size_t value_len = strlen(*extra_headers);
        size_t header_len = key_len + value_len + HEADER_TEMPLATE_LEN;

        buf = darray_ensure(&conn->write_buffer, (unsigned)header_len);
        int num_written = snprintf(&buf[total_written],
                                      (unsigned)header_len,
                                      g_header_template, key, value);
        hhassert(num_written >= 0);
        darray_add_len(conn->write_buffer, (unsigned)num_written);
        total_written += num_written;
        extra_headers++;
    }

    /* write out the closing CRLF */
    const char closing_chars[] = "\r\n";
    size_t close_size = sizeof(closing_chars);

    /* +1 to add null term back in */
    buf = darray_ensure(&conn->write_buffer, close_size);
    total_written += snprintf(&buf[total_written], close_size,
                              closing_chars);
    darray_add_len(conn->write_buffer, close_size - 1);

    hhassert((size_t)total_written == darray_get_len(conn->write_buffer));
    conn->state = PROTOCOL_STATE_READ_HANDSHAKE;

    return PROTOCOL_HANDSHAKE_SUCCESS;
}

/*
 * Call this before reading data into the read buffer. If the state is
 * PROTOCOL_STATE_READ_HANDSHAKE, darray_ensure will be called on the
 * protocol_handshake buffer, otherwise it will be called on the read_buffer.
 * Will return a pointer to the buffer that should be read into.
 */
char* protocol_prepare_read(protocol_conn* conn, size_t ensure_len)
{
    /*
     * figure out which buffer to read into.  We have a separate buffer for
     * handshake info we want to keep around for the duration of the connection
     */
    darray** read_buffer;
    if (conn->state == PROTOCOL_STATE_READ_HANDSHAKE)
    {
        read_buffer = &conn->info.buffer;
    }
    else
    {
        read_buffer = &conn->read_buffer;
    }

    size_t end_pos = darray_get_len((*read_buffer));

    char* buf = darray_ensure(read_buffer, ensure_len);
    return &buf[end_pos];
}

/*
 * Call this after reading data into the read buffer. Will add num_read to
 * the size of the darray
 */
void protocol_update_read(protocol_conn* conn, size_t num_read)
{
    darray** read_buffer;
    if (conn->state == PROTOCOL_STATE_READ_HANDSHAKE)
    {
        read_buffer = &conn->info.buffer;
    }
    else
    {
        read_buffer = &conn->read_buffer;
    }

    darray_add_len((*read_buffer), num_read);
}
/*
 * Get the the field name of one of the headers sent by the client
 */
const char* protocol_get_header_name(protocol_conn* conn, unsigned index)
{
    hhassert(index < darray_get_len(conn->info.headers));
    protocol_header* headers = darray_get_data(conn->info.headers);
    return headers[index].name;
}

/*
 * Get the darray of values (type char*) for a given header index
 */
const darray* protocol_get_header_values(protocol_conn* conn, unsigned index)
{
    hhassert(index < darray_get_len(conn->info.headers));
    protocol_header* headers = darray_get_data(conn->info.headers);
    return headers[index].values;
}

/*
 * Get the number of headers sent by the client any index less than this
 * can be used in protocol_get_header_name and protocol_get_header_values
 */
unsigned protocol_get_num_headers(protocol_conn* conn)
{
    return darray_get_len(conn->info.headers);
}

/*
 * Get the number of times this header appeared in the request
 */
unsigned protocol_get_num_header_values(protocol_conn* conn, const char* name)
{
    protocol_header* headers = darray_get_data(conn->info.headers);
    size_t len = darray_get_len(conn->info.headers);

    for (unsigned i = 0; i < len; i++)
    {
        if (strcasecmp(name, headers[i].name) == 0)
        {
            return (unsigned)darray_get_len(headers[i].values);
        }
    }

    return 0;
}

/*
 * Get one of the values retrieved for a header
 */
const char* protocol_get_header_value(protocol_conn* conn, const char* name,
                                      unsigned index)
{
    protocol_header* headers = darray_get_data(conn->info.headers);
    size_t len = darray_get_len(conn->info.headers);

    for (unsigned i = 0; i < len; i++)
    {
        if (strcasecmp(name, headers[i].name) == 0)
        {
            hhassert(index < darray_get_len(headers[i].values));
            char** val = darray_get_elem_addr(headers[i].values, index);
            return *val;
        }
    }

    return NULL;
}

/*
 * Helper for easily getting the number of values the client sent in the
 * Sec-WebSocket-Protocol header
 */
unsigned protocol_get_num_subprotocols(protocol_conn* conn)
{
    return protocol_get_num_header_values(conn, HEADER_PROTOCOL);
}

/*
 * Helper for getting values of the Sec-WebSocket-Protocol header sent by
 * the client
 */
const char* protocol_get_subprotocol(protocol_conn* conn, unsigned index)
{
    return protocol_get_header_value(conn, HEADER_PROTOCOL, index);
}

/*
 * Helper for easily getting the number of values the client sent in the
 * Sec-WebSocket-Extensions header
 */
unsigned protocol_get_num_extensions(protocol_conn* conn)
{
    return protocol_get_num_header_values(conn, HEADER_EXTENSION);
}

/*
 * Helper for getting values of the Sec-WebSocket-Extensions header sent by
 * the client
 */
const char* protocol_get_extension(protocol_conn* conn, unsigned index)
{
    return protocol_get_header_value(conn, HEADER_EXTENSION, index);
}

/*
 * Helper for getting the resource name sent by the client
 */
const char* protocol_get_resource(protocol_conn* conn)
{
    return conn->info.resource;
}

/*
 * process a frame from the read buffer starting at start_pos into
 * conn->read_msg. read_msg will contain the read message if protocol_result
 * is PROTOCOL_RESULT_MESSAGE_FINISHED.  Otherwise, it is untouched.
 */
static protocol_result
protocol_read_msg(protocol_conn* conn, size_t* start_pos, bool expect_mask,
                  protocol_msg* read_msg)
{
    size_t pos = *start_pos;
    char* raw_buffer = darray_get_data(conn->read_buffer);
    char* data = &raw_buffer[pos];
    size_t data_len = darray_get_len(conn->read_buffer) - pos;
    char* data_end = data + data_len;

    protocol_result r = protocol_parse_frame_hdr(conn, expect_mask, &pos);
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

    bool fin = hdr->fin;
    bool msg_finished = false;
    bool frame_finished = false;
    protocol_opcode opcode = hdr->opcode;
    protocol_msg_type msg_type = hdr->msg_type;
    char* masking_key = (hdr->masked) ? hdr->masking_key : NULL;

    /*
     * we're going to process either the rest of the message,
     * or an incomplete piece of the message
     */
    hhassert(data_end >= data);
    hhassert(hdr->payload_len >= hdr->payload_processed);
    size_t len = (size_t)hhmin(data_end - data,
                       hdr->payload_len - hdr->payload_processed);

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
            mask_data(data, len, masking_key, hdr->payload_processed,
                      (msg->type == PROTOCOL_MSG_TEXT),
                      &conn->valid_state.state,
                      &conn->valid_state.codepoint);
            hdr->payload_processed += len;
            msg->msg_len += len;
            pos += len;

            if (hdr->payload_len == hdr->payload_processed &&
                conn->valid_state.state != UTF8_REJECT)
            {
                hdr->payload_len = -1;
                frame_finished = true;
            }
        }
        else
        {
            /*
             * this is a later fragment.
             */
            mask_and_move_data(&raw_buffer[msg->start_pos+(size_t)msg->msg_len],
                               data, len, masking_key, hdr->payload_processed,
                               (msg->type == PROTOCOL_MSG_TEXT),
                               &conn->valid_state.state,
                               &conn->valid_state.codepoint);
            hdr->payload_processed += len;
            msg->msg_len += len;
            pos += len;

            if (hdr->payload_processed == hdr->payload_len &&
                conn->valid_state.state != UTF8_REJECT)
            {
                /* we just finished processing a frame... clear hdr */
                hdr->payload_len = -1;
                frame_finished = true;
            }
        }

        (*start_pos) = pos;

        if (conn->valid_state.state == UTF8_REJECT)
        {
            handle_violation(conn, HH_ERROR_BAD_DATA,
                             "text frame was not valid utf-8 text");
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
            mask_and_move_data(&raw_buffer[msg->start_pos+(size_t)msg->msg_len],
                               data, len, masking_key, hdr->payload_processed,
                               (msg->type == PROTOCOL_MSG_TEXT),
                               &conn->valid_state.state,
                               &conn->valid_state.codepoint);
            msg->msg_len += len;
            hdr->payload_processed += len;
            pos += len;

            if (hdr->payload_processed == hdr->payload_len &&
                conn->valid_state.state != UTF8_REJECT)

            {
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

                msg_finished = true;
            }
        }
        else
        {
            /*
             * mask data in place, no need to move it,
             * since this is the only fragment
             */
            mask_data(data, len, hdr->masking_key, hdr->payload_processed,
                      (msg_type == PROTOCOL_MSG_TEXT),
                      &conn->valid_state.state,
                      &conn->valid_state.codepoint);

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

                msg_finished = true;
            }
        }

        (*start_pos) = pos;
        if (conn->valid_state.state == UTF8_REJECT ||
            (msg_finished && conn->valid_state.state != UTF8_ACCEPT))
        {
            handle_violation(conn, HH_ERROR_BAD_DATA,
                             "text frame was not valid utf-8 text");
            return PROTOCOL_RESULT_FAIL;
        }
        else if (msg_finished &&
                 read_msg->type == PROTOCOL_MSG_CLOSE &&
                 read_msg->msg_len >= 2)
        {
            uint16_t temp;
            memcpy(&temp, read_msg->data, sizeof(temp));
            if (!error_is_valid(hh_ntohs(temp)))
            {
                handle_violation(conn, HH_ERROR_PROTOCOL,
                                 "Invalid error code");
                return PROTOCOL_RESULT_FAIL;
            }
            else if (read_msg->msg_len > 2 &&
                     !is_valid_utf8(&read_msg->data[2],
                                    (int64_t)read_msg->msg_len-2))
            {
                handle_violation(conn, HH_ERROR_PROTOCOL,
                                 "Invalid utf-8 in close frame");
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

protocol_result
protocol_read_client_msg(protocol_conn* conn, size_t* start_pos,
                         protocol_msg* read_msg)
{
    return protocol_read_msg(conn, start_pos, true, read_msg);
}

protocol_result
protocol_read_server_msg(protocol_conn* conn, size_t* start_pos,
                         protocol_msg* read_msg)
{
    return protocol_read_msg(conn, start_pos, false, read_msg);
}

static protocol_result
protocol_write_msg(protocol_conn* conn, protocol_msg* write_msg,
                   protocol_endpoint type)
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

    int64_t num_written = 0;
    int64_t payload_num_written = 0;
    unsigned num_mask_bytes = (type == PROTOCOL_ENDPOINT_SERVER) ? 0 : 4;
    do
    {
        int64_t num_remaining = msg_len - payload_num_written;
        int64_t payload_len = hhmin(num_remaining, max_frame_size);
        int num_extra_len_bytes = get_num_extra_len_bytes(payload_len);
        int64_t total_frame_len =
            2 + (int)num_mask_bytes + num_extra_len_bytes + payload_len;

        hhassert(total_frame_len >= 0);
        /* make sure there is enough room for this frame */
        char* data = darray_ensure(&conn->write_buffer,
                                   (size_t)total_frame_len);

        /* get the data */
        data = &data[darray_get_len(conn->write_buffer)];
        const char* start_data = data;

        /* determine if this is the fin frame */
        unsigned char first_byte_mask =
            ((num_written + payload_len) >= msg_len) ? 0x80 : 0x00;

        /* write the first byte to the buffer */
        *data = (char)(first_byte_mask | (unsigned char)opcode);
        data++;

        /* set the mask bit if appropriate */
        *data = (char)((type == PROTOCOL_ENDPOINT_CLIENT) ? 0x80 : 0x00);

        /* write the payload length to the buffer */
        uint16_t short_len;
        uint64_t long_len;
        switch (num_extra_len_bytes)
        {
        case 0:
            *data |= (char)payload_len; /* |= to avoid blowing away mask bit */
            data++;
            break;

        case 2:
            *data |= 126; /* |= to avoid blowing away mask bit */
            data++;
            short_len = hh_htons((uint16_t)payload_len);
            memcpy(data, &short_len, sizeof(uint16_t));
            data += sizeof(uint16_t);
            break;

        case 8:
            *data |= 127; /* |= to avoid blowing away mask bit */
            data++;
            long_len = hh_htonll((uint64_t)payload_len);
            memcpy(data, &long_len, sizeof(uint64_t));
            data += sizeof(uint64_t);
            break;

        default:
            hhassert(0);
            break;
        }

        uint32_t val;
        char* mask_key = NULL;
        switch (type)
        {
        case PROTOCOL_ENDPOINT_SERVER:
            break;
        case PROTOCOL_ENDPOINT_CLIENT:
            hhassert(conn->settings->rand_func != NULL);
            val = conn->settings->rand_func(conn);
            hhassert(sizeof(val) == num_mask_bytes);
            memcpy(data, &val, num_mask_bytes);
            mask_key = data;
            data += num_mask_bytes;
            break;
        }

    #ifdef DEBUG
        hhassert(start_data+2+num_extra_len_bytes+num_mask_bytes == data);
    #else
        hhunused(start_data);
    #endif

        /* write the payload to the buffer */
        switch (type)
        {
        case PROTOCOL_ENDPOINT_SERVER:
            hhassert(payload_len >= 0);
            memcpy(data, msg_data, (size_t)payload_len);
            break;
        case PROTOCOL_ENDPOINT_CLIENT:
            hhassert(payload_len >= 0);
            mask_and_move_data(data, msg_data, (size_t)payload_len, mask_key,
                               0, false, NULL, NULL);
            break;
        }

        /* bookkeeping */
        darray_add_len(conn->write_buffer, (size_t)total_frame_len);
        msg_data += payload_len;
        payload_num_written += payload_len;
        num_written += total_frame_len;
        opcode = PROTOCOL_OPCODE_CONTINUATION;
    } while (payload_num_written < msg_len);

    return PROTOCOL_RESULT_MESSAGE_FINISHED;
}

/*
 * put the message in write_msg on conn->write_buffer.
 * msg will be broken up into frames of size write_max_frame_size
 */
protocol_result
protocol_write_server_msg(protocol_conn* conn, protocol_msg* write_msg)
{
    return protocol_write_msg(conn, write_msg, PROTOCOL_ENDPOINT_SERVER);
}

/*
 * write write_msg to conn->write_buffer.  must be called after
 * protocol_read_handshake_request.
 */
protocol_result
protocol_write_client_msg(protocol_conn* conn, protocol_msg* write_msg)
{
    return protocol_write_msg(conn, write_msg, PROTOCOL_ENDPOINT_CLIENT);
}

bool protocol_is_data(protocol_msg_type msg_type)
{
    switch (msg_type)
    {
        case PROTOCOL_MSG_NONE:
        case PROTOCOL_MSG_CLOSE:
        case PROTOCOL_MSG_PING:
        case PROTOCOL_MSG_PONG:
            return false;
        case PROTOCOL_MSG_TEXT:
        case PROTOCOL_MSG_BINARY:
            return true;
        default:
            return false;
    }
}

bool protocol_is_control(protocol_msg_type msg_type)
{
    switch (msg_type)
    {
        case PROTOCOL_MSG_NONE:
        case PROTOCOL_MSG_TEXT:
        case PROTOCOL_MSG_BINARY:
            return false;
        case PROTOCOL_MSG_CLOSE:
        case PROTOCOL_MSG_PING:
        case PROTOCOL_MSG_PONG:
            return true;
        default:
            return false;
    }
}

