<div dir="rtl" lang="ar">

# تثبيت LiberNT

> [English](../../INSTALL.md) | [中文](../zh-CN/INSTALL.md) | [Français](../fr-FR/INSTALL.md) | [Español](../es-ES/INSTALL.md) | [日本語](../ja-JP/INSTALL.md) | **العربية** | [Українська](../uk-UA/INSTALL.md)

## بناء الصور

جهّز بيئة البناء واضبط عملية بناء كما هو موضّح في [بناء LiberNT بنفسك](README.md#بناء-libernt-بنفسك). ثم ابنِ الصورة التي تحتاجها من مجلد الإخراج، مثلًا ‎`output-Clang-amd64-debug`‎:

| الأمر | ينشئ | استخدمه من أجل |
|---|---|---|
| ‎`ninja bootcd`‎ | ‎`bootcd.iso`‎ | تشغيل LiberNT من صورة قرص مضغوط، أو بدء برنامج إعداد LiberNT منها وتثبيت LiberNT على قرص. |
| ‎`ninja liberntimg`‎ | ‎`LiberNT.img`‎ | إقلاع قرص مثبّت عليه LiberNT مسبقًا. يقلع عبر UEFI أو BIOS. |
| ‎`ninja liberntvhd`‎ | ‎`LiberNT.vhd`‎، ‎`LiberNT.vhdx`‎ | استخدام القرص المثبّت مسبقًا نفسه في الآلات الافتراضية: VHD في VirtualBox وHyper-V من الجيل الأول، وVHDX في Hyper-V من الجيل الثاني الذي لا يقبل أقراص VHD. |

يبني كل هدف أولًا كل ما يحتاجه، وتُكتب الملفات في مجلد الإخراج.

## التثبيت من قرص الإعداد

أقلع ‎`bootcd.iso`‎ في آلة افتراضية أو على حاسوب، واتبع تعليمات برنامج إعداد LiberNT. يعرض برنامج الإعداد الأقراص كما يفعل برنامج إعداد Windows، ويستطيع إنشاء الأقسام وحذفها وتهيئتها وتقليصها وتوسيعها. ويثبّت LiberNT على NTFS وينشئ قسم نظام EFI الذي يحتاجه.

## استخدام القرص المثبّت مسبقًا

لا يحتاج ‎`LiberNT.img`‎ إلى أي إعداد. شغّله في آلة افتراضية مثل QEMU، أو اكتبه على وحدة USB أو بطاقة SD وأقلع الجهاز منها. استخدم ‎`LiberNT.vhd`‎ للآلات الافتراضية التي تقبل أقراص VHD، و‎`LiberNT.vhdx`‎ للآلات الافتراضية Hyper-V من الجيل الثاني، مع تعطيل الإقلاع الآمن.

يحمل القرص المثبّت مسبقًا أيضًا برنامج إعداد LiberNT. شغّل ‎`C:\<المعمارية>\libernt.exe`‎، مثلًا ‎`C:\arm64\libernt.exe`‎، لتثبيت LiberNT على قرص آخر من النظام قيد التشغيل.

## المساعدة

إذا حدث خطأ ما، فأبلغ عنه في [Issues](https://github.com/LiberNT/LiberNT/issues) مع ذكر المعمارية ونوع البناء والإيداع الذي بنيته والخطوات التي تؤدي إلى المشكلة. توجد الأدلة والملاحظات في [Wiki](https://github.com/LiberNT/LiberNT/wiki)، ويمكنك سؤال الفريق على [Discord](https://discord.gg/BpcKg7FZ2) أو [Matrix](https://matrix.to/#/#libernt:matrix.org).

</div>
