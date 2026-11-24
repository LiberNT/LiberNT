#!/usr/bin/env python3
# PROJECT:     LiberNT CI helpers
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Run clang-tidy on a set of translation units using the build's
#              own compile commands, and emit GitHub Actions annotations.
# COPYRIGHT:   Copyright 2026 LiberNT Contributors

"""Run clang-tidy over changed translation units in a configured build tree.

Usage:
    clang_check.py <build_dir> [--files F [F ...]] [--files-from FILE]
                   [--checks CHECKS] [--tidy-bin BIN] [--fail-on warning|error]

Each source file is looked up in `ninja -t compdb`. Its object target is built
first, so generated headers the translation unit includes already exist. Then
clang-tidy runs with the file's compile command. Diagnostics are printed as
::warning / ::error GitHub annotations and mirrored to stdout.
"""

import argparse
import json
import os
import re
import subprocess
import sys

DEFAULT_CHECKS = (
    "clang-analyzer-*,"
    "bugprone-argument-comment,bugprone-assert-side-effect,"
    "bugprone-assignment-in-if-condition,bugprone-branch-clone,"
    "bugprone-dangling-handle,bugprone-infinite-loop,"
    "bugprone-macro-repeated-side-effects,bugprone-misplaced-widening-cast,"
    "bugprone-sizeof-expression,bugprone-suspicious-string-compare,"
    "bugprone-undefined-memory-manipulation,bugprone-unused-return-value,"
    "bugprone-use-after-move"
)

DIAG_RE = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): "
    r"(?P<sev>warning|error|note|remark): (?P<msg>.*?)(?: \[(?P<check>[^\]]+)\])?$",
    re.MULTILINE,
)


def compdb(build_dir):
    out = subprocess.run(
        ["ninja", "-C", build_dir, "-t", "compdb"],
        check=True, capture_output=True, text=True).stdout
    entries = []
    for e in json.loads(out):
        e["file"] = os.path.normpath(e["file"])
        entries.append(e)
    return entries


def matching_entry(entries, path):
    path = os.path.normpath(path)
    for e in entries:
        if e["file"] == path:
            return e
    for e in entries:
        if e["file"].endswith("/" + path) or e["file"].endswith(os.sep + path):
            return e
    return None


def build_object(build_dir, entry):
    output = entry.get("output")
    if not output:
        return True
    r = subprocess.run(["ninja", "-C", build_dir, output],
                       capture_output=True, text=True)
    return r.returncode == 0


def tidy_resource_dir(tidy_bin):
    version = subprocess.run([tidy_bin, "--version"],
                             capture_output=True, text=True).stdout
    m = re.search(r"version (\d+)", version)
    if not m:
        return None
    major = m.group(1)
    for root in ("/usr/lib", "/usr/lib64", "/usr/local/lib"):
        for dirpath, dirnames, _ in os.walk(root):
            if os.path.basename(dirpath) == "include" and \
               os.path.basename(os.path.dirname(dirpath)) == major and \
               os.path.basename(os.path.dirname(os.path.dirname(dirpath))) == "clang":
                return dirpath
    return None


def run_tidy(tidy_bin, entry, checks, build_dir, resource_dir):
    tmpdir = os.path.join(build_dir, ".clang-check")
    os.makedirs(tmpdir, exist_ok=True)
    command = entry["command"]
    if resource_dir:
        command = re.sub(r"-isystem \S+/lib/clang/\d+/include",
                         "-isystem " + resource_dir, command)
    entry = dict(entry, command=command)
    db_path = os.path.join(tmpdir, "compile_commands.json")
    with open(db_path, "w") as f:
        json.dump([entry], f)
    r = subprocess.run(
        [tidy_bin, entry["file"], "-p", tmpdir,
         "--checks=" + checks],
        capture_output=True, text=True)
    return r.stdout + r.stderr


def annotate(diags, github):
    errors = 0
    warnings = 0
    for file, line, col, sev, msg, check in diags:
        title = ("clang-tidy " + check) if check else "clang-tidy"
        text = f"{file}:{line}:{col}: {sev}: {msg}"
        if check:
            text += f" [{check}]"
        if github and sev in ("warning", "error"):
            print(f"::{sev} file={file},line={line},col={col},title={title}::{msg}")
        print(text)
        if sev == "error":
            errors += 1
        elif sev == "warning":
            warnings += 1
    return errors, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("build_dir")
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--files-from", metavar="FILE")
    ap.add_argument("--checks", default=DEFAULT_CHECKS)
    ap.add_argument("--tidy-bin", default="clang-tidy")
    ap.add_argument("--fail-on", choices=["warning", "error"], default="error")
    ap.add_argument("--no-annotations", action="store_true")
    args = ap.parse_args()

    files = list(args.files)
    if args.files_from:
        with open(args.files_from) as f:
            files += [l.strip() for l in f if l.strip()]
    files = [f for f in files if f.endswith((".c", ".cpp", ".cxx", ".cc"))]

    if not files:
        print("clang-check: no translation units to analyze")
        return 0

    entries = compdb(args.build_dir)
    resource_dir = tidy_resource_dir(args.tidy_bin)
    all_diags = []
    analyzed = skipped = failed = 0
    for path in files:
        entry = matching_entry(entries, path)
        if entry is None:
            print(f"clang-check: no compile command for {path}, skipped")
            skipped += 1
            continue
        if not build_object(args.build_dir, entry):
            print(f"clang-check: failed to build dependencies of {path}, skipped")
            failed += 1
            continue
        out = run_tidy(args.tidy_bin, entry, args.checks, args.build_dir,
                       resource_dir)
        if "Error while processing" in out:
            print(f"clang-check: clang-tidy failed on {path}")
            sys.stdout.write(out)
            failed += 1
            continue
        for m in DIAG_RE.finditer(out):
            if m.group("sev") == "note":
                continue
            all_diags.append((m.group("file"), int(m.group("line")),
                              int(m.group("col")), m.group("sev"),
                              m.group("msg"), m.group("check") or ""))
        analyzed += 1

    errors, warnings = annotate(all_diags, github=not args.no_annotations)
    print(f"clang-check: {analyzed} analyzed, {skipped} skipped, "
          f"{failed} dependency failures, {warnings} warnings, {errors} errors")

    if failed:
        return 2
    if errors or (args.fail_on == "warning" and warnings):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
