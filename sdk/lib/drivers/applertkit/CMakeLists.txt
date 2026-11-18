
add_library(applertkitcore pmgrpower.c dartstream.c atcphy.c dwc3.c)
add_dependencies(applertkitcore bugcodes xdk)
target_link_libraries(applertkitcore fdtnode)

add_library(applertkit rtkit.c rtkitirq.c bootargs.c pmpptd.c pmgr.c dart.c uat.c adt.c agxinit.c agxobject.c spmi.c pcie.c pcielink.c gpio.c i2c.c nco.c admac.c mca.c acepd.c)
add_dependencies(applertkit bugcodes xdk)
target_link_libraries(applertkit applertkitcore)
