/*
 * HID parsing library
 *
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

#include "hidp.h"

#define NDEBUG
#include <debug.h>

enum
{
    INPUT_DATA_CONST = 0x01,
    INPUT_ARRAY_VAR = 0x02,
    INPUT_ABS_REL = 0x04,
    INPUT_WRAP = 0x08,
    INPUT_LINEAR = 0x10,
    INPUT_PREFSTATE = 0x20,
    INPUT_NULL = 0x40,
    INPUT_VOLATILE = 0x80,
    INPUT_BITFIELD = 0x100
};

enum
{
    TAG_TYPE_MAIN = 0x0,
    TAG_TYPE_GLOBAL,
    TAG_TYPE_LOCAL,
    TAG_TYPE_RESERVED,
};

enum
{
    TAG_MAIN_INPUT = 0x08,
    TAG_MAIN_OUTPUT = 0x09,
    TAG_MAIN_FEATURE = 0x0B,
    TAG_MAIN_COLLECTION = 0x0A,
    TAG_MAIN_END_COLLECTION = 0x0C
};

enum
{
    TAG_GLOBAL_USAGE_PAGE = 0x0,
    TAG_GLOBAL_LOGICAL_MINIMUM,
    TAG_GLOBAL_LOGICAL_MAXIMUM,
    TAG_GLOBAL_PHYSICAL_MINIMUM,
    TAG_GLOBAL_PHYSICAL_MAXIMUM,
    TAG_GLOBAL_UNIT_EXPONENT,
    TAG_GLOBAL_UNIT,
    TAG_GLOBAL_REPORT_SIZE,
    TAG_GLOBAL_REPORT_ID,
    TAG_GLOBAL_REPORT_COUNT,
    TAG_GLOBAL_PUSH,
    TAG_GLOBAL_POP
};

enum
{
    TAG_LOCAL_USAGE = 0x0,
    TAG_LOCAL_USAGE_MINIMUM,
    TAG_LOCAL_USAGE_MAXIMUM,
    TAG_LOCAL_DESIGNATOR_INDEX,
    TAG_LOCAL_DESIGNATOR_MINIMUM,
    TAG_LOCAL_DESIGNATOR_MAXIMUM,
    TAG_LOCAL_STRING_INDEX = 0x7,
    TAG_LOCAL_STRING_MINIMUM,
    TAG_LOCAL_STRING_MAXIMUM,
    TAG_LOCAL_DELIMITER
};

#define SHORT_ITEM(tag, type) (((tag) << 4) | ((type) << 2))

struct hid_parser_state
{
    USAGE usage;
    USAGE usage_page;
    USHORT number_link_collection_nodes;

    USAGE usages_page[256];
    USAGE usages_min[256];
    USAGE usages_max[256];
    ULONG usages_size;

    struct hid_value_caps items;
    ULONG report_count;
    ULONG bit_size;

    struct hid_value_caps *stack;
    ULONG stack_size;
    ULONG global_idx;
    ULONG collection_idx;

    struct hid_value_caps *collections;
    ULONG collections_size;

    struct hid_value_caps *values[3];
    ULONG values_size[3];

    ULONG bit_offset[3][256];
    USHORT byte_length[3];
    USHORT caps_count[3];
    USHORT empty_caps[3];
    USHORT data_count[3];

    NTSTATUS status;
};

static PVOID HidpAllocate(POOL_TYPE pool_type, SIZE_T size)
{
    PVOID ptr = ExAllocatePoolWithTag(pool_type, size, HIDP_TAG);
    if (ptr) RtlZeroMemory(ptr, size);
    return ptr;
}

static VOID HidpFree(PVOID ptr)
{
    if (ptr) ExFreePoolWithTag(ptr, HIDP_TAG);
}

static BOOLEAN parser_fail(struct hid_parser_state *state, NTSTATUS status)
{
    if (NT_SUCCESS(state->status)) state->status = status;
    return FALSE;
}

static BOOLEAN array_reserve(struct hid_parser_state *state, struct hid_value_caps **array, ULONG *array_size, ULONG index)
{
    struct hid_value_caps *new_array;
    ULONG new_size;

    if (index < *array_size) return TRUE;
    new_size = *array_size ? (*array_size * 3 / 2) : 32;
    if (new_size <= index || new_size > MAXUSHORT) return parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
    if (!(new_array = HidpAllocate(PagedPool, new_size * sizeof(**array)))) return parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
    if (*array)
    {
        RtlCopyMemory(new_array, *array, *array_size * sizeof(**array));
        HidpFree(*array);
    }
    *array = new_array;
    *array_size = new_size;
    return TRUE;
}

static void copy_global_items(struct hid_value_caps *dst, const struct hid_value_caps *src)
{
    dst->usage_page = src->usage_page;
    dst->logical_min = src->logical_min;
    dst->logical_max = src->logical_max;
    dst->physical_min = src->physical_min;
    dst->physical_max = src->physical_max;
    dst->units_exp = src->units_exp;
    dst->units = src->units;
    dst->bit_size = src->bit_size;
    dst->report_id = src->report_id;
    dst->report_count = src->report_count;
}

static void copy_collection_items(struct hid_value_caps *dst, const struct hid_value_caps *src)
{
    dst->link_collection = src->link_collection;
    dst->link_usage_page = src->link_usage_page;
    dst->link_usage = src->link_usage;
}

static void reset_local_items(struct hid_parser_state *state)
{
    struct hid_value_caps tmp;
    copy_global_items(&tmp, &state->items);
    copy_collection_items(&tmp, &state->items);
    RtlZeroMemory(&state->items, sizeof(state->items));
    copy_global_items(&state->items, &tmp);
    copy_collection_items(&state->items, &tmp);
    RtlZeroMemory(&state->usages_page, sizeof(state->usages_page));
    RtlZeroMemory(&state->usages_min, sizeof(state->usages_min));
    RtlZeroMemory(&state->usages_max, sizeof(state->usages_max));
    state->usages_size = 0;
}

static BOOLEAN parse_global_push(struct hid_parser_state *state)
{
    if (!array_reserve(state, &state->stack, &state->stack_size, state->global_idx)) return FALSE;

    copy_global_items(state->stack + state->global_idx, &state->items);
    state->global_idx++;
    return TRUE;
}

static BOOLEAN parse_global_pop(struct hid_parser_state *state)
{
    if (!state->global_idx) return parser_fail(state, STATUS_COULD_NOT_INTERPRET);

    state->global_idx--;
    copy_global_items(&state->items, state->stack + state->global_idx);
    return TRUE;
}

static BOOLEAN parse_local_usage(struct hid_parser_state *state, USAGE usage_page, USAGE usage)
{
    if (!usage_page) usage_page = state->items.usage_page;
    if (state->items.flags & HID_VALUE_CAPS_IS_RANGE) state->usages_size = 0;
    if (state->usages_size >= 255) return parser_fail(state, STATUS_COULD_NOT_INTERPRET);
    state->usages_page[state->usages_size] = usage_page;
    state->usages_min[state->usages_size] = usage;
    state->usages_max[state->usages_size] = usage;
    state->items.usage_min = usage;
    state->items.usage_max = usage;
    state->items.flags &= ~HID_VALUE_CAPS_IS_RANGE;
    state->usages_size++;
    return TRUE;
}

static void parse_local_usage_min(struct hid_parser_state *state, USAGE usage_page, USAGE usage)
{
    if (!usage_page) usage_page = state->items.usage_page;
    if (!(state->items.flags & HID_VALUE_CAPS_IS_RANGE)) state->usages_max[0] = 0;
    state->usages_page[0] = usage_page;
    state->usages_min[0] = usage;
    state->items.usage_min = usage;
    state->items.flags |= HID_VALUE_CAPS_IS_RANGE;
    state->usages_size = 1;
}

static void parse_local_usage_max(struct hid_parser_state *state, USAGE usage_page, USAGE usage)
{
    if (!usage_page) usage_page = state->items.usage_page;
    if (!(state->items.flags & HID_VALUE_CAPS_IS_RANGE)) state->usages_min[0] = 0;
    state->usages_page[0] = usage_page;
    state->usages_max[0] = usage;
    state->items.usage_max = usage;
    state->items.flags |= HID_VALUE_CAPS_IS_RANGE;
    state->usages_size = 1;
}

static BOOLEAN parse_new_collection(struct hid_parser_state *state)
{
    if (!array_reserve(state, &state->stack, &state->stack_size, state->collection_idx)) return FALSE;
    if (!array_reserve(state, &state->collections, &state->collections_size, state->number_link_collection_nodes)) return FALSE;

    copy_collection_items(state->stack + state->collection_idx, &state->items);
    state->collection_idx++;

    state->items.usage_min = state->usages_min[0];
    state->items.usage_max = state->usages_max[0];

    state->collections[state->number_link_collection_nodes] = state->items;
    state->items.link_collection = state->number_link_collection_nodes;
    state->items.link_usage_page = state->items.usage_page;
    state->items.link_usage = state->items.usage_min;
    if (!state->number_link_collection_nodes)
    {
        state->usage_page = state->items.usage_page;
        state->usage = state->items.usage_min;
    }
    state->number_link_collection_nodes++;

    reset_local_items(state);
    return TRUE;
}

static BOOLEAN parse_end_collection(struct hid_parser_state *state)
{
    if (!state->collection_idx) return parser_fail(state, STATUS_COULD_NOT_INTERPRET);

    state->collection_idx--;
    copy_collection_items(&state->items, state->stack + state->collection_idx);
    reset_local_items(state);
    return TRUE;
}

static void add_new_value_caps(struct hid_parser_state *state, struct hid_value_caps *values, LONG i, ULONG start_bit)
{
    ULONG count, usages_size = state->usages_size ? state->usages_size : 1;

    state->items.start_byte = (USHORT)(start_bit / 8);
    state->items.start_bit = (UCHAR)(start_bit % 8);
    state->items.total_bits = state->items.report_count * state->items.bit_size;
    state->items.end_byte = (USHORT)((start_bit + state->items.total_bits + 7) / 8);
    state->items.usage_page = state->usages_page[usages_size - 1 - i];
    state->items.usage_min = state->usages_min[usages_size - 1 - i];
    state->items.usage_max = state->usages_max[usages_size - 1 - i];
    if (!state->items.usage_max && !state->items.usage_min) count = (ULONG)-1;
    else count = state->items.usage_max - state->items.usage_min;
    state->items.data_index_min = state->items.data_index_max + 1;
    state->items.data_index_max = (USHORT)(state->items.data_index_min + count);
    values[i] = state->items;

    if (values[i].flags & HID_VALUE_CAPS_IS_BUTTON)
    {
        if (!HID_VALUE_CAPS_IS_ARRAY(values + i)) values[i].logical_min = 0;
        else values[i].logical_min = values[i].logical_max;
        values[i].logical_max = 0;
        values[i].physical_min = 0;
        values[i].physical_max = 0;
    }
}

static BOOLEAN parse_new_value_caps(struct hid_parser_state *state, HIDP_REPORT_TYPE type)
{
    struct hid_value_caps *values;
    USAGE usage_page = state->items.usage_page;
    USHORT report_count;
    ULONG i, usages_size = state->usages_size ? state->usages_size : 1;
    USHORT *byte_length = &state->byte_length[type];
    ULONG start_bit, *bit_offset = &state->bit_offset[type][state->items.report_id];
    ULONGLONG total_bits;
    BOOLEAN is_array;

    if (state->report_count > MAXUSHORT || state->bit_size > MAXUSHORT) return parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
    state->items.report_count = (USHORT)state->report_count;
    state->items.bit_size = (USHORT)state->bit_size;
    report_count = state->items.report_count;

    if (!*bit_offset) *bit_offset = 8;
    total_bits = (ULONGLONG)*bit_offset + (ULONGLONG)state->items.bit_size * state->items.report_count;
    if (total_bits + 7 > (ULONGLONG)MAXUSHORT * 8) return parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
    *bit_offset = (ULONG)total_bits;
    if (*byte_length < (*bit_offset + 7) / 8) *byte_length = (USHORT)((*bit_offset + 7) / 8);
    start_bit = *bit_offset;

    if (!state->usages_size && (state->items.bit_field & INPUT_DATA_CONST))
    {
        reset_local_items(state);
        return TRUE;
    }

    if (!state->items.report_count)
    {
        state->empty_caps[type] += (USHORT)usages_size;
        reset_local_items(state);
        return TRUE;
    }

    if (!array_reserve(state, &state->values[type], &state->values_size[type], state->caps_count[type] + usages_size)) return FALSE;
    values = state->values[type] + state->caps_count[type];

    if (!(is_array = HID_VALUE_CAPS_IS_ARRAY(&state->items)))
    {
        if (state->items.report_count < usages_size) return parser_fail(state, STATUS_COULD_NOT_INTERPRET);
        state->items.report_count -= (USHORT)(usages_size - 1);
    }
    else start_bit -= state->items.report_count * state->items.bit_size;

    if (!(state->items.bit_field & INPUT_ABS_REL)) state->items.flags |= HID_VALUE_CAPS_IS_ABSOLUTE;
    if (state->items.bit_field & INPUT_DATA_CONST) state->items.flags |= HID_VALUE_CAPS_IS_CONSTANT;
    if (state->items.bit_size == 1 || is_array) state->items.flags |= HID_VALUE_CAPS_IS_BUTTON;

    if (is_array) state->items.null_value = (USHORT)state->items.logical_min;
    else if (!(state->items.bit_field & INPUT_NULL)) state->items.null_value = 0;
    else state->items.null_value = 1;

    state->items.data_index_max = state->data_count[type] - 1;
    for (i = 0; i < usages_size; ++i)
    {
        if (!is_array) start_bit -= state->items.report_count * state->items.bit_size;
        else if (i) state->items.flags |= HID_VALUE_CAPS_ARRAY_HAS_MORE;
        else state->items.flags &= ~HID_VALUE_CAPS_ARRAY_HAS_MORE;
        add_new_value_caps(state, values, is_array ? usages_size - i - 1 : i, start_bit);
        if (!is_array) state->items.report_count = 1;
    }
    state->caps_count[type] += (USHORT)usages_size;
    state->data_count[type] = state->items.data_index_max + 1;

    state->items.usage_page = usage_page;
    state->items.report_count = report_count;
    reset_local_items(state);
    return TRUE;
}

static void free_parser_state(struct hid_parser_state *state)
{
    HidpFree(state->stack);
    HidpFree(state->collections);
    HidpFree(state->values[HidP_Input]);
    HidpFree(state->values[HidP_Output]);
    HidpFree(state->values[HidP_Feature]);
    HidpFree(state);
}

static struct hid_preparsed_data *build_preparsed_data(struct hid_parser_state *state, POOL_TYPE pool_type, PULONG data_size)
{
    struct hid_collection_node *nodes;
    struct hid_preparsed_data *data;
    struct hid_value_caps *caps;
    ULONG i, size, caps_size;

    caps_size = state->caps_count[HidP_Input] + state->caps_count[HidP_Output] + state->caps_count[HidP_Feature];
    caps_size += state->empty_caps[HidP_Input] + state->empty_caps[HidP_Output] + state->empty_caps[HidP_Feature];
    caps_size *= sizeof(struct hid_value_caps);

    size = caps_size + sizeof(struct hid_preparsed_data) +
           state->number_link_collection_nodes * sizeof(struct hid_collection_node);
    if (caps_size > MAXUSHORT || size > MAXUSHORT)
    {
        parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
        return NULL;
    }
    if (!(data = HidpAllocate(pool_type, size)))
    {
        parser_fail(state, STATUS_INSUFFICIENT_RESOURCES);
        return NULL;
    }

    RtlCopyMemory(data->magic, "HidP KDR", 8);
    data->usage = state->usage;
    data->usage_page = state->usage_page;
    data->input_caps_start = 0;
    data->input_caps_count = state->caps_count[HidP_Input] + state->empty_caps[HidP_Input];
    data->input_caps_end = data->input_caps_start + state->caps_count[HidP_Input];
    data->input_report_byte_length = state->byte_length[HidP_Input];
    data->output_caps_start = data->input_caps_start + data->input_caps_count;
    data->output_caps_count = state->caps_count[HidP_Output] + state->empty_caps[HidP_Output];
    data->output_caps_end = data->output_caps_start + state->caps_count[HidP_Output];
    data->output_report_byte_length = state->byte_length[HidP_Output];
    data->feature_caps_start = data->output_caps_start + data->output_caps_count;
    data->feature_caps_count = state->caps_count[HidP_Feature] + state->empty_caps[HidP_Feature];
    data->feature_caps_end = data->feature_caps_start + state->caps_count[HidP_Feature];
    data->feature_report_byte_length = state->byte_length[HidP_Feature];
    data->caps_size = (USHORT)caps_size;
    data->number_link_collection_nodes = state->number_link_collection_nodes;

    caps = HID_INPUT_VALUE_CAPS(data);
    if (state->caps_count[HidP_Input]) RtlCopyMemory(caps, state->values[HidP_Input], state->caps_count[HidP_Input] * sizeof(*caps));
    caps = HID_OUTPUT_VALUE_CAPS(data);
    if (state->caps_count[HidP_Output]) RtlCopyMemory(caps, state->values[HidP_Output], state->caps_count[HidP_Output] * sizeof(*caps));
    caps = HID_FEATURE_VALUE_CAPS(data);
    if (state->caps_count[HidP_Feature]) RtlCopyMemory(caps, state->values[HidP_Feature], state->caps_count[HidP_Feature] * sizeof(*caps));

    nodes = HID_COLLECTION_NODES(data);
    for (i = 0; i < data->number_link_collection_nodes; ++i)
    {
        nodes[i].usage_page = state->collections[i].usage_page;
        nodes[i].usage = state->collections[i].usage_min;
        nodes[i].parent = state->collections[i].link_collection;
        nodes[i].collection_type = state->collections[i].bit_field;
        nodes[i].first_child = 0;
        nodes[i].next_sibling = 0;
        nodes[i].number_of_children = 0;

        if (i > 0)
        {
            nodes[i].next_sibling = nodes[nodes[i].parent].first_child;
            nodes[nodes[i].parent].first_child = (USHORT)i;
            nodes[nodes[i].parent].number_of_children++;
        }
    }

    *data_size = size;
    return data;
}

static BOOLEAN read_item_value(const UCHAR *ptr, const UCHAR *end, ULONG *size, ULONG *value, LONG *signed_value)
{
    *size = (*ptr & 0x03);
    if (*size == 3) *size = 4;
    if ((ULONG)(end - ptr) <= *size) return FALSE;

    if (*size == 0) *signed_value = (LONG)(*value = 0);
    else if (*size == 1) *signed_value = (CHAR)(*value = ptr[1]);
    else if (*size == 2) *signed_value = (SHORT)(*value = ptr[1] | (ptr[2] << 8));
    else *signed_value = (LONG)(*value = ptr[1] | (ptr[2] << 8) | (ptr[3] << 16) | ((ULONG)ptr[4] << 24));
    return TRUE;
}

static BOOLEAN parse_item(struct hid_parser_state *state, UCHAR item, ULONG value, LONG signed_value)
{
    state->items.bit_field = value;

    switch (item & SHORT_ITEM(0xf, 0x3))
    {
    case SHORT_ITEM(TAG_MAIN_INPUT, TAG_TYPE_MAIN):
        return parse_new_value_caps(state, HidP_Input);
    case SHORT_ITEM(TAG_MAIN_OUTPUT, TAG_TYPE_MAIN):
        return parse_new_value_caps(state, HidP_Output);
    case SHORT_ITEM(TAG_MAIN_FEATURE, TAG_TYPE_MAIN):
        return parse_new_value_caps(state, HidP_Feature);
    case SHORT_ITEM(TAG_MAIN_COLLECTION, TAG_TYPE_MAIN):
        return parse_new_collection(state);
    case SHORT_ITEM(TAG_MAIN_END_COLLECTION, TAG_TYPE_MAIN):
        return parse_end_collection(state);

    case SHORT_ITEM(TAG_GLOBAL_USAGE_PAGE, TAG_TYPE_GLOBAL):
        state->items.usage_page = (USAGE)value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_LOGICAL_MINIMUM, TAG_TYPE_GLOBAL):
        state->items.logical_min = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_LOGICAL_MAXIMUM, TAG_TYPE_GLOBAL):
        state->items.logical_max = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_PHYSICAL_MINIMUM, TAG_TYPE_GLOBAL):
        state->items.physical_min = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_PHYSICAL_MAXIMUM, TAG_TYPE_GLOBAL):
        state->items.physical_max = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_UNIT_EXPONENT, TAG_TYPE_GLOBAL):
        state->items.units_exp = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_UNIT, TAG_TYPE_GLOBAL):
        state->items.units = signed_value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_REPORT_SIZE, TAG_TYPE_GLOBAL):
        state->bit_size = value;
        state->items.bit_size = (USHORT)value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_REPORT_ID, TAG_TYPE_GLOBAL):
        if (!value || value > MAXUCHAR) return parser_fail(state, HIDP_STATUS_INVALID_REPORT_TYPE);
        state->items.report_id = (UCHAR)value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_REPORT_COUNT, TAG_TYPE_GLOBAL):
        state->report_count = value;
        state->items.report_count = (USHORT)value;
        return TRUE;
    case SHORT_ITEM(TAG_GLOBAL_PUSH, TAG_TYPE_GLOBAL):
        return parse_global_push(state);
    case SHORT_ITEM(TAG_GLOBAL_POP, TAG_TYPE_GLOBAL):
        if (!parse_global_pop(state)) return FALSE;
        state->report_count = state->items.report_count;
        state->bit_size = state->items.bit_size;
        return TRUE;

    case SHORT_ITEM(TAG_LOCAL_USAGE, TAG_TYPE_LOCAL):
        return parse_local_usage(state, (USAGE)(value >> 16), (USAGE)(value & 0xffff));
    case SHORT_ITEM(TAG_LOCAL_USAGE_MINIMUM, TAG_TYPE_LOCAL):
        parse_local_usage_min(state, (USAGE)(value >> 16), (USAGE)(value & 0xffff));
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_USAGE_MAXIMUM, TAG_TYPE_LOCAL):
        parse_local_usage_max(state, (USAGE)(value >> 16), (USAGE)(value & 0xffff));
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_DESIGNATOR_INDEX, TAG_TYPE_LOCAL):
        state->items.designator_min = state->items.designator_max = (USHORT)value;
        state->items.flags &= ~HID_VALUE_CAPS_IS_DESIGNATOR_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_DESIGNATOR_MINIMUM, TAG_TYPE_LOCAL):
        state->items.designator_min = (USHORT)value;
        state->items.flags |= HID_VALUE_CAPS_IS_DESIGNATOR_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_DESIGNATOR_MAXIMUM, TAG_TYPE_LOCAL):
        state->items.designator_max = (USHORT)value;
        state->items.flags |= HID_VALUE_CAPS_IS_DESIGNATOR_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_STRING_INDEX, TAG_TYPE_LOCAL):
        state->items.string_min = state->items.string_max = (USHORT)value;
        state->items.flags &= ~HID_VALUE_CAPS_IS_STRING_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_STRING_MINIMUM, TAG_TYPE_LOCAL):
        state->items.string_min = (USHORT)value;
        state->items.flags |= HID_VALUE_CAPS_IS_STRING_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_STRING_MAXIMUM, TAG_TYPE_LOCAL):
        state->items.string_max = (USHORT)value;
        state->items.flags |= HID_VALUE_CAPS_IS_STRING_RANGE;
        return TRUE;
    case SHORT_ITEM(TAG_LOCAL_DELIMITER, TAG_TYPE_LOCAL):
        DPRINT1("HID delimiter %lu not implemented\n", value);
        return parser_fail(state, STATUS_ILLEGAL_INSTRUCTION);

    default:
        DPRINT("HID item type %x not implemented\n", item);
        return TRUE;
    }
}

struct report_lengths
{
    ULONG input[256];
    ULONG output[256];
    ULONG feature[256];
    ULONG collection[256];
    ULONG count;
};

static void add_report_lengths(struct report_lengths *lengths, ULONG bit_offset[3][256], ULONG collection)
{
    ULONG id;

    for (id = 0; id < 256; ++id)
    {
        if (!bit_offset[HidP_Input][id] && !bit_offset[HidP_Output][id] && !bit_offset[HidP_Feature][id]) continue;
        if (!lengths->input[id] && !lengths->output[id] && !lengths->feature[id]) lengths->count++;
        if (lengths->input[id] < bit_offset[HidP_Input][id]) lengths->input[id] = bit_offset[HidP_Input][id];
        if (lengths->output[id] < bit_offset[HidP_Output][id]) lengths->output[id] = bit_offset[HidP_Output][id];
        if (lengths->feature[id] < bit_offset[HidP_Feature][id]) lengths->feature[id] = bit_offset[HidP_Feature][id];
        lengths->collection[id] = collection;
    }
}

static NTSTATUS parse_descriptor(const UCHAR *descriptor, ULONG length, POOL_TYPE pool_type, struct report_lengths *lengths,
                                 ULONG collection, struct hid_preparsed_data **preparsed, PULONG preparsed_size)
{
    const UCHAR *ptr, *end = descriptor + length;
    struct hid_parser_state *state;
    ULONG size, value;
    LONG signed_value;
    NTSTATUS status;

    *preparsed = NULL;
    if (!(state = HidpAllocate(PagedPool, sizeof(*state)))) return STATUS_INSUFFICIENT_RESOURCES;
    state->status = STATUS_SUCCESS;

    for (ptr = descriptor; ptr != end; ptr += size + 1)
    {
        if (!read_item_value(ptr, end, &size, &value, &signed_value))
        {
            parser_fail(state, STATUS_BUFFER_TOO_SMALL);
            break;
        }
        if (!parse_item(state, *ptr, value, signed_value)) break;
    }

    if (NT_SUCCESS(state->status)) *preparsed = build_preparsed_data(state, pool_type, preparsed_size);
    if (NT_SUCCESS(state->status)) add_report_lengths(lengths, state->bit_offset, collection);
    status = state->status;
    free_parser_state(state);
    return status;
}

static NTSTATUS parse_top_level_collections(const UCHAR *descriptor, ULONG length, const UCHAR ***tlcs_out, PULONG count)
{
    const UCHAR *ptr, *end = descriptor + length, **tlcs, **tmp;
    ULONG size, value, depth = 0, capacity = 4;
    LONG signed_value;

    *tlcs_out = NULL;
    *count = 0;
    if (!(tlcs = HidpAllocate(PagedPool, capacity * sizeof(*tlcs)))) return STATUS_INSUFFICIENT_RESOURCES;
    tlcs[0] = descriptor;

    for (ptr = descriptor; ptr != end; ptr += size + 1)
    {
        if (!read_item_value(ptr, end, &size, &value, &signed_value)) break;

        switch (*ptr & SHORT_ITEM(0xf, 0x3))
        {
        case SHORT_ITEM(TAG_MAIN_COLLECTION, TAG_TYPE_MAIN):
            depth++;
            break;
        case SHORT_ITEM(TAG_MAIN_END_COLLECTION, TAG_TYPE_MAIN):
            if (!depth || --depth) break;
            if (*count + 2 > capacity)
            {
                if (!(tmp = HidpAllocate(PagedPool, capacity * 2 * sizeof(*tlcs))))
                {
                    HidpFree(tlcs);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                RtlCopyMemory(tmp, tlcs, capacity * sizeof(*tlcs));
                HidpFree(tlcs);
                tlcs = tmp;
                capacity *= 2;
            }
            *count = *count + 1;
            tlcs[*count] = ptr + size + 1;
            break;
        }
    }

    *tlcs_out = tlcs;
    return STATUS_SUCCESS;
}

static void free_device_desc(HIDP_DEVICE_DESC *device_desc)
{
    ULONG i;

    if (device_desc->CollectionDesc)
    {
        for (i = 0; i < device_desc->CollectionDescLength; ++i)
            HidpFree(device_desc->CollectionDesc[i].PreparsedData);
        HidpFree(device_desc->CollectionDesc);
    }
    HidpFree(device_desc->ReportIDs);
    RtlZeroMemory(device_desc, sizeof(*device_desc));
}

static NTSTATUS fill_collection_desc(HIDP_DEVICE_DESC *device_desc, const UCHAR **tlcs, ULONG tlc_count, POOL_TYPE pool_type,
                                     struct report_lengths *lengths)
{
    struct hid_preparsed_data *preparsed;
    HIDP_COLLECTION_DESC *desc;
    ULONG i, len = 0;
    NTSTATUS status;

    if (!(device_desc->CollectionDesc = HidpAllocate(pool_type, sizeof(*device_desc->CollectionDesc) * tlc_count)))
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < tlc_count; ++i)
    {
        status = parse_descriptor(tlcs[i], (ULONG)(tlcs[i + 1] - tlcs[i]), pool_type, lengths, i, &preparsed, &len);
        if (!NT_SUCCESS(status)) return status;

        desc = &device_desc->CollectionDesc[i];
        device_desc->CollectionDescLength++;
        desc->UsagePage = preparsed->usage_page;
        desc->Usage = preparsed->usage;
        desc->CollectionNumber = (UCHAR)(i + 1);
        desc->InputLength = preparsed->input_report_byte_length;
        desc->OutputLength = preparsed->output_report_byte_length;
        desc->FeatureLength = preparsed->feature_report_byte_length;
        desc->PreparsedDataLength = (USHORT)len;
        desc->PreparsedData = (PHIDP_PREPARSED_DATA)preparsed;

    }

    return STATUS_SUCCESS;
}

static USHORT report_id_length(ULONG bits, ULONG report_id)
{
    if (!report_id && bits >= 8) bits -= 8;
    return (USHORT)((bits + 7) / 8);
}

static NTSTATUS fill_report_ids(HIDP_DEVICE_DESC *device_desc, POOL_TYPE pool_type, const struct report_lengths *lengths)
{
    ULONG i, count = 0;

    if (!(device_desc->ReportIDs = HidpAllocate(pool_type, sizeof(*device_desc->ReportIDs) * (lengths->count ? lengths->count : 1))))
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < 256; ++i)
    {
        if (!lengths->input[i] && !lengths->output[i] && !lengths->feature[i]) continue;
        device_desc->ReportIDs[count].ReportID = (UCHAR)i;
        device_desc->ReportIDs[count].CollectionNumber = (UCHAR)(lengths->collection[i] + 1);
        device_desc->ReportIDs[count].InputLength = report_id_length(lengths->input[i], i);
        device_desc->ReportIDs[count].OutputLength = report_id_length(lengths->output[i], i);
        device_desc->ReportIDs[count].FeatureLength = report_id_length(lengths->feature[i], i);
        count++;
    }
    device_desc->ReportIDsLength = count;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidP_GetCollectionDescription(
    IN PHIDP_REPORT_DESCRIPTOR ReportDesc,
    IN ULONG DescLength,
    IN POOL_TYPE PoolType,
    OUT PHIDP_DEVICE_DESC DeviceDescription)
{
    struct report_lengths *lengths;
    const UCHAR **tlcs;
    ULONG tlc_count;
    NTSTATUS status;

    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    if (!ReportDesc || !DescLength) return STATUS_NO_DATA_DETECTED;

    status = parse_top_level_collections(ReportDesc, DescLength, &tlcs, &tlc_count);
    if (!NT_SUCCESS(status)) return status;
    if (!tlc_count)
    {
        HidpFree((PVOID)tlcs);
        return STATUS_NO_DATA_DETECTED;
    }

    if (!(lengths = HidpAllocate(PagedPool, sizeof(*lengths))))
    {
        HidpFree((PVOID)tlcs);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = fill_collection_desc(DeviceDescription, tlcs, tlc_count, PoolType, lengths);
    if (NT_SUCCESS(status)) status = fill_report_ids(DeviceDescription, PoolType, lengths);
    if (!NT_SUCCESS(status)) free_device_desc(DeviceDescription);

    HidpFree(lengths);
    HidpFree((PVOID)tlcs);
    return status;
}

VOID
NTAPI
HidP_FreeCollectionDescription(
    IN PHIDP_DEVICE_DESC DeviceDescription)
{
    free_device_desc(DeviceDescription);
}
