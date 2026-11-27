#!/usr/bin/env python3
"""Host-side responders for the wsperf guest suite.

  TCP :5001  echo     - read a block, send it back (drives 't e' duplex and 'r'/'s'/'p' modes)
  TCP :5002  sink     - read and discard (drives 't' TX and 'c' connect-rate)
  TCP :5003  source   - send until the peer closes (drives 'd' RX)
  UDP :5003  echo     - bounce each datagram back (drives 'u')

Pure stdlib, no dependencies. Runs until killed; tolerates any client behavior.
"""
import socket
import threading

CHUNK = 65536
RX_SECONDS = 30


def _echo_conn(c):
    try:
        while True:
            data = c.recv(CHUNK)
            if not data:
                return
            c.sendall(data)
    except OSError:
        pass
    finally:
        c.close()


def _sink_conn(c):
    try:
        while c.recv(CHUNK):
            pass
    except OSError:
        pass
    finally:
        c.close()


def _source_conn(c):
    try:
        c.sendall(b"\xa5" * CHUNK)
        end = threading.Event()
        end.wait(RX_SECONDS)
    except OSError:
        pass
    finally:
        c.close()


def _tcp_listener(port, handler):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    s.listen(64)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handler, args=(c,), daemon=True).start()


def _udp_echo(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", port))
    while True:
        data, peer = s.recvfrom(65535)
        s.sendto(data, peer)


def main():
    for port, handler in ((5001, _echo_conn), (5002, _sink_conn), (5003, _source_conn)):
        threading.Thread(target=_tcp_listener, args=(port, handler), daemon=True).start()
    threading.Thread(target=_udp_echo, args=(5003,), daemon=True).start()
    threading.Event().wait()


if __name__ == "__main__":
    main()
