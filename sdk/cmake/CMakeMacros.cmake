
function(add_dependency_node _node)
    if(GENERATE_DEPENDENCY_GRAPH)
        get_target_property(_type ${_node} TYPE)
        if(_type MATCHES SHARED_LIBRARY|MODULE_LIBRARY OR ${_node} MATCHES ntoskrnl)
            file(APPEND ${REACTOS_BINARY_DIR}/dependencies.graphml "    <node id=\"${_node}\"/>\n")
        endif()
     endif()
endfunction()

function(add_dependency_edge _source _target)
    if(GENERATE_DEPENDENCY_GRAPH)
        get_target_property(_type ${_source} TYPE)
        if(_type MATCHES SHARED_LIBRARY|MODULE_LIBRARY)
            file(APPEND ${REACTOS_BINARY_DIR}/dependencies.graphml "    <edge source=\"${_source}\" target=\"${_target}\"/>\n")
        endif()
    endif()
endfunction()

function(add_dependency_header)
    if(GENERATE_DEPENDENCY_GRAPH)
        file(WRITE ${REACTOS_BINARY_DIR}/dependencies.graphml "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<graphml>\n  <graph id=\"ReactOS dependencies\" edgedefault=\"directed\">\n")
    endif()
endfunction()

function(add_dependency_footer)
    if(GENERATE_DEPENDENCY_GRAPH)
        add_dependency_node(ntdll)
        file(APPEND ${REACTOS_BINARY_DIR}/dependencies.graphml "  </graph>\n</graphml>\n")
    endif()
endfunction()

function(add_message_headers _type)
    set(_files ${ARGN})
    set(_resource_only FALSE)
    list(FIND _files RESOURCE_ONLY _resource_only_index)
    if(NOT _resource_only_index EQUAL -1)
        set(_resource_only TRUE)
        list(REMOVE_AT _files ${_resource_only_index})
    endif()

    if(${_type} STREQUAL UNICODE)
        set(_flag "-U")
    else()
        set(_flag "-A")
    endif()
    foreach(_file ${_files})
        get_filename_component(_file_name ${_file} NAME_WE)
        set(_converted_file ${CMAKE_CURRENT_BINARY_DIR}/${_file}) ## ${_file_name}.mc
        set(_source_file ${CMAKE_CURRENT_SOURCE_DIR}/${_file})    ## ${_file_name}.mc
        if(_resource_only)
            set(_header_dir ${CMAKE_CURRENT_BINARY_DIR}/message_headers)
            set(_target_name ${_file_name}_message_resources)
        else()
            set(_header_dir ${CMAKE_CURRENT_BINARY_DIR})
            set(_target_name ${_file_name})
        endif()
        set(_mc_depends "${_converted_file}")
        if(TARGET ${CMAKE_MC_COMPILER})
            list(APPEND _mc_depends ${CMAKE_MC_COMPILER})
        endif()
        utf16le_convert(${_source_file} ${_converted_file} nobom)
        add_custom_command(
            OUTPUT ${_header_dir}/${_file_name}.h ${CMAKE_CURRENT_BINARY_DIR}/${_file_name}.rc
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_header_dir}
            COMMAND ${CMAKE_MC_COMPILER} -u ${_flag} -b -h ${_header_dir}/ -r ${CMAKE_CURRENT_BINARY_DIR}/ ${_converted_file}
            DEPENDS ${_mc_depends})
        set_source_files_properties(
            ${_header_dir}/${_file_name}.h ${CMAKE_CURRENT_BINARY_DIR}/${_file_name}.rc
            PROPERTIES GENERATED TRUE)
        add_custom_target(${_target_name} ALL DEPENDS ${_header_dir}/${_file_name}.h ${CMAKE_CURRENT_BINARY_DIR}/${_file_name}.rc)
    endforeach()
endfunction()

function(add_link name path)
    cmake_parse_arguments(_LINK "MINIMIZE" "WORKDIR;CMDLINE_ARGS;ICON;ICON_INDEX;GUID" "" ${ARGN})

    if(DEFINED _LINK_WORKDIR)
        set(_LINK_WORKDIR -w "${_LINK_WORKDIR}")
    endif()
    if(DEFINED _LINK_CMDLINE_ARGS)
        set(_LINK_CMDLINE_ARGS -c "${_LINK_CMDLINE_ARGS}")
    endif()

    if(DEFINED _LINK_ICON)
        #set(_LINK_ICON -i "${_LINK_ICON}")
        #if(DEFINED _LINK_ICON_INDEX)
        #    set(_LINK_ICON "${_LINK_ICON},${_LINK_ICON_INDEX}")
        #endif()
        if(DEFINED _LINK_ICON_INDEX)
            set(_LINK_ICON -i "${_LINK_ICON},${_LINK_ICON_INDEX}")
        else()
            set(_LINK_ICON -i "${_LINK_ICON}")
        endif()
    elseif(DEFINED _LINK_ICON_INDEX)
        set(_LINK_ICON -i ${_LINK_ICON_INDEX})
    endif()

    if(DEFINED _LINK_GUID)
        set(_LINK_GUID -g "${_LINK_GUID}")
    endif()

    if(_LINK_MINIMIZE)
        set(_LINK_MINIMIZE -m)
    else()
        set(_LINK_MINIMIZE)
    endif()

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.lnk
        COMMAND native-mkshelllink -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.lnk ${_LINK_WORKDIR} ${_LINK_CMDLINE_ARGS} ${_LINK_ICON} ${_LINK_GUID} ${_LINK_MINIMIZE} ${path}
        DEPENDS native-mkshelllink
        VERBATIM)
endfunction()

