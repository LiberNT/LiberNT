/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IMofCompiler::CompileBuffer parsing and argument checks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <initguid.h>
#include <wbemcli.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_GET_CLASS_OBJECT)(REFCLSID, REFIID, void **);

static IMofCompiler *
GetCompiler(void)
{
    IMofCompiler *Compiler = NULL;
    IClassFactory *Factory;
    PFN_GET_CLASS_OBJECT GetClassObject;
    HMODULE Module;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MofCompiler, NULL, CLSCTX_INPROC_SERVER, &IID_IMofCompiler, (void **)&Compiler);
    if (SUCCEEDED(hr))
        return Compiler;
    /* Windows PE has no WMI: a mofd.dll beside the test is loaded directly */
    Module = LoadLibraryW(L"mofd.dll");
    if (!Module)
    {
        skip("MofCompiler is not registered (0x%08lx) and mofd.dll does not load (%lu)\n", hr, GetLastError());
        return NULL;
    }
    GetClassObject = (PFN_GET_CLASS_OBJECT)GetProcAddress(Module, "DllGetClassObject");
    if (!GetClassObject || FAILED(GetClassObject(&CLSID_MofCompiler, &IID_IClassFactory, (void **)&Factory)))
    {
        skip("mofd.dll has no MofCompiler class object\n");
        return NULL;
    }
    hr = IClassFactory_CreateInstance(Factory, NULL, &IID_IMofCompiler, (void **)&Compiler);
    IClassFactory_Release(Factory);
    ok(hr == S_OK, "CreateInstance returned 0x%08lx\n", hr);
    return SUCCEEDED(hr) ? Compiler : NULL;
}

#define UNTOUCHED ((LONG)0xCCCCCCCC)

typedef struct _MOF_CASE
{
    const char *Name;
    const char *Text;
    LONG Size;
    const WCHAR *Namespace;
    LONG Options;
    LONG ClassFlags;
    LONG InstanceFlags;
    HRESULT Expected;
    LONG Phase;
    HRESULT Error;
} MOF_CASE;

static const char ValidMof[] =
    "#pragma namespace(\"\\\\\\\\.\\\\root\\\\cimv2\")\n"
    "#pragma deleteinstance(\"__Namespace.Name='LNT'\", NOFAIL)\n"
    "#pragma autorecover\n"
    "Instance of __Namespace\n"
    "{\n"
    "    Name = \"LNT\";\n"
    "};\n"
    "#pragma namespace(\"\\\\\\\\.\\\\root\\\\cimv2\\\\LNT\")\n"
    "instance of __Win32Provider as $P\n"
    "{\n"
    "    ClientLoadableCLSID = NULL;\n"
    "    CLSID = \"{96CDE098-DB47-4040-97CF-C522384F5554}\";\n"
    "    HostingModel = \"Decoupled:Com\";\n"
    "    Name = \"Lnt|Test\";\n"
    "    Version = 0x02000000;\n"
    "};\n"
    "instance of __InstanceProviderRegistration\n"
    "{\n"
    "    Provider = $P;\n"
    "    SupportsGet = TRUE;\n"
    "    QuerySupportLevels = {\"WQL:UnarySelect\", \"WQL:References\"};\n"
    "};\n"
    "[Dynamic, Provider(\"Lnt|Test\"), Description(\"A \" \"joined string\")]\n"
    "// comment between the qualifiers and the class\n"
    "CLASS Lnt_Thing\n"
    "{\n"
    "    [Key] uint32 Id;\n"
    "    string Names[];\n"
    "    real32 Ratio = 1.5;\n"
    "    sint32 Negative = -5;\n"
    "    /* block\n       comment */\n"
    "    Lnt_Thing ref Peer;\n"
    "    [Implemented] uint32 Do([in] uint32 A, [in, out] string B[], [out] boolean C);\n"
    "};\n";

static const WCHAR Utf16Mof[] = L"\xfeff" L"class A { uint32 X; };";

#define CHECK WBEM_FLAG_CHECK_ONLY
#define PARSE_ERROR(Error) WBEM_S_FALSE, 2, (Error)
#define PARSED S_OK, 0, S_OK
#define REFUSED WBEM_E_INVALID_PARAMETER, UNTOUCHED, (HRESULT)UNTOUCHED
#define ARGUMENT_ERROR WBEM_S_FALSE, 1, WBEM_E_INVALID_PARAMETER

