TODO
========
* Clean up protocol inteface - handshake buffer vs read\_buffer is ugly. It's
  gross that endpoint has to know which buffer to read into.  Maybe have
  protocol return it based on the current state?
* Test lots of concurrent clients
* Implement wss/TLS with OpenSSL
* More options to API/build actual things 
* Implement proper config parsing