function(add_cd_file)
    cmake_parse_arguments(_CD "NO_CAB;OPTIONAL" "DESTINATION;NAME_ON_CD;TARGET" "FILE;FOR" ${ARGN})
    if(NOT (_CD_TARGET OR _CD_FILE))
        message(FATAL_ERROR "You must provide a target or a file to install!")
    endif()

    if(NOT _CD_DESTINATION)
        message(FATAL_ERROR "You must provide a destination")
    elseif(${_CD_DESTINATION} STREQUAL root)
        set(_CD_DESTINATION "")
    endif()

    if(NOT _CD_FOR)
        message(FATAL_ERROR "You must provide a cd name (or \"all\" for all of them) to install the file on!")
    endif()

    # Trust exact OS build artifacts, independently of their runtime path.
    # The kernel embeds these hashes for code-integrity image policy checks.
    if(_CD_TARGET AND NOT _CD_OPTIONAL AND _CD_DESTINATION MATCHES "^reactos/(system32|winsxs)(/|$)")
        if(TARGET ${_CD_TARGET})
            get_target_property(_ci_module_type ${_CD_TARGET} REACTOS_MODULE_TYPE)
            if(_ci_module_type MATCHES "^(nativedll|win32dll|win32ocx|cpl|module)$")
                set_property(GLOBAL APPEND PROPERTY CI_SYSTEM_TARGETS ${_CD_TARGET})
            endif()
        endif()
    endif()

    if(_CD_OPTIONAL)
        set(_cd_list OPTIONAL_FILE_LIST)
    else()
        set(_cd_list FILE_LIST)
    endif()

    # get file if we need to
    if(NOT _CD_FILE)
        set(_CD_FILE "$<TARGET_FILE:${_CD_TARGET}>")
        if(NOT _CD_NAME_ON_CD)
            set(_CD_NAME_ON_CD "$<TARGET_FILE_NAME:${_CD_TARGET}>")
        endif()
    endif()

    # do we add it to all CDs?
    set(_cd_for_all FALSE)
    list(FIND _CD_FOR "all" __cd)
    if(NOT __cd EQUAL -1)
        set(_cd_for_all TRUE)
        list(REMOVE_ITEM _CD_FOR "all")
        list(APPEND _CD_FOR "bootcd;livecd;preinstall")
    endif()

    # do we add it to bootcd?
    list(FIND _CD_FOR bootcd __cd)
    if(NOT __cd EQUAL -1 AND NOT _cd_for_all)
        if(_CD_NO_CAB)
            # setup file - replace the "reactos/" directory name by the current build architecture name
            # WARNING: CMake REGEXes are always case-sensitive!
            string(REGEX REPLACE "^reactos([\\\\/]+|$)" "${ARCH}\\1" _CD_ARCH_DESTINATION "${_CD_DESTINATION}")
        else()
            set(_CD_ARCH_DESTINATION "${_CD_DESTINATION}")
        endif()
        foreach(item ${_CD_FILE})
            if(_CD_NAME_ON_CD)
                # rename it in the cd tree
                set(__file ${_CD_NAME_ON_CD})
            else()
                get_filename_component(__file ${item} NAME)
            endif()
            set_property(GLOBAL APPEND PROPERTY BOOTCD_${_cd_list} "${_CD_ARCH_DESTINATION}/${__file}=${item}")
        endforeach()
        # manage dependency
        if(_CD_TARGET AND NOT _CD_OPTIONAL)
            add_dependencies(bootcd ${_CD_TARGET} registry_inf)
        endif()
    endif() #end bootcd

    # do we add it to livecd?
    list(FIND _CD_FOR livecd __cd)
    if(NOT __cd EQUAL -1)
        # manage dependency
        if(_CD_TARGET AND NOT _CD_OPTIONAL)
            add_dependencies(livecd ${_CD_TARGET} registry_inf)
        endif()
        foreach(item ${_CD_FILE})
            if(_CD_NAME_ON_CD)
                # rename it in the cd tree
                set(__file ${_CD_NAME_ON_CD})
            else()
                get_filename_component(__file ${item} NAME)
            endif()
            set_property(GLOBAL APPEND PROPERTY LIVECD_${_cd_list} "${_CD_DESTINATION}/${__file}=${item}")
        endforeach()
    endif() #end livecd


    # do we add it to preinstall?
    list(FIND _CD_FOR preinstall __cd)
    if(NOT __cd EQUAL -1)
        # manage dependency
        if(_CD_TARGET AND NOT _CD_OPTIONAL)
            add_dependencies(preinstall_partition ${_CD_TARGET} registry_inf)
        endif()
        foreach(item ${_CD_FILE})
            if(_CD_NAME_ON_CD)
                # rename it in the cd tree
                set(__file ${_CD_NAME_ON_CD})
            else()
                get_filename_component(__file ${item} NAME)
            endif()
            set_property(GLOBAL APPEND PROPERTY PREINSTALL_${_cd_list} "${_CD_DESTINATION}/${__file}=${item}")
        endforeach()
    endif() #end preinstall
endfunction()

function(create_txtsetup_sif)
    set(_source ${REACTOS_SOURCE_DIR}/boot/bootdata/txtsetup.sif)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_source})

    file(GENERATE
         OUTPUT ${REACTOS_BINARY_DIR}/boot/bootdata/txtsetup.$<CONFIG>.sif
         INPUT ${_source})
endfunction()

function(create_iso_lists)
    create_txtsetup_sif()

if(FALSE) ## Disabled until we want a RAMDISK ISO
    # Add the LiveImage into the BootCD
    add_cd_file(
        TARGET livecd
        FILE ${CMAKE_CURRENT_BINARY_DIR}/liveimg.iso
        DESTINATION root
        NO_CAB FOR bootcd)
