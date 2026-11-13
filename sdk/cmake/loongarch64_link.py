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
        elif argument.startswith('-Wl,'):
            # CMake emits "-Wl,--image-base,0x9000000000000000" and
            # "-Wl,-entry,KiSystemStartup"; the value is a separate word.
            words = argument[4:].split(',')
            index = 0
            while index < len(words):
                word = words[index]
                name = None
                value = None
                for known in ('--image-base', '--entry', '-entry', '-e', '--subsystem'):
                    if word == known and index + 1 < len(words):
                        name, value = known, words[index + 1]
                        index += 1
                        break
                    if word.startswith(known + '='):
                        name, value = known, word.split('=', 1)[1]
                        break
                if name == '--image-base':
                    image_base = int(value, 0)
                elif name in ('--entry', '-entry', '-e'):
                    entry = value
                elif name == '--subsystem':
                    subsystem = value
                index += 1

    return objects, libraries, library_dirs, entry, image_base, subsystem


SUBSYSTEMS = {
    'native': 1,
    'windows': 2,
    'console': 3,
    'posix': 7,
    'efi_application': 10,
    'efi_boot_service_driver': 11,
    'efi_runtime_driver': 12,
    'efi_rom': 13,
}


def subsystem_number(text):
    if not text:
        return 10
    name = text.split(':')[0].strip().lower()
    return SUBSYSTEMS.get(name, 10)


def main(argv):
    kind = argv[0]
    target = argv[1]
    objects, libraries, library_dirs, entry, image_base, subsystem = classify(argv[2:])

    linker = os.environ.get('LOONGARCH64_LLD', 'ld.lld')
    elf_image = target + '.elf'
    # --emit-relocs feeds elf2pe's base-relocation directory; a loaded image
    # cannot be rebased without it. The ELF stays at 0 because PE RVAs and the
    # entry point are offsets from ImageBase, which elf2pe folds in itself.
    command = [linker, '-m', 'elf64loongarch', '--emit-relocs', '-o', elf_image]
    if entry:
        command += ['--entry', entry]
    command += library_dirs + objects + libraries
    subprocess.run(command, check=True)

    elf = ElfImage(open(elf_image, 'rb').read())
    image, sections, shift, patched, skipped = build_image(
        elf,
        subsystem_number(subsystem),
        image_base if image_base is not None else 0,
        kind != 'exe')
    with open(target, 'wb') as output:
        output.write(image)

    print(f'{target}: {len(sections)} sections, image base 0x{image_base or 0:x}, '
          f'entry rva 0x{elf.entry + shift:x}, '
          f'{patched} fixups, {skipped} other relocations')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
