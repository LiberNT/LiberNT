# 如何贡献

> [English](../../CONTRIBUTING.md) | **中文**
>
> 翻译说明：本译文由 AI 初译并排版；术语、代码块、链接与锚点已与英文原版核对，中文术语已经人工核对。如有纰漏，请提交 issue 指出；表述以英文版为准。

LiberNT 欢迎缺陷报告、测试、修复、新功能和文档。本文说明如何提交你的工作。所有改动都要遵守 [AGENTS.md](../../AGENTS.md) 里的工程规则，人类贡献者和 AI 工具都一样。

- [可以做什么？](#可以做什么)
- [贡献方式](#贡献方式)
- [版权与许可](#版权与许可)
- [从哪里开始？](#从哪里开始)

**法律声明：** 如果你接触过微软 Windows 的专有源码，由于存在版权风险，你的贡献将无法被接受。贡献之前，你必须确认以下陈述属实：
>I hereby swear that I have not used nor seen the source code to any version of the Windows operating system
>nor any Microsoft product that may be related to the proposed project that is under a license incompatible
>with contribution to LiberNT, including but not limited to the leaked Windows NT 3.5, NT 4 and 2000 source code
>and the Windows Research Kernel.

中文对照(如与英文原文不一致，以英文原文为准)：本人郑重声明：本人未曾使用或查看任何版本的 Windows 操作系统源代码，也未曾使用或查看可能与拟议项目相关、且许可证与向 LiberNT 贡献不兼容的任何微软产品源代码，包括但不限于泄露的 Windows NT 3.5、NT 4 和 2000 源代码，以及 Windows Research Kernel。

LiberNT 以净室方式开发：依据 Windows SDK 和 WDK 头文件、Windows 11 公开符号、微软文档，以及在 Windows 11 上的黑盒运行。哪些内容可以用、哪些不能用，见[可信来源](../../AGENTS.md#sources-of-truth)。

## 可以做什么？

### 报告缺陷

请在 [Issues](https://github.com/LiberNT/LiberNT/issues) 中报告缺陷，注明架构(amd64、i386、arm64 或 riscv64)、构建类型、所用提交，以及复现步骤。串口调试日志会很有帮助。

### 修复缺陷

修底层行为，不修表面症状；改动尽量小，只做正确的最小修改。修复要经过真实启动或测试运行验证，才算完成。

### 修复测试

测试以 Windows 的行为为基准检查 LiberNT。不少测试在 Windows 11 上同样无法通过，所以在为了让 LiberNT 通过某个测试而动手之前，先在 Windows 11 上跑同一个测试，逐项比对结果。Windows 11 也没通过的检查项，问题出在测试的预期。参见[对照 Windows 11 测试](../../AGENTS.md#testing-against-windows-11)。

### 测试 LiberNT

在虚拟机或真实硬件上运行 LiberNT，把出问题的地方报上来。驱动方面，真实硬件最有说服力：虚拟机只能证明驱动装得上、加载得起来。

### 实现新功能

Windows 11 是兼容性参照。动手做较大的功能之前，先在 [Issues](https://github.com/LiberNT/LiberNT/issues) 或 [Discord](https://discord.gg/BpcKg7FZ2) 问一声，看有没有人在做。

### 编写文档

指南和设计笔记放在 [Wiki](https://github.com/LiberNT/LiberNT/wiki)。遵循[净室逆向工程]的做法，只写源码、文档或测试运行能够证明的内容。

### 第三方代码

与 Wine 同步的组件列在 [media/doc/WINESYNC.txt](../../media/doc/WINESYNC.txt)，其他第三方代码列在 [media/doc/3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt)。对这类代码的改动请放进 `#ifdef __REACTOS__`，原代码留在 `#else` 分支，这样下次同步时改动还在。

## 贡献方式

建议在 Issue 报告和社区讨论中使用英语，以提高沟通效率。We recommend using English in issue reports and community discussions, to keep communication efficient.

### Pull Request

把工作以 [PR] 的形式提交到 `main-nt10` 分支。PR 让其他人看到你推到 GitHub 仓库的改动；打开之后，可以和团队讨论、审查，并在合并前继续追加提交。

打开之前：

- amd64、i386 和 arm64 都要构建，不能只构建你改动的架构。
- 启动或运行覆盖到你改动的测试，并在 PR 里说明跑了什么。
- 架构相关代码放在架构层。参见[架构中立](../../AGENTS.md#architecture-neutrality)。

### 提交风格

提交风格定义在 __[提交模板]__ 中。可以把它当作参考，也可以执行 `git config commit.template .gitmessage` 启用，之后本地的新提交会以该模板作为初始提交信息。完整规则见[改动与提交](../../AGENTS.md#changes-and-commits)。

### AI 的使用

你可以使用 AI 工具。请在 PR 里说明哪些部分用到了 AI、你又是如何测试的。AI 写的代码和其他代码遵循同样的规则，同样要经过人工审查。

### 规则与建议

- *使用 __真实姓名__ 和 __真实邮箱__。* 我们不接受匿名代码贡献！
  - 凡是改动代码或翻译的提交，作者都应使用完整法定姓名(拉丁字母，可带变音符号)。
  - 建议 GitHub 资料里的姓名(在[这里][GitHub 资料设置]的 Name 字段)与提交中的姓名保持一致。
- 媒体类改动是例外，例如美术资源(壁纸、主题、图标、声音)和代码之外的文档。
  - 这类改动允许使用昵称或别名作为作者名；同样建议 GitHub 资料里的姓名与提交一致。
- 无论哪种情况，作者都必须使用真实邮箱，包括 git 提交(`user.email` 设置)和 GitHub [邮箱设置][GitHub 邮箱设置] —— 其中的 "Keep my email addresses private" 复选框必须取消勾选。
  - 为了保护隐私，请在 GitHub 邮箱设置里选好会用于提交的 "Primary email address"。
- *把贡献描述清楚。* 相关时请附上 issue 编号。
- *只放相关的改动。* 审查者能少回忆一些被改动的既有代码，审查会更轻松。
- *提交前先搜索相似的 PR。* 可能已经有人开过类似的 PR 或 issue，那就直接在那边评论和审查。
- *改动尽量小，集中在主题上。* 遇到无关问题，别顺手修；另开 issue。
- *遵守我们的[代码风格]。*
- *有问题尽管问。* 到 [Discord](https://discord.gg/BpcKg7FZ2) 或 [Matrix](https://matrix.to/#/#libernt:matrix.org) 找团队。

## 版权与许可

你写的代码，版权归你。LiberNT 不要求版权转让，也没有贡献者许可协议。

- 新文件使用[代码风格说明](../../AGENTS.md#code-style)中给出的文件头，在 `COPYRIGHT` 行写上你的姓名和邮箱，并加 SPDX 许可证标识符。
- 对现有文件做较大改动时，可以在已有 `COPYRIGHT` 行下面追加自己的一行。
- 不要删除或修改已有的版权、许可证声明；也不要改动顶层目录里的许可证文本。
- 保持你改动文件的许可证不变。继承自 ReactOS 的代码沿用其文件头声明的许可证。
- 不要复制与 GNU GPL 3.0 不兼容的代码，也不要复制 Linux 的 GPL 代码。

LiberNT 和 ReactOS 背后的人，见 [CREDITS.md](../../CREDITS.md)。

## 从哪里开始？

- 看看开放的 [Issues](https://github.com/LiberNT/LiberNT/issues)。
- 找一个失败的测试，与 Windows 11 对照，把它修到通过。
- 读 [Wiki](https://github.com/LiberNT/LiberNT/wiki) 里的指南和设计笔记。
- 到 [Discord](https://discord.gg/BpcKg7FZ2) 或 [Matrix](https://matrix.to/#/#libernt:matrix.org) 求助。

  [净室逆向工程]:                                            https://en.wikipedia.org/wiki/Clean_room_design
  [PR]:                                                      https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/about-pull-requests
  [GitHub 资料设置]:                                          https://github.com/settings/profile
  [GitHub 邮箱设置]:                                          https://github.com/settings/emails
  [代码风格]:                                                CODING_STYLE.md
  [提交模板]:                                                ../../.gitmessage

# 另见

- [Pull Request 管理规则](PULL_REQUEST_MANAGEMENT.md)
- [行为准则](CODE_OF_CONDUCT.md)
