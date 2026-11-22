/*
 * PROJECT:     LiberNT NTFS tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Fail each disk write during promotion and reopen both hard links
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <ntfs_linux.h>

static const ULONG Offset = 4096;
static std::vector<UCHAR> Payload(128 * 1024 + 1536, 0x65);
static ULONG FailAt, Writes;
static bool Inject;
static PNtfsVolume Volume;
static WCHAR First[] = L"\\first", Second[] = L"\\dir\\second";

static void Check(bool Condition, const char *Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "write %u of %u: %s\n", FailAt, Writes, Message);
        std::exit(1);
    }
}

extern "C" NTFS_WRITE_VOLUME_ROUTINE __real_NtfsWriteVolumeContext;
extern "C" NTSTATUS __wrap_NtfsWriteVolumeContext(void *Context, ULONG Sector, ULONGLONG Position, ULONG Length, PUCHAR Data)
{
    if (Inject && ++Writes == FailAt)
        return STATUS_INVALID_DEVICE_STATE;
    return __real_NtfsWriteVolumeContext(Context, Sector, Position, Length, Data);
}

static PNtfsFileRecord Lookup(PWSTR Path)
{
    PNtfsFileRecord Record = nullptr;
    Check(NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(Volume), Path, &Record)), "lookup");
    return Record;
}

static void Mount(const char *Image)
{
    ULONG Sector;
    Check(NT_SUCCESS(NtfsDiskInitializeLinuxWritable(Image, &Sector)), "open backing device");
    NtfsSetReadOnlyMode(FALSE);
    Check(NT_SUCCESS(NtfsProbePartitionAndOpenVolume(Sector, &Volume)), "mount volume");
}

static void Unmount()
{
    NtfsVolumeDestroy(Volume);
    NtfsDiskCloseLinux();
}

static LONGLONG FreeClusters()
{
    LARGE_INTEGER Free;
    Check(NT_SUCCESS(NtfsVolumeGetFreeClusters(Volume, &Free)), "count free clusters");
    return Free.QuadPart;
}

static NTSTATUS Transfer(void *, struct DataRun *Runs, PUCHAR Prefix, ULONG Length)
{
    PNtfsFileRecord Visible = Lookup(Second);
    PAttribute Attribute = NtfsFileRecordGetAttribute(Visible, TypeData, nullptr);
    Check(Attribute && !Attribute->IsNonResident && Attribute->Resident.DataLength == 4, "early publication");
    NtfsFileRecordDestroy(Visible);
    Check(Length == 4 && !std::memcmp(Prefix, "seed", 4), "resident prefix");
    return NtfsWriteReservedRuns(Volume, Runs, Prefix, Length, Offset, Payload.data(), Payload.size(), __wrap_NtfsWriteVolumeContext);
}

static void CheckLink(PWSTR Path, const std::vector<UCHAR>& Expected)
{
    PNtfsFileRecord File = Lookup(Path);
    PAttribute Attribute = NtfsFileRecordGetAttribute(File, TypeData, nullptr);
    Check(Attribute && NtfsFileRecordGetHeader(File)->HardLinkCount == 2, "link count or attribute");
    if (Expected.size() == 4)
        Check(!Attribute->IsNonResident && Attribute->Resident.DataLength == 4, "resident EOF after rollback");
    else
        Check(Attribute->IsNonResident && Attribute->NonResident.DataSize == Expected.size() &&
              Attribute->NonResident.InitalizedDataSize == Expected.size(), "published sizes");
    std::vector<UCHAR> Actual(Expected.size());
    ULONG Length = Actual.size();
    Check(NT_SUCCESS(NtfsFileRecordCopyData(File, TypeData, nullptr, Actual.data(), &Length, 0)) && Actual == Expected, "stream contents");
    NtfsFileRecordDestroy(File);
}

static void CheckIndex(PWSTR Path, PCWSTR Name, ULONG Characters, ULONG Size)
{
    PNtfsFileRecord Parent = Lookup(Path);
    PNtfsDirectory Directory = NtfsDirectoryCreate(Volume);
    Check(Directory && NT_SUCCESS(NtfsDirectoryLoadDirectory(Directory, Parent)), "load parent");
    NtfsDirectoryEntry Entry;
    BOOLEAN Restart = TRUE;
    for (;;)
    {
        Check(NT_SUCCESS(NtfsDirectoryReadNext(Directory, Restart, &Entry)), "find hardlink in parent");
        if (Entry.NameLength == Characters && !std::memcmp(Entry.Name, Name, Characters * sizeof(WCHAR)))
            break;
        Restart = FALSE;
    }
    Check(Entry.EndOfFile == Size && Entry.AllocationSize == (Size == 4 ? 8 : ((Size + 4095) & ~4095u)), "parent index sizes");
    NtfsDirectoryDestroy(Directory);
    NtfsFileRecordDestroy(Parent);
}

int main(int argc, char **argv)
{
    Check(argc == 2, "expected a disposable NTFS image");
    WCHAR Root[] = L"\\", Dir[] = L"\\dir";
    for (ULONG Failure = 1; ; ++Failure)
    {
        Mount(argv[1]);
        LONGLONG Before = FreeClusters();
        PNtfsFileRecord File = Lookup(First);
        BOOLEAN Handled = FALSE;
        Writes = 0;
        FailAt = Failure;
        Inject = true;
        NTSTATUS Status = NtfsFileRecordWriteResident(File, Payload.data(), Payload.size(), Offset, Transfer, nullptr, &Handled);
        bool Injected = Writes >= Failure;
        Check(Handled && (Injected ? !NT_SUCCESS(Status) : NT_SUCCESS(Status)), "promotion status");
        Inject = false;
        NtfsFileRecordDestroy(File);
        Unmount();
        Mount(argv[1]);
        std::vector<UCHAR> Expected{'s','e','e','d'};
        if (!Injected)
        {
            Expected.resize(Offset);
            Expected.insert(Expected.end(), Payload.begin(), Payload.end());
        }
        CheckLink(First, Expected);
        CheckLink(Second, Expected);
        CheckIndex(Root, L"first", 5, Expected.size());
        CheckIndex(Dir, L"second", 6, Expected.size());
        LONGLONG Consumed = (static_cast<LONGLONG>(Expected.size()) + 4095) / 4096;
        Check(FreeClusters() == Before - (Injected ? 0 : Consumed), "allocation bitmap");
        Unmount();
        if (!Injected)
        {
            std::printf("Verified %u single-write failures and a successful retry.\n", Failure - 1);
            return 0;
        }
    }
}
