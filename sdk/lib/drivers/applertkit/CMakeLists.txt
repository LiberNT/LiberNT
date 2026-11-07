
add_library(applertkit rtkit.c pmgr.c dart.c spmi.c atcphy.c pcie.c gpio.c i2c.c nco.c admac.c mca.c)
add_dependencies(applertkit bugcodes xdk)
