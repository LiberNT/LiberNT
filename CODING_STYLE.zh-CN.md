# 代码风格

> [English](CODING_STYLE.md) | **中文**
>
> 翻译说明：本译文由 AI 初译并排版；术语、代码块、链接与锚点已与英文原版核对，中文术语已经人工核对。如有纰漏，请提交 issue 指出；表述以英文版为准。

本文介绍通用的代码风格准则，新增的 LiberNT 代码都应遵循。这些准则只适用于 C 和 C++ 源文件，来自 ReactOS 代码风格，是 ReactOS 成员在 2013 年 10 月的会议上确定的。

无关的改动不必顺手把既有代码转成这套风格。细节见[改写既有代码的注意事项](#改写既有代码的注意事项)。

与其他来源(例如 Wine)同步的代码不要重写。[3rd Party Files.txt](media/doc/3rd%20Party%20Files.txt) 和 [WINESYNC.txt](media/doc/WINESYNC.txt) 可以用来追踪同步文件。

## 文件结构

1. 每个新增的 LiberNT 源码文件都应带有这样的文件头：

```
/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Does cool things like Memory Management
 * COPYRIGHT:   Copyright 2017 Arno Nymous <abc@mailaddress.com>
 *              Copyright 2017 Mike Blablabla <mike@blabla.com>
 */
```

请使用 https://spdx.org/licenses 提供的 SPDX 许可证标识符。这样许可证工具才能解析我们的源文件！

如果你对某个文件有重大贡献，并且能为整个文件或其中一部分负责，就把自己加进该文件的 `COPYRIGHT` 段。每个文件的名单最多 3 人。不要删除或修改文件里已有的版权和许可证行。

旧文件头里的 `FILE` 行应当删除。

2. LiberNT 代码库使用 [Doxygen](https://www.doxygen.nl/) 生成文档，顶层目录里有 `Doxyfile`。函数请使用规范的头部注释，写法参见 ReactOS wiki 的 [API 文档](https://reactos.org/wiki/Documentation_Guidelines#API_Documentation)。

## 缩进与行宽

1. 行宽最多 **200 个字符**。
2. 行尾不要留空格或制表符。
3. 用 **4 个空格**缩进，不要用制表符！
4. switch 语句里，case 标签和 case 语句都要缩进。

**正确：**
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

**错误：**
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

5. 函数调用一行放不下时，参数这样对齐：
```c
FunctionCall(arg1,
             arg2,
             arg3);
```

6. 函数头按下面的格式书写(顺序与示例一致)：
```c
static    // scope identifier
CODE_SEG("PAGE")    // section placement
// other attributes
BOOLEAN    // return type
FASTCALL    // calling convention
IsOdd(
    _In_ UINT32 Number);
```

## 空格

1. 一元运算符两侧不加空格。
**正确：** `i++;`
**错误：** `i ++;`

2. 二元运算符和三元运算符两侧加空格。
**正确：** `a = b + c;`
**错误：** `a=b+c;`

3. 逗号和分号前不加空格。

**正确：**
```c
for (int i = 0; i < 5; i++)
    DoSomething();

func1(a, b);
```

**错误：**
```c
for (int i = 0; i < 5 ; i++)
    DoSomething();

func1(a , b) ;
```

4. 控制语句与括号之间加空格。

**正确：**
```c
if (Condition)
    DoSomething();
```

**错误：**
```c
if(Condition)
    DoSomething();
```

5. 函数名与括号之间、括号与其内容之间不加空格。

**正确：**
```c
func(a, b);
```

**错误：**
```c
func (a, b);
func( a, b );
```

## 换行

1. 每条语句单独占一行。

**正确：**
```c
x++;
y++;

if (Condition)
    DoSomething();
```

**错误：**
```c
x++; y++;

if (Condition) DoSomething();
```

## 花括号

1. 花括号(`{` 和 `}`)一律单独占一行。
2. 单行控制子句可以加花括号，但不作要求。带额外注释的单行控制子句例外。

**正确：**
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

**错误：**
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

## 控制结构

1. 控制条件里不要用反向逻辑。
**正确：** `if (i == 1)`
**错误：** `if (1 == i)`

2. 避免过多层级的嵌套控制结构，优先“线性风格”而不是“树状风格”。有助于让代码更清晰时可以用 `goto`(例如清理路径)。

**正确：**
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

**错误：**
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

## 命名

1. 变量和函数名首字母大写。开发 Win32 代码可以用匈牙利命名法，但不作要求。不用匈牙利命名法时，名字首字母同样必须大写(不要用 lowerCamelCase)，也不要用下划线分隔。

**正确：**
```c
PLIST_ENTRY FirstEntry;
VOID NTAPI IopDeleteIoCompletion(PVOID ObjectBody);
PWSTR pwszTest;
```

**错误：**
```c
PLIST_ENTRY first_entry;
VOID NTAPI iop_delete_io_completion(PVOID objectBody);
PWSTR pwsztest;
```

2. 函数名和变量名尽量不缩写；能用描述性动词就用。

3. 布尔值尽量以 "is"、"did" 这类有含义的动词开头，前提是用起来自然。

**正确：**
```c
BOOLEAN IsValid;
BOOLEAN DidSendData;
```

**错误：**
```c
BOOLEAN Valid;
BOOLEAN SentData;
```

## 注释

1. 避免浪费行数的注释：能并成一行就并成一行。

**正确：**
```c
// This is a one-line comment

/* This is a C-style comment */

// This is a comment over multiple lines.
// We don't define any strict rules for it.
```

**错误：**
```c
//
// This comment wastes two lines
//
```

## 空指针、false 与 0

1. 空指针写作 `NULL`。如果环境建议使用别的空指针写法(例如 C++11 的 `nullptr`)，当然可以用。不要用数值 `0`。

2. Win32/NT 布尔值写作 `TRUE` 和 `FALSE`。少数使用 C/C++ `bool` 变量的场合，写作 `true` 和 `false`。

3. 需要终止 ANSI 或 OEM 字符串、或检查其终止符时，用 `ANSI_NULL`；字符串是 Unicode 或宽字符串时，用 `UNICODE_NULL`。

## 改写既有代码的注意事项

- 不要把整文件重排和代码改动放进同一个提交，分开提交。
- 如果一个提交只做格式调整，请在提交信息开头加上 *[FORMATTING]* 说明。

## 其他要点

- 除非调用 API 需要，不要用 `LARGE_INTEGER`/`ULARGE_INTEGER`，改用 `INT64`/`UINT64`。
- 头文件用 `#pragma once`，不要用头文件保护宏。
- 除非必要(通常是 API 或导出符号)，不要指定调用约定。

## 使用自动代码风格工具

顶层目录的 `.clang-format` 描述了这套风格对应的 clang-format 配置。只对新代码使用，并遵循上文关于改写既有代码的注意事项。

## 另见

- ReactOS wiki 的[内核代码风格](https://reactos.org/wiki/Kernel_Coding_Style)
- ReactOS wiki 的 [GNU Indent](https://reactos.org/wiki/GNU_Indent)
