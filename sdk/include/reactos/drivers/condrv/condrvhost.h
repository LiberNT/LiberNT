/*
 * PROJECT:     LiberNT Console Driver
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Console host protocol between condrv.sys, conhost and kernelbase
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define IOCTL_CONDRV_SET_CTRL_ROUTINE  CTL_CODE(FILE_DEVICE_CONSOLE, 52, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CONDRV_GET_NEXT_REQUEST  CTL_CODE(FILE_DEVICE_CONSOLE, 92, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _CONDRV_CTRL_ROUTINE
{
    ULONGLONG Routine;
    ULONG GroupId;
    ULONG Reserved;
} CONDRV_CTRL_ROUTINE, *PCONDRV_CTRL_ROUTINE;

typedef struct _CONDRV_NEXT_REQUEST_PARAMS
{
    ULONG Signal;
    ULONG Read;
    ULONG Status;
    ULONG Reserved;
} CONDRV_NEXT_REQUEST_PARAMS, *PCONDRV_NEXT_REQUEST_PARAMS;

typedef struct _CONDRV_NEXT_REQUEST_REPLY
{
    ULONG Status;
    ULONG Code;
    ULONG Output;
    ULONG OutSize;
} CONDRV_NEXT_REQUEST_REPLY, *PCONDRV_NEXT_REQUEST_REPLY;
