/* test_network - test network module 
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

#include "../network.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

static const char* TEST_REQUEST_LINE = "GET %s HTTP/1.1\r\n";
static const char* RESOURCE_NAME = "/chat";
static struct 
{
    const char* name;
    const char* value;
    int value_index;
} g_headers[] = {
    {"Host", "server.example.com", 0},
    {"Upgrade", "websocket", 0},
    {"Connection", "Upgrade", 0},
    {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==", 0},
    {"Origin", "http://example.com", 0},
    {"Sec-WebSocket-Protocol", "chat, superchat", 0},
    {"Sec-WebSocket-Version", "13", 0},
    {"Sec-WebSocket-Protocol", "otherchat", 1},
    {NULL, NULL, 0} /* Sentinel */
};
const int NUM_UNIQUE_HEADERS = 7;
static char TEST_BROKEN_REQUEST_LINE[] = "GET /thing HTTP/1.1";
static char TEST_RESPONSE[] = 
"HTTP/1.1 101 Switching Protocols\r\n"
"Upgrade: websocket\r\n"
"Connection: Upgrade\r\n"
"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
"Sec-WebSocket-Protocol: chat\r\n\r\n";

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
            g_headers[i].value
        );
    }

    num_written += snprintf(&buffer[num_written], len - num_written, "\r\n");
    
    darray* in_buffer = darray_create_data(
        buffer, 
        sizeof(char),
        num_written, 
        num_written
    );
    network_conn* conn = network_create_conn(1024, 20);
    network_result r;
    if ((r=network_read_handshake(conn, in_buffer)) != NETWORK_RESULT_SUCCESS)
    {
        printf("FAIL, HANDSHAKE RETURN: %d\n", r);
        exit(1);
    }

    network_handshake* info = &conn->info;
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
            "HEADER COUNTS DON'T MATCH: %d, %d\n",
            darray_get_len(info->headers),
            NUM_UNIQUE_HEADERS
        );
        exit(1);
    }

    for(int i = 0; g_headers[i].name != NULL; i++)
    {
        const char* name = g_headers[i].name;
        int index = g_headers[i].value_index;
        if (index >= network_get_num_header_values(conn, name))
        {
            printf(
                "VALUE INDEX OUT OF BOUNDS: %s, %d, %d\n", 
                name, 
                index,
                network_get_num_header_values(conn, name)
            );
            exit(1);
        }

        const char* value = network_get_header_value(conn, name, index);
        if (value == NULL)
        {
            printf("NULL VALUE FOR NAME: %s\n", name);
            exit(1);
        }

        if (strcmp(g_headers[i].value, value) != 0)
        {
            printf(
                "VALUE DOESN'T MATCH: %s, %s\n", 
                g_headers[i].value, 
                value
            );
            exit(1);
        }
    }

    if ((r = network_write_handshake(conn, "chat", NULL)) 
            != NETWORK_RESULT_SUCCESS)
    {
        printf("FAIL WRITING HANDSHAKE: %d\n", r);
        exit(1);
    }

    char* write_buf = darray_get_data(conn->write_buffer);
    if (strlen(TEST_RESPONSE) != darray_get_len(conn->write_buffer))
    {
        printf(
            "LENGTH MISMATCH %lu %d\n%s\n%s\n", 
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

    network_destroy_conn(conn);

    in_buffer = darray_create_data(
        TEST_BROKEN_REQUEST_LINE, 
        sizeof(char),
        sizeof(TEST_BROKEN_REQUEST_LINE), 
        sizeof(TEST_BROKEN_REQUEST_LINE)
    );
    conn = network_create_conn(1024, 256);
    if ((r=network_read_handshake(conn, in_buffer)) != NETWORK_RESULT_FAIL)
    {
        printf("FAIL, HANDSHAKE RETURN WAS SUCCESS: %d\n", r);
        exit(1);
    }

    network_destroy_conn(conn);
    
    exit(0);
}
