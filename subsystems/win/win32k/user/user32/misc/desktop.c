/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS user32.dll
 * FILE:            win32ss/user/user32/misc/desktop.c
 * PURPOSE:         Desktops
 * PROGRAMMER:      Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      06-06-2001  CSH  Created
 */

#include <user32.h>

WINE_DEFAULT_DEBUG_CHANNEL(user32);

/*********************************************************************
 * desktop class descriptor
 */
#if 0 // Kept for referencing.
const struct builtin_class_descr DESKTOP_builtin_class =
{
  WC_DESKTOP,           /* name */
  CS_DBLCLKS,           /* style */
  NULL,                 /* procA (winproc is Unicode only) */
  DesktopWndProc,       /* procW */
  0,                    /* extra */
  IDC_ARROW,            /* cursor */
  (HBRUSH)(COLOR_BACKGROUND+1)    /* brush */
};
#endif

LRESULT
WINAPI
DesktopWndProcW(HWND Wnd,
                UINT Msg,
                WPARAM wParam,
                LPARAM lParam)
{
   TRACE("Desktop W Class Atom! hWnd 0x%x, Msg %d\n", Wnd, Msg);

   switch(Msg)
   {
      case WM_NCCREATE:
          if (!DefWindowProcW(Wnd, Msg, wParam, lParam)) return FALSE;
      case WM_ERASEBKGND:
      case WM_CREATE:
      case WM_CLOSE:
      case WM_DISPLAYCHANGE:
      case WM_PAINT:
      case WM_SYSCOLORCHANGE:
        {
          LRESULT lResult;
          NtUserMessageCall( Wnd, Msg, wParam, lParam, (ULONG_PTR)&lResult, FNID_DESKTOP, FALSE);
          TRACE("Desktop lResult %d\n", lResult);
          return lResult;
        }

      case WM_PALETTECHANGED:
          if (Wnd == (HWND)wParam) break;
      case WM_QUERYNEWPALETTE:
        {
          HDC hdc = GetWindowDC( Wnd );
          PaintDesktop(hdc);
          ReleaseDC( Wnd, hdc );
          break;
        }

      case WM_SETCURSOR:
          return (LRESULT)SetCursor(LoadCursorW(0, (LPCWSTR)IDC_ARROW));

      default:
          return DefWindowProcW(Wnd, Msg, wParam, lParam);
   }
   return 0;
}

VOID
WINAPI
LogFontA2W(LPLOGFONTW pW, CONST LOGFONTA *pA)
{
#define COPYS(f,len) MultiByteToWideChar ( CP_THREAD_ACP, 0, pA->f, len, pW->f, len )
#define COPYN(f) pW->f = pA->f

  COPYN(lfHeight);
  COPYN(lfWidth);
  COPYN(lfEscapement);
  COPYN(lfOrientation);
  COPYN(lfWeight);
  COPYN(lfItalic);
  COPYN(lfUnderline);
  COPYN(lfStrikeOut);
  COPYN(lfCharSet);
  COPYN(lfOutPrecision);
  COPYN(lfClipPrecision);
  COPYN(lfQuality);
  COPYN(lfPitchAndFamily);
  COPYS(lfFaceName,LF_FACESIZE);

#undef COPYN
#undef COPYS
}

VOID
WINAPI
LogFontW2A(LPLOGFONTA pA, CONST LOGFONTW *pW)
{
#define COPYS(f,len) WideCharToMultiByte ( CP_THREAD_ACP, 0, pW->f, len, pA->f, len, NULL, NULL )
#define COPYN(f) pA->f = pW->f

  COPYN(lfHeight);
  COPYN(lfWidth);
  COPYN(lfEscapement);
  COPYN(lfOrientation);
  COPYN(lfWeight);
  COPYN(lfItalic);
  COPYN(lfUnderline);
  COPYN(lfStrikeOut);
  COPYN(lfCharSet);
  COPYN(lfOutPrecision);
  COPYN(lfClipPrecision);
  COPYN(lfQuality);
  COPYN(lfPitchAndFamily);
  COPYS(lfFaceName,LF_FACESIZE);

#undef COPYN
#undef COPYS
}

ULONG WINAPI IntGetThreadDpiContext(VOID);
ULONG WINAPI IntSetThreadDpiContext(ULONG Context);

#define DPI_CONTEXT_SYSTEM_AWARE 0x00000011

BOOL
WINAPI
User32IsDpiVirtualizationActive(VOID)
{
#ifdef WOW64_I386_RUNTIME
  return !!(UserGetServerInfo(dwSRVIFlags, ROS_SERVERINFO_SRVIFLAGS) & SRVINFO_DPIVIRTUALIZATION);
#else
  return gpsi && (gpsi->dwSRVIFlags & SRVINFO_DPIVIRTUALIZATION);
#endif
}

