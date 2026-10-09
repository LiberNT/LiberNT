/*
 * PROJECT:         ReactOS EventLog Service
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/services/eventlog/logport.c
 * PURPOSE:         LPC Port Interface support
 * COPYRIGHT:       Copyright 2002 Eric Kohl
 *                  Copyright 2005 Saveliy Tretiakov
 */

/* INCLUDES *****************************************************************/

#include "eventlog.h"
#include <ndk/lpcfuncs.h>
#include <iolog/iolog.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

static HANDLE ConnectPortHandle = NULL;
static HANDLE MessagePortHandle = NULL;
extern BOOL onLiveCD;

/* FUNCTIONS ****************************************************************/

NTSTATUS WINAPI PortThreadRoutine(PVOID Param)
{
    NTSTATUS Status = STATUS_SUCCESS;

    Status = InitLogPort();
    if (!NT_SUCCESS(Status))
        return Status;

    while (NT_SUCCESS(Status))
        Status = ProcessPortMessage();

    if (ConnectPortHandle != NULL)
        NtClose(ConnectPortHandle);

    if (MessagePortHandle != NULL)
        NtClose(MessagePortHandle);

    return Status;
}

NTSTATUS InitLogPort(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING PortName = RTL_CONSTANT_STRING(ELF_PORT_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_PORT_ATTRIBUTES PortAttributes;
    union
    {
        PORT_MESSAGE Header;
        UCHAR Buffer[PORT_MAXIMUM_MESSAGE_LENGTH];
    } Request;
    SIZE_T BufferLength;

    ConnectPortHandle = NULL;
    MessagePortHandle = NULL;

    InitializeObjectAttributes(&ObjectAttributes, &PortName, 0, NULL, NULL);

    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.MaxMessageLength = PORT_MAXIMUM_MESSAGE_LENGTH;
    PortAttributes.MaxPoolUsage = 2 * PAGE_SIZE;
    PortAttributes.SecurityQos.Length = sizeof(PortAttributes.SecurityQos);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    Status = NtAlpcCreatePort(&ConnectPortHandle, &ObjectAttributes, &PortAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtAlpcCreatePort() failed (Status %lx)\n", Status);
        goto ByeBye;
    }

    do
    {
        BufferLength = sizeof(Request);
        Status = NtAlpcSendWaitReceivePort(ConnectPortHandle,
                                           0,
                                           NULL,
                                           NULL,
                                           &Request.Header,
                                           &BufferLength,
                                           NULL,
                                           NULL);
    } while (NT_SUCCESS(Status) &&
             ((Request.Header.u2.s2.Type & ~(LPC_CONTINUATION_REQUIRED | LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE)) != LPC_CONNECTION_REQUEST));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtAlpcSendWaitReceivePort() failed (Status %lx)\n", Status);
        goto ByeBye;
    }

    Status = NtAlpcAcceptConnectPort(&MessagePortHandle,
                                     ConnectPortHandle,
                                     0,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &Request.Header,
                                     NULL,
                                     TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtAlpcAcceptConnectPort() failed (Status %lx)\n", Status);
        goto ByeBye;
    }

ByeBye:
    if (!NT_SUCCESS(Status))
    {
        if (ConnectPortHandle != NULL)
            NtClose(ConnectPortHandle);

        if (MessagePortHandle != NULL)
            NtClose(MessagePortHandle);
    }
    return Status;
}

