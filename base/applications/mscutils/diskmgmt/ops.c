/*
 * PROJECT:     LiberNT Disk Management
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Volume and disk operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "diskmgmt.h"

#define DU_MIN_VOLUME   (16ull * DU_MEGABYTE)
#define DU_MSR_SIZE     (16ull * DU_MEGABYTE)
#define DU_LETTER_AUTO  ((WCHAR)1)

typedef enum _DU_WORK_KIND
{
    DuWorkCreate,
    DuWorkFormat,
    DuWorkCheck,
    DuWorkShrink,
    DuWorkExtend,
    DuWorkEraseDisk
} DU_WORK_KIND;

typedef struct _DU_WORK
{
    DU_WORK_KIND Kind;
    HWND hDlg;
    UINT Title;
    ULONG DiskNumber;
    PARTITION_STYLE Style;
    ULONGLONG Offset;
    ULONGLONG Length;
    ULONGLONG Bytes;
    WCHAR Letter;
    WCHAR Name[MAX_PATH];
    WCHAR FileSystem[32];
    WCHAR Root[MAX_PATH];
    WCHAR DeviceName[MAX_PATH];
    BOOL Quick;
    BOOL TempLetter;
    BOOL FormatOk;
    DWORD Error;
    UINT ErrorText;
    LONG Refs;
    volatile LONG Cancel;
} DU_WORK, *PDU_WORK;

static PDU_WORK DuCurrentWork;
static volatile LONG DuWorkActive;

static PDU_DISK
SelDisk(
    _In_ const DU_SELECTION *Sel)
{
    if (Sel->Type == DuSelNone || Sel->Disk >= g_Model.DiskCount)
        return NULL;
    return &g_Model.Disks[Sel->Disk];
}

static PDU_PART
SelPart(
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);

    if (!Disk || Sel->Type != DuSelPart || Sel->Part >= Disk->PartCount)
        return NULL;
    return &Disk->Parts[Sel->Part];
}

static PDU_VOLUME
SelVolume(
    _In_ const DU_SELECTION *Sel)
{
    PDU_PART Part = SelPart(Sel);

    return Part ? DuPartVolume(&g_Model, Part) : NULL;
}

static INT
LargestFree(
    _In_ PDU_DISK Disk)
{
    ULONG p;
    INT Best = -1;

    for (p = 0; p < Disk->PartCount; p++)
    {
        if (Disk->Parts[p].Kind == DuPartFree && Disk->Parts[p].Length >= DU_MIN_VOLUME &&
            (Best < 0 || Disk->Parts[p].Length > Disk->Parts[Best].Length))
        {
            Best = (INT)p;
        }
    }
    return Best;
}

static BOOL
VolumeLetter(
    _In_ PDU_VOLUME Volume,
    _Out_opt_ PWCHAR Letter)
{
    if (!Volume || !Volume->MountPath[0] || Volume->MountPath[1] != L':' || Volume->MountPath[3] != UNICODE_NULL)
        return FALSE;
    if (Letter)
        *Letter = Volume->MountPath[0];
    return TRUE;
}

BOOL
DuCanNewVolume(
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    PDU_PART Part = SelPart(Sel);

    if (!Disk || Disk->Style == PARTITION_STYLE_RAW)
        return FALSE;
    if (Part)
        return Part->Kind == DuPartFree && Part->Length >= DU_MIN_VOLUME;
    return LargestFree(Disk) >= 0;
}

BOOL
DuCanDelete(
    _In_ const DU_SELECTION *Sel)
{
    PDU_PART Part = SelPart(Sel);

    return Part && Part->Kind != DuPartFree && !DuPartIsProtected(&g_Model, SelDisk(Sel), Part);
}

BOOL
DuCanResize(
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);

    return Volume && Volume->HasFileSystem && !lstrcmpiW(Volume->FileSystem, L"NTFS") &&
           SelPart(Sel)->Kind == DuPartData;
}

BOOL
DuCanErase(
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    PDU_PART Part = SelPart(Sel);

    if (!Disk)
        return FALSE;
    if (Sel->Type == DuSelDisk)
        return !DuDiskHasProtectedVolume(&g_Model, Disk);
    return Part && (Part->Kind == DuPartData || Part->Kind == DuPartOther) &&
           SelVolume(Sel) && !DuPartIsProtected(&g_Model, Disk, Part);
}

BOOL
DuCanCheck(
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);

    return Volume && Volume->HasFileSystem;
}

BOOL
DuCanMount(
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);
    PDU_PART Part = SelPart(Sel);

    return Volume && Volume->DeviceName[0] && !Volume->Boot && Part->Kind == DuPartData;
}

BOOL
DuIsMounted(
    _In_ const DU_SELECTION *Sel)
{
    return VolumeLetter(SelVolume(Sel), NULL);
}

BOOL
DuCanInfo(
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);

    return Volume && Volume->HasFileSystem && Volume->MountPath[0];
}

static HANDLE
OpenDisk(
    _In_ ULONG Number)
{
    WCHAR Path[64];

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number);
    return CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
}

static PDRIVE_LAYOUT_INFORMATION_EX
ReadLayout(
    _In_ HANDLE Disk)
{
    DWORD Size = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 255 * sizeof(PARTITION_INFORMATION_EX);
    PDRIVE_LAYOUT_INFORMATION_EX Layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    DWORD Returned;

    if (Layout && !DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL))
    {
        DWORD Error = GetLastError();

        HeapFree(GetProcessHeap(), 0, Layout);
        SetLastError(Error);
        return NULL;
    }
    return Layout;
}

static BOOL
WriteLayout(
    _In_ HANDLE Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) +
                 max(Layout->PartitionCount, 1) * sizeof(PARTITION_INFORMATION_EX);
    DWORD Returned;
    ULONG i;

    for (i = 0; i < Layout->PartitionCount; i++)
        Layout->PartitionEntry[i].RewritePartition = TRUE;
    if (!DeviceIoControl(Disk, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, Layout, Size, NULL, 0, &Returned, NULL))
        return FALSE;
    DeviceIoControl(Disk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &Returned, NULL);
    return TRUE;
}

static PPARTITION_INFORMATION_EX
NewLayoutEntry(
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    ULONG i, Limit = (Layout->PartitionStyle == PARTITION_STYLE_MBR) ? min(Layout->PartitionCount, 4) : Layout->PartitionCount;

    for (i = 0; i < Limit; i++)
    {
        PPARTITION_INFORMATION_EX Entry = &Layout->PartitionEntry[i];

        if (Layout->PartitionStyle == PARTITION_STYLE_MBR ?
            Entry->Mbr.PartitionType == PARTITION_ENTRY_UNUSED :
            (IsEqualGUID(&Entry->Gpt.PartitionType, &GUID_NULL) || Entry->PartitionLength.QuadPart == 0))
        {
            return Entry;
        }
    }
    if (Layout->PartitionStyle == PARTITION_STYLE_MBR || Layout->PartitionCount >= 128)
        return NULL;
    return &Layout->PartitionEntry[Layout->PartitionCount++];
}

static DWORD
AddPartition(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length,
    _In_ const GUID *GptType,
    _In_ UCHAR MbrType,
    _In_opt_ PCWSTR Name)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout = ReadLayout(Disk);
    PPARTITION_INFORMATION_EX Entry;
    DWORD Error = ERROR_SUCCESS;

    if (!Layout)
        return GetLastError();
    Entry = NewLayoutEntry(Layout);
    if (!Entry)
    {
        HeapFree(GetProcessHeap(), 0, Layout);
        return ERROR_PARTITION_FAILURE;
    }

    ZeroMemory(Entry, sizeof(*Entry));
    Entry->PartitionStyle = Layout->PartitionStyle;
    Entry->StartingOffset.QuadPart = Offset;
    Entry->PartitionLength.QuadPart = Length;
    if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        Entry->Gpt.PartitionType = *GptType;
        CoCreateGuid(&Entry->Gpt.PartitionId);
        if (Name)
            StringCchCopyNW(Entry->Gpt.Name, ARRAYSIZE(Entry->Gpt.Name), Name, ARRAYSIZE(Entry->Gpt.Name) - 1);
    }
    else
    {
        Entry->Mbr.PartitionType = MbrType;
        Entry->Mbr.RecognizedPartition = TRUE;
        Entry->Mbr.HiddenSectors = 0;
    }
    if (!WriteLayout(Disk, Layout))
        Error = GetLastError();
    HeapFree(GetProcessHeap(), 0, Layout);
    return Error;
}

static DWORD
RemovePartition(
    _In_ HANDLE Disk,
    _In_ ULONGLONG Offset)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout = ReadLayout(Disk);
    DWORD Error = ERROR_NOT_FOUND;
    ULONG i;

    if (!Layout)
        return GetLastError();
    for (i = 0; i < Layout->PartitionCount; i++)
    {
        PPARTITION_INFORMATION_EX Entry = &Layout->PartitionEntry[i];

        if (Entry->StartingOffset.QuadPart != (LONGLONG)Offset || Entry->PartitionLength.QuadPart == 0)
            continue;
        if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
        {
            MoveMemory(Entry, Entry + 1, (Layout->PartitionCount - i - 1) * sizeof(*Entry));
            Layout->PartitionCount--;
        }
        else
        {
            ZeroMemory(&Entry->StartingOffset, sizeof(*Entry) - FIELD_OFFSET(PARTITION_INFORMATION_EX, StartingOffset));
            Entry->PartitionStyle = PARTITION_STYLE_MBR;
        }
        Error = WriteLayout(Disk, Layout) ? ERROR_SUCCESS : GetLastError();
        break;
    }
    HeapFree(GetProcessHeap(), 0, Layout);
    return Error;
}

static HANDLE
LockVolume(
    _In_ PDU_VOLUME Volume,
    _Out_ PDWORD Error)
{
    WCHAR Path[64];
    HANDLE Handle;
    DWORD Returned;
    SIZE_T Length;

    StringCchCopyW(Path, ARRAYSIZE(Path), Volume->GuidPath);
    Length = wcslen(Path);
    if (Length && Path[Length - 1] == L'\\')
        Path[Length - 1] = UNICODE_NULL;
    Handle = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        *Error = GetLastError();
        return NULL;
    }
    if (!DeviceIoControl(Handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Returned, NULL))
    {
        *Error = GetLastError();
        CloseHandle(Handle);
        return NULL;
    }
    DeviceIoControl(Handle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Returned, NULL);
    *Error = ERROR_SUCCESS;
    return Handle;
}

static VOID
RemoveLetters(
    _In_ PDU_VOLUME Volume)
{
    WCHAR Letter;

    if (VolumeLetter(Volume, &Letter))
        StorageUtilDeleteDriveLetter(Letter);
}

static PDU_MODEL
FindVolume(
    _In_ ULONG DiskNumber,
    _In_ ULONGLONG Offset,
    _Out_ PDU_VOLUME *Found)
{
    PDU_MODEL Model = HeapAlloc(GetProcessHeap(), 0, sizeof(DU_MODEL));
    ULONG Attempt, v;

    *Found = NULL;
    if (!Model)
        return NULL;
    for (Attempt = 0; Attempt < 120; Attempt++)
    {
        DuModelRefresh(Model);
        for (v = 0; v < Model->VolumeCount; v++)
        {
            if (Model->Volumes[v].DiskNumber == DiskNumber && Model->Volumes[v].Offset == Offset &&
                Model->Volumes[v].DeviceName[0])
            {
                *Found = &Model->Volumes[v];
                return Model;
            }
        }
        Sleep(500);
    }
    return Model;
}

static BOOL
WaitForRoot(
    _In_ PDU_WORK Work)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < 60 && !Work->Cancel; Attempt++)
    {
        UINT Type = GetDriveTypeW(Work->Root);

        if (Type != DRIVE_UNKNOWN && Type != DRIVE_NO_ROOT_DIR)
            return TRUE;
        Sleep(500);
    }
    return FALSE;
}

static BOOLEAN NTAPI
DuFmifsCallback(
    _In_ CALLBACKCOMMAND Command,
    _In_ ULONG SubAction,
    _In_ PVOID ActionInfo)
{
    PDU_WORK Work = DuCurrentWork;

    UNREFERENCED_PARAMETER(SubAction);
    if (!Work)
        return TRUE;
    if (Work->Cancel)
        return FALSE;
    switch (Command)
    {
        case PROGRESS:
            PostMessageW(Work->hDlg, WM_DU_WORK_PROGRESS, *(PULONG)ActionInfo, 0);
            break;
        case DONE:
            Work->FormatOk = *(PBOOLEAN)ActionInfo;
            break;
        case OUTPUT:
        {
            PTEXTOUTPUT Output = (PTEXTOUTPUT)ActionInfo;
            INT Length;
            PWSTR Text;

            if (!Output || !Output->Output)
                break;
            Length = MultiByteToWideChar(CP_OEMCP, 0, Output->Output, -1, NULL, 0);
            Text = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
            if (Text)
            {
                MultiByteToWideChar(CP_OEMCP, 0, Output->Output, -1, Text, Length);
                if (!PostMessageW(Work->hDlg, WM_DU_WORK_OUTPUT, 0, (LPARAM)Text))
                    HeapFree(GetProcessHeap(), 0, Text);
            }
            break;
        }
        case INSUFFICIENTRIGHTS:
            Work->Error = ERROR_ACCESS_DENIED;
            break;
        case FSNOTSUPPORTED:
            Work->Error = ERROR_NOT_SUPPORTED;
            break;
        case VOLUMEINUSE:
            Work->Error = ERROR_SHARING_VIOLATION;
            return FALSE;
        case CLUSTERSIZETOOSMALL:
            Work->Error = ERROR_INVALID_PARAMETER;
            break;
        default:
            break;
    }
    return TRUE;
}

static BOOLEAN NTAPI
DuResizeProgress(
    _In_opt_ PVOID Context,
    _In_ ULONG Percent)
{
    PDU_WORK Work = Context;

    PostMessageW(Work->hDlg, WM_DU_WORK_PROGRESS, Percent, 0);
    return TRUE;
}

static VOID
WorkStatus(
    _In_ PDU_WORK Work,
    _In_ UINT Text)
{
    PWSTR Copy = HeapAlloc(GetProcessHeap(), 0, 256 * sizeof(WCHAR));

    if (Copy)
    {
        DuLoadString(Text, Copy, 256);
        PostMessageW(Work->hDlg, WM_DU_WORK_OUTPUT, 1, (LPARAM)Copy);
    }
}

static BOOL
WorkFormat(
    _In_ PDU_WORK Work)
{
    WorkStatus(Work, IDS_STATUS_FORMATTING);
    Work->FormatOk = FALSE;
    DuCurrentWork = Work;
    FormatEx(Work->Root, FMIFS_HARDDISK, Work->FileSystem, Work->Name, (BOOLEAN)Work->Quick, 0, DuFmifsCallback);
    DuCurrentWork = NULL;
    if (!Work->FormatOk)
    {
        if (Work->Error == ERROR_SUCCESS)
            Work->Error = ERROR_GEN_FAILURE;
        Work->ErrorText = IDS_ERR_FORMAT;
        return FALSE;
    }
    SetVolumeLabelW(Work->Root, Work->Name[0] ? Work->Name : NULL);
    SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATHW, Work->Root, NULL);
    return TRUE;
}

static BOOL
AssignLetter(
    _In_ PCWSTR DeviceName,
    _Out_ PWCHAR Letter)
{
    DWORD Used;
    WCHAR Next;

    if (!StorageUtilAssignNextDriveLetter(DeviceName, Letter))
        return FALSE;
    if (*Letter)
        return TRUE;
    Used = GetLogicalDrives();
    for (Next = L'C'; Next <= L'Z'; Next++)
    {
        if (!(Used & (1u << (Next - L'A'))) && StorageUtilAssignDriveLetter(DeviceName, Next))
        {
            *Letter = Next;
            return TRUE;
        }
    }
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

static BOOL
WorkAcquireRoot(
    _In_ PDU_WORK Work,
    _In_ PCWSTR DeviceName,
    _In_ WCHAR Wanted)
{
    WCHAR Letter = 0;

    StorageUtilGetDriveLetter(DeviceName, &Letter);
    if (Wanted && Wanted != DU_LETTER_AUTO && towupper(Letter) != towupper(Wanted))
    {
        if (Letter)
            StorageUtilDeleteDriveLetter(Letter);
        Letter = StorageUtilAssignDriveLetter(DeviceName, Wanted) ? Wanted : 0;
    }
    if (!Letter)
    {
        if (!AssignLetter(DeviceName, &Letter))
        {
            Work->Error = GetLastError();
            Work->ErrorText = IDS_ERR_LETTER;
            return FALSE;
        }
        Work->TempLetter = !Wanted;
    }
    StringCchPrintfW(Work->Root, ARRAYSIZE(Work->Root), L"%c:\\", towupper(Letter));
    if (!WaitForRoot(Work))
    {
        Work->Error = Work->Cancel ? ERROR_CANCELLED : ERROR_NOT_READY;
        Work->ErrorText = IDS_ERR_LETTER;
        return FALSE;
    }
    return TRUE;
}

static VOID
WorkReleaseRoot(
    _In_ PDU_WORK Work)
{
    if (Work->TempLetter && Work->Root[0])
        StorageUtilDeleteDriveLetter(Work->Root[0]);
    Work->TempLetter = FALSE;
}

static BOOL
WorkCreate(
    _In_ PDU_WORK Work,
    _In_ HANDLE Disk)
{
    PDU_VOLUME Volume;
    PDU_MODEL Model;
    DWORD Error;
    BOOL Ok;

    WorkStatus(Work, IDS_STATUS_PARTITION);
    Error = AddPartition(Disk, Work->Offset, Work->Length, &PARTITION_BASIC_DATA_GUID,
                         !lstrcmpiW(Work->FileSystem, L"FAT32") ? PARTITION_FAT32_XINT13 : PARTITION_IFS,
                         L"Basic data partition");
    if (Error != ERROR_SUCCESS)
    {
        Work->Error = Error;
        Work->ErrorText = IDS_ERR_PARTITION;
        return FALSE;
    }

    WorkStatus(Work, IDS_STATUS_WAITING);
    Model = FindVolume(Work->DiskNumber, Work->Offset, &Volume);
    if (!Volume)
    {
        if (Model)
            HeapFree(GetProcessHeap(), 0, Model);
        Work->Error = ERROR_NOT_READY;
        Work->ErrorText = IDS_ERR_NOVOLUME;
        return FALSE;
    }
    StringCchCopyW(Work->DeviceName, ARRAYSIZE(Work->DeviceName), Volume->DeviceName);
    HeapFree(GetProcessHeap(), 0, Model);

    if (!WorkAcquireRoot(Work, Work->DeviceName, Work->Letter ? Work->Letter : DU_LETTER_AUTO))
        return FALSE;
    Ok = WorkFormat(Work);
    if (!Work->Letter)
        Work->TempLetter = TRUE;
    WorkReleaseRoot(Work);
    return Ok;
}

static VOID
WorkRelease(
    _In_ PDU_WORK Work)
{
    if (!InterlockedDecrement(&Work->Refs))
        HeapFree(GetProcessHeap(), 0, Work);
}

static DWORD WINAPI
WorkThread(
    _In_ LPVOID Parameter)
{
    PDU_WORK Work = Parameter;
    HANDLE Disk = NULL;
    BOOL Ok = FALSE;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    switch (Work->Kind)
    {
        case DuWorkCreate:
            Disk = OpenDisk(Work->DiskNumber);
            if (Disk == INVALID_HANDLE_VALUE)
            {
                Work->Error = GetLastError();
                Work->ErrorText = IDS_ERR_PARTITION;
                break;
            }
            Ok = WorkCreate(Work, Disk);
            CloseHandle(Disk);
            break;

        case DuWorkEraseDisk:
        {
            CREATE_DISK Create;
            DWORD Returned;
            ULONGLONG Start = DU_MEGABYTE, End;
            PDRIVE_LAYOUT_INFORMATION_EX Layout;

            Disk = OpenDisk(Work->DiskNumber);
            if (Disk == INVALID_HANDLE_VALUE)
            {
                Work->Error = GetLastError();
                Work->ErrorText = IDS_ERR_PARTITION;
                break;
            }
            WorkStatus(Work, IDS_STATUS_ERASING);
            DeviceIoControl(Disk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, NULL, 0, NULL, 0, &Returned, NULL);
            ZeroMemory(&Create, sizeof(Create));
            Create.PartitionStyle = Work->Style;
            if (Work->Style == PARTITION_STYLE_GPT)
            {
                CoCreateGuid(&Create.Gpt.DiskId);
                Create.Gpt.MaxPartitionCount = 128;
            }
            else
            {
                Create.Mbr.Signature = GetTickCount() ^ (ULONG)(ULONG_PTR)Disk ^ 0x4C4E5400;
            }
            if (!DeviceIoControl(Disk, IOCTL_DISK_CREATE_DISK, &Create, sizeof(Create), NULL, 0, &Returned, NULL))
            {
                Work->Error = GetLastError();
                Work->ErrorText = IDS_ERR_PARTITION;
                CloseHandle(Disk);
                break;
            }
            DeviceIoControl(Disk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &Returned, NULL);

            End = Work->Length;
            Layout = ReadLayout(Disk);
            if (Layout && Layout->PartitionStyle == PARTITION_STYLE_GPT)
            {
                Start = (Layout->Gpt.StartingUsableOffset.QuadPart + DU_MEGABYTE - 1) & ~(DU_MEGABYTE - 1);
                End = Layout->Gpt.StartingUsableOffset.QuadPart + Layout->Gpt.UsableLength.QuadPart;
            }
            if (Layout)
                HeapFree(GetProcessHeap(), 0, Layout);
            if (Work->Style == PARTITION_STYLE_GPT)
            {
                Work->Error = AddPartition(Disk, Start, DU_MSR_SIZE, &PARTITION_MSFT_RESERVED_GUID, 0,
                                           L"Microsoft reserved partition");
                if (Work->Error != ERROR_SUCCESS)
                {
                    Work->ErrorText = IDS_ERR_PARTITION;
                    CloseHandle(Disk);
                    break;
                }
                Start += DU_MSR_SIZE;
            }
            Work->Offset = Start;
            Work->Length = (End - Start) & ~(DU_MEGABYTE - 1);
            Ok = WorkCreate(Work, Disk);
            CloseHandle(Disk);
            break;
        }

        case DuWorkFormat:
            if (WorkAcquireRoot(Work, Work->DeviceName, 0))
            {
                Ok = WorkFormat(Work);
                WorkReleaseRoot(Work);
            }
            break;

        case DuWorkCheck:
            if (WorkAcquireRoot(Work, Work->DeviceName, 0))
            {
                Work->FormatOk = FALSE;
                DuCurrentWork = Work;
                Chkdsk(Work->Root, Work->FileSystem, FALSE, FALSE, FALSE, FALSE, NULL, NULL, DuFmifsCallback);
                DuCurrentWork = NULL;
                WorkReleaseRoot(Work);
                Ok = Work->FormatOk;
                if (!Ok)
                {
                    if (Work->Error == ERROR_SUCCESS)
                        Work->Error = ERROR_DISK_CORRUPT;
                    Work->ErrorText = IDS_ERR_CHECK;
                }
            }
            break;

        case DuWorkShrink:
        case DuWorkExtend:
        {
            UNICODE_STRING Path;
            NTSTATUS Status;

            RtlInitUnicodeString(&Path, Work->DeviceName);
            WorkStatus(Work, Work->Kind == DuWorkShrink ? IDS_STATUS_SHRINKING : IDS_STATUS_EXTENDING);
            if (Work->Kind == DuWorkShrink)
                Status = VolResizeShrink(&Path, Work->Bytes, DuResizeProgress, Work);
            else
                Status = VolResizeExtend(&Path, Work->Bytes, TRUE);
            Ok = NT_SUCCESS(Status);
            if (!Ok)
            {
                Work->Error = RtlNtStatusToDosError(Status);
                Work->ErrorText = Status == STATUS_MEDIA_WRITE_PROTECTED ? IDS_ERR_READONLY : IDS_ERR_RESIZE;
            }
            break;
        }
    }
    CoUninitialize();
    if (!Work->Cancel)
        PostMessageW(Work->hDlg, WM_DU_WORK_DONE, Ok, 0);
    InterlockedDecrement(&DuWorkActive);
    WorkRelease(Work);
    return 0;
}

static VOID
AppendOutput(
    _In_ HWND hDlg,
    _In_ PCWSTR Text)
{
    HWND Edit = GetDlgItem(hDlg, IDC_WORK_OUTPUT);
    INT Length = GetWindowTextLengthW(Edit);
    WCHAR Line[512];
    SIZE_T i, j = 0;

    for (i = 0; Text[i] && j + 2 < ARRAYSIZE(Line); i++)
    {
        if (Text[i] == L'\n' && (i == 0 || Text[i - 1] != L'\r'))
            Line[j++] = L'\r';
        Line[j++] = Text[i];
    }
    Line[j] = UNICODE_NULL;
    SendMessageW(Edit, EM_SETSEL, Length, Length);
    SendMessageW(Edit, EM_REPLACESEL, FALSE, (LPARAM)Line);
}

static INT_PTR CALLBACK
WorkDlgProc(
    _In_ HWND hDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDU_WORK Work = (PDU_WORK)GetWindowLongPtrW(hDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            WCHAR Text[128];
            HANDLE Thread;

            Work = (PDU_WORK)lParam;
            Work->hDlg = hDlg;
            SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)Work);
            DuLoadString(Work->Title, Text, ARRAYSIZE(Text));
            SetWindowTextW(hDlg, Text);
            SetDlgItemTextW(hDlg, IDC_WORK_TEXT, Text);
            SendDlgItemMessageW(hDlg, IDC_WORK_PROGRESS, PBM_SETMARQUEE, TRUE, 30);
            InterlockedIncrement(&Work->Refs);
            InterlockedIncrement(&DuWorkActive);
            Thread = CreateThread(NULL, 0, WorkThread, Work, 0, NULL);
            if (!Thread)
            {
                Work->Error = GetLastError();
                InterlockedDecrement(&DuWorkActive);
                InterlockedDecrement(&Work->Refs);
                EndDialog(hDlg, FALSE);
                return TRUE;
            }
            CloseHandle(Thread);
            return TRUE;
        }

        case WM_DU_WORK_PROGRESS:
        {
            HWND Progress = GetDlgItem(hDlg, IDC_WORK_PROGRESS);

            SetWindowLongPtrW(Progress, GWL_STYLE, GetWindowLongPtrW(Progress, GWL_STYLE) & ~PBS_MARQUEE);
            SendMessageW(Progress, PBM_SETRANGE32, 0, 100);
            SendMessageW(Progress, PBM_SETPOS, min(wParam, 100), 0);
            return TRUE;
        }

        case WM_DU_WORK_OUTPUT:
            if (wParam)
                SetDlgItemTextW(hDlg, IDC_WORK_STATUS, (PCWSTR)lParam);
            else
                AppendOutput(hDlg, (PCWSTR)lParam);
            HeapFree(GetProcessHeap(), 0, (PVOID)lParam);
            return TRUE;

        case WM_DU_WORK_DONE:
        {
            HWND Progress = GetDlgItem(hDlg, IDC_WORK_PROGRESS);
            WCHAR Text[128];

            SetWindowLongPtrW(Progress, GWL_STYLE, GetWindowLongPtrW(Progress, GWL_STYLE) & ~PBS_MARQUEE);
            SendMessageW(Progress, PBM_SETRANGE32, 0, 100);
            SendMessageW(Progress, PBM_SETPOS, 100, 0);
            DuLoadString(wParam ? IDS_STATUS_DONE : (Work->Kind == DuWorkCheck ? IDS_ERR_CHECK : IDS_STATUS_FAILED),
                         Text, ARRAYSIZE(Text));
            SetDlgItemTextW(hDlg, IDC_WORK_STATUS, Text);
            if (Work->Kind != DuWorkCheck)
            {
                EndDialog(hDlg, wParam);
                return TRUE;
            }
            DuLoadString(IDS_CLOSE, Text, ARRAYSIZE(Text));
            SetDlgItemTextW(hDlg, IDCANCEL, Text);
            SendMessageW(hDlg, DM_SETDEFID, IDCANCEL, 0);
            SetFocus(GetDlgItem(hDlg, IDCANCEL));
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, 1 + wParam);
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                if (GetWindowLongPtrW(hDlg, GWLP_USERDATA))
                {
                    EndDialog(hDlg, GetWindowLongPtrW(hDlg, GWLP_USERDATA) - 1);
                }
                else if (LOWORD(wParam) == IDCANCEL && Work->Kind == DuWorkCheck)
                {
                    InterlockedExchange(&Work->Cancel, TRUE);
                    EndDialog(hDlg, FALSE);
                }
            }
            return TRUE;

        case WM_CLOSE:
            SendMessageW(hDlg, WM_COMMAND, IDCANCEL, 0);
            return TRUE;

        case WM_DESTROY:
        {
            MSG Msg;

            while (PeekMessageW(&Msg, hDlg, WM_DU_WORK_OUTPUT, WM_DU_WORK_OUTPUT, PM_REMOVE))
                HeapFree(GetProcessHeap(), 0, (PVOID)Msg.lParam);
            return FALSE;
        }
    }
    return FALSE;
}

static BOOL
RunWork(
    _In_ HWND hwnd,
    _In_ const DU_WORK *Request)
{
    PDU_WORK Work;
    BOOL Ok;

    if (DuWorkActive)
    {
        DuError(hwnd, IDS_ERR_BUSY, ERROR_BUSY);
        return FALSE;
    }
    Work = HeapAlloc(GetProcessHeap(), 0, sizeof(*Work));
    if (!Work)
    {
        DuError(hwnd, IDS_STATUS_FAILED, ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *Work = *Request;
    Work->Refs = 1;
    Work->Cancel = FALSE;
    Ok = (BOOL)DialogBoxParamW(g_hInstance,
                               MAKEINTRESOURCEW(Work->Kind == DuWorkCheck ? IDD_CHECK : IDD_WORK),
                               hwnd, WorkDlgProc, (LPARAM)Work);
    if (!Ok && Work->Kind != DuWorkCheck && Work->ErrorText)
        DuError(hwnd, Work->ErrorText, Work->Error);
    WorkRelease(Work);
    return Ok;
}

static VOID
FillFileSystems(
    _In_ HWND Combo,
    _In_opt_ PCWSTR Select)
{
    WCHAR Name[MAX_PATH];
    UCHAR Major, Minor;
    BOOLEAN Latest;
    DWORD Index;
    INT Item, Chosen = 0;

    SendMessageW(Combo, CB_RESETCONTENT, 0, 0);
    for (Index = 0; QueryAvailableFileSystemFormat(Index, Name, &Major, &Minor, &Latest); Index++)
    {
        if (!lstrcmpiW(Name, L"FAT") || !lstrcmpiW(Name, L"BtrFS") || !lstrcmpiW(Name, L"EXT2"))
            continue;
        Item = (INT)SendMessageW(Combo, CB_ADDSTRING, 0, (LPARAM)Name);
        if (Item >= 0 && ((Select && !lstrcmpiW(Name, Select)) || (!Select && !lstrcmpiW(Name, L"NTFS"))))
            Chosen = Item;
    }
    SendMessageW(Combo, CB_SETCURSEL, Chosen, 0);
}

static VOID
FillLetters(
    _In_ HWND Combo)
{
    DWORD Used = GetLogicalDrives();
    WCHAR Text[64];
    WCHAR Letter;
    INT Item;

    SendMessageW(Combo, CB_RESETCONTENT, 0, 0);
    DuLoadString(IDS_LETTER_AUTO, Text, ARRAYSIZE(Text));
    Item = (INT)SendMessageW(Combo, CB_ADDSTRING, 0, (LPARAM)Text);
    SendMessageW(Combo, CB_SETITEMDATA, Item, DU_LETTER_AUTO);
    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        if (Used & (1u << (Letter - L'A')))
            continue;
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%c:", Letter);
        Item = (INT)SendMessageW(Combo, CB_ADDSTRING, 0, (LPARAM)Text);
        SendMessageW(Combo, CB_SETITEMDATA, Item, Letter);
    }
    DuLoadString(IDS_LETTER_NONE, Text, ARRAYSIZE(Text));
    Item = (INT)SendMessageW(Combo, CB_ADDSTRING, 0, (LPARAM)Text);
    SendMessageW(Combo, CB_SETITEMDATA, Item, 0);
    SendMessageW(Combo, CB_SETCURSEL, 0, 0);
}

typedef struct _DU_SIZE_DIALOG
{
    PDU_DISK Disk;
    INT EditPart;
    ULONGLONG Current;
    ULONGLONG Min;
    ULONGLONG Max;
    ULONGLONG Result;
    BOOL Creating;
    BOOL Syncing;
    PDU_WORK Work;
} DU_SIZE_DIALOG, *PDU_SIZE_DIALOG;

static VOID
SizeDialogShow(
    _In_ HWND hDlg,
    _In_ PDU_SIZE_DIALOG Dialog,
    _In_ ULONGLONG Bytes,
    _In_ BOOL UpdateEdit)
{
    WCHAR Size[32], Text[128];

    Dialog->Result = Bytes;
    if (UpdateEdit)
    {
        Dialog->Syncing = TRUE;
        SetDlgItemInt(hDlg, IDC_SZ_SIZE, (UINT)(Bytes / DU_MEGABYTE), FALSE);
        Dialog->Syncing = FALSE;
    }
    DuFormatSize(Bytes, Size, ARRAYSIZE(Size));
    if (!Dialog->Creating && Bytes != Dialog->Current)
    {
        WCHAR Delta[32];

        DuFormatSize(Bytes > Dialog->Current ? Bytes - Dialog->Current : Dialog->Current - Bytes, Delta, ARRAYSIZE(Delta));
        DuPrintf(Text, ARRAYSIZE(Text), Bytes > Dialog->Current ? IDS_RESIZE_GROW : IDS_RESIZE_SHRINK, Size, Delta);
    }
    else
    {
        StringCchCopyW(Text, ARRAYSIZE(Text), Size);
    }
    SetDlgItemTextW(hDlg, IDC_SZ_SUMMARY, Text);
    EnableWindow(GetDlgItem(hDlg, IDOK), Dialog->Creating || Bytes != Dialog->Current);
}

static INT_PTR CALLBACK
SizeDlgProc(
    _In_ HWND hDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDU_SIZE_DIALOG Dialog = (PDU_SIZE_DIALOG)GetWindowLongPtrW(hDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            WCHAR Text[256], DiskName[128], MinText[32], MaxText[32];

            Dialog = (PDU_SIZE_DIALOG)lParam;
            SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)Dialog);
            DuDiskTitle(Dialog->Disk, DiskName, ARRAYSIZE(DiskName));
            if (Dialog->Creating)
            {
                DuPrintf(Text, ARRAYSIZE(Text), IDS_NEW_TITLE, DiskName);
                SetDlgItemTextW(hDlg, IDC_SZ_TITLE, Text);
                FillFileSystems(GetDlgItem(hDlg, IDC_SZ_FS), NULL);
                FillLetters(GetDlgItem(hDlg, IDC_SZ_LETTER));
                DuLoadString(IDS_NEW_VOLUME, Text, ARRAYSIZE(Text));
                SetDlgItemTextW(hDlg, IDC_SZ_NAME, Text);
                SendDlgItemMessageW(hDlg, IDC_SZ_NAME, EM_LIMITTEXT, 32, 0);
            }
            else
            {
                DuPartTitle(&g_Model, &Dialog->Disk->Parts[Dialog->EditPart], DiskName, ARRAYSIZE(DiskName));
                DuPrintf(Text, ARRAYSIZE(Text), IDS_RESIZE_TITLE, DiskName);
                SetDlgItemTextW(hDlg, IDC_SZ_TITLE, Text);
            }
            DuFormatSize(Dialog->Min, MinText, ARRAYSIZE(MinText));
            DuFormatSize(Dialog->Max, MaxText, ARRAYSIZE(MaxText));
            DuPrintf(Text, ARRAYSIZE(Text), IDS_SIZE_RANGE, MinText, MaxText);
            SetDlgItemTextW(hDlg, IDC_SZ_RANGE, Text);
            SendDlgItemMessageW(hDlg, IDC_SZ_SPIN, UDM_SETRANGE32,
                                (WPARAM)(Dialog->Min / DU_MEGABYTE), (LPARAM)(Dialog->Max / DU_MEGABYTE));
            SendDlgItemMessageW(hDlg, IDC_SZ_SLIDER, TBM_SETRANGEMIN, FALSE, (LPARAM)(Dialog->Min / DU_MEGABYTE));
            SendDlgItemMessageW(hDlg, IDC_SZ_SLIDER, TBM_SETRANGEMAX, FALSE, (LPARAM)(Dialog->Max / DU_MEGABYTE));
            SendDlgItemMessageW(hDlg, IDC_SZ_SLIDER, TBM_SETPAGESIZE, 0,
                                (LPARAM)max(1, (Dialog->Max - Dialog->Min) / DU_MEGABYTE / 20));
            SendDlgItemMessageW(hDlg, IDC_SZ_SLIDER, TBM_SETPOS, TRUE, (LPARAM)(Dialog->Current / DU_MEGABYTE));
            SizeDialogShow(hDlg, Dialog, Dialog->Current, TRUE);
            SetFocus(GetDlgItem(hDlg, IDC_SZ_SLIDER));
            return FALSE;
        }

        case WM_HSCROLL:
            if ((HWND)lParam == GetDlgItem(hDlg, IDC_SZ_SLIDER))
            {
                LONG Mb = (LONG)SendMessageW((HWND)lParam, TBM_GETPOS, 0, 0);

                SizeDialogShow(hDlg, Dialog, (ULONGLONG)Mb * DU_MEGABYTE, TRUE);
                return TRUE;
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_SZ_SIZE:
                    if (HIWORD(wParam) == EN_CHANGE && !Dialog->Syncing)
                    {
                        BOOL Valid;
                        UINT Mb = GetDlgItemInt(hDlg, IDC_SZ_SIZE, &Valid, FALSE);
                        ULONGLONG Bytes = (ULONGLONG)Mb * DU_MEGABYTE;

                        if (Valid && Bytes >= Dialog->Min && Bytes <= Dialog->Max)
                        {
                            SendDlgItemMessageW(hDlg, IDC_SZ_SLIDER, TBM_SETPOS, TRUE, (LPARAM)Mb);
                            SizeDialogShow(hDlg, Dialog, Bytes, FALSE);
                        }
                    }
                    return TRUE;

                case IDOK:
                    if (Dialog->Creating && Dialog->Work)
                    {
                        HWND Letter = GetDlgItem(hDlg, IDC_SZ_LETTER);
                        INT Item = (INT)SendMessageW(Letter, CB_GETCURSEL, 0, 0);

                        GetDlgItemTextW(hDlg, IDC_SZ_NAME, Dialog->Work->Name, ARRAYSIZE(Dialog->Work->Name));
                        GetDlgItemTextW(hDlg, IDC_SZ_FS, Dialog->Work->FileSystem, ARRAYSIZE(Dialog->Work->FileSystem));
                        Dialog->Work->Letter = (WCHAR)SendMessageW(Letter, CB_GETITEMDATA, max(Item, 0), 0);
                    }
                    EndDialog(hDlg, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

BOOL
DuNewVolume(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    DU_SIZE_DIALOG Dialog;
    DU_WORK Work;
    PDU_PART Free;
    INT Index;

    if (!DuCanNewVolume(Sel))
        return FALSE;
    Index = Sel->Type == DuSelPart ? (INT)Sel->Part : LargestFree(Disk);
    Free = &Disk->Parts[Index];

    ZeroMemory(&Work, sizeof(Work));
    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.Disk = Disk;
    Dialog.EditPart = Index;
    Dialog.Creating = TRUE;
    Dialog.Min = DU_MIN_VOLUME;
    Dialog.Max = Free->Length;
    Dialog.Current = Free->Length;
    Dialog.Work = &Work;
    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_NEWVOLUME), hwnd, SizeDlgProc, (LPARAM)&Dialog) != IDOK)
        return FALSE;

    Work.Kind = DuWorkCreate;
    Work.Title = IDS_WORK_CREATE;
    Work.DiskNumber = Disk->Number;
    Work.Offset = Free->Offset;
    Work.Length = Dialog.Result & ~(DU_MEGABYTE - 1);
    Work.Quick = TRUE;
    RunWork(hwnd, &Work);
    return TRUE;
}

BOOL
DuResize(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    PDU_PART Part = SelPart(Sel);
    PDU_VOLUME Volume = SelVolume(Sel);
    DU_SIZE_DIALOG Dialog;
    VOLRESIZE_INFO Info;
    UNICODE_STRING Path;
    DU_WORK Work;
    NTSTATUS Status;
    ULONGLONG Following = 0;
    HCURSOR Old;

    if (!DuCanResize(Sel))
        return FALSE;
    if (Volume->FsFlags & FILE_READ_ONLY_VOLUME)
    {
        DuError(hwnd, IDS_ERR_READONLY, ERROR_WRITE_PROTECT);
        return FALSE;
    }
    RtlInitUnicodeString(&Path, Volume->DeviceName);
    Old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    Status = VolResizeQuery(&Path, &Info);
    SetCursor(Old);
    if (!NT_SUCCESS(Status))
    {
        DuError(hwnd, IDS_ERR_RESIZE, RtlNtStatusToDosError(Status));
        return FALSE;
    }
    if (Sel->Part + 1 < Disk->PartCount && Disk->Parts[Sel->Part + 1].Kind == DuPartFree &&
        Disk->Parts[Sel->Part + 1].Offset <= Part->Offset + Part->Length + DU_MEGABYTE)
    {
        Following = Disk->Parts[Sel->Part + 1].Offset + Disk->Parts[Sel->Part + 1].Length - (Part->Offset + Part->Length);
    }

    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.Disk = Disk;
    Dialog.EditPart = (INT)Sel->Part;
    Dialog.Current = Part->Length;
    Dialog.Min = (Part->Length - min(Part->Length - DU_MIN_VOLUME, Info.MaxReclaimableBytes) + DU_MEGABYTE - 1) & ~(DU_MEGABYTE - 1);
    Dialog.Max = Part->Length + (Following & ~(DU_MEGABYTE - 1));
    if (Dialog.Min >= Dialog.Max)
    {
        DuError(hwnd, IDS_ERR_NOROOM, ERROR_DISK_FULL);
        return FALSE;
    }
    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_RESIZE), hwnd, SizeDlgProc, (LPARAM)&Dialog) != IDOK ||
        Dialog.Result == Part->Length)
    {
        return FALSE;
    }

    ZeroMemory(&Work, sizeof(Work));
    StringCchCopyW(Work.DeviceName, ARRAYSIZE(Work.DeviceName), Volume->DeviceName);
    if (Dialog.Result < Part->Length)
    {
        Work.Kind = DuWorkShrink;
        Work.Title = IDS_WORK_SHRINK;
        Work.Bytes = Part->Length - Dialog.Result;
    }
    else
    {
        Work.Kind = DuWorkExtend;
        Work.Title = IDS_WORK_EXTEND;
        Work.Bytes = Dialog.Result - Part->Length;
    }
    RunWork(hwnd, &Work);
    return TRUE;
}

BOOL
DuDelete(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    PDU_PART Part = SelPart(Sel);
    PDU_VOLUME Volume = SelVolume(Sel);
    WCHAR Title[MAX_PATH], MainText[MAX_PATH + 64], Content[256];
    HANDLE Locked = NULL, Handle;
    DWORD Error;

    if (!DuCanDelete(Sel))
        return FALSE;
    DuPartTitle(&g_Model, Part, Title, ARRAYSIZE(Title));
    DuPrintf(MainText, ARRAYSIZE(MainText), IDS_DELETE_MAIN, Title);
    DuLoadString(IDS_DELETE_CONTENT, Content, ARRAYSIZE(Content));
    if (DuMessage(hwnd, TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, MainText, Content) != IDYES)
        return FALSE;

    if (Volume)
    {
        Locked = LockVolume(Volume, &Error);
        if (!Locked)
        {
            DuError(hwnd, IDS_ERR_INUSE, Error);
            return FALSE;
        }
        RemoveLetters(Volume);
    }

    Handle = OpenDisk(Disk->Number);
    if (Handle == INVALID_HANDLE_VALUE)
        Error = GetLastError();
    else
    {
        Error = RemovePartition(Handle, Part->Offset);
        CloseHandle(Handle);
    }
    if (Locked)
        CloseHandle(Locked);
    if (Error != ERROR_SUCCESS)
        DuError(hwnd, IDS_ERR_PARTITION, Error);
    return TRUE;
}

typedef struct _DU_ERASE_DIALOG
{
    BOOL Disk;
    PCWSTR Heading;
    PDU_WORK Work;
    PCWSTR FileSystem;
    PARTITION_STYLE Style;
} DU_ERASE_DIALOG, *PDU_ERASE_DIALOG;

static INT_PTR CALLBACK
EraseDlgProc(
    _In_ HWND hDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PDU_ERASE_DIALOG Dialog = (PDU_ERASE_DIALOG)GetWindowLongPtrW(hDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            HWND Scheme = GetDlgItem(hDlg, IDC_ER_SCHEME);
            WCHAR Text[128];

            Dialog = (PDU_ERASE_DIALOG)lParam;
            SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)Dialog);
            SetDlgItemTextW(hDlg, IDC_ER_TITLE, Dialog->Heading);
            SendDlgItemMessageW(hDlg, IDC_ER_NAME, EM_LIMITTEXT, 32, 0);
            SetDlgItemTextW(hDlg, IDC_ER_NAME, Dialog->Work->Name);
            FillFileSystems(GetDlgItem(hDlg, IDC_ER_FS), Dialog->FileSystem);
            CheckDlgButton(hDlg, IDC_ER_QUICK, BST_CHECKED);
            DuLoadString(IDS_SCHEME_GPT, Text, ARRAYSIZE(Text));
            SendMessageW(Scheme, CB_ADDSTRING, 0, (LPARAM)Text);
            DuLoadString(IDS_SCHEME_MBR, Text, ARRAYSIZE(Text));
            SendMessageW(Scheme, CB_ADDSTRING, 0, (LPARAM)Text);
            SendMessageW(Scheme, CB_SETCURSEL, Dialog->Style == PARTITION_STYLE_MBR ? 1 : 0, 0);
            if (!Dialog->Disk)
            {
                ShowWindow(Scheme, SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_ER_SCHEME_LABEL), SW_HIDE);
            }
            else
            {
                ShowWindow(GetDlgItem(hDlg, IDC_ER_QUICK), SW_HIDE);
            }
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                GetDlgItemTextW(hDlg, IDC_ER_NAME, Dialog->Work->Name, ARRAYSIZE(Dialog->Work->Name));
                GetDlgItemTextW(hDlg, IDC_ER_FS, Dialog->Work->FileSystem, ARRAYSIZE(Dialog->Work->FileSystem));
                Dialog->Work->Quick = Dialog->Disk || IsDlgButtonChecked(hDlg, IDC_ER_QUICK) == BST_CHECKED;
                Dialog->Work->Style = SendDlgItemMessageW(hDlg, IDC_ER_SCHEME, CB_GETCURSEL, 0, 0) == 1 ?
                                      PARTITION_STYLE_MBR : PARTITION_STYLE_GPT;
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

BOOL
DuErase(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_DISK Disk = SelDisk(Sel);
    PDU_VOLUME Volume = SelVolume(Sel);
    DU_ERASE_DIALOG Dialog;
    WCHAR Title[MAX_PATH], Heading[MAX_PATH + 64], MainText[MAX_PATH + 64], Content[256];
    DU_WORK Work;

    if (!DuCanErase(Sel))
        return FALSE;
    ZeroMemory(&Work, sizeof(Work));
    ZeroMemory(&Dialog, sizeof(Dialog));
    Dialog.Work = &Work;
    Dialog.Disk = Sel->Type == DuSelDisk;
    Dialog.Style = Disk->Style == PARTITION_STYLE_MBR ? PARTITION_STYLE_MBR : PARTITION_STYLE_GPT;
    if (Dialog.Disk)
        DuDiskTitle(Disk, Title, ARRAYSIZE(Title));
    else
        DuPartTitle(&g_Model, SelPart(Sel), Title, ARRAYSIZE(Title));
    DuPrintf(Heading, ARRAYSIZE(Heading), IDS_ERASE_TITLE, Title);
    Dialog.Heading = Heading;
    Dialog.FileSystem = (Volume && Volume->HasFileSystem) ? Volume->FileSystem : NULL;
    if (Volume && Volume->Label[0])
        StringCchCopyW(Work.Name, ARRAYSIZE(Work.Name), Volume->Label);
    else
        DuLoadString(IDS_UNTITLED, Work.Name, ARRAYSIZE(Work.Name));
    if (DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_ERASE), hwnd, EraseDlgProc, (LPARAM)&Dialog) != IDOK)
        return FALSE;
    if (!Work.Name[0])
        DuLoadString(IDS_UNTITLED, Work.Name, ARRAYSIZE(Work.Name));

    DuPrintf(MainText, ARRAYSIZE(MainText), IDS_ERASE_MAIN, Title);
    DuLoadString(Dialog.Disk ? IDS_ERASE_DISK_CONTENT : IDS_ERASE_CONTENT, Content, ARRAYSIZE(Content));
    if (DuMessage(hwnd, TD_WARNING_ICON, TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, MainText, Content) != IDYES)
        return FALSE;

    if (Dialog.Disk)
    {
        HANDLE Locked[DU_MAX_PARTS];
        ULONG p, Count = 0;
        DWORD Error;

        for (p = 0; p < Disk->PartCount; p++)
        {
            PDU_VOLUME PartVolume = DuPartVolume(&g_Model, &Disk->Parts[p]);

            if (!PartVolume)
                continue;
            Locked[Count] = LockVolume(PartVolume, &Error);
            if (!Locked[Count])
            {
                while (Count)
                    CloseHandle(Locked[--Count]);
                DuError(hwnd, IDS_ERR_INUSE, Error);
                return FALSE;
            }
            RemoveLetters(PartVolume);
            Count++;
        }
        Work.Kind = DuWorkEraseDisk;
        Work.Title = IDS_WORK_ERASE;
        Work.DiskNumber = Disk->Number;
        Work.Length = Disk->Size;
        Work.Letter = DU_LETTER_AUTO;
        RunWork(hwnd, &Work);
        while (Count)
            CloseHandle(Locked[--Count]);
        return TRUE;
    }

    Work.Kind = DuWorkFormat;
    Work.Title = IDS_WORK_ERASE;
    StringCchCopyW(Work.DeviceName, ARRAYSIZE(Work.DeviceName), Volume->DeviceName);
    RunWork(hwnd, &Work);
    return TRUE;
}

BOOL
DuCheck(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);
    DU_WORK Work;

    if (!DuCanCheck(Sel))
        return FALSE;
    ZeroMemory(&Work, sizeof(Work));
    Work.Kind = DuWorkCheck;
    Work.Title = IDS_WORK_CHECK;
    StringCchCopyW(Work.DeviceName, ARRAYSIZE(Work.DeviceName), Volume->DeviceName);
    StringCchCopyW(Work.FileSystem, ARRAYSIZE(Work.FileSystem), Volume->FileSystem);
    RunWork(hwnd, &Work);
    return FALSE;
}

BOOL
DuMount(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);
    WCHAR Letter;

    if (!DuCanMount(Sel))
        return FALSE;
    if (VolumeLetter(Volume, &Letter))
    {
        if (!StorageUtilDeleteDriveLetter(Letter))
        {
            DuError(hwnd, IDS_ERR_LETTER, GetLastError());
            return FALSE;
        }
        return TRUE;
    }
    if (!AssignLetter(Volume->DeviceName, &Letter))
    {
        DuError(hwnd, IDS_ERR_LETTER, GetLastError());
        return FALSE;
    }
    return TRUE;
}

VOID
DuInfo(
    _In_ HWND hwnd,
    _In_ const DU_SELECTION *Sel)
{
    PDU_VOLUME Volume = SelVolume(Sel);

    if (DuCanInfo(Sel))
        SHObjectProperties(hwnd, SHOP_FILEPATH, Volume->MountPath, NULL);
}
