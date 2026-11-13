#
# PROJECT:     LiberNT build system
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Stamp the build with the branch, commit and date it was made from
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT OR NOT DEFINED BUILD_ARCH)
    message(FATAL_ERROR "buildstamp.cmake needs SOURCE_DIR, OUTPUT and BUILD_ARCH")
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

# The date stays as it is until the tree moves, so a build that stamps the same
# branch and commit does not recompile what shows the stamp.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" STAMPED)
    if(STAMPED MATCHES "LIBERNT_BUILD_BRANCH +\"([^\"]*)\"")
        set(STAMPED_BRANCH "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_COMMIT +\"([^\"]*)\"")
        set(STAMPED_COMMIT "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_DATE +\"([^\"]*)\"")
        set(STAMPED_DATE "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED MATCHES "LIBERNT_BUILD_ARCH +\"([^\"]*)\"")
        set(STAMPED_ARCH "${CMAKE_MATCH_1}")
    endif()
    if(STAMPED_DATE AND
       "${STAMPED_BRANCH}" STREQUAL "${BUILD_BRANCH}" AND
       "${STAMPED_COMMIT}" STREQUAL "${BUILD_COMMIT}" AND
       "${STAMPED_ARCH}" STREQUAL "${BUILD_ARCH}")
        return()
    endif()
endif()

string(TIMESTAMP BUILD_DATE "%d.%m.%y" UTC)
set(BUILD_STAMP "${BUILD_BRANCH}.${BUILD_COMMIT}.${BUILD_DATE}")

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" TEMPORARY)
set(TEMPORARY "${OUTPUT}.${TEMPORARY}")
file(WRITE "${TEMPORARY}"
"/* Do not edit - Machine generated */
#ifndef _INC_LIBERNT_BUILDSTAMP
#define _INC_LIBERNT_BUILDSTAMP

#define LIBERNT_BUILD_BRANCH   \"${BUILD_BRANCH}\"
#define LIBERNT_BUILD_COMMIT   \"${BUILD_COMMIT}\"
#define LIBERNT_BUILD_DATE     \"${BUILD_DATE}\"
#define LIBERNT_BUILD_ARCH     \"${BUILD_ARCH}\"
#define LIBERNT_BUILD_ARCH_W  L\"${BUILD_ARCH}\"
#define LIBERNT_BUILD_STAMP    \"${BUILD_STAMP}\"
#define LIBERNT_BUILD_STAMP_W L\"${BUILD_STAMP}\"

#endif
/* EOF */
")
file(RENAME "${TEMPORARY}" "${OUTPUT}")
