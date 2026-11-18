/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests lodctr /m and unlodctr /m (performance counter manifests)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <winperf.h>
#include <perflib.h>
#include <stdio.h>

#define PERFLIB_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib"
#define PROVIDERS_KEY PERFLIB_KEY L"\\_V2Providers\\"
#define NONE 0xFFFFFFFF

#define PROVIDER_A L"{5a1c0b7e-7e57-4c4e-8000-0000000000a1}"
#define PROVIDER_B L"{5a1c0b7e-7e57-4c4e-8000-0000000000b1}"
#define PROVIDER_C L"{5a1c0b7e-7e57-4c4e-8000-0000000000c1}"
#define PROVIDER_D L"{5a1c0b7e-7e57-4c4e-8000-0000000000d1}"
#define PROVIDER_E L"{5a1c0b7e-7e57-4c4e-8000-0000000000e1}"
#define PROVIDER_F L"{5a1c0b7e-7e57-4c4e-8000-0000000000f1}"

/* One record of a counter set's CounterBlock, as Windows 11 lodctr writes it */
typedef struct _REG_COUNTER
{
    ULONG CounterId;
    ULONG Type;
    ULONGLONG Attrib;
    ULONG DetailLevel;
    LONG DefaultScale;
    ULONG NameResource;
    ULONG ExplainResource;
    ULONG AggregateFunc;
    ULONG BaseCounterId;
    ULONG PerfTimeId;
    ULONG PerfFreqId;
    ULONG MultiId;
    ULONG Reserved;
} REG_COUNTER;

C_ASSERT(sizeof(REG_COUNTER) == 56);

