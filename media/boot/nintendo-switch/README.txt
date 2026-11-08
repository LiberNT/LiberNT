LiberNT on the Nintendo Switch
==============================

The console must already start hekate (through RCM on an unpatched launch
model, otherwise through a modchip). LiberNT starts from hekate's L4T loader,
the same way as Switchroot Linux, then through U-Boot's EFI loader:

  hekate -> TF-A (bl31.bin) -> U-Boot (bl33.bin) -> boot.scr
         -> EFI/BOOT/BOOTAA64.EFI (LiberNT boot manager) -> winload -> LiberNT

Files on the SD card (FAT32)
----------------------------

  bootloader/ini/libernt.ini        hekate menu entry "LiberNT"
  switchroot/libernt/bl33.bin       U-Boot with the EFI loader enabled
  switchroot/libernt/boot.scr       starts the LiberNT boot manager
  switchroot/libernt/boot.cmd       source of boot.scr
  EFI/BOOT/BOOTAA64.EFI             LiberNT boot manager
  EFI/LiberNT/Boot/winload.efi      LiberNT OS loader
  EFI/LiberNT/Boot/BCD              boot configuration
  boot.wim                          the system, loaded into RAM

Firmware to copy from a Switchroot L4T installation
---------------------------------------------------

LiberNT does not ship Switchroot's TF-A or NVIDIA's BPMP firmware. Copy these
files from the folder of an existing Switchroot Linux installation on the same
card (for example switchroot/ubuntu/) into switchroot/libernt/:

  bl31.bin  sc7entry.bin  sc7exit.bin               all models
  bpmpfw_b01.bin  mtc_tbl_b01.bin                   2019, Lite and OLED models
  bpmpfw.bin                                        launch models (2017)

Use a hekate release that supports the L4T loader of that Switchroot release.

Use
---

Copy the files above onto the card's FAT32 partition, or write
LiberNT-switch-sd.img to a card of its own and add hekate to it. Then choose
"More configs" > "LiberNT" in hekate.

U-Boot
------

bl33.bin is Switchroot's U-Boot, https://gitlab.com/switchroot/bootstack/switch-uboot
branch linux-hekatf at 722e2b8, with u-boot.patch applied (EFI loader enabled and
fixed). U-Boot is GPL-2.0-or-later, see LICENCE.u-boot. To rebuild it:

  git am u-boot.patch
  make CROSS_COMPILE=aarch64-linux-gnu- HOSTCC="gcc -fcommon" nintendo-switch_defconfig
  make CROSS_COMPILE=aarch64-linux-gnu- HOSTCC="gcc -fcommon" KCFLAGS=-std=gnu11 \
       CONFIG_PYLIBFDT= u-boot.bin
  cp u-boot.bin bl33.bin
