<div dir="rtl" lang="ar">

> [English](../../README.md) | [中文](../zh-CN/README.md) | [Français](../fr-FR/README.md) | [Español](../es-ES/README.md) | [日本語](../ja-JP/README.md) | **العربية** | [Українська](../uk-UA/README.md)

<p align="center">
  <img src="../../media/graphics/branding/libernt-banner.png" width="480" alt="LiberNT">
</p>

> [!IMPORTANT]
> نظام LiberNT قيد التطوير النشط. النسخ المتوفرة في صفحة [Releases](https://github.com/LiberNT/LiberNT/releases) هي إصدارات تجريبية من نوع Debug مخصصة للاختبار: شغّلها في آلة افتراضية أو على جهاز احتياطي، وليس على حاسوب تعتمد عليه.

<div align="center">

# [LiberNT](https://libernt.com)
### نظام تشغيل NT حر

**صُنع حبًّا في معمارية NT. متوافق مع Windows® 11، لمعماريات amd64 وarm64 وriscv64.**

[![Build](https://img.shields.io/github/actions/workflow/status/LiberNT/LiberNT/build.yml?branch=main-nt10&label=Build&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/LiberNT/LiberNT/actions/workflows/build.yml)
[![License: GPL 3.0](https://img.shields.io/badge/License-GPL_3.0-0161B6?style=for-the-badge&logo=gnu&logoColor=white)](#الترخيص)
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

## الرعاة

<p align="center">
  <a href="https://github.com/sponsors/ahmedarif193">
    <img src="https://img.shields.io/badge/Sponsor-LiberNT-ea4aaa?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="Sponsor LiberNT">
  </a>
</p>

---

<p align="center">
  <img width="1000" alt="سطح مكتب LiberNT على arm64" src="../../media/graphics/screenshots/libernt-desktop-arm64.png">
</p>

<p align="center"><sub>سطح مكتب LiberNT على arm64، من النسخة nt10.0-043-2555 في QEMU</sub></p>

---

LiberNT نظام تشغيل NT حر. والاسم يدل على ذلك: كلمة *liber* تعني **حر** باللاتينية. يهدف النظام إلى **التوافق مع Windows® 11**، مع ترك مساحة لميزات حرة وتجريبية.

يقدّم LiberNT عملًا واسعًا على مكونات فضاء النواة وبرامج التشغيل، موجّهًا نحو الاستقرار والعتاد الحديث، ويركّز على منصات 64 بت: **amd64 وarm64 وriscv64**.

هدفنا نظام يبقى قابلًا للصيانة على المدى الطويل: **مستقر وموثوق وفعّال**، يعمل أيضًا على الأجهزة المدمجة، وقادر على دعم كل ميزة ينبغي أن يوفرها نظام تشغيل حديث.

> [!NOTE]
> جديد على NT؟ **NT** هي المعمارية التي تقوم عليها كل الإصدارات الحديثة من Windows: نواتها ونموذج برامج التشغيل وخدمات النظام. LiberNT نظام تشغيل حر مبني على هذه المعمارية، ويتخذ من Windows 11 مرجعًا يقيس نفسه به.

---

## جدول المحتويات

- [ما الذي يتضمنه LiberNT](#ما-الذي-يتضمنه-libernt)
- [المنصات المدعومة](#المنصات-المدعومة)
- [البدء](#البدء)
  - [الخطوة 1: تنزيل نسخة](#الخطوة-1-تنزيل-نسخة)
  - [الخطوة 2: الإقلاع](#الخطوة-2-الإقلاع)
  - [الخطوة 3: تثبيت LiberNT](#الخطوة-3-تثبيت-libernt)
- [بناء LiberNT بنفسك](#بناء-libernt-بنفسك)
- [أنظمة البناء](#أنظمة-البناء)
- [بنية المشروع](#بنية-المشروع)
- [المساهمة](#المساهمة)
- [استخدام الذكاء الاصطناعي](#استخدام-الذكاء-الاصطناعي)
- [الترخيص](#الترخيص)
- [حول المشروع](#حول-المشروع)

---

## ما الذي يتضمنه LiberNT

يجلب LiberNT العتاد والمنصات الحديثة إلى معمارية NT.

| الميزة | ما تعنيه لك |
| --- | --- |
| **64 بت أولًا** | تعمل النواة وطبقة HAL وFreeLoader على amd64 وarm64 وriscv64. |
| **الإقلاع عبر UEFI** | يقلع على برامج UEFI الثابتة، مع مخزن إطارات GOP ودعم مبكر للشاشة. ويقلع القرص المثبّت مسبقًا عبر UEFI أو BIOS. |
| **ناقلات وتخزين حديثة** | PCIe وPCI والتوصيل والتشغيل (Plug and Play) وUSB عبر xHCI وSD/eMMC، إضافة إلى برامج تشغيل التخزين والناقلات. |
| **دعم اللوحات** | لوحات حقيقية، منها Raspberry Pi ووحدة التحكم في الإدخال والإخراج RP1 الخاصة بها. |
| **نموذج برامج التشغيل في Windows 11** | التوافق مع Windows 11 (NT10) والعمل على نموذج برامج التشغيل، مع التحقق من ذلك مقارنةً بـ Windows 11 نفسه. |
| **الرسوميات وتطبيقات x86 على Arm** | نموذج برامج تشغيل الرسوميات مع Mesa، وWoW64 مع FEX، خيارات بناء يمكنك تفعيلها. |
| **برنامج إعداد LiberNT** | يعرض الأقراص كما يفعل برنامج إعداد Windows، وينشئ الأقسام ويحذفها ويهيئها ويقلّصها ويوسّعها، ويثبّت LiberNT على NTFS مع قسم نظام EFI الذي يحتاجه. |
| **نسختك الخاصة** | تتيح لك ‎`menuconfig`‎ اختيار الهدف والمترجم ونوع البناء، وتفعيل الميزات أو تعطيلها. |

> [!TIP]
> كل نسخة في صفحة [Releases](https://github.com/LiberNT/LiberNT/releases) تذكر ما تغيّر منذ النسخة السابقة.

---

## المنصات المدعومة

| المعمارية | صور جاهزة | البناء من المصدر | ملاحظات |
| --- | :---: | :---: | --- |
| **amd64** | ✓ | ✓ | Clang (الافتراضي) أو GCC |
| **arm64** | ✓ | ✓ | يتضمن دعم Raspberry Pi |
| **riscv64** | ✓ | ✓ | تُبنى صورة منفصلة للوحات JH7110 |
| **i386** | | ✓ | x86 بمعمارية 32 بت |

تتضمن نسخ الإصدار أيضًا صورًا لـ PowerPC.

---

## البدء

### الخطوة 1: تنزيل نسخة

تأتي كل نسخة في صفحة [Releases](https://github.com/LiberNT/LiberNT/releases) على شكل ملف ZIP لكل صورة ومعمارية:

| الملف | ما هو |
| --- | --- |
| ‎`bootcd-iso-…-<arch>-…zip`‎ | ‎`bootcd.iso`‎، صورة قرص مضغوط تشغّل LiberNT أو تبدأ برنامج إعداد LiberNT |
| ‎`LiberNT-img-…-<arch>-…zip`‎ | ‎`LiberNT.img`‎، قرص مثبّت عليه LiberNT مسبقًا |
| ‎`LiberNT-vhd-…-<arch>-…zip`‎ | ‎`LiberNT.vhd`‎، القرص المثبّت مسبقًا نفسه للآلات الافتراضية التي تعتمد VHD |
| ‎`LiberNT-vhdx-…-<arch>-…zip`‎ | ‎`LiberNT.vhdx`‎، القرص المثبّت مسبقًا نفسه لـ Hyper-V من الجيل الثاني |

> [!NOTE]
> هذه نسخ Debug مبنية بـ Clang، تنتجها GitHub Actions من الفرع ‎`main-nt10`‎.

### الخطوة 2: الإقلاع

أسرع طريقة لتجربة LiberNT هي القرص المثبّت مسبقًا:

1. فك ضغط ‎`LiberNT.img`‎ (أو ‎`LiberNT.vhd`‎)
2. شغّله في آلة افتراضية مثل **QEMU**، أو استخدم ملف VHD في **VirtualBox** أو في آلة افتراضية **Hyper-V** من الجيل الأول. لا يقبل Hyper-V من الجيل الثاني إلا ‎`LiberNT.vhdx`‎، مع تعطيل الإقلاع الآمن
3. أو اكتب ‎`LiberNT.img`‎ على وحدة USB أو بطاقة SD وأقلع الجهاز منها. يقلع عبر UEFI أو BIOS

> [!CAUTION]
> كتابة ‎`LiberNT.img`‎ على وحدة USB أو بطاقة SD تمحو كل ما عليها. تحقّق جيدًا من الجهاز الهدف قبل ذلك.

### الخطوة 3: تثبيت LiberNT

أقلع ‎`bootcd.iso`‎ في آلة افتراضية أو على حاسوب، واتبع تعليمات **برنامج إعداد LiberNT**.

يحمل القرص المثبّت مسبقًا أيضًا برنامج إعداد LiberNT. شغّل ‎`C:\<المعمارية>\libernt.exe`‎، مثلًا ‎`C:\arm64\libernt.exe`‎، لتثبيت LiberNT على قرص آخر من النظام قيد التشغيل.

> [!CAUTION]
> يستطيع برنامج الإعداد حذف الأقسام وتهيئتها. انسخ احتياطيًا كل ما هو مهم على القرص الهدف قبل التثبيت.

راجع [INSTALL.md](INSTALL.md) للاطلاع على دليل التثبيت الكامل.

---

## بناء LiberNT بنفسك

يُبنى LiberNT باستخدام RosBE، وهي سلسلة أدوات للبناء المتقاطع. تعمل الخطوات التالية على Linux وmacOS، وعلى Windows عبر WSL2.

### 1. تثبيت سلسلة الأدوات

تحتاج إلى Git وCMake وNinja ومترجم C/C++ لجهازك. يضيف RosBE المترجمات التي تبني LiberNT.

<div dir="ltr">

```sh
curl -fsSL https://raw.githubusercontent.com/LiberNT/libernt-build-toolchain/main/rosbe-unix-bootstrap.sh | sh
```

</div>

> [!TIP]
> على Windows، ننصح باستخدام WSL2. ثبّته بالأمر ‎`wsl --install`‎، وافتح طرفية Linux التي يضيفها، واتبع فيها خطوات Linux. احتفظ بالمصدر داخل نظام ملفات Linux بدلًا من ‎`/mnt/c`‎، حيث يكون البناء أبطأ.

### 2. الحصول على المصدر

<div dir="ltr">

```sh
git clone https://github.com/LiberNT/LiberNT.git
cd LiberNT
```

</div>

> [!NOTE]
> يجلب أول تشغيل للإعداد أيضًا الوحدة الفرعية والمصادر الخارجية التي يحتاجها البناء.

### 3. الإعداد

<div dir="ltr">

```sh
./configure.sh
```

</div>

دون أي خيار، يجهّز هذا الأمر بناء AMD64 من نوع Debug باستخدام Clang في المجلد ‎`output-Clang-amd64-debug`‎. يتبع اسم المجلد دائمًا النمط ‎`output-<المترجم>-<المعمارية>-<debug|release>`‎.

| الخيار | ما يفعله |
| --- | --- |
| ‎`-a`‎، ‎`--arch <arch>`‎ | يختار المعمارية الهدف: ‎`amd64`‎ (الافتراضية) أو ‎`i386`‎ أو ‎`arm64`‎ أو ‎`riscv64`‎. |
| ‎`--clang`‎ | يبني باستخدام Clang/LLVM من RosBE. وهذا هو الافتراضي. |
| ‎`--gcc`‎ | يبني باستخدام GCC من RosBE بدلًا من Clang. نسخة RosBE لـ macOS لا تتضمن GCC. |
| ‎`-r`‎، ‎`--release`‎ | ينتج بناء Release محسّنًا. من دونه تحصل على بناء Debug، وهو أسهل في التنقيح. |
| ‎`menuconfig`‎ | يفتح قائمة أولًا لاختيار المكونات الاختيارية. تُحفظ اختياراتك في مجلد الإخراج. |
| ‎`makefiles`‎ | يستخدم ‎`make`‎ بدلًا من Ninja. |
| ‎`--no-feeds-update`‎ | يتخطى جلب المصادر الخارجية المذكورة في ‎`feeds.conf`‎، مثلًا عند العمل دون اتصال. |
| ‎`-D<name>=<value>`‎ | يمرّر إعدادًا مباشرة إلى CMake. |

على سبيل المثال:

<div dir="ltr">

```sh
./configure.sh -a arm64              # بناء ARM64 من نوع Debug باستخدام Clang
./configure.sh -a i386 --gcc         # بناء x86 بمعمارية 32 بت باستخدام GCC
./configure.sh -a amd64 --release    # بناء AMD64 محسّن
```

</div>

لبناء أكثر تخصيصًا، أضف ‎`menuconfig`‎:

<div dir="ltr">

```sh
./configure.sh menuconfig
```

</div>

يفتح قائمة، تشبه كثيرًا قائمة نواة Linux، تختار فيها الهدف والمترجم ونوع البناء، وتفعّل الميزات أو تعطّلها: توليد الشيفرة وضبطها، ونموذج برامج تشغيل الرسوميات وMesa، وWoW64 وFEX، وتنقيح النواة، وتشغيل الاختبارات، وغير ذلك. اضغط ‎`?`‎ على أي خيار للمساعدة، و‎`S`‎ للحفظ، و‎`Q`‎ للخروج. تُحفظ اختياراتك في مجلد الإخراج وتُستخدم في كل مرة تعيد فيها إعداده.

### 4. البناء

انتقل إلى مجلد الإخراج وشغّل Ninja مع الهدف الذي تريده:

<div dir="ltr">

```sh
cd output-Clang-amd64-debug
ninja liberntimg
```

</div>

| الأمر | ينشئ | استخدمه من أجل |
| --- | --- | --- |
| ‎`ninja bootcd`‎ | ‎`bootcd.iso`‎ | تثبيت LiberNT من صورة قرص مضغوط، في آلة افتراضية أو على حاسوب. |
| ‎`ninja liberntimg`‎ | ‎`LiberNT.img`‎ | إقلاع قرص مثبّت عليه LiberNT مسبقًا. شغّله في آلة افتراضية مثل QEMU، أو اكتبه على وحدة USB أو بطاقة SD. يقلع عبر UEFI أو BIOS. |
| ‎`ninja liberntvhd`‎ | ‎`LiberNT.vhd`‎، ‎`LiberNT.vhdx`‎ | استخدام القرص المثبّت مسبقًا نفسه في الآلات الافتراضية: VHD في VirtualBox وHyper-V من الجيل الأول، وVHDX في Hyper-V من الجيل الثاني الذي لا يقبل أقراص VHD. |

يبني كل هدف أولًا كل ما يحتاجه، وتُكتب الملفات في مجلد الإخراج.

---

## أنظمة البناء

| النظام | المترجمات | ملاحظات |
| --- | --- | --- |
| **Linux** x86_64 / aarch64 | Clang وGCC | سلسلة أدوات RosBE كاملة |
| **macOS** Intel / Apple Silicon | Clang | لا يوفر RosBE نسخة GCC لـ macOS |
| **Windows** | Clang وGCC | عبر WSL2، باتباع خطوات Linux |

على كل نظام، ثبّت Git وCMake وNinja وFlex وBison من مدير الحزم لديك. لا يوفر RosBE إلا المترجمات المتقاطعة. توجد سلاسل الأدوات الجاهزة في [LiberNT/libernt-build-toolchain](https://github.com/LiberNT/libernt-build-toolchain).

---

## بنية المشروع

<div dir="ltr">

```text
LiberNT/
├── base/            # التطبيقات والخدمات وبرنامج الإعداد والصدفة
├── boot/            # FreeLoader وبيانات الإقلاع وصور الإقلاع
├── bootloader/      # مدير الإقلاع ومحمّل Windows ودعم UEFI وOpen Firmware
├── dll/             # مكتبات DLL في وضع المستخدم، ومنها ntdll ومكتبات Win32
├── drivers/         # برامج التشغيل في وضع النواة: الناقلات والتخزين وأنظمة الملفات والشبكة والإدخال وغيرها
├── hal/             # طبقات تجريد العتاد لـ x86 وArm وArm64 وPowerPC وRISC-V
├── media/           # الخطوط والسمات وملفات INF والأصوات والهوية البصرية
├── modules/         # تطبيقات اختيارية واختبارات وخلفيات
├── ntoskrnl/        # نواة NT والطبقة التنفيذية
├── sdk/             # الترويسات والمكتبات وأدوات البناء
├── submodules/      # وصفات المكونات الخارجية ورقعها
├── subsystems/      # CSRSS ونظام Windows الفرعي
├── win32ss/         # win32k وUSER وGDI وبرامج تشغيل العرض
├── configure.sh     # يجهّز البناء على Linux وmacOS وWSL2
└── configure.cmd    # يجهّز البناء على Windows
```

</div>

---

## المساهمة

أبلغ عن الأخطاء واقترح الميزات في [Issues](https://github.com/LiberNT/LiberNT/issues)، وستجد الأدلة والملاحظات في [Wiki](https://github.com/LiberNT/LiberNT/wiki).

راجع [CONTRIBUTING.md](CONTRIBUTING.md) و[PULL_REQUEST_MANAGEMENT.md](PULL_REQUEST_MANAGEMENT.md). إن كنت تخطط لتغيير كبير، فاسأل أولًا في [Issues](https://github.com/LiberNT/LiberNT/issues) أو على [Discord](https://discord.gg/BpcKg7FZ2).

> [!WARNING]
> **تنبيه قانوني:** إذا كنت قد اطّلعت على الشيفرة المصدرية المملوكة لـ Microsoft Windows (بما في ذلك على سبيل المثال لا الحصر الشيفرة المسرّبة لـ Windows NT 3.5 وNT 4 و2000، وWindows Research Kernel)، فلن تُقبل مساهمتك بسبب احتمال انتهاك حقوق النشر.

---

## استخدام الذكاء الاصطناعي

نريد أن نكون واضحين: الذكاء الاصطناعي جزء من طريقة بناء LiberNT. فهو يساعدنا في مراجعة الشيفرة وكتابتها، وكل ما يكتبه يراجعه إنسان بعد ذلك. وقبل أن يصل أي شيء إلى الفرع الرئيسي، يشغّل شخص اختبارات شاملة من البداية إلى النهاية ويتحقق منها. نحن نهتم بهذا المشروع أكثر من أن نترك تراجعًا يمرّ دون أن نلاحظه.

ويساعدنا أيضًا في قراءة التوثيق واستخراج التعريفات وإصلاح الأخطاء. فلكل خطأ، يقارن ما يقوله التوثيق الرسمي في Microsoft Learn بما تفعله دالتنا فعلًا، لنرى بالضبط أين يختلف سلوكنا.

ومثل أي أداة أخرى، الذكاء الاصطناعي موجود لمساعدتنا على تحقيق أهدافنا. وهو يتبع القواعد نفسها التي نلزم بها أنفسنا، والمحددة في [AGENTS.md](../../AGENTS.md):

- نبني اعتمادًا على ترويسات Windows SDK وWDK، والرموز العامة لـ Windows 11، وتوثيق Microsoft، والتشغيل كصندوق أسود على Windows 11.
- نعمل بأسلوب الغرفة النظيفة: لا ننسخ أبدًا شيفرة Windows مفككة أو معاد تجميعها، ولا ننسخ أبدًا شيفرة GPL من Linux.
- لا يُعدّ الإصلاح إصلاحًا إلا بعد أن نراه يعمل في إقلاع حقيقي أو تشغيل للاختبارات. وبعد نجاح الاختبارات، سيحافظ نظام CI مستقبلي على نجاحها ويحفظ تقدّمنا العام.
- نقارن نتائج اختباراتنا، فحصًا بفحص، بنتائج الاختبارات نفسها عند تشغيلها على Windows 11.

> [!TIP]
> إذا كنت تستخدم الذكاء الاصطناعي في مساهماتك، فأنت مرحّب بك أيضًا. فقط أخبرنا في طلب السحب بالأجزاء التي ساعد فيها وكيف اختبرتها. هذه الصراحة هي كل ما نطلبه.

---

## الترخيص

يُرخَّص LiberNT بموجب [GNU GPL 3.0](../../COPYING). وتحتفظ الشيفرة الموروثة من مشاريع أخرى بالترخيص المذكور في ترويسات ملفاتها، ومعظمها GPL 2.0 أو أحدث. نصوص GPL 2.0 وLGPL 2.1 وBSD التي تشير إليها تلك الملفات موجودة في [COPYING2](../../COPYING2) و[COPYING.LIB](../../COPYING.LIB) و[COPYING.ARM](../../COPYING.ARM).

التغييرات التي أُجريت في LiberNT محمية بحقوق النشر © 2026 لمساهمي LiberNT. وتحتفظ الشيفرة الموروثة وشيفرة الأطراف الثالثة بإشعارات حقوق النشر الخاصة بمؤلفيها. راجع [CREDITS.md](../../CREDITS.md).

LiberNT مشروع مستقل. وهو غير تابع لشركة Microsoft، ولا تؤيده ولا ترعاه. Windows® علامة تجارية مسجلة لشركة Microsoft Corporation.

---

## حول المشروع

يبني LiberNT **مساهمو LiberNT**، وقد بدأه [Ahmed Arif](https://github.com/ahmedarif193) ويتولى صيانته [Ahmed Arif](https://github.com/ahmedarif193) و[Ostap Brehin](https://github.com/osbre) و[Vivyy](https://github.com/enginelesscc).

- [زُر منظمة LiberNT](https://github.com/LiberNT)
- [الموقع الإلكتروني](https://libernt.com)
- [نزّل أحدث نسخة](https://github.com/LiberNT/LiberNT/releases)
- [Wiki](https://github.com/LiberNT/LiberNT/wiki)
- تحدّث معنا على [Discord](https://discord.gg/BpcKg7FZ2) أو [Matrix](https://matrix.to/#/#libernt:matrix.org)

</div>
