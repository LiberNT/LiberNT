/*
 * PROJECT:     LiberNT Windows Firewall service
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Load firewall rules and translate them into WFP filters
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <windows.h>
#include <initguid.h>
#include <fwpmu.h>
#include <fwpmk.h>

#include "mpssvc_policy.h"

DWORD
MpsApplyFirewallRule(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey,
    _In_ const MPS_FIREWALL_RULE *Rule)
{
    FWPM_FILTER0 Filter;
    FWPM_FILTER_CONDITION0 Conditions[4 + 2 * MPS_MAX_ADDRESSES];
    FWP_RANGE0 LocalRange, RemoteRange;
    FWP_V4_ADDR_AND_MASK LocalV4[MPS_MAX_ADDRESSES], RemoteV4[MPS_MAX_ADDRESSES];
    FWP_BYTE_BLOB *AppId = NULL;
    UINT32 LocalCount, RemoteCount, LocalIndex, RemoteIndex;
    UINT32 ConditionCount, Index;
    DWORD Error;

    if (Engine == NULL || SubLayerKey == NULL || Rule == NULL)
        return ERROR_INVALID_PARAMETER;

    if (!Rule->Active)
        return ERROR_SUCCESS;

    if (Rule->Unsupported || Rule->ProfileMask != 7 ||
        Rule->Action == MpsRuleActionBypass ||
        Rule->LocalPortCount > MPS_MAX_PORTS ||
        Rule->RemotePortCount > MPS_MAX_PORTS ||
        Rule->LocalAddressCount > MPS_MAX_ADDRESSES ||
        Rule->RemoteAddressCount > MPS_MAX_ADDRESSES ||
        (Rule->Protocol != 256 && Rule->Protocol > 255) ||
        ((Rule->LocalPortCount != 0 || Rule->RemotePortCount != 0) &&
         Rule->Protocol != 6 && Rule->Protocol != 17))
        return ERROR_NOT_SUPPORTED;

    Error = ERROR_SUCCESS;
    if (Rule->HasApplication)
    {
        WCHAR ExpandedApp[MPS_MAX_APP_PATH];
        DWORD ExpandedLength;

        ExpandedLength = ExpandEnvironmentStringsW(Rule->Application,
                                                   ExpandedApp,
                                                   ARRAYSIZE(ExpandedApp));
        if (ExpandedLength == 0 || ExpandedLength > ARRAYSIZE(ExpandedApp))
            return ERROR_NOT_SUPPORTED;

        Error = FwpmGetAppIdFromFileName0(ExpandedApp, &AppId);
        if (Error != ERROR_SUCCESS)
            return ERROR_NOT_SUPPORTED;
    }

    ZeroMemory(&Filter, sizeof(Filter));
    ZeroMemory(Conditions, sizeof(Conditions));
    Filter.displayData.name = L"LiberNT firewall rule";
    Filter.layerKey = Rule->Inbound ? FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4 :
                                      FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    Filter.subLayerKey = *SubLayerKey;
    Filter.action.type = Rule->Action == MpsRuleActionBlock ? FWP_ACTION_BLOCK :
                                                              FWP_ACTION_PERMIT;
    Filter.weight.type = FWP_UINT8;
    Filter.weight.uint8 = Rule->Action == MpsRuleActionBlock ? 1 : 0;
    Filter.filterCondition = Conditions;

    ConditionCount = 0;
    if (Rule->Protocol != 256)
    {
        Conditions[ConditionCount].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
        Conditions[ConditionCount].matchType = FWP_MATCH_EQUAL;
        Conditions[ConditionCount].conditionValue.type = FWP_UINT8;
        Conditions[ConditionCount].conditionValue.uint8 = (UINT8)Rule->Protocol;
        ConditionCount++;
    }

    if (Rule->HasApplication)
    {
        Conditions[ConditionCount].fieldKey = FWPM_CONDITION_ALE_APP_ID;
        Conditions[ConditionCount].matchType = FWP_MATCH_EQUAL;
        Conditions[ConditionCount].conditionValue.type = FWP_BYTE_BLOB_TYPE;
        Conditions[ConditionCount].conditionValue.byteBlob = AppId;
        ConditionCount++;
    }

    for (Index = 0; Index < Rule->LocalAddressCount; Index++)
    {
        Conditions[ConditionCount].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;
        Conditions[ConditionCount].matchType = FWP_MATCH_EQUAL;
        if (Rule->LocalAddresses[Index].Mask == 0xFFFFFFFF)
        {
            Conditions[ConditionCount].conditionValue.type = FWP_UINT32;
            Conditions[ConditionCount].conditionValue.uint32 = Rule->LocalAddresses[Index].Address;
        }
        else
        {
            LocalV4[Index].addr = Rule->LocalAddresses[Index].Address;
            LocalV4[Index].mask = Rule->LocalAddresses[Index].Mask;
            Conditions[ConditionCount].conditionValue.type = FWP_V4_ADDR_MASK;
            Conditions[ConditionCount].conditionValue.v4AddrMask = &LocalV4[Index];
        }
        ConditionCount++;
    }

    for (Index = 0; Index < Rule->RemoteAddressCount; Index++)
    {
        Conditions[ConditionCount].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
        Conditions[ConditionCount].matchType = FWP_MATCH_EQUAL;
        if (Rule->RemoteAddresses[Index].Mask == 0xFFFFFFFF)
        {
            Conditions[ConditionCount].conditionValue.type = FWP_UINT32;
            Conditions[ConditionCount].conditionValue.uint32 = Rule->RemoteAddresses[Index].Address;
        }
        else
        {
            RemoteV4[Index].addr = Rule->RemoteAddresses[Index].Address;
            RemoteV4[Index].mask = Rule->RemoteAddresses[Index].Mask;
            Conditions[ConditionCount].conditionValue.type = FWP_V4_ADDR_MASK;
            Conditions[ConditionCount].conditionValue.v4AddrMask = &RemoteV4[Index];
        }
        ConditionCount++;
    }

    Filter.numFilterConditions = ConditionCount;
    LocalCount = Rule->LocalPortCount == 0 ? 1 : Rule->LocalPortCount;
    RemoteCount = Rule->RemotePortCount == 0 ? 1 : Rule->RemotePortCount;

    if (LocalCount * RemoteCount > 64)
    {
        Error = ERROR_NOT_SUPPORTED;
        goto Exit;
    }

    for (LocalIndex = 0; LocalIndex < LocalCount; LocalIndex++)
    {
        for (RemoteIndex = 0; RemoteIndex < RemoteCount; RemoteIndex++)
        {
            UINT32 PortCondition = ConditionCount;
            UINT64 Id;

            if (Rule->LocalPortCount != 0)
            {
                Conditions[PortCondition].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
                Conditions[PortCondition].matchType = FWP_MATCH_RANGE;
                Conditions[PortCondition].conditionValue.type = FWP_RANGE_TYPE;
                Conditions[PortCondition].conditionValue.rangeValue = &LocalRange;
                LocalRange.valueLow.type = FWP_UINT16;
                LocalRange.valueLow.uint16 = Rule->LocalPorts[LocalIndex].First;
                LocalRange.valueHigh.type = FWP_UINT16;
                LocalRange.valueHigh.uint16 = Rule->LocalPorts[LocalIndex].Last;
                PortCondition++;
            }

            if (Rule->RemotePortCount != 0)
            {
                Conditions[PortCondition].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
                Conditions[PortCondition].matchType = FWP_MATCH_RANGE;
                Conditions[PortCondition].conditionValue.type = FWP_RANGE_TYPE;
                Conditions[PortCondition].conditionValue.rangeValue = &RemoteRange;
                RemoteRange.valueLow.type = FWP_UINT16;
                RemoteRange.valueLow.uint16 = Rule->RemotePorts[RemoteIndex].First;
                RemoteRange.valueHigh.type = FWP_UINT16;
                RemoteRange.valueHigh.uint16 = Rule->RemotePorts[RemoteIndex].Last;
                PortCondition++;
            }

            Filter.numFilterConditions = PortCondition;
            Error = FwpmFilterAdd0(Engine, &Filter, NULL, &Id);
            if (Error != ERROR_SUCCESS)
                goto Exit;
        }
    }

Exit:
    if (AppId != NULL)
        FwpmFreeMemory0((void **)&AppId);
    return Error;
}

DWORD
MpsAddFirewallSubLayer(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey)
{
    FWPM_SUBLAYER0 SubLayer;
    DWORD Error;

    if (Engine == NULL || SubLayerKey == NULL)
        return ERROR_INVALID_PARAMETER;

    ZeroMemory(&SubLayer, sizeof(SubLayer));
    SubLayer.subLayerKey = *SubLayerKey;
    SubLayer.displayData.name = L"Windows Defender Firewall Sublayer";
    SubLayer.weight = 0x8000;
    Error = FwpmSubLayerAdd0(Engine, &SubLayer, NULL);
    if (Error == (DWORD)FWP_E_ALREADY_EXISTS)
        Error = ERROR_SUCCESS;

    return Error;
}

DWORD
MpsAddLoopbackPermit(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey)
{
    FWPM_FILTER0 Filter;
    FWPM_FILTER_CONDITION0 Condition;
    UINT64 Id;

    if (Engine == NULL || SubLayerKey == NULL)
        return ERROR_INVALID_PARAMETER;

    ZeroMemory(&Condition, sizeof(Condition));
    Condition.fieldKey = FWPM_CONDITION_FLAGS;
    Condition.matchType = FWP_MATCH_FLAGS_ALL_SET;
    Condition.conditionValue.type = FWP_UINT32;
    Condition.conditionValue.uint32 = FWP_CONDITION_FLAG_IS_LOOPBACK;

    ZeroMemory(&Filter, sizeof(Filter));
    Filter.displayData.name = L"LiberNT firewall loopback";
    Filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    Filter.subLayerKey = *SubLayerKey;
    Filter.action.type = FWP_ACTION_PERMIT;
    Filter.weight.type = FWP_UINT8;
    Filter.weight.uint8 = 15;
    Filter.numFilterConditions = 1;
    Filter.filterCondition = &Condition;
    return FwpmFilterAdd0(Engine, &Filter, NULL, &Id);
}

DWORD
MpsLoadFirewallRulesFromKey(
    _In_ HANDLE Engine,
    _In_ const GUID *SubLayerKey,
    _In_ HKEY Key)
{
    DWORD Error;
    DWORD ValueCount = 0;
    DWORD Index;

    if (Engine == NULL || SubLayerKey == NULL || Key == NULL)
        return ERROR_INVALID_PARAMETER;

    Error = RegQueryInfoKeyW(Key, NULL, NULL, NULL, NULL, NULL, NULL, &ValueCount, NULL, NULL, NULL, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (ValueCount > 4096)
        return ERROR_BUFFER_OVERFLOW;

    Error = FwpmTransactionBegin0(Engine, 0);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (Index = 0; Index < ValueCount; Index++)
    {
        WCHAR ValueName[128];
        WCHAR Text[MPS_MAX_RULE_CHARS + 1];
        DWORD NameLength = ARRAYSIZE(ValueName);
        DWORD DataSize = sizeof(Text);
        DWORD Type;
        SIZE_T CharacterCount, CharacterIndex;
        MPS_FIREWALL_RULE Rule;
        DWORD RuleError;

        Error = RegEnumValueW(Key, Index, ValueName, &NameLength, NULL, &Type, (LPBYTE)Text, &DataSize);
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            Error = ERROR_SUCCESS;
            break;
        }
        if (Error == ERROR_MORE_DATA)
        {
            Error = ERROR_SUCCESS;
            continue;
        }
        if (Error != ERROR_SUCCESS)
            break;
        if (Type != REG_SZ || DataSize < sizeof(WCHAR) ||
            DataSize > sizeof(Text) || (DataSize % sizeof(WCHAR)) != 0)
            continue;

        CharacterCount = DataSize / sizeof(WCHAR);
        if (Text[CharacterCount - 1] != UNICODE_NULL)
            continue;
        for (CharacterIndex = 0; CharacterIndex + 1 < CharacterCount; CharacterIndex++)
        {
            if (Text[CharacterIndex] == UNICODE_NULL)
                break;
        }
        if (CharacterIndex + 1 < CharacterCount)
            continue;

        RuleError = MpsParseFirewallRule(Text, CharacterCount - 1, &Rule);
        if (RuleError != ERROR_SUCCESS)
            continue;
        RuleError = MpsApplyFirewallRule(Engine, SubLayerKey, &Rule);
        if (RuleError != ERROR_SUCCESS && RuleError != ERROR_NOT_SUPPORTED)
        {
            Error = RuleError;
            break;
        }
    }

    if (Error == ERROR_SUCCESS)
        Error = FwpmTransactionCommit0(Engine);
    if (Error != ERROR_SUCCESS)
        FwpmTransactionAbort0(Engine);

    return Error;
}
