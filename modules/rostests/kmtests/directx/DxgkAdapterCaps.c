/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reported WDDM version and capability gating
 *
 * The version a client sees decides which APIs it will call.  Claiming more
 * than the weakest participant implements is how an app is told a feature
 * works and then finds it does not.
 */

#include <kmt_test.h>
#include "object_core.h"
#include "feature_query_core.h"

static VOID InitInput(_Out_ PDXGK_CAPS_INPUT Input)
{
    RtlZeroMemory(Input, sizeof(*Input));
    Input->MiniportDeclaredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_3_2;
    Input->OsCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_3_2;
    Input->ProviderCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_3_2;
    Input->ConfiguredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_3_2;
    Input->HasRenderCallbacks = TRUE;
}

static VOID TestVersionIsTheMinimum(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_3_2); }

    /* Any one ceiling lowers the reported version on its own. */
    InitInput(&Input);
    Input.OsCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_1_3;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_1_3); }

    InitInput(&Input);
    Input.ProviderCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_2_0); }

    InitInput(&Input);
    Input.MiniportDeclaredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_2_0); }

    InitInput(&Input);
    Input.ConfiguredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_4;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_2_4); }

    /* The lowest of several wins, not the last one examined. */
    InitInput(&Input);
    Input.MiniportDeclaredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_9;
    Input.OsCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_1_3;
    Input.ProviderCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_0;
    Input.ConfiguredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_4;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_1_3); }

    /* A miniport below every ceiling keeps its own lower number. */
    InitInput(&Input);
    Input.MiniportDeclaredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_1_3;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_1_3); }

    /* Zero means "no opinion" and must not clamp the result to zero. */
    InitInput(&Input);
    Input.OsCompletedLevel = 0;
    Input.ProviderCompletedLevel = 0;
    Input.ConfiguredLevel = 0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_LEVEL_WDDM_3_2); }
}

static VOID TestRenderSupport(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    ok_bool_true(DxgkCapsCoreRenderSupported(&Input), "full adapter renders");

    /* A display-only adapter never renders, whatever version it declares. */
    InitInput(&Input);
    Input.DisplayOnly = TRUE;
    ok_bool_false(DxgkCapsCoreRenderSupported(&Input), "display-only cannot render");

    /*
     * Render support is a statement about the callbacks a submission actually
     * travels through, not about the version a miniport declares.
     */
    InitInput(&Input);
    Input.HasRenderCallbacks = FALSE;
    ok_bool_false(DxgkCapsCoreRenderSupported(&Input), "no render callbacks, no render");
}

static VOID TestFeatureGating(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    Input.OsCompletedLevel = DXGK_CAPS_CORE_LEVEL_WDDM_1_3;

    /* Everything above the reported version must read as unavailable, so a
     * client does not call an API the stack cannot honour. */
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_LEVEL_WDDM_1_3), "at the cap");
    ok_bool_false(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_LEVEL_WDDM_2_0), "above the cap");
    ok_bool_false(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_LEVEL_WDDM_3_0), "far above the cap");

    InitInput(&Input);
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_LEVEL_WDDM_2_0), "below the cap");
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_LEVEL_WDDM_3_2), "at the cap");

    /* A feature removed after 2.3 must not leak into 2.4+ merely because the
     * version comparison is monotonic. */
    Input.ConfiguredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_3;
    ok_bool_true(DxgkCapsCoreFeatureAvailableInRange(
        &Input,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_3),
        "bounded feature at supported level");
    Input.ConfiguredLevel = DXGK_CAPS_CORE_LEVEL_WDDM_2_4;
    ok_bool_false(DxgkCapsCoreFeatureAvailableInRange(
        &Input,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_3),
        "bounded feature above removal level");
    ok_bool_false(DxgkCapsCoreFeatureAvailableInRange(
        &Input,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_4,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_3),
        "invalid range");
}

