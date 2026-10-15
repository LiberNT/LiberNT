#!/usr/bin/env python3
"""
PROJECT:     LiberNT build tools
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Build the script registrations of the system DLLs into a hive INF file
COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
"""

import struct
import sys

SYSTEM32 = "%SystemRoot%\\system32"
SYSWOW64 = "%SystemRoot%\\SysWOW64"
MODULE_MARK = "\x01M\x01"
SYSROOT_MARK = "\x01R\x01"

REG_SZ = 1
REG_EXPAND_SZ = 2
REG_BINARY = 3
REG_DWORD = 4

ROOTS = {
    "HKEY_CLASSES_ROOT": ("HKLM", "SOFTWARE\\Classes"),
    "HKCR": ("HKLM", "SOFTWARE\\Classes"),
    "HKEY_LOCAL_MACHINE": ("HKLM", ""),
    "HKLM": ("HKLM", ""),
    "HKEY_CURRENT_USER": ("HKCU", ""),
    "HKCU": ("HKCU", ""),
}

REDIRECTED_CLASSES = ("clsid", "directshow", "interface", "media type", "mediafoundation")

SHARED_SOFTWARE_KEYS = (
    "clients",
    "microsoft\\com3",
    "microsoft\\cryptography\\calais\\current",
    "microsoft\\cryptography\\calais\\readers",
    "microsoft\\cryptography\\services",
    "microsoft\\ctf\\systemshared",
    "microsoft\\ctf\\tip",
    "microsoft\\dfs",
    "microsoft\\driver signing",
    "microsoft\\enterprisecertificates",
    "microsoft\\eventsystem",
    "microsoft\\msmq",
    "microsoft\\non-driver signing",
    "microsoft\\notepad\\defaultfonts",
    "microsoft\\ole",
    "microsoft\\ras",
    "microsoft\\rpc",
    "microsoft\\systemcertificates",
    "microsoft\\termservlicensing",
    "microsoft\\transaction server",
    "microsoft\\windows\\currentversion\\app paths",
    "microsoft\\windows\\currentversion\\control panel\\cursors\\schemes",
    "microsoft\\windows\\currentversion\\explorer\\autoplayhandlers",
    "microsoft\\windows\\currentversion\\explorer\\driveicons",
    "microsoft\\windows\\currentversion\\explorer\\kindmap",
    "microsoft\\windows\\currentversion\\group policy",
    "microsoft\\windows\\currentversion\\policies",
    "microsoft\\windows\\currentversion\\previewhandlers",
    "microsoft\\windows\\currentversion\\setup",
    "microsoft\\windows\\currentversion\\telephony\\locations",
    "microsoft\\windows nt\\currentversion\\console",
    "microsoft\\windows nt\\currentversion\\fontdpi",
    "microsoft\\windows nt\\currentversion\\fontlink",
    "microsoft\\windows nt\\currentversion\\fontmapper",
    "microsoft\\windows nt\\currentversion\\fonts",
    "microsoft\\windows nt\\currentversion\\fontsubstitutes",
    "microsoft\\windows nt\\currentversion\\gre_initialize",
    "microsoft\\windows nt\\currentversion\\image file execution options",
    "microsoft\\windows nt\\currentversion\\languagepack",
    "microsoft\\windows nt\\currentversion\\networkcards",
    "microsoft\\windows nt\\currentversion\\perflib",
    "microsoft\\windows nt\\currentversion\\ports",
    "microsoft\\windows nt\\currentversion\\print",
    "microsoft\\windows nt\\currentversion\\profilelist",
    "microsoft\\windows nt\\currentversion\\time zones",
    "policies",
    "registeredapplications",
)

class ScriptError(Exception):
    pass


def read_inf_section(lines, name):
    out = []
    active = False
    for line in lines:
        text = line.split(";", 1)[0].strip()
        if not text:
            continue
        if text.startswith("["):
            active = text[1:text.index("]")].strip().lower() == name.lower()
            continue
        if active:
            out.append([field.strip() for field in text.split(",")])
    return out


