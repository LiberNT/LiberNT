/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windows AppModel definitions
 * COPYRIGHT:   Copyright 2024 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AppPolicyProcessTerminationMethod
{
    AppPolicyProcessTerminationMethod_ExitProcess = 0,
    AppPolicyProcessTerminationMethod_TerminateProcess = 1,
} AppPolicyProcessTerminationMethod;

typedef enum AppPolicyThreadInitializationType
{
    AppPolicyThreadInitializationType_None = 0,
    AppPolicyThreadInitializationType_InitializeWinRT = 1,
} AppPolicyThreadInitializationType;

typedef enum AppPolicyShowDeveloperDiagnostic
{
    AppPolicyShowDeveloperDiagnostic_None = 0,
    AppPolicyShowDeveloperDiagnostic_ShowUI = 1,
} AppPolicyShowDeveloperDiagnostic;

typedef enum AppPolicyWindowingModel
{
    AppPolicyWindowingModel_None = 0,
    AppPolicyWindowingModel_Universal = 1,
    AppPolicyWindowingModel_ClassicDesktop = 2,
    AppPolicyWindowingModel_ClassicPhone = 3
} AppPolicyWindowingModel;

typedef enum AppPolicyMediaFoundationCodecLoading
{
    AppPolicyMediaFoundationCodecLoading_All = 0,
} AppPolicyMediaFoundationCodecLoading;