UINT
WINAPI
User32GetSystemDpi(VOID)
{
#ifdef WOW64_I386_RUNTIME
  return (UINT)UserGetServerInfo(dmLogPixels, ROS_SERVERINFO_LOGPIXELS);
#else
  return gpsi ? gpsi->dmLogPixels : USER_DEFAULT_SCREEN_DPI;
#endif
}

BOOL
WINAPI
User32IsDpiVirtualThread(VOID)
{
  return User32IsDpiVirtualizationActive() &&
         (IntGetThreadDpiContext() & 0x0f) == DPI_AWARENESS_UNAWARE;
}

ULONG
WINAPI
User32BeginSystemDpiContext(VOID)
{
  if (!User32IsDpiVirtualThread())
    return 0;
  return IntSetThreadDpiContext(DPI_CONTEXT_SYSTEM_AWARE | (User32GetSystemDpi() << 8));
}

VOID
WINAPI
User32EndSystemDpiContext(ULONG Previous)
{
  if (Previous)
    IntSetThreadDpiContext(Previous);
}

static LONG
User32DpiMulDiv(LONG Value, LONG Numerator, LONG Denominator)
{
  LONGLONG Product = (LONGLONG)Value * Numerator;

  if (Product >= 0)
    return (LONG)((Product + Denominator / 2) / Denominator);
  return -(LONG)((-Product + Denominator / 2) / Denominator);
}

LONG
WINAPI
User32DpiToLogical(LONG Value)
{
  return User32DpiMulDiv(Value, USER_DEFAULT_SCREEN_DPI, (LONG)User32GetSystemDpi());
}

LONG
WINAPI
User32DpiToPhysical(LONG Value)
{
  return User32DpiMulDiv(Value, (LONG)User32GetSystemDpi(), USER_DEFAULT_SCREEN_DPI);
}

VOID
WINAPI
User32DpiRectToLogical(LPRECT Rect)
{
  Rect->left = User32DpiToLogical(Rect->left);
  Rect->top = User32DpiToLogical(Rect->top);
  Rect->right = User32DpiToLogical(Rect->right);
  Rect->bottom = User32DpiToLogical(Rect->bottom);
}

int WINAPI
RealGetSystemMetrics(int nIndex)
{
  //FIXME("Global Server Data -> %x\n",gpsi);
  if (nIndex < 0 || nIndex >= SM_CMETRICS) return 0;
#ifdef WOW64_I386_RUNTIME
  return (int)NtUserCallOneParam(nIndex, ONEPARAM_ROUTINE_ROS_GETSYSTEMMETRICS);
#else
  if (!gpsi) return 0;
  if (User32IsDpiVirtualThread())
    return (int)NtUserCallOneParam(nIndex, ONEPARAM_ROUTINE_ROS_GETSYSTEMMETRICS);
  return UserGetImageMetric(gpsi->aiSysMet, nIndex, NtCurrentPeb()->ImageSubsystemMajorVersion < 6);
#endif
}

/*
 * @implemented
 */
int WINAPI
GetSystemMetrics(int nIndex)
{
   BOOL Hook;
   int Ret = 0;

   if (!gpsi) // Fixme! Hax! Need Timos delay load support?
   {
      return RealGetSystemMetrics(nIndex);
   }

   Hook = BeginIfHookedUserApiHook();

   /* Bypass SEH and go direct. */
   if (!Hook) return RealGetSystemMetrics(nIndex);

   _SEH2_TRY
   {
      Ret = guah.GetSystemMetrics(nIndex);
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
       ERR("Got exception in hooked GetSystemMetrics!\n");
   }
   _SEH2_END;

   EndUserApiHook();

   return Ret;
}

/*
 * @unimplemented
 */
BOOL WINAPI SetDeskWallpaper(LPCSTR filename)
{
	return SystemParametersInfoA(SPI_SETDESKWALLPAPER,0,(PVOID)filename,TRUE);
}