NTSTATUS ProcessPortMessage(VOID)
{
    NTSTATUS Status;
    PLOGFILE SystemLog = NULL;
    UCHAR Buffer[PORT_MAXIMUM_MESSAGE_LENGTH]; // IO_ERROR_LOG_MESSAGE_LENGTH
    PELF_API_MSG Message = (PELF_API_MSG)Buffer;
    PIO_ERROR_LOG_MESSAGE ErrorMessage;
    PEVENTLOGRECORD LogBuffer;
    SIZE_T RecSize;
    ULONG Time;
    USHORT EventType;
    UNICODE_STRING SourceName, ComputerName;
    DWORD dwComputerNameLength;
    WCHAR szComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    SIZE_T BufferLength;
    ULONG MessageType;

    DPRINT("ProcessPortMessage() called\n");

    SystemLog = LogfListItemByName(L"System");

    while (TRUE)
    {
        BufferLength = sizeof(Buffer);
        Status = NtAlpcSendWaitReceivePort(MessagePortHandle,
                                           0,
                                           NULL,
                                           NULL,
                                           &Message->Header,
                                           &BufferLength,
                                           NULL,
                                           NULL);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtAlpcSendWaitReceivePort() failed (Status %lx)\n", Status);
            break;
        }

        if (Status != STATUS_SUCCESS)
            continue;

        DPRINT("Received message\n");

        MessageType = Message->Header.u2.s2.Type &
                      ~(LPC_CONTINUATION_REQUIRED | LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE);

        if (MessageType == LPC_PORT_CLOSED)
        {
            DPRINT("Port closed\n");
            return STATUS_SUCCESS;
        }

        if (MessageType == LPC_REQUEST)
        {
            DPRINT("Received request\n");
        }
        else if (MessageType == LPC_DATAGRAM)
        {
            DPRINT("Received datagram (0x%x, 0x%x)\n",
                   Message->Unknown[0], Message->Unknown[1]);
            ErrorMessage = &Message->IoErrorMessage;

            // ASSERT(ErrorMessage->Type == IO_TYPE_ERROR_MESSAGE);

            RtlInitEmptyUnicodeString(&SourceName, NULL, 0);
            if (ErrorMessage->DriverNameLength > sizeof(UNICODE_NULL)) // DriverNameLength counts NULL-terminator
            {
                SourceName.Buffer = (PWSTR)((ULONG_PTR)ErrorMessage + ErrorMessage->DriverNameOffset);
                SourceName.MaximumLength = ErrorMessage->DriverNameLength;
                SourceName.Length = SourceName.MaximumLength - sizeof(UNICODE_NULL);
            }

            dwComputerNameLength = ARRAYSIZE(szComputerName);
            if (!GetComputerNameW(szComputerName, &dwComputerNameLength))
                szComputerName[0] = L'\0';

            RtlInitUnicodeString(&ComputerName, szComputerName);

            RtlTimeToSecondsSince1970(&ErrorMessage->TimeStamp, &Time);

            /* Set the event type based on the error code severity */
            EventType = (USHORT)(ErrorMessage->EntryData.ErrorCode >> 30);
            if (EventType == STATUS_SEVERITY_SUCCESS)
            {
                EventType = EVENTLOG_SUCCESS;
            }
            else if (EventType == STATUS_SEVERITY_INFORMATIONAL) // NT_INFORMATION
            {
                EventType = EVENTLOG_INFORMATION_TYPE;
            }
            else if (EventType == STATUS_SEVERITY_WARNING)       // NT_WARNING
            {
                EventType = EVENTLOG_WARNING_TYPE;
            }
            else if (EventType == STATUS_SEVERITY_ERROR)         // NT_ERROR
            {
                EventType = EVENTLOG_ERROR_TYPE;
            }
            else
            {
                /* Unknown severity, set to error */
                EventType = EVENTLOG_ERROR_TYPE;
            }

            /*
             * The data being saved consists of the IO_ERROR_LOG_PACKET structure
             * header, plus the additional raw data from the driver.
             */
            LogBuffer = LogfAllocAndBuildNewRecord(
                            &RecSize,
                            Time,
                            EventType,
                            ErrorMessage->EntryData.EventCategory,
                            ErrorMessage->EntryData.ErrorCode,
                            &SourceName,
                            &ComputerName,
                            0,
                            NULL,
                            ErrorMessage->EntryData.NumberOfStrings,
                            (PWSTR)((ULONG_PTR)ErrorMessage +
                                ErrorMessage->EntryData.StringOffset),
                            FIELD_OFFSET(IO_ERROR_LOG_PACKET, DumpData) +
                                ErrorMessage->EntryData.DumpDataSize,
                            (PVOID)&ErrorMessage->EntryData);
            if (LogBuffer == NULL)
            {
                DPRINT1("LogfAllocAndBuildNewRecord failed!\n");
                // return STATUS_NO_MEMORY;
                continue;
            }

            if (!onLiveCD && SystemLog)
            {
                Status = LogfWriteRecord(SystemLog, LogBuffer, RecSize);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ERROR writing to event log `%S' (Status 0x%08lx)\n",
                            SystemLog->LogName, Status);
                }
            }
            else
            {
                DPRINT1("\n--- EVENTLOG RECORD ---\n");
                PRINT_RECORD(LogBuffer);
                DPRINT1("\n");
            }

            LogfFreeRecord(LogBuffer);
        }
    }

    return Status;
}
