/*
 * PROJECT:     LiberNT OpenGL thunk
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native OpenGL ICD reached by the ICD of emulated AMD64 processes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <libernt/emuhost.h>
#include "glthunk.h"
#include "glsig.h"

#define GLHOST_CALLBACKS 3

typedef PVOID DHGLRC;

typedef struct _GLHOST_PROC_TABLE
{
    INT Entries;
    PVOID Table[GLTHUNK_TABLE_ENTRIES];
} GLHOST_PROC_TABLE;

typedef struct _GLHOST_GUEST_TABLE
{
    INT Entries;
    ULONG64 Table[GLTHUNK_TABLE_ENTRIES];
} GLHOST_GUEST_TABLE;

typedef struct _GLHOST_DEBUG
{
    ULONG64 Callback;
    ULONG64 User;
} GLHOST_DEBUG;

typedef VOID (APIENTRY *PGLHOST_DEBUG_MESSAGE_CALLBACK)(PVOID Callback, const VOID *User);

BOOL APIENTRY DrvValidateVersion(ULONG Version);
VOID APIENTRY DrvSetCallbackProcs(INT Count, PROC *Procedures);
BOOL APIENTRY DrvCopyContext(DHGLRC Source, DHGLRC Destination, UINT Mask);
DHGLRC APIENTRY DrvCreateContext(HDC Dc);
DHGLRC APIENTRY DrvCreateLayerContext(HDC Dc, INT Layer);
BOOL APIENTRY DrvDeleteContext(DHGLRC Context);
BOOL APIENTRY DrvDescribeLayerPlane(HDC Dc, INT Format, INT Layer, UINT Size, LPLAYERPLANEDESCRIPTOR Descriptor);
LONG APIENTRY DrvDescribePixelFormat(HDC Dc, INT Format, ULONG Size, PIXELFORMATDESCRIPTOR *Descriptor);
int APIENTRY DrvGetLayerPaletteEntries(HDC Dc, INT Layer, INT Start, INT Count, COLORREF *Entries);
PROC APIENTRY DrvGetProcAddress(LPCSTR Name);
BOOL APIENTRY DrvReleaseContext(DHGLRC Context);
BOOL APIENTRY DrvRealizeLayerPalette(HDC Dc, INT Layer, BOOL Realize);
const GLHOST_PROC_TABLE *APIENTRY DrvSetContext(HDC Dc, DHGLRC Context, PVOID Callback);
int APIENTRY DrvSetLayerPaletteEntries(HDC Dc, INT Layer, INT Start, INT Count, CONST COLORREF *Entries);
BOOL APIENTRY DrvSetPixelFormat(HDC Dc, LONG Format);
BOOL APIENTRY DrvShareLists(DHGLRC First, DHGLRC Second);
BOOL APIENTRY DrvSwapBuffers(HDC Dc);
BOOL APIENTRY DrvSwapLayerBuffers(HDC Dc, UINT Planes);

static const GLTHUNK_SIGNATURE GlHostWglSignatures[] =
{
    { "glAddSwapHintRectWIN", "vwwww" },
    { "wglBindTexImageARB", "wqw" },
    { "wglChoosePixelFormatARB", "wqqqwqq" },
    { "wglCreateContextAttribsARB", "qqqq" },
    { "wglCreatePbufferARB", "qqwwwq" },
    { "wglDestroyPbufferARB", "wq" },
    { "wglGetCurrentReadDCARB", "q" },
    { "wglGetExtensionsStringARB", "qq" },
    { "wglGetExtensionsStringEXT", "q" },
    { "wglGetPbufferDCARB", "qq" },
    { "wglGetPixelFormatAttribfvARB", "wqwwwqq" },
    { "wglGetPixelFormatAttribivARB", "wqwwwqq" },
    { "wglGetSwapIntervalEXT", "w" },
    { "wglMakeContextCurrentARB", "wqqq" },
    { "wglQueryPbufferARB", "wqwq" },
    { "wglReleasePbufferDCARB", "wqq" },
    { "wglReleaseTexImageARB", "wqw" },
    { "wglSetPbufferAttribARB", "wqq" },
    { "wglSwapIntervalEXT", "ww" },
};

static GLTHUNK_TABLE GlHostTable;
static RTL_RUN_ONCE GlHostOnce = RTL_RUN_ONCE_INIT;
static RTL_SRWLOCK GlHostLock = RTL_SRWLOCK_INIT;
static const GLHOST_PROC_TABLE *GlHostNativeTable;
static GLHOST_GUEST_TABLE GlHostGuestTable;
static ULONG64 GlHostCallbacks[GLHOST_CALLBACKS];
static PGLHOST_DEBUG_MESSAGE_CALLBACK GlHostDebugMessageCallback;
static PGLHOST_DEBUG_MESSAGE_CALLBACK GlHostDebugMessageCallbackARB;
static PGLHOST_DEBUG_MESSAGE_CALLBACK GlHostDebugMessageCallbackKHR;
static PGLHOST_DEBUG_MESSAGE_CALLBACK GlHostDebugMessageCallbackAMD;
static BOOLEAN GlHostRv64X;

static
const EMU_HOST_INTERFACE *
GlHostInterface(VOID)
{
    return EmuHostGetInterface();
}

static
ULONG64
GlHostThunk(
    _In_ PVOID Native,
    _In_ PCSTR Signature)
{
    ULONG64 Stub = 0;

    if (!Native) return 0;
    if (GlHostRv64X) Stub = GlHostInterface()->GuestThunk(Native, NULL);
    if (!Stub && Signature) Stub = GlHostInterface()->GuestThunk(Native, Signature);
    return Stub;
}

static
PCSTR
GlHostFindSignature(
    _In_ const GLTHUNK_SIGNATURE *Table,
    _In_ ULONG Count,
    _In_ PCSTR Name)
{
    ULONG Low = 0, High = Count, Middle;
    int Order;

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        Order = strcmp(Name, Table[Middle].Name);
        if (!Order) return Table[Middle].Signature;
        if (Order < 0) High = Middle;
        else Low = Middle + 1;
    }
    return NULL;
}

static
VOID
APIENTRY
GlHostSetCurrentValue(
    _In_opt_ PVOID Value)
{
    ULONG64 Arguments[1] = { (ULONG64)Value };

    GlHostInterface()->CallGuest(GlHostCallbacks[0], "vq", Arguments);
}

static
PVOID
APIENTRY
GlHostGetCurrentValue(VOID)
{
    return (PVOID)GlHostInterface()->CallGuest(GlHostCallbacks[1], "q", NULL);
}

static
DHGLRC
APIENTRY
GlHostGetDhglrc(
    _In_ PVOID Context)
{
    ULONG64 Arguments[1] = { (ULONG64)Context };

    return (DHGLRC)GlHostInterface()->CallGuest(GlHostCallbacks[2], "qq", Arguments);
}

static
VOID
APIENTRY
GlHostSetCallbackProcs(
    _In_ INT Count,
    _In_reads_(Count) PROC *Procedures)
{
    static PROC Native[GLHOST_CALLBACKS] =
    {
        (PROC)GlHostSetCurrentValue,
        (PROC)GlHostGetCurrentValue,
        (PROC)GlHostGetDhglrc
    };
    INT Index;

    if (GlHostRv64X)
    {
        DrvSetCallbackProcs(Count, Procedures);
        return;
    }
    if (Count < GLHOST_CALLBACKS) return;
    for (Index = 0; Index < GLHOST_CALLBACKS; Index++) GlHostCallbacks[Index] = (ULONG64)Procedures[Index];
    DrvSetCallbackProcs(GLHOST_CALLBACKS, Native);
}

static
ULONG64
APIENTRY
GlHostSetContext(
    _In_ HDC Dc,
    _In_ DHGLRC Context,
    _In_opt_ PVOID Callback)
{
    const GLHOST_PROC_TABLE *Native = DrvSetContext(Dc, Context, NULL);
    INT Index;

    if (!Native) return 0;

    RtlAcquireSRWLockExclusive(&GlHostLock);
    if (Native != GlHostNativeTable)
    {
        GlHostGuestTable.Entries = min(Native->Entries, GLTHUNK_TABLE_ENTRIES);
        for (Index = 0; Index < GlHostGuestTable.Entries; Index++)
            GlHostGuestTable.Table[Index] = GlHostThunk(Native->Table[Index], GlThunkTableSignatures[Index]);
        GlHostNativeTable = Native;
    }
    RtlReleaseSRWLockExclusive(&GlHostLock);
    return (ULONG64)&GlHostGuestTable;
}

static
VOID
APIENTRY
GlHostDebugMessage(
    _In_ ULONG Source,
    _In_ ULONG Type,
    _In_ ULONG Id,
    _In_ ULONG Severity,
    _In_ INT Length,
    _In_ const CHAR *Message,
    _In_ const VOID *User)
{
    const GLHOST_DEBUG *Debug = User;
    ULONG64 Arguments[7] = { Source, Type, Id, Severity, (ULONG64)(LONG64)Length, (ULONG64)Message, Debug->User };

    GlHostInterface()->CallGuest(Debug->Callback, "vwwwwwqq", Arguments);
}

static
VOID
APIENTRY
GlHostDebugMessageAMD(
    _In_ ULONG Id,
    _In_ ULONG Category,
    _In_ ULONG Severity,
    _In_ INT Length,
    _In_ const CHAR *Message,
    _In_ PVOID User)
{
    const GLHOST_DEBUG *Debug = User;
    ULONG64 Arguments[6] = { Id, Category, Severity, (ULONG64)(LONG64)Length, (ULONG64)Message, Debug->User };

    GlHostInterface()->CallGuest(Debug->Callback, "vwwwwqq", Arguments);
}

/* FIXME: the records of replaced debug callbacks are never freed, and GL_DEBUG_CALLBACK_* queries return them */
static
VOID
GlHostSetDebugCallback(
    _In_ PGLHOST_DEBUG_MESSAGE_CALLBACK Native,
    _In_ PVOID Trampoline,
    _In_opt_ PVOID Callback,
    _In_opt_ const VOID *User)
{
    GLHOST_DEBUG *Debug = NULL;

    if (Callback)
    {
        Debug = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Debug));
        if (!Debug) return;
        Debug->Callback = (ULONG64)Callback;
        Debug->User = (ULONG64)User;
    }
    Native(Debug ? Trampoline : NULL, Debug);
}

