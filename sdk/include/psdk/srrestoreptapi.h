/*
 * Definitions for the System File Checker (Windows File Protection)
 *
 * Copyright 2008 Pierre Schweitzer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _SRRESTOREPTAPI_H
#define _SRRESTOREPTAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Defines */

/* Event types */
#define BEGIN_NESTED_SYSTEM_CHANGE 102
#define END_NESTED_SYSTEM_CHANGE   103

/* Restore point types */
#define APPLICATION_INSTALL          0

#define MAX_DESC    64
#define MAX_DESC_W  256

/* Types */

#pragma pack(push, 1)

typedef struct _RESTOREPTINFOA {
  DWORD  dwEventType;
  DWORD  dwRestorePtType;
  INT64  llSequenceNumber;
  CHAR   szDescription[MAX_DESC];
} RESTOREPOINTINFOA, *PRESTOREPOINTINFOA;

typedef struct _RESTOREPTINFOW {
    DWORD  dwEventType;
    DWORD  dwRestorePtType;
    INT64  llSequenceNumber;
    WCHAR  szDescription[MAX_DESC_W];
} RESTOREPOINTINFOW, *PRESTOREPOINTINFOW;

typedef struct _SMGRSTATUS {
    DWORD  nStatus;
    INT64  llSequenceNumber;
} STATEMGRSTATUS, *PSTATEMGRSTATUS;

#pragma pack(pop)

/* Functions */

BOOL WINAPI SRSetRestorePointA(PRESTOREPOINTINFOA, PSTATEMGRSTATUS);
BOOL WINAPI SRSetRestorePointW(PRESTOREPOINTINFOW, PSTATEMGRSTATUS);
DWORD WINAPI SRRemoveRestorePoint(DWORD);

#ifdef __cplusplus
}
#endif

#ifndef ACCESSIBILITY_SETTING
#define ACCESSIBILITY_SETTING 3
#endif
#ifndef APPLICATION_RUN
#define APPLICATION_RUN 5
#endif
#ifndef APPLICATION_UNINSTALL
#define APPLICATION_UNINSTALL 1
#endif
#ifndef BACKUP
#define BACKUP 15
#endif
#ifndef BACKUP_RECOVERY
#define BACKUP_RECOVERY 14
#endif
#ifndef BEGIN_NESTED_SYSTEM_CHANGE_NORP
#define BEGIN_NESTED_SYSTEM_CHANGE_NORP 104
#endif
#ifndef BEGIN_SYSTEM_CHANGE
#define BEGIN_SYSTEM_CHANGE 100
#endif
#ifndef CANCELLED_OPERATION
#define CANCELLED_OPERATION 13
#endif
#ifndef CHECKPOINT
#define CHECKPOINT 7
#endif
#ifndef CRITICAL_UPDATE
#define CRITICAL_UPDATE 18
#endif
#ifndef DESKTOP_SETTING
#define DESKTOP_SETTING 2
#endif
#ifndef DEVICE_DRIVER_INSTALL
#define DEVICE_DRIVER_INSTALL 10
#endif
#ifndef END_SYSTEM_CHANGE
#define END_SYSTEM_CHANGE 101
#endif
#ifndef FIRSTRUN
#define FIRSTRUN 11
#endif
#ifndef MANUAL_CHECKPOINT
#define MANUAL_CHECKPOINT 16
#endif
#ifndef MAX_EVENT
#define MAX_EVENT 104
#endif
#ifndef MAX_RPT
#define MAX_RPT 18
#endif
#ifndef MIN_EVENT
#define MIN_EVENT 100
#endif
#ifndef MIN_RPT
#define MIN_RPT 0
#endif
#ifndef MODIFY_SETTINGS
#define MODIFY_SETTINGS 12
#endif
#ifndef OE_SETTING
#define OE_SETTING 4
#endif
#ifndef RESTORE
#define RESTORE 6
#endif
#ifndef WINDOWS_BOOT
#define WINDOWS_BOOT 9
#endif
#ifndef WINDOWS_SHUTDOWN
#define WINDOWS_SHUTDOWN 8
#endif
#ifndef WINDOWS_UPDATE
#define WINDOWS_UPDATE 17
#endif

#pragma pack(push, 1)
typedef struct _RESTOREPTINFOEX {
  FILETIME ftCreation;
  DWORD dwEventType;
  DWORD dwRestorePtType;
  DWORD dwRPNum;
  WCHAR szDescription[256];
} RESTOREPOINTINFOEX, *PRESTOREPOINTINFOEX;
#pragma pack(pop)

#define END_NESTED_SYSTEM_CHANGE_NORP 0x67
#ifdef UNICODE
#define PRESTOREPOINTINFO PRESTOREPOINTINFOW
#else
#define PRESTOREPOINTINFO PRESTOREPOINTINFOA
#endif
#define RESTOREPOINTINFO 0
#ifdef UNICODE
#define SRSetRestorePoint SRSetRestorePointW
#else
#define SRSetRestorePoint SRSetRestorePointA
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI SRSetRestorePointInternal(PRESTOREPOINTINFOW pRestorePtSpec, PSTATEMGRSTATUS pSMgrStatus, BOOL fForceSurrogate);

#ifdef __cplusplus
}
#endif

#endif
