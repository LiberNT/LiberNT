#!/usr/bin/python3
# PROJECT:     LiberNT Linux Subsystem
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Wayland compositor that shows Linux windows as LiberNT windows
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

import array
import ctypes
import errno
import mmap
import os
import selectors
import socket
import struct
import sys
import threading
import time
import traceback
import xml.etree.ElementTree as ET

PROTOCOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'protocols')
DEVICE = '/dev/lxwin'
SOCKET_DIR = '/run/lxwayland'
SOCKET_PATH = SOCKET_DIR + '/wayland-0'

WIN_CREATE, WIN_DESTROY, WIN_TITLE, WIN_SHOW, WIN_GEOMETRY, WIN_PIXELS = 1, 2, 3, 4, 5, 6
WIN_MOVE, WIN_RESIZE, WIN_STATE, WIN_CURSOR, WIN_SIZES, WIN_ACTIVATE, WIN_CLIPBOARD = 7, 8, 9, 10, 11, 12, 13
WIN_HELLO, WIN_CONFIGURE, WIN_CLOSE, WIN_FOCUS, WIN_POINTER = 100, 101, 102, 103, 104
WIN_BUTTON, WIN_AXIS, WIN_KEY, WIN_LEAVE, WIN_CLIPBOARD_SET = 105, 106, 107, 108, 109
TEXT_TYPES = ('text/plain;charset=utf-8', 'UTF8_STRING', 'text/plain', 'TEXT', 'STRING')
CLIPBOARD_LIMIT = 1024 * 1024 - 64
WIN_HOST_ATTACHED, WIN_HOST_DETACHED = 200, 201
FLAG_POPUP, FLAG_DECORATED = 1, 2
CONF_MAXIMIZED, CONF_FULLSCREEN, CONF_ACTIVATED, CONF_RESIZING = 1, 2, 4, 8

XDG_MAXIMIZED, XDG_FULLSCREEN, XDG_RESIZING, XDG_ACTIVATED = 1, 2, 3, 4
XDG_TILED = (5, 6, 7, 8)

CURSOR_SHAPES = {
    1: 0, 2: 0, 3: 0, 4: 2, 5: 3, 6: 3, 7: 4, 8: 4, 9: 1, 10: 1, 11: 0, 12: 0, 13: 9, 14: 11, 15: 11,
    16: 2, 17: 9, 18: 6, 19: 5, 20: 8, 21: 7, 22: 5, 23: 7, 24: 8, 25: 6, 26: 6, 27: 5, 28: 8, 29: 7,
    30: 6, 31: 5, 32: 9, 33: 0, 34: 0, 35: 0, 36: 9,
}


def log(*args):
    print('lxwayland:', *args, file=sys.stderr, flush=True)


def now_ms():
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


class Message:
    def __init__(self, element, opcode):
        self.name = element.get('name')
        self.opcode = opcode
        self.destructor = element.get('type') == 'destructor'
        self.since = int(element.get('since', '1'))
        self.args = [(a.get('type'), a.get('interface'), a.get('allow-null') == 'true') for a in element.findall('arg')]


class Interface:
    def __init__(self, element):
        self.name = element.get('name')
        self.version = int(element.get('version'))
        self.requests = [Message(e, i) for i, e in enumerate(element.findall('request'))]
        self.events = [Message(e, i) for i, e in enumerate(element.findall('event'))]
        self.event_map = {m.name: m for m in self.events}


INTERFACES = {}
for name in sorted(os.listdir(PROTOCOLS)):
    if name.endswith('.xml'):
        for element in ET.parse(os.path.join(PROTOCOLS, name)).getroot().findall('interface'):
            INTERFACES[element.get('name')] = Interface(element)


class ProtocolError(Exception):
    def __init__(self, resource, code, text):
        super().__init__(text)
        self.resource = resource
        self.code = code
        self.text = text


class Resource:
    def __init__(self, client, oid, interface, version):
        self.client = client
        self.id = oid
        self.interface = INTERFACES[interface]
        self.version = version
        self.data = None
        self.alive = True

    def send(self, event, *args):
        if self.alive:
            self.client.send(self, event, args)

    def send_owned(self, event, *args):
        self.client.send(self, event, args, True)


