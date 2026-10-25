/*
 * PROJECT:     LiberNT Intel Gigabit Ethernet Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intel I218 and I219 chipset LAN controllers: firmware handshake, PHY access, reset and link handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "e1000.h"

#define E1000_PCH_LINK_WORK_IDLE    0
#define E1000_PCH_LINK_WORK_QUEUED  1
#define E1000_PCH_LINK_WORK_AGAIN   2

static const struct
{
    USHORT DeviceId;
    ULONG Generation;
} E1000PchDevices[] =
{
    { 0x15A0, E1000_PCH_LPT },
    { 0x15F9, E1000_PCH_TGP },
};

static
VOID
E1000PchDelay(
    _In_ ULONG Microseconds)
{
    if (Microseconds >= 1000 && KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        NdisMSleep(Microseconds);
        return;
    }

    while (Microseconds > 50)
    {
        NdisStallExecution(50);
        Microseconds -= 50;
    }
    NdisStallExecution(Microseconds);
}

static
ULONGLONG
E1000PchDeadline(
    _In_ ULONG Milliseconds)
{
    return KeQueryInterruptTime() + (ULONGLONG)Milliseconds * 10000;
}

static
VOID
E1000PchFlush(
    _In_ PE1000_ADAPTER Adapter)
{
    E1000_READ_REG(Adapter, E1000_REG_STATUS);
}

static
BOOLEAN
E1000PchAcquirePhy(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONGLONG Deadline;
    ULONG Value;

    Deadline = E1000PchDeadline(E1000_PCH_SW_FLAG_TIMEOUT_MS);
    while (InterlockedCompareExchange(&Adapter->PchPhyBusy, 1, 0) != 0)
    {
        if (KeGetCurrentIrql() >= DISPATCH_LEVEL || KeQueryInterruptTime() >= Deadline)
            return FALSE;
        E1000PchDelay(1000);
    }

    Deadline = E1000PchDeadline(E1000_PCH_PHY_CFG_TIMEOUT_MS);
    for (;;)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL);
        if (!(Value & E1000_PCH_EXTCNF_CTRL_SWFLAG))
            break;
        if (KeQueryInterruptTime() >= Deadline)
        {
            InterlockedExchange(&Adapter->PchPhyBusy, 0);
            return FALSE;
        }
        E1000PchDelay(1000);
    }

    E1000_WRITE_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL, Value | E1000_PCH_EXTCNF_CTRL_SWFLAG);
    Deadline = E1000PchDeadline(E1000_PCH_SW_FLAG_TIMEOUT_MS);
    for (;;)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL);
        if (Value & E1000_PCH_EXTCNF_CTRL_SWFLAG)
            break;
        if (KeQueryInterruptTime() >= Deadline)
        {
            DPRINT1("E1000: Chipset LAN PHY is held by the firmware, FWSM=0x%08lx EXTCNF_CTRL=0x%08lx\n",
                    E1000_READ_REG(Adapter, E1000_REG_FWSM), Value);
            E1000_WRITE_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL, Value & ~E1000_PCH_EXTCNF_CTRL_SWFLAG);
            InterlockedExchange(&Adapter->PchPhyBusy, 0);
            return FALSE;
        }
        E1000PchDelay(1000);
    }

    return TRUE;
}

static
VOID
E1000PchReleasePhy(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONG Value;

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL);
    if (Value & E1000_PCH_EXTCNF_CTRL_SWFLAG)
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL, Value & ~E1000_PCH_EXTCNF_CTRL_SWFLAG);
    InterlockedExchange(&Adapter->PchPhyBusy, 0);
}

static
BOOLEAN
E1000PchMdic(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG PhyAddress,
    _In_ ULONG Register,
    _In_ BOOLEAN Write,
    _Inout_ PUSHORT Data)
{
    ULONG Mdic, Index;

    Mdic = (Register << E1000_MDIC_REGADD_SHIFT) | (PhyAddress << E1000_MDIC_PHYADD_SHIFT);
    if (Write)
        Mdic |= E1000_MDIC_OP_WRITE | *Data;
    else
        Mdic |= E1000_MDIC_OP_READ;
    E1000_WRITE_REG(Adapter, E1000_REG_MDIC, Mdic);

    for (Index = 0; Index < E1000_PCH_MDIC_POLL_COUNT; Index++)
    {
        NdisStallExecution(50);
        Mdic = E1000_READ_REG(Adapter, E1000_REG_MDIC);
        if (Mdic & E1000_MDIC_R)
            break;
    }

    if (!(Mdic & E1000_MDIC_R) || (Mdic & E1000_MDIC_E))
        return FALSE;
    if (((Mdic >> E1000_MDIC_REGADD_SHIFT) & MAX_PHY_REG_ADDRESS) != Register)
        return FALSE;

    if (!Write)
        *Data = (USHORT)(Mdic & E1000_MDIC_DATA_MASK);
    return TRUE;
}

static
BOOLEAN
E1000PchAccessPhy(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ BOOLEAN Write,
    _Inout_ PUSHORT Data)
{
    USHORT Page = E1000_PCH_PHY_PAGE(Offset);
    ULONG Register = E1000_PCH_PHY_NUM(Offset);
    ULONG PhyAddress;
    USHORT Select;

    if (Page == 0)
        PhyAddress = E1000_PCH_PHY_ADDRESS_MII;
    else if (Page >= E1000_PCH_PHY_PORT_PAGE_START && Page < 800)
        PhyAddress = E1000_PCH_PHY_ADDRESS_PORT;
    else
        return FALSE;

    if (Register > E1000_PCH_PHY_MULTI_PAGE_REG)
    {
        Select = (Page == E1000_PCH_PHY_PORT_PAGE_START) ? 0 : (USHORT)(Page << E1000_PCH_PHY_PAGE_SHIFT);
        if (!E1000PchMdic(Adapter, E1000_PCH_PHY_ADDRESS_PORT, E1000_PCH_PHY_PAGE_SELECT, TRUE, &Select))
            return FALSE;
    }

    return E1000PchMdic(Adapter, PhyAddress, Register, Write, Data);
}

static
BOOLEAN
E1000PchReadPhy(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Offset,
    _Out_ PUSHORT Data)
{
    *Data = 0;
    return E1000PchAccessPhy(Adapter, Offset, FALSE, Data);
}

static
BOOLEAN
E1000PchWritePhy(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ USHORT Data)
{
    return E1000PchAccessPhy(Adapter, Offset, TRUE, &Data);
}

static
BOOLEAN
E1000PchResetBlocked(
    _In_ PE1000_ADAPTER Adapter)
{
    ULONGLONG Deadline = E1000PchDeadline(E1000_PCH_RESET_BLOCK_TIMEOUT_MS);

    while (!(E1000_READ_REG(Adapter, E1000_REG_FWSM) & E1000_PCH_FWSM_RSPCIPHY))
    {
        if (KeQueryInterruptTime() >= Deadline)
            return TRUE;
        E1000PchDelay(10000);
    }

    return FALSE;
}

static
ULONG
E1000PchWaitConfigDone(
    _In_ PE1000_ADAPTER Adapter)
{
    ULONG Value = 0, Index;

    E1000PchDelay(10000);
    for (Index = 0; Index < E1000_PCH_LAN_INIT_POLL_COUNT; Index++)
    {
        Value = E1000_READ_REG(Adapter, E1000_REG_STATUS);
        if (Value & E1000_PCH_STATUS_LAN_INIT_DONE)
            break;
        NdisStallExecution(100);
    }
    if (Value == 0xFFFFFFFF)
        return Value;

    E1000_WRITE_REG(Adapter, E1000_REG_STATUS, Value & ~E1000_PCH_STATUS_LAN_INIT_DONE);
    Value = E1000_READ_REG(Adapter, E1000_REG_STATUS);
    if (Value & E1000_STATUS_PHYRA)
        E1000_WRITE_REG(Adapter, E1000_REG_STATUS, Value & ~E1000_STATUS_PHYRA);

    return Value;
}

static
BOOLEAN
E1000PchPhyIsAccessible(
    _Inout_ PE1000_ADAPTER Adapter)
{
    USHORT High, Low, Value;
    ULONG Attempt, PhyId = 0, MacValue;

    for (Attempt = 0; Attempt < 2 && !PhyId; Attempt++)
    {
        if (!E1000PchReadPhy(Adapter, E1000_PCH_MII_PHYSID1, &High) || High == 0xFFFF)
            continue;
        if (!E1000PchReadPhy(Adapter, E1000_PCH_MII_PHYSID2, &Low) || Low == 0xFFFF)
            continue;
        PhyId = (((ULONG)High << 16) | Low) & E1000_PCH_PHY_REVISION_MASK;
    }

    if (!PhyId)
        return FALSE;
    Adapter->PchPhyId = PhyId;

    if (!(E1000_READ_REG(Adapter, E1000_REG_FWSM) & E1000_PCH_FWSM_FW_VALID))
    {
        if (E1000PchReadPhy(Adapter, E1000_PCH_PHY_SMB_CTRL, &Value))
            E1000PchWritePhy(Adapter, E1000_PCH_PHY_SMB_CTRL, Value & ~E1000_PCH_PHY_SMB_CTRL_FORCE_SMBUS);

        MacValue = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, MacValue & ~E1000_PCH_CTRL_EXT_FORCE_SMBUS);
    }

    return TRUE;
}

static
VOID
E1000PchToggleLanPhyPc(
    _In_ PE1000_ADAPTER Adapter)
{
    ULONGLONG Deadline;
    ULONG Value;

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM3);
    Value &= ~E1000_PCH_FEXTNVM3_PHY_CFG_COUNTER_MASK;
    Value |= E1000_PCH_FEXTNVM3_PHY_CFG_COUNTER_50MSEC;
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_FEXTNVM3, Value);

    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
    Value |= E1000_PCH_CTRL_LANPHYPC_OVERRIDE;
    Value &= ~E1000_PCH_CTRL_LANPHYPC_VALUE;
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value);
    E1000PchFlush(Adapter);
    NdisStallExecution(20);
    Value &= ~E1000_PCH_CTRL_LANPHYPC_OVERRIDE;
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value);
    E1000PchFlush(Adapter);

    Deadline = E1000PchDeadline(E1000_PCH_LANPHYPC_TIMEOUT_MS);
    do
    {
        E1000PchDelay(5000);
    } while (!(E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT) & E1000_PCH_CTRL_EXT_LPCD) &&
             KeQueryInterruptTime() < Deadline);
    E1000PchDelay(30000);
}

static
VOID
E1000PchDisableUlp(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONGLONG Deadline;
    ULONG Value;
    USHORT PhyValue;

    if (Adapter->PchGeneration < E1000_PCH_SPT)
        return;

    if (E1000_READ_REG(Adapter, E1000_REG_FWSM) & E1000_PCH_FWSM_FW_VALID)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_H2ME);
        Value &= ~E1000_PCH_H2ME_ULP;
        Value |= E1000_PCH_H2ME_ENFORCE_SETTINGS;
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_H2ME, Value);

        Deadline = E1000PchDeadline(E1000_PCH_ULP_EXIT_TIMEOUT_MS);
        while (E1000_READ_REG(Adapter, E1000_REG_FWSM) & E1000_PCH_FWSM_ULP_CFG_DONE)
        {
            if (KeQueryInterruptTime() >= Deadline)
            {
                DPRINT1("E1000: Chipset LAN firmware did not take the PHY out of its low power mode\n");
                break;
            }
            E1000PchDelay(10000);
        }

        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_H2ME);
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_H2ME, Value & ~E1000_PCH_H2ME_ENFORCE_SETTINGS);
        return;
    }

    if (!E1000PchAcquirePhy(Adapter))
        return;

    E1000PchToggleLanPhyPc(Adapter);

    if (!E1000PchReadPhy(Adapter, E1000_PCH_PHY_SMB_CTRL, &PhyValue))
    {
        Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value | E1000_PCH_CTRL_EXT_FORCE_SMBUS);
        E1000PchDelay(50000);

        if (!E1000PchReadPhy(Adapter, E1000_PCH_PHY_SMB_CTRL, &PhyValue))
            goto Release;
    }
    E1000PchWritePhy(Adapter, E1000_PCH_PHY_SMB_CTRL, PhyValue & ~E1000_PCH_PHY_SMB_CTRL_FORCE_SMBUS);

    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value & ~E1000_PCH_CTRL_EXT_FORCE_SMBUS);

    if (!E1000PchReadPhy(Adapter, E1000_PCH_PHY_PM_CTRL, &PhyValue))
        goto Release;
    E1000PchWritePhy(Adapter, E1000_PCH_PHY_PM_CTRL, PhyValue | E1000_PCH_PHY_PM_CTRL_K1_ENABLE);

    if (!E1000PchReadPhy(Adapter, E1000_PCH_PHY_ULP_CONFIG1, &PhyValue))
        goto Release;
    PhyValue &= ~(E1000_PCH_PHY_ULP_CONFIG1_IND |
                  E1000_PCH_PHY_ULP_CONFIG1_STICKY_ULP |
                  E1000_PCH_PHY_ULP_CONFIG1_RESET_TO_SMBUS |
                  E1000_PCH_PHY_ULP_CONFIG1_WOL_HOST |
                  E1000_PCH_PHY_ULP_CONFIG1_INBAND_EXIT |
                  E1000_PCH_PHY_ULP_CONFIG1_EN_ULP_LANPHYPC |
                  E1000_PCH_PHY_ULP_CONFIG1_DIS_CLR_STICKY_ON_PERST |
                  E1000_PCH_PHY_ULP_CONFIG1_DISABLE_SMB_PERST);
    E1000PchWritePhy(Adapter, E1000_PCH_PHY_ULP_CONFIG1, PhyValue);
    E1000PchWritePhy(Adapter, E1000_PCH_PHY_ULP_CONFIG1, PhyValue | E1000_PCH_PHY_ULP_CONFIG1_START);

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM7);
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_FEXTNVM7, Value & ~E1000_PCH_FEXTNVM7_DISABLE_SMB_PERST);

Release:
    E1000PchReleasePhy(Adapter);
}

static
VOID
E1000PchPreparePhy(
    _Inout_ PE1000_ADAPTER Adapter)
{
    BOOLEAN Accessible, Blocked;
    ULONG Value;

    Blocked = E1000PchResetBlocked(Adapter);
    Adapter->PchPhyId = 0;

    if (!E1000PchAcquirePhy(Adapter))
    {
        DPRINT1("E1000: Chipset LAN PHY could not be reserved, FWSM=0x%08lx\n", E1000_READ_REG(Adapter, E1000_REG_FWSM));
        return;
    }

    Accessible = E1000PchPhyIsAccessible(Adapter);
    if (!Accessible)
    {
        Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value | E1000_PCH_CTRL_EXT_FORCE_SMBUS);
        E1000PchDelay(50000);
        Accessible = E1000PchPhyIsAccessible(Adapter);
    }

    if (!Accessible && !Blocked)
    {
        E1000PchToggleLanPhyPc(Adapter);
        Accessible = E1000PchPhyIsAccessible(Adapter);
        if (!Accessible)
        {
            Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
            E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value & ~E1000_PCH_CTRL_EXT_FORCE_SMBUS);
            Accessible = E1000PchPhyIsAccessible(Adapter);
        }
    }

    E1000PchReleasePhy(Adapter);

    if (Accessible && !Blocked && E1000PchAcquirePhy(Adapter))
    {
        Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value | E1000_CTRL_PHY_RST);
        E1000PchFlush(Adapter);
        NdisStallExecution(100);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value);
        E1000PchFlush(Adapter);
        NdisStallExecution(150);
        E1000PchReleasePhy(Adapter);
        E1000PchWaitConfigDone(Adapter);
        Blocked = E1000PchResetBlocked(Adapter);
    }

    DPRINT1("E1000: Chipset LAN PHY id 0x%08lx %s, management firmware %s, PHY reset %s\n",
            Adapter->PchPhyId,
            Accessible ? "reachable" : "not reachable",
            (E1000_READ_REG(Adapter, E1000_REG_FWSM) & E1000_PCH_FWSM_FW_VALID) ? "present" : "absent",
            Blocked ? "blocked by it" : "allowed");
}

static
VOID
E1000PchApplyLinkSettings(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONG Status, Speed, Value;
    USHORT PhyValue, Emi;
    BOOLEAN FullDuplex;

    if (Adapter->IoBase == NULL)
        return;

    Status = E1000_READ_REG(Adapter, E1000_REG_STATUS);
    if (Status & E1000_STATUS_LU)
    {
        Speed = Status & E1000_STATUS_SPEEDMASK;
        FullDuplex = (Status & E1000_STATUS_FD) != 0;

        Value = E1000_READ_REG(Adapter, E1000_REG_TIPG) & ~E1000_PCH_TIPG_IPGT_MASK;
        if (!FullDuplex && Speed == E1000_STATUS_SPEED_10)
        {
            Value |= E1000_PCH_TIPG_IPGT_10HALF;
            Emi = E1000_PCH_EMI_RX_CONFIG_10HALF;
        }
        else if (Adapter->PchGeneration >= E1000_PCH_SPT && FullDuplex && Speed != E1000_STATUS_SPEED_1000)
        {
            Value |= E1000_PCH_TIPG_IPGT_SLOW_FULL;
            Emi = E1000_PCH_EMI_RX_CONFIG_DEFAULT;
        }
        else
        {
            Value |= E1000_PCH_TIPG_IPGT_DEFAULT;
            Emi = E1000_PCH_EMI_RX_CONFIG_DEFAULT;
        }
        E1000_WRITE_REG(Adapter, E1000_REG_TIPG, Value);

        if (E1000PchAcquirePhy(Adapter))
        {
            if (E1000PchWritePhy(Adapter, E1000_PCH_MII_EMI_ADDR, E1000_PCH_EMI_RX_CONFIG))
                E1000PchWritePhy(Adapter, E1000_PCH_MII_EMI_DATA, Emi);

            if (E1000PchReadPhy(Adapter, E1000_PCH_PHY_PLL_CLOCK_GATE, &PhyValue))
            {
                PhyValue &= ~E1000_PCH_PHY_PLL_CLOCK_GATE_MASK;
                PhyValue |= (Speed == E1000_STATUS_SPEED_1000) ? E1000_PCH_PHY_PLL_CLOCK_GATE_1000 :
                                                                 E1000_PCH_PHY_PLL_CLOCK_GATE_SLOW;
                E1000PchWritePhy(Adapter, E1000_PCH_PHY_PLL_CLOCK_GATE, PhyValue);
            }

            if (Speed == E1000_STATUS_SPEED_1000)
            {
                if (E1000PchReadPhy(Adapter, E1000_PCH_PHY_PM_CTRL, &PhyValue))
                    E1000PchWritePhy(Adapter, E1000_PCH_PHY_PM_CTRL, PhyValue | E1000_PCH_PHY_PM_CTRL_K1_CLK_REQ);

                if (Adapter->PchGeneration >= E1000_PCH_SPT &&
                    E1000PchReadPhy(Adapter, E1000_PCH_PHY_POINTER_GAP, &PhyValue) &&
                    ((PhyValue & E1000_PCH_PHY_POINTER_GAP_MASK) >> E1000_PCH_PHY_POINTER_GAP_SHIFT) <
                        E1000_PCH_PHY_POINTER_GAP_1000_MINIMUM)
                {
                    PhyValue &= ~E1000_PCH_PHY_POINTER_GAP_MASK;
                    PhyValue |= E1000_PCH_PHY_POINTER_GAP_1000_MINIMUM << E1000_PCH_PHY_POINTER_GAP_SHIFT;
                    E1000PchWritePhy(Adapter, E1000_PCH_PHY_POINTER_GAP, PhyValue);
                }
            }
            else if (Adapter->PchGeneration >= E1000_PCH_SPT)
            {
                E1000PchWritePhy(Adapter, E1000_PCH_PHY_POINTER_GAP, E1000_PCH_PHY_POINTER_GAP_SLOW);
            }

            E1000PchReleasePhy(Adapter);
        }
    }

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM4);
    Value &= ~E1000_PCH_FEXTNVM4_BEACON_DURATION_MASK;
    Value |= E1000_PCH_FEXTNVM4_BEACON_DURATION_8USEC;
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_FEXTNVM4, Value);
}

static
VOID
NTAPI
E1000PchLinkWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PE1000_ADAPTER Adapter = WorkItemContext;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    for (;;)
    {
        if (!Adapter->PchHalting)
            E1000PchApplyLinkSettings(Adapter);

        if (InterlockedCompareExchange(&Adapter->PchLinkWorkState,
                                       E1000_PCH_LINK_WORK_IDLE,
                                       E1000_PCH_LINK_WORK_QUEUED) == E1000_PCH_LINK_WORK_QUEUED)
        {
            break;
        }
        InterlockedExchange(&Adapter->PchLinkWorkState, E1000_PCH_LINK_WORK_QUEUED);
    }
}

VOID
E1000PchLinkChanged(
    _Inout_ PE1000_ADAPTER Adapter)
{
    LONG State;

    if (Adapter->PchLinkWorkItem == NULL || Adapter->PchHalting)
        return;

    for (;;)
    {
        State = Adapter->PchLinkWorkState;
        if (State == E1000_PCH_LINK_WORK_IDLE)
        {
            if (InterlockedCompareExchange(&Adapter->PchLinkWorkState,
                                           E1000_PCH_LINK_WORK_QUEUED,
                                           E1000_PCH_LINK_WORK_IDLE) == E1000_PCH_LINK_WORK_IDLE)
            {
                NdisQueueIoWorkItem(Adapter->PchLinkWorkItem, E1000PchLinkWorker, Adapter);
                return;
            }
        }
        else if (State == E1000_PCH_LINK_WORK_QUEUED)
        {
            if (InterlockedCompareExchange(&Adapter->PchLinkWorkState,
                                           E1000_PCH_LINK_WORK_AGAIN,
                                           E1000_PCH_LINK_WORK_QUEUED) == E1000_PCH_LINK_WORK_QUEUED)
            {
                return;
            }
        }
        else
        {
            return;
        }
    }
}

BOOLEAN
E1000PchRecognize(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONG Index;

    Adapter->IsPchLan = FALSE;
    for (Index = 0; Index < ARRAYSIZE(E1000PchDevices); Index++)
    {
        if (E1000PchDevices[Index].DeviceId == Adapter->DeviceId)
        {
            Adapter->IsPchLan = TRUE;
            Adapter->PchGeneration = E1000PchDevices[Index].Generation;
            Adapter->IsPCIe = FALSE;
            return TRUE;
        }
    }

    return FALSE;
}

NDIS_STATUS
E1000PchResetHardware(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONG Value, Index;
    USHORT PhyValue;
    BOOLEAN Reserved, Blocked;

    if (Adapter->IoBase == NULL)
        return NDIS_STATUS_HARD_ERRORS;

    if (Adapter->PchLinkWorkItem == NULL && Adapter->MiniportAdapterHandle != NULL &&
        KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        Adapter->PchLinkWorkItem = NdisAllocateIoWorkItem(Adapter->MiniportAdapterHandle);
    }

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL);
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_EXTCNF_CTRL, Value | E1000_PCH_EXTCNF_CTRL_GATE_PHY_CFG);
        E1000PchDisableUlp(Adapter);
        E1000PchPreparePhy(Adapter);
    }

    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value | E1000_CTRL_GIO_MASTER_DIS);
    for (Index = 0; Index < E1000_PCH_MASTER_DISABLE_POLL_COUNT; Index++)
    {
        if (!(E1000_READ_REG(Adapter, E1000_REG_STATUS) & E1000_STATUS_GIO_MASTER_EN))
            break;
        NdisStallExecution(100);
    }

    E1000_WRITE_REG(Adapter, E1000_REG_IMC, 0xFFFFFFFF);
    E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);
    E1000_WRITE_REG(Adapter, E1000_REG_TCTL, E1000_TCTL_PSP);
    E1000PchFlush(Adapter);
    E1000PchDelay(10000);

    Blocked = E1000PchResetBlocked(Adapter);
    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
    if (!Blocked)
        Value |= E1000_CTRL_PHY_RST;
    Reserved = E1000PchAcquirePhy(Adapter);
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value | E1000_CTRL_RST);
    E1000PchDelay(20000);
    if (Reserved)
        InterlockedExchange(&Adapter->PchPhyBusy, 0);

    if (!Blocked)
    {
        if (E1000PchWaitConfigDone(Adapter) == 0xFFFFFFFF)
        {
            DPRINT1("E1000: Chipset LAN does not answer after the reset\n");
            return NDIS_STATUS_HARD_ERRORS;
        }

        if (!E1000PchResetBlocked(Adapter))
        {
            E1000PchDelay(10000);
            if (E1000PchAcquirePhy(Adapter))
            {
                if (E1000PchReadPhy(Adapter, E1000_PCH_PHY_PORT_GEN_CFG, &PhyValue))
                    E1000PchWritePhy(Adapter, E1000_PCH_PHY_PORT_GEN_CFG, PhyValue & ~E1000_PCH_PHY_PORT_GEN_CFG_HOST_WU);
                E1000PchReleasePhy(Adapter);
            }

            if (E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM) & E1000_PCH_FEXTNVM_SW_CONFIG)
                DPRINT1("E1000: Chipset LAN expects PHY settings from its NVM configuration region, which is not applied\n");
        }
    }

    E1000_WRITE_REG(Adapter, E1000_REG_IMC, 0xFFFFFFFF);
    E1000_READ_REG(Adapter, E1000_REG_ICR);

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_KABGTXD);
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_KABGTXD, Value | E1000_PCH_KABGTXD_BGSQLBIAS);

    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
    Value |= E1000_PCH_CTRL_EXT_RESERVED_22 | E1000_PCH_CTRL_EXT_PHYPDEN;
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value);

    Value = E1000_READ_REG(Adapter, E1000_REG_TXDCTL) | E1000_PCH_TXDCTL_COUNT_DESC;
    Value = (Value & ~E1000_PCH_TXDCTL_WTHRESH_MASK) | E1000_PCH_TXDCTL_FULL_TX_DESC_WB;
    Value = (Value & ~E1000_PCH_TXDCTL_PTHRESH_MASK) | E1000_PCH_TXDCTL_MAX_TX_DESC_PREFETCH;
    E1000_WRITE_REG(Adapter, E1000_REG_TXDCTL, Value);

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_TXDCTL1) | E1000_PCH_TXDCTL_COUNT_DESC;
    Value = (Value & ~E1000_PCH_TXDCTL_WTHRESH_MASK) | E1000_PCH_TXDCTL_FULL_TX_DESC_WB;
    Value = (Value & ~E1000_PCH_TXDCTL_PTHRESH_MASK) | E1000_PCH_TXDCTL_MAX_TX_DESC_PREFETCH;
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_TXDCTL1, Value);

    Value = E1000_READ_REG(Adapter, E1000_REG_TARC0);
    E1000_WRITE_REG(Adapter, E1000_REG_TARC0, Value | E1000_PCH_TARC0_INIT);

    Value = E1000_READ_REG(Adapter, E1000_REG_TARC1);
    if (E1000_READ_REG(Adapter, E1000_REG_TCTL) & E1000_TCTL_MULR)
        Value &= ~E1000_PCH_TARC1_SINGLE_REQUEST;
    else
        Value |= E1000_PCH_TARC1_SINGLE_REQUEST;
    E1000_WRITE_REG(Adapter, E1000_REG_TARC1, Value | E1000_PCH_TARC1_INIT);

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_RFCTL);
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_RFCTL, Value | E1000_PCH_RFCTL_NFSW_DIS | E1000_PCH_RFCTL_NFSR_DIS);

    Value = E1000_READ_REG(Adapter, E1000_PCH_REG_PBECCSTS);
    E1000_WRITE_REG(Adapter, E1000_PCH_REG_PBECCSTS, Value | E1000_PCH_PBECCSTS_ECC_ENABLE);

    for (Index = 0; Index < E1000_PCH_MTA_REG_COUNT; Index++)
        E1000_WRITE_REG_ARRAY(Adapter, E1000_REG_MTA, Index, 0);

    if (Adapter->PchGeneration >= E1000_PCH_TGP)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FFLT_DBG);
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_FFLT_DBG, Value | E1000_PCH_FFLT_DBG_DONT_GATE_WAKE_DMA_CLK);
    }

    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
    Value |= E1000_PCH_CTRL_EXT_RO_DIS | E1000_PCH_CTRL_EXT_DRV_LOAD;
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value);

    Adapter->CurrentPowerState = E1000PowerStateD0;
    Adapter->NdisPowerState = NdisDeviceStateD0;

    DPRINT1("E1000: Chipset LAN reset done, STATUS=0x%08lx CTRL=0x%08lx FWSM=0x%08lx\n",
            E1000_READ_REG(Adapter, E1000_REG_STATUS),
            E1000_READ_REG(Adapter, E1000_REG_CTRL),
            E1000_READ_REG(Adapter, E1000_REG_FWSM));
    return NDIS_STATUS_SUCCESS;
}

VOID
E1000PchSetupLink(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONG Value;
    USHORT PhyValue;

    if (Adapter->PchGeneration >= E1000_PCH_SPT && Adapter->InterruptMode == E1000InterruptModeLegacy)
    {
        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM7);
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_FEXTNVM7, Value | E1000_PCH_FEXTNVM7_SIDE_CLK_UNGATE);

        Value = E1000_READ_REG(Adapter, E1000_PCH_REG_FEXTNVM9);
        Value |= E1000_PCH_FEXTNVM9_IOSFSB_CLKGATE_DIS | E1000_PCH_FEXTNVM9_IOSFSB_CLKREQ_DIS;
        E1000_WRITE_REG(Adapter, E1000_PCH_REG_FEXTNVM9, Value);
    }

    if (!E1000PchResetBlocked(Adapter) && E1000PchAcquirePhy(Adapter))
    {
        if (E1000PchReadPhy(Adapter, E1000_PCH_MII_CFG, &PhyValue))
        {
            PhyValue |= E1000_PCH_CFG_ASSERT_CRS_ON_TX | E1000_PCH_CFG_ENABLE_DOWNSHIFT;
            E1000PchWritePhy(Adapter, E1000_PCH_MII_CFG, PhyValue);
        }

        if (E1000PchReadPhy(Adapter, E1000_PCH_MII_PHY_CTRL_2, &PhyValue))
        {
            PhyValue &= ~E1000_PCH_PHY_CTRL2_MDIX_CFG_MASK;
            PhyValue |= E1000_PCH_PHY_CTRL2_AUTO_MDI_MDIX;
            E1000PchWritePhy(Adapter, E1000_PCH_MII_PHY_CTRL_2, PhyValue);
        }

        if (E1000PchReadPhy(Adapter, E1000_PCH_MII_ADVERTISE, &PhyValue))
        {
            PhyValue &= ~(E1000_PCH_ADVERTISE_PAUSE_CAP | E1000_PCH_ADVERTISE_PAUSE_ASYM);
            PhyValue |= E1000_PCH_ADVERTISE_10HALF | E1000_PCH_ADVERTISE_10FULL |
                        E1000_PCH_ADVERTISE_100HALF | E1000_PCH_ADVERTISE_100FULL;
            E1000PchWritePhy(Adapter, E1000_PCH_MII_ADVERTISE, PhyValue);
        }

        if (E1000PchReadPhy(Adapter, E1000_PCH_MII_CTRL1000, &PhyValue))
        {
            PhyValue &= ~E1000_PCH_ADVERTISE_1000HALF;
            PhyValue |= E1000_PCH_ADVERTISE_1000FULL;
            E1000PchWritePhy(Adapter, E1000_PCH_MII_CTRL1000, PhyValue);
        }

        if (E1000PchReadPhy(Adapter, E1000_PCH_MII_BMCR, &PhyValue))
            E1000PchWritePhy(Adapter, E1000_PCH_MII_BMCR, PhyValue | E1000_PCH_BMCR_ANENABLE | E1000_PCH_BMCR_ANRESTART);

        E1000PchReleasePhy(Adapter);
    }

    E1000PchLinkChanged(Adapter);
}

VOID
E1000PchSetMulticastList(
    _In_ PE1000_ADAPTER Adapter,
    _In_reads_bytes_(MulticastCount * ETH_LENGTH_OF_ADDRESS) PUCHAR MulticastList,
    _In_ ULONG MulticastCount)
{
    ULONG Table[E1000_PCH_MTA_REG_COUNT];
    ULONG Index, Hash;
    PUCHAR Address;

    RtlZeroMemory(Table, sizeof(Table));
    for (Index = 0; Index < MulticastCount; Index++)
    {
        Address = MulticastList + Index * ETH_LENGTH_OF_ADDRESS;
        Hash = ((Address[4] >> 6) | ((ULONG)Address[5] << 2)) & (E1000_PCH_MTA_REG_COUNT * 32 - 1);
        Table[(Hash >> 5) & (E1000_PCH_MTA_REG_COUNT - 1)] |= 1UL << (Hash & 0x1F);
    }

    for (Index = 0; Index < E1000_PCH_MTA_REG_COUNT; Index++)
        E1000_WRITE_REG_ARRAY(Adapter, E1000_REG_MTA, Index, Table[Index]);
}

VOID
E1000PchHalt(
    _Inout_ PE1000_ADAPTER Adapter)
{
    ULONGLONG Deadline = E1000PchDeadline(E1000_PCH_HALT_TIMEOUT_MS);
    ULONG Value;

    Adapter->PchHalting = TRUE;

    while (Adapter->PchLinkWorkState != E1000_PCH_LINK_WORK_IDLE && KeQueryInterruptTime() < Deadline)
        NdisMSleep(1000);

    if (Adapter->PchLinkWorkItem != NULL && Adapter->PchLinkWorkState == E1000_PCH_LINK_WORK_IDLE)
    {
        NdisFreeIoWorkItem(Adapter->PchLinkWorkItem);
        Adapter->PchLinkWorkItem = NULL;
    }

    if (Adapter->IoBase != NULL)
    {
        Value = E1000_READ_REG(Adapter, E1000_REG_CTRL_EXT);
        E1000_WRITE_REG(Adapter, E1000_REG_CTRL_EXT, Value & ~E1000_PCH_CTRL_EXT_DRV_LOAD);
    }
}
