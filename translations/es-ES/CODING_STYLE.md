# Estilo de código

> [English](../../CODING_STYLE.md) | [中文](../zh-CN/CODING_STYLE.md) | [Français](../fr-FR/CODING_STYLE.md) | **Español** | [日本語](../ja-JP/CODING_STYLE.md) | [العربية](../ar-MA/CODING_STYLE.md) | [Українська](../uk-UA/CODING_STYLE.md)

Este artículo describe las pautas generales de estilo de código que deben usarse en el código nuevo de LiberNT. Estas pautas se aplican exclusivamente a los archivos fuente de C y C++. Proceden del estilo de código de ReactOS, que los miembros de ReactOS acordaron en su reunión de octubre de 2013.

El código existente no se convierte a este estilo como parte de un cambio no relacionado. Consulta [Notas sobre el reformateo del código existente](#notas-sobre-el-reformateo-del-código-existente) para más detalles.

El código sincronizado con otras fuentes (como Wine) no debe reescribirse. Los archivos [3rd Party Files.txt](../../media/doc/3rd%20Party%20Files.txt) y [WINESYNC.txt](../../media/doc/WINESYNC.txt) sirven para seguir los archivos sincronizados.

## Estructura de los archivos

1. Cada archivo nuevo de código fuente de LiberNT debe incluir una cabecera como esta:

```
/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Does cool things like Memory Management
 * COPYRIGHT:   Copyright 2017 Arno Nymous <abc@mailaddress.com>
 *              Copyright 2017 Mike Blablabla <mike@blabla.com>
 */
```

Usa los identificadores de licencia SPDX disponibles en https://spdx.org/licenses.
¡Así las herramientas de licencias pueden analizar nuestros archivos fuente!

Debes añadirte a la sección `COPYRIGHT` de un archivo si has hecho una contribución importante y puedes responsabilizarte de todo el archivo o de una parte. No debe haber más de 3 personas en esa lista por archivo. Nunca elimines ni cambies las líneas de copyright y de licencia que ya tiene un archivo.

La línea `FILE` de la cabecera antigua debe eliminarse.

2. Para el código de LiberNT se usa el generador de documentación [Doxygen](https://www.doxygen.nl/), con el `Doxyfile` del directorio raíz, así que usa una cabecera adecuada en las funciones; consulta [API Documentation](https://reactos.org/wiki/Documentation_Guidelines#API_Documentation) en la wiki de ReactOS para más detalles.

## Sangría y ancho de línea

1. El ancho de línea debe ser como máximo de **200 caracteres**.
2. No añadas un espacio ni un tabulador al final de ninguna línea.
3. Sangra con **4 espacios**, ¡no uses tabuladores!
4. Sangra tanto la etiqueta case como la instrucción de cada case de una instrucción switch.

**Correcto:**
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

**Incorrecto:**
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

5. Cuando una llamada a función no cabe en una línea, alinea los argumentos así:
```c
FunctionCall(arg1,
             arg2,
             arg3);
```

6. Las cabeceras de función deben tener este formato (respetando el orden del ejemplo):
```c
static    // scope identifier
CODE_SEG("PAGE")    // section placement
// other attributes
BOOLEAN    // return type
FASTCALL    // calling convention
IsOdd(
    _In_ UINT32 Number);
```

## Espacios

1. No uses espacios alrededor de los operadores unarios.  
**Correcto:** `i++;`  
**Incorrecto:** `i ++;`

2. Pon espacios alrededor de los operadores binarios y ternarios.  
**Correcto:** `a = b + c;`  
**Incorrecto:** `a=b+c;`

3. No pongas espacios antes de la coma ni del punto y coma.

**Correcto:**
```c
for (int i = 0; i < 5; i++)
    DoSomething();

func1(a, b);
```

**Incorrecto:**
```c
for (int i = 0; i < 5 ; i++)
    DoSomething();

func1(a , b) ;
```

4. Pon espacios entre las instrucciones de control y sus paréntesis.

**Correcto:**
```c
if (Condition)
    DoSomething();
```

**Incorrecto:**
```c
if(Condition)
    DoSomething();
```

5. No pongas espacios entre una función y sus paréntesis, ni entre un paréntesis y su contenido.

**Correcto:**
```c
func(a, b);
```

**Incorrecto:**
```c
func (a, b);
func( a, b );
```

## Saltos de línea

1. Cada instrucción debe ir en su propia línea.

**Correcto:**
```c
x++;
y++;

if (Condition)
    DoSomething();
```

**Incorrecto:**
```c
x++; y++;

if (Condition) DoSomething();
```

## Llaves

1. Pon siempre las llaves (`{` y `}`) en su propia línea.
2. Las cláusulas de control de una sola línea pueden usar llaves, pero no es obligatorio. La excepción son las cláusulas de control de una sola línea que incluyen comentarios adicionales.

**Correcto:**
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

**Incorrecto:**
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

## Estructuras de control

1. No uses lógica inversa en las cláusulas de control.  
**Correcto:** `if (i == 1)`  
**Incorrecto:** `if (1 == i)`

2. Evita demasiados niveles de estructuras de control anidadas. Prefiere un "estilo lineal" a un "estilo en árbol". Usa `goto` cuando ayude a que el código sea más limpio (por ejemplo, en las rutas de limpieza).

**Correcto:**
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

**Incorrecto:**
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

## Nombres

1. Escribe con mayúscula inicial los nombres de variables y funciones. La notación húngara puede usarse al desarrollar para Win32, pero no es obligatoria. Si no la usas, la primera letra del nombre también debe ser mayúscula (nada de lowerCamelCase). Tampoco uses guiones bajos como separadores.

**Correcto:**
```c
PLIST_ENTRY FirstEntry;
VOID NTAPI IopDeleteIoCompletion(PVOID ObjectBody);
PWSTR pwszTest;
```

**Incorrecto:**
```c
PLIST_ENTRY first_entry;
VOID NTAPI iop_delete_io_completion(PVOID objectBody);
PWSTR pwsztest;
```

2. Evita abreviar los nombres de funciones y variables, y usa verbos descriptivos siempre que puedas.

3. Antepón a los valores booleanos verbos significativos como "is" y "did" si es posible y encaja con el uso.

**Correcto:**
```c
BOOLEAN IsValid;
BOOLEAN DidSendData;
```

**Incorrecto:**
```c
BOOLEAN Valid;
BOOLEAN SentData;
```

## Comentarios

1. Evita los comentarios que desperdician líneas y que cabrían en una sola.

**Correcto:**
```c
// This is a one-line comment

/* This is a C-style comment */

// This is a comment over multiple lines.
// We don't define any strict rules for it.
```

**Incorrecto:**
```c
//
// This comment wastes two lines
//
```

## Null, false y 0

1. El puntero nulo debe escribirse como `NULL`. En el caso poco frecuente de que tu entorno recomiende otro puntero nulo (por ejemplo, `nullptr` de C++11), puedes usarlo, por supuesto. Pero no uses el valor `0`.

2. Los valores booleanos de Win32/NT deben escribirse como `TRUE` y `FALSE`. En el caso poco frecuente de que uses variables `bool` de C/C++, debes escribirlas como `true` y `false`.

3. Cuando necesites terminar una cadena ANSI u OEM, o comprobar su terminador, usa `ANSI_NULL`. Si la cadena es Unicode o de caracteres anchos, usa `UNICODE_NULL`.

## Notas sobre el reformateo del código existente

- Nunca reformatees un archivo por completo y metas a la vez un cambio de código. Hazlo en commits separados.
- Si un commit solo contiene cambios de formato, indícalo claramente en el mensaje del commit anteponiéndole *[FORMATTING]*.

## Otros puntos

- No uses `LARGE_INTEGER`/`ULARGE_INTEGER` salvo que lo necesites para usar API. Usa `INT64`/`UINT64` en su lugar
- Usa `#pragma once` en lugar de defines de guarda en las cabeceras
- No especifiques una convención de llamada para una función salvo que sea necesario (normalmente para API o símbolos exportados)

## Usar una herramienta automática de estilo de código

El archivo `.clang-format` del directorio raíz describe este estilo para clang-format. Úsalo solo en código nuevo, siguiendo las notas anteriores sobre el reformateo del código existente.

## Véase también

- [Kernel Coding Style](https://reactos.org/wiki/Kernel_Coding_Style) en la wiki de ReactOS
- [GNU Indent](https://reactos.org/wiki/GNU_Indent) en la wiki de ReactOS
