<div dir="rtl" lang="ar">

# أسلوب كتابة الشيفرة

> [English](../../CODING_STYLE.md) | [中文](../zh-CN/CODING_STYLE.md) | [Français](../fr-FR/CODING_STYLE.md) | [Español](../es-ES/CODING_STYLE.md) | [日本語](../ja-JP/CODING_STYLE.md) | **العربية** | [Українська](../uk-UA/CODING_STYLE.md)

يصف هذا المقال الإرشادات العامة لأسلوب كتابة الشيفرة، والتي ينبغي اتباعها في الشيفرة الجديدة لـ LiberNT. تنطبق هذه الإرشادات حصرًا على ملفات المصدر بلغتي C وC++. وهي مأخوذة من أسلوب كتابة الشيفرة في ReactOS، الذي اتفق عليه أعضاء ReactOS في اجتماعهم في أكتوبر 2013.

لا تُحوَّل الشيفرة الموجودة إلى هذا الأسلوب ضمن تغيير لا علاقة له بها. راجع [ملاحظات حول إعادة تنسيق الشيفرة الموجودة](#ملاحظات-حول-إعادة-تنسيق-الشيفرة-الموجودة) لمزيد من التفاصيل.

يجب عدم إعادة كتابة الشيفرة المتزامنة مع مصادر أخرى (مثل Wine). يمكن استخدام الملفين [3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt) و[WINESYNC.txt](../../media/doc/WINESYNC.txt) لتتبّع الملفات المتزامنة.

## بنية الملفات

1. يجب أن يتضمن كل ملف مصدر جديد في LiberNT ترويسة ملف مثل هذه:

<div dir="ltr">

```
/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Does cool things like Memory Management
 * COPYRIGHT:   Copyright 2017 Arno Nymous <abc@mailaddress.com>
 *              Copyright 2017 Mike Blablabla <mike@blabla.com>
 */
```

</div>

يُرجى استخدام معرّفات تراخيص SPDX المتاحة على https://spdx.org/licenses.
فهذا يجعل ملفات المصدر لدينا قابلة للتحليل بأدوات التراخيص!

ينبغي أن تضيف نفسك إلى قسم ‎`COPYRIGHT`‎ في ملف ما إذا قدّمت مساهمة كبيرة فيه وكنت قادرًا على تحمّل المسؤولية عن الملف كله أو عن جزء منه. لا ينبغي أن يزيد عدد الأشخاص في تلك القائمة عن 3 لكل ملف. لا تحذف ولا تغيّر أبدًا أسطر حقوق النشر والترخيص الموجودة في ملف.

ينبغي حذف السطر ‎`FILE`‎ من الترويسة القديمة.

2. تُستخدم أداة توليد التوثيق [Doxygen](https://www.doxygen.nl/) في شيفرة LiberNT، مع الملف ‎`Doxyfile`‎ في المجلد الجذر، لذا استخدم ترويسة مناسبة للدوال، وراجع [API Documentation](https://reactos.org/wiki/Documentation_Guidelines#API_Documentation) في ويكي ReactOS للتفاصيل.

## الإزاحة وعرض السطر

1. يجب ألا يتجاوز عرض السطر **200 حرف**.
2. لا تضف مسافة أو علامة جدولة في نهاية أي سطر.
3. استخدم **4 مسافات** للإزاحة، ولا تستخدم علامات الجدولة!
4. أزِح كلًّا من وسم case وتعليمات كل case في تعليمة switch.

**صحيح:**

<div dir="ltr">

```c
switch (Condition)
{
    case 1:
        DoSomething();
        break;

    case 2:
    {
        DoMany();
        ManyMore();
        OtherThings();
        break;
    }
}
```

</div>

**خطأ:**

<div dir="ltr">

```c
switch(Condition)
{
case 1:
     DoSomething();
     break;
case 2:
    DoMany();
    ManyMore();
    OtherThings();
    break;
}
```

</div>

5. عندما لا يتسع استدعاء دالة لسطر واحد، اصطفّ الوسائط هكذا:

<div dir="ltr">

```c
FunctionCall(arg1,
             arg2,
             arg3);
```

</div>

6. يجب أن تكون ترويسات الدوال بهذا الشكل (مع الحفاظ على الترتيب كما في المثال):

<div dir="ltr">

```c
static    // scope identifier
CODE_SEG("PAGE")    // section placement
// other attributes
BOOLEAN    // return type
FASTCALL    // calling convention
IsOdd(
    _In_ UINT32 Number);
```

</div>

## المسافات

1. لا تضع مسافات حول العوامل الأحادية.  
**صحيح:** ‎`i++;`‎  
**خطأ:** ‎`i ++;`‎

2. ضع مسافات حول العوامل الثنائية والثلاثية.  
**صحيح:** ‎`a = b + c;`‎  
**خطأ:** ‎`a=b+c;`‎

3. لا تضع مسافات قبل الفاصلة والفاصلة المنقوطة.

**صحيح:**

<div dir="ltr">

```c
for (int i = 0; i < 5; i++)
    DoSomething();

func1(a, b);
```

</div>

**خطأ:**

<div dir="ltr">

```c
for (int i = 0; i < 5 ; i++)
    DoSomething();

func1(a , b) ;
```

</div>

4. ضع مسافات بين تعليمات التحكم وأقواسها.

**صحيح:**

<div dir="ltr">

```c
if (Condition)
    DoSomething();
```

</div>

**خطأ:**

<div dir="ltr">

```c
if(Condition)
    DoSomething();
```

</div>

5. لا تضع مسافات بين الدالة وأقواسها، ولا بين القوس ومحتواه.

**صحيح:**

<div dir="ltr">

```c
func(a, b);
```

</div>

**خطأ:**

<div dir="ltr">

```c
func (a, b);
func( a, b );
```

</div>

## فواصل الأسطر

1. يجب أن تكون لكل تعليمة سطرها الخاص.

**صحيح:**

<div dir="ltr">

```c
x++;
y++;

if (Condition)
    DoSomething();
```

</div>

**خطأ:**

<div dir="ltr">

```c
x++; y++;

if (Condition) DoSomething();
```

</div>

## الأقواس المعقوصة

1. ضع الأقواس المعقوصة (‎`{`‎ و‎`}`‎) دائمًا في أسطر مستقلة.
2. يمكن أن تستخدم عبارات التحكم المكوّنة من سطر واحد الأقواس المعقوصة، لكن ذلك ليس إلزاميًا. ويُستثنى من ذلك عبارات التحكم المكوّنة من سطر واحد التي تتضمن تعليقات إضافية.

**صحيح:**

<div dir="ltr">

```c
if (Condition)
    DoSomething();

if (Condition)
{
    DoSomething();
}

if (Condition)
{
    // This is a comment
    DoSomething();
}

if (A_Very || (Very && Long || Condition) &&
    On_Many && Lines)
{
    DoSomething();
}

if (Condition)
    DoSomething();
else
    DoSomethingElse();

if (Condition)
{
    DoSomething();
}
else
{
    DoSomethingElse();
    YetAnother();
}
```

</div>

**خطأ:**

<div dir="ltr">

```c
if (Condition) {
    DoSomething();
}

if (Condition)
    // This is a comment
    DoSomething();

if (A_Very || (Very && Long || Condition) &&
    On_Many && Lines)
    DoSomething();

if (Condition)
    DoSomething();
else {
    DoSomethingElse();
    YetAnother();
}
```

</div>

## بنى التحكم

1. لا تستخدم المنطق المعكوس في عبارات التحكم.  
**صحيح:** ‎`if (i == 1)`‎  
**خطأ:** ‎`if (1 == i)`‎

2. تجنّب كثرة مستويات بنى التحكم المتداخلة. فضّل "الأسلوب الخطي" على "الأسلوب الشجري". استخدم ‎`goto`‎ عندما يساعد على جعل الشيفرة أوضح (مثلًا في مسارات التنظيف).

**صحيح:**

<div dir="ltr">

```c
if (!func1())
    return;

i = func2();
if (i == 0)
    return;

j = func3();
if (j == 1)
    return;
...
```

</div>

**خطأ:**

<div dir="ltr">

```c
if (func1())
{
    i = func2();
    if (func2())
    {
        j = func3();
        if (func3())
        {
            ...
        }
    }
}
```

</div>

## التسمية

1. ابدأ أسماء المتغيرات والدوال بحرف كبير. يمكن استخدام الترميز الهنغاري عند التطوير لـ Win32، لكنه ليس إلزاميًا. وإذا لم تستخدمه، فيجب أن يكون الحرف الأول من الاسم كبيرًا أيضًا (لا lowerCamelCase). ولا تستخدم الشرطات السفلية فواصلَ كذلك.

**صحيح:**

<div dir="ltr">

```c
PLIST_ENTRY FirstEntry;
VOID NTAPI IopDeleteIoCompletion(PVOID ObjectBody);
PWSTR pwszTest;
```

</div>

**خطأ:**

<div dir="ltr">

```c
PLIST_ENTRY first_entry;
VOID NTAPI iop_delete_io_completion(PVOID objectBody);
PWSTR pwsztest;
```

</div>

2. تجنّب اختصار أسماء الدوال والمتغيرات، واستخدم أفعالًا وصفية حيثما أمكن.

3. اسبق القيم المنطقية بأفعال ذات معنى مثل "is" و"did" إن أمكن ذلك وناسب الاستخدام.

**صحيح:**

<div dir="ltr">

```c
BOOLEAN IsValid;
BOOLEAN DidSendData;
```

</div>

**خطأ:**

<div dir="ltr">

```c
BOOLEAN Valid;
BOOLEAN SentData;
```

</div>

## التعليقات

1. تجنّب التعليقات التي تهدر الأسطر وكان يمكن أن تتسع لسطر واحد.

**صحيح:**

<div dir="ltr">

```c
// This is a one-line comment

/* This is a C-style comment */

// This is a comment over multiple lines.
// We don't define any strict rules for it.
```

</div>

**خطأ:**

<div dir="ltr">

```c
//
// This comment wastes two lines
//
```

</div>

## القيم Null وfalse و0

1. يجب كتابة المؤشر الفارغ على شكل ‎`NULL`‎. وفي الحالة النادرة التي توصي فيها بيئتك بمؤشر فارغ مختلف (مثل ‎`nullptr`‎ في C++11)، يمكنك استخدامه بالطبع. فقط لا تستخدم القيمة ‎`0`‎.

2. يجب كتابة القيم المنطقية في Win32/NT على شكل ‎`TRUE`‎ و‎`FALSE`‎. وفي الحالة النادرة التي تستخدم فيها متغيرات ‎`bool`‎ في C/C++، يجب كتابتها على شكل ‎`true`‎ و‎`false`‎.

3. عندما تحتاج إلى إنهاء سلسلة ANSI أو OEM، أو التحقق من نهايتها، استخدم ‎`ANSI_NULL`‎. وإذا كانت السلسلة Unicode أو سلسلة محارف عريضة، فاستخدم ‎`UNICODE_NULL`‎.

## ملاحظات حول إعادة تنسيق الشيفرة الموجودة

- لا تُعِد أبدًا تنسيق ملف بالكامل وتضع فيه تغييرًا في الشيفرة في الوقت نفسه. افعل ذلك في إيداعات منفصلة.
- إذا كان الإيداع يتكوّن من تغييرات في التنسيق فقط، فاذكر ذلك بوضوح في رسالة الإيداع بأن تسبقها بـ *[FORMATTING]*.

## نقاط أخرى

- لا تستخدم ‎`LARGE_INTEGER`‎/‎`ULARGE_INTEGER`‎ إلا إذا احتجت إليهما لاستخدام واجهات برمجية. استخدم ‎`INT64`‎/‎`UINT64`‎ بدلًا منهما
- استخدم ‎`#pragma once`‎ بدلًا من تعريفات الحماية في الترويسات
- لا تحدّد اصطلاح استدعاء لدالة إلا عند الضرورة (عادةً للواجهات البرمجية أو الرموز المصدَّرة)

## استخدام أداة آلية لأسلوب الشيفرة

يصف الملف ‎`.clang-format`‎ في المجلد الجذر هذا الأسلوب لأداة clang-format. استخدمه على الشيفرة الجديدة فقط، باتباع الملاحظات السابقة حول إعادة تنسيق الشيفرة الموجودة.

## انظر أيضًا

- [Kernel Coding Style](https://reactos.org/wiki/Kernel_Coding_Style) في ويكي ReactOS
- [GNU Indent](https://reactos.org/wiki/GNU_Indent) في ويكي ReactOS

</div>