endif()

    # Write the LiveImage file list
    get_property(_filelist GLOBAL PROPERTY LIVECD_FILE_LIST)
    if(_filelist)
        string(REPLACE ";" "\n" _filelist "${_filelist}")
        file(APPEND ${REACTOS_BINARY_DIR}/boot/livecd.cmake.lst "${_filelist}\n")
    endif()
    unset(_filelist)

    # Build-local LiveCD overlays are written after the regular image entries
    # so that a matching destination replaces the default file.
    get_property(_filelist GLOBAL PROPERTY LIVECD_OVERLAY_FILE_LIST)
    if(_filelist)
        string(REPLACE ";" "\n" _filelist "${_filelist}")
        file(APPEND ${REACTOS_BINARY_DIR}/boot/livecd.cmake.lst "${_filelist}\n")
    endif()
    unset(_filelist)
    file(GENERATE
         OUTPUT ${REACTOS_BINARY_DIR}/boot/livecd.$<CONFIG>.lst
         INPUT ${REACTOS_BINARY_DIR}/boot/livecd.cmake.lst)

    # Write the BootCD file list
    get_property(_filelist GLOBAL PROPERTY BOOTCD_FILE_LIST)
    string(REPLACE ";" "\n" _filelist "${_filelist}")
    file(APPEND ${REACTOS_BINARY_DIR}/boot/bootcd.cmake.lst "${_filelist}")
    unset(_filelist)
    # Also, append the file contents list of the LiveImage to the BootCD file list
    file(APPEND ${REACTOS_BINARY_DIR}/boot/bootcd.cmake.lst "\n")
    file(READ ${REACTOS_BINARY_DIR}/boot/livecd.cmake.lst _filelist)
    file(APPEND ${REACTOS_BINARY_DIR}/boot/bootcd.cmake.lst "${_filelist}")
    unset(_filelist)
    file(GENERATE
         OUTPUT ${REACTOS_BINARY_DIR}/boot/bootcd.$<CONFIG>.lst
         INPUT ${REACTOS_BINARY_DIR}/boot/bootcd.cmake.lst)

    get_property(_filelist GLOBAL PROPERTY PREINSTALL_FILE_LIST)
    if(_filelist)
        string(REPLACE ";" "\n" _filelist "${_filelist}")
        file(APPEND ${REACTOS_BINARY_DIR}/boot/preinstall.cmake.lst "${_filelist}\n")
    endif()
    unset(_filelist)

    # Build-local preinstall overlays are deliberately written after every
    # regular image entry so that a matching destination replaces the default.
    get_property(_filelist GLOBAL PROPERTY PREINSTALL_OVERLAY_FILE_LIST)
    if(_filelist)
        string(REPLACE ";" "\n" _filelist "${_filelist}")
        file(APPEND ${REACTOS_BINARY_DIR}/boot/preinstall.cmake.lst "${_filelist}\n")
    endif()
    unset(_filelist)
    file(GENERATE
         OUTPUT ${REACTOS_BINARY_DIR}/boot/preinstall.$<CONFIG>.lst
         INPUT ${REACTOS_BINARY_DIR}/boot/preinstall.cmake.lst)

    foreach(_image livecd bootcd preinstall)
        string(TOUPPER "${_image}" _property)
        get_property(_filelist GLOBAL PROPERTY ${_property}_OPTIONAL_FILE_LIST)
        if(_image STREQUAL "bootcd")
            get_property(_livecd_filelist GLOBAL PROPERTY LIVECD_OPTIONAL_FILE_LIST)
            list(APPEND _filelist ${_livecd_filelist})
        endif()
        string(REPLACE ";" "\n" _filelist "${_filelist}")
        file(WRITE ${REACTOS_BINARY_DIR}/boot/${_image}.optional.cmake.lst "${_filelist}\n")
        file(GENERATE
             OUTPUT ${REACTOS_BINARY_DIR}/boot/${_image}.optional.$<CONFIG>.lst
             INPUT ${REACTOS_BINARY_DIR}/boot/${_image}.optional.cmake.lst)
    endforeach()
endfunction()

# Create module_clean targets
function(add_clean_target _target)
    set(_clean_working_directory ${CMAKE_CURRENT_BINARY_DIR})
    if(CMAKE_GENERATOR STREQUAL "Unix Makefiles" OR CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
        set(_clean_command make clean)
    elseif(CMAKE_GENERATOR STREQUAL "NMake Makefiles")
        set(_clean_command nmake /nologo clean)
    elseif(CMAKE_GENERATOR STREQUAL "Ninja")
        set(_clean_command ninja -t clean ${_target})
        set(_clean_working_directory ${REACTOS_BINARY_DIR})
    endif()
    add_custom_target(${_target}_clean
        COMMAND ${_clean_command}
        WORKING_DIRECTORY ${_clean_working_directory}
        COMMENT "Cleaning ${_target}")
endfunction()

if(NOT MSVC_IDE)
    function(add_library name)
        _add_library(${name} ${ARGN})
        add_clean_target(${name})
        # cmake adds a module_EXPORTS define when compiling a module or a shared library. We don't use that.
        get_target_property(_type ${name} TYPE)
        if(_type MATCHES SHARED_LIBRARY|MODULE_LIBRARY)
            set_target_properties(${name} PROPERTIES DEFINE_SYMBOL "")
        endif()
    endfunction()

    function(add_executable name)
        _add_executable(${name} ${ARGN})
        add_clean_target(${name})
    endfunction()
elseif(USE_FOLDER_STRUCTURE)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    string(LENGTH ${CMAKE_SOURCE_DIR} CMAKE_SOURCE_DIR_LENGTH)

    function(add_custom_target name)
        _add_custom_target(${name} ${ARGN})
        string(SUBSTRING ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR_LENGTH} -1 CMAKE_CURRENT_SOURCE_DIR_RELATIVE)
        set_property(TARGET "${name}" PROPERTY FOLDER "${CMAKE_CURRENT_SOURCE_DIR_RELATIVE}")
    endfunction()

    function(add_library name)
        _add_library(${name} ${ARGN})
        get_target_property(_type ${name} TYPE)
        if (NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_target_excluded ${name} EXCLUDE_FROM_ALL)
            if(_target_excluded AND ${name} MATCHES "^lib.*")
                set_property(TARGET "${name}" PROPERTY FOLDER "Importlibs")
            else()
                string(SUBSTRING ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR_LENGTH} -1 CMAKE_CURRENT_SOURCE_DIR_RELATIVE)
                set_property(TARGET "${name}" PROPERTY FOLDER "${CMAKE_CURRENT_SOURCE_DIR_RELATIVE}")
            endif()
        endif()
        # cmake adds a module_EXPORTS define when compiling a module or a shared library. We don't use that.
        if(_type MATCHES SHARED_LIBRARY|MODULE_LIBRARY)
            set_target_properties(${name} PROPERTIES DEFINE_SYMBOL "")
        endif()
    endfunction()

    function(add_executable name)
        _add_executable(${name} ${ARGN})
        string(SUBSTRING ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR_LENGTH} -1 CMAKE_CURRENT_SOURCE_DIR_RELATIVE)
        set_property(TARGET "${name}" PROPERTY FOLDER "${CMAKE_CURRENT_SOURCE_DIR_RELATIVE}")
    endfunction()
else()
    function(add_library name)
        _add_library(${name} ${ARGN})
        # cmake adds a module_EXPORTS define when compiling a module or a shared library. We don't use that.
        get_target_property(_type ${name} TYPE)
        if(_type MATCHES SHARED_LIBRARY|MODULE_LIBRARY)
            set_target_properties(${name} PROPERTIES DEFINE_SYMBOL "")
        endif()
    endfunction()
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    function(concatenate_files _output _file1)
        file(TO_NATIVE_PATH "${_output}" _real_output)
        file(TO_NATIVE_PATH "${_file1}" _file_list)
        foreach(_file ${ARGN})
            file(TO_NATIVE_PATH "${_file}" _real_file)
            set(_file_list "${_file_list} + ${_real_file}")
        endforeach()
        add_custom_command(
            OUTPUT ${_output}
            COMMAND cmd.exe /C "copy /Y /B ${_file_list} ${_real_output} > nul"
            DEPENDS ${_file1} ${ARGN})
    endfunction()
