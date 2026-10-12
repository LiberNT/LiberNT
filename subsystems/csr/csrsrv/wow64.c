/*
 * PROJECT:     LiberNT Client/Server Runtime SubSystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WoW64 CSR API message layouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "srv.h"

#include <wingdi.h>
#include <wincon.h>
#include <wincon_undoc.h>
#include <reactos/subsys/csr/csrwow64.h>
#include <reactos/subsys/win/basemsg.h>
#include <reactos/subsys/win/winmsg.h>

#define NDEBUG
#include <debug.h>

typedef enum _CSR_WOW64_FIELD_TYPE
{
    CsrWow64Data,
    CsrWow64Handle,
    CsrWow64Pointer
} CSR_WOW64_FIELD_TYPE;

typedef struct _CSR_WOW64_FIELD
{
    USHORT Offset;
    USHORT Size;
    CSR_WOW64_FIELD_TYPE Type;
} CSR_WOW64_FIELD;

typedef struct _CSR_WOW64_MESSAGE
{
    const CSR_WOW64_FIELD *Fields;
    ULONG FieldCount;
    ULONG Size;
} CSR_WOW64_MESSAGE;

#define CSR_WOW64_DATA(Type, First, Last) \
    { FIELD_OFFSET(Type, First), FIELD_OFFSET(Type, Last) + RTL_FIELD_SIZE(Type, Last) - FIELD_OFFSET(Type, First), CsrWow64Data }
#define CSR_WOW64_HANDLE(Type, Field) { FIELD_OFFSET(Type, Field), sizeof(ULONG_PTR), CsrWow64Handle }
#define CSR_WOW64_POINTER(Type, Field) { FIELD_OFFSET(Type, Field), sizeof(ULONG_PTR), CsrWow64Pointer }
#define CSR_WOW64_MESSAGE(Type, Fields) { Fields, RTL_NUMBER_OF(Fields), sizeof(Type) }

static const CSR_WOW64_FIELD CsrpWow64DefineDosDevice[] =
{
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, Flags, Flags),
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, DeviceName.Length, DeviceName.MaximumLength),
    CSR_WOW64_POINTER(BASE_DEFINE_DOS_DEVICE, DeviceName.Buffer),
    CSR_WOW64_DATA(BASE_DEFINE_DOS_DEVICE, TargetPath.Length, TargetPath.MaximumLength),
    CSR_WOW64_POINTER(BASE_DEFINE_DOS_DEVICE, TargetPath.Buffer),
};

static const CSR_WOW64_MESSAGE CsrpWow64BaseMessages[BasepMaxApiNumber - BASESRV_FIRST_API_NUMBER] =
{
    [BasepDefineDosDevice - BASESRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(BASE_DEFINE_DOS_DEVICE, CsrpWow64DefineDosDevice),
};

static const CSR_WOW64_FIELD CsrpWow64EndTask[] =
{
    CSR_WOW64_DATA(USER_END_TASK, LastError, LastError),
    CSR_WOW64_HANDLE(USER_END_TASK, WndHandle),
    CSR_WOW64_DATA(USER_END_TASK, Force, Success),
};

static const CSR_WOW64_FIELD CsrpWow64GetThreadConsoleDesktop[] =
{
    CSR_WOW64_POINTER(USER_GET_THREAD_CONSOLE_DESKTOP, ThreadId),
    CSR_WOW64_HANDLE(USER_GET_THREAD_CONSOLE_DESKTOP, ConsoleDesktop),
};

static const CSR_WOW64_MESSAGE CsrpWow64UserMessages[UserpMaxApiNumber - USERSRV_FIRST_API_NUMBER] =
{
    [UserpEndTask - USERSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(USER_END_TASK, CsrpWow64EndTask),
    [UserpGetThreadConsoleDesktop - USERSRV_FIRST_API_NUMBER] = CSR_WOW64_MESSAGE(USER_GET_THREAD_CONSOLE_DESKTOP, CsrpWow64GetThreadConsoleDesktop),
};

static const CSR_WOW64_MESSAGE *
CsrpWow64GetMessage(IN PCSR_API_MESSAGE ApiMessage)
{
    ULONG ApiId = CSR_API_NUMBER_TO_API_ID(ApiMessage->ApiNumber);
    const CSR_WOW64_MESSAGE *Messages;
    ULONG Count;

    switch (CSR_API_NUMBER_TO_SERVER_ID(ApiMessage->ApiNumber))
    {
        case BASESRV_SERVERDLL_INDEX:
            Messages = CsrpWow64BaseMessages;
            Count = RTL_NUMBER_OF(CsrpWow64BaseMessages);
            ApiId -= BASESRV_FIRST_API_NUMBER;
            break;

        case USERSRV_SERVERDLL_INDEX:
            Messages = CsrpWow64UserMessages;
            Count = RTL_NUMBER_OF(CsrpWow64UserMessages);
            ApiId -= USERSRV_FIRST_API_NUMBER;
            break;

        default:
            return NULL;
    }

    if (ApiId >= Count || !Messages[ApiId].Fields) return NULL;
    return &Messages[ApiId];
}

static ULONG
CsrpWow64GetOffset32(IN const CSR_WOW64_FIELD *Field, IN OUT PULONG End32)
{
    ULONG Offset32 = (Field->Type == CsrWow64Data) ? *End32 : ALIGN_UP_BY(*End32, sizeof(ULONG));

    *End32 = Offset32 + ((Field->Type == CsrWow64Data) ? Field->Size : sizeof(ULONG));
    return Offset32;
}

static ULONG
CsrpWow64ConvertMessage(IN const CSR_WOW64_MESSAGE *Message, IN PUCHAR Data64, IN PUCHAR Data32, IN BOOLEAN ToServer)
{
    ULONG Index, End32 = 0;

    for (Index = 0; Index < Message->FieldCount; ++Index)
    {
        const CSR_WOW64_FIELD *Field = &Message->Fields[Index];
        PUCHAR Field32 = Data32 + CsrpWow64GetOffset32(Field, &End32);
        PUCHAR Field64 = Data64 + Field->Offset;

        if (Field->Type == CsrWow64Data)
        {
            if (ToServer) RtlCopyMemory(Field64, Field32, Field->Size);
            else RtlCopyMemory(Field32, Field64, Field->Size);
        }
        else if (!ToServer)
        {
            *(PULONG)Field32 = (ULONG)*(PULONG_PTR)Field64;
        }
        else if (Field->Type == CsrWow64Handle)
        {
            *(PULONG_PTR)Field64 = (ULONG_PTR)(LONG_PTR)*(PLONG)Field32;
        }
        else
        {
            *(PULONG_PTR)Field64 = *(PULONG)Field32;
        }
    }
    return ALIGN_UP_BY(End32, sizeof(ULONG));
}

VOID
NTAPI
CsrWow64MessageToServer(IN OUT PCSR_API_MESSAGE ApiMessage)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    UCHAR Data32[RTL_FIELD_SIZE(CSR_API_MESSAGE32, Data)];

    if (!Message) return;
    RtlCopyMemory(Data32, &ApiMessage->Data, sizeof(Data32));
    RtlZeroMemory(&ApiMessage->Data, Message->Size);
    CsrpWow64ConvertMessage(Message, (PUCHAR)&ApiMessage->Data, Data32, TRUE);
    ApiMessage->Header.u1.s1.TotalLength = (CSHORT)(FIELD_OFFSET(CSR_API_MESSAGE, Data) + Message->Size);
    ApiMessage->Header.u1.s1.DataLength = ApiMessage->Header.u1.s1.TotalLength - sizeof(PORT_MESSAGE);
}

VOID
NTAPI
CsrWow64MessageToClient(IN OUT PCSR_API_MESSAGE ApiMessage)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    UCHAR Data64[RTL_FIELD_SIZE(CSR_API_MESSAGE, Data)];
    ULONG Size32;

    if (!Message) return;
    RtlCopyMemory(Data64, &ApiMessage->Data, Message->Size);
    Size32 = CsrpWow64ConvertMessage(Message, Data64, (PUCHAR)&ApiMessage->Data, FALSE);
    ApiMessage->Header.u1.s1.TotalLength = (CSHORT)(FIELD_OFFSET(CSR_API_MESSAGE, Data) + Size32);
    ApiMessage->Header.u1.s1.DataLength = ApiMessage->Header.u1.s1.TotalLength - sizeof(PORT_MESSAGE);
}

ULONG_PTR
NTAPI
CsrWow64GetMessagePointerOffset(IN PCSR_API_MESSAGE ApiMessage, IN ULONG Offset32)
{
    const CSR_WOW64_MESSAGE *Message = CsrpWow64GetMessage(ApiMessage);
    ULONG Index, End32 = 0;

    if (!Message) return MAXULONG_PTR;
    for (Index = 0; Index < Message->FieldCount; ++Index)
    {
        const CSR_WOW64_FIELD *Field = &Message->Fields[Index];
        ULONG FieldOffset32 = CsrpWow64GetOffset32(Field, &End32);

        if (Field->Type == CsrWow64Pointer && FIELD_OFFSET(CSR_API_MESSAGE32, Data) + FieldOffset32 == Offset32)
            return FIELD_OFFSET(CSR_API_MESSAGE, Data) + Field->Offset;
    }
    return MAXULONG_PTR;
}
