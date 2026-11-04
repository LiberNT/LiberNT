/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the NUMA node query and NUMA-preferred allocation APIs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GET_NUMA_HIGHEST_NODE_NUMBER)(PULONG);
typedef BOOL (WINAPI *PFN_GET_NUMA_AVAILABLE_MEMORY_NODE)(UCHAR, PULONGLONG);
typedef BOOL (WINAPI *PFN_GET_NUMA_AVAILABLE_MEMORY_NODE_EX)(USHORT, PULONGLONG);
typedef BOOL (WINAPI *PFN_GET_NUMA_PROXIMITY_NODE)(ULONG, PUCHAR);
typedef BOOL (WINAPI *PFN_GET_NUMA_PROXIMITY_NODE_EX)(ULONG, PUSHORT);
typedef BOOL (WINAPI *PFN_GET_NUMA_NODE_NUMBER_FROM_HANDLE)(HANDLE, PUSHORT);
typedef BOOL (WINAPI *PFN_GET_NUMA_NODE_PROCESSOR_MASK_EX)(USHORT, PGROUP_AFFINITY);
typedef BOOL (WINAPI *PFN_GET_NUMA_NODE_PROCESSOR_MASK2)(USHORT, PGROUP_AFFINITY, USHORT, PUSHORT);
typedef LPVOID (WINAPI *PFN_VIRTUAL_ALLOC_EX_NUMA)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD, DWORD);
typedef HANDLE (WINAPI *PFN_CREATE_FILE_MAPPING_NUMA_W)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR, DWORD);
typedef HANDLE (WINAPI *PFN_CREATE_FILE_MAPPING_NUMA_A)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR, DWORD);
typedef LPVOID (WINAPI *PFN_MAP_VIEW_OF_FILE_EX_NUMA)(HANDLE, DWORD, DWORD, DWORD, SIZE_T, LPVOID, DWORD);
typedef BOOL (WINAPI *PFN_ALLOCATE_USER_PHYSICAL_PAGES_NUMA)(HANDLE, PULONG_PTR, PULONG_PTR, DWORD);
typedef NTSTATUS (NTAPI *PFN_NT_ALLOCATE_VIRTUAL_MEMORY_EX)(HANDLE, PVOID *, PSIZE_T, ULONG, ULONG, PMEM_EXTENDED_PARAMETER, ULONG);
typedef NTSTATUS (NTAPI *PFN_NT_QUERY_INFORMATION_FILE)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

static PFN_GET_NUMA_HIGHEST_NODE_NUMBER pGetNumaHighestNodeNumber;
static PFN_GET_NUMA_AVAILABLE_MEMORY_NODE pGetNumaAvailableMemoryNode;
static PFN_GET_NUMA_AVAILABLE_MEMORY_NODE_EX pGetNumaAvailableMemoryNodeEx;
static PFN_GET_NUMA_PROXIMITY_NODE pGetNumaProximityNode;
static PFN_GET_NUMA_PROXIMITY_NODE_EX pGetNumaProximityNodeEx;
static PFN_GET_NUMA_NODE_NUMBER_FROM_HANDLE pGetNumaNodeNumberFromHandle;
static PFN_GET_NUMA_NODE_PROCESSOR_MASK_EX pGetNumaNodeProcessorMaskEx;
static PFN_GET_NUMA_NODE_PROCESSOR_MASK2 pGetNumaNodeProcessorMask2;
static PFN_VIRTUAL_ALLOC_EX_NUMA pVirtualAllocExNuma;
static PFN_CREATE_FILE_MAPPING_NUMA_W pCreateFileMappingNumaW;
static PFN_CREATE_FILE_MAPPING_NUMA_A pCreateFileMappingNumaA;
static PFN_MAP_VIEW_OF_FILE_EX_NUMA pMapViewOfFileExNuma;
static PFN_ALLOCATE_USER_PHYSICAL_PAGES_NUMA pAllocateUserPhysicalPagesNuma;
static PFN_NT_ALLOCATE_VIRTUAL_MEMORY_EX pNtAllocateVirtualMemoryEx;
static PFN_NT_QUERY_INFORMATION_FILE pNtQueryInformationFile;