else()
    macro(concatenate_files _output)
        add_custom_command(
            OUTPUT ${_output}
            COMMAND cat ${ARGN} > ${_output}
            DEPENDS ${ARGN})
    endmacro()
endif()

function(add_importlibs _module)
    add_dependency_node(${_module})

    if(ARM64EC_RUNTIME AND NOT _module STREQUAL "ntdll_chpe")
        # Prefer native ABI bridge exports over regular ntdll syscall stubs.
        list(FIND ARGN "ntdll" _ntdll_index)
        if(NOT _ntdll_index EQUAL -1)
            target_link_libraries(${_module} libntdll_chpe)
            add_dependency_edge(${_module} ntdll_chpe)
        endif()
    endif()

    foreach(LIB ${ARGN})
        target_link_libraries(${_module} lib${LIB})
        add_dependency_edge(${_module} ${LIB})
    endforeach()
endfunction()

# Some helper lists
list(APPEND VALID_MODULE_TYPES kernel kerneldll kernelmodedriver kmdfdriver wdmdriver nativecui nativedll win32cui win32gui win32dll win32ocx cpl module)
list(APPEND KERNEL_MODULE_TYPES kernel kerneldll kernelmodedriver kmdfdriver wdmdriver)
list(APPEND NATIVE_MODULE_TYPES kernel kerneldll kernelmodedriver kmdfdriver wdmdriver nativecui nativedll)

# Signs a driver if it is kernelmodedriver or wdmdriver if the cert exists
function(sign_driver_if_needed TARGET)
    get_target_property(_type ${TARGET} REACTOS_MODULE_TYPE)
    if(NOT _type)
        message(STATUS "sign_driver_if_needed: No REACTOS_MODULE_TYPE for ${TARGET}")
        return()
    endif()
    if(NOT (_type STREQUAL "kernelmodedriver" OR _type STREQUAL "wdmdriver"))
        return()
    endif()
    if(NOT MSVC)
        return()
    endif()
    if(NOT EXISTS "C:/ReactOSCerts/ReactOSDevCert.cer")
        return()
    endif()
    # Get output file name
    get_target_property(_output_name ${TARGET} OUTPUT_NAME)
    if(NOT _output_name)
        set(_output_name ${TARGET})
    endif()
    set(_driver_path "${CMAKE_CURRENT_BINARY_DIR}/${_output_name}.sys")
    set(_driver_path "$<TARGET_FILE:${TARGET}>")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND SignTool sign /v /fd sha1 /s PrivateCertStore /n reactos.org /t http://timestamp.digicert.com "${_driver_path}"
        COMMENT "Signing driver: ${_driver_path}")
endfunction()

# Example usage after driver target creation:
# add_library(my_driver ...)
# set_module_type(my_driver kernelmodedriver)
# sign_driver_if_needed(my_driver)

