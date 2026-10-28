#!/usr/bin/env python3
'''
PROJECT:     LiberNT Kernel Debugger Tools
LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
PURPOSE:     Serves driver replacements (.kdfiles) to a target on the KDBG serial terminal
COPYRIGHT:   Copyright 2026 Ostap Brehin <hi@ostapbrehin.com>
'''

import argparse
import ntpath
import os
import select
import socket
import struct
import subprocess
import sys

PACKET_LEADER = b'0000'
PACKET_TYPE_KD_FILE_IO = 11
PACKET_TRAILING_BYTE = 0xAA
PACKET_HEADER = struct.Struct('<IHHII')
PACKET_MAX_SIZE = 4000
FILE_IO = struct.Struct('<II56s')
FILE_IO_SIZE = FILE_IO.size

DbgKdCreateFileApi = 0x3430
DbgKdReadFileApi = 0x3431
DbgKdCloseFileApi = 0x3433

STATUS_SUCCESS = 0
STATUS_NOT_IMPLEMENTED = 0xC0000002
STATUS_INVALID_HANDLE = 0xC0000008
STATUS_NO_SUCH_FILE = 0xC000000F


def read_map(path):
    entries = []
    lines = [line.strip() for line in open(path, encoding='utf-8-sig')]
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if not line or line.startswith('#'):
            continue
        block = lines[i:i + 2]
        if line.lower() != 'map' or len(block) < 2 or not all(block) or any(n.startswith('#') for n in block):
            sys.exit(f'{path}:{i}: expected "map" followed by the old and the new file on the next two lines')
        entries.append(tuple(block))
        i += 2
    return entries


def components(name):
    name = name.lower().replace('/', '\\')
    if name.startswith('\\??\\'):
        name = name[4:]
    if name[1:2] == ':':
        name = name[2:]
    parts = [part for part in name.split('\\') if part]
    if parts and parts[0] == 'systemroot':
        parts[0] = None
    return parts


def matches(entry, target):
    entry = entry.lower().strip()
    if '\\' not in entry and '/' not in entry:
        base = ntpath.basename(target.lower())
        return base == entry or ntpath.splitext(base)[0] == entry
    entry, target = components(entry), components(target)
    if not entry or len(entry) > len(target):
        return False
    return all(e is None or t is None or e == t for e, t in zip(entry, target[len(target) - len(entry):]))


class Server:
    def __init__(self, entries, log, verbose):
        self.entries = entries
        self.log = log
        self.verbose = verbose
        self.handles = {}
        self.next_handle = 1

    def create(self, name):
        for old, new in self.entries:
            if matches(old, name):
                try:
                    data = open(new, 'rb').read()
                except OSError as error:
                    self.log(f'kdfiles: {name}: {new}: {error.strerror}')
                    return STATUS_NO_SUCH_FILE, 0, 0
                handle = self.next_handle
                self.next_handle += 1
                self.handles[handle] = data
                self.log(f'kdfiles: sending {new} ({len(data)} bytes) for {name}')
                return STATUS_SUCCESS, handle, len(data)
        if self.verbose:
            self.log(f'kdfiles: {name} is not mapped')
        return STATUS_NO_SUCH_FILE, 0, 0

    def handle(self, header, data):
        api, _, body = FILE_IO.unpack(header)
        body = bytearray(body)
        status, payload = STATUS_SUCCESS, b''
        if api == DbgKdCreateFileApi:
            name = data.decode('utf-16le', 'replace').split('\0')[0]
            status, handle, length = self.create(name)
            struct.pack_into('<QQ', body, 24, handle, length)
        elif api == DbgKdReadFileApi:
            handle, offset, length = struct.unpack_from('<QQI', body)
            if handle in self.handles:
                payload = self.handles[handle][offset:offset + min(length, PACKET_MAX_SIZE - FILE_IO_SIZE)]
                struct.pack_into('<I', body, 16, len(payload))
            else:
                status = STATUS_INVALID_HANDLE
        elif api == DbgKdCloseFileApi:
            handle, = struct.unpack_from('<Q', body)
            if self.handles.pop(handle, None) is not None:
                self.log(f'kdfiles: handle {handle} closed')
        else:
            status = STATUS_NOT_IMPLEMENTED
        return FILE_IO.pack(api, status, bytes(body)) + payload


