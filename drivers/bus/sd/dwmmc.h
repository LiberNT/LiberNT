/*
 * PROJECT:     LiberNT SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synopsys DesignWare mobile storage host controller backend definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SDBUS_DWMMC_H_
#define _SDBUS_DWMMC_H_

#include "sdbus.h"

#define DWMMC_CTRL                      0x000
#define DWMMC_PWREN                     0x004
#define DWMMC_CLKDIV                    0x008
#define DWMMC_CLKSRC                    0x00C
#define DWMMC_CLKENA                    0x010
#define DWMMC_TMOUT                     0x014
#define DWMMC_CTYPE                     0x018
#define DWMMC_BLKSIZ                    0x01C
#define DWMMC_BYTCNT                    0x020
#define DWMMC_INTMASK                   0x024
#define DWMMC_CMDARG                    0x028
#define DWMMC_CMD                       0x02C
#define DWMMC_RESP0                     0x030
#define DWMMC_RESP1                     0x034
#define DWMMC_RESP2                     0x038
#define DWMMC_RESP3                     0x03C
#define DWMMC_RINTSTS                   0x044
#define DWMMC_STATUS                    0x048
#define DWMMC_FIFOTH                    0x04C
#define DWMMC_VERID                     0x06C
#define DWMMC_HCON                      0x070
#define DWMMC_BMOD                      0x080
#define DWMMC_PLDMND                    0x084
#define DWMMC_DBADDR                    0x088
#define DWMMC_DBADDRU                   0x08C
#define DWMMC_IDSTS32                   0x08C
#define DWMMC_IDINTEN32                 0x090
#define DWMMC_IDSTS64                   0x090
#define DWMMC_IDINTEN64                 0x094
#define DWMMC_DATA_OFFSET               0x100
#define DWMMC_DATA_240A_OFFSET          0x200
#define DWMMC_VERSION_240A              0x240A

#define DWMMC_CTRL_RESET                (1UL << 0)
#define DWMMC_CTRL_FIFO_RESET           (1UL << 1)
#define DWMMC_CTRL_DMA_RESET            (1UL << 2)
#define DWMMC_CTRL_INT_ENABLE           (1UL << 4)
#define DWMMC_CTRL_DMA_ENABLE           (1UL << 5)
#define DWMMC_CTRL_USE_IDMAC            (1UL << 25)
#define DWMMC_CTRL_ALL_RESET            (DWMMC_CTRL_RESET | DWMMC_CTRL_FIFO_RESET | DWMMC_CTRL_DMA_RESET)

#define DWMMC_HCON_DMA_INTERFACE(Hcon)  (((Hcon) >> 16) & 0x3UL)
#define DWMMC_HCON_DMA_IDMAC            0UL
#define DWMMC_HCON_ADDR_64BIT           (1UL << 27)

#define DWMMC_BMOD_SWR                  (1UL << 0)
#define DWMMC_BMOD_FB                   (1UL << 1)
#define DWMMC_BMOD_DE                   (1UL << 7)

#define DWMMC_IDSTS_TI                  (1UL << 0)
#define DWMMC_IDSTS_RI                  (1UL << 1)
#define DWMMC_IDSTS_FBE                 (1UL << 2)
#define DWMMC_IDSTS_DU                  (1UL << 4)
#define DWMMC_IDSTS_CES                 (1UL << 5)
#define DWMMC_IDSTS_NIS                 (1UL << 8)
#define DWMMC_IDSTS_AIS                 (1UL << 9)
#define DWMMC_IDSTS_ALL                 (DWMMC_IDSTS_TI | DWMMC_IDSTS_RI | DWMMC_IDSTS_FBE | DWMMC_IDSTS_DU | \
                                         DWMMC_IDSTS_CES | DWMMC_IDSTS_NIS | DWMMC_IDSTS_AIS)
#define DWMMC_IDSTS_ERRORS              (DWMMC_IDSTS_FBE | DWMMC_IDSTS_DU | DWMMC_IDSTS_CES)
#define DWMMC_IDINTEN_COMPLETION        (DWMMC_IDSTS_TI | DWMMC_IDSTS_RI | DWMMC_IDSTS_NIS)

#define DWMMC_DES0_DIC                  (1UL << 1)
#define DWMMC_DES0_LD                   (1UL << 2)
#define DWMMC_DES0_FS                   (1UL << 3)
#define DWMMC_DES0_CH                   (1UL << 4)
#define DWMMC_DES0_OWN                  (1UL << 31)
#define DWMMC_DES_BS1_MASK              0x1FFFUL

#define DWMMC_IDMAC_BUFFER_MAX          4096UL
#define DWMMC_IDMAC_DESCRIPTORS         128UL

typedef struct _DWMMC_IDMAC_DESC32
{
    ULONG Des0;
    ULONG Des1;
    ULONG Des2;
    ULONG Des3;
} DWMMC_IDMAC_DESC32, *PDWMMC_IDMAC_DESC32;

typedef struct _DWMMC_IDMAC_DESC64
{
    ULONG Des0;
    ULONG Des1;
    ULONG Des2;
    ULONG Des3;
    ULONG Des4;
    ULONG Des5;
    ULONG Des6;
    ULONG Des7;
} DWMMC_IDMAC_DESC64, *PDWMMC_IDMAC_DESC64;

C_ASSERT(sizeof(DWMMC_IDMAC_DESC32) == 16);
C_ASSERT(sizeof(DWMMC_IDMAC_DESC64) == 32);
C_ASSERT(DWMMC_IDMAC_BUFFER_MAX <= DWMMC_DES_BS1_MASK);
C_ASSERT(DWMMC_IDMAC_DESCRIPTORS * sizeof(DWMMC_IDMAC_DESC64) <= PAGE_SIZE);

#define DWMMC_CLKEN_ENABLE              (1UL << 0)
#define DWMMC_TMOUT_MAX                 0xFFFFFFFFUL
#define DWMMC_CTYPE_1BIT                0UL
#define DWMMC_CTYPE_4BIT                (1UL << 0)
#define DWMMC_CTYPE_8BIT                (1UL << 16)

#define DWMMC_INT_RESP_ERR              (1UL << 1)
#define DWMMC_INT_CMD_DONE              (1UL << 2)
#define DWMMC_INT_DATA_OVER             (1UL << 3)
#define DWMMC_INT_RCRC                  (1UL << 6)
#define DWMMC_INT_DCRC                  (1UL << 7)
#define DWMMC_INT_RTO                   (1UL << 8)
#define DWMMC_INT_DRTO                  (1UL << 9)
#define DWMMC_INT_HTO                   (1UL << 10)
#define DWMMC_INT_FRUN                  (1UL << 11)
#define DWMMC_INT_HLE                   (1UL << 12)
#define DWMMC_INT_SBE                   (1UL << 13)
#define DWMMC_INT_ACD                   (1UL << 14)
#define DWMMC_INT_EBE                   (1UL << 15)
#define DWMMC_INT_ALL                   0xFFFFFFFFUL
#define DWMMC_INT_CMD_ERRORS            (DWMMC_INT_RESP_ERR | DWMMC_INT_RCRC | DWMMC_INT_RTO | DWMMC_INT_HLE)
#define DWMMC_INT_DATA_ERRORS           (DWMMC_INT_DCRC | DWMMC_INT_DRTO | DWMMC_INT_HTO | DWMMC_INT_FRUN | \
                                         DWMMC_INT_SBE | DWMMC_INT_EBE)

#define DWMMC_CMD_INDEX_MASK            0x3FUL
#define DWMMC_CMD_RESP_EXP              (1UL << 6)
#define DWMMC_CMD_RESP_LONG             (1UL << 7)
#define DWMMC_CMD_RESP_CRC              (1UL << 8)
#define DWMMC_CMD_DAT_EXP               (1UL << 9)
#define DWMMC_CMD_DAT_WR                (1UL << 10)
#define DWMMC_CMD_SEND_STOP             (1UL << 12)
#define DWMMC_CMD_PRV_DAT_WAIT          (1UL << 13)
#define DWMMC_CMD_STOP                  (1UL << 14)
#define DWMMC_CMD_INIT                  (1UL << 15)
#define DWMMC_CMD_UPD_CLK               (1UL << 21)
#define DWMMC_CMD_USE_HOLD_REG          (1UL << 29)
#define DWMMC_CMD_START                 (1UL << 31)

#define DWMMC_STATUS_BUSY               (1UL << 9)
#define DWMMC_STATUS_FCNT_SHIFT         17
#define DWMMC_STATUS_FCNT_MASK          0x1FFFUL

#define DWMMC_FIFOTH_MSIZE_8            (2UL << 28)
#define DWMMC_FIFOTH_RX_SHIFT           16
#define DWMMC_FIFOTH_DEPTH_MASK         0xFFFUL

NTSTATUS
DwMmcInitializeController(_In_ PFDO_EXTENSION FdoExtension);

NTSTATUS
DwMmcReset(_In_ PFDO_EXTENSION FdoExtension);

NTSTATUS
DwMmcSetClock(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG TargetClockKhz);

VOID
DwMmcSetBusWidth(_In_ PFDO_EXTENSION FdoExtension, _In_ UCHAR BusWidth);

NTSTATUS
DwMmcSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response);

NTSTATUS
DwMmcExecuteDataCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _Inout_updates_bytes_(BlockSize * BlockCount) PVOID Buffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _Out_opt_ PULONG Response);

NTSTATUS
DwMmcExecuteRequest(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ ULONG BlockSize,
    _In_ USHORT RequestFlags,
    _Out_opt_ PULONG Response);

#endif /* _SDBUS_DWMMC_H_ */
