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

#include "client.h"
#include "hhassert.h"
#include "hhlog.h"
#include "util.h"

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

static bool
client_on_connect_callback(endpoint* conn_info,
                        protocol_conn* proto_conn, void* userdata)
{
    hhunused(conn_info);
    hhunused(proto_conn);

    /*
     * for clients endpoints, the on_connect callback is called right after the
     * handshake is complete. Because of that, the client interface exposes
     * this as 'on_open' rather than 'on_connect', to make things more
     * consistent with the server interface.
     */
    client* c = userdata;
    if (c->cbs->on_open != NULL)
    {
        return c->cbs->on_open(c, c->userdata);
    }

    return true;
}

static void client_on_message_callback(endpoint* conn_info, endpoint_msg* msg,
                                       void* userdata)
{
    hhunused(conn_info);

    client* c = userdata;

    if (c->cbs->on_message != NULL)
    {
        c->cbs->on_message(c, msg, c->userdata);
    }
}

static void client_on_ping_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata)
{
    hhunused(conn_info);

    client* c = userdata;

    if (c->cbs->on_ping != NULL)
    {
        c->cbs->on_ping(c, payload, payload_len, c->userdata);
    }
}

static void client_on_pong_callback(endpoint* conn_info, char* payload,
                                    int payload_len, void* userdata)
{
    hhunused(conn_info);

    client* c = userdata;

    if (c->cbs->on_pong != NULL)
    {
        c->cbs->on_pong(c, payload, payload_len, c->userdata);
    }
}

static void client_on_close_callback(endpoint* conn_info, int code,
                                     const char* reason, int reason_len,
                                     void* userdata)
{
    hhunused(conn_info);

    client* c = userdata;
    if (c->cbs->on_close != NULL)
    {
        c->cbs->on_close(c, code, reason, reason_len, c->userdata);
    }

    /* close the socket */
    close(c->fd);
}

static endpoint_callbacks g_client_cbs =
{
    .on_connect = client_on_connect_callback,
    .on_message = client_on_message_callback,
    .on_ping = client_on_ping_callback,
    .on_pong = client_on_pong_callback,
    .on_close = client_on_close_callback
};

static client_result endpoint_result_to_client_result(endpoint_result r)
{
    switch (r)
    {
    case ENDPOINT_RESULT_SUCCESS:
        return CLIENT_RESULT_SUCCESS;
    case ENDPOINT_RESULT_FAIL:
        return CLIENT_RESULT_FAIL;
    }

    hhassert(0);
    return CLIENT_RESULT_FAIL;
}

/* Opens a non-blocking socket to addr, port, and puts a handshake on the write
 * buffer. Since this is non-blocking, you will have to use client_get_fd and
 * select() (or some such) before calling client_write_data()
 *
 * TODO: pass in an actual uri rather than addr, port, resource
 */
client_result
client_connect_raw(client* c, config_client_options* opt,
                   client_callbacks* cbs, const char* ip_addr, uint16_t port,
                   const char* resource, const char* host,
                   const char** subprotocols, const char** extensions,
                   const char** extra_headers, void* userdata)
{
    hhassert(ip_addr != NULL);
    hhassert(resource != NULL);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "failed to create socket: %s",
              strerror(errno));
        return CLIENT_RESULT_FAIL;
    }

    if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
    {
        hhlog(HHLOG_LEVEL_ERROR, "fcntl failed on socket: %s",
              strerror(errno));
        close(s);
        return CLIENT_RESULT_FAIL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = hh_htonl(INADDR_ANY);
    if (inet_aton(ip_addr, &addr.sin_addr) == 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "invalid bind address: %s",
              strerror(errno));
        return CLIENT_RESULT_FAIL;
    }

    if (connect(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1 &&
        errno != EINPROGRESS)
    {
        hhlog(HHLOG_LEVEL_ERROR, "connect failed on socket: %s",
              strerror(errno));
        close(s);
        return CLIENT_RESULT_FAIL;
    }

    c->fd = s;
    c->cbs = cbs;
    c->userdata = userdata;
    int r = endpoint_init(&c->endp, ENDPOINT_CLIENT, &opt->endp_settings,
                          &g_client_cbs, c);
    if (r < 0)
    {
        hhlog(HHLOG_LEVEL_ERROR, "endpoint_init failed: %s",
              strerror(errno));
        close(s);
        return CLIENT_RESULT_FAIL;
    }

    endpoint_result er;
    er = endpoint_send_handshake_request(&c->endp, resource, host,
                                         subprotocols, extensions,
                                         extra_headers);
    if (er != ENDPOINT_RESULT_SUCCESS)
    {
        hhlog(HHLOG_LEVEL_ERROR,
              "endpoint failed to write handshake result: %d, socket: %d",
              er, s);
        close(s);
        return CLIENT_RESULT_FAIL;
    }

    return CLIENT_RESULT_SUCCESS;
}

