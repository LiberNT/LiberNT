"""
PROJECT:     LiberNT SDK tools
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Build an ELF64 import library out of a spec2def import description
COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>

llvm-dlltool has no LoongArch64 machine, and a LoongArch64 image is linked as
ELF and only afterwards assembled into a PE32+ file, so its import libraries
have to be ELF archives rather than COFF ones.

For every function an import description exports we emit one global symbol and
one thunk that loads the import address table slot and tail calls through it:

    pcalau12i $t0, %pc_hi20(__imp_Foo)
    ld.d      $t0, $t0, %pc_lo12(__imp_Foo)
    jr        $t0

Every imported symbol becomes an *archive member of its own*, the way an
llvm-dlltool import library is laid out. One member for the whole description
would make the linker pull the thunk of every import as soon as one of them is
undefined, and a symbol the image defines itself while the DLL also exports it
would then be defined twice (ntoskrnl defines ExAcquireFastMutex and hal.spec
exports it too). With one member per symbol the archive index decides, and
only the imports the image really leaves undefined are pulled in.

The slot itself is a zeroed 8-byte entry in a section of its own,

    .iat$foo.dll        allocatable, writable, 8 bytes per imported function

with one global `__imp_Foo` symbol per entry and the entries in description
order. Both halves of the `$pc_hi20`/`$pc_lo12` pair name the same symbol and
the two instructions therefore only work if the slot really lives at the
address the linker gave it, so the slot has to be *defined* here: a slot left
undefined makes ld.lld reject the pc-relative reference ("cannot be used
against symbol ... recompile with -fPIC") and an import library that cannot be
linked is of no use. Because a PE import address table is exactly this -- one
contiguous array of pointer sized slots per DLL, which the loader fills in --
sdk/tools/elf2pe only has to turn the section into an IMAGE_IMPORT_DESCRIPTOR
and write the hint/name pairs. The linker keeps the slots of every member it
pulled in one section, so the array stays contiguous however few of them an
image uses.

A second, non-allocatable section carries the mapping the PE writer cannot
recover from the symbol table alone:

    .libimports         NUL terminated library name, then one NUL terminated
                        symbol name per slot, then an empty name to end the
                        record; one record per member, so an image that pulls
                        two imports of one DLL carries two records and the PE
                        writer merges them by library name

`__imp_Foo` says nothing about which DLL `Foo` comes from, and the library name
is not part of the symbol table, so it travels here. The PE writer in
sdk/tools/elf2pe reads that section to build the import directory.

Accepted from the description:

    LIBRARY name / NAME name      names the DLL in the .libimports record,
                                  so a description with functions needs it
    name                          a function import
    name=target / name==target    imported under the left-hand name, like
                                  llvm-dlltool does; the target half only says
                                  where the DLL's own export table points
    name @N NONAME                kept by the description for the PE writer
    name PRIVATE / name SINGLE    same
    name DATA / name CONSTANT     rejected, see below
    ; comment and "quoted name"   handled

A DATA or CONSTANT import has no thunk, and cannot be given a slot either:
the loader would put the address of the imported object in it, which is only
what a `dllimport` consumer wants. clang reports `'dllimport' attribute is not
supported` for loongarch64-unknown-elf, so a consumer here names the variable
itself, and one ELF symbol cannot be both the variable and the slot that points
at it: the slot would resolve, then be read as if its address were the
variable. Both are reported and skipped instead of being linked into a pointer
whose meaning the caller cannot use. Anything importing data on this target
needs an explicit `__imp_`-style pointer that neither the headers nor the
compiler synthesise.

`NAME`/`LIBRARY` is what the `.libimports` record names, so a description that
describes functions without it is rejected: without it the PE writer would have
to guess which DLL a slot belongs to.

The instruction words and the relocation types are the ones clang produces for
the assembly above, which sdk/tools/def2elf/def2elf_selftest.py checks against
a real assembler and a real link.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

EM_LOONGARCH = 258
ET_REL = 1

# e_flags as clang emits them for --target=loongarch64-unknown-elf: the LP64
# data model with the double float ABI. ld.lld refuses to mix objects that
# disagree about it.
EF_LOONGARCH_ABI_LP64 = 0x40
EF_LOONGARCH_ABI_DOUBLE_FLOAT = 0x3
ELF_FLAGS = EF_LOONGARCH_ABI_LP64 | EF_LOONGARCH_ABI_DOUBLE_FLOAT

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHN_UNDEF = 0

STB_GLOBAL = 1
STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2
STV_HIDDEN = 2

R_LARCH_PCALA_HI20 = 71
R_LARCH_PCALA_LO12 = 72

SLOT_SIZE = 8
IMPORT_SECTION_PREFIX = '.iat$'
IMPORT_METADATA_SECTION = '.libimports'

# Section indices inside the object build_object writes.
TEXT_SECTION = 1
RELA_TEXT_SECTION = 2
SYMTAB_SECTION = 3
STRTAB_SECTION = 4
SHSTRTAB_SECTION = 5
IAT_SECTION = 6
METADATA_SECTION = 7

# $t0 is r12. The immediates are filled in by R_LARCH_PCALA_HI20/LO12.
THUNK_WORDS = (
    0x1A00000C,  # pcalau12i $t0, 0
    0x28C0018C,  # ld.d      $t0, $t0, 0
    0x4C000180,  # jr        $t0
)
THUNK_SIZE = 4 * len(THUNK_WORDS)

SECTION_NAMES = ('.text', '.rela.text', '.symtab', '.strtab', '.shstrtab')

# Directives that an import description may carry but that say nothing about
# the symbols an importer resolves.
IGNORED_DIRECTIVES = ('description', 'version', 'stacksize', 'heapsize',
                      'sections', 'segments')

EXPORT_FLAGS = ('NONAME', 'DATA', 'CONSTANT', 'PRIVATE', 'SINGLE')


class DefError(Exception):
    pass


class Export:
    """The symbol side of one parsed `EXPORTS` entry."""

    def __init__(self, name, is_data):
        self.name = name
        self.is_data = is_data


def tokenize(line):
    tokens = []
    current = []
    quoted = False
    for character in line:
        if character == '"':
            quoted = not quoted
        elif character.isspace() and not quoted:
            if current:
                tokens.append(''.join(current))
                current = []
        else:
            current.append(character)
    if quoted:
        raise DefError('unterminated quote')
    if current:
        tokens.append(''.join(current))
    return tokens


def split_alias(token):
    """Split `name=target` / `name==target` into its two halves."""
    for separator in ('==', '='):
        if separator in token:
            name, target = token.split(separator, 1)
            return name, target
    return token, None


def parse_export(tokens, number):
    # `Name=Target` redirects inside the exporting DLL and `@N`/`NONAME`/
    # `PRIVATE` describe its PE export. An importer always references `Name`
    # and the PE writer re-reads those parts of the description, so only the
    # name and the data-ness of the entry matter here.
    name = split_alias(tokens[0])[0]
    if not name:
        raise DefError(f'export {number} has no name')

    flags = set()
    for token in tokens[1:]:
        upper = token.upper()
        if upper in EXPORT_FLAGS:
            flags.add(upper)
        elif not (upper.startswith('@') and upper[1:].isdigit()):
            raise DefError(f"export '{name}' has an unknown keyword '{token}'")

    return Export(name, 'DATA' in flags or 'CONSTANT' in flags)


def parse_def(text):
    """Return (library name, [function Export], [data export name])."""
    library = None
    exports = []
    data = []
    in_exports = False

    for number, line in enumerate(text.splitlines(), 1):
        line = line.split(';', 1)[0].strip()
        if not line:
            continue
        tokens = tokenize(line)
        keyword = tokens[0].lower()

        if not in_exports:
            if keyword in ('library', 'name'):
                library = tokens[1] if len(tokens) > 1 else None
            elif keyword == 'exports':
                in_exports = True
            elif keyword in IGNORED_DIRECTIVES:
                continue
            elif keyword == 'imports':
                raise DefError('line %d: an IMPORTS description describes a '
                               'forwarding library, not a DLL' % number)
            else:
                raise DefError(f"line {number}: unknown directive '{tokens[0]}'")
            continue

        if keyword == 'exports':
            continue
        try:
            export = parse_export(tokens, len(exports) + len(data))
        except DefError as error:
            raise DefError(f'line {number}: {error}') from None
        if export.is_data:
            data.append(export.name)
        else:
            exports.append(export)

    return library, exports, data


def build_object(library, exports):
    """Return the ET_REL object: one thunk per import plus its IAT slot.

    Section index order is fixed because the symbol table names sections by
    index: .text(1) .rela.text(2) .symtab(3) .strtab(4) .shstrtab(5)
    .iat$<library>(6) .libimports(7). The string table section stays 5 so the
    ELF header keeps pointing at it.
    """
    text = bytearray()
    symtab = bytearray(struct.pack('<IBBHQQ', 0, 0, 0, SHN_UNDEF, 0, 0))
    strtab = bytearray(b'\0')
    rela = bytearray()
    iat = bytearray()
    metadata = bytearray(library.encode() + b'\0')
    interned = {}

    def intern(name):
        if name not in interned:
            interned[name] = len(strtab)
            strtab.extend(name.encode() + b'\0')
        return interned[name]

    for index, export in enumerate(exports):
        offset = len(text)
        text.extend(struct.pack('<III', *THUNK_WORDS))
        symtab.extend(struct.pack('<IBBHQQ', intern(export.name),
                                  (STB_GLOBAL << 4) | STT_FUNC, 0, 1, offset,
                                  THUNK_SIZE))
        # The slot is defined here, in the image, so that the pc-relative
        # thunk resolves at link time; the PE writer turns the section into
        # an import address table and the loader fills the slots in. It has
        # to be hidden: a preemptible definition would make the linker reject
        # R_LARCH_PCALA_LO12 in a shared link.
        slot = index * SLOT_SIZE
        iat.extend(bytes(SLOT_SIZE))
        symtab.extend(struct.pack('<IBBHQQ', intern('__imp_' + export.name),
                                  (STB_GLOBAL << 4) | STT_OBJECT, STV_HIDDEN,
                                  IAT_SECTION, slot, SLOT_SIZE))
        imp_index = 2 + 2 * index
        rela.extend(struct.pack('<QQq', offset,
                                (imp_index << 32) | R_LARCH_PCALA_HI20, 0))
        rela.extend(struct.pack('<QQq', offset + 4,
                                (imp_index << 32) | R_LARCH_PCALA_LO12, 0))
        metadata.extend(export.name.encode() + b'\0')

    # An empty name ends the record, so that several objects (one per
    # description) can be concatenated by the linker and still be told apart.
    metadata.extend(b'\0')

    names = ('.text', '.rela.text', '.symtab', '.strtab', '.shstrtab',
             IMPORT_SECTION_PREFIX + library, IMPORT_METADATA_SECTION)
    shstrtab = bytearray(b'\0')
    offsets_in_names = []
    for name in names:
        offsets_in_names.append(len(shstrtab))
        shstrtab.extend(name.encode() + b'\0')

    sections = [
        # name, type, flags, link, info, alignment, entry size, payload
        ('.text', SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, 0, 4, 0,
         bytes(text)),
        ('.rela.text', SHT_RELA, 0, SYMTAB_SECTION, TEXT_SECTION, 8, 24,
         bytes(rela)),
        ('.symtab', SHT_SYMTAB, 0, STRTAB_SECTION, 1, 8, 24, bytes(symtab)),
        ('.strtab', SHT_STRTAB, 0, 0, 0, 1, 0, bytes(strtab)),
        ('.shstrtab', SHT_STRTAB, 0, 0, 0, 1, 0, bytes(shstrtab)),
        (names[5], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0, 0, SLOT_SIZE, 0,
         bytes(iat)),
        (names[6], SHT_PROGBITS, 0, 0, 0, 1, 0, bytes(metadata)),
    ]

    offsets = []
    offset = 64
    for entry in sections:
        alignment = entry[5]
        offset = (offset + alignment - 1) & ~(alignment - 1)
        offsets.append(offset)
        offset += len(entry[7])
    section_offset = (offset + 7) & ~7

    section_headers = bytearray(struct.pack('<IIQQQQIIQQ', *[0] * 10))
    for index, entry in enumerate(sections):
        name, kind, flags, link, info, alignment, entry_size, payload = entry
        section_headers += struct.pack('<IIQQQQIIQQ', offsets_in_names[index],
                                      kind, flags, 0, offsets[index],
                                      len(payload), link, info, alignment,
                                      entry_size)

    image = bytearray(64)
    image[0:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQIHHHHHH', image, 16, ET_REL, EM_LOONGARCH, 1, 0, 0,
                     section_offset, ELF_FLAGS, 64, 0, 0, 64,
                     len(sections) + 1, SHSTRTAB_SECTION)

    for entry, start in zip(sections, offsets):
        if len(image) < start:
            image.extend(bytes(start - len(image)))
        image[start:start + len(entry[7])] = entry[7]
    if len(image) < section_offset:
        image.extend(bytes(section_offset - len(image)))
    image.extend(section_headers)
    return bytes(image)


def member_name(index, symbol):
    """A file name for one member: unique, and safe on every host."""
    safe = ''.join(character if character.isalnum() or character in '._-' else '_'
                   for character in symbol)[:32]
    return f'{index:04d}_{safe}.o'


def archive(tool, library, members):
    """Run the archiver over [(member name, object image)] in that order."""
    library = os.path.abspath(library)
    # The archiver updates an archive in place, so a rebuild must start empty
    # or the members of the previous description would survive.
    if os.path.exists(library):
        os.remove(library)
    with tempfile.TemporaryDirectory(prefix='def2elf-') as directory:
        for member, image in members:
            with open(os.path.join(directory, member), 'wb') as output:
                output.write(image)
        # The index this creates is what makes the linker pull a member only
        # for the symbol that is still undefined.
        result = subprocess.run([tool, 'rcs', library] + [member for member, _ in members],
                                cwd=directory, capture_output=True, text=True)
    if result.returncode != 0:
        raise DefError(f'{tool} failed: {result.stderr.strip()}')


def main(argv):
    parser = argparse.ArgumentParser(
        description='Build a LoongArch64 ELF import library from a .def file.')
    parser.add_argument('-d', '--def', dest='description', required=True,
                        help='import description produced by spec2def --implib')
    parser.add_argument('-o', '--output', required=True,
                        help='archive to create')
    parser.add_argument('--ar', default='llvm-ar',
                        help='archive tool to run')
    options = parser.parse_args(argv)

    try:
        with open(options.description, 'r', encoding='utf-8') as source:
            library, exports, data = parse_def(source.read())
        if data:
            print(f'def2elf: {options.description}: skipped {len(data)} data '
                  f'imports ({", ".join(data[:4])}{", ..." if len(data) > 4 else ""}): '
                  f'this target has no dllimport, so a caller names the variable '
                  f'itself and a pointer slot cannot stand in for it',
                  file=sys.stderr)
        if exports and not library:
            raise DefError(f'{options.description}: the description has no NAME '
                           f'or LIBRARY directive, so the import address table '
                           f'slots it describes cannot be attributed to a DLL')
        members = [(member_name(index, export.name),
                    build_object(library or '', [export]))
                   for index, export in enumerate(exports)]
        archive(options.ar, options.output, members)
    except (DefError, OSError) as error:
        print(f'def2elf: {error}', file=sys.stderr)
        return 1

    print(f'{options.output}: {len(exports)} thunks from {library or "unnamed"}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