static VOID TestInterfaceSelectorNormalization(VOID)
{
    static const struct
    {
        ULONG Selector;
        ULONG Level;
    } Cases[] =
    {
        { 0x1052, DXGK_CAPS_CORE_LEVEL_WDDM_1_0 },
        { 0x1053, DXGK_CAPS_CORE_LEVEL_WDDM_1_0 },
        { 0x2005, DXGK_CAPS_CORE_LEVEL_WDDM_1_1 },
        { 0x300E, DXGK_CAPS_CORE_LEVEL_WDDM_1_2 },
        { 0x4002, DXGK_CAPS_CORE_LEVEL_WDDM_1_3 },
        { 0x4003, DXGK_CAPS_CORE_LEVEL_WDDM_1_3 },
        { 0x5020, DXGK_CAPS_CORE_LEVEL_WDDM_2_0 },
        { 0x5022, DXGK_CAPS_CORE_LEVEL_WDDM_2_0 },
        { 0x5023, DXGK_CAPS_CORE_LEVEL_WDDM_2_0 },
        { 0x6003, DXGK_CAPS_CORE_LEVEL_WDDM_2_1 },
        { 0x6010, DXGK_CAPS_CORE_LEVEL_WDDM_2_1 },
        { 0x6011, DXGK_CAPS_CORE_LEVEL_WDDM_2_1 },
        { 0x700A, DXGK_CAPS_CORE_LEVEL_WDDM_2_2 },
        { 0x8001, DXGK_CAPS_CORE_LEVEL_WDDM_2_3 },
        { 0x9006, DXGK_CAPS_CORE_LEVEL_WDDM_2_4 },
        { 0xA00B, DXGK_CAPS_CORE_LEVEL_WDDM_2_5 },
        { 0xB004, DXGK_CAPS_CORE_LEVEL_WDDM_2_6 },
        { 0xC004, DXGK_CAPS_CORE_LEVEL_WDDM_2_7 },
        { 0xD001, DXGK_CAPS_CORE_LEVEL_WDDM_2_8 },
        { 0xE003, DXGK_CAPS_CORE_LEVEL_WDDM_2_9 },
        { 0xF003, DXGK_CAPS_CORE_LEVEL_WDDM_3_0 },
        { 0x10004, DXGK_CAPS_CORE_LEVEL_WDDM_3_1 },
        { 0x11007, DXGK_CAPS_CORE_LEVEL_WDDM_3_2 },
        { 0x11008, DXGK_CAPS_CORE_LEVEL_WDDM_3_2 },
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(Cases[Index].Selector),
                    Cases[Index].Level);
    }
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x1054), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x5024), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x7000), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x700B), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x11009), 0);
    ok_eq_ulong(DxgkCapsCoreInterfaceVersionToLevel(0x12001), 0);

    ok_bool_true(DxgkCapsCoreInterfaceVersionPermitted(
        0x5022, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "historical selector at configured level");
    ok_bool_true(DxgkCapsCoreInterfaceVersionPermitted(
        0x4003, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "WDDM 1.3 selector below configured level");
    ok_bool_false(DxgkCapsCoreInterfaceVersionPermitted(
        0x2005, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "pre-WDDM 1.2 selector is outside the Win11 profile");
    ok_bool_false(DxgkCapsCoreInterfaceVersionPermitted(
        0x6003, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "known selector above configured level");
    ok_bool_false(DxgkCapsCoreInterfaceVersionPermitted(
        0x7000, DXGK_CAPS_CORE_LEVEL_WDDM_3_2),
        "unknown selector inside a known family");
    ok_bool_true(DxgkCapsCoreInterfaceVersionPermitted(
        0x11008, 0),
        "zero configured level does not clamp a known selector");

    ok_bool_true(DxgkCapsCoreInterfaceVersionAtLeast(
        0x5022, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "historical selector meets its level");
    ok_bool_false(DxgkCapsCoreInterfaceVersionAtLeast(
        0x5022, DXGK_CAPS_CORE_LEVEL_WDDM_2_1),
        "historical selector below required level");
    ok_bool_false(DxgkCapsCoreInterfaceVersionAtLeast(
        0x7000, DXGK_CAPS_CORE_LEVEL_WDDM_2_0),
        "unknown selector cannot satisfy minimum");
    ok_bool_true(DxgkCapsCoreInterfaceVersionInRange(
        0x6011,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_1,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_1),
        "exact-level interface range");
    ok_bool_false(DxgkCapsCoreInterfaceVersionInRange(
        0x700A,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_1),
        "interface above bounded range");
}

static VOID
TestFeatureQueryNegotiation(VOID)
{
    DXGK_FEATURE_QUERY_CORE_INPUT Input;
    DXGK_FEATURE_QUERY_CORE_RESULT Result;

    RtlZeroMemory(&Input, sizeof(Input));
    Result.Version = MAXUSHORT;
    Result.Value = MAXUSHORT;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "unknown feature result");
    ok_eq_ulong(Result.Version, 0);
    ok_eq_ulong(Result.Value, 0);

    RtlZeroMemory(&Input, sizeof(Input));
    Input.KnownFeature = TRUE;
    Input.RequiresDriverSupport = TRUE;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "known but unsupported feature result");
    ok_eq_ulong(Result.Version, 0);
    ok_eq_ulong(Result.Value, DXGK_FEATURE_QUERY_RESULT_KNOWN);

    /*
     * Driver support remains observable even when the OS deliberately keeps
     * a known feature disabled.  Version is meaningful only when Enabled is
     * set.
     */
    Input.DriverResponseValid = TRUE;
    Input.SupportedByDriver = TRUE;
    Input.SupportedOnCurrentConfig = TRUE;
    Input.DriverMinVersion = 1;
    Input.DriverMaxVersion = 1;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "disabled OS with valid driver support");
    ok_eq_ulong(Result.Version, 0);
    ok_eq_ulong(Result.Value,
                DXGK_FEATURE_QUERY_RESULT_KNOWN |
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_BY_DRIVER |
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG);

    Input.DriverMinVersion = 2;
    Input.DriverMaxVersion = 1;
    ok_bool_false(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                  "malformed driver version range");
    ok_eq_ulong(Result.Version, 0);
    ok_eq_ulong(Result.Value, 0);

    RtlZeroMemory(&Input, sizeof(Input));
    Input.KnownFeature = TRUE;
    Input.RequiresDriverSupport = TRUE;
    Input.OsSupported = TRUE;
    Input.OsSupportedOnCurrentConfig = TRUE;
    Input.OsMinVersion = 1;
    Input.OsMaxVersion = 3;
    Input.DriverResponseValid = TRUE;
    Input.SupportedByDriver = TRUE;
    Input.SupportedOnCurrentConfig = TRUE;
    Input.DriverMinVersion = 2;
    Input.DriverMaxVersion = 5;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "overlapping feature versions");
    ok_eq_ulong(Result.Version, 3);
    ok_eq_ulong(Result.Value, DXGK_FEATURE_QUERY_RESULT_VALID_MASK);

    Input.DriverMinVersion = 4;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "non-overlapping feature versions");
    ok_eq_ulong(Result.Version, 0);
    ok_eq_ulong(Result.Value,
                DXGK_FEATURE_QUERY_RESULT_KNOWN |
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_BY_DRIVER |
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG);

    RtlZeroMemory(&Input, sizeof(Input));
    Input.KnownFeature = TRUE;
    Input.OsSupported = TRUE;
    Input.OsSupportedOnCurrentConfig = TRUE;
    Input.OsMinVersion = 1;
    Input.OsMaxVersion = 1;
    ok_bool_true(DxgkFeatureQueryCoreEvaluate(&Input, &Result),
                 "OS-only feature result");
    ok_eq_ulong(Result.Version, 1);
    ok_eq_ulong(Result.Value,
                DXGK_FEATURE_QUERY_RESULT_ENABLED |
                DXGK_FEATURE_QUERY_RESULT_KNOWN |
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG);
}

