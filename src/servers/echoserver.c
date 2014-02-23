#include "../server.h"
#include "../hhlog.h"
#include "../util.h"

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

static server* g_serv = NULL;

static void on_message_received(server_conn* conn, endpoint_msg* msg)
{
    server_conn_send_msg(conn, msg);
}

static BOOL on_open(
    server_conn* conn,
    int* subprotocol_out,
    int* extensions_out
)
{
    hhunused(subprotocol_out);
    hhunused(extensions_out);
    hhunused(conn);
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
    int reason_len
)
{
    hhunused(conn);
    hhunused(code);
    hhunused(reason);
    hhunused(reason_len);
    printf("Got close: (%d, %.*s)\n", code, (int)reason_len, reason);
}

static void signal_handler(int sig)
{
    hhunused(sig);
    /* signal server to stop */
    if (g_serv != NULL)
    {
        server_stop(g_serv);
    }
}

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_DEBUG,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = TRUE
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s port\n", argv[0]);
        exit(1);
    }

    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = signal_handler;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    config_server_options options;
    options.bindaddr = NULL;
    options.max_clients = 1;

    options.endp_settings.protocol_buf_init_len = 4 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 20 * 1024 * 1024;
    conn_settings->read_max_msg_size = 20 * 1024 * 1024;
    conn_settings->read_max_num_frames = 20 * 1024 * 1024;
    options.port = atoi(argv[1]);

    server_callbacks callbacks;
    callbacks.on_open_callback = on_open;
    callbacks.on_message_callback = on_message_received;
    callbacks.on_ping_callback = NULL;
    callbacks.on_close_callback = on_close;

    hhlog_set_options(&g_log_options);

    g_serv = server_create(&options, &callbacks);

    server_listen(g_serv);

    server_destroy(g_serv);
    g_serv = NULL;
    exit(0);
}

