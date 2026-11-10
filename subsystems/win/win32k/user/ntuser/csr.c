/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Interface between Win32k and USERSRV
 * FILE:             win32ss/user/ntuser/csr.c
 * PROGRAMMER:       Hermes Belusca-Maito (hermes.belusca@sfr.fr), based on
 *                   the original code by Ge van Geldorp (ge@gse.nl) and by
 *                   the CSR code in NTDLL.
 */

#include <win32k.h>

DBG_DEFAULT_CHANNEL(UserCsr);

VOID
InitCsrProcess(PUSER_SESSION Session)
{
    /* Save the EPROCESS of CSRSS */
    Session->CsrProcess = PsGetCurrentProcess();
    ObReferenceObject(Session->CsrProcess);
}

VOID
ResetCsrProcess(PUSER_SESSION Session)
{
    if (Session->CsrProcess)
        ObDereferenceObject(Session->CsrProcess);

    Session->CsrProcess = NULL;
}

NTSTATUS
InitCsrApiPort(PUSER_SESSION Session, IN HANDLE CsrPortHandle)
{
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(CsrPortHandle,
                                       0,
                                       /* * */LpcPortObjectType, // or NULL,
                                       UserMode,
                                       &Session->CsrApiPort,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        Session->CsrApiPort = NULL;
        ERR("Failed to set CSR API Port.\n");
    }

    return Status;
}

VOID
ResetCsrApiPort(PUSER_SESSION Session)
{
    if (Session->CsrApiPort)
        ObDereferenceObject(Session->CsrApiPort);

    Session->CsrApiPort = NULL;
}

/*
 * Function copied from ntdll/csr/connect.c::CsrClientCallServer
 * and adapted for kernel-mode.
 *
 * NOTE: This is really a co_* function!
 */
NTSTATUS
NTAPI
CsrClientCallServer(IN OUT PCSR_API_MESSAGE ApiMessage,
                    IN OUT PCSR_CAPTURE_BUFFER CaptureBuffer OPTIONAL,
                    IN CSR_API_NUMBER ApiNumber,
                    IN ULONG DataLength)
{
    NTSTATUS Status;
    SIZE_T BufferLength = sizeof(*ApiMessage);
    PUSER_SESSION Session = UserGetCurrentSession();
#if 0
    ULONG PointerCount;
    PULONG_PTR OffsetPointer;
#endif

    /* Do we have a connection to CSR yet? */
    if (!Session || !Session->CsrApiPort)
        return STATUS_INVALID_PORT_HANDLE;

    /* Fill out the Port Message Header */
    ApiMessage->Header.u2.ZeroInit = 0;
    RtlZeroMemory(&ApiMessage->Header.ClientId, sizeof(ApiMessage->Header) - FIELD_OFFSET(PORT_MESSAGE, ClientId));
    ApiMessage->Header.u1.s1.TotalLength = FIELD_OFFSET(CSR_API_MESSAGE, Data) + DataLength;
    ApiMessage->Header.u1.s1.DataLength = ApiMessage->Header.u1.s1.TotalLength -
        sizeof(ApiMessage->Header);

    /* Fill out the CSR Header */
    ApiMessage->ApiNumber = ApiNumber;
    ApiMessage->CsrCaptureData = NULL;

    TRACE("API: %lx, u1.s1.DataLength: %x, u1.s1.TotalLength: %x\n",
          ApiNumber,
          ApiMessage->Header.u1.s1.DataLength,
          ApiMessage->Header.u1.s1.TotalLength);

#if 0
    /* Check if we got a Capture Buffer */
    if (CaptureBuffer)
    {
        /*
         * We have to convert from our local (client) view
         * to the remote (server) view.
         */
        ApiMessage->CsrCaptureData = (PCSR_CAPTURE_BUFFER)
            ((ULONG_PTR)CaptureBuffer + CsrPortMemoryDelta);

        /* Lock the buffer. */
        CaptureBuffer->BufferEnd = NULL;

        /*
         * Each client pointer inside the CSR message is converted into
         * a server pointer, and each pointer to these message pointers
         * is converted into an offset.
         */
        PointerCount  = CaptureBuffer->PointerCount;
        OffsetPointer = CaptureBuffer->PointerOffsetsArray;
        while (PointerCount--)
        {
            if (*OffsetPointer != 0)
            {
                *(PULONG_PTR)*OffsetPointer += CsrPortMemoryDelta;
                *OffsetPointer -= (ULONG_PTR)ApiMessage;
            }
            ++OffsetPointer;
        }
    }
#endif

    UserLeaveCo();

    /* Send the LPC Message */

    // The wait logic below is subject to change in the future. One can
    // imagine adding an external parameter to CsrClientCallServer, or write
    // two versions of CsrClientCallServer, synchronous and asynchronous.
    if (PsGetCurrentProcess() == Session->CsrProcess)
    {
        Status = LpcRequestPort(Session->CsrApiPort,
                                &ApiMessage->Header);
    }
    else
    {
        Status = LpcSendWaitReceivePort(Session->CsrApiPort, ALPC_MSGFLG_SYNC_REQUEST, &ApiMessage->Header, &ApiMessage->Header, &BufferLength, NULL);
    }

    UserEnterCo();

#if 0
    /* Check if we got a Capture Buffer */
    if (CaptureBuffer)
    {
        /*
         * We have to convert back from the remote (server) view
         * to our local (client) view.
         */
        ApiMessage->CsrCaptureData = (PCSR_CAPTURE_BUFFER)
            ((ULONG_PTR)ApiMessage->CsrCaptureData - CsrPortMemoryDelta);

        /*
         * Convert back the offsets into pointers to CSR message
         * pointers, and convert back these message server pointers
         * into client pointers.
         */
        PointerCount  = CaptureBuffer->PointerCount;
        OffsetPointer = CaptureBuffer->PointerOffsetsArray;
        while (PointerCount--)
        {
            if (*OffsetPointer != 0)
            {
                *OffsetPointer += (ULONG_PTR)ApiMessage;
                *(PULONG_PTR)*OffsetPointer -= CsrPortMemoryDelta;
            }
            ++OffsetPointer;
        }
    }
#endif

    /* Check for success */
    if (!NT_SUCCESS(Status))
    {
        /* We failed. Overwrite the return value with the failure. */
        ERR("LPC Failed: %lx\n", Status);
        ApiMessage->Status = Status;
    }

    /* Return the CSR Result */
    TRACE("Got back: 0x%lx\n", ApiMessage->Status);
    return ApiMessage->Status;
}