#define GET_PROC(Module, Name) \
    p##Name = (void *)GetProcAddress(Module, #Name); \
    ok(p##Name != NULL, #Name " is not exported\n")

static
void
Test_AvailableMemoryNodeEx(ULONG HighestNode)
{
    MEMORYSTATUSEX MemoryStatus;
    ULONGLONG Bytes, LegacyBytes;
    USHORT BadNodes[3];
    USHORT Node;
    BOOL Result;
    ULONG i;

    if (!pGetNumaAvailableMemoryNodeEx || !pGetNumaAvailableMemoryNode)
        return;

    MemoryStatus.dwLength = sizeof(MemoryStatus);
    ok(GlobalMemoryStatusEx(&MemoryStatus), "GlobalMemoryStatusEx failed: %lu\n", GetLastError());

    for (Node = 0; Node <= HighestNode; Node++)
    {
        Bytes = 0xCCCCCCCCCCCCCCCCULL;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaAvailableMemoryNodeEx(Node, &Bytes);
        ok(Result, "Node %u: GetNumaAvailableMemoryNodeEx failed: %lu\n", Node, GetLastError());
        ok(GetLastError() == 0xDEADBEEF, "Node %u: LastError = %lu\n", Node, GetLastError());
        if (!Result)
            continue;

        ok(Bytes != 0, "Node %u: no available memory\n", Node);
        ok(Bytes <= MemoryStatus.ullTotalPhys,
           "Node %u: %I64u available bytes, %I64u physical bytes\n", Node, Bytes, MemoryStatus.ullTotalPhys);
        ok((Bytes & 0xFFF) == 0, "Node %u: %I64u available bytes is not a page multiple\n", Node, Bytes);

        LegacyBytes = 0;
        Result = pGetNumaAvailableMemoryNode((UCHAR)Node, &LegacyBytes);
        ok(Result, "Node %u: GetNumaAvailableMemoryNode failed: %lu\n", Node, GetLastError());
        ok(LegacyBytes != 0 && LegacyBytes <= MemoryStatus.ullTotalPhys,
           "Node %u: legacy %I64u available bytes\n", Node, LegacyBytes);
    }

    BadNodes[0] = (USHORT)(HighestNode + 1);
    BadNodes[1] = 0x100;
    BadNodes[2] = 0xFFFF;
    for (i = 0; i < ARRAYSIZE(BadNodes); i++)
    {
        Bytes = 0xCCCCCCCCCCCCCCCCULL;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaAvailableMemoryNodeEx(BadNodes[i], &Bytes);
        ok(!Result, "Node %u: expected failure\n", BadNodes[i]);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Node %u: LastError = %lu\n", BadNodes[i], GetLastError());
        ok(Bytes == 0xCCCCCCCCCCCCCCCCULL, "Node %u: output changed to %I64x\n", BadNodes[i], Bytes);
    }

    Bytes = 0xCCCCCCCCCCCCCCCCULL;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaAvailableMemoryNode((UCHAR)(HighestNode + 1), &Bytes);
    ok(!Result, "Legacy node %lu: expected failure\n", HighestNode + 1);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Legacy node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
}

static
void
Test_ProximityNode(void)
{
    ULONG BadIds[3];
    USHORT Node;
    UCHAR LegacyNode;
    BOOL Result;
    ULONG i;

    if (!pGetNumaProximityNodeEx || !pGetNumaProximityNode)
        return;

    Node = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaProximityNodeEx(0, &Node);
    ok(Result, "GetNumaProximityNodeEx(0) failed: %lu\n", GetLastError());
    ok(Node == 0, "GetNumaProximityNodeEx(0): node %u\n", Node);
    ok(GetLastError() == 0xDEADBEEF, "GetNumaProximityNodeEx(0): LastError = %lu\n", GetLastError());

    LegacyNode = 0xCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaProximityNode(0, &LegacyNode);
    ok(Result, "GetNumaProximityNode(0) failed: %lu\n", GetLastError());
    ok(LegacyNode == 0, "GetNumaProximityNode(0): node %u\n", LegacyNode);
    ok(GetLastError() == 0xDEADBEEF, "GetNumaProximityNode(0): LastError = %lu\n", GetLastError());

    BadIds[0] = 1;
    BadIds[1] = 0x10000;
    BadIds[2] = 0xFFFFFFFF;
    for (i = 0; i < ARRAYSIZE(BadIds); i++)
    {
        Node = 0xCCCC;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaProximityNodeEx(BadIds[i], &Node);
        ok(!Result, "Proximity %lx: expected failure\n", BadIds[i]);
        ok(GetLastError() == ERROR_NOT_FOUND, "Proximity %lx: LastError = %lu\n", BadIds[i], GetLastError());
        ok(Node == 0xCCCC, "Proximity %lx: node changed to %x\n", BadIds[i], Node);

        LegacyNode = 0xCC;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaProximityNode(BadIds[i], &LegacyNode);
        ok(!Result, "Legacy proximity %lx: expected failure\n", BadIds[i]);
        ok(GetLastError() == ERROR_NOT_FOUND, "Legacy proximity %lx: LastError = %lu\n", BadIds[i], GetLastError());
        ok(LegacyNode == 0xCC, "Legacy proximity %lx: node changed to %x\n", BadIds[i], LegacyNode);
    }
}

static
void
Test_ProximityNodeQuery(void)
{
    ULONG Buffer[4];
    ULONG Ids[2];
    ULONG Sizes[3];
    ULONG Length;
    NTSTATUS Status;
    ULONG i, j;

    Ids[0] = 0;
    Ids[1] = 1;
    Sizes[0] = 12;
    Sizes[1] = 8;
    Sizes[2] = 4;
    for (i = 0; i < ARRAYSIZE(Ids); i++)
    {
        for (j = 0; j < ARRAYSIZE(Sizes); j++)
        {
            FillMemory(Buffer, sizeof(Buffer), 0xCC);
            Buffer[0] = Ids[i];
            Length = 0xCCCCCCCC;
            Status = NtQuerySystemInformation(SystemNumaProximityNodeInformation, Buffer, Sizes[j], &Length);
            ok(Length == sizeof(ULONG) * 2, "Id %lu size %lu: length %lu\n", Ids[i], Sizes[j], Length);
            ok(Buffer[0] == Ids[i] && Buffer[2] == 0xCCCCCCCC,
               "Id %lu size %lu: data %08lx %08lx %08lx\n", Ids[i], Sizes[j], Buffer[0], Buffer[1], Buffer[2]);
            if (Sizes[j] < sizeof(ULONG) * 2)
            {
                ok(Status == STATUS_INFO_LENGTH_MISMATCH, "Id %lu size %lu: status %lx\n", Ids[i], Sizes[j], Status);
                ok(Buffer[1] == 0xCCCCCCCC, "Id %lu size %lu: node written %08lx\n", Ids[i], Sizes[j], Buffer[1]);
            }
            else if (Ids[i] == 0)
            {
                ok(Status == STATUS_SUCCESS, "Id %lu size %lu: status %lx\n", Ids[i], Sizes[j], Status);
                ok(Buffer[1] == 0xCCCC0000, "Id %lu size %lu: node %08lx\n", Ids[i], Sizes[j], Buffer[1]);
            }
            else
            {
                ok(Status == STATUS_NOT_FOUND, "Id %lu size %lu: status %lx\n", Ids[i], Sizes[j], Status);
                ok(Buffer[1] == 0xCCCCCCCC, "Id %lu size %lu: node written %08lx\n", Ids[i], Sizes[j], Buffer[1]);
            }
        }
    }
}

static
void
Test_NodeNumberFromHandle(void)
{
    WCHAR TempPath[MAX_PATH], FileName[MAX_PATH];
    HANDLE File, Event;
    USHORT Node;
    BOOL Result;

    if (!pGetNumaNodeNumberFromHandle)
        return;

    Node = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeNumberFromHandle(NULL, &Node);
    ok(!Result, "NULL handle: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "NULL handle: LastError = %lu\n", GetLastError());

    Node = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeNumberFromHandle(INVALID_HANDLE_VALUE, &Node);
    ok(!Result, "Current process handle: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Current process handle: LastError = %lu\n", GetLastError());
    ok(Node == 0xCCCC, "Current process handle: node changed to %x\n", Node);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    Node = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeNumberFromHandle(Event, &Node);
    ok(!Result, "Event handle: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Event handle: LastError = %lu\n", GetLastError());
    CloseHandle(Event);

    GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    GetTempFileNameW(TempPath, L"nma", 0, FileName);
    File = CreateFileW(FileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;

    if (pNtQueryInformationFile)
    {
        IO_STATUS_BLOCK IoStatus;
        ULONG Info[2];
        NTSTATUS Status;

        FillMemory(Info, sizeof(Info), 0xCC);
        IoStatus.Status = 0xCCCCCCCC;
        IoStatus.Information = 0xCCCC;
        Status = pNtQueryInformationFile(File, &IoStatus, Info, sizeof(Info), FileNumaNodeInformation);
        ok(Status == STATUS_SUCCESS, "FileNumaNodeInformation: status %lx\n", Status);
        ok(IoStatus.Status == STATUS_SUCCESS && IoStatus.Information == sizeof(USHORT),
           "FileNumaNodeInformation: iosb %lx/%Iu\n", IoStatus.Status, (size_t)IoStatus.Information);
        ok(Info[0] == 0xCCCC0000 && Info[1] == 0xCCCCCCCC,
           "FileNumaNodeInformation: data %08lx %08lx\n", Info[0], Info[1]);

        FillMemory(Info, sizeof(Info), 0xCC);
        Status = pNtQueryInformationFile(File, &IoStatus, Info, 0, FileNumaNodeInformation);
        ok(Status == STATUS_INFO_LENGTH_MISMATCH, "FileNumaNodeInformation size 0: status %lx\n", Status);

        FillMemory(Info, sizeof(Info), 0xCC);
        Status = pNtQueryInformationFile(File, &IoStatus, (PUCHAR)Info + 2, sizeof(USHORT), FileNumaNodeInformation);
        ok(Status == STATUS_SUCCESS, "FileNumaNodeInformation at offset 2: status %lx\n", Status);
        ok(Info[0] == 0x0000CCCC, "FileNumaNodeInformation at offset 2: data %08lx\n", Info[0]);

        Status = pNtQueryInformationFile(File, &IoStatus, (PUCHAR)Info + 1, sizeof(USHORT), FileNumaNodeInformation);
        ok(Status == STATUS_DATATYPE_MISALIGNMENT, "FileNumaNodeInformation at offset 1: status %lx\n", Status);
    }

    Node = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeNumberFromHandle(File, &Node);
    ok(Result, "File handle: GetNumaNodeNumberFromHandle failed: %lu\n", GetLastError());
    ok(Node == 0, "File handle: node %u\n", Node);
    ok(GetLastError() == 0xDEADBEEF, "File handle: LastError = %lu\n", GetLastError());
    CloseHandle(File);

    File = CreateFileW(L"C:\\Windows", 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Directory: CreateFileW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Node = 0xCCCC;
        Result = pGetNumaNodeNumberFromHandle(File, &Node);
        ok(Result && Node == 0, "Directory without access: result %d node %x error %lu\n", Result, Node, GetLastError());
        CloseHandle(File);
    }

    File = CreateFileW(L"\\\\.\\C:", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Volume: CreateFileW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Node = 0xCCCC;
        Result = pGetNumaNodeNumberFromHandle(File, &Node);
        ok(Result && Node == 0, "Volume: result %d node %x error %lu\n", Result, Node, GetLastError());
        CloseHandle(File);
    }

    File = CreateFileW(L"NUL", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "NUL: CreateFileW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        if (pNtQueryInformationFile)
        {
            IO_STATUS_BLOCK IoStatus;
            USHORT Info = 0xCCCC;
            NTSTATUS Status = pNtQueryInformationFile(File, &IoStatus, &Info, sizeof(Info), FileNumaNodeInformation);

            ok(Status == STATUS_NO_SUCH_DEVICE, "FileNumaNodeInformation(NUL): status %lx\n", Status);
            ok(Info == 0xCCCC, "FileNumaNodeInformation(NUL): node changed to %x\n", Info);
        }
        Node = 0xCCCC;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaNodeNumberFromHandle(File, &Node);
        ok(!Result, "NUL: expected failure, node %u\n", Node);
        ok(GetLastError() == ERROR_NO_SUCH_DEVICE, "NUL: LastError = %lu\n", GetLastError());
        ok(Node == 0xCCCC, "NUL: node changed to %x\n", Node);
        CloseHandle(File);
    }

    File = CreateNamedPipeW(L"\\\\.\\pipe\\LiberNTNumaApis", PIPE_ACCESS_DUPLEX, 0, 1, 512, 512, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateNamedPipeW failed: %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        Node = 0xCCCC;
        SetLastError(0xDEADBEEF);
        Result = pGetNumaNodeNumberFromHandle(File, &Node);
        ok(!Result, "Pipe: expected failure, node %u\n", Node);
        ok(GetLastError() == ERROR_NO_SUCH_DEVICE, "Pipe: LastError = %lu\n", GetLastError());
        CloseHandle(File);
    }
}

static
void
Test_ProcessorMask2(ULONG HighestNode)
{
    GROUP_AFFINITY Masks[4], ExMask;
    USHORT Required;
    BOOL Result;

    if (!pGetNumaNodeProcessorMask2 || !pGetNumaNodeProcessorMaskEx)
        return;

    Required = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeProcessorMask2(0, NULL, 0, &Required);
    ok(!Result, "No buffer: expected failure\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "No buffer: LastError = %lu\n", GetLastError());
    ok(Required == 1, "No buffer: required %u\n", Required);

    FillMemory(Masks, sizeof(Masks), 0xCC);
    Required = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeProcessorMask2(0, Masks, ARRAYSIZE(Masks), &Required);
    ok(Result, "GetNumaNodeProcessorMask2 failed: %lu\n", GetLastError());
    ok(Required == 1, "Required %u\n", Required);
    ok(GetLastError() == 0xDEADBEEF, "LastError = %lu\n", GetLastError());
    ok(Masks[1].Mask == (KAFFINITY)0xCCCCCCCCCCCCCCCCULL, "Second entry written: %Ix\n", (size_t)Masks[1].Mask);

    ok(pGetNumaNodeProcessorMaskEx(0, &ExMask), "GetNumaNodeProcessorMaskEx failed: %lu\n", GetLastError());
    ok(Masks[0].Mask == ExMask.Mask && Masks[0].Group == ExMask.Group,
       "Mask %Ix/%u, Ex mask %Ix/%u\n", (size_t)Masks[0].Mask, Masks[0].Group, (size_t)ExMask.Mask, ExMask.Group);
    ok(Masks[0].Reserved[0] == 0 && Masks[0].Reserved[1] == 0 && Masks[0].Reserved[2] == 0,
       "Reserved %x %x %x\n", Masks[0].Reserved[0], Masks[0].Reserved[1], Masks[0].Reserved[2]);

    Required = 0xCCCC;
    SetLastError(0xDEADBEEF);
    Result = pGetNumaNodeProcessorMask2((USHORT)(HighestNode + 1), Masks, ARRAYSIZE(Masks), &Required);
    ok(!Result, "Node %lu: expected failure\n", HighestNode + 1);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
    ok(Required == 0xCCCC, "Node %lu: required changed to %x\n", HighestNode + 1, Required);
}

static
void
Test_VirtualAllocExNuma(ULONG HighestNode)
{
    MEM_EXTENDED_PARAMETER Parameter;
    DWORD Nodes[2];
    NTSTATUS Status;
    SIZE_T Size;
    PVOID Base;
    ULONG i;

    if (!pVirtualAllocExNuma)
        return;

    Nodes[0] = 0;
    Nodes[1] = NUMA_NO_PREFERRED_NODE;
    for (i = 0; i < ARRAYSIZE(Nodes); i++)
    {
        SetLastError(0xDEADBEEF);
        Base = pVirtualAllocExNuma(GetCurrentProcess(), NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, Nodes[i]);
        ok(Base != NULL, "Node %lx: VirtualAllocExNuma failed: %lu\n", Nodes[i], GetLastError());
        if (!Base)
            continue;
        ((volatile UCHAR *)Base)[0x1000] = 0x5A;
        ok(((volatile UCHAR *)Base)[0x1000] == 0x5A, "Node %lx: write lost\n", Nodes[i]);
        ok(VirtualFree(Base, 0, MEM_RELEASE), "VirtualFree failed: %lu\n", GetLastError());
    }

    SetLastError(0xDEADBEEF);
    Base = pVirtualAllocExNuma(GetCurrentProcess(), NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, HighestNode + 1);
    ok(Base == NULL, "Node %lu: expected failure\n", HighestNode + 1);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
    if (Base)
        VirtualFree(Base, 0, MEM_RELEASE);

    SetLastError(0xDEADBEEF);
    Base = pVirtualAllocExNuma(GetCurrentProcess(), NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, 0x80000000);
    ok(Base == NULL, "Node 80000000: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Node 80000000: LastError = %lu\n", GetLastError());
    if (Base)
        VirtualFree(Base, 0, MEM_RELEASE);

    SetLastError(0xDEADBEEF);
    Base = pVirtualAllocExNuma(NULL, NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, HighestNode + 1);
    ok(Base == NULL, "NULL process: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "NULL process, node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());

    Base = VirtualAlloc(NULL, 0x20000, MEM_RESERVE, PAGE_READWRITE);
    ok(Base != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (Base)
    {
        PVOID Commit;

        SetLastError(0xDEADBEEF);
        Commit = pVirtualAllocExNuma(GetCurrentProcess(), Base, 0x1000, MEM_COMMIT, PAGE_READWRITE, HighestNode + 1);
        ok(Commit == NULL, "Commit in reserved region, node %lu: expected failure\n", HighestNode + 1);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Commit, node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());

        SetLastError(0xDEADBEEF);
        Commit = pVirtualAllocExNuma(GetCurrentProcess(), Base, 0x1000, MEM_COMMIT, PAGE_READWRITE, NUMA_NO_PREFERRED_NODE);
        ok(Commit == Base, "Commit in reserved region, no node: %p, error %lu\n", Commit, GetLastError());
        VirtualFree(Base, 0, MEM_RELEASE);
    }

    if (!pNtAllocateVirtualMemoryEx)
        return;

    ZeroMemory(&Parameter, sizeof(Parameter));
    Parameter.Type = MemExtendedParameterNumaNode;
    Parameter.ULong = HighestNode + 1;
    Base = NULL;
    Size = 0x10000;
    Status = pNtAllocateVirtualMemoryEx(GetCurrentProcess(), &Base, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &Parameter, 1);
    ok(Status == STATUS_INVALID_PARAMETER, "Node %lu: Status = %lx\n", HighestNode + 1, Status);
    if (NT_SUCCESS(Status))
        VirtualFree(Base, 0, MEM_RELEASE);

    Parameter.ULong = NUMA_NO_PREFERRED_NODE;
    Base = NULL;
    Size = 0x10000;
    Status = pNtAllocateVirtualMemoryEx(GetCurrentProcess(), &Base, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &Parameter, 1);
    ok(Status == STATUS_INVALID_PARAMETER, "Node -1: Status = %lx\n", Status);
    if (NT_SUCCESS(Status))
        VirtualFree(Base, 0, MEM_RELEASE);

    Parameter.ULong = 0;
    Base = NULL;
    Size = 0x10000;
    Status = pNtAllocateVirtualMemoryEx(GetCurrentProcess(), &Base, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &Parameter, 1);
    ok(Status == STATUS_SUCCESS, "Node 0: Status = %lx\n", Status);
    if (NT_SUCCESS(Status))
        VirtualFree(Base, 0, MEM_RELEASE);
}

static
void
Test_FileMappingNuma(ULONG HighestNode)
{
    HANDLE Mapping;
    PVOID View;
    DWORD Nodes[2];
    ULONG i;

    if (!pCreateFileMappingNumaW || !pCreateFileMappingNumaA || !pMapViewOfFileExNuma)
        return;

    Nodes[0] = 0;
    Nodes[1] = NUMA_NO_PREFERRED_NODE;
    for (i = 0; i < ARRAYSIZE(Nodes); i++)
    {
        SetLastError(0xDEADBEEF);
        Mapping = pCreateFileMappingNumaW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL, Nodes[i]);
        ok(Mapping != NULL, "Node %lx: CreateFileMappingNumaW failed: %lu\n", Nodes[i], GetLastError());
        ok(GetLastError() == ERROR_SUCCESS, "Node %lx: LastError = %lu\n", Nodes[i], GetLastError());
        if (!Mapping)
            continue;

        SetLastError(0xDEADBEEF);
        View = pMapViewOfFileExNuma(Mapping, FILE_MAP_WRITE, 0, 0, 0, NULL, Nodes[i]);
        ok(View != NULL, "Node %lx: MapViewOfFileExNuma failed: %lu\n", Nodes[i], GetLastError());
        if (View)
        {
            ((volatile UCHAR *)View)[0x2000] = 0xA5;
            ok(((volatile UCHAR *)View)[0x2000] == 0xA5, "Node %lx: write lost\n", Nodes[i]);
            ok(UnmapViewOfFile(View), "UnmapViewOfFile failed: %lu\n", GetLastError());
        }

        SetLastError(0xDEADBEEF);
        View = pMapViewOfFileExNuma(Mapping, FILE_MAP_WRITE, 0, 0, 0, NULL, HighestNode + 1);
        ok(View == NULL, "Map node %lu: expected failure\n", HighestNode + 1);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Map node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
        if (View)
            UnmapViewOfFile(View);

        CloseHandle(Mapping);
    }

    SetLastError(0xDEADBEEF);
    Mapping = pCreateFileMappingNumaW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL, HighestNode + 1);
    ok(Mapping == NULL, "Create node %lu: expected failure\n", HighestNode + 1);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Create node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
    if (Mapping)
        CloseHandle(Mapping);

    SetLastError(0xDEADBEEF);
    Mapping = pCreateFileMappingNumaA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, "LiberNTNumaMapping", 0);
    ok(Mapping != NULL, "CreateFileMappingNumaA failed: %lu\n", GetLastError());
    ok(GetLastError() == ERROR_SUCCESS, "CreateFileMappingNumaA: LastError = %lu\n", GetLastError());
    if (Mapping)
    {
        HANDLE Second;

        SetLastError(0xDEADBEEF);
        Second = pCreateFileMappingNumaA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, "LiberNTNumaMapping", 0);
        ok(Second != NULL, "Second CreateFileMappingNumaA failed: %lu\n", GetLastError());
        ok(GetLastError() == ERROR_ALREADY_EXISTS, "Second CreateFileMappingNumaA: LastError = %lu\n", GetLastError());
        if (Second)
            CloseHandle(Second);

        SetLastError(0xDEADBEEF);
        Second = pCreateFileMappingNumaA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, "LiberNTNumaMapping", HighestNode + 1);
        ok(Second == NULL, "Existing name, node %lu: expected failure\n", HighestNode + 1);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Existing name, node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());
        if (Second)
            CloseHandle(Second);
        CloseHandle(Mapping);
    }

    SetLastError(0xDEADBEEF);
    View = pMapViewOfFileExNuma(NULL, FILE_MAP_WRITE, 0, 0, 0, NULL, 0);
    ok(View == NULL, "NULL section handle: expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "NULL section handle: LastError = %lu\n", GetLastError());
}