static const char ManifestHead[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<instrumentationManifest xmlns=\"http://schemas.microsoft.com/win/2004/08/events\">\n"
    "  <instrumentation>\n"
    "    <counters xmlns=\"http://schemas.microsoft.com/win/2005/12/counters\" schemaVersion=\"1.1\">\n";
static const char ManifestTail[] = "    </counters>\n  </instrumentation>\n</instrumentationManifest>\n";

static const char ManifestA[] =
    "      <provider applicationIdentity=\"lntperfa.exe\" providerName=\"lntperfa\""
    " providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000a1}\" symbol=\"LntPerfA\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000a1001}\" uri=\"Lnt.A1\" name=\"Lnt A1\""
    " description=\"Set A1\" instances=\"multiple\" symbol=\"LntA1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.A1.1\" name=\"C1\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.A1.2\" name=\"C2\" description=\"d\" type=\"perf_counter_large_rawcount\""
    " detailLevel=\"standard\" defaultScale=\"-3\"/>\n"
    "          <counter id=\"3\" uri=\"Lnt.A1.3\" name=\"C3\" description=\"d\" type=\"perf_counter_rawcount_hex\""
    " detailLevel=\"advanced\"/>\n"
    "        </counterSet>\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000a1002}\" uri=\"Lnt.A2\" name=\"Lnt A2\""
    " description=\"Set A2\" instances=\"globalAggregate\" symbol=\"LntA2\">\n"
    "          <counter id=\"1\" uri=\"Lnt.A2.1\" name=\"C1\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.A2.2\" name=\"C2\" description=\"d\" type=\"perf_raw_fraction\""
    " detailLevel=\"standard\" baseID=\"3\" aggregate=\"avg\"/>\n"
    "          <counter id=\"3\" uri=\"Lnt.A2.3\" name=\"C3\" description=\"d\" type=\"perf_raw_base\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"4\" uri=\"Lnt.A2.4\" name=\"C4\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\" aggregate=\"max\"><counterAttributes><counterAttribute name=\"reference\"/>"
    "<counterAttribute name=\"displayAsHex\"/></counterAttributes></counter>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static const char ManifestB[] =
    "      <provider applicationIdentity=\"lntperfb.sys\" providerName=\"lntperfb\" providerType=\"kernelMode\""
    " resourceBase=\"1000\" providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000b1}\" symbol=\"LntPerfB\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000b1001}\" uri=\"Lnt.B1\" name=\"Lnt B1\""
    " description=\"Set B1\" instances=\"multipleAggregate\" symbol=\"LntB1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.B1.1\" name=\"C1\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\" aggregate=\"total\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.B1.2\" name=\"C2\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\" baseID=\"1\"/>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static const char ManifestC[] =
    "      <provider applicationIdentity=\"lntperfc.exe\" providerName=\"lntperfc\""
    " providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000c1}\" symbol=\"LntPerfC\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000c1001}\" uri=\"Lnt.C1\" name=\"Lnt C1\""
    " description=\"Set C1\" instances=\"single\" symbol=\"LntC1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.C1.1\" name=\"C1\" description=\"d\" type=\"perf_counter_multi_timer\""
    " detailLevel=\"standard\" multiCounterID=\"2\" perfTimeID=\"3\" perfFreqID=\"4\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.C1.2\" name=\"C2\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"3\" uri=\"Lnt.C1.3\" name=\"C3\" description=\"d\" type=\"perf_counter_large_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"4\" uri=\"Lnt.C1.4\" name=\"C4\" description=\"d\" type=\"perf_counter_large_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"5\" uri=\"Lnt.C1.5\" name=\"C5\" description=\"d\" type=\"perf_elapsed_time\""
    " detailLevel=\"standard\" perfTimeID=\"3\" perfFreqID=\"4\"/>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static const char ManifestD[] =
    "      <provider applicationIdentity=\"lntperfd.exe\" providerName=\"lntperfd\""
    " providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000d1}\" symbol=\"LntPerfD\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000d1001}\" uri=\"Lnt.D1\" name=\"Lnt D1\""
    " description=\"Set D1\" instances=\"single\" symbol=\"LntD1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.D1.1\" name=\"C1\" description=\"d\" type=\"perf_bogus_type\""
    " detailLevel=\"standard\"/>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static const char ManifestE[] =
    "      <provider applicationIdentity=\"lntperfe.exe\" providerName=\"lntperfe\""
    " providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000e1}\" symbol=\"LntPerfE\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000e1001}\" uri=\"Lnt.E1\" name=\"Lnt E1\""
    " description=\"Set E1\" instances=\"single\" symbol=\"LntE1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.E1.1\" name=\"C1\" description=\"d\" type=\"perf_counter_multi_timer\""
    " detailLevel=\"standard\" multiCounterID=\"2\" perfTimeID=\"3\" perfFreqID=\"3\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.E1.2\" name=\"C2\" description=\"d\" type=\"perf_counter_multi_base\""
    " detailLevel=\"standard\"/>\n"
    "          <counter id=\"3\" uri=\"Lnt.E1.3\" name=\"C3\" description=\"d\" type=\"perf_counter_large_rawcount\""
    " detailLevel=\"standard\"/>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static const char ManifestF[] =
    "      <provider applicationIdentity=\"lntperff.exe\" providerName=\"lntperff\""
    " providerGuid=\"{5a1c0b7e-7e57-4c4e-8000-0000000000f1}\" symbol=\"LntPerfF\">\n"
    "        <counterSet guid=\"{5a1c0b7e-7e57-4c4e-8000-0000000f1001}\" uri=\"Lnt.F1\" name=\"Lnt F1\""
    " description=\"Set F1\" instances=\"multiple\" symbol=\"LntF1\">\n"
    "          <counter id=\"1\" uri=\"Lnt.F1.1\" name=\"C1\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\" defaultScale=\"0\"/>\n"
    "          <counter id=\"2\" uri=\"Lnt.F1.2\" name=\"C2\" description=\"d\" type=\"perf_counter_rawcount\""
    " detailLevel=\"standard\" defaultScale=\"0\"/>\n"
    "        </counterSet>\n"
    "      </provider>\n";

static WCHAR g_Directory[MAX_PATH];
static WCHAR g_System[MAX_PATH];

static void
ManifestPath(const WCHAR *Name, WCHAR *Path)
{
    swprintf(Path, MAX_PATH, L"%ls\\%ls", g_Directory, Name);
}

static BOOL
WriteManifest(const WCHAR *Name, const char *Body)
{
    WCHAR Path[MAX_PATH];
    DWORD Written;
    HANDLE File;
    BOOL Ok;

    ManifestPath(Name, Path);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Ok = WriteFile(File, ManifestHead, sizeof(ManifestHead) - 1, &Written, NULL) &&
         WriteFile(File, Body, (DWORD)strlen(Body), &Written, NULL) &&
         WriteFile(File, ManifestTail, sizeof(ManifestTail) - 1, &Written, NULL);
    CloseHandle(File);
    return Ok;
}

static DWORD
RunTool(const WCHAR *Tool, const WCHAR *Arguments)
{
    WCHAR CommandLine[2 * MAX_PATH + 64];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD Code = 0xDEADBEEF;

    swprintf(CommandLine, ARRAYSIZE(CommandLine), L"\"%ls\\%ls\" %ls", g_System, Tool, Arguments);
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "CreateProcessW(%ls) failed with %lu\n", CommandLine, GetLastError());
        return Code;
    }
    ok(WaitForSingleObject(Process.hProcess, 30000) == WAIT_OBJECT_0, "%ls did not finish\n", Tool);
    GetExitCodeProcess(Process.hProcess, &Code);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return Code;
}

