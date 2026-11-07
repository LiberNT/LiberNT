# コーディングスタイル

> [English](../../CODING_STYLE.md) | [中文](../zh-CN/CODING_STYLE.md) | [Français](../fr-FR/CODING_STYLE.md) | [Español](../es-ES/CODING_STYLE.md) | **日本語** | [العربية](../ar-MA/CODING_STYLE.md) | [Українська](../uk-UA/CODING_STYLE.md)

この記事では、LiberNT の新しいコードで使うべき一般的なコーディングスタイルのガイドラインを説明します。このガイドラインは C と C++ のソースファイルにのみ適用されます。これは ReactOS のコーディングスタイルに由来し、ReactOS のメンバーが 2013 年 10 月の会合で合意したものです。

既存のコードを、無関係な変更のついでにこのスタイルへ変換することはしません。詳しくは[既存コードの再フォーマットに関する注意](#既存コードの再フォーマットに関する注意)を参照してください。

ほかのソース（Wine など）と同期しているコードは書き換えてはいけません。同期しているファイルの追跡には [3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt) と [WINESYNC.txt](../../media/doc/WINESYNC.txt) を使えます。

## ファイルの構成

1. LiberNT の新しいソースコードファイルには、次のようなファイルヘッダーを含めてください。

```
/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Does cool things like Memory Management
 * COPYRIGHT:   Copyright 2017 Arno Nymous <abc@mailaddress.com>
 *              Copyright 2017 Mike Blablabla <mike@blabla.com>
 */
```

https://spdx.org/licenses にある SPDX ライセンス識別子を使ってください。
こうすると、ライセンスツールでソースファイルを解析できます！

ファイルに大きく貢献し、ファイル全体または一部に責任を持てる場合は、そのファイルの `COPYRIGHT` セクションに自分を追加してください。この一覧は 1 ファイルにつき 3 人までとします。ファイルにすでにある著作権とライセンスの行は、決して削除・変更しないでください。

古いヘッダーの `FILE` 行は削除してください。

2. LiberNT のコードベースではドキュメント生成ツール [Doxygen](https://www.doxygen.nl/) を使い、`Doxyfile` はトップレベルディレクトリにあります。関数には適切なヘッダーを付けてください。詳しくは ReactOS wiki の [API Documentation](https://reactos.org/wiki/Documentation_Guidelines#API_Documentation) を参照してください。

## インデントと行幅

1. 行幅は最大 **200 文字**です。
2. 行末にスペースやタブを付けないでください。
3. インデントは **4 つのスペース**で行い、タブは使わないでください！
4. switch 文では、case ラベルと case 内の文の両方をインデントしてください。

**正しい例:**
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

**誤った例:**
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

5. 関数呼び出しが 1 行に収まらないときは、引数を次のようにそろえてください。
```c
FunctionCall(arg1,
             arg2,
             arg3);
```

6. 関数ヘッダーは次の形式にしてください（例の順序を守ってください）。
```c
static    // scope identifier
CODE_SEG("PAGE")    // section placement
// other attributes
BOOLEAN    // return type
FASTCALL    // calling convention
IsOdd(
    _In_ UINT32 Number);
```

## スペース

1. 単項演算子の前後にスペースを入れないでください。  
**正しい例:** `i++;`  
**誤った例:** `i ++;`

2. 二項演算子と三項演算子の前後にはスペースを入れてください。  
**正しい例:** `a = b + c;`  
**誤った例:** `a=b+c;`

3. カンマとセミコロンの前にスペースを入れないでください。

**正しい例:**
```c
for (int i = 0; i < 5; i++)
    DoSomething();

func1(a, b);
```

**誤った例:**
```c
for (int i = 0; i < 5 ; i++)
    DoSomething();

func1(a , b) ;
```

4. 制御文とその括弧のあいだにはスペースを入れてください。

**正しい例:**
```c
if (Condition)
    DoSomething();
```

**誤った例:**
```c
if(Condition)
    DoSomething();
```

5. 関数とその括弧のあいだ、また括弧とその中身のあいだにはスペースを入れないでください。

**正しい例:**
```c
func(a, b);
```

**誤った例:**
```c
func (a, b);
func( a, b );
```

## 改行

1. 1 つの文は 1 行に書いてください。

**正しい例:**
```c
x++;
y++;

if (Condition)
    DoSomething();
```

**誤った例:**
```c
x++; y++;

if (Condition) DoSomething();
```

## 波括弧

1. 波括弧（`{` と `}`）は常にそれぞれ独立した行に置いてください。
2. 1 行の制御節では波括弧を使ってもかまいませんが、必須ではありません。ただし、コメントを伴う 1 行の制御節は例外です。

**正しい例:**
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

**誤った例:**
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

## 制御構造

1. 制御節で逆の論理を使わないでください。  
**正しい例:** `if (i == 1)`  
**誤った例:** `if (1 == i)`

2. 制御構造を何段にも入れ子にしないでください。「ツリー型」より「直線型」を優先してください。コードがすっきりする場合（たとえば後始末の経路）は `goto` を使ってください。

**正しい例:**
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

**誤った例:**
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

1. 変数と関数の名前は大文字で始めてください。Win32 向けの開発ではハンガリアン記法を使ってもかまいませんが、必須ではありません。使わない場合は、名前の最初の文字も大文字にしてください（lowerCamelCase は使いません）。区切りとしてアンダースコアも使わないでください。

**正しい例:**
```c
PLIST_ENTRY FirstEntry;
VOID NTAPI IopDeleteIoCompletion(PVOID ObjectBody);
PWSTR pwszTest;
```

**誤った例:**
```c
PLIST_ENTRY first_entry;
VOID NTAPI iop_delete_io_completion(PVOID objectBody);
PWSTR pwsztest;
```

2. 関数名や変数名を省略せず、できるだけ内容を表す動詞を使ってください。

3. 可能で用途に合う場合は、真偽値の前に「is」や「did」のような意味のある動詞を付けてください。

**正しい例:**
```c
BOOLEAN IsValid;
BOOLEAN DidSendData;
```

**誤った例:**
```c
BOOLEAN Valid;
BOOLEAN SentData;
```

## コメント

1. 1 行に収まるのに行を無駄にするコメントは避けてください。

**正しい例:**
```c
// This is a one-line comment

/* This is a C-style comment */

// This is a comment over multiple lines.
// We don't define any strict rules for it.
```

**誤った例:**
```c
//
// This comment wastes two lines
//
```

## Null、false、0

1. ヌルポインターは `NULL` と書いてください。まれに、環境が別のヌルポインター（たとえば C++11 の `nullptr`）を推奨する場合は、もちろんそちらを使ってかまいません。ただし、値 `0` は使わないでください。

2. Win32/NT の真偽値は `TRUE` と `FALSE` と書いてください。まれに C/C++ の `bool` 変数を使う場合は、`true` と `false` と書いてください。

3. ANSI または OEM 文字列を終端する、あるいはその終端を確認するときは `ANSI_NULL` を使ってください。Unicode またはワイド文字列の場合は `UNICODE_NULL` を使ってください。

## 既存コードの再フォーマットに関する注意

- ファイル全体を再フォーマットし、そこにコードの変更を混ぜることは決してしないでください。別々のコミットにしてください。
- コミットが書式の変更だけで構成される場合は、コミットメッセージの先頭に *[FORMATTING]* を付けて、そのことをはっきり示してください。

## その他

- API の使用で必要な場合を除き、`LARGE_INTEGER`/`ULARGE_INTEGER` は使わないでください。代わりに `INT64`/`UINT64` を使ってください
- ヘッダーではガード用の define ではなく `#pragma once` を使ってください
- 必要な場合（通常は API やエクスポートするシンボル）を除き、関数に呼び出し規約を指定しないでください

## 自動コードスタイルツールを使う

トップレベルディレクトリの `.clang-format` ファイルが、このスタイルを clang-format 用に記述しています。上記の既存コードの再フォーマットに関する注意に従い、新しいコードにだけ使ってください。

## 関連項目

- ReactOS wiki の [Kernel Coding Style](https://reactos.org/wiki/Kernel_Coding_Style)
- ReactOS wiki の [GNU Indent](https://reactos.org/wiki/GNU_Indent)
