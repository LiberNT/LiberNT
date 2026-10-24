/*
 * PROJECT:     LiberNT msvcr90.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 C++ V-tables for MSVC builds
 * COPYRIGHT:   Copyright 2000-2026 Wine team
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

    .data

    .macro START_VTABLE shortname
    .balign 8
    .quad \shortname\()_rtti
    .global \shortname\()_vtable
\shortname\()_vtable:
    .endm

    .macro VTABLE_ADD_FUNC func
    .quad \func
    .endm

START_VTABLE type_info
    VTABLE_ADD_FUNC type_info_vector_dtor

START_VTABLE exception
    VTABLE_ADD_FUNC exception_vector_dtor
    VTABLE_ADD_FUNC exception_what

START_VTABLE exception_old
    VTABLE_ADD_FUNC exception_vector_dtor
    VTABLE_ADD_FUNC exception_what

START_VTABLE bad_alloc
    VTABLE_ADD_FUNC exception_vector_dtor
    VTABLE_ADD_FUNC exception_what

START_VTABLE bad_typeid
    VTABLE_ADD_FUNC bad_typeid_vector_dtor
    VTABLE_ADD_FUNC exception_what

START_VTABLE bad_cast
    VTABLE_ADD_FUNC bad_cast_vector_dtor
    VTABLE_ADD_FUNC exception_what

START_VTABLE __non_rtti_object
    VTABLE_ADD_FUNC __non_rtti_object_vector_dtor
    VTABLE_ADD_FUNC exception_what