def packet(header_and_data, packet_id):
    checksum = sum(header_and_data) & 0xFFFFFFFF
    return (PACKET_HEADER.pack(0x30303030, PACKET_TYPE_KD_FILE_IO, len(header_and_data),
                               packet_id, checksum)
            + header_and_data + bytes([PACKET_TRAILING_BYTE]))


class Demux:
    def __init__(self, server, write_target, write_text):
        self.server = server
        self.write_target = write_target
        self.write_text = write_text
        self.buffer = b''

    def feed(self, data):
        self.buffer += data
        while self.buffer:
            start = self.buffer.find(PACKET_LEADER)
            if start < 0:
                keep = len(self.buffer) - len(self.buffer.rstrip(b'0'))
                self.write_text(self.buffer[:len(self.buffer) - keep])
                self.buffer = self.buffer[len(self.buffer) - keep:]
                return
            if start:
                self.write_text(self.buffer[:start])
                self.buffer = self.buffer[start:]
            if len(self.buffer) < PACKET_HEADER.size:
                return
            _, kind, count, packet_id, checksum = PACKET_HEADER.unpack_from(self.buffer)
            if kind != PACKET_TYPE_KD_FILE_IO or not FILE_IO_SIZE <= count <= PACKET_MAX_SIZE:
                self.write_text(self.buffer[:1])
                self.buffer = self.buffer[1:]
                continue
            end = PACKET_HEADER.size + count + 1
            if len(self.buffer) < end:
                return
            body = self.buffer[PACKET_HEADER.size:end - 1]
            if self.buffer[end - 1] != PACKET_TRAILING_BYTE or sum(body) & 0xFFFFFFFF != checksum:
                self.write_text(self.buffer[:1])
                self.buffer = self.buffer[1:]
                continue
            self.buffer = self.buffer[end:]
            self.write_target(packet(self.server.handle(body[:FILE_IO_SIZE], body[FILE_IO_SIZE:]), packet_id))


def connect(spec):
    kind, _, address = spec.partition(':')
    if kind == 'unix':
        sock = socket.socket(socket.AF_UNIX)
        sock.connect(address)
        return sock.fileno(), sock.fileno(), sock
    if kind == 'tcp':
        host, _, port = address.rpartition(':')
        sock = socket.create_connection((host, int(port)))
        return sock.fileno(), sock.fileno(), sock
    if kind == 'exec':
        process = subprocess.Popen(address, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        return process.stdout.fileno(), process.stdin.fileno(), process
    sys.exit(f'unknown connection {spec}: use unix:PATH, tcp:HOST:PORT or exec:COMMAND')


def main():
    parser = argparse.ArgumentParser(
        description='Relay the KDBG serial terminal and answer its driver replacement requests.')
    parser.add_argument('connection', help='unix:PATH, tcp:HOST:PORT or exec:COMMAND (the serial line)')
    parser.add_argument('-f', '--map', help='driver replacement map file (.kdfiles format)')
    parser.add_argument('-m', nargs=2, action='append', default=[], metavar=('OLD', 'NEW'),
                        help='add one replacement, as .kdfiles -m')
    parser.add_argument('-v', '--verbose', action='store_true', help='also report the images that are not mapped')
    parser.add_argument('-o', '--output', help='append the terminal text to this file instead of stdout')
    args = parser.parse_args()

    entries = (read_map(args.map) if args.map else []) + [tuple(m) for m in args.m]
    for _, new in entries:
        if not os.path.isfile(new):
            print(f'kdfiles: warning: {new} does not exist yet', file=sys.stderr)

    output = open(args.output, 'ab', buffering=0) if args.output else sys.stdout.buffer

    def write_text(data):
        output.write(data)
        output.flush()

    def log(text):
        write_text(f'\n{text}\n'.encode())
        if args.output:
            print(text, file=sys.stderr)

    source, sink, connection = connect(args.connection)
    demux = Demux(Server(entries, log, args.verbose), lambda data: os.write(sink, data), write_text)
    inputs = [source, sys.stdin.fileno()]
    while True:
        ready, _, _ = select.select(inputs, [], [])
        if source in ready:
            data = os.read(source, 65536)
            if not data:
                break
            demux.feed(data)
        if sys.stdin.fileno() in ready:
            data = os.read(sys.stdin.fileno(), 4096)
            if data:
                os.write(sink, data)
            else:
                inputs.remove(sys.stdin.fileno())


if __name__ == '__main__':
    main()