static const MOF_CASE Cases[] =
{
    { "valid", ValidMof, -1, NULL, CHECK, 0, 0, PARSED },
    { "semicolon", "class A\n{\n    uint32 X\n};\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_EXPECTED_SEMI) },
    { "embedded-type", "class A\n{\n    uint33 X;\n};\n", -1, NULL, CHECK, 0, 0, PARSED },
    { "deleteinstance", "#pragma deleteinstance(\"A.X=1\")\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_INVALID_DELETEINSTANCE_SYNTAX) },
    { "eof", "class A\n{\n    uint32 X;\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_EXPECTED_BRACE_OR_BAD_TYPE) },
    { "token", "\n\n@\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_UNRECOGNIZED_TOKEN) },
    { "empty", "", 0, NULL, CHECK, 0, 0, REFUSED },
    { "nullbuffer", NULL, 16, NULL, CHECK, 0, 0, REFUSED },
    { "autorecover+dontadd", "class A { };", -1, NULL, WBEM_FLAG_AUTORECOVER | WBEM_FLAG_DONT_ADD_TO_LIST, 0, 0, ARGUMENT_ERROR },
    { "unknownoption", "class A { };", -1, NULL, 0x40000000, 0, 0, ARGUMENT_ERROR },
    { "unknownoption-before-parse", "@", -1, NULL, 0x40000000, 0, 0, ARGUMENT_ERROR },
    { "known-options", "class A { };", -1, NULL, CHECK | WBEM_FLAG_WMI_CHECK | WBEM_FLAG_CONSOLE_PRINT | WBEM_FLAG_DONT_ADD_TO_LIST | WBEM_FLAG_SPLIT_FILES | WBEM_FLAG_STORE_FILE, 0, 0, PARSED },
    { "missing-value", "instance of A\n{\n    X = ;\n};\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_ILLEGAL_CONSTANT_VALUE) },
    { "missing-of", "instance A\n{\n};\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_INVALID_INSTANCE_DECLARATION) },
    { "duplicate-property", "class A\n{\n    uint32 X;\n    uint32 X;\n};\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_DUPLICATE_PROPERTY) },
    { "unknown-pragma", "#pragma frobnicate(\"x\")\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_INVALID_PRAGMA) },
    { "check+autorecover", ValidMof, -1, NULL, CHECK | WBEM_FLAG_AUTORECOVER, 0, 0, PARSED },
    { "unknown-flavor", "[Key : Frob] class A\n{\n};\n", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_EXPECTED_FLAVOR_TYPE) },
    { "utf16-buffer", (const char *)Utf16Mof, sizeof(Utf16Mof) - sizeof(WCHAR), NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_UNRECOGNIZED_TOKEN) },
    { "utf8bom-buffer", "\xef\xbb\xbf" "class A { uint32 X; };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_UNRECOGNIZED_TOKEN) },
    { "8bit-string", "[Description(\"caf\xc3\xa9\")] class A { uint32 X; };", -1, NULL, CHECK, 0, 0, PARSED },
    { "bad-hex", "class A { uint32 X = 0xZ; };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_ILLEGAL_CONSTANT_VALUE) },
    { "bad-octal", "class A { uint32 X = 010; uint32 Y = 09; };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_ILLEGAL_CONSTANT_VALUE) },
    { "null-array-element", "class A { uint32 X[] = {1, NULL}; };", -1, NULL, CHECK, 0, 0, PARSED },
    { "unknown-alias", "instance of A { X = $Nope; };", -1, NULL, CHECK, 0, 0, PARSED },
    { "void-property", "class A { void X; };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_EXPECTED_TYPE_IDENTIFIER) },
    { "unmarked-parameter", "class A { uint32 M(uint32 a); };", -1, NULL, CHECK, 0, 0, PARSED },
    { "duplicate-qualifier", "[Key, key] class A { };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_DUPLICATE_QUALIFIER) },
    { "class-alias", "class A as $X { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "typedef", "typedef uint32 T;", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_TYPEDEF_NOT_SUPPORTED) },
    { "qualifier-declaration", "qualifier Key : boolean = false, scope(property, reference), flavor(DisableOverride);", -1, NULL, CHECK, 0, 0, PARSED },
    { "pragma-include", "#pragma include(\"nothere.mof\")", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_INVALID_PRAGMA) },
    { "pragma-locale", "#pragma locale(\"ms_409\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "pragma-classflags", "#pragma classflags(\"forceupdate\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "pragma-classflags-bad", "#pragma classflags(\"sideways\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_INVALID_FLAGS_SYNTAX) },
    { "class-flags-conflict", "class A { };", -1, NULL, CHECK, WBEM_FLAG_CREATE_ONLY | WBEM_FLAG_UPDATE_ONLY, 0, PARSED },
    { "instance-flags-conflict", "class A { };", -1, NULL, CHECK, 0, WBEM_FLAG_CREATE_ONLY | WBEM_FLAG_UPDATE_ONLY, PARSED },
    { "autorecover-namespace", "#pragma autorecover\nclass A { };", -1, L"root\\cimv2", CHECK, 0, 0, ARGUMENT_ERROR },
    { "autorecover-dot-namespace", "#pragma autorecover\nclass A { };", -1, L".", CHECK, 0, 0, ARGUMENT_ERROR },
    { "autorecover-empty-namespace", "#pragma autorecover\nclass A { };", -1, L"", CHECK, 0, 0, PARSED },
    { "autorecover-class-flags", "#pragma autorecover\nclass A { };", -1, NULL, CHECK, WBEM_FLAG_UPDATE_SAFE_MODE, 0, ARGUMENT_ERROR },
    { "autorecover-instance-flags", "#pragma autorecover\nclass A { };", -1, NULL, CHECK, 0, WBEM_FLAG_UPDATE_ONLY, ARGUMENT_ERROR },
    { "autorecover-option-flags", "class A { };", -1, NULL, CHECK | WBEM_FLAG_AUTORECOVER, WBEM_FLAG_CREATE_ONLY, 0, ARGUMENT_ERROR },
    { "autorecover-store-flags", "#pragma autorecover\nclass A { };", -1, NULL, 0, WBEM_FLAG_UPDATE_ONLY, 0, ARGUMENT_ERROR },
    { "autorecover-late", "class A { };\n#pragma autorecover\nclass B { };", -1, NULL, CHECK, WBEM_FLAG_CREATE_ONLY, 0, ARGUMENT_ERROR },
    { "autorecover-parse-first", "#pragma autorecover\n@", -1, NULL, CHECK, WBEM_FLAG_CREATE_ONLY, 0, PARSE_ERROR(WBEMMOF_E_UNRECOGNIZED_TOKEN) },
    { "autorecover-parse-later", "class A { }\n#pragma autorecover\n", -1, NULL, CHECK, WBEM_FLAG_CREATE_ONLY, 0, PARSE_ERROR(WBEMMOF_E_EXPECTED_SEMI) },
    { "autorecover-pragma-flags", "#pragma autorecover\n#pragma classflags(\"createonly\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "autorecover-pragma-namespace", "#pragma autorecover\n#pragma namespace(\"root\\\\cimv2\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "autorecover-twice", "#pragma autorecover\n#pragma autorecover\nclass A { };", -1, NULL, CHECK, 0, 0, PARSED },
    { "autorecover-argument", "#pragma autorecover(\"x\")\nclass A { };", -1, NULL, CHECK, 0, 0, PARSE_ERROR(WBEMMOF_E_UNRECOGNIZED_TOKEN) },
};

static void
TestStoreWithoutRepository(IMofCompiler *Compiler)
{
    static const char Text[] = "class Lnt_Store\n{\n    uint32 X;\n};\n";
    WBEM_COMPILE_STATUS_INFO Info;
    IWbemLocator *Locator;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&Locator);
    if (SUCCEEDED(hr))
    {
        IWbemLocator_Release(Locator);
        /* TODO: store into a throwaway namespace once the WMI repository can hold classes */
        skip("WMI is present; storing is not tested\n");
        return;
    }
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileBuffer(Compiler, sizeof(Text) - 1, (BYTE *)Text, NULL, NULL, NULL, NULL, 0, 0, 0, &Info);
    ok(hr == WBEM_S_FALSE, "store: hr 0x%08lx\n", hr);
    ok(Info.lPhaseError == 3, "store: phase %ld\n", Info.lPhaseError);
    ok(Info.hRes == REGDB_E_CLASSNOTREG, "store: hRes 0x%08lx\n", Info.hRes);
    ok(Info.ObjectNum == UNTOUCHED && Info.FirstLine == UNTOUCHED && Info.LastLine == UNTOUCHED,
       "store: object %ld lines %ld-%ld written\n", Info.ObjectNum, Info.FirstLine, Info.LastLine);
}

static void
WriteTestFile(const WCHAR *Path, const void *Data, DWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Written = 0;

    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, Data, Size, &Written, NULL) && Written == Size, "WriteFile failed %lu\n", GetLastError());
    CloseHandle(File);
}

static void
CheckFileResult(const char *Name, HRESULT hr, const WBEM_COMPILE_STATUS_INFO *Info, HRESULT Expected, LONG Phase, HRESULT Error)
{
    ok(hr == Expected, "%s: hr 0x%08lx, expected 0x%08lx\n", Name, hr, Expected);
    ok(Info->lPhaseError == Phase, "%s: phase %ld, expected %ld\n", Name, Info->lPhaseError, Phase);
    ok(Info->hRes == Error, "%s: hRes 0x%08lx, expected 0x%08lx\n", Name, Info->hRes, Error);
    ok(!Info->ObjectNum && !Info->FirstLine && !Info->LastLine && !Info->dwOutFlags,
       "%s: object %ld lines %ld-%ld flags 0x%lx not cleared\n", Name, Info->ObjectNum, Info->FirstLine, Info->LastLine,
       Info->dwOutFlags);
}

static void
TestCompileFile(IMofCompiler *Compiler)
{
    static const BYTE Utf16File[] = { 0xFF, 0xFE, 'c', 0, 'l', 0, 'a', 0, 's', 0, 's', 0, ' ', 0, 'A', 0, ' ', 0, '{', 0, ' ', 0, '}', 0, ';', 0 };
    static const BYTE Utf8BomFile[] = { 0xEF, 0xBB, 0xBF, 'c', 'l', 'a', 's', 's', ' ', 'A', ' ', '{', ' ', '}', ';' };
    WCHAR Path[MAX_PATH], Missing[MAX_PATH];
    WBEM_COMPILE_STATUS_INFO Info;
    HRESULT hr;

    GetTempPathW(ARRAYSIZE(Path), Path);
    lstrcpyW(Missing, Path);
    lstrcatW(Path, L"mofd_apitest.mof");
    lstrcatW(Missing, L"mofd_apitest_missing.mof");
    DeleteFileW(Missing);

    WriteTestFile(Path, ValidMof, sizeof(ValidMof) - 1);
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Path, NULL, NULL, NULL, NULL, CHECK, 0, 0, &Info);
    CheckFileResult("file", hr, &Info, S_OK, 0, S_OK);
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Path, L"root\\cimv2", NULL, NULL, NULL, CHECK | WBEM_FLAG_AUTORECOVER,
                                  WBEM_FLAG_CREATE_ONLY | WBEM_FLAG_UPDATE_ONLY, WBEM_FLAG_CREATE_ONLY | WBEM_FLAG_UPDATE_ONLY, &Info);
    CheckFileResult("file-flags", hr, &Info, WBEM_S_FALSE, 1, WBEM_E_INVALID_PARAMETER);
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Path, NULL, NULL, NULL, NULL, CHECK | WBEM_FLAG_AUTORECOVER, 0, 0, &Info);
    CheckFileResult("file-autorecover", hr, &Info, S_OK, 0, S_OK);
    hr = IMofCompiler_CompileFile(Compiler, Path, NULL, NULL, NULL, NULL, CHECK, 0, 0, NULL);
    ok(hr == S_OK, "no status info: hr 0x%08lx\n", hr);

    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Missing, NULL, NULL, NULL, NULL, CHECK, 0, 0, &Info);
    CheckFileResult("missing-file", hr, &Info, WBEM_S_FALSE, 2, WBEMMOF_E_INVALID_FILE);
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Missing, NULL, NULL, NULL, NULL, 0x40000000, 0, 0, &Info);
    CheckFileResult("missing-file-bad-option", hr, &Info, WBEM_S_FALSE, 1, WBEM_E_INVALID_PARAMETER);
    hr = IMofCompiler_CompileBuffer(Compiler, sizeof(ValidMof) - 1, (BYTE *)ValidMof, NULL, NULL, NULL, NULL, 0x40000000, 0, 0, NULL);
    ok(hr == WBEM_S_FALSE, "bad option without status info: hr 0x%08lx\n", hr);
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CreateBMOF(Compiler, NULL, NULL, NULL, 0, 0, 0, &Info);
    ok(hr == WBEM_E_INVALID_PARAMETER, "CreateBMOF without files: hr 0x%08lx\n", hr);
    ok(Info.lPhaseError == UNTOUCHED && Info.hRes == (HRESULT)UNTOUCHED, "CreateBMOF without files: status info written\n");

    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, NULL, NULL, NULL, NULL, NULL, CHECK, 0, 0, &Info);
    ok(hr == WBEM_E_INVALID_PARAMETER, "no file name: hr 0x%08lx\n", hr);
    ok(Info.lPhaseError == UNTOUCHED && Info.hRes == (HRESULT)UNTOUCHED && Info.ObjectNum == UNTOUCHED,
       "no file name: status info written\n");

    WriteTestFile(Path, Utf16File, sizeof(Utf16File));
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Path, NULL, NULL, NULL, NULL, CHECK, 0, 0, &Info);
    CheckFileResult("utf16-file", hr, &Info, S_OK, 0, S_OK);

    WriteTestFile(Path, Utf8BomFile, sizeof(Utf8BomFile));
    memset(&Info, 0xCC, sizeof(Info));
    hr = IMofCompiler_CompileFile(Compiler, Path, NULL, NULL, NULL, NULL, CHECK, 0, 0, &Info);
    CheckFileResult("utf8bom-file", hr, &Info, WBEM_S_FALSE, 2, WBEMMOF_E_UNRECOGNIZED_TOKEN);

    DeleteFileW(Path);
}

START_TEST(CompileBuffer)
{
    IMofCompiler *Compiler;
    ULONG Index;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "CoInitializeEx returned 0x%08lx\n", hr);
    Compiler = GetCompiler();
    if (!Compiler)
        return;

    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
    {
        const MOF_CASE *Case = &Cases[Index];
        WBEM_COMPILE_STATUS_INFO Info;
        LONG Size = Case->Size >= 0 ? Case->Size : (LONG)strlen(Case->Text);

        memset(&Info, 0xCC, sizeof(Info));
        hr = IMofCompiler_CompileBuffer(Compiler, Size, (BYTE *)Case->Text, (LPWSTR)Case->Namespace, NULL, NULL, NULL,
                                        Case->Options, Case->ClassFlags, Case->InstanceFlags, &Info);
        ok(hr == Case->Expected, "%s: hr 0x%08lx, expected 0x%08lx\n", Case->Name, hr, Case->Expected);
        ok(Info.lPhaseError == Case->Phase, "%s: phase %ld, expected %ld\n", Case->Name, Info.lPhaseError, Case->Phase);
        ok(Info.hRes == Case->Error, "%s: hRes 0x%08lx, expected 0x%08lx\n", Case->Name, Info.hRes, Case->Error);
        ok(Info.ObjectNum == UNTOUCHED && Info.FirstLine == UNTOUCHED && Info.LastLine == UNTOUCHED &&
           Info.dwOutFlags == (DWORD)UNTOUCHED, "%s: object %ld lines %ld-%ld flags 0x%lx written\n", Case->Name,
           Info.ObjectNum, Info.FirstLine, Info.LastLine, Info.dwOutFlags);
    }

    TestStoreWithoutRepository(Compiler);
    TestCompileFile(Compiler);
    IMofCompiler_Release(Compiler);
    CoUninitialize();
}