/* forcibly closes the socket and frees all resources */
void client_disconnect(client* c)
{
    close(c->fd);
    endpoint_deinit(&c->endp);
}

/* get the file descriptor for this client */
int client_fd(client* c)
{
    return c->fd;
}

/* queue up a message to send on this connection */
client_result client_send_msg(client* conn, endpoint_msg* msg)
{
    endpoint_result r = endpoint_send_msg(&conn->endp, msg);
    return endpoint_result_to_client_result(r);
}

/* queue up a ping with payload (NULL for no payload)*/
client_result
client_send_ping(client* conn, char* payload, int payload_len)
{
    endpoint_result r = endpoint_send_ping(&conn->endp, payload, payload_len);
    return endpoint_result_to_client_result(r);
}

/* queue up a pong with payload (NULL for no payload)*/
client_result
client_send_pong(client* conn, char* payload, int payload_len)
{
    endpoint_result r = endpoint_send_pong(&conn->endp, payload, payload_len);
    return endpoint_result_to_client_result(r);
}

/*
 * close this connection. queues up a close message with the error
 * code and reason
 */
client_result client_close(client* conn, uint16_t code,
                                const char* reason, int reason_len)
{
    endpoint_result r = endpoint_close(&conn->endp, code, reason, reason_len);
    return endpoint_result_to_client_result(r);
}

/*
 * get the subprotocol the client chose
 */
const char* client_get_client_subprotocol(client* c)
{
    return protocol_get_subprotocol(&c->endp.pconn, 0);
}

/*
 * get number of extensions the client chose
 */
unsigned client_get_num_extensions(client* c)
{
    return protocol_get_num_extensions(&c->endp.pconn);
}

/*
 * get an extension the client chose
 */
const char* client_get_extension(client* c, unsigned index)
{
    return protocol_get_extension(&c->endp.pconn, index);
}

/*
 * write data from client to a ready socket
 */
client_write_result client_write(client* c, int fd)
{
    endpoint_write_result r = endpoint_write(&c->endp, fd);
    switch (r)
    {
    case ENDPOINT_WRITE_CONTINUE:
        return CLIENT_WRITE_CONTINUE;

    case ENDPOINT_WRITE_DONE:
        return CLIENT_WRITE_DONE;

    case ENDPOINT_WRITE_ERROR:
        return CLIENT_WRITE_ERROR;

    case ENDPOINT_WRITE_CLOSED:
        return CLIENT_WRITE_CLOSED;
    }

    hhassert(0);
    return CLIENT_WRITE_ERROR;
}

/*
 * read data from a ready socket into the endpoint
 */
client_read_result client_read(client* c, int fd)
{
    endpoint_read_result r = endpoint_read(&c->endp, fd);
    switch (r)
    {
    case ENDPOINT_READ_SUCCESS:
        return CLIENT_READ_SUCCESS;
    case ENDPOINT_READ_SUCCESS_WROTE_DATA:
        return CLIENT_READ_SUCCESS_WROTE_DATA;
    case ENDPOINT_READ_ERROR:
        return CLIENT_READ_ERROR;
    case ENDPOINT_READ_CLOSED:
        return CLIENT_READ_CLOSED;
    }

    hhassert(0);
    return CLIENT_READ_ERROR;
}

