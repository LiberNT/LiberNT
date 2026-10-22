#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

CACHE_KEYS = (
    "CMAKE_TOOLCHAIN_FILE",
    "CMAKE_MAKE_PROGRAM",
    "REACTOS_CLANG_LLVM_MINGW_ROOT",
    "MESA_REACTOS_SDK",
    "Python3_EXECUTABLE",
    "BISON_EXECUTABLE",
    "FLEX_EXECUTABLE",
    "REACTOS_SDK_ARCH",
    "REACTOS_SDK_TRIPLE",
    "REACTOS_SDK_ARCH_FLAGS",
    "REACTOS_SDK_SOURCE_DIR",
    "REACTOS_SDK_BUILD_DIR",
    "REACTOS_SDK_LIBDIR",
    "REACTOS_SDK_CXX_RUNTIME",
    "MESA_REACTOS_SOURCE_DIR",
    "MESA_REACTOS_BUILD_DIR",
    "MESA_ARCH",
)
WGL_NAME = "mesa_llvmpipe"
WGL_TARGET = "src/gallium/targets/wgl/%s.dll" % WGL_NAME
LVP_DLL = "src/gallium/targets/lavapipe/vulkan_lvp.dll"
LVP_MANIFEST = "src/gallium/targets/lavapipe/lvp_icd.json"


