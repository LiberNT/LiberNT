# Style de code

> [English](../../CODING_STYLE.md) | [中文](../zh-CN/CODING_STYLE.md) | **Français** | [Español](../es-ES/CODING_STYLE.md) | [日本語](../ja-JP/CODING_STYLE.md) | [العربية](../ar-MA/CODING_STYLE.md) | [Українська](../uk-UA/CODING_STYLE.md)

Cet article décrit les règles générales de style de code à appliquer au nouveau code de LiberNT. Ces règles s'appliquent exclusivement aux fichiers source C et C++. Elles viennent du style de code de ReactOS, adopté par les membres de ReactOS lors de leur réunion d'octobre 2013.

Le code existant n'est pas converti à ce style à l'occasion d'une modification sans rapport. Consultez [Remarques sur le reformatage du code existant](#remarques-sur-le-reformatage-du-code-existant) pour plus de détails.

Le code synchronisé avec d'autres sources (comme Wine) ne doit pas être réécrit. Les fichiers [3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt) et [WINESYNC.txt](../../media/doc/WINESYNC.txt) permettent de suivre les fichiers synchronisés.

## Structure des fichiers

1. Chaque nouveau fichier source de LiberNT doit comporter un en-tête comme celui-ci :

```
/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Does cool things like Memory Management
 * COPYRIGHT:   Copyright 2017 Arno Nymous <abc@mailaddress.com>
 *              Copyright 2017 Mike Blablabla <mike@blabla.com>
 */
```

Utilisez les identifiants de licence SPDX disponibles sur https://spdx.org/licenses.
Ainsi, nos fichiers source peuvent être analysés par les outils de gestion des licences !

Ajoutez-vous à la section `COPYRIGHT` d'un fichier si vous y avez apporté une contribution importante et pouvez assumer la responsabilité de tout ou partie du fichier. Il ne doit pas y avoir plus de 3 personnes dans cette liste pour chaque fichier. Ne supprimez ni ne modifiez jamais les lignes de copyright et de licence déjà présentes dans un fichier.

La ligne `FILE` de l'ancien en-tête doit être supprimée.

