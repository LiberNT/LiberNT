/*
 * ReactOS WoW64 D3DKMT functions
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

#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"

/*
 * gdi.c uses Wine's D3DKMT declarations.  Keep ReactOS additions in a
 * separate translation unit so that the current native WDDM structures can
 * be used without changing the Wine-synced source.
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include "../../../sdk/include/ddk/d3dkmthk.h"

#include "wow64win_private.h"

W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateContext(D3DKMT_CREATECONTEXT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyContext(const D3DKMT_DESTROYCONTEXT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetSharedPrimaryHandle(D3DKMT_GETSHAREDPRIMARYHANDLE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDILock(D3DKMT_LOCK *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIPresent(D3DKMT_PRESENT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUnlock(const D3DKMT_UNLOCK *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreatePagingQueue(D3DKMT_CREATEPAGINGQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyPagingQueue(D3DDDI_DESTROYPAGINGQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIMapGpuVirtualAddress(D3DDDI_MAPGPUVIRTUALADDRESS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIFreeGpuVirtualAddress(const D3DKMT_FREEGPUVIRTUALADDRESS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIInvalidateCache(const D3DKMT_INVALIDATECACHE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObjectFromGpu(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateAllocation(D3DKMT_CREATEALLOCATION *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateAllocation2(D3DKMT_CREATEALLOCATION *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIOpenResource(D3DKMT_OPENRESOURCE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIOpenResource2(D3DKMT_OPENRESOURCE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIOpenResourceFromNtHandle(D3DKMT_OPENRESOURCEFROMNTHANDLE *desc);
W32KAPI BOOLEAN WINAPI NtGdiDdDDICheckExclusiveOwnership(void);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetProcessSchedulingPriorityClass(HANDLE process, D3DKMT_SCHEDULINGPRIORITYCLASS *priority);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetProcessSchedulingPriorityClass(HANDLE process, D3DKMT_SCHEDULINGPRIORITYCLASS priority);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIReleaseProcessVidPnSourceOwners(HANDLE process);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateContextVirtual(D3DKMT_CREATECONTEXTVIRTUAL *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDILock2(D3DKMT_LOCK2 *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIMakeResident(D3DDDI_MAKERESIDENT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIEvict(D3DKMT_EVICT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUpdateGpuVirtualAddress(const D3DKMT_UPDATEGPUVIRTUALADDRESS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitCommand(const D3DKMT_SUBMITCOMMAND *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIRender(D3DKMT_RENDER *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIQueryAllocationResidency(const D3DKMT_QUERYALLOCATIONRESIDENCY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetAllocationPriority(const D3DKMT_SETALLOCATIONPRIORITY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetAllocationPriority(const D3DKMT_GETALLOCATIONPRIORITY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIOfferAllocations(const D3DKMT_OFFERALLOCATIONS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIReclaimAllocations(D3DKMT_RECLAIMALLOCATIONS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIReclaimAllocations2(D3DKMT_RECLAIMALLOCATIONS2 *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetResourcePresentPrivateDriverData(D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIRegisterTrimNotification(D3DKMT_REGISTERTRIMNOTIFICATION *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUnregisterTrimNotification(D3DKMT_UNREGISTERTRIMNOTIFICATION *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObject2(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObjectFromGpu(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObjectFromGpu2(const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateHwContext(D3DKMT_CREATEHWCONTEXT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateHwQueue(D3DKMT_CREATEHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitCommandToHwQueue(const D3DKMT_SUBMITCOMMANDTOHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue(const D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitSignalSyncObjectsToHwQueue(const D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitPresentToHwQueue(D3DKMT_SUBMITPRESENTTOHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISubmitPresentBltToHwQueue(const D3DKMT_SUBMITPRESENTBLTTOHWQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateOverlay(D3DKMT_CREATEOVERLAY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUpdateOverlay(const D3DKMT_UPDATEOVERLAY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIFlipOverlay(const D3DKMT_FLIPOVERLAY *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetDisplayModeList(D3DKMT_GETDISPLAYMODELIST *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetMultisampleMethodList(D3DKMT_GETMULTISAMPLEMETHODLIST *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetRuntimeData(const D3DKMT_GETRUNTIMEDATA *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetGammaRamp(const D3DKMT_SETGAMMARAMP *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForVerticalBlankEvent2(const D3DKMT_WAITFORVERTICALBLANKEVENT2 *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetSharedResourceAdapterLuid(D3DKMT_GETSHAREDRESOURCEADAPTERLUID *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICheckMonitorPowerState(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForVerticalBlankEvent(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForIdle(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyOverlay(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetContextSchedulingPriority(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetContextSchedulingPriority(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetDeviceState(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetScanLine(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIPollDisplayChildren(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetDisplayMode(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetDisplayPrivateDriverFormat(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISharedPrimaryLockNotification(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISharedPrimaryUnLockNotification(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObject(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObject(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObject2(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIReserveGpuVirtualAddress(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetOverlayState(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICheckSharedResourceAccess(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUnlock2(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyHwQueue(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyHwContext(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUpdateAllocationProperty(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetStablePowerState(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetContextInProcessSchedulingPriority(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetContextInProcessSchedulingPriority(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetSyncRefreshCountWaitTarget(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIIsFeatureEnabled(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetFSEBlock(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIQueryFSEBlock(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIAdjustFullscreenGamma(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyProtectedSession(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIFlushHeapTransitions(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIMarkDeviceAsError(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIQueryProtectedSessionStatus(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetHwProtectionTeardownRecovery(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDISetVidPnSourceHwProtection(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetNativeFenceLogDetail(void *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIRegisterVailProcess(void *desc);

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NodeOrdinal;
    UINT EngineAffinity;
    D3DDDI_CREATECONTEXTFLAGS Flags;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DKMT_CLIENTHINT ClientHint;
    D3DKMT_HANDLE hContext;
    ULONG pCommandBuffer;
    UINT CommandBufferSize;
    ULONG pAllocationList;
    UINT AllocationListSize;
    ULONG pPatchLocationList;
    UINT PatchLocationListSize;
    UINT64 CommandBuffer;
} D3DKMT_CREATECONTEXT32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DDDI_PAGINGQUEUE_PRIORITY Priority;
    D3DKMT_HANDLE hPagingQueue;
    D3DKMT_HANDLE hSyncObject;
    ULONG FenceValueCPUVirtualAddress;
    UINT PhysicalAdapterIndex;
} D3DKMT_CREATEPAGINGQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hPagingQueue;
    UINT64 BaseAddress;
    UINT64 MinimumAddress;
    UINT64 MaximumAddress;
    D3DKMT_HANDLE hAllocation;
    UINT64 OffsetInPages;
    UINT64 SizeInPages;
    D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;
    UINT64 DriverProtection;
    UINT Reserved0;
    UINT64 Reserved1;
    UINT64 VirtualAddress;
    UINT64 PagingFenceValue;
} D3DDDI_MAPGPUVIRTUALADDRESS32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    UINT64 BaseAddress;
    UINT64 Size;
} D3DKMT_FREEGPUVIRTUALADDRESS32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    UINT PrivateDriverData;
    UINT NumPages;
    ULONG pPages;
    ULONG pData;
    D3DDDICB_LOCKFLAGS Flags;
    UINT64 GpuVirtualAddress;
} D3DKMT_LOCK32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NumAllocations;
    ULONG phAllocations;
} D3DKMT_UNLOCK32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    ULONG Offset;
    ULONG Length;
} D3DKMT_INVALIDATECACHE32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    union
    {
        ULONG MonitoredFenceValueArray;
        UINT64 FenceValue;
        UINT64 Reserved[8];
    };
} D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32;

typedef union
{
    struct
    {
        D3DKMT_PRESENT_MODEL Model;
        UINT TokenSize;
        BYTE Payload[1072];
    };
    UINT64 Alignment;
    BYTE Bytes[1080];
} D3DKMT_PRESENTHISTORYTOKEN32;

typedef struct
{
    UINT DirtyRectCount;
    ULONG pDirtyRects;
    UINT MoveRectCount;
    ULONG pMoveRects;
} D3DKMT_PRESENT_RGNS32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    ULONG hWindow;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DKMT_HANDLE hSource;
    D3DKMT_HANDLE hDestination;
    UINT Color;
    RECT DstRect;
    RECT SrcRect;
    UINT SubRectCnt;
    ULONG pSrcSubRects;
    UINT PresentCount;
    D3DDDI_FLIPINTERVAL_TYPE FlipInterval;
    D3DKMT_PRESENTFLAGS Flags;
    ULONG BroadcastContextCount;
    D3DKMT_HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG PresentLimitSemaphore;
    D3DKMT_PRESENTHISTORYTOKEN32 PresentHistoryToken;
    ULONG pPresentRegions;
    D3DKMT_HANDLE hAdapter;
    UINT Duration;
    ULONG BroadcastSrcAllocation;
    ULONG BroadcastDstAllocation;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
    BOOLEAN bOptimizeForComposition;
} D3DKMT_PRESENT32;

typedef struct
{
    D3DKMT_HANDLE hAllocation;
    ULONG pSystemMem;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Flags;
} D3DDDI_ALLOCATIONINFO32;

typedef struct
{
    D3DKMT_HANDLE hAllocation;
    ULONG pSystemMem;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Flags;
    UINT64 GpuVirtualAddress;
    ULONG Priority;
    ULONG Reserved[5];
} D3DDDI_ALLOCATIONINFO2_32;

typedef struct
{
    D3DKMT_STANDARDALLOCATIONTYPE Type;
    ULONG Size;
    D3DKMT_CREATESTANDARDALLOCATIONFLAGS Flags;
} D3DKMT_CREATESTANDARDALLOCATION32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hGlobalShare;
    ULONG pPrivateRuntimeData;
    UINT PrivateRuntimeDataSize;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    UINT NumAllocations;
    ULONG pAllocationInfo;
    D3DKMT_CREATEALLOCATIONFLAGS Flags;
    ULONG hPrivateRuntimeResourceHandle;
} D3DKMT_CREATEALLOCATION32;

typedef struct
{
    D3DKMT_HANDLE hAllocation;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DDDI_OPENALLOCATIONINFO32;

typedef struct
{
    D3DKMT_HANDLE hAllocation;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    UINT64 GpuVirtualAddress;
    ULONG Reserved[6];
} D3DDDI_OPENALLOCATIONINFO2_32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hGlobalShare;
    UINT NumAllocations;
    ULONG pOpenAllocationInfo;
    ULONG pPrivateRuntimeData;
    UINT PrivateRuntimeDataSize;
    ULONG pResourcePrivateDriverData;
    UINT ResourcePrivateDriverDataSize;
    ULONG pTotalPrivateDriverDataBuffer;
    UINT TotalPrivateDriverDataBufferSize;
    D3DKMT_HANDLE hResource;
} D3DKMT_OPENRESOURCE32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    ULONG hNtHandle;
    UINT NumAllocations;
    ULONG pOpenAllocationInfo2;
    UINT PrivateRuntimeDataSize;
    ULONG pPrivateRuntimeData;
    UINT ResourcePrivateDriverDataSize;
    ULONG pResourcePrivateDriverData;
    UINT TotalPrivateDriverDataBufferSize;
    ULONG pTotalPrivateDriverDataBuffer;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hKeyedMutex;
    ULONG pKeyedMutexPrivateRuntimeData;
    UINT KeyedMutexPrivateRuntimeDataSize;
    D3DKMT_HANDLE hSyncObject;
} D3DKMT_OPENRESOURCEFROMNTHANDLE32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NodeOrdinal;
    UINT EngineAffinity;
    D3DDDI_CREATECONTEXTFLAGS Flags;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DKMT_CLIENTHINT ClientHint;
    D3DKMT_HANDLE hContext;
} D3DKMT_CREATECONTEXTVIRTUAL32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    D3DDDICB_LOCK2FLAGS Flags;
    ULONG pData;
} D3DKMT_LOCK2_32;

typedef struct
{
    D3DKMT_HANDLE hPagingQueue;
    UINT NumAllocations;
    ULONG AllocationList;
    ULONG PriorityList;
    D3DDDI_MAKERESIDENT_FLAGS Flags;
    UINT64 PagingFenceValue;
    UINT64 NumBytesToTrim;
} D3DDDI_MAKERESIDENT32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NumAllocations;
    ULONG AllocationList;
    D3DDDI_EVICT_FLAGS Flags;
    UINT64 NumBytesToTrim;
} D3DKMT_EVICT32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext;
    D3DKMT_HANDLE hFenceObject;
    UINT NumOperations;
    ULONG Operations;
    ULONG Reserved0;
    UINT64 Reserved1;
    UINT64 FenceValue;
    UINT Flags;
} D3DKMT_UPDATEGPUVIRTUALADDRESS32;

typedef struct
{
    UINT64 Commands;
    UINT CommandLength;
    D3DKMT_SUBMITCOMMANDFLAGS Flags;
    UINT64 PresentHistoryToken;
    UINT BroadcastContextCount;
    D3DKMT_HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    UINT NumPrimaries;
    D3DKMT_HANDLE WrittenPrimaries[D3DDDI_MAX_WRITTEN_PRIMARIES];
    UINT NumHistoryBuffers;
    ULONG HistoryBufferArray;
} D3DKMT_SUBMITCOMMAND32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    UINT CommandOffset;
    UINT CommandLength;
    UINT AllocationCount;
    UINT PatchLocationCount;
    ULONG pNewCommandBuffer;
    UINT NewCommandBufferSize;
    ULONG pNewAllocationList;
    UINT NewAllocationListSize;
    ULONG pNewPatchLocationList;
    UINT NewPatchLocationListSize;
    D3DKMT_RENDERFLAGS Flags;
    UINT64 PresentHistoryToken;
    ULONG BroadcastContextCount;
    D3DKMT_HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG QueuedBufferCount;
    UINT64 NewCommandBuffer;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DKMT_RENDER32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hResource;
    ULONG phAllocationList;
    UINT AllocationCount;
    ULONG pResidencyStatus;
} D3DKMT_QUERYALLOCATIONRESIDENCY32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hResource;
    ULONG phAllocationList;
    UINT AllocationCount;
    ULONG pPriorities;
} D3DKMT_ALLOCATIONPRIORITY32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    ULONG pResources;
    ULONG HandleList;
    UINT NumAllocations;
    D3DKMT_OFFER_PRIORITY Priority;
    D3DKMT_OFFER_FLAGS Flags;
} D3DKMT_OFFERALLOCATIONS32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    ULONG pResources;
    ULONG HandleList;
    ULONG pDiscarded;
    UINT NumAllocations;
} D3DKMT_RECLAIMALLOCATIONS32;

typedef struct
{
    D3DKMT_HANDLE hPagingQueue;
    UINT NumAllocations;
    ULONG pResources;
    ULONG HandleList;
    ULONG pResults;
    UINT64 PagingFenceValue;
} D3DKMT_RECLAIMALLOCATIONS2_32;

typedef struct
{
    D3DKMT_HANDLE hResource;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
} D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA32;

typedef struct
{
    LUID AdapterLuid;
    D3DKMT_HANDLE hDevice;
    ULONG Callback;
    ULONG Context;
    ULONG Handle;
} D3DKMT_REGISTERTRIMNOTIFICATION32;

typedef struct
{
    ULONG Handle;
    ULONG Callback;
} D3DKMT_UNREGISTERTRIMNOTIFICATION32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    UINT ObjectCount;
    D3DKMT_HANDLE ObjectHandleArray[D3DDDI_MAX_OBJECT_SIGNALED];
    D3DDDICB_SIGNALFLAGS Flags;
    ULONG BroadcastContextCount;
    D3DKMT_HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    union
    {
        UINT64 FenceValue;
        ULONG CpuEventHandle;
        UINT64 Reserved[8];
    };
} D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2_32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    union
    {
        ULONG MonitoredFenceValueArray;
        UINT64 Reserved[8];
    };
} D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU32;

typedef struct
{
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    D3DDDICB_SIGNALFLAGS Flags;
    ULONG BroadcastContextCount;
    ULONG BroadcastContextArray;
    union
    {
        ULONG CpuEventHandle;
        ULONG MonitoredFenceValueArray;
        UINT64 Reserved[8];
    };
} D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2_32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NodeOrdinal;
    UINT EngineAffinity;
    D3DDDI_CREATEHWCONTEXTFLAGS Flags;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
    D3DKMT_HANDLE hHwContext;
} D3DKMT_CREATEHWCONTEXT32;

typedef struct
{
    D3DKMT_HANDLE hHwContext;
    D3DDDI_CREATEHWQUEUEFLAGS Flags;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
    D3DKMT_HANDLE hHwQueue;
    D3DKMT_HANDLE hHwQueueProgressFence;
    ULONG HwQueueProgressFenceCPUVirtualAddress;
    UINT64 HwQueueProgressFenceGPUVirtualAddress;
} D3DKMT_CREATEHWQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hHwQueue;
    UINT64 HwQueueProgressFenceId;
    UINT64 CommandBuffer;
    UINT CommandLength;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
    UINT NumPrimaries;
    ULONG WrittenPrimaries;
} D3DKMT_SUBMITCOMMANDTOHWQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hHwQueue;
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    ULONG FenceValueArray;
} D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE32;

typedef struct
{
    D3DDDICB_SIGNALFLAGS Flags;
    ULONG BroadcastHwQueueCount;
    ULONG BroadcastHwQueueArray;
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    ULONG FenceValueArray;
} D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE32;

typedef struct
{
    ULONG hHwQueues;
    D3DKMT_PRESENT32 PrivatePresentData;
} D3DKMT_SUBMITPRESENTTOHWQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hHwQueue;
    UINT64 HwQueueProgressFenceId;
    D3DKMT_PRESENT32 PrivatePresentData;
} D3DKMT_SUBMITPRESENTBLTTOHWQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hAllocation;
    D3DDDIRECT DstRect;
    D3DDDIRECT SrcRect;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DDDI_KERNELOVERLAYINFO32;

typedef struct
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DKMT_HANDLE hDevice;
    D3DDDI_KERNELOVERLAYINFO32 OverlayInfo;
    D3DKMT_HANDLE hOverlay;
} D3DKMT_CREATEOVERLAY32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hOverlay;
    D3DDDI_KERNELOVERLAYINFO32 OverlayInfo;
} D3DKMT_UPDATEOVERLAY32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hOverlay;
    D3DKMT_HANDLE hSource;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DKMT_FLIPOVERLAY32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    ULONG pModeList;
    UINT ModeCount;
} D3DKMT_GETDISPLAYMODELIST32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Width;
    UINT Height;
    D3DDDIFORMAT Format;
    ULONG pMethodList;
    UINT MethodCount;
} D3DKMT_GETMULTISAMPLEMETHODLIST32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hGlobalShare;
    ULONG pRuntimeData;
    UINT RuntimeDataSize;
} D3DKMT_GETRUNTIMEDATA32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_GAMMARAMP_TYPE Type;
    ULONG pGammaRamp;
    UINT Size;
} D3DKMT_SETGAMMARAMP32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT NumObjects;
    ULONG ObjectHandleArray[D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS];
} D3DKMT_WAITFORVERTICALBLANKEVENT2_32;

typedef struct
{
    D3DKMT_HANDLE hGlobalShare;
    ULONG hNtHandle;
    LUID AdapterLuid;
} D3DKMT_GETSHAREDRESOURCEADAPTERLUID32;

C_ASSERT(sizeof(D3DKMT_CREATECONTEXT32) == 64);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATECONTEXT32, CommandBuffer) == 56);
C_ASSERT(sizeof(D3DKMT_CREATEPAGINGQUEUE32) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE32, PhysicalAdapterIndex) == 20);
C_ASSERT(sizeof(D3DDDI_MAPGPUVIRTUALADDRESS32) == 104);
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS32, hAllocation) == 32);
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS32, VirtualAddress) == 88);
C_ASSERT(sizeof(D3DKMT_FREEGPUVIRTUALADDRESS32) == 24);
C_ASSERT(sizeof(D3DKMT_LOCK32) == 40);
C_ASSERT(FIELD_OFFSET(D3DKMT_LOCK32, GpuVirtualAddress) == 32);
C_ASSERT(sizeof(D3DKMT_UNLOCK32) == 12);
C_ASSERT(sizeof(D3DKMT_INVALIDATECACHE32) == 16);
C_ASSERT(sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32) == 80);
C_ASSERT(FIELD_OFFSET(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32, Reserved) == 16);
C_ASSERT(sizeof(D3DKMT_PRESENTHISTORYTOKEN32) == 1080);
C_ASSERT(sizeof(D3DKMT_PRESENT_RGNS32) == 16);
C_ASSERT(sizeof(D3DKMT_PRESENT32) == 1456);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, PresentHistoryToken) == 344);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, pPresentRegions) == 1424);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, bOptimizeForComposition) == 1452);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO32) == 24);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO) == 40);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO2_32) == 56);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2_32, GpuVirtualAddress) == 24);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO2) == 96);
C_ASSERT(sizeof(D3DKMT_CREATESTANDARDALLOCATION32) == 12);
C_ASSERT(sizeof(D3DKMT_CREATESTANDARDALLOCATION) == 24);
C_ASSERT(sizeof(D3DKMT_CREATEALLOCATION32) == 44);
C_ASSERT(sizeof(D3DKMT_CREATEALLOCATION) == 72);
C_ASSERT(sizeof(D3DDDI_OPENALLOCATIONINFO32) == 12);
C_ASSERT(sizeof(D3DDDI_OPENALLOCATIONINFO) == 24);
C_ASSERT(sizeof(D3DDDI_OPENALLOCATIONINFO2_32) == 48);
C_ASSERT(FIELD_OFFSET(D3DDDI_OPENALLOCATIONINFO2_32, GpuVirtualAddress) == 16);
C_ASSERT(sizeof(D3DDDI_OPENALLOCATIONINFO2) == 80);
C_ASSERT(sizeof(D3DKMT_OPENRESOURCE32) == 44);
C_ASSERT(sizeof(D3DKMT_OPENRESOURCE) == 72);
C_ASSERT(sizeof(D3DKMT_OPENRESOURCEFROMNTHANDLE32) == 60);
C_ASSERT(sizeof(D3DKMT_OPENRESOURCEFROMNTHANDLE) == 104);
C_ASSERT(sizeof(D3DKMT_CREATECONTEXTVIRTUAL32) == 32);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATECONTEXTVIRTUAL32, hContext) == 28);
C_ASSERT(sizeof(D3DKMT_CREATECONTEXTVIRTUAL) == 40);
C_ASSERT(sizeof(D3DKMT_LOCK2_32) == 16);
C_ASSERT(FIELD_OFFSET(D3DKMT_LOCK2_32, pData) == 12);
C_ASSERT(sizeof(D3DKMT_LOCK2) == 24);
C_ASSERT(sizeof(D3DDDI_MAKERESIDENT32) == 40);
C_ASSERT(FIELD_OFFSET(D3DDDI_MAKERESIDENT32, PagingFenceValue) == 24);
C_ASSERT(sizeof(D3DDDI_MAKERESIDENT) == 48);
C_ASSERT(sizeof(D3DKMT_EVICT32) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_EVICT32, NumBytesToTrim) == 16);
C_ASSERT(sizeof(D3DKMT_EVICT) == 32);
C_ASSERT(sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS32) == 48);
C_ASSERT(FIELD_OFFSET(D3DKMT_UPDATEGPUVIRTUALADDRESS32, Reserved1) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_UPDATEGPUVIRTUALADDRESS32, Flags) == 40);
C_ASSERT(sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS) == 56);
C_ASSERT(sizeof(D3DKMT_SUBMITCOMMAND32) == 368);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITCOMMAND32, pPrivateDriverData) == 284);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITCOMMAND32, HistoryBufferArray) == 364);
C_ASSERT(sizeof(D3DKMT_SUBMITCOMMAND) == 384);
C_ASSERT(sizeof(D3DKMT_RENDER32) == 336);
C_ASSERT(FIELD_OFFSET(D3DKMT_RENDER32, PresentHistoryToken) == 48);
C_ASSERT(FIELD_OFFSET(D3DKMT_RENDER32, NewCommandBuffer) == 320);
C_ASSERT(FIELD_OFFSET(D3DKMT_RENDER32, PrivateDriverDataSize) == 332);
C_ASSERT(sizeof(D3DKMT_RENDER) == 368);
C_ASSERT(sizeof(D3DKMT_QUERYALLOCATIONRESIDENCY32) == 20);
C_ASSERT(sizeof(D3DKMT_QUERYALLOCATIONRESIDENCY) == 32);
C_ASSERT(sizeof(D3DKMT_ALLOCATIONPRIORITY32) == 20);
C_ASSERT(sizeof(D3DKMT_SETALLOCATIONPRIORITY) == 32);
C_ASSERT(sizeof(D3DKMT_GETALLOCATIONPRIORITY) == 32);
C_ASSERT(sizeof(D3DKMT_OFFERALLOCATIONS32) == 24);
C_ASSERT(sizeof(D3DKMT_OFFERALLOCATIONS) == 40);
C_ASSERT(sizeof(D3DKMT_RECLAIMALLOCATIONS32) == 20);
C_ASSERT(sizeof(D3DKMT_RECLAIMALLOCATIONS) == 40);
C_ASSERT(sizeof(D3DKMT_RECLAIMALLOCATIONS2_32) == 32);
C_ASSERT(FIELD_OFFSET(D3DKMT_RECLAIMALLOCATIONS2_32, PagingFenceValue) == 24);
C_ASSERT(sizeof(D3DKMT_RECLAIMALLOCATIONS2) == 40);
C_ASSERT(sizeof(D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA32) == 12);
C_ASSERT(sizeof(D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA) == 16);
C_ASSERT(sizeof(D3DKMT_REGISTERTRIMNOTIFICATION32) == 24);
C_ASSERT(sizeof(D3DKMT_REGISTERTRIMNOTIFICATION) == 40);
C_ASSERT(sizeof(D3DKMT_UNREGISTERTRIMNOTIFICATION32) == 8);
C_ASSERT(sizeof(D3DKMT_UNREGISTERTRIMNOTIFICATION) == 16);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2_32) == 464);
C_ASSERT(FIELD_OFFSET(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2_32, FenceValue) == 400);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2) == 464);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU32) == 80);
C_ASSERT(FIELD_OFFSET(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU32, Reserved) == 16);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU) == 80);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2_32) == 88);
C_ASSERT(FIELD_OFFSET(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2_32, Reserved) == 24);
C_ASSERT(sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2) == 96);
C_ASSERT(sizeof(D3DKMT_CREATEHWCONTEXT32) == 28);
C_ASSERT(sizeof(D3DKMT_CREATEHWCONTEXT) == 40);
C_ASSERT(sizeof(D3DKMT_CREATEHWQUEUE32) == 40);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEHWQUEUE32, HwQueueProgressFenceGPUVirtualAddress) == 32);
C_ASSERT(sizeof(D3DKMT_CREATEHWQUEUE) == 48);
C_ASSERT(sizeof(D3DKMT_SUBMITCOMMANDTOHWQUEUE32) == 48);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITCOMMANDTOHWQUEUE32, HwQueueProgressFenceId) == 8);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITCOMMANDTOHWQUEUE32, WrittenPrimaries) == 40);
C_ASSERT(sizeof(D3DKMT_SUBMITCOMMANDTOHWQUEUE) == 56);
C_ASSERT(sizeof(D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE32) == 16);
C_ASSERT(sizeof(D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE) == 24);
C_ASSERT(sizeof(D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE32) == 24);
C_ASSERT(sizeof(D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE) == 40);
C_ASSERT(sizeof(D3DKMT_SUBMITPRESENTTOHWQUEUE32) == 1464);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITPRESENTTOHWQUEUE32, PrivatePresentData) == 8);
C_ASSERT(sizeof(D3DKMT_SUBMITPRESENTTOHWQUEUE) == 1504);
C_ASSERT(sizeof(D3DKMT_SUBMITPRESENTBLTTOHWQUEUE32) == 1472);
C_ASSERT(FIELD_OFFSET(D3DKMT_SUBMITPRESENTBLTTOHWQUEUE32, PrivatePresentData) == 16);
C_ASSERT(sizeof(D3DKMT_SUBMITPRESENTBLTTOHWQUEUE) == 1512);
C_ASSERT(sizeof(D3DKMT_CREATEOVERLAY32) == 56);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEOVERLAY32, hOverlay) == 52);
C_ASSERT(sizeof(D3DKMT_CREATEOVERLAY) == 72);
C_ASSERT(sizeof(D3DKMT_UPDATEOVERLAY32) == 52);
C_ASSERT(sizeof(D3DKMT_UPDATEOVERLAY) == 64);
C_ASSERT(sizeof(D3DKMT_FLIPOVERLAY32) == 20);
C_ASSERT(sizeof(D3DKMT_FLIPOVERLAY) == 32);
C_ASSERT(sizeof(D3DKMT_GETDISPLAYMODELIST32) == 16);
C_ASSERT(sizeof(D3DKMT_GETDISPLAYMODELIST) == 24);
C_ASSERT(sizeof(D3DKMT_GETMULTISAMPLEMETHODLIST32) == 28);
C_ASSERT(sizeof(D3DKMT_GETMULTISAMPLEMETHODLIST) == 40);
C_ASSERT(sizeof(D3DKMT_GETRUNTIMEDATA32) == 16);
C_ASSERT(sizeof(D3DKMT_GETRUNTIMEDATA) == 24);
C_ASSERT(sizeof(D3DKMT_SETGAMMARAMP32) == 20);
C_ASSERT(sizeof(D3DKMT_SETGAMMARAMP) == 32);
C_ASSERT(sizeof(D3DKMT_WAITFORVERTICALBLANKEVENT2_32) == 48);
C_ASSERT(sizeof(D3DKMT_WAITFORVERTICALBLANKEVENT2) == 80);
C_ASSERT(sizeof(D3DKMT_GETSHAREDRESOURCEADAPTERLUID32) == 16);
C_ASSERT(sizeof(D3DKMT_GETSHAREDRESOURCEADAPTERLUID) == 24);

struct trim_notification
{
    void *handle;
    ULONG callback;
};

static struct trim_notification *trim_notifications;
static ULONG trim_notification_count;
static RTL_SRWLOCK trim_notification_lock = RTL_SRWLOCK_INIT;

static NTSTATUS present_32to64(const D3DKMT_PRESENT32 *desc32, D3DKMT_PRESENT *desc,
                               D3DKMT_PRESENT_RGNS *regions)
{
    const D3DKMT_PRESENT_RGNS32 *regions32;

    if (desc32->PresentHistoryToken.Model != D3DKMT_PM_UNINITIALIZED ||
        desc32->PresentHistoryToken.TokenSize != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    memset(desc, 0, sizeof(*desc));
    desc->hContext = desc32->hContext;
    desc->hWindow = UlongToHandle(desc32->hWindow);
    desc->VidPnSourceId = desc32->VidPnSourceId;
    desc->hSource = desc32->hSource;
    desc->hDestination = desc32->hDestination;
    desc->Color = desc32->Color;
    desc->DstRect = desc32->DstRect;
    desc->SrcRect = desc32->SrcRect;
    desc->SubRectCnt = desc32->SubRectCnt;
    desc->pSrcSubRects = ULongToPtr(desc32->pSrcSubRects);
    desc->PresentCount = desc32->PresentCount;
    desc->FlipInterval = desc32->FlipInterval;
    desc->Flags = desc32->Flags;
    desc->BroadcastContextCount = desc32->BroadcastContextCount;
    memcpy(desc->BroadcastContext, desc32->BroadcastContext, sizeof(desc->BroadcastContext));
    desc->PresentLimitSemaphore = UlongToHandle(desc32->PresentLimitSemaphore);

    regions32 = ULongToPtr(desc32->pPresentRegions);
    if (regions32)
    {
        regions->DirtyRectCount = regions32->DirtyRectCount;
        regions->pDirtyRects = ULongToPtr(regions32->pDirtyRects);
        regions->MoveRectCount = regions32->MoveRectCount;
        regions->pMoveRects = ULongToPtr(regions32->pMoveRects);
        desc->pPresentRegions = regions;
    }

    desc->hAdapter = desc32->hAdapter;
    desc->Duration = desc32->Duration;
    desc->BroadcastSrcAllocation = ULongToPtr(desc32->BroadcastSrcAllocation);
    desc->BroadcastDstAllocation = ULongToPtr(desc32->BroadcastDstAllocation);
    desc->PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc->pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    return STATUS_SUCCESS;
}

static void create_allocation_32to64(const D3DKMT_CREATEALLOCATION32 *desc32, D3DKMT_CREATEALLOCATION *desc,
                                     D3DKMT_CREATESTANDARDALLOCATION *standard)
{
    const D3DKMT_CREATESTANDARDALLOCATION32 *standard32;

    memset(desc, 0, sizeof(*desc));
    desc->hDevice = desc32->hDevice;
    desc->hResource = desc32->hResource;
    desc->hGlobalShare = desc32->hGlobalShare;
    desc->pPrivateRuntimeData = ULongToPtr(desc32->pPrivateRuntimeData);
    desc->PrivateRuntimeDataSize = desc32->PrivateRuntimeDataSize;
    desc->pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    standard32 = ULongToPtr(desc32->pPrivateDriverData);
    if (desc32->Flags.StandardAllocation && standard32)
    {
        memset(standard, 0, sizeof(*standard));
        standard->Type = standard32->Type;
        standard->ExistingHeapData.Size = standard32->Size;
        standard->Flags = standard32->Flags;
        desc->pStandardAllocation = standard;
    }
    desc->PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc->NumAllocations = desc32->NumAllocations;
    desc->Flags = desc32->Flags;
    desc->hPrivateRuntimeResourceHandle = UlongToHandle(desc32->hPrivateRuntimeResourceHandle);
}

static void open_resource_32to64(const D3DKMT_OPENRESOURCE32 *desc32, D3DKMT_OPENRESOURCE *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->hDevice = desc32->hDevice;
    desc->hGlobalShare = desc32->hGlobalShare;
    desc->NumAllocations = desc32->NumAllocations;
    desc->pPrivateRuntimeData = ULongToPtr(desc32->pPrivateRuntimeData);
    desc->PrivateRuntimeDataSize = desc32->PrivateRuntimeDataSize;
    desc->pResourcePrivateDriverData = ULongToPtr(desc32->pResourcePrivateDriverData);
    desc->ResourcePrivateDriverDataSize = desc32->ResourcePrivateDriverDataSize;
    desc->pTotalPrivateDriverDataBuffer = ULongToPtr(desc32->pTotalPrivateDriverDataBuffer);
    desc->TotalPrivateDriverDataBufferSize = desc32->TotalPrivateDriverDataBufferSize;
    desc->hResource = desc32->hResource;
}

static void overlay_info_32to64(const D3DDDI_KERNELOVERLAYINFO32 *info32, D3DDDI_KERNELOVERLAYINFO *info)
{
    info->hAllocation = info32->hAllocation;
    info->DstRect = info32->DstRect;
    info->SrcRect = info32->SrcRect;
    info->pPrivateDriverData = ULongToPtr(info32->pPrivateDriverData);
    info->PrivateDriverDataSize = info32->PrivateDriverDataSize;
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateContext(UINT *args)
{
    D3DKMT_CREATECONTEXT32 *desc32 = get_ptr(&args);
    D3DKMT_CREATECONTEXT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.NodeOrdinal = desc32->NodeOrdinal;
    desc.EngineAffinity = desc32->EngineAffinity;
    desc.Flags = desc32->Flags;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.ClientHint = desc32->ClientHint;

    status = NtGdiDdDDICreateContext(&desc);
    desc32->hContext = desc.hContext;
    desc32->pCommandBuffer = PtrToUlong(desc.pCommandBuffer);
    desc32->CommandBufferSize = desc.CommandBufferSize;
    desc32->pAllocationList = PtrToUlong(desc.pAllocationList);
    desc32->AllocationListSize = desc.AllocationListSize;
    desc32->pPatchLocationList = PtrToUlong(desc.pPatchLocationList);
    desc32->PatchLocationListSize = desc.PatchLocationListSize;
    desc32->CommandBuffer = desc.CommandBuffer;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyContext(UINT *args)
{
    const D3DKMT_DESTROYCONTEXT *desc32 = get_ptr(&args);
    D3DKMT_DESTROYCONTEXT desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hContext = desc32->hContext;
    return NtGdiDdDDIDestroyContext(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetSharedPrimaryHandle(UINT *args)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE *desc32 = get_ptr(&args);
    D3DKMT_GETSHAREDPRIMARYHANDLE desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hAdapter = desc32->hAdapter;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.hSharedPrimary = desc32->hSharedPrimary;
    status = NtGdiDdDDIGetSharedPrimaryHandle(&desc);
    desc32->hSharedPrimary = desc.hSharedPrimary;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDILock(UINT *args)
{
    D3DKMT_LOCK32 *desc32 = get_ptr(&args);
    D3DKMT_LOCK desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hAllocation = desc32->hAllocation;
    desc.PrivateDriverData = desc32->PrivateDriverData;
    desc.NumPages = desc32->NumPages;
    desc.pPages = ULongToPtr(desc32->pPages);
    desc.Flags = desc32->Flags;

    status = NtGdiDdDDILock(&desc);
    desc32->hAllocation = desc.hAllocation;
    desc32->pData = PtrToUlong(desc.pData);
    desc32->GpuVirtualAddress = desc.GpuVirtualAddress;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIPresent(UINT *args)
{
    D3DKMT_PRESENT32 *desc32 = get_ptr(&args);
    D3DKMT_PRESENT_RGNS regions;
    D3DKMT_PRESENT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    status = present_32to64(desc32, &desc, &regions);
    if (status) return status;

    status = NtGdiDdDDIPresent(&desc);
    desc32->bOptimizeForComposition = desc.bOptimizeForComposition;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUnlock(UINT *args)
{
    const D3DKMT_UNLOCK32 *desc32 = get_ptr(&args);
    D3DKMT_UNLOCK desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hDevice = desc32->hDevice;
    desc.NumAllocations = desc32->NumAllocations;
    desc.phAllocations = ULongToPtr(desc32->phAllocations);
    return NtGdiDdDDIUnlock(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreatePagingQueue(UINT *args)
{
    D3DKMT_CREATEPAGINGQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_CREATEPAGINGQUEUE desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.Priority = desc32->Priority;
    desc.PhysicalAdapterIndex = desc32->PhysicalAdapterIndex;

    status = NtGdiDdDDICreatePagingQueue(&desc);
    desc32->hPagingQueue = desc.hPagingQueue;
    desc32->hSyncObject = desc.hSyncObject;
    desc32->FenceValueCPUVirtualAddress = PtrToUlong(desc.FenceValueCPUVirtualAddress);
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyPagingQueue(UINT *args)
{
    const D3DDDI_DESTROYPAGINGQUEUE *desc32 = get_ptr(&args);
    D3DDDI_DESTROYPAGINGQUEUE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hPagingQueue = desc32->hPagingQueue;
    return NtGdiDdDDIDestroyPagingQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIMapGpuVirtualAddress(UINT *args)
{
    D3DDDI_MAPGPUVIRTUALADDRESS32 *desc32 = get_ptr(&args);
    D3DDDI_MAPGPUVIRTUALADDRESS desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hPagingQueue = desc32->hPagingQueue;
    desc.BaseAddress = desc32->BaseAddress;
    desc.MinimumAddress = desc32->MinimumAddress;
    desc.MaximumAddress = desc32->MaximumAddress;
    desc.hAllocation = desc32->hAllocation;
    desc.OffsetInPages = desc32->OffsetInPages;
    desc.SizeInPages = desc32->SizeInPages;
    desc.Protection = desc32->Protection;
    desc.DriverProtection = desc32->DriverProtection;
    desc.Reserved0 = desc32->Reserved0;
    desc.Reserved1 = desc32->Reserved1;
    desc.VirtualAddress = desc32->VirtualAddress;
    desc.PagingFenceValue = desc32->PagingFenceValue;

    status = NtGdiDdDDIMapGpuVirtualAddress(&desc);
    desc32->VirtualAddress = desc.VirtualAddress;
    desc32->PagingFenceValue = desc.PagingFenceValue;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIFreeGpuVirtualAddress(UINT *args)
{
    const D3DKMT_FREEGPUVIRTUALADDRESS32 *desc32 = get_ptr(&args);
    D3DKMT_FREEGPUVIRTUALADDRESS desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hAdapter = desc32->hAdapter;
    desc.BaseAddress = desc32->BaseAddress;
    desc.Size = desc32->Size;
    return NtGdiDdDDIFreeGpuVirtualAddress(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIInvalidateCache(UINT *args)
{
    const D3DKMT_INVALIDATECACHE32 *desc32 = get_ptr(&args);
    D3DKMT_INVALIDATECACHE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hDevice = desc32->hDevice;
    desc.hAllocation = desc32->hAllocation;
    desc.Offset = desc32->Offset;
    desc.Length = desc32->Length;
    return NtGdiDdDDIInvalidateCache(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForSynchronizationObjectFromGpu(UINT *args)
{
    const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32 *desc32 = get_ptr(&args);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hContext = desc32->hContext;
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.MonitoredFenceValueArray = ULongToPtr(desc32->MonitoredFenceValueArray);
    return NtGdiDdDDIWaitForSynchronizationObjectFromGpu(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICheckMonitorPowerState(UINT *args)
{
    return NtGdiDdDDICheckMonitorPowerState(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForVerticalBlankEvent(UINT *args)
{
    return NtGdiDdDDIWaitForVerticalBlankEvent(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForIdle(UINT *args)
{
    return NtGdiDdDDIWaitForIdle(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyOverlay(UINT *args)
{
    return NtGdiDdDDIDestroyOverlay(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetContextSchedulingPriority(UINT *args)
{
    return NtGdiDdDDIGetContextSchedulingPriority(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetContextSchedulingPriority(UINT *args)
{
    return NtGdiDdDDISetContextSchedulingPriority(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetDeviceState(UINT *args)
{
    return NtGdiDdDDIGetDeviceState(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetScanLine(UINT *args)
{
    return NtGdiDdDDIGetScanLine(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIPollDisplayChildren(UINT *args)
{
    return NtGdiDdDDIPollDisplayChildren(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetDisplayMode(UINT *args)
{
    return NtGdiDdDDISetDisplayMode(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetDisplayPrivateDriverFormat(UINT *args)
{
    return NtGdiDdDDISetDisplayPrivateDriverFormat(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISharedPrimaryLockNotification(UINT *args)
{
    return NtGdiDdDDISharedPrimaryLockNotification(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISharedPrimaryUnLockNotification(UINT *args)
{
    return NtGdiDdDDISharedPrimaryUnLockNotification(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISignalSynchronizationObject(UINT *args)
{
    return NtGdiDdDDISignalSynchronizationObject(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForSynchronizationObject(UINT *args)
{
    return NtGdiDdDDIWaitForSynchronizationObject(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForSynchronizationObject2(UINT *args)
{
    return NtGdiDdDDIWaitForSynchronizationObject2(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIReserveGpuVirtualAddress(UINT *args)
{
    return NtGdiDdDDIReserveGpuVirtualAddress(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetOverlayState(UINT *args)
{
    return NtGdiDdDDIGetOverlayState(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDICheckSharedResourceAccess(UINT *args)
{
    return NtGdiDdDDICheckSharedResourceAccess(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUnlock2(UINT *args)
{
    return NtGdiDdDDIUnlock2(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyHwQueue(UINT *args)
{
    return NtGdiDdDDIDestroyHwQueue(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyHwContext(UINT *args)
{
    return NtGdiDdDDIDestroyHwContext(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUpdateAllocationProperty(UINT *args)
{
    return NtGdiDdDDIUpdateAllocationProperty(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetStablePowerState(UINT *args)
{
    return NtGdiDdDDISetStablePowerState(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetContextInProcessSchedulingPriority(UINT *args)
{
    return NtGdiDdDDISetContextInProcessSchedulingPriority(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetContextInProcessSchedulingPriority(UINT *args)
{
    return NtGdiDdDDIGetContextInProcessSchedulingPriority(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetSyncRefreshCountWaitTarget(UINT *args)
{
    return NtGdiDdDDISetSyncRefreshCountWaitTarget(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIIsFeatureEnabled(UINT *args)
{
    return NtGdiDdDDIIsFeatureEnabled(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetFSEBlock(UINT *args)
{
    return NtGdiDdDDISetFSEBlock(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIQueryFSEBlock(UINT *args)
{
    return NtGdiDdDDIQueryFSEBlock(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIAdjustFullscreenGamma(UINT *args)
{
    return NtGdiDdDDIAdjustFullscreenGamma(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyProtectedSession(UINT *args)
{
    return NtGdiDdDDIDestroyProtectedSession(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIFlushHeapTransitions(UINT *args)
{
    return NtGdiDdDDIFlushHeapTransitions(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIMarkDeviceAsError(UINT *args)
{
    return NtGdiDdDDIMarkDeviceAsError(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIQueryProtectedSessionStatus(UINT *args)
{
    return NtGdiDdDDIQueryProtectedSessionStatus(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName(UINT *args)
{
    return NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetHwProtectionTeardownRecovery(UINT *args)
{
    return NtGdiDdDDISetHwProtectionTeardownRecovery(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetVidPnSourceHwProtection(UINT *args)
{
    return NtGdiDdDDISetVidPnSourceHwProtection(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetNativeFenceLogDetail(UINT *args)
{
    return NtGdiDdDDIGetNativeFenceLogDetail(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDIRegisterVailProcess(UINT *args)
{
    return NtGdiDdDDIRegisterVailProcess(get_ptr(&args));
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateAllocation(UINT *args)
{
    D3DKMT_CREATEALLOCATION32 *desc32 = get_ptr(&args);
    D3DKMT_CREATESTANDARDALLOCATION standard;
    D3DDDI_ALLOCATIONINFO32 *allocs32;
    D3DKMT_CREATEALLOCATION desc;
    NTSTATUS status;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    create_allocation_32to64(desc32, &desc, &standard);
    allocs32 = ULongToPtr(desc32->pAllocationInfo);
    if (allocs32 && desc32->NumAllocations)
    {
        desc.pAllocationInfo = Wow64AllocateTemp((SIZE_T)desc32->NumAllocations * sizeof(*desc.pAllocationInfo));
        if (!desc.pAllocationInfo) return STATUS_NO_MEMORY;
        memset(desc.pAllocationInfo, 0, (SIZE_T)desc32->NumAllocations * sizeof(*desc.pAllocationInfo));
        for (i = 0; i < desc32->NumAllocations; i++)
        {
            desc.pAllocationInfo[i].hAllocation = allocs32[i].hAllocation;
            desc.pAllocationInfo[i].pSystemMem = ULongToPtr(allocs32[i].pSystemMem);
            desc.pAllocationInfo[i].pPrivateDriverData = ULongToPtr(allocs32[i].pPrivateDriverData);
            desc.pAllocationInfo[i].PrivateDriverDataSize = allocs32[i].PrivateDriverDataSize;
            desc.pAllocationInfo[i].VidPnSourceId = allocs32[i].VidPnSourceId;
            desc.pAllocationInfo[i].Flags.Value = allocs32[i].Flags;
        }
    }

    status = NtGdiDdDDICreateAllocation(&desc);
    desc32->hResource = desc.hResource;
    desc32->hGlobalShare = desc.hGlobalShare;
    for (i = 0; desc.pAllocationInfo && i < desc32->NumAllocations; i++)
        allocs32[i].hAllocation = desc.pAllocationInfo[i].hAllocation;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateAllocation2(UINT *args)
{
    D3DKMT_CREATEALLOCATION32 *desc32 = get_ptr(&args);
    D3DKMT_CREATESTANDARDALLOCATION standard;
    D3DDDI_ALLOCATIONINFO2_32 *allocs32;
    D3DKMT_CREATEALLOCATION desc;
    NTSTATUS status;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    create_allocation_32to64(desc32, &desc, &standard);
    allocs32 = ULongToPtr(desc32->pAllocationInfo);
    if (allocs32 && desc32->NumAllocations)
    {
        desc.pAllocationInfo2 = Wow64AllocateTemp((SIZE_T)desc32->NumAllocations * sizeof(*desc.pAllocationInfo2));
        if (!desc.pAllocationInfo2) return STATUS_NO_MEMORY;
        memset(desc.pAllocationInfo2, 0, (SIZE_T)desc32->NumAllocations * sizeof(*desc.pAllocationInfo2));
        for (i = 0; i < desc32->NumAllocations; i++)
        {
            desc.pAllocationInfo2[i].hAllocation = allocs32[i].hAllocation;
            desc.pAllocationInfo2[i].pSystemMem = ULongToPtr(allocs32[i].pSystemMem);
            desc.pAllocationInfo2[i].pPrivateDriverData = ULongToPtr(allocs32[i].pPrivateDriverData);
            desc.pAllocationInfo2[i].PrivateDriverDataSize = allocs32[i].PrivateDriverDataSize;
            desc.pAllocationInfo2[i].VidPnSourceId = allocs32[i].VidPnSourceId;
            desc.pAllocationInfo2[i].Flags.Value = allocs32[i].Flags;
            desc.pAllocationInfo2[i].GpuVirtualAddress = allocs32[i].GpuVirtualAddress;
            desc.pAllocationInfo2[i].Priority = allocs32[i].Priority;
        }
    }

    status = NtGdiDdDDICreateAllocation2(&desc);
    desc32->hResource = desc.hResource;
    desc32->hGlobalShare = desc.hGlobalShare;
    for (i = 0; desc.pAllocationInfo2 && i < desc32->NumAllocations; i++)
    {
        allocs32[i].hAllocation = desc.pAllocationInfo2[i].hAllocation;
        allocs32[i].GpuVirtualAddress = desc.pAllocationInfo2[i].GpuVirtualAddress;
    }
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIOpenResource(UINT *args)
{
    D3DKMT_OPENRESOURCE32 *desc32 = get_ptr(&args);
    D3DDDI_OPENALLOCATIONINFO32 *allocs32;
    D3DKMT_OPENRESOURCE desc;
    NTSTATUS status;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    open_resource_32to64(desc32, &desc);
    allocs32 = ULongToPtr(desc32->pOpenAllocationInfo);
    if (allocs32 && desc32->NumAllocations)
    {
        desc.pOpenAllocationInfo = Wow64AllocateTemp((SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo));
        if (!desc.pOpenAllocationInfo) return STATUS_NO_MEMORY;
        memset(desc.pOpenAllocationInfo, 0, (SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo));
        for (i = 0; i < desc32->NumAllocations; i++)
        {
            desc.pOpenAllocationInfo[i].hAllocation = allocs32[i].hAllocation;
            desc.pOpenAllocationInfo[i].pPrivateDriverData = ULongToPtr(allocs32[i].pPrivateDriverData);
            desc.pOpenAllocationInfo[i].PrivateDriverDataSize = allocs32[i].PrivateDriverDataSize;
        }
    }

    status = NtGdiDdDDIOpenResource(&desc);
    desc32->TotalPrivateDriverDataBufferSize = desc.TotalPrivateDriverDataBufferSize;
    desc32->hResource = desc.hResource;
    for (i = 0; desc.pOpenAllocationInfo && i < desc32->NumAllocations; i++)
    {
        allocs32[i].hAllocation = desc.pOpenAllocationInfo[i].hAllocation;
        allocs32[i].pPrivateDriverData = PtrToUlong(desc.pOpenAllocationInfo[i].pPrivateDriverData);
        allocs32[i].PrivateDriverDataSize = desc.pOpenAllocationInfo[i].PrivateDriverDataSize;
    }
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIOpenResource2(UINT *args)
{
    D3DKMT_OPENRESOURCE32 *desc32 = get_ptr(&args);
    D3DDDI_OPENALLOCATIONINFO2_32 *allocs32;
    D3DKMT_OPENRESOURCE desc;
    NTSTATUS status;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    open_resource_32to64(desc32, &desc);
    allocs32 = ULongToPtr(desc32->pOpenAllocationInfo);
    if (allocs32 && desc32->NumAllocations)
    {
        desc.pOpenAllocationInfo2 = Wow64AllocateTemp((SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo2));
        if (!desc.pOpenAllocationInfo2) return STATUS_NO_MEMORY;
        memset(desc.pOpenAllocationInfo2, 0, (SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo2));
        for (i = 0; i < desc32->NumAllocations; i++)
        {
            desc.pOpenAllocationInfo2[i].hAllocation = allocs32[i].hAllocation;
            desc.pOpenAllocationInfo2[i].pPrivateDriverData = ULongToPtr(allocs32[i].pPrivateDriverData);
            desc.pOpenAllocationInfo2[i].PrivateDriverDataSize = allocs32[i].PrivateDriverDataSize;
            desc.pOpenAllocationInfo2[i].GpuVirtualAddress = allocs32[i].GpuVirtualAddress;
        }
    }

    status = NtGdiDdDDIOpenResource2(&desc);
    desc32->TotalPrivateDriverDataBufferSize = desc.TotalPrivateDriverDataBufferSize;
    desc32->hResource = desc.hResource;
    for (i = 0; desc.pOpenAllocationInfo2 && i < desc32->NumAllocations; i++)
    {
        allocs32[i].hAllocation = desc.pOpenAllocationInfo2[i].hAllocation;
        allocs32[i].pPrivateDriverData = PtrToUlong(desc.pOpenAllocationInfo2[i].pPrivateDriverData);
        allocs32[i].PrivateDriverDataSize = desc.pOpenAllocationInfo2[i].PrivateDriverDataSize;
        allocs32[i].GpuVirtualAddress = desc.pOpenAllocationInfo2[i].GpuVirtualAddress;
    }
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIOpenResourceFromNtHandle(UINT *args)
{
    D3DKMT_OPENRESOURCEFROMNTHANDLE32 *desc32 = get_ptr(&args);
    D3DDDI_OPENALLOCATIONINFO2_32 *allocs32;
    D3DKMT_OPENRESOURCEFROMNTHANDLE desc;
    NTSTATUS status;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hNtHandle = UlongToHandle(desc32->hNtHandle);
    desc.NumAllocations = desc32->NumAllocations;
    desc.PrivateRuntimeDataSize = desc32->PrivateRuntimeDataSize;
    desc.pPrivateRuntimeData = ULongToPtr(desc32->pPrivateRuntimeData);
    desc.ResourcePrivateDriverDataSize = desc32->ResourcePrivateDriverDataSize;
    desc.pResourcePrivateDriverData = ULongToPtr(desc32->pResourcePrivateDriverData);
    desc.TotalPrivateDriverDataBufferSize = desc32->TotalPrivateDriverDataBufferSize;
    desc.pTotalPrivateDriverDataBuffer = ULongToPtr(desc32->pTotalPrivateDriverDataBuffer);
    desc.hResource = desc32->hResource;
    desc.hKeyedMutex = desc32->hKeyedMutex;
    desc.pKeyedMutexPrivateRuntimeData = ULongToPtr(desc32->pKeyedMutexPrivateRuntimeData);
    desc.KeyedMutexPrivateRuntimeDataSize = desc32->KeyedMutexPrivateRuntimeDataSize;
    desc.hSyncObject = desc32->hSyncObject;
    allocs32 = ULongToPtr(desc32->pOpenAllocationInfo2);
    if (allocs32 && desc32->NumAllocations)
    {
        desc.pOpenAllocationInfo2 = Wow64AllocateTemp((SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo2));
        if (!desc.pOpenAllocationInfo2) return STATUS_NO_MEMORY;
        memset(desc.pOpenAllocationInfo2, 0, (SIZE_T)desc32->NumAllocations * sizeof(*desc.pOpenAllocationInfo2));
        for (i = 0; i < desc32->NumAllocations; i++)
        {
            desc.pOpenAllocationInfo2[i].hAllocation = allocs32[i].hAllocation;
            desc.pOpenAllocationInfo2[i].pPrivateDriverData = ULongToPtr(allocs32[i].pPrivateDriverData);
            desc.pOpenAllocationInfo2[i].PrivateDriverDataSize = allocs32[i].PrivateDriverDataSize;
            desc.pOpenAllocationInfo2[i].GpuVirtualAddress = allocs32[i].GpuVirtualAddress;
        }
    }

    status = NtGdiDdDDIOpenResourceFromNtHandle(&desc);
    desc32->PrivateRuntimeDataSize = desc.PrivateRuntimeDataSize;
    desc32->ResourcePrivateDriverDataSize = desc.ResourcePrivateDriverDataSize;
    desc32->TotalPrivateDriverDataBufferSize = desc.TotalPrivateDriverDataBufferSize;
    desc32->hResource = desc.hResource;
    desc32->hKeyedMutex = desc.hKeyedMutex;
    desc32->hSyncObject = desc.hSyncObject;
    for (i = 0; desc.pOpenAllocationInfo2 && i < desc32->NumAllocations; i++)
    {
        allocs32[i].hAllocation = desc.pOpenAllocationInfo2[i].hAllocation;
        allocs32[i].pPrivateDriverData = PtrToUlong(desc.pOpenAllocationInfo2[i].pPrivateDriverData);
        allocs32[i].PrivateDriverDataSize = desc.pOpenAllocationInfo2[i].PrivateDriverDataSize;
        allocs32[i].GpuVirtualAddress = desc.pOpenAllocationInfo2[i].GpuVirtualAddress;
    }
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDICheckExclusiveOwnership(UINT *args)
{
    return NtGdiDdDDICheckExclusiveOwnership();
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetProcessSchedulingPriorityClass(UINT *args)
{
    HANDLE process = get_handle(&args);
    D3DKMT_SCHEDULINGPRIORITYCLASS *priority = get_ptr(&args);

    return NtGdiDdDDIGetProcessSchedulingPriorityClass(process, priority);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetProcessSchedulingPriorityClass(UINT *args)
{
    HANDLE process = get_handle(&args);
    D3DKMT_SCHEDULINGPRIORITYCLASS priority = get_ulong(&args);

    return NtGdiDdDDISetProcessSchedulingPriorityClass(process, priority);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIReleaseProcessVidPnSourceOwners(UINT *args)
{
    HANDLE process = get_handle(&args);

    return NtGdiDdDDIReleaseProcessVidPnSourceOwners(process);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateContextVirtual(UINT *args)
{
    D3DKMT_CREATECONTEXTVIRTUAL32 *desc32 = get_ptr(&args);
    D3DKMT_CREATECONTEXTVIRTUAL desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.NodeOrdinal = desc32->NodeOrdinal;
    desc.EngineAffinity = desc32->EngineAffinity;
    desc.Flags = desc32->Flags;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.ClientHint = desc32->ClientHint;

    status = NtGdiDdDDICreateContextVirtual(&desc);
    desc32->hContext = desc.hContext;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDILock2(UINT *args)
{
    D3DKMT_LOCK2_32 *desc32 = get_ptr(&args);
    D3DKMT_LOCK2 desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hAllocation = desc32->hAllocation;
    desc.Flags = desc32->Flags;

    status = NtGdiDdDDILock2(&desc);
    desc32->pData = PtrToUlong(desc.pData);
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIMakeResident(UINT *args)
{
    D3DDDI_MAKERESIDENT32 *desc32 = get_ptr(&args);
    D3DDDI_MAKERESIDENT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hPagingQueue = desc32->hPagingQueue;
    desc.NumAllocations = desc32->NumAllocations;
    desc.AllocationList = ULongToPtr(desc32->AllocationList);
    desc.PriorityList = ULongToPtr(desc32->PriorityList);
    desc.Flags = desc32->Flags;
    desc.PagingFenceValue = desc32->PagingFenceValue;
    desc.NumBytesToTrim = desc32->NumBytesToTrim;

    status = NtGdiDdDDIMakeResident(&desc);
    desc32->PagingFenceValue = desc.PagingFenceValue;
    desc32->NumBytesToTrim = desc.NumBytesToTrim;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIEvict(UINT *args)
{
    D3DKMT_EVICT32 *desc32 = get_ptr(&args);
    D3DKMT_EVICT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.NumAllocations = desc32->NumAllocations;
    desc.AllocationList = ULongToPtr(desc32->AllocationList);
    desc.Flags = desc32->Flags;
    desc.NumBytesToTrim = desc32->NumBytesToTrim;

    status = NtGdiDdDDIEvict(&desc);
    desc32->NumBytesToTrim = desc.NumBytesToTrim;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUpdateGpuVirtualAddress(UINT *args)
{
    const D3DKMT_UPDATEGPUVIRTUALADDRESS32 *desc32 = get_ptr(&args);
    D3DKMT_UPDATEGPUVIRTUALADDRESS desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hContext = desc32->hContext;
    desc.hFenceObject = desc32->hFenceObject;
    desc.NumOperations = desc32->NumOperations;
    desc.Operations = ULongToPtr(desc32->Operations);
    desc.Reserved0 = desc32->Reserved0;
    desc.Reserved1 = desc32->Reserved1;
    desc.FenceValue = desc32->FenceValue;
    desc.Flags.Value = desc32->Flags;
    return NtGdiDdDDIUpdateGpuVirtualAddress(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitCommand(UINT *args)
{
    const D3DKMT_SUBMITCOMMAND32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITCOMMAND desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.Commands = desc32->Commands;
    desc.CommandLength = desc32->CommandLength;
    desc.Flags = desc32->Flags;
    desc.PresentHistoryToken = desc32->PresentHistoryToken;
    desc.BroadcastContextCount = desc32->BroadcastContextCount;
    memcpy(desc.BroadcastContext, desc32->BroadcastContext, sizeof(desc.BroadcastContext));
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.NumPrimaries = desc32->NumPrimaries;
    memcpy(desc.WrittenPrimaries, desc32->WrittenPrimaries, sizeof(desc.WrittenPrimaries));
    desc.NumHistoryBuffers = desc32->NumHistoryBuffers;
    desc.HistoryBufferArray = ULongToPtr(desc32->HistoryBufferArray);
    return NtGdiDdDDISubmitCommand(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIRender(UINT *args)
{
    D3DKMT_RENDER32 *desc32 = get_ptr(&args);
    D3DKMT_RENDER desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hContext = desc32->hContext;
    desc.CommandOffset = desc32->CommandOffset;
    desc.CommandLength = desc32->CommandLength;
    desc.AllocationCount = desc32->AllocationCount;
    desc.PatchLocationCount = desc32->PatchLocationCount;
    desc.NewCommandBufferSize = desc32->NewCommandBufferSize;
    desc.NewAllocationListSize = desc32->NewAllocationListSize;
    desc.NewPatchLocationListSize = desc32->NewPatchLocationListSize;
    desc.Flags = desc32->Flags;
    desc.PresentHistoryToken = desc32->PresentHistoryToken;
    desc.BroadcastContextCount = desc32->BroadcastContextCount;
    memcpy(desc.BroadcastContext, desc32->BroadcastContext, sizeof(desc.BroadcastContext));
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;

    status = NtGdiDdDDIRender(&desc);
    desc32->pNewCommandBuffer = PtrToUlong(desc.pNewCommandBuffer);
    desc32->NewCommandBufferSize = desc.NewCommandBufferSize;
    desc32->pNewAllocationList = PtrToUlong(desc.pNewAllocationList);
    desc32->NewAllocationListSize = desc.NewAllocationListSize;
    desc32->pNewPatchLocationList = PtrToUlong(desc.pNewPatchLocationList);
    desc32->NewPatchLocationListSize = desc.NewPatchLocationListSize;
    desc32->QueuedBufferCount = desc.QueuedBufferCount;
    desc32->NewCommandBuffer = desc.NewCommandBuffer;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIQueryAllocationResidency(UINT *args)
{
    const D3DKMT_QUERYALLOCATIONRESIDENCY32 *desc32 = get_ptr(&args);
    D3DKMT_QUERYALLOCATIONRESIDENCY desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hResource = desc32->hResource;
    desc.phAllocationList = ULongToPtr(desc32->phAllocationList);
    desc.AllocationCount = desc32->AllocationCount;
    desc.pResidencyStatus = ULongToPtr(desc32->pResidencyStatus);
    return NtGdiDdDDIQueryAllocationResidency(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetAllocationPriority(UINT *args)
{
    const D3DKMT_ALLOCATIONPRIORITY32 *desc32 = get_ptr(&args);
    D3DKMT_SETALLOCATIONPRIORITY desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hResource = desc32->hResource;
    desc.phAllocationList = ULongToPtr(desc32->phAllocationList);
    desc.AllocationCount = desc32->AllocationCount;
    desc.pPriorities = ULongToPtr(desc32->pPriorities);
    return NtGdiDdDDISetAllocationPriority(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetAllocationPriority(UINT *args)
{
    const D3DKMT_ALLOCATIONPRIORITY32 *desc32 = get_ptr(&args);
    D3DKMT_GETALLOCATIONPRIORITY desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hResource = desc32->hResource;
    desc.phAllocationList = ULongToPtr(desc32->phAllocationList);
    desc.AllocationCount = desc32->AllocationCount;
    desc.pPriorities = ULongToPtr(desc32->pPriorities);
    return NtGdiDdDDIGetAllocationPriority(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIOfferAllocations(UINT *args)
{
    const D3DKMT_OFFERALLOCATIONS32 *desc32 = get_ptr(&args);
    D3DKMT_OFFERALLOCATIONS desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.pResources = ULongToPtr(desc32->pResources);
    desc.HandleList = ULongToPtr(desc32->HandleList);
    desc.NumAllocations = desc32->NumAllocations;
    desc.Priority = desc32->Priority;
    desc.Flags = desc32->Flags;
    return NtGdiDdDDIOfferAllocations(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIReclaimAllocations(UINT *args)
{
    D3DKMT_RECLAIMALLOCATIONS32 *desc32 = get_ptr(&args);
    D3DKMT_RECLAIMALLOCATIONS desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.pResources = ULongToPtr(desc32->pResources);
    desc.HandleList = ULongToPtr(desc32->HandleList);
    desc.pDiscarded = ULongToPtr(desc32->pDiscarded);
    desc.NumAllocations = desc32->NumAllocations;
    return NtGdiDdDDIReclaimAllocations(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIReclaimAllocations2(UINT *args)
{
    D3DKMT_RECLAIMALLOCATIONS2_32 *desc32 = get_ptr(&args);
    D3DKMT_RECLAIMALLOCATIONS2 desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hPagingQueue = desc32->hPagingQueue;
    desc.NumAllocations = desc32->NumAllocations;
    desc.pResources = ULongToPtr(desc32->pResources);
    desc.HandleList = ULongToPtr(desc32->HandleList);
    desc.pResults = ULongToPtr(desc32->pResults);

    status = NtGdiDdDDIReclaimAllocations2(&desc);
    desc32->PagingFenceValue = desc.PagingFenceValue;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetResourcePresentPrivateDriverData(UINT *args)
{
    D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA32 *desc32 = get_ptr(&args);
    D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hResource = desc32->hResource;
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);

    status = NtGdiDdDDIGetResourcePresentPrivateDriverData(&desc);
    desc32->PrivateDriverDataSize = desc.PrivateDriverDataSize;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIRegisterTrimNotification(UINT *args)
{
    D3DKMT_REGISTERTRIMNOTIFICATION32 *desc32 = get_ptr(&args);
    D3DKMT_UNREGISTERTRIMNOTIFICATION unregister;
    D3DKMT_REGISTERTRIMNOTIFICATION desc;
    struct trim_notification *entries;
    NTSTATUS status;
    ULONG i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.AdapterLuid = desc32->AdapterLuid;
    desc.hDevice = desc32->hDevice;
    desc.Callback = ULongToPtr(desc32->Callback);
    desc.Context = ULongToPtr(desc32->Context);

    status = NtGdiDdDDIRegisterTrimNotification(&desc);
    if (status) return status;

    RtlAcquireSRWLockExclusive(&trim_notification_lock);
    for (i = 0; i < trim_notification_count; i++)
    {
        if (!trim_notifications[i].handle) break;
    }
    if (i == trim_notification_count)
    {
        if (trim_notifications)
        {
            entries = RtlReAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, trim_notifications,
                                        (trim_notification_count + 8) * sizeof(*entries));
        }
        else
        {
            entries = RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, 8 * sizeof(*entries));
        }
        if (!entries)
        {
            RtlReleaseSRWLockExclusive(&trim_notification_lock);
            unregister.Handle = desc.Handle;
            unregister.Callback = desc.Callback;
            NtGdiDdDDIUnregisterTrimNotification(&unregister);
            return STATUS_NO_MEMORY;
        }
        trim_notifications = entries;
        trim_notification_count += 8;
    }
    trim_notifications[i].handle = desc.Handle;
    trim_notifications[i].callback = desc32->Callback;
    RtlReleaseSRWLockExclusive(&trim_notification_lock);

    desc32->Handle = i + 1;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUnregisterTrimNotification(UINT *args)
{
    D3DKMT_UNREGISTERTRIMNOTIFICATION32 *desc32 = get_ptr(&args);
    D3DKMT_UNREGISTERTRIMNOTIFICATION desc;
    NTSTATUS status;
    ULONG i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.Handle = NULL;
    desc.Callback = ULongToPtr(desc32->Callback);

    RtlAcquireSRWLockExclusive(&trim_notification_lock);
    if (desc32->Handle)
    {
        if (desc32->Handle > trim_notification_count ||
            !trim_notifications[desc32->Handle - 1].handle)
        {
            RtlReleaseSRWLockExclusive(&trim_notification_lock);
            return STATUS_INVALID_PARAMETER;
        }
        desc.Handle = trim_notifications[desc32->Handle - 1].handle;
    }

    status = NtGdiDdDDIUnregisterTrimNotification(&desc);
    if (!status)
    {
        for (i = 0; i < trim_notification_count; i++)
        {
            if (desc32->Handle ? i == desc32->Handle - 1 :
                trim_notifications[i].callback == desc32->Callback)
            {
                trim_notifications[i].handle = NULL;
                trim_notifications[i].callback = 0;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&trim_notification_lock);
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDISignalSynchronizationObject2(UINT *args)
{
    const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2_32 *desc32 = get_ptr(&args);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memcpy(&desc, desc32, sizeof(desc));
    if (desc32->Flags.EnqueueCpuEvent)
        desc.CpuEventHandle = UlongToHandle(desc32->CpuEventHandle);
    return NtGdiDdDDISignalSynchronizationObject2(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISignalSynchronizationObjectFromGpu(UINT *args)
{
    const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU32 *desc32 = get_ptr(&args);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hContext = desc32->hContext;
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.MonitoredFenceValueArray = ULongToPtr(desc32->MonitoredFenceValueArray);
    return NtGdiDdDDISignalSynchronizationObjectFromGpu(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISignalSynchronizationObjectFromGpu2(UINT *args)
{
    const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2_32 *desc32 = get_ptr(&args);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.Flags = desc32->Flags;
    desc.BroadcastContextCount = desc32->BroadcastContextCount;
    desc.BroadcastContextArray = ULongToPtr(desc32->BroadcastContextArray);
    if (desc32->Flags.EnqueueCpuEvent)
        desc.CpuEventHandle = UlongToHandle(desc32->CpuEventHandle);
    else
        desc.MonitoredFenceValueArray = ULongToPtr(desc32->MonitoredFenceValueArray);
    return NtGdiDdDDISignalSynchronizationObjectFromGpu2(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateHwContext(UINT *args)
{
    D3DKMT_CREATEHWCONTEXT32 *desc32 = get_ptr(&args);
    D3DKMT_CREATEHWCONTEXT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.NodeOrdinal = desc32->NodeOrdinal;
    desc.EngineAffinity = desc32->EngineAffinity;
    desc.Flags = desc32->Flags;
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);

    status = NtGdiDdDDICreateHwContext(&desc);
    desc32->hHwContext = desc.hHwContext;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateHwQueue(UINT *args)
{
    D3DKMT_CREATEHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_CREATEHWQUEUE desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hHwContext = desc32->hHwContext;
    desc.Flags = desc32->Flags;
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);

    status = NtGdiDdDDICreateHwQueue(&desc);
    desc32->hHwQueue = desc.hHwQueue;
    desc32->hHwQueueProgressFence = desc.hHwQueueProgressFence;
    desc32->HwQueueProgressFenceCPUVirtualAddress = PtrToUlong(desc.HwQueueProgressFenceCPUVirtualAddress);
    desc32->HwQueueProgressFenceGPUVirtualAddress = desc.HwQueueProgressFenceGPUVirtualAddress;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitCommandToHwQueue(UINT *args)
{
    const D3DKMT_SUBMITCOMMANDTOHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITCOMMANDTOHWQUEUE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hHwQueue = desc32->hHwQueue;
    desc.HwQueueProgressFenceId = desc32->HwQueueProgressFenceId;
    desc.CommandBuffer = desc32->CommandBuffer;
    desc.CommandLength = desc32->CommandLength;
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.NumPrimaries = desc32->NumPrimaries;
    desc.WrittenPrimaries = ULongToPtr(desc32->WrittenPrimaries);
    return NtGdiDdDDISubmitCommandToHwQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue(UINT *args)
{
    const D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hHwQueue = desc32->hHwQueue;
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.FenceValueArray = ULongToPtr(desc32->FenceValueArray);
    return NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitSignalSyncObjectsToHwQueue(UINT *args)
{
    const D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.Flags = desc32->Flags;
    desc.BroadcastHwQueueCount = desc32->BroadcastHwQueueCount;
    desc.BroadcastHwQueueArray = ULongToPtr(desc32->BroadcastHwQueueArray);
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.FenceValueArray = ULongToPtr(desc32->FenceValueArray);
    return NtGdiDdDDISubmitSignalSyncObjectsToHwQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitPresentToHwQueue(UINT *args)
{
    D3DKMT_SUBMITPRESENTTOHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITPRESENTTOHWQUEUE desc;
    D3DKMT_PRESENT_RGNS regions;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hHwQueues = ULongToPtr(desc32->hHwQueues);
    status = present_32to64(&desc32->PrivatePresentData, &desc.PrivatePresentData, &regions);
    if (status) return status;

    status = NtGdiDdDDISubmitPresentToHwQueue(&desc);
    desc32->PrivatePresentData.bOptimizeForComposition = desc.PrivatePresentData.bOptimizeForComposition;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDISubmitPresentBltToHwQueue(UINT *args)
{
    const D3DKMT_SUBMITPRESENTBLTTOHWQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_SUBMITPRESENTBLTTOHWQUEUE desc;
    D3DKMT_PRESENT_RGNS regions;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hHwQueue = desc32->hHwQueue;
    desc.HwQueueProgressFenceId = desc32->HwQueueProgressFenceId;
    status = present_32to64(&desc32->PrivatePresentData, &desc.PrivatePresentData, &regions);
    if (status) return status;
    return NtGdiDdDDISubmitPresentBltToHwQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreateOverlay(UINT *args)
{
    D3DKMT_CREATEOVERLAY32 *desc32 = get_ptr(&args);
    D3DKMT_CREATEOVERLAY desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.hDevice = desc32->hDevice;
    overlay_info_32to64(&desc32->OverlayInfo, &desc.OverlayInfo);

    status = NtGdiDdDDICreateOverlay(&desc);
    desc32->hOverlay = desc.hOverlay;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUpdateOverlay(UINT *args)
{
    const D3DKMT_UPDATEOVERLAY32 *desc32 = get_ptr(&args);
    D3DKMT_UPDATEOVERLAY desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hOverlay = desc32->hOverlay;
    overlay_info_32to64(&desc32->OverlayInfo, &desc.OverlayInfo);
    return NtGdiDdDDIUpdateOverlay(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIFlipOverlay(UINT *args)
{
    const D3DKMT_FLIPOVERLAY32 *desc32 = get_ptr(&args);
    D3DKMT_FLIPOVERLAY desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hOverlay = desc32->hOverlay;
    desc.hSource = desc32->hSource;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    return NtGdiDdDDIFlipOverlay(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetDisplayModeList(UINT *args)
{
    D3DKMT_GETDISPLAYMODELIST32 *desc32 = get_ptr(&args);
    D3DKMT_GETDISPLAYMODELIST desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hAdapter = desc32->hAdapter;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.pModeList = ULongToPtr(desc32->pModeList);
    desc.ModeCount = desc32->ModeCount;

    status = NtGdiDdDDIGetDisplayModeList(&desc);
    desc32->ModeCount = desc.ModeCount;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetMultisampleMethodList(UINT *args)
{
    D3DKMT_GETMULTISAMPLEMETHODLIST32 *desc32 = get_ptr(&args);
    D3DKMT_GETMULTISAMPLEMETHODLIST desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hAdapter = desc32->hAdapter;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.Width = desc32->Width;
    desc.Height = desc32->Height;
    desc.Format = desc32->Format;
    desc.pMethodList = ULongToPtr(desc32->pMethodList);
    desc.MethodCount = desc32->MethodCount;

    status = NtGdiDdDDIGetMultisampleMethodList(&desc);
    desc32->MethodCount = desc.MethodCount;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetRuntimeData(UINT *args)
{
    const D3DKMT_GETRUNTIMEDATA32 *desc32 = get_ptr(&args);
    D3DKMT_GETRUNTIMEDATA desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hAdapter = desc32->hAdapter;
    desc.hGlobalShare = desc32->hGlobalShare;
    desc.pRuntimeData = ULongToPtr(desc32->pRuntimeData);
    desc.RuntimeDataSize = desc32->RuntimeDataSize;
    return NtGdiDdDDIGetRuntimeData(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDISetGammaRamp(UINT *args)
{
    const D3DKMT_SETGAMMARAMP32 *desc32 = get_ptr(&args);
    D3DKMT_SETGAMMARAMP desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.Type = desc32->Type;
    desc.pGammaRampRgb256x3x16 = ULongToPtr(desc32->pGammaRamp);
    desc.Size = desc32->Size;
    return NtGdiDdDDISetGammaRamp(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForVerticalBlankEvent2(UINT *args)
{
    const D3DKMT_WAITFORVERTICALBLANKEVENT2_32 *desc32 = get_ptr(&args);
    D3DKMT_WAITFORVERTICALBLANKEVENT2 desc;
    UINT i;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hAdapter = desc32->hAdapter;
    desc.hDevice = desc32->hDevice;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.NumObjects = desc32->NumObjects;
    for (i = 0; i < D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS; i++)
        desc.ObjectHandleArray[i] = UlongToHandle(desc32->ObjectHandleArray[i]);
    return NtGdiDdDDIWaitForVerticalBlankEvent2(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetSharedResourceAdapterLuid(UINT *args)
{
    D3DKMT_GETSHAREDRESOURCEADAPTERLUID32 *desc32 = get_ptr(&args);
    D3DKMT_GETSHAREDRESOURCEADAPTERLUID desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hGlobalShare = desc32->hGlobalShare;
    desc.hNtHandle = UlongToHandle(desc32->hNtHandle);

    status = NtGdiDdDDIGetSharedResourceAdapterLuid(&desc);
    desc32->AdapterLuid = desc.AdapterLuid;
    return status;
}
