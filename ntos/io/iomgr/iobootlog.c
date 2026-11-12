/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Recording which drivers answered during the boot
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define IOP_BOOT_LOG_SIZE (PAGE_SIZE * 8)

static BOOLEAN IopBootLogEnabled = FALSE;
static BOOLEAN IopBootLogSaved = FALSE;
static PCHAR IopBootLogBuffer = NULL;
static ULONG IopBootLogUsed = 0;
static KSPIN_LOCK IopBootLogLock;

static const UNICODE_STRING IopBootLogFileName =
    RTL_CONSTANT_STRING(L"\\SystemRoot\\ntbtlog.txt");

/* PRIVATE FUNCTIONS **********************************************************/

static
VOID
IopAppendBootLogLine(
    _In_ PCSTR Prefix,
    _In_ PUNICODE_STRING Name)
{
    ANSI_STRING AnsiName;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG PrefixLength;
    ULONG Needed;

    if (!IopBootLogEnabled || IopBootLogBuffer == NULL) return;

    Status = RtlUnicodeStringToAnsiString(&AnsiName, Name, TRUE);
    if (!NT_SUCCESS(Status)) return;

    PrefixLength = (ULONG)strlen(Prefix);
    Needed = PrefixLength + AnsiName.Length + 2;

    KeAcquireSpinLock(&IopBootLogLock, &OldIrql);

    if (IopBootLogUsed + Needed < IOP_BOOT_LOG_SIZE)
    {
        RtlCopyMemory(IopBootLogBuffer + IopBootLogUsed,
                      Prefix,
                      PrefixLength);
        IopBootLogUsed += PrefixLength;

        RtlCopyMemory(IopBootLogBuffer + IopBootLogUsed,
                      AnsiName.Buffer,
                      AnsiName.Length);
        IopBootLogUsed += AnsiName.Length;

        IopBootLogBuffer[IopBootLogUsed++] = '\r';
        IopBootLogBuffer[IopBootLogUsed++] = '\n';
    }

    KeReleaseSpinLock(&IopBootLogLock, OldIrql);

    RtlFreeAnsiString(&AnsiName);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
IopInitBootLog(
    IN BOOLEAN StartBootLog)
{
    KeInitializeSpinLock(&IopBootLogLock);

    IopBootLogBuffer = ExAllocatePoolZero(NonPagedPool,
                                          IOP_BOOT_LOG_SIZE,
                                          TAG_IO);
    if (IopBootLogBuffer == NULL) return;

    IopBootLogUsed = 0;

    if (StartBootLog) IopStartBootLog();
}

CODE_SEG("INIT")
VOID
IopStartBootLog(VOID)
{
    IopBootLogEnabled = TRUE;
    IopBootLogSaved = FALSE;
}

VOID
IopStopBootLog(VOID)
{
    IopBootLogEnabled = FALSE;
}

VOID
IopBootLog(
    IN PUNICODE_STRING DriverName,
    IN BOOLEAN Success)
{
    IopAppendBootLogLine(Success ? "Loaded driver " : "Did not load driver ",
                         DriverName);
}

VOID
IopSaveBootLogToFile(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    NTSTATUS Status;
    HANDLE Handle;
    KIRQL OldIrql;
    ULONG Length;

    PAGED_CODE();

    if (IopBootLogBuffer == NULL || IopBootLogSaved) return;

    KeAcquireSpinLock(&IopBootLogLock, &OldIrql);
    Length = IopBootLogUsed;
    KeReleaseSpinLock(&IopBootLogLock, OldIrql);

    if (Length == 0) return;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&IopBootLogFileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateFile(&Handle,
                          FILE_APPEND_DATA | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT |
                              FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status)) return;

    Offset.QuadPart = 0;

    Status = ZwWriteFile(Handle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         IopBootLogBuffer,
                         Length,
                         &Offset,
                         NULL);

    ZwClose(Handle);

    if (NT_SUCCESS(Status)) IopBootLogSaved = TRUE;
}