static DWORD
Install(const WCHAR *Name, const WCHAR *Directory)
{
    WCHAR Path[MAX_PATH], Arguments[2 * MAX_PATH + 8];

    ManifestPath(Name, Path);
    if (Directory)
        swprintf(Arguments, ARRAYSIZE(Arguments), L"/m:%ls %ls", Path, Directory);
    else
        swprintf(Arguments, ARRAYSIZE(Arguments), L"/m:%ls", Path);
    return RunTool(L"lodctr.exe", Arguments);
}

static DWORD
Uninstall(const WCHAR *Name)
{
    WCHAR Path[MAX_PATH], Arguments[MAX_PATH + 8];

    ManifestPath(Name, Path);
    swprintf(Arguments, ARRAYSIZE(Arguments), L"/m:%ls", Path);
    return RunTool(L"unlodctr.exe", Arguments);
}

static DWORD
QueryDword(const WCHAR *Key, const WCHAR *Name)
{
    DWORD Value = NONE, Size = sizeof(Value), Type = 0;
    LONG Error = RegGetValueW(HKEY_LOCAL_MACHINE, Key, Name, RRF_RT_ANY, &Type, &Value, &Size);

    if (Error || Type != REG_DWORD)
        return NONE;
    return Value;
}

static BOOL
KeyExists(const WCHAR *Key)
{
    HKEY Handle;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_READ, &Handle))
        return FALSE;
    RegCloseKey(Handle);
    return TRUE;
}

#define ok_dword(Key, Name, Expected) CheckDword(__LINE__, Key, Name, Expected)
static void
CheckDword(int Line, const WCHAR *Key, const WCHAR *Name, DWORD Expected)
{
    DWORD Value = QueryDword(Key, Name);

    ok_(__FILE__, Line)(Value == Expected, "%ls: %ls is 0x%lx, expected 0x%lx\n", Key, Name, Value, Expected);
}

#define ok_string(Key, Name, Type, Expected) CheckString(__LINE__, Key, Name, Type, Expected)
static void
CheckString(int Line, const WCHAR *Key, const WCHAR *Name, DWORD ExpectedType, const WCHAR *Expected)
{
    WCHAR Value[MAX_PATH] = L"";
    DWORD Size = sizeof(Value), Type = 0;
    LONG Error = RegGetValueW(HKEY_LOCAL_MACHINE, Key, Name, RRF_RT_ANY | RRF_NOEXPAND, &Type, Value, &Size);

    ok_(__FILE__, Line)(!Error, "%ls: %ls error %ld\n", Key, Name, Error);
    ok_(__FILE__, Line)(Type == ExpectedType, "%ls: %ls type %lu, expected %lu\n", Key, Name, Type, ExpectedType);
    ok_(__FILE__, Line)(!wcscmp(Value, Expected), "%ls: %ls is '%ls', expected '%ls'\n", Key, Name, Value,
                        Expected);
}

