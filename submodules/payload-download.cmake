# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
# Downloads a GitHub release asset, retrying transient failures. GitHub throttles
# anonymous downloads from shared addresses such as CI runners and ignores tokens
# on github.com download links, so with GITHUB_TOKEN set the asset is fetched
# through the authenticated REST API instead.

include_guard(GLOBAL)

function(_libernt_find_release_asset repo tag name token url_var error_var transient_var)
    set(${transient_var} FALSE PARENT_SCOPE)
    set(_release_file "${CMAKE_BINARY_DIR}/CMakeFiles/release-${tag}.json")
    file(DOWNLOAD "https://api.github.com/repos/${repo}/releases/tags/${tag}" "${_release_file}"
        TLS_VERIFY ON
        STATUS _status
        HTTPHEADER "Accept: application/vnd.github+json"
        HTTPHEADER "Authorization: Bearer ${token}")
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        file(REMOVE "${_release_file}")
        set(${error_var} "release ${tag}: ${_status}" PARENT_SCOPE)
        set(${transient_var} TRUE PARENT_SCOPE)
        return()
    endif()
    file(READ "${_release_file}" _release)
    file(REMOVE "${_release_file}")
    string(JSON _count ERROR_VARIABLE _json_error LENGTH "${_release}" assets)
    if(_json_error)
        set(${error_var} "release ${tag}: ${_json_error}" PARENT_SCOPE)
        return()
    endif()
    if(_count GREATER 0)
        math(EXPR _last "${_count} - 1")
        foreach(_index RANGE ${_last})
            string(JSON _asset_name GET "${_release}" assets ${_index} name)
            if(_asset_name STREQUAL name)
                string(JSON _asset_url GET "${_release}" assets ${_index} url)
                set(${url_var} "${_asset_url}" PARENT_SCOPE)
                set(${error_var} "" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endif()
    set(${error_var} "release ${tag} has no asset ${name}" PARENT_SCOPE)
endfunction()

function(libernt_download_payload repo tag name file sha256 error_var)
    if(EXISTS "${file}")
        file(SHA256 "${file}" _have_hash)
        if(_have_hash STREQUAL sha256)
            set(${error_var} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(_token "$ENV{GITHUB_TOKEN}")
    if(CMAKE_VERSION VERSION_LESS 3.19)
        set(_token "")
    endif()
    set(_asset_url "")

    message(STATUS "Downloading https://github.com/${repo}/releases/download/${tag}/${name}")
    set(_error "")
    set(_delay 10)
    foreach(_attempt RANGE 1 4)
        if(_attempt GREATER 1)
            message(STATUS "Download failed (${_error}); retrying in ${_delay} s")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${_delay})
            math(EXPR _delay "${_delay} * 2")
        endif()
        if(_token STREQUAL "")
            file(DOWNLOAD "https://github.com/${repo}/releases/download/${tag}/${name}" "${file}.part"
                TLS_VERIFY ON
                STATUS _status)
        else()
            if(_asset_url STREQUAL "")
                _libernt_find_release_asset("${repo}" "${tag}" "${name}" "${_token}" _asset_url _error _transient)
                if(NOT _error STREQUAL "" AND _transient)
                    continue()
                elseif(NOT _error STREQUAL "")
                    set(${error_var} "${_error}" PARENT_SCOPE)
                    return()
                endif()
            endif()
            file(DOWNLOAD "${_asset_url}" "${file}.part"
                TLS_VERIFY ON
                STATUS _status
                HTTPHEADER "Accept: application/octet-stream"
                HTTPHEADER "Authorization: Bearer ${_token}")
        endif()
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${file}.part")
            set(_error "${_status}")
            continue()
        endif()
        file(SHA256 "${file}.part" _have_hash)
        if(NOT _have_hash STREQUAL sha256)
            file(REMOVE "${file}.part")
            set(${error_var} "SHA256 ${_have_hash}, expected ${sha256}" PARENT_SCOPE)
            return()
        endif()
        file(RENAME "${file}.part" "${file}")
        set(${error_var} "" PARENT_SCOPE)
        return()
    endforeach()
    set(${error_var} "${_error}" PARENT_SCOPE)
endfunction()
