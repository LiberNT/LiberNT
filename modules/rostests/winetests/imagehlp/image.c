/*
 * Copyright 2008 Juan Lang
 * Copyright 2010 Andrey Turkin
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <stdio.h>
#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <winver.h>
#include <winnt.h>
#include <winuser.h>
#include <imagehlp.h>

#include "wine/test.h"

static char *load_resource(const char *name)
{
    static char path[MAX_PATH];
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    GetTempPathA(ARRAY_SIZE(path), path);
    strcat(path, name);

    file = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create file %s, error %lu.\n",
            debugstr_a(path), GetLastError());

    res = FindResourceA(NULL, name, "TESTDLL");
    ok(!!res, "Failed to load resource, error %lu.\n", GetLastError());
    ptr = LockResource(LoadResource(GetModuleHandleA(NULL), res));
    WriteFile(file, ptr, SizeofResource( GetModuleHandleA(NULL), res), &written, NULL);
    ok(written == SizeofResource(GetModuleHandleA(NULL), res), "Failed to write resource.\n");
    CloseHandle(file);

    return path;
}

/* minimal PE file image */
#define VA_START 0x400000
#define FILE_PE_START 0x50
#define NUM_SECTIONS 3
#define FILE_TEXT 0x200
#define RVA_TEXT 0x1000
#define RVA_BSS 0x2000
#define FILE_IDATA 0x400
#define RVA_IDATA 0x3000
#define FILE_TOTAL 0x600
#define RVA_TOTAL 0x4000

#pragma pack(push,1)
struct imports
{
    IMAGE_IMPORT_DESCRIPTOR descriptors[2];
    IMAGE_THUNK_DATA32 original_thunks[2];
    IMAGE_THUNK_DATA32 thunks[2];
    struct
    {
        WORD hint;
        char funcname[0x20];
    } ibn;
    char dllname[0x10];
};
#define EXIT_PROCESS (VA_START + RVA_IDATA + FIELD_OFFSET(struct imports, thunks))

static struct image
{
    IMAGE_DOS_HEADER dos_header;
    char __alignment1[FILE_PE_START - sizeof(IMAGE_DOS_HEADER)];
    IMAGE_NT_HEADERS32 nt_headers;
    IMAGE_SECTION_HEADER sections[NUM_SECTIONS];
    char __alignment2[FILE_TEXT - FILE_PE_START - sizeof(IMAGE_NT_HEADERS32) -
        NUM_SECTIONS * sizeof(IMAGE_SECTION_HEADER)];
    unsigned char text_section[FILE_IDATA-FILE_TEXT];
    struct imports idata_section;
    char __alignment3[FILE_TOTAL-FILE_IDATA-sizeof(struct imports)];
}
bin =
{
    /* dos header */
    {IMAGE_DOS_SIGNATURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0, {0}, FILE_PE_START},
    /* alignment before PE header */
    {0},
    /* nt headers */
    {IMAGE_NT_SIGNATURE,
        /* basic headers - 3 sections, no symbols, EXE file */
        {IMAGE_FILE_MACHINE_I386, NUM_SECTIONS, 0, 0, 0, sizeof(IMAGE_OPTIONAL_HEADER32),
            IMAGE_FILE_32BIT_MACHINE | IMAGE_FILE_EXECUTABLE_IMAGE},
        /* optional header */
        {IMAGE_NT_OPTIONAL_HDR32_MAGIC, 4, 0, FILE_IDATA-FILE_TEXT,
            FILE_TOTAL-FILE_IDATA + FILE_IDATA-FILE_TEXT, 0x400,
            RVA_TEXT, RVA_TEXT, RVA_BSS, VA_START, 0x1000, 0x200, 4, 0, 1, 0, 4, 0, 0,
            RVA_TOTAL, FILE_TEXT, 0, IMAGE_SUBSYSTEM_WINDOWS_GUI, 0,
            0x200000, 0x1000, 0x100000, 0x1000, 0, 0x10,
            {{0, 0},
             {RVA_IDATA, sizeof(struct imports)}
            }
        }
    },
    /* sections */
    {
        {".text", {0x100}, RVA_TEXT, FILE_IDATA-FILE_TEXT, FILE_TEXT,
            0, 0, 0, 0, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ},
        {".bss", {0x400}, RVA_BSS, 0, 0, 0, 0, 0, 0,
            IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE},
        {".idata", {sizeof(struct imports)}, RVA_IDATA, FILE_TOTAL-FILE_IDATA, FILE_IDATA, 0,
            0, 0, 0, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE}
    },
    /* alignment before first section */
    {0},
    /* .text section */
    {
        0x31, 0xC0, /* xor eax, eax */
        0xFF, 0x25, EXIT_PROCESS&0xFF, (EXIT_PROCESS>>8)&0xFF, (EXIT_PROCESS>>16)&0xFF,
            (EXIT_PROCESS>>24)&0xFF, /* jmp ExitProcess */
        0
    },
    /* .idata section */
    {
        {
            {{RVA_IDATA + FIELD_OFFSET(struct imports, original_thunks)}, 0, 0,
            RVA_IDATA + FIELD_OFFSET(struct imports, dllname),
            RVA_IDATA + FIELD_OFFSET(struct imports, thunks)
            },
            {{0}, 0, 0, 0, 0}
        },
        {{{RVA_IDATA+FIELD_OFFSET(struct imports, ibn)}}, {{0}}},
        {{{RVA_IDATA+FIELD_OFFSET(struct imports, ibn)}}, {{0}}},
        {0,"ExitProcess"},
        "KERNEL32.DLL"
    },
    /* final alignment */
    {0}
};

struct imports64
{
    IMAGE_IMPORT_DESCRIPTOR descriptors[2];
    IMAGE_THUNK_DATA64 original_thunks[2];
    IMAGE_THUNK_DATA64 thunks[2];
    struct
    {
        WORD hint;
        char funcname[0x20];
    } ibn;
    char dllname[0x10];
};

