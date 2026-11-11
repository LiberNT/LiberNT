/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test how SetWindowPos lays out the whole owner tree of a window
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

enum { W_O, W_A, W_B, W_C, W_P, W_Q, W_COUNT };
enum { OP_NOTOPMOST, OP_TOP, OP_BOTTOM, OP_AFTER_P, OP_AFTER_Q };

typedef struct _OWNER_TREE_CASE
{
    char Active;
    const char *Spec;
    char Target;
    int Op;
    const char *ZOrder;
    const char *Messages;
} OWNER_TREE_CASE;

/* Windows 11 results, non-topmost band from the top; A and B are owned by O, C by A; '-' activates a separate topmost window. */
static const OWNER_TREE_CASE Cases[] =
{
    { '-', "AOP", 'O', OP_NOTOPMOST, "AOP", "A>? O>A" },
    { '-', "AOP", 'O', OP_TOP, "AOP", "A>? O>A" },
    { '-', "AOP", 'O', OP_BOTTOM, "PAO", "A>? O>A A! O!" },
    { '-', "AOP", 'O', OP_AFTER_P, "PAO", "A>P O>A A! O!" },
    { '-', "AOP", 'A', OP_NOTOPMOST, "AOP", "A>? O>A" },
    { '-', "AOP", 'A', OP_TOP, "AOP", "A>? O>A" },
    { '-', "AOP", 'A', OP_BOTTOM, "PAO", "A>BOTTOM O>A A! O!" },
    { '-', "AOP", 'A', OP_AFTER_P, "PAO", "A>P O>A A! O!" },
    { '-', "APO", 'O', OP_NOTOPMOST, "PAO", "A>P O>A A!" },
    { '-', "APO", 'O', OP_TOP, "AOP", "A>? O>A O!" },
    { '-', "APO", 'O', OP_BOTTOM, "PAO", "A>? O>A A! O!" },
    { '-', "APO", 'O', OP_AFTER_P, "PAO", "A>P O>A A!" },
    { '-', "APO", 'A', OP_NOTOPMOST, "AOP", "A>? O>A O!" },
    { '-', "APO", 'A', OP_TOP, "AOP", "A>? O>A O!" },
    { '-', "APO", 'A', OP_BOTTOM, "PAO", "A>BOTTOM O>A A! O!" },
    { '-', "APO", 'A', OP_AFTER_P, "PAO", "A>P O>A A!" },
    { '-', "ABOP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B" },
    { '-', "ABOP", 'O', OP_TOP, "ABOP", "A>? B>A O>B" },
    { '-', "ABOP", 'O', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "ABOP", 'O', OP_AFTER_P, "PABO", "A>P B>A O>B A! B! O!" },
    { '-', "ABOP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B" },
    { '-', "ABOP", 'A', OP_TOP, "ABOP", "A>? B>A O>B" },
    { '-', "ABOP", 'A', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "ABOP", 'A', OP_AFTER_P, "PBAO", "B>P A>B O>A B! A! O!" },
    { '-', "ABOP", 'B', OP_NOTOPMOST, "ABOP", "B>A" },
    { '-', "ABOP", 'B', OP_TOP, "BAOP", "B>? A>B O>A B!" },
    { '-', "ABOP", 'B', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "ABOP", 'B', OP_AFTER_P, "PABO", "A>P B>A O>B A! B! O!" },
    { '-', "AOBP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B!" },
    { '-', "AOBP", 'O', OP_TOP, "ABOP", "A>? B>A O>B B!" },
    { '-', "AOBP", 'O', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "AOBP", 'O', OP_AFTER_P, "PABO", "A>P B>A O>B A! B! O!" },
    { '-', "AOBP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B!" },
    { '-', "AOBP", 'A', OP_TOP, "ABOP", "A>? B>A O>B B!" },
    { '-', "AOBP", 'A', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "AOBP", 'A', OP_AFTER_P, "PBAO", "B>P A>B O>A B! A! O!" },
    { '-', "AOBP", 'B', OP_NOTOPMOST, "AOBP", "B>O" },
    { '-', "AOBP", 'B', OP_TOP, "BAOP", "B>? A>B O>A B!" },
    { '-', "AOBP", 'B', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "AOBP", 'B', OP_AFTER_P, "PABO", "A>P B>A O>B A! B! O!" },
    { '-', "BAOP", 'O', OP_NOTOPMOST, "BAOP", "B>? A>B O>A" },
    { '-', "BAOP", 'O', OP_TOP, "BAOP", "B>? A>B O>A" },
    { '-', "BAOP", 'O', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "BAOP", 'O', OP_AFTER_P, "PBAO", "B>P A>B O>A B! A! O!" },
    { '-', "BAOP", 'A', OP_NOTOPMOST, "BAOP", "A>B" },
    { '-', "BAOP", 'A', OP_TOP, "ABOP", "A>? B>A O>B A!" },
    { '-', "BAOP", 'A', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "BAOP", 'A', OP_AFTER_P, "PBAO", "B>P A>B O>A B! A! O!" },
    { '-', "BAOP", 'B', OP_NOTOPMOST, "BAOP", "B>? A>B O>A" },
    { '-', "BAOP", 'B', OP_TOP, "BAOP", "B>? A>B O>A" },
    { '-', "BAOP", 'B', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "BAOP", 'B', OP_AFTER_P, "PABO", "A>P B>A O>B A! B! O!" },
    { '-', "APBO", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B! O!" },
    { '-', "APBO", 'O', OP_TOP, "ABOP", "A>? B>A O>B B! O!" },
    { '-', "APBO", 'O', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "APBO", 'O', OP_AFTER_P, "PABO", "A>P B>A O>B A!" },
    { '-', "APBO", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B! O!" },
    { '-', "APBO", 'A', OP_TOP, "ABOP", "A>? B>A O>B B! O!" },
    { '-', "APBO", 'A', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "APBO", 'A', OP_AFTER_P, "PABO", "A>P B>A O>B A!" },
    { '-', "APBO", 'B', OP_NOTOPMOST, "PABO", "A>P B>A O>B A!" },
    { '-', "APBO", 'B', OP_TOP, "BAOP", "B>? A>B O>A B! O!" },
    { '-', "APBO", 'B', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "APBO", 'B', OP_AFTER_P, "PABO", "A>P B>A O>B A!" },
    { '-', "CAOP", 'O', OP_NOTOPMOST, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'O', OP_TOP, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'O', OP_BOTTOM, "PCAO", "C>? A>C O>A C! A! O!" },
    { '-', "CAOP", 'O', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "CAOP", 'A', OP_NOTOPMOST, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'A', OP_TOP, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'A', OP_BOTTOM, "PCAO", "C>? A>C O>A C! A! O!" },
    { '-', "CAOP", 'A', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "CAOP", 'C', OP_NOTOPMOST, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'C', OP_TOP, "CAOP", "C>? A>C O>A" },
    { '-', "CAOP", 'C', OP_BOTTOM, "PCAO", "C>BOTTOM A>C O>A C! A! O!" },
    { '-', "CAOP", 'C', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "ACOP", 'O', OP_NOTOPMOST, "CAOP", "C>? A>C O>A C!" },
    { '-', "ACOP", 'O', OP_TOP, "CAOP", "C>? A>C O>A C!" },
    { '-', "ACOP", 'O', OP_BOTTOM, "PCAO", "C>? A>C O>A C! A! O!" },
    { '-', "ACOP", 'O', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "ACOP", 'A', OP_NOTOPMOST, "CAOP", "C>? A>C O>A C!" },
    { '-', "ACOP", 'A', OP_TOP, "CAOP", "C>? A>C O>A C!" },
    { '-', "ACOP", 'A', OP_BOTTOM, "PCAO", "C>? A>C O>A C! A! O!" },
    { '-', "ACOP", 'A', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "ACOP", 'C', OP_NOTOPMOST, "ACOP", "C>A" },
    { '-', "ACOP", 'C', OP_TOP, "CAOP", "C>? A>C O>A C!" },
    { '-', "ACOP", 'C', OP_BOTTOM, "PCAO", "C>BOTTOM A>C O>A C! A! O!" },
    { '-', "ACOP", 'C', OP_AFTER_P, "PCAO", "C>P A>C O>A C! A! O!" },
    { '-', "PAO", 'O', OP_NOTOPMOST, "PAO", "A>P O>A" },
    { '-', "PAO", 'O', OP_TOP, "AOP", "A>? O>A A! O!" },
    { '-', "PAO", 'O', OP_BOTTOM, "PAO", "A>? O>A A! O!" },
    { '-', "PAO", 'O', OP_AFTER_P, "PAO", "A>P O>A" },
    { '-', "PAO", 'A', OP_NOTOPMOST, "PAO", "A>P O>A" },
    { '-', "PAO", 'A', OP_TOP, "AOP", "A>? O>A A! O!" },
    { '-', "PAO", 'A', OP_BOTTOM, "PAO", "A>BOTTOM O>A A! O!" },
    { '-', "PAO", 'A', OP_AFTER_P, "PAO", "A>P O>A" },
    { '-', "PQAO", 'O', OP_NOTOPMOST, "PQAO", "A>Q O>A" },
    { '-', "PQAO", 'O', OP_TOP, "AOPQ", "A>? O>A A! O!" },
    { '-', "PQAO", 'O', OP_BOTTOM, "PQAO", "A>? O>A A! O!" },
    { '-', "PQAO", 'O', OP_AFTER_P, "PAOQ", "A>P O>A A! O!" },
    { '-', "PQAO", 'O', OP_AFTER_Q, "PQAO", "A>Q O>A" },
    { '-', "PQAO", 'A', OP_NOTOPMOST, "PQAO", "A>Q O>A" },
    { '-', "PQAO", 'A', OP_TOP, "AOPQ", "A>? O>A A! O!" },
    { '-', "PQAO", 'A', OP_BOTTOM, "PQAO", "A>BOTTOM O>A A! O!" },
    { '-', "PQAO", 'A', OP_AFTER_P, "PAOQ", "A>P O>A A! O!" },
    { '-', "PQAO", 'A', OP_AFTER_Q, "PQAO", "A>Q O>A" },
    { '-', "APQBO", 'O', OP_NOTOPMOST, "ABOPQ", "A>? B>A O>B B! O!" },
    { '-', "APQBO", 'O', OP_TOP, "ABOPQ", "A>? B>A O>B B! O!" },
    { '-', "APQBO", 'O', OP_BOTTOM, "PQABO", "A>? B>A O>B A! B! O!" },
    { '-', "APQBO", 'O', OP_AFTER_P, "PABOQ", "A>P B>A O>B A! B! O!" },
    { '-', "APQBO", 'O', OP_AFTER_Q, "PQABO", "A>Q B>A O>B A!" },
    { '-', "APQBO", 'A', OP_NOTOPMOST, "ABOPQ", "A>? B>A O>B B! O!" },
    { '-', "APQBO", 'A', OP_TOP, "ABOPQ", "A>? B>A O>B B! O!" },
    { '-', "APQBO", 'A', OP_BOTTOM, "PQBAO", "B>? A>B O>A B! A! O!" },
    { '-', "APQBO", 'A', OP_AFTER_P, "PABOQ", "A>P B>A O>B A! B! O!" },
    { '-', "APQBO", 'A', OP_AFTER_Q, "PQABO", "A>Q B>A O>B A!" },
    { '-', "APQBO", 'B', OP_NOTOPMOST, "PQABO", "A>Q B>A O>B A!" },
    { '-', "APQBO", 'B', OP_TOP, "BAOPQ", "B>? A>B O>A B! O!" },
    { '-', "APQBO", 'B', OP_BOTTOM, "PQABO", "A>? B>A O>B A! B! O!" },
    { '-', "APQBO", 'B', OP_AFTER_P, "PABOQ", "A>P B>A O>B A! B! O!" },
    { '-', "APQBO", 'B', OP_AFTER_Q, "PQABO", "A>Q B>A O>B A!" },
    { '-', "BPAO", 'O', OP_NOTOPMOST, "BAOP", "B>? A>B O>A A! O!" },
    { '-', "BPAO", 'O', OP_TOP, "BAOP", "B>? A>B O>A A! O!" },
    { '-', "BPAO", 'O', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "BPAO", 'O', OP_AFTER_P, "PBAO", "B>P A>B O>A B!" },
    { '-', "BPAO", 'A', OP_NOTOPMOST, "PBAO", "B>P A>B O>A B!" },
    { '-', "BPAO", 'A', OP_TOP, "ABOP", "A>? B>A O>B A! O!" },
    { '-', "BPAO", 'A', OP_BOTTOM, "PBAO", "B>? A>B O>A B! A! O!" },
    { '-', "BPAO", 'A', OP_AFTER_P, "PBAO", "B>P A>B O>A B!" },
    { '-', "BPAO", 'B', OP_NOTOPMOST, "BAOP", "B>? A>B O>A A! O!" },
    { '-', "BPAO", 'B', OP_TOP, "BAOP", "B>? A>B O>A A! O!" },
    { '-', "BPAO", 'B', OP_BOTTOM, "PABO", "A>? B>A O>B A! B! O!" },
    { '-', "BPAO", 'B', OP_AFTER_P, "PBAO", "B>P A>B O>A B!" },
    { 'A', "AOP", 'O', OP_NOTOPMOST, "AOP", "A>? O>A" },
    { 'A', "AOP", 'A', OP_NOTOPMOST, "AOP", "A>? O>A" },
    { 'A', "APO", 'O', OP_NOTOPMOST, "PAO", "A>? O>A A!" },
    { 'A', "APO", 'A', OP_NOTOPMOST, "AOP", "A>? O>A O!" },
    { 'A', "ABOP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B" },
    { 'A', "ABOP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B" },
    { 'A', "ABOP", 'B', OP_NOTOPMOST, "ABOP", "B>A" },
    { 'A', "AOBP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B!" },
    { 'A', "AOBP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B!" },
    { 'A', "AOBP", 'B', OP_NOTOPMOST, "AOBP", "B>O" },
    { 'A', "APBO", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B! O!" },
    { 'A', "APBO", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>B B! O!" },
    { 'A', "APBO", 'B', OP_NOTOPMOST, "PABO", "A>? B>A O>B A!" },
    { 'A', "BAOP", 'O', OP_NOTOPMOST, "BAOP", "B>? A>? O>A" },
    { 'A', "BAOP", 'A', OP_NOTOPMOST, "BAOP", "B>? A>? O>A" },
    { 'A', "BAOP", 'B', OP_NOTOPMOST, "BAOP", "B>? A>? O>A" },
    { 'B', "ABOP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>? O>B" },
    { 'B', "ABOP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>? O>B" },
    { 'B', "ABOP", 'B', OP_NOTOPMOST, "ABOP", "A>? B>? O>B" },
    { 'B', "AOBP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>? O>B B!" },
    { 'B', "AOBP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>? O>B B!" },
    { 'B', "AOBP", 'B', OP_NOTOPMOST, "ABOP", "A>? B>? O>B B!" },
    { 'B', "APBO", 'O', OP_NOTOPMOST, "ABOP", "A>? B>? O>B B! O!" },
    { 'B', "APBO", 'A', OP_NOTOPMOST, "ABOP", "A>? B>? O>B B! O!" },
    { 'B', "APBO", 'B', OP_NOTOPMOST, "PABO", "A>P B>? O>B A!" },
    { 'B', "BAOP", 'O', OP_NOTOPMOST, "BAOP", "B>? A>B O>A" },
    { 'B', "BAOP", 'A', OP_NOTOPMOST, "BAOP", "A>B" },
    { 'B', "BAOP", 'B', OP_NOTOPMOST, "BAOP", "B>? A>B O>A" },
    { 'O', "AOP", 'O', OP_NOTOPMOST, "AOP", "A>? O>?" },
    { 'O', "AOP", 'A', OP_NOTOPMOST, "AOP", "A>? O>?" },
    { 'O', "APO", 'O', OP_NOTOPMOST, "PAO", "A>P O>? A!" },
    { 'O', "APO", 'A', OP_NOTOPMOST, "AOP", "A>? O>? O!" },
    { 'O', "ABOP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>?" },
    { 'O', "ABOP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>?" },
    { 'O', "ABOP", 'B', OP_NOTOPMOST, "ABOP", "B>A" },
    { 'O', "AOBP", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>? B!" },
    { 'O', "AOBP", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>? B!" },
    { 'O', "AOBP", 'B', OP_NOTOPMOST, "AOBP", "B>O" },
    { 'O', "APBO", 'O', OP_NOTOPMOST, "ABOP", "A>? B>A O>? B! O!" },
    { 'O', "APBO", 'A', OP_NOTOPMOST, "ABOP", "A>? B>A O>? B! O!" },
    { 'O', "APBO", 'B', OP_NOTOPMOST, "PABO", "A>P B>A O>? A!" },
    { 'O', "BAOP", 'O', OP_NOTOPMOST, "BAOP", "B>? A>B O>?" },
    { 'O', "BAOP", 'A', OP_NOTOPMOST, "BAOP", "A>B" },
    { 'O', "BAOP", 'B', OP_NOTOPMOST, "BAOP", "B>? A>B O>?" },
};

static const char Names[] = "OABCPQ";
static HWND Windows[W_COUNT];
static HWND hwndActive;
static BOOL Logging;
static char Log[512];

static int
WindowIndex(HWND hWnd)
{
    int i;

    for (i = 0; i < W_COUNT; i++)
    {
        if (hWnd && Windows[i] == hWnd)
            return i;
    }
    return -1;
}

static void
Append(const char *Text)
{
    if (strlen(Log) + strlen(Text) + 2 < sizeof(Log))
    {
        if (Log[0])
            strcat(Log, " ");
        strcat(Log, Text);
    }
}

static LRESULT CALLBACK
OwnerTreeWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    int Index = WindowIndex(hWnd);

    if (Logging && Index >= 0 && (Msg == WM_WINDOWPOSCHANGING || Msg == WM_WINDOWPOSCHANGED))
    {
        WINDOWPOS *Pos = (WINDOWPOS *)lParam;
        char Text[16];

        if (Msg == WM_WINDOWPOSCHANGED)
        {
            sprintf(Text, "%c!", Names[Index]);
        }
        else
        {
            int After = WindowIndex(Pos->hwndInsertAfter);
            char AfterName = After >= 0 ? Names[After] : '?';

            if (Pos->hwndInsertAfter == HWND_BOTTOM)
                sprintf(Text, "%c>BOTTOM", Names[Index]);
            else
                sprintf(Text, "%c>%c", Names[Index], AfterName);
            if (Pos->flags & SWP_NOZORDER)
                strcat(Text, "/nz");
        }
        Append(Text);
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
FlushMessages(void)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&Msg);
}

static void
GetZOrder(char *Buffer)
{
    HWND hWnd;
    int Index, Length = 0;

    for (hWnd = GetTopWindow(NULL); hWnd; hWnd = GetWindow(hWnd, GW_HWNDNEXT))
    {
        Index = WindowIndex(hWnd);
        if (Index >= 0 && !(GetWindowLongPtrW(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
            Buffer[Length++] = Names[Index];
    }
    Buffer[Length] = 0;
}

static void
DestroyTree(void)
{
    int i;

    for (i = W_COUNT - 1; i >= 0; i--)
    {
        if (Windows[i])
            DestroyWindow(Windows[i]);
        Windows[i] = NULL;
    }
    FlushMessages();
}

static HWND
CreateTreeWindow(int Index, HWND hwndOwner)
{
    HWND hWnd = CreateWindowExW(0, L"SetWindowPosOwnerTree", L"tree", WS_OVERLAPPEDWINDOW,
                                20 + 60 * Index, 60 + 20 * Index, 160, 120, hwndOwner, NULL, NULL, NULL);

    ShowWindow(hWnd, SW_SHOWNA);
    return hWnd;
}

static void
Arrange(const char *Spec)
{
    int i;

    for (i = (int)strlen(Spec) - 1; i >= 0; i--)
    {
        SetWindowPos(Windows[strchr(Names, Spec[i]) - Names], HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

static BOOL
SetupTree(const OWNER_TREE_CASE *Case)
{
    HWND hwndWant;

    DestroyTree();
    Windows[W_O] = CreateTreeWindow(W_O, NULL);
    if (strchr(Case->Spec, 'A'))
        Windows[W_A] = CreateTreeWindow(W_A, Windows[W_O]);
    if (strchr(Case->Spec, 'B'))
        Windows[W_B] = CreateTreeWindow(W_B, Windows[W_O]);
    if (strchr(Case->Spec, 'C'))
        Windows[W_C] = CreateTreeWindow(W_C, Windows[W_A]);
    Windows[W_P] = CreateTreeWindow(W_P, NULL);
    if (strchr(Case->Spec, 'Q'))
        Windows[W_Q] = CreateTreeWindow(W_Q, NULL);
    Arrange(Case->Spec);

    hwndWant = Case->Active == '-' ? hwndActive : Windows[strchr(Names, Case->Active) - Names];
    SetForegroundWindow(hwndWant);
    SetActiveWindow(hwndWant);
    if (Case->Active != '-')
        Arrange(Case->Spec);
    FlushMessages();
    return GetActiveWindow() == hwndWant;
}

static void
RunCase(const OWNER_TREE_CASE *Case)
{
    char Before[W_COUNT + 1], After[W_COUNT + 1];
    HWND hwndAfter;

    if (!SetupTree(Case))
    {
        skip("%c %s %c op %d: the window could not be activated\n", Case->Active, Case->Spec, Case->Target, Case->Op);
        return;
    }
    GetZOrder(Before);
    if (strcmp(Before, Case->Spec))
    {
        skip("%c %s %c op %d: the setup gave %s\n", Case->Active, Case->Spec, Case->Target, Case->Op, Before);
        return;
    }

    switch (Case->Op)
    {
        case OP_NOTOPMOST: hwndAfter = HWND_NOTOPMOST; break;
        case OP_TOP: hwndAfter = HWND_TOP; break;
        case OP_BOTTOM: hwndAfter = HWND_BOTTOM; break;
        case OP_AFTER_P: hwndAfter = Windows[W_P]; break;
        default: hwndAfter = Windows[W_Q]; break;
    }

    Log[0] = 0;
    Logging = TRUE;
    SetWindowPos(Windows[strchr(Names, Case->Target) - Names], hwndAfter, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    FlushMessages();
    Logging = FALSE;

    GetZOrder(After);
    ok(!strcmp(After, Case->ZOrder), "%c %s %c op %d: z-order %s, expected %s\n",
       Case->Active, Case->Spec, Case->Target, Case->Op, After, Case->ZOrder);
    ok(!strcmp(Log, Case->Messages), "%c %s %c op %d: messages \"%s\", expected \"%s\"\n",
       Case->Active, Case->Spec, Case->Target, Case->Op, Log, Case->Messages);
}

START_TEST(SetWindowPosOwnerTree)
{
    WNDCLASSW Class = { 0 };
    UINT i;

    Class.lpfnWndProc = OwnerTreeWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"SetWindowPosOwnerTree";
    Class.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassW(&Class);

    hwndActive = CreateWindowExW(WS_EX_TOPMOST, L"SetWindowPosOwnerTree", L"active", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 700, 400, 160, 120, NULL, NULL, NULL, NULL);
    ok(hwndActive != NULL, "CreateWindowEx failed, error %lu\n", GetLastError());
    if (!hwndActive)
        return;

    for (i = 0; i < _countof(Cases); i++)
        RunCase(&Cases[i]);

    DestroyTree();
    DestroyWindow(hwndActive);
    UnregisterClassW(L"SetWindowPosOwnerTree", GetModuleHandleW(NULL));
}