2. Le générateur de documentation [Doxygen](https://www.doxygen.nl/) est utilisé pour le code de LiberNT, avec le `Doxyfile` du répertoire racine ; utilisez donc un en-tête approprié pour les fonctions, voir [API Documentation](https://reactos.org/wiki/Documentation_Guidelines#API_Documentation) dans le wiki de ReactOS pour les détails.

## Indentation et largeur de ligne

1. La largeur de ligne ne doit pas dépasser **200 caractères**.
2. N'ajoutez ni espace ni tabulation à la fin d'une ligne.
3. Indentez avec **4 espaces**, n'utilisez pas de tabulations !
4. Indentez à la fois l'étiquette case et l'instruction de chaque case d'une instruction switch.

**Correct :**
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

**Incorrect :**
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

5. Quand un appel de fonction ne tient pas sur une ligne, alignez les arguments ainsi :
```c
FunctionCall(arg1,
             arg2,
             arg3);
```

6. Les en-têtes de fonction doivent avoir ce format (en respectant l'ordre de l'exemple) :
```c
static    // scope identifier
CODE_SEG("PAGE")    // section placement
// other attributes
BOOLEAN    // return type
FASTCALL    // calling convention
IsOdd(
    _In_ UINT32 Number);
```

## Espaces

1. N'utilisez pas d'espaces autour des opérateurs unaires.  
**Correct :** `i++;`  
**Incorrect :** `i ++;`

2. Mettez des espaces autour des opérateurs binaires et ternaires.  
**Correct :** `a = b + c;`  
**Incorrect :** `a=b+c;`

3. Ne mettez pas d'espace avant la virgule et le point-virgule.

**Correct :**
```c
for (int i = 0; i < 5; i++)
    DoSomething();

func1(a, b);
```

**Incorrect :**
```c
for (int i = 0; i < 5 ; i++)
    DoSomething();

func1(a , b) ;
```

4. Mettez des espaces entre les instructions de contrôle et leurs parenthèses.

**Correct :**
```c
if (Condition)
    DoSomething();
```

**Incorrect :**
```c
if(Condition)
    DoSomething();
```

5. Ne mettez pas d'espace entre une fonction et ses parenthèses, ni entre une parenthèse et son contenu.

**Correct :**
```c
func(a, b);
```

**Incorrect :**
```c
func (a, b);
func( a, b );
```

## Retours à la ligne

1. Chaque instruction doit avoir sa propre ligne.

**Correct :**
```c
x++;
y++;

if (Condition)
    DoSomething();
```

**Incorrect :**
```c
x++; y++;

if (Condition) DoSomething();
```

## Accolades

1. Placez toujours les accolades (`{` et `}`) sur leur propre ligne.
2. Les clauses de contrôle d'une seule ligne peuvent utiliser des accolades, mais ce n'est pas obligatoire. Les clauses de contrôle d'une seule ligne accompagnées de commentaires supplémentaires font exception.

**Correct :**
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

**Incorrect :**
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

## Structures de contrôle

1. N'utilisez pas de logique inversée dans les clauses de contrôle.  
**Correct :** `if (i == 1)`  
**Incorrect :** `if (1 == i)`

2. Évitez trop de niveaux de structures de contrôle imbriquées. Préférez un "style linéaire" à un "style en arbre". Utilisez `goto` lorsque cela rend le code plus clair (par exemple pour les chemins de nettoyage).

**Correct :**
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

**Incorrect :**
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

## Nommage

1. Mettez une majuscule aux noms de variables et de fonctions. La notation hongroise peut être utilisée pour le développement Win32, mais elle n'est pas obligatoire. Si vous ne l'utilisez pas, la première lettre du nom doit aussi être une majuscule (pas de lowerCamelCase). N'utilisez pas non plus de tirets bas comme séparateurs.

**Correct :**
```c
PLIST_ENTRY FirstEntry;
VOID NTAPI IopDeleteIoCompletion(PVOID ObjectBody);
PWSTR pwszTest;
```

**Incorrect :**
```c
PLIST_ENTRY first_entry;
VOID NTAPI iop_delete_io_completion(PVOID objectBody);
PWSTR pwsztest;
```

2. Évitez d'abréger les noms de fonctions et de variables, et utilisez des verbes descriptifs autant que possible.

3. Faites précéder les valeurs booléennes de verbes significatifs comme "is" et "did" si possible et si cela convient à l'usage.

**Correct :**
```c
BOOLEAN IsValid;
BOOLEAN DidSendData;
```

**Incorrect :**
```c
BOOLEAN Valid;
BOOLEAN SentData;
```

## Commentaires

1. Évitez les commentaires qui gaspillent des lignes alors qu'ils tiendraient sur une seule.

**Correct :**
```c
// This is a one-line comment

/* This is a C-style comment */

// This is a comment over multiple lines.
// We don't define any strict rules for it.
```

**Incorrect :**
```c
//
// This comment wastes two lines
//
```

## Null, false et 0

1. Le pointeur nul doit s'écrire `NULL`. Dans le cas rare où votre environnement recommande un autre pointeur nul (par exemple `nullptr` en C++11), vous pouvez bien sûr l'utiliser. N'utilisez simplement pas la valeur `0`.

2. Les valeurs booléennes Win32/NT doivent s'écrire `TRUE` et `FALSE`. Dans le cas rare où vous utilisez des variables `bool` de C/C++, écrivez-les `true` et `false`.

3. Lorsque vous devez terminer une chaîne ANSI ou OEM, ou tester son terminateur, utilisez `ANSI_NULL`. Si la chaîne est Unicode ou en caractères larges, utilisez `UNICODE_NULL`.

## Remarques sur le reformatage du code existant

- Ne reformatez jamais entièrement un fichier en y glissant une modification de code. Faites-le dans des commits séparés.
- Si un commit ne contient que des modifications de mise en forme, indiquez-le clairement dans le message de commit en le faisant précéder de *[FORMATTING]*.

## Autres points

- N'utilisez pas `LARGE_INTEGER`/`ULARGE_INTEGER` sauf si des API l'exigent. Utilisez plutôt `INT64`/`UINT64`
- Utilisez `#pragma once` au lieu de gardes `#define` dans les en-têtes
- Ne précisez pas de convention d'appel pour une fonction sauf si nécessaire (en général pour les API ou les symboles exportés)

## Utiliser un outil automatique de mise en forme

Le fichier `.clang-format` du répertoire racine décrit ce style pour clang-format. Ne l'utilisez que sur du nouveau code, en suivant les remarques ci-dessus sur le reformatage du code existant.

## Voir aussi

- [Kernel Coding Style](https://reactos.org/wiki/Kernel_Coding_Style) dans le wiki de ReactOS
- [GNU Indent](https://reactos.org/wiki/GNU_Indent) dans le wiki de ReactOS
