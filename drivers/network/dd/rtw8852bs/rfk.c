/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RF calibration: RC, DAC/ADC DC offset and receiver DC offset
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"
#include "phyreg.h"

#define NDEBUG
#include <debug.h>

#define RTW_RFK_WRF                     0
#define RTW_RFK_WM                      1
#define RTW_RFK_WS                      2
#define RTW_RFK_WC                      3
#define RTW_RFK_DELAY                   4

typedef struct _RTW_RFK_ENTRY
{
    UCHAR Flag;
    UCHAR Path;
    ULONG Address;
    ULONG Mask;
    ULONG Data;
} RTW_RFK_ENTRY;

#define WM(a, m, d) { RTW_RFK_WM, 0, (a), (m), (d) }
#define WRF(p, a, m, d) { RTW_RFK_WRF, (p), (a), (m), (d) }
#define DLY(d) { RTW_RFK_DELAY, 0, 0, 0, (d) }

static const RTW_RFK_ENTRY RtwAfeInit[] =
{
    WM(0xC0D4, 0xffffffff, 0x4486888c), WM(0xC0D8, 0xffffffff, 0xc6ba10e0),
    WM(0xC0DC, 0xffffffff, 0x30c52868), WM(0xC0E0, 0xffffffff, 0x05008128),
    WM(0xC0E4, 0xffffffff, 0x0000272b), WM(0xC1D4, 0xffffffff, 0x4486888c),
    WM(0xC1D8, 0xffffffff, 0xc6ba10e0), WM(0xC1DC, 0xffffffff, 0x30c52868),
    WM(0xC1E0, 0xffffffff, 0x05008128), WM(0xC1E4, 0xffffffff, 0x0000272b),
};

static const RTW_RFK_ENTRY RtwCheckDadcEnB[] =
{
    WM(0x032C, RTW_BIT(30), 0x0), WM(0x030C, 0x0f000000, 0xf), WM(0x030C, 0x0f000000, 0x3),
    WM(0x032C, RTW_BIT(16), 0x0), WM(0x32DC, RTW_BIT(0), 0x1), WM(0x32E8, RTW_BIT(2), 0x1),
    WRF(1, 0x8f, RTW_BIT(13), 0x1),
};

static const RTW_RFK_ENTRY RtwCheckDadcDisB[] =
{
    WM(0x32DC, RTW_BIT(0), 0x0), WM(0x32E8, RTW_BIT(2), 0x0), WRF(1, 0x8f, RTW_BIT(13), 0x0),
    WM(0x032C, RTW_BIT(16), 0x1),
};

static const RTW_RFK_ENTRY RtwCheckAddcB[] =
{
    WM(0x20F4, RTW_BIT(24), 0x0), WM(0x20F8, 0x80000000, 0x1), WM(0x20F0, 0xff0000, 0x1),
    WM(0x20F0, 0xf00, 0x2), WM(0x20F0, 0xf, 0x0), WM(0x20F0, 0xc0, 0x3),
};

static const RTW_RFK_ENTRY RtwDackS0Part1[] =
{
    WM(0x12A0, RTW_BIT(15), 0x1), WM(0x12A0, 0x00007000, 0x3), WM(0x12B8, RTW_BIT(30), 0x1),
    WM(0x030C, RTW_BIT(28), 0x1), WM(0x032C, 0x80000000, 0x0), WM(0xC0D8, RTW_BIT(16), 0x1),
    WM(0xC0DC, 0x0c000000, 0x3), WM(0xC004, RTW_BIT(30), 0x0), WM(0xC024, RTW_BIT(30), 0x0),
    WM(0xC004, 0x3ff00000, 0x30), WM(0xC004, 0xc0000000, 0x0), WM(0xC004, RTW_BIT(17), 0x1),
    WM(0xC024, RTW_BIT(17), 0x1), WM(0xC00C, RTW_BIT(2), 0x0), WM(0xC02C, RTW_BIT(2), 0x0),
    WM(0xC004, RTW_BIT(0), 0x1), WM(0xC024, RTW_BIT(0), 0x1), DLY(1),
};

