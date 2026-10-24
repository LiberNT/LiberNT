/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Wayland protocol tables generated from the protocol XML files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wlproto.h"

static const WL_MESSAGE wl_display_requests[] =
{
    { "sync", "n", 0, 1 },
    { "get_registry", "n", 0, 1 },
};

static const WL_MESSAGE wl_display_events[] =
{
    { "error", "ous", 0, 1 },
    { "delete_id", "u", 0, 1 },
};

const WL_INTERFACE wl_display_interface =
{
    "wl_display", 1, 2, wl_display_requests, 2, wl_display_events
};

static const WL_MESSAGE wl_registry_requests[] =
{
    { "bind", "usun", 0, 1 },
};

static const WL_MESSAGE wl_registry_events[] =
{
    { "global", "usu", 0, 1 },
    { "global_remove", "u", 0, 1 },
};

const WL_INTERFACE wl_registry_interface =
{
    "wl_registry", 1, 1, wl_registry_requests, 2, wl_registry_events
};

static const WL_MESSAGE wl_callback_events[] =
{
    { "done", "u", 1, 1 },
};

const WL_INTERFACE wl_callback_interface =
{
    "wl_callback", 1, 0, 0, 1, wl_callback_events
};

static const WL_MESSAGE wl_compositor_requests[] =
{
    { "create_surface", "n", 0, 1 },
    { "create_region", "n", 0, 1 },
};

const WL_INTERFACE wl_compositor_interface =
{
    "wl_compositor", 6, 2, wl_compositor_requests, 0, 0
};

static const WL_MESSAGE wl_shm_pool_requests[] =
{
    { "create_buffer", "niiiiu", 0, 1 },
    { "destroy", "", 1, 1 },
    { "resize", "i", 0, 1 },
};

const WL_INTERFACE wl_shm_pool_interface =
{
    "wl_shm_pool", 2, 3, wl_shm_pool_requests, 0, 0
};

static const WL_MESSAGE wl_shm_requests[] =
{
    { "create_pool", "nhi", 0, 1 },
    { "release", "", 1, 2 },
};

static const WL_MESSAGE wl_shm_events[] =
{
    { "format", "u", 0, 1 },
};

const WL_INTERFACE wl_shm_interface =
{
    "wl_shm", 2, 2, wl_shm_requests, 1, wl_shm_events
};

static const WL_MESSAGE wl_buffer_requests[] =
{
    { "destroy", "", 1, 1 },
};

static const WL_MESSAGE wl_buffer_events[] =
{
    { "release", "", 0, 1 },
};

const WL_INTERFACE wl_buffer_interface =
{
    "wl_buffer", 1, 1, wl_buffer_requests, 1, wl_buffer_events
};

static const WL_MESSAGE wl_data_offer_requests[] =
{
    { "accept", "u?s", 0, 1 },
    { "receive", "sh", 0, 1 },
    { "destroy", "", 1, 1 },
    { "finish", "", 0, 3 },
    { "set_actions", "uu", 0, 3 },
};

static const WL_MESSAGE wl_data_offer_events[] =
{
    { "offer", "s", 0, 1 },
    { "source_actions", "u", 0, 3 },
    { "action", "u", 0, 3 },
};

const WL_INTERFACE wl_data_offer_interface =
{
    "wl_data_offer", 3, 5, wl_data_offer_requests, 3, wl_data_offer_events
};

static const WL_MESSAGE wl_data_source_requests[] =
{
    { "offer", "s", 0, 1 },
    { "destroy", "", 1, 1 },
    { "set_actions", "u", 0, 3 },
};

static const WL_MESSAGE wl_data_source_events[] =
{
    { "target", "?s", 0, 1 },
    { "send", "sh", 0, 1 },
    { "cancelled", "", 0, 1 },
    { "dnd_drop_performed", "", 0, 3 },
    { "dnd_finished", "", 0, 3 },
    { "action", "u", 0, 3 },
};

const WL_INTERFACE wl_data_source_interface =
{
    "wl_data_source", 3, 3, wl_data_source_requests, 6, wl_data_source_events
};

static const WL_MESSAGE wl_data_device_requests[] =
{
    { "start_drag", "?oo?ou", 0, 1 },
    { "set_selection", "?ou", 0, 1 },
    { "release", "", 1, 2 },
};

static const WL_MESSAGE wl_data_device_events[] =
{
    { "data_offer", "n", 0, 1 },
    { "enter", "uoff?o", 0, 1 },
    { "leave", "", 0, 1 },
    { "motion", "uff", 0, 1 },
    { "drop", "", 0, 1 },
    { "selection", "?o", 0, 1 },
};

