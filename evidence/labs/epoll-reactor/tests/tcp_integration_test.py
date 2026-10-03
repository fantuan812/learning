#!/usr/bin/env python3
"""Real loopback TCP checks. Synchronize on READY/PAUSE, never sleep for readiness.

Socket deadlines, condition deadlines, worker joins and a whole-process watchdog
make failures nonzero and bounded. This is correctness coverage, not a benchmark.
"""

import argparse
import concurrent.futures
import contextlib
import re
import signal
import socket
import struct
import subprocess
import threading
import time


TIMEOUT = 12.0
PASSED = 0


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def test(name, operation):
    global PASSED
    operation()
    PASSED += 1
    print(f"PASS integration {name}", flush=True)


def payload(size):
    return (bytes(range(256)) * ((size + 255) // 256))[:size]


def receive_eof(peer):
    result = bytearray()
    deadline = time.monotonic() + TIMEOUT
    while True:
        remaining = deadline - time.monotonic()
        require(remaining > 0, "receive total deadline exceeded")
        peer.settimeout(remaining)
        data = peer.recv(4096)
        if not data:
            return bytes(result)
        result.extend(data)


class Server:
    def __init__(self, executable, mode, port):
        self.mode = mode
        self.lines = []
        self.condition = threading.Condition()
        self.process = subprocess.Popen(
            [executable, str(port), mode], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
        )
        self.reader = threading.Thread(target=self._collect, daemon=True)
        self.reader.start()
        try:
            line = self.wait_line("READY ", 0)
            match = re.fullmatch(
                r"READY address=127\.0\.0\.1 port=(\d+) mode=(lt|et) "
                r"queue_limit=(\d+) max_connections=(\d+)", line)
            require(match is not None, f"invalid READY: {line}")
            self.port = int(match[1])
            self.limit = int(match[3])
            require(0 < self.port <= 65535 and match[2] == mode, "bad selected port/mode")
            require(self.limit == 256 * 1024 and int(match[4]) == 64, "unexpected resource bounds")
            if port:
                require(self.port == port, "requested fixed port not used")
        except BaseException:
            self.stop()
            raise

    def _collect(self):
        try:
            for line in self.process.stdout:
                with self.condition:
                    self.lines.append(line.rstrip("\n"))
                    self.condition.notify_all()
        finally:
            with self.condition:
                self.condition.notify_all()

    def mark(self):
        with self.condition:
            return len(self.lines)

    def wait_line(self, prefix, after):
        deadline = time.monotonic() + TIMEOUT
        with self.condition:
            while True:
                for line in self.lines[after:]:
                    if line.startswith(prefix):
                        return line
                if self.process.poll() is not None:
                    raise AssertionError(f"server exited {self.process.returncode}: {self.lines}")
                remaining = deadline - time.monotonic()
                require(remaining > 0, f"timeout waiting for {prefix}: {self.lines}")
                self.condition.wait(remaining)

    @contextlib.contextmanager
    def connect(self, small_receive=False):
        peer = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            peer.settimeout(TIMEOUT)
            if small_receive:
                peer.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            peer.connect(("127.0.0.1", self.port))
            yield peer
        finally:
            peer.close()

    def probe(self, data=b"still-alive\x00\xff"):
        require(self.process.poll() is None, "server died before probe")
        with self.connect() as peer:
            peer.sendall(data)
            peer.shutdown(socket.SHUT_WR)
            require(receive_eof(peer) == data, "survivor probe mismatch")
        require(self.process.poll() is None, "server died after probe")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.reader.join(timeout=3)
        if not self.reader.is_alive():
            self.process.stdout.close()
        return self.process.returncode

    def show_log(self):
        print(f"--- server {self.mode} raw output ---", flush=True)
        for line in self.lines:
            print(line, flush=True)
        print(f"--- server {self.mode} exit={self.process.returncode} ---", flush=True)

    def validate_stop(self):
        require(self.process.returncode == 0, f"server shutdown exit {self.process.returncode}")
        require(not self.reader.is_alive(), "server log reader failed to finish")
        summaries = [line for line in self.lines if line.startswith("STATS ")]
        require(len(summaries) == 1, "missing unique server STATS")
        fields = dict(re.findall(r"(\w+)=(\d+)", summaries[0]))
        require(int(fields["read_pauses"]) > 0, "backpressure pause was not observed")
        require(int(fields["write_eagain"]) > 0, "TCP write EAGAIN was not observed")
        require(int(fields["max_pending"]) == self.limit, "queue cap was not exercised")
        require(int(fields["closed"]) <= int(fields["accepted"]), "invalid connection counters")


def binary_multichunk(server):
    data = payload(192 * 1024 + 137)
    with server.connect() as peer:
        sizes = [1, 17, 4095, 4096, 8193, 313]
        sent = 0
        index = 0
        while sent < len(data):
            size = sizes[index % len(sizes)]
            peer.sendall(data[sent:sent + size])
            sent += min(size, len(data) - sent)
            index += 1
        peer.shutdown(socket.SHUT_WR)
        require(receive_eof(peer) == data, "multichunk binary echo mismatch")


def empty_half_close(server):
    with server.connect() as peer:
        peer.shutdown(socket.SHUT_WR)
        require(receive_eof(peer) == b"", "empty half-close returned bytes")


def half_close_pending(server):
    # Fits the client send buffer but exceeds the server's bounded user queue.
    data = payload(320 * 1024)
    mark = server.mark()
    with server.connect(small_receive=True) as peer:
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024)
        peer.sendall(data)
        peer.shutdown(socket.SHUT_WR)
        server.wait_line("PAUSE ", mark)
        require(receive_eof(peer) == data, "EOF discarded pending output")


def slow_reader(server):
    data = payload(2 * 1024 * 1024 + 71)
    mark = server.mark()
    errors = []
    with server.connect(small_receive=True) as peer:
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 16384)

        def send():
            try:
                peer.sendall(data)
                peer.shutdown(socket.SHUT_WR)
            except BaseException as error:
                errors.append(error)

        writer = threading.Thread(target=send, daemon=True)
        writer.start()
        try:
            # Deliberately read nothing until the server proves its queue reached
            # the high-water mark. No wall-clock sleep chooses when to resume.
            server.wait_line("PAUSE ", mark)
            server.probe(b"other-peer-while-paused")
            require(receive_eof(peer) == data, "slow-reader resume lost bytes")
        finally:
            if writer.is_alive():
                # Closing/shutting down the socket unblocks a failed sender.
                try:
                    peer.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            writer.join(timeout=3)
        require(not writer.is_alive(), "sender did not exit")
        require(not errors, f"sender errors: {errors}")


