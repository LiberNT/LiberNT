1. What's LiberNT?
-------------------
LiberNT is a free NT operating system, built out of love for the NT architecture. The name says it: liber is Latin for free. LiberNT targets Windows® 11 compatibility, with room for freestyle and experimental features.

LiberNT is based on ReactOS, the Open Source effort to develop an operating system that is compatible with applications and drivers written for the Microsoft Windows NT family of operating systems. It is a fork with extensive changes to kernel-space components and drivers, aimed at stability and modern hardware, and it focuses on 64-bit platforms: amd64, arm64 and riscv64.

Our goal is a system that stays maintainable in the long run: stable, reliable and efficient, running on embedded devices too, and able to support every feature a modern operating system should have.

2. What LiberNT Enables
-----------------------

LiberNT brings modern hardware and platforms to the NT architecture. The current focus is:

    - 64-bit platform support on amd64, arm64 and riscv64: kernel, HAL and FreeLoader
    - UEFI boot, GOP framebuffer, and early display support
    - PCIe, PCI, PnP, USB/xHCI, SD/eMMC, and storage/bus drivers
    - Board support, including the Raspberry Pi and its RP1 I/O controller
    - Windows 11 (NT10) compatibility and driver-model work

This means it can potentially work on modern hardware, but it's not recommended to use it as your main operating system, because it's still an experiment at this stage.


3. Differences Between LiberNT and Upstream ReactOS
---------------------------------------------------

Most of the source tree is shared with ReactOS, because it's a direct fork of ReactOS.

However, there are some differences between the two:

    - Memory Management: NVS (NT Virtual Memory Subsystem) is LiberNT's clean-room virtual memory manager and kernel pool implementation, accompanied by a new cache manager that replaces the corresponding components inherited from ReactOS.
    - ARM64 Support: The kernel, hardware abstraction layer (HAL), and the bootloader can run on ARM64-powered devices.
    - App Compatibility: LiberNT uses WoW64 to allow running 32-bit apps on 64-bit processors. On ARM64-powered devices, x86 apps run through the FEX emulator with ARM64EC support.
    - Graphics: The more modern WDDM display driver model and DWM desktop composition is used, like in Windows 11.
    - USB: Generic USB RNDIS networking is supported. LiberNT also supports controllers like the PlayStation 5 DualSense, PlayStation 5 DualSense Edge, and the XBOX Series X|S controller.
    - Single-Board Computers: Raspberry Pi display, network and audio drivers, with much more coming.


4. AI Usage in LiberNT
----------------------

A lot of LiberNT's code is reviewed and generated with the assistance of AI. However, the code that the AI writes is reviewed, run, and validated with end-to-end tests by a real person before it reaches the main branch. We care about this project too much to let a regression slip through.
It also helps us read documentation, extract definitions and fix bugs. For any given bug, it compares what the official Microsoft Learn documentation says with what our function actually does, so we can see exactly where our behavior differs.



