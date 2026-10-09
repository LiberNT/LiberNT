/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/lsalib/lsa.c
 * PURPOSE:         Client-side LSA functions
 * UPDATE HISTORY:
 *                  Created 05/08/00
 */

/* INCLUDES ******************************************************************/

#include <ndk/exfuncs.h>
#include <ndk/lpctypes.h>
#include <ndk/lpcfuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/obfuncs.h>
#include <lsass/lsass.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
LsapClientConnect(
    _Out_ PHANDLE PortHandle,
    _Inout_ PLSA_CONNECTION_INFO ConnectInfo)
{
    UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\LsaAuthenticationPort");
    ALPC_PORT_ATTRIBUTES PortAttributes;
    struct
    {
        PORT_MESSAGE h;
        LSA_CONNECTION_INFO ConnectInfo;
    } ConnectMsg;
    SIZE_T BufferLength = sizeof(ConnectMsg);
    NTSTATUS Status;

    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.MaxMessageLength = sizeof(LSA_API_MSG);
    PortAttributes.SecurityQos.Length = sizeof(PortAttributes.SecurityQos);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    PortAttributes.SecurityQos.EffectiveOnly = TRUE;

    RtlZeroMemory(&ConnectMsg.h, sizeof(ConnectMsg.h));
    ConnectMsg.h.u1.s1.DataLength = sizeof(ConnectMsg.ConnectInfo);
    ConnectMsg.h.u1.s1.TotalLength = sizeof(ConnectMsg.h) + sizeof(ConnectMsg.ConnectInfo);
    ConnectMsg.ConnectInfo = *ConnectInfo;

    Status = ZwAlpcConnectPort(PortHandle,
                               &PortName,
                               NULL,
                               &PortAttributes,
                               ALPC_SYNC_CONNECTION,
                               NULL,
                               &ConnectMsg.h,
                               &BufferLength,
                               NULL,
                               NULL,
                               NULL);
    if (NT_SUCCESS(Status))
        *ConnectInfo = ConnectMsg.ConnectInfo;

    return Status;
}

