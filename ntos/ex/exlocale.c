/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The locale and the language the system and its users read in
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LCID PsDefaultSystemLocaleId = MAKELCID(LANG_ENGLISH, SORT_DEFAULT);
LCID PsDefaultThreadLocaleId = MAKELCID(LANG_ENGLISH, SORT_DEFAULT);
LANGID PsDefaultUILanguageId = LANG_ENGLISH;
LANGID PsInstallUILanguageId = LANG_ENGLISH;

/* PRIVATE DATA ***************************************************************/

#define EXP_LOCALE_TAG 'colL'

/* Where a user's own choice of locale and language is kept */
#define EXP_LOCALE_USER_KEY \
    L"\\Registry\\User\\.Default\\Control Panel\\International"

#define EXP_LOCALE_USER_VALUE L"Locale"
#define EXP_LOCALE_USER_UI_VALUE L"MultiUILanguageId"

/* Where the system's own choice is kept, which applies when no user has one */
#define EXP_LOCALE_SYSTEM_KEY \
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Nls\\Language"

#define EXP_LOCALE_SYSTEM_VALUE L"Default"
#define EXP_LOCALE_INSTALL_VALUE L"InstallLanguage"

/* How many characters the hexadecimal form of a locale takes, with its zero */
#define EXP_LOCALE_DIGITS 9

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The locale is kept as the characters of its hexadecimal form rather than as a
 * number, because that is the form the rest of the system reads it in.
 */
static
NTSTATUS
ExpOpenLocaleKey(
    _Out_ PHANDLE Key,
    _In_ PCWSTR Path,
    _In_ BOOLEAN Write)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;

    RtlInitUnicodeString(&KeyName, Path);

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    return ZwOpenKey(Key,
                     Write ? (KEY_READ | KEY_WRITE) : KEY_READ,
                     &ObjectAttributes);
}

