#!/usr/bin/env python3
'''
PROJECT:     LiberNT Network Kernel Debugger Transport
LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
PURPOSE:     Host side of the network debugger: attaches to a target and relays its terminal
COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
'''

import argparse
import hashlib
import hmac
import os
import select
import shutil
import socket
import struct
import sys
import time

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

MAGIC = b'LKDN'
VERSION = 1
TYPE_HELLO, TYPE_INIT, TYPE_RESPONSE, TYPE_DATA, TYPE_SUBSCRIBE = 1, 2, 3, 4, 5
HEADER_SIZE = 20
HELLO_SIZE = 72
TAG_SIZE = 16
STREAM_HEADER = 24
SEGMENT = 1100
PROTOCOL_NAME = b'Noise_NNpsk0_25519_ChaChaPoly_SHA256'
PROLOGUE_LABEL = b'LiberNT KDNET 1'
NO_OFFSET = 0xFFFFFFFFFFFFFFFF
CONTROL_GAP = 1
CONTROL_INFO = 2
CONTROL_SIZE = 1
CONTROL_MISSING = 2
CONTROL_QUERY = 4
HELLO_ATTACHED, HELLO_QUERY, HELLO_STOPPED, HELLO_CRASHED = 1, 2, 4, 8
INFO_SYSTEM, INFO_PCI, INFO_ACPI, INFO_MODULES = 1, 2, 3, 4
INFO_NAMES = {INFO_SYSTEM: 'system', INFO_PCI: 'pci', INFO_ACPI: 'acpi', INFO_MODULES: 'modules'}
INFO_LIMIT = 0x10000
QUIT_KEY = 0x1D


def sha256(*parts):
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part)
    return digest.digest()


def hmac256(key, *parts):
    mac = hmac.new(key, digestmod=hashlib.sha256)
    for part in parts:
        mac.update(part)
    return mac.digest()


def hkdf(chaining_key, material, count):
    temporary = hmac256(chaining_key, material)
    outputs = [hmac256(temporary, b'\x01')]
    for index in range(2, count + 1):
        outputs.append(hmac256(temporary, outputs[-1], bytes([index])))
    return outputs


def nonce(counter):
    return b'\x00\x00\x00\x00' + struct.pack('<Q', counter)


def normalize_key(text):
    return text.replace('-', '').replace(' ', '').upper().encode('ascii')