NTSTATUS
NTAPI
LsapClientCall(
    _In_ HANDLE PortHandle,
    _Inout_ PLSA_API_MSG ApiMessage)
{
    SIZE_T BufferLength = sizeof(*ApiMessage);
    CSHORT DataLength = ApiMessage->h.u1.s1.DataLength;

    RtlZeroMemory(&ApiMessage->h, sizeof(ApiMessage->h));
    ApiMessage->h.u1.s1.DataLength = DataLength;
    ApiMessage->h.u1.s1.TotalLength = DataLength + sizeof(ApiMessage->h);

    return ZwAlpcSendWaitReceivePort(PortHandle,
                                     ALPC_MSGFLG_SYNC_REQUEST,
                                     &ApiMessage->h,
                                     NULL,
                                     &ApiMessage->h,
                                     &BufferLength,
                                     NULL,
                                     NULL);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaCallAuthenticationPackage(IN HANDLE LsaHandle,
                             IN ULONG AuthenticationPackage,
                             IN PVOID ProtocolSubmitBuffer,
                             IN ULONG SubmitBufferLength,
                             OUT PVOID *ProtocolReturnBuffer,
                             OUT PULONG ReturnBufferLength,
                             OUT PNTSTATUS ProtocolStatus)
{
    LSA_API_MSG ApiMessage;
    NTSTATUS Status;

    DPRINT1("LsaCallAuthenticationPackage()\n");

    ApiMessage.ApiNumber = LSASS_REQUEST_CALL_AUTHENTICATION_PACKAGE;
    ApiMessage.h.u1.s1.DataLength = LSA_PORT_DATA_SIZE(ApiMessage.CallAuthenticationPackage);
    ApiMessage.h.u1.s1.TotalLength = LSA_PORT_MESSAGE_SIZE;
    ApiMessage.h.u2.ZeroInit = 0;

    ApiMessage.CallAuthenticationPackage.Request.AuthenticationPackage = AuthenticationPackage;
    ApiMessage.CallAuthenticationPackage.Request.ProtocolSubmitBuffer = ProtocolSubmitBuffer;
    ApiMessage.CallAuthenticationPackage.Request.SubmitBufferLength = SubmitBufferLength;

    Status = LsapClientCall(LsaHandle, &ApiMessage);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("LsapClientCall() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (!NT_SUCCESS(ApiMessage.Status))
    {
        DPRINT1("LsapClientCall() failed (ApiMessage.Status 0x%08lx)\n", ApiMessage.Status);
        return ApiMessage.Status;
    }

    *ProtocolReturnBuffer = ApiMessage.CallAuthenticationPackage.Reply.ProtocolReturnBuffer;
    *ReturnBufferLength = ApiMessage.CallAuthenticationPackage.Reply.ReturnBufferLength;
    *ProtocolStatus = ApiMessage.CallAuthenticationPackage.Reply.ProtocolStatus;

    return Status;
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaDeregisterLogonProcess(IN HANDLE LsaHandle)
{
    LSA_API_MSG ApiMessage;
    NTSTATUS Status;

    DPRINT("LsaDeregisterLogonProcess()\n");

    ApiMessage.ApiNumber = LSASS_REQUEST_DEREGISTER_LOGON_PROCESS;
    ApiMessage.h.u1.s1.DataLength = LSA_PORT_DATA_SIZE(ApiMessage.DeregisterLogonProcess);
    ApiMessage.h.u1.s1.TotalLength = LSA_PORT_MESSAGE_SIZE;
    ApiMessage.h.u2.ZeroInit = 0;

    Status = LsapClientCall(LsaHandle, &ApiMessage);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("LsapClientCall() failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    if (!NT_SUCCESS(ApiMessage.Status))
    {
        DPRINT1("LsapClientCall() failed (ApiMessage.Status 0x%08lx)\n", ApiMessage.Status);
        return ApiMessage.Status;
    }

    ZwClose(LsaHandle);

    DPRINT("LsaDeregisterLogonProcess() done (Status 0x%08lx)\n", Status);

    return Status;
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaFreeReturnBuffer(IN PVOID Buffer)
{
    SIZE_T Size = 0;

    return ZwFreeVirtualMemory(NtCurrentProcess(),
                               &Buffer,
                               &Size,
                               MEM_RELEASE);
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaLookupAuthenticationPackage(IN HANDLE LsaHandle,
                               IN PLSA_STRING PackageName,
                               OUT PULONG AuthenticationPackage)
{
    LSA_API_MSG ApiMessage;
    NTSTATUS Status;

    /* Check the package name length */
    if (PackageName->Length > LSASS_MAX_PACKAGE_NAME_LENGTH)
    {
        return STATUS_NAME_TOO_LONG;
    }

    ApiMessage.ApiNumber = LSASS_REQUEST_LOOKUP_AUTHENTICATION_PACKAGE;
    ApiMessage.h.u1.s1.DataLength = LSA_PORT_DATA_SIZE(ApiMessage.LookupAuthenticationPackage);
    ApiMessage.h.u1.s1.TotalLength = LSA_PORT_MESSAGE_SIZE;
    ApiMessage.h.u2.ZeroInit = 0;

    ApiMessage.LookupAuthenticationPackage.Request.PackageNameLength = PackageName->Length;
    strncpy(ApiMessage.LookupAuthenticationPackage.Request.PackageName,
            PackageName->Buffer,
            ApiMessage.LookupAuthenticationPackage.Request.PackageNameLength);
    ApiMessage.LookupAuthenticationPackage.Request.PackageName[ApiMessage.LookupAuthenticationPackage.Request.PackageNameLength] = ANSI_NULL;

    Status = LsapClientCall(LsaHandle, &ApiMessage);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!NT_SUCCESS(ApiMessage.Status))
    {
        return ApiMessage.Status;
    }

    *AuthenticationPackage = ApiMessage.LookupAuthenticationPackage.Reply.Package;

    return Status;
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaLogonUser(IN HANDLE LsaHandle,
             IN PLSA_STRING OriginName,
             IN SECURITY_LOGON_TYPE LogonType,
             IN ULONG AuthenticationPackage,
             IN PVOID AuthenticationInformation,
             IN ULONG AuthenticationInformationLength,
             IN PTOKEN_GROUPS LocalGroups OPTIONAL,
             IN PTOKEN_SOURCE SourceContext,
             OUT PVOID *ProfileBuffer,
             OUT PULONG ProfileBufferLength,
             OUT PLUID LogonId,
             OUT PHANDLE Token,
             OUT PQUOTA_LIMITS Quotas,
             OUT PNTSTATUS SubStatus)
{
    LSA_API_MSG ApiMessage;
    NTSTATUS Status;

    ApiMessage.ApiNumber = LSASS_REQUEST_LOGON_USER;
    ApiMessage.h.u1.s1.DataLength = LSA_PORT_DATA_SIZE(ApiMessage.LogonUser);
    ApiMessage.h.u1.s1.TotalLength = LSA_PORT_MESSAGE_SIZE;
    ApiMessage.h.u2.ZeroInit = 0;

    ApiMessage.LogonUser.Request.OriginName = *OriginName;
    ApiMessage.LogonUser.Request.LogonType = LogonType;
    ApiMessage.LogonUser.Request.AuthenticationPackage = AuthenticationPackage;
    ApiMessage.LogonUser.Request.AuthenticationInformation = AuthenticationInformation;
    ApiMessage.LogonUser.Request.AuthenticationInformationLength = AuthenticationInformationLength;
    ApiMessage.LogonUser.Request.LocalGroups = LocalGroups;
    if (LocalGroups != NULL)
        ApiMessage.LogonUser.Request.LocalGroupsCount = LocalGroups->GroupCount;
    else
        ApiMessage.LogonUser.Request.LocalGroupsCount = 0;
    ApiMessage.LogonUser.Request.SourceContext = *SourceContext;

    Status = LsapClientCall(LsaHandle, &ApiMessage);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *SubStatus = ApiMessage.LogonUser.Reply.SubStatus;

    if (!NT_SUCCESS(ApiMessage.Status))
    {
        return ApiMessage.Status;
    }

    *ProfileBuffer = ApiMessage.LogonUser.Reply.ProfileBuffer;
    *ProfileBufferLength = ApiMessage.LogonUser.Reply.ProfileBufferLength;
    *LogonId = ApiMessage.LogonUser.Reply.LogonId;
    *Token = ApiMessage.LogonUser.Reply.Token;
    *Quotas = ApiMessage.LogonUser.Reply.Quotas;

    return Status;
}


/*
 * @implemented
 */
NTSTATUS
NTAPI
LsaRegisterLogonProcess(IN PLSA_STRING LogonProcessName,
                        OUT PHANDLE LsaHandle,
                        OUT PLSA_OPERATIONAL_MODE OperationalMode)
{
    LSA_CONNECTION_INFO ConnectInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING EventName;
    HANDLE EventHandle;
    NTSTATUS Status;

    DPRINT("LsaRegisterLogonProcess()\n");

    /* Check the logon process name length */
    if (LogonProcessName->Length > LSASS_MAX_LOGON_PROCESS_NAME_LENGTH)
        return STATUS_NAME_TOO_LONG;

    /* Wait for the LSA authentication thread */
    RtlInitUnicodeString(&EventName,
                         L"\\SECURITY\\LSA_AUTHENTICATION_INITIALIZED");
    InitializeObjectAttributes(&ObjectAttributes,
                               &EventName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               NULL);
    Status = ZwOpenEvent(&EventHandle,
                         SYNCHRONIZE,
                         &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("ZwOpenEvent failed (Status 0x%08lx)\n", Status);

        Status = ZwCreateEvent(&EventHandle,
                               SYNCHRONIZE,
                               &ObjectAttributes,
                               NotificationEvent,
                               FALSE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ZwCreateEvent failed (Status 0x%08lx)\n", Status);
            return Status;
        }
    }

    Status = ZwWaitForSingleObject(EventHandle,
                                   TRUE,
                                   NULL);
    ZwClose(EventHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ZwWaitForSingleObject failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    /* Establish the connection */
    strncpy(ConnectInfo.LogonProcessNameBuffer,
            LogonProcessName->Buffer,
            LogonProcessName->Length);
    ConnectInfo.Length = LogonProcessName->Length;
    ConnectInfo.LogonProcessNameBuffer[ConnectInfo.Length] = ANSI_NULL;
    ConnectInfo.CreateContext = TRUE;
    ConnectInfo.TrustedCaller = CHECK;

    Status = LsapClientConnect(LsaHandle, &ConnectInfo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("LsapClientConnect failed (Status 0x%08lx)\n", Status);
        return Status;
    }

    DPRINT("ConnectInfo.OperationalMode: 0x%08lx\n", ConnectInfo.OperationalMode);
    *OperationalMode = ConnectInfo.OperationalMode;

    if (!NT_SUCCESS(ConnectInfo.Status))
    {
        DPRINT1("ConnectInfo.Status: 0x%08lx\n", ConnectInfo.Status);
    }

    return ConnectInfo.Status;
}