def reset_then_survive(server):
    for _ in range(8):
        with server.connect() as peer:
            peer.sendall(payload(32768))
            peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    server.probe(b"after-eight-resets")


def concurrent_echo(server):
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(server.probe, bytes([index]) + payload(65536 + index))
                   for index in range(4)]
        for future in futures:
            future.result(timeout=TIMEOUT)


def invalid_cli(executable, args):
    result = subprocess.run([executable] + args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=3)
    require(result.returncode == 2, f"bad CLI accepted: {args}, rc={result.returncode}")
    require("usage:" in result.stdout or "invalid port or mode" in result.stdout,
            "CLI error diagnostic missing")
    print(f"CLI args={args!r} exit={result.returncode} output={result.stdout.strip()!r}", flush=True)


def watchdog(_signum, _frame):
    raise TimeoutError("whole integration suite watchdog expired")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("server")
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    require(0 <= args.port <= 65535, "invalid integration port")
    signal.signal(signal.SIGALRM, watchdog)
    signal.alarm(110)
    for index, bad in enumerate(([], ["abc", "lt"], ["-1", "lt"],
                                 ["65536", "et"], ["0", "bad"], ["0", "lt", "extra"]), 1):
        test(f"invalid_cli_{index}", lambda bad=bad: invalid_cli(args.server, bad))
    cases = (
        ("binary_multichunk", binary_multichunk),
        ("empty_half_close", empty_half_close),
        ("half_close_pending_output", half_close_pending),
        ("slow_reader_pause_resume_other_peer", slow_reader),
        ("reset_disconnect_then_survive", reset_then_survive),
        ("concurrent_echo", concurrent_echo),
    )
    for mode in ("lt", "et"):
        server = Server(args.server, mode, args.port)
        try:
            for name, operation in cases:
                test(f"{mode}_{name}", lambda operation=operation: operation(server))
        finally:
            server.stop()
            server.show_log()
        test(f"{mode}_shutdown_and_bounded_stats", server.validate_stop)
    signal.alarm(0)
    print(f"SUMMARY integration passed={PASSED} failed=0", flush=True)


if __name__ == "__main__":
    try:
        main()
    except BaseException as error:
        print(f"FAIL integration after={PASSED}: {type(error).__name__}: {error}", flush=True)
        raise