const WL_INTERFACE wl_data_device_interface =
{
    "wl_data_device", 3, 3, wl_data_device_requests, 6, wl_data_device_events
};

static const WL_MESSAGE wl_data_device_manager_requests[] =
{
    { "create_data_source", "n", 0, 1 },
    { "get_data_device", "no", 0, 1 },
};

const WL_INTERFACE wl_data_device_manager_interface =
{
    "wl_data_device_manager", 3, 2, wl_data_device_manager_requests, 0, 0
};

static const WL_MESSAGE wl_shell_requests[] =
{
    { "get_shell_surface", "no", 0, 1 },
};

const WL_INTERFACE wl_shell_interface =
{
    "wl_shell", 1, 1, wl_shell_requests, 0, 0
};

static const WL_MESSAGE wl_shell_surface_requests[] =
{
    { "pong", "u", 0, 1 },
    { "move", "ou", 0, 1 },
    { "resize", "ouu", 0, 1 },
    { "set_toplevel", "", 0, 1 },
    { "set_transient", "oiiu", 0, 1 },
    { "set_fullscreen", "uu?o", 0, 1 },
    { "set_popup", "ouoiiu", 0, 1 },
    { "set_maximized", "?o", 0, 1 },
    { "set_title", "s", 0, 1 },
    { "set_class", "s", 0, 1 },
};

static const WL_MESSAGE wl_shell_surface_events[] =
{
    { "ping", "u", 0, 1 },
    { "configure", "uii", 0, 1 },
    { "popup_done", "", 0, 1 },
};

const WL_INTERFACE wl_shell_surface_interface =
{
    "wl_shell_surface", 1, 10, wl_shell_surface_requests, 3, wl_shell_surface_events
};

static const WL_MESSAGE wl_surface_requests[] =
{
    { "destroy", "", 1, 1 },
    { "attach", "?oii", 0, 1 },
    { "damage", "iiii", 0, 1 },
    { "frame", "n", 0, 1 },
    { "set_opaque_region", "?o", 0, 1 },
    { "set_input_region", "?o", 0, 1 },
    { "commit", "", 0, 1 },
    { "set_buffer_transform", "i", 0, 2 },
    { "set_buffer_scale", "i", 0, 3 },
    { "damage_buffer", "iiii", 0, 4 },
    { "offset", "ii", 0, 5 },
};

static const WL_MESSAGE wl_surface_events[] =
{
    { "enter", "o", 0, 1 },
    { "leave", "o", 0, 1 },
    { "preferred_buffer_scale", "i", 0, 6 },
    { "preferred_buffer_transform", "u", 0, 6 },
};

const WL_INTERFACE wl_surface_interface =
{
    "wl_surface", 6, 11, wl_surface_requests, 4, wl_surface_events
};

static const WL_MESSAGE wl_seat_requests[] =
{
    { "get_pointer", "n", 0, 1 },
    { "get_keyboard", "n", 0, 1 },
    { "get_touch", "n", 0, 1 },
    { "release", "", 1, 5 },
};

static const WL_MESSAGE wl_seat_events[] =
{
    { "capabilities", "u", 0, 1 },
    { "name", "s", 0, 2 },
};

const WL_INTERFACE wl_seat_interface =
{
    "wl_seat", 10, 4, wl_seat_requests, 2, wl_seat_events
};

static const WL_MESSAGE wl_pointer_requests[] =
{
    { "set_cursor", "u?oii", 0, 1 },
    { "release", "", 1, 3 },
};

static const WL_MESSAGE wl_pointer_events[] =
{
    { "enter", "uoff", 0, 1 },
    { "leave", "uo", 0, 1 },
    { "motion", "uff", 0, 1 },
    { "button", "uuuu", 0, 1 },
    { "axis", "uuf", 0, 1 },
    { "frame", "", 0, 5 },
    { "axis_source", "u", 0, 5 },
    { "axis_stop", "uu", 0, 5 },
    { "axis_discrete", "ui", 0, 5 },
    { "axis_value120", "ui", 0, 8 },
    { "axis_relative_direction", "uu", 0, 9 },
};

const WL_INTERFACE wl_pointer_interface =
{
    "wl_pointer", 10, 2, wl_pointer_requests, 11, wl_pointer_events
};

static const WL_MESSAGE wl_keyboard_requests[] =
{
    { "release", "", 1, 3 },
};

