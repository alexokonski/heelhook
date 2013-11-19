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
#include "hhmemory.h"
#include "platform.h"
#include "pqueue.h"

typedef struct
{
    int fd;
    int mask;
    void* data;
    event_io_callback* read_callback;
    event_io_callback* write_callback;
} event_io;

struct event_time
{
    void* data;
    event_time_callback* callback;
    pqueue_elem_ref pqueue_ref;
    uint64_t frequency_ms;
    uint64_t next_fire_time_ms;
};

typedef struct event_time event_time;

typedef struct
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
    event_time_id id_counter;
    pqueue* time_events;
    int stop;
};

typedef enum
{
    PLATFORM_RESULT_SUCCESS,
    PLATFORM_RESULT_OUT_OF_MEMORY,
    PLATFORM_RESULT_ERROR
} event_platform_result;

static int time_event_cmp(pqueue_value a, pqueue_value b);

static pqueue_spec event_pqueue_spec =
{
   .sort = PQUEUE_SORT_MIN,
   .cmp = time_event_cmp
};

#ifdef HAVE_EPOLL
    #include "event_epoll.c"
#else
    #include "event_poll.c"
#endif

static int time_event_cmp(pqueue_value a, pqueue_value b)
{
    event_time* t1 = (event_time*)a.p_val;
    event_time* t2 = (event_time*)b.p_val;

    if (t1->next_fire_time_ms < t2->next_fire_time_ms)
    {
        return -1;
    }
    else if(t1->next_fire_time_ms > t2->next_fire_time_ms)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

event_loop* event_create_loop(int max_io_events)
{
    event_loop* loop = hhmalloc(sizeof(*loop));
    if (loop == NULL) goto create_error;

    loop->io_events = hhmalloc(sizeof(*(loop->io_events)) * max_io_events);
    loop->fired_events =
        hhmalloc(sizeof(*(loop->fired_events)) * max_io_events);

    if (loop->io_events == NULL || loop->fired_events == NULL)
    {
        goto create_error;
    }

    loop->time_events = pqueue_create(&event_pqueue_spec);

    loop->num_events = max_io_events;
    loop->max_fd = -1;
    loop->stop = 0;

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

static uint64_t event_get_now_ms(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000 + now.tv_usec / 1000;
}

event_time_id event_add_time_event(
    event_loop* loop,
    event_time_callback* callback,
    uint64_t frequency_ms,
    void* data
)
{
    /* initialize the new time event */
    event_time* et = hhmalloc(sizeof(*et));
    if (et == NULL)
    {
        return EVENT_INVALID_TIME_ID;
    }

    et->data = data;
    et->callback = callback;
    et->frequency_ms = frequency_ms;
    et->next_fire_time_ms = event_get_now_ms() + frequency_ms;

    /* now put it in the priority queue */
    pqueue_value val;
    val.p_val = et;
    et->pqueue_ref = pqueue_insert(loop->time_events, val);

    return et;
}

void event_delete_time_event(event_loop* loop, event_time_id id)
{
    event_time* et = id;

    /* remove from priority queue */
    pqueue_delete(loop->time_events, et->pqueue_ref);

    hhfree(et);
}

void event_destroy_loop(event_loop* loop)
{
    event_platform_destroy(loop);
    hhfree(loop->io_events);
    hhfree(loop->fired_events);

    /* destroy all time events */
    pqueue* q = loop->time_events;
    pqueue_iterator it;
    for (pqueue_iter_begin(q, &it);
         pqueue_iter_is_valid(q, &it);
         pqueue_iter_next(q, &it))
    {
        /* free event */
        event_time* et = pqueue_iter_get_value(&it).p_val;
        hhfree(et);
    }

    /* destroy pqueue object */
    pqueue_destroy(q);

    hhfree(loop);
}

static void event_process_all_events(event_loop* loop, int flags)
{
    int time_ms;
    uint64_t now;
    event_time* et;

    if (flags & EVENT_DONT_BLOCK)
    {
        /* blocking for 0 means don't block */
        time_ms = 0;
    }
    else if (pqueue_get_size(loop->time_events) > 0)
    {
        et = pqueue_peek(loop->time_events).p_val;
        now = event_get_now_ms();
        time_ms = et->next_fire_time_ms - now;

        /* don't block if somehow we went back to the future */
        if (time_ms < 0) time_ms = 0;
    }
    else
    {
        /*
         * no time events and no EVENT_DONT_BLOCK flag, block until an
         * io event
         */
        time_ms = -1;
    }

    int num_fired;
    if (event_platform_poll(loop, time_ms, &num_fired)
                                            != PLATFORM_RESULT_SUCCESS)
    {
        return;
    }

    /* fire time events */
    if (pqueue_get_size(loop->time_events))
    {
        now = event_get_now_ms();
        et = pqueue_peek(loop->time_events).p_val;
        while (now >= et->next_fire_time_ms)
        {
            /* get ready to fire the next event */
            et = pqueue_pop(loop->time_events).p_val;

            /* fire it  */
            et->callback(loop, et, et->data);
            pqueue_value val;
            val.p_val = et;

            /* put it back in for next time */
            et->next_fire_time_ms = now + et->frequency_ms;
            et->pqueue_ref = pqueue_insert(loop->time_events, val);
        }
    }

    /* now fire io events */
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

