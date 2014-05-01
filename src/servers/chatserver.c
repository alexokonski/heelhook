/* chatserver - implements a simple chatserver
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

#include "../hhassert.h"
#include "../error_code.h"
#include "../hhlog.h"
#include "../hhmemory.h"
#include "../inlist.h"
#include "../server.h"
#include "../util.h"
#include "cJSON.h"

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static server* g_serv = NULL;

#define MAX_CHATROOMS 100
#define MAX_CLIENTS 10000
#define CLIENT_NAME_SIZE 32
#define ROOM_NAME_SIZE 128
#define MAX_MESSAGE_SIZE (16 * 1024)

typedef struct chatroom_client chatroom_client;
typedef struct chatroom chatroom;

struct chatroom_client
{
    char name[CLIENT_NAME_SIZE];
    server_conn* conn;
    chatroom_client* global_next;
    chatroom_client* global_prev;
    chatroom_client* room_next;
    chatroom_client* room_prev;
    chatroom* room;
};

struct chatroom
{
    char name[ROOM_NAME_SIZE];
    int num_connected;

    chatroom_client* client_head;
    chatroom_client* client_tail;

    chatroom* next;
    chatroom* prev;
};

static struct all_data
{
    chatroom_client clients[MAX_CLIENTS];
    chatroom rooms[MAX_CHATROOMS];

    chatroom* room_active_head;
    chatroom* room_active_tail;
    chatroom* room_free_head;
    chatroom* room_free_tail;

    chatroom_client* client_active_head;
    chatroom_client* client_active_tail;
    chatroom_client* client_free_head;
    chatroom_client* client_free_tail;
} g_data;

typedef struct all_data all_data;

static void broadcast_msg(chatroom* room, endpoint_msg* msg)
{
    hhlog(HHLOG_LEVEL_DEBUG, "Broadcasting msg: %.*s", (int)msg->msg_len,
          msg->data);
    INLIST_FOREACH(room,chatroom_client,c,room_next,room_prev,client_head,
                   client_tail)
    {
        server_conn_send_msg(c->conn, msg);
    }
}

static void broadcast_chat_msg(chatroom_client* c, const char* type,
                          const char* message)
{
    chatroom* room = c->room;
    hhassert_pointer(room);
    if (room->client_head == NULL) return;

    size_t data_size = strlen(type)+strlen(message)+CLIENT_NAME_SIZE+1024;
    char* data = hhmalloc(data_size);
    int len = snprintf(data, data_size,
        "{\"message\":{\"value\":\"%s\",\"type\":\"%s\",\"client\":\"%s\"}}",
        message, type, c->name);

    endpoint_msg msg;
    msg.is_text = true;
    msg.data = data;
    msg.msg_len = len;

    broadcast_msg(room, &msg);

    hhfree(data);
}

static void send_msg(chatroom_client* c, char* data, size_t len)
{
    endpoint_msg msg;
    msg.is_text = true;
    msg.data = data;
    msg.msg_len = len;
    hhlog(HHLOG_LEVEL_DEBUG, "Sending msg: %.*s", (int)len, data);
    server_conn_send_msg(c->conn, &msg);
}

static chatroom* allocate_room(all_data* data)
{
    if (data->room_free_head == NULL)
    {
        return NULL;
    }

    chatroom* room = data->room_free_head;
    INLIST_REMOVE(data, room, next, prev, room_free_head, room_free_tail);
    INLIST_APPEND(data, room, next, prev, room_active_head, room_active_tail);

    return room;
}

static void deallocate_room(all_data* data, chatroom* room)
{
    room->name[0] = '\0';
    INLIST_REMOVE(data, room, next, prev, room_active_head, room_active_tail);
    INLIST_APPEND(data, room, next, prev, room_free_head, room_free_tail);
}

static cJSON* get_room_list(all_data* data)
{
    cJSON* rooms_json = cJSON_CreateArray();
    INLIST_FOREACH(data,chatroom,r,next,prev,room_active_head,
                   room_active_tail)
    {
        cJSON_AddItemToArray(rooms_json, cJSON_CreateString(r->name));
    }

    return rooms_json;
}

static cJSON* get_client_list(chatroom* room)
{
    cJSON* clients_json = cJSON_CreateArray();
    INLIST_FOREACH(room,chatroom_client,c,room_next,room_prev,client_head,
                   client_tail)
    {
        cJSON_AddItemToArray(clients_json, cJSON_CreateString(c->name));
    }

    return clients_json;
}

static void send_room_list(chatroom_client* c, all_data* data)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "rooms", get_room_list(data));

    char* json_str = cJSON_PrintUnformatted(json);
    send_msg(c, json_str, strlen(json_str));

    hhfree(json_str);
    cJSON_Delete(json);
}

static void send_client_list(chatroom_client* c)
{
    hhassert_pointer(c->room);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "clients", get_client_list(c->room));

    char* json_str = cJSON_PrintUnformatted(json);
    send_msg(c, json_str, strlen(json_str));

    hhfree(json_str);
    cJSON_Delete(json);
}

static void send_join_messages(chatroom_client* c, all_data* data)
{
    hhassert_pointer(c->room);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "joined_room", c->room->name);
    cJSON_AddItemToObject(json, "rooms", get_room_list(data));
    cJSON_AddItemToObject(json, "clients", get_client_list(c->room));
    char* join_str = cJSON_PrintUnformatted(json);

    send_msg(c, join_str, strlen(join_str));

    hhfree(join_str);
    cJSON_Delete(json);

    broadcast_chat_msg(c, "connect", "connected");
}

static void room_remove_client(all_data* data, chatroom* r, chatroom_client* c)
{
    hhassert_pointer(c->room);

    INLIST_REMOVE(r, c, room_next, room_prev, client_head, client_tail);

    /* tell everyone still left in the room that we disconnected */
    broadcast_chat_msg(c, "disconnect", "disconnected");

    /* destroy the room if it's now empty */
    if (c->room->client_head == NULL)
    {
        deallocate_room(data, c->room);
    }

    c->room = NULL;
}

