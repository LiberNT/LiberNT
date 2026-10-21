#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

import argparse
import os
import shlex
import struct
import subprocess
import sys

INPUT_SUFFIXES = (".obj", ".o", ".a", ".lib", ".res", ".def")
VALUE_OPTIONS = {"-o", "-m", "-e", "-entry", "-L", "--out-implib", "--image-base", "--subsystem", "--stack", "--heap",
                 "--major-image-version", "--minor-image-version", "--major-os-version", "--minor-os-version",
                 "--major-subsystem-version", "--minor-subsystem-version", "--file-alignment", "--section-alignment",
                 "--pdb", "--native-def", "--version-script", "--exclude-symbols", "--require-defined", "--defsym",
                 "--wrap", "-u", "--undefined", "-Map"}


def final_command(ninja, build_dir, target):
    output = subprocess.run([ninja, "-C", build_dir, "-t", "commands", "-s", target],
                            check=True, capture_output=True, text=True).stdout
    segments = output.strip().splitlines()[-1].split(" && ")
    for index, segment in enumerate(segments):
        tokens = shlex.split(segment)
        if "-o" in tokens and tokens[tokens.index("-o") + 1] == target and \
                any(token.startswith("--target=") for token in tokens):
            return segment, segments[index + 1:]
    sys.exit("arm64x_link: no link command for %s in %s" % (target, build_dir))


def linker_argv(build_dir, driver_command):
    result = subprocess.run(driver_command + " -###", shell=True, cwd=build_dir,
                            capture_output=True, text=True)
    lines = [line for line in result.stderr.splitlines() if "ld.lld" in line]
    if result.returncode or not lines:
        sys.exit("arm64x_link: cannot expand %s\n%s" % (driver_command, result.stderr))
    return shlex.split(lines[-1])


def resource_only(path):
    if not path.endswith((".o", ".obj")):
        return False
    with open(path, "rb") as handle:
        data = handle.read(4096)
    if len(data) < 20:
        return False
    count, = struct.unpack_from("<H", data, 2)
    optional, = struct.unpack_from("<H", data, 16)
    names = [data[20 + optional + i * 40:20 + optional + i * 40 + 8].rstrip(b"\0") for i in range(count)]
    return bool(names) and all(name.startswith(b".rsrc") for name in names)


def find_library(name, search_dirs):
    for directory in search_dirs:
        for candidate in ("lib%s.dll.a" % name, "%s.dll.a" % name, "lib%s.a" % name, "%s.lib" % name, "lib%s.lib" % name):
            path = os.path.join(directory, candidate)
            if os.path.isfile(path):
                return path
    sys.exit("arm64x_link: cannot find native library -l%s" % name)


def native_inputs(build_dir, argv):
    search_dirs = [os.path.join(build_dir, argument[2:]) for argument in argv if argument.startswith("-L") and len(argument) > 2]
    search_dirs += [os.path.join(build_dir, argv[i + 1]) for i, argument in enumerate(argv[:-1]) if argument == "-L"]
    inputs = []
    skip = False
    whole_archive = False
    for argument in argv[1:]:
        if skip:
            skip = False
            continue
        if argument in VALUE_OPTIONS:
            skip = True
            continue
        if argument in ("--whole-archive", "--no-whole-archive"):
            whole_archive = argument == "--whole-archive"
            inputs.append(argument)
            continue
        if argument.startswith("-l") and len(argument) > 2:
            inputs.append(find_library(argument[2:], search_dirs))
            continue
        if argument.startswith("-") or not argument.endswith(INPUT_SUFFIXES):
            continue
        path = os.path.normpath(os.path.join(build_dir, argument))
        if not os.path.isfile(path) or path.endswith(".res") or resource_only(path):
            continue
        inputs.append("--native-def=" + path if path.endswith(".def") else path)
    if whole_archive:
        inputs.append("--no-whole-archive")
    return inputs


def drop_option(argv, option):
    while option in argv:
        index = argv.index(option)
        del argv[index:index + 2]


def image_base_object(clang, output):
    source = output + ".imagebase.s"
    obj = output + ".imagebase.obj"
    with open(source, "w") as handle:
        handle.write('.section .drectve,"yn"\n.ascii " -alternatename:__image_base__=__ImageBase"\n')
    subprocess.run([clang, "--target=aarch64-w64-mingw32", "-c", source, "-o", obj], check=True)
    return obj


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ninja", required=True)
    parser.add_argument("--native-dir", required=True)
    parser.add_argument("--native-target", required=True)
    parser.add_argument("--ec-dir", required=True)
    parser.add_argument("--ec-target", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    native_dir = os.path.abspath(args.native_dir)
    ec_dir = os.path.abspath(args.ec_dir)
    output = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output), exist_ok=True)

    native_link, native_post = final_command(args.ninja, native_dir, args.native_target)
    ec_link, _ = final_command(args.ninja, ec_dir, args.ec_target)

    argv = linker_argv(ec_dir, ec_link)
    argv[argv.index("-m") + 1] = "arm64xpe"
    argv[argv.index("-o") + 1] = output
    drop_option(argv, "--out-implib")
    extra = native_inputs(native_dir, linker_argv(native_dir, native_link))
    extra.append(image_base_object(shlex.split(native_link)[0], output))
    position = argv.index("--start-group") + 1 if "--start-group" in argv else argv.index("-o") + 2
    argv[position:position] = extra
    subprocess.run(argv, cwd=ec_dir, check=True)

    native_output = os.path.join(native_dir, args.native_target)
    post = [shlex.join(output if token in (args.native_target, native_output) else token
                       for token in shlex.split(segment))
            for segment in native_post]
    if post:
        subprocess.run(" && ".join(post), shell=True, cwd=native_dir, check=True)


if __name__ == "__main__":
    main()
