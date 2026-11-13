"""
PROJECT:     LiberNT SDK tools
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Check def2elf against a real assembler, link and PE writer
COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

TOOL_NAME = 'def2elf.py'
TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), TOOL_NAME)
ELF2PE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'elf2pe',
                      'elf2pe.py')

EM_LOONGARCH = 258
EF_LOONGARCH_ABI_LP64 = 0x40
EF_LOONGARCH_ABI_DOUBLE_FLOAT = 0x3

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_SYMTAB = 2
SHT_RELA = 4
SHN_UNDEF = 0
STT_OBJECT = 1
STT_FUNC = 2
STV_HIDDEN = 2
R_LARCH_PCALA_HI20 = 71
R_LARCH_PCALA_LO12 = 72

SLOT_SIZE = 8
THUNK_SIZE = 12
T0 = 12
IMAGE_BASE = 0x140000000

DESCRIPTION = """\
; File generated automatically, do not edit!

NAME foo.dll

EXPORTS
 MessageBoxA
 CreateFileW=api.CreateFileW
 Ordinal @7
 NoName NONAME @8
 Data1 DATA
 Constant1 CONSTANT
"""

NAMES = ['MessageBoxA', 'CreateFileW', 'Ordinal', 'NoName']

REFERENCE = """\
    .text
    .globl MessageBoxA
    .type MessageBoxA,@function
MessageBoxA:
    pcalau12i $t0, %pc_hi20(__imp_MessageBoxA)
    ld.d $t0, $t0, %pc_lo12(__imp_MessageBoxA)
    jr $t0
"""

CALLER = """\
extern void MessageBoxA(void);
extern void CreateFileW(void);

/* The image defines a symbol the DLL also exports, like ntoskrnl defines
   ExAcquireFastMutex while hal.spec exports it. Pulling one import must not
   drag this one in and collide with it. */
void Ordinal(void)
{
    for (;;)
        ;
}