static BOOL
GetHighContrastW(UINT uiParam, LPHIGHCONTRASTW HighContrast, UINT fWinIni)
{
    HIGHCONTRASTW Request;
    WCHAR Scheme[MAX_PATH];
    SIZE_T Size;
    LPWSTR Copy;

    if (!HighContrast || HighContrast->cbSize != sizeof(HIGHCONTRASTW))
        return NtUserSystemParametersInfo(SPI_GETHIGHCONTRAST, uiParam, HighContrast, fWinIni);
    Request.cbSize = sizeof(Request);
    Request.dwFlags = 0;
    Request.lpszDefaultScheme = Scheme;
    Scheme[0] = UNICODE_NULL;
    if (!NtUserSystemParametersInfo(SPI_GETHIGHCONTRAST, uiParam, &Request, fWinIni))
        return FALSE;
    Scheme[MAX_PATH - 1] = UNICODE_NULL;
    Size = (wcslen(Scheme) + 1) * sizeof(WCHAR);
    Copy = LocalAlloc(LMEM_FIXED, Size);
    if (!Copy)
        return FALSE;
    RtlCopyMemory(Copy, Scheme, Size);
    HighContrast->dwFlags = Request.dwFlags;
    HighContrast->lpszDefaultScheme = Copy;
    return TRUE;
}

BOOL WINAPI
RealSystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);

static VOID
User32DpiScaleLogFont(LPLOGFONTW LogFont, BOOL ToLogical)
{
  LogFont->lfHeight = ToLogical ? User32DpiToLogical(LogFont->lfHeight) : User32DpiToPhysical(LogFont->lfHeight);
  LogFont->lfWidth = ToLogical ? User32DpiToLogical(LogFont->lfWidth) : User32DpiToPhysical(LogFont->lfWidth);
}

static VOID
User32DpiScaleNonClientMetrics(LPNONCLIENTMETRICSW Metrics, BOOL ToLogical)
{
  LONG (WINAPI *Scale)(LONG) = ToLogical ? User32DpiToLogical : User32DpiToPhysical;

  Metrics->iBorderWidth = Scale(Metrics->iBorderWidth);
  Metrics->iScrollWidth = Scale(Metrics->iScrollWidth);
  Metrics->iScrollHeight = Scale(Metrics->iScrollHeight);
  Metrics->iCaptionWidth = Scale(Metrics->iCaptionWidth);
  Metrics->iCaptionHeight = Scale(Metrics->iCaptionHeight);
  Metrics->iSmCaptionWidth = Scale(Metrics->iSmCaptionWidth);
  Metrics->iSmCaptionHeight = Scale(Metrics->iSmCaptionHeight);
  Metrics->iMenuWidth = Scale(Metrics->iMenuWidth);
  Metrics->iMenuHeight = Scale(Metrics->iMenuHeight);
  if (Metrics->cbSize == sizeof(NONCLIENTMETRICSW))
    Metrics->iPaddedBorderWidth = Scale(Metrics->iPaddedBorderWidth);
  User32DpiScaleLogFont(&Metrics->lfCaptionFont, ToLogical);
  User32DpiScaleLogFont(&Metrics->lfSmCaptionFont, ToLogical);
  User32DpiScaleLogFont(&Metrics->lfMenuFont, ToLogical);
  User32DpiScaleLogFont(&Metrics->lfStatusFont, ToLogical);
  User32DpiScaleLogFont(&Metrics->lfMessageFont, ToLogical);
}

