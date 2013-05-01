#include "../event.h"
#include "../hhmemory.h"

#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNUSED(V) ((void) V)

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

static void write_to_client_callback(event_loop* loop, int fd, void* data)
{
    UNUSED(data);

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

    event_delete_io_event(loop, fd, EVENT_WRITEABLE);
}

static void read_from_client_callback(event_loop* loop, int fd, void* data)
{
    UNUSED(data);

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
    UNUSED(data);

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
    UNUSED(in);

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

    event_pump_events(loop, 0);

    event_destroy_loop(loop);

    return NULL;
}

int main(int argc, char** argv)
{
    UNUSED(argc);
    UNUSED(argv);

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

        if (strncmp(g_test_replies[i], buffer, 
                    strsize(g_test_replies[i])) != 0)
        {
            printf("Messages don't match: %s, %s\n", buffer, g_test_replies[i]);
            error_exit("REPLY DIDN'T MATCH");
        }
    }

    pthread_join(thread, NULL);

    exit(0);
}
