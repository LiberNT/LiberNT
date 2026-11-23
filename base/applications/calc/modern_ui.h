#pragma once
#include <windows.h>

void ModernCalcInit(HWND hwnd);
void ModernCalcLayout(HWND hwnd);
void ModernCalcCleanup(void);
INT_PTR ModernCalcDrawButton(const DRAWITEMSTRUCT *dis);
INT_PTR ModernCalcErase(HDC dc, HWND hwnd);
INT_PTR ModernCalcStaticColor(HDC dc, HWND control, HWND hwnd);
void ModernCalcAppendCredit(TCHAR *buffer, size_t capacity);

void ModernCalcInitOther(HWND hwnd, int mode);
void ModernCalcSetExpression(HWND hwnd, LPCTSTR value);

BOOL ModernCalcHistoryCommand(HWND hwnd, UINT id);
void ModernCalcHistoryAdd(LPCTSTR expression, LPCTSTR result);

INT_PTR ModernCalcHistoryColor(HDC dc);

void ModernCalcShowAbout(HWND owner, LPCTSTR title, LPCTSTR credit);

BOOL ModernCalcScientificCommand(HWND hwnd, UINT id);
void ModernCalcScientificRefresh(HWND hwnd);
