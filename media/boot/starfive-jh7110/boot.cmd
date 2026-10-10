# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
echo "LiberNT: looking for FreeLoader on USB storage"
pci enum
usb start
for libernt_dev in 0 1 2 3 4 5 6 7; do
	if test -e usb ${libernt_dev}:1 efi/boot/bootriscv64.efi; then
		echo "LiberNT: starting FreeLoader from usb ${libernt_dev}:1"
		setenv libernt_fdt ${fdtcontroladdr}
		if load usb ${libernt_dev}:1 ${fdt_addr_r} dtb/starfive/jh7110-orangepi-rv.dtb; then
			setenv libernt_fdt ${fdt_addr_r}
		fi
		load usb ${libernt_dev}:1 ${kernel_addr_r} efi/boot/bootriscv64.efi
		bootefi ${kernel_addr_r} ${libernt_fdt}
	fi
done
echo "LiberNT: no USB disk holds efi/boot/bootriscv64.efi"
