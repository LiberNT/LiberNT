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
CheckImageEx(
    _In_ PVOID Address,
    _In_ PVOID Base,
    _In_ ULONG NotExecutable,
    _In_ PCSTR What)
{
    MEMORY_IMAGE_INFORMATION Image;
    SIZE_T Returned;
    NTSTATUS Status;

    Status = QueryImage(Address, &Image, sizeof(Image), &Returned);
    ok(Status == STATUS_SUCCESS, "%s: status 0x%lx\n", What, Status);
    ok(Returned == sizeof(Image), "%s: returned %Iu\n", What, (size_t)Returned);
    ok(Image.ImageBase == Base, "%s: base %p, expected %p\n", What, Image.ImageBase, Base);
    ok(Image.SizeOfImage == ImageSize(Base), "%s: size 0x%Ix, expected 0x%Ix\n", What, (size_t)Image.SizeOfImage, (size_t)ImageSize(Base));
    ok(Image.ImagePartialMap == 0, "%s: partial map\n", What);
    ok(Image.ImageNotExecutable == NotExecutable, "%s: not executable %lu, expected %lu\n", What,
       (ULONG)Image.ImageNotExecutable, NotExecutable);
}

static
void
CheckImage(
    _In_ PVOID Address,
    _In_ PVOID Base,
    _In_ PCSTR What)
{
    CheckImageEx(Address, Base, 0, What);
}

