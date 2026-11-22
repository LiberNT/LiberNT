/*
 * PROJECT:     LiberNT NTFS tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Create files whose EAs and index entries are complete from the start
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <ntfs_linux.h>

static PNtfsVolume Volume;
static ULONG FailAt, Writes;
static bool Inject;

extern "C" NTSTATUS __real_NtfsWriteVolumeContext(void*, ULONG, ULONGLONG, ULONG, PUCHAR);
extern "C" NTSTATUS
__wrap_NtfsWriteVolumeContext(void* Context, ULONG Sector, ULONGLONG Position, ULONG Length, PUCHAR Data)
{
    if (Inject && ++Writes == FailAt)
        return STATUS_INVALID_DEVICE_STATE;
    return __real_NtfsWriteVolumeContext(Context, Sector, Position, Length, Data);
}

static void
Check(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "create_eas: %s\n", Message);
        std::exit(1);
    }
}

static void
Mount(const char* Image)
{
    ULONG Sector;

    Check(NT_SUCCESS(NtfsDiskInitializeLinuxWritable(Image, &Sector)), "open backing device");
    NtfsSetReadOnlyMode(FALSE);
    Check(NT_SUCCESS(NtfsProbePartitionAndOpenVolume(Sector, &Volume)), "mount volume");
}

static void
Unmount()
{
    NtfsVolumeDestroy(Volume);
    NtfsDiskCloseLinux();
}

static std::vector<WCHAR>
Wide(const std::string& Text)
{
    return std::vector<WCHAR>(Text.begin(), Text.end());
}

static bool
Exists(const std::string& Path)
{
    std::vector<WCHAR> Query = Wide(Path);
    PNtfsFileRecord Record = nullptr;

    Query.push_back(0);
    if (!NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(Volume), Query.data(), &Record)))
        return false;
    NtfsFileRecordDestroy(Record);
    return true;
}

static PNtfsFileRecord
Lookup(const std::string& Path)
{
    std::vector<WCHAR> Query = Wide(Path);
    PNtfsFileRecord Record = nullptr;

    Query.push_back(0);
    Check(NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQuery(NtfsVolumeGetMft(Volume), Query.data(), &Record)),
          "lookup");
    return Record;
}

static NTSTATUS
Create(const std::string& Name, const NtfsExtendedAttributeUpdate* Eas, ULONG Count)
{
    PNtfsFileRecord Root = Lookup("\\");
    std::vector<WCHAR> Leaf = Wide(Name);
    PNtfsFileRecord File = nullptr;
    NTSTATUS Status;

    Status = NtfsMasterFileTableCreateFileWithEas(NtfsVolumeGetMft(Volume), Root, Leaf.data(), (ULONG)Leaf.size(),
                                                  FALSE, 0, TRUE, Eas, Count, &File);
    Check(NT_SUCCESS(Status) == (File != nullptr), "file returned only on success");
    NtfsFileRecordDestroy(File);
    NtfsFileRecordDestroy(Root);
    return Status;
}

static USHORT
IndexEaSize(const std::string& Name)
{
    std::vector<WCHAR> Wanted = Wide(Name);
    PNtfsFileRecord Root = Lookup("\\");
    PNtfsDirectory Directory = NtfsDirectoryCreate(Volume);
    NtfsDirectoryEntry Entry;
    BOOLEAN Restart = TRUE;

    Check(Directory && NT_SUCCESS(NtfsDirectoryLoadDirectory(Directory, Root)), "load root");
    for (;;)
    {
        Check(NT_SUCCESS(NtfsDirectoryReadNext(Directory, Restart, &Entry)), "find the name in the root");
        if (Entry.NameLength == Wanted.size() && std::equal(Wanted.begin(), Wanted.end(), Entry.Name))
            break;
        Restart = FALSE;
    }
    NtfsDirectoryDestroy(Directory);
    NtfsFileRecordDestroy(Root);
    return Entry.EaSize;
}

static void
CheckEas(const std::string& Name, const char* EaName, const std::vector<UCHAR>& Value)
{
    PNtfsFileRecord File = Lookup("\\" + Name);
    std::vector<UCHAR> Buffer(65536);
    ULONG Length = (ULONG)Buffer.size();
    EAInformationEx Information = {};
    size_t NameLength = std::strlen(EaName);
    bool Found = false;

    Check(NT_SUCCESS(NtfsFileRecordReadExtendedAttributes(File, Buffer.data(), &Length, &Information)), "read EAs");
    for (ULONG Offset = 0; Offset + 8 <= Length;)
    {
        const UCHAR* Entry = Buffer.data() + Offset;
        ULONG Next;
        USHORT ValueLength;

        std::memcpy(&Next, Entry, sizeof(Next));
        std::memcpy(&ValueLength, Entry + 6, sizeof(ValueLength));
        if (Entry[5] == NameLength && !std::memcmp(Entry + 8, EaName, NameLength))
            Found = ValueLength == Value.size() && !std::memcmp(Entry + 8 + NameLength + 1, Value.data(), Value.size());
        if (Next == 0)
            break;
        Offset += Next;
    }
    Check(Found, "EA value");
    Check(Information.PackedEASize != 0 && IndexEaSize(Name) == Information.PackedEASize, "index entry EA size");
    NtfsFileRecordDestroy(File);
}

int
main(int argc, char** argv)
{
    static const UINT32 Uid = 1000, Mode = 0100644;
    std::vector<UCHAR> UidValue((const UCHAR*)&Uid, (const UCHAR*)&Uid + 4);
    std::vector<UCHAR> Large(900, 0x5a);
    std::string LongName(255, 'n');
    NtfsExtendedAttributeUpdate Small[2] = {};
    NtfsExtendedAttributeUpdate Big[1] = {};

    Check(argc == 2, "expected a disposable NTFS image");
    Small[0].Operation = NTFS_EA_UPDATE_SET;
    Small[0].NameLength = 6;
    Small[0].Name = (const UCHAR*)"$LXUID";
    Small[0].ValueLength = 4;
    Small[0].Value = (const UCHAR*)&Uid;
    Small[1] = Small[0];
    Small[1].Name = (const UCHAR*)"$LXMOD";
    Small[1].Value = (const UCHAR*)&Mode;
    Big[0].Operation = NTFS_EA_UPDATE_SET;
    Big[0].NameLength = 5;
    Big[0].Name = (const UCHAR*)"LARGE";
    Big[0].ValueLength = (USHORT)Large.size();
    Big[0].Value = Large.data();

    Mount(argv[1]);
    Check(NT_SUCCESS(Create("small", Small, 2)), "create with EAs");
    Check(Create("SMALL", Small, 2) == STATUS_OBJECT_NAME_COLLISION, "a known-missing name that exists collides");
    Check(NT_SUCCESS(Create(LongName, Big, 1)), "create with EAs larger than the new record holds");
    Check(Create(LongName, Big, 1) == STATUS_OBJECT_NAME_COLLISION, "a long name that exists collides");
    Check(NT_SUCCESS(Create("plain", nullptr, 0)), "create without EAs");
    CheckEas("small", "$LXUID", UidValue);
    CheckEas(LongName, "LARGE", Large);
    Check(IndexEaSize("plain") == 0, "no EA size without EAs");
    Unmount();

    Mount(argv[1]);
    CheckEas("small", "$LXUID", UidValue);
    CheckEas(LongName, "LARGE", Large);
    Unmount();

    ULONG FailedCreates = 0;

    for (ULONG Failure = 1;; Failure++)
    {
        std::string Name = "failed" + std::to_string(Failure);
        NTSTATUS Status;
        bool Injected;

        Name += std::string(255 - Name.size(), 'f');
        Mount(argv[1]);
        Writes = 0;
        FailAt = Failure;
        Inject = true;
        Status = Create(Name, Big, 1);
        Inject = false;
        Injected = Writes >= Failure;
        Check(Injected || NT_SUCCESS(Status), "create without a failed write");
        FailedCreates += !NT_SUCCESS(Status);
        Unmount();
        Mount(argv[1]);
        if (Exists("\\" + Name) != NT_SUCCESS(Status))
        {
            std::fprintf(stderr, "create_eas: write %lu failed, create returned 0x%08lx, name %s\n",
                         (unsigned long)Failure, (unsigned long)Status,
                         NT_SUCCESS(Status) ? "missing" : "left behind");
            std::exit(1);
        }
        if (NT_SUCCESS(Status))
            CheckEas(Name, "LARGE", Large);
        Unmount();
        if (!Injected)
            break;
    }
    Check(FailedCreates != 0, "some failed write fails the create");
    std::printf("Created files with EAs in place and refused existing names.\n");
    return 0;
}
