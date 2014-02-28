/* hhlog - logging mechanism
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

#include "hhlog.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>

#define ENDPOINT_MAX_LOG_LENGTH 2048

static hhlog_options g_default_options =
{
    .loglevel = HHLOG_LEVEL_INFO,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = HHTRUE
};

static hhlog_options* g_current_options = &g_default_options;

/* stores a pointer to options */
void hhlog_set_options(hhlog_options* options)
{
    g_current_options = (options != NULL) ? options : &g_default_options;
    if (g_current_options->syslogident != NULL)
    {
        openlog(g_current_options->syslogident, LOG_PID, LOG_USER);
    }
}

/*
 * logs the message format with options used with hhlog_set_options 
 * will default to INFO, no syslog, stdout if you never call hhlog_set_options
 */
void hhlog_log(hhlog_level level, const char* format, ...)
{
    if (level < g_current_options->loglevel)
    {
        return;
    }

    FILE* fp = NULL;
    if (g_current_options->logfilepath != NULL)
    {
        fp = fopen(g_current_options->logfilepath, "a");
    }

    char buffer[ENDPOINT_MAX_LOG_LENGTH];

    struct timeval tv;
    char time_buffer[64];
    gettimeofday(&tv, NULL);
    int num_written = strftime(time_buffer, sizeof(time_buffer),
        "%d %b %H:%M:%S.",
        localtime(&tv.tv_sec)
    );
    num_written += snprintf(&time_buffer[num_written], sizeof(time_buffer) - num_written,
        "%03d",
        (int)tv.tv_usec / 1000
    );

    char* level_str = NULL;
    int syslog_level = HHLOG_LEVEL_INFO;
    switch(level)
    {
    case HHLOG_LEVEL_DEBUG:
        level_str = "D";
        syslog_level = LOG_DEBUG;
        break;
    case HHLOG_LEVEL_INFO:
        level_str = "I";
        syslog_level = LOG_INFO;
        break;
    case HHLOG_LEVEL_NOTICE:
        level_str = "N";
        syslog_level = LOG_NOTICE;
        break;
    case HHLOG_LEVEL_WARNING:
        level_str = "W";
        syslog_level = LOG_WARNING;
        break;
    case HHLOG_LEVEL_ERROR:
        level_str = "E";
        syslog_level = LOG_ERR;
        break;
    }
    num_written = snprintf(buffer, sizeof(buffer),
        "%s %s ",
        time_buffer,
        level_str
    );

    va_list list;
    va_start(list, format);
    num_written += vsnprintf(&buffer[num_written], (int)sizeof(buffer) - num_written,
        format,
        list
    );
    va_end(list);

    if (fp != NULL)
    {
        fprintf(fp, "%s\n", buffer);
    }

    if (g_current_options->syslogident != NULL)
    {
        syslog(syslog_level | LOG_USER, "%s\n", buffer);
    }

    if (g_current_options->log_to_stdout)
    {
        printf("%s\n", buffer);
    }

    if (fp != NULL)
    {
        fflush(fp);
        fclose(fp);
    }
}