class Client:
    def __init__(self, server, sock):
        self.server = server
        self.sock = sock
        self.objects = {}
        self.inbuf = bytearray()
        self.infds = []
        self.out = []
        self.next_server_id = 0xFF000000
        self.closed = False
        display = Resource(self, 1, 'wl_display', 1)
        self.objects[1] = display
        self.display = display

    def new_server_id(self):
        oid = self.next_server_id
        self.next_server_id += 1
        return oid

    def add(self, oid, interface, version):
        if oid in self.objects and oid != 0:
            raise ProtocolError(self.display, 0, 'object %d already exists' % oid)
        resource = Resource(self, oid, interface, version)
        self.objects[oid] = resource
        return resource

    def remove(self, resource):
        resource.alive = False
        if self.objects.get(resource.id) is resource:
            del self.objects[resource.id]
            if resource.id < 0xFF000000:
                self.send(self.display, 'delete_id', (resource.id,))

    def send(self, resource, event, args, owned=False):
        message = resource.interface.event_map[event]
        body = bytearray()
        fds = []
        for (kind, iface, nullable), value in zip(message.args, args):
            if kind == 'int':
                body += struct.pack('<i', value)
            elif kind in ('uint', 'object', 'new_id'):
                if kind == 'object' and value is not None and not isinstance(value, int):
                    value = value.id
                if kind == 'new_id' and not isinstance(value, int):
                    value = value.id
                body += struct.pack('<I', 0 if value is None else value & 0xFFFFFFFF)
            elif kind == 'fixed':
                body += struct.pack('<i', int(round(value * 256.0)))
            elif kind == 'string':
                if value is None:
                    body += struct.pack('<I', 0)
                else:
                    data = value.encode('utf-8') + b'\0'
                    body += struct.pack('<I', len(data)) + data + b'\0' * (-len(data) % 4)
            elif kind == 'array':
                data = bytes(value)
                body += struct.pack('<I', len(data)) + data + b'\0' * (-len(data) % 4)
            elif kind == 'fd':
                fds.append(value)
        header = struct.pack('<II', resource.id, ((8 + len(body)) << 16) | message.opcode)
        self.out.append((header + body, fds, owned))

    def flush(self):
        while self.out and not self.closed:
            data, fds, owned = self.out[0]
            try:
                if fds:
                    sent = self.sock.sendmsg([data], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', fds))])
                else:
                    sent = self.sock.send(data)
            except BlockingIOError:
                return False
            except OSError:
                self.server.drop(self)
                return True
            if owned:
                for fd in fds:
                    os.close(fd)
            if sent < len(data):
                self.out[0] = (data[sent:], [], False)
                return False
            self.out.pop(0)
        return True

    def discard(self):
        for data, fds, owned in self.out:
            if owned:
                for fd in fds:
                    os.close(fd)
        self.out = []

    def decode(self, message, data):
        args = []
        offset = 0
        for kind, iface, nullable in message.args:
            if kind == 'fd':
                if not self.infds:
                    raise ProtocolError(self.display, 1, 'missing file descriptor')
                args.append(self.infds.pop(0))
                continue
            value, = struct.unpack_from('<I', data, offset)
            offset += 4
            if kind == 'int':
                args.append(struct.unpack('<i', struct.pack('<I', value))[0])
            elif kind == 'uint':
                args.append(value)
            elif kind == 'fixed':
                args.append(struct.unpack('<i', struct.pack('<I', value))[0] / 256.0)
            elif kind == 'object':
                if value == 0:
                    args.append(None)
                else:
                    target = self.objects.get(value)
                    if target is None:
                        args.append(None)
                    else:
                        args.append(target)
            elif kind == 'new_id':
                if iface is None:
                    length = value
                    name = bytes(data[offset:offset + length - 1]).decode()
                    offset += length + (-length % 4)
                    version, oid = struct.unpack_from('<II', data, offset)
                    offset += 8
                    args.append((name, version, oid))
                else:
                    args.append(value)
            elif kind in ('string', 'array'):
                length = value
                raw = bytes(data[offset:offset + length])
                offset += length + (-length % 4)
                if kind == 'string':
                    args.append(None if length == 0 else raw[:-1].decode('utf-8', 'replace'))
                else:
                    args.append(raw)
        return args

    def receive(self):
        try:
            data, ancdata, flags, addr = self.sock.recvmsg(65536, socket.CMSG_SPACE(28 * 4))
        except BlockingIOError:
            return
        except OSError:
            self.server.drop(self)
            return
        for level, kind, payload in ancdata:
            if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                fds = array.array('i')
                fds.frombytes(payload[:len(payload) - len(payload) % fds.itemsize])
                self.infds.extend(fds)
        if not data:
            self.server.drop(self)
            return
        self.inbuf += data
        while len(self.inbuf) >= 8 and not self.closed:
            oid, word = struct.unpack_from('<II', self.inbuf, 0)
            size = word >> 16
            opcode = word & 0xFFFF
            if size < 8:
                self.server.drop(self)
                return
            if len(self.inbuf) < size:
                break
            body = self.inbuf[8:size]
            del self.inbuf[:size]
            resource = self.objects.get(oid)
            if resource is None:
                continue
            if opcode >= len(resource.interface.requests):
                self.error(resource, 1, 'invalid opcode')
                return
            message = resource.interface.requests[opcode]
            try:
                args = self.decode(message, body)
                handler = getattr(self.server, '%s_%s' % (resource.interface.name, message.name), None)
                if handler is not None:
                    handler(resource, *args)
                if message.destructor and resource.alive:
                    self.server.destroy_resource(resource)
                    self.remove(resource)
            except ProtocolError as e:
                self.error(e.resource, e.code, e.text)
                return
            except Exception:
                log('request %s.%s failed:\n%s' % (resource.interface.name, message.name, traceback.format_exc()))
                self.server.drop(self)
                return

    def error(self, resource, code, text):
        log('protocol error on %s@%d: %s' % (resource.interface.name, resource.id, text))
        self.send(self.display, 'error', (resource.id, code, text))
        self.flush()
        self.server.drop(self)


class Pool:
    def __init__(self, fd, size):
        self.fd = fd
        self.size = size
        self.map = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ) if size > 0 else None
        self.refs = 1

    def resize(self, size):
        if self.map is not None:
            self.map.close()
        self.size = size
        self.map = mmap.mmap(self.fd, size, mmap.MAP_SHARED, mmap.PROT_READ)

    def release(self):
        self.refs -= 1
        if self.refs == 0:
            if self.map is not None:
                self.map.close()
                self.map = None
            os.close(self.fd)


class Buffer:
    def __init__(self, pool, offset, width, height, stride, fmt):
        self.pool = pool
        self.offset = offset
        self.width = width
        self.height = height
        self.stride = stride
        self.format = fmt
        self.color = None


class Surface:
    def __init__(self, resource):
        self.resource = resource
        self.pending_buffer = None
        self.pending_attached = False
        self.pending_damage = []
        self.pending_frames = []
        self.buffer = None
        self.width = 0
        self.height = 0
        self.role = None
        self.xdg = None
        self.parent = None
        self.children = []
        self.sub_x = 0
        self.sub_y = 0
        self.pending_sub = None
        self.cursor_shape = None


class XdgSurface:
    def __init__(self, resource, surface):
        self.resource = resource
        self.surface = surface
        self.role = None
        self.geometry = None
        self.pending_geometry = None
        self.configured = False
        self.serial = 0
        self.window = None


class Toplevel:
    def __init__(self, resource, xdg):
        self.resource = resource
        self.xdg = xdg
        self.title = ''
        self.app_id = ''
        self.min_size = (0, 0)
        self.max_size = (0, 0)
        self.decoration = None
        self.decorated = False
        self.width = 0
        self.height = 0
        self.states = set()
        self.parent = None


class Popup:
    def __init__(self, resource, xdg, parent, positioner):
        self.resource = resource
        self.xdg = xdg
        self.parent = parent
        self.positioner = dict(positioner)
        self.grab = False
        self.geometry = (0, 0, 1, 1)


class Window:
    def __init__(self, wid, xdg, popup):
        self.id = wid
        self.xdg = xdg
        self.popup = popup
        self.width = 0
        self.height = 0
        self.image = None
        self.created = False


