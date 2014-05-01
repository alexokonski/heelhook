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

Should compile on any recent linux, but only tested on Ubuntu 12.04.

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

