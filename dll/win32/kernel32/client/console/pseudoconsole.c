/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pseudo console client
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

static
NTSTATUS
BasepCallPseudoConsoleServer(
    _In_ CONSRV_API_NUMBER ApiNumber,
    _In_ HANDLE ConsoleHandle,
    _In_ COORD Size)
{
    CONSOLE_API_MESSAGE ApiMessage;
    PCONSOLE_PSEUDOCONSOLE Request = &ApiMessage.Data.PseudoConsoleRequest;

    RtlZeroMemory(Request, sizeof(*Request));
    Request->ConsoleHandle = ConsoleHandle;
    Request->Size = Size;
    CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                        NULL,
                        CSR_CREATE_API_NUMBER(CONSRV_SERVERDLL_INDEX, ApiNumber),
                        sizeof(*Request));
    return ApiMessage.Status;
}

HRESULT
WINAPI
CreatePseudoConsole(
    _In_ COORD Size,
    _In_ HANDLE Input,
    _In_ HANDLE Output,
    _In_ DWORD Flags,
    _Out_ HPCON *PseudoConsole)
{
    PUNICODE_STRING Desktop = &NtCurrentPeb()->ProcessParameters->DesktopInfo;
    CONSOLE_API_MESSAGE ApiMessage;
    PCONSOLE_PSEUDOCONSOLE Request = &ApiMessage.Data.PseudoConsoleRequest;
    PCSR_CAPTURE_BUFFER CaptureBuffer = NULL;
    PBASE_PSEUDO_CONSOLE Pty;

    if (!PseudoConsole || Size.X <= 0 || Size.Y <= 0)
        return E_INVALIDARG;

    Pty = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Pty));
    if (!Pty)
        return E_OUTOFMEMORY;

    RtlZeroMemory(Request, sizeof(*Request));
    Request->InputHandle = Input;
    Request->OutputHandle = Output;
    Request->Size = Size;
    Request->Flags = Flags;

    if (Desktop->Buffer && Desktop->Length)
    {
        Request->DesktopLength = min(Desktop->Length + sizeof(WCHAR), (MAX_PATH + 1) * sizeof(WCHAR));
        CaptureBuffer = CsrAllocateCaptureBuffer(1, Request->DesktopLength);
        if (!CaptureBuffer)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, Pty);
            return E_OUTOFMEMORY;
        }

        CsrCaptureMessageBuffer(CaptureBuffer,
                                Desktop->Buffer,
                                Request->DesktopLength,
                                (PVOID*)&Request->Desktop);
    }

    CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                        CaptureBuffer,
                        CSR_CREATE_API_NUMBER(CONSRV_SERVERDLL_INDEX, ConsolepCreatePseudoConsole),
                        sizeof(*Request));
    if (CaptureBuffer)
        CsrFreeCaptureBuffer(CaptureBuffer);

    if (!NT_SUCCESS(ApiMessage.Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Pty);
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(ApiMessage.Status));
    }

    Pty->ConsoleHandle = Request->ConsoleHandle;
    *PseudoConsole = (HPCON)Pty;
    return S_OK;
}

HRESULT
WINAPI
ResizePseudoConsole(
    _In_ HPCON PseudoConsole,
    _In_ COORD Size)
{
    PBASE_PSEUDO_CONSOLE Pty = (PBASE_PSEUDO_CONSOLE)PseudoConsole;
    NTSTATUS Status;

    if (!Pty || Size.X <= 0 || Size.Y <= 0)
        return E_INVALIDARG;

    Status = BasepCallPseudoConsoleServer(ConsolepResizePseudoConsole, Pty->ConsoleHandle, Size);
    if (!NT_SUCCESS(Status))
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));

    return S_OK;
}

VOID
WINAPI
ClosePseudoConsole(
    _In_ HPCON PseudoConsole)
{
    PBASE_PSEUDO_CONSOLE Pty = (PBASE_PSEUDO_CONSOLE)PseudoConsole;
    COORD Size = { 0, 0 };

    if (!Pty)
        return;

    BasepCallPseudoConsoleServer(ConsolepClosePseudoConsole, Pty->ConsoleHandle, Size);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Pty);
}