class PeImage:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        nt = struct.unpack_from("<I", d, 0x3C)[0]
        if d[nt:nt + 4] != b"PE\0\0":
            raise ValueError("%s is not a PE image" % path)
        nsec, optsize = struct.unpack_from("<H12xH", d, nt + 6)
        magic = struct.unpack_from("<H", d, nt + 24)[0]
        ddir = nt + 24 + (112 if magic == 0x20B else 96)
        self.rsrc_rva = struct.unpack_from("<I", d, ddir + 2 * 8)[0]
        self.sections = []
        sec = nt + 24 + optsize
        for i in range(nsec):
            vsize, va, rsize, rptr = struct.unpack_from("<IIII", d, sec + 40 * i + 8)
            self.sections.append((va, max(vsize, rsize), rptr))

    def offset(self, rva):
        for va, size, rptr in self.sections:
            if va <= rva < va + size:
                return rva - va + rptr
        raise ValueError("RVA 0x%x outside the image" % rva)

    def resources(self, rtype):
        if not self.rsrc_rva:
            return []
        d = self.data
        base = self.offset(self.rsrc_rva)

        def entries(off):
            named, ids = struct.unpack_from("<HH", d, base + off + 12)
            out = []
            for i in range(named + ids):
                key, ptr = struct.unpack_from("<II", d, base + off + 16 + 8 * i)
                if key & 0x80000000:
                    n = struct.unpack_from("<H", d, base + (key & 0x7FFFFFFF))[0]
                    start = base + (key & 0x7FFFFFFF) + 2
                    key = d[start:start + 2 * n].decode("utf-16-le")
                out.append((key, ptr & 0x7FFFFFFF))
            return out

        out = []
        for tkey, tptr in entries(0):
            if not isinstance(tkey, str) or tkey.upper() != rtype:
                continue
            for nkey, nptr in entries(tptr):
                for _, lptr in entries(nptr):
                    rva, size = struct.unpack_from("<II", d, base + lptr)
                    start = self.offset(rva)
                    out.append((nkey, d[start:start + size]))
        return out


class Registry:
    def __init__(self):
        self.keys = {}
        self.order = []

    def _norm(self, root, path):
        return (root, "\\".join(p.lower() for p in path.split("\\") if p))

    def create(self, root, path):
        parts = [p for p in path.split("\\") if p]
        for i in range(1, len(parts) + 1):
            key = self._norm(root, "\\".join(parts[:i]))
            if key not in self.keys:
                prefix = self.keys[self._norm(root, "\\".join(parts[:i - 1]))]["name"] if i > 1 else ""
                name = (prefix + "\\" if prefix else "") + parts[i - 1]
                self.keys[key] = {"root": root, "name": name, "values": {}}
                self.order.append(key)
        return self.keys[self._norm(root, path)]

    def delete_tree(self, root, path):
        key = self._norm(root, path)
        prefix = key[1] + "\\"
        doomed = [k for k in self.keys if k[0] == root and (k[1] == key[1] or k[1].startswith(prefix))]
        for k in doomed:
            del self.keys[k]
        if doomed:
            self.order = [k for k in self.order if k in self.keys]

    def set_value(self, root, path, name, vtype, data):
        values = self.create(root, path)["values"]
        values[(name or "").lower()] = (name or "", vtype, data)


def wow64_view(root, path):
    parts = path.split("\\")
    if parts[0].lower() != "software" or (len(parts) > 1 and parts[1].lower() == "wow6432node"):
        return path
    if len(parts) > 1 and parts[1].lower() == "classes":
        if len(parts) > 2 and parts[2].lower() in REDIRECTED_CLASSES:
            return "\\".join(parts[:2] + ["Wow6432Node"] + parts[2:])
        return path
    if root != "HKLM":
        return path
    for i in range(2, len(parts) + 1):
        if "\\".join(parts[1:i]).lower() in SHARED_SOFTWARE_KEYS:
            return path
    return "\\".join(parts[:1] + ["Wow6432Node"] + parts[1:])


def merge_wow64(registry, wow64):
    for key in wow64.order:
        entry = wow64.keys[key]
        path = wow64_view(entry["root"], entry["name"])
        values = registry.create(entry["root"], path)["values"]
        for vname, value in entry["values"].items():
            values.setdefault(vname, value)


def join_key(base, sub):
    return base + "\\" + sub if base else sub