static
NTSTATUS
ExpReadLocaleValue(
    _In_ PCWSTR Path,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Result)
{
    UNICODE_STRING Name;
    UNICODE_STRING Text;
    PKEY_VALUE_PARTIAL_INFORMATION Value;
    HANDLE Key;
    ULONG Size = (ULONG)sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
                 EXP_LOCALE_DIGITS * (ULONG)sizeof(WCHAR);
    ULONG Returned;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ExpOpenLocaleKey(&Key, Path, FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Value = ExAllocatePoolZero(PagedPool, Size, EXP_LOCALE_TAG);
    if (Value == NULL)
    {
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitUnicodeString(&Name, ValueName);

    Status = ZwQueryValueKey(Key,
                             &Name,
                             KeyValuePartialInformation,
                             Value,
                             Size,
                             &Returned);

    ZwClose(Key);

    if (NT_SUCCESS(Status))
    {
        if (Value->Type == REG_DWORD)
        {
            *Result = *(PULONG)Value->Data;
        }
        else
        {
            Text.Buffer = (PWCHAR)Value->Data;
            Text.Length = (USHORT)Value->DataLength;
            Text.MaximumLength = Text.Length;

            /* The stored form may carry its terminator; the parse must not. */
            while ((Text.Length >= sizeof(WCHAR)) &&
                   (Text.Buffer[Text.Length / sizeof(WCHAR) - 1] ==
                    UNICODE_NULL))
            {
                Text.Length -= sizeof(WCHAR);
            }

            Status = RtlUnicodeStringToInteger(&Text, 16, Result);
        }
    }

    ExFreePoolWithTag(Value, EXP_LOCALE_TAG);

    return Status;
}

static
NTSTATUS
ExpWriteLocaleValue(
    _In_ PCWSTR Path,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value)
{
    WCHAR Text[EXP_LOCALE_DIGITS];
    UNICODE_STRING Name;
    HANDLE Key;
    ULONG Digit;
    ULONG Index = 0;
    NTSTATUS Status;

    PAGED_CODE();

    for (Digit = 8; Digit > 0; Digit--)
    {
        ULONG Nibble = (Value >> ((Digit - 1) * 4)) & 0xF;

        Text[Index++] = (WCHAR)((Nibble < 10) ? (L'0' + Nibble) :
                                                (L'0' + Nibble + 7));
    }

    Text[Index] = UNICODE_NULL;

    Status = ExpOpenLocaleKey(&Key, Path, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitUnicodeString(&Name, ValueName);

    Status = ZwSetValueKey(Key,
                           &Name,
                           0,
                           REG_SZ,
                           Text,
                           (Index + 1) * (ULONG)sizeof(WCHAR));

    ZwClose(Key);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The language a user reads in belongs to that user, not to the system, so it
 * is kept with the thread that is acting for them.
 */
NTSTATUS
NTAPI
ExpGetCurrentUserUILanguage(
    _In_ PCWSTR MuiName,
    _Out_ LANGID *LanguageId)
{
    ULONG Value;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(MuiName);

    if (NT_SUCCESS(ExpReadLocaleValue(EXP_LOCALE_USER_KEY,
                                      EXP_LOCALE_USER_UI_VALUE,
                                      &Value)))
    {
        *LanguageId = (LANGID)Value;
        return STATUS_SUCCESS;
    }

    /* No choice of their own leaves the one the system was installed with. */
    *LanguageId = PsDefaultUILanguageId;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExpSetCurrentUserUILanguage(
    _In_ PCWSTR MuiName,
    _In_ LANGID LanguageId)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(MuiName);

    PsDefaultUILanguageId = LanguageId;

    return ExpWriteLocaleValue(EXP_LOCALE_USER_KEY,
                               EXP_LOCALE_USER_UI_VALUE,
                               LanguageId);
}

NTSTATUS
NTAPI
NtQueryDefaultLocale(
    _In_ BOOLEAN UserProfile,
    _Out_ PLCID DefaultLocaleId)
{
    LCID Locale;
    ULONG Value;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (UserProfile)
    {
        if (!NT_SUCCESS(ExpReadLocaleValue(EXP_LOCALE_USER_KEY,
                                           EXP_LOCALE_USER_VALUE,
                                           &Value)))
        {
            Value = PsDefaultThreadLocaleId;
        }
    }
    else if (!NT_SUCCESS(ExpReadLocaleValue(EXP_LOCALE_SYSTEM_KEY,
                                            EXP_LOCALE_SYSTEM_VALUE,
                                            &Value)))
    {
        Value = PsDefaultSystemLocaleId;
    }

    Locale = (LCID)Value;

    if (ExGetPreviousMode() == KernelMode)
    {
        *DefaultLocaleId = Locale;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteUlong(DefaultLocaleId);
        *DefaultLocaleId = Locale;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSetDefaultLocale(
    _In_ BOOLEAN UserProfile,
    _In_ LCID DefaultLocaleId)
{
    PAGED_CODE();

    if (!UserProfile &&
        !SeSinglePrivilegeCheck(SeTcbPrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (UserProfile)
    {
        PsDefaultThreadLocaleId = DefaultLocaleId;

        return ExpWriteLocaleValue(EXP_LOCALE_USER_KEY,
                                   EXP_LOCALE_USER_VALUE,
                                   DefaultLocaleId);
    }

    PsDefaultSystemLocaleId = DefaultLocaleId;

    return ExpWriteLocaleValue(EXP_LOCALE_SYSTEM_KEY,
                               EXP_LOCALE_SYSTEM_VALUE,
                               DefaultLocaleId);
}

NTSTATUS
NTAPI
NtQueryDefaultUILanguage(
    _Out_ LANGID *LanguageId)
{
    LANGID Language;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!NT_SUCCESS(ExpGetCurrentUserUILanguage(NULL, &Language)))
    {
        Language = PsInstallUILanguageId;
    }

    if (ExGetPreviousMode() == KernelMode)
    {
        *LanguageId = Language;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteUshort(LanguageId);
        *LanguageId = Language;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSetDefaultUILanguage(
    _In_ LANGID LanguageId)
{
    PAGED_CODE();

    return ExpSetCurrentUserUILanguage(NULL, LanguageId);
}

NTSTATUS
NTAPI
NtQueryInstallUILanguage(
    _Out_ LANGID *LanguageId)
{
    LANGID Language = PsInstallUILanguageId;
    ULONG Value;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (NT_SUCCESS(ExpReadLocaleValue(EXP_LOCALE_SYSTEM_KEY,
                                      EXP_LOCALE_INSTALL_VALUE,
                                      &Value)))
    {
        Language = (LANGID)Value;
    }

    if (ExGetPreviousMode() == KernelMode)
    {
        *LanguageId = Language;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteUshort(LanguageId);
        *LanguageId = Language;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
