/*
 * PROJECT:     LiberNT PSDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Process topology API set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PROCESSTOPOLOGYAPI_H_
#define _PROCESSTOPOLOGYAPI_H_

#include <apiset.h>
#include <apisetcconv.h>
#include <minwindef.h>
#include <minwinbase.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (_WIN32_WINNT >= 0x0601)
BOOL WINAPI GetProcessGroupAffinity(_In_ HANDLE hProcess, _Inout_ PUSHORT GroupCount, _Out_writes_(*GroupCount) PUSHORT GroupArray);
BOOL WINAPI GetThreadGroupAffinity(_In_ HANDLE, _Out_ PGROUP_AFFINITY);
BOOL WINAPI SetThreadGroupAffinity(_In_ HANDLE, _In_ const GROUP_AFFINITY *, _Out_opt_ PGROUP_AFFINITY);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _PROCESSTOPOLOGYAPI_H_ */
