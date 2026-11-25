#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     A boot CD that starts from boot.wim keeps its boot files on the medium and packs the system into the WIM
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cmake', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
script = root / 'image/bcd/medialist.cmake'
images = (root / 'image/images.cmake').read_text()

failures = 0


def check(condition, what):
    global failures
    if not condition:
        failures += 1
        print('FAIL: ' + what)


keep = re.search(r'^add_custom_target\(bootcd\n.*?^    VERBATIM\)', images, re.M | re.S)
check(keep is not None and '${_bootcd_wim_command}' in keep.group(0), 'the bootcd target runs the WIM packing step')
keep = re.search(r'"-DKEEP=([^"]+)"', images)
check(keep is not None, 'image/images.cmake passes the files bootcd keeps on the medium')
if keep is None:
    print('FAIL: %d failure(s)' % failures)
    sys.exit(1)
keep = keep.group(1)

BOOT = ['loader/efisys.bin', 'loader/media.bcd', 'efi/boot/bootx64.efi', '/bootmgr', 'boot/etfsboot.com',
        'boot/bcd', 'boot/winload.exe', '/setup.exe']
ROOT = ['/autorun.inf', '/icon.ico', '/readme.txt']
SYSTEM = ['Windows/system32/ntoskrnl.exe', 'Windows/system32/config/BCD-Template', 'Windows/Boot/PCAT/bootmgr',
          'Program Files/Common Files/a.dll', 'ProgramData/b.txt', 'amd64/txtsetup.sif', 'Bootstrap/c.txt',
          'loaders.txt']
DIRECTORIES = ['Users/Public/Desktop', 'Windows/TEMP']

FAKE = '''#!%s
import os
import sys
arguments = sys.argv[1:]
if os.environ.get('FAKE_WIMAGE_FAIL'):
    sys.stderr.write('capture refused\\n')
    sys.exit(1)
if arguments[0] != 'capture' or arguments[3] != 'LiberNT' or '--boot' not in arguments or '--compress=xpress' not in arguments:
    sys.stderr.write('unexpected arguments %%r\\n' %% arguments)
    sys.exit(2)
stage, wim = arguments[1], arguments[2]
lines = []
for base, directories, files in os.walk(stage):
    relative = os.path.relpath(base, stage).replace(os.sep, '/')
    prefix = '' if relative == '.' else relative + '/'
    if not directories and not files and prefix:
        lines.append('D ' + prefix[:-1])
    for name in files:
        with open(os.path.join(base, name)) as source:
            lines.append('F %%s %%s' %% (prefix + name, source.read()))
with open(wim, 'w') as output:
    output.write('\\n'.join(sorted(lines)) + '\\n')
''' % sys.executable


def run(work, name, entries, options, environment=None, pack=True):
    listing = work / (name + '.lst')
    listing.write_text('\n'.join(entries) + '\n')
    command = [args.cmake, '-DLIST=' + str(listing)]
    if pack:
        command += ['-DWIM=' + str(work / (name + '.wim')), '-DSTAGE=' + str(work / (name + '.stage')),
                    '-DWIMAGE=' + str(work / 'wimage')]
    result = subprocess.run(command + options + ['-P', str(script)], capture_output=True, text=True,
                            env=dict(os.environ, **(environment or {})))
    return result, listing.read_text().splitlines()


with tempfile.TemporaryDirectory() as temporary:
    work = pathlib.Path(temporary)
    source = work / 'source'
    empty = source / 'empty'
    empty.mkdir(parents=True)
    fake = work / 'wimage'
    fake.write_text(FAKE)
    fake.chmod(0o755)

    def entry(path):
        host = source / path.strip('/').replace('/', '_')
        host.write_text(path)
        return '%s=%s' % (path, host)

    entries = [str(empty)]
    entries += [entry(path) for path in SYSTEM[:4]]
    entries += ['%s=%s' % (path, empty) for path in DIRECTORIES]
    entries += [entry(path) for path in BOOT + ROOT]
    entries += [entry(path) for path in SYSTEM[4:]]
    entries += [str(empty)]
    flat = {line.split('=', 1)[0]: line for line in entries if '=' in line}

    result, output = run(work, 'bootcd', entries, ['-DKEEP=' + keep])
    check(result.returncode == 0, 'the boot CD list is rewritten: ' + result.stderr.strip())
    check(output[:1] == [str(empty)], 'the list still starts with the empty directory the boot catalog is placed in')
    for path in BOOT + ROOT:
        check(output.count(flat[path]) == 1, path + ' stays on the medium as it was listed')
    for path in SYSTEM + DIRECTORIES:
        check(not any(line.startswith(path + '=') for line in output), path + ' is no longer listed on the medium')
    wim = work / 'bootcd.wim'
    check(output.count('boot.wim=' + str(wim)) == 1, 'boot.wim is listed once at the root of the medium')
    check(not any(line.startswith('efi/efisys.bin=') for line in output), 'no second EFI boot image is added')
    check(len(output) == 2 + len(BOOT) + len(ROOT) + 1, 'the medium lists only its boot files and boot.wim: %r' % output)
    packed = wim.read_text().splitlines() if wim.exists() else []
    expected = sorted(['F %s %s' % (path, path) for path in SYSTEM] + ['D ' + path for path in DIRECTORIES])
    check(packed == expected, 'boot.wim holds the system files and empty directories, and nothing else: %r' % packed)
    check(not (work / 'bootcd.stage').exists(), 'the staging directory is removed after the capture')

    result, output = run(work, 'failed', entries, ['-DKEEP=' + keep], {'FAKE_WIMAGE_FAIL': '1'})
    check(result.returncode != 0 and 'capture refused' in result.stderr, 'a failed capture stops the build')
    check(not any(line.startswith('boot.wim=') for line in output), 'a failed capture leaves the list without boot.wim')

    efisys = source / 'media-efisys.bin'
    efisys.write_text('efisys')
    bios = ['bootmgr=%s' % (source / 'bootmgr'), 'boot/bcd=%s' % (source / 'boot_bcd')]
    result, output = run(work, 'media', entries, ['-DEFISYS=' + str(efisys), '-DBIOSFILES=' + ';'.join(bios)])
    check(result.returncode == 0, 'the live medium list is rewritten: ' + result.stderr.strip())
    check(not any(line.startswith(('loader/', 'efi/boot/')) for line in output),
          'a live medium drops the boot files of the list it starts from')
    check(output[-3:] == ['efi/efisys.bin=' + str(efisys)] + bios, 'a live medium ends with its own boot files')
    check(output.count('boot.wim=' + str(work / 'media.wim')) == 1, 'a live medium lists boot.wim once')
    for path in ROOT:
        check(output.count(flat[path]) == 1, path + ' stays at the root of a live medium')

    result, output = run(work, 'plain', entries, ['-DEFISYS=' + str(efisys)], pack=False)
    check(result.returncode == 0 and not (work / 'plain.wim').exists(), 'a medium without a WIM captures nothing')
    check(all(flat[path] in output for path in SYSTEM + ROOT), 'a medium without a WIM keeps the system files listed')

print('%s: %d failure(s)' % ('FAIL' if failures else 'PASS', failures))
sys.exit(failures != 0)
