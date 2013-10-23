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

#include <poll.h>

typedef struct platform_state
{
    struct pollfd* poll_fds;
} platform_state;

static event_platform_result event_platform_create(event_loop* loop)
{
    platform_state* state = hhmalloc(sizeof(platform_state));
    if (state == NULL) return PLATFORM_RESULT_OUT_OF_MEMORY;

    state->poll_fds = hhmalloc(sizeof(struct pollfd) * loop->num_events);

    for (int i = 0; i < loop->num_events; i++)
    {
        struct pollfd* pfd = &state->poll_fds[i];
        pfd->fd = i;
        pfd->events = 0;
        pfd->revents = 0;
    }

    loop->platform_data = state;

    return PLATFORM_RESULT_SUCCESS;
}

static void event_platform_destroy(event_loop* loop)
{
    platform_state* state = loop->platform_data;

    hhfree(state->poll_fds);
    hhfree(state);
}

static event_platform_result event_platform_add(event_loop* loop, int fd,
                                                int mask)
{
    platform_state* state = loop->platform_data;
    struct pollfd* pfd = &state->poll_fds[fd];

    pfd->fd = fd; /* tell poll to actually block on this fd */
    if (mask & EVENT_READABLE) pfd->events |= POLLIN;
    if (mask & EVENT_WRITEABLE) pfd->events |= POLLOUT;

    return PLATFORM_RESULT_SUCCESS;
}

static event_platform_result event_platform_remove(event_loop* loop, int fd,
                                                    int remove_mask)
{
    platform_state* state = loop->platform_data;
    struct pollfd* pfd = &state->poll_fds[fd];

    int new_mask = loop->io_events[fd].mask & (~remove_mask);

    pfd->events = 0;
    if (new_mask & EVENT_READABLE) pfd->events |= POLLIN;
    if (new_mask & EVENT_WRITEABLE) pfd->events |= POLLOUT;

    return PLATFORM_RESULT_SUCCESS;
}

event_platform_result event_platform_poll(event_loop* loop, struct timeval *tv,
                                          int* num_fired)
{
    platform_state* state = loop->platform_data;

    int timeout = tv != NULL ? (tv->tv_sec*1000 + tv->tv_usec/1000) : -1;

    int nfired = poll(state->poll_fds, loop->max_fd + 1, timeout);

    (*num_fired) = nfired;

    if (nfired == -1)
    {
        return PLATFORM_RESULT_ERROR;
    }

    if (nfired > 0)
    {
        int j = 0;
        for (int i = 0; i < loop->max_fd + 1; i++)
        {
            int mask = 0;
            struct pollfd* pfd = &state->poll_fds[i];

            if (pfd->revents == 0) continue;

            if (pfd->revents & POLLIN) mask |= EVENT_READABLE;
            if (pfd->revents & POLLOUT) mask |= EVENT_WRITEABLE;

            /*
             * If an error occurs, inform the user on whatever events he's
             * listening to
             */
            if (pfd->revents & (POLLERR|POLLHUP))
            {
               mask |= loop->io_events[i].mask;
            }
            loop->fired_events[j].fd = i;
            loop->fired_events[j].mask = mask;
            j++;
        }
    }

    return PLATFORM_RESULT_SUCCESS;
}
