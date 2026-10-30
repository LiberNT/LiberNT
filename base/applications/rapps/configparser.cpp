/*
 * PROJECT:     ReactOS Applications Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Config parser
 * COPYRIGHT:   Copyright 2009 Dmitry Chapyshev (dmitry@reactos.org)
 *              Copyright 2015 Ismael Ferreras Morezuelas (swyterzone+ros@gmail.com)
 *              Copyright 2017 Alexander Shaposhnikov (sanchaez@reactos.org)
 *              Copyright 2021 Mark Jansen <mark.jansen@reactos.org>
 */

#include "rapps.h"
#include <debug.h>

struct CLocaleSections
{
    CStringW Locale;
    CStringW LocaleNeutral;
    CStringW Section;
};

struct CSectionNames
{
    CLocaleSections ArchSpecific[3];
    CLocaleSections ArchNeutral;
    LPCWSTR Architectures[3];
    UINT Count;
    BOOL NeutralDownload;
    CStringW Key;
};
static CSectionNames g_Names;

HRESULT
ReadIniValue(LPCWSTR File, LPCWSTR Section, LPCWSTR Name, CStringW &Output)
{
    for (DWORD len = 256, ret;; len *= 2)
    {
        ret = GetPrivateProfileString(Section, Name, L"\n", Output.GetBuffer(len), len, File);
        if (ret + 1 != len)
        {
            Output.ReleaseBuffer(ret);
            return ret && Output[0] != L'\n' ? ret : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
    }
}

CConfigParser::CConfigParser(const CStringW &FilePath) : szConfigPath(FilePath)
{
    CacheINI();
}

void
CConfigParser::ReadSection(CStringW &Buffer, const CStringW &Section, BOOL isArch, LPCWSTR Architecture)
{
    DWORD len = 512;
    DWORD result;

    do
    {
        len *= 2;

        result = GetPrivateProfileSectionW(Section, Buffer.GetBuffer(len), len, szConfigPath);
        Buffer.ReleaseBuffer(result);
    } while (result == len - 2);

    len = 0;
    while (len < result)
    {
        // Explicitly use the null terminator!
        CString tmp = Buffer.GetBuffer() + len;
        if (tmp.GetLength() > 0)
        {
            len += tmp.GetLength() + 1;

            int idx = tmp.Find('=');
            if (idx >= 0)
            {
                CString key = tmp.Left(idx);

                // The base section downloads the x86 build
                if (!isArch && key == "URLDownload")
                {
                    continue;
                }

                // Is this key already present from a more specific translation?
                if (m_Keys.FindKey(key) >= 0)
                {
                    continue;
                }
                if (key == "URLDownload")
                {
                    m_Architecture = Architecture;
                }

                CString value = tmp.Mid(idx + 1);
                m_Keys.Add(key, value);
            }
            else
            {
                DPRINT1("ERROR: invalid key/value pair: '%S'\n", tmp.GetString());
            }
        }
        else
        {
            break;
        }
    }
}

VOID
CConfigParser::CacheINI()
{
    LPCWSTR Architectures[_countof(g_Names.Architectures)];
    UINT Count = GetPackageArchitectures(Architectures, _countof(Architectures));
    CStringW Key;

    for (UINT i = 0; i < Count; i++)
    {
        Key += Architectures[i];
        Key += L";";
    }
    if (g_Names.Key != Key)
    {
        g_Names = CSectionNames();
        g_Names.Key = Key;
        g_Names.Count = Count;
        for (UINT i = 0; i < Count; i++)
        {
            g_Names.Architectures[i] = Architectures[i];
            if (!StrCmpIW(Architectures[i], L"x86"))
                g_Names.NeutralDownload = TRUE;
        }
    }

    // Cache section names
    if (g_Names.ArchNeutral.Section.IsEmpty())
    {
        CString szLocaleID;
        const INT cchLocaleSize = 5;

        GetLocaleInfoW(GetUserDefaultLCID(), LOCALE_ILANGUAGE, szLocaleID.GetBuffer(cchLocaleSize), cchLocaleSize);
        szLocaleID.ReleaseBuffer();
        CString INISectionLocale = L"Section." + szLocaleID;

        g_Names.ArchNeutral.Locale = INISectionLocale;
        for (UINT i = 0; i < g_Names.Count; i++)
            g_Names.ArchSpecific[i].Locale = INISectionLocale + L"." + g_Names.Architectures[i];

        // turn "Section.0c0a" into "Section.0a", keeping just the neutral lang part
        if (szLocaleID.GetLength() >= 2)
        {
            g_Names.ArchNeutral.LocaleNeutral = L"Section." + szLocaleID.Right(2);
            for (UINT i = 0; i < g_Names.Count; i++)
                g_Names.ArchSpecific[i].LocaleNeutral = g_Names.ArchNeutral.LocaleNeutral + L"." + g_Names.Architectures[i];
        }

        g_Names.ArchNeutral.Section = L"Section";
        for (UINT i = 0; i < g_Names.Count; i++)
            g_Names.ArchSpecific[i].Section = L"Section." + CStringW(g_Names.Architectures[i]);
    }

    // Use a shared buffer so that we don't have to re-allocate it every time
    CStringW Buffer;

    UINT Selected = 0;
    for (UINT i = 0; i < g_Names.Count; i++)
    {
        const CLocaleSections &Names = g_Names.ArchSpecific[i];

        if (ReadIniValue(szConfigPath, Names.Locale, L"URLDownload", Buffer) > 0 ||
            (!Names.LocaleNeutral.IsEmpty() && ReadIniValue(szConfigPath, Names.LocaleNeutral, L"URLDownload", Buffer) > 0) ||
            ReadIniValue(szConfigPath, Names.Section, L"URLDownload", Buffer) > 0)
        {
            Selected = i;
            break;
        }
    }

    if (g_Names.Count)
    {
        const CLocaleSections &Names = g_Names.ArchSpecific[Selected];

        ReadSection(Buffer, Names.Locale, TRUE, g_Names.Architectures[Selected]);
        if (!Names.LocaleNeutral.IsEmpty())
        {
            ReadSection(Buffer, Names.LocaleNeutral, TRUE, g_Names.Architectures[Selected]);
        }
        ReadSection(Buffer, Names.Section, TRUE, g_Names.Architectures[Selected]);
    }

    ReadSection(Buffer, g_Names.ArchNeutral.Locale, g_Names.NeutralDownload, L"x86");
    if (!g_Names.ArchNeutral.LocaleNeutral.IsEmpty())
    {
        ReadSection(Buffer, g_Names.ArchNeutral.LocaleNeutral, g_Names.NeutralDownload, L"x86");
    }
    ReadSection(Buffer, g_Names.ArchNeutral.Section, g_Names.NeutralDownload, L"x86");
}

BOOL
CConfigParser::GetString(const CStringW &KeyName, CStringW &ResultString)
{
    int nIndex = m_Keys.FindKey(KeyName);
    if (nIndex >= 0)
    {
        ResultString = m_Keys.GetValueAt(nIndex);
        return TRUE;
    }

    ResultString.Empty();
    return FALSE;
}

BOOL
CConfigParser::GetInt(const CStringW &KeyName, INT &iResult)
{
    CStringW Buffer;

    iResult = 0;

    // grab the text version of our entry
    if (!GetString(KeyName, Buffer))
        return FALSE;

    if (Buffer.IsEmpty())
        return FALSE;

    // convert it to an actual integer
    iResult = StrToIntW(Buffer.GetString());

    // we only care about values > 0
    return (iResult > 0);
}

UINT
CConfigParser::GetSectionString(LPCWSTR Section, LPCWSTR Name, CStringW &Result)
{
    HRESULT hr; // Return value; length of ini string or 0 on failure.
    CStringW SecBuf;
    LPCWSTR Architecture = m_Architecture.IsEmpty() ? GetPackageArchitecture() : m_Architecture.GetString();
    WCHAR FullLoc[5], *NeutralLoc = FullLoc + 2;
    GetLocaleInfoW(GetUserDefaultLCID(), LOCALE_ILANGUAGE, FullLoc, _countof(FullLoc));

    SecBuf.Format(L"%s.%s.%s", Section, FullLoc, Architecture);
    if ((hr = ReadIniValue(szConfigPath, SecBuf, Name, Result)) > 0)
        return hr;

    if (*NeutralLoc)
    {
        SecBuf.Format(L"%s.%s.%s", Section, NeutralLoc, Architecture);
        if ((hr = ReadIniValue(szConfigPath, SecBuf, Name, Result)) > 0)
            return hr;
    }

    SecBuf.Format(L"%s.%s", Section, Architecture);
    if ((hr = ReadIniValue(szConfigPath, SecBuf, Name, Result)) > 0)
        return hr;

    SecBuf.Format(L"%s.%s", Section, FullLoc);
    if ((hr = ReadIniValue(szConfigPath, SecBuf, Name, Result)) > 0)
        return hr;

    if (*NeutralLoc)
    {
        SecBuf.Format(L"%s.%s", Section, NeutralLoc);
        if ((hr = ReadIniValue(szConfigPath, SecBuf, Name, Result)) > 0)
            return hr;
    }

    if ((hr = ReadIniValue(szConfigPath, Section, Name, Result)) > 0)
        return hr;
    return 0;
}
