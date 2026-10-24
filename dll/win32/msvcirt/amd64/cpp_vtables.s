/*
 * PROJECT:     LiberNT msvcirt.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     x64 C++ V-tables for MSVC builds
 * COPYRIGHT:   Copyright 2007-2026 Wine team
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>

.const

MACRO(START_VTABLE, shortname)
EXTERN &shortname&_rtti:PROC
    .quad &shortname&_rtti
PUBLIC &shortname&_vtable
&shortname&_vtable:
ENDM

MACRO(VTABLE_ADD_FUNC, func)
EXTERN &func:PROC
    .quad &func
ENDM

START_VTABLE type_info
    VTABLE_ADD_FUNC type_info_vector_dtor

START_VTABLE exception
    VTABLE_ADD_FUNC MSVCP_exception_vector_dtor
    VTABLE_ADD_FUNC MSVCP_exception_what

START_VTABLE bad_alloc
    VTABLE_ADD_FUNC MSVCP_bad_alloc_vector_dtor
    VTABLE_ADD_FUNC MSVCP_exception_what

START_VTABLE logic_error
    VTABLE_ADD_FUNC MSVCP_logic_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_logic_error_what

START_VTABLE length_error
    VTABLE_ADD_FUNC MSVCP_logic_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_logic_error_what

START_VTABLE out_of_range
    VTABLE_ADD_FUNC MSVCP_logic_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_logic_error_what

START_VTABLE invalid_argument
    VTABLE_ADD_FUNC MSVCP_logic_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_logic_error_what

START_VTABLE runtime_error
    VTABLE_ADD_FUNC MSVCP_runtime_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_runtime_error_what

START_VTABLE failure
    VTABLE_ADD_FUNC MSVCP_failure_vector_dtor
    VTABLE_ADD_FUNC MSVCP_failure_what

START_VTABLE bad_cast
    VTABLE_ADD_FUNC MSVCP_bad_cast_vector_dtor
    VTABLE_ADD_FUNC MSVCP_exception_what

START_VTABLE range_error
    VTABLE_ADD_FUNC MSVCP_runtime_error_vector_dtor
    VTABLE_ADD_FUNC MSVCP_runtime_error_what

START_VTABLE streambuf
    VTABLE_ADD_FUNC streambuf_vector_dtor
    VTABLE_ADD_FUNC streambuf_sync
    VTABLE_ADD_FUNC streambuf_setbuf
    VTABLE_ADD_FUNC streambuf_seekoff
    VTABLE_ADD_FUNC streambuf_seekpos
    VTABLE_ADD_FUNC streambuf_xsputn
    VTABLE_ADD_FUNC streambuf_xsgetn
    VTABLE_ADD_FUNC streambuf_overflow
    VTABLE_ADD_FUNC streambuf_underflow
    VTABLE_ADD_FUNC streambuf_pbackfail
    VTABLE_ADD_FUNC streambuf_doallocate

START_VTABLE filebuf
    VTABLE_ADD_FUNC filebuf_vector_dtor
    VTABLE_ADD_FUNC filebuf_sync
    VTABLE_ADD_FUNC filebuf_setbuf
    VTABLE_ADD_FUNC filebuf_seekoff
    VTABLE_ADD_FUNC streambuf_seekpos
    VTABLE_ADD_FUNC streambuf_xsputn
    VTABLE_ADD_FUNC streambuf_xsgetn
    VTABLE_ADD_FUNC filebuf_overflow
    VTABLE_ADD_FUNC filebuf_underflow
    VTABLE_ADD_FUNC streambuf_pbackfail
    VTABLE_ADD_FUNC streambuf_doallocate

START_VTABLE strstreambuf
    VTABLE_ADD_FUNC strstreambuf_vector_dtor
    VTABLE_ADD_FUNC strstreambuf_sync
    VTABLE_ADD_FUNC strstreambuf_setbuf
    VTABLE_ADD_FUNC strstreambuf_seekoff
    VTABLE_ADD_FUNC streambuf_seekpos
    VTABLE_ADD_FUNC streambuf_xsputn
    VTABLE_ADD_FUNC streambuf_xsgetn
    VTABLE_ADD_FUNC strstreambuf_overflow
    VTABLE_ADD_FUNC strstreambuf_underflow
    VTABLE_ADD_FUNC streambuf_pbackfail
    VTABLE_ADD_FUNC strstreambuf_doallocate

START_VTABLE stdiobuf
    VTABLE_ADD_FUNC stdiobuf_vector_dtor
    VTABLE_ADD_FUNC stdiobuf_sync
    VTABLE_ADD_FUNC streambuf_setbuf
    VTABLE_ADD_FUNC stdiobuf_seekoff
    VTABLE_ADD_FUNC streambuf_seekpos
    VTABLE_ADD_FUNC streambuf_xsputn
    VTABLE_ADD_FUNC streambuf_xsgetn
    VTABLE_ADD_FUNC stdiobuf_overflow
    VTABLE_ADD_FUNC stdiobuf_underflow
    VTABLE_ADD_FUNC stdiobuf_pbackfail
    VTABLE_ADD_FUNC streambuf_doallocate

START_VTABLE ios
    VTABLE_ADD_FUNC ios_vector_dtor

START_VTABLE ostream
    VTABLE_ADD_FUNC ostream_vector_dtor

START_VTABLE ostream_withassign
    VTABLE_ADD_FUNC ostream_vector_dtor

START_VTABLE ostrstream
    VTABLE_ADD_FUNC ostream_vector_dtor

START_VTABLE ofstream
    VTABLE_ADD_FUNC ostream_vector_dtor

START_VTABLE istream
    VTABLE_ADD_FUNC istream_vector_dtor

START_VTABLE istream_withassign
    VTABLE_ADD_FUNC istream_vector_dtor

START_VTABLE istrstream
    VTABLE_ADD_FUNC istream_vector_dtor

START_VTABLE ifstream
    VTABLE_ADD_FUNC istream_vector_dtor

START_VTABLE iostream
    VTABLE_ADD_FUNC iostream_vector_dtor

START_VTABLE strstream
    VTABLE_ADD_FUNC iostream_vector_dtor

START_VTABLE stdiostream
    VTABLE_ADD_FUNC iostream_vector_dtor

START_VTABLE fstream
    VTABLE_ADD_FUNC iostream_vector_dtor

END
