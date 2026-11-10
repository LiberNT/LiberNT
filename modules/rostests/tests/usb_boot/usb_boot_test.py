#!/usr/bin/env python3
# PROJECT:     LiberNT USB boot tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Check USB controller startup during an AMD64 KVM boot
# COPYRIGHT:   Copyright 2026 LiberNT contributors

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--vars', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, default=Path('/usr/share/OVMF/OVMF_CODE_4M.fd'))
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--single', action='store_true')
    parser.add_argument('--usb-boot', action='store_true')
    parser.add_argument('--timeout', type=float, default=28)
    args = parser.parse_args()
    if not 0 < args.timeout <= 30:
        parser.error('--timeout must be greater than zero and no more than 30 seconds')
    if not args.image.is_file() or not args.vars.is_file() or not args.firmware.is_file():
        parser.error('image, firmware and firmware variables must be existing files')

    with tempfile.TemporaryDirectory(prefix='libernt-usb-boot-') as temporary:
        work = Path(temporary)
        vars_copy = work / 'vars.fd'
        serial = work / 'serial.log'
        stderr_path = work / 'qemu.log'
        shutil.copyfile(args.vars, vars_copy)
        command = [args.qemu, '-L', '/usr/share/qemu', '-machine', 'q35', '-accel', 'kvm',
                   '-cpu', 'host', '-smp', '4', '-m', '2048',
                   '-drive', f'if=pflash,format=raw,readonly=on,file={args.firmware.resolve()}',
                   '-drive', f'if=pflash,format=raw,file={vars_copy}', '-vga', 'std',
                   '-usb', '-device', 'usb-tablet', '-nic', 'none', '-display', 'none',
                   '-monitor', 'none', '-serial', f'file:{serial}',
                   '-drive', f'if=none,id=systemdisk,format=vpc,file={args.image.resolve()}',
                   '-device', 'usb-storage,drive=systemdisk' if args.usb_boot else 'ide-hd,drive=systemdisk',
                   '-snapshot', '-no-reboot']
        data = ''
        with stderr_path.open('wb') as stderr:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                       stdout=subprocess.DEVNULL, stderr=stderr)
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    if serial.exists():
                        data = serial.read_text(errors='replace')
                    if ('root hub initialization did not complete' in data or
                        '*** Fatal System Error' in data or process.poll() is not None):
                        break
                    display_ready = 'DISPLAY_BRIDGE:' in data and 'desktop ready' in data
                    usb_volume_ready = ('Product: QEMU USB HARDDRIVE' in data and
                                        'KD: Crash dump target initialized' in data)
                    if ('Product: QEMU USB Tablet' in data and
                        (usb_volume_ready if args.usb_boot else display_ready) and
                        'KeStartAllProcessors: started ' in data):
                        break
                    time.sleep(0.2)
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if serial.exists():
            data = serial.read_text(errors='replace')
        errors = []
        count = re.search(r'KeStartAllProcessors: started (\d+) processor\(s\)', data)
        if not count:
            errors.append('kernel processor-start result was not observed')
        elif args.single and (int(count.group(1)) != 0 or 'NUMPROC=1' not in data):
            errors.append('the boot did not apply NUMPROC=1')
        elif not args.single and int(count.group(1)) < 1:
            errors.append('the SMP boot did not start a secondary processor')
        if 'root hub initialization did not complete' in data:
            errors.append('a root hub initialization timed out')
        if 'Product: QEMU USB Tablet' not in data:
            errors.append('the USB tablet was not enumerated')
        if args.usb_boot:
            if ('Product: QEMU USB HARDDRIVE' not in data or
                'KD: Crash dump target initialized' not in data):
                errors.append('the USB boot volume did not reach crash-dump initialization')
        elif 'DISPLAY_BRIDGE:' not in data or 'desktop ready' not in data:
            errors.append('desktop display initialization was not observed')
        if '*** Fatal System Error' in data:
            errors.append('the guest reported a fatal system error')
        if errors:
            print('\n'.join(errors), file=sys.stderr)
            print('\n'.join(data.splitlines()[-30:]), file=sys.stderr)
            if stderr_path.exists():
                print(stderr_path.read_text(errors='replace'), file=sys.stderr)
            return 1
        milestone = 'USB boot volume initialized' if args.usb_boot else 'desktop display initialized'
        print(f"PASS: {'one processor' if args.single else 'SMP'}, {milestone}, USB tablet enumerated")
        return 0


if __name__ == '__main__':
    sys.exit(main())
