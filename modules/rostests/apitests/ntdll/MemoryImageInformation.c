/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test NtQueryVirtualMemory MemoryImageInformation on images, other allocations and free memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static
NTSTATUS
QueryImage(
    _In_ PVOID Address,
    _Out_ PMEMORY_IMAGE_INFORMATION Image,
    _In_ SIZE_T Length,
    _Out_ PSIZE_T Returned)
{
    UCHAR Buffer[sizeof(MEMORY_IMAGE_INFORMATION) + 16];
    NTSTATUS Status;

    RtlFillMemory(Buffer, sizeof(Buffer), 0xcc);
    *Returned = 0xdead;
    Status = NtQueryVirtualMemory(NtCurrentProcess(), Address, MemoryImageInformation, Buffer, Length, Returned);
    RtlCopyMemory(Image, Buffer, sizeof(*Image));
    return Status;
}

static
SIZE_T
ImageSize(
    _In_ PVOID Base)
{
    PIMAGE_DOS_HEADER Dos = Base;
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)((PUCHAR)Base + Dos->e_lfanew);

    return Nt->OptionalHeader.SizeOfImage;
}

static
void
CheckImage(
    _In_ PVOID Address,
    _In_ PVOID Base,
    _In_ PCSTR What)
{
    MEMORY_IMAGE_INFORMATION Image;
    SIZE_T Returned;
    NTSTATUS Status;

    Status = QueryImage(Address, &Image, sizeof(Image), &Returned);
    ok(Status == STATUS_SUCCESS, "%s: status 0x%lx\n", What, Status);
    ok(Returned == sizeof(Image), "%s: returned %Iu\n", What, Returned);
    ok(Image.ImageBase == Base, "%s: base %p, expected %p\n", What, Image.ImageBase, Base);
    ok(Image.SizeOfImage == ImageSize(Base), "%s: size 0x%Ix, expected 0x%Ix\n", What, Image.SizeOfImage, ImageSize(Base));
    ok(Image.ImagePartialMap == 0, "%s: partial map\n", What);
    ok(Image.ImageNotExecutable == 0, "%s: not executable\n", What);
}

static
void
CheckNotImage(
    _In_ PVOID Address,
    _In_ PCSTR What)
{
    MEMORY_IMAGE_INFORMATION Image;
    SIZE_T Returned;
    NTSTATUS Status;

    Status = QueryImage(Address, &Image, sizeof(Image), &Returned);
    ok(Status == STATUS_SUCCESS, "%s: status 0x%lx\n", What, Status);
    ok(Returned == sizeof(Image), "%s: returned %Iu\n", What, Returned);
    ok(Image.ImageBase == NULL && Image.SizeOfImage == 0 && Image.ImageFlags == 0,
       "%s: base %p size 0x%Ix flags 0x%lx\n", What, Image.ImageBase, Image.SizeOfImage, Image.ImageFlags);
}

START_TEST(MemoryImageInformation)
{
    PVOID Ntdll = GetModuleHandleW(L"ntdll.dll"), Exe = GetModuleHandleW(NULL);
    MEMORY_IMAGE_INFORMATION Image;
    UCHAR Buffer[sizeof(MEMORY_IMAGE_INFORMATION)];
    SIZE_T Returned, Size;
    HANDLE Section;
    LARGE_INTEGER MaximumSize;
    PVOID Heap, Reserve = NULL, View = NULL;
    HMODULE Data;
    NTSTATUS Status;

    CheckImage(Ntdll, Ntdll, "ntdll");
    CheckImage((PUCHAR)Ntdll + 0x1234, Ntdll, "inside ntdll");
    CheckImage(Exe, Exe, "exe");

    Status = QueryImage(Ntdll, &Image, 0, &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "length 0: status 0x%lx returned %Iu\n", Status, Returned);
    Status = QueryImage(Ntdll, &Image, 2 * sizeof(PVOID), &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "short length: status 0x%lx returned %Iu\n", Status, Returned);
    Status = QueryImage(Ntdll, &Image, sizeof(Image) - 1, &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "length - 1: status 0x%lx returned %Iu\n", Status, Returned);
    Status = QueryImage(Ntdll, &Image, sizeof(Image) + 8, &Returned);
    ok(Status == STATUS_SUCCESS && Returned == sizeof(Image), "longer length: status 0x%lx returned %Iu\n", Status, Returned);

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Ntdll, MemoryImageInformation, Buffer, sizeof(Buffer), NULL);
    ok(Status == STATUS_SUCCESS, "NULL return length: status 0x%lx\n", Status);
    Returned = 0;
    Status = NtQueryVirtualMemory(NtCurrentProcess(), Ntdll, MemoryImageInformation, NULL, sizeof(Buffer), &Returned);
    ok(Status == STATUS_ACCESS_VIOLATION, "NULL buffer: status 0x%lx\n", Status);

    Heap = RtlAllocateHeap(RtlGetProcessHeap(), 0, 64);
    CheckNotImage(Heap, "heap");
    RtlFreeHeap(RtlGetProcessHeap(), 0, Heap);

    Size = 0x10000;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Reserve, 0, &Size, MEM_RESERVE, PAGE_NOACCESS);
    ok(Status == STATUS_SUCCESS, "NtAllocateVirtualMemory returned 0x%lx\n", Status);
    CheckNotImage(Reserve, "reserved");
    Size = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &Reserve, &Size, MEM_RELEASE);
    Status = QueryImage(Reserve, &Image, sizeof(Image), &Returned);
    ok(Status == STATUS_INVALID_ADDRESS && Returned == 0xdead, "free: status 0x%lx returned %Iu\n", Status, Returned);

    Status = QueryImage((PVOID)~(ULONG_PTR)0xfff, &Image, sizeof(Image), &Returned);
    ok(Status == STATUS_INVALID_PARAMETER, "kernel address: status 0x%lx\n", Status);

    MaximumSize.QuadPart = 0x10000;
    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &MaximumSize, PAGE_READWRITE, SEC_COMMIT, NULL);
    ok(Status == STATUS_SUCCESS, "NtCreateSection returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Size = 0;
        Status = NtMapViewOfSection(Section, NtCurrentProcess(), &View, 0, 0, NULL, &Size, ViewUnmap, 0, PAGE_READWRITE);
        ok(Status == STATUS_SUCCESS, "NtMapViewOfSection returned 0x%lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            CheckNotImage(View, "data view");
            NtUnmapViewOfSection(NtCurrentProcess(), View);
        }
        NtClose(Section);
    }

    Data = LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    ok(Data != NULL, "LoadLibraryExW failed with %lu\n", GetLastError());
    if (Data != NULL)
    {
        CheckNotImage(Data, "data file");
        FreeLibrary(Data);
    }
}
