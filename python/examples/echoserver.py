import sys
import heelhook
from heelhook import Server, ServerConn, LogLevel

heelhook.set_opts(log_to_stdout=True,loglevel=LogLevel.DEBUG_4)

class EchoConnection(ServerConn):
    def on_connect(self):
        self.num = self.server.num_clients
        self.server.num_clients += 1

        print self.num, "Client connected. Resource:", self.get_resource()
        print self.num, "Got subprotocols:", self.get_sub_protocols()
        print self.num, "All headers:", self.get_headers()

    def on_message(self, msg, is_text):
        #print self.num, "Got message:", msg
        self.send(msg, is_text)

    def on_close(self, code, reason):
        print self.num, "Got close. code:", code, "reason:", reason

class EchoServer(Server):
    def __init__(self, *args, **kwargs):
        self.num_clients = 0
        super(EchoServer, self).__init__(*args, **kwargs)

if __name__ == "__main__":
    max_size = 20 * 1024 * 1024
    server = EchoServer(port=int(sys.argv[1]), connection_class=EchoConnection,
                    write_max_frame_size=max_size, read_max_msg_size=max_size,
                    read_max_num_frames=max_size)

    server.listen()
