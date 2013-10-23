#include "../server.h"
#include "../util.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

static void on_message_received(server_conn* conn, server_msg* msg)
{
    /*printf(
        "Got message: %.*s\nsize: %" PRId64 ", is_text: %d\n",
        (int)msg->msg_len,
        msg->data,
        msg->msg_len,
        msg->is_text
    );*/
    server_conn_send_msg(conn, msg);
}

static BOOL on_open(server_conn* conn)
{
    int num_protocols = server_get_num_client_subprotocols(conn);
    printf("Got subprotocols [\n");
    for (int i = 0; i < num_protocols; i++)
    {
        printf("    %s\n", server_get_client_subprotocol(conn, i));
    }
    printf("]\n\n");

    return TRUE;
}

static void on_close(
    server_conn* conn,
    int code,
    const char* reason,
    int64_t reason_len
)
{
    hhunused(conn);
    printf("Got close: (%d, %.*s)\n", code, (int)reason_len, reason);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s port\n", argv[0]);
        exit(1);
    }

    config_options options;
    options.bindaddr = NULL;
    options.logfilepath = NULL;
    options.protocol_buf_init_len = 4 * 1024;
    options.max_clients = 1;
    options.conn_settings.write_max_frame_size = 20 * 1024 * 1024;
    options.conn_settings.read_max_msg_size = 20 * 1024 * 1024;
    options.conn_settings.read_max_num_frames = 20 * 1024 * 1024;
    options.conn_settings.fail_by_drop = TRUE;
    options.loglevel = CONFIG_LOG_LEVEL_DEBUG;
    options.port = atoi(argv[1]);

    server_callbacks callbacks;
    callbacks.on_open_callback = on_open;
    callbacks.on_message_callback = on_message_received;
    callbacks.on_ping_callback = NULL;
    callbacks.on_close_callback = on_close;

    server* serv = server_create(&options, &callbacks);

    server_listen(serv);

    exit(0);
}

