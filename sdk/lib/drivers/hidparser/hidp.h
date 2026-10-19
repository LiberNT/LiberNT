/*
 * HID parsing library
 *
 * Copyright 2015 Aric Stewart
 * Copyright 2021 Rémi Bernon for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidusage.h>
#include <hidpddi.h>
#include <hidpi.h>

#define HIDP_TAG 'PdiH'

struct hid_collection_node
{
    USAGE usage;
    USAGE usage_page;
    USHORT parent;
    USHORT number_of_children;
    USHORT next_sibling;
    USHORT first_child;
    ULONG collection_type;
};

struct hid_value_caps
{
    USHORT usage_page;
    UCHAR report_id;
    UCHAR start_bit;
    USHORT bit_size;
    USHORT report_count;
    USHORT start_byte;
    USHORT total_bits;
    ULONG bit_field;
    USHORT end_byte;
    USHORT link_collection;
    USAGE link_usage_page;
    USAGE link_usage;
    ULONG flags;
    ULONG padding[8];
    USAGE usage_min;
    USAGE usage_max;
    USHORT string_min;
    USHORT string_max;
    USHORT designator_min;
    USHORT designator_max;
    USHORT data_index_min;
    USHORT data_index_max;
    USHORT null_value;
    USHORT unknown;
    LONG logical_min;
    LONG logical_max;
    LONG physical_min;
    LONG physical_max;
    LONG units;
    LONG units_exp;
};

#define HID_VALUE_CAPS_ARRAY_HAS_MORE       0x01
#define HID_VALUE_CAPS_IS_CONSTANT          0x02
#define HID_VALUE_CAPS_IS_BUTTON            0x04
#define HID_VALUE_CAPS_IS_ABSOLUTE          0x08
#define HID_VALUE_CAPS_IS_RANGE             0x10
#define HID_VALUE_CAPS_IS_STRING_RANGE      0x40
#define HID_VALUE_CAPS_IS_DESIGNATOR_RANGE  0x80

#define HID_VALUE_CAPS_HAS_NULL(x) (((x)->bit_field & 0x40) != 0)
#define HID_VALUE_CAPS_IS_ARRAY(c) (((c)->bit_field & 2) == 0)

struct hid_preparsed_data
{
    char magic[8];
    USAGE usage;
    USAGE usage_page;
    USHORT unknown[2];
    USHORT input_caps_start;
    USHORT input_caps_count;
    USHORT input_caps_end;
    USHORT input_report_byte_length;
    USHORT output_caps_start;
    USHORT output_caps_count;
    USHORT output_caps_end;
    USHORT output_report_byte_length;
    USHORT feature_caps_start;
    USHORT feature_caps_count;
    USHORT feature_caps_end;
    USHORT feature_report_byte_length;
    USHORT caps_size;
    USHORT number_link_collection_nodes;
    struct hid_value_caps value_caps[1];
};

C_ASSERT(FIELD_OFFSET(struct hid_preparsed_data, value_caps) == 44);
C_ASSERT(sizeof(struct hid_value_caps) == 104);
C_ASSERT(sizeof(struct hid_collection_node) == 16);

#define HID_INPUT_VALUE_CAPS(d) ((d)->value_caps + (d)->input_caps_start)
#define HID_OUTPUT_VALUE_CAPS(d) ((d)->value_caps + (d)->output_caps_start)
#define HID_FEATURE_VALUE_CAPS(d) ((d)->value_caps + (d)->feature_caps_start)
#define HID_COLLECTION_NODES(d) (struct hid_collection_node *)((char *)(d)->value_caps + (d)->caps_size)

NTSTATUS
HidParser_TranslateKbdUsage(
    IN USAGE Usage,
    IN HIDP_KEYBOARD_DIRECTION KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE ModifierState,
    IN PHIDP_INSERT_SCANCODES InsertCodesProcedure,
    IN PVOID InsertCodesContext);

NTSTATUS
HidParser_TranslateCustUsage(
    IN USAGE Usage,
    IN HIDP_KEYBOARD_DIRECTION KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE ModifierState,
    IN PHIDP_INSERT_SCANCODES InsertCodesProcedure,
    IN PVOID InsertCodesContext);

NTSTATUS
NTAPI
HidParser_UsageListDifference(
    IN PUSAGE PreviousUsageList,
    IN PUSAGE CurrentUsageList,
    OUT PUSAGE BreakUsageList,
    OUT PUSAGE MakeUsageList,
    IN ULONG UsageListLength);

NTSTATUS
NTAPI
HidParser_UsageAndPageListDifference(
    IN PUSAGE_AND_PAGE PreviousUsageList,
    IN PUSAGE_AND_PAGE CurrentUsageList,
    OUT PUSAGE_AND_PAGE BreakUsageList,
    OUT PUSAGE_AND_PAGE MakeUsageList,
    IN ULONG UsageListLength);

NTSTATUS
NTAPI
HidParser_TranslateUsageAndPagesToI8042ScanCodes(
    IN PUSAGE_AND_PAGE ChangedUsageList,
    IN ULONG UsageListLength,
    IN HIDP_KEYBOARD_DIRECTION KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE ModifierState,
    IN PHIDP_INSERT_SCANCODES InsertCodesProcedure,
    IN PVOID InsertCodesContext);
