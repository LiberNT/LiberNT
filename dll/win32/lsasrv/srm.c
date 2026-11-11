/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     Local Security Authority Server DLL
 * FILE:        dll/win32/lsasrv/srm.c
 * PURPOSE:     Security Reference Monitor Server
 *
 * PROGRAMMERS: Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ****************************************************************/

#include "lsasrv.h"
#include <ndk/ntndk.h>

/* GLOBALS *****************************************************************/

HANDLE SeLsaCommandPort;
HANDLE SeRmCommandPort;

/* FUNCTIONS ***************************************************************/

static
VOID
LsapComponentTest(
    PLSAP_RM_API_MESSAGE Message)
{
    ERR("Security: LSA Component Test Command Received\n");
}

static
VOID
LsapAdtWriteLog(
    PLSAP_RM_API_MESSAGE Message)
{
    ERR("LsapAdtWriteLog\n");
}

static
VOID
LsapAsync(
    PLSAP_RM_API_MESSAGE Message)
{
    ERR("LsapAsync\n");
}

static
VOID
LsapLogonSessionDeleted(
    PLSAP_RM_API_MESSAGE Message)
{
    static const LUID SystemLuid = SYSTEM_LUID;
    PLUID LogonId = &Message->u.LogonLuid;

    /* The well-known sessions, the system's own and the service accounts', stay */
    if (LogonId->HighPart == 0 && LogonId->LowPart <= SystemLuid.LowPart)
        return;

    LsapLockLogonSessions();
    LsapDeleteLogonSession(LogonId);
    LsapUnlockLogonSessions();
}

static
HANDLE
LsapRmAcceptKernel(VOID)
{
    LSAP_RM_API_MESSAGE Message;
    HANDLE MessagePort;
    SIZE_T BufferLength;
    NTSTATUS Status;

    for (;;)
    {
        BufferLength = sizeof(Message);
        Status = NtAlpcSendWaitReceivePort(SeLsaCommandPort,
                                           0,
                                           NULL,
                                           NULL,
                                           &Message.Header,
                                           &BufferLength,
                                           NULL,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ERR("LsapRmAcceptKernel - Port Listen failed 0x%lx\n", Status);
            return NULL;
        }

        if ((Message.Header.u2.s2.Type & ~(LPC_CONTINUATION_REQUIRED | LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE)) !=
            LPC_CONNECTION_REQUEST)
        {
            continue;
        }

        Status = NtAlpcAcceptConnectPort(&MessagePort,
                                         SeLsaCommandPort,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         &Message.Header,
                                         NULL,
                                         TRUE);
        if (NT_SUCCESS(Status)) return MessagePort;

        ERR("LsapRmAcceptKernel - Port Accept Connect failed 0x%lx\n", Status);
    }
}

static
DWORD
WINAPI
LsapRmServerThread(
    PVOID StartContext)
{
    LSAP_RM_API_MESSAGE Message;
    PPORT_MESSAGE ReplyMessage = NULL;
    HANDLE MessagePort = NULL, DummyPortHandle;
    SIZE_T BufferLength;
    ULONG MessageType;
    NTSTATUS Status;

    for (;;)
    {
        /* Only the kernel can connect, and it connects again after losing the port */
        if (MessagePort == NULL)
        {
            MessagePort = LsapRmAcceptKernel();
            if (MessagePort == NULL) return STATUS_PORT_DISCONNECTED;

            ReplyMessage = NULL;
        }

        BufferLength = sizeof(Message);
        Status = NtAlpcSendWaitReceivePort(MessagePort,
                                           ReplyMessage ? ALPC_MSGFLG_REPLY_MESSAGE : 0,
                                           ReplyMessage,
                                           NULL,
                                           &Message.Header,
                                           &BufferLength,
                                           NULL,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ERR("LsapRmServerThread - Failed to get message: 0x%lx\n", Status);
            if (ReplyMessage == NULL)
            {
                NtClose(MessagePort);
                MessagePort = NULL;
            }
            ReplyMessage = NULL;
            continue;
        }

        ReplyMessage = NULL;
        MessageType = Message.Header.u2.s2.Type &
                      ~(LPC_CONTINUATION_REQUIRED | LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE);

        switch (MessageType)
        {
            case LPC_CONNECTION_REQUEST:
                NtAlpcAcceptConnectPort(&DummyPortHandle,
                                        SeLsaCommandPort,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &Message.Header,
                                        NULL,
                                        FALSE);
                break;

            case LPC_PORT_CLOSED:
            case LPC_CLIENT_DIED:
                NtClose(MessagePort);
                MessagePort = NULL;
                break;

            case LPC_REQUEST:
                ReplyMessage = &Message.Header;

                switch (Message.ApiNumber)
                {
                    case LsapAdtWriteLogApi:
                        LsapAdtWriteLog(&Message);
                        break;

                    case LsapAsyncApi:
                        LsapAsync(&Message);
                        break;

                    case LsapComponentTestApi:
                        LsapComponentTest(&Message);
                        break;

                    case LsapLogonSessionDeletedApi:
                        LsapLogonSessionDeleted(&Message);
                        break;

                    default:
                        ERR("LsapRmServerThread - invalid API number: 0x%lx\n",
                            Message.ApiNumber);
                        ReplyMessage = NULL;
                }
                break;

            default:
                ERR("LsapRmServerThread - unexpected message type: 0x%lx\n",
                    Message.Header.u2.s2.Type);
                break;
        }
    }
}

