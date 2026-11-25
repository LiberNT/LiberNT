#!/usr/bin/env python3
#
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Checks that WimBootExtract writes back each image of a two-image WIM exactly
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

import argparse
import filecmp
import os
import random
import subprocess
import sys
import tempfile


def tree(root, files):
    for path, data in files.items():
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        if data is None:
            os.makedirs(full, exist_ok=True)
        else:
            with open(full, "wb") as stream:
                stream.write(data)


def same(left, right):
    compare = filecmp.dircmp(left, right)
    if compare.left_only or compare.right_only or compare.funny_files:
        return False
    _, mismatch, errors = filecmp.cmpfiles(left, right, compare.common_files, shallow=False)
    return not mismatch and not errors and all(same(os.path.join(left, name), os.path.join(right, name))
                                               for name in compare.common_dirs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--extract", required=True)
    parser.add_argument("--wimage", required=True)
    args = parser.parse_args()
    random.seed(7)
    shared = bytes(random.getrandbits(8) for _ in range(3 * 1024 * 1024 + 17))
    failures = 0

    with tempfile.TemporaryDirectory() as work:
        live, installed = os.path.join(work, "live"), os.path.join(work, "installed")
        tree(live, {"Windows/system32/ntoskrnl.exe": shared, "Windows/system32/config/system": b"live hive" * 999,
                    "amd64/txtsetup.sif": b"[Version]\n", "Windows/Temp": None})
        tree(installed, {"Windows/system32/ntoskrnl.exe": shared, "Windows/system32/config/system": b"installed hive" * 777,
                         "Windows/system32/config/BCD-Template": b"regf" + bytes(4092), "Program Files/Common Files": None,
                         "Users/Default/NTUSER.DAT": b"", "Windows/System32/Drivers/etc/hosts": b"127.0.0.1 localhost\n"})
        wim = os.path.join(work, "boot.wim")
        subprocess.run([args.wimage, "capture", live, wim, "LiberNT", "--boot", "--compress=xpress",
                        "--add-image=LiberNT Installation=" + installed], check=True, stdout=subprocess.DEVNULL)

        for index, source in ((2, installed), (1, live), (0, live)):
            out = os.path.join(work, "out%d" % index)
            os.mkdir(out)
            result = subprocess.run([args.extract, wim, str(index), out], capture_output=True, text=True)
            ok = result.returncode == 0 and same(source, out)
            failures += not ok
            print("%s: image %d extracts to the tree it was captured from (%s)" % ("PASS" if ok else "FAIL", index,
                                                                                      result.stdout.strip()))

        result = subprocess.run([args.extract, wim, "3", os.path.join(work, "none")], capture_output=True, text=True)
        ok = result.returncode == 4
        failures += not ok
        print("%s: an image past the last one is refused" % ("PASS" if ok else "FAIL"))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