static BOOL
User32SystemParametersInfo(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
{
  BOOL Ret;

  if (!pvParam && uiAction != SPI_ICONHORIZONTALSPACING && uiAction != SPI_ICONVERTICALSPACING)
    return NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
  if (!User32IsDpiVirtualThread())
    return NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);

  switch (uiAction)
  {
    case SPI_GETNONCLIENTMETRICS:
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
        User32DpiScaleNonClientMetrics(pvParam, TRUE);
      return Ret;

    case SPI_SETNONCLIENTMETRICS:
    {
      NONCLIENTMETRICSW Metrics;
      UINT Size = ((LPNONCLIENTMETRICSW)pvParam)->cbSize;

      if (Size > sizeof(Metrics))
        return NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      RtlCopyMemory(&Metrics, pvParam, Size);
      User32DpiScaleNonClientMetrics(&Metrics, FALSE);
      return NtUserSystemParametersInfo(uiAction, uiParam, &Metrics, fWinIni);
    }

    case SPI_GETICONMETRICS:
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
      {
        LPICONMETRICSW Metrics = pvParam;

        Metrics->iHorzSpacing = User32DpiToLogical(Metrics->iHorzSpacing);
        Metrics->iVertSpacing = User32DpiToLogical(Metrics->iVertSpacing);
        User32DpiScaleLogFont(&Metrics->lfFont, TRUE);
      }
      return Ret;

    case SPI_SETICONMETRICS:
    {
      ICONMETRICSW Metrics = *(LPICONMETRICSW)pvParam;

      Metrics.iHorzSpacing = User32DpiToPhysical(Metrics.iHorzSpacing);
      Metrics.iVertSpacing = User32DpiToPhysical(Metrics.iVertSpacing);
      User32DpiScaleLogFont(&Metrics.lfFont, FALSE);
      return NtUserSystemParametersInfo(uiAction, uiParam, &Metrics, fWinIni);
    }

    case SPI_GETICONTITLELOGFONT:
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
        User32DpiScaleLogFont(pvParam, TRUE);
      return Ret;

    case SPI_SETICONTITLELOGFONT:
    {
      LOGFONTW LogFont = *(LPLOGFONTW)pvParam;

      User32DpiScaleLogFont(&LogFont, FALSE);
      return NtUserSystemParametersInfo(uiAction, uiParam, &LogFont, fWinIni);
    }

    case SPI_GETWORKAREA:
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
        User32DpiRectToLogical(pvParam);
      return Ret;

    case SPI_SETWORKAREA:
    {
      RECT Rect = *(LPRECT)pvParam;

      Rect.left = User32DpiToPhysical(Rect.left);
      Rect.top = User32DpiToPhysical(Rect.top);
      Rect.right = User32DpiToPhysical(Rect.right);
      Rect.bottom = User32DpiToPhysical(Rect.bottom);
      return NtUserSystemParametersInfo(uiAction, uiParam, &Rect, fWinIni);
    }

    case SPI_GETMINIMIZEDMETRICS:
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
      {
        LPMINIMIZEDMETRICS Metrics = pvParam;

        Metrics->iWidth = User32DpiToLogical(Metrics->iWidth);
        Metrics->iHorzGap = User32DpiToLogical(Metrics->iHorzGap);
        Metrics->iVertGap = User32DpiToLogical(Metrics->iVertGap);
      }
      return Ret;

    case SPI_SETMINIMIZEDMETRICS:
    {
      MINIMIZEDMETRICS Metrics = *(LPMINIMIZEDMETRICS)pvParam;

      Metrics.iWidth = User32DpiToPhysical(Metrics.iWidth);
      Metrics.iHorzGap = User32DpiToPhysical(Metrics.iHorzGap);
      Metrics.iVertGap = User32DpiToPhysical(Metrics.iVertGap);
      return NtUserSystemParametersInfo(uiAction, uiParam, &Metrics, fWinIni);
    }

    case SPI_ICONHORIZONTALSPACING:
    case SPI_ICONVERTICALSPACING:
      if (!pvParam)
        return NtUserSystemParametersInfo(uiAction, User32DpiToPhysical(uiParam), pvParam, fWinIni);
      Ret = NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
      if (Ret)
        *(PINT)pvParam = User32DpiToLogical(*(PINT)pvParam);
      return Ret;
  }
  return NtUserSystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
}

