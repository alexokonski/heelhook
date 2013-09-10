/* test_protocol - test protocol module 
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

#include "../protocol.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char* TEST_REQUEST_LINE = "GET %s HTTP/1.1\r\n";
static const char* RESOURCE_NAME = "/chat";
static const struct 
{
    const char* name;
    const char* send_value;
    const char* values[3];
    int value_index_start;
} g_headers[] = {
    {"Host", "server.example.com", {"server.example.com", NULL}, 0},
    {"Upgrade", "websocket", {"websocket", NULL}, 0},
    {"Connection", "Upgrade", {"Upgrade", NULL}, 0},
    {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==", 
        {"dGhlIHNhbXBsZSBub25jZQ==", NULL}, 0},
    {"Origin", "http://example.com", {"http://example.com", NULL}, 0},
    {"Sec-WebSocket-Protocol", "chat, superchat", 
        {"chat", "superchat", NULL}, 0},
    {"Sec-WebSocket-Version", "13", {"13", NULL},  0},
    {"Sec-WebSocket-Protocol", "otherchat", {"otherchat", NULL}, 2},
    {NULL, NULL, {NULL}, 0} /* Sentinel */
};
const uint32_t NUM_UNIQUE_HEADERS = 7;
static char TEST_BROKEN_REQUEST_LINE[] = "GET /thing HTTP/1.1";
static char TEST_RESPONSE[] = 
"HTTP/1.1 101 Switching Protocols\r\n"
"Upgrade: websocket\r\n"
"Connection: Upgrade\r\n"
"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
"Sec-WebSocket-Protocol: chat\r\n\r\n";

static const unsigned char TEST_CLIENT_FRAME[] = 
{
    0x81, 0x9c, 0xe7, 0x63, 0x33, 0x96, 0xb5, 0x0c, 0x50, 0xfd, 0xc7, 0x0a, 
    0x47, 0xb6, 0x90, 0x0a, 0x47, 0xfe, 0xc7, 0x2b, 0x67, 0xdb, 0xab, 0x56, 
    0x13, 0xc1, 0x82, 0x01, 0x60, 0xf9, 0x84, 0x08, 0x56, 0xe2
};

static const unsigned char TEST_CLIENT_FRAG_1[] =
{
    0x01, 0x89, 0x97, 0xa4, 0xcc, 0xb0, 0xf1, 0xd6, 0xad, 0xd7, 0xfa, 0xc1, 
    0xa2, 0xc4, 0xa6    
};

static const unsigned char TEST_CLIENT_FRAG_2[] =
{
    0x80, 0x89, 0x49, 0x82, 0x34, 0xd8, 0x2f, 0xf0, 0x55, 0xbf, 0x24, 0xe7, 
    0x5a, 0xac, 0x7b
};

static const unsigned char TEST_SERVER_FRAG_1[] =
{
    0x01, 0x03, 0x48, 0x65, 0x6c
};

static const unsigned char TEST_SERVER_FRAG_2[] =
{
    0x80, 0x02, 0x6c, 0x6f
};

