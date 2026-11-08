# LiberNT for the Nintendo Switch.
# hekate's L4T loader starts U-Boot, whose distro boot runs this script from ${boot_prefixes}.
# Start the LiberNT boot manager through the U-Boot EFI loader with U-Boot's device tree,
# adding the PSCI node of the TF-A firmware that hekate started when the tree lacks it.

fdt addr ${fdtcontroladdr}
if fdt move ${fdtcontroladdr} ${fdt_addr_r} 0x200000; then
    fdt addr ${fdt_addr_r}
    if fdt get value libernt_psci /psci method; then
        echo "LiberNT: the device tree describes PSCI (${libernt_psci})"
    else
        fdt mknode / psci
        fdt set /psci compatible "arm,psci-1.0"
        fdt set /psci method "smc"
    fi

    if load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} EFI/BOOT/BOOTAA64.EFI; then
        bootefi ${kernel_addr_r} ${fdt_addr_r}
    fi
    echo "LiberNT: EFI/BOOT/BOOTAA64.EFI did not start from ${devtype} ${devnum}:${distro_bootpart}"
else
    echo "LiberNT: the device tree could not be copied to ${fdt_addr_r}"
fi