BOOL WINAPI
RealSystemParametersInfoA(UINT uiAction,
		      UINT uiParam,
		      PVOID pvParam,
		      UINT fWinIni)
{
  switch (uiAction)
    {

      case SPI_GETNONCLIENTMETRICS:
        {
           LPNONCLIENTMETRICSA pnclma = (LPNONCLIENTMETRICSA)pvParam;
           NONCLIENTMETRICSW nclmw;
           if(pnclma->cbSize != sizeof(NONCLIENTMETRICSA) &&
              pnclma->cbSize != FIELD_OFFSET(NONCLIENTMETRICSA, iPaddedBorderWidth))
           {
               SetLastError(ERROR_INVALID_PARAMETER);
               return FALSE;
           }
           nclmw.cbSize = sizeof(NONCLIENTMETRICSW);

           if (!SystemParametersInfoW(uiAction, sizeof(NONCLIENTMETRICSW),
                                      &nclmw, fWinIni))
             return FALSE;

           if (pnclma->cbSize == sizeof(NONCLIENTMETRICSA))
               pnclma->iPaddedBorderWidth = nclmw.iPaddedBorderWidth;

           pnclma->iBorderWidth = nclmw.iBorderWidth;
           pnclma->iScrollWidth = nclmw.iScrollWidth;
           pnclma->iScrollHeight = nclmw.iScrollHeight;
           pnclma->iCaptionWidth = nclmw.iCaptionWidth;
           pnclma->iCaptionHeight = nclmw.iCaptionHeight;
           pnclma->iSmCaptionWidth = nclmw.iSmCaptionWidth;
           pnclma->iSmCaptionHeight = nclmw.iSmCaptionHeight;
           pnclma->iMenuWidth = nclmw.iMenuWidth;
           pnclma->iMenuHeight = nclmw.iMenuHeight;
           LogFontW2A(&(pnclma->lfCaptionFont), &(nclmw.lfCaptionFont));
           LogFontW2A(&(pnclma->lfSmCaptionFont), &(nclmw.lfSmCaptionFont));
           LogFontW2A(&(pnclma->lfMenuFont), &(nclmw.lfMenuFont));
           LogFontW2A(&(pnclma->lfStatusFont), &(nclmw.lfStatusFont));
           LogFontW2A(&(pnclma->lfMessageFont), &(nclmw.lfMessageFont));
           return TRUE;
        }
      case SPI_SETNONCLIENTMETRICS:
        {
           LPNONCLIENTMETRICSA pnclma = (LPNONCLIENTMETRICSA)pvParam;
           NONCLIENTMETRICSW nclmw;
           if(pnclma->cbSize != sizeof(NONCLIENTMETRICSA) &&
              pnclma->cbSize != FIELD_OFFSET(NONCLIENTMETRICSA, iPaddedBorderWidth))
           {
               SetLastError(ERROR_INVALID_PARAMETER);
               return FALSE;
           }
           nclmw.cbSize = sizeof(NONCLIENTMETRICSW);
           nclmw.iPaddedBorderWidth = (pnclma->cbSize == sizeof(NONCLIENTMETRICSA)) ? pnclma->iPaddedBorderWidth : 0;
           nclmw.iBorderWidth = pnclma->iBorderWidth;
           nclmw.iScrollWidth = pnclma->iScrollWidth;
           nclmw.iScrollHeight = pnclma->iScrollHeight;
           nclmw.iCaptionWidth = pnclma->iCaptionWidth;
           nclmw.iCaptionHeight = pnclma->iCaptionHeight;
           nclmw.iSmCaptionWidth = pnclma->iSmCaptionWidth;
           nclmw.iSmCaptionHeight = pnclma->iSmCaptionHeight;
           nclmw.iMenuWidth = pnclma->iMenuWidth;
           nclmw.iMenuHeight = pnclma->iMenuHeight;
           LogFontA2W(&(nclmw.lfCaptionFont), &(pnclma->lfCaptionFont));
           LogFontA2W(&(nclmw.lfSmCaptionFont), &(pnclma->lfSmCaptionFont));
           LogFontA2W(&(nclmw.lfMenuFont), &(pnclma->lfMenuFont));
           LogFontA2W(&(nclmw.lfStatusFont), &(pnclma->lfStatusFont));
           LogFontA2W(&(nclmw.lfMessageFont), &(pnclma->lfMessageFont));

           return SystemParametersInfoW(uiAction, sizeof(NONCLIENTMETRICSW),
                                        &nclmw, fWinIni);
        }
      case SPI_GETICONMETRICS:
          {
              LPICONMETRICSA picma = (LPICONMETRICSA)pvParam;
              ICONMETRICSW icmw;
              if(picma->cbSize != sizeof(ICONMETRICSA))
              {
                  SetLastError(ERROR_INVALID_PARAMETER);
                  return FALSE;
              }
              icmw.cbSize = sizeof(ICONMETRICSW);
              if (!SystemParametersInfoW(uiAction, sizeof(ICONMETRICSW),
                                        &icmw, fWinIni))
                  return FALSE;

              picma->iHorzSpacing = icmw.iHorzSpacing;
              picma->iVertSpacing = icmw.iVertSpacing;
              picma->iTitleWrap = icmw.iTitleWrap;
              LogFontW2A(&(picma->lfFont), &(icmw.lfFont));
              return TRUE;
          }
      case SPI_SETICONMETRICS:
          {
              LPICONMETRICSA picma = (LPICONMETRICSA)pvParam;
              ICONMETRICSW icmw;
              if(picma->cbSize != sizeof(ICONMETRICSA))
              {
                  SetLastError(ERROR_INVALID_PARAMETER);
                  return FALSE;
              }
              icmw.cbSize = sizeof(ICONMETRICSW);
              icmw.iHorzSpacing = picma->iHorzSpacing;
              icmw.iVertSpacing = picma->iVertSpacing;
              icmw.iTitleWrap = picma->iTitleWrap;
              LogFontA2W(&(icmw.lfFont), &(picma->lfFont));

              return SystemParametersInfoW(uiAction, sizeof(ICONMETRICSW),
                                           &icmw, fWinIni);
          }
      case SPI_GETICONTITLELOGFONT:
        {
           LOGFONTW lfw;
           if (!SystemParametersInfoW(uiAction, 0, &lfw, fWinIni))
             return FALSE;
           LogFontW2A(pvParam, &lfw);
           return TRUE;
        }
      case SPI_SETICONTITLELOGFONT:
          {
              LPLOGFONTA plfa = (LPLOGFONTA)pvParam;
              LOGFONTW lfw;
              LogFontA2W(&lfw,plfa);
              return SystemParametersInfoW(uiAction, 0, &lfw, fWinIni);
          }
      case SPI_GETDESKWALLPAPER:
      {
        BOOL Ret;
        WCHAR awc[MAX_PATH];
        UNICODE_STRING ustrWallpaper;
        ANSI_STRING astrWallpaper;

        Ret = NtUserSystemParametersInfo(SPI_GETDESKWALLPAPER, MAX_PATH, awc, fWinIni);
        RtlInitUnicodeString(&ustrWallpaper, awc);
        RtlInitEmptyAnsiString(&astrWallpaper, pvParam, uiParam);
        RtlUnicodeStringToAnsiString(&astrWallpaper, &ustrWallpaper, FALSE);
        return Ret;
      }

      case SPI_GETHIGHCONTRAST:
      {
          LPHIGHCONTRASTA HighContrast = (LPHIGHCONTRASTA)pvParam;
          HIGHCONTRASTW Wide;
          UNICODE_STRING Scheme;
          ANSI_STRING AnsiScheme;

          if (!HighContrast || HighContrast->cbSize != sizeof(HIGHCONTRASTA))
          {
              SetLastError(ERROR_INVALID_PARAMETER);
              return FALSE;
          }
          Wide.cbSize = sizeof(Wide);
          if (!GetHighContrastW(sizeof(Wide), &Wide, fWinIni))
              return FALSE;
          RtlInitUnicodeString(&Scheme, Wide.lpszDefaultScheme);
          AnsiScheme.Length = 0;
          AnsiScheme.MaximumLength = (USHORT)RtlUnicodeStringToAnsiSize(&Scheme);
          AnsiScheme.Buffer = LocalAlloc(LMEM_FIXED, AnsiScheme.MaximumLength);
          if (AnsiScheme.Buffer)
              RtlUnicodeStringToAnsiString(&AnsiScheme, &Scheme, FALSE);
          LocalFree(Wide.lpszDefaultScheme);
          if (!AnsiScheme.Buffer)
              return FALSE;
          HighContrast->dwFlags = Wide.dwFlags;
          HighContrast->lpszDefaultScheme = AnsiScheme.Buffer;
          return TRUE;
      }

      case SPI_SETHIGHCONTRAST:
      {
          LPHIGHCONTRASTA HighContrast = (LPHIGHCONTRASTA)pvParam;
          HIGHCONTRASTW Wide;
          UNICODE_STRING Scheme;
          BOOL Ret;

          if (!HighContrast || HighContrast->cbSize != sizeof(HIGHCONTRASTA))
          {
              SetLastError(ERROR_INVALID_PARAMETER);
              return FALSE;
          }
          RtlInitEmptyUnicodeString(&Scheme, NULL, 0);
          if (HighContrast->lpszDefaultScheme &&
              !RtlCreateUnicodeStringFromAsciiz(&Scheme, HighContrast->lpszDefaultScheme))
          {
              SetLastError(ERROR_NOT_ENOUGH_MEMORY);
              return FALSE;
          }
          Wide.cbSize = sizeof(Wide);
          Wide.dwFlags = HighContrast->dwFlags;
          Wide.lpszDefaultScheme = Scheme.Buffer;
          Ret = NtUserSystemParametersInfo(SPI_SETHIGHCONTRAST, sizeof(Wide), &Wide, fWinIni);
          RtlFreeUnicodeString(&Scheme);
          return Ret;
      }

      case SPI_SETDESKWALLPAPER:
      {
          UNICODE_STRING ustrWallpaper;
          BOOL Ret;

          if (pvParam)
          {
            if (!RtlCreateUnicodeStringFromAsciiz(&ustrWallpaper, pvParam))
            {
                ERR("RtlCreateUnicodeStringFromAsciiz failed\n");
                return FALSE;
            }
            pvParam = &ustrWallpaper;
          }

          Ret = RealSystemParametersInfoW(SPI_SETDESKWALLPAPER, uiParam,
                                          pvParam ? ustrWallpaper.Buffer : NULL, fWinIni);

          if (pvParam)
            RtlFreeUnicodeString(&ustrWallpaper);

          return Ret;
      }
    }
    return User32SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
}

