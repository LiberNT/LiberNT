#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production USBCCGP function select-configuration handler with a simulated lower stack."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--revision', help='Git revision of pdo.c to test')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[3]
name = 'drivers/usb/usbccgp/pdo.c'
source = (subprocess.check_output(['git', 'show', f'{args.revision}:{name}'], cwd=root, text=True)
          if args.revision else (root/name).read_text())
start = source.index('NTSTATUS\nUSBCCGP_PDOSelectConfiguration(')
end = source.index('\nNTSTATUS\nPDO_HandleInternalDeviceControl(', start)
with tempfile.TemporaryDirectory(prefix='usbccgp-select-') as directory:
    work = Path(directory)
    (work/'select_configuration.h').write_text(source[start:end])
    executable = work/'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(work), str(here/'select_configuration_host.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