static struct image64
{
    IMAGE_DOS_HEADER dos_header;
    char __alignment1[FILE_PE_START - sizeof(IMAGE_DOS_HEADER)];
    IMAGE_NT_HEADERS64 nt_headers;
    IMAGE_SECTION_HEADER sections[2];
    char __alignment2[FILE_TEXT - FILE_PE_START - sizeof(IMAGE_NT_HEADERS64) - 2 * sizeof(IMAGE_SECTION_HEADER)];
    unsigned char text_section[FILE_IDATA - FILE_TEXT];
    struct imports64 idata_section;
    char __alignment3[FILE_TOTAL - FILE_IDATA - sizeof(struct imports64)];
}
bin64 =
{
    /* dos header */
    { IMAGE_DOS_SIGNATURE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}, 0, 0, {0}, FILE_PE_START },
    /* alignment before PE header */
    {0},
    /* nt headers */
    {
        IMAGE_NT_SIGNATURE,
        /* basic headers - 2 sections, no symbols, EXE file */
        {IMAGE_FILE_MACHINE_AMD64, 2, 0, 0, 0, sizeof(IMAGE_OPTIONAL_HEADER64),
         IMAGE_FILE_EXECUTABLE_IMAGE},
        /* optional header */
        {
            IMAGE_NT_OPTIONAL_HDR64_MAGIC, 6, 0, FILE_IDATA - FILE_TEXT,
            FILE_TOTAL - FILE_IDATA + FILE_IDATA - FILE_TEXT, 0x400,
            RVA_TEXT, RVA_TEXT, VA_START, 0x1000, 0x200, 4, 0, 1, 0, 5, 2, 0,
            RVA_TOTAL, FILE_TEXT, 0, IMAGE_SUBSYSTEM_WINDOWS_GUI, 0,
            0x200000, 0x1000, 0x100000, 0x1000, 0, 0x10,
            {{0, 0}, {RVA_IDATA, sizeof(struct imports64)}}
        }
    },
    /* sections */
    {
        { ".text", {0x100}, RVA_TEXT, FILE_IDATA - FILE_TEXT, FILE_TEXT,
          0, 0, 0, 0, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ },
        { ".idata", {sizeof(struct imports64)}, RVA_IDATA, FILE_TOTAL - FILE_IDATA, FILE_IDATA, 0,
          0, 0, 0, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE }
    },
    /* alignment before first section */
    {0},
    /* .text section */
    {
        0x90
    },
    /* .idata section */
    {
        { {{RVA_IDATA + FIELD_OFFSET(struct imports64, original_thunks)}, 0, 0,
            RVA_IDATA + FIELD_OFFSET(struct imports64, dllname),
            RVA_IDATA + FIELD_OFFSET(struct imports64, thunks)},
          {{0}, 0, 0, 0, 0} },
        { {{RVA_IDATA + FIELD_OFFSET(struct imports64, ibn)}}, {{0}} },
        { {{RVA_IDATA + FIELD_OFFSET(struct imports64, ibn)}}, {{0}} },
        { 0, "ExitProcess" },
        "KERNEL32.DLL"
    },
    /* final alignment */
    {0}
};
#pragma pack(pop)

struct blob
{
    DWORD cb;
    BYTE *pb;
};

struct expected_blob
{
    DWORD cb;
    const void *pb;
};

struct update_accum
{
    DWORD cUpdates;
    struct blob *updates;
};

struct expected_update_accum
{
    DWORD cUpdates;
    const struct expected_blob *updates;
    BOOL  todo;
};

static BOOL WINAPI accumulating_stream_output(DIGEST_HANDLE handle, BYTE *pb,
 DWORD cb)
{
    struct update_accum *accum = (struct update_accum *)handle;
    BOOL ret = FALSE;

    if (accum->cUpdates)
        accum->updates = HeapReAlloc(GetProcessHeap(), 0, accum->updates,
         (accum->cUpdates + 1) * sizeof(struct blob));
    else
        accum->updates = HeapAlloc(GetProcessHeap(), 0, sizeof(struct blob));
    if (accum->updates)
    {
        struct blob *blob = &accum->updates[accum->cUpdates];

        blob->pb = HeapAlloc(GetProcessHeap(), 0, cb);
        if (blob->pb)
        {
            memcpy(blob->pb, pb, cb);
            blob->cb = cb;
            ret = TRUE;
        }
        accum->cUpdates++;
    }
    return ret;
}

static void check_updates(LPCSTR header, const struct expected_update_accum *expected,
        const struct update_accum *got)
{
    DWORD i;

    todo_wine_if (expected->todo)
        ok(expected->cUpdates == got->cUpdates, "%s: expected %ld updates, got %ld\n",
            header, expected->cUpdates, got->cUpdates);
    for (i = 0; i < min(expected->cUpdates, got->cUpdates); i++)
    {
        ok(expected->updates[i].cb == got->updates[i].cb, "%s, update %ld: expected %ld bytes, got %ld\n",
                header, i, expected->updates[i].cb, got->updates[i].cb);
        if (expected->updates[i].cb && expected->updates[i].cb == got->updates[i].cb)
            ok(!memcmp(expected->updates[i].pb, got->updates[i].pb, got->updates[i].cb),
                    "%s, update %ld: unexpected value\n", header, i);
    }
}

/* Frees the updates stored in accum */
static void free_updates(struct update_accum *accum)
{
    DWORD i;

    for (i = 0; i < accum->cUpdates; i++)
        HeapFree(GetProcessHeap(), 0, accum->updates[i].pb);
    HeapFree(GetProcessHeap(), 0, accum->updates);
    accum->updates = NULL;
    accum->cUpdates = 0;
}

static const struct expected_blob b1[] = {
    {FILE_PE_START,  &bin},
    /* with zeroed Checksum/SizeOfInitializedData/SizeOfImage fields */
    {sizeof(bin.nt_headers), &bin.nt_headers},
    {sizeof(bin.sections),  &bin.sections},
    {FILE_IDATA-FILE_TEXT, &bin.text_section},
    {sizeof(bin.idata_section.descriptors[0].OriginalFirstThunk),
        &bin.idata_section.descriptors[0].OriginalFirstThunk},
    {FIELD_OFFSET(struct imports, thunks)-
        (FIELD_OFFSET(struct imports, descriptors)+FIELD_OFFSET(IMAGE_IMPORT_DESCRIPTOR, Name)),
        &bin.idata_section.descriptors[0].Name},
    {FILE_TOTAL-FILE_IDATA-FIELD_OFFSET(struct imports, ibn),
        &bin.idata_section.ibn}
};
static const struct expected_update_accum a1 = { ARRAY_SIZE(b1), b1, TRUE };