BOOL WINAPI
RealSystemParametersInfoW(UINT uiAction,
		      UINT uiParam,
		      PVOID pvParam,
		      UINT fWinIni)
{
  switch(uiAction)
  {
    case SPI_GETHIGHCONTRAST:
      return GetHighContrastW(uiParam, pvParam, fWinIni);

    case SPI_SETCURSORS:
      LoadUserCursors();
      break;

    case SPI_SETDESKWALLPAPER:
      {
          UNICODE_STRING ustrWallpaper;
          HBITMAP Bitmap;

          RtlInitUnicodeString(&ustrWallpaper, pvParam);
          Bitmap = pvParam ? UserLoadWallpaperBitmap(pvParam, fWinIni) : NULL;
          if (Bitmap)
          {
              if (NtUserSystemParametersInfo(SPI_ROS_SETDESKWALLPAPERBITMAP, HandleToUlong(Bitmap), &ustrWallpaper, fWinIni))
                  return TRUE;
              DeleteObject(Bitmap);
          }
          return NtUserSystemParametersInfo(SPI_SETDESKWALLPAPER, uiParam, &ustrWallpaper, fWinIni);
      }
  }
  return User32SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
}


/*
 * @implemented
 */
BOOL WINAPI
SystemParametersInfoA(UINT uiAction,
		      UINT uiParam,
		      PVOID pvParam,
		      UINT fWinIni)
{
   BOOL Hook, Ret = FALSE;

   Hook = BeginIfHookedUserApiHook();

   /* Bypass SEH and go direct. */
   if (!Hook) return RealSystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);

   _SEH2_TRY
   {
      Ret = guah.SystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
       ERR("Got exception in hooked SystemParametersInfoA!\n");
   }
   _SEH2_END;

   EndUserApiHook();

   return Ret;
}

