/*
 * PROJECT:     LiberNT GPT Support Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Identifiers and default names of new GPT partitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

BOOLEAN
NTAPI
GptCreatePartitionId(
    _Out_ GUID* PartitionId);

PCWSTR
NTAPI
GptDefaultPartitionName(
    _In_ const GUID* PartitionType);

#ifdef __cplusplus
}
#endif