static const struct expected_blob b2[] = {
    {FILE_PE_START,  &bin},
    /* with zeroed Checksum/SizeOfInitializedData/SizeOfImage fields */
    {sizeof(bin.nt_headers), &bin.nt_headers},
    {sizeof(bin.sections),  &bin.sections},
    {FILE_IDATA-FILE_TEXT, &bin.text_section},
    {FILE_TOTAL-FILE_IDATA, &bin.idata_section}
};
static const struct expected_update_accum a2 = { ARRAY_SIZE(b2), b2, FALSE };

static const struct expected_blob b3[] = {
    {FILE_PE_START,  &bin64},
    /* with zeroed Checksum/SizeOfInitializedData/SizeOfImage fields */
    {sizeof(bin64.nt_headers), &bin64.nt_headers},
    {sizeof(bin64.sections),  &bin64.sections},
    {FILE_IDATA - FILE_TEXT, &bin64.text_section},
    {sizeof(bin64.idata_section.descriptors[0].OriginalFirstThunk),
     &bin64.idata_section.descriptors[0].OriginalFirstThunk},
    {FIELD_OFFSET(struct imports64, thunks) -
     (FIELD_OFFSET(struct imports64, descriptors) + FIELD_OFFSET(IMAGE_IMPORT_DESCRIPTOR, Name)),
     &bin64.idata_section.descriptors[0].Name},
    {FILE_TOTAL - FILE_IDATA - FIELD_OFFSET(struct imports64, ibn), &bin64.idata_section.ibn}
};
static const struct expected_update_accum a3 = { ARRAY_SIZE(b3), b3, TRUE };

static const struct expected_blob b4[] = {
    {FILE_PE_START,  &bin64},
    /* with zeroed Checksum/SizeOfInitializedData/SizeOfImage fields */
    {sizeof(bin64.nt_headers), &bin64.nt_headers},
    {sizeof(bin64.sections),  &bin64.sections},
    {FILE_IDATA - FILE_TEXT, &bin64.text_section},
    {FILE_TOTAL - FILE_IDATA, &bin64.idata_section}
};
static const struct expected_update_accum a4 = { ARRAY_SIZE(b4), b4, FALSE };

/* Creates a test file and returns a handle to it.  The file's path is returned
 * in temp_file, which must be at least MAX_PATH characters in length.
 */
