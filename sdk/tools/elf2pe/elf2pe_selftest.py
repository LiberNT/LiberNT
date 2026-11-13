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


def build_import_elf(record=True):
    """An ELF image with two DLLs' import address table slots and thunks.

    The addresses are deliberately below the headers so that elf2pe has to
    shift the image, and the thunks are the real instruction sequence
    sdk/tools/def2elf emits, encoding the slot addresses the linker would have
    resolved.
    """
    # (page delta, low half) per thunk: .text sits on page 0, the first DLL's
    # slots on page 1 and the second DLL's a page further on, as the linker
    # would have encoded them.
    thunks = bytearray()
    for high, low in ((1, 0x000), (1, 0x008), (2, 0x000)):
        thunks += struct.pack('<III', 0x1A00000C | (high << 5),
                              0x28C0018C | (low << 10), 0x4C000180)

    foo = bytes(16)
    bar = bytes(8)
    metadata = b'foo.dll\0MessageBoxA\0CreateFileW\0\0bar.dll\0Baz\0\0'
    if not record:
        metadata = b''

    shstrtab = bytearray(b'\0')
    names = {}
    for name in ('.text', '.iat$foo.dll', '.iat$bar.dll', '.symtab', '.strtab',
                 '.shstrtab', '.libimports'):
        names[name] = len(shstrtab)
        shstrtab += name.encode() + b'\0'

    strtab = bytearray(b'\0')
    index_of = {}

    def intern(name):
        index_of[name] = len(strtab)
        strtab.extend(name.encode() + b'\0')

    for name in ('__imp_MessageBoxA', '__imp_CreateFileW', '__imp_Baz'):
        intern(name)

    # (name, info, other, section, value, size); a linked image keeps absolute
    # symbol values, so these are the section addresses plus the slot offset.
    symbols = [(0, 0, 0, 0, 0, 0)]
    for name, section, value in (('__imp_MessageBoxA', 2, 0x1000),
                                 ('__imp_CreateFileW', 2, 0x1008),
                                 ('__imp_Baz', 3, 0x2000)):
        symbols.append((index_of[name], (1 << 4) | 1, 2, section, value, 8))
    symtab = b''.join(struct.pack('<IBBHQQ', *entry) for entry in symbols)

    blobs = [bytes(thunks), foo, bar, symtab, bytes(strtab), bytes(shstrtab),
             metadata]
    alignment = [4, 8, 8, 8, 1, 1, 1]
    offsets = []
    offset = 64
    for blob, align in zip(blobs, alignment):
        offset = (offset + align - 1) & ~(align - 1)
        offsets.append(offset)
        offset += len(blob)
    section_offset = (offset + 7) & ~7

    # name, type, flags, address, link, info, alignment, entry size
    sections = [
        (names['.text'], SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x0, 0, 0, 4, 0),
        (names['.iat$foo.dll'], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x1000, 0, 0, 8, 0),
        (names['.iat$bar.dll'], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x2000, 0, 0, 8, 0),
        (names['.symtab'], SHT_SYMTAB, 0, 0, 5, 1, 8, 24),
        (names['.strtab'], SHT_STRTAB, 0, 0, 0, 0, 1, 0),
        (names['.shstrtab'], SHT_STRTAB, 0, 0, 0, 0, 1, 0),
        (names['.libimports'], SHT_PROGBITS, 0, 0, 0, 0, 1, 0),
    ]
    shdrs = struct.pack('<IIQQQQIIQQ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for index, section in enumerate(sections):
        name, kind, flags, address, link, info, align, entry_size = section
        shdrs += struct.pack('<IIQQQQIIQQ', name, kind, flags, address, offsets[index],
                             len(blobs[index]), link, info, align, entry_size)

    header = bytearray(64)
    header[0:4] = b'\x7fELF'
    header[4] = 2
    header[5] = 1
    header[6] = 1
    struct.pack_into('<HHIQQQIHHHHHH', header, 16, 2, 258, 1, 0, 0, section_offset,
                     0, 64, 0, 0, 64, len(sections) + 1, 6)
    image = bytearray(header)
    for blob, start in zip(blobs, offsets):
        if len(image) < start:
            image += bytes(start - len(image))
        image[start:start + len(blob)] = blob
    image += bytes(section_offset - len(image))
    image += shdrs
    return bytes(image)


def section_offset(image, rva):
    """Map an RVA of a section we know to be loaded to its file offset."""
    for address, virtual_size, raw_size, raw_offset in image['sections'].values():
        if address <= rva < address + max(virtual_size, raw_size):
            return raw_offset + rva - address
    raise SystemExit(f'FAIL: rva 0x{rva:x} is in no section')


def parse_imports(image):
    """Read the import directory back without sharing code with elf2pe."""
    data = image['data']

    def offset_of(rva):
        return section_offset(image, rva)

    def read_name(rva):
        start = offset_of(rva)
        return data[start:data.index(b'\0', start)].decode()

    libraries = []
    directory_rva, directory_size = image['import_directory']
    for index in range(directory_size // 20):
        original, stamp, chain, name, first = struct.unpack_from(
            '<IIIII', data, offset_of(directory_rva) + 20 * index)
        if not (original or name or first):
            break
        names = []
        entry = 0
        while True:
            value = struct.unpack_from('<Q', data, offset_of(original) + 8 * entry)[0]
            if value == 0:
                break
            if value >> 63:
                names.append(value & 0xFFFF)
            else:
                hint = struct.unpack_from('<H', data, offset_of(value))[0]
                names.append((read_name(value + 2), hint))
            entry += 1
        libraries.append({'name': read_name(name), 'lookup': original, 'address': first,
                          'entries': entry, 'symbols': names,
                          'ial': [struct.unpack_from('<Q', data, offset_of(first) + 8 * slot)[0]
                                  for slot in range(entry + 1)]})
    return libraries


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
        'import_directory': struct.unpack_from('<II', data, optional + 112 + 8),
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


def read_base_relocations(image, size):
    """Return [(page, offset, kind)] for every entry of the .reloc block."""
    data = image['data']
    start = image['sections']['.reloc'][3]
    entries = []
    cursor = 0
    while cursor < size:
        page, block = struct.unpack_from('<II', data, start + cursor)
        for index in range((block - 8) // 2):
            value = struct.unpack_from('<H', data, start + cursor + 8 + 2 * index)[0]
            if value >> 12 != 0:
                entries.append((page, value & 0xFFF, value >> 12))
        cursor += block
    return entries


def decode_thunk(data, address_of, rva):
    """Resolve the import address the pcalau12i/ld.d/jr sequence loads."""
    first, second, _ = struct.unpack_from('<III', data, address_of(rva))
    delta = (first >> 5) & 0xFFFFF
    if delta & (1 << 19):
        delta -= 1 << 20
    low = (second >> 10) & 0xFFF
    if low & (1 << 11):
        low -= 1 << 12
    return (rva & ~0xFFF) + (delta << 12) + low


def run_import_case():
    with tempfile.TemporaryDirectory() as directory:
        elf_path = os.path.join(directory, 'import.elf')
        pe_path = os.path.join(directory, 'import.efi')
        open(elf_path, 'wb').write(build_import_elf())
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'elf2pe.py')
        output = subprocess.run([sys.executable, tool, elf_path, pe_path],
                                capture_output=True, text=True)
        check(output.returncode == 0, f'elf2pe failed: {output.stderr.strip()}')

        image = load_pe(pe_path)
        data = image['data']

        def address_of(rva):
            return section_offset(image, rva)

        check(image['import_directory'][1] == 60, 'import directory does not hold two '
                                                  'descriptors and the null one')
        check(image['import_directory'][0] >= image['header_size'],
              'import directory overlaps the headers')
        libraries = parse_imports(image)

        check([library['name'] for library in libraries] == ['foo.dll', 'bar.dll'],
              f"imported libraries are {[x['name'] for x in libraries]}")
        foo, bar = libraries
        check(foo['symbols'] == [('MessageBoxA', 0), ('CreateFileW', 0)],
              f"imported names are {foo['symbols']}")
        check(bar['symbols'] == [('Baz', 0)], f"imported names are {bar['symbols']}")
        check(foo['address'] == 0x2000 and bar['address'] == 0x3000,
              'the import address tables are not the sections the image placed')

        # The loaders fill these in, so the image has to leave them empty and
        # has to point at the slot the thunk loads.
        for library in (foo, bar):
            check(not any(library['ial']), f"{library['name']}: the IAT is not empty")
            check(library['ial'][-1] == 0, f"{library['name']}: unterminated IAT")
        for rva, expected in ((0x1000, 0x2000), (0x100C, 0x2008), (0x1018, 0x3000)):
            check(decode_thunk(data, address_of, rva) == expected,
                  f'the thunk at 0x{rva:x} loads 0x{decode_thunk(data, address_of, rva):x}, '
                  f'not the slot at 0x{expected:x}')
        check(foo['lookup'] + 8 * 3 <= image['image_size'], 'lookup table past the image')
        check(bar['lookup'] >= foo['lookup'] + 8 * 3, 'lookup tables overlap')

        entries = read_base_relocations(image, image['base_relocation'][1])
        check(sorted(entries) == [(0x2000, 0x000, 10), (0x2000, 0x008, 10),
                                  (0x3000, 0x000, 10)],
              f'the import address table has fixups {entries}')
        print(f'  imports: {len(libraries)} DLLs, slots at '
              f'0x{foo["address"]:x}/0x{bar["address"]:x}, '
              f'{len(entries)} DIR64 fixups')


def run_missing_record_case():
    """Slots without the record that names their DLL must not pass silently."""
    with tempfile.TemporaryDirectory() as directory:
        elf_path = os.path.join(directory, 'stripped.elf')
        pe_path = os.path.join(directory, 'stripped.efi')
        open(elf_path, 'wb').write(build_import_elf(record=False))
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'elf2pe.py')
        output = subprocess.run([sys.executable, tool, elf_path, pe_path],
                                capture_output=True, text=True)
        check(output.returncode != 0, 'elf2pe accepted slots with no import record')
        check('.libimports' in output.stderr, f'wrong diagnostic: {output.stderr.strip()}')
        check(not os.path.exists(pe_path), 'elf2pe wrote a PE with no import directory')


def main():
    run_case(258, 0x6264)
    run_case(62, 0x8664, extra_relocations=[(DATA_ADDRESS + 8, R_X86_64_PC32, 0)], ignored=1)
    run_import_case()
    run_missing_record_case()
    print('elf2pe selftest: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
