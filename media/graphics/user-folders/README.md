# Shell icons

The folder, drive, desktop and shell icons below come from the Papirus icon theme by
the Papirus Development Team
(https://github.com/PapirusDevelopmentTeam/papirus-icon-theme), commit
`bf539287ef5dc18529424a02cccee76175920a6f`, released under the GNU General
Public License version 3 (`papirus/LICENSE`). The SVGs in `papirus/` are
copied unmodified from that commit with their theme paths; `papirus/sources.json`
records the icon name, design size, file and SHA-256 of each one.

| Icon | Papirus icon | Resource |
| --- | --- | --- |
| Generic folder | `places/folder-yellow` | shell32.dll, -4 |
| Open folder | `places/folder-yellow-open` | shell32.dll, -5 |
| Fixed drive | `devices/drive-harddisk` | shell32.dll, -9 |
| My Computer | `devices/computer` | shell32.dll, -16 |
| Network Places | `places/network-workgroup` | shell32.dll, -18 |
| Recycle Bin, empty and full | `places/user-trash`, `places/user-trash-full` | shell32.dll, -32 and -33 |
| Desktop | `places/user-teal-home` | shell32.dll, -35 |
| Control Panel | `places/folder-blue-development` | shell32.dll, -137 |
| Read Me | `mimetypes/text-x-readme` | shell32.dll, -152 |
| Documents | `places/folder-blue-documents` | shell32.dll, -235; mydocs.dll, 100 |
| Pictures | `places/folder-cyan-pictures` | shell32.dll, -236; mydocs.dll, 101 |
| Music | `places/folder-deeporange-music` | shell32.dll, -237 |
| Videos | `places/folder-violet-videos` (`folder-violet-video`) | shell32.dll, -238 |
| Internet browser | `apps/web-browser` (`internet-web-browser`) | shell32.dll, -512; iexplore.exe |
| Downloads | `places/folder-green-download` | shell32.dll, 16722 (`downloads.ico`) |
| NT Object Namespace | `places/folder-blue-code` | ntobjshex.dll, 1 |
| System Registry | `places/folder-blue-private` | ntobjshex.dll, -7 |
| Applications Manager, RosGet | `apps/system-software-install` | rapps.exe, rosget.exe |
| DWM Settings | `apps/preferences-system-windows` | dwmsettings.exe |
| Command Prompt | `apps/utilities-terminal` | cmd.exe |
| Task Manager | `devices/computer` with `apps/com.github.stsdc.monitor` centered on its screen | taskmgr.exe, taskmgr11.exe |
| Device Manager | `devices/computer` with `emblems/emblem-system` over its bottom-right quarter | devmgmt.exe |

Every ICO holds 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixel frames as
32-bit DIBs with alpha and legacy AND masks. Each frame is rasterized from the
smallest Papirus design size at least as large as the frame, or from the
largest design for bigger frames; where Papirus has no 16 pixel design of a
colored folder, those frames use the 22 pixel design.

To rebuild the ICOs, install Pillow, NumPy and resvg-py in a Python
environment and run:

```sh
python media/graphics/user-folders/render-icons.py
```

The Device Manager and Task Manager icons combine two unmodified Papirus
icons as the user asked: the Device Manager emblem is drawn at half the frame
size in the bottom-right corner, the Task Manager graph at the screen height
centered on the monitor. At the user's request the Task Manager graph keeps
only the stroked line and its shadow of `com.github.stsdc.monitor`; the
generator leaves out its filled background tile when rendering, and the
vendored SVG itself stays unmodified.

## Device Manager

The Device Manager device class and toolbar icons come from the scalable
icons of the Fluent icon theme by Vince Liuice
(https://github.com/vinceliuice/Fluent-icon-theme), commit
`c8a244f571ef8fb47812bdc9dbce76546cedce69`, released under the GNU General
Public License version 3 (`fluent/COPYING`). The SVGs in `fluent/` are copied
unmodified; `fluent/sources.json` records their SHA-256. Every frame is
rasterized from the scalable SVG.

| Icon | Fluent icon | Resource |
| --- | --- | --- |
| Root node | Oxygen `devices/computer` | devmgr.dll, computer.ico and root.bmp |
| Toolbar: Properties, Scan for hardware changes, Enable, Disable, Update driver, Uninstall | `mimetypes/application-info`, `apps/preferences-system-search`, `emblems/emblem-checked`, `emblems/emblem-remove`, `emblems/emblem-downloads`, `places/user-trash` | devmgr.dll, toolbar.bmp |
| Display adapters | MoreWaita `devices/device_pci` (`apps/jockey`) | setupapi.dll, -1 |
| Mice | `devices/input-mouse` | setupapi.dll, -2 |
| Keyboards | `devices/input-keyboard` | setupapi.dll, -3 |
| Printers | `devices/printer` | setupapi.dll, -4 |
| Network adapters | `devices/network-card` | setupapi.dll, -5 |
| IDE ATA/ATAPI controllers | `apps/disk-check` | setupapi.dll, -9 |
| SCSI and RAID controllers | `devices/network-server` | setupapi.dll, -12 |
| Computer | Adwaita `devices/computer` | setupapi.dll, -13 |
| Other devices | `mimetypes/unknown` | setupapi.dll, -18 |
| USB controllers | `devices/media-flash-memory-stick` | setupapi.dll, -20 |
| Removable drives | `devices/drive-removable-media` | setupapi.dll, -21 |
| Human Interface Devices | `devices/input-touchscreen` | setupapi.dll, -24 |
| System devices | `devices/computer` | setupapi.dll, -27 |
| Processors | `devices/cpu` | setupapi.dll, -28 |
| Floppy controllers and drives | `devices/media-floppy` | setupapi.dll, -34, -50 and -105 |
| Monitors | `apps/desktop` | setupapi.dll, -35 |
| SD host controllers | `devices/media-flash` | setupapi.dll, -39 |
| DVD/CD-ROM drives | `devices/drive-optical` | setupapi.dll, -51 |
| Disk drives | `devices/drive-harddisk` | setupapi.dll, -53 |

The Display adapters icon comes from MoreWaita by somepaulo
(https://github.com/somepaulo/MoreWaita), commit
`298968f878ce5f529321aecf586bf98061969e9b`, GNU General Public License
version 3 (`morewaita/LICENSE`), copied unmodified into `morewaita/`.

The Computer icon comes from the Adwaita icon theme of the GNOME Project
(https://gitlab.gnome.org/GNOME/adwaita-icon-theme), commit
`82d305723107d2ab619c0a4e4ec3136558bb4fe7`, GNU LGPL v3 or CC BY-SA 3.0 US
(`adwaita/COPYING`), copied unmodified into `adwaita/`.

The root node icon comes from the Oxygen icon theme by the Oxygen Team (KDE)
(https://invent.kde.org/frameworks/oxygen-icons), commit
`acf76c74a1ab863a6cdf31d36ec29bbc710eed58`, GNU LGPL version 3 or later
(`oxygen/COPYING`): like the Windows 11 root node it shows a tower computer.
The PNGs and the scalable SVG in `oxygen/` are copied unmodified;
`oxygen/sources.json` records their SHA-256. Frames of 16, 32, 48, 64, 128
and 256 pixels are the Oxygen PNGs, the other frames are rendered from the
scalable SVG.

The class to icon numbers follow Windows 11, measured with
SetupDiGetClassPropertyW(DEVPKEY_DeviceClass_IconPath): Computer -13, Monitor
-35, SCSIAdapter -12, FDC -34 and SDHost -39 have their own icons there.

Normal builds use the checked-in ICOs and do not run the script.
