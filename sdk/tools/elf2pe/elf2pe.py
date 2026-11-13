"""
PROJECT:     LiberNT SDK tools
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Assemble a linked ELF image into a PE32+ image
COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
"""

import argparse
import struct
import sys

SECTION_ALIGNMENT = 0x1000
FILE_ALIGNMENT = 0x200

ELF_MACHINES = {
    62: ('x86_64', 0x8664),
    183: ('aarch64', 0xAA64),
    243: ('riscv64', 0x5064),
    258: ('loongarch64', 0x6264),
}

REL_BASED_ABSOLUTE = 0
REL_BASED_HIGHLOW = 3
REL_BASED_DIR64 = 10

ABSOLUTE_RELOCATIONS = {
    'x86_64': {1: (REL_BASED_DIR64, 8), 10: (REL_BASED_HIGHLOW, 4), 11: (REL_BASED_HIGHLOW, 4),
               8: (REL_BASED_DIR64, 8)},
    'aarch64': {257: (REL_BASED_DIR64, 8), 258: (REL_BASED_HIGHLOW, 4), 1027: (REL_BASED_DIR64, 8)},
    'riscv64': {1: (REL_BASED_HIGHLOW, 4), 2: (REL_BASED_DIR64, 8), 3: (REL_BASED_DIR64, 8)},
    'loongarch64': {1: (REL_BASED_HIGHLOW, 4), 2: (REL_BASED_DIR64, 8), 3: (REL_BASED_DIR64, 8)},
}

RELATIVE_RELOCATIONS = {
    'x86_64': {8},
    'aarch64': {1027},
    'riscv64': {3},
    'loongarch64': {3},
}

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHN_UNDEF = 0
SHN_ABS = 0xFFF1

ET_EXEC = 2
ET_DYN = 3

IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMAGE_FILE_DLL = 0x2000

SCN_CNT_CODE = 0x00000020
SCN_CNT_INITIALIZED_DATA = 0x00000040
SCN_MEM_DISCARDABLE = 0x02000000
SCN_MEM_EXECUTE = 0x20000000
SCN_MEM_READ = 0x40000000
SCN_MEM_WRITE = 0x80000000

PE32_PLUS_MAGIC = 0x20B
OPTIONAL_HEADER_SIZE = 240
DATA_DIRECTORY_COUNT = 16
DIRECTORY_IMPORT = 1
DIRECTORY_BASE_RELOCATION = 5
SUBSYSTEM_EFI_APPLICATION = 10

# One import address table slot per imported function, and the sections
# sdk/tools/def2elf leaves in the image for them: the slots themselves and the
# record that says which DLL each slot belongs to.
SLOT_SIZE = 8
IMPORT_SECTION_PREFIX = '.iat$'
IMPORT_METADATA_SECTION = '.libimports'
IMPORT_SYMBOL_PREFIX = '__imp_'


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


class Symbol:
    def __init__(self, name, value, section):
        self.name = name
        self.value = value
        self.section = section


