/* iloop - Loop intervace. Abstracts event loop operations, to aid in support
 * of third party event loops
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

#ifndef ILOOP_H__
#define ILOOP_H__

/* flags for mask in add_io_event_cb */
#define ILOOP_NONE 0
#define ILOOP_READABLE 1
#define ILOOP_WRITEABLE 2

typedef struct iloop iloop;

typedef enum
{
    ILOOP_SUCCESS,
    ILOOP_FAILURE
} iloop_result;

typedef enum
{
    ILOOP_ACCEPT_CB,
    ILOOP_READ_CB,
    ILOOP_WRITE_CB,
    ILOOP_NUMBER_OF_IO_CB
} iloop_cb_type;

typedef enum
{
    ILOOP_WATCHDOG_CB,
    ILOOP_HEARTBEAT_CB,
    ILOOP_HEARTBEAT_EXPIRE_CB,
    ILOOP_HANDSHAKE_TIMEOUT_CB,
    ILOOP_NUMBER_OF_TIME_CB
} iloop_time_cb_type;

typedef void (iloop_io_callback)(iloop* loop, int fd, void* data);

typedef void (iloop_time_callback)(iloop* loop, iloop_time_cb_type type,
                                   void* data);

typedef iloop_result (iloop_add_io_event_cb)(iloop* loop, int fd, int mask,
                                   iloop_cb_type type, void* data);

typedef void (iloop_stop_loop_cb)(iloop* loop);

typedef void (iloop_listen_cb)(iloop* loop);

typedef void (iloop_cleanup_cb)(iloop* loop);

typedef void (iloop_delete_io_event_cb)(iloop* loop, int fd, int mask);

typedef void
(iloop_add_time_event_with_delay_cb)(iloop* loop,
                                  iloop_time_cb_type type,
                                  uint64_t frequency_ms,
                                  uint64_t initial_delay_ms,
                                  void* data);

typedef void (iloop_delete_time_event_cb)(iloop* loop, iloop_time_cb_type type);

struct iloop
{
    /* array of size ILOOP_NUMBER_OF_IO_CB */
    iloop_io_callback** io_cbs;

    /* array of size ILOOP_NUMBER_OF_TIME_CB */
    iloop_time_callback** time_cbs;

    void* userdata;

    iloop_add_io_event_cb* add_io;
    iloop_delete_io_event_cb* delete_io;
    iloop_add_time_event_with_delay_cb* add_time;
    iloop_delete_time_event_cb* delete_time;
    iloop_cleanup_cb* cleanup; /* NULL is okay */
    iloop_listen_cb* listen; /* NULL is okay if you don't call server_listen */

    /*
     * set to null if you don't want the server to
     * tear down the event loop when it's done
     */
    iloop_stop_loop_cb* stop;
};

iloop* iloop_from_io(iloop_cb_type type, void *data);

iloop* iloop_from_time(iloop_time_cb_type type, void *data);

#endif /* ILOOP_H__ */

