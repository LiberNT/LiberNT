/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for TPM Base Services contexts and command submission
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <tbs.h>

#define TPM_ST_NO_SESSIONS  0x8001
#define TPM_RC_SUCCESS      0

static const BYTE GetRandom16[] =
{
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x7B, 0x00, 0x10
};

static const BYTE UnknownCommand[] =
{
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0xFF, 0xFF
};

static UINT32 ReadBigEndian32(const BYTE *Buffer)
{
    return ((UINT32)Buffer[0] << 24) | ((UINT32)Buffer[1] << 16) | ((UINT32)Buffer[2] << 8) | Buffer[3];
}

static USHORT ReadBigEndian16(const BYTE *Buffer)
{
    return (USHORT)((Buffer[0] << 8) | Buffer[1]);
}

static void TestContextParameters(void)
{
    TBS_CONTEXT_PARAMS2 Params2;
    TBS_CONTEXT_PARAMS Params;
    TBS_HCONTEXT Context;
    TBS_RESULT Result;

    Context = (TBS_HCONTEXT)(ULONG_PTR)0x55;
    Result = Tbsi_Context_Create(NULL, &Context);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    ok_ptr(Context, (TBS_HCONTEXT)(ULONG_PTR)0x55);

    ZeroMemory(&Params2, sizeof(Params2));
    Params2.version = TBS_CONTEXT_VERSION_TWO;
    Params2.includeTpm20 = 1;
    Result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&Params2, NULL);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_OUTPUT_POINTER);

    Params.version = 3;
    Context = NULL;
    Result = Tbsi_Context_Create(&Params, &Context);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_CONTEXT_PARAM);
    ok_ptr(Context, NULL);

    Result = Tbsip_Context_Close((TBS_HCONTEXT)(ULONG_PTR)0x1234);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_CONTEXT);
    Result = Tbsip_Cancel_Commands((TBS_HCONTEXT)(ULONG_PTR)0x1234);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_CONTEXT);
}

static void TestDeviceInfo(void)
{
    TPM_DEVICE_INFO Info;
    TBS_RESULT Result;

    Result = Tbsi_GetDeviceInfo(sizeof(Info), NULL);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    Result = Tbsi_GetDeviceInfo(sizeof(Info) - 1, &Info);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);

    FillMemory(&Info, sizeof(Info), 0xCC);
    Result = Tbsi_GetDeviceInfo(sizeof(Info), &Info);
    ok_hex(Result, TBS_SUCCESS);
    ok_long(Info.structVersion, 1);
    ok_long(Info.tpmVersion, TPM_VERSION_20);
    ok(Info.tpmInterfaceType != TPM_IFTYPE_UNKNOWN && Info.tpmInterfaceType != 0xCCCCCCCC,
       "tpmInterfaceType %lu\n", (ULONG)Info.tpmInterfaceType);
}

static void TestSubmit(TBS_HCONTEXT Context)
{
    BYTE Response[64];
    UINT32 ResponseSize;
    TBS_RESULT Result;

    ResponseSize = sizeof(Response);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, TBS_SUCCESS);
    ok_long(ResponseSize, 10 + 2 + 16);
    if (Result == TBS_SUCCESS && ResponseSize >= 12)
    {
        ok_hex(ReadBigEndian16(Response), TPM_ST_NO_SESSIONS);
        ok_long(ReadBigEndian32(Response + 2), ResponseSize);
        ok_hex(ReadBigEndian32(Response + 6), TPM_RC_SUCCESS);
        ok_long(ReadBigEndian16(Response + 10), 16);
    }

    ResponseSize = sizeof(Response);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  UnknownCommand, sizeof(UnknownCommand), Response, &ResponseSize);
    ok_hex(Result, TBS_SUCCESS);
    ok_long(ResponseSize, 10);
    if (Result == TBS_SUCCESS && ResponseSize == 10)
    {
        ok_long(ReadBigEndian32(Response + 2), 10);
        ok(ReadBigEndian32(Response + 6) != TPM_RC_SUCCESS, "unknown command code succeeded\n");
    }

    ResponseSize = 12;
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_INSUFFICIENT_BUFFER);

    ResponseSize = sizeof(Response);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ONE, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, 150,
                                  GetRandom16, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  NULL, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, 9, Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_BAD_PARAMETER);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, sizeof(GetRandom16), Response, NULL);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_OUTPUT_POINTER);

    Result = Tbsip_Cancel_Commands(Context);
    ok_hex(Result, TBS_SUCCESS);
}

START_TEST(Tbs)
{
    TBS_CONTEXT_PARAMS2 Params2;
    TBS_CONTEXT_PARAMS Params;
    TBS_HCONTEXT Context = NULL;
    TBS_HCONTEXT Second = NULL;
    BYTE Response[64];
    UINT32 ResponseSize;
    TBS_RESULT Result;

    TestContextParameters();

    ZeroMemory(&Params2, sizeof(Params2));
    Params2.version = TBS_CONTEXT_VERSION_TWO;
    Params2.includeTpm20 = 1;
    Result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&Params2, &Context);
    if (Result == (TBS_RESULT)TBS_E_TPM_NOT_FOUND)
    {
        skip("No TPM 2.0 device\n");
        return;
    }
    ok_hex(Result, TBS_SUCCESS);
    ok(Context != NULL, "Context is NULL\n");
    if (Result != TBS_SUCCESS)
        return;

    Params.version = TBS_CONTEXT_VERSION_ONE;
    Result = Tbsi_Context_Create(&Params, &Second);
    ok_hex(Result, (TBS_RESULT)TBS_E_TPM_NOT_FOUND);

    Params2.includeTpm20 = 0;
    Params2.includeTpm12 = 1;
    Result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&Params2, &Second);
    ok_hex(Result, (TBS_RESULT)TBS_E_TPM_NOT_FOUND);

    Params2.includeTpm12 = 0;
    Params2.includeTpm20 = 1;
    Result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&Params2, &Second);
    ok_hex(Result, TBS_SUCCESS);
    ok(Second != NULL && Second != Context, "Second %p Context %p\n", Second, Context);

    TestDeviceInfo();
    TestSubmit(Context);

    if (Result == TBS_SUCCESS)
    {
        Result = Tbsip_Context_Close(Second);
        ok_hex(Result, TBS_SUCCESS);
        Result = Tbsip_Context_Close(Second);
        ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_CONTEXT);
    }

    Result = Tbsip_Context_Close(Context);
    ok_hex(Result, TBS_SUCCESS);
    ResponseSize = sizeof(Response);
    Result = Tbsip_Submit_Command(Context, TBS_COMMAND_LOCALITY_ZERO, TBS_COMMAND_PRIORITY_NORMAL,
                                  GetRandom16, sizeof(GetRandom16), Response, &ResponseSize);
    ok_hex(Result, (TBS_RESULT)TBS_E_INVALID_CONTEXT);
}