static const RTW_RFK_ENTRY RtwDackS0Part2[] =
{
    WM(0xC0DC, 0x0c000000, 0x0), WM(0xC00C, RTW_BIT(2), 0x1), WM(0xC02C, RTW_BIT(2), 0x1),
};

static const RTW_RFK_ENTRY RtwDackS0Part3[] =
{
    WM(0xC004, RTW_BIT(0), 0x0), WM(0xC024, RTW_BIT(0), 0x0), WM(0xC0D8, RTW_BIT(16), 0x0),
    WM(0x12A0, RTW_BIT(15), 0x0), WM(0x12A0, 0x00007000, 0x7),
};

static const RTW_RFK_ENTRY RtwDackS1Part1[] =
{
    WM(0x32A0, RTW_BIT(15), 0x1), WM(0x32A0, 0x7000, 0x3), WM(0x32B8, RTW_BIT(30), 0x1),
    WM(0x030C, RTW_BIT(28), 0x1), WM(0x032C, 0x80000000, 0x0), WM(0xC1D8, RTW_BIT(16), 0x1),
    WM(0xC1DC, 0x0c000000, 0x3), WM(0xC104, RTW_BIT(30), 0x0), WM(0xC124, RTW_BIT(30), 0x0),
    WM(0xC104, 0x3ff00000, 0x30), WM(0xC104, 0xc0000000, 0x0), WM(0xC104, RTW_BIT(17), 0x1),
    WM(0xC124, RTW_BIT(17), 0x1), WM(0xC10C, RTW_BIT(2), 0x0), WM(0xC12C, RTW_BIT(2), 0x0),
    WM(0xC104, RTW_BIT(0), 0x1), WM(0xC124, RTW_BIT(0), 0x1), DLY(1),
};

static const RTW_RFK_ENTRY RtwDackS1Part2[] =
{
    WM(0xC1DC, 0x0c000000, 0x0), WM(0xC10C, RTW_BIT(2), 0x1), WM(0xC12C, RTW_BIT(2), 0x1), DLY(1),
};

static const RTW_RFK_ENTRY RtwDackS1Part3[] =
{
    WM(0xC104, RTW_BIT(0), 0x0), WM(0xC124, RTW_BIT(0), 0x0), WM(0xC1D8, RTW_BIT(16), 0x0),
    WM(0x32A0, RTW_BIT(15), 0x0), WM(0x32A0, 0x7000, 0x7),
};

#undef WM
#undef WRF
#undef DLY

static
VOID
RtwRfkParse(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_(Count) const RTW_RFK_ENTRY *Table,
    _In_ ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        switch (Table[i].Flag)
        {
            case RTW_RFK_WRF:
                RtwRfWrite(Adapter, Table[i].Path, Table[i].Address, Table[i].Mask, Table[i].Data);
                break;
            case RTW_RFK_WM:
                RtwBbWrite32(Adapter, Table[i].Address, Table[i].Mask, Table[i].Data);
                break;
            case RTW_RFK_WS:
                RtwBbWrite32(Adapter, Table[i].Address, 0xFFFFFFFF,
                             RtwBbRead32(Adapter, Table[i].Address, 0xFFFFFFFF) | Table[i].Mask);
                break;
            case RTW_RFK_WC:
                RtwBbWrite32(Adapter, Table[i].Address, 0xFFFFFFFF,
                             RtwBbRead32(Adapter, Table[i].Address, 0xFFFFFFFF) & ~Table[i].Mask);
                break;
            case RTW_RFK_DELAY:
                KeStallExecutionProcessor(Table[i].Data);
                break;
        }
    }
}

