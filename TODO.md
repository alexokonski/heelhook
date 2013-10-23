TODO
========

* Make on\_close and on\_open actually work properly
* Slice output buffer after some length sent rather than waiting for it to
  run out completely... this will avoid massive memory use when continuously
  sending messages
* Test server\_stop
* Add definition of subprotocols to users of server.h
* Test lots of concurrent clients
* Implement wss/TLS with OpenSSL
* More options to API/build actual things 