static HANDLE create_temp_file(char *temp_file)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    char temp_path[MAX_PATH];

    if (GetTempPathA(sizeof(temp_path), temp_path))
    {
        if (GetTempFileNameA(temp_path, "img", 0, temp_file))
            file = CreateFileA(temp_file, GENERIC_READ | GENERIC_WRITE, 0, NULL,
             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    return file;
}

static DWORD compute_checksum(const WORD *image, DWORD image_size)
{
    const WORD *ptr = image;
    DWORD size, sum = 0;

    for (size = (image_size + 1) / sizeof(WORD); size > 0; ptr++, size--)
    {
        sum += *ptr;
        if (HIWORD(sum)) sum = LOWORD(sum) + HIWORD(sum);
    }
    sum = (WORD)(LOWORD(sum) + HIWORD(sum));
    sum += image_size;

    return sum;
}

static void test_get_digest_stream(void)
{
    BOOL ret;
    HANDLE file;
    char temp_file[MAX_PATH];
    DWORD count, checksum;
    struct update_accum accum = { 0, NULL };

    SetLastError(0xdeadbeef);
    ret = ImageGetDigestStream(NULL, 0, NULL, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
    file = create_temp_file(temp_file);
    if (file == INVALID_HANDLE_VALUE)
    {
        skip("couldn't create temp file\n");
        return;
    }
    SetLastError(0xdeadbeef);
    ret = ImageGetDigestStream(file, 0, NULL, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = ImageGetDigestStream(NULL, 0, accumulating_stream_output, &accum);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
    /* Even with "valid" parameters, it fails with an empty file */
    SetLastError(0xdeadbeef);
    ret = ImageGetDigestStream(file, 0, accumulating_stream_output, &accum);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
    /* Finally, with a valid executable in the file, it succeeds.  Note that
     * the file pointer need not be positioned at the beginning.
     */
    bin.nt_headers.OptionalHeader.CheckSum = 0;
    checksum = compute_checksum((const WORD *)&bin, sizeof(bin));
    bin.nt_headers.OptionalHeader.CheckSum = checksum;

    WriteFile(file, &bin, sizeof(bin), &count, NULL);
    FlushFileBuffers(file);

    /* zero out some fields ImageGetDigestStream would zero out */
    bin.nt_headers.OptionalHeader.CheckSum = 0;
    bin.nt_headers.OptionalHeader.SizeOfInitializedData = 0;
    bin.nt_headers.OptionalHeader.SizeOfImage = 0;

    ret = ImageGetDigestStream(file, 0, accumulating_stream_output, &accum);
    ok(ret, "ImageGetDigestStream failed: %ld\n", GetLastError());
    check_updates("flags = 0", &a1, &accum);
    free_updates(&accum);
    ret = ImageGetDigestStream(file, CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO,
     accumulating_stream_output, &accum);
    ok(ret, "ImageGetDigestStream failed: %ld\n", GetLastError());
    check_updates("flags = CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO", &a2, &accum);
    free_updates(&accum);
    CloseHandle(file);
    DeleteFileA(temp_file);

    file = create_temp_file(temp_file);
    if (file == INVALID_HANDLE_VALUE)
    {
        skip("couldn't create temp file\n");
        return;
    }

    bin64.nt_headers.OptionalHeader.CheckSum = 0;
    checksum = compute_checksum((const WORD *)&bin64, sizeof(bin64));
    bin64.nt_headers.OptionalHeader.CheckSum = checksum;

    WriteFile(file, &bin64, sizeof(bin64), &count, NULL);
    FlushFileBuffers(file);

    bin64.nt_headers.OptionalHeader.CheckSum = 0;
    bin64.nt_headers.OptionalHeader.SizeOfInitializedData = 0;
    bin64.nt_headers.OptionalHeader.SizeOfImage = 0;

    ret = ImageGetDigestStream(file, 0, accumulating_stream_output, &accum);
    ok(ret, "ImageGetDigestStream failed: %lu\n", GetLastError());
    check_updates("flags = 0", &a3, &accum);
    free_updates(&accum);
    ret = ImageGetDigestStream(file, CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO,
                               accumulating_stream_output, &accum);
    ok(ret, "ImageGetDigestStream failed: %lu\n", GetLastError());
    check_updates("flags = CERT_PE_IMAGE_DIGEST_ALL_IMPORT_INFO", &a4, &accum);
    free_updates(&accum);
    CloseHandle(file);
    DeleteFileA(temp_file);
}

static unsigned int got_SysAllocString, got_GetOpenFileNameA, got_SHRegGetIntW;

static BOOL WINAPI bind_image_cb(IMAGEHLP_STATUS_REASON reason, const char *file,
        const char *module, ULONG_PTR va, ULONG_PTR param)
{
    static char last_module[MAX_PATH];

    if (winetest_debug > 1)
        trace("reason %u, file %s, module %s, va %#Ix, param %#Ix\n",
                reason, debugstr_a(file), debugstr_a(module), (size_t)va, (size_t)param);

    if (reason == BindImportModule)
    {
        ok(!strchr(module, '\\'), "got module name %s\n", debugstr_a(module));
        strcpy(last_module, module);
        ok(!va, "got VA %#Ix\n", (size_t)va);
        ok(!param, "got param %#Ix\n", (size_t)param);
    }
    else if (reason == BindImportProcedure)
    {
        char full_path[MAX_PATH];
        BOOL ret;

        todo_wine ok(!!va, "expected nonzero VA\n");
        ret = SearchPathA(NULL, last_module, ".dll", sizeof(full_path), full_path, NULL);
        ok(ret, "got error %lu\n", GetLastError());
        ok(!strcmp(module, full_path), "expected %s, got %s\n", debugstr_a(full_path), debugstr_a(module));

        if (!strcmp((const char *)param, "SysAllocString"))
        {
            ok(!strcmp(last_module, "oleaut32.dll"), "got wrong module %s\n", debugstr_a(module));
            ++got_SysAllocString;
        }
        else if (!strcmp((const char *)param, "GetOpenFileNameA"))
        {
            ok(!strcmp(last_module, "comdlg32.dll"), "got wrong module %s\n", debugstr_a(module));
            ++got_GetOpenFileNameA;
        }
        else if (!strcmp((const char *)param, "Ordinal117"))
        {
            ok(!strcmp(last_module, "shlwapi.dll"), "got wrong module %s\n", debugstr_a(module));
            ++got_SHRegGetIntW;
        }
    }
    else
    {
        ok(reason == BindForwarderNOT, "got unexpected reason %#x\n", reason);
    }
    return TRUE;
}

static void test_bind_image_ex(void)
{
    const char *filename = load_resource("testdll.dll");
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = BindImageEx(BIND_ALL_IMAGES | BIND_NO_BOUND_IMPORTS | BIND_NO_UPDATE,
            "nonexistent.dll", 0, 0, bind_image_cb);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_INVALID_PARAMETER,
            "got error %lu\n", GetLastError());

    ret = BindImageEx(BIND_ALL_IMAGES | BIND_NO_BOUND_IMPORTS | BIND_NO_UPDATE,
            filename, NULL, NULL, bind_image_cb);
    ok(ret, "got error %lu\n", GetLastError());
    ok(got_SysAllocString == 1, "got %u imports of SysAllocString\n", got_SysAllocString);
    ok(got_GetOpenFileNameA == 1, "got %u imports of GetOpenFileNameA\n", got_GetOpenFileNameA);
    todo_wine ok(got_SHRegGetIntW == 1, "got %u imports of SHRegGetIntW\n", got_SHRegGetIntW);

    ret = DeleteFileA(filename);
    ok(ret, "got error %lu\n", GetLastError());
}

static void test_image_load(void)
{
    char temp_file[MAX_PATH];
    PLOADED_IMAGE img;
    DWORD ret, count;
    HANDLE file;

    file = create_temp_file(temp_file);
    if (file == INVALID_HANDLE_VALUE)
    {
        skip("couldn't create temp file\n");
        return;
    }

    WriteFile(file, &bin, sizeof(bin), &count, NULL);
    CloseHandle(file);

    img = ImageLoad(temp_file, NULL);
    ok(img != NULL, "ImageLoad unexpectedly failed\n");

    if (img)
    {
        ok(!strcmp(img->ModuleName, temp_file),
           "unexpected ModuleName, got %s instead of %s\n", img->ModuleName, temp_file);
        ok(img->MappedAddress != NULL, "MappedAddress != NULL\n");
        if (img->MappedAddress)
        {
            ok(!memcmp(img->MappedAddress, &bin.dos_header, sizeof(bin.dos_header)),
               "MappedAddress doesn't point to IMAGE_DOS_HEADER\n");
        }
        ok(img->FileHeader != NULL, "FileHeader != NULL\n");
        if (img->FileHeader)
        {
            ok(!memcmp(img->FileHeader, &bin.nt_headers, sizeof(bin.nt_headers)),
                "FileHeader doesn't point to IMAGE_NT_HEADERS32\n");
        }
        ok(img->NumberOfSections == 3,
           "unexpected NumberOfSections, got %ld instead of 3\n", img->NumberOfSections);
        if (img->NumberOfSections >= 3)
        {
            ok(!strcmp((const char *)img->Sections[0].Name, ".text"),
               "unexpected name for section 0, expected .text, got %s\n",
               (const char *)img->Sections[0].Name);
            ok(!strcmp((const char *)img->Sections[1].Name, ".bss"),
               "unexpected name for section 1, expected .bss, got %s\n",
               (const char *)img->Sections[1].Name);
            ok(!strcmp((const char *)img->Sections[2].Name, ".idata"),
               "unexpected name for section 2, expected .idata, got %s\n",
               (const char *)img->Sections[2].Name);
        }
        ok(img->Characteristics == 0x102,
           "unexpected Characteristics, got 0x%lx instead of 0x102\n", img->Characteristics);
        ok(img->fSystemImage == 0,
           "unexpected fSystemImage, got %d instead of 0\n", img->fSystemImage);
        ok(img->fDOSImage == 0,
           "unexpected fDOSImage, got %d instead of 0\n", img->fDOSImage);
        todo_wine ok(img->fReadOnly == 1 || broken(!img->fReadOnly) /* <= WinXP */,
           "unexpected fReadOnly, got %d instead of 1\n", img->fReadOnly);
        todo_wine ok(img->Version == 1 || broken(!img->Version) /* <= WinXP */,
           "unexpected Version, got %d instead of 1\n", img->Version);
        ok(img->SizeOfImage == 0x600,
           "unexpected SizeOfImage, got 0x%lx instead of 0x600\n", img->SizeOfImage);

        count = 0xdeadbeef;
        ret = GetImageUnusedHeaderBytes(img, &count);
        todo_wine
        ok(ret == 448, "GetImageUnusedHeaderBytes returned %lu instead of 448\n", ret);
        todo_wine
        ok(count == 64, "unexpected size for unused header bytes, got %lu instead of 64\n", count);

        ImageUnload(img);
    }

    DeleteFileA(temp_file);
}

static void test_image_config(void)
{
    union
    {
        struct image narrow;
        struct image64 wide;
        BYTE bytes[FILE_TOTAL];
        ULONGLONG align;
    } input;
    union
    {
        IMAGE_LOAD_CONFIG_DIRECTORY32 narrow;
        IMAGE_LOAD_CONFIG_DIRECTORY64 wide;
        BYTE bytes[FILE_IDATA - FILE_TEXT];
    } config;
    struct
    {
        ULONGLONG before[2];
        union
        {
            IMAGE_LOAD_CONFIG_DIRECTORY directory;
            BYTE bytes[FILE_IDATA - FILE_TEXT];
        } data;
        ULONGLONG after[2];
    } output, expected;
    BYTE readback[FILE_TOTAL];
    char path[MAX_PATH] = {0};
    LOADED_IMAGE loaded;
    IMAGE_DATA_DIRECTORY *directory;
    LARGE_INTEGER length;
    HANDLE file;
    DWORD prefix, full, other_prefix, written, error;
    unsigned wide, i;
    BOOL ret, success;

    file = create_temp_file(path);
    ok(file != INVALID_HANDLE_VALUE, "Cannot create configuration fixture, error %lu.\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE) goto done;
    for (wide = 0; wide < 2; ++wide)
    {
        struct { DWORD directory, embedded; BOOL valid; } cases[14];

        prefix = wide ? FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, SEHandlerTable) :
                        FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY32, SEHandlerTable);
        other_prefix = wide ? FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY32, SEHandlerTable) :
                              FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, SEHandlerTable);
        full = wide ? sizeof(config.wide) : sizeof(config.narrow);
        cases[0].directory = 0; cases[0].embedded = 0; cases[0].valid = FALSE;
        cases[1].directory = prefix; cases[1].embedded = 0; cases[1].valid = TRUE;
        cases[2].directory = prefix; cases[2].embedded = 1; cases[2].valid = FALSE;
        cases[3].directory = prefix; cases[3].embedded = 3; cases[3].valid = FALSE;
        cases[4].directory = prefix; cases[4].embedded = 4; cases[4].valid = FALSE;
        cases[5].directory = prefix; cases[5].embedded = prefix - 1; cases[5].valid = FALSE;
        cases[6].directory = prefix; cases[6].embedded = prefix; cases[6].valid = TRUE;
        cases[7].directory = prefix; cases[7].embedded = prefix + 1; cases[7].valid = TRUE;
        cases[8].directory = prefix; cases[8].embedded = full; cases[8].valid = TRUE;
        cases[9].directory = prefix - 1; cases[9].embedded = prefix; cases[9].valid = FALSE;
        cases[10].directory = prefix + 1; cases[10].embedded = prefix; cases[10].valid = FALSE;
        cases[11].directory = full; cases[11].embedded = full; cases[11].valid = FALSE;
        cases[12].directory = full; cases[12].embedded = 0; cases[12].valid = FALSE;
        cases[13].directory = other_prefix; cases[13].embedded = other_prefix; cases[13].valid = FALSE;
        for (i = 0; i < ARRAY_SIZE(cases); ++i)
        {
            winetest_push_context("PE%u directory %lu embedded %lu", wide ? 64 : 32,
                                  cases[i].directory, cases[i].embedded);
            if (wide)
            {
                input.wide = bin64;
                input.wide.nt_headers.OptionalHeader.SizeOfImage = RVA_TOTAL;
                input.wide.nt_headers.OptionalHeader.SizeOfInitializedData = FILE_TOTAL - FILE_TEXT;
                input.wide.sections[0].Misc.VirtualSize = sizeof(config.bytes);
                directory = &input.wide.nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
            }
            else
            {
                input.narrow = bin;
                input.narrow.nt_headers.OptionalHeader.SizeOfImage = RVA_TOTAL;
                input.narrow.nt_headers.OptionalHeader.SizeOfInitializedData = FILE_TOTAL - FILE_TEXT;
                input.narrow.sections[0].Misc.VirtualSize = sizeof(config.bytes);
                directory = &input.narrow.nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
            }
            directory->VirtualAddress = cases[i].directory ? RVA_TEXT : 0;
            directory->Size = cases[i].directory;
            memset(&config, 0, sizeof(config));
#define CONFIG_FIELD(field, value) do { if (wide) config.wide.field = (value); else config.narrow.field = (value); } while (0)
            CONFIG_FIELD(Size, cases[i].embedded);
            CONFIG_FIELD(TimeDateStamp, 0x61234567);
            CONFIG_FIELD(MajorVersion, 0x1234);
            CONFIG_FIELD(MinorVersion, 0x5678);
            CONFIG_FIELD(GlobalFlagsClear, 0x10);
            CONFIG_FIELD(GlobalFlagsSet, 0x20);
            CONFIG_FIELD(CriticalSectionDefaultTimeout, 0x10203040);
            CONFIG_FIELD(DeCommitFreeBlockThreshold, 0x12345678);
            CONFIG_FIELD(DeCommitTotalFreeThreshold, 0x23456789);
            CONFIG_FIELD(MaximumAllocationSize, 0x3456789a);
            CONFIG_FIELD(VirtualMemoryThreshold, 0x456789ab);
            CONFIG_FIELD(ProcessAffinityMask, 2);
            CONFIG_FIELD(ProcessHeapFlags, 0x81);
            CONFIG_FIELD(CSDVersion, 0x123);
            CONFIG_FIELD(DependentLoadFlags, 0x456);
            CONFIG_FIELD(SecurityCookie, VA_START + RVA_TEXT + 0x1c0);
#undef CONFIG_FIELD
            if (wide) config.wide.MaximumAllocationSize |= (ULONGLONG)0x76543210 << 32;
            memcpy(input.bytes + FILE_TEXT, config.bytes, sizeof(config.bytes));
            if (file == INVALID_HANDLE_VALUE)
                file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            ok(file != INVALID_HANDLE_VALUE, "Cannot reopen fixture, error %lu.\n", GetLastError());
            if (file == INVALID_HANDLE_VALUE) goto case_done;
            written = 0;
            ret = WriteFile(file, input.bytes, sizeof(input.bytes), &written, NULL);
            ok(ret && written == sizeof(input.bytes), "WriteFile returned %d, bytes %lu, error %lu.\n",
               ret, written, GetLastError());
            if (!ret || written != sizeof(input.bytes)) goto case_done;
            ret = FlushFileBuffers(file);
            ok(ret, "FlushFileBuffers failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            ret = CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            ok(ret, "CloseHandle failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            memset(&loaded, 0, sizeof(loaded));
            ret = MapAndLoad(path, NULL, &loaded, FALSE, TRUE);
            ok(ret, "MapAndLoad failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            ok(loaded.MappedAddress != NULL && loaded.SizeOfImage == sizeof(input.bytes),
               "Unexpected mapped fixture %p, size %lu.\n", loaded.MappedAddress, loaded.SizeOfImage);
            if (loaded.MappedAddress && loaded.SizeOfImage == sizeof(input.bytes))
            {
                ok(!memcmp(loaded.MappedAddress, input.bytes, sizeof(input.bytes)), "MapAndLoad changed fixture bytes.\n");
                memset(&output, 0xa5, sizeof(output));
                output.data.directory.Size = sizeof(IMAGE_LOAD_CONFIG_DIRECTORY);
                expected = output;
                success = wide == (sizeof(void *) == sizeof(ULONGLONG)) && cases[i].valid;
                if (success) memcpy(expected.data.bytes, config.bytes, prefix);
                SetLastError(0xdeadbeef);
                ret = GetImageConfigInformation(&loaded, &output.data.directory);
                error = GetLastError();
                ok(ret == success, "GetImageConfigInformation returned %d, expected %d.\n", ret, success);
                ok(error == (success ? 0xdeadbeef : wide == (sizeof(void *) == sizeof(ULONGLONG)) ?
                             ERROR_INVALID_DATA : ERROR_INVALID_PARAMETER), "Unexpected error %lu.\n", error);
                ok(!memcmp(&output, &expected, sizeof(output)), "Unexpected configuration bytes, tail or guards.\n");
                ok(!memcmp(loaded.MappedAddress, input.bytes, sizeof(input.bytes)), "GetImageConfigInformation changed fixture bytes.\n");
            }
            ret = UnMapAndLoad(&loaded);
            ok(ret, "UnMapAndLoad failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            ok(file != INVALID_HANDLE_VALUE, "Cannot read back fixture, error %lu.\n", GetLastError());
            if (file == INVALID_HANDLE_VALUE) goto case_done;
            ret = GetFileSizeEx(file, &length);
            ok(ret && length.QuadPart == sizeof(input.bytes), "Unexpected fixture file length.\n");
            if (!ret || length.QuadPart != sizeof(input.bytes)) goto case_done;
            written = 0;
            ret = ReadFile(file, readback, sizeof(readback), &written, NULL);
            ok(ret && written == sizeof(readback), "Cannot read fixture bytes, error %lu.\n", GetLastError());
            if (ret && written == sizeof(readback))
                ok(!memcmp(readback, input.bytes, sizeof(input.bytes)), "Read-only mapping changed on-disk fixture.\n");
case_done:
            if (file != INVALID_HANDLE_VALUE)
            {
                ret = CloseHandle(file);
                ok(ret, "CloseHandle failed, error %lu.\n", GetLastError());
                file = INVALID_HANDLE_VALUE;
            }
            winetest_pop_context();
        }
    }
done:
    if (path[0])
    {
        ret = DeleteFileA(path);
        ok(ret, "DeleteFile failed, error %lu.\n", GetLastError());
    }
}

static void test_set_image_config(void)
{
    union { ULONGLONG align; BYTE bytes[0x1000]; } original, expected;
    union
    {
        IMAGE_LOAD_CONFIG_DIRECTORY32 narrow;
        IMAGE_LOAD_CONFIG_DIRECTORY64 wide;
        BYTE bytes[512];
    } stored;
    struct
    {
        ULONGLONG prefix[2];
        union { IMAGE_LOAD_CONFIG_DIRECTORY config; BYTE bytes[512]; } value;
        ULONGLONG suffix[2];
    } input, saved;
    BYTE readback[sizeof(original.bytes)];
    char path[MAX_PATH] = {0};
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt32;
    IMAGE_NT_HEADERS64 *nt64;
    IMAGE_SECTION_HEADER *section;
    IMAGE_DATA_DIRECTORY *directory;
    LOADED_IMAGE loaded;
    LARGE_INTEGER length;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD prefix, full, caller_prefix, header_end, output_offset, available, directory_offset, checksum_offset;
    DWORD old_size, directory_size, supplied_size, written, error, old_checksum, checksum;
    unsigned wide, i, mode, count;
    BOOL ret, success, relocate, mapped = FALSE;

    caller_prefix = FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY, SEHandlerTable);
    file = create_temp_file(path);
    ok(file != INVALID_HANDLE_VALUE, "Cannot create setter fixture, error %lu.\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE) goto done;
    for (wide = 0; wide < 2; ++wide)
    {
        prefix = wide ? FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, SEHandlerTable) :
                        FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY32, SEHandlerTable);
        full = wide ? sizeof(stored.wide) : sizeof(stored.narrow);
        count = wide == (sizeof(void *) == sizeof(ULONGLONG)) ? 22 : 8;
        for (i = 0; i < count; ++i)
        {
            mode = i < 11 ? i : i == 11 ? 0 : i == 12 ? 1 : i == 13 ? 4 : i < 18 ? i - 3 : 0;
            directory_size = !mode || mode == 11 || mode == 12 ? 0 :
                             mode == 5 ? full : mode == 6 ? prefix - 1 : prefix;
            old_size = !mode || mode == 2 || mode == 11 || mode == 12 || mode == 13 ? 0 :
                       mode == 5 ? full : mode == 14 ? 1 : mode == 8 || mode == 9 ? prefix + 1 : prefix;
            supplied_size = mode == 3 || mode == 11 || mode == 13 ? 0 : mode == 4 ? sizeof(input.value.config) :
                            mode == 14 ? 1 : mode == 7 || mode == 12 ? caller_prefix - 1 :
                            mode == 9 || mode == 10 ? caller_prefix + 1 : caller_prefix;
            winetest_push_context("Set PE%u case %u directory %lu old %lu supplied %lu", wide ? 64 : 32,
                                  i, directory_size, old_size, supplied_size);
            memset(&original, 0, sizeof(original));
            dos = (void *)original.bytes;
            dos->e_magic = IMAGE_DOS_SIGNATURE;
            dos->e_lfanew = sizeof(*dos);
            nt32 = (void *)(original.bytes + dos->e_lfanew);
            nt64 = (void *)nt32;
            nt64->Signature = IMAGE_NT_SIGNATURE;
            nt64->FileHeader.Machine = wide ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386;
            nt64->FileHeader.NumberOfSections = 1;
            nt64->FileHeader.TimeDateStamp = 0x61000002;
            nt64->FileHeader.SizeOfOptionalHeader = wide ? sizeof(nt64->OptionalHeader) : sizeof(nt32->OptionalHeader);
            nt64->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL | IMAGE_FILE_RELOCS_STRIPPED |
                                               (wide ? IMAGE_FILE_LARGE_ADDRESS_AWARE : IMAGE_FILE_32BIT_MACHINE);
#define SET_OPTIONAL(field, value) do { if (wide) nt64->OptionalHeader.field = (value); else nt32->OptionalHeader.field = (value); } while (0)
            SET_OPTIONAL(Magic, wide ? IMAGE_NT_OPTIONAL_HDR64_MAGIC : IMAGE_NT_OPTIONAL_HDR32_MAGIC);
            SET_OPTIONAL(MajorLinkerVersion, 4);
            SET_OPTIONAL(ImageBase, VA_START);
            SET_OPTIONAL(SizeOfInitializedData, 0xc00);
            SET_OPTIONAL(SectionAlignment, 0x1000);
            SET_OPTIONAL(FileAlignment, 0x200);
            SET_OPTIONAL(SizeOfImage, 0x2000);
            SET_OPTIONAL(SizeOfHeaders, 0x400);
            SET_OPTIONAL(MajorOperatingSystemVersion, 6);
            SET_OPTIONAL(MajorSubsystemVersion, 6);
            SET_OPTIONAL(Subsystem, IMAGE_SUBSYSTEM_WINDOWS_CUI);
            SET_OPTIONAL(SizeOfStackReserve, 0x100000);
            SET_OPTIONAL(SizeOfStackCommit, 0x1000);
            SET_OPTIONAL(SizeOfHeapReserve, 0x100000);
            SET_OPTIONAL(SizeOfHeapCommit, 0x1000);
            SET_OPTIONAL(NumberOfRvaAndSizes, IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
#undef SET_OPTIONAL
            section = (void *)(original.bytes + dos->e_lfanew + (wide ? sizeof(*nt64) : sizeof(*nt32)));
            memcpy(section->Name, ".data", 5);
            section->Misc.VirtualSize = 0x1000;
            section->VirtualAddress = 0x1000;
            section->SizeOfRawData = 0xc00;
            section->PointerToRawData = 0x400;
            section->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
            header_end = (BYTE *)(section + 1) - original.bytes;
            available = 0x400 - header_end;
            output_offset = header_end;
            memset(original.bytes + header_end, 0x5a, available);
            if (i >= 18)
            {
                IMAGE_DEBUG_DIRECTORY debug;
                DWORD debug_offset;

                available = i == 18 ? prefix - 1 : i == 19 ? prefix : i == 20 ? prefix + 1 : 0;
                debug_offset = 0x400 - available - sizeof(debug);
                memset(&debug, 0, sizeof(debug));
                debug.TimeDateStamp = 0x61234567;
                debug.Type = IMAGE_DEBUG_TYPE_UNKNOWN;
                memcpy(original.bytes + debug_offset, &debug, sizeof(debug));
                directory = wide ? &nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG] :
                                   &nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
                directory->VirtualAddress = debug_offset;
                directory->Size = sizeof(debug);
                output_offset = debug_offset + sizeof(debug);
            }
            directory = wide ? &nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG] :
                               &nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
            directory->VirtualAddress = directory_size ? 0x1800 : 0;
            directory->Size = directory_size;
            directory_offset = (BYTE *)directory - original.bytes;
            checksum_offset = (BYTE *)(wide ? &nt64->OptionalHeader.CheckSum : &nt32->OptionalHeader.CheckSum) - original.bytes;
            memset(&stored, 0, sizeof(stored));
#define SET_STORED(field, value) do { if (wide) stored.wide.field = (value); else stored.narrow.field = (value); } while (0)
            SET_STORED(Size, old_size);
            SET_STORED(TimeDateStamp, 0x61234567);
            SET_STORED(MajorVersion, 0x1234);
            SET_STORED(MinorVersion, 0x5678);
            SET_STORED(GlobalFlagsClear, 0x10);
            SET_STORED(GlobalFlagsSet, 0x20);
            SET_STORED(CriticalSectionDefaultTimeout, 0x10203040);
            SET_STORED(DeCommitFreeBlockThreshold, 0x12345678);
            SET_STORED(DeCommitTotalFreeThreshold, 0x23456789);
            SET_STORED(MaximumAllocationSize, 0x3456789a);
            SET_STORED(VirtualMemoryThreshold, 0x456789ab);
            SET_STORED(ProcessHeapFlags, HEAP_GROWABLE);
            SET_STORED(ProcessAffinityMask, 0x81);
            SET_STORED(CSDVersion, 0x123);
            SET_STORED(DependentLoadFlags, 0x456);
            SET_STORED(SecurityCookie, VA_START + 0x1780);
#undef SET_STORED
            memcpy(original.bytes + 0xc00, stored.bytes, sizeof(stored.bytes));
            if (directory_size) memset(original.bytes + 0xc00 + directory_size, 0x5a, sizeof(stored.bytes) - directory_size);
            memset(&input, 0xa5, sizeof(input));
            memset(&input.value.config, 0, sizeof(input.value.config));
            input.value.config.Size = supplied_size;
            input.value.config.TimeDateStamp = 0x72345678;
            input.value.config.MajorVersion = 0x2345;
            input.value.config.MinorVersion = 0x6789;
            input.value.config.GlobalFlagsClear = 0x40;
            input.value.config.GlobalFlagsSet = 0x80;
            input.value.config.CriticalSectionDefaultTimeout = 0x21314151;
            input.value.config.DeCommitFreeBlockThreshold = 0x24681357;
            input.value.config.DeCommitTotalFreeThreshold = 0x35792468;
            input.value.config.MaximumAllocationSize = 0x468a3579;
            input.value.config.VirtualMemoryThreshold = 0x579b468a;
            input.value.config.ProcessHeapFlags = HEAP_GROWABLE;
            input.value.config.ProcessAffinityMask = 0x42;
            input.value.config.CSDVersion = 0x234;
            input.value.config.DependentLoadFlags = 0x567;
            if ((i >= 11 && i <= 13) || i >= 18)
            {
                input.value.config.SEHandlerTable = VA_START + 0x1740;
                input.value.config.CodeIntegrity.Catalog = 0x2345;
                input.value.config.GuardMemcpyFunctionPointer = VA_START + 0x1780;
            }
            saved = input;
            expected = original;
            success = wide == (sizeof(void *) == sizeof(ULONGLONG)) && mode != 3 && mode != 7 && mode != 8 &&
                      i != 18 && i != 21;
            relocate = mode != 1 && mode != 9 && mode != 13 && mode != 14;
            if (success)
            {
                memcpy(expected.bytes + (relocate ? output_offset : 0xc00), input.value.bytes,
                       relocate ? sizeof(input.value.config) : supplied_size ? supplied_size : prefix);
                if (relocate)
                {
                    directory = (void *)(expected.bytes + directory_offset);
                    directory->VirtualAddress = output_offset;
                    directory->Size = prefix;
                }
            }
            if (file == INVALID_HANDLE_VALUE)
                file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            ok(file != INVALID_HANDLE_VALUE, "Cannot reopen setter fixture, error %lu.\n", GetLastError());
            if (file == INVALID_HANDLE_VALUE) goto case_done;
            written = 0;
            ret = WriteFile(file, original.bytes, sizeof(original.bytes), &written, NULL);
            ok(ret && written == sizeof(original.bytes), "WriteFile returned %d, bytes %lu, error %lu.\n", ret, written, GetLastError());
            if (!ret || written != sizeof(original.bytes)) goto case_done;
            ret = FlushFileBuffers(file);
            ok(ret, "FlushFileBuffers failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            ret = CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            ok(ret, "CloseHandle failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            memset(&loaded, 0, sizeof(loaded));
            ret = MapAndLoad(path, NULL, &loaded, TRUE, FALSE);
            ok(ret, "MapAndLoad failed, error %lu.\n", GetLastError());
            if (!ret) goto case_done;
            mapped = TRUE;
            ok(loaded.MappedAddress != NULL && loaded.SizeOfImage == sizeof(original.bytes),
               "Unexpected mapped image %p, size %lu.\n", loaded.MappedAddress, loaded.SizeOfImage);
            if (!loaded.MappedAddress || loaded.SizeOfImage != sizeof(original.bytes)) goto case_done;
            ok(!memcmp(loaded.MappedAddress, original.bytes, sizeof(original.bytes)), "MapAndLoad changed fixture bytes.\n");
            if (i >= 18)
            {
                DWORD unused = 0xcccccccc, offset;

                SetLastError(0xdeadbeef);
                offset = GetImageUnusedHeaderBytes(&loaded, &unused);
                error = GetLastError();
                ok(offset == output_offset && unused == available, "Unused header offset %lu size %lu, expected %lu/%lu.\n",
                   offset, unused, output_offset, available);
                ok(error == 0xdeadbeef, "GetImageUnusedHeaderBytes changed error to %lu.\n", error);
            }
            SetLastError(0xdeadbeef);
            ret = SetImageConfigInformation(&loaded, &input.value.config);
            error = GetLastError();
            ok(ret == success, "SetImageConfigInformation returned %d, expected %d.\n", ret, success);
            ok(error == 0xdeadbeef, "Unexpected error %lu.\n", error);
            ok(!memcmp(&input, &saved, sizeof(input)), "Setter changed input or guards.\n");
            ok(loaded.MappedAddress != NULL && loaded.SizeOfImage == sizeof(expected.bytes),
               "Setter changed mapping unexpectedly, %p size %lu.\n", loaded.MappedAddress, loaded.SizeOfImage);
            if (loaded.MappedAddress && loaded.SizeOfImage == sizeof(expected.bytes))
                ok(!memcmp(loaded.MappedAddress, expected.bytes, sizeof(expected.bytes)), "Unexpected mapped configuration, header, or tail bytes.\n");
            SetLastError(0xdeadbeef);
            ret = UnMapAndLoad(&loaded);
            error = GetLastError();
            mapped = FALSE;
            ok(ret, "UnMapAndLoad failed, error %lu.\n", error);
            ok(error == 0xdeadbeef, "UnMapAndLoad changed error to %lu.\n", error);
            if (!ret) goto case_done;
            ret = CheckSumMappedFile(expected.bytes, sizeof(expected.bytes), &old_checksum, &checksum) != NULL;
            ok(ret, "Cannot calculate expected checksum.\n");
            if (!ret) goto case_done;
            memcpy(expected.bytes + checksum_offset, &checksum, sizeof(checksum));
            file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            ok(file != INVALID_HANDLE_VALUE, "Cannot read setter fixture, error %lu.\n", GetLastError());
            if (file == INVALID_HANDLE_VALUE) goto case_done;
            ret = GetFileSizeEx(file, &length);
            ok(ret && length.QuadPart == sizeof(expected.bytes), "Unexpected persisted file size.\n");
            if (!ret || length.QuadPart != sizeof(expected.bytes)) goto case_done;
            written = 0;
            ret = ReadFile(file, readback, sizeof(readback), &written, NULL);
            ok(ret && written == sizeof(readback), "ReadFile returned %d, bytes %lu, error %lu.\n", ret, written, GetLastError());
            if (ret && written == sizeof(readback))
                ok(!memcmp(readback, expected.bytes, sizeof(readback)), "Unexpected persisted configuration or unrelated bytes.\n");
case_done:
            if (mapped)
            {
                ret = UnMapAndLoad(&loaded);
                ok(ret, "Cleanup UnMapAndLoad failed, error %lu.\n", GetLastError());
                mapped = FALSE;
            }
            if (file != INVALID_HANDLE_VALUE)
            {
                ret = CloseHandle(file);
                ok(ret, "CloseHandle failed, error %lu.\n", GetLastError());
                file = INVALID_HANDLE_VALUE;
            }
            winetest_pop_context();
        }
    }
done:
    if (path[0])
    {
        ret = DeleteFileA(path);
        ok(ret, "DeleteFile failed, error %lu.\n", GetLastError());
    }
}

START_TEST(image)
{
    test_get_digest_stream();
    test_bind_image_ex();
    test_image_load();
    test_image_config();
    test_set_image_config();
}
