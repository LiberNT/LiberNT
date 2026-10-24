/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Freestanding Linux runtime of the Wayland compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxrt.h"

#define LX_ARENA_SIZE       (1024 * 1024)
#define LX_BLOCK_HEADER     16
#define LX_SMALL_CLASSES    12
#define LX_SMALL_LIMIT      (16u << (LX_SMALL_CLASSES - 1))

typedef struct _LX_FREE_BLOCK
{
    struct _LX_FREE_BLOCK *Next;
} LX_FREE_BLOCK;

static LX_FREE_BLOCK *LxFreeLists[LX_SMALL_CLASSES];
static unsigned char *LxArena;
static size_t LxArenaLeft;

void *
memcpy(void *Destination, const void *Source, size_t Length)
{
    unsigned char *Out = Destination;
    const unsigned char *In = Source;

    while (Length-- != 0)
        *Out++ = *In++;
    return Destination;
}

void *
memmove(void *Destination, const void *Source, size_t Length)
{
    unsigned char *Out = Destination;
    const unsigned char *In = Source;

    if (Out == In || Length == 0)
        return Destination;
    if (Out < In || Out >= In + Length)
        return memcpy(Destination, Source, Length);
    while (Length-- != 0)
        Out[Length] = In[Length];
    return Destination;
}

void *
memset(void *Destination, int Value, size_t Length)
{
    unsigned char *Out = Destination;

    while (Length-- != 0)
        *Out++ = (unsigned char)Value;
    return Destination;
}

int
memcmp(const void *Left, const void *Right, size_t Length)
{
    const unsigned char *A = Left, *B = Right;

    for (; Length != 0; Length--, A++, B++)
    {
        if (*A != *B)
            return *A - *B;
    }
    return 0;
}

int
bcmp(const void *Left, const void *Right, size_t Length)
{
    return memcmp(Left, Right, Length);
}

size_t
strlen(const char *String)
{
    size_t Length = 0;

    while (String[Length] != '\0')
        Length++;
    return Length;
}

int
strcmp(const char *Left, const char *Right)
{
    while (*Left != '\0' && *Left == *Right)
    {
        Left++;
        Right++;
    }
    return (unsigned char)*Left - (unsigned char)*Right;
}

static
void *
LxMapAnonymous(size_t Size)
{
    long Result = LxSyscall(LX_NR_MMAP, 0, (long)Size, LX_PROT_READ | LX_PROT_WRITE,
                            LX_MAP_PRIVATE | LX_MAP_ANONYMOUS, -1, 0);

    if (Result < 0 && Result > -4096)
        return NULL;
    return (void *)Result;
}

static
unsigned
LxSizeClass(size_t Size)
{
    unsigned Class = 0;

    while ((16u << Class) < Size)
        Class++;
    return Class;
}

void *
LxAlloc(size_t Size)
{
    unsigned char *Block;
    size_t Total;
    unsigned Class;

    if (Size + LX_BLOCK_HEADER > LX_SMALL_LIMIT)
    {
        Total = (Size + LX_BLOCK_HEADER + 4095) & ~(size_t)4095;
        if (Total < Size)
            return NULL;
        Block = LxMapAnonymous(Total);
        if (Block == NULL)
            return NULL;
        *(size_t *)Block = Total;
        return Block + LX_BLOCK_HEADER;
    }

    Class = LxSizeClass(Size + LX_BLOCK_HEADER);
    if (LxFreeLists[Class] != NULL)
    {
        Block = (unsigned char *)LxFreeLists[Class];
        LxFreeLists[Class] = LxFreeLists[Class]->Next;
    }
    else
    {
        Total = 16u << Class;
        if (LxArenaLeft < Total)
        {
            LxArena = LxMapAnonymous(LX_ARENA_SIZE);
            if (LxArena == NULL)
            {
                LxArenaLeft = 0;
                return NULL;
            }
            LxArenaLeft = LX_ARENA_SIZE;
        }
        Block = LxArena;
        LxArena += Total;
        LxArenaLeft -= Total;
    }
    *(size_t *)Block = Class;
    return Block + LX_BLOCK_HEADER;
}

void *
LxAllocZero(size_t Size)
{
    void *Block = LxAlloc(Size);

    if (Block != NULL)
        memset(Block, 0, Size);
    return Block;
}

static
size_t
LxBlockCapacity(void *Block)
{
    size_t Tag = *(size_t *)((unsigned char *)Block - LX_BLOCK_HEADER);

    if (Tag < LX_SMALL_CLASSES)
        return (16u << Tag) - LX_BLOCK_HEADER;
    return Tag - LX_BLOCK_HEADER;
}

