#
# PROJECT:     LiberNT Apple PWM Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the driver of the Apple silicon PWM controllers and their LEDs
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

add_library(applefpwm MODULE applefpwm.c fpwm.c pwmled.c applefpwm.rc)
set_module_type(applefpwm kernelmodedriver)
target_link_libraries(applefpwm applertkit fdtdrv)
add_importlibs(applefpwm ntoskrnl hal)
add_cd_file(TARGET applefpwm DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(applefpwm applefpwm.inf)
