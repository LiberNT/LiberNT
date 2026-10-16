/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS GUI first stage setup application
 * FILE:            base/setup/lib/fileqsup.c
 * PURPOSE:         Interfacing with Setup* API File Queue support functions
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *****************************************************************/

#include "reactos.h"

#define NDEBUG
#include <debug.h>

#define _USETUP_PCH_ // Disable USetup header inclusion

#define NTOS_MODE_USER
#include <ndk/mmfuncs.h>
#include <ndk/obfuncs.h>

#include <ntstrsafe.h>

#define SetupOpenFileQueue      _SetupOpenFileQueue
#define SetupCloseFileQueue     _SetupCloseFileQueue
#define SetupQueueCopyW         _SetupQueueCopyW
#define SetupQueueDeleteW       _SetupQueueDeleteW
#define SetupQueueRenameW       _SetupQueueRenameW
#define SetupCommitFileQueueW   _SetupCommitFileQueueW

#include "fileq.c"

/* EOF */