static
VOID
APIENTRY
GlHostDebugMessageCallbackCore(
    _In_opt_ PVOID Callback,
    _In_opt_ const VOID *User)
{
    GlHostSetDebugCallback(GlHostDebugMessageCallback, (PVOID)GlHostDebugMessage, Callback, User);
}

static
VOID
APIENTRY
GlHostDebugMessageCallbackArb(
    _In_opt_ PVOID Callback,
    _In_opt_ const VOID *User)
{
    GlHostSetDebugCallback(GlHostDebugMessageCallbackARB, (PVOID)GlHostDebugMessage, Callback, User);
}

static
VOID
APIENTRY
GlHostDebugMessageCallbackKhr(
    _In_opt_ PVOID Callback,
    _In_opt_ const VOID *User)
{
    GlHostSetDebugCallback(GlHostDebugMessageCallbackKHR, (PVOID)GlHostDebugMessage, Callback, User);
}

static
VOID
APIENTRY
GlHostDebugMessageCallbackAmd(
    _In_opt_ PVOID Callback,
    _In_opt_ const VOID *User)
{
    GlHostSetDebugCallback(GlHostDebugMessageCallbackAMD, (PVOID)GlHostDebugMessageAMD, Callback, User);
}

static
ULONG64
APIENTRY
GlHostGetProcAddress(
    _In_ LPCSTR Name)
{
    PVOID Native = (PVOID)DrvGetProcAddress(Name);
    PCSTR Signature;

    if (!Native) return 0;

    if (GlHostRv64X)
    {
        Signature = GlHostFindSignature(GlThunkSignatures, GlThunkSignatureCount, Name);
        if (!Signature) Signature = GlHostFindSignature(GlHostWglSignatures, RTL_NUMBER_OF(GlHostWglSignatures), Name);
        return GlHostThunk(Native, Signature);
    }

    if (!strcmp(Name, "glDebugMessageCallback"))
    {
        GlHostDebugMessageCallback = Native;
        return GlHostThunk((PVOID)GlHostDebugMessageCallbackCore, "vqq");
    }
    if (!strcmp(Name, "glDebugMessageCallbackARB"))
    {
        GlHostDebugMessageCallbackARB = Native;
        return GlHostThunk((PVOID)GlHostDebugMessageCallbackArb, "vqq");
    }
    if (!strcmp(Name, "glDebugMessageCallbackKHR"))
    {
        GlHostDebugMessageCallbackKHR = Native;
        return GlHostThunk((PVOID)GlHostDebugMessageCallbackKhr, "vqq");
    }
    if (!strcmp(Name, "glDebugMessageCallbackAMD"))
    {
        GlHostDebugMessageCallbackAMD = Native;
        return GlHostThunk((PVOID)GlHostDebugMessageCallbackAmd, "vqq");
    }

    Signature = GlHostFindSignature(GlThunkSignatures, GlThunkSignatureCount, Name);
    if (!Signature) Signature = GlHostFindSignature(GlHostWglSignatures, RTL_NUMBER_OF(GlHostWglSignatures), Name);
    if (!Signature && Name[0] == 'g' && Name[1] == 'l') Signature = "v";
    return Signature ? GlHostThunk(Native, Signature) : 0;
}

