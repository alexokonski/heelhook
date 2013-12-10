/* test_event - Test the most basic functionality of event module
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

#include "../event.h"
#include "../util.h"

#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char* g_test_messages[] =
{
    "TEST_MESSAGE 1",
    "TEST 2",
    "MSG 3"
};

const char* g_test_replies[] =
{
    "REPLY 1",
    "TEST REPLY 2",
    "REPLRPEL 3"
};

static const int NUM_TEST_MESSAGES = 3;

static int g_messages_received = 0;
static int g_messages_sent = 0;
static int g_server_socket = -1;

typedef struct
{
    uint64_t freq_ms;
    uint64_t last_fire_time_ms;
} time_event_state;

static time_event_state TIME_EVENT_STATES[] =
{
    {10, 0},
    {15, 0},
    {11, 0},
    {55, 0}
};

static void error_exit(const char* error)
{
    if (error != NULL)
    {
        printf("FAIL: %s\n", error);
    }
    else
    {
        printf("FAIL: %s\n", strerror(errno));
    }
    exit(1);
}

static int strsize(const char* string)
{
    return strlen(string) + 1;
}

static uint64_t get_now_ms(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000 + now.tv_usec / 1000;
}

static void time_callback(
    event_loop* loop,
    event_time_id id,
    void* data
)
{
    hhunused(loop);
    hhunused(id);

    time_event_state* state = data;
    uint64_t last_fire_time_ms = state->last_fire_time_ms;

    if (last_fire_time_ms == 0)
    {
        state->last_fire_time_ms = get_now_ms();
    }
    else
    {
        uint64_t now = get_now_ms();
        state->last_fire_time_ms = now;
        int64_t elapsed = now - last_fire_time_ms;
        int64_t expected_elapsed = state->freq_ms;
        const int64_t epsilon = 4;
        if (llabs(elapsed - expected_elapsed) > epsilon)
        {
            printf("EXPECTED ELAPSED TO BE WITHIN %" PRIi64 " OF %" PRIi64
                   " INSTEAD WAS %" PRIi64 "\n",
                   epsilon, expected_elapsed, elapsed);
            error_exit("TIME EVENT ERROR");
        }
        else
        {
            /*printf("FREQ %" PRIu64 " FIRED IN %lli EPSILON\n",
                   state->freq_ms, llabs(elapsed - expected_elapsed));*/
        }
    }

}


static void write_to_client_callback(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    const char* msg = g_test_replies[g_messages_sent];

    g_messages_sent++;

    int num_written = write(fd, msg, strsize(msg));
    if (num_written == -1)
    {
        error_exit(NULL);
    }

    if (g_messages_sent == NUM_TEST_MESSAGES)
    {
        event_stop_loop(loop);
    }
    /*printf("WROTE: %s\n", msg);*/
    event_delete_io_event(loop, fd, EVENT_WRITEABLE);
}

static void read_from_client_callback(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    char buffer[1024];
    int num_read = read(fd, buffer, sizeof(buffer));
    if (num_read == -1)
    {
        error_exit(NULL);
    }

    if (g_messages_received >= NUM_TEST_MESSAGES)
    {
        error_exit("TOO MANY MESSAGES");
    }

    const char* msg = g_test_messages[g_messages_received];
    if (strncmp(buffer, msg, strsize(msg)) != 0)
    {
        printf("Messages don't match: %s, %s\n", buffer, msg);
        error_exit(NULL);
    }

    g_messages_received++;

    /*printf("READ: %s, ADDING IO EVENT %d\n", buffer, fd);*/
    event_result r = event_add_io_event(
        loop,
        fd,
        EVENT_WRITEABLE,
        write_to_client_callback,
        NULL
    );

    if (r != EVENT_RESULT_SUCCESS)
    {
        error_exit("NON_SUCCESS WHEN ADDING WRITE CALLBACK");
    }
}

static void accept_callback(event_loop* loop, int fd, void* data)
{
    hhunused(data);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr*)(&addr), &len);

    if (client_fd == -1) error_exit(NULL);

    event_result r = event_add_io_event(
        loop,
        client_fd,
        EVENT_READABLE,
        read_from_client_callback,
        NULL
    );

    if (r != EVENT_RESULT_SUCCESS)
    {
        error_exit("NON-SUCCESS WHEN ADDING READ CALLBACK");
    }
}


static void* event_thread(void* in)
{
    hhunused(in);

    event_loop* loop = event_create_loop(1024);
    if (loop == NULL) error_exit("NULL WHEN CREATING EVENT LOOP");

    if ((g_server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        error_exit(NULL);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8088);
    inet_aton("127.0.0.1", &addr.sin_addr);

    if (bind(g_server_socket, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
    {
        error_exit(NULL);
    }

    if (listen(g_server_socket, 512) == -1)
    {
        error_exit(NULL);
    }

    event_result r = event_add_io_event(
        loop,
        g_server_socket,
        EVENT_READABLE,
        accept_callback,
        NULL
    );

    if (r != EVENT_RESULT_SUCCESS)
    {
        error_exit("NON-SUCCESS WHEN ADDING accept_callback");
    }

    for (unsigned int i = 0; i < hhcountof(TIME_EVENT_STATES); i++)
    {
        event_add_time_event(
            loop,
            time_callback,
            TIME_EVENT_STATES[i].freq_ms,
            &TIME_EVENT_STATES[i]
        );
    }

    event_pump_events(loop, 0);

    event_destroy_loop(loop);

    return NULL;
}

int main(int argc, char** argv)
{
    hhunused(argc);
    hhunused(argv);

    pthread_t thread;

    if (pthread_create(&thread, NULL, event_thread, NULL) != 0)
    {
        error_exit("CREATING THREAD FAILED");
    }

    /* give thread time to set up */
    sleep(1);

    int s = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8088);
    inet_aton("127.0.0.1", &addr.sin_addr);
    if (connect(s, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
    {
        error_exit(NULL);
    }

    char buffer[1024];
    int n;
    for (int i = 0; i < NUM_TEST_MESSAGES; i++)
    {
        const char* msg = g_test_messages[i];
        /*printf("SENDING: %s\n", msg);*/
        n = write(s, msg, strsize(msg));
        if (n == -1)
        {
            error_exit(NULL);
        }

        n = read(s, buffer, sizeof(buffer));
        if (n == -1)
        {
            error_exit(NULL);
        }

        /*printf("GOT REPLY: %s\n", buffer);*/
        if (strncmp(g_test_replies[i], buffer,
                    strsize(g_test_replies[i])) != 0)
        {
            printf("Messages don't match: %s, %s\n", buffer, g_test_replies[i]);
            error_exit("REPLY DIDN'T MATCH");
        }
    }

    /* sleep for some msecs so the time events can be tested */
    usleep(800 * 1000);

    for (unsigned int i = 0; i < hhcountof(TIME_EVENT_STATES); i++)
    {
        if (TIME_EVENT_STATES[i].last_fire_time_ms == 0)
        {
            printf(
                "TIME EVENT STATE WITH 0 TIME: %" PRIi64 " ms event\n",
                TIME_EVENT_STATES[i].freq_ms
            );
            exit(1);
        }
    }

    pthread_join(thread, NULL);

    exit(0);
}
