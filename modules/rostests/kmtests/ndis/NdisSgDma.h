/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Values shared by the scatter/gather DMA test miniport and its user-mode test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#pragma once

#define NDISSG_HARDWARE_ID L"PCI\\VEN_1234&DEV_11E8"
#define NDISSG_PARAMETERS_KEY L"SYSTEM\\CurrentControlSet\\Services\\ndissg_drv\\Parameters"
#define NDISSG_UNSET 0xA5A5A5A5UL
#define NDISSG_MAPPING_COUNT 2

static const ULONG NdisSgMappings[NDISSG_MAPPING_COUNT] = { 1514, 0x10000 };
static const PCWSTR NdisSgStatusNames[NDISSG_MAPPING_COUNT] = { L"Status0", L"Status1" };
static const PCWSTR NdisSgSizeNames[NDISSG_MAPPING_COUNT] = { L"Size0", L"Size1" };
static const PCWSTR NdisSgRequiredNames[NDISSG_MAPPING_COUNT] = { L"Required0", L"Required1" };