int main(int argc, char** argv)
{
    hhunused(argc);
    hhunused(argv);

    char buffer[1024];
    size_t len = sizeof(buffer);
    int num_written = 0;

    num_written = snprintf(
        &buffer[num_written], 
        len - num_written, 
        TEST_REQUEST_LINE,
        RESOURCE_NAME
    );

    for(int i = 0; g_headers[i].name != NULL; i++)
    {
        num_written += snprintf(
            &buffer[num_written], 
            len - num_written, 
            "%s: %s\r\n", 
            g_headers[i].name,
            g_headers[i].send_value
        );
    }

    num_written += snprintf(&buffer[num_written], len - num_written, "\r\n");
    
    protocol_settings settings;
    settings.write_max_frame_size = 1024;
    settings.read_max_msg_size = 65537;
    settings.read_max_num_frames = 1024;
    protocol_conn* conn = protocol_create_conn(&settings, 20);
    darray_append(&conn->info.buffer, buffer, num_written);
    protocol_result r;
    protocol_handshake_result hr;
    if ((hr=protocol_read_handshake(conn)) != 
            PROTOCOL_HANDSHAKE_SUCCESS)
    {
        printf("FAIL, HANDSHAKE RETURN: %d\n", hr);
        exit(1);
    }

    protocol_handshake* info = &conn->info;
    if (strcmp(info->resource_name, RESOURCE_NAME) != 0)
    {
        printf(
            "FAIL, RESOURCE NAMES DON'T MATCH: %s, %s\n",
            info->resource_name,
            RESOURCE_NAME
        );
        exit(1);
    }

    if (NUM_UNIQUE_HEADERS != darray_get_len(info->headers))
    {
        printf(
            "HEADER COUNTS DON'T MATCH: %lu, %d\n",
            darray_get_len(info->headers),
            NUM_UNIQUE_HEADERS
        );
        exit(1);
    }

    for (int i = 0; g_headers[i].name != NULL; i++)
    {
        const char* name = g_headers[i].name;

        for (int j = 0; g_headers[i].values[j] != NULL; j++)
        {
            int index = g_headers[i].value_index_start + j;
            if (index >= protocol_get_num_header_values(conn, name))
            {
                printf(
                    "VALUE INDEX OUT OF BOUNDS: %s, %d, %d\n", 
                    name, 
                    index,
                    protocol_get_num_header_values(conn, name)
                );
                exit(1);
            }

            const char* value = protocol_get_header_value(conn, name, index);
            if (value == NULL)
            {
                printf("NULL VALUE FOR NAME: %s\n", name);
                exit(1);
            }

            if (strcmp(g_headers[i].values[j], value) != 0)
            {
                printf(
                    "VALUE DOESN'T MATCH: %s, %s\n", 
                    g_headers[i].values[j], 
                    value
                );
                exit(1);
            }
        }
    }

    if ((hr = protocol_write_handshake(conn, "chat", NULL)) 
            != PROTOCOL_HANDSHAKE_SUCCESS)
    {
        printf("FAIL WRITING HANDSHAKE: %d\n", hr);
        exit(1);
    }

    char* write_buf = darray_get_data(conn->write_buffer);
    if (strlen(TEST_RESPONSE) != darray_get_len(conn->write_buffer))
    {
        printf(
            "LENGTH MISMATCH %lu %lu\n%s\n%s\n", 
            strlen(TEST_RESPONSE), 
            darray_get_len(conn->write_buffer),
            TEST_RESPONSE,
            write_buf
        );
        exit(1);
    }

    if (memcmp(TEST_RESPONSE, write_buf, strlen(TEST_RESPONSE)) != 0)
    {
        printf("RESPONSE MISMATCH %s %s\n", TEST_RESPONSE, write_buf);
        exit(1);
    }

    /* test reading a message now that the handshake worked */
    darray_append(
        &conn->read_buffer, 
        TEST_CLIENT_FRAME, 
        sizeof(TEST_CLIENT_FRAME)
    );

    protocol_msg msg;
    size_t pos = 0;
    r = protocol_read_msg(conn, &pos, &msg);

    if (r != PROTOCOL_RESULT_MESSAGE_FINISHED)
    {
        printf("RESULT MISMATCH: %d\n", r);
        exit(1);
    }

    if (msg.type != PROTOCOL_MSG_TEXT)
    {
        printf("MESSAGE SHOULD BE TEXT\n");
        exit(1);
    }

    static const char expected_msg[] = "Rock it with HTML5 WebSocket";
    static const int expected_len = sizeof(expected_msg) - 1;
    if (msg.msg_len != expected_len)
    {
        printf(
            "MESSAGE SHOULD BE LEN %d, GOT %" PRId64 "\n", 
            expected_len,
            msg.msg_len
        );
        exit(1);
    }

    if (strncmp(msg.data, expected_msg, msg.msg_len) != 0)
    {
        char stuff[expected_len+1];
        memcpy(stuff, msg.data, expected_len);
        stuff[expected_len] = '\0';
        printf("MESSAGE MISMATCH: %s\n", stuff);
        exit(1);
    }

    darray_clear(conn->read_buffer);
    darray_append(
        &conn->read_buffer, 
        TEST_CLIENT_FRAG_1, 
        sizeof(TEST_CLIENT_FRAG_1)
    );
    memset(&msg, 0, sizeof(msg));
    pos = 0;
    r = protocol_read_msg(conn, &pos, &msg);

    if (r != PROTOCOL_RESULT_FRAME_FINISHED)
    {
        printf("MESSAGE FRAGMENT RESULT MISMATCH: %d\n", r);
        exit(1);
    }

    size_t start_pos = darray_get_len(conn->read_buffer);
    darray_append(
        &conn->read_buffer,
        TEST_CLIENT_FRAG_2,
        sizeof(TEST_CLIENT_FRAG_2)
    );

    r = protocol_read_msg(conn, &start_pos, &msg);

    if (r != PROTOCOL_RESULT_MESSAGE_FINISHED)
    {
        printf("NON-SUCCESS SECOND MSG FRAG: %d\n", r);
        exit(1);
    }

    if (msg.type != PROTOCOL_MSG_TEXT)
    {
        printf("FRAG TYPE MISMATCH: %d\n", msg.type);
        exit(1);
    }

    static const char expected_frag_msg[] = "fragment1fragment2";
    static const int expected_frag_len = sizeof(expected_frag_msg) - 1;
    if (msg.msg_len != expected_frag_len)
    {
        printf(
            "FRAG LEN MISMATCH: %d %" PRId64 "\n", 
            expected_frag_len, 
            msg.msg_len
        );
        exit(1);
    }

    if (strncmp(msg.data, "fragment1fragment2", msg.msg_len) != 0)
    {
        char stuff[expected_frag_len+1];
        memcpy(stuff, msg.data, expected_frag_len);
        stuff[expected_frag_len] = '\0';
        printf("FRAG MSG MISMATCH: %s\n", stuff);
        exit(1);
    }

    darray_clear(conn->read_buffer);
    
    const unsigned char header[] = 
    {
        0x82, 0xff, /* fin bit, opcode, extended len */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /* extended len */
        0x37, 0xfa, 0x21, 0x3d /* masking key */
    };
    const int payload_len = 65536;
    int total_len = sizeof(header) + payload_len;

    darray_ensure(&conn->read_buffer, total_len);

    const unsigned char* masking_key = &header[sizeof(header) - 4];
    char* data = darray_get_data(conn->read_buffer);
    memcpy(data, header, sizeof(header));
    data += sizeof(header);

    for (int i = 0; i < payload_len; i++)
    {
        *data = '*' ^ masking_key[i % 4];
        data++;
    }

    darray_add_len(conn->read_buffer, total_len);

    memset(&msg, 0, sizeof(msg));
    pos = 0;
    r = protocol_read_msg(conn, &pos, &msg);
 
    if (r != PROTOCOL_RESULT_MESSAGE_FINISHED)
    {
        printf("NON-SUCCESS LONG MSG FRAG: %d\n", r);
        exit(1);
    }

    if (msg.type != PROTOCOL_MSG_BINARY)
    {
        printf("LONG MSG TYPE MISMATCH: %d\n", msg.type);
        exit(1);
    }

    if (msg.msg_len != 65536)
    {
        printf("LONG MSG LEN MISMATCH: %" PRId64 "\n", msg.msg_len);
        exit(1);
    }

    if (msg.data[65535] != '*')
    {
        printf("LONG MSG INVALID LAST BYTE: %c\n", msg.data[65535]);
        exit(1);
    }

    /*darray_clear(conn->write_buffer);*/
    size_t before_len = darray_get_len(conn->write_buffer); 
    static char out_message[] = "Hello";
    settings.write_max_frame_size = 3;
    msg.type = PROTOCOL_MSG_TEXT;
    msg.data = out_message;
    msg.msg_len = sizeof(out_message) - 1;
    r = protocol_write_msg(conn, &msg);

    if (r != PROTOCOL_RESULT_MESSAGE_FINISHED)
    {
        printf("WRITE MSG RESULT FAIL: %d\n", r);
        exit(1);
    }

    size_t new_len = darray_get_len(conn->write_buffer) - before_len;
    size_t total_msg_len = sizeof(TEST_SERVER_FRAG_1) + 
                        sizeof(TEST_SERVER_FRAG_2);
    if (new_len != total_msg_len)
    {
        printf(
            "WRITE MSG LEN WRONG %lu %lu\n", 
            total_msg_len, 
            darray_get_len(conn->write_buffer)
        );
        exit(1);
    }

    data = darray_get_data(conn->write_buffer);
    data = &data[before_len];
    if (memcmp(data, TEST_SERVER_FRAG_1, sizeof(TEST_SERVER_FRAG_1)) != 0)
    {
        printf("WRITE MSG MEMCMP FAIL FRAG 1\n");
        exit(1);
    }

    if (
        memcmp(
            &data[sizeof(TEST_SERVER_FRAG_1)], 
            TEST_SERVER_FRAG_2, 
            sizeof(TEST_SERVER_FRAG_2)
        ) != 0
    )
    {
        printf("WRITE MSG MEMCMP FAIL FRAG 2\n");
        exit(1);
    }

    protocol_destroy_conn(conn);

    settings.write_max_frame_size = 1024;
    conn = protocol_create_conn(&settings, 256);
    darray_clear(conn->info.buffer);
    darray_append(
        &conn->info.buffer, 
        TEST_BROKEN_REQUEST_LINE, 
        sizeof(TEST_BROKEN_REQUEST_LINE)
    );
    if ((hr=protocol_read_handshake(conn)) == PROTOCOL_HANDSHAKE_SUCCESS)
    {
        printf("FAIL, HANDSHAKE RETURN WAS SUCCESS: %d\n", hr);
        exit(1);
    }

    protocol_destroy_conn(conn);
    
    exit(0);
}
