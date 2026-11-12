# LoongArch64 boot loader sources. Everything this architecture needs stays in
# this directory; the shared build file only includes this list.

list(APPEND WINLOAD_ASM_SOURCE
    arch/loongarch64/uefiasm.S)

# TODO: winldr.c and archosl.c once the kernel handoff ABI is defined.
