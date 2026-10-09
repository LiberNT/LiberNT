/*
 * PROJECT:     LiberNT Console Server DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pseudo console support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "consrv.h"
#include "pty.h"

#include <ndk/iofuncs.h>
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

NTSTATUS NTAPI
ConDrvSetConsoleScreenBufferSize(IN PCONSOLE Console,
                                 IN PTEXTMODE_SCREEN_BUFFER Buffer,
                                 IN PCOORD Size);
NTSTATUS NTAPI
ConDrvSetConsoleWindowInfo(IN PCONSOLE Console,
                           IN PTEXTMODE_SCREEN_BUFFER Buffer,
                           IN BOOLEAN Absolute,
                           IN PSMALL_RECT WindowRect);

#define PTY_ATTRIBUTE_MASK (0x00FF | COMMON_LVB_REVERSE_VIDEO | COMMON_LVB_UNDERSCORE)
#define PTY_INPUT_PARAMETERS 8
#define PTY_FIXED_WIDTH_LIMIT 0x300

typedef struct _PTY_CELL
{
    WCHAR Char;
    WORD Attributes;
} PTY_CELL, *PPTY_CELL;

typedef struct _PTY_BUFFER
{
    PCHAR Data;
    ULONG Length;
    ULONG Capacity;
} PTY_BUFFER, *PPTY_BUFFER;

typedef struct _CONSRV_PTY
{
    LIST_ENTRY ListEntry;
    PCONSRV_CONSOLE Console;
    HANDLE ConsoleHandle;
    HANDLE Owner;
    PFRONTEND_VTBL InnerVtbl;
    FRONTEND_VTBL Vtbl;
    HANDLE Input;
    HANDLE Output;
    HANDLE CloseEvent;
    HANDLE OutputEvent;
    HANDLE InputIoEvent;
    HANDLE OutputIoEvent;
    HANDLE InputThread;
    HANDLE OutputThread;
    COORD Size;
    ULONG Flags;
    LONG ScrolledLines;
    LONG Reset;
    LONG TitleChanged;
    LONG Bell;
    PPTY_CELL Shadow;
    COORD ShadowSize;
    COORD RenderCursor;
    WORD RenderAttributes;
    BOOLEAN RenderAttributesValid;
    BOOLEAN CursorVisible;
} CONSRV_PTY, *PCONSRV_PTY;

typedef enum _PTY_INPUT_STATE
{
    PtyInputGround,
    PtyInputEscape,
    PtyInputCsi,
    PtyInputSs3
} PTY_INPUT_STATE;

typedef struct _PTY_INPUT_PARSER
{
    PTY_INPUT_STATE State;
    ULONG Parameters[PTY_INPUT_PARAMETERS];
    ULONG ParameterCount;
    BOOLEAN ParameterPresent;
    ULONG CodePoint;
    ULONG Continuation;
} PTY_INPUT_PARSER, *PPTY_INPUT_PARSER;

static LIST_ENTRY PtyList;
static RTL_CRITICAL_SECTION PtyListLock;

static
BOOLEAN
PtyAppend(
    _Inout_ PPTY_BUFFER Buffer,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length)
{
    if (Buffer->Length + Length > Buffer->Capacity)
    {
        ULONG Capacity = Buffer->Capacity ? Buffer->Capacity : 1024;
        PCHAR NewData;

        while (Capacity < Buffer->Length + Length)
            Capacity *= 2;

        NewData = ConsoleAllocHeap(0, Capacity);
        if (!NewData)
            return FALSE;

        if (Buffer->Data)
        {
            RtlCopyMemory(NewData, Buffer->Data, Buffer->Length);
            ConsoleFreeHeap(Buffer->Data);
        }
        Buffer->Data = NewData;
        Buffer->Capacity = Capacity;
    }

    RtlCopyMemory(Buffer->Data + Buffer->Length, Data, Length);
    Buffer->Length += Length;
    return TRUE;
}

static
VOID
PtyAppendString(
    _Inout_ PPTY_BUFFER Buffer,
    _In_ PCSTR String)
{
    PtyAppend(Buffer, String, (ULONG)strlen(String));
}

static
VOID
PtyAppendCodePoint(
    _Inout_ PPTY_BUFFER Buffer,
    _In_ ULONG CodePoint)
{
    CHAR Utf8[4];
    ULONG Length;

    if (CodePoint < 0x80)
    {
        Utf8[0] = (CHAR)CodePoint;
        Length = 1;
    }
    else if (CodePoint < 0x800)
    {
        Utf8[0] = (CHAR)(0xC0 | (CodePoint >> 6));
        Utf8[1] = (CHAR)(0x80 | (CodePoint & 0x3F));
        Length = 2;
    }
    else if (CodePoint < 0x10000)
    {
        Utf8[0] = (CHAR)(0xE0 | (CodePoint >> 12));
        Utf8[1] = (CHAR)(0x80 | ((CodePoint >> 6) & 0x3F));
        Utf8[2] = (CHAR)(0x80 | (CodePoint & 0x3F));
        Length = 3;
    }
    else
    {
        Utf8[0] = (CHAR)(0xF0 | (CodePoint >> 18));
        Utf8[1] = (CHAR)(0x80 | ((CodePoint >> 12) & 0x3F));
        Utf8[2] = (CHAR)(0x80 | ((CodePoint >> 6) & 0x3F));
        Utf8[3] = (CHAR)(0x80 | (CodePoint & 0x3F));
        Length = 4;
    }

    PtyAppend(Buffer, Utf8, Length);
}

static
VOID
PtyAppendCursorPosition(
    _Inout_ PPTY_BUFFER Buffer,
    _In_ SHORT X,
    _In_ SHORT Y)
{
    CHAR Sequence[32];

    RtlStringCbPrintfA(Sequence, sizeof(Sequence), "\x1b[%d;%dH", Y + 1, X + 1);
    PtyAppendString(Buffer, Sequence);
}

static
ULONG
PtyAnsiColor(
    _In_ ULONG Color)
{
    return ((Color & FOREGROUND_RED) ? 1 : 0) |
           ((Color & FOREGROUND_GREEN) ? 2 : 0) |
           ((Color & FOREGROUND_BLUE) ? 4 : 0);
}

static
VOID
PtyAppendAttributes(
    _Inout_ PPTY_BUFFER Buffer,
    _In_ WORD Attributes,
    _In_ WORD DefaultAttributes)
{
    CHAR Sequence[48];
    ULONG Foreground = Attributes & 0x0F;
    ULONG Background = (Attributes >> 4) & 0x0F;

    if (Attributes == (DefaultAttributes & 0x00FF))
    {
        PtyAppendString(Buffer, "\x1b[m");
        return;
    }

    RtlStringCbPrintfA(Sequence,
                       sizeof(Sequence),
                       "\x1b[0;%lu;%lu%s%sm",
                       ((Foreground & FOREGROUND_INTENSITY) ? 90 : 30) + PtyAnsiColor(Foreground),
                       ((Background & FOREGROUND_INTENSITY) ? 100 : 40) + PtyAnsiColor(Background),
                       (Attributes & COMMON_LVB_REVERSE_VIDEO) ? ";7" : "",
                       (Attributes & COMMON_LVB_UNDERSCORE) ? ";4" : "");
    PtyAppendString(Buffer, Sequence);
}

static
VOID
PtyResetShadow(
    _Inout_ PCONSRV_PTY Pty,
    _In_ WORD DefaultAttributes)
{
    ULONG Count = (ULONG)Pty->ShadowSize.X * Pty->ShadowSize.Y;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Pty->Shadow[Index].Char = L' ';
        Pty->Shadow[Index].Attributes = DefaultAttributes & 0x00FF;
    }
}

static
VOID
PtyRender(
    _Inout_ PCONSRV_PTY Pty,
    _Inout_ PPTY_BUFFER Out)
{
    PCONSRV_CONSOLE Console = Pty->Console;
    PTEXTMODE_SCREEN_BUFFER Buffer;
    COORD View, Origin, Cursor;
    WORD DefaultAttributes;
    BOOLEAN CursorVisible;
    LONG Scrolled;
    SHORT X, Y;

    if (GetType(Console->ActiveBuffer) != TEXTMODE_BUFFER)
        return;

    Buffer = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
    View = Buffer->ViewSize;
    Origin = Buffer->ViewOrigin;
    DefaultAttributes = Buffer->ScreenDefaultAttrib;
    if (View.X <= 0 || View.Y <= 0)
        return;

    if (!Pty->Shadow || Pty->ShadowSize.X != View.X || Pty->ShadowSize.Y != View.Y)
    {
        PPTY_CELL Shadow = ConsoleAllocHeap(0, (ULONG)View.X * View.Y * sizeof(PTY_CELL));

        if (!Shadow)
            return;
        if (Pty->Shadow)
            ConsoleFreeHeap(Pty->Shadow);
        Pty->Shadow = Shadow;
        Pty->ShadowSize = View;
        InterlockedExchange(&Pty->Reset, 1);
    }

    Scrolled = InterlockedExchange(&Pty->ScrolledLines, 0);
    if (InterlockedExchange(&Pty->Reset, 0))
    {
        PtyAppendString(Out, "\x1b[m\x1b[2J\x1b[H");
        PtyResetShadow(Pty, DefaultAttributes);
        Pty->RenderAttributes = DefaultAttributes & 0x00FF;
        Pty->RenderAttributesValid = TRUE;
        Pty->RenderCursor.X = Pty->RenderCursor.Y = 0;
        Scrolled = 0;
    }

    if (Scrolled > 0 && Scrolled < View.Y)
    {
        ULONG Kept = (ULONG)(View.Y - Scrolled) * View.X;
        ULONG Index;

        PtyAppendString(Out, "\x1b[m");
        Pty->RenderAttributes = DefaultAttributes & 0x00FF;
        Pty->RenderAttributesValid = TRUE;
        PtyAppendCursorPosition(Out, 0, View.Y - 1);
        for (Index = 0; Index < (ULONG)Scrolled; Index++)
            PtyAppend(Out, "\n", 1);

        RtlMoveMemory(Pty->Shadow, Pty->Shadow + (ULONG)Scrolled * View.X, Kept * sizeof(PTY_CELL));
        for (Index = Kept; Index < (ULONG)View.X * View.Y; Index++)
        {
            Pty->Shadow[Index].Char = L' ';
            Pty->Shadow[Index].Attributes = DefaultAttributes & 0x00FF;
        }
        Pty->RenderCursor.X = -1;
    }

    for (Y = 0; Y < View.Y; Y++)
    {
        PPTY_CELL Row = Pty->Shadow + (ULONG)Y * View.X;
        SHORT First = -1, Last = -1;
        BOOLEAN Reposition = FALSE;

        if (Origin.Y + Y >= Buffer->ScreenBufferSize.Y)
            break;

        for (X = 0; X < View.X && Origin.X + X < Buffer->ScreenBufferSize.X; X++)
        {
            PCHAR_INFO Cell = ConioCoordToPointer(Buffer, Origin.X + X, Origin.Y + Y);

            if (Cell->Char.UnicodeChar != Row[X].Char ||
                (Cell->Attributes & PTY_ATTRIBUTE_MASK) != Row[X].Attributes)
            {
                if (First < 0)
                    First = X;
                Last = X;
            }
        }

        if (First < 0)
            continue;

        if (Pty->RenderCursor.X != First || Pty->RenderCursor.Y != Y)
            PtyAppendCursorPosition(Out, First, Y);

        for (X = First; X <= Last; X++)
        {
            PCHAR_INFO Cell = ConioCoordToPointer(Buffer, Origin.X + X, Origin.Y + Y);
            WORD Attributes = Cell->Attributes & PTY_ATTRIBUTE_MASK;
            ULONG CodePoint = Cell->Char.UnicodeChar;

            Row[X].Char = Cell->Char.UnicodeChar;
            Row[X].Attributes = Attributes;

            if (Cell->Attributes & COMMON_LVB_TRAILING_BYTE)
                continue;

            if (Reposition)
            {
                PtyAppendCursorPosition(Out, X, Y);
                Reposition = FALSE;
            }

            if (!Pty->RenderAttributesValid || Pty->RenderAttributes != Attributes)
            {
                PtyAppendAttributes(Out, Attributes, DefaultAttributes);
                Pty->RenderAttributes = Attributes;
                Pty->RenderAttributesValid = TRUE;
            }

            if (CodePoint >= 0xD800 && CodePoint <= 0xDBFF && X < Last)
            {
                PCHAR_INFO Next = ConioCoordToPointer(Buffer, Origin.X + X + 1, Origin.Y + Y);
                ULONG Low = Next->Char.UnicodeChar;

                if (Low >= 0xDC00 && Low <= 0xDFFF)
                {
                    CodePoint = 0x10000 + ((CodePoint - 0xD800) << 10) + (Low - 0xDC00);
                    X++;
                    Row[X].Char = Next->Char.UnicodeChar;
                    Row[X].Attributes = Next->Attributes & PTY_ATTRIBUTE_MASK;
                }
            }

            if (CodePoint < 0x20 || CodePoint == 0x7F || (CodePoint >= 0xD800 && CodePoint <= 0xDFFF))
                CodePoint = L' ';

            PtyAppendCodePoint(Out, CodePoint);
            if (CodePoint >= PTY_FIXED_WIDTH_LIMIT)
                Reposition = TRUE;
        }

        Pty->RenderCursor.X = (!Reposition && X < View.X) ? X : -1;
        Pty->RenderCursor.Y = Y;
    }

    Cursor.X = Buffer->CursorPosition.X - Origin.X;
    Cursor.Y = Buffer->CursorPosition.Y - Origin.Y;
    if (Cursor.X >= 0 && Cursor.X < View.X && Cursor.Y >= 0 && Cursor.Y < View.Y &&
        (Pty->RenderCursor.X != Cursor.X || Pty->RenderCursor.Y != Cursor.Y))
    {
        PtyAppendCursorPosition(Out, Cursor.X, Cursor.Y);
        Pty->RenderCursor = Cursor;
    }

    CursorVisible = (BOOLEAN)(Buffer->CursorInfo.bVisible && !Buffer->ForceCursorOff);
    if (CursorVisible != Pty->CursorVisible)
    {
        PtyAppendString(Out, CursorVisible ? "\x1b[?25h" : "\x1b[?25l");
        Pty->CursorVisible = CursorVisible;
    }

    if (InterlockedExchange(&Pty->TitleChanged, 0))
    {
        ULONG Index;

        PtyAppendString(Out, "\x1b]0;");
        for (Index = 0; Index < Console->Title.Length / sizeof(WCHAR); Index++)
        {
            WCHAR Char = Console->Title.Buffer[Index];

            if (Char >= 0x20 && Char != 0x7F && (Char < 0xD800 || Char > 0xDFFF))
                PtyAppendCodePoint(Out, Char);
        }
        PtyAppend(Out, "\a", 1);
    }

    if (InterlockedExchange(&Pty->Bell, 0))
        PtyAppend(Out, "\a", 1);
}

static
NTSTATUS
PtyWaitIo(
    _In_ PCONSRV_PTY Pty,
    _In_ HANDLE File,
    _In_ HANDLE IoEvent,
    _In_ NTSTATUS Status,
    _Inout_ PIO_STATUS_BLOCK IoStatusBlock)
{
    HANDLE Handles[2];
    IO_STATUS_BLOCK CancelStatusBlock;

    if (Status != STATUS_PENDING)
        return Status;

    Handles[0] = IoEvent;
    Handles[1] = Pty->CloseEvent;
    Status = NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, NULL);
    if (Status != STATUS_WAIT_0)
    {
        NtCancelIoFile(File, &CancelStatusBlock);
        NtWaitForSingleObject(IoEvent, FALSE, NULL);
        return STATUS_CANCELLED;
    }

    return IoStatusBlock->Status;
}

static
ULONG
NTAPI
PtyOutputThread(
    _In_ PVOID Parameter)
{
    PCONSRV_PTY Pty = Parameter;
    PTY_BUFFER Out = { NULL, 0, 0 };
    HANDLE Handles[2];
    NTSTATUS Status;

    Handles[0] = Pty->OutputEvent;
    Handles[1] = Pty->CloseEvent;

    for (;;)
    {
        IO_STATUS_BLOCK IoStatusBlock;
        ULONG Written = 0;

        Status = NtWaitForMultipleObjects(2, Handles, WaitAny, FALSE, NULL);
        if (Status != STATUS_WAIT_0)
            break;

        if (!ConDrvValidateConsoleUnsafe((PCONSOLE)Pty->Console, CONSOLE_RUNNING, TRUE))
            break;

        Out.Length = 0;
        PtyRender(Pty, &Out);
        LeaveCriticalSection(&Pty->Console->Lock);

        while (Written < Out.Length)
        {
            Status = NtWriteFile(Pty->Output,
                                 Pty->OutputIoEvent,
                                 NULL,
                                 NULL,
                                 &IoStatusBlock,
                                 Out.Data + Written,
                                 Out.Length - Written,
                                 NULL,
                                 NULL);
            Status = PtyWaitIo(Pty, Pty->Output, Pty->OutputIoEvent, Status, &IoStatusBlock);
            if (!NT_SUCCESS(Status))
                break;
            Written += (ULONG)IoStatusBlock.Information;
        }

        if (Written < Out.Length)
            break;
    }

    if (Out.Data)
        ConsoleFreeHeap(Out.Data);
    return 0;
}

static
VOID
PtyQueueKey(
    _In_ PCONSRV_PTY Pty,
    _In_ WORD VirtualKey,
    _In_ WCHAR Char,
    _In_ DWORD ControlKeyState)
{
    INPUT_RECORD Record;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.EventType = KEY_EVENT;
    Record.Event.KeyEvent.wRepeatCount = 1;
    Record.Event.KeyEvent.wVirtualKeyCode = VirtualKey;
    Record.Event.KeyEvent.uChar.UnicodeChar = Char;
    Record.Event.KeyEvent.dwControlKeyState = ControlKeyState;

    Record.Event.KeyEvent.bKeyDown = TRUE;
    ConioProcessInputEvent(Pty->Console, &Record);
    Record.Event.KeyEvent.bKeyDown = FALSE;
    ConioProcessInputEvent(Pty->Console, &Record);
}

static
DWORD
PtyModifierState(
    _In_ ULONG Modifier)
{
    DWORD State = 0;

    if (Modifier < 2)
        return 0;

    Modifier--;
    if (Modifier & 1) State |= SHIFT_PRESSED;
    if (Modifier & 2) State |= LEFT_ALT_PRESSED;
    if (Modifier & 4) State |= LEFT_CTRL_PRESSED;
    return State;
}

static
VOID
PtyQueueCodePoint(
    _In_ PCONSRV_PTY Pty,
    _In_ ULONG CodePoint,
    _In_ DWORD ControlKeyState)
{
    WORD VirtualKey = 0;

    if (CodePoint >= 0x10000)
    {
        CodePoint -= 0x10000;
        PtyQueueKey(Pty, 0, (WCHAR)(0xD800 + (CodePoint >> 10)), ControlKeyState);
        PtyQueueKey(Pty, 0, (WCHAR)(0xDC00 + (CodePoint & 0x3FF)), ControlKeyState);
        return;
    }

    if (CodePoint == L'\r')
    {
        VirtualKey = VK_RETURN;
    }
    else if (CodePoint == L'\t')
    {
        VirtualKey = VK_TAB;
    }
    else if (CodePoint == 0x7F || CodePoint == L'\b')
    {
        VirtualKey = VK_BACK;
        CodePoint = L'\b';
    }
    else if (CodePoint == 0x1B)
    {
        VirtualKey = VK_ESCAPE;
    }
    else if (CodePoint == 0)
    {
        VirtualKey = VK_SPACE;
        ControlKeyState |= LEFT_CTRL_PRESSED;
    }
    else if (CodePoint < 0x20)
    {
        VirtualKey = (WORD)(L'A' + CodePoint - 1);
        ControlKeyState |= LEFT_CTRL_PRESSED;
    }
    else if (CodePoint == L' ')
    {
        VirtualKey = VK_SPACE;
    }
    else if (CodePoint >= L'a' && CodePoint <= L'z')
    {
        VirtualKey = (WORD)(CodePoint - L'a' + L'A');
    }
    else if (CodePoint >= L'A' && CodePoint <= L'Z')
    {
        VirtualKey = (WORD)CodePoint;
        ControlKeyState |= SHIFT_PRESSED;
    }
    else if (CodePoint >= L'0' && CodePoint <= L'9')
    {
        VirtualKey = (WORD)CodePoint;
    }

    PtyQueueKey(Pty, VirtualKey, (WCHAR)CodePoint, ControlKeyState);
}

static
VOID
PtyDispatchCsi(
    _In_ PCONSRV_PTY Pty,
    _In_ PPTY_INPUT_PARSER Parser,
    _In_ UCHAR Final)
{
    ULONG First = Parser->ParameterCount > 0 ? Parser->Parameters[0] : 0;
    ULONG Second = Parser->ParameterCount > 1 ? Parser->Parameters[1] : 0;
    DWORD State = PtyModifierState(Second);
    WORD VirtualKey = 0;

    switch (Final)
    {
        case 'A': VirtualKey = VK_UP; break;
        case 'B': VirtualKey = VK_DOWN; break;
        case 'C': VirtualKey = VK_RIGHT; break;
        case 'D': VirtualKey = VK_LEFT; break;
        case 'H': VirtualKey = VK_HOME; break;
        case 'F': VirtualKey = VK_END; break;
        case 'P': VirtualKey = VK_F1; break;
        case 'Q': VirtualKey = VK_F2; break;
        case 'S': VirtualKey = VK_F4; break;
        case 'Z':
            PtyQueueKey(Pty, VK_TAB, L'\t', SHIFT_PRESSED);
            return;

        case '~':
            switch (First)
            {
                case 1: case 7: VirtualKey = VK_HOME; break;
                case 2: VirtualKey = VK_INSERT; break;
                case 3: VirtualKey = VK_DELETE; break;
                case 4: case 8: VirtualKey = VK_END; break;
                case 5: VirtualKey = VK_PRIOR; break;
                case 6: VirtualKey = VK_NEXT; break;
                case 11: VirtualKey = VK_F1; break;
                case 12: VirtualKey = VK_F2; break;
                case 13: VirtualKey = VK_F3; break;
                case 14: VirtualKey = VK_F4; break;
                case 15: VirtualKey = VK_F5; break;
                case 17: VirtualKey = VK_F6; break;
                case 18: VirtualKey = VK_F7; break;
                case 19: VirtualKey = VK_F8; break;
                case 20: VirtualKey = VK_F9; break;
                case 21: VirtualKey = VK_F10; break;
                case 23: VirtualKey = VK_F11; break;
                case 24: VirtualKey = VK_F12; break;
            }
            break;

        case '_':
        {
            INPUT_RECORD Record;

            RtlZeroMemory(&Record, sizeof(Record));
            Record.EventType = KEY_EVENT;
            Record.Event.KeyEvent.wVirtualKeyCode = (WORD)First;
            Record.Event.KeyEvent.wVirtualScanCode = (WORD)Second;
            Record.Event.KeyEvent.uChar.UnicodeChar =
                (WCHAR)(Parser->ParameterCount > 2 ? Parser->Parameters[2] : 0);
            Record.Event.KeyEvent.bKeyDown =
                (Parser->ParameterCount > 3 ? Parser->Parameters[3] : 0) != 0;
            Record.Event.KeyEvent.dwControlKeyState =
                Parser->ParameterCount > 4 ? Parser->Parameters[4] : 0;
            Record.Event.KeyEvent.wRepeatCount =
                (WORD)(Parser->ParameterCount > 5 && Parser->Parameters[5] ? Parser->Parameters[5] : 1);
            ConioProcessInputEvent(Pty->Console, &Record);
            return;
        }

        case 'I':
        case 'O':
        {
            INPUT_RECORD Record;

            RtlZeroMemory(&Record, sizeof(Record));
            Record.EventType = FOCUS_EVENT;
            Record.Event.FocusEvent.bSetFocus = (Final == 'I');
            ConioProcessInputEvent(Pty->Console, &Record);
            return;
        }
    }

    if (VirtualKey)
        PtyQueueKey(Pty, VirtualKey, 0, State | ENHANCED_KEY);
}

static
VOID
PtyDispatchSs3(
    _In_ PCONSRV_PTY Pty,
    _In_ UCHAR Final)
{
    WORD VirtualKey = 0;

    switch (Final)
    {
        case 'A': VirtualKey = VK_UP; break;
        case 'B': VirtualKey = VK_DOWN; break;
        case 'C': VirtualKey = VK_RIGHT; break;
        case 'D': VirtualKey = VK_LEFT; break;
        case 'H': VirtualKey = VK_HOME; break;
        case 'F': VirtualKey = VK_END; break;
        case 'P': VirtualKey = VK_F1; break;
        case 'Q': VirtualKey = VK_F2; break;
        case 'R': VirtualKey = VK_F3; break;
        case 'S': VirtualKey = VK_F4; break;
    }

    if (VirtualKey)
        PtyQueueKey(Pty, VirtualKey, 0, ENHANCED_KEY);
}

static
VOID
PtyParseInput(
    _In_ PCONSRV_PTY Pty,
    _Inout_ PPTY_INPUT_PARSER Parser,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        UCHAR Byte = Data[Index];

        switch (Parser->State)
        {
            case PtyInputEscape:
                if (Byte == '[')
                {
                    Parser->State = PtyInputCsi;
                    Parser->ParameterCount = 0;
                    Parser->ParameterPresent = FALSE;
                    RtlZeroMemory(Parser->Parameters, sizeof(Parser->Parameters));
                    continue;
                }
                if (Byte == 'O')
                {
                    Parser->State = PtyInputSs3;
                    continue;
                }
                Parser->State = PtyInputGround;
                if (Byte == 0x1B)
                {
                    PtyQueueCodePoint(Pty, 0x1B, 0);
                    Parser->State = PtyInputEscape;
                    continue;
                }
                if (Byte < 0x80)
                {
                    PtyQueueCodePoint(Pty, Byte, LEFT_ALT_PRESSED);
                    continue;
                }
                PtyQueueCodePoint(Pty, 0x1B, 0);
                break;

            case PtyInputCsi:
                if (Byte >= '0' && Byte <= '9')
                {
                    if (!Parser->ParameterPresent)
                    {
                        if (Parser->ParameterCount < PTY_INPUT_PARAMETERS)
                            Parser->ParameterCount++;
                        Parser->ParameterPresent = TRUE;
                    }
                    Parser->Parameters[Parser->ParameterCount - 1] =
                        Parser->Parameters[Parser->ParameterCount - 1] * 10 + (Byte - '0');
                    continue;
                }
                if (Byte == ';')
                {
                    if (!Parser->ParameterPresent && Parser->ParameterCount < PTY_INPUT_PARAMETERS)
                        Parser->ParameterCount++;
                    Parser->ParameterPresent = FALSE;
                    continue;
                }
                if (Byte >= 0x20 && Byte <= 0x3F)
                    continue;
                Parser->State = PtyInputGround;
                if (Byte >= 0x40 && Byte <= 0x7E)
                    PtyDispatchCsi(Pty, Parser, Byte);
                continue;

            case PtyInputSs3:
                Parser->State = PtyInputGround;
                PtyDispatchSs3(Pty, Byte);
                continue;

            default:
                break;
        }

        if (Parser->Continuation)
        {
            if ((Byte & 0xC0) == 0x80)
            {
                Parser->CodePoint = (Parser->CodePoint << 6) | (Byte & 0x3F);
                if (--Parser->Continuation == 0)
                    PtyQueueCodePoint(Pty, Parser->CodePoint, 0);
                continue;
            }
            Parser->Continuation = 0;
        }

        if (Byte == 0x1B)
        {
            Parser->State = PtyInputEscape;
        }
        else if (Byte < 0x80)
        {
            PtyQueueCodePoint(Pty, Byte, 0);
        }
        else if ((Byte & 0xE0) == 0xC0)
        {
            Parser->CodePoint = Byte & 0x1F;
            Parser->Continuation = 1;
        }
        else if ((Byte & 0xF0) == 0xE0)
        {
            Parser->CodePoint = Byte & 0x0F;
            Parser->Continuation = 2;
        }
        else if ((Byte & 0xF8) == 0xF0)
        {
            Parser->CodePoint = Byte & 0x07;
            Parser->Continuation = 3;
        }
    }

    if (Parser->State == PtyInputEscape)
    {
        Parser->State = PtyInputGround;
        PtyQueueCodePoint(Pty, 0x1B, 0);
    }
}

static
ULONG
NTAPI
PtyInputThread(
    _In_ PVOID Parameter)
{
    PCONSRV_PTY Pty = Parameter;
    PTY_INPUT_PARSER Parser;
    UCHAR Data[256];
    NTSTATUS Status;

    RtlZeroMemory(&Parser, sizeof(Parser));

    for (;;)
    {
        IO_STATUS_BLOCK IoStatusBlock;

        Status = NtReadFile(Pty->Input,
                            Pty->InputIoEvent,
                            NULL,
                            NULL,
                            &IoStatusBlock,
                            Data,
                            sizeof(Data),
                            NULL,
                            NULL);
        Status = PtyWaitIo(Pty, Pty->Input, Pty->InputIoEvent, Status, &IoStatusBlock);
        if (!NT_SUCCESS(Status) || IoStatusBlock.Information == 0)
            break;

        if (!ConDrvValidateConsoleUnsafe((PCONSOLE)Pty->Console, CONSOLE_RUNNING, TRUE))
            break;

        PtyParseInput(Pty, &Parser, Data, (ULONG)IoStatusBlock.Information);
        LeaveCriticalSection(&Pty->Console->Lock);
    }

    return 0;
}

static
PCONSRV_PTY
PtyFromFrontEnd(
    _In_ PFRONTEND This)
{
    return This->Console->PseudoConsole;
}

static
VOID
PtySignal(
    _In_ PCONSRV_PTY Pty)
{
    if (Pty->OutputEvent)
        NtSetEvent(Pty->OutputEvent, NULL);
}

static VOID NTAPI
PtyDeinitFrontEnd(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->DeinitFrontEnd(This);
    This->Console->PseudoConsole = NULL;

    if (Pty->Console)
    {
        if (Pty->Shadow) ConsoleFreeHeap(Pty->Shadow);
        ConsoleFreeHeap(Pty);
    }
}

static VOID NTAPI
PtyDrawRegion(IN OUT PFRONTEND This,
              SMALL_RECT* Region)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->DrawRegion(This, Region);
    PtySignal(Pty);
}

static VOID NTAPI
PtyWriteStream(IN OUT PFRONTEND This,
               SMALL_RECT* Region,
               SHORT CursorStartX,
               SHORT CursorStartY,
               UINT ScrolledLines,
               PWCHAR Buffer,
               UINT Length)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->WriteStream(This, Region, CursorStartX, CursorStartY, ScrolledLines, Buffer, Length);
    InterlockedExchangeAdd(&Pty->ScrolledLines, (LONG)ScrolledLines);
    PtySignal(Pty);
}

static VOID NTAPI
PtyRingBell(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    InterlockedExchange(&Pty->Bell, 1);
    PtySignal(Pty);
}

static BOOL NTAPI
PtySetCursorInfo(IN OUT PFRONTEND This,
                 PCONSOLE_SCREEN_BUFFER ScreenBuffer)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);
    BOOL Result = Pty->InnerVtbl->SetCursorInfo(This, ScreenBuffer);

    PtySignal(Pty);
    return Result;
}

static BOOL NTAPI
PtySetScreenInfo(IN OUT PFRONTEND This,
                 PCONSOLE_SCREEN_BUFFER ScreenBuffer,
                 SHORT OldCursorX,
                 SHORT OldCursorY)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);
    BOOL Result = Pty->InnerVtbl->SetScreenInfo(This, ScreenBuffer, OldCursorX, OldCursorY);

    PtySignal(Pty);
    return Result;
}

static VOID NTAPI
PtyResizeTerminal(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->ResizeTerminal(This);
    PtySignal(Pty);
}

static VOID NTAPI
PtySetActiveScreenBuffer(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->SetActiveScreenBuffer(This);
    PtySignal(Pty);
}

static VOID NTAPI
PtyRefreshInternalInfo(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    if (!IsListEmpty(&This->Console->ProcessList))
        Pty->InnerVtbl->RefreshInternalInfo(This);
}

static VOID NTAPI
PtyChangeTitle(IN OUT PFRONTEND This)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    Pty->InnerVtbl->ChangeTitle(This);
    InterlockedExchange(&Pty->TitleChanged, 1);
    PtySignal(Pty);
}

static VOID NTAPI
PtyGetLargestConsoleWindowSize(IN OUT PFRONTEND This,
                               PCOORD pSize)
{
    PCONSRV_PTY Pty = PtyFromFrontEnd(This);

    if (pSize)
        *pSize = Pty->Size;
}

VOID
ConSrvPtyWrapFrontEnd(
    _Inout_ PFRONTEND FrontEnd,
    _In_ PVOID PseudoConsole)
{
    PCONSRV_PTY Pty = PseudoConsole;

    Pty->InnerVtbl = FrontEnd->Vtbl;
    Pty->Vtbl = *FrontEnd->Vtbl;
    Pty->Vtbl.DeinitFrontEnd = PtyDeinitFrontEnd;
    Pty->Vtbl.DrawRegion = PtyDrawRegion;
    Pty->Vtbl.WriteStream = PtyWriteStream;
    Pty->Vtbl.RingBell = PtyRingBell;
    Pty->Vtbl.SetCursorInfo = PtySetCursorInfo;
    Pty->Vtbl.SetScreenInfo = PtySetScreenInfo;
    Pty->Vtbl.ResizeTerminal = PtyResizeTerminal;
    Pty->Vtbl.SetActiveScreenBuffer = PtySetActiveScreenBuffer;
    Pty->Vtbl.RefreshInternalInfo = PtyRefreshInternalInfo;
    Pty->Vtbl.ChangeTitle = PtyChangeTitle;
    Pty->Vtbl.GetLargestConsoleWindowSize = PtyGetLargestConsoleWindowSize;
    FrontEnd->Vtbl = &Pty->Vtbl;
}

VOID
ConSrvPtyInitSupport(VOID)
{
    InitializeListHead(&PtyList);
    RtlInitializeCriticalSection(&PtyListLock);
}

static
NTSTATUS
PtyStartThread(
    _In_ PCONSRV_PTY Pty,
    _In_ PVOID Routine,
    _Out_ PHANDLE Thread)
{
    CLIENT_ID ClientId;
    NTSTATUS Status;

    Status = RtlCreateUserThread(NtCurrentProcess(),
                                 NULL,
                                 TRUE,
                                 0,
                                 0,
                                 0,
                                 Routine,
                                 Pty,
                                 Thread,
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        *Thread = NULL;
        return Status;
    }

    CsrAddStaticServerThread(*Thread, &ClientId, 0);
    return NtResumeThread(*Thread, NULL);
}

static
VOID
PtyStopThread(
    _Inout_ PHANDLE Thread)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Timeout;

    if (!*Thread)
        return;

    Timeout.QuadPart = -10 * 1000 * 100;
    while (NtWaitForSingleObject(*Thread, FALSE, &Timeout) == STATUS_TIMEOUT)
        NtCancelSynchronousIoFile(*Thread, NULL, &IoStatusBlock);

    NtClose(*Thread);
    *Thread = NULL;
}

static
VOID
PtyClose(
    _Inout_ PCONSRV_PTY Pty)
{
    PCONSRV_CONSOLE Console = Pty->Console;
    HANDLE OutputEvent = Pty->OutputEvent;

    NtSetEvent(Pty->CloseEvent, NULL);
    PtyStopThread(&Pty->InputThread);
    PtyStopThread(&Pty->OutputThread);

    if (ConDrvValidateConsoleUnsafe((PCONSOLE)Console, CONSOLE_RUNNING, TRUE))
    {
        Pty->OutputEvent = NULL;
        ConSrvConsoleProcessCloseEvent(Console);
        LeaveCriticalSection(&Console->Lock);
    }
    else
    {
        Pty->OutputEvent = NULL;
    }

    NtClose(Pty->Input);
    NtClose(Pty->Output);
    NtClose(Pty->InputIoEvent);
    NtClose(Pty->OutputIoEvent);
    NtClose(Pty->CloseEvent);
    NtClose(OutputEvent);
    Pty->Input = Pty->Output = NULL;

    ConSrvReleaseConsole(Console, FALSE);
}

static
PCONSRV_PTY
PtyLookup(
    _In_ HANDLE ConsoleHandle,
    _In_ HANDLE Owner,
    _In_ BOOLEAN Remove)
{
    PLIST_ENTRY Entry;
    PCONSRV_PTY Pty = NULL;

    for (Entry = PtyList.Flink; Entry != &PtyList; Entry = Entry->Flink)
    {
        PCONSRV_PTY Current = CONTAINING_RECORD(Entry, CONSRV_PTY, ListEntry);

        if (Current->Owner == Owner && (!ConsoleHandle || Current->ConsoleHandle == ConsoleHandle))
        {
            Pty = Current;
            break;
        }
    }

    if (Pty && Remove)
        RemoveEntryList(&Pty->ListEntry);

    return Pty;
}

VOID
ConSrvPtyDisconnectProcess(
    _In_ PCSR_PROCESS Process)
{
    PCONSRV_PTY Pty;

    for (;;)
    {
        RtlEnterCriticalSection(&PtyListLock);
        Pty = PtyLookup(NULL, Process->ClientId.UniqueProcess, TRUE);
        RtlLeaveCriticalSection(&PtyListLock);
        if (!Pty)
            break;

        PtyClose(Pty);
    }
}

static
NTSTATUS
PtyCreateEvent(
    _Out_ PHANDLE Event,
    _In_ EVENT_TYPE Type)
{
    return NtCreateEvent(Event, EVENT_ALL_ACCESS, NULL, Type, FALSE);
}

CON_API_NOCONSOLE(SrvCreatePseudoConsole,
                  CONSOLE_PSEUDOCONSOLE, PseudoConsoleRequest)
{
    static WCHAR EmptyString[] = L"";
    PCSR_PROCESS Process = CsrGetClientThread()->Process;
    CONSOLE_START_INFO StartInfo;
    CONSOLE_INIT_INFO InitInfo;
    PCONSRV_CONSOLE Console;
    HANDLE ConsoleHandle;
    PCONSRV_PTY Pty;
    NTSTATUS Status;

    if (PseudoConsoleRequest->Size.X <= 0 || PseudoConsoleRequest->Size.Y <= 0)
        return STATUS_INVALID_PARAMETER;

    if (!CsrValidateMessageBuffer(ApiMessage,
                                  (PVOID*)&PseudoConsoleRequest->Desktop,
                                  PseudoConsoleRequest->DesktopLength,
                                  sizeof(BYTE)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Pty = ConsoleAllocHeap(HEAP_ZERO_MEMORY, sizeof(*Pty));
    if (!Pty)
        return STATUS_NO_MEMORY;

    Pty->Size = PseudoConsoleRequest->Size;
    Pty->Flags = PseudoConsoleRequest->Flags;
    Pty->Owner = Process->ClientId.UniqueProcess;
    Pty->Reset = 1;

    Status = NtDuplicateObject(Process->ProcessHandle,
                               PseudoConsoleRequest->InputHandle,
                               NtCurrentProcess(),
                               &Pty->Input,
                               0,
                               0,
                               DUPLICATE_SAME_ACCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtDuplicateObject(Process->ProcessHandle,
                                   PseudoConsoleRequest->OutputHandle,
                                   NtCurrentProcess(),
                                   &Pty->Output,
                                   0,
                                   0,
                                   DUPLICATE_SAME_ACCESS);
    }
    if (NT_SUCCESS(Status)) Status = PtyCreateEvent(&Pty->CloseEvent, NotificationEvent);
    if (NT_SUCCESS(Status)) Status = PtyCreateEvent(&Pty->OutputEvent, SynchronizationEvent);
    if (NT_SUCCESS(Status)) Status = PtyCreateEvent(&Pty->InputIoEvent, SynchronizationEvent);
    if (NT_SUCCESS(Status)) Status = PtyCreateEvent(&Pty->OutputIoEvent, SynchronizationEvent);
    if (!NT_SUCCESS(Status))
        goto Failure;

    RtlZeroMemory(&StartInfo, sizeof(StartInfo));
    StartInfo.dwStartupFlags = STARTF_USECOUNTCHARS | STARTF_USESIZE | STARTF_USESHOWWINDOW;
    StartInfo.dwScreenBufferSize = Pty->Size;
    StartInfo.dwWindowSize = Pty->Size;
    StartInfo.wShowWindow = SW_HIDE;

    RtlZeroMemory(&InitInfo, sizeof(InitInfo));
    InitInfo.ConsoleStartInfo = &StartInfo;
    InitInfo.IsWindowVisible = TRUE;
    InitInfo.ConsoleTitle = EmptyString;
    InitInfo.AppName = EmptyString;
    InitInfo.CurDir = EmptyString;
    InitInfo.DesktopLength = PseudoConsoleRequest->DesktopLength;
    InitInfo.Desktop = PseudoConsoleRequest->Desktop;
    InitInfo.PseudoConsole = Pty;

    Status = ConSrvInitConsole(&ConsoleHandle, &Console, &InitInfo, Process);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Pty->Console = Console;
    Pty->ConsoleHandle = ConsoleHandle;
    Console->QuickEdit = TRUE;
    _InterlockedIncrement(&Console->ReferenceCount);

    Status = PtyStartThread(Pty, PtyOutputThread, &Pty->OutputThread);
    if (NT_SUCCESS(Status))
        Status = PtyStartThread(Pty, PtyInputThread, &Pty->InputThread);
    if (!NT_SUCCESS(Status))
    {
        PtyClose(Pty);
        return Status;
    }

    RtlEnterCriticalSection(&PtyListLock);
    InsertTailList(&PtyList, &Pty->ListEntry);
    RtlLeaveCriticalSection(&PtyListLock);

    NtSetEvent(Pty->OutputEvent, NULL);
    PseudoConsoleRequest->ConsoleHandle = ConsoleHandle;
    return STATUS_SUCCESS;

Failure:
    if (Pty->Input) NtClose(Pty->Input);
    if (Pty->Output) NtClose(Pty->Output);
    if (Pty->CloseEvent) NtClose(Pty->CloseEvent);
    if (Pty->OutputEvent) NtClose(Pty->OutputEvent);
    if (Pty->InputIoEvent) NtClose(Pty->InputIoEvent);
    if (Pty->OutputIoEvent) NtClose(Pty->OutputIoEvent);
    ConsoleFreeHeap(Pty);
    return Status;
}

CON_API_NOCONSOLE(SrvResizePseudoConsole,
                  CONSOLE_PSEUDOCONSOLE, PseudoConsoleRequest)
{
    PCSR_PROCESS Process = CsrGetClientThread()->Process;
    PCONSRV_CONSOLE Console;
    PCONSRV_PTY Pty;
    NTSTATUS Status = STATUS_INVALID_HANDLE;
    COORD Size = PseudoConsoleRequest->Size;

    if (Size.X <= 0 || Size.Y <= 0)
        return STATUS_INVALID_PARAMETER;

    RtlEnterCriticalSection(&PtyListLock);
    Pty = PtyLookup(PseudoConsoleRequest->ConsoleHandle, Process->ClientId.UniqueProcess, FALSE);
    if (Pty)
    {
        Console = Pty->Console;
        if (ConDrvValidateConsoleUnsafe((PCONSOLE)Console, CONSOLE_RUNNING, TRUE))
        {
            Status = STATUS_SUCCESS;
            if (GetType(Console->ActiveBuffer) == TEXTMODE_BUFFER)
            {
                PTEXTMODE_SCREEN_BUFFER Buffer = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
                SMALL_RECT Window;

                Window.Left = Window.Top = 0;
                Window.Right = min(Size.X, Buffer->ScreenBufferSize.X) - 1;
                Window.Bottom = min(Size.Y, Buffer->ScreenBufferSize.Y) - 1;
                Pty->Size.X = max(Size.X, Pty->Size.X);
                Pty->Size.Y = max(Size.Y, Pty->Size.Y);

                Status = ConDrvSetConsoleWindowInfo((PCONSOLE)Console, Buffer, TRUE, &Window);
                if (NT_SUCCESS(Status))
                    Status = ConDrvSetConsoleScreenBufferSize((PCONSOLE)Console, Buffer, &Size);
                if (NT_SUCCESS(Status))
                {
                    Window.Right = Size.X - 1;
                    Window.Bottom = Size.Y - 1;
                    Status = ConDrvSetConsoleWindowInfo((PCONSOLE)Console, Buffer, TRUE, &Window);
                }
            }

            if (NT_SUCCESS(Status))
                Pty->Size = Size;
            InterlockedExchange(&Pty->Reset, 1);
            PtySignal(Pty);
            LeaveCriticalSection(&Console->Lock);
        }
    }
    RtlLeaveCriticalSection(&PtyListLock);

    return Status;
}

CON_API_NOCONSOLE(SrvClosePseudoConsole,
                  CONSOLE_PSEUDOCONSOLE, PseudoConsoleRequest)
{
    PCSR_PROCESS Process = CsrGetClientThread()->Process;
    PCONSRV_PTY Pty;

    RtlEnterCriticalSection(&PtyListLock);
    Pty = PtyLookup(PseudoConsoleRequest->ConsoleHandle, Process->ClientId.UniqueProcess, TRUE);
    RtlLeaveCriticalSection(&PtyListLock);
    if (!Pty)
        return STATUS_INVALID_HANDLE;

    PtyClose(Pty);
    return STATUS_SUCCESS;
}
