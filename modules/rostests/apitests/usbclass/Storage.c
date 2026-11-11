/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test reading and writing a USB mass storage volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <ntddscsi.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define MARKER_NAME "USBCLASS.TXT"
#define MARKER_TEXT "LiberNT USB storage marker\r\n"
#define SCSI_ADAPTER_CLASS "{4d36e97b-e325-11ce-bfc1-08002be10318}"
#define USB_CLASS "{36fc9e60-c465-11cf-8056-444553540000}"
#define STATUS_CHECK_CONDITION 0x02
#define SENSE_KEY_ILLEGAL_REQUEST 0x05
#define SENSE_KEY_UNIT_ATTENTION 0x06

static const GUID DiskInterface = { 0x53f56307, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };

#define LOAD_THREADS 12
#define LOAD_BLOCKS 128
#define OTHER_UNIT_MS 15000
#define PURGED_READS 2
#define RECOVERY_MS 45000
#define UAS_SENSE_LENGTH 18

typedef struct _STORAGE_SPT
{
    SCSI_PASS_THROUGH_DIRECT Spt;
    UCHAR Sense[32];
} STORAGE_SPT;

typedef struct _STORAGE_SPT_BUFFERED
{
    SCSI_PASS_THROUGH Spt;
    UCHAR Sense[32];
    UCHAR Data[512];
} STORAGE_SPT_BUFFERED;

typedef struct _STORAGE_LOAD
{
    CHAR Path[MAX_PATH];
    HANDLE Threads[LOAD_THREADS];
    PUCHAR Expected;
    volatile LONG NextSlot;
    volatile LONG Stop;
    volatile LONG Reads;
    volatile LONG DeviceErrors;
    volatile LONG Failures;
    volatile LONG Reported;
} STORAGE_LOAD;

static
BOOL
SendCdb(HANDLE Disk, const UCHAR *Cdb, UCHAR DataIn, PVOID Buffer, ULONG Length, ULONG Timeout,
        STORAGE_SPT *Request, DWORD *Error)
{
    DWORD Returned;
    BOOL Result;

    ZeroMemory(Request, sizeof(*Request));
    Request->Spt.Length = sizeof(Request->Spt);
    Request->Spt.CdbLength = 10;
    Request->Spt.SenseInfoLength = sizeof(Request->Sense);
    Request->Spt.DataIn = DataIn;
    Request->Spt.DataTransferLength = Length;
    Request->Spt.TimeOutValue = Timeout;
    Request->Spt.DataBuffer = Buffer;
    Request->Spt.SenseInfoOffset = FIELD_OFFSET(STORAGE_SPT, Sense);
    memcpy(Request->Spt.Cdb, Cdb, 10);
    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, Request, sizeof(*Request), Request,
                             sizeof(*Request), &Returned, NULL);
    *Error = Result ? ERROR_SUCCESS : GetLastError();
    return Result;
}

