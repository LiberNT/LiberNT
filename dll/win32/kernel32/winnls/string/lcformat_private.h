/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            dll/win32/kernel32/winnls/string/lcformat_private.h
 * PURPOSE:         Win32 Kernel Library Header
 * PROGRAMMER:      Dmitry Chapyshev (dmitry@reactos.org)
 */

#pragma once

/*lcformat.c */
extern BOOL NLS_IsUnicodeOnlyLcid(LCID);

/* Returns the ANSI codepage used by locale formatting when LOCALE_USE_CP_ACP is not set.
 * dwFlags should contain the LOCALE_ flags relevant to formatting (typically LOCALE_NOUSEROVERRIDE).
 */
extern DWORD NLS_GetAnsiCodePage(LCID lcid, DWORD dwFlags);

extern const void * WINAPI NlsValidateLocale(LCID *lcid, ULONG flags);
extern BOOL WINAPI Internal_EnumCalendarInfo(CALINFO_ENUMPROCW proc, const void *locale, CALID id,
                                             CALTYPE type, BOOL unicode, BOOL ex, BOOL exex, LPARAM lparam);
extern BOOL WINAPI Internal_EnumDateFormats(DATEFMT_ENUMPROCW proc, const void *locale, DWORD flags,
                                            BOOL unicode, BOOL ex, BOOL exex, LPARAM lparam);
extern BOOL WINAPI Internal_EnumTimeFormats(TIMEFMT_ENUMPROCW proc, const void *locale, DWORD flags,
                                            BOOL unicode, BOOL ex, LPARAM lparam);
