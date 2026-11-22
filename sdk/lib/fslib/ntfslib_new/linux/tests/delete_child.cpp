/*
 * PROJECT:     LiberNT NTFS tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Delete names through a child record the caller already looked up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <ntfs_linux.h>

static PNtfsVolume Volume;

static void
Check(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "delete_child: %s\n", Message);
        std::exit(1);
    }
}

static std::vector<WCHAR>
Wide(const std::string& Text)
{
    return std::vector<WCHAR>(Text.begin(), Text.end());
}

static PNtfsFileRecord
TryLookup(const std::string& Path)
{
    std::vector<WCHAR> Query = Wide(Path);
    PNtfsFileRecord Record = nullptr;

    Query.push_back(0);
    if (!NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(Volume), Query.data(), &Record)))
        return nullptr;
    return Record;
}

static PNtfsFileRecord
Lookup(const std::string& Path)
{
    PNtfsFileRecord Record = TryLookup(Path);

    Check(Record != nullptr, "lookup");
    return Record;
}

static void
Create(const std::string& Directory, const std::string& Name, bool IsDirectory)
{
    PNtfsFileRecord Parent = Lookup(Directory);
    std::vector<WCHAR> Leaf = Wide(Name);
    PNtfsFileRecord File = nullptr;

    Check(NT_SUCCESS(NtfsMasterFileTableCreateFileWithEas(NtfsVolumeGetMft(Volume), Parent, Leaf.data(),
                                                          (ULONG)Leaf.size(), IsDirectory, 0, TRUE, nullptr, 0,
                                                          &File)),
          "create");
    NtfsFileRecordDestroy(File);
    NtfsFileRecordDestroy(Parent);
}

static NTSTATUS
Delete(const std::string& Directory, const std::string& ChildPath, const std::string& Name, bool IsDirectory,
       BOOLEAN* Deleted)
{
    PNtfsFileRecord Parent = Lookup(Directory);
    PNtfsFileRecord Child = Lookup(ChildPath);
    std::vector<WCHAR> Leaf = Wide(Name);
    NTSTATUS Status;

    Status = NtfsMasterFileTableDeleteChild(NtfsVolumeGetMft(Volume), Parent, Child, Leaf.data(), (ULONG)Leaf.size(),
                                            IsDirectory, nullptr, TRUE, FALSE, Deleted);
    NtfsFileRecordDestroy(Child);
    NtfsFileRecordDestroy(Parent);
    return Status;
}

int
main(int argc, char** argv)
{
    ULONG Sector;
    BOOLEAN Deleted = TRUE;

    Check(argc == 2, "expected a disposable NTFS image");
    Check(NT_SUCCESS(NtfsDiskInitializeLinuxWritable(argv[1], &Sector)), "open backing device");
    NtfsSetReadOnlyMode(FALSE);
    Check(NT_SUCCESS(NtfsProbePartitionAndOpenVolume(Sector, &Volume)), "mount volume");

    Create("\\", "dir", true);
    Create("\\", "kept", false);
    Create("\\dir", "inner", false);
    Create("\\dir", "gone", false);

    Check(!NT_SUCCESS(Delete("\\", "\\dir\\gone", "gone", false, &Deleted)) && !Deleted,
          "a child from another directory is refused");
    Check(!NT_SUCCESS(Delete("\\dir", "\\kept", "gone", false, &Deleted)) && !Deleted,
          "a child that does not carry the name is refused");
    Check(Delete("\\", "\\$MFT", "$MFT", false, &Deleted) == STATUS_ACCESS_DENIED && !Deleted,
          "system files are refused");
    Check(!NT_SUCCESS(Delete("\\", "\\dir", "dir", true, &Deleted)) && !Deleted,
          "a directory with names is not removed");
    Check(NT_SUCCESS(Delete("\\dir", "\\dir\\gone", "gone", false, &Deleted)) && Deleted,
          "delete through the looked-up child");
    Check(TryLookup("\\dir\\gone") == nullptr, "the name is gone");
    Check(NT_SUCCESS(Delete("\\dir", "\\dir\\inner", "inner", false, &Deleted)) && Deleted, "delete the last name");
    Check(NT_SUCCESS(Delete("\\", "\\dir", "dir", true, &Deleted)) && Deleted, "remove the emptied directory");
    Check(TryLookup("\\dir") == nullptr, "the directory is gone");
    NtfsFileRecordDestroy(Lookup("\\kept"));

    NtfsVolumeDestroy(Volume);
    NtfsDiskCloseLinux();
    std::printf("Deleted through looked-up children and refused mismatched ones.\n");
    return 0;
}