function(set_module_type MODULE TYPE)
    cmake_parse_arguments(__module "UNICODE" "IMAGEBASE;KMDF_VERSION;KMDF_MINIMUM_VERSION" "ENTRYPOINT" ${ARGN})

    if(__module_UNPARSED_ARGUMENTS)
        message(STATUS "set_module_type : unparsed arguments ${__module_UNPARSED_ARGUMENTS}, module : ${MODULE}")
    endif()

    # Check this is a type that we know
    if (NOT TYPE IN_LIST VALID_MODULE_TYPES)
        message(FATAL_ERROR "Unknown type ${TYPE} for module ${MODULE}")
    endif()

    # Set our target property
    set_target_properties(${MODULE} PROPERTIES REACTOS_MODULE_TYPE ${TYPE})

    # Add the module to the module group list, if it is defined
    if(DEFINED CURRENT_MODULE_GROUP)
        set_property(GLOBAL APPEND PROPERTY ${CURRENT_MODULE_GROUP}_MODULE_LIST "${MODULE}")
    endif()

    # Set subsystem.
    if(TYPE IN_LIST NATIVE_MODULE_TYPES)
        set_subsystem(${MODULE} native)
    elseif(${TYPE} STREQUAL win32cui)
        set_subsystem(${MODULE} console)
    elseif(${TYPE} STREQUAL win32gui)
        set_subsystem(${MODULE} windows)
    endif()

    # Set unicode definitions
    if(__module_UNICODE)
        target_compile_definitions(${MODULE} PRIVATE UNICODE _UNICODE)
    endif()

    if(TYPE IN_LIST KERNEL_MODULE_TYPES)
        target_compile_definitions(${MODULE} PRIVATE _GCC_SAL_CHECK_RETURN)
    endif()

    # Set entry point
    if(__module_ENTRYPOINT OR (__module_ENTRYPOINT STREQUAL "0"))
        set_entrypoint(${MODULE} ${__module_ENTRYPOINT})
    elseif(${TYPE} STREQUAL nativecui)
        set_entrypoint(${MODULE} NtProcessStartup 4)
    elseif(${TYPE} STREQUAL win32cui)
        if(__module_UNICODE)
            set_entrypoint(${MODULE} wmainCRTStartup)
        else()
            set_entrypoint(${MODULE} mainCRTStartup)
        endif()
    elseif(${TYPE} STREQUAL win32gui)
        if(__module_UNICODE)
            set_entrypoint(${MODULE} wWinMainCRTStartup)
        else()
            set_entrypoint(${MODULE} WinMainCRTStartup)
        endif()
    elseif((${TYPE} STREQUAL win32dll) OR (${TYPE} STREQUAL win32ocx)
            OR (${TYPE} STREQUAL cpl))
        set_entrypoint(${MODULE} DllMainCRTStartup 12)
    elseif((${TYPE} STREQUAL kernelmodedriver) OR (${TYPE} STREQUAL wdmdriver))
        set_entrypoint(${MODULE} DriverEntry 8)
    elseif(${TYPE} STREQUAL kmdfdriver)
        set_entrypoint(${MODULE} FxDriverEntry 8)
    elseif(${TYPE} STREQUAL nativedll)
        set_entrypoint(${MODULE} DllMain 12)
    elseif(TYPE STREQUAL kernel)
        set_entrypoint(${MODULE} KiSystemStartup 4)
    elseif(${TYPE} STREQUAL module)
        set_entrypoint(${MODULE} 0)
    endif()

    # Set base address
    # Use 'IMAGEBASE default' to skip these set_image_base(), especially for win32dll test files
    if(__module_IMAGEBASE)
        if(NOT ${__module_IMAGEBASE} STREQUAL "default")
            set_image_base(${MODULE} ${__module_IMAGEBASE})
        endif()
    elseif(${TYPE} STREQUAL win32dll)
        if(DEFINED baseaddress_${MODULE})
            set_image_base(${MODULE} ${baseaddress_${MODULE}})
        else()
            message(STATUS "${MODULE} has no base address")
        endif()
    elseif(TYPE IN_LIST KERNEL_MODULE_TYPES)
        # special case for kernel
        if (TYPE STREQUAL kernel)
            set_image_base(${MODULE} 0x00400000)
        elseif(ARCH STREQUAL "arm64")
            set_image_base(${MODULE} 0x140000000)
        else()
            set_image_base(${MODULE} 0x00010000)
        endif()
    endif()

    # Now do some stuff which is specific to each type
    if(TYPE IN_LIST KERNEL_MODULE_TYPES)
        add_dependencies(${MODULE} bugcodes xdk)
        if((${TYPE} STREQUAL kernelmodedriver) OR
           (${TYPE} STREQUAL kmdfdriver) OR
           (${TYPE} STREQUAL wdmdriver))
            set_target_properties(${MODULE} PROPERTIES SUFFIX ".sys")
            sign_driver_if_needed(${MODULE})
        endif()
    endif()

    if(TYPE STREQUAL kernel)
        # Kernels are executables with exports
        set_target_properties(${MODULE}
            PROPERTIES
            ENABLE_EXPORTS TRUE
            DEFINE_SYMBOL "")
    endif()

    if(TYPE STREQUAL kmdfdriver)
        if(NOT __module_KMDF_VERSION)
            set(__module_KMDF_VERSION "1.17")
        endif()

        if(NOT __module_KMDF_VERSION MATCHES "^1\\.([0-9]+)$")
            message(FATAL_ERROR "Invalid KMDF version '${__module_KMDF_VERSION}' for ${MODULE}")
        endif()

        set(_kmdf_minor ${CMAKE_MATCH_1})
        set(_kmdf_include_dir ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/${__module_KMDF_VERSION})
        if(NOT EXISTS ${_kmdf_include_dir}/wdf.h)
            message(FATAL_ERROR "KMDF ${__module_KMDF_VERSION} headers are not available for ${MODULE}")
        endif()

        if(__module_KMDF_VERSION STREQUAL "1.17")
            set(_wdfdriverentry_target wdfdriverentry)
        else()
            string(REPLACE "." "_" _kmdf_version_suffix ${__module_KMDF_VERSION})
            set(_wdfdriverentry_target wdfdriverentry_${_kmdf_version_suffix})
        endif()

        target_include_directories(${MODULE} PUBLIC ${_kmdf_include_dir})
        target_compile_definitions(${MODULE} PRIVATE
            KMDF_VERSION_MAJOR=1
            KMDF_VERSION_MINOR=${_kmdf_minor})

        if(__module_KMDF_MINIMUM_VERSION)
            if(NOT __module_KMDF_MINIMUM_VERSION MATCHES "^1\\.([0-9]+)$")
                message(FATAL_ERROR "Invalid minimum KMDF version '${__module_KMDF_MINIMUM_VERSION}' for ${MODULE}")
            endif()
            set(_kmdf_minimum_minor ${CMAKE_MATCH_1})
            if(_kmdf_minor LESS 25 OR
               _kmdf_minimum_minor LESS 25 OR
               _kmdf_minimum_minor GREATER _kmdf_minor)
                message(FATAL_ERROR
                    "KMDF minimum version ${__module_KMDF_MINIMUM_VERSION} is invalid for target ${__module_KMDF_VERSION}")
            endif()
            target_compile_definitions(${MODULE} PRIVATE
                KMDF_MINIMUM_VERSION_REQUIRED=${_kmdf_minimum_minor})
        endif()

        add_importlibs(${MODULE} wdfldr)
        target_link_libraries(${MODULE} ${_wdfdriverentry_target} wdmsec)
    endif()

    if(${TYPE} STREQUAL win32ocx)
        set_target_properties(${MODULE} PROPERTIES SUFFIX ".ocx")
    endif()

    if(${TYPE} STREQUAL cpl)
        set_target_properties(${MODULE} PROPERTIES SUFFIX ".cpl")
    endif()

    # Do compiler specific stuff
    set_module_type_toolchain(${MODULE} ${TYPE})
endfunction()

function(start_module_group __name)
    if(DEFINED CURRENT_MODULE_GROUP)
        message(FATAL_ERROR "CURRENT_MODULE_GROUP is already set ('${CURRENT_MODULE_GROUP}')")
    endif()
    set(CURRENT_MODULE_GROUP ${__name} PARENT_SCOPE)
endfunction()

function(end_module_group)
    get_property(__modulelist GLOBAL PROPERTY ${CURRENT_MODULE_GROUP}_MODULE_LIST)
    add_custom_target(${CURRENT_MODULE_GROUP})
    foreach(__module ${__modulelist})
        add_dependencies(${CURRENT_MODULE_GROUP} ${__module})
    endforeach()
    set(CURRENT_MODULE_GROUP PARENT_SCOPE)
endfunction()

function(utf16le_convert _in _out)
    add_custom_command(OUTPUT "${_out}"
                       COMMAND native-utf16le "${_in}" "${_out}" ${ARGN}
                       DEPENDS native-utf16le "${_in}")
    set_source_files_properties("${_out}" PROPERTIES GENERATED TRUE)
endfunction()

function(preprocess_file __in __out)
    set(__arg ${__in})
    foreach(__def ${ARGN})
        list(APPEND __arg -D${__def})
    endforeach()
    if(MSVC)
        add_custom_command(OUTPUT ${_out}
            COMMAND ${CMAKE_C_COMPILER} /EP ${__arg}
            DEPENDS ${__in})
    else()
        add_custom_command(OUTPUT ${_out}
            COMMAND ${CMAKE_C_COMPILER} -E ${__arg}
            DEPENDS ${__in})
    endif()
endfunction()

function(get_includes OUTPUT_VAR)
    get_directory_property(_includes INCLUDE_DIRECTORIES)
    foreach(arg ${_includes})
        list(APPEND __tmp_var -I${arg})
    endforeach()
    set(${OUTPUT_VAR} ${__tmp_var} PARENT_SCOPE)
endfunction()

function(get_defines OUTPUT_VAR)
    get_directory_property(_defines COMPILE_DEFINITIONS)
    foreach(arg ${_defines})
        # Skip generator expressions
        if (NOT arg MATCHES [[^\$<.*>$]])
            list(APPEND __tmp_var -D${arg})
        endif()
    endforeach()
    set(${OUTPUT_VAR} ${__tmp_var} PARENT_SCOPE)