class RgsScript:
    def __init__(self, text, replacements, atl):
        self.text = text
        self.replacements = {k.lower(): v for k, v in replacements.items()}
        self.atl = atl
        self.pos = 0

    def preprocess(self):
        out = []
        text = self.text
        i = 0
        while True:
            j = text.find("%", i)
            if j < 0:
                out.append(text[i:])
                break
            out.append(text[i:j])
            k = text.find("%", j + 1)
            if k < 0:
                raise ScriptError("unterminated replacement")
            name = text[j + 1:k]
            if not name:
                out.append("%")
            elif name.lower() in self.replacements:
                out.append(self.replacements[name.lower()])
            else:
                raise ScriptError("no replacement for %%%s%%" % name)
            i = k + 1
        self.text = "".join(out)
        self.pos = 0

    def word(self):
        t = self.text
        n = len(t)
        p = self.pos
        while p < n and t[p].isspace():
            p += 1
        if p >= n:
            self.pos = p
            return ""
        if t[p] in "}=":
            w = t[p]
            p += 1
        elif t[p] == "'":
            parts = []
            p += 1
            while True:
                q = t.find("'", p)
                if q < 0:
                    raise ScriptError("unterminated string")
                if not self.atl and q + 1 < n and t[q + 1] == "'":
                    parts.append(t[p:q + 1])
                    p = q + 2
                    continue
                parts.append(t[p:q])
                p = q + 1
                break
            w = "".join(parts)
        else:
            q = p
            while q < n and not t[q].isspace():
                q += 1
            w = t[p:q]
            p = q
        while p < n and t[p].isspace():
            p += 1
        self.pos = p
        return w

    def peek(self):
        return self.text[self.pos] if self.pos < len(self.text) else ""

    def value(self, kind, raw):
        if kind == "s":
            return REG_SZ, raw
        if kind == "e" and self.atl:
            return REG_EXPAND_SZ, raw
        if kind == "d":
            if self.atl and (raw[:2] == "0x" or raw[:2] == "&H"):
                return REG_DWORD, int(raw[2:] or "0", 16) & 0xFFFFFFFF
            text = raw.lstrip()
            sign = -1 if text[:1] == "-" else 1
            if text[:1] in "+-":
                text = text[1:]
            digits = ""
            for c in text:
                if not c.isdigit():
                    break
                digits += c
            return REG_DWORD, (sign * int(digits or "0")) & 0xFFFFFFFF
        if kind == "b":
            if self.atl and len(raw) % 2:
                raise ScriptError("odd binary length")
            return REG_BINARY, bytes.fromhex(raw[:len(raw) // 2 * 2])
        raise ScriptError("unknown value type %r" % kind)

    def process_key(self, ops, root, parent):
        w = self.word()
        while w != "}":
            if w == "":
                raise ScriptError("unexpected end of script")
            kind = "normal"
            low = w.lower()
            if low == "noremove":
                kind = "noremove"
            elif low == "forceremove":
                kind = "forceremove"
            elif low == "val":
                kind = "val"
            elif low == "delete":
                kind = "delete"
            if kind != "normal":
                w = self.word()
            name = w
            path = parent
            if kind == "delete":
                ops.append(("delete", root, join_key(parent, name)))
            elif kind != "val":
                path = join_key(parent, name)
                if kind == "forceremove":
                    ops.append(("delete", root, path))
                ops.append(("create", root, path))
            if kind != "delete" and self.peek() == "=":
                self.pos += 1
                t = self.word()
                if len(t) != 1:
                    raise ScriptError("bad value type %r" % t)
                vtype, data = self.value(t, self.word())
                ops.append(("set", root, path, name if kind == "val" else "", vtype, data))
            elif kind == "val":
                raise ScriptError("value without data")
            if kind not in ("val", "delete") and self.peek() == "{" and \
                    self.pos + 1 < len(self.text) and self.text[self.pos + 1].isspace():
                self.word()
                self.process_key(ops, root, path)
            w = self.word()

    def operations(self):
        self.preprocess()
        ops = []
        w = self.word()
        while self.pos < len(self.text):
            if not w:
                raise ScriptError("empty root key")
            if w.upper() not in ROOTS:
                raise ScriptError("unsupported root key %s" % w)
            root, base = ROOTS[w.upper()]
            if self.word() != "{":
                raise ScriptError("expected '{'")
            self.process_key(ops, root, base)
            w = self.word()
        return ops


def decode_script(data):
    if data[:2] == b"\xff\xfe":
        return data[2:].decode("utf-16-le")
    if len(data) > 1 and data[1] == 0:
        return data.decode("utf-16-le")
    if data[:3] == b"\xef\xbb\xbf":
        data = data[3:]
    return data.decode("utf-8")


def module_path(subdir, name, root=SYSTEM32):
    return root + "\\" + (subdir + "\\" if subdir else "") + name


def script_ops(image, rtype, atl, warnings, label):
    ops = []
    replacements = {"MODULE": MODULE_MARK, "SystemRoot": SYSROOT_MARK}
    if atl:
        replacements = {"Module": MODULE_MARK, "Module_Raw": MODULE_MARK, "APPID": ""}
    for name, data in image.resources(rtype):
        try:
            ops.extend(RgsScript(decode_script(data), replacements, atl).operations())
        except (ScriptError, UnicodeDecodeError, ValueError) as e:
            warnings.append("%s: %s %s: %s" % (label, rtype, name, e))
    return ops


def resolve(text, path):
    return text.replace(MODULE_MARK, path).replace(SYSROOT_MARK, "%SystemRoot%")


def apply(registry, ops, path):
    for op in ops:
        if op[0] == "delete":
            registry.delete_tree(op[1], resolve(op[2], path))
        elif op[0] == "create":
            registry.create(op[1], resolve(op[2], path))
        else:
            kind, root, key, name, vtype, data = op
            if vtype in (REG_SZ, REG_EXPAND_SZ) and (MODULE_MARK in data or SYSROOT_MARK in data):
                if name.lower() != "image path":
                    vtype = REG_EXPAND_SZ
                data = resolve(data, path)
            registry.set_value(root, resolve(key, path), resolve(name, path), vtype, data)


def inf_string(text):
    return '"' + text.replace('"', '""').replace("%", "%%") + '"'


def write_inf(registry, out):
    out.write("; Generated by sdk/tools/mkcomreg.py. Do not edit.\n\n")
    out.write('[Version]\nSignature = "$Windows NT$"\n\n[AddReg]\n')
    for key in registry.order:
        entry = registry.keys[key]
        root = entry["root"]
        name = entry["name"]
        if root == "HKLM" and name.lower().startswith("software\\classes\\"):
            root, name = "HKCR", name[len("software\\classes\\"):]
        elif root == "HKLM" and name.lower() in ("software", "software\\classes"):
            continue
        head = "%s,%s" % (root, inf_string(name))
        if not entry["values"]:
            out.write("%s,,0x00000010\n" % head)
            continue
        for vname, vtype, data in entry["values"].values():
            if vtype == REG_SZ:
                out.write("%s,%s,0x00000000,%s\n" % (head, inf_string(vname), inf_string(data)))
            elif vtype == REG_EXPAND_SZ:
                out.write("%s,%s,0x00020000,%s\n" % (head, inf_string(vname), inf_string(data)))
            elif vtype == REG_DWORD:
                out.write("%s,%s,0x00010001,0x%08x\n" % (head, inf_string(vname), data))
            elif vtype == REG_BINARY:
                out.write("%s,%s,0x00000001%s\n" % (head, inf_string(vname),
                                                    "".join(",%02x" % b for b in data)))
            else:
                raise ValueError("unsupported value type %d" % vtype)


def register(registry, modules, subdir, name, warnings, root=SYSTEM32):
    image_path = modules.get(name.lower())
    if not image_path:
        warnings.append("%s: no module in this build" % name)
        return
    image = PeImage(image_path)
    path = module_path(subdir, name, root)
    apply(registry, script_ops(image, "WINE_REGISTRY", False, warnings, name), path)
    apply(registry, script_ops(image, "REGISTRY", True, warnings, name), path)


def build(inf, modules, wow64_modules, warnings):
    entries = [tuple(f[:4]) for f in read_inf_section(inf, "OleControlDlls") if len(f) >= 4]
    code = [tuple(f[:4]) for f in read_inf_section(inf, "OleControlDllsCode") if len(f) >= 4]
    keys = [tuple(x.lower() for x in entry) for entry in entries]
    last = -1
    for entry in code:
        key = tuple(x.lower() for x in entry)
        following = [i for i in range(last + 1, len(keys)) if keys[i] == key]
        if not following:
            raise ValueError("[OleControlDllsCode] entry %s is not in [OleControlDlls] order" % ",".join(entry))
        last = following[0]
    code_names = {entry[2].lower() for entry in code}

    registry = Registry()
    wow64 = Registry()
    for dirid, subdir, name, flags in entries:
        if name.lower() in code_names:
            continue
        if dirid != "11" or int(flags or "1") & ~1:
            raise ValueError("%s must be registered at run time; add it to [OleControlDllsCode]" % name)
        register(registry, modules, subdir, name, warnings)
        if name.lower() in wow64_modules:
            register(wow64, wow64_modules, subdir, name, warnings, SYSWOW64)
    for fields in read_inf_section(inf, "TypeLibraries"):
        if len(fields) > 1 and fields[1]:
            raise ValueError("type library %s is not in the system directory" % fields[0])
        if fields[0].lower() in wow64_modules:
            register(wow64, wow64_modules, "", fields[0], warnings, SYSWOW64)
        register(registry, modules, "", fields[0], warnings)
    merge_wow64(registry, wow64)
    return registry


def read_modules(path):
    modules = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if "|" in line:
                name, image = line.rstrip("\n").split("|", 1)
                modules[name.lower()] = image
    return modules


def main(argv):
    if len(argv) != 5:
        sys.stderr.write("usage: mkcomreg.py <syssetup.inf> <modules.txt> <wow64_modules.txt> <output.inf>\n")
        return 2
    with open(argv[1], encoding="utf-8-sig", errors="replace") as f:
        inf = f.read().splitlines()
    modules = read_modules(argv[2])
    wow64_modules = read_modules(argv[3])
    warnings = []
    try:
        registry = build(inf, modules, wow64_modules, warnings)
    except ValueError as e:
        sys.stderr.write("mkcomreg: error: %s\n" % e)
        return 1
    for w in warnings:
        sys.stderr.write("mkcomreg: warning: %s\n" % w)
    with open(argv[4], "w", encoding="utf-16", newline="\r\n") as out:
        write_inf(registry, out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