void
LxFree(void *Block)
{
    unsigned char *Base;
    size_t Tag;

    if (Block == NULL)
        return;
    Base = (unsigned char *)Block - LX_BLOCK_HEADER;
    Tag = *(size_t *)Base;
    if (Tag < LX_SMALL_CLASSES)
    {
        ((LX_FREE_BLOCK *)Base)->Next = LxFreeLists[Tag];
        LxFreeLists[Tag] = (LX_FREE_BLOCK *)Base;
    }
    else
    {
        LxSyscall(LX_NR_MUNMAP, (long)Base, (long)Tag, 0, 0, 0, 0);
    }
}

void *
LxRealloc(void *Block, size_t Size)
{
    void *Copy;
    size_t Capacity;

    if (Block == NULL)
        return LxAlloc(Size);
    Capacity = LxBlockCapacity(Block);
    if (Size <= Capacity)
        return Block;
    Copy = LxAlloc(Size);
    if (Copy == NULL)
        return NULL;
    memcpy(Copy, Block, Capacity);
    LxFree(Block);
    return Copy;
}

char *
LxStrDup(const char *String)
{
    size_t Length = strlen(String) + 1;
    char *Copy = LxAlloc(Length);

    if (Copy != NULL)
        memcpy(Copy, String, Length);
    return Copy;
}

typedef struct _LX_LOG_BUFFER
{
    char Data[1024];
    size_t Length;
} LX_LOG_BUFFER;

static
void
LxLogChar(LX_LOG_BUFFER *Buffer, char Value)
{
    if (Buffer->Length < sizeof(Buffer->Data) - 1)
        Buffer->Data[Buffer->Length++] = Value;
}

static
void
LxLogNumber(LX_LOG_BUFFER *Buffer, uint64_t Value, unsigned Base, bool Negative)
{
    char Digits[24];
    unsigned Count = 0;

    if (Negative)
        LxLogChar(Buffer, '-');
    do
    {
        Digits[Count++] = "0123456789abcdef"[Value % Base];
        Value /= Base;
    } while (Value != 0);
    while (Count != 0)
        LxLogChar(Buffer, Digits[--Count]);
}

void
LxLog(const char *Format, ...)
{
    static const char Prefix[] = "lxwayland: ";
    LX_LOG_BUFFER Buffer;
    va_list Args;
    const char *Text;
    long long Signed;

    Buffer.Length = 0;
    for (Text = Prefix; *Text != '\0'; Text++)
        LxLogChar(&Buffer, *Text);
    va_start(Args, Format);
    for (; *Format != '\0'; Format++)
    {
        if (*Format != '%')
        {
            LxLogChar(&Buffer, *Format);
            continue;
        }
        switch (*++Format)
        {
            case 'd':
                Signed = va_arg(Args, int);
                LxLogNumber(&Buffer, Signed < 0 ? (uint64_t)-Signed : (uint64_t)Signed, 10, Signed < 0);
                break;
            case 'u':
                LxLogNumber(&Buffer, va_arg(Args, unsigned), 10, false);
                break;
            case 'x':
                LxLogNumber(&Buffer, va_arg(Args, unsigned), 16, false);
                break;
            case 's':
                Text = va_arg(Args, const char *);
                for (Text = Text != NULL ? Text : "(null)"; *Text != '\0'; Text++)
                    LxLogChar(&Buffer, *Text);
                break;
            case '%':
                LxLogChar(&Buffer, '%');
                break;
            case '\0':
                Format--;
                break;
            default:
                LxLogChar(&Buffer, '?');
                break;
        }
    }
    va_end(Args);
    LxLogChar(&Buffer, '\n');
    LxSyscall(LX_NR_WRITE, 2, (long)Buffer.Data, (long)Buffer.Length, 0, 0, 0);
}

uint64_t
LxMonotonicMs(void)
{
    LX_TIMESPEC Now;

    LxSyscall(LX_NR_CLOCK_GETTIME, LX_CLOCK_MONOTONIC, (long)&Now, 0, 0, 0, 0);
    return (uint64_t)Now.Seconds * 1000 + (uint64_t)Now.Nanoseconds / 1000000;
}

void
LxIgnoreSigpipe(void)
{
    struct
    {
        uintptr_t Handler;
        unsigned long Flags;
        uintptr_t Words[2];
    } Action;

    memset(&Action, 0, sizeof(Action));
    Action.Handler = 1;
    LxSyscall(LX_NR_RT_SIGACTION, LX_SIGPIPE, (long)&Action, 0, 8, 0, 0);
}

void
LxExit(int Code)
{
    for (;;)
        LxSyscall(LX_NR_EXIT_GROUP, Code, 0, 0, 0, 0, 0);
}

void
LxStart(long *Stack)
{
    (void)Stack;
    LxExit(LxMain());
}
