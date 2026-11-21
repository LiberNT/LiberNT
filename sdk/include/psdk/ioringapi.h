/*
 * Copyright (C) 2023 Paul Gofman for CodeWeavers
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

#ifndef __IORINGAPI_H_
#define __IORINGAPI_H_

#include <ntioring_x.h>

struct IORING_CAPABILITIES
{
    IORING_VERSION       MaxVersion;
    UINT32               MaxSubmissionQueueSize;
    UINT32               MaxCompletionQueueSize;
    IORING_FEATURE_FLAGS FeatureFlags;
};
typedef struct IORING_CAPABILITIES IORING_CAPABILITIES;

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI QueryIoRingCapabilities(IORING_CAPABILITIES *caps);

#ifdef __cplusplus
}
#endif
typedef enum IORING_CREATE_ADVISORY_FLAGS {
    IORING_CREATE_ADVISORY_FLAGS_NONE = 0,
    IORING_CREATE_SKIP_BUILDER_PARAM_CHECKS = 1
} IORING_CREATE_ADVISORY_FLAGS;

typedef enum IORING_CREATE_REQUIRED_FLAGS {
    IORING_CREATE_REQUIRED_FLAGS_NONE = 0
} IORING_CREATE_REQUIRED_FLAGS;

typedef enum IORING_REF_KIND {
    IORING_REF_RAW = 0,
    IORING_REF_REGISTERED = 1
} IORING_REF_KIND;

typedef enum IORING_SQE_FLAGS {
    IOSQE_FLAGS_NONE = 0,
    IOSQE_FLAGS_DRAIN_PRECEDING_OPS = 1
} IORING_SQE_FLAGS;

typedef struct HIORING__ {
  int unused;
} *HIORING;

typedef struct IORING_CQE {
  UINT_PTR UserData;
  HRESULT ResultCode;
  ULONG_PTR Information;
} IORING_CQE;

typedef struct IORING_CREATE_FLAGS {
  IORING_CREATE_REQUIRED_FLAGS Required;
  IORING_CREATE_ADVISORY_FLAGS Advisory;
} IORING_CREATE_FLAGS;

typedef struct IORING_INFO {
  IORING_VERSION IoRingVersion;
  IORING_CREATE_FLAGS Flags;
  UINT32 SubmissionQueueSize;
  UINT32 CompletionQueueSize;
} IORING_INFO;

#define _APISET_IORING_ 0
#define IoRingBufferRefFromIndexAndOffseti, o) { IORING_REF_REGISTERED, { . IndexAndOffset = { (i), (o) } } }
#define IoRingBufferRefFromPointerp) { IORING_REF_RAW, { . Address = p } }
#define IoRingHandleRefFromHandleh) { IORING_REF_RAW, { . Handle = h } }
#define IoRingHandleRefFromIndexi) { IORING_REF_REGISTERED, { . Index = i } }

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
HRESULT BuildIoRingRegisterFileHandles(HIORING ioRing, UINT32 count, const HANDLE handles[], UINT_PTR userData);
HRESULT CloseIoRing(HIORING ioRing);
HRESULT CreateIoRing(IORING_VERSION ioringVersion, IORING_CREATE_FLAGS flags, UINT32 submissionQueueSize, UINT32 completionQueueSize, HIORING *h);
HRESULT GetIoRingInfo(HIORING ioRing, IORING_INFO *info);
HRESULT PopIoRingCompletion(HIORING ioRing, IORING_CQE *cqe);
HRESULT SetIoRingCompletionEvent(HIORING ioRing, HANDLE hEvent);
HRESULT SubmitIoRing(HIORING ioRing, UINT32 waitOperations, UINT32 milliseconds, UINT32 *submittedEntries);

#ifdef __cplusplus
}
#endif

#define IoRingBufferRefFromIndexAndOffseti, o) { IORING_REF_REGISTERED, { . IndexAndOffset = { (i), (o) } } }
#define IoRingBufferRefFromPointerp) { IORING_REF_RAW, { . Address = p } }
#define IoRingHandleRefFromHandleh) { IORING_REF_RAW, { . Handle = h } }
#define IoRingHandleRefFromIndexi) { IORING_REF_REGISTERED, { . Index = i } }

struct IORING_BUFFER_REF
{
    IORING_REF_KIND Kind;
    union BufferUnion Buffer;
};

struct IORING_HANDLE_REF
{
    IORING_REF_KIND Kind;
    union HandleUnion Handle;
};

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
HRESULT BuildIoRingCancelRequest(HIORING ioRing, IORING_HANDLE_REF file, UINT_PTR opToCancel, UINT_PTR userData);
HRESULT BuildIoRingFlushFile(HIORING ioRing, IORING_HANDLE_REF fileRef, FILE_FLUSH_MODE flushMode, UINT_PTR userData, IORING_SQE_FLAGS sqeFlags);
HRESULT BuildIoRingReadFile(HIORING ioRing, IORING_HANDLE_REF fileRef, IORING_BUFFER_REF dataRef, UINT32 numberOfBytesToRead, UINT64 fileOffset, UINT_PTR userData, IORING_SQE_FLAGS sqeFlags);
HRESULT BuildIoRingReadFileScatter(HIORING ioRing, IORING_HANDLE_REF fileRef, UINT32 segmentCount, FILE_SEGMENT_ELEMENT segmentArray[], UINT32 numberOfBytesToRead, UINT64 fileOffset, UINT_PTR userData, IORING_SQE_FLAGS sqeFlags);
HRESULT BuildIoRingWriteFile(HIORING ioRing, IORING_HANDLE_REF fileRef, IORING_BUFFER_REF bufferRef, UINT32 numberOfBytesToWrite, UINT64 fileOffset, FILE_WRITE_FLAGS writeFlags, UINT_PTR userData, IORING_SQE_FLAGS sqeFlags);
HRESULT BuildIoRingWriteFileGather(HIORING ioRing, IORING_HANDLE_REF fileRef, UINT32 segmentCount, FILE_SEGMENT_ELEMENT segmentArray[], UINT32 numberOfBytesToWrite, UINT64 fileOffset, FILE_WRITE_FLAGS writeFlags, UINT_PTR userData, IORING_SQE_FLAGS sqeFlags);

#ifdef __cplusplus
}
#endif

#define IoRingBufferRefFromIndexAndOffseti, o) { IORING_REF_REGISTERED, { . IndexAndOffset = { (i), (o) } } }
#define IoRingBufferRefFromPointerp) { IORING_REF_RAW, { . Address = p } }
#define IoRingHandleRefFromHandleh) { IORING_REF_RAW, { . Handle = h } }
#define IoRingHandleRefFromIndexi) { IORING_REF_REGISTERED, { . Index = i } }

#define IoRingBufferRefFromIndexAndOffseti, o) { IORING_REF_REGISTERED, { . IndexAndOffset = { (i), (o) } } }
#define IoRingBufferRefFromPointerp) { IORING_REF_RAW, { . Address = p } }
#define IoRingHandleRefFromHandleh) { IORING_REF_RAW, { . Handle = h } }
#define IoRingHandleRefFromIndexi) { IORING_REF_REGISTERED, { . Index = i } }

#endif