class HostSource:
    def __init__(self, text):
        self.text = text
        self.data = list(TEXT_TYPES)
        self.alive = True


def write_and_close(fd, data):
    try:
        os.set_blocking(fd, True)
        view = memoryview(data)
        while view:
            written = os.write(fd, view)
            view = view[written:]
    except OSError:
        pass
    finally:
        os.close(fd)


class Keyboard:
    def __init__(self):
        self.lib = None
        self.keymap_text = None
        self.state = None
        try:
            lib = ctypes.CDLL('libxkbcommon.so.0')
            lib.xkb_context_new.restype = ctypes.c_void_p
            lib.xkb_keymap_new_from_names.restype = ctypes.c_void_p
            lib.xkb_keymap_new_from_names.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
            lib.xkb_keymap_get_as_string.restype = ctypes.c_void_p
            lib.xkb_keymap_get_as_string.argtypes = [ctypes.c_void_p, ctypes.c_int]
            lib.xkb_state_new.restype = ctypes.c_void_p
            lib.xkb_state_new.argtypes = [ctypes.c_void_p]
            lib.xkb_state_update_key.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int]
            lib.xkb_state_serialize_mods.restype = ctypes.c_uint32
            lib.xkb_state_serialize_mods.argtypes = [ctypes.c_void_p, ctypes.c_int]
            lib.xkb_state_serialize_layout.restype = ctypes.c_uint32
            lib.xkb_state_serialize_layout.argtypes = [ctypes.c_void_p, ctypes.c_int]
            context = lib.xkb_context_new(0)
            keymap = lib.xkb_keymap_new_from_names(context, None, 0)
            text = lib.xkb_keymap_get_as_string(keymap, 1)
            self.keymap_text = ctypes.string_at(text) + b'\0'
            ctypes.CDLL(None).free(ctypes.c_void_p(text))
            self.state = lib.xkb_state_new(keymap)
            self.lib = lib
        except OSError as e:
            log('no xkbcommon keymap:', e)

    def update(self, key, pressed):
        if self.lib is None:
            return None
        self.lib.xkb_state_update_key(self.state, key + 8, 1 if pressed else 0)
        return self.mods()

    def mods(self):
        if self.lib is None:
            return (0, 0, 0, 0)
        return (self.lib.xkb_state_serialize_mods(self.state, 1), self.lib.xkb_state_serialize_mods(self.state, 2),
                self.lib.xkb_state_serialize_mods(self.state, 4), self.lib.xkb_state_serialize_layout(self.state, 0x80))

    def keymap_fd(self):
        if self.keymap_text is None:
            return None, 0
        fd = os.memfd_create('lxwayland-keymap', os.MFD_CLOEXEC)
        os.write(fd, self.keymap_text)
        return fd, len(self.keymap_text)


