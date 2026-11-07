# Instalar LiberNT

> [English](../../INSTALL.md) | [中文](../zh-CN/INSTALL.md) | [Français](../fr-FR/INSTALL.md) | **Español** | [日本語](../ja-JP/INSTALL.md) | [العربية](../ar-MA/INSTALL.md) | [Українська](../uk-UA/INSTALL.md)

## Compilar las imágenes

Prepara el entorno de compilación y configura una compilación como se describe en [Compilar LiberNT por tu cuenta](README.md#compilar-libernt-por-tu-cuenta). Después compila la imagen que necesites desde la carpeta de salida, por ejemplo `output-Clang-amd64-debug`:

| Comando | Crea | Sirve para |
|---|---|---|
| `ninja bootcd` | `bootcd.iso` | Ejecutar LiberNT desde una imagen de CD, o iniciar desde ella la instalación de LiberNT e instalar LiberNT en un disco. |
| `ninja liberntimg` | `LiberNT.img` | Arrancar un disco con LiberNT ya instalado. Arranca con UEFI o BIOS. |
| `ninja liberntvhd` | `LiberNT.vhd`, `LiberNT.vhdx` | Usar el mismo disco preinstalado en máquinas virtuales: el VHD en VirtualBox y en Hyper-V de generación 1, el VHDX en Hyper-V de generación 2, que no acepta discos VHD. |

Cada destino compila primero todo lo que necesita, y los archivos se escriben en la carpeta de salida.

## Instalar desde el CD de instalación

Arranca `bootcd.iso` en una máquina virtual o en un PC y sigue las instrucciones de la instalación de LiberNT. La instalación muestra las unidades igual que el instalador de Windows, y puede crear, eliminar, formatear, reducir y ampliar particiones. Instala LiberNT en NTFS y crea la partición del sistema EFI que necesita.

## Usar el disco preinstalado

`LiberNT.img` no necesita instalación. Inícialo en una máquina virtual como QEMU, o escríbelo en una unidad USB o una tarjeta SD y arranca el equipo desde ella. Usa `LiberNT.vhd` en las máquinas virtuales que aceptan discos VHD, y `LiberNT.vhdx` en las máquinas virtuales Hyper-V de generación 2, con el arranque seguro desactivado.

El disco preinstalado también incluye la instalación de LiberNT. Ejecuta `C:\<arquitectura>\libernt.exe`, por ejemplo `C:\arm64\libernt.exe`, para instalar LiberNT en otro disco desde el sistema en ejecución.

## Ayuda

Si algo sale mal, infórmalo en [Issues](https://github.com/LiberNT/LiberNT/issues) con la arquitectura, el tipo de compilación, el commit que compilaste y los pasos que llevan al problema. Las guías y notas están en la [Wiki](https://github.com/LiberNT/LiberNT/wiki), y puedes preguntar al equipo en [Discord](https://discord.gg/BpcKg7FZ2) o en [Matrix](https://matrix.to/#/#libernt:matrix.org).
