/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D11Query Begin, End and GetData ordering
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>

#define QUERY_ORDER_WAIT_MS 5000

static HRESULT WaitForQuery(ID3D11DeviceContext *Context, ID3D11Query *Query)
{
    DWORD Start = GetTickCount();
    HRESULT Hr;

    do
    {
        Hr = Context->GetData(Query, NULL, 0, 0);
        if (Hr != S_FALSE) break;
        Sleep(1);
    } while (GetTickCount() - Start < QUERY_ORDER_WAIT_MS);
    return Hr;
}

static ID3D11Query *CreateOcclusionQuery(ID3D11Device *Device)
{
    D3D11_QUERY_DESC Desc = {D3D11_QUERY_OCCLUSION, 0};
    ID3D11Query *Query = NULL;
    HRESULT Hr = Device->CreateQuery(&Desc, &Query);

    ok(Hr == S_OK, "CreateQuery: %#lx\n", Hr);
    return Query;
}

static void TestNeverStarted(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    ID3D11Query *Query = CreateOcclusionQuery(Device);
    HRESULT Hr;

    if (!Query) return;
    Hr = Context->GetData(Query, NULL, 0, 0);
    ok(Hr == DXGI_ERROR_INVALID_CALL, "GetData on a new query: %#lx\n", Hr);
    Query->Release();
}

static void TestEndWithoutBegin(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    ID3D11Query *Query = CreateOcclusionQuery(Device);
    HRESULT Hr;

    if (!Query) return;
    Context->End(Query);
    Hr = WaitForQuery(Context, Query);
    trace("GetData after End without Begin: %#lx\n", Hr);
    ok(Hr == S_OK, "GetData after End without Begin: %#lx\n", Hr);
    Query->Release();
}

static void TestBeginTwice(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    ID3D11Query *Query = CreateOcclusionQuery(Device);
    HRESULT Hr;

    if (!Query) return;
    Context->Begin(Query);
    Hr = Context->GetData(Query, NULL, 0, 0);
    ok(Hr == DXGI_ERROR_INVALID_CALL, "GetData while active: %#lx\n", Hr);
    Context->Begin(Query);
    Hr = Context->GetData(Query, NULL, 0, 0);
    ok(Hr == DXGI_ERROR_INVALID_CALL, "GetData after a second Begin: %#lx\n", Hr);
    Context->End(Query);
    Hr = WaitForQuery(Context, Query);
    ok(Hr == S_OK, "GetData after Begin, Begin, End: %#lx\n", Hr);
    Context->End(Query);
    Hr = WaitForQuery(Context, Query);
    trace("GetData after a second End: %#lx\n", Hr);
    ok(Hr == S_OK, "GetData after a second End: %#lx\n", Hr);
    Query->Release();
}

static void TestBeginBoundPredicate(ID3D11Device *Device, ID3D11DeviceContext *Context)
{
    D3D11_QUERY_DESC Desc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    ID3D11Predicate *Predicate = NULL;
    ID3D11Query *Query;
    HRESULT Hr = Device->CreatePredicate(&Desc, &Predicate);

    ok(Hr == S_OK, "CreatePredicate: %#lx\n", Hr);
    if (!Predicate) return;
    Query = static_cast<ID3D11Query *>(static_cast<ID3D11Asynchronous *>(Predicate));
    Context->Begin(Predicate);
    Context->End(Predicate);
    Hr = WaitForQuery(Context, Query);
    ok(Hr == S_OK, "GetData on an ended predicate: %#lx\n", Hr);
    Context->SetPredication(Predicate, FALSE);
    Context->Begin(Predicate);
    Hr = Context->GetData(Predicate, NULL, 0, 0);
    trace("GetData after Begin of the bound predicate: %#lx\n", Hr);
    ok(Hr == DXGI_ERROR_INVALID_CALL, "GetData after Begin of the bound predicate: %#lx\n", Hr);
    Context->End(Predicate);
    Hr = WaitForQuery(Context, Query);
    trace("GetData after End of the bound predicate: %#lx\n", Hr);
    ok(Hr == S_OK, "GetData after End of the bound predicate: %#lx\n", Hr);
    Context->SetPredication(NULL, FALSE);
    Predicate->Release();
}

START_TEST(query_order)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE Create;
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr;

    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice is unavailable\n"); FreeLibrary(Module); return; }
    Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Module); return; }

    TestNeverStarted(Device, Context);
    TestEndWithoutBegin(Device, Context);
    TestBeginTwice(Device, Context);
    TestBeginBoundPredicate(Device, Context);

    Context->Release();
    Device->Release();
    FreeLibrary(Module);
}
