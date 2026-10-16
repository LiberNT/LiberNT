# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
setenv bootfs_devname ${devtype}
setenv boot_devnum ${devnum}
setenv bootfs_part ${distro_bootpart}
load ${devtype} ${devnum}:${distro_bootpart} 0x10000000 env_k1-x.txt
env import -t 0x10000000 ${filesize}
setexpr k1_board sub "^k1-?x?_" "k1-x_" "${product_name}"
run k1_stop_watchdogs
load ${devtype} ${devnum}:${distro_bootpart} 0x10000000 EFI/BOOT/BOOTRISCV64.EFI
if load ${devtype} ${devnum}:${distro_bootpart} ${k1_fdt} dtb/${k1_board}.dtb && fdt addr ${k1_fdt} && fdt resize 0x10000 && run k1_fixup_fdt; then bootefi 0x10000000 ${k1_fdt}; else bootefi 0x10000000 ${fdtcontroladdr}; fi