static void room_add_client(all_data* data, chatroom* r, chatroom_client* c,
                            const char* user_name)
{
    INLIST_APPEND(r, c, room_next, room_prev, client_head, client_tail);
    c->room = r;
    strncpy(c->name, user_name, sizeof(c->name));
    c->name[sizeof(c->name) - 1] = '\0';

    send_join_messages(c, data);
}

static chatroom_client* allocate_client(all_data* data)
{
    if (data->client_free_head == NULL)
    {
        return NULL;
    }

    chatroom_client* c = data->client_free_head;
    INLIST_REMOVE(data, c, global_next, global_prev, client_free_head,
                  client_free_tail);
    INLIST_APPEND(data, c, global_next, global_prev, client_active_head,
                  client_active_tail);

    return c;
}

static void deallocate_client(all_data* data, chatroom_client* c)
{
    INLIST_REMOVE(data, c, global_next, global_prev,
                  client_active_head, client_active_tail);
    INLIST_APPEND(data, c, global_next, global_prev, client_free_head,
                  client_free_tail);
}

static void send_error(server_conn* conn, const char* error)
{
    server_conn_close(conn, HH_ERROR_NORMAL, error, strlen(error));
}

static void join_room(all_data* data, chatroom_client* c,
                      const char* room_str, const char* user_name)
{
    INLIST_FOREACH(data, chatroom, r, next, prev, room_active_head,
                   room_active_tail)
    {
        if (strcmp(r->name, room_str) == 0)
        {
            if (c->room != NULL)
            {
                room_remove_client(data, c->room, c);
            }

            room_add_client(data, r, c, user_name);
            return;
        }
    }

    char error_str[] = "{\"join_error\":\"room does not exist\"}";
    send_msg(c, error_str, sizeof(error_str) - 1);
}

static void create_room(all_data* data, chatroom_client* c,
                        const char* room_str, const char* user_name)
{
    INLIST_FOREACH(data, chatroom, r, next, prev, room_active_head,
                   room_active_tail)
    {
        if (strcmp(r->name, room_str) == 0)
        {
            char error_str[] = "{\"create_error\":\"room already exists\"}";
            send_msg(c, error_str, sizeof(error_str) - 1);
            return;
        }
    }

    chatroom* room = allocate_room(data);
    if (data->room_free_head == NULL)
    {
        char error_str[] = "{\"create_error\":\"max rooms reached\"}";
        send_msg(c, error_str, sizeof(error_str) - 1);
        return;
    }

    strncpy(room->name, room_str, sizeof(room->name));
    room->name[sizeof(room->name) - 1] = '\0';

    if (c->room != NULL)
    {
        room_remove_client(data, c->room, c);
    }

    /* put this user in the room */
    room_add_client(data, room, c, user_name);
}