static
void
CheckSectionProtections(
    _In_ PVOID Base,
    _In_ BOOLEAN NoExecute,
    _In_ PCSTR What)
{
    PIMAGE_NT_HEADERS Nt = (PIMAGE_NT_HEADERS)((PUCHAR)Base + ((PIMAGE_DOS_HEADER)Base)->e_lfanew);
    PIMAGE_SECTION_HEADER Section = IMAGE_FIRST_SECTION(Nt);
    MEMORY_BASIC_INFORMATION Basic;
    WORD Index;

    ok(VirtualQuery(Base, &Basic, sizeof(Basic)) == sizeof(Basic), "%s: VirtualQuery failed\n", What);
    ok(Basic.Type == MEM_IMAGE, "%s: type 0x%lx\n", What, Basic.Type);
    ok(Basic.Protect == PAGE_READONLY, "%s: header protect 0x%lx\n", What, Basic.Protect);
    ok(Basic.AllocationProtect == (NoExecute ? PAGE_READONLY : PAGE_EXECUTE_WRITECOPY), "%s: allocation protect 0x%lx\n",
       What, Basic.AllocationProtect);
    for (Index = 0; Index < Nt->FileHeader.NumberOfSections; Index++)
    {
        ULONG Expected = PAGE_READONLY;

        if (Section[Index].Misc.VirtualSize == 0)
            continue;
        if (!NoExecute && (Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            Expected = PAGE_EXECUTE_READ;
        else if (!NoExecute && (Section[Index].Characteristics & IMAGE_SCN_MEM_WRITE))
            Expected = PAGE_WRITECOPY;
        if (VirtualQuery((PUCHAR)Base + Section[Index].VirtualAddress, &Basic, sizeof(Basic)) != sizeof(Basic))
            continue;
        ok(Basic.Protect == Expected, "%s: section %.8s protect 0x%lx, expected 0x%lx\n", What, Section[Index].Name,
           Basic.Protect, Expected);
    }
}

static
void
TestImageView(
    _In_ ULONG Attributes,
    _In_ PCSTR What)
{
    WCHAR Path[MAX_PATH];
    HANDLE File, Mapping;
    PVOID View;

    GetSystemDirectoryW(Path, RTL_NUMBER_OF(Path));
    wcscat(Path, L"\\shell32.dll");
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "%s: CreateFileW failed with %lu\n", What, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    Mapping = CreateFileMappingW(File, NULL, PAGE_READONLY | Attributes, 0, 0, NULL);
    ok(Mapping != NULL, "%s: CreateFileMappingW failed with %lu\n", What, GetLastError());
    if (Mapping != NULL)
    {
        View = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        ok(View != NULL, "%s: MapViewOfFile failed with %lu\n", What, GetLastError());
        if (View != NULL)
        {
            CheckImageEx(View, View, (Attributes == SEC_IMAGE_NO_EXECUTE) ? 1 : 0, What);
            CheckSectionProtections(View, (BOOLEAN)(Attributes == SEC_IMAGE_NO_EXECUTE), What);
            UnmapViewOfFile(View);
        }
        CloseHandle(Mapping);
    }
    CloseHandle(File);
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
    ok(Returned == sizeof(Image), "%s: returned %Iu\n", What, (size_t)Returned);
    ok(Image.ImageBase == NULL && Image.SizeOfImage == 0 && Image.ImageFlags == 0,
       "%s: base %p size 0x%Ix flags 0x%lx\n", What, Image.ImageBase, (size_t)Image.SizeOfImage, Image.ImageFlags);
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
    WCHAR SystemPath[MAX_PATH], Directory[MAX_PATH], ImagePath[MAX_PATH];
    UINT Length;
    NTSTATUS Status;

    CheckImage(Ntdll, Ntdll, "ntdll");
    CheckImage((PUCHAR)Ntdll + 0x1234, Ntdll, "inside ntdll");
    CheckImage(Exe, Exe, "exe");

    Status = QueryImage(Ntdll, &Image, 0, &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "length 0: status 0x%lx returned %Iu\n", Status, (size_t)Returned);
    Status = QueryImage(Ntdll, &Image, 2 * sizeof(PVOID), &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "short length: status 0x%lx returned %Iu\n", Status, (size_t)Returned);
    Status = QueryImage(Ntdll, &Image, sizeof(Image) - 1, &Returned);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH && Returned == 0xdead, "length - 1: status 0x%lx returned %Iu\n", Status, (size_t)Returned);
    Status = QueryImage(Ntdll, &Image, sizeof(Image) + 8, &Returned);
    ok(Status == STATUS_SUCCESS && Returned == sizeof(Image), "longer length: status 0x%lx returned %Iu\n", Status, (size_t)Returned);

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
    ok(Status == STATUS_INVALID_ADDRESS && Returned == 0xdead, "free: status 0x%lx returned %Iu\n", Status, (size_t)Returned);

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

    TestImageView(SEC_IMAGE, "image view");
    TestImageView(SEC_IMAGE_NO_EXECUTE, "no-execute image view");

    Length = GetSystemDirectoryW(SystemPath, ARRAYSIZE(SystemPath));
    if (!Length || Length + ARRAYSIZE(L"\\shell32.dll") > ARRAYSIZE(SystemPath))
    {
        ok(0, "Cannot get the system directory\n");
        return;
    }
    wcscat(SystemPath, L"\\shell32.dll");
    Length = GetTempPathW(ARRAYSIZE(Directory), Directory);
    if (!Length || Length >= ARRAYSIZE(Directory) || !GetTempFileNameW(Directory, L"img", 0, ImagePath))
    {
        ok(0, "Cannot create a temporary path, error %lu\n", GetLastError());
        return;
    }
    if (!CopyFileW(SystemPath, ImagePath, FALSE))
    {
        ok(0, "Cannot copy the test image, error %lu\n", GetLastError());
        DeleteFileW(ImagePath);
        return;
    }
    Data = LoadLibraryExW(ImagePath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ok(Data != NULL, "LoadLibraryExW failed with %lu\n", GetLastError());
    if (Data != NULL)
    {
        CheckNotImage(Data, "data file");
        FreeLibrary(Data);
    }

    Data = LoadLibraryExW(ImagePath, NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    ok(Data != NULL, "LoadLibraryExW image resource failed with %lu\n", GetLastError());
    if (Data != NULL)
    {
        PVOID Base = (PVOID)((ULONG_PTR)Data & ~(ULONG_PTR)3);

        CheckImageEx(Base, Base, 1, "image resource");
        CheckSectionProtections(Base, TRUE, "image resource");
        FreeLibrary(Data);
    }

    ok(DeleteFileW(ImagePath), "Cannot delete the test image, error %lu\n", GetLastError());
}
