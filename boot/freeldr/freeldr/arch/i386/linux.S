/*
 *  FreeLoader
 *  Copyright (C) 1998-2002  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <asm.inc>
#include <arch/pc/x86common.h>
#include <arch/pc/pcbios.h>

EXTERN _DiskStopFloppyMotor:PROC
EXTERN _Relocator16Boot:PROC
/* The import table slot, not the thunk: the thunk lives in this image, which the
   relocation may be about to overwrite. */
EXTERN __imp__Relocator16Boot:DWORD

.code32

Regs:
    .space REGS_SIZE

/*
 * Offset, inside the relocation stub's page, of the REGS block the stub hands to
 * Relocator16Boot.  The stub code itself must stay below it (checked below).
 */
#define LINUX_STUB_REGS_OFFSET HEX(100)

/*
 * VOID __cdecl
 * BootLinuxKernel(
 *     _In_ ULONG KernelSize,
 *     _In_ PVOID KernelCurrentLoadAddress,
 *     _In_ PVOID KernelTargetLoadAddress);
 */
PUBLIC _BootLinuxKernel
_BootLinuxKernel:

    /* Stop the floppy drive motor */
    call _DiskStopFloppyMotor

    /* Set all segment registers to 0x9000 */
    mov ax, HEX(9000)
    mov word ptr [Regs + REGS_DS], ax
    mov word ptr [Regs + REGS_ES], ax
    mov word ptr [Regs + REGS_FS], ax
    mov word ptr [Regs + REGS_GS], ax

    /*
     * Relocate the kernel image to its final destination (can be as low as 0x10000).
     *
     * The *downward* move cannot be performed from this function: for a LOAD_HIGH
     * kernel the destination is 0x100000 and this image (the second stage loader) is
     * loaded at 0x200000, so any kernel larger than 1 MiB has this code, its REGS
     * block and its import table *inside* the range it is rewriting.  A copy loop
     * executed from there destroys the instruction it is executing.
     *
     * reloc_down therefore moves the loop, and the real-mode hand-off that follows
     * it, into the spare page that LinuxReadKernel allocates directly above the
     * staging buffer, and jumps there.  Writes never go above
     * max(source, target) + KernelSize, and that page starts at source + KernelSize
     * (which is strictly above the end of the write range because target < source
     * here), so the stub cannot overwrite itself.
     *
     * The upward move (a non-LOAD_HIGH kernel, target 0x10000) still runs in place;
     * it has no such page and is unchanged from before (see the FIXME below).
     */

    /* Get KernelSize in ECX */
    mov ecx, dword ptr [esp + 4]
    test ecx, ecx   // If size is zero, do not perform relocations
    jz after_reloc

    /* Load the source and target addresses */
    mov esi, dword ptr [esp +  8] // HEX(100000) // LINUX_KERNEL_LOAD_ADDRESS
    mov edi, dword ptr [esp + 12] // HEX(10000)

//
// FIXME: Support relocating *upwards*, overlapping regions, aligned addresses,
// etc... !! See memmove code.
//
    /* Check how we should perform relocation */
    cmp edi, esi
    je after_reloc  // target == source: do not perform relocations
    ja reloc_up     // target  > source: relocate up
//  jb reloc_down   // target  < source: relocate down (default)

reloc_down:
reloc_up:
    /*
     * **One path for both directions.**  EDX = a page above the end of *both* ranges, which is the page the
     * model in contrib/freeldr-f2fs/reloc-model proves is never written by the copy: in place, an upward
     * move has its loop between the two ranges (inside the destination) and a downward move has it inside
     * the staging buffer (also inside the destination).  Taking the maximum covers both cases with one page,
     * which is why the two directions no longer need separate tricks.
     */
    mov edx, esi            // Source
    cmp edi, edx
    jbe 1f
    mov edx, edi            // Target, when it is the higher of the two
