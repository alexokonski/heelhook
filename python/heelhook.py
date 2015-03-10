"""
Copyright (c) 2013, Alex O'Konski
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of heelhook nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
"""


from _heelhook import *

class CloseCode(object):
    NORMAL                 = 1000
    GOING_AWAY             = 1001
    PROTOCOL               = 1002
    BAD_DATA_TYPE          = 1003
    BAD_DATA               = 1007
    POLICY_VIOLATION       = 1008
    LARGE_MESSAGE          = 1009
    CLIENT_NEEDS_EXTENSION = 1010
    UNEXPECTED_CONDITION   = 1011

class LogLevel(object):
    DEBUG_4 = 0
    DEBUG_3 = 1
    DEBUG_2 = 2
    DEBUG_1 = 3
    DEBUG_0 = 4
    INFO    = 5
    NOTICE  = 6
    WARNING = 7
    ERROR   = 8

