
list(APPEND SOURCE
    source/plutovg-blend.c
    source/plutovg-canvas.c
    source/plutovg-font.c
    source/plutovg-matrix.c
    source/plutovg-paint.c
    source/plutovg-path.c
    source/plutovg-rasterize.c
    source/plutovg-surface.c
    source/plutovg-ft-math.c
    source/plutovg-ft-raster.c
    source/plutovg-ft-stroker.c)

add_library(plutovg STATIC ${SOURCE})
target_include_directories(plutovg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(plutovg PUBLIC PLUTOVG_BUILD_STATIC
                                   PRIVATE PLUTOVG_BUILD PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
                                           _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_compile_options(plutovg PRIVATE -w)
add_dependencies(plutovg psdk)
