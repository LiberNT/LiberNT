# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The debugger's break-in poll on a running system leaves the PC keyboard controller to its driver
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'ntos/kd/kdconsole.c').read_text()


def body(name):
    start = source.index('\n' + name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1:end - 1]


fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#define VOID void
#define TRUE 1
#define FALSE 0
#define ANSI_NULL ((CHAR)0)
#define CHECK(x) do { if (!(x)) { printf("%d: %s failed\n", __LINE__, #x); exit(1); } } while (0)
typedef unsigned char BOOLEAN, UCHAR;
typedef char CHAR;
typedef unsigned long ULONG, *PULONG;
static struct { BOOLEAN Net, Serial; } KdpDebugMode;
static BOOLEAN KdTermSerial, KdTermCrashMode;
static int KeyboardReads, SerialReads;
static CHAR SerialByte = (CHAR)-1, KeyboardByte = (CHAR)-1;
static BOOLEAN KdpNetPollBreakIn(void) { return FALSE; }
static CHAR KdbpTryGetCharSerial(ULONG Retry) { (void)Retry; SerialReads++; return SerialByte; }
/* Reading the controller's data port takes the byte from whoever was waiting for it */
static CHAR KdbpTryGetCharKeyboard(PULONG ScanCode, ULONG Retry) { (void)Retry; *ScanCode = 0; KeyboardReads++; return KeyboardByte; }
static CHAR KdpUsbKeyboardTryGetChar(PULONG ScanCode) { *ScanCode = 0; return (CHAR)-1; }
__attribute__((unused)) static CHAR KdpTryGetChar(PULONG ScanCode, ULONG Retry) { @TRYGET@ }
static BOOLEAN KdpTerminalPollBreakIn(void) { @POLL@ }
int main(void) {
    KeyboardByte = 0x03;
    CHECK(!KdpTerminalPollBreakIn());
    if (KeyboardReads != 0) { printf("the break-in poll read the keyboard controller %d times\n", KeyboardReads); return 1; }

    KdTermSerial = TRUE;
    KdpDebugMode.Serial = TRUE;
    SerialByte = 'a';
    CHECK(!KdpTerminalPollBreakIn());
    SerialByte = 0x03;
    CHECK(KdpTerminalPollBreakIn());
    CHECK(SerialReads == 2 && KeyboardReads == 0);
    return 0;
}
'''
fixture = fixture.replace('@TRYGET@', body('KdpTryGetChar')).replace('@POLL@', body('KdpTerminalPollBreakIn'))
with tempfile.TemporaryDirectory(prefix='kd-break-in-') as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
