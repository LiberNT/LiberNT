/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File transfer interface of the RDP console server
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once
#include <windows.h>
#include <freerdp/freerdp.h>

typedef struct RDP_TRANSFER RDP_TRANSFER;
typedef void (*TRANSFER_LOG)(const char *, ...);
RDP_TRANSFER *TransferStart(HANDLE vcm, rdpContext *rdp, const WCHAR *root, TRANSFER_LOG log);
void TransferStop(RDP_TRANSFER *transfer);
