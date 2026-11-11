/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ISO 15924 script lists of Unicode strings
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wchar.h>
#include "../dwrite/scripts.h"

extern const unsigned short wine_scripts_table[];

static const WCHAR ScriptCodes[][5] =
{
    L"Zzzz", L"Zyyy", L"Qaii", L"Arab", L"Armn", L"Avst", L"Bali", L"Bamu", L"Batk", L"Beng",
    L"Bopo", L"Brah", L"Brai", L"Bugi", L"Buhd", L"Cans", L"Cari", L"Cham", L"Cher", L"Copt",
    L"Xsux", L"Cprt", L"Cyrl", L"Dsrt", L"Deva", L"Egyp", L"Ethi", L"Geor", L"Glag", L"Goth",
    L"Grek", L"Gujr", L"Guru", L"Hani", L"Hang", L"Hano", L"Hebr", L"Hira", L"Armi", L"Phli",
    L"Prti", L"Java", L"Kthi", L"Knda", L"Kana", L"Kali", L"Khar", L"Khmr", L"Laoo", L"Latn",
    L"Lepc", L"Limb", L"Linb", L"Lisu", L"Lyci", L"Lydi", L"Mlym", L"Mand", L"Mtei", L"Mong",
    L"Mymr", L"Talu", L"Nkoo", L"Ogam", L"Olck", L"Ital", L"Xpeo", L"Sarb", L"Orkh", L"Orya",
    L"Osma", L"Phag", L"Phnx", L"Rjng", L"Runr", L"Samr", L"Saur", L"Shaw", L"Sinh", L"Sund",
    L"Sylo", L"Syrc", L"Tglg", L"Tagb", L"Tale", L"Lana", L"Tavt", L"Taml", L"Telu", L"Thaa",
    L"Thai", L"Tibt", L"Tfng", L"Ugar", L"Vaii", L"Yiii", L"Cakm", L"Merc", L"Mero", L"Plrd",
    L"Shrd", L"Sora", L"Takr", L"Bass", L"Aghb", L"Dupl", L"Elba", L"Gran", L"Khoj", L"Sind",
    L"Lina", L"Mahj", L"Mani", L"Mend", L"Modi", L"Mroo", L"Nbat", L"Narb", L"Perm", L"Hmng",
    L"Palm", L"Pauc", L"Phlp", L"Sidd", L"Tirh", L"Wara", L"Adlm", L"Ahom", L"Hluw", L"Bhks",
    L"Hatr", L"Marc", L"Mult", L"Newa", L"Hung", L"Osge", L"Sgnw", L"Tang", L"Gonm", L"Nshu",
    L"Soyo", L"Zanb", L"Dogr", L"Gong", L"Rohg", L"Maka", L"Medf", L"Sogo", L"Sogd", L"Elym",
    L"Hmnp", L"Nand", L"Wcho", L"Chrs", L"Diak", L"Kits", L"Yezi", L"Cpmn", L"Kawi", L"Nagm",
    L"Ougr", L"Tnsa", L"Toto", L"Vith", L"Gara", L"Gukh", L"Krai", L"Onao", L"Sunu", L"Todr",
    L"Tutg",
};

C_ASSERT(ARRAYSIZE(ScriptCodes) == Script_LastId + 1);

#define SCRIPT_CODE_LENGTH 4
#define SCRIPT_ENTRY_LENGTH (SCRIPT_CODE_LENGTH + 1)

static
unsigned short
GetCharScript(
    _In_ UINT Char)
{
    const unsigned short *Table = wine_scripts_table;

    return Table[Table[Table[Table[Char >> 12] + ((Char >> 8) & 0x0F)] + ((Char >> 4) & 0x0F)] + (Char & 0x0F)];
}

static
BOOL
IsScriptListValid(
    _In_reads_(Length) LPCWSTR List,
    _In_ int Length)
{
    int Index;

    if (Length % SCRIPT_ENTRY_LENGTH)
        return FALSE;

    for (Index = 0; Index < Length; Index += SCRIPT_ENTRY_LENGTH)
    {
        if (List[Index + SCRIPT_CODE_LENGTH] != L';')
            return FALSE;
    }

    return TRUE;
}

