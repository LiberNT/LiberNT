/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM floating point control and status words over the VFP FPSCR
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <float.h>

#define FPSCR_IOC   0x00000001
#define FPSCR_DZC   0x00000002
#define FPSCR_OFC   0x00000004
#define FPSCR_UFC   0x00000008
#define FPSCR_IXC   0x00000010
#define FPSCR_IDC   0x00000080
#define FPSCR_FLAGS (FPSCR_IOC | FPSCR_DZC | FPSCR_OFC | FPSCR_UFC | FPSCR_IXC | FPSCR_IDC)
#define FPSCR_IOE   0x00000100
#define FPSCR_DZE   0x00000200
#define FPSCR_OFE   0x00000400
#define FPSCR_UFE   0x00000800
#define FPSCR_IXE   0x00001000
#define FPSCR_IDE   0x00008000
#define FPSCR_RMODE_SHIFT 22
#define FPSCR_RMODE 0x00C00000
#define FPSCR_FZ    0x01000000

static const struct
{
    unsigned int Fpscr;
    unsigned int Word;
} FpExceptions[] =
{
    { FPSCR_IOC, _SW_INVALID },
    { FPSCR_DZC, _SW_ZERODIVIDE },
    { FPSCR_OFC, _SW_OVERFLOW },
    { FPSCR_UFC, _SW_UNDERFLOW },
    { FPSCR_IXC, _SW_INEXACT },
    { FPSCR_IDC, _SW_DENORMAL },
};

/* Each FPSCR trap enable sits eight bits above its cumulative flag. */
#define FPSCR_ENABLE_OF(Flag) ((Flag) << 8)

static unsigned int FpReadFpscr(void)
{
    unsigned int Fpscr;

    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(Fpscr));
    return Fpscr;
}

static void FpWriteFpscr(unsigned int Fpscr)
{
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(Fpscr) : "memory");
}

static unsigned int FpStatusWord(unsigned int Fpscr)
{
    unsigned int Word = 0;
    unsigned int i;

    for (i = 0; i < sizeof(FpExceptions) / sizeof(FpExceptions[0]); i++)
    {
        if (Fpscr & FpExceptions[i].Fpscr)
            Word |= FpExceptions[i].Word;
    }
    return Word;
}

static unsigned int FpControlWord(unsigned int Fpscr)
{
    static const unsigned int Rounding[] = { _RC_NEAR, _RC_UP, _RC_DOWN, _RC_CHOP };
    unsigned int Word = Rounding[(Fpscr & FPSCR_RMODE) >> FPSCR_RMODE_SHIFT];
    unsigned int i;

    for (i = 0; i < sizeof(FpExceptions) / sizeof(FpExceptions[0]); i++)
    {
        if (!(Fpscr & FPSCR_ENABLE_OF(FpExceptions[i].Fpscr)))
            Word |= FpExceptions[i].Word;
    }
    if (Fpscr & FPSCR_FZ)
        Word |= _DN_FLUSH;
    return Word;
}

static unsigned int FpApplyControlWord(unsigned int Fpscr, unsigned int Word)
{
    unsigned int Mode;
    unsigned int i;

    for (i = 0; i < sizeof(FpExceptions) / sizeof(FpExceptions[0]); i++)
    {
        if (Word & FpExceptions[i].Word)
            Fpscr &= ~FPSCR_ENABLE_OF(FpExceptions[i].Fpscr);
        else
            Fpscr |= FPSCR_ENABLE_OF(FpExceptions[i].Fpscr);
    }

    switch (Word & _MCW_RC)
    {
        case _RC_UP:   Mode = 1; break;
        case _RC_DOWN: Mode = 2; break;
        case _RC_CHOP: Mode = 3; break;
        default:       Mode = 0; break;
    }
    Fpscr = (Fpscr & ~FPSCR_RMODE) | (Mode << FPSCR_RMODE_SHIFT);

    if ((Word & _MCW_DN) == _DN_FLUSH)
        Fpscr |= FPSCR_FZ;
    else
        Fpscr &= ~FPSCR_FZ;

    return Fpscr;
}

unsigned int __cdecl _statusfp(void)
{
    return FpStatusWord(FpReadFpscr());
}

unsigned int __cdecl _clearfp(void)
{
    unsigned int Fpscr = FpReadFpscr();

    FpWriteFpscr(Fpscr & ~FPSCR_FLAGS);
    return FpStatusWord(Fpscr);
}

unsigned int __cdecl _controlfp(unsigned int NewValue, unsigned int Mask)
{
    unsigned int Fpscr = FpReadFpscr();
    unsigned int Word = FpControlWord(Fpscr);

    Mask &= (_MCW_EM & ~_EM_DENORMAL) | _MCW_RC | _MCW_DN;
    Word = (Word & ~Mask) | (NewValue & Mask);
    FpWriteFpscr(FpApplyControlWord(Fpscr, Word));
    return Word;
}

void __cdecl _fpreset(void)
{
    /* All exceptions masked, round to nearest, denormals kept, flags cleared. */
    FpWriteFpscr(0);
}