static void
CheckSet(const WCHAR *Provider, const WCHAR *Set, const WCHAR *Name, DWORD InstanceType, DWORD NameResource,
         DWORD First, const REG_COUNTER *Expected, ULONG Count)
{
    WCHAR Key[160];
    REG_COUNTER Block[8];
    DWORD Size = sizeof(Block), Type = 0;
    LONG Error;
    ULONG Index;

    swprintf(Key, ARRAYSIZE(Key), L"%ls%ls\\%ls", PROVIDERS_KEY, Provider, Set);
    ok_dword(Key, L"NameResource", NameResource);
    ok_dword(Key, L"ExplainResource", NameResource + 2);
    ok_string(Key, L"NeutralName", REG_SZ, Name);
    ok_dword(Key, L"InstanceType", InstanceType);
    ok_dword(Key, L"First Counter", First);
    ok_dword(Key, L"Last Counter", First + 2 * Count);
    ok_dword(Key, L"CounterCount", Count);
    Error = RegGetValueW(HKEY_LOCAL_MACHINE, Key, L"CounterBlock", RRF_RT_ANY, &Type, Block, &Size);
    ok(!Error && Type == REG_BINARY, "%ls: CounterBlock error %ld type %lu\n", Key, Error, Type);
    ok(Size == Count * sizeof(REG_COUNTER), "%ls: CounterBlock is %lu bytes, expected %lu\n", Key, Size,
       (ULONG)(Count * sizeof(REG_COUNTER)));
    for (Index = 0; !Error && Index < Count && Index < Size / sizeof(REG_COUNTER); Index++)
    {
        const REG_COUNTER *Got = &Block[Index], *Want = &Expected[Index];

        winetest_push_context("%ls counter %lu", Set, Want->CounterId);
        ok(Got->CounterId == Want->CounterId, "id %lu\n", Got->CounterId);
        ok(Got->Type == Want->Type, "type 0x%lx, expected 0x%lx\n", Got->Type, Want->Type);
        ok(Got->Attrib == Want->Attrib, "attrib 0x%I64x, expected 0x%I64x\n", Got->Attrib, Want->Attrib);
        ok(Got->DetailLevel == Want->DetailLevel, "detail %lu\n", Got->DetailLevel);
        ok(Got->DefaultScale == Want->DefaultScale, "scale %ld\n", Got->DefaultScale);
        ok(Got->NameResource == Want->NameResource, "name %lu, expected %lu\n", Got->NameResource,
           Want->NameResource);
        ok(Got->ExplainResource == Want->ExplainResource, "explain %lu, expected %lu\n", Got->ExplainResource,
           Want->ExplainResource);
        ok(Got->AggregateFunc == Want->AggregateFunc, "aggregate %lu, expected %lu\n", Got->AggregateFunc,
           Want->AggregateFunc);
        ok(Got->BaseCounterId == Want->BaseCounterId, "base 0x%lx, expected 0x%lx\n", Got->BaseCounterId,
           Want->BaseCounterId);
        ok(Got->PerfTimeId == Want->PerfTimeId, "time 0x%lx, expected 0x%lx\n", Got->PerfTimeId, Want->PerfTimeId);
        ok(Got->PerfFreqId == Want->PerfFreqId, "freq 0x%lx, expected 0x%lx\n", Got->PerfFreqId, Want->PerfFreqId);
        ok(Got->MultiId == Want->MultiId, "multi 0x%lx, expected 0x%lx\n", Got->MultiId, Want->MultiId);
        ok(Got->Reserved == 0, "reserved 0x%lx\n", Got->Reserved);
        winetest_pop_context();
    }
}

static void
CheckProvider(const WCHAR *Provider, DWORD Type, const WCHAR *Name, const WCHAR *Application)
{
    WCHAR Key[128];

    swprintf(Key, ARRAYSIZE(Key), L"%ls%ls", PROVIDERS_KEY, Provider);
    ok(KeyExists(Key), "%ls is not installed\n", Provider);
    ok_dword(Key, L"ProviderType", Type);
    ok_string(Key, L"ProviderName", REG_SZ, Name);
    ok_string(Key, L"ApplicationIdentity", REG_EXPAND_SZ, Application);
}

