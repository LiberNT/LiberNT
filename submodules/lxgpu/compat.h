/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX names virglrenderer uses that the ReactOS CRT does not declare
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <string.h>
#include <time.h>
#include <unistd.h>

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define strtok_r strtok_s

int clock_gettime(int Clock, struct timespec *Time);
int clock_nanosleep(int Clock, int Flags, const struct timespec *Request, struct timespec *Remain);
void *lxgpu_vulkan_loader(void);
