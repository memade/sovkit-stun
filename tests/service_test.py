#!/usr/bin/env python3
"""Loopback-only integration tests. No SovKit SDK or third-party Python packages."""
import contextlib
import json
import os
from pathlib import Path
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

BINARY = str(Path(sys.argv.pop(1)).resolve())
MAGIC = 0x2112A442


def frame(attrs=b"", fingerprint=False, transaction=None):
    transaction = transaction or os.urandom(12)
    payload = struct.pack("!HHI", 1, len(attrs) + (8 if fingerprint else 0), MAGIC)
    payload += transaction + attrs
    if fingerprint:
        payload += struct.pack("!HHI", 0x8028, 4, zlib.crc32(payload) ^ 0x5354554E)
    return payload


def filtering(operation, cookie=bytes(16), transaction=None):
    attrs = struct.pack("!HHBBBB", 0xC0A1, 20, 1, operation, 0, 0) + cookie
    return frame(attrs + struct.pack("!HH", 0x8026, 8) + bytes(8), True, transaction)


def client():
    result = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    result.bind(("127.0.0.1", 0))
    result.settimeout(0.3)
    return result


def ports():
    with client() as first, client() as second:
        return [first.getsockname()[1], second.getsockname()[1]]


def readline(process, timeout=5):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(timeout):
            raise AssertionError("server did not report readiness")
    line = process.stdout.readline()
    if not line:
        raise AssertionError("server exited before readiness: " + process.stderr.read())
    return json.loads(line)


class Service:
    def __init__(self, extra=(), config=None):
        self.ports = ports()
        self.temp = tempfile.TemporaryDirectory(prefix="sovkit-stun-test-")
        args = ["--ports", ",".join(map(str, self.ports)), *extra]
        if config is not None:
            path = Path(self.temp.name) / "server.conf"
            path.write_text(f"port_a={self.ports[0]}\nport_b={self.ports[1]}\n" + config)
            args = ["--config", str(path)]
        self.process = subprocess.Popen([BINARY, *args], stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True,
                                        cwd=self.temp.name)
        try:
            self.ready = readline(self.process)
            if self.ready["event"] != "started":
                raise AssertionError(self.ready)
        except BaseException:
            self.close()
            raise
        self.final = None

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            out, err = self.process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()
            raise AssertionError("server did not stop within 5 seconds")
        finally:
            self.temp.cleanup()
        if self.process.returncode != 0 or err:
            raise AssertionError((self.process.returncode, err))
        records = [json.loads(row) for row in out.splitlines()]
        if records:
            self.final = records[-1]
        return self.final


@contextlib.contextmanager
def server(*args, **kwargs):
    service = Service(*args, **kwargs)
    try:
        yield service
    finally:
        service.close()