/*
 * @implemented
 */
BOOL WINAPI
SystemParametersInfoW(UINT uiAction,
		      UINT uiParam,
		      PVOID pvParam,
		      UINT fWinIni)
{
   BOOL Hook, Ret = FALSE;

   Hook = BeginIfHookedUserApiHook();

   /* Bypass SEH and go direct. */
   if (!Hook) return RealSystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);

   _SEH2_TRY
   {
      Ret = guah.SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
   }
   _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
   {
       ERR("Got exception in hooked SystemParametersInfoW!\n");
   }
   _SEH2_END;

   EndUserApiHook();

   return Ret;
}

/*
 * @implemented
 */
HDESK WINAPI
CreateDesktopA(LPCSTR lpszDesktop,
	       LPCSTR lpszDevice,
	       LPDEVMODEA pDevmode,
	       DWORD dwFlags,
	       ACCESS_MASK dwDesiredAccess,
	       LPSECURITY_ATTRIBUTES lpsa)
{
    UNICODE_STRING DesktopNameU;
    HDESK hDesktop;
    LPDEVMODEW DevmodeW = NULL;

    if (lpszDesktop)
    {
        /* After conversion, the buffer is zero-terminated */
        RtlCreateUnicodeStringFromAsciiz(&DesktopNameU, lpszDesktop);
    }
    else
    {
        RtlInitUnicodeString(&DesktopNameU, NULL);
    }

    if (pDevmode)
        DevmodeW = GdiConvertToDevmodeW(pDevmode);

    hDesktop = CreateDesktopW(DesktopNameU.Buffer,
                              NULL,
                              DevmodeW,
                              dwFlags,
                              dwDesiredAccess,
                              lpsa);

    /* Free the string, if it was allocated */
    if (lpszDesktop) RtlFreeUnicodeString(&DesktopNameU);

    return hDesktop;
}


/*
 * @implemented
 */
HDESK WINAPI
CreateDesktopW(LPCWSTR lpszDesktop,
	       LPCWSTR lpszDevice,
	       LPDEVMODEW pDevmode,
	       DWORD dwFlags,
	       ACCESS_MASK dwDesiredAccess,
	       LPSECURITY_ATTRIBUTES lpsa)
{
  OBJECT_ATTRIBUTES oas;
  UNICODE_STRING DesktopName, DesktopDevice;
  HWINSTA hWinSta;
  HDESK hDesktop;
  ULONG Attributes = (OBJ_OPENIF|OBJ_CASE_INSENSITIVE);

  /* Retrive WinStation handle. */
  hWinSta = NtUserGetProcessWindowStation();

  /* Initialize the strings. */
  RtlInitUnicodeString(&DesktopName, lpszDesktop);
  RtlInitUnicodeString(&DesktopDevice, lpszDevice);

  /* Check for process is inherited, set flag if set. */
  if (lpsa && lpsa->bInheritHandle) Attributes |= OBJ_INHERIT;

  /* Initialize the attributes for the desktop. */
  InitializeObjectAttributes( &oas,
                              &DesktopName,
                               Attributes,
                               hWinSta,
                               lpsa ? lpsa->lpSecurityDescriptor : NULL);

  /* Send the request and call to win32k. */
  hDesktop = NtUserCreateDesktop( &oas,
                                  &DesktopDevice,
                                   pDevmode,
				   dwFlags,
				   dwDesiredAccess);

  return(hDesktop);
}


