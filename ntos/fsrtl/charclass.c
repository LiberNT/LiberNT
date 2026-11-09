/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which characters a file system may put in a name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define FS_ANY (FSRTL_FAT_LEGAL | FSRTL_HPFS_LEGAL | FSRTL_NTFS_LEGAL | FSRTL_OLE_LEGAL)
#define FS_NO_FAT (FSRTL_HPFS_LEGAL | FSRTL_NTFS_LEGAL | FSRTL_OLE_LEGAL)
#define FS_WILD (FSRTL_WILD_CHARACTER | FSRTL_OLE_LEGAL)
#define FS_NO_OLE (FSRTL_FAT_LEGAL | FSRTL_HPFS_LEGAL | FSRTL_NTFS_LEGAL)

static const UCHAR LegalAnsiCharacterArray[128] =
{
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,
    0,          0,          0,          0,

    FS_ANY,                 /* 0x20 ' ' */
    FS_NO_OLE,          /* 0x21 '!' */
    FS_WILD,             /* 0x22 '"' DOS_DOT */
    FS_ANY,                 /* 0x23 '#' */
    FS_ANY,                 /* 0x24 '$' */
    FS_ANY,                 /* 0x25 '%' */
    FS_ANY,                 /* 0x26 '&' */
    FS_ANY,                 /* 0x27 '\'' */
    FS_ANY,                 /* 0x28 '(' */
    FS_ANY,                 /* 0x29 ')' */
    FS_WILD,             /* 0x2A '*' */
    FS_NO_FAT,              /* 0x2B '+' */
    FS_NO_FAT,              /* 0x2C ',' */
    FS_ANY,                 /* 0x2D '-' */
    FS_ANY,                 /* 0x2E '.' */
    0,                      /* 0x2F '/' */

    FS_ANY,                 /* 0x30 '0' */
    FS_ANY,                 /* 0x31 '1' */
    FS_ANY,                 /* 0x32 '2' */
    FS_ANY,                 /* 0x33 '3' */
    FS_ANY,                 /* 0x34 '4' */
    FS_ANY,                 /* 0x35 '5' */
    FS_ANY,                 /* 0x36 '6' */
    FS_ANY,                 /* 0x37 '7' */
    FS_ANY,                 /* 0x38 '8' */
    FS_ANY,                 /* 0x39 '9' */
    0,                      /* 0x3A ':' */
    FS_NO_FAT,              /* 0x3B ';' */
    FS_WILD,             /* 0x3C '<' DOS_STAR */
    FS_NO_FAT,              /* 0x3D '=' */
    FS_WILD,             /* 0x3E '>' DOS_QM */
    FS_WILD,             /* 0x3F '?' */

    FS_ANY,                 /* 0x40 '@' */
    FS_ANY,                 /* 0x41 'A' */
    FS_ANY,                 /* 0x42 'B' */
    FS_ANY,                 /* 0x43 'C' */
    FS_ANY,                 /* 0x44 'D' */
    FS_ANY,                 /* 0x45 'E' */
    FS_ANY,                 /* 0x46 'F' */
    FS_ANY,                 /* 0x47 'G' */
    FS_ANY,                 /* 0x48 'H' */
    FS_ANY,                 /* 0x49 'I' */
    FS_ANY,                 /* 0x4A 'J' */
    FS_ANY,                 /* 0x4B 'K' */
    FS_ANY,                 /* 0x4C 'L' */
    FS_ANY,                 /* 0x4D 'M' */
    FS_ANY,                 /* 0x4E 'N' */
    FS_ANY,                 /* 0x4F 'O' */

    FS_ANY,                 /* 0x50 'P' */
    FS_ANY,                 /* 0x51 'Q' */
    FS_ANY,                 /* 0x52 'R' */
    FS_ANY,                 /* 0x53 'S' */
    FS_ANY,                 /* 0x54 'T' */
    FS_ANY,                 /* 0x55 'U' */
    FS_ANY,                 /* 0x56 'V' */
    FS_ANY,                 /* 0x57 'W' */
    FS_ANY,                 /* 0x58 'X' */
    FS_ANY,                 /* 0x59 'Y' */
    FS_ANY,                 /* 0x5A 'Z' */
    FS_NO_FAT,              /* 0x5B '[' */
    0,                      /* 0x5C '\\' */
    FS_NO_FAT,              /* 0x5D ']' */
    FS_ANY,                 /* 0x5E '^' */
    FS_ANY,                 /* 0x5F '_' */

    FS_ANY,                 /* 0x60 '`' */
    FS_ANY,                 /* 0x61 'a' */
    FS_ANY,                 /* 0x62 'b' */
    FS_ANY,                 /* 0x63 'c' */
    FS_ANY,                 /* 0x64 'd' */
    FS_ANY,                 /* 0x65 'e' */
    FS_ANY,                 /* 0x66 'f' */
    FS_ANY,                 /* 0x67 'g' */
    FS_ANY,                 /* 0x68 'h' */
    FS_ANY,                 /* 0x69 'i' */
    FS_ANY,                 /* 0x6A 'j' */
    FS_ANY,                 /* 0x6B 'k' */
    FS_ANY,                 /* 0x6C 'l' */
    FS_ANY,                 /* 0x6D 'm' */
    FS_ANY,                 /* 0x6E 'n' */
    FS_ANY,                 /* 0x6F 'o' */

    FS_ANY,                 /* 0x70 'p' */
    FS_ANY,                 /* 0x71 'q' */
    FS_ANY,                 /* 0x72 'r' */
    FS_ANY,                 /* 0x73 's' */
    FS_ANY,                 /* 0x74 't' */
    FS_ANY,                 /* 0x75 'u' */
    FS_ANY,                 /* 0x76 'v' */
    FS_ANY,                 /* 0x77 'w' */
    FS_ANY,                 /* 0x78 'x' */
    FS_ANY,                 /* 0x79 'y' */
    FS_ANY,                 /* 0x7A 'z' */
    FS_ANY,                 /* 0x7B '{' */
    FSRTL_OLE_LEGAL,        /* 0x7C '|' */
    FS_ANY,                 /* 0x7D '}' */
    FS_ANY,                 /* 0x7E '~' */
    FS_ANY                  /* 0x7F DEL */
};

/* TODO: FSRTL_OLE_LEGAL is set from the documented NTFS stream-name rule (no \ / :) only; no in-tree caller tests the bit on its own */
const UCHAR * const FsRtlLegalAnsiCharacterArray = LegalAnsiCharacterArray;

/* EOF */