static
BOOLEAN
RtwPollBb(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG TimeoutUs)
{
    ULONG Elapsed;

    for (Elapsed = 0; Elapsed <= TimeoutUs; Elapsed += 10)
    {
        if (RtwBbRead32(Adapter, Address, Mask))
            return TRUE;
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static
VOID
RtwRck(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path)
{
    ULONG Reg5, Value, Poll;

    Reg5 = RtwRfRead(Adapter, Path, RR_RSV1, RFREG_MASK);
    RtwRfWrite(Adapter, Path, RR_RSV1, RR_RSV1_RST, 0x0);
    RtwRfWrite(Adapter, Path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
    RtwRfWrite(Adapter, Path, RR_RCKC, RFREG_MASK, 0x00240);

    for (Poll = 0; Poll < 15; Poll++)
    {
        if (RtwRfRead(Adapter, Path, RR_RCKS, RTW_BIT(3)))
            break;
        KeStallExecutionProcessor(2);
    }

    Value = RtwRfRead(Adapter, Path, RR_RCKC, RR_RCKC_CA);
    RtwRfWrite(Adapter, Path, RR_RCKC, RFREG_MASK, Value);
    RtwRfWrite(Adapter, Path, RR_RSV1, RFREG_MASK, Reg5);
}

static
VOID
RtwDrck(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Value;

    RtwBbWrite32(Adapter, R_DRCK_V1, B_DRCK_V1_KICK, 0x1);
    if (!RtwPollBb(Adapter, R_DRCK_RS, B_DRCK_RS_DONE, 10000))
        DPRINT1("RTW8852BS: DRCK timed out\n");
    RtwBbWrite32(Adapter, R_DRCK_V1, B_DRCK_V1_KICK, 0x0);
    RtwBbWrite32(Adapter, R_DRCK_FH, B_DRCK_LAT, 0x1);
    KeStallExecutionProcessor(1);
    RtwBbWrite32(Adapter, R_DRCK_FH, B_DRCK_LAT, 0x0);
    Value = RtwBbRead32(Adapter, R_DRCK_RS, B_DRCK_RS_LPS);
    RtwBbWrite32(Adapter, R_DRCK_V1, B_DRCK_V1_SEL, 0x0);
    RtwBbWrite32(Adapter, R_DRCK_V1, B_DRCK_V1_CV, Value);
}

static
VOID
RtwAddckPath(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path)
{
    ULONG Sample = Path == 0 ? R_PATH0_SAMPL_DLY_T_V1 : R_PATH1_SAMPL_DLY_T_V1;

    if (Path == 0)
    {
        RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0_MAN, 0x0);
        RtwBbWrite32(Adapter, R_PATH1_SAMPL_DLY_T_V1, 0x30, 0x0);
        RtwBbWrite32(Adapter, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
    }
    else
    {
        RtwBbWrite32(Adapter, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
    }

    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_ADCCLK, 0x0);
    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
    RtwBbWrite32(Adapter, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xf);
    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_EN, 0x0);
    RtwBbWrite32(Adapter, Sample, RTW_BIT(1), 0x1);
    RtwBbWrite32(Adapter, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);

    if (Path == 0)
    {
        RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0_TRG, 0x1);
        RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0_TRG, 0x0);
        KeStallExecutionProcessor(1);
        RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0, 0x1);
        if (!RtwPollBb(Adapter, R_ADDCKR0, RTW_BIT(0), 10000))
            DPRINT1("RTW8852BS: S0 ADDCK timed out\n");
    }
    else
    {
        RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK1_TRG, 0x1);
        RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK1_TRG, 0x0);
        KeStallExecutionProcessor(1);
        RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK1, 0x1);
        if (!RtwPollBb(Adapter, R_ADDCKR1, RTW_BIT(0), 10000))
            DPRINT1("RTW8852BS: S1 ADDCK timed out\n");
    }

    RtwBbWrite32(Adapter, Sample, RTW_BIT(1), 0x0);
    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_EN, 0x1);
    RtwBbWrite32(Adapter, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xc);
    RtwBbWrite32(Adapter, R_ANAPAR, B_ANAPAR_ADCCLK, 0x1);
    if (Path == 0)
        RtwBbWrite32(Adapter, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);
    else
        RtwBbWrite32(Adapter, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static
VOID
RtwAddck(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG I0, Q0, I1, Q1;

    RtwAddckPath(Adapter, 0);
    RtwAddckPath(Adapter, 1);

    RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0, 0x0);
    I0 = RtwBbRead32(Adapter, R_ADDCKR0, B_ADDCKR0_A0);
    Q0 = RtwBbRead32(Adapter, R_ADDCKR0, B_ADDCKR0_A1);
    RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK1, 0x0);
    I1 = RtwBbRead32(Adapter, R_ADDCKR1, B_ADDCKR1_A0);
    Q1 = RtwBbRead32(Adapter, R_ADDCKR1, B_ADDCKR1_A1);

    RtwBbWrite32(Adapter, R_ADDCK0D, B_ADDCK0D_VAL, I0);
    RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0_VAL, Q0 >> 6);
    RtwBbWrite32(Adapter, R_ADDCK0D, B_ADDCK0D_VAL2, Q0 & 0x3F);
    RtwBbWrite32(Adapter, R_ADDCK0, B_ADDCK0_MAN, 0x3);
    RtwBbWrite32(Adapter, R_ADDCK1D, B_ADDCK1D_VAL, I1);
    RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK0_VAL, Q1 >> 6);
    RtwBbWrite32(Adapter, R_ADDCK1D, B_ADDCK1D_VAL2, Q1 & 0x3F);
    RtwBbWrite32(Adapter, R_ADDCK1, B_ADDCK1_MAN, 0x3);

    DPRINT("RTW8852BS: ADDCK S0 %03lx/%03lx S1 %03lx/%03lx\n", I0, Q0, I1, Q1);
}

