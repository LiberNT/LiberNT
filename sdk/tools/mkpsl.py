#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

import sys


def main():
    source, target = sys.argv[1], sys.argv[2]
    rules = []
    icann = False
    with open(source, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line.startswith("// ===BEGIN ICANN DOMAINS==="):
                icann = True
            elif line.startswith("// ===END ICANN DOMAINS==="):
                icann = False
            elif icann and line and not line.startswith("//"):
                rules.append(line.split()[0].lower().encode("utf-8"))
    rules = sorted(set(rules))
    with open(target, "w", encoding="ascii", newline="\n") as out:
        out.write("/* This Source Code Form is subject to the terms of the Mozilla Public\n")
        out.write(" * License, v. 2.0. If a copy of the MPL was not distributed with this\n")
        out.write(" * file, You can obtain one at https://mozilla.org/MPL/2.0/. */\n\n")
        out.write("static const char *const public_suffixes[] =\n{\n")
        for rule in rules:
            text = "".join(chr(b) if 0x20 <= b < 0x7f and b not in (0x22, 0x5c) else "\\%03o" % b for b in rule)
            out.write('    "%s",\n' % text)
        out.write("};\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