static
void
Test_AllocateUserPhysicalPagesNuma(ULONG HighestNode)
{
    TOKEN_PRIVILEGES Privileges;
    ULONG_PTR Pages, PageArray[4];
    HANDLE Token;
    BOOL Result;

    if (!pAllocateUserPhysicalPagesNuma)
        return;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
    {
        skip("OpenProcessToken failed: %lu\n", GetLastError());
        return;
    }
    Privileges.PrivilegeCount = 1;
    ok(LookupPrivilegeValueW(NULL, L"SeLockMemoryPrivilege", &Privileges.Privileges[0].Luid),
       "LookupPrivilegeValueW failed: %lu\n", GetLastError());
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
    AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL);
    CloseHandle(Token);

    Pages = 1;
    SetLastError(0xDEADBEEF);
    Result = pAllocateUserPhysicalPagesNuma(GetCurrentProcess(), &Pages, PageArray, 0);
    ok(!Result, "Node 0 without SeLockMemoryPrivilege: expected failure\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "Node 0: LastError = %lu\n", GetLastError());

    Pages = 1;
    SetLastError(0xDEADBEEF);
    Result = pAllocateUserPhysicalPagesNuma(GetCurrentProcess(), &Pages, PageArray, HighestNode + 1);
    ok(!Result, "Node %lu: expected failure\n", HighestNode + 1);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Node %lu: LastError = %lu\n", HighestNode + 1, GetLastError());

    Pages = 1;
    SetLastError(0xDEADBEEF);
    Result = pAllocateUserPhysicalPagesNuma(GetCurrentProcess(), &Pages, PageArray, NUMA_NO_PREFERRED_NODE);
    ok(!Result, "No preferred node without SeLockMemoryPrivilege: expected failure\n");
    ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD, "No preferred node: LastError = %lu\n", GetLastError());
}

