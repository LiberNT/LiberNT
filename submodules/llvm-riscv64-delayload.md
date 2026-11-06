# RISC-V64 delay-import unwind information

The RISC-V64 COFF support in the LiberNT LLVM fork
(`https://github.com/sjljeh/llvm-project`, commit
`720de96891932b393be49926a56b1e532772733b`) emits the delay-import tail
thunk `__tailMerge_<dll>` without a `.pdata` entry. The thunk allocates 144
bytes, saves `ra` and calls `__delayLoadHelper2`, so an exception raised by
the helper cannot unwind back to the thunk's caller. The delayimp hook tests
terminate with `0xC06D007F` on LiberNT RISC-V64.

[`llvm-riscv64-delayload.patch`](llvm-riscv64-delayload.patch) adds a
`RUNTIME_FUNCTION` (begin, end, unwind data) and an RVUW version 1 record for
the existing 172-byte thunk. The record is the one the fork's RISC-V
assembler emits for the same instructions with `.seh_set_cfa x2, 144` after
the stack allocation, `.seh_save_gpr x1, -80` after the `ra` store, and an
epilogue scope over the `ra` reload, the stack release and the final jump. The
record holds no relocations, so the linker writes it as a constant.

The patched linker gives each tail thunk its record: five in
`delayimp_nohook_apitest.exe` and six in each hook test, with `.pdata` still
sorted. Linked with it, the delayimp tests pass on LiberNT RISC-V64:
nohook 18/0, globalhook 396/0 and runtimehook 396/0. Build and select the
linker as described in [`llvm-arm64-delayload.md`](llvm-arm64-delayload.md),
applying this patch to the fork at the commit above.
