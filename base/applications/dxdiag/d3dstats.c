/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Frame rate, CPU and GPU load shown while a rendering test runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <winternl.h>
#include <d3dkmthk.h>

#define D3DTEST_STATS_PERIOD_MS 500

typedef NTSTATUS (APIENTRY *PFN_ENUM_ADAPTERS2)(D3DKMT_ENUMADAPTERS2 *);
typedef NTSTATUS (APIENTRY *PFN_CLOSE_ADAPTER)(const D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS (APIENTRY *PFN_QUERY_STATISTICS)(const D3DKMT_QUERYSTATISTICS *);

static PFN_QUERY_STATISTICS D3DTestQueryStatistics(VOID)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

    return Gdi32 ? (PFN_QUERY_STATISTICS)GetProcAddress(Gdi32, "D3DKMTQueryStatistics") : NULL;
}

static ULONGLONG D3DTestFileTime(const FILETIME *Time)
{
    return ((ULONGLONG)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
}

static BOOL D3DTestCpuTimes(ULONGLONG *Idle, ULONGLONG *Total)
{
    FILETIME IdleTime, KernelTime, UserTime;

    if (!GetSystemTimes(&IdleTime, &KernelTime, &UserTime))
        return FALSE;
    *Idle = D3DTestFileTime(&IdleTime);
    *Total = D3DTestFileTime(&KernelTime) + D3DTestFileTime(&UserTime);
    return TRUE;
}

static BOOL D3DTestNodeRunning(const LUID *AdapterLuid, UINT Node, ULONGLONG *Running)
{
    PFN_QUERY_STATISTICS QueryStatistics = D3DTestQueryStatistics();
    D3DKMT_QUERYSTATISTICS Query;

    if (!QueryStatistics)
        return FALSE;
    ZeroMemory(&Query, sizeof(Query));
    Query.Type = D3DKMT_QUERYSTATISTICS_NODE;
    Query.AdapterLuid = *AdapterLuid;
    Query.QueryNode.NodeId = Node;
    if (QueryStatistics(&Query) < 0 || Query.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart < 0)
        return FALSE;
    *Running = (ULONGLONG)Query.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
    return TRUE;
}

static BOOL D3DTestOpenGpu(D3DTEST_GPU *Gpu, const LUID *AdapterLuid)
{
    PFN_QUERY_STATISTICS QueryStatistics = D3DTestQueryStatistics();
    D3DKMT_QUERYSTATISTICS Query;
    UINT Node;

    ZeroMemory(&Query, sizeof(Query));
    Query.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    Query.AdapterLuid = *AdapterLuid;
    if (!QueryStatistics || QueryStatistics(&Query) < 0)
        return FALSE;
    Gpu->Luid = *AdapterLuid;
    Gpu->NodeCount = min(Query.QueryResult.AdapterInformation.NodeCount, (ULONG)D3DTEST_STATS_MAX_NODES);
    for (Node = 0; Node < Gpu->NodeCount; ++Node)
    {
        if (!D3DTestNodeRunning(&Gpu->Luid, Node, &Gpu->NodeRunning[Node]))
            return FALSE;
    }
    return Gpu->NodeCount != 0;
}

/* The load shown is the busiest engine of any adapter: a test may render on another adapter than the one that
 * drives its window. */
static VOID D3DTestOpenGpus(D3DTEST_STATS *Stats)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");
    PFN_ENUM_ADAPTERS2 EnumAdapters;
    PFN_CLOSE_ADAPTER CloseAdapter;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_CLOSEADAPTER Close;
    ULONG Index;

    if (!Gdi32)
        return;
    EnumAdapters = (PFN_ENUM_ADAPTERS2)GetProcAddress(Gdi32, "D3DKMTEnumAdapters2");
    CloseAdapter = (PFN_CLOSE_ADAPTER)GetProcAddress(Gdi32, "D3DKMTCloseAdapter");
    if (!EnumAdapters || !CloseAdapter)
        return;
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (EnumAdapters(&Enum) < 0)
        return;
    for (Index = 0; Index < Enum.NumAdapters && Index < ARRAYSIZE(Adapters); ++Index)
    {
        if (Stats->GpuCount < ARRAYSIZE(Stats->Gpus) &&
            D3DTestOpenGpu(&Stats->Gpus[Stats->GpuCount], &Adapters[Index].AdapterLuid))
            ++Stats->GpuCount;
        Close.hAdapter = Adapters[Index].hAdapter;
        CloseAdapter(&Close);
    }
}

VOID D3DTestStatsBegin(D3DTEST_STATS *Stats)
{
    ZeroMemory(Stats, sizeof(*Stats));
    Stats->GpuLoad = -1;
    Stats->CpuLoad = -1;
    D3DTestCpuTimes(&Stats->CpuIdle, &Stats->CpuTotal);
    D3DTestOpenGpus(Stats);
}