static const WL_MESSAGE wl_keyboard_events[] =
{
    { "keymap", "uhu", 0, 1 },
    { "enter", "uoa", 0, 1 },
    { "leave", "uo", 0, 1 },
    { "key", "uuuu", 0, 1 },
    { "modifiers", "uuuuu", 0, 1 },
    { "repeat_info", "ii", 0, 4 },
};

const WL_INTERFACE wl_keyboard_interface =
{
    "wl_keyboard", 10, 1, wl_keyboard_requests, 6, wl_keyboard_events
};

static const WL_MESSAGE wl_touch_requests[] =
{
    { "release", "", 1, 3 },
};

static const WL_MESSAGE wl_touch_events[] =
{
    { "down", "uuoiff", 0, 1 },
    { "up", "uui", 0, 1 },
    { "motion", "uiff", 0, 1 },
    { "frame", "", 0, 1 },
    { "cancel", "", 0, 1 },
    { "shape", "iff", 0, 6 },
    { "orientation", "if", 0, 6 },
};

const WL_INTERFACE wl_touch_interface =
{
    "wl_touch", 10, 1, wl_touch_requests, 7, wl_touch_events
};

static const WL_MESSAGE wl_output_requests[] =
{
    { "release", "", 1, 3 },
};

static const WL_MESSAGE wl_output_events[] =
{
    { "geometry", "iiiiissi", 0, 1 },
    { "mode", "uiii", 0, 1 },
    { "done", "", 0, 2 },
    { "scale", "i", 0, 2 },
    { "name", "s", 0, 4 },
    { "description", "s", 0, 4 },
};

const WL_INTERFACE wl_output_interface =
{
    "wl_output", 4, 1, wl_output_requests, 6, wl_output_events
};

static const WL_MESSAGE wl_region_requests[] =
{
    { "destroy", "", 1, 1 },
    { "add", "iiii", 0, 1 },
    { "subtract", "iiii", 0, 1 },
};

const WL_INTERFACE wl_region_interface =
{
    "wl_region", 1, 3, wl_region_requests, 0, 0
};

static const WL_MESSAGE wl_subcompositor_requests[] =
{
    { "destroy", "", 1, 1 },
    { "get_subsurface", "noo", 0, 1 },
};

const WL_INTERFACE wl_subcompositor_interface =
{
    "wl_subcompositor", 1, 2, wl_subcompositor_requests, 0, 0
};

static const WL_MESSAGE wl_subsurface_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_position", "ii", 0, 1 },
    { "place_above", "o", 0, 1 },
    { "place_below", "o", 0, 1 },
    { "set_sync", "", 0, 1 },
    { "set_desync", "", 0, 1 },
};

const WL_INTERFACE wl_subsurface_interface =
{
    "wl_subsurface", 1, 6, wl_subsurface_requests, 0, 0
};

static const WL_MESSAGE wl_fixes_requests[] =
{
    { "destroy", "", 1, 1 },
    { "destroy_registry", "o", 0, 1 },
};

const WL_INTERFACE wl_fixes_interface =
{
    "wl_fixes", 1, 2, wl_fixes_requests, 0, 0
};

static const WL_MESSAGE xdg_wm_base_requests[] =
{
    { "destroy", "", 1, 1 },
    { "create_positioner", "n", 0, 1 },
    { "get_xdg_surface", "no", 0, 1 },
    { "pong", "u", 0, 1 },
};

static const WL_MESSAGE xdg_wm_base_events[] =
{
    { "ping", "u", 0, 1 },
};

const WL_INTERFACE xdg_wm_base_interface =
{
    "xdg_wm_base", 7, 4, xdg_wm_base_requests, 1, xdg_wm_base_events
};

static const WL_MESSAGE xdg_positioner_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_size", "ii", 0, 1 },
    { "set_anchor_rect", "iiii", 0, 1 },
    { "set_anchor", "u", 0, 1 },
    { "set_gravity", "u", 0, 1 },
    { "set_constraint_adjustment", "u", 0, 1 },
    { "set_offset", "ii", 0, 1 },
    { "set_reactive", "", 0, 3 },
    { "set_parent_size", "ii", 0, 3 },
    { "set_parent_configure", "u", 0, 3 },
};

const WL_INTERFACE xdg_positioner_interface =
{
    "xdg_positioner", 7, 10, xdg_positioner_requests, 0, 0
};

static const WL_MESSAGE xdg_surface_requests[] =
{
    { "destroy", "", 1, 1 },
    { "get_toplevel", "n", 0, 1 },
    { "get_popup", "n?oo", 0, 1 },
    { "set_window_geometry", "iiii", 0, 1 },
    { "ack_configure", "u", 0, 1 },
};

