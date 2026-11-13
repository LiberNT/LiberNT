#
# PROJECT:     LiberNT build system
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Stamp the build with the branch, commit, target, toolchain and time
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT OR NOT DEFINED BUILD_ARCH OR NOT DEFINED COMPILER_ID)
    message(FATAL_ERROR "buildstamp.cmake needs SOURCE_DIR, OUTPUT, BUILD_ARCH and COMPILER_ID")
endif()

if(COMPILER_ID STREQUAL "GNU")
    set(BUILD_TOOLCHAIN "gcc")
elseif(COMPILER_ID STREQUAL "MSVC")
    set(BUILD_TOOLCHAIN "msvc")
else()
    string(TOLOWER "${COMPILER_ID}" BUILD_TOOLCHAIN)
endif()

set(BUILD_BRANCH "no-branch")
set(BUILD_COMMIT "unknown")

find_program(GIT_COMMAND NAMES git)
if(GIT_COMMAND AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_COMMAND}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_BRANCH
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(GIT_RESULT EQUAL 0 AND GIT_BRANCH)
        if(GIT_BRANCH STREQUAL "HEAD")
            set(BUILD_BRANCH "detached")
        else()
            set(BUILD_BRANCH "${GIT_BRANCH}")
        endif()
    endif()

    execute_process(
        COMMAND "${GIT_COMMAND}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_COMMIT
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(GIT_RESULT EQUAL 0 AND GIT_COMMIT)
        set(BUILD_COMMIT "${GIT_COMMIT}")
    endif()
endif()

# The time stays as it is until the build moves, so a build that stamps the same
# tree the same way does not recompile what shows the stamp.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" STAMPED)
    if(STAMPED MATCHES "LIBERNT_BUILD_BRANCH +\"([^\"]*)\"")
        set(STAMPED_BRANCH "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_COMMIT +\"([^\"]*)\"")
        set(STAMPED_COMMIT "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_TIME +\"([^\"]*)\"")
        set(STAMPED_TIME "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_ARCH +\"([^\"]*)\"")
        set(STAMPED_ARCH "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_TOOLCHAIN +\"([^\"]*)\"")
        set(STAMPED_TOOLCHAIN "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED_TIME AND
       "${STAMPED_BRANCH}" STREQUAL "${BUILD_BRANCH}" AND
       "${STAMPED_COMMIT}" STREQUAL "${BUILD_COMMIT}" AND
       "${STAMPED_ARCH}" STREQUAL "${BUILD_ARCH}" AND
       "${STAMPED_TOOLCHAIN}" STREQUAL "${BUILD_TOOLCHAIN}")
        return()
    endif()
endif()

string(TIMESTAMP BUILD_TIME "%M%H-%d%m%y" UTC)
set(BUILD_STAMP "${BUILD_BRANCH}.${BUILD_COMMIT}.${BUILD_ARCH}.${BUILD_TOOLCHAIN}.${BUILD_TIME}")

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" TEMPORARY)
set(TEMPORARY "${OUTPUT}.${TEMPORARY}")
file(WRITE "${TEMPORARY}"
"/* Do not edit - Machine generated */
#ifndef _INC_LIBERNT_BUILDSTAMP
#define _INC_LIBERNT_BUILDSTAMP

#define LIBERNT_BUILD_BRANCH    \"${BUILD_BRANCH}\"
#define LIBERNT_BUILD_COMMIT    \"${BUILD_COMMIT}\"
#define LIBERNT_BUILD_ARCH      \"${BUILD_ARCH}\"
#define LIBERNT_BUILD_TOOLCHAIN \"${BUILD_TOOLCHAIN}\"
#define LIBERNT_BUILD_TIME      \"${BUILD_TIME}\"
#define LIBERNT_BUILD_STAMP     \"${BUILD_STAMP}\"
#define LIBERNT_BUILD_STAMP_W  L\"${BUILD_STAMP}\"

#endif
/* EOF */
")
file(RENAME "${TEMPORARY}" "${OUTPUT}")