class Target:
    def __init__(self, key_text):
        self.psk = sha256(b'LiberNT KDNET v1 key', normalize_key(key_text))
        self.hello_key = sha256(b'LiberNT KDNET v1 hello', self.psk)
        self.rendezvous = sha256(b'LiberNT KDNET v1 rendezvous', self.psk)[:8]
        self.attached = False
        self.session = 0
        self.send_key = None
        self.receive_key = None
        self.send_counter = 0
        self.receive_counter = -1
        self.expected = 0
        self.input_base = 0
        self.input = bytearray()
        self.lost = 0
        self.missing = False
        self.handshake = None
        self.device_nonce = None
        self.info = {}
        self.queries = {}
        self.answers = {}
        self.query_now = False

    def header(self, kind, session=0):
        return MAGIC + bytes([VERSION, kind, 0, 0]) + self.rendezvous + struct.pack('<I', session)

    def check_header(self, data):
        return (len(data) >= HEADER_SIZE and data[:4] == MAGIC and data[4] == VERSION and
                data[8:16] == self.rendezvous)

    def parse_hello(self, data):
        if len(data) != HEADER_SIZE + 36 + TAG_SIZE:
            return False
        if not hmac.compare_digest(hmac256(self.hello_key, data[:-TAG_SIZE])[:TAG_SIZE], data[-TAG_SIZE:]):
            return False
        self.device_nonce = data[20:36]
        mac = data[36:42]
        flags, uptime, head = struct.unpack('<HIQ', data[42:56])
        self.info = {'mac': '-'.join('%02x' % b for b in mac), 'attached': bool(flags & HELLO_ATTACHED),
                     'query': bool(flags & HELLO_QUERY), 'stopped': bool(flags & HELLO_STOPPED),
                     'crashed': bool(flags & HELLO_CRASHED), 'uptime': uptime, 'output': head}
        return True

    def make_init(self, start, columns, rows):
        h = sha256(PROTOCOL_NAME)
        ck = h
        h = sha256(h, PROLOGUE_LABEL + self.rendezvous + self.device_nonce)
        ck, temp_h, k = hkdf(ck, self.psk, 3)
        h = sha256(h, temp_h)
        private = X25519PrivateKey.generate()
        public = private.public_key().public_bytes(serialization.Encoding.Raw,
                                                   serialization.PublicFormat.Raw)
        h = sha256(h, public)
        ck, k = hkdf(ck, public, 2)
        payload = struct.pack('<QHHI', start, columns, rows, 0)
        cipher = ChaCha20Poly1305(k).encrypt(nonce(0), payload, h)
        h = sha256(h, cipher)
        self.handshake = (private, h, ck)
        return self.header(TYPE_INIT) + public + cipher

    def handle_response(self, data):
        if self.handshake is None or len(data) != HEADER_SIZE + 32 + 24 + TAG_SIZE:
            return False
        private, h, ck = self.handshake
        remote = data[HEADER_SIZE:HEADER_SIZE + 32]
        h = sha256(h, remote)
        ck, k = hkdf(ck, remote, 2)
        ck, k = hkdf(ck, private.exchange(X25519PublicKey.from_public_bytes(remote)), 2)
        try:
            payload = ChaCha20Poly1305(k).decrypt(nonce(0), data[HEADER_SIZE + 32:], h)
        except InvalidTag:
            return False
        start, head, session, _ = struct.unpack('<QQII', payload)
        self.send_key, self.receive_key = hkdf(ck, b'', 2)
        self.session = session
        self.expected = start
        self.send_counter = 0
        self.receive_counter = -1
        self.input_base = 0
        self.input = bytearray()
        self.attached = True
        self.handshake = None
        self.queries = {}
        self.answers = {}
        self.info['start'] = start
        self.info['head'] = head
        return True

    def make_data(self, columns, rows):
        header = self.header(TYPE_DATA, self.session) + struct.pack('<Q', self.send_counter)
        payload = bytes(self.input[:SEGMENT])
        control = CONTROL_SIZE | (CONTROL_MISSING if self.missing else 0)
        plain = struct.pack('<QQHHHH', self.input_base, self.expected, control,
                            columns, rows, 0) + payload
        cipher = ChaCha20Poly1305(self.send_key).encrypt(nonce(self.send_counter), plain, header)
        self.send_counter += 1
        return header + cipher

    def request(self, kind):
        self.answers.pop(kind, None)
        self.queries[kind] = bytearray()
        self.query_now = True

    def make_query(self):
        kind = min(self.queries)
        header = self.header(TYPE_DATA, self.session) + struct.pack('<Q', self.send_counter)
        plain = struct.pack('<QQHHI', self.input_base, self.expected, CONTROL_QUERY, kind,
                            len(self.queries[kind]))
        cipher = ChaCha20Poly1305(self.send_key).encrypt(nonce(self.send_counter), plain, header)
        self.send_counter += 1
        return header + cipher

    def handle_information(self, plain):
        kind, offset = struct.unpack('<HI', plain[18:24])
        received = self.queries.get(kind)
        if received is None or len(plain) < STREAM_HEADER + 4 or offset != len(received):
            return
        total = struct.unpack('<I', plain[STREAM_HEADER:STREAM_HEADER + 4])[0]
        received += plain[STREAM_HEADER + 4:]
        self.query_now = True
        if not total or total > INFO_LIMIT or len(plain) == STREAM_HEADER + 4 or len(received) >= total:
            del self.queries[kind]
            self.answers[kind] = bytes(received[:total]).decode('ascii', 'replace') if total <= INFO_LIMIT else ''

    def handle_data(self, data):
        if not self.attached or len(data) < HEADER_SIZE + 8 + STREAM_HEADER + TAG_SIZE:
            return None
        if struct.unpack('<I', data[16:20])[0] != self.session:
            return None
        counter = struct.unpack('<Q', data[20:28])[0]
        if counter <= self.receive_counter:
            return None
        try:
            plain = ChaCha20Poly1305(self.receive_key).decrypt(nonce(counter), data[28:], data[:28])
        except InvalidTag:
            return None
        self.receive_counter = counter
        sequence, acknowledged, control = struct.unpack('<QQH', plain[:18])
        if self.input_base < acknowledged <= self.input_base + len(self.input):
            del self.input[:acknowledged - self.input_base]
            self.input_base = acknowledged
        if control & CONTROL_INFO:
            self.handle_information(plain)
            return b''
        payload = plain[STREAM_HEADER:]
        if sequence > self.expected and (control & CONTROL_GAP):
            self.lost += sequence - self.expected
            self.expected = sequence
        if sequence != self.expected:
            if sequence > self.expected:
                self.missing = True
            return b''
        self.missing = False
        self.expected += len(payload)
        return payload