static VOID TestDriverCapsQuerySize(VOID)
{
    static const struct
    {
        ULONG Selector;
        ULONG Size;
    } Cases[] =
    {
        { 0x104D, 0 },
        { 0x1052, 0x150 },
        { 0x2000, 0 },
        { 0x2005, 0x210 },
        { 0x300E, 0x220 },
        { 0x4002, 0x228 },
        { 0x5003, 0 },
        { 0x5010, 0x228 },
        { 0x5023, 0x240 },
        { 0x8001, 0x240 },
        { 0x9006, 0x248 },
        { 0xD001, 0x248 },
        { 0xE003, 0x250 },
        { 0x11007, 0x250 },
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        ok(DxgkCapsCoreDriverCapsSize(Cases[Index].Selector) == Cases[Index].Size,
           "DRIVERCAPS size for 0x%lx is 0x%lx, expected 0x%lx\n", Cases[Index].Selector,
           DxgkCapsCoreDriverCapsSize(Cases[Index].Selector), Cases[Index].Size);
    }
}

static VOID TestPhysicalAdapterCapsQuerySize(VOID)
{
    ok_eq_ulong(DxgkCapsCorePhysicalAdapterCapsSize(0x5023), 0x14);
    ok_eq_ulong(DxgkCapsCorePhysicalAdapterCapsSize(0x6003), 0x18);
    ok_eq_ulong(DxgkCapsCorePhysicalAdapterCapsSize(0xB004), 0x18);
    ok_eq_ulong(DxgkCapsCorePhysicalAdapterCapsSize(0xC004), 0x20);
    ok_eq_ulong(DxgkCapsCorePhysicalAdapterCapsSize(0x11007), 0x20);
}

