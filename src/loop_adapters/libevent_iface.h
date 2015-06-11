/* libevent_iface - loop interface implementation for libevent
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

#ifndef LIBEVENT_IFACE_H__
#define LIBEVENT_IFACE_H__

#include "../config.h"
#include "../event.h"
#include "../hhmemory.h"
#include "../hhassert.h"
#include "../util.h"
#include "../iloop.h"
#include <event2/event.h>

typedef struct
{
    struct event_base* eloop;
    struct event* time_ids[ILOOP_NUMBER_OF_TIME_CB];
    struct event* read_evs;
    struct event* write_evs;
    int num_events;
} iloop_libevent_data;

static inline 
void iloop_libevent_call_io_cb(iloop_cb_type type, int fd, void* data)
{
    iloop* loop = iloop_from_io(type, data);
    loop->io_cbs[type](loop, fd, data);
}

static void iloop_libevent_accept_cb(int fd, short event, void *data)
{
    hhunused(event);
    iloop_libevent_call_io_cb(ILOOP_ACCEPT_CB, fd, data);
}

static void iloop_libevent_read_cb(int fd, short event, void* data)
{
    hhunused(event);
    iloop_libevent_call_io_cb(ILOOP_READ_CB, fd, data);
}

static void iloop_libevent_write_cb(int fd, short event, void* data)
{
    hhunused(event);
    iloop_libevent_call_io_cb(ILOOP_WRITE_CB, fd, data);
}

static void iloop_libevent_worker_cb(int fd, short event, void* data)
{
    hhunused(event);
    iloop_libevent_call_io_cb(ILOOP_WORKER_CB, fd, data);
}

static event_callback_fn const g_iloop_libevent_io_cbs[ILOOP_NUMBER_OF_IO_CB] =
{
    iloop_libevent_accept_cb, /* ILOOP_ACCEPT_CB */
    iloop_libevent_read_cb, /* ILOOP_READ_CB */
    iloop_libevent_write_cb, /* ILOOP_WRITE_CB */
    iloop_libevent_worker_cb /* ILOOP_WORKER_CB */
};

static iloop_result
iloop_libevent_io_helper(struct event* ev_array, int fd, int mask,
                         iloop_cb_type type, struct event_base* eloop,
                         void* data)
{
    struct event* ev = (struct event*)((char*)ev_array + (fd * event_get_struct_event_size()));
    if (event_initialized(ev) && event_pending(ev, mask, NULL))
    {
    
        hhassert(fd == event_get_fd(ev));
        hhassert(data == event_get_callback_arg(ev));
    }
    else
    {
        int r = event_assign(ev, eloop, fd, mask | EV_PERSIST,
                             g_iloop_libevent_io_cbs[type], data);

        if (r != 0) return ILOOP_FAILURE;
        event_add(ev, NULL);
    }

    return ILOOP_SUCCESS;
}

static iloop_result
iloop_libevent_add_io(iloop* loop, int fd, int mask, iloop_cb_type type,
                      void* data)
{
    iloop_libevent_data* iloop_data = loop->userdata;
    if (fd >= iloop_data->num_events)
    {
        hhassert(0);
        return ILOOP_FAILURE;
    }

    struct event_base* eloop = iloop_data->eloop;
    iloop_result r;
    if (mask & ILOOP_READABLE)
    {
        struct event* evs = iloop_data->read_evs;
        if ((r=iloop_libevent_io_helper(evs, fd, EV_READ, type, eloop, data))
                != ILOOP_SUCCESS)
        {
            return r;
        }
    }

    if (mask & ILOOP_WRITEABLE)
    {
        struct event* evs = iloop_data->write_evs;
        if ((r=iloop_libevent_io_helper(evs, fd, EV_WRITE, type, eloop, data))
                != ILOOP_SUCCESS)
        {
            return r;
        }
    }

    return ILOOP_SUCCESS;
}

static void iloop_libevent_stop_loop(iloop* loop)
{
    iloop_libevent_data* iloop_data = loop->userdata;
    struct event_base* eloop = iloop_data->eloop;
    event_base_loopbreak(eloop);
}

static void iloop_libevent_delete_io(iloop* loop, int fd, int mask)
{
    iloop_libevent_data* iloop_data = loop->userdata;
    if (mask & ILOOP_READABLE)
    {
        struct event* rev = (struct event*)((char*)iloop_data->read_evs + (fd * event_get_struct_event_size()));
        if (event_initialized(rev))
        {
            event_del(rev);
        }
    }

    if (mask & ILOOP_WRITEABLE)
    {
        struct event* wev = (struct event*)((char*)iloop_data->write_evs + (fd * event_get_struct_event_size()));
        if (event_initialized(wev))
        {
            event_del(wev);
        }
    }
}

static inline
void iloop_call_time_cb(iloop_time_cb_type type, void* data)
{
    iloop* loop = iloop_from_time(type, data);
    loop->time_cbs[type](loop, type, data);
}

static void iloop_libevent_watchdog_cb(int fd, short event, void *data)
{
    hhunused(fd);
    hhunused(event);
    iloop_call_time_cb(ILOOP_WATCHDOG_CB, data);
}

static void iloop_libevent_heartbeat_cb(int fd, short event, void *data)
{
    hhunused(fd);
    hhunused(event);
    iloop_call_time_cb(ILOOP_HEARTBEAT_CB, data);
}

