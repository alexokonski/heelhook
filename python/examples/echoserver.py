import sys
from heelhook import Server, ServerConn

client_num = 0

class EchoConnection(ServerConn):
    def on_connect(self):
        global client_num
        self.num = client_num
        client_num += 1

        print self.num, "Client connected. Resource:", self.get_resource()
        print self.num, "Got subprotocols:", self.get_sub_protocols()
        print self.num, "All headers:", self.get_headers()

    def on_message(self, msg, is_text):
        print self.num, "Got message:", msg
        self.send(msg, is_text)

    def on_close(self, code, reason):
        print self.num, "Got close. code:", code, "reason:", reason

if __name__ == "__main__":
    max_size = 20 * 1024 * 1024
    server = Server(port=int(sys.argv[1]), connection_class=EchoConnection,
                    write_max_frame_size=max_size, read_max_msg_size=max_size,
                    read_max_num_frames=max_size)

    server.listen()