def run(args, cwd=None):
    print("+ " + " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, check=True)


def read_cache(path):
    values = {}
    with open(path, encoding="utf-8") as cache:
        for line in cache:
            line = line.rstrip("\n")
            if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
                continue
            key, value = line.split("=", 1)
            name, kind = key.split(":", 1)
            if name in CACHE_KEYS:
                values[name] = (kind, value)
    missing = [key for key in CACHE_KEYS if key not in values]
    if missing:
        sys.exit("%s lacks %s; build mesa_gallium in that build directory first" % (path, ", ".join(missing)))
    return values


def build_mesa(source, mesa_build_dir, work, llvm_root, directx_headers, lavapipe):
    cache = read_cache(os.path.join(mesa_build_dir, "CMakeCache.txt"))
    args = ["cmake", "-S", source, "-B", work, "-G", "Ninja"]
    for key, (kind, value) in cache.items():
        args.append("-D%s:%s=%s" % (key, kind, value))
    args += [
        "-DCMAKE_BUILD_TYPE:STRING=Release",
        "-DMESA_WGL_DLL_NAME:STRING=%s" % WGL_NAME,
        "-DMESA_ASSERTIONS:BOOL=OFF",
        "-DMESA_V3D:BOOL=OFF",
        "-DMESA_LLVMPIPE:BOOL=ON",
        "-DMESA_LLVM_ROOT:PATH=%s" % llvm_root,
        "-DMESA_LAVAPIPE:BOOL=%s" % ("ON" if lavapipe else "OFF"),
    ]
    if lavapipe:
        args.append("-DMESA_DIRECTX_HEADERS_ROOT:PATH=%s" % directx_headers)
    run(args)
    targets = ["mesa_gallium"]
    if lavapipe:
        targets += ["vulkan_lvp", "lvp_icd"]
    env = dict(os.environ, CCACHE_DISABLE="1", SCCACHE_DISABLE="1")
    print("+ ninja -C %s %s" % (work, " ".join(targets)), flush=True)
    subprocess.run([cache["CMAKE_MAKE_PROGRAM"][1], "-C", work] + targets, check=True, env=env)
    return cache


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Build the LiberNT LLVMpipe/Lavapipe release payload")
    parser.add_argument("--arch", required=True, choices=("amd64", "arm64"))
    parser.add_argument("--source-dir", required=True, help="LiberNT source tree")
    parser.add_argument("--build-dir", required=True, help="LiberNT build directory that already built mesa_gallium")
    parser.add_argument("--llvm-root", required=True, help="Static Windows LLVM 22 install for the native architecture")
    parser.add_argument("--llvm-root-ec", help="Static Windows LLVM 22 install for ARM64EC (arm64 only)")
    parser.add_argument("--llvm-license", required=True, help="LLVM LICENSE.TXT")
    parser.add_argument("--directx-headers", required=True, help="Installed DirectX-Headers tree")
    parser.add_argument("--directx-headers-license", required=True, help="DirectX-Headers LICENSE")
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--release", required=True, help="Payload release name, for example 26.2.2-1")
    parser.add_argument("--output", required=True, help="Output .zip")
    args = parser.parse_args()

    source = os.path.abspath(args.source_dir)
    build = os.path.abspath(args.build_dir)
    work = os.path.abspath(args.work_dir)
    mesa_source = os.path.join(source, "submodules", "mesa")
    with open(os.path.join(mesa_source, "VERSION"), encoding="utf-8") as stream:
        mesa_version = stream.read().strip()
    mesa_commit = subprocess.run(["git", "-C", mesa_source, "rev-parse", "HEAD"],
                                 capture_output=True, text=True).stdout.strip()
    mesa_dirty = bool(subprocess.run(["git", "-C", mesa_source, "status", "--porcelain", "--untracked-files=no"],
                                     capture_output=True, text=True).stdout.strip())

    native_mesa = os.path.join(build, "dll", "opengl", "mesa_gallium", "mesa-source", "cmake-build")
    native_work = os.path.join(work, "mesa-%s" % args.arch)
    cache = build_mesa(mesa_source, native_mesa, native_work, os.path.abspath(args.llvm_root),
                       os.path.abspath(args.directx_headers), True)
    wgl = os.path.join(native_work, WGL_TARGET)

    if args.arch == "arm64":
        if not args.llvm_root_ec:
            sys.exit("--llvm-root-ec is required for arm64")
        ec_mesa = os.path.join(build, "_fex_arm64ec", "dll", "opengl", "mesa_gallium", "mesa-source", "cmake-build")
        ec_work = os.path.join(work, "mesa-arm64ec")
        build_mesa(mesa_source, ec_mesa, ec_work, os.path.abspath(args.llvm_root_ec), None, False)
        wgl = os.path.join(work, "arm64x", "%s.dll" % WGL_NAME)
        os.makedirs(os.path.dirname(wgl), exist_ok=True)
        run([sys.executable, os.path.join(source, "sdk", "tools", "arm64x_link.py"),
             "--ninja", cache["CMAKE_MAKE_PROGRAM"][1],
             "--native-dir", native_work, "--native-target", WGL_TARGET,
             "--ec-dir", ec_work, "--ec-target", WGL_TARGET,
             "--output", wgl])

    stage = os.path.join(work, "stage-%s" % args.arch)
    shutil.rmtree(stage, ignore_errors=True)
    system32 = os.path.join(stage, "system32")
    licenses = os.path.join(stage, "licenses")
    os.makedirs(system32)
    os.makedirs(licenses)
    strip = os.path.join(cache["REACTOS_CLANG_LLVM_MINGW_ROOT"][1], "bin", "llvm-strip")
    for path in (wgl, os.path.join(native_work, LVP_DLL)):
        target = os.path.join(system32, os.path.basename(path))
        shutil.copyfile(path, target)
        run([strip, "--strip-debug", target])
    shutil.copyfile(os.path.join(native_work, LVP_MANIFEST), os.path.join(system32, "lvp_icd.json"))
    shutil.copyfile(os.path.join(mesa_source, "docs", "license.rst"), os.path.join(licenses, "Mesa-LICENSE.rst"))
    shutil.copytree(os.path.join(mesa_source, "licenses"), os.path.join(licenses, "mesa"))
    shutil.copyfile(args.llvm_license, os.path.join(licenses, "LLVM-LICENSE.TXT"))
    shutil.copyfile(args.directx_headers_license, os.path.join(licenses, "DirectX-Headers-LICENSE.txt"))

    files = {}
    for root, _, names in os.walk(stage):
        for name in sorted(names):
            path = os.path.join(root, name)
            files[os.path.relpath(path, stage).replace(os.sep, "/")] = sha256(path)
    manifest = {
        "release": args.release,
        "arch": args.arch,
        "mesa_version": mesa_version,
        "mesa_commit": mesa_commit,
        "mesa_dirty": mesa_dirty,
        "llvm_version": "22.1.8",
        "files": files,
    }
    with open(os.path.join(stage, "payload.json"), "w", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")

    output = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for root, _, names in os.walk(stage):
            for name in sorted(names):
                path = os.path.join(root, name)
                archive.write(path, os.path.relpath(path, stage).replace(os.sep, "/"))
    print("%s  %s" % (sha256(output), output))


if __name__ == "__main__":
    main()
