/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX descriptor calls named by the felix86 sources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <fcntl.h>
#include <io.h>
#include <sys/types.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