endfunction()

# Wine marks a small number of sources with `#pragma makedep arm64ec_x64`.
# They contain genuine AMD64 call thunks which must be linked into the hybrid
# image as AMD64 objects; compiling their ARM64 branch changes the ABI.
function(add_arm64ec_x64_source OUTPUT_VAR SOURCE_FILE)
    if(NOT (ARCH STREQUAL "arm64" AND ARM64EC_RUNTIME))
        set(${OUTPUT_VAR} "${SOURCE_FILE}" PARENT_SCOPE)
        return()
    endif()

    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "ARM64EC x64 source objects require Clang")
    endif()

    get_filename_component(_source_path "${SOURCE_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_source_name "${SOURCE_FILE}" NAME_WE)
    set(_object_path "${CMAKE_CURRENT_BINARY_DIR}/${_source_name}.arm64ec_x64.obj")
    set(_depfile_path "${_object_path}.d")
    set(_x64_compiler "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")

    if(NOT EXISTS "${_x64_compiler}")
        message(FATAL_ERROR "ARM64EC x64 compiler not found: ${_x64_compiler}")
    endif()

    get_includes(_x64_includes)
    get_defines(_directory_defines)
    foreach(_define IN LISTS _directory_defines)
        if(NOT _define MATCHES "^-D(_ARM64EC_|_M_ARM64EC|__arm64ec__)(=.*)?$")
            list(APPEND _x64_defines "${_define}")
        endif()
    endforeach()
    foreach(_define IN LISTS ARGN)
        list(APPEND _x64_defines "-D${_define}")
    endforeach()
    list(APPEND _x64_defines -D__arm64ec_x64__)

    set(_debug_flags)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _debug_flags -gdwarf-2 -ggdb)
    endif()

    if(POLICY CMP0116)
        cmake_policy(SET CMP0116 NEW)
    endif()

    add_custom_command(
        OUTPUT "${_object_path}"
        COMMAND "${_x64_compiler}"
            --target=x86_64-w64-mingw32
            --sysroot="${REACTOS_CLANG_LLVM_MINGW_ROOT}"
            ${_x64_defines}
            -isystem "${CLANG_RESOURCE_DIR}/include"
            ${_x64_includes}
            -std=gnu99
            -ffile-prefix-map=${REACTOS_SOURCE_DIR}=
            -ffile-prefix-map=../../=
            -fms-extensions
            -fno-strict-aliasing
            -fno-common
            -nostdlibinc
            -Wno-microsoft
            -Wno-pragma-pack
            -O1
            -fno-optimize-sibling-calls
            -fno-omit-frame-pointer
            ${_debug_flags}
            -MMD -MF "${_depfile_path}" -MT "${_object_path}"
            -c "${_source_path}" -o "${_object_path}"
        DEPENDS "${_source_path}"
        DEPFILE "${_depfile_path}"
        COMMAND_EXPAND_LISTS
        VERBATIM)

    set_source_files_properties("${_object_path}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    set(${OUTPUT_VAR} "${_object_path}" PARENT_SCOPE)
endfunction()

function(add_registry_inf)
    # Add to the inf files list
    foreach(_file ${ARGN})
        if(IS_ABSOLUTE "${_file}")
            set(_source_file "${_file}")
        else()
            set(_source_file "${CMAKE_CURRENT_SOURCE_DIR}/${_file}")
        endif()
        set_property(GLOBAL APPEND PROPERTY REGISTRY_INF_LIST ${_source_file})
    endforeach()
endfunction()

function(add_optional_registry_inf)
    cmake_parse_arguments(_REG "" "INF;REQUIRES" "" ${ARGN})
    if(NOT _REG_INF OR NOT _REG_REQUIRES)
        message(FATAL_ERROR "add_optional_registry_inf requires INF and REQUIRES")
    endif()
    if(IS_ABSOLUTE "${_REG_INF}")
        set(_source_file "${_REG_INF}")
    else()
        set(_source_file "${CMAKE_CURRENT_SOURCE_DIR}/${_REG_INF}")
    endif()
    set_property(GLOBAL APPEND PROPERTY REGISTRY_OPTIONAL_INF_LIST "${_REG_REQUIRES}|${_source_file}")
endfunction()

function(create_registry_hives)

    # Register the launcher only when the source script has been populated.
    # Even an empty batch file would otherwise open a console at logon.
    set(_app_launcher "${CMAKE_SOURCE_DIR}/boot/bootdata/app_launcher.cmd")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_app_launcher}")
    file(READ "${_app_launcher}" _app_launcher_contents)
    string(STRIP "${_app_launcher_contents}" _app_launcher_contents)

    # Shortcut to the registry.inf file
    set(_registry_inf "${CMAKE_BINARY_DIR}/boot/bootdata/registry.inf")

    # Get the list of inf files
    get_property(_inf_files GLOBAL PROPERTY REGISTRY_INF_LIST)

    # Convert files to utf16le
    foreach(_file ${_inf_files})
        get_filename_component(_file_name ${_file} NAME_WE)
        file(RELATIVE_PATH _subdir ${CMAKE_SOURCE_DIR} ${_file})
        get_filename_component(_subdir ${_subdir}  DIRECTORY)
        set(_converted_file ${CMAKE_BINARY_DIR}/${_subdir}/${_file_name}_utf16.inf)
        utf16le_convert(${_file} ${_converted_file})
        list(APPEND _converted_files ${_converted_file})
    endforeach()

    get_property(_driver_infs GLOBAL PROPERTY DRIVER_INF_LIST)
    get_property(_driver_inf_targets GLOBAL PROPERTY DRIVER_INF_TARGETS)
    if(ARCH STREQUAL "i386")
        set(_driver_database_arch x86)
    else()
        set(_driver_database_arch ${ARCH})
    endif()
    set(_driver_database_list "${CMAKE_BINARY_DIR}/boot/bootdata/driverdb_infs.txt")
    set(_driver_database_inf "${CMAKE_BINARY_DIR}/boot/bootdata/driverdb.inf")
    string(REPLACE ";" "\n" _driver_database_text "${_driver_infs}")
    file(GENERATE OUTPUT ${_driver_database_list} CONTENT "${_driver_database_text}\n")
    add_custom_command(
        OUTPUT ${_driver_database_inf}
        COMMAND native-mkdrvdb ${_driver_database_arch} ${_driver_database_inf} ${_driver_database_list}
        DEPENDS native-mkdrvdb ${_driver_database_list} ${_driver_infs} ${_driver_inf_targets}
        VERBATIM)
    utf16le_convert(${_driver_database_inf} ${CMAKE_BINARY_DIR}/boot/bootdata/driverdb_utf16.inf)
    list(APPEND _converted_files ${CMAKE_BINARY_DIR}/boot/bootdata/driverdb_utf16.inf)

    # Concatenate all registry files to registry.inf
    set(_registry_base_inf "${CMAKE_BINARY_DIR}/boot/bootdata/registry_base.inf")
    concatenate_files(${_registry_base_inf} ${_converted_files})

    get_property(_optional_infs GLOBAL PROPERTY REGISTRY_OPTIONAL_INF_LIST)
    set(_optional_inf_list "")
    set(_converted_optional_files "")
    foreach(_entry ${_optional_infs})
        string(FIND "${_entry}" "|" _separator)
        string(SUBSTRING "${_entry}" 0 ${_separator} _required)
        math(EXPR _start "${_separator} + 1")
        string(SUBSTRING "${_entry}" ${_start} -1 _file)
        get_filename_component(_file_name ${_file} NAME_WE)
        file(RELATIVE_PATH _subdir ${CMAKE_SOURCE_DIR} ${_file})
        get_filename_component(_subdir ${_subdir} DIRECTORY)
        set(_converted_file ${CMAKE_BINARY_DIR}/${_subdir}/${_file_name}_utf16.inf)
        utf16le_convert(${_file} ${_converted_file})
        list(APPEND _converted_optional_files ${_converted_file})
        string(APPEND _optional_inf_list "${_required}|${_converted_file}\n")
    endforeach()
    file(WRITE ${CMAKE_BINARY_DIR}/boot/bootdata/registry_optional.txt "${_optional_inf_list}")

    add_custom_command(
        OUTPUT ${_registry_inf} ${_registry_inf}.always
        COMMAND ${CMAKE_COMMAND} -DMODE=binary
            -DBASE=${_registry_base_inf}
            -DOPTIONAL=${CMAKE_BINARY_DIR}/boot/bootdata/registry_optional.txt
            -DOUTPUT=${_registry_inf}
            -P ${CMAKE_SOURCE_DIR}/sdk/cmake/optional_files.cmake
        DEPENDS ${_registry_base_inf} ${_converted_optional_files}
        VERBATIM)
    set_source_files_properties(${_registry_inf}.always PROPERTIES SYMBOLIC TRUE)

    # Add registry.inf to bootcd
    add_custom_target(registry_inf DEPENDS ${_registry_inf})
    add_cd_file(TARGET registry_inf
                FILE ${_registry_inf}
                DESTINATION reactos
                NO_CAB FOR bootcd)

    # LiveCD hives
    list(APPEND _livecd_inf_files
        ${_registry_inf}
        ${CMAKE_SOURCE_DIR}/boot/bootdata/livecd.inf
        ${CMAKE_SOURCE_DIR}/boot/bootdata/caroots.inf)
    if(NOT "${_app_launcher_contents}" STREQUAL "")
        list(APPEND _livecd_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/app_launcher.inf)
    endif()
    if(REACTOS_USE_XPDM AND (SARCH STREQUAL "xbox"))
        list(APPEND _livecd_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst_xbox.inf)
    elseif(REACTOS_USE_XPDM AND (SARCH STREQUAL "pc98"))
        list(APPEND _livecd_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst_pc98.inf)
    else()
        list(APPEND _livecd_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst.inf)
    endif()
    foreach(_livecd_extra_registry_inf IN LISTS LIVECD_EXTRA_REGISTRY_INF)
        if(_livecd_extra_registry_inf STREQUAL "")
            continue()
        endif()
        get_filename_component(_livecd_extra_registry_inf "${_livecd_extra_registry_inf}" ABSOLUTE BASE_DIR "${REACTOS_BINARY_DIR}")
        list(APPEND _livecd_inf_files ${_livecd_extra_registry_inf})
    endforeach()

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/boot/bootdata/system
               ${CMAKE_BINARY_DIR}/boot/bootdata/software
               ${CMAKE_BINARY_DIR}/boot/bootdata/default
               ${CMAKE_BINARY_DIR}/boot/bootdata/sam
               ${CMAKE_BINARY_DIR}/boot/bootdata/security
        COMMAND native-mkhive -h:SYSTEM,SOFTWARE,DEFAULT,SAM,SECURITY -d:${CMAKE_BINARY_DIR}/boot/bootdata ${_livecd_inf_files}
        DEPENDS native-mkhive ${_livecd_inf_files})

    add_custom_target(livecd_hives
        DEPENDS ${CMAKE_BINARY_DIR}/boot/bootdata/system
                ${CMAKE_BINARY_DIR}/boot/bootdata/software
                ${CMAKE_BINARY_DIR}/boot/bootdata/default
                ${CMAKE_BINARY_DIR}/boot/bootdata/sam
                ${CMAKE_BINARY_DIR}/boot/bootdata/security)

    add_cd_file(
        FILE ${CMAKE_BINARY_DIR}/boot/bootdata/system
             ${CMAKE_BINARY_DIR}/boot/bootdata/software
             ${CMAKE_BINARY_DIR}/boot/bootdata/default
             ${CMAKE_BINARY_DIR}/boot/bootdata/sam
             ${CMAKE_BINARY_DIR}/boot/bootdata/security
        TARGET livecd_hives
        DESTINATION reactos/system32/config
        FOR livecd)

    # Preinstall hives (same as LiveCD but with preinstall.inf instead of livecd.inf)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall)

    list(APPEND _preinstall_inf_files
        ${_registry_inf}
        ${CMAKE_SOURCE_DIR}/boot/bootdata/preinstall.inf
        ${CMAKE_SOURCE_DIR}/boot/bootdata/caroots.inf)
    if(NOT "${_app_launcher_contents}" STREQUAL "")
        list(APPEND _preinstall_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/app_launcher.inf)
    endif()
    if(REACTOS_USE_XPDM AND (SARCH STREQUAL "xbox"))
        list(APPEND _preinstall_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst_xbox.inf)
    elseif(REACTOS_USE_XPDM AND (SARCH STREQUAL "pc98"))
        list(APPEND _preinstall_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst_pc98.inf)
    else()
        list(APPEND _preinstall_inf_files
            ${CMAKE_SOURCE_DIR}/boot/bootdata/hiveinst.inf)
    endif()

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/system
               ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/software
               ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/default
               ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/sam
               ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/security
        COMMAND native-mkhive -h:SYSTEM,SOFTWARE,DEFAULT,SAM,SECURITY -d:${CMAKE_BINARY_DIR}/boot/bootdata/preinstall ${_preinstall_inf_files}
        DEPENDS native-mkhive ${_preinstall_inf_files})

    add_custom_target(preinstall_hives
        DEPENDS ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/system
                ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/software
                ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/default
                ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/sam
                ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/security)

    add_cd_file(
        FILE ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/system
             ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/software
             ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/default
             ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/sam
             ${CMAKE_BINARY_DIR}/boot/bootdata/preinstall/security
        TARGET preinstall_hives
        DESTINATION reactos/system32/config
        FOR preinstall)

    # BCD hive (for EFI-compatible platforms)
    if(NOT ARCH STREQUAL "i386" OR NOT (SARCH STREQUAL "pc98" OR SARCH STREQUAL "xbox"))
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/boot/bootdata/BCD
            COMMAND native-mkhive -h:BCD -u -d:${CMAKE_BINARY_DIR}/boot/bootdata ${CMAKE_BINARY_DIR}/boot/bootdata/hivebcd_utf16.inf
            DEPENDS native-mkhive ${CMAKE_BINARY_DIR}/boot/bootdata/hivebcd_utf16.inf)

        add_custom_target(bcd_hive
            DEPENDS ${CMAKE_BINARY_DIR}/boot/bootdata/BCD)

        add_cd_file(
            FILE ${CMAKE_BINARY_DIR}/boot/bootdata/BCD
            TARGET bcd_hive
            DESTINATION efi/boot
            NO_CAB FOR bootcd livecd)
    endif()