class Server:
    def __init__(self):
        self.selector = selectors.DefaultSelector()
        self.clients = {}
        self.serial = 1
        self.globals = []
        self.windows = {}
        self.next_window = 1
        self.frames = []
        self.screen = (1280, 800)
        self.host = False
        self.device = None
        self.keyboard = Keyboard()
        self.pointer_focus = None
        self.keyboard_focus = None
        self.focused_window = None
        self.popups = []
        self.selection = None
        self.pressed = set()
        for name, version in (('wl_compositor', 6), ('wl_subcompositor', 1), ('wl_shm', 1), ('wl_seat', 7),
                              ('wl_output', 4), ('wl_data_device_manager', 3), ('xdg_wm_base', 5),
                              ('zxdg_decoration_manager_v1', 1), ('wp_cursor_shape_manager_v1', 1)):
            self.globals.append((len(self.globals) + 1, name, version))

    def next_serial(self):
        self.serial = (self.serial + 1) & 0xFFFFFFFF
        return self.serial

    def drop(self, client):
        if client.closed:
            return
        client.closed = True
        log('client %d disconnected' % getattr(client, 'pid', 0))
        try:
            self.selector.unregister(client.sock)
        except (KeyError, ValueError):
            pass
        for resource in list(client.objects.values()):
            self.destroy_resource(resource)
        for fd in client.infds:
            os.close(fd)
        client.infds = []
        client.discard()
        client.sock.close()
        self.clients.pop(client, None)

    def destroy_resource(self, resource):
        resource.alive = False
        data = resource.data
        if isinstance(data, XdgSurface):
            self.unmap(data)
        elif isinstance(data, Toplevel):
            self.unmap(data.xdg)
        elif isinstance(data, Popup):
            self.unmap(data.xdg)
            if data in self.popups:
                self.popups.remove(data)
        elif isinstance(data, Pool):
            data.release()
        elif isinstance(data, Buffer):
            data.pool.release() if data.pool is not None else None
        elif isinstance(data, Surface):
            if self.pointer_focus is data:
                self.pointer_focus = None
            if self.keyboard_focus is data:
                self.keyboard_focus = None
            if data.parent is not None and data in data.parent.children:
                data.parent.children.remove(data)
        if self.selection is resource:
            self.set_selection(None)

    def host_send(self, kind, window, payload=b''):
        if self.device is None or not self.host:
            return
        data = struct.pack('<IIII', kind, 16 + len(payload), window, 0) + payload
        try:
            os.write(self.device, data)
        except OSError as e:
            if e.errno == errno.ENOTCONN:
                self.host = False
            else:
                log('window host write failed:', e)

    def wl_display_sync(self, display, callback):
        res = display.client.add(callback, 'wl_callback', 1)
        res.send('done', self.serial)
        display.client.remove(res)

    def wl_display_get_registry(self, display, registry):
        res = display.client.add(registry, 'wl_registry', 1)
        for name, interface, version in self.globals:
            res.send('global', name, interface, version)

    def wl_registry_bind(self, registry, name, spec):
        interface, version, oid = spec
        entry = next((g for g in self.globals if g[0] == name), None)
        if entry is None or entry[1] != interface or version > entry[2] or version < 1:
            raise ProtocolError(registry, 0, 'invalid global %d' % name)
        res = registry.client.add(oid, interface, version)
        if interface == 'wl_shm':
            res.send('format', 0)
            res.send('format', 1)
        elif interface == 'wl_seat':
            res.send('capabilities', 3)
            if version >= 2:
                res.send('name', 'seat0')
        elif interface == 'wl_output':
            self.send_output(res)
        elif interface == 'xdg_wm_base':
            res.data = []

    def send_output(self, res):
        width, height = self.screen
        res.send('geometry', 0, 0, int(width * 25.4 / 96), int(height * 25.4 / 96), 0, 'LiberNT', 'Desktop', 0)
        res.send('mode', 3, width, height, 60000)
        if res.version >= 2:
            res.send('scale', 1)
        if res.version >= 4:
            res.send('name', 'LiberNT-1')
            res.send('description', 'LiberNT desktop')
        if res.version >= 2:
            res.send('done')

    def wl_compositor_create_surface(self, compositor, oid):
        res = compositor.client.add(oid, 'wl_surface', compositor.version)
        res.data = Surface(res)

    def wl_compositor_create_region(self, compositor, oid):
        compositor.client.add(oid, 'wl_region', 1)

    def wl_shm_create_pool(self, shm, oid, fd, size):
        res = shm.client.add(oid, 'wl_shm_pool', 1)
        try:
            res.data = Pool(fd, size)
        except (OSError, ValueError) as e:
            os.close(fd)
            raise ProtocolError(shm, 2, 'cannot map pool: %s' % e)

    def wl_shm_pool_create_buffer(self, pool, oid, offset, width, height, stride, fmt):
        if fmt not in (0, 1):
            raise ProtocolError(pool, 0, 'unsupported format %d' % fmt)
        if width <= 0 or height <= 0 or stride < width * 4 or offset < 0 or offset + stride * height > pool.data.size:
            raise ProtocolError(pool, 1, 'invalid buffer geometry')
        res = pool.client.add(oid, 'wl_buffer', 1)
        pool.data.refs += 1
        res.data = Buffer(pool.data, offset, width, height, stride, fmt)

    def wl_shm_pool_resize(self, pool, size):
        if size < pool.data.size:
            raise ProtocolError(pool, 2, 'pool cannot shrink')
        pool.data.resize(size)

    def wl_surface_attach(self, surface, buffer, x, y):
        s = surface.data
        s.pending_buffer = buffer
        s.pending_attached = True

    def wl_surface_damage(self, surface, x, y, w, h):
        surface.data.pending_damage.append((x, y, w, h))

    def wl_surface_damage_buffer(self, surface, x, y, w, h):
        surface.data.pending_damage.append((x, y, w, h))

    def wl_surface_frame(self, surface, oid):
        res = surface.client.add(oid, 'wl_callback', 1)
        surface.data.pending_frames.append(res)

    def wl_surface_commit(self, surface):
        s = surface.data
        if s.pending_attached:
            s.buffer = s.pending_buffer if (s.pending_buffer is not None and s.pending_buffer.alive) else None
            if s.buffer is not None:
                s.width = s.buffer.data.width
                s.height = s.buffer.data.height
            else:
                s.width = s.height = 0
        damage = s.pending_damage
        s.pending_damage = []
        self.frames.extend(s.pending_frames)
        s.pending_frames = []
        if s.pending_sub is not None:
            s.sub_x, s.sub_y = s.pending_sub
            s.pending_sub = None
        attached = s.pending_attached
        s.pending_attached = False
        if s.role in ('toplevel', 'popup'):
            self.commit_xdg(s.xdg, damage, attached)
        elif s.role == 'subsurface':
            root = s
            while root.parent is not None and root.role == 'subsurface':
                root = root.parent
            if root.xdg is not None and root.xdg.window is not None:
                self.draw_window(root.xdg.window, None)
        if s.buffer is not None and attached:
            s.buffer.send('release')

    def wl_subcompositor_get_subsurface(self, subcompositor, oid, surface, parent):
        res = subcompositor.client.add(oid, 'wl_subsurface', 1)
        s = surface.data
        s.role = 'subsurface'
        s.parent = parent.data
        parent.data.children.append(s)
        res.data = s

    def wl_subsurface_set_position(self, subsurface, x, y):
        subsurface.data.pending_sub = (x, y)

    def wl_subsurface_destroy(self, subsurface):
        s = subsurface.data
        if s.parent is not None and s in s.parent.children:
            s.parent.children.remove(s)
        s.parent = None

    def wl_seat_get_pointer(self, seat, oid):
        seat.client.add(oid, 'wl_pointer', seat.version)

    def wl_seat_get_keyboard(self, seat, oid):
        res = seat.client.add(oid, 'wl_keyboard', seat.version)
        fd, size = self.keyboard.keymap_fd()
        if fd is None:
            fd = os.open('/dev/null', os.O_RDONLY | os.O_CLOEXEC)
            res.send_owned('keymap', 0, fd, 0)
        else:
            res.send_owned('keymap', 1, fd, size)
        if res.version >= 4:
            res.send('repeat_info', 25, 600)
        if self.keyboard_focus is not None and self.keyboard_focus.resource.client is seat.client:
            self.keyboard_enter(res, self.keyboard_focus)

    def wl_seat_get_touch(self, seat, oid):
        seat.client.add(oid, 'wl_touch', seat.version)

    def wl_pointer_set_cursor(self, pointer, serial, surface, x, y):
        if surface is not None:
            surface.data.role = surface.data.role or 'cursor'
        window = self.pointer_window()
        if window is not None:
            self.host_send(WIN_CURSOR, window.id, struct.pack('<II', 10 if surface is None else 0, 0))

    def wp_cursor_shape_manager_v1_get_pointer(self, manager, oid, pointer):
        manager.client.add(oid, 'wp_cursor_shape_device_v1', 1)

    def wp_cursor_shape_device_v1_set_shape(self, device, serial, shape):
        window = self.pointer_window()
        if window is not None:
            self.host_send(WIN_CURSOR, window.id, struct.pack('<II', CURSOR_SHAPES.get(shape, 0), 0))

    def wl_data_device_manager_create_data_source(self, manager, oid):
        res = manager.client.add(oid, 'wl_data_source', manager.version)
        res.data = []

    def wl_data_source_offer(self, source, mime):
        source.data.append(mime)

    def wl_data_device_manager_get_data_device(self, manager, oid, seat):
        res = manager.client.add(oid, 'wl_data_device', manager.version)
        res.data = 'device'
        if self.keyboard_focus is not None and self.keyboard_focus.resource.client is manager.client:
            self.offer_selection(res)

    def wl_data_device_set_selection(self, device, source, serial):
        self.set_selection(source)
        if source is not None and self.host:
            mime = next((m for m in TEXT_TYPES if m in source.data), None)
            if mime is not None:
                read_fd, write_fd = os.pipe()
                os.set_blocking(read_fd, False)
                source.send_owned('send', mime, write_fd)
                self.selector.register(read_fd, selectors.EVENT_READ, ('clipboard', read_fd, bytearray(), source))

    def wl_data_device_start_drag(self, device, source, origin, icon, serial):
        if source is not None:
            source.send('cancelled')

    def set_selection(self, source):
        if (self.selection is not None and self.selection is not source and self.selection.alive and
                not isinstance(self.selection, HostSource)):
            self.selection.send('cancelled')
        self.selection = source
        if self.keyboard_focus is not None:
            for res in list(self.keyboard_focus.resource.client.objects.values()):
                if res.interface.name == 'wl_data_device':
                    self.offer_selection(res)

    def offer_selection(self, device):
        source = self.selection
        if source is None or not source.alive:
            device.send('selection', None)
            return
        oid = device.client.new_server_id()
        offer = device.client.add(oid, 'wl_data_offer', device.version)
        offer.data = source
        device.send('data_offer', offer)
        for mime in source.data:
            offer.send('offer', mime)
        device.send('selection', offer)

    def wl_data_offer_receive(self, offer, mime, fd):
        source = offer.data
        if isinstance(source, HostSource):
            threading.Thread(target=write_and_close, args=(fd, source.text), daemon=True).start()
        elif source is not None and source.alive:
            source.send_owned('send', mime, fd)
        else:
            os.close(fd)

    def xdg_wm_base_create_positioner(self, base, oid):
        res = base.client.add(oid, 'xdg_positioner', base.version)
        res.data = {'size': (1, 1), 'anchor_rect': (0, 0, 1, 1), 'anchor': 0, 'gravity': 0, 'offset': (0, 0)}

    def xdg_positioner_set_size(self, pos, w, h):
        pos.data['size'] = (w, h)

    def xdg_positioner_set_anchor_rect(self, pos, x, y, w, h):
        pos.data['anchor_rect'] = (x, y, w, h)

    def xdg_positioner_set_anchor(self, pos, anchor):
        pos.data['anchor'] = anchor

    def xdg_positioner_set_gravity(self, pos, gravity):
        pos.data['gravity'] = gravity

    def xdg_positioner_set_offset(self, pos, x, y):
        pos.data['offset'] = (x, y)

    def xdg_wm_base_get_xdg_surface(self, base, oid, surface):
        res = base.client.add(oid, 'xdg_surface', base.version)
        res.data = XdgSurface(res, surface.data)
        surface.data.xdg = res.data

    def xdg_wm_base_pong(self, base, serial):
        pass

    def xdg_surface_get_toplevel(self, xdg, oid):
        res = xdg.client.add(oid, 'xdg_toplevel', xdg.version)
        res.data = Toplevel(res, xdg.data)
        xdg.data.role = res.data
        xdg.data.surface.role = 'toplevel'
        if xdg.version >= 5:
            res.send('wm_capabilities', struct.pack('<III', 2, 3, 4))

    def xdg_surface_get_popup(self, xdg, oid, parent, positioner):
        if parent is None:
            raise ProtocolError(xdg, 0, 'popups without a parent are not supported')
        res = xdg.client.add(oid, 'xdg_popup', xdg.version)
        res.data = Popup(res, xdg.data, parent.data, positioner.data)
        xdg.data.role = res.data
        xdg.data.surface.role = 'popup'
        res.data.geometry = self.place_popup(res.data.positioner)

    def place_popup(self, pos):
        ax, ay, aw, ah = pos['anchor_rect']
        w, h = pos['size']
        anchor = pos['anchor']
        gravity = pos['gravity']
        px = ax + aw // 2
        py = ay + ah // 2
        if anchor in (3, 5, 6):
            px = ax
        elif anchor in (4, 7, 8):
            px = ax + aw
        if anchor in (1, 5, 7):
            py = ay
        elif anchor in (2, 6, 8):
            py = ay + ah
        x = px - w // 2
        y = py - h // 2
        if gravity in (3, 5, 6):
            x = px - w
        elif gravity in (4, 7, 8):
            x = px
        if gravity in (1, 5, 7):
            y = py - h
        elif gravity in (2, 6, 8):
            y = py
        return (x + pos['offset'][0], y + pos['offset'][1], w, h)

    def xdg_surface_set_window_geometry(self, xdg, x, y, w, h):
        xdg.data.pending_geometry = (x, y, w, h)

    def xdg_surface_ack_configure(self, xdg, serial):
        pass

    def xdg_toplevel_set_title(self, toplevel, title):
        toplevel.data.title = title or ''
        window = toplevel.data.xdg.window
        if window is not None:
            self.host_send(WIN_TITLE, window.id, toplevel.data.title.encode('utf-8'))

    def xdg_toplevel_set_app_id(self, toplevel, app_id):
        toplevel.data.app_id = app_id or ''

    def xdg_toplevel_set_parent(self, toplevel, parent):
        toplevel.data.parent = parent.data if parent is not None else None

    def xdg_toplevel_set_min_size(self, toplevel, w, h):
        toplevel.data.min_size = (w, h)
        self.send_sizes(toplevel.data)

    def xdg_toplevel_set_max_size(self, toplevel, w, h):
        toplevel.data.max_size = (w, h)
        self.send_sizes(toplevel.data)

    def send_sizes(self, top):
        window = top.xdg.window
        if window is not None:
            self.host_send(WIN_SIZES, window.id, struct.pack('<IIII', top.min_size[0], top.min_size[1],
                                                             top.max_size[0], top.max_size[1]))

    def xdg_toplevel_move(self, toplevel, seat, serial):
        window = toplevel.data.xdg.window
        if window is not None:
            self.host_send(WIN_MOVE, window.id)

    def xdg_toplevel_resize(self, toplevel, seat, serial, edges):
        window = toplevel.data.xdg.window
        if window is not None:
            self.host_send(WIN_RESIZE, window.id, struct.pack('<II', edges, 0))

    def xdg_toplevel_set_maximized(self, toplevel):
        self.request_state(toplevel.data, 1)

    def xdg_toplevel_unset_maximized(self, toplevel):
        self.request_state(toplevel.data, 0)

    def xdg_toplevel_set_minimized(self, toplevel):
        self.request_state(toplevel.data, 2)

    def xdg_toplevel_set_fullscreen(self, toplevel, output):
        self.request_state(toplevel.data, 1)

    def xdg_toplevel_unset_fullscreen(self, toplevel):
        self.request_state(toplevel.data, 0)

    def request_state(self, top, state):
        window = top.xdg.window
        if window is not None:
            self.host_send(WIN_STATE, window.id, struct.pack('<II', state, 0))
        elif state == 1:
            top.states.add(XDG_MAXIMIZED)

    def xdg_popup_grab(self, popup, seat, serial):
        popup.data.grab = True

    def xdg_popup_reposition(self, popup, positioner, token):
        p = popup.data
        p.positioner = dict(positioner.data)
        p.geometry = self.place_popup(p.positioner)
        x, y, w, h = p.geometry
        p.resource.send('repositioned', token)
        p.resource.send('configure', x, y, w, h)
        self.send_xdg_configure(p.xdg)

    def zxdg_decoration_manager_v1_get_toplevel_decoration(self, manager, oid, toplevel):
        res = manager.client.add(oid, 'zxdg_toplevel_decoration_v1', 1)
        res.data = toplevel.data
        toplevel.data.decoration = res
        toplevel.data.decorated = True
        res.send('configure', 2)

    def zxdg_toplevel_decoration_v1_set_mode(self, decoration, mode):
        decoration.data.decorated = (mode == 2)
        decoration.send('configure', mode)

    def zxdg_toplevel_decoration_v1_unset_mode(self, decoration):
        decoration.data.decorated = True
        decoration.send('configure', 2)

    def send_xdg_configure(self, xdg):
        xdg.serial = self.next_serial()
        xdg.resource.send('configure', xdg.serial)
        xdg.configured = True

    def send_toplevel_configure(self, top):
        states = set(top.states)
        if not top.decorated:
            states.update(XDG_TILED)
        if self.focused_window is not None and self.focused_window.xdg is top.xdg:
            states.add(XDG_ACTIVATED)
        allowed = [s for s in sorted(states) if s <= 4 or top.resource.version >= 2]
        top.resource.send('configure', top.width, top.height, b''.join(struct.pack('<I', s) for s in allowed))
        self.send_xdg_configure(top.xdg)

    def commit_xdg(self, xdg, damage, attached):
        if xdg.pending_geometry is not None:
            xdg.geometry = xdg.pending_geometry
            xdg.pending_geometry = None
        role = xdg.role
        if role is None:
            return
        if not xdg.configured:
            if isinstance(role, Toplevel):
                self.send_toplevel_configure(role)
            else:
                x, y, w, h = role.geometry
                role.resource.send('configure', x, y, w, h)
                self.send_xdg_configure(xdg)
            return
        surface = xdg.surface
        if surface.buffer is None:
            if xdg.window is not None:
                self.unmap(xdg)
            return
        if xdg.window is None:
            self.map(xdg)
        if xdg.window is not None:
            self.draw_window(xdg.window, damage if not attached or damage else None)

    def window_rect(self, xdg):
        surface = xdg.surface
        if xdg.geometry is not None and xdg.geometry[2] > 0 and xdg.geometry[3] > 0:
            x, y, w, h = xdg.geometry
        else:
            x, y, w, h = 0, 0, surface.width, surface.height
        x = max(0, x)
        y = max(0, y)
        w = max(1, min(w, surface.width - x))
        h = max(1, min(h, surface.height - y))
        return x, y, w, h

    def map(self, xdg):
        if not self.host:
            return
        role = xdg.role
        gx, gy, w, h = self.window_rect(xdg)
        wid = self.next_window
        self.next_window += 1
        window = Window(wid, xdg, isinstance(role, Popup))
        window.width, window.height = w, h
        log('map %s %d %dx%d %s' % ('popup' if isinstance(role, Popup) else 'toplevel', wid, w, h,
                                    getattr(role, 'title', '')))
        window.image = bytearray(w * h * 4)
        xdg.window = window
        self.windows[wid] = window
        if isinstance(role, Popup):
            parent_window = role.parent.window if role.parent is not None else None
            if parent_window is None:
                xdg.window = None
                del self.windows[wid]
                return
            px, py, pw, ph = role.geometry
            pgx, pgy, _, _ = self.window_rect(role.parent)
            self.host_send(WIN_CREATE, wid, struct.pack('<iiIIII', px, py, w, h, parent_window.id, FLAG_POPUP))
            self.popups.append(role)
        else:
            flags = FLAG_DECORATED if role.decorated else 0
            self.host_send(WIN_CREATE, wid, struct.pack('<iiIIII', 0, 0, w, h, 0, flags))
            if role.title:
                self.host_send(WIN_TITLE, wid, role.title.encode('utf-8'))
            self.send_sizes(role)
        window.created = True

    def unmap(self, xdg):
        window = xdg.window
        if window is None:
            return
        xdg.window = None
        self.windows.pop(window.id, None)
        if self.focused_window is window:
            self.focused_window = None
        if self.pointer_focus is not None and self.pointer_focus.xdg is xdg:
            self.pointer_focus = None
        if isinstance(xdg.role, Popup) and xdg.role in self.popups:
            self.popups.remove(xdg.role)
        self.host_send(WIN_DESTROY, window.id)

    def draw_window(self, window, damage):
        xdg = window.xdg
        surface = xdg.surface
        gx, gy, w, h = self.window_rect(xdg)
        if (w, h) != (window.width, window.height):
            window.width, window.height = w, h
            window.image = bytearray(w * h * 4)
            px, py = (xdg.role.geometry[0], xdg.role.geometry[1]) if window.popup else (0, 0)
            self.host_send(WIN_GEOMETRY, window.id, struct.pack('<iiII', px, py, w, h))
            damage = None
        rects = [(0, 0, w, h)] if not damage else []
        for dx, dy, dw, dh in damage or []:
            x0 = max(0, dx - gx)
            y0 = max(0, dy - gy)
            x1 = min(w, dx - gx + dw)
            y1 = min(h, dy - gy + dh)
            if x1 > x0 and y1 > y0:
                rects.append((x0, y0, x1 - x0, y1 - y0))
        if not rects:
            return
        self.blit(window, surface, -gx, -gy)
        x0 = min(r[0] for r in rects)
        y0 = min(r[1] for r in rects)
        x1 = max(r[0] + r[2] for r in rects)
        y1 = max(r[1] + r[3] for r in rects)
        self.send_pixels(window, x0, y0, x1 - x0, y1 - y0)

    def blit(self, window, surface, ox, oy):
        buffer = surface.buffer.data if surface.buffer is not None else None
        if buffer is not None and buffer.pool is not None and buffer.pool.map is not None:
            mm = buffer.pool.map
            x0 = max(0, ox)
            y0 = max(0, oy)
            x1 = min(window.width, ox + buffer.width)
            y1 = min(window.height, oy + buffer.height)
            if x1 > x0 and y1 > y0:
                span = (x1 - x0) * 4
                for row in range(y0, y1):
                    src = buffer.offset + (row - oy) * buffer.stride + (x0 - ox) * 4
                    dst = (row * window.width + x0) * 4
                    window.image[dst:dst + span] = mm[src:src + span]
        for child in surface.children:
            self.blit(window, child, ox + child.sub_x, oy + child.sub_y)

    def send_pixels(self, window, x, y, w, h):
        if w <= 0 or h <= 0:
            return
        if x == 0 and w == window.width:
            data = bytes(window.image[y * w * 4:(y + h) * w * 4])
        else:
            data = b''.join(bytes(window.image[((y + r) * window.width + x) * 4:((y + r) * window.width + x + w) * 4])
                            for r in range(h))
        self.host_send(WIN_PIXELS, window.id, struct.pack('<iiII', x, y, w, h) + data)

    def pointer_window(self):
        if self.pointer_focus is None or self.pointer_focus.xdg is None:
            return None
        return self.pointer_focus.xdg.window

    def client_resources(self, client, interface):
        return [r for r in list(client.objects.values()) if r.interface.name == interface and r.alive]

    def surface_local(self, window, x, y):
        gx, gy, _, _ = self.window_rect(window.xdg)
        return x + gx, y + gy

    def pointer_motion(self, window, x, y):
        surface = window.xdg.surface
        lx, ly = self.surface_local(window, x, y)
        client = surface.resource.client
        if self.pointer_focus is not surface:
            self.pointer_leave()
            self.pointer_focus = surface
            serial = self.next_serial()
            for pointer in self.client_resources(client, 'wl_pointer'):
                pointer.send('enter', serial, surface.resource, lx, ly)
                if pointer.version >= 5:
                    pointer.send('frame')
        for pointer in self.client_resources(client, 'wl_pointer'):
            pointer.send('motion', now_ms(), lx, ly)
            if pointer.version >= 5:
                pointer.send('frame')

    def pointer_leave(self):
        surface = self.pointer_focus
        if surface is None:
            return
        self.pointer_focus = None
        if not surface.resource.alive:
            return
        serial = self.next_serial()
        for pointer in self.client_resources(surface.resource.client, 'wl_pointer'):
            pointer.send('leave', serial, surface.resource)
            if pointer.version >= 5:
                pointer.send('frame')

    def dismiss_popups(self, keep=None):
        for popup in reversed(list(self.popups)):
            if keep is not None and self.popup_chain_contains(keep, popup):
                continue
            if popup.resource.alive:
                popup.resource.send('popup_done')
            self.unmap(popup.xdg)

    def popup_chain_contains(self, xdg, popup):
        node = xdg
        while node is not None:
            if node is popup.xdg:
                return True
            node = node.role.parent if isinstance(node.role, Popup) else None
        return False

    def pointer_button(self, window, button, pressed):
        if pressed and self.popups:
            if not window.popup:
                self.dismiss_popups()
        surface = self.pointer_focus
        if surface is None or not surface.resource.alive:
            return
        serial = self.next_serial()
        for pointer in self.client_resources(surface.resource.client, 'wl_pointer'):
            pointer.send('button', serial, now_ms(), button, 1 if pressed else 0)
            if pointer.version >= 5:
                pointer.send('frame')

    def pointer_axis(self, window, axis, delta):
        surface = self.pointer_focus
        if surface is None or not surface.resource.alive:
            return
        for pointer in self.client_resources(surface.resource.client, 'wl_pointer'):
            if pointer.version >= 5:
                pointer.send('axis_source', 0)
            if pointer.version >= 8:
                pointer.send('axis_value120', axis, delta)
            elif pointer.version >= 5:
                pointer.send('axis_discrete', axis, int(delta / 120) or (1 if delta > 0 else -1))
            pointer.send('axis', now_ms(), axis, delta * 10.0 / 120.0)
            if pointer.version >= 5:
                pointer.send('frame')

    def keyboard_target(self):
        for popup in reversed(self.popups):
            if popup.grab and popup.xdg.surface.resource.alive:
                return popup.xdg.surface
        if self.focused_window is not None:
            return self.focused_window.xdg.surface
        return None

    def keyboard_enter(self, keyboard, surface):
        keyboard.send('enter', self.next_serial(), surface.resource, b''.join(struct.pack('<I', k) for k in sorted(self.pressed)))
        depressed, latched, locked, group = self.keyboard.mods()
        keyboard.send('modifiers', self.next_serial(), depressed, latched, locked, group)

    def update_keyboard_focus(self):
        target = self.keyboard_target()
        if target is self.keyboard_focus:
            return
        old = self.keyboard_focus
        if old is not None and old.resource.alive:
            serial = self.next_serial()
            for keyboard in self.client_resources(old.resource.client, 'wl_keyboard'):
                keyboard.send('leave', serial, old.resource)
        self.keyboard_focus = target
        if target is not None:
            for keyboard in self.client_resources(target.resource.client, 'wl_keyboard'):
                self.keyboard_enter(keyboard, target)
            for device in self.client_resources(target.resource.client, 'wl_data_device'):
                self.offer_selection(device)

    def key(self, key, pressed):
        if pressed:
            self.pressed.add(key)
        else:
            self.pressed.discard(key)
        mods = self.keyboard.update(key, pressed)
        target = self.keyboard_focus
        if target is None or not target.resource.alive:
            return
        serial = self.next_serial()
        for keyboard in self.client_resources(target.resource.client, 'wl_keyboard'):
            keyboard.send('key', serial, now_ms(), key, 1 if pressed else 0)
            if mods is not None:
                keyboard.send('modifiers', serial, *mods)

    def clipboard_read(self, entry):
        _, fd, buffer, source = entry
        try:
            chunk = os.read(fd, 65536)
        except BlockingIOError:
            return
        except OSError:
            chunk = b''
        if chunk and len(buffer) < CLIPBOARD_LIMIT:
            buffer += chunk
            return
        self.selector.unregister(fd)
        os.close(fd)
        if self.selection is source and buffer:
            self.host_send(WIN_CLIPBOARD, 0, bytes(buffer[:CLIPBOARD_LIMIT]))

    def host_event(self, data):
        if len(data) < 16:
            return
        kind, size, wid, _ = struct.unpack_from('<IIII', data, 0)
        payload = data[16:size]
        if kind == WIN_CLIPBOARD_SET:
            self.set_selection(HostSource(bytes(payload)))
            return
        if kind == WIN_HOST_ATTACHED:
            log('window host attached')
            self.host = True
            return
        if kind == WIN_HOST_DETACHED:
            log('window host detached')
            self.host = False
            for window in list(self.windows.values()):
                window.xdg.window = None
            self.windows.clear()
            self.popups.clear()
            self.focused_window = None
            return
        if kind == WIN_HELLO:
            width, height, dpi, refresh = struct.unpack_from('<IIII', payload, 0)
            self.screen = (width, height)
            self.host = True
            log('window host ready, screen %dx%d' % (width, height))
            for client in list(self.clients):
                for output in self.client_resources(client, 'wl_output'):
                    self.send_output(output)
            for client in list(self.clients):
                for res in self.client_resources(client, 'xdg_surface'):
                    if res.data.configured and res.data.surface.buffer is not None and res.data.window is None:
                        self.map(res.data)
                        if res.data.window is not None:
                            self.draw_window(res.data.window, None)
            return
        window = self.windows.get(wid)
        if window is None:
            return
        role = window.xdg.role
        if kind == WIN_CONFIGURE and isinstance(role, Toplevel):
            width, height, states, _ = struct.unpack_from('<IIII', payload, 0)
            new_states = set()
            if states & CONF_MAXIMIZED:
                new_states.add(XDG_MAXIMIZED)
            if states & CONF_FULLSCREEN:
                new_states.add(XDG_FULLSCREEN)
            gx, gy, gw, gh = self.window_rect(window.xdg)
            if (width, height) != (gw, gh) or new_states != role.states:
                role.width, role.height = width, height
                role.states = new_states
                self.send_toplevel_configure(role)
        elif kind == WIN_CLOSE and isinstance(role, Toplevel):
            role.resource.send('close')
        elif kind == WIN_FOCUS:
            focused, = struct.unpack_from('<I', payload, 0)
            if focused:
                self.focused_window = window
            elif self.focused_window is window:
                self.focused_window = None
                self.dismiss_popups()
            if isinstance(role, Toplevel):
                self.send_toplevel_configure(role)
            self.update_keyboard_focus()
        elif kind == WIN_POINTER:
            x, y = struct.unpack_from('<ii', payload, 0)
            self.pointer_motion(window, x, y)
        elif kind == WIN_LEAVE:
            if self.pointer_focus is window.xdg.surface:
                self.pointer_leave()
        elif kind == WIN_BUTTON:
            button, pressed = struct.unpack_from('<II', payload, 0)
            self.pointer_button(window, button, pressed)
        elif kind == WIN_AXIS:
            axis, delta = struct.unpack_from('<Ii', payload, 0)
            self.pointer_axis(window, axis, delta)
        elif kind == WIN_KEY:
            key, pressed = struct.unpack_from('<II', payload, 0)
            self.update_keyboard_focus()
            self.key(key, pressed)

    def accept(self, listener):
        try:
            sock, _ = listener.accept()
        except BlockingIOError:
            return
        sock.setblocking(False)
        client = Client(self, sock)
        try:
            pid = struct.unpack('3i', sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[0]
        except OSError:
            pid = 0
        client.pid = pid
        log('client %d connected' % pid)
        self.clients[client] = True
        self.selector.register(sock, selectors.EVENT_READ, client)

    def run(self):
        try:
            self.device = os.open(DEVICE, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
        except OSError as e:
            log('cannot open %s (%s); another compositor owns this root' % (DEVICE, e.strerror))
            return
        os.makedirs(SOCKET_DIR, mode=0o755, exist_ok=True)
        os.makedirs('/run/user/0', mode=0o700, exist_ok=True)
        try:
            os.unlink(SOCKET_PATH)
        except FileNotFoundError:
            pass
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(SOCKET_PATH)
        os.chmod(SOCKET_PATH, 0o777)
        listener.listen(64)
        listener.setblocking(False)
        self.selector.register(listener, selectors.EVENT_READ, 'listener')
        self.selector.register(self.device, selectors.EVENT_READ, 'device')
        log('listening on', SOCKET_PATH)
        next_frame = time.monotonic()
        while True:
            timeout = max(0.0, next_frame - time.monotonic()) if self.frames else None
            for key, mask in self.selector.select(timeout):
                if key.data == 'listener':
                    self.accept(listener)
                elif isinstance(key.data, tuple):
                    self.clipboard_read(key.data)
                elif key.data == 'device':
                    while True:
                        try:
                            data = os.read(self.device, 2 * 1024 * 1024)
                        except BlockingIOError:
                            break
                        if not data:
                            break
                        self.host_event(data)
                else:
                    key.data.receive()
            if self.frames and time.monotonic() >= next_frame:
                stamp = now_ms()
                frames, self.frames = self.frames, []
                for callback in frames:
                    if callback.alive and not callback.client.closed:
                        callback.send('done', stamp)
                        callback.client.remove(callback)
                next_frame = time.monotonic() + 1 / 60
            for client in list(self.clients):
                client.flush()


if __name__ == '__main__':
    Server().run()
