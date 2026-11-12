
add_library(applertkit rtkit.c rtkitirq.c bootargs.c pmpptd.c pmgr.c dart.c uat.c adt.c agxinit.c agxobject.c spmi.c atcphy.c pcie.c pcielink.c gpio.c i2c.c nco.c admac.c mca.c acepd.c)
add_dependencies(applertkit bugcodes xdk)