static BOOL
ProviderInstalled(const WCHAR *Provider)
{
    WCHAR Key[128];

    swprintf(Key, ARRAYSIZE(Key), L"%ls%ls", PROVIDERS_KEY, Provider);
    return KeyExists(Key);
}

static void
CheckCode(int Line, DWORD Code, DWORD Expected)
{
    ok_(__FILE__, Line)(Code == Expected, "exit code 0x%lx, expected 0x%lx\n", Code, Expected);
}

static void
LastCounters(DWORD Expected)
{
    ok_dword(PERFLIB_KEY, L"Last Counter", Expected);
    ok_dword(PERFLIB_KEY, L"Last Help", Expected + 1);
}

static void
TestProviderA(void)
{
    const REG_COUNTER A1[] =
    {
        { 1, PERF_COUNTER_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 5, 7, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE, NONE },
        { 2, PERF_COUNTER_LARGE_RAWCOUNT, 0, PERF_DETAIL_NOVICE, -3, 9, 11, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE,
          NONE },
        { 3, PERF_COUNTER_RAWCOUNT_HEX, 0, PERF_DETAIL_ADVANCED, 0, 13, 15, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE,
          NONE },
    };
    const REG_COUNTER A2[] =
    {
        { 1, PERF_COUNTER_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 21, 23, PERF_AGGREGATE_TOTAL, NONE, NONE, NONE, NONE },
        { 2, PERF_RAW_FRACTION, 0, PERF_DETAIL_NOVICE, 0, 25, 27, PERF_AGGREGATE_AVG, 3, NONE, NONE, NONE },
        { 3, PERF_RAW_BASE, 0, PERF_DETAIL_NOVICE, 0, 29, 31, PERF_AGGREGATE_TOTAL, NONE, NONE, NONE, NONE },
        { 4, PERF_COUNTER_RAWCOUNT, PERF_ATTRIB_BY_REFERENCE | PERF_ATTRIB_DISPLAY_AS_HEX, PERF_DETAIL_NOVICE, 0, 33, 35,
          PERF_AGGREGATE_MAX, NONE, NONE, NONE, NONE },
    };
    WCHAR Application[MAX_PATH], Key[128], Other[MAX_PATH];
    DWORD Last = QueryDword(PERFLIB_KEY, L"Last Counter");

    swprintf(Key, ARRAYSIZE(Key), L"%ls%ls", PROVIDERS_KEY, PROVIDER_A);
    ok(Install(L"a.man", NULL) == 0, "lodctr /m a.man failed\n");
    swprintf(Application, ARRAYSIZE(Application), L"%ls\\lntperfa.exe", g_Directory);
    CheckProvider(PROVIDER_A, 0, L"lntperfa", Application);
    CheckSet(PROVIDER_A, L"{5a1c0b7e-7e57-4c4e-8000-0000000a1001}", L"Lnt A1", PERF_COUNTERSET_MULTI_INSTANCES, 1,
             Last + 2, A1, ARRAYSIZE(A1));
    CheckSet(PROVIDER_A, L"{5a1c0b7e-7e57-4c4e-8000-0000000a1002}", L"Lnt A2", PERF_COUNTERSET_SINGLE_AGGREGATE, 17,
             Last + 10, A2, ARRAYSIZE(A2));
    LastCounters(Last + 18);

    ok(Uninstall(L"a.man") == 0, "unlodctr /m a.man failed\n");
    ok(!ProviderInstalled(PROVIDER_A), "provider A is still installed\n");
    LastCounters(Last + 18);
    ok(Uninstall(L"a.man") == 0, "second unlodctr /m a.man failed\n");

    /* The path argument replaces the directory of the application */
    swprintf(Other, ARRAYSIZE(Other), L"%ls\\bin", g_Directory);
    ok(Install(L"a.man", Other) == 0, "lodctr /m a.man <path> failed\n");
    swprintf(Application, ARRAYSIZE(Application), L"%ls\\lntperfa.exe", Other);
    ok_string(Key, L"ApplicationIdentity", REG_EXPAND_SZ, Application);
    ok(Uninstall(L"a.man") == 0, "unlodctr /m a.man failed\n");
}

