/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Widths of the NT types on a target whose C long is wider
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

#pragma once

#if defined(__ROS_LONG64__) && !defined(_MSC_VER)
#define __NT_LONG32 int
#define __NT_ULONG32 unsigned int
#else
#define __NT_LONG32 long
#define __NT_ULONG32 unsigned long
#endif
