# RV64X

RV64X lets x64 code emulated by felix86 and native riscv64 code call each other
directly, the way ARM64EC does on ARM64. Unlike ARM64EC it adds no second ABI:
LiberNT riscv64 user mode uses LP64 (soft-float), where every scalar argument
travels in `a0`-`a7` and then on the stack and every result in `a0`. Moving
between that and the x64 calling convention only needs to know, per call, which
values are floating point and how narrow integers are extended. An RV64X binary
is therefore an ordinary riscv64 binary that native processes load unchanged.

## Compiler contract

The pass lives in the toolchain fork (`submodules/llvm-rv64x.patch`, against the
revision of the RosBE LLVM build) and runs when `-mllvm -riscv-rv64x` is given.

**Entry descriptor.** Every function that is externally visible or whose address
is taken gets 16 bytes of prefix data, ending right before its entry point:

| Bytes | Content |
|---|---|
| 0-1 | `0x5852` |
| 2 | argument count |
| 3 | low nibble: result kind; `0x10` variadic, `0x20` hidden struct result, `0x40` unsupported shape |
| 4-15 | 24 four-bit argument kinds, argument 0 in the low nibble of byte 4 |

Kinds: 0 void, 1 64-bit integer or pointer, 2 32-bit integer, 3 signed 16-bit,
4 unsigned 16-bit, 5 signed 8-bit, 6 unsigned 8-bit or `bool`, 7 `float`,
8 `double`, 9 16-byte aggregate in two registers, 15 unsupported.

**Checked indirect call.** Before an indirect call the pass loads
`TEB->ProcessEnvironmentBlock->EcCodeBitMap` (TEB `+0x60`, PEB `+0x368`). A NULL
bitmap, the native-process case, calls the target directly. Otherwise a target
whose page bit is set (`bitmap[address >> 15] >> ((address >> 12) & 7)`) is native
code and is called directly; any other target goes through the exit thunk.

**Exit thunk.** One `linkonce_odr` thunk per call shape stores the arguments as
64-bit slots (floats as their bit pattern) and calls

```c
ULONG64 __rv64x_call_x64(PVOID Target, ULONG64 DescriptorLow, ULONG64 DescriptorHigh,
                         const ULONG64 *Arguments);
```

with the call site's descriptor, then narrows the returned bits to the call's
result type (`int` results are sign-extended as the LP64 ABI requires).

## Runtime

- The riscv64 `ntdll` exports `__rv64x_call_x64`, so every RV64X module links
  it through its `ntdll` import. It forwards to the emulator's
  `EMU_HOST_INTERFACE::CallDescribed`; native processes never reach it because
  their bitmap is NULL.
- `amd64emu` points the host PEB's `EcCodeBitMap` at a bitmap sized from
  `SystemBasicInformation.MaximumUserModeAddress`, marks the executable
  sections of every host image and keeps the bitmap current with a loader DLL
  notification, which marks images as they load and clears them as they
  unload. `CallDescribed` calls native targets that are not marked yet (a DLL's
  own `DllMain`, for example) natively and marks their image; everything else
  runs as x64 through `CallGuest`.
- `EMU_HOST_INTERFACE::GuestThunk` with a NULL signature reads the target's entry
  descriptor, so x64 code can call any RV64X function given its address.
- When x64 code branches to an address the bitmap marks native, felix86 does not
  translate it: it continues at the guest thunk built from the target's entry
  descriptor, so a raw native function pointer works as a call target. A
  marked target without a descriptor ends the process with a message naming it.

## Building and testing

Configure the riscv64 tree with `-DRV64X_CLANG=<clang built with the patch>`
(`sdk/cmake/rv64x.cmake`). That compiles Mesa, the OpenGL host
(`glthunk_host`), `rv64xhost.dll` (a host library made for the tests) and
`emulation_test_rv64x` (the native test) as RV64X. `emulation_test` run
as x64 loads `rv64xhost.dll` and checks both directions: narrow integers, mixed
float, double and stack arguments into native code; native code calling x64
callbacks with ten arguments, a negative `int` result, a `float` result and a
variadic `_snprintf`. `emulation_test_rv64x` checks that RV64X code runs
unchanged in a native process.

## Stages

1. Compiler pass, runtime exit path and entry descriptors in the current
   emulation host (done; the tests above pass).
2. The native OpenGL host as RV64X: when Mesa carries entry descriptors, the
   host thunks GL entry points from them, hands the x64 callbacks of
   `opengl32` and `glDebugMessageCallback*` to Mesa unchanged and Mesa reaches
   them through exit thunks; the generated signature table only covers code
   without descriptors.
3. One loader for x64 processes: native `ntdll` loads x64 images through the
   CHPE runtime behind an EC-host switch instead of `_M_ARM64` and x64 imports
   bind to RV64X system DLLs. Done so far: felix86 leaves translated code when a
   target is native.
4. Build the system DLLs as RV64X and retire the x64 userland in `SystemAMD64`.

Open: variadic RV64X functions called from x64 (the stack window is unknown),
16-byte aggregates by value and struct results, C++ `invoke` sites (left
unchecked), and native code generated at run time (treated as x64).