static
void iloop_libevent_heartbeat_expire_cb(int fd, short event, void *data)
{
    hhunused(fd);
    hhunused(event);
    iloop_call_time_cb(ILOOP_HEARTBEAT_EXPIRE_CB, data);
}

static void
iloop_libevent_handshake_timeout_cb(int fd, short event, void *data)
{
    hhunused(fd);
    hhunused(event);
    iloop_call_time_cb(ILOOP_HANDSHAKE_TIMEOUT_CB, data);
}

static event_callback_fn const g_iloop_event_time_cbs[ILOOP_NUMBER_OF_TIME_CB] =
{
    iloop_libevent_watchdog_cb, /* ILOOP_WATCHDOG_CB */
    iloop_libevent_heartbeat_cb, /* ILOOP_HEARTBEAT_CB */
    iloop_libevent_heartbeat_expire_cb, /* ILOOP_HEARTBEAT_EXPIRE_CB */
    iloop_libevent_handshake_timeout_cb /* ILOOP_HANDSHAKE_TIMEOUT_CB */
};

typedef struct
{
    void* data;
    uint64_t frequency_ms;
    iloop_time_cb_type type;
    iloop_libevent_data* iloop_data;
} iloop_libevent_deferred_time;

static void iloop_libevent_ms_to_timeval(uint64_t ms, struct timeval* val)
{
    val->tv_sec = ms / 1000;
    val->tv_usec = (ms % 1000) * 1000;
}

static void iloop_libevent_time_delay_cb(int fd, short event, void *data)
{
    hhunused(fd);
    hhunused(event);

    iloop_libevent_deferred_time* d = data;
    iloop_libevent_data* iloop_data = d->iloop_data;
    struct event_base* eloop = iloop_data->eloop;

    if (iloop_data->time_ids[d->type] == NULL)
    {
        iloop_data->time_ids[d->type] =
            event_new(eloop, -1, EV_PERSIST, g_iloop_event_time_cbs[d->type],
                      d->data);
    }
    struct timeval val;
    iloop_libevent_ms_to_timeval(d->frequency_ms, &val);
    event_add(iloop_data->time_ids[d->type], &val);
    hhfree(d);
}

static void iloop_libevent_add_time(iloop* loop, iloop_time_cb_type type,
                           uint64_t frequency_ms, uint64_t initial_delay_ms,
                           void* data)
{
    iloop_libevent_data* iloop_data = loop->userdata;
    struct event_base* eloop = iloop_data->eloop;
    struct timeval val;

    if (initial_delay_ms > 0)
    {
        iloop_libevent_deferred_time* d = hhmalloc(sizeof(*d));
        d->data = data;
        d->frequency_ms = frequency_ms;
        d->type = type; 
        d->iloop_data = iloop_data;

        iloop_libevent_ms_to_timeval(initial_delay_ms, &val);
        event_base_once(eloop, -1, EV_TIMEOUT, iloop_libevent_time_delay_cb,
                        d, &val);
    }
    else
    {
        if (iloop_data->time_ids[type] == NULL)
        {
            iloop_data->time_ids[type] =
                event_new(eloop,-1,EV_PERSIST,g_iloop_event_time_cbs[type],data);
        }

        iloop_libevent_ms_to_timeval(frequency_ms, &val);
        event_add(iloop_data->time_ids[type], &val);
    }
}

static void iloop_libevent_delete_time(iloop* loop, iloop_time_cb_type type)
{
    iloop_libevent_data* iloop_data = loop->userdata;
    if (iloop_data->time_ids[type] != NULL)
    {
        event_del(iloop_data->time_ids[type]);
    }
}

static void iloop_libevent_cleanup(iloop* loop)
{
    iloop_libevent_data* iloop_data = loop->userdata;

    for (int i = 0; i < ILOOP_NUMBER_OF_TIME_CB; i++)
    {
        if (iloop_data->time_ids[i] != NULL)
        {
            event_free(iloop_data->time_ids[i]);
        }
    }
    hhfree(iloop_data->read_evs);
    hhfree(iloop_data->write_evs);
    hhfree(iloop_data);
    loop->userdata = NULL;
}

static bool iloop_attach_libevent(iloop* loop, struct event_base* eloop,
                                  config_server_options* options)
{
    iloop_libevent_data* iloop_data = hhcalloc(1, sizeof(*iloop_data));    
    if (iloop_data == NULL) return false;

    iloop_data->eloop = eloop;
    iloop_data->num_events = options->max_clients + 1024;
    size_t ev_size = event_get_struct_event_size();
    iloop_data->read_evs = hhcalloc(iloop_data->num_events, ev_size);
    iloop_data->write_evs = hhcalloc(iloop_data->num_events, ev_size);

    loop->userdata = iloop_data;
    loop->add_io = iloop_libevent_add_io;
    loop->delete_io = iloop_libevent_delete_io;
    loop->add_time = iloop_libevent_add_time;
    loop->delete_time = iloop_libevent_delete_time;
    loop->cleanup = iloop_libevent_cleanup;
    loop->listen = NULL;
    loop->stop = iloop_libevent_stop_loop;

    return true;
}

#endif /* LIBEVENT_IFACE_H__ */