static const WL_MESSAGE xdg_surface_events[] =
{
    { "configure", "u", 0, 1 },
};

const WL_INTERFACE xdg_surface_interface =
{
    "xdg_surface", 7, 5, xdg_surface_requests, 1, xdg_surface_events
};

static const WL_MESSAGE xdg_toplevel_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_parent", "?o", 0, 1 },
    { "set_title", "s", 0, 1 },
    { "set_app_id", "s", 0, 1 },
    { "show_window_menu", "ouii", 0, 1 },
    { "move", "ou", 0, 1 },
    { "resize", "ouu", 0, 1 },
    { "set_max_size", "ii", 0, 1 },
    { "set_min_size", "ii", 0, 1 },
    { "set_maximized", "", 0, 1 },
    { "unset_maximized", "", 0, 1 },
    { "set_fullscreen", "?o", 0, 1 },
    { "unset_fullscreen", "", 0, 1 },
    { "set_minimized", "", 0, 1 },
};

static const WL_MESSAGE xdg_toplevel_events[] =
{
    { "configure", "iia", 0, 1 },
    { "close", "", 0, 1 },
    { "configure_bounds", "ii", 0, 4 },
    { "wm_capabilities", "a", 0, 5 },
};

const WL_INTERFACE xdg_toplevel_interface =
{
    "xdg_toplevel", 7, 14, xdg_toplevel_requests, 4, xdg_toplevel_events
};

static const WL_MESSAGE xdg_popup_requests[] =
{
    { "destroy", "", 1, 1 },
    { "grab", "ou", 0, 1 },
    { "reposition", "ou", 0, 3 },
};

static const WL_MESSAGE xdg_popup_events[] =
{
    { "configure", "iiii", 0, 1 },
    { "popup_done", "", 0, 1 },
    { "repositioned", "u", 0, 3 },
};

const WL_INTERFACE xdg_popup_interface =
{
    "xdg_popup", 7, 3, xdg_popup_requests, 3, xdg_popup_events
};

static const WL_MESSAGE zxdg_decoration_manager_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "get_toplevel_decoration", "no", 0, 1 },
};

const WL_INTERFACE zxdg_decoration_manager_v1_interface =
{
    "zxdg_decoration_manager_v1", 1, 2, zxdg_decoration_manager_v1_requests, 0, 0
};

static const WL_MESSAGE zxdg_toplevel_decoration_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_mode", "u", 0, 1 },
    { "unset_mode", "", 0, 1 },
};

static const WL_MESSAGE zxdg_toplevel_decoration_v1_events[] =
{
    { "configure", "u", 0, 1 },
};

const WL_INTERFACE zxdg_toplevel_decoration_v1_interface =
{
    "zxdg_toplevel_decoration_v1", 1, 3, zxdg_toplevel_decoration_v1_requests, 1, zxdg_toplevel_decoration_v1_events
};

static const WL_MESSAGE wp_cursor_shape_manager_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "get_pointer", "no", 0, 1 },
    { "get_tablet_tool_v2", "no", 0, 1 },
};

const WL_INTERFACE wp_cursor_shape_manager_v1_interface =
{
    "wp_cursor_shape_manager_v1", 2, 3, wp_cursor_shape_manager_v1_requests, 0, 0
};

static const WL_MESSAGE wp_cursor_shape_device_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_shape", "uu", 0, 1 },
};

const WL_INTERFACE wp_cursor_shape_device_v1_interface =
{
    "wp_cursor_shape_device_v1", 2, 2, wp_cursor_shape_device_v1_requests, 0, 0
};

static const WL_MESSAGE xdg_toplevel_icon_manager_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "create_icon", "n", 0, 1 },
    { "set_icon", "o?o", 0, 1 },
};

static const WL_MESSAGE xdg_toplevel_icon_manager_v1_events[] =
{
    { "icon_size", "i", 0, 1 },
    { "done", "", 0, 1 },
};

const WL_INTERFACE xdg_toplevel_icon_manager_v1_interface =
{
    "xdg_toplevel_icon_manager_v1", 1, 3, xdg_toplevel_icon_manager_v1_requests, 2, xdg_toplevel_icon_manager_v1_events
};

static const WL_MESSAGE xdg_toplevel_icon_v1_requests[] =
{
    { "destroy", "", 1, 1 },
    { "set_name", "s", 0, 1 },
    { "add_buffer", "oi", 0, 1 },
};

const WL_INTERFACE xdg_toplevel_icon_v1_interface =
{
    "xdg_toplevel_icon_v1", 1, 3, xdg_toplevel_icon_v1_requests, 0, 0
};
