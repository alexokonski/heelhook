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

#include <sys/epoll.h>

typedef struct
{
    int epollfd;
    struct epoll_event* epoll_events;
} platform_state;

static event_platform_result event_platform_create(event_loop* loop)
{
    platform_state* state = hhmalloc(sizeof(platform_state));
    if (state == NULL) return PLATFORM_RESULT_OUT_OF_MEMORY;

    state->epoll_events = hhmalloc(
        sizeof(struct epoll_event) * loop->num_events
    );

    if (state->epoll_events == NULL)
    {
        hhfree(state);
        return PLATFORM_RESULT_OUT_OF_MEMORY;
    }

    /* constant does nothing on newer kernels */
    state->epollfd = epoll_create(1024);
    if (state->epollfd == -1)
    {
        hhfree(state->epoll_events);
        hhfree(state);
        return PLATFORM_RESULT_ERROR;
    }
    
    loop->platform_data = state;

    return PLATFORM_RESULT_SUCCESS;
}

static void event_platform_destroy(event_loop* loop)
{
    platform_state* state = loop->platform_data;

    close(state->epollfd);
    hhfree(state->epoll_events);
    hhfree(state);
}

static event_platform_result event_platform_add(event_loop* loop, int fd,
                                                int mask)
{
    platform_state* state = loop->platform_data;
    struct epoll_event epevent;

    /* mod if this fd is already monitored, otherwise it's new and we add */
    int op = loop->io_events[fd].mask == EVENT_NONE ? 
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    epevent.events = 0;
    if (mask & EVENT_READABLE) epevent.events |= EPOLLIN;
    if (mask & EVENT_WRITEABLE) epevent.events |= EPOLLOUT;

    epevent.data.fd = fd;
    if (epoll_ctl(state->epollfd, op, fd, &epevent) == -1)
    {
        return PLATFORM_RESULT_ERROR;
    }

    return PLATFORM_RESULT_SUCCESS;
}

static event_platform_result event_platform_remove(event_loop* loop, int fd,
                                                    int remove_mask)
{
    platform_state* state = loop->platform_data;
    struct epoll_event epevent;

    int new_mask = loop->io_events[fd].mask & (~remove_mask);

    epevent.events = 0;
    if (new_mask & EVENT_READABLE) epevent.events |= EPOLLIN;
    if (new_mask & EVENT_WRITEABLE) epevent.events |= EPOLLOUT;

    epevent.data.fd = fd;
    int op = new_mask == EVENT_NONE ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;

    if(epoll_ctl(state->epollfd, op, fd, &epevent) == -1)
    {
        return PLATFORM_RESULT_ERROR;
    }

    return PLATFORM_RESULT_SUCCESS;
}

event_platform_result event_platform_poll(event_loop* loop, struct timeval *tv,
                                          int* num_fired)
{
    platform_state* state = loop->platform_data;

    int num_ready = epoll_wait(
        state->epollfd, 
        state->epoll_events, 
        loop->num_events,
        tv != NULL ? (tv->tv_sec*1000 + tv->tv_usec/1000) : -1
    );

    (*num_fired) = num_ready;

    if (num_ready < 0) return PLATFORM_RESULT_ERROR;

    for (int i = 0; i < num_ready; i++)
    {
        int mask = 0;
        struct epoll_event* event = &state->epoll_events[i];
        
        if (event->events & EPOLLIN) mask |= EVENT_READABLE;
        if (event->events & EPOLLOUT) mask |= EVENT_WRITEABLE;
        
        /*
         * If an error occurs, inform the user on whatever events he's
         * listening to
         */
        if (event->events & (EPOLLERR|EPOLLHUP))
        {
           mask |= loop->io_events[event->data.fd].mask;
        }
        loop->fired_events[i].fd = event->data.fd;
        loop->fired_events[i].mask = mask;
    }

    return PLATFORM_RESULT_SUCCESS;
}
