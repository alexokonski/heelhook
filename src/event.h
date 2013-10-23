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

#ifndef __EVENT_H__
#define __EVENT_H__

/* flags for mask in add_io_event */
#define EVENT_NONE 0
#define EVENT_READABLE 1
#define EVENT_WRITEABLE 2

/* flags for event_pump_events */
#define EVENT_DONT_BLOCK 1

typedef enum
{
    EVENT_RESULT_SUCCESS,
    EVENT_RESULT_EVENT_LOOP_FULL,
    EVENT_RESULT_OUT_OF_MEMORY,
    EVENT_RESULT_PLATFORM_ERROR,
} event_result;

typedef struct event_loop event_loop;

typedef void (event_io_callback)(struct event_loop* loop, int fd, void* data);

event_loop*     event_create_loop(int loop_size);
void            event_stop_loop(event_loop* loop);
event_result    event_add_io_event(event_loop* loop, int fd, int mask,
                                   event_io_callback* callback, void* data);
void            event_delete_io_event(event_loop* loop, int fd, int mask);
void            event_destroy_loop(event_loop* loop);

/* This function blocks until an event calls event_stop_loop */
void            event_pump_events(event_loop* loop, int flags);

#endif /* __EVENT_H__ */
