PyDoc_STRVAR(heelhook_set_opts__doc__,
"set_opts(log_to_stdout=False,loglevel=LogLevel.INFO)\n"
);

PyDoc_STRVAR(Server__doc__,
"Server(bindaddr=\"0.0.0.0\",port=9001,connection_class=heelhook.ServerConn,\n"
"       max_clients=1024,heartbeat_interval_ms=0,\n"
"       heartbeat_ttl_ms=0,handshake_timeout_ms=0,init_buffer_len=4096,\n"
"       write_max_frame_size=-1,read_max_msg_size=1*1024*1024,\n"
"       read_max_num_frames=-1,max_handshake_size=-1)\n\n"
"Constructs a server object with the settings specified:\n"
"    bindaddr - string specifying IP address to bind the main server socket\n"
"               (TODO: do hostname resolution here)\n"
"    port - port to bind the main server socket\n"
"    connection_class - subclass of ServerConn (NOT an instance) for handling\n"
"    events, and sending messages\n"
"    max_clients - maximum number of clients the server will allow to be\n"
"                  concurrently connected\n"
"    heartbeat_interval_ms - how often to send heartbeat pings. 0 for never\n"
"    heartbeat_ttl_ms - how long to wait for a reply to a heartbeat ping. If\n"
"                       set to 0, heartbeats will be pongs instead of pings and\n"
"                       won't be checked for acks\n"
"    handshake_timeout_ms - if a handshake takes longer than this, the\n"
"                           connection will be closed (slowloris mitigation).\n"
"                           0 for none.\n"
"    init_buffer_len - initial length for read/write buffers\n"
"    write_max_frame_size - written messages will be broken up into frames of\n"
"                           this size. -1 for no limit\n"
"    read_max_msg_size - maximum size of a message a client is allowed to send\n"
"    read_max_num_frames - maximum number of frames a client is allowed to\n"
"                          send in a single message. -1 for no limit\n"
"    max_handshake_size - if the handshake is larger than this, the connection\n"
"                         will be closed");

PyDoc_STRVAR(Server_listen__doc__,
"listen(self)\n\n"
"Block until Server.stop() is called and listen for client connections\n\n"
"NOTE: During the call, installs signal handlers for SIGTERM and SIGINT that\n"
"will stop the server when they are handled. Without this, python's default\n"
"handlers would not be invoked reliably");

PyDoc_STRVAR(Server_stop__doc__,
"stop(self)\n\n"
"Close all client connections and return from Server.listen()");

PyDoc_STRVAR(ServerConn__doc__,
"Represents a connection to a single client. Subtype this to define your app\n"
"and pass that as the 'connection_class' to the Server constructor.\n"
"It is INVALID to instantiate one of these yourself; leave that to the Server\n"
"object. There will be a 'server' member available for the lifetime of this\n"
"class that is a reference to the Server object that created it.");

PyDoc_STRVAR(ServerConn_on_connect__doc__,
"on_connect(self)\n\n"
"Called when a client has sent their side of the handshake, but the server has\n"
"not yet responded\n\n"
"Depending on what you want to do, you can return the following from this\n"
"function:\n"
"    - False if you want to reject this client.\n"
"    - True or None if you accept this client, but don't want to specify any\n"
"      subprotocol or extensions\n"
"    - a tuple of (subprotocol, extensions) where subprotocol is a string\n"
"      and extensions is a list of strings. The server's response will include\n"
"      the subprotocol and extensions. Either of these can be None if you don't\n"
"      wish to specify any to the client");

PyDoc_STRVAR(ServerConn_on_open__doc__,
"on_open(self)\n\n"
"Called when the handshake is complete and it's okay to send messages");

PyDoc_STRVAR(ServerConn_on_message__doc__,
"on_message(self, msg, is_text)\n\n"
"Called when a message is received from a client");

PyDoc_STRVAR(ServerConn_on_ping__doc__,
"on_ping(self, msg)\n\n"
"Called when a ping message is received from a client");

PyDoc_STRVAR(ServerConn_on_pong__doc__,
"on_pong(self, msg)\n\n"
"Called when a pong message is received from a client");

PyDoc_STRVAR(ServerConn_on_close__doc__,
"on_close(self, code, reason)\n\n"
"Called whenever the server is closing the connection for any reason. Includes\n"
"the close code and reason received from the client (if any). If none were\n"
"received, code and reason will be None");

PyDoc_STRVAR(ServerConn_send__doc__,
"send(self, msg, is_text=True)\n\n"
"Send a message to this client");

PyDoc_STRVAR(ServerConn_send_ping__doc__,
"send_ping(self, msg)\n\n"
"Send a ping message to this client");

PyDoc_STRVAR(ServerConn_send_pong__doc__,
"send_pong(self, msg)\n\n"
"Send a pong message to this client");

PyDoc_STRVAR(ServerConn_send_close__doc__,
"send_close(self, code=-1, reason='')\n\n"
"Send a close message to this client. Will cause on_close() to be called\n"
"eventually");

PyDoc_STRVAR(ServerConn_get_sub_protocols__doc__,
"get_sub_protocols(self)\n\n"
"Get the list of subprotocols the client says they support");

PyDoc_STRVAR(ServerConn_get_extensions__doc__,
"get_extensions(self)\n\n"
"Get the list of extensions the client says they support");

PyDoc_STRVAR(ServerConn_get_headers__doc__,
"get_headers(self)\n\n"
"Get all the headers sent by the client. Returns a dictionary that looks like:\n"
"{\n"
"   \"Host\": [\"server.example.com\"],\n"
"   \"Sec-WebSocket-Protocol\": [\"chat\", \"otherchat\"]\n"
"   ...\n"
"}");

PyDoc_STRVAR(ServerConn_get_resource__doc__,
"get_resource(self)\n\n"
"Get the resource requested by the client.");

