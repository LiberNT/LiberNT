/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What one part of the file system runtime library asks of another
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeContexts(VOID);

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeWorkerThread(VOID);

VOID
NTAPI
FsRtlAcquireFileForCcFlush(
    _In_ PFILE_OBJECT FileObject);

/* EOF */
