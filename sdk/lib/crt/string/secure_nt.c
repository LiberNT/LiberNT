/*
 * PROJECT:     LiberNT Kernel CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The secure routines the kernel exports: _wcsnset_s, _wcslwr_s, _ultow_s and bsearch_s
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>

errno_t
__cdecl
_wcsnset_s(
    wchar_t *String,
    size_t NumberOfElements,
    wchar_t Character,
    size_t Count)
{
    wchar_t *Cursor;
    size_t Available;

    if (String == NULL && NumberOfElements == 0 && Count == 0)
        return 0;
    if (String == NULL || NumberOfElements == 0)
        return EINVAL;

    Cursor = String;
    Available = NumberOfElements;
    for (; Count != 0 && Available != 0 && *Cursor != L'\0'; Count--, Available--)
        *Cursor++ = Character;

    while (Available != 0 && *Cursor != L'\0')
    {
        Cursor++;
        Available--;
    }

    if (Available == 0)
    {
        String[0] = L'\0';
        return EINVAL;
    }
    return 0;
}

errno_t
__cdecl
_wcslwr_s(
    wchar_t *String,
    size_t NumberOfElements)
{
    size_t Length;

    if (String == NULL)
        return EINVAL;

    for (Length = 0; Length < NumberOfElements && String[Length] != L'\0'; Length++);
    if (Length == NumberOfElements)
    {
        String[0] = L'\0';
        return EINVAL;
    }

    for (; Length != 0; Length--, String++)
    {
        if (*String >= L'A' && *String <= L'Z')
            *String += L'a' - L'A';
    }
    return 0;
}

errno_t
__cdecl
_ultow_s(
    unsigned long Value,
    wchar_t *Buffer,
    size_t Size,
    int Radix)
{
    size_t Length = 0, Index;
    unsigned long Digit;
    wchar_t Swap;

    if (Buffer == NULL || Size == 0)
        return EINVAL;

    Buffer[0] = L'\0';
    if (Radix < 2 || Radix > 36)
        return EINVAL;

    do
    {
        if (Length >= Size)
        {
            Buffer[0] = L'\0';
            return ERANGE;
        }
        Digit = Value % (unsigned long)Radix;
        Value /= (unsigned long)Radix;
        Buffer[Length++] = (wchar_t)(Digit < 10 ? L'0' + Digit : L'a' + Digit - 10);
    } while (Value != 0);

    if (Length >= Size)
    {
        Buffer[0] = L'\0';
        return ERANGE;
    }

    Buffer[Length] = L'\0';
    for (Index = 0; Index < Length / 2; Index++)
    {
        Swap = Buffer[Index];
        Buffer[Index] = Buffer[Length - 1 - Index];
        Buffer[Length - 1 - Index] = Swap;
    }
    return 0;
}

void *
__cdecl
bsearch_s(
    const void *Key,
    const void *Base,
    size_t Count,
    size_t Width,
    int (__cdecl *Compare)(void *, const void *, const void *),
    void *Context)
{
    const unsigned char *Low = Base;
    size_t Remaining = Count, Half;
    int Result;

    if (Count == 0)
        return NULL;
    /* FIXME: on Windows 11 a call with an invalid parameter never returns; LiberNT returns NULL */
    if (Key == NULL || Base == NULL || Width == 0 || Compare == NULL)
        return NULL;

    while (Remaining != 0)
    {
        Half = Remaining / 2;
        Result = Compare(Context, Key, Low + Half * Width);
        if (Result == 0)
            return (void *)(Low + Half * Width);
        if (Result > 0)
        {
            Low += (Half + 1) * Width;
            Remaining -= Half + 1;
        }
        else
        {
            Remaining = Half;
        }
    }
    return NULL;
}
