#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Run the receive interrupt handlers against a single burst of completions
# COPYRIGHT:   Copyright 2026 The LiberNT contributors

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'^' + name + r'\(.*?^\}', source, re.M | re.S)
    if match is None:
        raise ValueError(f'Cannot locate {name}')
    return match.group()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('driver', choices=('vmxnet3', 'nvnet'))
    args = parser.parse_args()
    folder = args.root / 'drivers/network/dd' / args.driver
    if args.driver == 'vmxnet3':
        source = (folder / 'vmx3.c').read_text()
        header = (folder / 'vmx3.h').read_text()
        routines = [('void', 'Vmx3InterruptDpc')]
        defines = ('VMX3_',)
    else:
        source = (folder / 'interrupt.c').read_text()
        header = (folder / 'nic.h').read_text() + (folder / 'nvnet.h').read_text()
        routines = [('ULONG', 'ProcessReceiveDescriptors'), ('void', 'MiniportInterruptDpc')]
        defines = ('NV_RX', 'FLAG_MASK_', 'LEN_MASK_', 'NVREG_IRQ_', 'NVNET_RECEIVE_')

    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        constants = []
        # Use the non-Xbox ring configuration.
        header = re.sub(r'#if defined\(SARCH_XBOX\).*?#else', '', header, flags=re.S)
        header = header.replace('\\\n', ' ')
        for macro in re.finditer(r'^#define[ \t]+(\w+)[ \t]+.+$', header, re.M):
            if macro[1].startswith(defines):
                constants.append(macro[0])
        (work / 'rx-constants.h').write_text('\n'.join(constants))
        (work / 'rx-routines.h').write_text('\n'.join(
            ret + '\n' + function(source, name) for ret, name in routines))
        binary = work / 'test.exe'
        fixture = Path(__file__).with_name(args.driver + '-rx-burst.c')
        subprocess.run([args.cc, '-std=c11', '-I', str(work), str(fixture),
                        '-o', str(binary)], check=True)
        return subprocess.run([str(binary)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