class ServiceTests(unittest.TestCase):
    def exchange(self, sock, service, packet, port=0):
        sock.sendto(packet, ("127.0.0.1", service.ports[port]))
        return sock.recvfrom(2048)

    def silent(self, sock, service, packet, port=0):
        sock.sendto(packet, ("127.0.0.1", service.ports[port]))
        with self.assertRaises(socket.timeout):
            sock.recvfrom(2048)

    def mapping(self, reply, request, sock):
        self.assertEqual(reply[:2], b"\x01\x01")
        self.assertEqual(reply[8:20], request[8:20])
        self.assertEqual(reply[20:26], b"\x00\x20\x00\x08\x00\x01")
        self.assertEqual(struct.unpack("!H", reply[26:28])[0] ^ (MAGIC >> 16),
                         sock.getsockname()[1])
        self.assertEqual(socket.inet_ntoa(struct.pack("!I", struct.unpack("!I", reply[28:32])[0] ^ MAGIC)),
                         sock.getsockname()[0])
        self.assertEqual(struct.unpack("!H", reply[2:4])[0], len(reply) - 20)

    def test_binding_two_ports_fingerprint_and_no_runtime_files(self):
        with server() as service, client() as sock:
            self.assertEqual(service.ready["bind_address"], "127.0.0.1")
            self.assertFalse(service.ready["filtering_diagnostics"])
            for i in range(4):
                packet = frame(fingerprint=bool(i % 2))
                reply, address = self.exchange(sock, service, packet, i % 2)
                self.assertEqual(address, ("127.0.0.1", service.ports[i % 2]))
                self.assertEqual(len(reply), 40 if i % 2 else 32)
                self.mapping(reply, packet, sock)
                if i % 2:
                    self.assertEqual(struct.unpack("!I", reply[-4:])[0],
                                     zlib.crc32(reply[:-8]) ^ 0x5354554E)
            self.assertEqual(list(Path(service.temp.name).iterdir()), [])
        self.assertEqual(service.final["bindings"], 4)
        self.assertEqual(service.final["sends_accepted"], 4)
        self.assertNotIn("observations", service.final)
        self.assertNotIn("transaction", service.final)

    def test_malformed_unsupported_and_oversized(self):
        bad_crc = bytearray(frame(fingerprint=True))
        bad_crc[-1] ^= 1
        malformed = [
            b"", b"x" * 19, b"\x00\x02" + frame()[2:],  # not Binding
            frame()[:4] + bytes(4) + frame()[8:],
            frame(struct.pack("!HH", 6, 0)),  # required USERNAME
            frame(struct.pack("!HH", 0x8001, 8)),  # truncated attribute
            bytes(bad_crc),
            frame(bytes(1080)),
            filtering(1),  # default-off extension
        ]
        with server() as service, client() as sock:
            for packet in malformed:
                self.silent(sock, service, packet)
            reply, _ = self.exchange(sock, service, frame())
            self.assertEqual(len(reply), 32)
        self.assertEqual(service.final["invalid_requests"], len(malformed))

    def test_unknown_optional_attribute_not_echoed(self):
        with server() as service, client() as sock:
            packet = frame(struct.pack("!HH", 0x8022, 5) + b"hello" + bytes(3))
            reply, _ = self.exchange(sock, service, packet)
            self.assertEqual(len(reply), 32)
            self.assertNotIn(b"hello", reply)

    def test_per_source_rate_limit_and_recovery(self):
        with server() as service, client() as sock:
            packet = frame()
            for _ in range(60):
                sock.sendto(packet, ("127.0.0.1", service.ports[0]))
            received = 0
            while True:
                try:
                    sock.recvfrom(2048)
                    received += 1
                except socket.timeout:
                    break
            self.assertGreater(received, 0)
            self.assertLessEqual(received, 10)  # burst 8, scheduler tolerance
            time.sleep(0.35)
            self.exchange(sock, service, frame())
        self.assertGreater(service.final["rate_limited"], 0)

    def test_filtering_source_port_binding_budget_and_privacy(self):
        with server(["--filtering-diagnostics"]) as service, client() as sock, client() as other:
            packet = filtering(1)
            reply, address = self.exchange(sock, service, packet)
            self.assertEqual(len(reply), 64)
            self.mapping(reply, packet, sock)
            cookie = reply[40:56]
            self.assertNotEqual(cookie, bytes(16))
            repeat, _ = self.exchange(sock, service, packet)
            self.assertEqual(repeat[40:56], cookie)
            self.silent(other, service, filtering(2, cookie))
            self.silent(sock, service, filtering(2, cookie), 1)
            self.silent(sock, service, filtering(2))
            for _ in range(8):
                time.sleep(0.27)
                request = filtering(2, cookie)
                response, address = self.exchange(sock, service, request)
                self.mapping(response, request, sock)
                self.assertEqual(address, ("127.0.0.1", service.ports[1]))
                self.assertEqual(response[40:56], cookie)
            time.sleep(0.27)
            self.silent(sock, service, filtering(2, cookie))
        self.assertNotIn(cookie.hex(), json.dumps(service.final))
        self.assertNotIn("transaction", json.dumps(service.final))

    def test_filtering_cookie_expiry(self):
        with server(["--filtering-diagnostics"]) as service, client() as sock:
            response, _ = self.exchange(sock, service, filtering(1))
            cookie = response[40:56]
            time.sleep(30.2)
            self.silent(sock, service, filtering(2, cookie))
            renewed, _ = self.exchange(sock, service, filtering(1))
            self.assertNotEqual(renewed[40:56], cookie)

    def test_config_duration_exit(self):
        with server(config="# loopback test\nbind_address=127.0.0.1\nduration_seconds=1\n"
                           "filtering_diagnostics=false\n") as service:
            service.process.wait(timeout=5)
        self.assertEqual(service.final["reason"], "duration-limit")

    def test_sigint_releases_both_ports(self):
        with server() as service:
            service.process.send_signal(signal.SIGINT)
            service.process.wait(timeout=5)
        for port in service.ports:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("127.0.0.1", port))
        self.assertEqual(service.final["reason"], "stop-requested")

    def test_bind_failure_rolls_back_first_port(self):
        first, _ = ports()
        with client() as occupied:
            second = occupied.getsockname()[1]
            if first == second:
                first, _ = ports()
            result = subprocess.run([BINARY, "--ports", f"{first},{second}"],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertIn("start failed", result.stderr)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("127.0.0.1", first))

    def test_bad_options_and_config(self):
        for args in [
            ["--bind", "::1"], ["--bind", "localhost"], ["--bind", "224.0.0.1"],
            ["--ports", "3478,3478"], ["--ports", "0,3479"],
            ["--ports", "65536,3479"], ["--ports", "3478,3479,3480"],
            ["--duration", "-1"], ["--duration", "86401"], ["--duration", "1x"],
            ["--bind"], ["--turn"], ["--config", "/nonexistent/stun.conf"],
            ["--duration", "1", "--duration", "2"],
            ["--filtering-diagnostics", "--filtering-diagnostics"],
            ["--config", "server.conf", "--duration", "1"],
        ]:
            with self.subTest(args=args):
                result = subprocess.run([BINARY, *args], capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn(b'"event":"started"', result.stdout)
        with tempfile.TemporaryDirectory(prefix="sovkit-stun-bad-config-") as directory:
            path = Path(directory) / "server.conf"
            for data in [b"enabled=true", b"diagnostics=true", b"port_a=1234\nport_a=2345",
                         b"bind_address=\n", b"port_a=+3478", b"duration_seconds=1\0",
                         b"x" * 16385, b"filtering_diagnostics=yes", b"missing-equals"]:
                path.write_bytes(data)
                result = subprocess.run([BINARY, "--config", str(path)], capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2, data[:80])

    def test_help_version(self):
        for option in ["--help", "--version"]:
            result = subprocess.run([BINARY, option], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0)
            self.assertIn("sovkit-stun 0.1.0", result.stdout)
            self.assertNotIn('"event":"started"', result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
