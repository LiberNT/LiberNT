> [English](README.md) | **中文**
>
> 翻译说明：本译文由 AI 初译并排版；术语、代码块、链接与锚点已与英文原版核对，中文术语已经人工核对。如有纰漏，请提交 issue 指出；表述以英文版为准。

<p align="center">
  <img src="media/graphics/branding/libernt-banner.png" width="480" alt="LiberNT">
</p>

> [!IMPORTANT]
> LiberNT 正在积极开发中。[Releases](https://github.com/LiberNT/LiberNT/releases) 页面上的构建均为用于测试的 Debug 预发布版：请放在虚拟机或备用机上运行，别装到日常使用的电脑上。

<div align="center">

# [LiberNT](https://libernt.com)
### 自由的 NT 操作系统

**源于对 NT 架构的热爱，兼容 Windows® 11，支持 amd64、arm64 与 riscv64。**

[![Build](https://img.shields.io/github/actions/workflow/status/LiberNT/LiberNT/build.yml?branch=main-nt10&label=Build&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/LiberNT/LiberNT/actions/workflows/build.yml)
[![License: GPL 3.0](https://img.shields.io/badge/License-GPL_3.0-0161B6?style=for-the-badge&logo=gnu&logoColor=white)](#许可证)
[![Discord](https://img.shields.io/badge/Discord-Join-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/BpcKg7FZ2)
[![Matrix](https://img.shields.io/badge/Matrix-%23libernt-000000?style=for-the-badge&logo=matrix&logoColor=white)](https://matrix.to/#/#libernt:matrix.org)

<img src="https://img.shields.io/github/repo-size/LiberNT/LiberNT?style=flat-square&color=blue" alt="Repo Size">
<img src="https://img.shields.io/github/stars/LiberNT/LiberNT?style=flat-square&color=yellow" alt="Stars">
<img src="https://img.shields.io/github/forks/LiberNT/LiberNT?style=flat-square&color=lightgrey" alt="Forks">
<img src="https://img.shields.io/github/last-commit/LiberNT/LiberNT/main-nt10?style=flat-square&color=green" alt="Last Commit">
<img src="https://img.shields.io/github/issues/LiberNT/LiberNT?style=flat-square&color=orange" alt="Issues">

<br>

[![Download the latest build](https://img.shields.io/badge/Download-Latest_build-0161B6?style=for-the-badge&logo=github&logoColor=white)](https://github.com/LiberNT/LiberNT/releases)

</div>

## 赞助

<p align="center">
  <a href="https://github.com/sponsors/ahmedarif193">
    <img src="https://img.shields.io/badge/Sponsor-LiberNT-ea4aaa?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="Sponsor LiberNT">
  </a>
</p>

---

<p align="center">
  <img width="1000" alt="arm64 上的 LiberNT 桌面" src="media/graphics/screenshots/libernt-desktop-arm64.png">
</p>

<p align="center"><sub>arm64 上的 LiberNT 桌面，运行于 QEMU，构建版本 nt10.0-043-2555</sub></p>

---

LiberNT 是一个自由的 NT 操作系统。名字本身已经说明：*liber* 在拉丁语里意为**自由**。它以**兼容 Windows® 11** 为目标，也为自由发挥和实验性功能留出余地。

LiberNT 在内核态组件与驱动上投入了大量工作，着眼稳定性与现代硬件，主攻 64 位平台：**amd64、arm64 与 riscv64**。

我们希望这套系统长期可维护：**稳定、可靠、高效**，既能跑在嵌入式设备上，也具备现代操作系统应有的完整能力。

> [!NOTE]
> 初次接触 NT？**NT** 是支撑所有现代 Windows 版本的架构，涵盖内核、驱动模型和系统服务。LiberNT 正是构建在这套架构之上，以 Windows 11 作为对标的参照。

---

## 目录

- [LiberNT 带来了什么](#libernt-带来了什么)
- [支持的平台](#支持的平台)
- [快速上手](#快速上手)
  - [第 1 步：下载构建](#第-1-步下载构建)
  - [第 2 步：启动](#第-2-步启动)
  - [第 3 步：安装 LiberNT](#第-3-步安装-libernt)
- [自行构建 LiberNT](#自行构建-libernt)
- [构建主机](#构建主机)
- [项目结构](#项目结构)
- [参与贡献](#参与贡献)
- [关于 AI 的使用](#关于-ai-的使用)
- [许可证](#许可证)
- [关于](#关于)

---

## LiberNT 带来了什么

LiberNT 将现代硬件与平台引入 NT 架构。

| 特性 | 实际作用 |
| --- | --- |
| **64 位优先** | 内核、HAL 和 FreeLoader 均运行于 amd64、arm64 与 riscv64。 |
| **UEFI 启动** | 支持 UEFI 固件启动，提供 GOP 帧缓冲和早期显示支持。预装盘同时支持 UEFI 与 BIOS 两种启动方式。 |
| **现代总线与存储** | 覆盖 PCIe、PCI、即插即用、xHCI USB、SD/eMMC，以及各类存储与总线驱动。 |
| **开发板支持** | 面向真实开发板，包括树莓派及其 RP1 I/O 控制器。 |
| **Windows 11 驱动模型** | 围绕 Windows 11(NT10)的兼容性与驱动模型展开，并与 Windows 11 本身逐项核对。 |
| **Arm 上的图形与 x86 应用** | 图形驱动模型可搭配 Mesa，WoW64 可搭配 FEX，两者都是可按需开启的构建选项。 |
| **LiberNT 安装程序** | 磁盘列表的呈现方式与 Windows 安装程序一致，可新建、删除、格式化、压缩和扩展分区，并把 LiberNT 安装到 NTFS 分区，同时创建所需的 EFI 系统分区。 |
| **定制构建** | 通过 `menuconfig` 选择目标、编译器和构建类型，逐项开关功能。 |

> [!TIP]
> [Releases](https://github.com/LiberNT/LiberNT/releases) 页面的每个构建都附有相对上一版的改动说明。

---

## 支持的平台

| 架构 | 预编译镜像 | 源码构建 | 备注 |
| --- | :---: | :---: | --- |
| **amd64** | ✓ | ✓ | Clang(默认)或 GCC |
| **arm64** | ✓ | ✓ | 支持树莓派 |
| **riscv64** | ✓ | ✓ | 为 JH7110 开发板单独提供镜像 |
| **i386** | | ✓ | 32 位 x86 |

发布构建中另有 PowerPC 镜像。

---

## 快速上手

### 第 1 步：下载构建

[Releases](https://github.com/LiberNT/LiberNT/releases) 页面上，每种镜像、每个架构各自打包成一个 ZIP 文件：

| 文件 | 内容 |
| --- | --- |
| `bootcd-iso-…-<arch>-…zip` | `bootcd.iso`，可运行 LiberNT 或启动 LiberNT 安装程序的 CD 镜像 |
| `LiberNT-img-…-<arch>-…zip` | `LiberNT.img`，已装好 LiberNT 的磁盘 |
| `LiberNT-vhd-…-<arch>-…zip` | `LiberNT.vhd`，同一份预装盘，供基于 VHD 的虚拟机使用 |
| `LiberNT-vhdx-…-<arch>-…zip` | `LiberNT.vhdx`，同一份预装盘，供 Hyper-V 第二代使用 |

> [!NOTE]
> 这些构建由 GitHub Actions 从 `main-nt10` 分支生成，均为 Clang Debug 版本。

### 第 2 步：启动

想尽快体验 LiberNT，直接用预装盘：

1. 解压 `LiberNT.img`(或 `LiberNT.vhd`)
2. 在 **QEMU** 等虚拟机中启动；VHD 也可用于 **VirtualBox** 或 **Hyper-V** 第一代虚拟机。Hyper-V 第二代只接受 `LiberNT.vhdx`，并需关闭安全启动
3. 也可以把 `LiberNT.img` 写入 U 盘或 SD 卡，再从该设备启动，UEFI 与 BIOS 均可

> [!CAUTION]
> 把 `LiberNT.img` 写入 U 盘或 SD 卡会清空盘上的全部数据。操作前请仔细确认目标设备。

### 第 3 步：安装 LiberNT

在虚拟机或 PC 上启动 `bootcd.iso`，按 **LiberNT 安装程序**的提示操作。

预装盘里同样带有 LiberNT 安装程序。在运行中的系统里执行 `C:\<architecture>\libernt.exe`，例如 `C:\arm64\libernt.exe`，就能把 LiberNT 装到另一块磁盘。

> [!CAUTION]
> 安装程序会删除并格式化分区。安装前请备份目标磁盘上的重要数据。

完整安装步骤见 [INSTALL.zh-CN.md](INSTALL.zh-CN.md)。

---

## 自行构建 LiberNT

LiberNT 使用 RosBE 交叉编译工具链构建。下面的步骤适用于 Linux 和 macOS，Windows 上可通过 WSL2 完成。

### 1. 安装工具链

本机需要 Git、CMake、Ninja 和一套 C/C++ 编译器；构建 LiberNT 所需的交叉编译器由 RosBE 提供。

```sh
curl -fsSL https://raw.githubusercontent.com/LiberNT/libernt-build-toolchain/main/rosbe-unix-bootstrap.sh | sh
```

> [!TIP]
> Windows 上推荐用 WSL2：执行 `wsl --install`，打开它新增的 Linux 终端，再按 Linux 的步骤操作。源码放在 Linux 文件系统里，`/mnt/c` 下构建会慢不少。

### 2. 获取源码

```sh
git clone https://github.com/LiberNT/LiberNT.git
cd LiberNT
```

> [!NOTE]
> 首次运行配置脚本时，会一并拉取构建所需的子模块和外部源码。

### 3. 配置

```sh
./configure.sh
```

不带参数时，会在 `output-Clang-amd64-debug` 目录中建立 AMD64 Debug(Clang)构建。目录名始终遵循 `output-<compiler>-<architecture>-<debug|release>` 的格式。

| 选项 | 作用 |
| --- | --- |
| `-a`、`--arch <arch>` | 选择目标架构：`amd64`(默认)、`i386`、`arm64` 或 `riscv64`。 |
| `--clang` | 使用 RosBE 中的 Clang/LLVM 构建，默认选项。 |
| `--gcc` | 改用 RosBE 中的 GCC 构建。macOS 版 RosBE 不含 GCC。 |
| `-r`、`--release` | 生成优化后的 Release 构建。不加此选项则为 Debug 构建，便于调试。 |
| `menuconfig` | 打开菜单，先挑选可选组件。选择结果保存在输出目录中。 |
| `makefiles` | 用 `make` 代替 Ninja。 |
| `--no-feeds-update` | 跳过拉取 `feeds.conf` 中列出的外部源码，离线时可用。 |
| `-D<name>=<value>` | 直接将设置传给 CMake。 |

示例：

```sh
./configure.sh -a arm64              # ARM64 Debug 构建(Clang)
./configure.sh -a i386 --gcc         # 32 位 x86 构建(GCC)
./configure.sh -a amd64 --release    # 优化后的 AMD64 构建
```

想要更贴合自己需求的构建，可以加 `menuconfig`：

```sh
./configure.sh menuconfig
```

它会打开一个类似 Linux 内核配置的菜单，在这里选择目标、编译器和构建类型，并开关各项功能：代码生成与调优、图形驱动模型与 Mesa、WoW64 与 FEX、内核调试、测试运行等等。在选项上按 `?` 查看帮助，`S` 保存，`Q` 退出。选择结果保存在输出目录中，之后每次配置都会沿用。

### 4. 构建

进入输出目录，用 Ninja 执行需要的目标：

```sh
cd output-Clang-amd64-debug
ninja liberntimg
```

| 命令 | 生成 | 用途 |
| --- | --- | --- |
| `ninja bootcd` | `bootcd.iso` | 在虚拟机或 PC 上从 CD 镜像安装 LiberNT。 |
| `ninja liberntimg` | `LiberNT.img` | 启动一块已装好 LiberNT 的磁盘。可在 QEMU 等虚拟机中运行，也可写入 U 盘或 SD 卡；UEFI 与 BIOS 均可启动。 |
| `ninja liberntvhd` | `LiberNT.vhd`、`LiberNT.vhdx` | 在虚拟机里复用同一份预装盘：VHD 用于 VirtualBox 和 Hyper-V 第一代，VHDX 用于 Hyper-V 第二代(该代不支持 VHD)。 |

每个目标都会先把它依赖的内容构建好，产物统一写入输出目录。

---

## 构建主机

| 主机 | 编译器 | 备注 |
| --- | --- | --- |
| **Linux** x86_64 / aarch64 | Clang 与 GCC | 完整的 RosBE 工具链 |
| **macOS** Intel / Apple Silicon | Clang | macOS 版 RosBE 不含 GCC |
| **Windows** | Clang 与 GCC | 通过 WSL2，按 Linux 步骤操作 |

无论在哪种主机上，都先用系统包管理器安装 Git、CMake、Ninja、Flex 和 Bison。RosBE 只提供交叉编译器。预编译好的工具链位于 [LiberNT/libernt-build-toolchain](https://github.com/LiberNT/libernt-build-toolchain)。

---

## 项目结构

```text
LiberNT/
├── base/            # 应用、服务、安装程序与 shell
├── boot/            # FreeLoader、启动数据与启动镜像
├── bootloader/      # 启动管理器、Windows 加载器、UEFI 与 Open Firmware 支持
├── dll/             # 用户态 DLL,含 ntdll 与 Win32 库
├── drivers/         # 内核态驱动:总线、存储、文件系统、网络、输入等
├── hal/             # x86、Arm、Arm64、PowerPC 与 RISC-V 的硬件抽象层
├── media/           # 字体、主题、INF 文件、声音与品牌资源
├── modules/         # 可选应用、测试与壁纸
├── ntoskrnl/        # NT 内核与执行体
├── sdk/             # 头文件、库与构建工具
├── submodules/      # 外部组件的配方与补丁
├── subsystems/      # CSRSS 与 Windows 子系统
├── win32ss/         # win32k、USER、GDI 与显示驱动
├── configure.sh     # 在 Linux、macOS 与 WSL2 上配置构建
└── configure.cmd    # 在 Windows 上配置构建
```

---

## 参与贡献

问题反馈和功能建议请提交到 [Issues](https://github.com/LiberNT/LiberNT/issues)，指南与笔记见 [Wiki](https://github.com/LiberNT/LiberNT/wiki)。

动手前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [PULL_REQUEST_MANAGEMENT.md](PULL_REQUEST_MANAGEMENT.md)。如果打算做较大的改动，建议先在 [Issues](https://github.com/LiberNT/LiberNT/issues) 或 [Discord](https://discord.gg/BpcKg7FZ2) 上讨论。

> [!WARNING]
> **法律声明：** 如果你接触过微软 Windows 的专有源码(包括但不限于泄露的 Windows NT 3.5、NT 4、2000 源码以及 Windows Research Kernel)，由于存在版权风险，你的贡献将无法被接受。

---

## 关于 AI 的使用

先说清楚：AI 是 LiberNT 开发方式的一部分。它协助我们审查和编写代码，写出的内容随后由人工复核。任何改动进入主分支之前，都会有人运行并验证端到端测试。我们对这个项目足够上心，回归问题必须挡在合并之前。

AI 还帮我们读文档、提取定义、修复缺陷。遇到具体缺陷时，它会拿 Microsoft Learn 官方文档与函数的实际行为对照，把差异定位到具体位置。

和别的工具一样，AI 用来帮我们达成目标，并遵守 [AGENTS.md](AGENTS.md) 里写明的同一套规则：

- 我们基于 Windows SDK 和 WDK 头文件、Windows 11 公开符号、微软文档，以及在 Windows 11 上的黑盒运行来开发。
- 坚持净室方式：反汇编、反编译得到的 Windows 代码一律不照抄，Linux 的 GPL 代码也不复制。
- 修复要经过真实启动或测试运行的验证才算数。测试通过之后，后续 CI 会保证它们一直通过，守住整体进度。
- 测试结果与 Windows 11 上跑同一套测试的结果逐项比对。

> [!TIP]
> 你在自己的贡献里用了 AI，同样欢迎。只要在 PR 里说明 AI 帮了哪些部分、你又是怎么测试的。这份坦诚就足够了。

---

## 许可证

LiberNT 以 [GNU GPL 3.0](COPYING) 授权。沿用自其他项目的代码保留其文件头声明的许可证，大多为 GPL 2.0 或更高版本。这些文件引用的 GPL 2.0、LGPL 2.1 和 BSD 文本见 [COPYING2](COPYING2)、[COPYING.LIB](COPYING.LIB) 和 [COPYING.ARM](COPYING.ARM)。

LiberNT 中的改动版权归 LiberNT 贡献者所有，© 2026。沿用代码与第三方代码保留原作者的版权声明。详见 [CREDITS.md](CREDITS.md)。

LiberNT 是独立项目，与 Microsoft 没有隶属、认可或赞助关系。Windows® 是 Microsoft Corporation 的注册商标。

---

## 关于

LiberNT 由 **LiberNT 贡献者**共同构建，由 [Ahmed Arif](https://github.com/ahmedarif193) 发起并维护。

- [访问 LiberNT 组织](https://github.com/LiberNT)
- [官网](https://libernt.com)
- [下载最新构建](https://github.com/LiberNT/LiberNT/releases)
- [Wiki](https://github.com/LiberNT/LiberNT/wiki)
- 在 [Discord](https://discord.gg/BpcKg7FZ2) 或 [Matrix](https://matrix.to/#/#libernt:matrix.org) 上交流
