"""
PROJECT:     LiberNT SDK tools
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Check elf2pe against hand-built ELF images
COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
"""

import os
import struct
import subprocess
import sys
import tempfile

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

R_LARCH_64 = 2
R_X86_64_PC32 = 2
R_X86_64_64 = 1

TEXT_ADDRESS = 0x0
DATA_ADDRESS = 0x1000
DATA_SIZE = 16
POINTER_VALUE = DATA_ADDRESS + 8


def build_elf(machine, extra_relocations=()):
    text = bytes([0x03, 0x00, 0x00, 0x00]) * 4
    data = bytearray(DATA_SIZE)
    struct.pack_into('<Q', data, 0, POINTER_VALUE)

    strtab = b'\0'
    shstrtab = b'\0'
    names = {}

    def add_name(name):
        nonlocal shstrtab
        names[name] = len(shstrtab)
        shstrtab += name.encode() + b'\0'

    for name in ('.text', '.data', '.symtab', '.strtab', '.shstrtab', '.rela.data'):
        add_name(name)

    symbols = struct.pack('<IBBHQQ', 0, 0, 0, 0, 0, 0)
    symbols += struct.pack('<IBBHQQ', 0, 3, 0, 2, DATA_ADDRESS, 0)

    relocations = struct.pack('<QQq', DATA_ADDRESS, (1 << 32) | R_LARCH_64, 8) \
        if machine == 258 else struct.pack('<QQq', DATA_ADDRESS, (1 << 32) | R_X86_64_64, 8)
    for address, kind, symbol in extra_relocations:
        relocations += struct.pack('<QQq', address, (symbol << 32) | kind, 0)

    blobs = [text, bytes(data), symbols, strtab, shstrtab, relocations]
    alignment = [16, 16, 8, 1, 1, 8]
    offsets = []
    offset = 64
    for blob, align in zip(blobs, alignment):
        offset = (offset + align - 1) & ~(align - 1)
        offsets.append(offset)
        offset += len(blob)
    section_offset = (offset + 7) & ~7

    shdrs = struct.pack('<IIQQQQIIQQ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    sections = [
        (names['.text'], SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, TEXT_ADDRESS, 0, 0, 16, 0),
        (names['.data'], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, DATA_ADDRESS, 0, 0, 16, 0),
        (names['.symtab'], SHT_SYMTAB, 0, 0, 3, 1, 8, 24),
        (names['.strtab'], SHT_STRTAB, 0, 0, 0, 0, 1, 0),
        (names['.shstrtab'], SHT_STRTAB, 0, 0, 0, 0, 1, 0),
        (names['.rela.data'], SHT_RELA, 0, 0, 2, 2, 8, 24),
    ]
    for index, (name, kind, flags, address, link, info, alignment, entry_size) in enumerate(sections):
        shdrs += struct.pack('<IIQQQQIIQQ', name, kind, flags, address, offsets[index],
                             len(blobs[index]), link, info, alignment, entry_size)

    header = bytearray(64)
    header[0:4] = b'\x7fELF'
    header[4] = 2
    header[5] = 1
    header[6] = 1
    struct.pack_into('<HHIQQQIHHHHHH', header, 16, 2, machine, 1, TEXT_ADDRESS, 0,
                     section_offset, 0, 64, 0, 0, 64, 7, 5)

    image = bytearray(header)
    for blob, start in zip(blobs, offsets):
        if len(image) < start:
            image += bytes(start - len(image))
        image[start:start + len(blob)] = blob
    image += bytes(section_offset - len(image))
    image += shdrs
    return bytes(image)


def load_pe(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    result = {
        'data': data,
        'machine': machine,
        'entry': struct.unpack_from('<I', data, optional + 16)[0],
        'image_size': struct.unpack_from('<I', data, optional + 56)[0],
        'header_size': struct.unpack_from('<I', data, optional + 60)[0],
        'section_alignment': struct.unpack_from('<I', data, optional + 32)[0],
        'base_relocation': struct.unpack_from('<II', data, optional + 112 + 40),
        'sections': {},
    }
    if struct.unpack_from('<H', data, optional)[0] != 0x20B:
        raise SystemExit('not a PE32+ image')
    table = optional + optional_size
    for index in range(count):
        name, virtual_size, address, raw_size, raw_offset = struct.unpack_from(
            '<8sIIII', data, table + 40 * index)
        result['sections'][name.rstrip(b'\0').decode()] = (address, virtual_size, raw_size, raw_offset)
    return result


def check(condition, message):
    if not condition:
        raise SystemExit(f'FAIL: {message}')


def run_case(machine, expected_machine, extra_relocations=(), ignored=0):
    with tempfile.TemporaryDirectory() as directory:
        elf_path = os.path.join(directory, 'input.elf')
        pe_path = os.path.join(directory, 'output.efi')
        open(elf_path, 'wb').write(build_elf(machine, extra_relocations))
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'elf2pe.py')
        output = subprocess.run([sys.executable, tool, elf_path, pe_path],
                                capture_output=True, text=True)
        check(output.returncode == 0, f'elf2pe failed: {output.stderr.strip()}')
        if ignored:
            check(f'{ignored} other relocations' in output.stdout,
                  f'machine {machine}: expected {ignored} ignored relocations')

        image = load_pe(pe_path)
        check(image['machine'] == expected_machine, 'wrong COFF machine')
        check(image['entry'] == 0x1000, 'entry point not shifted with the image')
        check(image['section_alignment'] == 0x1000, 'wrong section alignment')
        check('.text' in image['sections'] and '.data' in image['sections'], 'missing section')
        text_address = image['sections']['.text'][0]
        data_address = image['sections']['.data'][0]
        check(text_address == 0x1000, 'text not shifted past the headers')
        check(data_address == 0x2000, 'data not shifted with the image')
        check(text_address >= image['header_size'], 'text overlaps the headers')

        stored = struct.unpack_from('<Q', image['data'],
                                    image['sections']['.data'][3])[0]
        check(stored == 0x2008, f'pointer not relocated: 0x{stored:x}')

        relocation_address, relocation_size = image['base_relocation']
        check(relocation_size > 0, 'no base relocation directory')
        page, block_size = struct.unpack_from('<II', image['data'],
                                              image['sections']['.reloc'][3])
        check(block_size >= 8 and block_size % 4 == 0, 'malformed relocation block')
        entry = struct.unpack_from('<H', image['data'], image['sections']['.reloc'][3] + 8)[0]
        check(entry >> 12 == 10, 'pointer did not become a DIR64 fixup')
        check((page | (entry & 0xFFF)) == 0x2000, 'fixup address is wrong')


def main():
    run_case(258, 0x6264)
    run_case(62, 0x8664, extra_relocations=[(DATA_ADDRESS + 8, R_X86_64_PC32, 0)], ignored=1)
    print('elf2pe selftest: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
