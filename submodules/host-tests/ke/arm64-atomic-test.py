#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ARM64 128-bit atomic regression tests
# COPYRIGHT:   Copyright 2026 LiberNT contributors
"""Exercise the ARM64 128-bit atomic helpers under forced interleavings."""
import argparse
import pathlib
import re
import socket
import struct
import subprocess
import tempfile
import time


def function(source, name):
    match = re.search(r'(?m)^(?:__uint128_t|_Bool)\s*\n' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise RuntimeError('function not found: ' + name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def label_load(body, label):
    match = re.search(r'"ldaxp [^"]+"', body)
    if not match:
        raise RuntimeError('exclusive load not found')
    return body[:match.end()-1] + r'\n\t.globl ' + label + r'\n\t' + label + r':\n\t' + body[match.end()-1:]


def retry_failures(body):
    match = re.search(r'(?m)^(\s*)if \(Status (?:==|!=) 0\)', body)
    if not match:
        raise RuntimeError('exclusive store status check not found')
    injection = '    if (TestFailuresRemaining) { --TestFailuresRemaining; Status = 1; }\n'
    return body[:match.start()] + '\n' + injection + body[match.start():]


FIXTURE = r'''
typedef unsigned long long ULONGLONG;
typedef unsigned int ULONG;
typedef void VOID;
#define _In_
#define _Inout_
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define DPRINT1(...) ((void)0)
static ULONG TestFailuresRemaining;
@LOAD@
@CAS@
__attribute__((aligned(16),visibility("default"))) volatile __uint128_t TestCell = ((__uint128_t)0x2222222222222222ULL << 64) | 0x1111111111111111ULL;
static int test(void)
{
    const __uint128_t A = ((__uint128_t)0x2222222222222222ULL << 64) | 0x1111111111111111ULL;
    const __uint128_t B = ((__uint128_t)0x4444444444444444ULL << 64) | 0x3333333333333333ULL;
#ifdef TEST_LOAD
    __uint128_t Value = __atomic_load_16(&TestCell, 0);
    return Value == A || Value == B ? 0 : 1;
#elif defined(TEST_RETRY)
    __uint128_t Expected = A;
    TestFailuresRemaining = 1 << 20;
    return __atomic_compare_exchange_16(&TestCell, &Expected, A, 0, 0, 0) && Expected == A && !TestFailuresRemaining ? 0 : 1;
#elif defined(TEST_NORMAL)
    __uint128_t Expected = A;
    if (!__atomic_compare_exchange_16(&TestCell, &Expected, B, 0, 0, 0) || Expected != A || TestCell != B)
        return 1;
    Expected = A;
    return !__atomic_compare_exchange_16(&TestCell, &Expected, A, 0, 0, 0) && Expected == B && TestCell == B ? 0 : 1;
#else
    __uint128_t Expected = A;
    _Bool Success = __atomic_compare_exchange_16(&TestCell, &Expected, B, 0, 0, 0);
    return TestCell == B && ((Success && Expected == A) || (!Success && Expected == B)) ? 0 : 1;
#endif
}
void _start(void)
{
    long Result = test();
    __asm__ __volatile__("mov x0, %0\n\tmov x8, #93\n\tsvc #0" :: "r"(Result) : "x8", "memory");
    __builtin_unreachable();
}
'''


def force_torn(qemu, nm_tool, objdump, exe, mode):
    label = 'test_load_after' if mode == 'LOAD' else 'test_cas_after'
    symbols = subprocess.check_output([nm_tool, '-n', str(exe)], text=True)
    def symbol(name):
        return int(next(line.split()[0] for line in symbols.splitlines() if line.endswith(' ' + name)), 16)
    address, cell = symbol(label), symbol('TestCell')
    dis = subprocess.check_output([objdump, '-d', str(exe)], text=True).splitlines()
    index = next(i for i, line in enumerate(dis) if '<' + label + '>:' in line)
    previous = next(line for line in reversed(dis[:index]) if 'ldaxp' in line)
    match = re.search(r'ldaxp\s+x(\d+),\s*x(\d+),', previous)
    if not match:
        raise RuntimeError('cannot locate pair registers: ' + previous)
    high_register = int(match.group(2))
    probe = socket.socket()
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
    probe.close()
    process = subprocess.Popen([qemu, '-g', str(port), str(exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    sock = None
    try:
        for _ in range(100):
            sock = socket.socket()
            sock.settimeout(8)
            try:
                sock.connect(('127.0.0.1', port))
                break
            except OSError:
                sock.close()
                sock = None
                time.sleep(.03)
        else:
            raise RuntimeError('qemu debug stub did not start')
        def packet(command):
            payload = command.encode('ascii')
            sock.sendall(b'$' + payload + b'#' + ('%02x' % (sum(payload) & 255)).encode())
            while True:
                byte = sock.recv(1)
                if not byte:
                    raise RuntimeError('debug stub closed')
                if byte == b'$':
                    break
            data = bytearray()
            while True:
                b = sock.recv(1)
                if b == b'#':
                    break
                if not b:
                    raise RuntimeError('debug stub closed')
                data += b
            if int(sock.recv(2), 16) != sum(data) & 255:
                raise RuntimeError('bad debug checksum')
            sock.sendall(b'+')
            return data.decode('ascii')
        def require(condition):
            if not condition:
                raise RuntimeError('unexpected debug protocol response')
        require(packet('?').startswith(('S', 'T')))
        require(packet('Z0,%x,4' % address) == 'OK')
        require(packet('c').startswith(('S', 'T')))
        data = struct.pack('<QQ', 0x3333333333333333, 0x4444444444444444).hex()
        require(packet('M%x,10:%s' % (cell, data)) == 'OK')
        require(packet('P%x=%s' % (high_register, struct.pack('<Q', 0x4444444444444444).hex())) == 'OK')
        require(packet('z0,%x,4' % address) == 'OK')
        result = packet('c')
        if not result.startswith('W'):
            raise RuntimeError('unexpected exit: ' + result)
        code = int(result[1:3], 16)
        process.wait(timeout=5)
        if code:
            raise RuntimeError(mode + ' returned a torn observation')
    finally:
        if sock is not None:
            sock.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser()
    for option in ('source', 'clang', 'ld', 'qemu', 'nm', 'objdump'):
        parser.add_argument('--' + option, required=True)
    parser.add_argument('--mode', choices=('LOAD', 'CAS', 'RETRY', 'NORMAL'))
    args = parser.parse_args()
    source = pathlib.Path(args.source).read_text()
    load = function(source, '__atomic_load_16')
    cas = function(source, '__atomic_compare_exchange_16')
    with tempfile.TemporaryDirectory(prefix='ke-arm64-atomic-') as directory:
        root = pathlib.Path(directory)
        for mode in ((args.mode,) if args.mode else ('LOAD', 'CAS', 'RETRY', 'NORMAL')):
            load_body = label_load(load, 'test_load_after') if mode == 'LOAD' else load
            cas_body = label_load(cas, 'test_cas_after') if mode == 'CAS' else cas
            if mode == 'RETRY':
                cas_body = retry_failures(cas_body)
            fixture = root / (mode + '.c')
            fixture.write_text(FIXTURE.replace('@LOAD@', load_body).replace('@CAS@', cas_body))
            exe = root / mode
            subprocess.run([args.clang, '--target=aarch64-linux-gnu', '--ld-path=' + args.ld, '-nostdlib', '-static', '-fno-builtin', '-fno-inline', '-O1', '-Wl,-e,_start', '-DTEST_' + mode, str(fixture), '-o', str(exe)], check=True)
            subprocess.run([args.qemu, str(exe)], check=True, timeout=30)
            if mode in ('LOAD', 'CAS'):
                force_torn(args.qemu, args.nm, args.objdump, exe, mode)
            print(mode + ' passed')


if __name__ == '__main__':
    main()