static void
TestProviderB(void)
{
    const REG_COUNTER B1[] =
    {
        { 1, PERF_COUNTER_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 1004, 1006, PERF_AGGREGATE_TOTAL, NONE, NONE, NONE,
          NONE },
        { 2, PERF_COUNTER_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 1008, 1010, PERF_AGGREGATE_TOTAL, NONE, NONE, NONE,
          NONE },
    };
    WCHAR Application[MAX_PATH];
    DWORD Last = QueryDword(PERFLIB_KEY, L"Last Counter");

    ok(Install(L"b.man", NULL) == 0, "lodctr /m b.man failed\n");
    swprintf(Application, ARRAYSIZE(Application), L"%ls\\lntperfb.sys", g_Directory);
    CheckProvider(PROVIDER_B, 1, L"lntperfb", Application);
    CheckSet(PROVIDER_B, L"{5a1c0b7e-7e57-4c4e-8000-0000000b1001}", L"Lnt B1", PERF_COUNTERSET_MULTI_AGGREGATE, 1000,
             Last + 2, B1, ARRAYSIZE(B1));
    LastCounters(Last + 6);
    ok(Uninstall(L"b.man") == 0, "unlodctr /m b.man failed\n");
}

static void
TestProviderC(void)
{
    const REG_COUNTER C1[] =
    {
        { 1, PERF_COUNTER_MULTI_TIMER, 0, PERF_DETAIL_NOVICE, 0, 5, 7, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE, 2 },
        { 2, PERF_COUNTER_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 9, 11, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE,
          NONE },
        { 3, PERF_COUNTER_LARGE_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 13, 15, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE,
          NONE },
        { 4, PERF_COUNTER_LARGE_RAWCOUNT, 0, PERF_DETAIL_NOVICE, 0, 17, 19, PERF_AGGREGATE_UNDEFINED, NONE, NONE, NONE,
          NONE },
        { 5, PERF_ELAPSED_TIME, 0, PERF_DETAIL_NOVICE, 0, 21, 23, PERF_AGGREGATE_UNDEFINED, NONE, 3, 4, NONE },
    };
    WCHAR Application[MAX_PATH];
    DWORD Last = QueryDword(PERFLIB_KEY, L"Last Counter");

    ok(Install(L"c.man", NULL) == 0, "lodctr /m c.man failed\n");
    swprintf(Application, ARRAYSIZE(Application), L"%ls\\lntperfc.exe", g_Directory);
    CheckProvider(PROVIDER_C, 0, L"lntperfc", Application);
    CheckSet(PROVIDER_C, L"{5a1c0b7e-7e57-4c4e-8000-0000000c1001}", L"Lnt C1", PERF_COUNTERSET_SINGLE_INSTANCE, 1,
             Last + 2, C1, ARRAYSIZE(C1));
    ok(Uninstall(L"c.man") == 0, "unlodctr /m c.man failed\n");
}

/* An installed provider whose application keeps its file name is left as it is; one whose application changed
   name is removed and the install fails. Both take their counter indexes again. */