/*
 * Valid client messages:
 *
 *{
 *    "request": {
 *        "value": "get_rooms"
 *    }
 *}
 *
 *{
 *    "request": {
 *        "value": "join_room",
 *        "room": "<room_name>".
 *        "username": "<username>"
 *    }
 *}
 *
 *{
 *    "request": {
 *        "value": "create_room",
 *        "room": "<room_name>"
 *    }
 *}
 *
 *{
 *    "message": "anything here broadcast to everyone in room"
 *}
 *
 * Valid server messages:
 *
 *{
 *    "rooms": [
 *        "<room_name_1>",
 *        "<room_name_2>"
 *    ]
 *}
 *
 *{
 *    "joined_room": "<room name>"
 *}
 *
 *{
 *    "join_error": "room does not exist"
 *}
 *
 *{
 *    "create_error": "room already exists"
 *}
 *
 *{
 *    "create_error": "max rooms reached"
 *}
 *
 *{
 *    "message": {
 *        "value": "message text"
 *        "type": "<connect|disconnect|chat>"
 *        "client": "<client name>"
 *    }
 *}
 *
 */
static void on_message_received(server_conn* conn, endpoint_msg* msg,
                                void* userdata)
{
    all_data* data = userdata;
    chatroom_client* c = server_conn_get_userdata(conn);

    hhlog(HHLOG_LEVEL_DEBUG, "Got message: %.*s", (int)msg->msg_len,
          msg->data);

    cJSON* json = cJSON_ParseWithOpts(msg->data, NULL, 0);
    if (json == NULL || json->next != NULL)
    {
        send_error(conn, "not valid json");
        goto cleanup;
    }

    cJSON* cmd = json->child;
    while (cmd != NULL)
    {
        if (cmd->type == cJSON_Object && strcmp(cmd->string, "request") == 0)
        {
            cJSON* req = cmd->child;
            const char* value = NULL;
            const char* room = NULL;
            const char* user_name = NULL;

            while (req != NULL)
            {
                if (req->type == cJSON_String &&
                    strcmp(req->string, "value") == 0)
                {
                    value = req->valuestring;
                }
                else if(req->type == cJSON_String &&
                        strcmp(req->string, "room") == 0)
                {
                    room = req->valuestring;
                }
                else if(req->type == cJSON_String &&
                        strcmp(req->string, "user_name") == 0)
                {
                    user_name = req->valuestring;
                }
                else
                {
                    send_error(conn, "not valid request");
                    goto cleanup;
                }
                req = req->next;
            }

            if (value == NULL)
            {
                send_error(conn, "not valid request");
                goto cleanup;
            }

            if(strcmp(value, "get_rooms") == 0)
            {
                send_room_list(c, data);
                goto cleanup;
            }
            else if (strcmp(value, "join_room") == 0)
            {
                if (room == NULL || user_name == NULL)
                {
                    send_error(conn, "not valid request");
                    goto cleanup;
                }

                join_room(data, c, room, user_name);
            }
            else if (strcmp(value, "create_room") == 0)
            {
                if (room == NULL || user_name == NULL)
                {
                    send_error(conn, "not valid request");
                    goto cleanup;
                }

                create_room(data, c, room, user_name);
            }
            else if (strcmp(value, "get_clients") == 0)
            {
                if (c->room == NULL)
                {
                    send_error(conn, "you must be in a room to get clients");
                    goto cleanup;
                }

                send_client_list(c);
            }
            else
            {
                send_error(conn, "not valid request");
                goto cleanup;
            }
        }
        else if(cmd->type == cJSON_String &&
                strcmp(cmd->string, "message") == 0)
        {
            if (c->room == NULL)
            {
                send_error(conn, "not a member of a room");
                goto cleanup;
            }

            /* all messages are broadcast to everyone in the room */
            broadcast_chat_msg(c, "chat", cmd->valuestring);
            goto cleanup;
        }
        else
        {
            send_error(conn, "not valid command");
            goto cleanup;
        }

        cmd = cmd->next;
    }

cleanup:
    cJSON_Delete(json);
}