static LONG D3DTestSampleGpu(D3DTEST_STATS *Stats, DWORD Interval)
{
    ULONGLONG Running, Busiest = 0;
    BOOL Sampled = FALSE;
    UINT Gpu, Node;

    if (!Interval)
        return -1;
    for (Gpu = 0; Gpu < Stats->GpuCount; ++Gpu)
    {
        D3DTEST_GPU *Adapter = &Stats->Gpus[Gpu];

        for (Node = 0; Node < Adapter->NodeCount; ++Node)
        {
            if (!D3DTestNodeRunning(&Adapter->Luid, Node, &Running) || Running < Adapter->NodeRunning[Node])
                continue;
            Busiest = max(Busiest, Running - Adapter->NodeRunning[Node]);
            Adapter->NodeRunning[Node] = Running;
            Sampled = TRUE;
        }
    }
    return Sampled ? (LONG)min(Busiest / ((ULONGLONG)Interval * 100), 100) : -1;
}

VOID D3DTestStatsSample(D3DTEST_STATS *Stats, DWORD Elapsed, UINT Frames)
{
    ULONGLONG Idle, Total;
    DWORD Interval = Elapsed - Stats->SampleTime;

    if (Interval < D3DTEST_STATS_PERIOD_MS)
        return;

    Stats->FpsTenths = (ULONG)((ULONGLONG)(Frames - Stats->SampleFrames) * 10000 / Interval);
    Stats->HaveFps = TRUE;
    if (D3DTestCpuTimes(&Idle, &Total) && Total > Stats->CpuTotal && Idle >= Stats->CpuIdle)
    {
        ULONGLONG TotalDelta = Total - Stats->CpuTotal;
        ULONGLONG IdleDelta = min(Idle - Stats->CpuIdle, TotalDelta);

        Stats->CpuLoad = (LONG)((TotalDelta - IdleDelta) * 100 / TotalDelta);
        Stats->CpuIdle = Idle;
        Stats->CpuTotal = Total;
    }
    Stats->GpuLoad = D3DTestSampleGpu(Stats, Interval);
    Stats->SampleTime = Elapsed;
    Stats->SampleFrames = Frames;
}

UINT D3DTestStatsText(const D3DTEST_RESULT *Result, CHAR Text[D3DTEST_STATS_LINES][D3DTEST_STATS_LINE_LENGTH])
{
    const D3DTEST_STATS *Stats = &Result->Stats;

    if (Stats->HaveFps)
        StringCchPrintfA(Text[0], D3DTEST_STATS_LINE_LENGTH, "FPS %lu.%lu", Stats->FpsTenths / 10, Stats->FpsTenths % 10);
    else
        StringCchCopyA(Text[0], D3DTEST_STATS_LINE_LENGTH, "FPS --");
    StringCchPrintfA(Text[1], D3DTEST_STATS_LINE_LENGTH, "MARK %u", Result->Frames);
    if (Stats->CpuLoad >= 0)
        StringCchPrintfA(Text[2], D3DTEST_STATS_LINE_LENGTH, "CPU %ld%%", Stats->CpuLoad);
    else
        StringCchCopyA(Text[2], D3DTEST_STATS_LINE_LENGTH, "CPU --");
    if (Stats->GpuLoad >= 0)
        StringCchPrintfA(Text[3], D3DTEST_STATS_LINE_LENGTH, "GPU %ld%%", Stats->GpuLoad);
    else
        StringCchCopyA(Text[3], D3DTEST_STATS_LINE_LENGTH, "GPU N/A");
    return D3DTEST_STATS_LINES;
}

UINT D3DTestBuildScene(PD3DTEST_VERTEX Vertices, DWORD Elapsed, UINT Width, UINT Height, PD3DTEST_RESULT Result)
{
    CHAR Text[D3DTEST_STATS_LINES][D3DTEST_STATS_LINE_LENGTH];
    PCSTR Lines[D3DTEST_STATS_LINES];
    UINT Count, LineCount, i;

    Count = D3DTestBuildMark(Vertices, Elapsed, Width, Height);
    LineCount = D3DTestStatsText(Result, Text);
    for (i = 0; i < LineCount; ++i)
        Lines[i] = Text[i];
    return Count + D3DTestBuildHud(Vertices + Count, D3DTEST_MAX_VERTEX_COUNT - Count, Lines, LineCount, Height);
}

VOID D3DTestDrawStats(HDC hdc, const D3DTEST_RESULT *Result, UINT Height)
{
    CHAR Text[D3DTEST_STATS_LINES][D3DTEST_STATS_LINE_LENGTH];
    PCSTR Lines[D3DTEST_STATS_LINES];
    RECT Rects[D3DTEST_HUD_VERTEX_COUNT / 6];
    HBRUSH Back, Fore;
    UINT LineCount, Count, i;

    LineCount = D3DTestStatsText(Result, Text);
    for (i = 0; i < LineCount; ++i)
        Lines[i] = Text[i];
    Count = D3DTestHudRects(Rects, ARRAYSIZE(Rects), Lines, LineCount, Height);
    Back = CreateSolidBrush(RGB(0x1c, 0x1c, 0x1c));
    Fore = (HBRUSH)GetStockObject(WHITE_BRUSH);
    for (i = 0; i < Count; ++i)
        FillRect(hdc, &Rects[i], i ? Fore : Back);
    if (Back)
        DeleteObject(Back);
}