static void
TestReinstall(void)
{
    WCHAR Key[128];
    DWORD Last = QueryDword(PERFLIB_KEY, L"Last Counter");

    swprintf(Key, ARRAYSIZE(Key), L"%ls%ls", PROVIDERS_KEY, PROVIDER_F);
    ok(Install(L"f.man", NULL) == 0, "lodctr /m f.man failed\n");
    LastCounters(Last + 6);
    ok(!RegSetKeyValueW(HKEY_LOCAL_MACHINE, Key, L"ApplicationIdentity", REG_EXPAND_SZ, L"C:\\other\\LntPerfF.exe",
                        sizeof(L"C:\\other\\LntPerfF.exe")), "RegSetKeyValueW failed\n");
    ok(Install(L"f.man", NULL) == 0, "second lodctr /m f.man failed\n");
    ok_string(Key, L"ApplicationIdentity", REG_EXPAND_SZ, L"C:\\other\\LntPerfF.exe");
    LastCounters(Last + 12);

    ok(!RegSetKeyValueW(HKEY_LOCAL_MACHINE, Key, L"ApplicationIdentity", REG_EXPAND_SZ, L"C:\\other\\f.exe",
                        sizeof(L"C:\\other\\f.exe")), "RegSetKeyValueW failed\n");
    CheckCode(__LINE__, Install(L"f.man", NULL), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    ok(!ProviderInstalled(PROVIDER_F), "provider F is still installed\n");
    LastCounters(Last + 18);
    ok(Uninstall(L"f.man") == 0, "unlodctr /m f.man failed\n");
}

static void
TestFailures(void)
{
    WCHAR Arguments[MAX_PATH + 8], Path[MAX_PATH];
    DWORD Last = QueryDword(PERFLIB_KEY, L"Last Counter");

    CheckCode(__LINE__, Install(L"d.man", NULL), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    ok(!ProviderInstalled(PROVIDER_D), "provider D is installed\n");
    CheckCode(__LINE__, Uninstall(L"d.man"), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    CheckCode(__LINE__, Install(L"e.man", NULL), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    ok(!ProviderInstalled(PROVIDER_E), "provider E is installed\n");
    CheckCode(__LINE__, Install(L"missing.man", NULL), HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    CheckCode(__LINE__, Uninstall(L"missing.man"), HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    CheckCode(__LINE__, RunTool(L"lodctr.exe", L"/m:"), ERROR_INVALID_PARAMETER);
    LastCounters(Last);

    ManifestPath(L"a.man", Path);
    swprintf(Arguments, ARRAYSIZE(Arguments), L"/M:%ls", Path);
    ok(RunTool(L"lodctr.exe", Arguments) == 0, "lodctr /M: failed\n");
    ok(ProviderInstalled(PROVIDER_A), "lodctr /M: did not install provider A\n");
    swprintf(Arguments, ARRAYSIZE(Arguments), L"/M:%ls", Path);
    ok(RunTool(L"unlodctr.exe", Arguments) == 0, "unlodctr /M: failed\n");
    ok(!ProviderInstalled(PROVIDER_A), "unlodctr /M: did not remove provider A\n");
}

START_TEST(manifest)
{
    static const struct { const WCHAR *Name; const char *Body; } Manifests[] =
    {
        { L"a.man", ManifestA }, { L"b.man", ManifestB }, { L"c.man", ManifestC }, { L"d.man", ManifestD },
        { L"e.man", ManifestE }, { L"f.man", ManifestF },
    };
    WCHAR Temp[MAX_PATH], Path[MAX_PATH];
    HKEY Perflib;
    ULONG Index;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, PERFLIB_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &Perflib, NULL))
    {
        skip("Cannot write the Perflib key; run as administrator\n");
        return;
    }
    RegCloseKey(Perflib);
    GetSystemDirectoryW(g_System, ARRAYSIZE(g_System));
    GetTempPathW(ARRAYSIZE(Temp), Temp);
    swprintf(g_Directory, ARRAYSIZE(g_Directory), L"%lslntperf%lu", Temp, GetCurrentProcessId());
    ok(CreateDirectoryW(g_Directory, NULL), "CreateDirectoryW failed with %lu\n", GetLastError());
    for (Index = 0; Index < ARRAYSIZE(Manifests); Index++)
        ok(WriteManifest(Manifests[Index].Name, Manifests[Index].Body), "cannot write %ls\n", Manifests[Index].Name);

    TestProviderA();
    TestProviderB();
    TestProviderC();
    TestReinstall();
    TestFailures();

    for (Index = 0; Index < ARRAYSIZE(Manifests); Index++)
    {
        Uninstall(Manifests[Index].Name);
        ManifestPath(Manifests[Index].Name, Path);
        DeleteFileW(Path);
    }
    RemoveDirectoryW(g_Directory);
}