endfunction()

function(add_driver_inf _module)
    # Add to the inf files list
    foreach(_file ${ARGN})
        if(IS_ABSOLUTE ${_file})
            set(_source_item ${_file})
            get_filename_component(_file ${_file} NAME)
        else()
            set(_source_item ${CMAKE_CURRENT_SOURCE_DIR}/${_file})
        endif()
        set(_converted_item ${CMAKE_CURRENT_BINARY_DIR}/${_file})
        utf16le_convert(${_source_item} ${_converted_item})
        list(APPEND _converted_inf_files ${_converted_item})
    endforeach()

    add_custom_target(${_module}_inf_files DEPENDS ${_converted_inf_files})
    add_cd_file(FILE ${_converted_inf_files} TARGET ${_module}_inf_files DESTINATION reactos/inf FOR all)
    set_property(GLOBAL APPEND PROPERTY DRIVER_INF_LIST ${_converted_inf_files})
    set_property(GLOBAL APPEND PROPERTY DRIVER_INF_TARGETS ${_module}_inf_files)
endfunction()


function(add_rc_deps _target_rc)
    set_source_files_properties(${_target_rc} PROPERTIES OBJECT_DEPENDS "${ARGN}")
endfunction()

add_custom_target(rostests_install COMMAND ${CMAKE_COMMAND} -DCOMPONENT=rostests -P ${CMAKE_BINARY_DIR}/cmake_install.cmake)
function(add_rostests_file)
    cmake_parse_arguments(_ROSTESTS "" "RENAME;SUBDIR;TARGET" "FILE" ${ARGN})
    if(NOT (_ROSTESTS_TARGET OR _ROSTESTS_FILE))
        message(FATAL_ERROR "You must provide a target or a file to install!")
    endif()

    set(_ROSTESTS_NAME_ON_CD "${_ROSTESTS_RENAME}")
    if(NOT _ROSTESTS_FILE)
        set(_ROSTESTS_FILE "$<TARGET_FILE:${_ROSTESTS_TARGET}>")
        if(NOT _ROSTESTS_RENAME)
            set(_ROSTESTS_NAME_ON_CD "$<TARGET_FILE_NAME:${_ROSTESTS_TARGET}>")
        endif()
    else()
        if(NOT _ROSTESTS_RENAME)
            get_filename_component(_ROSTESTS_NAME_ON_CD ${_ROSTESTS_FILE} NAME)
        endif()
    endif()

    if(_ROSTESTS_SUBDIR)
        set(_ROSTESTS_SUBDIR "/${_ROSTESTS_SUBDIR}")
    endif()

    if(_ROSTESTS_TARGET)
        add_cd_file(TARGET ${_ROSTESTS_TARGET} FILE ${_ROSTESTS_FILE} DESTINATION "reactos/bin${_ROSTESTS_SUBDIR}" NAME_ON_CD ${_ROSTESTS_NAME_ON_CD} FOR all)
    else()
        add_cd_file(FILE ${_ROSTESTS_FILE} DESTINATION "reactos/bin${_ROSTESTS_SUBDIR}" NAME_ON_CD ${_ROSTESTS_NAME_ON_CD} FOR all)
    endif()

    if(DEFINED ENV{ROSTESTS_INSTALL})
        if(_ROSTESTS_RENAME)
            install(FILES ${_ROSTESTS_FILE} DESTINATION "$ENV{ROSTESTS_INSTALL}${_ROSTESTS_SUBDIR}" COMPONENT rostests RENAME ${_ROSTESTS_RENAME})
        else()
            install(FILES ${_ROSTESTS_FILE} DESTINATION "$ENV{ROSTESTS_INSTALL}${_ROSTESTS_SUBDIR}" COMPONENT rostests)
        endif()
    endif()
