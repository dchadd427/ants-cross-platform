#!/usr/bin/env python3
"""A TCP proxy for the end-to-end tests of the dedicated server: it forwards every connection to the server, and on a signal it cuts them all.

usage: flaky_proxy.py LISTEN_PORT TARGET_PORT [REFUSE_SECONDS]

  Listens on 127.0.0.1:LISTEN_PORT and forwards each connection to 127.0.0.1:TARGET_PORT, byte for byte, both ways.
  SIGUSR1: every connection that is open is dropped at once (both ends see the connection closed, as when a cable is pulled), and for REFUSE_SECONDS (default 5)
           a new connection is accepted and closed at once (the network is down); after that the proxy forwards again.
  SIGTERM: stops.

It prints "listening PORT" when it is ready and "dropped N" for every cut, one line each, flushed. It exists for tests/scripts/test_ants_server.sh: a native game client
connects through it, the script cuts the link, and the server's status shows the room pause for the player that was lost.
"""
import os
import signal
import socket
import sys
import threading
import time


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    listen_port = int(sys.argv[1])
    target_port = int(sys.argv[2])
    refuse_seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0

    lock = threading.RLock()                  # (the handler of a signal runs in the main thread, maybe inside one of its own `with lock` blocks)
    open_sockets = set()
    state = {"refuse_until": 0.0, "stop": False}

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", listen_port))
    listener.listen(16)
    listener.settimeout(0.2)

    def close_quietly(s):
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            s.close()
        except OSError:
            pass

    def cut(_signum=None, _frame=None):
        with lock:
            victims = list(open_sockets)
            open_sockets.clear()
            state["refuse_until"] = time.time() + refuse_seconds
        for s in victims:
            close_quietly(s)
        print("dropped %d" % (len(victims) // 2), flush=True)

    def stop(_signum=None, _frame=None):
        state["stop"] = True

    signal.signal(signal.SIGUSR1, cut)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    def pump(src, dst):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        finally:
            close_quietly(src)
            close_quietly(dst)
            with lock:
                open_sockets.discard(src)
                open_sockets.discard(dst)

    print("listening %d" % listen_port, flush=True)
    while not state["stop"]:
        try:
            client, _ = listener.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        with lock:
            refusing = time.time() < state["refuse_until"]
        if refusing:
            close_quietly(client)
            continue
        try:
            upstream = socket.create_connection(("127.0.0.1", target_port), timeout=5)
        except OSError:
            close_quietly(client)
            continue
        client.settimeout(None)
        upstream.settimeout(None)
        with lock:
            open_sockets.add(client)
            open_sockets.add(upstream)
        threading.Thread(target=pump, args=(client, upstream), daemon=True).start()
        threading.Thread(target=pump, args=(upstream, client), daemon=True).start()
    cut()
    listener.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
