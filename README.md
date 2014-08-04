heelhook
========
A single process, single-threaded, event-driven WebSocket server written in C
with 0 external dependencies (except for libc).

As of the time of this writing, passes all autobahn testsuite tests 1-10
(Autobahn 0.8.6, AutobahnTestSuite 0.6.1). To build heelhook and run autobahn
tests yourself (assumes test suite is already installed):

    git clone https://github.com/alexokonski/heelhook.git
    cd heelook/src
    make
    ./echoserver 9001

In another terminal, assuming your fuzzingclient.json is setup accordingly:

    wstest -m fuzzingclient -s fuzzingclient.json

Should compile on any recent linux, but only tested on 64-bit Ubuntu 12.04
and 14.04

Python Extension
================
To build the python extension and run the example echoserver:

    git clone https://github.com/alexokonski/heelhook.git
    cd heelook/python
    python setup.py build
    PYTHONPATH=./build/lib.linux-x86_64-2.7/ python examples/echoserver.py 9001

There are docstrings available for everything, for ex. by doing the standard:

    >>> import heelhook
    >>> help(heelhook)

Platforms
=========
In theory, heelhook should build on any POSIX conforming system with standard
BSD socket functions. However, in practice, the only known supported platforms
are:

    Ubuntu 14.04 LTS
    Ubuntu 12.04 LTS

Compilers tested are gcc and clang.

Notes
=====
* Currently does not support secure websockets (wss)

References
==========
AutoBahnTestSuite: http://autobahn.ws/testsuite/  
WebSocket RFC: http://tools.ietf.org/html/rfc6455  
libb64: http://sourceforge.net/projects/libb64  
SHA-1 RFC: http://tools.ietf.org/html/rfc3174  
cJSON (used only for client example): http://sourceforge.net/projects/cjson/

License
=======
3-clause BSD

