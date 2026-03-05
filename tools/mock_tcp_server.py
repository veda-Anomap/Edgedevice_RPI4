#!/usr/bin/env python3
import argparse
import json
import socket
import struct
from typing import Tuple

ACK = 0x00
FAIL = 0x03
DEVICE = 0x04
AVAILABLE = 0x05

TYPE_NAME = {
    ACK: "ACK",
    FAIL: "FAIL",
    DEVICE: "DEVICE",
    AVAILABLE: "AVAILABLE",
}


def recv_exact(conn: socket.socket, size: int) -> bytes:
    out = b""
    while len(out) < size:
        chunk = conn.recv(size - len(out))
        if not chunk:
            raise ConnectionError("peer disconnected")
        out += chunk
    return out


def recv_packet(conn: socket.socket) -> Tuple[int, bytes]:
    header = recv_exact(conn, 5)
    msg_type = header[0]
    body_len = struct.unpack("!I", header[1:5])[0]
    body = recv_exact(conn, body_len) if body_len > 0 else b""
    return msg_type, body


def send_packet(conn: socket.socket, msg_type: int, body: bytes) -> None:
    header = bytes([msg_type]) + struct.pack("!I", len(body))
    conn.sendall(header + body)


def print_packet(prefix: str, msg_type: int, body: bytes) -> None:
    name = TYPE_NAME.get(msg_type, f"0x{msg_type:02X}")
    print(f"{prefix} type={name} (0x{msg_type:02X}), len={len(body)}")
    if body:
        try:
            print(f"{prefix} json={json.loads(body.decode('utf-8'))}")
        except Exception:
            print(f"{prefix} raw={body!r}")


def send_device(conn: socket.socket, cmd: str) -> None:
    body = json.dumps({"motor": cmd}, separators=(",", ":")).encode("utf-8")
    send_packet(conn, DEVICE, body)
    print_packet("TX", DEVICE, body)


def send_status_req(conn: socket.socket) -> None:
    send_packet(conn, AVAILABLE, b"")
    print_packet("TX", AVAILABLE, b"")


def interactive(conn: socket.socket) -> None:
    allowed_motor = {"w", "a", "s", "d", "auto", "manual", "on", "off"}
    print("Commands: w/a/s/d/auto/manual/on/off, status, recv, quit")
    while True:
        cmd = input("> ").strip()
        if cmd in {"quit", "q", "exit"}:
            return
        if cmd == "status":
            send_status_req(conn)
            continue
        if cmd == "recv":
            rx_type, rx_body = recv_packet(conn)
            print_packet("RX", rx_type, rx_body)
            continue
        if not cmd:
            continue
        if cmd in allowed_motor:
            send_device(conn, cmd)
            continue
        print("Invalid command. Use one of: w/a/s/d/auto/manual/on/off, status, recv, quit")


def run(args: argparse.Namespace) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"mock server listening on {args.host}:{args.port}")

        conn, addr = server.accept()
        with conn:
            print(f"connected from {addr[0]}:{addr[1]}")

            if args.auto:
                send_device(conn, "w")
                rx_type, rx_body = recv_packet(conn)
                print_packet("RX", rx_type, rx_body)

                send_status_req(conn)
                rx_type, rx_body = recv_packet(conn)
                print_packet("RX", rx_type, rx_body)
                return

            interactive(conn)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Mock TCP server for stm_bridge binary protocol")
    p.add_argument("--host", default="0.0.0.0", help="listen host (default: 0.0.0.0)")
    p.add_argument("--port", type=int, default=9000, help="listen port (default: 9000)")
    p.add_argument("--auto", action="store_true", help="send one DEVICE and one AVAILABLE request, then exit")
    return p.parse_args()


if __name__ == "__main__":
    run(parse_args())