NTSTATUS
LsapRmInitializeServer(VOID)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_PORT_ATTRIBUTES PortAttributes;
    SECURITY_DESCRIPTOR PortSd;
    ACL PortDacl;
    HANDLE InitEvent;
    HANDLE ThreadHandle;
    DWORD ThreadId;
    NTSTATUS Status;

    /* Create the LSA command port, which only the kernel may connect to */
    RtlCreateSecurityDescriptor(&PortSd, SECURITY_DESCRIPTOR_REVISION);
    RtlCreateAcl(&PortDacl, sizeof(PortDacl), ACL_REVISION);
    RtlSetDaclSecurityDescriptor(&PortSd, TRUE, &PortDacl, FALSE);
    RtlInitUnicodeString(&Name, L"\\SeLsaCommandPort");
    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, &PortSd);
    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.MaxMessageLength = PORT_MAXIMUM_MESSAGE_LENGTH;
    PortAttributes.MaxPoolUsage = 2 * PAGE_SIZE;
    PortAttributes.SecurityQos.Length = sizeof(PortAttributes.SecurityQos);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    Status = NtAlpcCreatePort(&SeLsaCommandPort, &ObjectAttributes, &PortAttributes);
    if (!NT_SUCCESS(Status))
    {
        ERR("LsapRmInitializeServer - Port Create failed 0x%lx\n", Status);
        return Status;
    }

    /* Open the LSA init event */
    RtlInitUnicodeString(&Name, L"\\SeLsaInitEvent");
    InitializeObjectAttributes(&ObjectAttributes, &Name, 0, NULL, NULL);
    Status = NtOpenEvent(&InitEvent, 2, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        ERR("LsapRmInitializeServer - Lsa Init Event Open failed 0x%lx\n", Status);
        return Status;
    }

    /* Signal the kernel, that we are ready */
    Status = NtSetEvent(InitEvent, 0);
    if (!NT_SUCCESS(Status))
    {
        ERR("LsapRmInitializeServer - Set Init Event failed 0x%lx\n", Status);
        return Status;
    }

    /* Setup the QoS structure */
    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.MaxMessageLength = PORT_MAXIMUM_MESSAGE_LENGTH;
    PortAttributes.SecurityQos.Length = sizeof(PortAttributes.SecurityQos);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    PortAttributes.SecurityQos.EffectiveOnly = TRUE;

    /* Connect to the kernel server */
    RtlInitUnicodeString(&Name, L"\\SeRmCommandPort");
    Status = NtAlpcConnectPort(&SeRmCommandPort,
                               &Name,
                               NULL,
                               &PortAttributes,
                               ALPC_SYNC_CONNECTION,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL);
    if (!NT_SUCCESS(Status))
    {
        ERR("LsapRmInitializeServer - Connect to Rm Command Port failed 0x%lx\n", Status);
        return Status;
    }

    /* Create the server thread */
    ThreadHandle = CreateThread(NULL, 0, LsapRmServerThread, NULL, 0, &ThreadId);
    if (ThreadHandle == NULL)
    {
        ERR("LsapRmInitializeServer - Create Thread  failed 0x%lx\n", Status);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Close the server thread handle */
    CloseHandle(ThreadHandle);

    return STATUS_SUCCESS;
}