class ElfImage:
    def __init__(self, data):
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            raise ValueError('not a little-endian ELF64 image')
        self.data = bytearray(data)
        (self.type, self.machine, _, self.entry, _, self.section_offset,
         _, _, _, _, self.section_entry_size,
         self.section_count, self.string_section) = struct.unpack_from(
            '<HHIQQQIHHHHHH', data, 16)
        if self.type not in (ET_EXEC, ET_DYN):
            raise ValueError('only a linked ELF image can be assembled')
        if self.machine not in ELF_MACHINES:
            raise ValueError(f'unsupported ELF machine {self.machine}')
        self.sections = [list(struct.unpack_from('<IIQQQQIIQQ', data,
                                                self.section_offset + index * self.section_entry_size))
                         for index in range(self.section_count)]
        self.symbols = self.read_symbols()

    def section_name(self, section):
        start = self.sections[self.string_section][4] + section[0]
        return self.data[start:self.data.index(b'\0', start)].decode()

    def named_sections(self):
        for section in self.sections:
            yield self.section_name(section), section

    def section_data(self, section):
        return self.data[section[4]:section[4] + section[5]]

    def read_symbols(self):
        symbols = []
        for _, section in self.named_sections():
            if section[1] != SHT_SYMTAB:
                continue
            strings = self.sections[section[6]][4]
            for index in range(section[5] // section[9]):
                entry = section[4] + index * section[9]
                name, _, _, index_section, value, _ = struct.unpack_from(
                    '<IBBHQQ', self.data, entry)
                start = strings + name
                symbols.append(Symbol(self.data[start:self.data.index(b'\0', start)].decode(),
                                      value, index_section))
        return symbols

    def file_offset(self, address):
        for _, section in self.named_sections():
            if section[2] & SHF_ALLOC and section[3] <= address < section[3] + section[5]:
                return section[4] + address - section[3]
        raise ValueError(f'address 0x{address:x} is not backed by a section')


class Region:
    def __init__(self, start, end):
        self.start = start
        self.end = end
        self.members = []
        self.raw = b''
        self.raw_end = start
        self.executable = False
        self.writable = False

    def contains(self, address):
        return self.start <= address < self.end


def collect_regions(elf):
    sections = []
    for name, section in elf.named_sections():
        if not section[2] & SHF_ALLOC or section[1] in (SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_REL):
            continue
        sections.append((section[3], section[3] + section[5], name, section))
    sections.sort(key=lambda item: item[0])

    regions = []
    for start, end, name, section in sections:
        if regions and (start & ~(SECTION_ALIGNMENT - 1)) <= \
                ((regions[-1].end - 1) & ~(SECTION_ALIGNMENT - 1)):
            region = regions[-1]
            region.end = max(region.end, end)
        else:
            region = Region(start, end)
            regions.append(region)
        region.members.append((start, end, name, section))
        if section[2] & SHF_EXECINSTR:
            region.executable = True
        if section[2] & SHF_WRITE:
            region.writable = True

    for region in regions:
        region.start &= ~(SECTION_ALIGNMENT - 1)
        buffer = bytearray(region.end - region.start)
        for start, end, _, section in region.members:
            if section[1] == SHT_NOBITS:
                continue
            blob = elf.section_data(section)
            buffer[start - region.start:start - region.start + len(blob)] = blob
            region.raw_end = max(region.raw_end, end)
        region.raw = bytes(buffer[:region.raw_end - region.start])
        region.name = region.members[0][2]
    return regions


def collect_relocations(elf):
    relocations = []
    for _, section in elf.named_sections():
        if section[1] not in (SHT_RELA, SHT_REL):
            continue
        if not elf.sections[section[7]][2] & SHF_ALLOC:
            continue
        rela = section[1] == SHT_RELA
        entry_size = section[9] or (24 if rela else 16)
        for index in range(section[5] // entry_size):
            offset = section[4] + index * entry_size
            if rela:
                address, info, addend = struct.unpack_from('<QQq', elf.data, offset)
            else:
                address, info = struct.unpack_from('<QQ', elf.data, offset)
                addend = None
            relocations.append((address, info & 0xFFFFFFFF, info >> 32, addend))
    return relocations


def collect_imports(elf):
    """Group the image's imported symbols by DLL, in description order.

    sdk/tools/def2elf records one DLL per object in a non-allocatable
    .libimports section: the library name, one symbol name per import address
    table slot, then an empty name. The linker concatenates the records of
    every object it kept, so several archives name their own DLL here.
    """
    blob = b''.join(elf.section_data(section) for name, section in elf.named_sections()
                    if name == IMPORT_METADATA_SECTION)
    if not blob:
        return []

    fields = blob.split(b'\0')
    if fields[-1] == b'':
        fields.pop()
    libraries = []
    library_index = {}
    index = 0
    while index < len(fields):
        library = fields[index].decode()
        index += 1
        if library in library_index:
            names = libraries[library_index[library]][1]
        else:
            library_index[library] = len(libraries)
            names = []
            libraries.append((library, names))
        while index < len(fields) and fields[index]:
            names.append(fields[index].decode())
            index += 1
        index += 1
    return [entry for entry in libraries if entry[1]]


def import_slots(elf, imports, shift):
    """Return [(DLL, [(slot rva, name), ...])], one run of slots per DLL.

    A PE import address table is one contiguous array of pointer sized slots
    that the loader fills in, so the slots of a DLL have to be an array in the
    image. def2elf gives each DLL a section of its own for exactly that and
    the linker keeps a section contiguous, which the check below proves.
    """
    symbols = {}
    for symbol in elf.symbols:
        if symbol.section not in (SHN_UNDEF, SHN_ABS):
            symbols[symbol.name] = symbol

    layout = []
    for library, names in imports:
        entries = []
        for name in names:
            symbol = symbols.get(IMPORT_SYMBOL_PREFIX + name)
            if symbol is None:
                raise ValueError(f'{library}: the image has no import address table '
                                 f'slot for {name}')
            entries.append((symbol.value + shift, name))
        entries.sort()
        first = entries[0][0]
        for index, (address, name) in enumerate(entries):
            if address != first + SLOT_SIZE * index:
                raise ValueError(f'{library}: the import address table slots are not '
                                 f'one array; {name} is at 0x{address:x} where '
                                 f'0x{first + SLOT_SIZE * index:x} was expected')
        layout.append((library, entries))
    return layout


def build_import_directory(imports, address):
    """Lay out the import directory for `imports` at RVA `address`.

    Returns (section bytes, directory size). The descriptors come first, one
    per DLL plus a null one, then one import lookup table per descriptor, then
    the hint/name entries and the library names. The import address table is
    not part of it: FirstThunk points at the slots the linker placed, which is
    where the loader writes the resolved addresses.
    """
    descriptor_size = 20 * (len(imports) + 1)
    lookup_offset = align_up(descriptor_size, 8)

    lookup_offsets = []
    cursor = lookup_offset
    for _, entries in imports:
        lookup_offsets.append(cursor)
        cursor += SLOT_SIZE * (len(entries) + 1)
    names_offset = align_up(cursor, 8)

    # Hint/name entries are two byte aligned inside the name area, which the
    # second pass below relies on.
    positions = []
    library_positions = []
    names_size = 0
    for library, entries in imports:
        if names_size % 2:
            names_size += 1
        library_positions.append(names_size)
        names_size += len(library.encode()) + 1
        offsets = []
        for _, name in entries:
            if names_size % 2:
                names_size += 1
            offsets.append(names_size)
            names_size += 2 + len(name.encode()) + 1
        positions.append(offsets)

    blob = bytearray(names_offset + names_size)
    for index, ((library, entries), lookup, library_position) in enumerate(
            zip(imports, lookup_offsets, library_positions)):
        struct.pack_into('<IIIII', blob, 20 * index, address + lookup, 0, 0,
                         address + names_offset + library_position, entries[0][0])
        for entry_index, (slot, name) in enumerate(entries):
            struct.pack_into('<Q', blob, lookup + SLOT_SIZE * entry_index,
                             address + names_offset + positions[index][entry_index])

    for (library, entries), library_position, offsets in zip(imports, library_positions,
                                                             positions):
        start = names_offset + library_position
        blob[start:start + len(library.encode())] = library.encode()
        for (_, name), offset in zip(entries, offsets):
            encoded = name.encode()
            blob[names_offset + offset:names_offset + offset + len(encoded) + 2] = \
                struct.pack('<H', 0) + encoded
    return bytes(blob), descriptor_size


def relocated_value(elf, regions, address, symbol_index, addend, shift, width, relative=False,
                    image_base=0):
    if relative:
        return addend if addend is not None else 0
    if addend is not None:
        if symbol_index >= len(elf.symbols):
            raise ValueError(f'relocation at 0x{address:x} references no symbol')
        symbol = elf.symbols[symbol_index]
        if symbol.section == SHN_UNDEF:
            # An imported function reaches the image through the import address
            # table slot its thunk loads (see collect_imports), so anything
            # still undefined here has no address to point at.
            raise ValueError(f"undefined symbol '{symbol.name}' in the relocation "
                             f'at 0x{address:x}')
        target = symbol.value + addend
        if symbol.section == SHN_ABS:
            return target
    else:
        target = struct.unpack_from('<Q' if width == 8 else '<I', elf.data,
                                    elf.file_offset(address))[0]
    if not any(region.contains(target) for region in regions):
        return target
    # An in-image pointer names the address it will have once the image is
    # loaded at its preferred base, which is what a PE loader rebases from.
    target += image_base + shift
    if width == 4 and target > 0xFFFFFFFF:
        raise ValueError(f'relocation at 0x{address:x} cannot hold image base '
                         f'0x{image_base:x} in 32 bits')
    return target


def patch_relocations(elf, regions, relocations, machine, shift, image_base=0):
    table = ABSOLUTE_RELOCATIONS[machine]
    patched = 0
    skipped = 0
    for address, kind, symbol_index, addend in relocations:
        if kind not in table:
            skipped += 1
            continue
        _, width = table[kind]
        value = relocated_value(elf, regions, address, symbol_index, addend, shift, width,
                                kind in RELATIVE_RELOCATIONS[machine], image_base)
        struct.pack_into('<Q' if width == 8 else '<I', elf.data, elf.file_offset(address),
                         value & ((1 << (width * 8)) - 1))
        patched += 1
    return patched, skipped


def build_base_relocations(relocations, machine, shift, extra_slots=()):
    table = ABSOLUTE_RELOCATIONS[machine]
    pages = {}
    for address, kind, _, _ in relocations:
        if kind not in table:
            continue
        target = address + shift
        pages.setdefault(target & ~0xFFF, []).append((target & 0xFFF, table[kind][0]))
    # The imported addresses the loader writes into the import address table are
    # pointers too, so they need the same DIR64 fixup when the image is rebased.
    for address in extra_slots:
        pages.setdefault(address & ~0xFFF, []).append((address & 0xFFF, REL_BASED_DIR64))

    blob = bytearray()
    for page in sorted(pages):
        block = bytearray(struct.pack('<II', page, 0))
        for offset, kind in sorted(pages[page]):
            block += struct.pack('<H', (kind << 12) | offset)
        while len(block) % 4:
            block += struct.pack('<H', REL_BASED_ABSOLUTE << 12)
        struct.pack_into('<I', block, 4, len(block))
        blob += block
    return bytes(blob)


class OutputSection:
    def __init__(self, name, address, virtual_size, raw, flags, file_offset):
        self.name = name
        self.address = address
        self.virtual_size = virtual_size
        self.raw = raw
        self.flags = flags
        self.file_offset = file_offset


def build_image(elf, subsystem, image_base, dll):
    machine_name, machine = ELF_MACHINES[elf.machine]
    regions = collect_regions(elf)
    if not regions:
        raise ValueError('the image has no allocatable section')

    imports = collect_imports(elf)
    if not imports:
        # A section holding slots without the record that names their DLL means
        # the record was stripped away; an image whose import directory is
        # missing would load and then jump through empty slots.
        for name, _ in elf.named_sections():
            if name.startswith(IMPORT_SECTION_PREFIX):
                raise ValueError(f'{name} holds import address table slots but the '
                                 f'image has no {IMPORT_METADATA_SECTION} record to name '
                                 f'their DLL')
    header_size = align_up(0x40 + 4 + 20 + OPTIONAL_HEADER_SIZE +
                           40 * (len(regions) + 1 + (1 if imports else 0)), FILE_ALIGNMENT)
    first = min(region.start for region in regions)
    shift = align_up(header_size - first, SECTION_ALIGNMENT) if first < header_size else 0

    relocations = collect_relocations(elf)
    patched, skipped = patch_relocations(elf, regions, relocations, machine_name, shift,
                                         image_base)
    regions = collect_regions(elf)
    for region in regions:
        region.start += shift
        region.end += shift

    file_offset = header_size
    sections = []
    for region in regions:
        flags = SCN_MEM_READ | SCN_CNT_INITIALIZED_DATA
        if region.executable:
            flags |= SCN_MEM_EXECUTE | SCN_CNT_CODE
        if region.writable:
            flags |= SCN_MEM_WRITE
        sections.append(OutputSection(region.name, region.start, region.end - region.start,
                                      region.raw, flags, file_offset))
        file_offset += align_up(len(region.raw), FILE_ALIGNMENT)

    import_layout = import_slots(elf, imports, shift)
    import_directory = (0, 0)
    if import_layout:
        address = align_up(sections[-1].address + sections[-1].virtual_size,
                           SECTION_ALIGNMENT)
        idata, directory_size = build_import_directory(import_layout, address)
        sections.append(OutputSection('.idata', address, len(idata), idata,
                                      SCN_MEM_READ | SCN_CNT_INITIALIZED_DATA,
                                      file_offset))
        file_offset += align_up(len(idata), FILE_ALIGNMENT)
        import_directory = (address, directory_size)

    slots = [slot for _, entries in import_layout for slot, _ in entries]
    base_relocations = build_base_relocations(relocations, machine_name, shift, slots)
    if base_relocations:
        address = align_up(sections[-1].address + sections[-1].virtual_size, SECTION_ALIGNMENT)
        sections.append(OutputSection('.reloc', address, len(base_relocations), base_relocations,
                                      SCN_MEM_READ | SCN_MEM_DISCARDABLE |
                                      SCN_CNT_INITIALIZED_DATA, file_offset))

    image_size = align_up(sections[-1].address + sections[-1].virtual_size, SECTION_ALIGNMENT)
    size_of_code = sum(align_up(len(section.raw), FILE_ALIGNMENT) for section in sections
                       if section.flags & SCN_CNT_CODE)
    size_of_data = sum(align_up(len(section.raw), FILE_ALIGNMENT) for section in sections
                       if section.flags & SCN_CNT_INITIALIZED_DATA and not section.flags & SCN_CNT_CODE)

    header = bytearray(header_size)
    header[0:2] = b'MZ'
    struct.pack_into('<I', header, 0x3C, 0x40)
    struct.pack_into('<I', header, 0x40, 0x00004550)
    characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE
    if dll:
        characteristics |= IMAGE_FILE_DLL
    struct.pack_into('<HHIIIHH', header, 0x44, machine, len(sections), 0, 0, 0,
                     OPTIONAL_HEADER_SIZE, characteristics)

    optional = 0x44 + 20
    entry = elf.entry + shift
    base_of_code = sections[0].address if sections[0].flags & SCN_CNT_CODE else 0
    struct.pack_into('<HBBIIIII', header, optional, PE32_PLUS_MAGIC, 0, 0, size_of_code,
                     size_of_data, 0, entry, base_of_code)
    struct.pack_into('<Q', header, optional + 24, image_base)
    struct.pack_into('<II', header, optional + 32, SECTION_ALIGNMENT, FILE_ALIGNMENT)
    struct.pack_into('<I', header, optional + 56, image_size)
    struct.pack_into('<I', header, optional + 60, header_size)
    struct.pack_into('<H', header, optional + 68, subsystem)
    struct.pack_into('<I', header, optional + 108, DATA_DIRECTORY_COUNT)
    if base_relocations:
        struct.pack_into('<II', header, optional + 112 + 8 * DIRECTORY_BASE_RELOCATION,
                         sections[-1].address, len(base_relocations))
    if import_directory[1]:
        struct.pack_into('<II', header, optional + 112 + 8 * DIRECTORY_IMPORT,
                         *import_directory)

    table = optional + OPTIONAL_HEADER_SIZE
    for index, section in enumerate(sections):
        struct.pack_into('<8sIIIIIIHHI', header, table + 40 * index, section.name.encode()[:8],
                         section.virtual_size, section.address,
                         align_up(len(section.raw), FILE_ALIGNMENT), section.file_offset,
                         0, 0, 0, 0, section.flags & 0xFFFFFFFF)

    image = bytearray(header)
    for section in sections:
        end = section.file_offset + align_up(len(section.raw), FILE_ALIGNMENT)
        if len(image) < end:
            image += bytes(end - len(image))
        image[section.file_offset:section.file_offset + len(section.raw)] = section.raw

    return bytes(image), sections, shift, patched, skipped


def main(argv):
    parser = argparse.ArgumentParser(description='Assemble a linked ELF64 image into a PE32+ image.')
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('--subsystem', type=int, default=SUBSYSTEM_EFI_APPLICATION)
    parser.add_argument('--image-base', type=lambda value: int(value, 0), default=0)
    parser.add_argument('--dll', action='store_true')
    options = parser.parse_args(argv)

    try:
        elf = ElfImage(open(options.input, 'rb').read())
        image, sections, shift, patched, skipped = build_image(
            elf, options.subsystem, options.image_base, options.dll)
    except (OSError, ValueError, struct.error) as error:
        print(f'elf2pe: {error}', file=sys.stderr)
        return 1

    with open(options.output, 'wb') as output:
        output.write(image)

    print(f'{options.output}: {len(sections)} sections, entry 0x{elf.entry + shift:x}, '
          f'shift 0x{shift:x}, {patched} fixups, {skipped} other relocations')
    for section in sections:
        print(f'  {section.name:<8} rva 0x{section.address:08x} '
              f'size 0x{section.virtual_size:06x} raw 0x{len(section.raw):06x}')
    for library, names in collect_imports(elf):
        print(f'  imports {library}: {len(names)} slots')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