endfunction()

if(PCH)
    macro(add_pch _target _pch _skip_list)
        target_precompile_headers(${_target} PRIVATE ${_pch})
        set_source_files_properties(${_skip_list} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endmacro()
else()
    macro(add_pch _target _pch _skip_list)
    endmacro()
endif()

# Some targets rely on declarations in their PCH source even when PCH is
# disabled. Include it for only the sources that normally receive the PCH;
# callers leave their PCH_SKIP_SOURCE files out of the list.
function(add_pch_fallback _header)
    if(PCH)
        return()
    endif()

    if(MSVC)
        set(_include_option "/FI${CMAKE_CURRENT_SOURCE_DIR}/${_header}")
    else()
        set(_include_option "-include;${CMAKE_CURRENT_SOURCE_DIR}/${_header}")
    endif()
    set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "${_include_option}")
endfunction()

function(set_target_cpp_properties _target)
    cmake_parse_arguments(_CPP "WITH_EXCEPTIONS;WITH_RTTI" "" "" ${ARGN})

    if (_CPP_WITH_EXCEPTIONS)
        set_target_properties(${_target} PROPERTIES WITH_CXX_EXCEPTIONS TRUE)
    endif()

    if (_CPP_WITH_RTTI)
        set_target_properties(${_target} PROPERTIES WITH_CXX_RTTI TRUE)
    endif()
endfunction()
