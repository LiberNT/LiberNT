/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 port and register-mapped I/O routines
 * COPYRIGHT:   Copyright Alex Ionescu <alex@relsoft.net>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ks386.inc>

PUBLIC _READ_REGISTER_UCHAR@4
PUBLIC _READ_REGISTER_USHORT@4
PUBLIC _READ_REGISTER_ULONG@4
PUBLIC _READ_REGISTER_BUFFER_UCHAR@12
PUBLIC _READ_REGISTER_BUFFER_USHORT@12
PUBLIC _READ_REGISTER_BUFFER_ULONG@12
PUBLIC _WRITE_REGISTER_UCHAR@8
PUBLIC _WRITE_REGISTER_USHORT@8
PUBLIC _WRITE_REGISTER_ULONG@8
PUBLIC _WRITE_REGISTER_BUFFER_UCHAR@12
PUBLIC _WRITE_REGISTER_BUFFER_USHORT@12
PUBLIC _WRITE_REGISTER_BUFFER_ULONG@12

.code

_READ_REGISTER_UCHAR@4:

    mov edx, [esp+4]
    mov al, [edx]
    ret 4

_READ_REGISTER_USHORT@4:

    mov edx, [esp+4]
    mov ax, [edx]
    ret 4

_READ_REGISTER_ULONG@4:

    mov edx, [esp+4]
    mov eax, [edx]
    ret 4

_READ_REGISTER_BUFFER_UCHAR@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+4]
    mov edi, [esp+8]
    rep movsb

    mov edi, edx
    mov esi, eax
    ret 12

_READ_REGISTER_BUFFER_USHORT@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+4]
    mov edi, [esp+8]
    rep movsw

    mov edi, edx
    mov esi, eax
    ret 12

_READ_REGISTER_BUFFER_ULONG@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+4]
    mov edi, [esp+8]
    rep movsd

    mov edi, edx
    mov esi, eax
    ret 12

_WRITE_REGISTER_UCHAR@8:

    mov edx, [esp+4]
    mov al, [esp+8]
    mov [edx], al

    lock or [esp+4], edx
    ret 8

_WRITE_REGISTER_USHORT@8:

    mov edx, [esp+4]
    mov eax, [esp+8]
    mov [edx], ax

    lock or [esp+4], edx
    ret 8

_WRITE_REGISTER_ULONG@8:

    mov edx, [esp+4]
    mov eax, [esp+8]
    mov [edx], eax

    lock or [esp+4], edx
    ret 8

_WRITE_REGISTER_BUFFER_UCHAR@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+8]
    mov edi, [esp+4]
    rep movsb

    lock or [esp+4], ecx

    mov edi, edx
    mov esi, eax
    ret 12

_WRITE_REGISTER_BUFFER_USHORT@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+8]
    mov edi, [esp+4]
    rep movsw

    lock or [esp+4], ecx

    mov edi, edx
    mov esi, eax
    ret 12

_WRITE_REGISTER_BUFFER_ULONG@12:

    mov eax, esi
    mov edx, edi

    mov ecx, [esp+12]
    mov esi, [esp+8]
    mov edi, [esp+4]
    rep movsd

    lock or [esp+4], ecx

    mov edi, edx
    mov esi, eax
    ret 12

END
