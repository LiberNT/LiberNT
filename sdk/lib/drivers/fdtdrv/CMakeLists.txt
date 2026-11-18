
add_library(fdtnode fdtnode.c)
add_dependencies(fdtnode bugcodes xdk)

add_library(fdtdrv fdtdrv.c)
add_dependencies(fdtdrv bugcodes xdk)
target_link_libraries(fdtdrv fdtnode)
