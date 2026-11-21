
include(softgpu.cmake)

option(SOFTGPU_PHYSICAL_ADDRESSING "Run the BasicDisplay engine nodes in WDDM 2.0 physical mode" OFF)

if(SOFTGPU_PHYSICAL_ADDRESSING)
    add_softgpu_engine(softgpu_engine DEFINITIONS SOFTGPU_PLATFORM_PHYSICAL_ADDRESSING=1)
else()
    add_softgpu_engine(softgpu_engine)
endif()
