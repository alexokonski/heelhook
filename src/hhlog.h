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

#ifndef __HHLOG_H_
#define __HHLOG_H_

#include "util.h"

typedef enum
{
    HHLOG_LEVEL_DEBUG,
    HHLOG_LEVEL_INFO,
    HHLOG_LEVEL_NOTICE,
    HHLOG_LEVEL_WARNING,
    HHLOG_LEVEL_ERROR
} hhlog_level;

typedef struct
{
    hhlog_level loglevel; /* the current log level */
    char* syslogident; /* syslog ident to log to... don't use syslog if NULL */
    char* logfilepath; /* file path to log to... don't use a file if NULL */
    bool log_to_stdout; /* log to stdout if true */
    bool log_location; /* log filename/line no. if true */
} hhlog_options;

/* stores a pointer to options */
void hhlog_set_options(hhlog_options* options);

/* 
 * DO NOT CALL THIS DIRECTLY, USE hhlog MACRO
 *
 * logs the message format with options used with hhlog_set_options 
 * will default to INFO, no syslog, stdout if you never call hhlog_set_options
 */
void hhlog_log__(hhlog_level level, const char* filename, int line, ...);

/*
 * This is a macro so filename and line number can work, first vararg is a
 * format string.
 */
#define hhlog(level, ...)\
    hhlog_log__(level, __FILE__, __LINE__, __VA_ARGS__);

#endif /* __HHLOG_H_ */

