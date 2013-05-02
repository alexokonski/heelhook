/* event - Event driven IO module.  Originally based on the 'ae' 
 * implementation found in redis: https://github.com/antirez/redis
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

#include <sys/time.h>
#include <unistd.h>

#include "event.h"
#include "platform.h"
#include "hhmemory.h"

typedef struct event_io
{
    int fd;
    int mask;
    void* data;
    event_io_callback* read_callback;
    event_io_callback* write_callback;
} event_io;

typedef struct event_fired
{
    int fd;
    int mask;
} event_fired;

struct event_loop
{
    event_io* io_events; /* array of mapped io_events */
    event_fired* fired_events; /* events that were just fired */
    int num_events; /* max size of io_events and fired_events */
    int max_fd; /* max fd in io_events; current size of io_events */
    void* platform_data; /* platform-specific polling API data */
    int stop;
};

typedef enum event_platform_result
{
    PLATFORM_RESULT_SUCCESS,
    PLATFORM_RESULT_OUT_OF_MEMORY,
    PLATFORM_RESULT_ERROR
} event_platform_result;

#ifdef HAVE_EPOLL
    #include "event_epoll.c"
#else
    #include "event_poll.c"
#endif

event_loop* event_create_loop(int loop_size)
{
    event_loop* loop = hhmalloc(sizeof(*loop));
    if (loop == NULL) goto create_error;
    
    loop->io_events = hhmalloc(sizeof(*(loop->io_events)) * loop_size);
    loop->fired_events = hhmalloc(sizeof(*(loop->fired_events)) * loop_size);

    if (loop->io_events == NULL || loop->fired_events == NULL)
    {
        goto create_error;
    }

    loop->num_events = loop_size;
    loop->max_fd = -1;

    if (event_platform_create(loop) != PLATFORM_RESULT_SUCCESS)
    {
        goto create_error;
    }

    for (int i = 0; i < loop->num_events; i++)
    {
        event_io* event = &loop->io_events[i];

        event->fd = -1;
        event->mask = EVENT_NONE;
        event->read_callback = NULL;
        event->write_callback = NULL;
    }

    return loop;

create_error:
    if (loop != NULL)
    {
        hhfree(loop->io_events);
        hhfree(loop->fired_events);
        hhfree(loop);
    }

    return NULL;
}

void event_stop_loop(event_loop* loop)
{
    loop->stop = 1;
}

event_result event_add_io_event(event_loop* loop, int fd, int mask,
                                event_io_callback* callback, void* data)
{
    if (fd >= loop->num_events) return EVENT_RESULT_EVENT_LOOP_FULL;

    event_io* event = &loop->io_events[fd];

    if (event_platform_add(loop, fd, mask) != PLATFORM_RESULT_SUCCESS)
    {
        return EVENT_RESULT_PLATFORM_ERROR; 
    }

    event->fd = fd;
    event->mask |= mask;
    event->data = data;
     
    if (mask & EVENT_READABLE) event->read_callback = callback;
    if (mask & EVENT_WRITEABLE) event->write_callback = callback;

    if (loop->max_fd < fd) loop->max_fd = fd;

    return EVENT_RESULT_SUCCESS;
}

void event_delete_io_event(event_loop* loop, int fd, int mask)
{
    if (fd >= loop->num_events) return;

    event_io* event = &loop->io_events[fd];
    if (event->mask == EVENT_NONE) return; /* event already deleted... */

    event->mask &= (~mask);

    if (fd == loop->max_fd && event->mask == EVENT_NONE)
    {
        for(int i = loop->max_fd-1; i >= 0; i--)
        {
            if(loop->io_events[i].mask != EVENT_NONE)
            {
                loop->max_fd = i;
                break;
            }
        }
    }
    event_platform_remove(loop, fd, mask);
}

void event_destroy_loop(event_loop* loop)
{
    event_platform_destroy(loop);
    hhfree(loop->io_events);
    hhfree(loop->fired_events);
    hhfree(loop);
}

static void event_process_all_events(event_loop* loop, int flags)
{
    struct timeval tv, *tv_ptr = NULL;
    if (flags & EVENT_DONT_BLOCK)
    {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tv_ptr = &tv;
    }

    int num_fired;
    if (event_platform_poll(loop, tv_ptr, &num_fired)
                                            != PLATFORM_RESULT_SUCCESS)
    {
        return;
    }

    for (int i = 0; i < num_fired; i++)
    {
        event_fired* fired = &loop->fired_events[i];
        int fd = fired->fd;
        int fired_mask = fired->mask;

        event_io* event = &loop->io_events[fd];
        
        /* 
         * Make sure to also check the original mask, it might have
         * been cleared by a call to event_delete_io_event while processing 
         * a previous event.
         */
        int read_fired = 0;
        if (event->mask & (fired_mask & EVENT_READABLE))
        {
            read_fired = 1;
            event->read_callback(loop, fd, event->data); 
        }
        
        if (event->mask & (fired_mask & EVENT_WRITEABLE))
        {
            if (!read_fired || event->read_callback != event->write_callback)
            {
                event->write_callback(loop, fd, event->data);                
            }
        }
    }
}

/* This function blocks until an event calls event_stop_loop */
void event_pump_events(event_loop* loop, int flags)
{
    while (!loop->stop)
    {
        event_process_all_events(loop, flags);
    }
}

