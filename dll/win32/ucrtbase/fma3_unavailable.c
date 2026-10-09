/*
 * PROJECT:     LiberNT Universal C Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FMA3 math library switch for targets without the x64 FMA3 library
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

int __cdecl _set_FMA3_enable(int flag)
{
    (void)flag;
    return 0;
}
