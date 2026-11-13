# LoongArch64 boot loader sources. Everything this architecture needs stays in
# this directory; the shared build file only includes this list.

list(APPEND WINLOAD_ASM_SOURCE
    arch/loongarch64/uefiasm.S)

list(APPEND WINLOAD_SOURCE
    arch/loongarch64/archosl.c
    arch/loongarch64/runtime.c
    arch/loongarch64/winldr.c)