1:  add edx, ecx            // + KernelSize
    add edx, HEX(0FFF)      // Round up ...
    and edx, HEX(0FFFF000)  // ... to the next page boundary

    cld
    push esi
    push edi

    /* Copy the relocation stub itself out of this image */
    mov edi, edx
    mov esi, offset LinuxRelocationStub
    mov ecx, (LinuxRelocationStubEnd - LinuxRelocationStub)
    rep movsb

    /* Copy the REGS block the stub hands to Relocator16Boot */
    mov edi, edx
    add edi, LINUX_STUB_REGS_OFFSET
    mov esi, offset Regs
    mov ecx, (REGS_SIZE / 4)
    rep movsd

    /* Restore the copy arguments and enter the stub */
    pop edi                 // Target address
    pop esi                 // Source address
    mov ecx, dword ptr [esp + 4]            // KernelSize
    mov ebx, edx
    add ebx, LINUX_STUB_REGS_OFFSET         // REGS* for Relocator16Boot
    mov ebp, dword ptr [__imp__Relocator16Boot] // Must be read before the copy
    jmp edx

/* The in-place upward relocation is gone: both directions run from the shared stub page above, which is
 * the only placement the model in contrib/freeldr-f2fs/reloc-model shows to be safe for either. */

after_reloc:

    push HEX(0000) // CodePointer
    push HEX(9020) // CodeSegment
    push HEX(9000) // StackPointer
    push HEX(9000) // StackSegment
    mov eax, offset Regs
    push eax
    call _Relocator16Boot

    /* We must never get there */
    int 3

/*
 * The downward relocation, executed from the scratch page described above.
 *
 * It must be position independent (it is copied to an arbitrary page) and it must
 * not reference anything in this image: on entry ESI, EDI and ECX hold the source,
 * the target and the size, EBX is the address of the REGS block (inside the same
 * page, at LINUX_STUB_REGS_OFFSET) and EBP is Relocator16Boot, whose own code lives
 * in the first stage loader below 1 MiB and therefore survives the copy.
 */
.balign 16
LinuxRelocationStub:
    /*
     * Both directions, from the same page.  ESI/EDI are source/target and ECX the size, as on entry.
     * Forward (target below source) copies low to high; backward copies high to low.  Getting this wrong is
     * the bug this stub exists to avoid, and the model checks the choice independently of the asm.
     */
    cmp edi, esi
    ja  .backward
    cld
    mov edx, ecx            // Copy the total number of bytes in EDX
    and edx, HEX(0FFFFFFFC) // Number of bytes we copy using DWORDs
    xor edx, ecx            // Number of remaining bytes to copy after the DWORDs
    shr ecx, 2              // Count number of DWORDs
    rep movsd               // Move DWORDs
    mov ecx, edx            // Count number of remaining bytes
    rep movsb               // Move bytes
    jmp  .handoff

.backward:
    /* Target above source: copy from the top down, or the copy overwrites what it has not read yet. */
    std
    add esi, ecx
    add edi, ecx
    dec esi
    dec edi
    rep movsb
    cld                     // the hand-off below expects DF clear

.handoff:
    /* Enter the setup code at 0x9020:0000 exactly as after_reloc would have */
    push HEX(0000) // CodePointer
    push HEX(9020) // CodeSegment
    push HEX(9000) // StackPointer
    push HEX(9000) // StackSegment
    push ebx       // REGS*
    call ebp       // Relocator16Boot

    /* We must never get there */
    int 3
LinuxRelocationStubEnd:

/* NB: ".if" cannot be used here -- asm.inc #defines "if" (the "." and the "if"
   are separate preprocessor tokens), so the MASM-compatible spelling is required. */
/*
 * The stub must fit below LINUX_STUB_REGS_OFFSET (256) inside its page, or it overwrites the REGS block it
 * hands to Relocator16Boot.
 *
 * The assembly-time guard that used to enforce this (`.if (End - Start) > LIMIT` / `.error`) fails with
 * "non-constant expression" once the stub spans the direction branch, and it fails the same way with a plain
 * literal bound -- so the cause is the labels, not the `HEX()` macro.  The labels are exported instead, and
 * `contrib/freeldr-f2fs/loader-fix/check-stub-size.sh` measures them from the built object.  A guard outside
 * the assembler cannot be defeated by its constant folding, which is the point.
 */
.globl LinuxRelocationStub
.globl LinuxRelocationStubEnd

END
