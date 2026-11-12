#!/usr/bin/env python3
"""
PROJECT:     LiberNT Build
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Link a LoongArch64 image as ELF and assemble the PE32+ file
COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
"""

import os
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'elf2pe'))

from elf2pe import ElfImage, build_image


def classify(arguments):
    objects = []
    libraries = []
    library_dirs = []
    entry = None
    image_base = None
    subsystem = None

    for argument in arguments:
        if argument.endswith('.o') or argument.endswith('.obj'):
            objects.append(argument)
        elif argument.endswith('.a') or argument.endswith('.lib'):
            libraries.append(argument)
        elif argument.startswith('-l') and len(argument) > 2:
            libraries.append(argument)
        elif argument.startswith('-L') and len(argument) > 2:
            library_dirs.append(argument)
        elif argument.startswith('-Wl,--entry='):
            entry = argument.split('=', 1)[1]
        elif argument.startswith('-Wl,-e,'):
            entry = argument.split(',', 2)[2]
        elif argument.startswith('-Wl,--image-base='):
            image_base = int(argument.split('=', 1)[1], 0)
        elif argument.startswith('-Wl,--subsystem,'):
            subsystem = argument.split(',')[-1]

    return objects, libraries, library_dirs, entry, image_base, subsystem


def main(argv):
    kind = argv[0]
    target = argv[1]
    objects, libraries, library_dirs, entry, image_base, subsystem = classify(argv[2:])

    linker = os.environ.get('LOONGARCH64_LLD', 'ld.lld')
    elf_image = target + '.elf'
    command = [linker, '-m', 'elf64loongarch', '-o', elf_image]
    if entry:
        command += ['--entry', entry]
    if image_base is not None:
        command += ['--image-base', hex(image_base)]
    command += library_dirs + objects + libraries
    if kind != 'exe':
        command += ['-shared']
    subprocess.run(command, check=True)

    image, sections, shift, patched, skipped = build_image(
        ElfImage(open(elf_image, 'rb').read()),
        10,
        image_base if image_base is not None else 0,
        kind != 'exe')
    with open(target, 'wb') as output:
        output.write(image)

    print(f'{target}: {len(sections)} sections, entry 0x{shift:x} shift, '
          f'{patched} fixups, {skipped} other relocations')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
