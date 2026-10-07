#!/usr/bin/env python3
'''
PROJECT:     LiberNT Network Kernel Debugger Transport
LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
PURPOSE:     Rendezvous relay: forwards the encrypted datagrams between targets and debugger clients
COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
'''

import argparse
import hashlib
import hmac
import json
import os
import socket
import struct
import sys
import time

MAGIC = b'LKDN'
VERSION = 1
TYPE_HELLO, TYPE_INIT, TYPE_RESPONSE, TYPE_DATA, TYPE_SUBSCRIBE = 1, 2, 3, 4, 5
HEADER_SIZE = 20
HELLO_SIZE = 72
MAXIMUM_DATAGRAM = 1500
DEVICE_TIMEOUT = 30.0
CLIENT_TIMEOUT = 60.0
MAXIMUM_DEVICES = 4096
MAXIMUM_CLIENTS = 4
COOKIE_SIZE = 16
COOKIE_PERIOD = 64.0
RATE_PACKETS = 400
RATE_WINDOW = 1.0


class Relay:
    def __init__(self, sock, status_path):
        self.sock = sock
        self.status_path = status_path
        self.devices = {}
        self.clients = {}
        self.rates = {}
        self.window = time.monotonic()
        self.status_time = 0.0
        self.secret = os.urandom(32)

    def cookie(self, key, address, period):
        text = b'%s|%d|%d|' % (address[0].encode(), address[1], period)
        return hmac.new(self.secret, text + key, hashlib.sha256).digest()[:COOKIE_SIZE]

    def routable(self, data, key, address):
        period = int(time.time() / COOKIE_PERIOD)
        offered = data[HEADER_SIZE:HEADER_SIZE + COOKIE_SIZE]
        if any(hmac.compare_digest(offered, self.cookie(key, address, p)) for p in (period, period - 1)):
            return True
        self.sock.sendto(data[:HEADER_SIZE] + self.cookie(key, address, period), address)
        return False

    def allowed(self, address, now):
        if now - self.window >= RATE_WINDOW:
            self.rates.clear()
            self.window = now
        count = self.rates.get(address[0], 0) + 1
        self.rates[address[0]] = count
        return count <= RATE_PACKETS

    def expire(self, now):
        for key in [k for k, d in self.devices.items() if now - d['seen'] > DEVICE_TIMEOUT]:
            del self.devices[key]
        for key in list(self.clients):
            live = {a: t for a, t in self.clients[key].items() if now - t <= CLIENT_TIMEOUT}
            if live:
                self.clients[key] = live
            else:
                del self.clients[key]

    def handle(self, data, address, now):
        if (len(data) < HEADER_SIZE or len(data) > MAXIMUM_DATAGRAM or data[:4] != MAGIC or
                data[4] != VERSION or not self.allowed(address, now)):
            return
        kind = data[5]
        key = data[8:16]
        device = self.devices.get(key)
        clients = self.clients.get(key, {})

        if kind == TYPE_HELLO:
            if len(data) != HELLO_SIZE:
                return
            if device is None:
                if len(self.devices) >= MAXIMUM_DEVICES:
                    return
                device = self.devices[key] = {'first': time.time()}
            flags, uptime, output = struct.unpack('<HIQ', data[42:56])
            device.update(address=address, seen=now, wall=time.time(), hello=data,
                          mac='-'.join('%02x' % b for b in data[36:42]),
                          attached=bool(flags & 1), stopped=bool(flags & 4), crashed=bool(flags & 8),
                          uptime=uptime, output=output)
            for client in clients:
                self.sock.sendto(data, client)
        elif kind == TYPE_SUBSCRIBE:
            if len(data) != HELLO_SIZE or not self.routable(data, key, address):
                return
            if address not in clients and (len(clients) >= MAXIMUM_CLIENTS or
                                           (not clients and len(self.clients) >= MAXIMUM_DEVICES)):
                return
            self.clients.setdefault(key, {})[address] = now
            if device is not None:
                self.sock.sendto(device['hello'], address)
        elif kind in (TYPE_INIT, TYPE_RESPONSE, TYPE_DATA):
            if device is None:
                return
            if address == device['address']:
                device['seen'] = now
                for client in clients:
                    self.sock.sendto(data, client)
            elif address in clients:
                clients[address] = now
                self.sock.sendto(data, device['address'])

    def write_status(self, now):
        if not self.status_path or now - self.status_time < 2.0:
            return
        self.status_time = now
        devices = [{'id': key.hex(), 'address': d['address'][0], 'mac': d['mac'],
                    'first_seen': d['first'], 'last_seen': d['wall'], 'uptime': d['uptime'],
                    'attached': d['attached'], 'stopped': d['stopped'], 'crashed': d['crashed'], 'output': d['output'],
                    'clients': len(self.clients.get(key, {}))}
                   for key, d in self.devices.items()]
        temporary = self.status_path + '.tmp'
        with open(temporary, 'w') as stream:
            json.dump({'updated': time.time(), 'devices': devices}, stream)
        os.replace(temporary, self.status_path)

    def run(self):
        self.sock.settimeout(1.0)
        while True:
            try:
                data, address = self.sock.recvfrom(2048)
            except socket.timeout:
                data = None
            now = time.monotonic()
            if data is not None:
                self.handle(data, address, now)
            self.expire(now)
            self.write_status(now)


def main():
    parser = argparse.ArgumentParser(description='Relay between LiberNT debug targets and debugger clients.')
    parser.add_argument('--bind', default='0.0.0.0', help='address to listen on')
    parser.add_argument('--port', type=int, default=50000, help='UDP port to listen on')
    parser.add_argument('--status', help='file that receives the list of targets as JSON')
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    try:
        Relay(sock, args.status).run()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
