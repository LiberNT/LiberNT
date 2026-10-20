# Installing LiberNT

## Building the images

Set up the build environment and configure a build as described in [Building](README.md#building). Then build the image you need from the output folder, for example `output-Clang-amd64-debug`:

| Command | Creates | Use it to |
|---|---|---|
| `ninja bootcd` | `bootcd.iso` | Run LiberNT from a CD image, or start LiberNT Setup from it and install LiberNT on a disk. |
| `ninja liberntimg` | `LiberNT.img` | Boot a disk with LiberNT already installed. It boots with UEFI or BIOS. |
| `ninja liberntvhd` | `LiberNT.vhd` | Use the same preinstalled disk in virtual machines that take VHD disks, such as Hyper-V and VirtualBox. |

Each target builds everything it needs first, and the files are written to the output folder.

## Installing from the setup CD

Boot `bootcd.iso` in a virtual machine or on a PC, and follow the instructions of LiberNT Setup. Setup lists the drives the way Windows Setup does, and can create, delete, format, shrink and extend partitions. It installs LiberNT on NTFS and creates the EFI system partition it needs.

## Using the preinstalled disk

`LiberNT.img` needs no setup. Start it in a virtual machine such as QEMU, or write it to a USB drive or an SD card and boot the machine from it. Use `LiberNT.vhd` for virtual machines that take VHD disks.

The preinstalled disk also carries LiberNT Setup. Run `C:\<architecture>\libernt.exe`, for example `C:\arm64\libernt.exe`, to install LiberNT on another disk from the running system.

## Help

If something goes wrong, report it in [Issues](https://github.com/LiberNT/LiberNT/issues) with the architecture, the build type, the commit you built and the steps that lead to the problem. Guides and notes are in the [Wiki](https://github.com/LiberNT/LiberNT/wiki), and you can ask the team on [Discord](https://discord.gg/BpcKg7FZ2) or [Matrix](https://matrix.to/#/#libernt:matrix.org).
