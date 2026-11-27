#!/usr/bin/env python3
# PROJECT:     LiberNT CI helpers
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Run clang-tidy on the lines a PR changed using the build's
#              own compile commands, and emit GitHub Actions annotations.
# COPYRIGHT:   Copyright 2026 LiberNT Contributors

"""Run clang-tidy over changed translation units in a configured build tree.

Usage:
    clang_check.py <build_dir> [--files F [F ...]] [--files-from FILE]
                   [--checks CHECKS] [--tidy-bin BIN] [--base REF]

Each source file is looked up in `ninja -t compdb` and clang-tidy runs with
the file's own compile command, restricted to the changed lines (`--base`
diffs REF...HEAD with -U0 and feeds the added ranges to --line-filter).
Only diagnostics on changed lines are reported. A translation unit that
cannot be analyzed is a tool failure: it is reported as a notice and does
not fail the job. The job fails only on error-level diagnostics on changed
lines.
"""

import argparse
import json
import os
import re
import shutil
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

HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", re.MULTILINE)


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


def find_tidy_bin():
    """clang-tidy of the toolchain the build uses: next to its clang first,
    then PATH. Version must match the build's compiler so the project's own
    headers parse."""
    clang = shutil.which("clang")
    if clang:
        candidate = os.path.join(os.path.dirname(os.path.realpath(clang)),
                                 "clang-tidy")
        if os.path.exists(candidate):
            return candidate
    return shutil.which("clang-tidy")


def changed_lines(path, base):
    """Added-line ranges of `path` in `git diff -U0 base...HEAD`, in the
    repository-relative form clang-tidy expects for --line-filter."""
    try:
        out = subprocess.run(
            ["git", "diff", "-U0", f"{base}...HEAD", "--", path],
            check=True, capture_output=True, text=True).stdout
    except subprocess.CalledProcessError:
        return None
    lines = []
    for m in HUNK_RE.finditer(out):
        start = int(m.group(1))
        count = int(m.group(2) or "1")
        if count:
            lines.append([start, start + count - 1])
    return lines


def run_tidy(tidy_bin, entry, checks, build_dir, line_filter):
    tmpdir = os.path.join(build_dir, ".clang-check")
    os.makedirs(tmpdir, exist_ok=True)
    db_path = os.path.join(tmpdir, "compile_commands.json")
    with open(db_path, "w") as f:
        json.dump([entry], f)
    cmd = [tidy_bin, entry["file"], "-p", tmpdir, "--checks=" + checks]
    if line_filter is not None:
        cmd.append("--line-filter=" + json.dumps(
            [{"name": entry["file"], "lines": line_filter}]))
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def in_changed_lines(path, line, changed):
    """True when a diagnostic falls inside the changed ranges of `path`
    (or no range information exists for it)."""
    norm = os.path.normpath(path)
    for changed_path, ranges in changed.items():
        if norm.endswith(changed_path) or changed_path.endswith(norm):
            if ranges is None:
                return True
            return any(start <= line <= end for start, end in ranges)
    return False


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
    ap.add_argument("--tidy-bin", default=None,
                    help="clang-tidy binary; default is the one next to the "
                         "clang on PATH")
    ap.add_argument("--base", metavar="REF",
                    help="git ref the changes are diffed against; restricts "
                         "diagnostics to lines added since REF")
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

    tidy_bin = args.tidy_bin or find_tidy_bin()
    if not tidy_bin:
        print("::notice title=clang-check::no clang-tidy in the build "
              "toolchain; analysis skipped")
        return 0

    changed = {}
    if args.base:
        for path in files:
            changed[path] = changed_lines(path, args.base)

    entries = compdb(args.build_dir)
    all_diags = []
    analyzed = skipped = failed = 0
    for path in files:
        entry = matching_entry(entries, path)
        if entry is None:
            print(f"clang-check: no compile command for {path}, skipped")
            skipped += 1
            continue
        rc, out = run_tidy(tidy_bin, entry, args.checks, args.build_dir,
                           changed.get(path))
        if rc != 0 or "Error while processing" in out:
            # A translation unit the tool cannot analyze is a tool failure,
            # not a defect of the change.
            print(f"::notice title=clang-check::clang-tidy could not "
                  f"analyze {path}")
            print(f"clang-check: clang-tidy failed on {path}")
            sys.stdout.write(out)
            failed += 1
            continue
        for m in DIAG_RE.finditer(out):
            if m.group("sev") in ("note", "remark"):
                continue
            file, line = m.group("file"), int(m.group("line"))
            if args.base and not in_changed_lines(file, line, changed):
                continue
            all_diags.append((file, line, int(m.group("col")),
                              m.group("sev"), m.group("msg"),
                              m.group("check") or ""))
        analyzed += 1

    errors, warnings = annotate(all_diags, github=not args.no_annotations)
    print(f"clang-check: {analyzed} analyzed, {skipped} skipped, "
          f"{failed} unanalyzable, {warnings} warnings, {errors} errors")

    if errors:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