#pragma pack(push, 4)
typedef struct PACKAGE_VERSION
{
    union
    {
        UINT64 Version;
        struct
        {
            USHORT Revision;
            USHORT Build;
            USHORT Minor;
            USHORT Major;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
} PACKAGE_VERSION;

typedef struct PACKAGE_ID
{
    UINT32 reserved;
    UINT32 processorArchitecture;
    PACKAGE_VERSION version;
    PWSTR name;
    PWSTR publisher;
    PWSTR resourceId;
    PWSTR publisherId;
} PACKAGE_ID;
#pragma pack(pop)

#define PACKAGE_PROPERTY_FRAMEWORK          0x00000001
#define PACKAGE_PROPERTY_RESOURCE           0x00000002
#define PACKAGE_PROPERTY_BUNDLE             0x00000004
#define PACKAGE_PROPERTY_OPTIONAL           0x00000008
#define PACKAGE_FILTER_HEAD                 0x00000010
#define PACKAGE_FILTER_DIRECT               0x00000020
#define PACKAGE_FILTER_RESOURCE             0x00000040
#define PACKAGE_FILTER_BUNDLE               0x00000080
#define PACKAGE_INFORMATION_BASIC           0x00000000
#define PACKAGE_INFORMATION_FULL            0x00000100
#define PACKAGE_PROPERTY_DEVELOPMENT_MODE   0x00010000
#define PACKAGE_FILTER_OPTIONAL             0x00020000
#define PACKAGE_PROPERTY_IS_IN_RELATED_SET  0x00040000
#define PACKAGE_FILTER_IS_IN_RELATED_SET    PACKAGE_PROPERTY_IS_IN_RELATED_SET
#define PACKAGE_PROPERTY_STATIC             0x00080000
#define PACKAGE_FILTER_STATIC               PACKAGE_PROPERTY_STATIC
#define PACKAGE_PROPERTY_DYNAMIC            0x00100000
#define PACKAGE_FILTER_DYNAMIC              PACKAGE_PROPERTY_DYNAMIC

WINBASEAPI
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
GetPackageFamilyName(
    _In_ HANDLE hProcess,
    _Inout_ UINT32* packageFamilyNameLength,
    _Out_writes_opt_(*packageFamilyNameLength) PWSTR packageFamilyName);

WINBASEAPI
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
GetPackageId(
    _In_ HANDLE hProcess,
    _Inout_ UINT32* bufferLength,
    _Out_writes_bytes_opt_(*bufferLength) BYTE* buffer);

WINBASEAPI
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
GetCurrentPackageFullName(
    _Inout_ UINT32* packageFullNameLength,
    _Out_writes_opt_(*packageFullNameLength) PWSTR packageFullName);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
_On_failure_(_Unchanged_(*count))
_On_failure_(_Unchanged_(*bufferLength))
LONG
WINAPI
FindPackagesByPackageFamily(
    _In_ PCWSTR packageFamilyName,
    _In_ UINT32 packageFilters,
    _Inout_ UINT32* count,
    _Out_writes_opt_(*count) PWSTR* packageFullNames,
    _Inout_ UINT32* bufferLength,
    _Out_writes_opt_(*bufferLength) WCHAR* buffer,
    _Out_writes_opt_(*count) UINT32* packageProperties);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
AppPolicyGetProcessTerminationMethod(
    _In_ HANDLE processToken,
    _Out_ AppPolicyProcessTerminationMethod* policy);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
AppPolicyGetThreadInitializationType(
    _In_ HANDLE processToken,
    _Out_ AppPolicyThreadInitializationType* policy);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
AppPolicyGetShowDeveloperDiagnostic(
    _In_ HANDLE processToken,
    _Out_ AppPolicyShowDeveloperDiagnostic* policy);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
AppPolicyGetWindowingModel(
    _In_ HANDLE processToken,
    _Out_ AppPolicyWindowingModel* policy);

WINBASEAPI
_Check_return_
_Success_(return == ERROR_SUCCESS)
LONG
WINAPI
AppPolicyGetMediaFoundationCodecLoading(
    _In_ HANDLE processToken,
    _Out_ AppPolicyMediaFoundationCodecLoading* policy);

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef PACKAGE_DEPENDENCY_RANK_DEFAULT
#define PACKAGE_DEPENDENCY_RANK_DEFAULT 0
typedef enum PackagePathType {
    PackagePathType_Install = 0,
    PackagePathType_Mutable = 1,
    PackagePathType_Effective = 2,
    PackagePathType_MachineExternal = 3,
    PackagePathType_UserExternal = 4,
    PackagePathType_EffectiveExternal = 5
} PackagePathType;

typedef enum _PackageOrigin {
    PackageOrigin_Unknown = 0,
    PackageOrigin_Unsigned = 1,
    PackageOrigin_Inbox = 2,
    PackageOrigin_Store = 3,
    PackageOrigin_DeveloperUnsigned = 4,
    PackageOrigin_DeveloperSigned = 5,
    PackageOrigin_LineOfBusiness = 6,
    PackageOrigin_SignedSBOM = 7
} PackageOrigin;

typedef enum _PackageOrigin2 {
    PackageOrigin2_Unknown = 0,
    PackageOrigin2_Unsigned = 1,
    PackageOrigin2_Inbox = 2,
    PackageOrigin2_Store = 3,
    PackageOrigin2_DeveloperUnsigned = 4,
    PackageOrigin2_DeveloperSigned = 5,
    PackageOrigin2_LineOfBusiness = 6,
    PackageOrigin2_SignedSBOM = 7
} PackageOrigin2;

typedef enum AppModelPackageFeature {
    AppModelPackageFeature_SignedSBOM = 1
} AppModelPackageFeature;

typedef enum CreatePackageDependencyOptions {
    CreatePackageDependencyOptions_None = 0,
    CreatePackageDependencyOptions_DoNotVerifyDependencyResolution = 1,
    CreatePackageDependencyOptions_ScopeIsSystem = 2
} CreatePackageDependencyOptions;

typedef enum PackageDependencyLifetimeKind {
    PackageDependencyLifetimeKind_Process = 0,
    PackageDependencyLifetimeKind_FilePath = 1,
    PackageDependencyLifetimeKind_RegistryKey = 2
} PackageDependencyLifetimeKind;

typedef enum AddPackageDependencyOptions {
    AddPackageDependencyOptions_None = 0,
    AddPackageDependencyOptions_PrependIfRankCollision = 1
} AddPackageDependencyOptions;

typedef enum AddPackageDependencyOptions2 {
    AddPackageDependencyOptions2_None = 0,
    AddPackageDependencyOptions2_PrependIfRankCollision = 1,
    AddPackageDependencyOptions2_SpecifiedPackageFamilyOnly = 2
} AddPackageDependencyOptions2;

typedef enum PackageDependencyProcessorArchitectures {
    PackageDependencyProcessorArchitectures_None = 0,
    PackageDependencyProcessorArchitectures_Neutral = 1,
    PackageDependencyProcessorArchitectures_X86 = 2,
    PackageDependencyProcessorArchitectures_X64 = 4,
    PackageDependencyProcessorArchitectures_Arm = 8,
    PackageDependencyProcessorArchitectures_Arm64 = 16,
    PackageDependencyProcessorArchitectures_X86A64 = 32
} PackageDependencyProcessorArchitectures;

typedef enum AppPolicyLifecycleManagement {
    AppPolicyLifecycleManagement_Unmanaged = 0,
    AppPolicyLifecycleManagement_Managed = 1
} AppPolicyLifecycleManagement;

typedef enum AppPolicyClrCompat {
    AppPolicyClrCompat_Other = 0,
    AppPolicyClrCompat_ClassicDesktop = 1,
    AppPolicyClrCompat_Universal = 2,
    AppPolicyClrCompat_PackagedDesktop = 3
} AppPolicyClrCompat;

typedef enum AppPolicyCreateFileAccess {
    AppPolicyCreateFileAccess_Full = 0,
    AppPolicyCreateFileAccess_Limited = 1
} AppPolicyCreateFileAccess;

#endif
#ifndef PACKAGE_FILTER_ALL_LOADED
#define PACKAGE_FILTER_ALL_LOADED 0
#endif
#ifndef PACKAGE_PROPERTY_HOSTRUNTIME
#define PACKAGE_PROPERTY_HOSTRUNTIME 0x00200000
#endif