/*
 * UserSystemThreadProc
 *
 * Called form dedicated thread in CSRSS. RIT is started in context of this
 * thread because it needs valid Win32 process with TEB initialized.
 */
DWORD UserSystemThreadProc(BOOL bRemoteProcess)
{
    PUSER_SESSION Session = UserGetCurrentSession();
    DWORD Type;

    if (!Session || !Session->PendingSystemThreads)
    {
        ERR("PendingSystemThreads is 0!\n");
        return 0;
    }

    /* Decide which thread this will be */
    if (Session->PendingSystemThreads & ST_RIT)
        Type = ST_RIT;
    else if (Session->PendingSystemThreads & ST_DESKTOP_THREAD)
        Type = ST_DESKTOP_THREAD;
    else
        Type = ST_GHOST_THREAD;

    ASSERT(Type);

    /* We will handle one of these threads right here so unmark it as pending */
    Session->PendingSystemThreads &= ~Type;

    UserLeave();

    TRACE("UserSystemThreadProc: %d\n", Type);

    switch (Type)
    {
        case ST_RIT: RawInputThreadMain(); break;
        case ST_DESKTOP_THREAD: DesktopThreadMain(); break;
        case ST_GHOST_THREAD: UserGhostThreadEntry(); break;
        default: ERR("Wrong type: %x\n", Type);
    }

    UserEnterShared();

    return 0;
}

BOOL UserCreateSystemThread(DWORD Type)
{
    USER_API_MESSAGE ApiMessage;
    PUSER_CREATE_SYSTEM_THREAD pCreateThreadRequest = &ApiMessage.Data.CreateSystemThreadRequest;
    PUSER_SESSION Session = UserGetCurrentSession();

    TRACE("UserCreateSystemThread: %d\n", Type);

    ASSERT(UserIsEnteredExclusive());

    if (!Session)
        return FALSE;

    if (Session->PendingSystemThreads & Type)
    {
        ERR("System thread 0x%x already pending for creation\n", Type);
        return TRUE;
    }

    /* We can't pass a parameter to the new thread so mark what the new thread needs to do */
    Session->PendingSystemThreads |= Type;

    /* Ask winsrv to create a new system thread. This new thread will enter win32k again calling UserSystemThreadProc */
    pCreateThreadRequest->bRemote = FALSE;
    CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                        NULL,
                        CSR_CREATE_API_NUMBER(USERSRV_SERVERDLL_INDEX, UserpCreateSystemThreads),
                        sizeof(USER_CREATE_SYSTEM_THREAD));
    if (!NT_SUCCESS(ApiMessage.Status))
    {
        ERR("Csr call failed!\n");
        return FALSE;
    }

    return TRUE;
}

/* EOF */