static
void
TestTransport(HANDLE Disk, BOOL Uas, BOOL Usb3Host)
{
    STORAGE_PROPERTY_QUERY Query = { StorageDeviceProperty, PropertyStandardQuery };
    STORAGE_DEVICE_DESCRIPTOR Device;
    STORAGE_ADAPTER_DESCRIPTOR Adapter;
    UCHAR Whole[512];
    DWORD Returned;
    BOOL Result;

    ZeroMemory(&Device, sizeof(Device));
    Result = DeviceIoControl(Disk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), &Device, sizeof(Device),
                             &Returned, NULL);
    ok(Result, "StorageDeviceProperty failed: %lu\n", GetLastError());
    ok(Device.BusType == BusTypeUsb, "The disk is on bus type %u\n", Device.BusType);
    ok(Device.CommandQueueing == Uas, "The %s disk reports command queueing %u\n", Uas ? "UAS" : "bulk-only",
       Device.CommandQueueing);
    ok(Device.RawPropertiesLength ==
       (Uas ? 0 : sizeof(Device) - FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties)),
       "The %s disk reports %lu raw property bytes in a %u-byte buffer\n", Uas ? "UAS" : "bulk-only",
       Device.RawPropertiesLength, (UINT)sizeof(Device));
    ZeroMemory(Whole, sizeof(Whole));
    Result = DeviceIoControl(Disk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), Whole, sizeof(Whole),
                             &Returned, NULL);
    ok(Result && ((PSTORAGE_DEVICE_DESCRIPTOR)Whole)->RawPropertiesLength == (Uas ? 0 : 36),
       "The %s disk reports %lu raw property bytes\n", Uas ? "UAS" : "bulk-only",
       ((PSTORAGE_DEVICE_DESCRIPTOR)Whole)->RawPropertiesLength);

    ZeroMemory(&Adapter, sizeof(Adapter));
    Query.PropertyId = StorageAdapterProperty;
    Result = DeviceIoControl(Disk, IOCTL_STORAGE_QUERY_PROPERTY, &Query, sizeof(Query), &Adapter, sizeof(Adapter),
                             &Returned, NULL);
    ok(Result, "StorageAdapterProperty failed: %lu\n", GetLastError());
    ok(Adapter.BusType == BusTypeUsb, "The adapter is on bus type %u\n", Adapter.BusType);
    ok(Adapter.CommandQueueing == Uas, "The %s adapter reports command queueing %u\n", Uas ? "UAS" : "bulk-only",
       Adapter.CommandQueueing);
    ok(Adapter.AcceleratedTransfer == Uas && Adapter.AdapterUsesPio == Uas,
       "The %s adapter reports accelerated transfer %u and PIO %u\n", Uas ? "UAS" : "bulk-only",
       Adapter.AcceleratedTransfer, Adapter.AdapterUsesPio);
    ok(Adapter.BusMajorVersion == 2 && Adapter.BusMinorVersion == 0, "The %s adapter reports bus version %u.%u\n",
       Uas ? "UAS" : "bulk-only", Adapter.BusMajorVersion, Adapter.BusMinorVersion);
    ok(Adapter.AlignmentMask == (Uas ? 3 : 0), "The %s adapter reports alignment mask %lu\n", Uas ? "UAS" : "bulk-only",
       Adapter.AlignmentMask);
    if (Uas)
        ok(Adapter.MaximumTransferLength == 512 * 1024 && Adapter.MaximumPhysicalPages == 0xFFFFFFFF,
           "The UAS adapter transfers %lu bytes in %lu pages\n", Adapter.MaximumTransferLength,
           Adapter.MaximumPhysicalPages);
    else
        ok(Adapter.MaximumTransferLength == (Usb3Host ? 512 * 1024 : 64 * 1024) &&
           Adapter.MaximumPhysicalPages == Adapter.MaximumTransferLength / 4096 + 1,
           "The bulk-only adapter transfers %lu bytes in %lu pages\n", Adapter.MaximumTransferLength,
           Adapter.MaximumPhysicalPages);
}