NTSTATUS
LsapRmCreateLogonSession(
    PLUID LogonId)
{
    SEP_RM_API_MESSAGE RequestMessage;
    SEP_RM_API_MESSAGE ReplyMessage;
    SIZE_T BufferLength;
    NTSTATUS Status;

    TRACE("LsapRmCreateLogonSession(%p)\n", LogonId);

    RtlZeroMemory(&RequestMessage.Header, sizeof(RequestMessage.Header));
    RequestMessage.Header.u1.s1.TotalLength =
        (CSHORT)(sizeof(PORT_MESSAGE) + sizeof(ULONG) + sizeof(LUID));
    RequestMessage.Header.u1.s1.DataLength =
        RequestMessage.Header.u1.s1.TotalLength -
        (CSHORT)sizeof(PORT_MESSAGE);

    RequestMessage.ApiNumber = (ULONG)RmCreateLogonSession;
    RtlCopyLuid(&RequestMessage.u.LogonLuid, LogonId);

    ReplyMessage.Header.u2.ZeroInit = 0;
    ReplyMessage.Header.u1.s1.TotalLength =
        (CSHORT)(sizeof(PORT_MESSAGE) + sizeof(ULONG) + sizeof(NTSTATUS));
    ReplyMessage.Header.u1.s1.DataLength =
        ReplyMessage.Header.u1.s1.TotalLength -
        (CSHORT)sizeof(PORT_MESSAGE);

    ReplyMessage.u.ResultStatus = STATUS_SUCCESS;

    BufferLength = sizeof(ReplyMessage);
    Status = NtAlpcSendWaitReceivePort(SeRmCommandPort,
                                       ALPC_MSGFLG_SYNC_REQUEST,
                                       (PPORT_MESSAGE)&RequestMessage,
                                       NULL,
                                       (PPORT_MESSAGE)&ReplyMessage,
                                       &BufferLength,
                                       NULL,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = ReplyMessage.u.ResultStatus;
    }

    return Status;
}

NTSTATUS
LsapRmDeleteLogonSession(
    PLUID LogonId)
{
    SEP_RM_API_MESSAGE RequestMessage;
    SEP_RM_API_MESSAGE ReplyMessage;
    SIZE_T BufferLength;
    NTSTATUS Status;

    TRACE("LsapRmDeleteLogonSession(%p)\n", LogonId);

    RtlZeroMemory(&RequestMessage.Header, sizeof(RequestMessage.Header));
    RequestMessage.Header.u1.s1.TotalLength =
        (CSHORT)(sizeof(PORT_MESSAGE) + sizeof(ULONG) + sizeof(LUID));
    RequestMessage.Header.u1.s1.DataLength =
        RequestMessage.Header.u1.s1.TotalLength -
        (CSHORT)sizeof(PORT_MESSAGE);

    RequestMessage.ApiNumber = (ULONG)RmDeleteLogonSession;
    RtlCopyLuid(&RequestMessage.u.LogonLuid, LogonId);

    ReplyMessage.Header.u2.ZeroInit = 0;
    ReplyMessage.Header.u1.s1.TotalLength =
        (CSHORT)(sizeof(PORT_MESSAGE) + sizeof(ULONG) + sizeof(NTSTATUS));
    ReplyMessage.Header.u1.s1.DataLength =
        ReplyMessage.Header.u1.s1.TotalLength -
        (CSHORT)sizeof(PORT_MESSAGE);

    ReplyMessage.u.ResultStatus = STATUS_SUCCESS;

    BufferLength = sizeof(ReplyMessage);
    Status = NtAlpcSendWaitReceivePort(SeRmCommandPort,
                                       ALPC_MSGFLG_SYNC_REQUEST,
                                       (PPORT_MESSAGE)&RequestMessage,
                                       NULL,
                                       (PPORT_MESSAGE)&ReplyMessage,
                                       &BufferLength,
                                       NULL,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = ReplyMessage.u.ResultStatus;
    }

    return Status;
}
