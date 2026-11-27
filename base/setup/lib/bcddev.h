/*
 * PROJECT:     LiberNT Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Partition device elements of the boot configuration data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BCD_DEVICE_ELEMENT_SIZE     0x58
#define BCD_DEVICE_TYPE_OFFSET      0x10
#define BCD_DEVICE_SIZE_OFFSET      0x18
#define BCD_DEVICE_PARTITION_OFFSET 0x20
#define BCD_DEVICE_STYLE_OFFSET     0x34
#define BCD_DEVICE_DISK_OFFSET      0x38
#define BCD_DEVICE_PARTITION        6
#define BCD_DEVICE_BODY_SIZE        0x48
#define BCD_PARTITION_STYLE_MBR     1

BOOLEAN
IsBcdPartitionElementPresent(
    _In_reads_bytes_(BCD_DEVICE_ELEMENT_SIZE) const UCHAR *Element);