void caller(void)
{
    MessageBoxA();
    CreateFileW();
}
"""


def check(condition, message):
    if not condition:
        raise SystemExit(f'FAIL: {message}')


def sign_extend(value, bits):
    if value & (1 << (bits - 1)):
        return value - (1 << bits)
    return value


class Image:
    """Just enough ELF64 to inspect what def2elf writes."""

    def __init__(self, data):
        check(data[:4] == b'\x7fELF' and data[4] == 2, 'not an ELF64 file')
        (self.type, self.machine, _, self.entry, _, self.section_offset, self.flags,
         _, _, _, self.section_size,
         self.section_count, self.string_section) = struct.unpack_from(
            '<HHIQQQIHHHHHH', data, 16)
        self.data = data
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', data,
                                            self.section_offset + index * self.section_size)
                         for index in range(self.section_count)]

    def names(self):
        return [self.name_of(section) for section in self.sections]

    def name_of(self, section):
        start = self.sections[self.string_section][4] + section[0]
        return self.data[start:self.data.index(b'\0', start)].decode()

    def section(self, name):
        for section in self.sections:
            if self.name_of(section) == name:
                return section
        raise SystemExit(f'FAIL: no {name} section')

    def contents(self, name):
        section = self.section(name)
        return self.data[section[4]:section[4] + section[5]]

    def symbols(self):
        section = self.section('.symtab')
        strings = self.sections[section[6]][4]
        result = []
        for index in range(section[5] // section[9]):
            entry = section[4] + index * section[9]
            name = struct.unpack_from('<I', self.data, entry)[0]
            info, other, target, value, size = struct.unpack_from(
                '<BBHQQ', self.data, entry + 4)
            start = strings + name
            result.append({'name': self.data[start:self.data.index(b'\0', start)].decode(),
                           'type': info & 0xF, 'binding': info >> 4, 'other': other,
                           'section': target, 'value': value, 'size': size})
        return result

    def relocations(self):
        section = self.section('.rela.text')
        result = []
        for index in range(section[5] // section[9]):
            offset, info, addend = struct.unpack_from(
                '<QQq', self.data, section[4] + index * section[9])
            result.append((offset, info & 0xFFFFFFFF, info >> 32, addend))
        return result


def find_tool(names):
    for name in names:
        for prefix in ('/usr/lib/llvm-18/bin/', '/usr/bin/', ''):
            candidate = prefix + name
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
        found = shutil.which(name)
        if found:
            return found
    return None


def run(command, **keywords):
    result = subprocess.run(command, capture_output=True, text=True, **keywords)
    check(result.returncode == 0,
          f'{" ".join(command)} failed:\n{result.stdout}{result.stderr}')
    return result


def build_archive(directory):
    archive = os.path.join(directory, 'libfoo.a')
    output = run([sys.executable, TOOL, '-d', os.path.join(directory, 'foo_implib.def'),
                  '-o', archive, '--ar', find_tool(('llvm-ar-18', 'llvm-ar'))])
    check('skipped 2 data imports' in output.stderr,
          'the data and constant imports were not reported as skipped')
    return archive


def check_object(directory, archive):
    """One archive member per import, each with a thunk, its slot and a record."""
    member = os.path.join(directory, 'member')
    os.mkdir(member)
    run([find_tool(('llvm-ar-18', 'llvm-ar')), 'x', archive], cwd=member)
    members = sorted(os.listdir(member))
    check(members == [f'{index:04d}_{name}.o' for index, name in enumerate(NAMES)],
          f'unexpected archive members {members}')

    iat_name = '.iat$foo.dll'
    first_thunk = b''
    for index, name in enumerate(NAMES):
        image = Image(open(os.path.join(member, members[index]), 'rb').read())
        check(image.type == 1, 'the object is not relocatable')
        check(image.machine == EM_LOONGARCH, 'wrong machine')
        check(image.flags == EF_LOONGARCH_ABI_LP64 | EF_LOONGARCH_ABI_DOUBLE_FLOAT,
              'wrong LoongArch ABI flags')

        names = image.names()
        check(names[1] == '.text' and names[4] == '.strtab' and names[5] == '.shstrtab',
              'the section order moved')
        check(iat_name in names and '.libimports' in names,
              'the import sections are missing')

        text = image.contents('.text')
        check(len(text) == THUNK_SIZE,
              f'{name}: {len(text)} bytes of text, expected one thunk')
        if index == 0:
            first_thunk = text

        iat = image.section(iat_name)
        check(iat[2] == SHF_ALLOC | SHF_WRITE, f'{name}: the slot section is not writable data')
        check(iat[3] == 0, f'{name}: the slot section must not be given an address')
        check(len(image.contents(iat_name)) == SLOT_SIZE, f'{name}: expected exactly one slot')
        check(image.section('.libimports')[2] == 0,
              f'{name}: the record section must not be allocated')

        symbols = image.symbols()
        thunks = [symbol for symbol in symbols if symbol['type'] == STT_FUNC]
        slots = [symbol for symbol in symbols if symbol['type'] == STT_OBJECT]
        check([symbol['name'] for symbol in thunks] == [name],
              f'{name}: wrong thunk symbol')
        check(thunks[0]['section'] == 1 and thunks[0]['value'] == 0 and
              thunks[0]['size'] == THUNK_SIZE, f'{name}: the thunk is not in .text')
        check([symbol['name'] for symbol in slots] == ['__imp_' + name],
              f'{name}: wrong slot symbol')
        check(slots[0]['section'] == names.index(iat_name) and slots[0]['value'] == 0,
              f'{name}: the slot is not in the slot section')
        check(slots[0]['size'] == SLOT_SIZE, f'{name}: the slot is not pointer sized')
        check(slots[0]['other'] == STV_HIDDEN, f'{name}: the slot must be hidden')
        check(not [symbol for symbol in symbols if symbol['section'] == SHN_UNDEF
                   and symbol['name']], f'{name}: the object has undefined symbols')

        relocations = image.relocations()
        check(relocations == [(0, R_LARCH_PCALA_HI20, 2, 0),
                              (4, R_LARCH_PCALA_LO12, 2, 0)],
              f'{name}: unexpected relocations {relocations}')

        record = image.contents('.libimports')
        check(record == b'foo.dll\0' + name.encode() + b'\0\0',
              f'{name}: unexpected import record {record!r}')

    return first_thunk, iat_name


def check_reference_assembler(directory, text, clang):
    """The instruction words are the ones clang emits for the thunk."""
    source = os.path.join(directory, 'reference.s')
    opened = os.path.join(directory, 'reference.o')
    open(source, 'w').write(REFERENCE)
    run([clang, '--target=loongarch64-unknown-elf', '-c', source, '-o', opened])
    reference = Image(open(opened, 'rb').read())
    check(reference.flags == EF_LOONGARCH_ABI_LP64 | EF_LOONGARCH_ABI_DOUBLE_FLOAT,
          'the reference object uses a different LoongArch ABI')
    check(reference.contents('.text') == text[:THUNK_SIZE],
          'the thunk differs from what clang assembles')


def read_pe_imports(path):
    """Return (DLL, [(slot rva, name)]) from the import directory of a PE32+ file."""
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    check(data[pe:pe + 4] == b'PE\0\0', 'not a PE file')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    table = optional + optional_size

    sections = []
    for index in range(count):
        name, size, rva, _, raw = struct.unpack_from('<8sIIII', data, table + 40 * index)
        sections.append((name.rstrip(b'\0').decode(), rva, size, raw))

    image_base = struct.unpack_from('<Q', data, optional + 24)[0]
    directory_rva, _ = struct.unpack_from('<II', data, optional + 112 + 8)

    def section_of(rva):
        for _, start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise SystemExit(f'FAIL: rva 0x{rva:x} is outside the image')

    def string_at(rva):
        start = section_of(rva)
        return data[start:data.index(b'\0', start)].decode()

    offset = section_of(directory_rva)
    lookup, _, _, name, thunk = struct.unpack_from('<IIIII', data, offset)
    entries = []
    while True:
        value = struct.unpack_from('<Q', data, section_of(lookup))[0]
        if value == 0:
            break
        entries.append((thunk, string_at(value + 2)))
        lookup += SLOT_SIZE
        thunk += SLOT_SIZE
    return image_base, string_at(name), entries


def check_link(directory, archive, clang, linker):
    """An importer links against the archive and elf2pe builds the directory.

    The caller also defines `Ordinal`, which the DLL exports too. That link
    only succeeds because the import of `MessageBoxA` does not drag `Ordinal`'s
    thunk out of the archive, which is why each import is its own member.
    """
    source = os.path.join(directory, 'caller.c')
    opened = os.path.join(directory, 'caller.o')
    elf_path = os.path.join(directory, 'caller.elf')
    pe_path = os.path.join(directory, 'caller.exe')
    open(source, 'w').write(CALLER)
    run([clang, '--target=loongarch64-unknown-elf', '-ffreestanding', '-fno-pic',
         '-O1', '-c', source, '-o', opened])
    # --gc-sections must keep the record section that nothing references.
    run([linker, '-m', 'elf64loongarch', '-o', elf_path, '--entry', 'caller',
         '--gc-sections', opened, archive])

    elf = Image(open(elf_path, 'rb').read())
    symbols = {symbol['name']: symbol for symbol in elf.symbols()}
    thunk = symbols['MessageBoxA']
    slot = symbols['__imp_MessageBoxA']
    check(thunk['value'] != 0 and slot['value'] != 0, 'the import was not resolved')
    check('__imp_Ordinal' not in symbols,
          'the unused imports were pulled out of the archive')
    check(symbols['Ordinal']['size'] != THUNK_SIZE,
          'the caller definition of Ordinal was replaced by a thunk')
    check(elf.section('.libimports')[5] > 0,
          '--gc-sections dropped the import record')

    text_address = elf.section('.text')[3]
    text = elf.contents('.text')
    offset = thunk['value'] - text_address
    first, second, third = struct.unpack('<III', text[offset:offset + THUNK_SIZE])
    check(first >> 25 == 0x0D and first & 0x1F == T0,
          'the thunk does not start with pcalau12i $t0')
    check(second >> 22 == 0x0A3 and second & 0x1F == T0 and (second >> 5) & 0x1F == T0,
          'the thunk does not load through $t0 with ld.d')
    check(third == 0x4C000000 | (T0 << 5), 'the thunk does not end with jr $t0')

    # pcalau12i adds a signed page delta to the page of its own address and
    # ld.d adds the signed low half, which has to land on the IAT slot.
    page = (thunk['value'] & ~0xFFF) + (sign_extend((first >> 5) & 0xFFFFF, 20) << 12)
    slot_address = page + sign_extend((second >> 10) & 0xFFF, 12)
    check(slot_address == slot['value'],
          f'the thunk loads 0x{slot_address:x}, not the slot at 0x{slot["value"]:x}')

    # The call site has to reach the thunk.
    caller = symbols['caller']
    start = caller['value'] - text_address
    body = text[start:start + caller['size']]
    targets = []
    for index in range(0, len(body) - 3, 4):
        word = struct.unpack_from('<I', body, index)[0]
        if word >> 26 == 0x15:  # bl
            # B26 splits its offset: the low 16 bits sit at bit 10, the top 10 at bit 0.
            units = ((word >> 10) & 0xFFFF) | ((word & 0x3FF) << 16)
            targets.append(caller['value'] + index + (sign_extend(units, 26) << 2))
    check(thunk['value'] in targets, 'the call did not resolve to the thunk')

    run([sys.executable, ELF2PE, elf_path, pe_path, '--subsystem', '10',
         '--image-base', hex(IMAGE_BASE)])
    image_base, library, entries = read_pe_imports(pe_path)
    check(image_base == IMAGE_BASE, 'elf2pe ignored the image base')
    check(library == 'foo.dll', f'the import descriptor names {library}')
    check([name for _, name in entries] == ['MessageBoxA', 'CreateFileW'],
          f'the directory describes {[name for _, name in entries]}')
    check(entries[0][0] == slot['value'], 'the slot array does not start at the slot')
    check(entries[1][0] == slot['value'] + SLOT_SIZE, 'the slots are not one array')

    print(f'  linked: caller 0x{caller["value"]:x} -> thunk 0x{thunk["value"]:x} -> '
          f'slot 0x{slot["value"]:x}; {library} imports '
          f'{", ".join(name for _, name in entries)}')


def main():
    ar = find_tool(('llvm-ar-18', 'llvm-ar'))
    if not ar:
        raise SystemExit('FAIL: no llvm-ar found')
    clang = find_tool(('clang-18', 'clang'))
    linker = find_tool(('ld.lld-18', 'ld.lld'))

    with tempfile.TemporaryDirectory() as directory:
        open(os.path.join(directory, 'foo_implib.def'), 'w').write(DESCRIPTION)
        archive = build_archive(directory)
        text, _ = check_object(directory, archive)
        if clang:
            check_reference_assembler(directory, text, clang)
            if linker:
                check_link(directory, archive, clang, linker)
            else:
                print('def2elf selftest: skipped the link check (no ld.lld)')
        else:
            print('def2elf selftest: skipped the assembler and link checks (no clang)')

    print('def2elf selftest: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