START_TEST(NumaApis)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    ULONG HighestNode;

    GET_PROC(Kernel32, GetNumaHighestNodeNumber);
    GET_PROC(Kernel32, GetNumaAvailableMemoryNode);
    GET_PROC(Kernel32, GetNumaAvailableMemoryNodeEx);
    GET_PROC(Kernel32, GetNumaProximityNode);
    GET_PROC(Kernel32, GetNumaProximityNodeEx);
    GET_PROC(Kernel32, GetNumaNodeNumberFromHandle);
    GET_PROC(Kernel32, GetNumaNodeProcessorMaskEx);
    GET_PROC(Kernel32, GetNumaNodeProcessorMask2);
    GET_PROC(Kernel32, VirtualAllocExNuma);
    GET_PROC(Kernel32, CreateFileMappingNumaW);
    GET_PROC(Kernel32, CreateFileMappingNumaA);
    GET_PROC(Kernel32, MapViewOfFileExNuma);
    GET_PROC(Kernel32, AllocateUserPhysicalPagesNuma);
    pNtAllocateVirtualMemoryEx = (void *)GetProcAddress(Ntdll, "NtAllocateVirtualMemoryEx");
    pNtQueryInformationFile = (void *)GetProcAddress(Ntdll, "NtQueryInformationFile");

    if (!pGetNumaHighestNodeNumber || !pGetNumaHighestNodeNumber(&HighestNode))
    {
        skip("GetNumaHighestNodeNumber failed\n");
        return;
    }

    Test_AvailableMemoryNodeEx(HighestNode);
    Test_ProximityNode();
    Test_ProximityNodeQuery();
    Test_NodeNumberFromHandle();
    Test_ProcessorMask2(HighestNode);
    Test_VirtualAllocExNuma(HighestNode);
    Test_FileMappingNuma(HighestNode);
    Test_AllocateUserPhysicalPagesNuma(HighestNode);
}