static
BOOLEAN
RtwDackS0Done(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Part1)
{
    if (Part1)
        return RtwBbRead32(Adapter, R_DACK_S0P0, B_DACK_S0P0_OK) != 0 &&
               RtwBbRead32(Adapter, R_DACK_S0P1, B_DACK_S0P1_OK) != 0;
    return RtwBbRead32(Adapter, R_DACK_S0P2, B_DACK_S0P2_OK) != 0 &&
           RtwBbRead32(Adapter, R_DACK_S0P3, B_DACK_S0P3_OK) != 0;
}

static
BOOLEAN
RtwDackS1Done(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Part1)
{
    if (Part1)
        return RtwBbRead32(Adapter, R_DACK_S1P0, B_DACK_S1P0_OK) != 0 ||
               RtwBbRead32(Adapter, R_DACK_S1P1, B_DACK_S1P1_OK) != 0;
    return RtwBbRead32(Adapter, R_DACK10S, B_DACK_S1P2_OK) != 0 ||
           RtwBbRead32(Adapter, R_DACK11S, B_DACK_S1P3_OK) != 0;
}

static
BOOLEAN
RtwWaitDack(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Path1,
    _In_ BOOLEAN Part1)
{
    ULONG Poll;

    for (Poll = 0; Poll < 1000; Poll++)
    {
        if (Path1 ? RtwDackS1Done(Adapter, Part1) : RtwDackS0Done(Adapter, Part1))
            return TRUE;
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static
VOID
RtwDack(
    _In_ PRTW_ADAPTER Adapter)
{
    BOOLEAN Ok[4];

    RtwRfkParse(Adapter, RtwDackS0Part1, RTL_NUMBER_OF(RtwDackS0Part1));
    Ok[0] = RtwWaitDack(Adapter, FALSE, TRUE);
    RtwRfkParse(Adapter, RtwDackS0Part2, RTL_NUMBER_OF(RtwDackS0Part2));
    Ok[1] = RtwWaitDack(Adapter, FALSE, FALSE);
    RtwRfkParse(Adapter, RtwDackS0Part3, RTL_NUMBER_OF(RtwDackS0Part3));
    RtwBbWrite32(Adapter, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
    RtwBbWrite32(Adapter, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);

    RtwRfkParse(Adapter, RtwDackS1Part1, RTL_NUMBER_OF(RtwDackS1Part1));
    Ok[2] = RtwWaitDack(Adapter, TRUE, TRUE);
    RtwRfkParse(Adapter, RtwDackS1Part2, RTL_NUMBER_OF(RtwDackS1Part2));
    Ok[3] = RtwWaitDack(Adapter, TRUE, FALSE);
    RtwRfkParse(Adapter, RtwDackS1Part3, RTL_NUMBER_OF(RtwDackS1Part3));
    RtwRfkParse(Adapter, RtwCheckDadcEnB, RTL_NUMBER_OF(RtwCheckDadcEnB));
    RtwRfkParse(Adapter, RtwCheckAddcB, RTL_NUMBER_OF(RtwCheckAddcB));
    RtwRfkParse(Adapter, RtwCheckDadcDisB, RTL_NUMBER_OF(RtwCheckDadcDisB));
    RtwBbWrite32(Adapter, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
    RtwBbWrite32(Adapter, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);

    DPRINT("RTW8852BS: DACK S0 %u/%u S1 %u/%u\n", Ok[0], Ok[1], Ok[2], Ok[3]);
}

static
VOID
RtwDacCalibration(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Mode0, Mode1;

    Mode0 = RtwRfRead(Adapter, 0, RR_MOD, RFREG_MASK);
    Mode1 = RtwRfRead(Adapter, 1, RR_MOD, RFREG_MASK);

    RtwWrite32(Adapter, R_AX_PHYREG_SET, 0xF);
    RtwRfkParse(Adapter, RtwAfeInit, RTL_NUMBER_OF(RtwAfeInit));
    RtwDrck(Adapter);

    RtwRfWrite(Adapter, 0, RR_RSV1, RR_RSV1_RST, 0x0);
    RtwRfWrite(Adapter, 1, RR_RSV1, RR_RSV1_RST, 0x0);
    RtwRfWrite(Adapter, 0, RR_MOD, RFREG_MASK, 0x337e1);
    RtwRfWrite(Adapter, 1, RR_MOD, RFREG_MASK, 0x337e1);
    RtwAddck(Adapter);

    RtwRfWrite(Adapter, 0, RR_MODOPT, RFREG_MASK, 0x0);
    RtwRfWrite(Adapter, 1, RR_MODOPT, RFREG_MASK, 0x0);
    RtwDack(Adapter);

    RtwRfWrite(Adapter, 0, RR_MOD, RFREG_MASK, Mode0);
    RtwRfWrite(Adapter, 1, RR_MOD, RFREG_MASK, Mode1);
    RtwRfWrite(Adapter, 0, RR_RSV1, RR_RSV1_RST, 0x1);
    RtwRfWrite(Adapter, 1, RR_RSV1, RR_RSV1_RST, 0x1);
}

VOID
RtwRxDck(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Reg5, Tune;
    UCHAR Path;

    for (Path = 0; Path < 2; Path++)
    {
        Reg5 = RtwRfRead(Adapter, Path, RR_RSV1, RFREG_MASK);
        Tune = RtwRfRead(Adapter, Path, RR_DCK, RR_DCK_FINE);

        RtwRfWrite(Adapter, Path, RR_RSV1, RR_RSV1_RST, 0x0);
        RtwRfWrite(Adapter, Path, RR_DCK, RR_DCK_FINE, 0x0);
        RtwRfWrite(Adapter, Path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
        RtwRfWrite(Adapter, Path, RR_DCK1, RR_DCK1_CLR, 0x0);
        RtwRfWrite(Adapter, Path, RR_DCK, RR_DCK_LV, 0x0);
        RtwRfWrite(Adapter, Path, RR_DCK, RR_DCK_LV, 0x1);
        RtwSleep(1);
        RtwRfWrite(Adapter, Path, RR_DCK, RR_DCK_FINE, Tune);
        RtwRfWrite(Adapter, Path, RR_RSV1, RFREG_MASK, Reg5);
    }
}

VOID
RtwRfkInit(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG OfdmBackoff, TxScale;
    UCHAR Path;

    OfdmBackoff = RtwBbRead32(Adapter, R_DPD_BF, B_DPD_BF_OFDM);
    TxScale = RtwBbRead32(Adapter, R_DPD_BF, B_DPD_BF_SCA);
    if (OfdmBackoff + TxScale >= 44)
    {
        for (Path = 0; Path < 2; Path++)
            RtwBbWrite32(Adapter, R_DPD_CH0A + ((ULONG)Path << 8), B_DPD_CFG, 0x7f7f7f);
    }

    RtwRck(Adapter, 0);
    RtwRck(Adapter, 1);
    RtwDacCalibration(Adapter);
    RtwRxDck(Adapter);
}
