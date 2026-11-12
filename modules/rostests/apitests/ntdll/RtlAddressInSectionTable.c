/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlAddressInSectionTable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef PVOID (NTAPI *FN_RtlAddressInSectionTable)(PIMAGE_NT_HEADERS, PVOID, ULONG);

typedef struct _TEST_IMAGE
{
    IMAGE_DOS_HEADER Dos;
    IMAGE_NT_HEADERS Nt;
    IMAGE_SECTION_HEADER Sections[2];
} TEST_IMAGE, *PTEST_IMAGE;

static
VOID
CheckRva(
    _In_ FN_RtlAddressInSectionTable pRtlAddressInSectionTable,
    _In_ PTEST_IMAGE Image,
    _In_ ULONG Rva,
    _In_opt_ PVOID Expected)
{
    PVOID Address, ViaRvaToVa;

    Address = pRtlAddressInSectionTable(&Image->Nt, Image, Rva);
    ok(Address == Expected, "Rva 0x%lx: got %p, expected %p\n", Rva, Address, Expected);
    ViaRvaToVa = RtlImageRvaToVa(&Image->Nt, Image, Rva, NULL);
    ok(Address == ViaRvaToVa, "Rva 0x%lx: got %p, RtlImageRvaToVa %p\n", Rva, Address, ViaRvaToVa);
}

START_TEST(RtlAddressInSectionTable)
{
    FN_RtlAddressInSectionTable pRtlAddressInSectionTable;
    static TEST_IMAGE Image;
    PUCHAR Base = (PUCHAR)&Image;

    pRtlAddressInSectionTable = (FN_RtlAddressInSectionTable)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                            "RtlAddressInSectionTable");
    if (!pRtlAddressInSectionTable)
    {
        skip("RtlAddressInSectionTable is not exported\n");
        return;
    }

    Image.Dos.e_magic = IMAGE_DOS_SIGNATURE;
    Image.Dos.e_lfanew = FIELD_OFFSET(TEST_IMAGE, Nt);
    Image.Nt.Signature = IMAGE_NT_SIGNATURE;
    Image.Nt.FileHeader.NumberOfSections = RTL_NUMBER_OF(Image.Sections);
    Image.Nt.FileHeader.SizeOfOptionalHeader = sizeof(Image.Nt.OptionalHeader);
    Image.Nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
    Image.Sections[0].VirtualAddress = 0x1000;
    Image.Sections[0].Misc.VirtualSize = 0x800;
    Image.Sections[0].SizeOfRawData = 0x200;
    Image.Sections[0].PointerToRawData = 0x400;
    Image.Sections[1].VirtualAddress = 0x2000;
    Image.Sections[1].Misc.VirtualSize = 0x1000;
    Image.Sections[1].SizeOfRawData = 0x400;
    Image.Sections[1].PointerToRawData = 0x600;

    CheckRva(pRtlAddressInSectionTable, &Image, 0x1000, Base + 0x400);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x1010, Base + 0x410);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x11ff, Base + 0x5ff);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x2000, Base + 0x600);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x2123, Base + 0x723);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x10, NULL);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x3000, NULL);
    CheckRva(pRtlAddressInSectionTable, &Image, 0x80000000, NULL);
}
