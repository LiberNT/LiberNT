/*
 * PROJECT:     LiberNT CRT startup
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Lets a program or DLL that must also run on Windows start on any host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

int __cdecl
__libernt_host_check(void)
{
    return 1;
}
