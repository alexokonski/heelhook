/* event_iface - loop interface implementation for the default 'event' loop.
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

#ifndef EVENT_IFACE_H__
#define EVENT_IFACE_H__

#include "../config.h"
#include "../event.h"
#include "../hhmemory.h"
#include "../hhassert.h"
#include "../util.h"
#include "../iloop.h"

typedef struct
{
    event_loop* eloop;
    event_time_id time_ids[ILOOP_NUMBER_OF_TIME_CB];
} iloop_event_data;

static inline 
void iloop_call_io_cb(iloop_cb_type type, int fd, void* data)
{
    iloop* loop = iloop_from_io(type, data);
    loop->io_cbs[type](loop, fd, data);
}

static void iloop_accept_cb(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    iloop_call_io_cb(ILOOP_ACCEPT_CB, fd, data);
}

static void iloop_read_cb(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    iloop_call_io_cb(ILOOP_READ_CB, fd, data);
}

static void iloop_write_cb(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    iloop_call_io_cb(ILOOP_WRITE_CB, fd, data);
}

static void iloop_worker_cb(event_loop* loop, int fd, void* data)
{
    hhunused(loop);
    iloop_call_io_cb(ILOOP_WORKER_CB, fd, data);
}

static event_io_callback* const g_iloop_event_io_cbs[ILOOP_NUMBER_OF_IO_CB] =
{
    iloop_accept_cb, /* ILOOP_ACCEPT_CB */
    iloop_read_cb, /* ILOOP_READ_CB */
    iloop_write_cb, /* ILOOP_WRITE_CB */
    iloop_worker_cb /* ILOOP_WORKER_CB */
};

static
iloop_result iloop_add_io(iloop* loop, int fd, int mask, iloop_cb_type type,
                          void* data)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_loop* eloop = iloop_data->eloop;
    event_result r;
    int emask = 0;
    if (mask & ILOOP_READABLE) emask |= EVENT_READABLE;
    if (mask & ILOOP_WRITEABLE) emask |= EVENT_WRITEABLE;

    r = event_add_io_event(eloop,fd,emask,g_iloop_event_io_cbs[type],data);

    return (r == EVENT_RESULT_SUCCESS) ? ILOOP_SUCCESS : ILOOP_FAILURE;
}

static void iloop_stop_loop(iloop* loop)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_loop* eloop = iloop_data->eloop;
    event_stop_loop(eloop);
}

static void iloop_delete_io(iloop* loop, int fd, int mask)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_loop* eloop = iloop_data->eloop;
    int emask = 0;
    if (mask & ILOOP_READABLE) emask |= EVENT_READABLE;
    if (mask & ILOOP_WRITEABLE) emask |= EVENT_WRITEABLE;

    event_delete_io_event(eloop, fd, emask); 
}

static inline
void iloop_call_time_cb(iloop_time_cb_type type, void* data)
{
    iloop* loop = iloop_from_time(type, data);
    loop->time_cbs[type](loop, type, data);
}

static void iloop_watchdog_cb(event_loop* loop, event_time_id id, void* data)
{
    hhunused(loop);
    hhunused(id);
    iloop_call_time_cb(ILOOP_WATCHDOG_CB, data);
}

static void iloop_heartbeat_cb(event_loop* loop, event_time_id id, void* data)
{
    hhunused(loop);
    hhunused(id);
    iloop_call_time_cb(ILOOP_HEARTBEAT_CB, data);
}

static
void iloop_heartbeat_expire_cb(event_loop* loop, event_time_id id, void* data)
{
    hhunused(loop);
    hhunused(id);
    iloop_call_time_cb(ILOOP_HEARTBEAT_EXPIRE_CB, data);
}

static void
iloop_handshake_timeout_cb(event_loop* loop, event_time_id id, void* data)
{
    hhunused(loop);
    hhunused(id);
    iloop_call_time_cb(ILOOP_HANDSHAKE_TIMEOUT_CB, data);
}

static
event_time_callback* const g_iloop_event_time_cbs[ILOOP_NUMBER_OF_TIME_CB] =
{
    iloop_watchdog_cb, /* ILOOP_WATCHDOG_CB */
    iloop_heartbeat_cb, /* ILOOP_HEARTBEAT_CB */
    iloop_heartbeat_expire_cb, /* ILOOP_HEARTBEAT_EXPIRE_CB */
    iloop_handshake_timeout_cb /* ILOOP_HANDSHAKE_TIMEOUT_CB */
};

static void iloop_add_time(iloop* loop, iloop_time_cb_type type,
                           uint64_t frequency_ms, uint64_t initial_delay_ms,
                           void* data)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_time_id id;
    id = event_add_time_event_with_delay(iloop_data->eloop,
                                         g_iloop_event_time_cbs[type],
                                         frequency_ms,
                                         initial_delay_ms,
                                         data);
    iloop_data->time_ids[type] = id;
}

static void iloop_delete_time(iloop* loop, iloop_time_cb_type type)
{
    iloop_event_data* iloop_data = loop->userdata;
    if (iloop_data->time_ids[type] != EVENT_INVALID_TIME_ID)
    {
        event_delete_time_event(iloop_data->eloop, iloop_data->time_ids[type]);
        iloop_data->time_ids[type] = EVENT_INVALID_TIME_ID;
    }
}

static void iloop_cleanup(iloop* loop)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_destroy_loop(iloop_data->eloop);
    hhfree(iloop_data);
    loop->userdata = NULL;
}

static void iloop_listen(iloop* loop)
{
    iloop_event_data* iloop_data = loop->userdata;
    event_pump_events(iloop_data->eloop, 0);
}

static bool iloop_event_attach_internal(iloop* loop,
        config_server_options* options)
{
    iloop_event_data* iloop_data = hhmalloc(sizeof(*iloop_data));    
    if (iloop_data == NULL) return false;

    iloop_data->eloop = event_create_loop(options->max_clients + 1024);
    for (unsigned i = 0; i < hhcountof(iloop_data->time_ids); i++)
    {
        iloop_data->time_ids[i] = EVENT_INVALID_TIME_ID;
    }

    loop->userdata = iloop_data;
    loop->add_io = iloop_add_io;
    loop->delete_io = iloop_delete_io;
    loop->add_time = iloop_add_time;
    loop->delete_time = iloop_delete_time;
    loop->cleanup = iloop_cleanup;
    loop->listen = iloop_listen;
    loop->stop = iloop_stop_loop;

    return true;
}

#endif /* EVENT_IFACE_H__ */

