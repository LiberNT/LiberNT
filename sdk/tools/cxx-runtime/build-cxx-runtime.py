#!/usr/bin/env python3
"""
PROJECT:     LiberNT Build
LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
PURPOSE:     Build the LLVM runtimes of a configured Clang build tree against the SDK headers
COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

TARGETS = {
    ('amd64', False): ('x86_64-w64-mingw32', 'x86_64', 'x86_64', '-mlong-double-64', []),
    ('i386', False): ('i686-w64-mingw32', 'i686', 'i386', '-mlong-double-64', []),
    ('arm64', False): ('aarch64-w64-mingw32', 'aarch64', 'aarch64', '-marm64x', []),
    ('arm', False): ('arm-w64-mingw32', 'arm', 'arm', '', []),
    ('riscv64', False): ('riscv64-w64-mingw32', 'riscv64', 'riscv64',
                         '-march=rv64gc -mabi=lp64 -mcmodel=medany -mno-relax', []),
    ('ppc', False): ('powerpcle-w64-mingw32', 'ppc', 'powerpcle', '-mcpu=604 -D_PPC_',
                     ['-DCOMPILER_RT_EXCLUDE_ATOMIC_BUILTIN=OFF']),
}


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def cmake_string(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reactos-build', required=True, type=Path)
    parser.add_argument('--llvm-source', type=Path)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--jobs', default=os.cpu_count() or 8, type=int)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[3]
    build = args.reactos_build.resolve()
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    arch = cache.get('ARCH')
    selected = TARGETS.get((arch, cache.get('ARM64EC_RUNTIME', '').upper() in ('1', 'ON', 'TRUE', 'YES')))
    if selected is None:
        parser.error(f'--reactos-build is configured for {arch}, which has no runtime recipe here')
    triple, processor, builtins_name, machine_flags, builtins_options = selected
    toolchain = (args.toolchain or Path(cache['REACTOS_CLANG_LLVM_MINGW_ROOT'])).resolve()
    llvm = (args.llvm_source or Path(os.environ.get('REACTOS_LLVM_SOURCE') or toolchain / 'src/llvm-project')).resolve()
    prefix = (args.prefix or build / f'{arch}-cxx-runtime').resolve()
    runtime = build / f'{arch}-cxx-build'
    runtime.mkdir(parents=True, exist_ok=True)
    exe = '.exe' if os.name == 'nt' else ''
    clang = toolchain / 'bin' / f'clang{exe}'
    resource = Path(subprocess.check_output([str(clang), '-print-resource-dir'], text=True).strip())
    for path in [llvm / 'runtimes/CMakeLists.txt', llvm / 'libcxxabi/include/cxxabi.h',
                 llvm / 'libunwind/src/Unwind-seh.cpp', llvm / 'compiler-rt/lib/builtins/CMakeLists.txt']:
        if not path.is_file():
            parser.error(f'missing matching runtime input: {path}')
    run('cmake', '--build', build, '--target', 'xdk', 'psdk')
    includes = [resource / 'include'] + [source / 'sdk/include' / path for path in
                ['ucrt', 'vcruntime', 'crt', 'psdk', '', 'ddk', 'reactos']] + [
                build / 'sdk/include', build / 'sdk/include/psdk', build / 'sdk/include/ddk']
    flags = (f'{machine_flags} -fms-extensions -funwind-tables '
             '-D_DLL -D__USE_CRTIMP -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 '
             '-D__REACTOS__ -D_CRT_DECLARE_NONSTDC_NAMES=1 -D__LARGE_MBSTATE_T '
             '-nostdlibinc ').lstrip()
    flags += ' '.join('-isystem ' + cmake_string(path) for path in includes)
    config = ['set(CMAKE_SYSTEM_NAME Windows)', f'set(CMAKE_SYSTEM_PROCESSOR {processor})',
              'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)']
    for language, compiler in [('C', 'clang'), ('CXX', 'clang++'), ('ASM', 'clang')]:
        config += [f'set(CMAKE_{language}_COMPILER {cmake_string(toolchain / "bin" / (compiler + exe))})',
                   f'set(CMAKE_{language}_COMPILER_TARGET {triple})']
    for name, executable in [('AR', 'llvm-ar'), ('RANLIB', 'llvm-ranlib')]:
        config += [f'set(CMAKE_{name} {cmake_string(toolchain / "bin" / (executable + exe))})']
    for language in ['C', 'CXX']:
        config += [f'set(CMAKE_{language}_FLAGS_INIT {cmake_string(flags)})',
                   f'set(CMAKE_{language}_STANDARD_LIBRARIES "" CACHE STRING "" FORCE)']
    config += [f'set(CMAKE_ASM_FLAGS_INIT {cmake_string(machine_flags)})']
    toolchain_file = runtime / 'reactos-toolchain.cmake'
    toolchain_file.write_text('\n'.join(config) + '\n')
    options = {
        'CMAKE_TOOLCHAIN_FILE': toolchain_file,
        'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_INSTALL_PREFIX': prefix,
        'LLVM_ENABLE_RUNTIMES': 'libunwind;libcxxabi;libcxx', 'LIBCXX_CXX_ABI': 'libcxxabi',
        'LIBUNWIND_ENABLE_SHARED': 'OFF', 'LIBUNWIND_ENABLE_STATIC': 'ON',
        'LIBUNWIND_ENABLE_THREADS': 'ON', 'LIBUNWIND_INCLUDE_TESTS': 'OFF',
        'LIBUNWIND_USE_COMPILER_RT': 'ON',
        'LIBCXXABI_ENABLE_SHARED': 'OFF', 'LIBCXXABI_ENABLE_STATIC': 'ON',
        'LIBCXXABI_USE_LLVM_UNWINDER': 'ON', 'LIBCXXABI_INCLUDE_TESTS': 'OFF',
        'LIBCXXABI_USE_COMPILER_RT': 'ON',
        'LIBCXX_ENABLE_SHARED': 'OFF', 'LIBCXX_ENABLE_STATIC': 'ON',
        'LIBCXX_ENABLE_STATIC_ABI_LIBRARY': 'OFF', 'LIBCXX_ENABLE_ABI_LINKER_SCRIPT': 'OFF',
        'LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS': 'ON',
        'LIBCXX_ENABLE_TESTS': 'OFF', 'LIBCXX_INCLUDE_TESTS': 'OFF',
        'LIBCXX_INCLUDE_BENCHMARKS': 'OFF', 'LIBCXX_HAS_WIN32_THREAD_API': 'ON',
        'LIBCXX_HAS_PTHREAD_API': 'OFF', 'LIBCXX_HAS_ATOMIC_LIB': 'OFF',
        'LIBCXX_ENABLE_FILESYSTEM': 'ON', 'LIBCXX_ENABLE_EXPERIMENTAL_LIBRARY': 'OFF',
    }
    run('cmake', '--fresh', '-S', llvm / 'runtimes', '-B', runtime, '-G', 'Ninja',
        *(f'-D{key}={value}' for key, value in options.items()))
    run('cmake', '--build', runtime, '--target', 'cxx', 'cxxabi', 'unwind', '--parallel', args.jobs)
    run('cmake', '--install', runtime)
    builtins = build / f'{arch}-builtins-build'
    run('cmake', '--fresh', '-S', llvm / 'compiler-rt/lib/builtins', '-B', builtins,
        '-G', 'Ninja', f'-DCMAKE_TOOLCHAIN_FILE={toolchain_file}',
        '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}',
        '-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON', '-DCOMPILER_RT_INCLUDE_TESTS=OFF',
        '-DCOMPILER_RT_BUILD_CRT=OFF', '-DCOMPILER_RT_BUILTINS_ENABLE_PIC=OFF',
        '-DCOMPILER_RT_EXCLUDE_EMUTLS=ON', *builtins_options)
    run('cmake', '--build', builtins, '--parallel', args.jobs)
    run('cmake', '--install', builtins)
    manifest = {'compiler': subprocess.check_output([str(clang), '--version'], text=True),
                'llvm_source': str(llvm), 'flags': flags, 'inputs': {}}
    for path in [prefix / 'lib/libc++abi.a', prefix / 'lib/libunwind.a', prefix / 'lib/libc++.a',
                 prefix / f'lib/windows/libclang_rt.builtins-{builtins_name}.a']:
        manifest['inputs'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    (prefix / 'reactos-runtime.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
