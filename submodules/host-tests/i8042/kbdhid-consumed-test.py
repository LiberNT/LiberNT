# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Keyboard HID initializes the class callback consumed count
# COPYRIGHT:   Copyright 2026 LiberNT contributors
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
source = (pathlib.Path(args.root) / 'drivers/hid/kbdhid/kbdhid.c').read_text(encoding='latin-1')
match = re.search(r'(?ms)^VOID\nKbdHid_DispatchInputData\(.*?^\}\n', source)
if not match:
    raise RuntimeError('KbdHid_DispatchInputData not found')
fixture = r'''
#include <assert.h>
#include <stdio.h>
#define VOID void
#define IN
#define ASSERT assert
#define DISPATCH_LEVEL 2
typedef unsigned char KIRQL;
typedef unsigned int ULONG;
typedef struct { int code; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
typedef void (*PSERVICE_CALLBACK_ROUTINE)(void *, KEYBOARD_INPUT_DATA *, KEYBOARD_INPUT_DATA *, ULONG *);
typedef struct { void *ClassDeviceObject; PSERVICE_CALLBACK_ROUTINE ClassService; } KBDHID_DEVICE_EXTENSION, *PKBDHID_DEVICE_EXTENSION;
static void KeRaiseIrql(int level, KIRQL *old) { assert(level == 2); *old = 0; }
static void KeLowerIrql(KIRQL old) { assert(old == 0); }
''' + match.group(0) + r'''
static int calls;
static void callback(void *object, KEYBOARD_INPUT_DATA *start, KEYBOARD_INPUT_DATA *end, ULONG *count)
{
    assert(object && end == start + 1);
    calls++;
    if (*count != 0)
    {
        fprintf(stderr, "initial consumed count is %u, expected zero\n", *count);
        assert(0);
    }
    *count += 1;
}
int main(void)
{
    KEYBOARD_INPUT_DATA data = {1};
    KBDHID_DEVICE_EXTENSION device = {&data, callback};
    KbdHid_DispatchInputData(&device, &data);
    assert(calls == 1);
    device.ClassService = NULL;
    KbdHid_DispatchInputData(&device, &data);
    assert(calls == 1);
    puts("keyboard HID callback starts with a zero consumed count");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kbdhid-consumed-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(fixture)
    subprocess.run([args.cc, '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-ftrivial-auto-var-init=pattern', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