static bool
on_open(server_conn* conn, int* subprotocol_out, int* extensions_out,
        void* userdata)
{
    hhunused(extensions_out);
    hhunused(userdata);

    unsigned num_protocols = server_get_num_client_subprotocols(conn);

    hhlog(HHLOG_LEVEL_DEBUG, "Got subprotocols [");
    bool found = false;
    for (unsigned i = 0; i < num_protocols; i++)
    {
        const char* subprotocol = server_get_client_subprotocol(conn, i);
        hhlog(HHLOG_LEVEL_DEBUG, "    %s", subprotocol);
        if (strcmp(subprotocol, "chatserver") == 0)
        {
            found = true;
            *subprotocol_out = i;
        }
    }
    hhlog(HHLOG_LEVEL_DEBUG, "]");

    return found;
}

static void on_connect(server_conn* conn, void* userdata)
{
    all_data* data = userdata;

    /*
     * since number of chatroom_client is equal to server max_conns, this
     * should never happen
     */
    hhassert_pointer(data->client_free_head);

    chatroom_client* c = allocate_client(data);
    server_conn_set_userdata(conn, c);

    memset(c, 0, sizeof(*c));
    c->conn = conn;
    hhlog(HHLOG_LEVEL_DEBUG, "Got connect (conn: %p)", conn);
}

static void
on_close(server_conn* conn, int code, const char* reason, int reason_len,
         void* userdata)
{
    hhunused(code);
    hhunused(reason);
    hhunused(reason_len);

    all_data* data = userdata;
    chatroom_client* c = server_conn_get_userdata(conn);

    if (c == NULL)
    {
        hhlog(HHLOG_LEVEL_DEBUG, "Got close, NULL client");
        return;
    }

    /* remove the client from the chatroom, if applicable */
    if (c != NULL && c->room != NULL)
    {
        room_remove_client(data, c->room, c);
    }

    deallocate_client(data, c);

    hhlog(HHLOG_LEVEL_DEBUG, "Got close: (%d, %.*s)", code, (int)reason_len,
          reason);
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
    .log_to_stdout = true,
    .log_location = true
};

static void init_data(all_data* data)
{
    hhassert(data->client_active_head == NULL);
    hhassert(data->client_active_tail == NULL);
    hhassert(data->client_free_head == NULL);
    hhassert(data->client_free_tail == NULL);
    hhassert(data->room_active_head == NULL);
    hhassert(data->room_active_tail == NULL);
    hhassert(data->room_free_head == NULL);
    hhassert(data->room_free_tail == NULL);

    /* all rooms and clients start out free, add them all to the free lists */
    for (int i = 0; i < MAX_CHATROOMS; i++)
    {
        chatroom* r = &(data->rooms[i]);
        INLIST_APPEND(data, r, next, prev, room_free_head, room_free_tail);
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        chatroom_client* c = &(data->clients[i]);
        INLIST_APPEND(data, c, global_next, global_prev, client_free_head,
                      client_free_tail);
    }
}

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
    options.max_clients = MAX_CLIENTS;

    options.endp_settings.protocol_buf_init_len = 1 * 1024;

    protocol_settings* conn_settings = &options.endp_settings.conn_settings;
    conn_settings->write_max_frame_size = 16 * 1024;
    conn_settings->read_max_msg_size = MAX_MESSAGE_SIZE;
    conn_settings->read_max_num_frames = 1024;
    conn_settings->max_handshake_size = 2048;
    conn_settings->rand_func = NULL;
    options.port = (uint16_t)atoi(argv[1]);

    server_callbacks callbacks;
    callbacks.on_open = on_open;
    callbacks.on_message = on_message_received;
    callbacks.on_connect = on_connect;
    callbacks.on_ping = NULL;
    callbacks.on_close = on_close;

    hhlog_set_options(&g_log_options);

    init_data(&g_data);

    cJSON_Hooks hooks;
    hooks.malloc_fn = hhmalloc;
    hooks.free_fn = hhfree;
    cJSON_InitHooks(&hooks);

    g_serv = server_create(&options, &callbacks, &g_data);

    server_listen(g_serv);

    server_destroy(g_serv);
    g_serv = NULL;
    exit(0);
}

