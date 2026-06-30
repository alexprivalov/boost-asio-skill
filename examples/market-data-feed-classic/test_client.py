#!/usr/bin/env python3
"""Integration test client for the framed market-data feed server.

Frame: [4-byte BE length N][1-byte type][N-1 byte payload].
Verifies: full-duplex push (TICK arrives after SUBSCRIBE while we keep the
socket open), PING/PONG, and UNSUBSCRIBE stopping the push.
"""
import socket, struct, sys, time

import os
HOST = "127.0.0.1"
PORT = int(os.environ.get("FEED_PORT", "9090"))
SUBSCRIBE, UNSUBSCRIBE, PING = 0x01, 0x02, 0x03
TICK, PONG, ERROR = 0x10, 0x11, 0x12

def frame(t, payload=b""):
    body = bytes([t]) + payload
    return struct.pack(">I", len(body)) + body

def read_frame(sock, timeout=5.0):
    sock.settimeout(timeout)
    hdr = recvn(sock, 4)
    (n,) = struct.unpack(">I", hdr)
    body = recvn(sock, n)
    return body[0], body[1:]

def recvn(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf

def main():
    s = socket.create_connection((HOST, PORT))
    ok = True

    # 1. PING -> PONG
    s.sendall(frame(PING))
    t, _ = read_frame(s)
    assert t == PONG, "expected PONG got {:#x}".format(t)
    print("PASS ping/pong")

    # 2. SUBSCRIBE -> server pushes TICKs full-duplex
    s.sendall(frame(SUBSCRIBE, b"AAPL"))
    ticks = []
    deadline = time.time() + 2.0
    while time.time() < deadline and len(ticks) < 3:
        t, p = read_frame(s)
        if t == TICK:
            ticks.append(p.decode())
    assert len(ticks) >= 3, "expected >=3 ticks, got {}".format(ticks)
    assert all(p.startswith("AAPL:") for p in ticks), ticks
    print("PASS full-duplex push: {}".format(ticks))

    # 3. PING while subscribed still answered (interleaved with pushes)
    s.sendall(frame(PING))
    got_pong = False
    deadline = time.time() + 2.0
    while time.time() < deadline:
        t, _ = read_frame(s)
        if t == PONG:
            got_pong = True
            break
    assert got_pong, "no PONG while subscribed"
    print("PASS interleaved ping/pong during push")

    # 4. UNSUBSCRIBE -> ticks stop
    s.sendall(frame(UNSUBSCRIBE, b"AAPL"))
    time.sleep(0.6)  # let in-flight ticks drain
    try:
        while True:
            read_frame(s, timeout=0.3)
    except socket.timeout:
        pass
    # after draining, confirm no new ticks for ~1s
    quiet = True
    try:
        t, _ = read_frame(s, timeout=1.0)
        if t == TICK:
            quiet = False
    except socket.timeout:
        pass
    assert quiet, "ticks still arriving after UNSUBSCRIBE"
    print("PASS unsubscribe stops push")

    s.close()
    print("ALL TESTS PASSED")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