static
BOOL
TestIdentity(ULONG DiskNumber, BOOL Uas, ULONG Luns)
{
    SP_DEVICE_INTERFACE_DATA Interface = { sizeof(Interface) };
    SP_DEVINFO_DATA Info = { sizeof(Info) };
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A Detail;
    STORAGE_DEVICE_NUMBER Number;
    CHAR Id[MAX_DEVICE_ID_LEN], ClassGuid[64], Ids[2048], Suffix[16], *Entry;
    DEVINST Parent = 0, Hub, Child;
    DWORD Index, Size, Returned;
    HDEVINFO Set;
    HANDLE Disk;
    BOOL Found = FALSE, Usb3Host = FALSE, Listed[2] = { FALSE, FALSE };
    ULONG Length, Children = 0, Lun;

    Set = SetupDiGetClassDevsA(&DiskInterface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevs failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;
    Detail = HeapAlloc(GetProcessHeap(), 0, 1024);
    for (Index = 0; !Found && SetupDiEnumDeviceInterfaces(Set, NULL, &DiskInterface, Index, &Interface); Index++)
    {
        Detail->cbSize = sizeof(*Detail);
        Size = 1024;
        if (!SetupDiGetDeviceInterfaceDetailA(Set, &Interface, Detail, Size, NULL, &Info))
            continue;
        Disk = CreateFileA(Detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;
        Found = DeviceIoControl(Disk, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned,
                                NULL) && Number.DeviceNumber == DiskNumber;
        CloseHandle(Disk);
    }
    HeapFree(GetProcessHeap(), 0, Detail);
    ok(Found, "No disk interface has device number %lu\n", DiskNumber);
    if (!Found)
    {
        SetupDiDestroyDeviceInfoList(Set);
        return FALSE;
    }

    ok(CM_Get_Device_IDA(Info.DevInst, Id, sizeof(Id), 0) == CR_SUCCESS, "CM_Get_Device_ID failed\n");
    ok(!_strnicmp(Id, Uas ? "SCSI\\DISK&VEN_" : "USBSTOR\\DISK&VEN_", Uas ? 14 : 17),
       "The %s disk is %s\n", Uas ? "UAS" : "bulk-only", Id);
    ok(CM_Get_Parent(&Parent, Info.DevInst, 0) == CR_SUCCESS, "CM_Get_Parent failed\n");
    ok(CM_Get_Device_IDA(Parent, Id, sizeof(Id), 0) == CR_SUCCESS && !_strnicmp(Id, "USB\\VID_", 8),
       "The %s disk's parent is %s\n", Uas ? "UAS" : "bulk-only", Id);
    Length = sizeof(ClassGuid);
    ok(CM_Get_DevNode_Registry_PropertyA(Parent, CM_DRP_CLASSGUID, NULL, ClassGuid, &Length, 0) == CR_SUCCESS &&
       !_stricmp(ClassGuid, Uas ? SCSI_ADAPTER_CLASS : USB_CLASS),
       "The %s disk's parent is in class %s\n", Uas ? "UAS" : "bulk-only", ClassGuid);
    Length = sizeof(Ids);
    ZeroMemory(Ids, sizeof(Ids));
    ok(CM_Get_DevNode_Registry_PropertyA(Parent, CM_DRP_COMPATIBLEIDS, NULL, Ids, &Length, 0) == CR_SUCCESS,
       "The parent has no compatible IDs\n");
    for (Entry = Ids; *Entry; Entry += strlen(Entry) + 1)
    {
        if (!_stricmp(Entry, "USB\\Class_08&SubClass_06&Prot_62"))
            Listed[0] = TRUE;
        else if (!_stricmp(Entry, "USB\\Class_08&SubClass_06&Prot_50"))
            Listed[1] = TRUE;
    }
    ok(Listed[0] == Uas && Listed[1] == !Uas, "The %s disk's parent lists Prot_62 %d and Prot_50 %d\n",
       Uas ? "UAS" : "bulk-only", Listed[0], Listed[1]);
    Hub = Parent;
    for (Index = 0; Index < 8; Index++)
    {
        if (CM_Get_Parent(&Hub, Hub, 0) != CR_SUCCESS || CM_Get_Device_IDA(Hub, Id, sizeof(Id), 0) != CR_SUCCESS ||
            !_strnicmp(Id, "USB\\ROOT_HUB", 12))
        {
            break;
        }
    }
    Usb3Host = !_strnicmp(Id, "USB\\ROOT_HUB30\\", 15);
    ok(Usb3Host || !_strnicmp(Id, "USB\\ROOT_HUB20\\", 15), "The USB device is attached to %s\n", Id);

    for (Index = CM_Get_Child(&Child, Parent, 0); Index == CR_SUCCESS; Index = CM_Get_Sibling(&Child, Child, 0))
    {
        if (CM_Get_Device_IDA(Child, Id, sizeof(Id), 0) != CR_SUCCESS)
            continue;
        Children++;
        if (!Uas)
            continue;
        Entry = strrchr(Id, '&');
        ok(!_strnicmp(Id, "SCSI\\DISK&VEN_", 14) && Entry != NULL && strlen(Entry) == 7,
           "The UAS device has child %s\n", Id);
        for (Lun = 0; Entry != NULL && Lun < Luns; Lun++)
        {
            sprintf(Suffix, "&%06lX", Lun);
            if (!_stricmp(Entry, Suffix))
                break;
        }
        ok(Lun < Luns, "The UAS device has child %s for no logical unit\n", Id);
    }
    ok(Children == Luns, "The %s device has %lu children instead of %lu\n", Uas ? "UAS" : "bulk-only", Children, Luns);
    SetupDiDestroyDeviceInfoList(Set);
    return Usb3Host;
}

static
BOOL
FindOtherUnit(ULONG DiskNumber, PCHAR Path)
{
    SP_DEVICE_INTERFACE_DATA Interface = { sizeof(Interface) };
    SP_DEVINFO_DATA Info = { sizeof(Info) };
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A Detail;
    STORAGE_DEVICE_NUMBER Number;
    DEVINST Parent, Own = 0;
    DWORD Index, Returned, Pass;
    HDEVINFO Set;
    HANDLE Disk;
    BOOL Found = FALSE;

    Set = SetupDiGetClassDevsA(&DiskInterface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;
    Detail = HeapAlloc(GetProcessHeap(), 0, 1024);
    for (Pass = 0; Pass < 2 && !Found; Pass++)
    {
        for (Index = 0; !Found && SetupDiEnumDeviceInterfaces(Set, NULL, &DiskInterface, Index, &Interface); Index++)
        {
            Detail->cbSize = sizeof(*Detail);
            if (!SetupDiGetDeviceInterfaceDetailA(Set, &Interface, Detail, 1024, NULL, &Info) ||
                CM_Get_Parent(&Parent, Info.DevInst, 0) != CR_SUCCESS)
            {
                continue;
            }
            Disk = CreateFileA(Detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (Disk == INVALID_HANDLE_VALUE)
                continue;
            if (!DeviceIoControl(Disk, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned,
                                 NULL))
            {
                Number.DeviceNumber = MAXDWORD;
            }
            CloseHandle(Disk);
            if (Pass == 0 && Number.DeviceNumber == DiskNumber)
            {
                Own = Parent;
                break;
            }
            if (Pass == 1 && Own != 0 && Parent == Own && Number.DeviceNumber != DiskNumber &&
                Number.DeviceNumber != MAXDWORD)
            {
                sprintf(Path, "\\\\.\\PhysicalDrive%lu", Number.DeviceNumber);
                Found = TRUE;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(Set);
    return Found;
}

static
void
ReadBlocks(PUCHAR Cdb, ULONG Lba)
{
    Cdb[0] = 0x28;
    Cdb[2] = (UCHAR)(Lba >> 24);
    Cdb[3] = (UCHAR)(Lba >> 16);
    Cdb[4] = (UCHAR)(Lba >> 8);
    Cdb[5] = (UCHAR)Lba;
    Cdb[7] = (UCHAR)(LOAD_BLOCKS >> 8);
    Cdb[8] = (UCHAR)LOAD_BLOCKS;
}

static
DWORD
WINAPI
LoadThread(PVOID Context)
{
    STORAGE_LOAD *Load = Context;
    ULONG Slot = (ULONG)InterlockedIncrement(&Load->NextSlot) - 1;
    UCHAR Cdb[10] = { 0 };
    STORAGE_SPT Request;
    PUCHAR Buffer;
    HANDLE Disk;
    DWORD Error;
    BOOL Result;

    Disk = CreateFileA(Load->Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    Buffer = VirtualAlloc(NULL, LOAD_BLOCKS * 512, MEM_COMMIT, PAGE_READWRITE);
    if (Disk == INVALID_HANDLE_VALUE || Buffer == NULL)
    {
        InterlockedIncrement(&Load->Failures);
        if (Disk != INVALID_HANDLE_VALUE)
            CloseHandle(Disk);
        return 0;
    }
    ReadBlocks(Cdb, Slot * LOAD_BLOCKS);
    while (!Load->Stop)
    {
        Result = SendCdb(Disk, Cdb, SCSI_IOCTL_DATA_IN, Buffer, LOAD_BLOCKS * 512, 10, &Request, &Error);
        if (Result && Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == LOAD_BLOCKS * 512 &&
            !memcmp(Buffer, Load->Expected + Slot * LOAD_BLOCKS * 512, LOAD_BLOCKS * 512))
        {
            InterlockedIncrement(&Load->Reads);
            continue;
        }
        if (!Result && Error == ERROR_IO_DEVICE)
        {
            InterlockedIncrement(&Load->DeviceErrors);
            continue;
        }
        InterlockedIncrement(&Load->Failures);
        if (InterlockedExchange(&Load->Reported, 1) == 0)
        {
            trace("A READ on the other logical unit gave %d, error %lu, SCSI status %u, sense key %x, %lu bytes\n",
                  Result, Error, Request.Spt.ScsiStatus, Request.Sense[2] & 0x0F, Request.Spt.DataTransferLength);
        }
    }
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(Disk);
    return 0;
}

static
STORAGE_LOAD *
StartLoad(ULONG DiskNumber)
{
    STORAGE_LOAD *Load;
    UCHAR Cdb[10] = { 0 };
    STORAGE_SPT Request;
    ULONGLONG Start;
    HANDLE Disk;
    DWORD Error;
    ULONG Index, Elapsed;
    BOOL Result = TRUE;

    Load = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Load));
    ok(Load != NULL && FindOtherUnit(DiskNumber, Load->Path), "The UAS device has no second logical unit\n");
    if (Load == NULL || Load->Path[0] == 0)
        goto Fail;
    Load->Expected = VirtualAlloc(NULL, LOAD_THREADS * LOAD_BLOCKS * 512, MEM_COMMIT, PAGE_READWRITE);
    Disk = CreateFileA(Load->Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    ok(Disk != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", Load->Path, GetLastError());
    if (Disk == INVALID_HANDLE_VALUE || Load->Expected == NULL)
        goto Fail;
    for (Index = 0; Result && Index < LOAD_THREADS; Index++)
    {
        ReadBlocks(Cdb, Index * LOAD_BLOCKS);
        Result = SendCdb(Disk, Cdb, SCSI_IOCTL_DATA_IN, Load->Expected + Index * LOAD_BLOCKS * 512,
                         LOAD_BLOCKS * 512, 10, &Request, &Error) && Request.Spt.ScsiStatus == 0;
    }
    trace("Throttling the other logical unit\n");
    Sleep(1500);
    Start = GetTickCount64();
    for (Index = 0; Result && Index < 4; Index++)
    {
        Result = SendCdb(Disk, Cdb, SCSI_IOCTL_DATA_IN, Load->Expected + (LOAD_THREADS - 1) * LOAD_BLOCKS * 512,
                         LOAD_BLOCKS * 512, 10, &Request, &Error) && Request.Spt.ScsiStatus == 0;
    }
    Elapsed = (ULONG)(GetTickCount64() - Start);
    CloseHandle(Disk);
    ok(Result, "Reading %s failed: error %lu, SCSI status %u\n", Load->Path, Error, Request.Spt.ScsiStatus);
    if (!Result)
        goto Fail;
    if (Elapsed < 600)
    {
        skip("The other logical unit is not throttled (4 READs in %lu ms)\n", Elapsed);
        goto Fail;
    }
    for (Index = 0; Index < LOAD_THREADS; Index++)
        Load->Threads[Index] = CreateThread(NULL, 0, LoadThread, Load, 0, NULL);
    Sleep(100);
    return Load;

Fail:
    if (Load != NULL)
    {
        if (Load->Expected != NULL)
            VirtualFree(Load->Expected, 0, MEM_RELEASE);
        HeapFree(GetProcessHeap(), 0, Load);
    }
    return NULL;
}

static
void
FinishLoad(STORAGE_LOAD *Load, ULONGLONG Start)
{
    UCHAR Cdb[10] = { 0 };
    STORAGE_SPT Request;
    PUCHAR Buffer;
    HANDLE Disk;
    DWORD Error, Wait;
    ULONG Index, Elapsed;
    BOOL Result = FALSE, Hung = FALSE;

    Sleep(200);
    InterlockedExchange(&Load->Stop, 1);
    for (Index = 0; Index < LOAD_THREADS; Index++)
    {
        ok(Load->Threads[Index] != NULL, "Creating load thread %lu failed\n", Index);
        if (Load->Threads[Index] == NULL)
            continue;
        Wait = WaitForSingleObject(Load->Threads[Index], 15000);
        ok(Wait == WAIT_OBJECT_0, "A READ on the other logical unit still hangs: wait gave %lu\n", Wait);
        if (Wait == WAIT_OBJECT_0)
            CloseHandle(Load->Threads[Index]);
        else
            Hung = TRUE;
    }
    if (Hung)
        return;
    trace("The other logical unit completed %ld READs with data and %ld with ERROR_IO_DEVICE during the reset\n",
          Load->Reads, Load->DeviceErrors);
    ok(Load->Failures == 0 && Load->Reads != 0,
       "The other logical unit completed %ld READs, %ld with ERROR_IO_DEVICE and %ld wrongly during the reset\n",
       Load->Reads, Load->DeviceErrors, Load->Failures);
    ok(Load->DeviceErrors <= PURGED_READS, "%ld READs on the other logical unit failed with ERROR_IO_DEVICE\n",
       Load->DeviceErrors);

    Disk = CreateFileA(Load->Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    Buffer = VirtualAlloc(NULL, LOAD_BLOCKS * 512, MEM_COMMIT, PAGE_READWRITE);
    if (Disk != INVALID_HANDLE_VALUE && Buffer != NULL)
    {
        ReadBlocks(Cdb, 0);
        Result = SendCdb(Disk, Cdb, SCSI_IOCTL_DATA_IN, Buffer, LOAD_BLOCKS * 512, 10, &Request, &Error) &&
                 Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == LOAD_BLOCKS * 512 &&
                 !memcmp(Buffer, Load->Expected, LOAD_BLOCKS * 512);
    }
    ok(Result, "The other logical unit does not read after the reset: error %lu\n", GetLastError());
    Elapsed = (ULONG)(GetTickCount64() - Start);
    trace("The other logical unit read again %lu ms after the hung READ was sent\n", Elapsed);
    ok(Elapsed < OTHER_UNIT_MS, "The other logical unit read again only %lu ms after the hung READ was sent\n",
       Elapsed);
    if (Buffer != NULL)
        VirtualFree(Buffer, 0, MEM_RELEASE);
    if (Disk != INVALID_HANDLE_VALUE)
        CloseHandle(Disk);
    VirtualFree(Load->Expected, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, Load);
}

static
void
TestCheckCondition(HANDLE Disk, const UCHAR *Cdb, ULONG BlockSize, BOOL Uas)
{
    STORAGE_SPT_BUFFERED Buffered;
    STORAGE_SPT Direct;
    PUCHAR Data;
    DWORD Returned, Error;
    ULONG SenseLength = Uas ? UAS_SENSE_LENGTH : sizeof(Buffered.Sense), DataLength = Uas ? BlockSize : 0;
    BOOL Result;

    ZeroMemory(&Buffered, sizeof(Buffered));
    Buffered.Spt.Length = sizeof(Buffered.Spt);
    Buffered.Spt.CdbLength = 10;
    Buffered.Spt.SenseInfoLength = sizeof(Buffered.Sense);
    Buffered.Spt.DataIn = SCSI_IOCTL_DATA_IN;
    Buffered.Spt.DataTransferLength = BlockSize;
    Buffered.Spt.TimeOutValue = 10;
    Buffered.Spt.DataBufferOffset = FIELD_OFFSET(STORAGE_SPT_BUFFERED, Data);
    Buffered.Spt.SenseInfoOffset = FIELD_OFFSET(STORAGE_SPT_BUFFERED, Sense);
    memcpy(Buffered.Spt.Cdb, Cdb, 10);
    Returned = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_SCSI_PASS_THROUGH, &Buffered, sizeof(Buffered), &Buffered, sizeof(Buffered),
                             &Returned, NULL);
    Error = Result ? ERROR_SUCCESS : GetLastError();
    trace("IOCTL_SCSI_PASS_THROUGH on a CHECK CONDITION gave %d, error %lu, SCSI status %u, %u sense bytes, sense key "
          "%x, %lu data bytes, %lu bytes returned\n", Result, Error, Buffered.Spt.ScsiStatus,
          Buffered.Spt.SenseInfoLength, Buffered.Sense[2] & 0x0F, Buffered.Spt.DataTransferLength, Returned);
    ok(Result, "IOCTL_SCSI_PASS_THROUGH on a CHECK CONDITION failed: %lu\n", Error);
    ok(Buffered.Spt.ScsiStatus == STATUS_CHECK_CONDITION, "IOCTL_SCSI_PASS_THROUGH returned SCSI status %u\n",
       Buffered.Spt.ScsiStatus);
    ok(Buffered.Spt.SenseInfoLength == SenseLength && (Buffered.Sense[2] & 0x0F) == SENSE_KEY_ILLEGAL_REQUEST,
       "IOCTL_SCSI_PASS_THROUGH returned %u sense bytes with sense key %x\n", Buffered.Spt.SenseInfoLength,
       Buffered.Sense[2] & 0x0F);
    ok(Buffered.Spt.DataTransferLength == DataLength, "IOCTL_SCSI_PASS_THROUGH returned %lu data bytes\n",
       Buffered.Spt.DataTransferLength);
    ok(Returned == FIELD_OFFSET(STORAGE_SPT_BUFFERED, Data) + Buffered.Spt.DataTransferLength,
       "IOCTL_SCSI_PASS_THROUGH returned %lu bytes, the data buffer starts at %lu\n", Returned,
       (ULONG)FIELD_OFFSET(STORAGE_SPT_BUFFERED, Data));

    Data = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    ok(Data != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (Data == NULL)
        return;
    ZeroMemory(&Direct, sizeof(Direct));
    Direct.Spt.Length = sizeof(Direct.Spt);
    Direct.Spt.CdbLength = 10;
    Direct.Spt.SenseInfoLength = sizeof(Direct.Sense);
    Direct.Spt.DataIn = SCSI_IOCTL_DATA_IN;
    Direct.Spt.DataTransferLength = BlockSize;
    Direct.Spt.TimeOutValue = 10;
    Direct.Spt.DataBuffer = Data;
    Direct.Spt.SenseInfoOffset = FIELD_OFFSET(STORAGE_SPT, Sense);
    memcpy(Direct.Spt.Cdb, Cdb, 10);
    Returned = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Result = DeviceIoControl(Disk, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Direct, sizeof(Direct), &Direct, sizeof(Direct),
                             &Returned, NULL);
    Error = Result ? ERROR_SUCCESS : GetLastError();
    trace("IOCTL_SCSI_PASS_THROUGH_DIRECT on a CHECK CONDITION gave %d, error %lu, SCSI status %u, %u sense bytes, "
          "sense key %x, %lu data bytes, %lu bytes returned\n", Result, Error, Direct.Spt.ScsiStatus,
          Direct.Spt.SenseInfoLength, Direct.Sense[2] & 0x0F, Direct.Spt.DataTransferLength, Returned);
    ok(Result, "IOCTL_SCSI_PASS_THROUGH_DIRECT on a CHECK CONDITION failed: %lu\n", Error);
    ok(Direct.Spt.ScsiStatus == STATUS_CHECK_CONDITION, "IOCTL_SCSI_PASS_THROUGH_DIRECT returned SCSI status %u\n",
       Direct.Spt.ScsiStatus);
    ok(Direct.Spt.SenseInfoLength == SenseLength && (Direct.Sense[2] & 0x0F) == SENSE_KEY_ILLEGAL_REQUEST,
       "IOCTL_SCSI_PASS_THROUGH_DIRECT returned %u sense bytes with sense key %x\n", Direct.Spt.SenseInfoLength,
       Direct.Sense[2] & 0x0F);
    ok(Direct.Spt.DataTransferLength == DataLength, "IOCTL_SCSI_PASS_THROUGH_DIRECT returned %lu data bytes\n",
       Direct.Spt.DataTransferLength);
    ok(Returned == FIELD_OFFSET(STORAGE_SPT, Sense) + Direct.Spt.SenseInfoLength,
       "IOCTL_SCSI_PASS_THROUGH_DIRECT returned %lu bytes, the sense data starts at %lu\n", Returned,
       (ULONG)FIELD_OFFSET(STORAGE_SPT, Sense));
    VirtualFree(Data, 0, MEM_RELEASE);
}

static
void
TestRecovery(const CHAR *Root)
{
    static const UCHAR ReadCapacity[10] = { 0x25 };
    UCHAR Read[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, Past[10];
    CHAR Path[MAX_PATH];
    STORAGE_DEVICE_NUMBER Number;
    STORAGE_SPT Request;
    PUCHAR First, Again;
    ULONGLONG Start, Elapsed;
    DWORD Returned, Error, BlockSize, Last;
    HANDLE Volume, Disk, File;
    BOOL Result, Uas;
    ULONG Luns;
    CHAR Text[16];
    STORAGE_LOAD *Load = NULL;

    sprintf(Path, "\\\\.\\%c:", Root[0]);
    Volume = CreateFileA(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Volume != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", Path, GetLastError());
    if (Volume == INVALID_HANDLE_VALUE)
        return;
    Result = DeviceIoControl(Volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &Number, sizeof(Number), &Returned,
                             NULL);
    ok(Result, "IOCTL_STORAGE_GET_DEVICE_NUMBER failed: %lu\n", GetLastError());
    CloseHandle(Volume);
    if (!Result)
        return;
    sprintf(Path, "\\\\.\\PhysicalDrive%lu", Number.DeviceNumber);
    Disk = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                       NULL);
    ok(Disk != INVALID_HANDLE_VALUE, "Opening %s failed: %lu\n", Path, GetLastError());
    if (Disk == INVALID_HANDLE_VALUE)
        return;
    sprintf(Path, "%sUSBCLASS.UAS", Root);
    Uas = GetFileAttributesA(Path) != INVALID_FILE_ATTRIBUTES;
    sprintf(Path, "%sUSBCLASS.LUN", Root);
    Luns = 1;
    File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(Text, sizeof(Text));
        if (ReadFile(File, Text, sizeof(Text) - 1, &Returned, NULL))
            Luns = strtoul(Text, NULL, 10);
        CloseHandle(File);
    }
    TestTransport(Disk, Uas, TestIdentity(Number.DeviceNumber, Uas, Luns));
    First = VirtualAlloc(NULL, 2 * 4096, MEM_COMMIT, PAGE_READWRITE);
    Again = First + 4096;

    Result = SendCdb(Disk, ReadCapacity, SCSI_IOCTL_DATA_IN, First, 8, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == 0, "READ CAPACITY gave %d, error %lu, SCSI status %u\n", Result, Error,
       Request.Spt.ScsiStatus);
    BlockSize = (First[4] << 24) | (First[5] << 16) | (First[6] << 8) | First[7];
    ok(BlockSize == 512, "The block size is %lu\n", BlockSize);
    if (!Result || BlockSize == 0 || BlockSize > 4096)
        goto Done;
    memcpy(Read + 2, First, 4);
    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_IN, First, BlockSize, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == BlockSize,
       "READ gave %d, error %lu, SCSI status %u, %lu bytes\n", Result, Error, Request.Spt.ScsiStatus,
       Request.Spt.DataTransferLength);

    Last = ((ULONG)Read[2] << 24) | ((ULONG)Read[3] << 16) | ((ULONG)Read[4] << 8) | Read[5];
    memcpy(Past, Read, sizeof(Past));
    Past[2] = (UCHAR)((Last + 1) >> 24);
    Past[3] = (UCHAR)((Last + 1) >> 16);
    Past[4] = (UCHAR)((Last + 1) >> 8);
    Past[5] = (UCHAR)(Last + 1);
    Result = SendCdb(Disk, Past, SCSI_IOCTL_DATA_IN, Again, BlockSize, 10, &Request, &Error);
    ok(Result && Request.Spt.ScsiStatus == STATUS_CHECK_CONDITION && Request.Spt.SenseInfoLength > 2 &&
       (Request.Sense[2] & 0x0F) == SENSE_KEY_ILLEGAL_REQUEST,
       "A READ past the end gave %d, error %lu, SCSI status %u, %u sense bytes, sense key %x\n", Result, Error,
       Request.Spt.ScsiStatus, Request.Spt.SenseInfoLength, Request.Sense[2] & 0x0F);
    TestCheckCondition(Disk, Past, BlockSize, Uas);

    if (Uas && Luns > 1)
        Load = StartLoad(Number.DeviceNumber);
    trace("Sending a READ with no data phase\n");
    Start = GetTickCount64();
    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_UNSPECIFIED, NULL, 0, 2, &Request, &Error);
    Elapsed = GetTickCount64() - Start;
    trace("The READ with no data phase gave error %lu after %lu ms\n", Error, (ULONG)Elapsed);
    ok(!Result && (Error == ERROR_SEM_TIMEOUT || Error == ERROR_IO_DEVICE),
       "A READ with no data phase gave %d, error %lu, SCSI status %u after %lu ms\n", Result, Error,
       Request.Spt.ScsiStatus, (ULONG)Elapsed);
    ok(Elapsed < RECOVERY_MS, "A READ with no data phase completed only after %lu ms\n", (ULONG)Elapsed);
    if (Load != NULL)
        FinishLoad(Load, Start);

    Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_IN, Again, BlockSize, 10, &Request, &Error);
    if (Result && Request.Spt.ScsiStatus == STATUS_CHECK_CONDITION && Request.Spt.SenseInfoLength > 2 &&
        (Request.Sense[2] & 0x0F) == SENSE_KEY_UNIT_ATTENTION)
    {
        trace("The device reported the unit attention of its reset\n");
        Result = SendCdb(Disk, Read, SCSI_IOCTL_DATA_IN, Again, BlockSize, 10, &Request, &Error);
    }
    ok(Result && Request.Spt.ScsiStatus == 0 && Request.Spt.DataTransferLength == BlockSize &&
       !memcmp(First, Again, BlockSize), "The device did not recover: READ gave %d, error %lu, SCSI status %u\n",
       Result, Error, Request.Spt.ScsiStatus);
    Elapsed = GetTickCount64() - Start;
    trace("The device read again %lu ms after the hung READ was sent\n", (ULONG)Elapsed);
    ok(Elapsed < RECOVERY_MS, "The device read again only %lu ms after the hung READ was sent\n", (ULONG)Elapsed);
Done:
    VirtualFree(First, 0, MEM_RELEASE);
    CloseHandle(Disk);
}

/* The volume under test carries a marker file the harness put there, and USBCLASS.UAS when it is attached through UAS. */
START_TEST(Storage)
{
    CHAR Root[] = "A:\\", Path[MAX_PATH], Buffer[256];
    static const CHAR Payload[] = "written through the USB storage stack";
    DWORD Drives = GetLogicalDrives(), Read, Written;
    HANDLE File;
    UINT Drive;
    BOOL Found = FALSE;

    for (Drive = 2; Drive < 26 && !Found; ++Drive)
    {
        if (!(Drives & (1u << Drive)))
            continue;
        Root[0] = (CHAR)('A' + Drive);
        if (GetDriveTypeA(Root) != DRIVE_REMOVABLE && GetDriveTypeA(Root) != DRIVE_FIXED)
            continue;
        sprintf(Path, "%s%s", Root, MARKER_NAME);
        File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (File == INVALID_HANDLE_VALUE)
            continue;
        Found = TRUE;
        ZeroMemory(Buffer, sizeof(Buffer));
        ok(ReadFile(File, Buffer, sizeof(Buffer) - 1, &Read, NULL), "Reading %s failed: %lu\n", Path, GetLastError());
        CloseHandle(File);
        ok(!strcmp(Buffer, MARKER_TEXT), "The marker on %s reads '%s'\n", Root, Buffer);

        sprintf(Path, "%sUSBWRITE.TXT", Root);
        File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
        ok(File != INVALID_HANDLE_VALUE, "Creating %s failed: %lu\n", Path, GetLastError());
        if (File == INVALID_HANDLE_VALUE)
            break;
        ok(WriteFile(File, Payload, sizeof(Payload), &Written, NULL) && Written == sizeof(Payload),
           "Writing %s failed: %lu\n", Path, GetLastError());
        ok(FlushFileBuffers(File), "Flushing %s failed: %lu\n", Path, GetLastError());
        CloseHandle(File);
        File = CreateFileA(Path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
        ok(File != INVALID_HANDLE_VALUE, "Opening %s again failed: %lu\n", Path, GetLastError());
        if (File != INVALID_HANDLE_VALUE)
        {
            PVOID Aligned = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);

            ok(ReadFile(File, Aligned, 4096, &Read, NULL), "Reading %s back failed: %lu\n", Path, GetLastError());
            ok(Read == sizeof(Payload) && !memcmp(Aligned, Payload, sizeof(Payload)),
               "%s read back %lu bytes\n", Path, Read);
            VirtualFree(Aligned, 0, MEM_RELEASE);
            CloseHandle(File);
        }
        ok(DeleteFileA(Path), "Deleting %s failed: %lu\n", Path, GetLastError());
        trace("USB storage volume %s read and written\n", Root);
        TestRecovery(Root);
    }
    if (!Found)
        skip("No volume carries %s\n", MARKER_NAME);
}