static VOID TestStablePowerCounting(VOID)
{
    LONG Process1 = 0, Process2 = 0, Adapter = 0;

    ok_bool_false(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, FALSE), "disable without a request stays silent");
    ok_eq_long(Process1, 0);
    ok_bool_true(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, TRUE), "first request reaches the miniport");
    ok_bool_false(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, TRUE), "second request of a process stays silent");
    ok_bool_false(DxgkCapsCoreStablePowerSet(&Process2, &Adapter, TRUE), "second process stays silent");
    ok_eq_long(Adapter, 2);
    ok_bool_false(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, FALSE), "a process still holds a request");
    ok_bool_false(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, FALSE), "another process still holds a request");
    ok_eq_long(Adapter, 1);
    ok_bool_true(DxgkCapsCoreStablePowerSet(&Process2, &Adapter, FALSE), "last release reaches the miniport");
    ok_eq_long(Adapter, 0);

    ok_bool_true(DxgkCapsCoreStablePowerSet(&Process1, &Adapter, TRUE), "request again");
    (VOID)DxgkCapsCoreStablePowerSet(&Process1, &Adapter, TRUE);
    ok_bool_true(DxgkCapsCoreStablePowerRelease(&Process1, &Adapter), "process exit drops every request at once");
    ok_eq_long(Process1, 0);
    ok_eq_long(Adapter, 0);
    ok_bool_false(DxgkCapsCoreStablePowerRelease(&Process1, &Adapter), "exit without a request stays silent");
}