def parse_args():
    parser = argparse.ArgumentParser(description='Attach to a LiberNT target over the network debugger transport.')
    parser.add_argument('--key', required=True, help='key shown by the target or set with /ENCRYPTION_KEY')
    parser.add_argument('--port', type=int, default=50000, help='UDP port to listen on')
    parser.add_argument('--bind', default='0.0.0.0', help='local address to listen on')
    parser.add_argument('--relay', metavar='HOST[:PORT]', help='reach the target through this relay')
    parser.add_argument('--live', action='store_true', help='skip the output buffered before the attach')
    parser.add_argument('--output', help='also write the target output to this file')
    parser.add_argument('--duration', type=float, help='detach after this many seconds')
    parser.add_argument('--size', metavar='COLUMNSxROWS', help='terminal size to report instead of the real one')
    parser.add_argument('--info', action='store_true',
                        help='print the description of the machine (system, PCI, ACPI, modules) and detach')
    parser.add_argument('--send', action='append', default=[], metavar='SECONDS:TEXT',
                        help='type TEXT this many seconds after the attach (\\n, \\x03 are decoded)')
    return parser.parse_args()


def main():
    args = parse_args()
    target = Target(args.key)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    relay = None
    if args.relay:
        host, _, port = args.relay.partition(':')
        relay = (socket.gethostbyname(host), int(port or 50000))
    else:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.bind, args.port))
    sock.setblocking(False)

    interactive = sys.stdin.isatty() and sys.stdout.isatty()
    saved_terminal = None
    if interactive:
        import termios
        import tty
        saved_terminal = termios.tcgetattr(sys.stdin.fileno())
        tty.setraw(sys.stdin.fileno())

    log = open(args.output, 'wb') if args.output else None
    script = []
    for item in args.send:
        delay, _, text = item.partition(':')
        script.append((float(delay), text.encode('ascii').decode('unicode_escape').encode('latin-1')))
    script.sort()

    def status(text):
        sys.stderr.write(text + ('\r\n' if interactive else '\n'))
        sys.stderr.flush()

    def terminal_size():
        if args.size:
            columns, _, rows = args.size.lower().partition('x')
            return os.terminal_size((int(columns), int(rows)))
        return shutil.get_terminal_size((0, 0)) if interactive else os.terminal_size((0, 0))

    if relay:
        status('kdnet: waiting for target %s at relay %s:%d' % ((target.rendezvous.hex(),) + relay))
    else:
        status('kdnet: waiting for target %s on UDP port %d' % (target.rendezvous.hex(), args.port))
    peer = None
    cookie = bytes(HELLO_SIZE - HEADER_SIZE)
    last_subscribe = -60.0
    last_init = 0.0
    last_send = 0.0
    attach_time = None
    session_time = 0.0
    resume = None
    acknowledge = False
    last_query = 0.0
    try:
        while True:
            now = time.monotonic()
            if attach_time is not None and args.duration is not None and now - attach_time >= args.duration:
                break

            if relay and now - last_subscribe >= 10.0:
                sock.sendto(target.header(TYPE_SUBSCRIBE) + cookie, relay)
                last_subscribe = now

            readers = [sock]
            if interactive:
                readers.append(sys.stdin)
            ready, _, _ = select.select(readers, [], [], 0.05)
            now = time.monotonic()

            if sock in ready:
                while True:
                    try:
                        data, address = sock.recvfrom(2048)
                    except BlockingIOError:
                        break
                    if not target.check_header(data) or (relay and address != relay):
                        continue
                    kind = data[5]
                    if kind == TYPE_SUBSCRIBE and relay:
                        offered = data[HEADER_SIZE:]
                        if offered and offered != cookie[:len(offered)]:
                            cookie = offered + bytes(HELLO_SIZE - HEADER_SIZE - len(offered))
                            last_subscribe = -60.0
                    elif kind == TYPE_HELLO and target.parse_hello(data):
                        peer = address
                        if target.attached and not target.info['attached'] and now - session_time >= 3.0:
                            resume = target.expected if target.info['output'] >= target.expected else None
                            target.attached = False
                            status('kdnet: the target %s, attaching again' %
                                   ('dropped the session' if resume is not None else 'restarted'))
                        if not target.attached and now - last_init >= 0.5:
                            size = terminal_size()
                            start = resume if resume is not None else NO_OFFSET if args.live else 0
                            sock.sendto(target.make_init(start, size.columns, size.lines), peer)
                            last_init = now
                    elif kind == TYPE_RESPONSE and not target.attached:
                        if target.handle_response(data):
                            peer = address
                            session_time = now
                            if attach_time is None:
                                attach_time = now
                            acknowledge = True
                            status('kdnet: attached to %s at %s, output %d..%d' %
                                   (target.info.get('mac'), address[0], target.info['start'], target.info['head']))
                            if args.info and target.info.get('query'):
                                for kind in INFO_NAMES:
                                    target.request(kind)
                            elif args.info:
                                status('kdnet: this target does not describe itself')
                                args.duration = 0.0
                    elif kind == TYPE_DATA:
                        payload = target.handle_data(data)
                        if payload is None:
                            continue
                        peer = address
                        acknowledge = True
                        if payload and not args.info:
                            if log:
                                log.write(payload)
                                log.flush()
                            text = payload.replace(b'\n', b'\r\n') if interactive else payload
                            sys.stdout.buffer.write(text)
                            sys.stdout.buffer.flush()

            if interactive and sys.stdin in ready:
                typed = os.read(sys.stdin.fileno(), 256)
                if not typed or QUIT_KEY in typed:
                    break
                if target.attached:
                    target.input += typed
                    last_send = 0.0

            while target.attached and script and now - attach_time >= script[0][0]:
                target.input += script.pop(0)[1]
                last_send = 0.0

            if args.info and target.attached and not target.queries and target.answers:
                for kind, name in INFO_NAMES.items():
                    sys.stdout.write('[%s]\n%s\n' % (name, target.answers.get(kind, '')))
                sys.stdout.flush()
                break

            if target.attached and peer and target.queries and (target.query_now or now - last_query >= 0.25):
                sock.sendto(target.make_query(), peer)
                target.query_now = False
                last_query = now

            if target.attached and peer:
                interval = 0.05 if target.missing else 0.2 if target.input else 5.0
                if acknowledge or now - last_send >= interval:
                    size = terminal_size()
                    sock.sendto(target.make_data(size.columns, size.lines), peer)
                    last_send = now
                    acknowledge = False
    except KeyboardInterrupt:
        pass
    finally:
        if saved_terminal is not None:
            import termios
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, saved_terminal)
        if log:
            log.close()

    if target.lost:
        status('kdnet: %d bytes of output were dropped by the target' % target.lost)
    return 0 if target.attached else 1


if __name__ == '__main__':
    sys.exit(main())
