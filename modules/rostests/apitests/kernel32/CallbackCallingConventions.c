/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Check public callback calling conventions
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"
#include <compressapi.h>
#include <davclnt.h>
#include <perflib.h>

static LONG FreeCalls;

static void * CDECL CompressAllocate(void *Context, SIZE_T Size)
{
    return Size == 123 ? Context : NULL;
}

static void CDECL CompressFree(void *Context, void *Memory)
{
    if (Context == Memory) InterlockedIncrement(&FreeCalls);
}

static void * CALLBACK PerfAllocate(SIZE_T Size, void *Context)
{
    return Size == 123 ? Context : NULL;
}

static void CALLBACK PerfFree(void *Memory, void *Context)
{
    if (Context == Memory) InterlockedIncrement(&FreeCalls);
}

static DWORD CALLBACK DavFree(PVOID Buffer)
{
    return Buffer ? 17 : 0;
}

static DWORD CALLBACK DavAuth(LPWSTR Server, LPWSTR Remote, DWORD Scheme, DWORD Flags,
                              PDAV_CALLBACK_CRED Credential, AUTHNEXTSTEP *Next,
                              PFNDAVAUTHCALLBACK_FREECRED *Free)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Remote);
    UNREFERENCED_PARAMETER(Scheme);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Credential);
    *Next = RetryRequest;
    *Free = DavFree;
    return 19;
}

START_TEST(CallbackCallingConventions)
{
    PFN_COMPRESS_ALLOCATE CompressAllocateCallback = CompressAllocate;
    PFN_COMPRESS_FREE CompressFreeCallback = CompressFree;
    PERF_MEM_ALLOC PerfAllocateCallback = PerfAllocate;
    PERF_MEM_FREE PerfFreeCallback = PerfFree;
    PFNDAVAUTHCALLBACK DavAuthCallback = DavAuth;
    PFNDAVAUTHCALLBACK_FREECRED DavFreeCallback = NULL;
    AUTHNEXTSTEP Next = DefaultBehavior;
    ULONG Context = 0;
    DWORD Result;

    FreeCalls = 0;
    ok(CompressAllocateCallback(&Context, 123) == &Context, "Compression allocation callback failed\n");
    CompressFreeCallback(&Context, &Context);
    ok(PerfAllocateCallback(123, &Context) == &Context, "Performance allocation callback failed\n");
    PerfFreeCallback(&Context, &Context);
    ok(FreeCalls == 2, "Free callbacks ran %ld times\n", FreeCalls);
    Result = DavAuthCallback(NULL, NULL, 0, 0, NULL, &Next, &DavFreeCallback);
    ok(Result == 19, "WebDAV authentication returned %lu\n", Result);
    ok(Next == RetryRequest, "WebDAV next step is %d\n", Next);
    ok(DavFreeCallback != NULL, "WebDAV free callback is NULL\n");
    if (DavFreeCallback) ok(DavFreeCallback(&Context) == 17, "WebDAV free callback failed\n");
}
