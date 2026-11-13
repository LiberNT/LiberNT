/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ISO 9660 directory record names, with Rock Ridge SUSP names
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <disk.h>
#include <fs/iso.h>

#define RECORD_BUFFER_SIZE 256
#define FILE_ID_LENGTH_OFFSET 32
#define FILE_ID_OFFSET 33

static UCHAR RecordBuffer[RECORD_BUFFER_SIZE];
static SIZE_T RecordPosition;

static int Failures;
static int Checks;

static void
BeginRecord(
    _In_ PCSTR FileId)
{
    SIZE_T FileIdLength = strlen(FileId);

    memset(RecordBuffer, 0, sizeof(RecordBuffer));
    RecordBuffer[FILE_ID_LENGTH_OFFSET] = (UCHAR)FileIdLength;
    memcpy(&RecordBuffer[FILE_ID_OFFSET], FileId, FileIdLength);

    RecordPosition = FILE_ID_OFFSET + FileIdLength;
    if ((FileIdLength & 1) == 0)
        RecordPosition++;
}

static void
AddSuspEntry(
    _In_ PCSTR Signature,
    _In_ UCHAR Flags,
    _In_ PCSTR Data)
{
    SIZE_T DataLength = strlen(Data);

    RecordBuffer[RecordPosition + 0] = (UCHAR)Signature[0];
    RecordBuffer[RecordPosition + 1] = (UCHAR)Signature[1];
    RecordBuffer[RecordPosition + 2] = (UCHAR)(5 + DataLength);
    RecordBuffer[RecordPosition + 3] = 1;
    RecordBuffer[RecordPosition + 4] = Flags;
    memcpy(&RecordBuffer[RecordPosition + 5], Data, DataLength);
    RecordPosition += 5 + DataLength;
}

static PDIR_RECORD
EndRecord(void)
{
    RecordBuffer[0] = (UCHAR)RecordPosition;
    return (PDIR_RECORD)RecordBuffer;
}

static void
ExpectName(
    _In_ PCSTR Case,
    _In_ PDIR_RECORD Record,
    _In_ BOOLEAN ExpectedResult,
    _In_ PCSTR ExpectedName)
{
    CHAR NameBuffer[64];

    memset(NameBuffer, 0x7f, sizeof(NameBuffer));
    Checks++;
    if (IsoExtractName(Record, NameBuffer, sizeof(NameBuffer)) != ExpectedResult)
    {
        printf("FAIL %s: result %d, expected %d\n", Case, !ExpectedResult, ExpectedResult);
        Failures++;
        return;
    }

    if (ExpectedResult && strcmp(NameBuffer, ExpectedName) != 0)
    {
        printf("FAIL %s: name '%s', expected '%s'\n", Case, NameBuffer, ExpectedName);
        Failures++;
    }
}

static void
ExpectTruncatedName(
    _In_ PCSTR Case,
    _In_ PDIR_RECORD Record,
    _In_ PCSTR ExpectedName)
{
    CHAR NameBuffer[8];
    int Index;

    Checks++;
    if (!IsoExtractName(Record, NameBuffer, sizeof(NameBuffer)))
    {
        printf("FAIL %s: result 0, expected 1\n", Case);
        Failures++;
        return;
    }

    if (strcmp(NameBuffer, ExpectedName) != 0)
    {
        printf("FAIL %s: name '%s', expected '%s'\n", Case, NameBuffer, ExpectedName);
        Failures++;
        return;
    }

    for (Index = (int)strlen(NameBuffer); Index < (int)sizeof(NameBuffer); Index++)
    {
        if (NameBuffer[Index] != 0)
        {
            printf("FAIL %s: byte %d of the truncated name was written\n", Case, Index);
            Failures++;
            return;
        }
    }
}

int
main(void)
{
    PDIR_RECORD Record;

    /* The Rock Ridge name replaces the folded primary identifier */
    BeginRecord("WINDOWS_STORAGE.DLL;1");
    AddSuspEntry("NM", 0x00, "windows.storage.dll");
    ExpectName("rock ridge name", EndRecord(), TRUE, "windows.storage.dll");

    /* A name longer than 255 bytes is split over CONTINUE entries */
    BeginRecord("PROGRAM_FILES");
    AddSuspEntry("NM", 0x01, "Program ");
    AddSuspEntry("NM", 0x00, "Files");
    ExpectName("continued name", EndRecord(), TRUE, "Program Files");

    /* Only the name entry matters; the other SUSP entries are skipped */
    BeginRecord("README.TXT;1");
    AddSuspEntry("PX", 0x00, "\x01\x02\x03\x04\x05\x06\x07\x08");
    AddSuspEntry("TF", 0x00, "\x0e");
    AddSuspEntry("NM", 0x00, "readme.txt");
    ExpectName("other entries", EndRecord(), TRUE, "readme.txt");

    /* CURRENT and PARENT carry no name */
    BeginRecord("CLAVIER.CFG;1");
    AddSuspEntry("NM", 0x02, "");
    AddSuspEntry("NM", 0x04, "");
    AddSuspEntry("NM", 0x00, "clavier.cfg");
    ExpectName("current and parent", EndRecord(), TRUE, "clavier.cfg");

    /* The walk stops at the name entry that does not continue */
    BeginRecord("BOOT.INI;1");
    AddSuspEntry("NM", 0x00, "boot.ini");
    AddSuspEntry("NM", 0x00, "ignored.ini");
    ExpectName("stop after a final entry", EndRecord(), TRUE, "boot.ini");

    /* Without SUSP names the ISO 9660 identifier answers, without ';1' */
    BeginRecord("README.TXT;1");
    ExpectName("primary identifier", EndRecord(), TRUE, "README.TXT");

    /* A directory record has no version and may keep its dot */
    BeginRecord("PROGRAM FILES.");
    ExpectName("directory identifier", EndRecord(), TRUE, "PROGRAM FILES");

    /* A malformed entry stops the walk and falls back to the identifier */
    BeginRecord("SYSTEM32");
    AddSuspEntry("NM", 0x00, "system32");
    Record = EndRecord();
    RecordBuffer[RecordPosition - 5] = 5;
    RecordBuffer[0] = (UCHAR)(RecordPosition - 10);
    ExpectName("truncated entry", Record, TRUE, "SYSTEM32");

    /* The name is cut to the buffer and stays terminated */
    BeginRecord("VERYLONGNAME.TXT;1");
    AddSuspEntry("NM", 0x00, "verylongname.txt");
    ExpectTruncatedName("truncated name", EndRecord(), "verylon");

    /* '.' and '..' are not names */
    BeginRecord(".");
    Record = EndRecord();
    RecordBuffer[FILE_ID_LENGTH_OFFSET] = 1;
    RecordBuffer[FILE_ID_OFFSET] = 1;
    ExpectName("parent entry", Record, FALSE, "");

    /* An empty identifier is not a name either */
    BeginRecord("");
    ExpectName("empty identifier", EndRecord(), FALSE, "");

    printf("freeldr iso names: %d checks, %d failures\n", Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
