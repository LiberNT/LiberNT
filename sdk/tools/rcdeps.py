#!/usr/bin/env python3
# PROJECT:     LiberNT build tools
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Add the files that resource statements read (icons, cursors, bitmaps...) to a resource depfile
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

import os
import re
import sys

MEMORY_OPTIONS = {"PRELOAD", "LOADONCALL", "FIXED", "MOVEABLE", "DISCARDABLE", "PURE", "IMPURE", "SHARED",
                  "NONSHARED"}
HEADER_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".inl")
STATEMENT = re.compile(r'^\s*([^\s",#]+)\s+([^\s",]+)((?:\s+[A-Za-z]+)*)\s+'
                       r'(?:"((?:[^"\\]|\\.)*)"|([^\s",]+\.[A-Za-z0-9_]+))\s*$')


def strip_comments(lines):
    in_block = False
    for line in lines:
        out, i, in_string = "", 0, False
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                    break
                in_block, i = False, end + 2
                out += " "
                continue
            c = line[i]
            if in_string:
                out += c
                if c == "\\" and i + 1 < len(line):
                    out += line[i + 1]
                    i += 2
                    continue
                if c == '"':
                    in_string = False
            elif c == '"':
                in_string = True
                out += c
            elif line.startswith("//", i):
                break
            elif line.startswith("/*", i):
                in_block, i = True, i + 2
                continue
            else:
                out += c
            i += 1
        yield out


def read_depfile(path):
    try:
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
    except FileNotFoundError:
        return None, []
    body = text.replace("\\\r\n", " ").replace("\\\n", " ")
    target, _, rest = body.partition(": ")
    deps, current, i = [], "", 0
    while i < len(rest):
        c = rest[i]
        if c == "\\" and i + 1 < len(rest) and rest[i + 1] in " #":
            current += rest[i + 1]
            i += 2
            continue
        if c.isspace():
            if current:
                deps.append(current)
                current = ""
        else:
            current += c
        i += 1
    if current:
        deps.append(current)
    return text, deps


def resource_files(path):
    try:
        lines = open(path, encoding="utf-8", errors="surrogateescape").read().splitlines()
    except OSError:
        return
    for line in strip_comments(lines):
        match = STATEMENT.match(line)
        if not match:
            continue
        if any(option.upper() not in MEMORY_OPTIONS for option in match.group(3).split()):
            continue
        name = match.group(4) if match.group(4) is not None else match.group(5)
        yield name.replace("\\\\", "\\").replace("\\", "/")


def resolve(name, directories):
    if os.path.isabs(name):
        return os.path.normpath(name) if os.path.isfile(name) else None
    for directory in directories:
        candidate = os.path.normpath(os.path.join(directory, name))
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)
    return None


def escape(path):
    return path.replace("\\", "/").replace(" ", "\\ ").replace("#", "\\#")


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: rcdeps.py <object> <source.rc> [-I dir]...")
    target, source = sys.argv[1], sys.argv[2]
    includes, args = [], sys.argv[3:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-I", "/I", "--include-dir") and i + 1 < len(args):
            includes.append(args[i + 1])
            i += 2
            continue
        if arg.startswith("--include-dir="):
            includes.append(arg[len("--include-dir="):])
        elif arg.startswith(("-I", "/I")):
            includes.append(arg[2:])
        i += 1
    depfile = target + ".d"
    text, deps = read_depfile(depfile)
    known = {os.path.normcase(os.path.abspath(dep)) for dep in deps}
    scanned = [source] + [dep for dep in deps if not dep.lower().endswith(HEADER_SUFFIXES)]
    extra, seen = [], set()
    for path in scanned:
        key = os.path.normcase(os.path.abspath(path))
        if key in seen:
            continue
        seen.add(key)
        directories = [os.path.dirname(source), os.path.dirname(path)] + includes + [os.getcwd()]
        for name in resource_files(path):
            found = resolve(name, directories)
            if found and os.path.normcase(found) not in known:
                known.add(os.path.normcase(found))
                extra.append(found)
    if not extra:
        return
    if text is None:
        text = escape(target) + ":"
    with open(depfile, "w", encoding="utf-8", errors="surrogateescape") as out:
        out.write(text.rstrip("\r\n") + "".join(" \\\n  " + escape(path) for path in extra) + "\n")


if __name__ == "__main__":
    main()
