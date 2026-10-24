/*
 * PROJECT:     LiberNT msvcp90.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 C++ V-tables for MSVC builds
 * COPYRIGHT:   Copyright 2010-2026 Wine team
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

START_VTABLE iosb
    VTABLE_ADD_FUNC iosb_vector_dtor

START_VTABLE ios_base
    VTABLE_ADD_FUNC ios_base_vector_dtor

START_VTABLE basic_ios_char
    VTABLE_ADD_FUNC basic_ios_char_vector_dtor

START_VTABLE basic_ios_wchar
    VTABLE_ADD_FUNC basic_ios_wchar_vector_dtor

START_VTABLE basic_ios_short
    VTABLE_ADD_FUNC basic_ios_wchar_vector_dtor

START_VTABLE basic_streambuf_char
    VTABLE_ADD_FUNC basic_streambuf_char_vector_dtor
    VTABLE_ADD_FUNC basic_streambuf_char_overflow
    VTABLE_ADD_FUNC basic_streambuf_char_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_char_showmanyc
    VTABLE_ADD_FUNC basic_streambuf_char_underflow
    VTABLE_ADD_FUNC basic_streambuf_char_uflow
    VTABLE_ADD_FUNC basic_streambuf_char_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_char__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_char_xsputn
    VTABLE_ADD_FUNC basic_streambuf_char_seekoff
    VTABLE_ADD_FUNC basic_streambuf_char_seekpos
    VTABLE_ADD_FUNC basic_streambuf_char_setbuf
    VTABLE_ADD_FUNC basic_streambuf_char_sync
    VTABLE_ADD_FUNC basic_streambuf_char_imbue

START_VTABLE basic_streambuf_wchar
    VTABLE_ADD_FUNC basic_streambuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_streambuf_wchar_overflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_streambuf_wchar_underflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_streambuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_streambuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_streambuf_wchar_setbuf
    VTABLE_ADD_FUNC basic_streambuf_wchar_sync
    VTABLE_ADD_FUNC basic_streambuf_wchar_imbue

START_VTABLE basic_streambuf_short
    VTABLE_ADD_FUNC basic_streambuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_streambuf_wchar_overflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_streambuf_wchar_underflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_streambuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_streambuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_streambuf_wchar_setbuf
    VTABLE_ADD_FUNC basic_streambuf_wchar_sync
    VTABLE_ADD_FUNC basic_streambuf_wchar_imbue

START_VTABLE basic_filebuf_char
    VTABLE_ADD_FUNC basic_filebuf_char_vector_dtor
    VTABLE_ADD_FUNC basic_filebuf_char_overflow
    VTABLE_ADD_FUNC basic_filebuf_char_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_char_showmanyc
    VTABLE_ADD_FUNC basic_filebuf_char_underflow
    VTABLE_ADD_FUNC basic_filebuf_char_uflow
    VTABLE_ADD_FUNC basic_streambuf_char_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_char__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_char_xsputn
    VTABLE_ADD_FUNC basic_filebuf_char_seekoff
    VTABLE_ADD_FUNC basic_filebuf_char_seekpos
    VTABLE_ADD_FUNC basic_filebuf_char_setbuf
    VTABLE_ADD_FUNC basic_filebuf_char_sync
    VTABLE_ADD_FUNC basic_filebuf_char_imbue

START_VTABLE basic_filebuf_wchar
    VTABLE_ADD_FUNC basic_filebuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_filebuf_wchar_overflow
    VTABLE_ADD_FUNC basic_filebuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_filebuf_wchar_underflow
    VTABLE_ADD_FUNC basic_filebuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_filebuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_filebuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_filebuf_wchar_setbuf
    VTABLE_ADD_FUNC basic_filebuf_wchar_sync
    VTABLE_ADD_FUNC basic_filebuf_wchar_imbue

START_VTABLE basic_filebuf_short
    VTABLE_ADD_FUNC basic_filebuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_filebuf_wchar_overflow
    VTABLE_ADD_FUNC basic_filebuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_filebuf_wchar_underflow
    VTABLE_ADD_FUNC basic_filebuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_filebuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_filebuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_filebuf_short_setbuf
    VTABLE_ADD_FUNC basic_filebuf_wchar_sync
    VTABLE_ADD_FUNC basic_filebuf_short_imbue

START_VTABLE basic_stringbuf_char
    VTABLE_ADD_FUNC basic_stringbuf_char_vector_dtor
    VTABLE_ADD_FUNC basic_stringbuf_char_overflow
    VTABLE_ADD_FUNC basic_stringbuf_char_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_char_showmanyc
    VTABLE_ADD_FUNC basic_stringbuf_char_underflow
    VTABLE_ADD_FUNC basic_streambuf_char_uflow
    VTABLE_ADD_FUNC basic_streambuf_char_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_char__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_char_xsputn
    VTABLE_ADD_FUNC basic_stringbuf_char_seekoff
    VTABLE_ADD_FUNC basic_stringbuf_char_seekpos
    VTABLE_ADD_FUNC basic_streambuf_char_setbuf
    VTABLE_ADD_FUNC basic_streambuf_char_sync
    VTABLE_ADD_FUNC basic_streambuf_char_imbue

START_VTABLE basic_stringbuf_wchar
    VTABLE_ADD_FUNC basic_stringbuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_stringbuf_wchar_overflow
    VTABLE_ADD_FUNC basic_stringbuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_stringbuf_wchar_underflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_stringbuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_stringbuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_streambuf_wchar_setbuf
    VTABLE_ADD_FUNC basic_streambuf_wchar_sync
    VTABLE_ADD_FUNC basic_streambuf_wchar_imbue

START_VTABLE basic_stringbuf_short
    VTABLE_ADD_FUNC basic_stringbuf_wchar_vector_dtor
    VTABLE_ADD_FUNC basic_stringbuf_wchar_overflow
    VTABLE_ADD_FUNC basic_stringbuf_wchar_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_wchar_showmanyc
    VTABLE_ADD_FUNC basic_stringbuf_wchar_underflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_uflow
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_wchar__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_wchar_xsputn
    VTABLE_ADD_FUNC basic_stringbuf_wchar_seekoff
    VTABLE_ADD_FUNC basic_stringbuf_wchar_seekpos
    VTABLE_ADD_FUNC basic_streambuf_wchar_setbuf
    VTABLE_ADD_FUNC basic_streambuf_wchar_sync
    VTABLE_ADD_FUNC basic_streambuf_wchar_imbue

START_VTABLE basic_ostream_char
    VTABLE_ADD_FUNC basic_ostream_char_vector_dtor

START_VTABLE basic_ostream_wchar
    VTABLE_ADD_FUNC basic_ostream_wchar_vector_dtor

START_VTABLE basic_ostream_short
    VTABLE_ADD_FUNC basic_ostream_wchar_vector_dtor

START_VTABLE basic_istream_char
    VTABLE_ADD_FUNC basic_istream_char_vector_dtor

START_VTABLE basic_istream_wchar
    VTABLE_ADD_FUNC basic_istream_wchar_vector_dtor

START_VTABLE basic_istream_short
    VTABLE_ADD_FUNC basic_istream_wchar_vector_dtor

START_VTABLE basic_iostream_char
    VTABLE_ADD_FUNC basic_iostream_char_vector_dtor

START_VTABLE basic_iostream_wchar
    VTABLE_ADD_FUNC basic_iostream_wchar_vector_dtor

START_VTABLE basic_iostream_short
    VTABLE_ADD_FUNC basic_iostream_wchar_vector_dtor

START_VTABLE basic_ofstream_char
    VTABLE_ADD_FUNC basic_ofstream_char_vector_dtor

START_VTABLE basic_ofstream_wchar
    VTABLE_ADD_FUNC basic_ofstream_wchar_vector_dtor

START_VTABLE basic_ofstream_short
    VTABLE_ADD_FUNC basic_ofstream_wchar_vector_dtor

START_VTABLE basic_ifstream_char
    VTABLE_ADD_FUNC basic_ifstream_char_vector_dtor

START_VTABLE basic_ifstream_wchar
    VTABLE_ADD_FUNC basic_ifstream_wchar_vector_dtor

START_VTABLE basic_ifstream_short
    VTABLE_ADD_FUNC basic_ifstream_wchar_vector_dtor

START_VTABLE basic_fstream_char
    VTABLE_ADD_FUNC basic_fstream_char_vector_dtor

START_VTABLE basic_fstream_wchar
    VTABLE_ADD_FUNC basic_fstream_wchar_vector_dtor

START_VTABLE basic_fstream_short
    VTABLE_ADD_FUNC basic_fstream_wchar_vector_dtor

START_VTABLE basic_ostringstream_char
    VTABLE_ADD_FUNC basic_ostringstream_char_vector_dtor

START_VTABLE basic_ostringstream_wchar
    VTABLE_ADD_FUNC basic_ostringstream_wchar_vector_dtor

START_VTABLE basic_ostringstream_short
    VTABLE_ADD_FUNC basic_ostringstream_wchar_vector_dtor

START_VTABLE basic_istringstream_char
    VTABLE_ADD_FUNC basic_istringstream_char_vector_dtor

START_VTABLE basic_istringstream_wchar
    VTABLE_ADD_FUNC basic_istringstream_wchar_vector_dtor

START_VTABLE basic_istringstream_short
    VTABLE_ADD_FUNC basic_istringstream_wchar_vector_dtor

START_VTABLE basic_stringstream_char
    VTABLE_ADD_FUNC basic_stringstream_char_vector_dtor

START_VTABLE basic_stringstream_wchar
    VTABLE_ADD_FUNC basic_stringstream_wchar_vector_dtor

START_VTABLE basic_stringstream_short
    VTABLE_ADD_FUNC basic_stringstream_wchar_vector_dtor

START_VTABLE strstreambuf
    VTABLE_ADD_FUNC strstreambuf_vector_dtor
    VTABLE_ADD_FUNC strstreambuf_overflow
    VTABLE_ADD_FUNC strstreambuf_pbackfail
    VTABLE_ADD_FUNC basic_streambuf_char_showmanyc
    VTABLE_ADD_FUNC strstreambuf_underflow
    VTABLE_ADD_FUNC basic_streambuf_char_uflow
    VTABLE_ADD_FUNC basic_streambuf_char_xsgetn
    VTABLE_ADD_FUNC basic_streambuf_char__Xsgetn_s
    VTABLE_ADD_FUNC basic_streambuf_char_xsputn
    VTABLE_ADD_FUNC strstreambuf_seekoff
    VTABLE_ADD_FUNC strstreambuf_seekpos
    VTABLE_ADD_FUNC basic_streambuf_char_setbuf
    VTABLE_ADD_FUNC basic_streambuf_char_sync
    VTABLE_ADD_FUNC basic_streambuf_char_imbue

START_VTABLE ostrstream
    VTABLE_ADD_FUNC ostrstream_vector_dtor

START_VTABLE strstream
    VTABLE_ADD_FUNC strstream_vector_dtor

START_VTABLE _Facet_base
    VTABLE_ADD_FUNC locale_facet_vector_dtor
    VTABLE_ADD_FUNC locale_facet__Incref
    VTABLE_ADD_FUNC locale_facet__Incref

START_VTABLE locale_facet
    VTABLE_ADD_FUNC locale_facet_vector_dtor

START_VTABLE locale__Locimp
    VTABLE_ADD_FUNC locale__Locimp_vector_dtor

START_VTABLE collate_char
    VTABLE_ADD_FUNC collate_char_vector_dtor
    VTABLE_ADD_FUNC collate_char_do_compare
    VTABLE_ADD_FUNC collate_char_do_transform
    VTABLE_ADD_FUNC collate_char_do_hash

START_VTABLE collate_wchar
    VTABLE_ADD_FUNC collate_wchar_vector_dtor
    VTABLE_ADD_FUNC collate_wchar_do_compare
    VTABLE_ADD_FUNC collate_wchar_do_transform
    VTABLE_ADD_FUNC collate_wchar_do_hash

START_VTABLE collate_short
    VTABLE_ADD_FUNC collate_wchar_vector_dtor
    VTABLE_ADD_FUNC collate_wchar_do_compare
    VTABLE_ADD_FUNC collate_wchar_do_transform
    VTABLE_ADD_FUNC collate_wchar_do_hash

START_VTABLE ctype_base
    VTABLE_ADD_FUNC ctype_base_vector_dtor

START_VTABLE ctype_char
    VTABLE_ADD_FUNC ctype_char_vector_dtor
    VTABLE_ADD_FUNC ctype_char_do_tolower
    VTABLE_ADD_FUNC ctype_char_do_tolower_ch
    VTABLE_ADD_FUNC ctype_char_do_toupper
    VTABLE_ADD_FUNC ctype_char_do_toupper_ch
    VTABLE_ADD_FUNC ctype_char_do_widen
    VTABLE_ADD_FUNC ctype_char_do_widen_ch
    VTABLE_ADD_FUNC ctype_char__Do_widen_s
    VTABLE_ADD_FUNC ctype_char_do_narrow
    VTABLE_ADD_FUNC ctype_char_do_narrow_ch
    VTABLE_ADD_FUNC ctype_char__Do_narrow_s

START_VTABLE ctype_wchar
    VTABLE_ADD_FUNC ctype_wchar_vector_dtor
    VTABLE_ADD_FUNC ctype_wchar_do_is
    VTABLE_ADD_FUNC ctype_wchar_do_is_ch
    VTABLE_ADD_FUNC ctype_wchar_do_scan_is
    VTABLE_ADD_FUNC ctype_wchar_do_scan_not
    VTABLE_ADD_FUNC ctype_wchar_do_tolower
    VTABLE_ADD_FUNC ctype_wchar_do_tolower_ch
    VTABLE_ADD_FUNC ctype_wchar_do_toupper
    VTABLE_ADD_FUNC ctype_wchar_do_toupper_ch
    VTABLE_ADD_FUNC ctype_wchar_do_widen
    VTABLE_ADD_FUNC ctype_wchar_do_widen_ch
    VTABLE_ADD_FUNC ctype_wchar__Do_widen_s
    VTABLE_ADD_FUNC ctype_wchar_do_narrow
    VTABLE_ADD_FUNC ctype_wchar_do_narrow_ch
    VTABLE_ADD_FUNC ctype_wchar__Do_narrow_s

START_VTABLE ctype_short
    VTABLE_ADD_FUNC ctype_wchar_vector_dtor
    VTABLE_ADD_FUNC ctype_wchar_do_is
    VTABLE_ADD_FUNC ctype_wchar_do_is_ch
    VTABLE_ADD_FUNC ctype_wchar_do_scan_is
    VTABLE_ADD_FUNC ctype_wchar_do_scan_not
    VTABLE_ADD_FUNC ctype_wchar_do_tolower
    VTABLE_ADD_FUNC ctype_wchar_do_tolower_ch
    VTABLE_ADD_FUNC ctype_wchar_do_toupper
    VTABLE_ADD_FUNC ctype_wchar_do_toupper_ch
    VTABLE_ADD_FUNC ctype_wchar_do_widen
    VTABLE_ADD_FUNC ctype_wchar_do_widen_ch
    VTABLE_ADD_FUNC ctype_wchar__Do_widen_s
    VTABLE_ADD_FUNC ctype_wchar_do_narrow
    VTABLE_ADD_FUNC ctype_wchar_do_narrow_ch
    VTABLE_ADD_FUNC ctype_wchar__Do_narrow_s

START_VTABLE codecvt_base
    VTABLE_ADD_FUNC codecvt_base_vector_dtor
    VTABLE_ADD_FUNC codecvt_base_do_always_noconv
    VTABLE_ADD_FUNC codecvt_base_do_max_length
    VTABLE_ADD_FUNC codecvt_base_do_encoding

START_VTABLE codecvt_char
    VTABLE_ADD_FUNC codecvt_char_vector_dtor
    VTABLE_ADD_FUNC codecvt_base_do_always_noconv
    VTABLE_ADD_FUNC codecvt_base_do_max_length
    VTABLE_ADD_FUNC codecvt_base_do_encoding
    VTABLE_ADD_FUNC codecvt_char_do_in
    VTABLE_ADD_FUNC codecvt_char_do_out
    VTABLE_ADD_FUNC codecvt_char_do_unshift
    VTABLE_ADD_FUNC codecvt_char_do_length

START_VTABLE codecvt_wchar
    VTABLE_ADD_FUNC codecvt_wchar_vector_dtor
    VTABLE_ADD_FUNC codecvt_wchar_do_always_noconv
    VTABLE_ADD_FUNC codecvt_wchar_do_max_length
    VTABLE_ADD_FUNC codecvt_base_do_encoding
    VTABLE_ADD_FUNC codecvt_wchar_do_in
    VTABLE_ADD_FUNC codecvt_wchar_do_out
    VTABLE_ADD_FUNC codecvt_wchar_do_unshift
    VTABLE_ADD_FUNC codecvt_wchar_do_length

START_VTABLE codecvt_short
    VTABLE_ADD_FUNC codecvt_wchar_vector_dtor
    VTABLE_ADD_FUNC codecvt_wchar_do_always_noconv
    VTABLE_ADD_FUNC codecvt_wchar_do_max_length
    VTABLE_ADD_FUNC codecvt_base_do_encoding
    VTABLE_ADD_FUNC codecvt_wchar_do_in
    VTABLE_ADD_FUNC codecvt_wchar_do_out
    VTABLE_ADD_FUNC codecvt_wchar_do_unshift
    VTABLE_ADD_FUNC codecvt_wchar_do_length

START_VTABLE numpunct_char
    VTABLE_ADD_FUNC numpunct_char_vector_dtor
    VTABLE_ADD_FUNC numpunct_char_do_decimal_point
    VTABLE_ADD_FUNC numpunct_char_do_thousands_sep
    VTABLE_ADD_FUNC numpunct_char_do_grouping
    VTABLE_ADD_FUNC numpunct_char_do_falsename
    VTABLE_ADD_FUNC numpunct_char_do_truename

START_VTABLE numpunct_wchar
    VTABLE_ADD_FUNC numpunct_wchar_vector_dtor
    VTABLE_ADD_FUNC numpunct_wchar_do_decimal_point
    VTABLE_ADD_FUNC numpunct_wchar_do_thousands_sep
    VTABLE_ADD_FUNC numpunct_wchar_do_grouping
    VTABLE_ADD_FUNC numpunct_wchar_do_falsename
    VTABLE_ADD_FUNC numpunct_wchar_do_truename

START_VTABLE numpunct_short
    VTABLE_ADD_FUNC numpunct_wchar_vector_dtor
    VTABLE_ADD_FUNC numpunct_wchar_do_decimal_point
    VTABLE_ADD_FUNC numpunct_wchar_do_thousands_sep
    VTABLE_ADD_FUNC numpunct_wchar_do_grouping
    VTABLE_ADD_FUNC numpunct_wchar_do_falsename
    VTABLE_ADD_FUNC numpunct_wchar_do_truename

START_VTABLE num_get_char
    VTABLE_ADD_FUNC num_get_char_vector_dtor
    VTABLE_ADD_FUNC num_get_char_do_get_void
    VTABLE_ADD_FUNC num_get_char_do_get_double
    VTABLE_ADD_FUNC num_get_char_do_get_double
    VTABLE_ADD_FUNC num_get_char_do_get_float
    VTABLE_ADD_FUNC num_get_char_do_get_uint64
    VTABLE_ADD_FUNC num_get_char_do_get_int64
    VTABLE_ADD_FUNC num_get_char_do_get_ulong
    VTABLE_ADD_FUNC num_get_char_do_get_long
    VTABLE_ADD_FUNC num_get_char_do_get_uint
    VTABLE_ADD_FUNC num_get_char_do_get_ushort
    VTABLE_ADD_FUNC num_get_char_do_get_bool

START_VTABLE num_get_short
    VTABLE_ADD_FUNC num_get_wchar_vector_dtor
    VTABLE_ADD_FUNC num_get_short_do_get_void
    VTABLE_ADD_FUNC num_get_short_do_get_double
    VTABLE_ADD_FUNC num_get_short_do_get_double
    VTABLE_ADD_FUNC num_get_short_do_get_float
    VTABLE_ADD_FUNC num_get_short_do_get_uint64
    VTABLE_ADD_FUNC num_get_short_do_get_int64
    VTABLE_ADD_FUNC num_get_short_do_get_ulong
    VTABLE_ADD_FUNC num_get_short_do_get_long
    VTABLE_ADD_FUNC num_get_short_do_get_uint
    VTABLE_ADD_FUNC num_get_short_do_get_ushort
    VTABLE_ADD_FUNC num_get_short_do_get_bool

START_VTABLE num_get_wchar
    VTABLE_ADD_FUNC num_get_wchar_vector_dtor
    VTABLE_ADD_FUNC num_get_wchar_do_get_void
    VTABLE_ADD_FUNC num_get_wchar_do_get_double
    VTABLE_ADD_FUNC num_get_wchar_do_get_double
    VTABLE_ADD_FUNC num_get_wchar_do_get_float
    VTABLE_ADD_FUNC num_get_wchar_do_get_uint64
    VTABLE_ADD_FUNC num_get_wchar_do_get_int64
    VTABLE_ADD_FUNC num_get_wchar_do_get_ulong
    VTABLE_ADD_FUNC num_get_wchar_do_get_long
    VTABLE_ADD_FUNC num_get_wchar_do_get_uint
    VTABLE_ADD_FUNC num_get_wchar_do_get_ushort
    VTABLE_ADD_FUNC num_get_wchar_do_get_bool

START_VTABLE num_put_char
    VTABLE_ADD_FUNC num_put_char_vector_dtor
    VTABLE_ADD_FUNC num_put_char_do_put_ptr
    VTABLE_ADD_FUNC num_put_char_do_put_double
    VTABLE_ADD_FUNC num_put_char_do_put_double
    VTABLE_ADD_FUNC num_put_char_do_put_uint64
    VTABLE_ADD_FUNC num_put_char_do_put_int64
    VTABLE_ADD_FUNC num_put_char_do_put_ulong
    VTABLE_ADD_FUNC num_put_char_do_put_long
    VTABLE_ADD_FUNC num_put_char_do_put_bool

START_VTABLE num_put_wchar
    VTABLE_ADD_FUNC num_put_wchar_vector_dtor
    VTABLE_ADD_FUNC num_put_wchar_do_put_ptr
    VTABLE_ADD_FUNC num_put_wchar_do_put_double
    VTABLE_ADD_FUNC num_put_wchar_do_put_double
    VTABLE_ADD_FUNC num_put_wchar_do_put_uint64
    VTABLE_ADD_FUNC num_put_wchar_do_put_int64
    VTABLE_ADD_FUNC num_put_wchar_do_put_ulong
    VTABLE_ADD_FUNC num_put_wchar_do_put_long
    VTABLE_ADD_FUNC num_put_wchar_do_put_bool

START_VTABLE num_put_short
    VTABLE_ADD_FUNC num_put_wchar_vector_dtor
    VTABLE_ADD_FUNC num_put_short_do_put_ptr
    VTABLE_ADD_FUNC num_put_short_do_put_double
    VTABLE_ADD_FUNC num_put_short_do_put_double
    VTABLE_ADD_FUNC num_put_short_do_put_uint64
    VTABLE_ADD_FUNC num_put_short_do_put_int64
    VTABLE_ADD_FUNC num_put_short_do_put_ulong
    VTABLE_ADD_FUNC num_put_short_do_put_long
    VTABLE_ADD_FUNC num_put_short_do_put_bool

START_VTABLE time_put_char
    VTABLE_ADD_FUNC time_put_char_vector_dtor
    VTABLE_ADD_FUNC time_put_char_do_put

START_VTABLE time_put_wchar
    VTABLE_ADD_FUNC time_put_wchar_vector_dtor
    VTABLE_ADD_FUNC time_put_wchar_do_put

START_VTABLE time_put_short
    VTABLE_ADD_FUNC time_put_wchar_vector_dtor
    VTABLE_ADD_FUNC time_put_wchar_do_put

START_VTABLE time_get_char
    VTABLE_ADD_FUNC time_get_char_vector_dtor
    VTABLE_ADD_FUNC time_get_char_do_date_order
    VTABLE_ADD_FUNC time_get_char_do_get_time
    VTABLE_ADD_FUNC time_get_char_do_get_date
    VTABLE_ADD_FUNC time_get_char_do_get_weekday
    VTABLE_ADD_FUNC time_get_char_do_get_monthname
    VTABLE_ADD_FUNC time_get_char_do_get_year

START_VTABLE time_get_wchar
    VTABLE_ADD_FUNC time_get_wchar_vector_dtor
    VTABLE_ADD_FUNC time_get_wchar_do_date_order
    VTABLE_ADD_FUNC time_get_wchar_do_get_time
    VTABLE_ADD_FUNC time_get_wchar_do_get_date
    VTABLE_ADD_FUNC time_get_wchar_do_get_weekday
    VTABLE_ADD_FUNC time_get_wchar_do_get_monthname
    VTABLE_ADD_FUNC time_get_wchar_do_get_year
