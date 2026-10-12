# Control Panel icons

The PNGs in this directory are the 512-pixel transparent RGBA artwork from
`/Users/mac/Downloads/Control-Panel-Icons`. Their filenames identify the
classic Control Panel items whose existing icon resources they replace.

| Artwork | ReactOS resource |
| --- | --- |
| Add Hardware | `hdwwiz.cpl,-100` |
| Administrative Tools | `main.cpl,-500` |
| Console | `console.dll,-40` |
| Date and Time | `timedate.cpl,-1` |
| Device Manager | `devmgmt.exe,0` |
| Display | `desk.cpl,-40` |
| Ease of Access Center | `access.cpl,-110` |
| Folder Options | `shell32.dll,-210` |
| Fonts | `main.cpl,-400` |
| Game Controllers | `joy.cpl,-100` |
| Internet Options | `inetcpl.cpl,-100` |
| Keyboard | `main.cpl,-200` |
| Mouse | `main.cpl,-100` |
| Network Connections | `netshell.dll,-100` |
| OpenGL Configuration | `openglcfg.cpl,-1` |
| Personalization | `desk.cpl,-100` |
| Phone and Modem | `telephon.cpl,-100` |
| Power Options | `powercfg.cpl,-100` |
| Printers | `shell32.dll,-138` |
| Programs and Features | `appwiz.cpl,-1500` |
| Region and Language | `intl.cpl,-200` |
| Safely Remove Hardware | `hotplug.dll,-100` |
| Sound | `mmsys.cpl,-3004` |
| System | `sysdm.cpl,-50` |
| Taskbar and Start Menu | `shell32.dll,-40` |
| Text Services and Input | `input.dll,-1502` |
| User Accounts | `usrmgr.cpl,-40` |
| WineD3D Options | `wined3dcfg.cpl,-1` |

Display keeps the primary `desk.cpl` icon resource. Personalization uses the
existing secondary resource so both entries can show their matching artwork.
The obsolete Control11-only Network and Sharing Center entry is intentionally
not represented; Network Connections remains the working network item.

With Pillow and NumPy installed, regenerate the ICO resources using:

```sh
python media/graphics/control-panel-icons/render-icons.py
```

The shared ICO encoder writes 16, 20, 24, 32, 40, 48, 64, 96, 128, and 256
pixel 32-bit DIB frames with alpha and legacy AND masks. Ordinary ReactOS
builds consume the generated ICO files directly.