static
ULONG
NTAPI
GlHostInitializeOnce(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _Inout_opt_ PVOID Parameter,
    _Inout_opt_ PVOID *Context)
{
    GLTHUNK_TABLE Table = { sizeof(GLTHUNK_TABLE) };

    GlHostRv64X = GlHostInterface()->GuestThunk((PVOID)DrvSetCallbackProcs, NULL) != 0;
    Table.DrvValidateVersion = GlHostThunk((PVOID)DrvValidateVersion, "ww");
    Table.DrvSetCallbackProcs = GlHostThunk((PVOID)GlHostSetCallbackProcs, "vwq");
    Table.DrvCopyContext = GlHostThunk((PVOID)DrvCopyContext, "wqqw");
    Table.DrvCreateContext = GlHostThunk((PVOID)DrvCreateContext, "qq");
    Table.DrvCreateLayerContext = GlHostThunk((PVOID)DrvCreateLayerContext, "qqw");
    Table.DrvDeleteContext = GlHostThunk((PVOID)DrvDeleteContext, "wq");
    Table.DrvDescribeLayerPlane = GlHostThunk((PVOID)DrvDescribeLayerPlane, "wqwwwq");
    Table.DrvDescribePixelFormat = GlHostThunk((PVOID)DrvDescribePixelFormat, "wqwwq");
    Table.DrvGetLayerPaletteEntries = GlHostThunk((PVOID)DrvGetLayerPaletteEntries, "wqwwwq");
    Table.DrvGetProcAddress = GlHostThunk((PVOID)GlHostGetProcAddress, "qq");
    Table.DrvReleaseContext = GlHostThunk((PVOID)DrvReleaseContext, "wq");
    Table.DrvRealizeLayerPalette = GlHostThunk((PVOID)DrvRealizeLayerPalette, "wqww");
    Table.DrvSetContext = GlHostThunk((PVOID)GlHostSetContext, "qqqq");
    Table.DrvSetLayerPaletteEntries = GlHostThunk((PVOID)DrvSetLayerPaletteEntries, "wqwwwq");
    Table.DrvSetPixelFormat = GlHostThunk((PVOID)DrvSetPixelFormat, "wqw");
    Table.DrvShareLists = GlHostThunk((PVOID)DrvShareLists, "wqq");
    Table.DrvSwapBuffers = GlHostThunk((PVOID)DrvSwapBuffers, "wq");
    Table.DrvSwapLayerBuffers = GlHostThunk((PVOID)DrvSwapLayerBuffers, "wqw");

    if (!Table.DrvValidateVersion || !Table.DrvSetCallbackProcs || !Table.DrvCopyContext || !Table.DrvCreateContext ||
        !Table.DrvCreateLayerContext || !Table.DrvDeleteContext || !Table.DrvDescribeLayerPlane ||
        !Table.DrvDescribePixelFormat || !Table.DrvGetLayerPaletteEntries || !Table.DrvGetProcAddress ||
        !Table.DrvReleaseContext || !Table.DrvRealizeLayerPalette || !Table.DrvSetContext ||
        !Table.DrvSetLayerPaletteEntries || !Table.DrvSetPixelFormat || !Table.DrvShareLists ||
        !Table.DrvSwapBuffers || !Table.DrvSwapLayerBuffers)
    {
        return FALSE;
    }

    GlHostTable = Table;
    return TRUE;
}

__declspec(dllexport)
ULONG64
NTAPI
EmuHostInitialize(VOID)
{
    if (!NT_SUCCESS(RtlRunOnceExecuteOnce(&GlHostOnce, GlHostInitializeOnce, NULL, NULL))) return 0;
    return (ULONG64)&GlHostTable;
}