static VOID TestWddmCaps(VOID)
{
    DXGK_WDDM_CAPS_CORE_INPUT Input;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.HwSchSupportState = 2;
    Input.HwSchEnabled = TRUE;
    Input.HwFlipQueueSupportState = 1;
    Input.HwFlipQueueEnabled = TRUE;
    Input.IndependentVidPnVSyncControl = TRUE;
    Input.DisplayableSupport = TRUE;
    Input.NativeGpuFence = TRUE;
    ok_eq_ulong(DxgkWddmCapsCore27(&Input), 0UL);
    ok_eq_ulong(DxgkWddmCapsCore29(&Input), 0UL);
    ok_eq_ulong(DxgkWddmCapsCore30(&Input), 0UL);
    ok_eq_ulong(DxgkWddmCapsCore31(&Input), 0UL);

    /* Win11 NVIDIA, hardware scheduling off: WDDM_2_7_CAPS 0x9. */
    RtlZeroMemory(&Input, sizeof(Input));
    Input.RenderCapable = TRUE;
    Input.HwSchSupportState = 2;
    Input.IndependentVidPnVSyncControl = TRUE;
    ok_eq_ulong(DxgkWddmCapsCore27(&Input), 0x9UL);
    ok_eq_ulong(DxgkWddmCapsCore29(&Input), 0x2UL);
    Input.HwSchEnabled = TRUE;
    ok_eq_ulong(DxgkWddmCapsCore27(&Input), 0xBUL);
    ok_eq_ulong(DxgkWddmCapsCore29(&Input), 0x6UL);
    Input.HwSchSupportState = 1;
    ok_eq_ulong(DxgkWddmCapsCore27(&Input) & 0x1UL, 0x1UL);
    ok_eq_ulong(DxgkWddmCapsCore29(&Input) & 0x3UL, 0x1UL);
    Input.HwSchSupportState = 3;
    ok_eq_ulong(DxgkWddmCapsCore29(&Input) & 0x3UL, 0x3UL);
    Input.HwSchSupportState = 0;
    Input.HwSchEnabled = FALSE;
    ok_eq_ulong(DxgkWddmCapsCore27(&Input), 0x8UL);
    ok_eq_ulong(DxgkWddmCapsCore29(&Input), 0UL);

    Input.HwFlipQueueSupportState = 2;
    ok_eq_ulong(DxgkWddmCapsCore30(&Input), 0x2UL);
    Input.HwFlipQueueEnabled = TRUE;
    Input.DisplayableSupport = TRUE;
    ok_eq_ulong(DxgkWddmCapsCore30(&Input), 0xEUL);
    ok_eq_ulong(DxgkWddmCapsCore31(&Input), 0UL);
    Input.NativeGpuFence = TRUE;
    ok_eq_ulong(DxgkWddmCapsCore31(&Input), 0x1UL);
}

static VOID TestAdapterTypeBits(VOID)
{
    /* Win11: NVIDIA (WDDM 3.2) 0x2313, Basic Render (WDDM 3.2) 0x105, Basic Display (WDDM 1.3, ACGSupported value) 0x10a. */
    ok_bool_true(DxgkAdapterTypeCoreAcgSupported(FALSE, 0x3200), "WDDM 3.2 driver");
    ok_bool_true(DxgkAdapterTypeCoreAcgSupported(FALSE, 0x2200), "WDDM 2.2 driver");
    ok_bool_false(DxgkAdapterTypeCoreAcgSupported(FALSE, 0x2100), "WDDM 2.1 driver");
    ok_bool_false(DxgkAdapterTypeCoreAcgSupported(FALSE, 0x1300), "WDDM 1.3 driver");
    ok_bool_true(DxgkAdapterTypeCoreAcgSupported(TRUE, 0x1300), "ACGSupported value");

    ok_bool_true(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x3200, 0x11007, TRUE, TRUE, TRUE), "WDDM 3.2 full driver");
    ok_bool_false(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x3200, 0x11007, TRUE, TRUE, FALSE), "render without MPO3");
    ok_bool_true(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x3200, 0x11007, TRUE, FALSE, FALSE), "display-only driver");
    ok_bool_false(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x3200, 0x11007, FALSE, TRUE, TRUE), "no SetTimingsFromVidPn");
    ok_bool_false(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x2100, 0x11007, TRUE, TRUE, TRUE), "WDDM 2.1 caps");
    ok_bool_false(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x3200, 0x7006, TRUE, TRUE, TRUE), "DDI before 0x7007");
    ok_bool_true(DxgkAdapterTypeCoreSetTimingsFromVidPn(0x2200, 0x7007, TRUE, TRUE, TRUE), "lowest accepted pair");
}

START_TEST(DxgkAdapterCaps)
{
    TestAdapterTypeBits();
    TestWddmCaps();
    TestDriverCapsQuerySize();
    TestStablePowerCounting();
    TestPhysicalAdapterCapsQuerySize();
    TestVersionIsTheMinimum();
    TestRenderSupport();
    TestFeatureGating();
    TestInterfaceSelectorNormalization();
    TestFeatureQueryNegotiation();
}

/* EOF */
