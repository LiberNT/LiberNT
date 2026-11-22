/*
 * PROJECT:     LiberNT Task Scheduler service
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Start the action of a registered task on demand
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

HRESULT TaskExecRun(const WCHAR *Xml, DWORD ArgCount, const WCHAR **Args, DWORD Flags, DWORD SessionId, GUID *Guid);
