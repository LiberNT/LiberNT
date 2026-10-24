
foreach(_file lxwayland.py lxapp gui-setup lxwayland.service lx-gui-setup.service)
    add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/${_file} DESTINATION reactos/system32/lxwayland NO_CAB FOR all)
endforeach()

foreach(_file wayland.xml xdg-shell.xml xdg-decoration-unstable-v1.xml cursor-shape-v1.xml)
    add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${_file} DESTINATION reactos/system32/lxwayland/protocols NO_CAB FOR all)
endforeach()