int
WINAPI
GetStringScripts(
    _In_ DWORD dwFlags,
    _In_ LPCWSTR lpString,
    _In_ int cchString,
    _Out_writes_opt_(cchScripts) LPWSTR lpScripts,
    _In_ int cchScripts)
{
    BOOLEAN Found[Script_LastId + 1] = { FALSE };
    unsigned short Order[Script_LastId + 1];
    unsigned short Script;
    int Index, Slot, Count = 0, Required;
    UINT Char;

    if (dwFlags & ~GSS_ALLOW_INHERITED_COMMON)
    {
        SetLastError(ERROR_INVALID_FLAGS);
        return 0;
    }

    if (!lpString || cchString < -1 || cchScripts < 0 || (cchScripts && !lpScripts))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (cchString == -1)
        cchString = (int)wcslen(lpString);

    for (Index = 0; Index < cchString; Index++)
    {
        Char = lpString[Index];
        if (IS_HIGH_SURROGATE(Char) && Index + 1 < cchString && IS_LOW_SURROGATE(lpString[Index + 1]))
        {
            Char = 0x10000 + ((Char - HIGH_SURROGATE_START) << 10) + (lpString[Index + 1] - LOW_SURROGATE_START);
            Index++;
        }

        Script = GetCharScript(Char);
        if ((Script == Script_Common || Script == Script_Inherited) && !(dwFlags & GSS_ALLOW_INHERITED_COMMON))
            continue;
        if (Found[Script])
            continue;
        Found[Script] = TRUE;

        for (Slot = Count; Slot > 0 && wcsncmp(ScriptCodes[Order[Slot - 1]], ScriptCodes[Script], SCRIPT_CODE_LENGTH) > 0; Slot--)
            Order[Slot] = Order[Slot - 1];
        Order[Slot] = Script;
        Count++;
    }

    Required = Count * SCRIPT_ENTRY_LENGTH + 1;
    if (!cchScripts)
        return Required;

    if (cchScripts < Required)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }

    for (Index = 0; Index < Count; Index++)
    {
        RtlCopyMemory(&lpScripts[Index * SCRIPT_ENTRY_LENGTH], ScriptCodes[Order[Index]], SCRIPT_CODE_LENGTH * sizeof(WCHAR));
        lpScripts[Index * SCRIPT_ENTRY_LENGTH + SCRIPT_CODE_LENGTH] = L';';
    }
    lpScripts[Count * SCRIPT_ENTRY_LENGTH] = UNICODE_NULL;

    return Required;
}

BOOL
WINAPI
VerifyScripts(
    _In_ DWORD dwFlags,
    _In_ LPCWSTR lpLocaleScripts,
    _In_ int cchLocaleScripts,
    _In_ LPCWSTR lpTestScripts,
    _In_ int cchTestScripts)
{
    int Test, Locale;

    if (dwFlags & ~VS_ALLOW_LATIN)
    {
        SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    if (!lpLocaleScripts || !lpTestScripts || !cchLocaleScripts || !cchTestScripts ||
        cchLocaleScripts < -1 || cchTestScripts < -1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    cchLocaleScripts = (int)(cchLocaleScripts == -1 ? wcslen(lpLocaleScripts) : wcsnlen(lpLocaleScripts, cchLocaleScripts));
    cchTestScripts = (int)(cchTestScripts == -1 ? wcslen(lpTestScripts) : wcsnlen(lpTestScripts, cchTestScripts));

    if (!IsScriptListValid(lpLocaleScripts, cchLocaleScripts) || !IsScriptListValid(lpTestScripts, cchTestScripts))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (Test = 0; Test < cchTestScripts; Test += SCRIPT_ENTRY_LENGTH)
    {
        if ((dwFlags & VS_ALLOW_LATIN) && !wcsncmp(&lpTestScripts[Test], L"Latn", SCRIPT_CODE_LENGTH))
            continue;

        for (Locale = 0; Locale < cchLocaleScripts; Locale += SCRIPT_ENTRY_LENGTH)
        {
            if (!wcsncmp(&lpTestScripts[Test], &lpLocaleScripts[Locale], SCRIPT_CODE_LENGTH))
                break;
        }

        if (Locale >= cchLocaleScripts)
        {
            SetLastError(ERROR_SUCCESS);
            return FALSE;
        }
    }

    return TRUE;
}
