/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internet checksum for architectures without an assembly version
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

unsigned short
lwip_standard_chksum(const void *dataptr, int len);

unsigned short
tcpip_chksum(const void *dataptr, int len)
{
    return lwip_standard_chksum(dataptr, len);
}