/*
 * @implemented
 */
BOOL
WINAPI
EnumDesktopsA(
  HWINSTA WindowStation,
  DESKTOPENUMPROCA EnumFunc,
  LPARAM Context)
{
   return EnumNamesA(WindowStation, EnumFunc, Context, TRUE);
}


/*
 * @implemented
 */
BOOL
WINAPI
EnumDesktopsW(
  HWINSTA WindowStation,
  DESKTOPENUMPROCW EnumFunc,
  LPARAM Context)
{
   return EnumNamesW(WindowStation, EnumFunc, Context, TRUE);
}


/*
 * @implemented
 */
HDESK
WINAPI
GetThreadDesktop(
  DWORD dwThreadId)
{
    USER_API_MESSAGE ApiMessage;
    PUSER_GET_THREAD_CONSOLE_DESKTOP GetThreadConsoleDesktopRequest = &ApiMessage.Data.GetThreadConsoleDesktopRequest;
    DWORD LastError;
    HDESK Desktop;

    /* Most threads have a desktop in win32k. Ask CONSRV only for the
       non-Win32 console-thread fallback. */
    LastError = GetLastError();
    Desktop = NtUserGetThreadDesktop(dwThreadId, NULL);
    if (Desktop)
        return Desktop;

    if (GetLastError() != ERROR_INVALID_PARAMETER)
        return NULL;

    GetThreadConsoleDesktopRequest->ThreadId = dwThreadId;

    CsrClientCallServer((PCSR_API_MESSAGE)&ApiMessage,
                        NULL,
                        CSR_CREATE_API_NUMBER(USERSRV_SERVERDLL_INDEX, UserpGetThreadConsoleDesktop),
                        sizeof(*GetThreadConsoleDesktopRequest));
    if (!NT_SUCCESS(ApiMessage.Status))
    {
        UserSetLastNTError(ApiMessage.Status);
        return NULL;
    }

    SetLastError(LastError);
    return NtUserGetThreadDesktop(dwThreadId,
                                  GetThreadConsoleDesktopRequest->ConsoleDesktop);
}


/*
 * @implemented
 */
HDESK
WINAPI
OpenDesktopA(
  LPCSTR lpszDesktop,
  DWORD dwFlags,
  BOOL fInherit,
  ACCESS_MASK dwDesiredAccess)
{
    UNICODE_STRING DesktopNameU;
    HDESK hDesktop;

    if (lpszDesktop)
    {
        /* After conversion, the buffer is zero-terminated */
        RtlCreateUnicodeStringFromAsciiz(&DesktopNameU, lpszDesktop);
    }
    else
    {
        RtlInitUnicodeString(&DesktopNameU, NULL);
    }

    hDesktop = OpenDesktopW(DesktopNameU.Buffer,
                            dwFlags,
                            fInherit,
                            dwDesiredAccess);

    /* Free the string, if it was allocated */
    if (lpszDesktop) RtlFreeUnicodeString(&DesktopNameU);

    return hDesktop;
}


/*
 * @implemented
 */
HDESK
WINAPI
OpenDesktopW(
  LPCWSTR lpszDesktop,
  DWORD dwFlags,
  BOOL fInherit,
  ACCESS_MASK dwDesiredAccess)
{
  UNICODE_STRING DesktopName;
  OBJECT_ATTRIBUTES ObjectAttributes;

  RtlInitUnicodeString(&DesktopName, lpszDesktop);

  InitializeObjectAttributes(&ObjectAttributes,
                             &DesktopName,
                             OBJ_CASE_INSENSITIVE,
                             GetProcessWindowStation(),
                             0);

  if( fInherit )
  {
      ObjectAttributes.Attributes |= OBJ_INHERIT;
  }

  return NtUserOpenDesktop(&ObjectAttributes, dwFlags, dwDesiredAccess);
}


/*
 * @implemented
 */
BOOL WINAPI
SetShellWindow(HWND hwndShell)
{
	return SetShellWindowEx(hwndShell, hwndShell);
}


/*
 * @implemented
 */
HWND WINAPI
GetShellWindow(VOID)
{
#ifdef WOW64_I386_RUNTIME
   return (HWND)NtUserCallOneParam(0, ONEPARAM_ROUTINE_ROS_GETSHELLWINDOW);
#else
   PDESKTOPINFO pdi;
   pdi = GetThreadDesktopInfo();
   if (pdi) return pdi->hShellWindow;
   return NULL;
#endif
}


/* EOF */
