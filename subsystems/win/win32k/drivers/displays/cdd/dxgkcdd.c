/*
 * PROJECT:     LiberNT Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display-only scan-out through a graphics kernel's display driver interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include "cdd.h"
#include <d3dkmthk.h>

#include <debug.h>

NTKERNELAPI
PCHAR
NTAPI
PsGetProcessImageFileName(
   _In_ PEPROCESS Process);

typedef struct _RCDD_DXGK_CALL_CONTEXT
{
   KAPC_STATE ApcState;
   BOOLEAN Attached;
} RCDD_DXGK_CALL_CONTEXT;

/* The display driver interface serves the session's CSRSS; the calls are made in its address space */
static BOOL
RcddDxgkEnterDisplayProcess(
   RCDD_DXGK_CALL_CONTEXT *Call)
{
   PEPROCESS Process = RosWddmHandOverDisplayProcess();

   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   Call->Attached = FALSE;
   if (Process == NULL)
      return FALSE;
   if (Process != PsGetCurrentProcess())
   {
      KeStackAttachProcess((PRKPROCESS)Process, &Call->ApcState);
      Call->Attached = TRUE;
   }
   return TRUE;
}

static VOID
RcddDxgkLeaveDisplayProcess(
   RCDD_DXGK_CALL_CONTEXT *Call)
{
   if (Call->Attached)
      KeUnstackDetachProcess(&Call->ApcState);
   Call->Attached = FALSE;
}

typedef struct _RCDD_DXGK_THREAD_CALL
{
   VOID (*Routine)(PVOID);
   PVOID Parameter;
} RCDD_DXGK_THREAD_CALL;

static VOID NTAPI
RcddDxgkThreadEntry(
   PVOID Parameter)
{
   RCDD_DXGK_THREAD_CALL *Call = Parameter;

   Call->Routine(Call->Parameter);
   PsTerminateSystemThread(STATUS_SUCCESS);
}

/* The display driver interface binds a source to the CSRSS thread that enables it; requests run on a CSRSS system thread */
static NTSTATUS
RcddDxgkRunInDisplayProcess(
   VOID (*Routine)(PVOID),
   PVOID Parameter)
{
   PEPROCESS Process = RosWddmHandOverDisplayProcess();
   RCDD_DXGK_THREAD_CALL Call;
   HANDLE ProcessHandle, ThreadHandle;
   PVOID Thread;
   NTSTATUS Status;

   PAGED_CODE();
   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   ASSERT(Routine != NULL);
   if (Process == NULL)
      return STATUS_NOT_FOUND;
   Status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode,
                                  &ProcessHandle);
   if (!NT_SUCCESS(Status))
      return Status;
   Call.Routine = Routine;
   Call.Parameter = Parameter;
   Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, ProcessHandle, NULL, RcddDxgkThreadEntry, &Call);
   ZwClose(ProcessHandle);
   if (!NT_SUCCESS(Status))
      return Status;
   Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &Thread, NULL);
   ZwClose(ThreadHandle);
   if (!NT_SUCCESS(Status))
      return Status;
   KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
   ObDereferenceObject(Thread);
   return STATUS_SUCCESS;
}

#define RCDD_DXGK_INTERFACE_IOCTL CTL_CODE(FILE_DEVICE_VIDEO, 0x816, METHOD_NEITHER, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define RCDD_DXGK_INTERFACE_SIZE 0x210
#define RCDD_DXGK_INTERFACE_VERSION 9
#define RCDD_DXGK_FUNCTIONS 63
#define RCDD_DXGK_CREATE 3
#define RCDD_DXGK_ENABLE 5
#define RCDD_DXGK_GET_DISPLAY_MODE_LIST 10
#define RCDD_DXGK_GET_DRIVER_CAPS 31
#define RCDD_DXGK_PRESENT_ON_SCREEN 43
#define RCDD_DXGK_MAX_MODES 256
#define RCDD_DXGK_CAPS_SIZE 0x400
#define RCDD_DXGK_GDI_DEVICE_ADAPTER 0xB0

typedef struct _RCDD_DXGK_INTERFACE
{
   USHORT Size;
   USHORT Version;
   ULONG Reserved;
   PVOID Context;
   ULONG_PTR Reserved2;
   PVOID Functions[RCDD_DXGK_FUNCTIONS];
} RCDD_DXGK_INTERFACE, *PRCDD_DXGK_INTERFACE;

C_ASSERT(FIELD_OFFSET(RCDD_DXGK_INTERFACE, Functions) == 0x18 || sizeof(PVOID) != 8);
C_ASSERT(sizeof(RCDD_DXGK_INTERFACE) == RCDD_DXGK_INTERFACE_SIZE || sizeof(PVOID) != 8);

typedef struct _RCDD_DXGK_DRIVERINFO
{
   ULONG VendorId;
   ULONG DeviceId;
   ULONG Reserved[3];
   ULONG DriverVersion;
   ULONG Tail[2];
} RCDD_DXGK_DRIVERINFO;

typedef struct _RCDD_CDD_INTERFACE
{
   PVOID Context;
   PVOID AddD3DDirtyRgn;
   PVOID NotifyExclusiveGdiOwner;
   PVOID UpdatePresentRects;
   ULONG_PTR Reserved;
   CHAR ProcessName[15];
   UCHAR Reserved2;
   ULONG Reserved3;
   UCHAR Tail[0x40];
} RCDD_CDD_INTERFACE;

C_ASSERT(FIELD_OFFSET(RCDD_CDD_INTERFACE, ProcessName) == 0x28 || sizeof(PVOID) != 8);

typedef struct _RCDD_PRESENT_ON_SCREEN
{
   ULONG Unknown0;
   PVOID Context;
   ULONG_PTR Reserved;
   PVOID Source;
   ULONG Width;
   ULONG Height;
   ULONG Pitch;
   UCHAR Reserved2[0x48 - 0x2C];
   ULONG Unknown48;
   ULONG Unknown4C;
   const RECT *DirtyRect;
   ULONG Unknown58;
   UCHAR Tail[0x24];
} RCDD_PRESENT_ON_SCREEN;

C_ASSERT(FIELD_OFFSET(RCDD_PRESENT_ON_SCREEN, Source) == 0x18 || sizeof(PVOID) != 8);
C_ASSERT(FIELD_OFFSET(RCDD_PRESENT_ON_SCREEN, DirtyRect) == 0x50 || sizeof(PVOID) != 8);

typedef LONG (__cdecl *PFN_RCDD_DXGK_CREATE)(PVOID, PVOID, const VOID *, PUINT, PUINT, PUINT,
                                             RCDD_DXGK_DRIVERINFO *, RCDD_DXGK_DRIVERINFO *, LUID *, PVOID *);
typedef LONG (__cdecl *PFN_RCDD_DXGK_ENABLE)(UINT, UINT, const RCDD_CDD_INTERFACE *, UINT, const D3DKMT_DISPLAYMODE *,
                                             PVOID *, ULONG_PTR *, PUINT);
typedef LONG (__cdecl *PFN_RCDD_DXGK_GET_DISPLAY_MODE_LIST)(PVOID, D3DKMT_GETDISPLAYMODELIST *);
typedef LONG (__cdecl *PFN_RCDD_DXGK_PRESENT_ON_SCREEN)(RCDD_PRESENT_ON_SCREEN *);
typedef LONG (__cdecl *PFN_RCDD_DXGK_GET_DRIVER_CAPS)(PVOID, PVOID, PVOID *, PULONGLONG, PVOID);

typedef struct _RCDD_DXGK_ADAPTER
{
   HANDLE hDriver;
   PVOID Context;
   PVOID Second;
   UINT hAdapter;
   ULONG ModeCount;
   D3DKMT_DISPLAYMODE *Modes;
   RCDD_DXGK_INTERFACE Interface;
   RCDD_CDD_INTERFACE CddInterface;
} RCDD_DXGK_ADAPTER, *PRCDD_DXGK_ADAPTER;

static FAST_MUTEX gRcddDxgkLock;
static LONG gRcddDxgkLockState;
static PRCDD_DXGK_ADAPTER gRcddDxgkAdapter;

/* FIXME: the graphics kernel does not call these back on the display-only path; their behaviour is INFO_NEEDED */
static VOID __cdecl RcddDxgkAddD3DDirtyRgn(PVOID Context, const RECT *Rect)
{
   UNREFERENCED_PARAMETER(Context);
   UNREFERENCED_PARAMETER(Rect);
}

/* FIXME: the graphics kernel does not call these back on the display-only path; their behaviour is INFO_NEEDED */
static VOID __cdecl RcddDxgkNotifyExclusiveGdiOwner(PVOID Context)
{
   UNREFERENCED_PARAMETER(Context);
}

/* FIXME: the graphics kernel does not call these back on the display-only path; their behaviour is INFO_NEEDED */
static VOID __cdecl RcddDxgkUpdatePresentRects(PVOID Context, const RECT **Rects, PUINT Count)
{
   UNREFERENCED_PARAMETER(Context);
   UNREFERENCED_PARAMETER(Rects);
   if (Count != NULL)
      *Count = 0;
}

VOID
RcddDxgkInitialize(VOID)
{
   LONG State = InterlockedCompareExchange(&gRcddDxgkLockState, 1, 0);

   ASSERT(State >= 0 && State <= 2);
   if (State == 0)
   {
      ExInitializeFastMutex(&gRcddDxgkLock);
      State = InterlockedExchange(&gRcddDxgkLockState, 2);
      ASSERT(State == 1);
      return;
   }
   while (ReadAcquire(&gRcddDxgkLockState) != 2)
      YieldProcessor();
}

static NTSTATUS
RcddDxgkQueryInterface(
   PRCDD_DXGK_INTERFACE Interface,
   PVOID Context)
{
   UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\DxgKrnl");
   PFILE_OBJECT FileObject;
   PDEVICE_OBJECT DeviceObject;
   IO_STATUS_BLOCK IoStatus;
   KEVENT Event;
   PIRP Irp;
   NTSTATUS Status;

   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   ASSERT(Interface != NULL);
   ASSERT(Context != NULL);

   RtlZeroMemory(Interface, sizeof(*Interface));
   Interface->Size = RCDD_DXGK_INTERFACE_SIZE;
   Interface->Version = RCDD_DXGK_INTERFACE_VERSION;
   Interface->Context = Context;

   Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA | FILE_WRITE_DATA, &FileObject, &DeviceObject);
   if (!NT_SUCCESS(Status))
      return Status;

   KeInitializeEvent(&Event, NotificationEvent, FALSE);
   Irp = IoBuildDeviceIoControlRequest(RCDD_DXGK_INTERFACE_IOCTL, DeviceObject, Interface, sizeof(*Interface),
                                       Interface, sizeof(*Interface), TRUE, &Event, &IoStatus);
   if (Irp == NULL)
   {
      ObDereferenceObject(FileObject);
      return STATUS_INSUFFICIENT_RESOURCES;
   }

   Status = IoCallDriver(DeviceObject, Irp);
   if (Status == STATUS_PENDING)
   {
      KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
      Status = IoStatus.Status;
   }
   ObDereferenceObject(FileObject);

   if (NT_SUCCESS(Status) &&
       (Interface->Functions[RCDD_DXGK_CREATE] == NULL ||
        Interface->Functions[RCDD_DXGK_ENABLE] == NULL ||
        Interface->Functions[RCDD_DXGK_GET_DISPLAY_MODE_LIST] == NULL ||
        Interface->Functions[RCDD_DXGK_GET_DRIVER_CAPS] == NULL ||
        Interface->Functions[RCDD_DXGK_PRESENT_ON_SCREEN] == NULL))
   {
      Status = STATUS_NOT_SUPPORTED;
   }
   return Status;
}

static NTSTATUS
RcddDxgkQueryModes(
   PRCDD_DXGK_ADAPTER Adapter)
{
   PFN_RCDD_DXGK_GET_DISPLAY_MODE_LIST GetDisplayModeList;
   D3DKMT_GETDISPLAYMODELIST List;
   D3DKMT_DISPLAYMODE *Modes;
   LONG Status;

   GetDisplayModeList = (PFN_RCDD_DXGK_GET_DISPLAY_MODE_LIST)Adapter->Interface.Functions[RCDD_DXGK_GET_DISPLAY_MODE_LIST];

   RtlZeroMemory(&List, sizeof(List));
   Status = GetDisplayModeList(Adapter->Context, &List);
   if (Status < 0)
      return Status;
   if (List.ModeCount == 0 || List.ModeCount > RCDD_DXGK_MAX_MODES)
      return STATUS_NOT_FOUND;

   Modes = ExAllocatePoolZero(PagedPool, List.ModeCount * sizeof(*Modes), ALLOC_TAG);
   if (Modes == NULL)
      return STATUS_INSUFFICIENT_RESOURCES;

   List.pModeList = Modes;
   Status = GetDisplayModeList(Adapter->Context, &List);
   if (Status < 0 || List.ModeCount == 0 || List.ModeCount > RCDD_DXGK_MAX_MODES)
   {
      ExFreePoolWithTag(Modes, ALLOC_TAG);
      return Status < 0 ? Status : STATUS_NOT_FOUND;
   }

   Adapter->Modes = Modes;
   Adapter->ModeCount = List.ModeCount;
   return STATUS_SUCCESS;
}

static PRCDD_DXGK_ADAPTER
RcddDxgkCreateAdapter(
   HANDLE hDriver,
   const VOID *EngineInterface)
{
   PFN_RCDD_DXGK_GET_DRIVER_CAPS GetDriverCaps;
   PUCHAR Extension;
   PFN_RCDD_DXGK_CREATE Create;
   PRCDD_DXGK_ADAPTER Adapter;
   PVOID Caps;
   PVOID CapsObject = NULL;
   ULONGLONG CapsValue = 0;
   RCDD_DXGK_DRIVERINFO DisplayInfo, RenderInfo;
   UINT Handle1 = 0, Handle2 = 0;
   PVOID ObjectType = NULL;
   LUID Luid = {0, 0};
   NTSTATUS Status;
   LONG Result;

   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   ASSERT(hDriver != NULL);
   ASSERT(EngineInterface != NULL);
   Extension = ((PDEVICE_OBJECT)hDriver)->DeviceExtension;
   if (Extension == NULL)
      return NULL;
   Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Adapter), ALLOC_TAG);
   if (Adapter == NULL)
      return NULL;
   Adapter->hDriver = hDriver;
   /* FIXME: the display adapter object is read where the graphics kernel keeps it in its display device; that layout is private */
   Adapter->Context = *(PVOID *)(Extension + RCDD_DXGK_GDI_DEVICE_ADAPTER);
   Adapter->Second = Adapter->Context != NULL ? ((PVOID *)Adapter->Context)[1] : NULL;
   if (Adapter->Context == NULL)
   {
      RcddDxgkFreeAdapter(Adapter);
      return NULL;
   }

   Status = RcddDxgkQueryInterface(&Adapter->Interface, Adapter->Context);
   if (!NT_SUCCESS(Status))
   {
      DPRINT1("CDD: the graphics kernel offers no display driver interface: 0x%08lx\n", Status);
      RcddDxgkFreeAdapter(Adapter);
      return NULL;
   }

   Caps = ExAllocatePoolZero(PagedPool, 2 * RCDD_DXGK_CAPS_SIZE, ALLOC_TAG);
   if (Caps == NULL)
   {
      RcddDxgkFreeAdapter(Adapter);
      return NULL;
   }
   GetDriverCaps = (PFN_RCDD_DXGK_GET_DRIVER_CAPS)Adapter->Interface.Functions[RCDD_DXGK_GET_DRIVER_CAPS];
   /* FIXME: the driver capabilities are queried as the display driver does before it creates the device; their use is INFO_NEEDED */
   Result = GetDriverCaps(Adapter->Context, Caps, &CapsObject, &CapsValue, (PUCHAR)Caps + RCDD_DXGK_CAPS_SIZE);
   ExFreePoolWithTag(Caps, ALLOC_TAG);
   if (Result < 0)
      DPRINT1("CDD: the graphics kernel gave no driver capabilities: 0x%08lx\n", Result);

   RtlZeroMemory(&DisplayInfo, sizeof(DisplayInfo));
   RtlZeroMemory(&RenderInfo, sizeof(RenderInfo));
   Create = (PFN_RCDD_DXGK_CREATE)Adapter->Interface.Functions[RCDD_DXGK_CREATE];
   Result = Create(Adapter->Context, Adapter->Second, EngineInterface, &Adapter->hAdapter, &Handle1, &Handle2,
                   &DisplayInfo, &RenderInfo, &Luid, &ObjectType);
   if (Result < 0 || Adapter->hAdapter == 0)
   {
      DPRINT1("CDD: the graphics kernel refused the display device: 0x%08lx\n", Result);
      RcddDxgkFreeAdapter(Adapter);
      return NULL;
   }

   Status = RcddDxgkQueryModes(Adapter);
   if (!NT_SUCCESS(Status))
   {
      DPRINT1("CDD: no display modes from the graphics kernel: 0x%08lx\n", Status);
      /* TODO: destroy the display device the graphics kernel created */
      RcddDxgkFreeAdapter(Adapter);
      return NULL;
   }

   Adapter->CddInterface.Context = Adapter;
   /* FIXME: the two fields after the process name take the values seen from the display driver; their meaning is INFO_NEEDED */
   Adapter->CddInterface.Reserved2 = 2;
   Adapter->CddInterface.Reserved3 = 4;
   Adapter->CddInterface.AddD3DDirtyRgn = (PVOID)RcddDxgkAddD3DDirtyRgn;
   Adapter->CddInterface.NotifyExclusiveGdiOwner = (PVOID)RcddDxgkNotifyExclusiveGdiOwner;
   Adapter->CddInterface.UpdatePresentRects = (PVOID)RcddDxgkUpdatePresentRects;
   RtlCopyMemory(Adapter->CddInterface.ProcessName, PsGetProcessImageFileName(PsGetCurrentProcess()),
                 sizeof(Adapter->CddInterface.ProcessName));

   DPRINT1("CDD: display device %04lx:%04lx adapter 0x%x, %lu modes\n",
           DisplayInfo.VendorId, DisplayInfo.DeviceId, Adapter->hAdapter, Adapter->ModeCount);
   return Adapter;
}

VOID
RcddDxgkFreeAdapter(
   PRCDD_DXGK_ADAPTER Adapter)
{
   if (Adapter == NULL)
      return;
   if (Adapter->Modes != NULL)
      ExFreePoolWithTag(Adapter->Modes, ALLOC_TAG);
   ExFreePoolWithTag(Adapter, ALLOC_TAG);
}

PRCDD_DXGK_ADAPTER
RcddDxgkGetAdapter(
   HANDLE hDriver)
{
   const VOID *EngineInterface;
   PRCDD_DXGK_ADAPTER Adapter;

   PAGED_CODE();
   ASSERT(ReadAcquire(&gRcddDxgkLockState) == 2);

   EngineInterface = RosWddmHandOverEngineInterface();
   if (EngineInterface == NULL)
      return NULL;

   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   KeEnterCriticalRegion();
   ExAcquireFastMutexUnsafe(&gRcddDxgkLock);
   Adapter = gRcddDxgkAdapter;
   if (Adapter == NULL)
   {
      RCDD_DXGK_CALL_CONTEXT Call;

      if (RcddDxgkEnterDisplayProcess(&Call))
      {
         Adapter = RcddDxgkCreateAdapter(hDriver, EngineInterface);
         RcddDxgkLeaveDisplayProcess(&Call);
      }
      gRcddDxgkAdapter = Adapter;
   }
   /* TODO: one display device per graphics kernel adapter; further devices share the first */
   ExReleaseFastMutexUnsafe(&gRcddDxgkLock);
   KeLeaveCriticalRegion();
   ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
   return Adapter;
}

static BOOL
RcddDxgkModeIsUsable(
   const D3DKMT_DISPLAYMODE *Mode)
{
   return Mode->Width != 0 && Mode->Height != 0 && Mode->Width <= 0x4000 && Mode->Height <= 0x4000 &&
          (Mode->Format == D3DDDIFMT_A8R8G8B8 || Mode->Format == D3DDDIFMT_X8R8G8B8);
}

DWORD
RcddDxgkGetModes(
   PRCDD_DXGK_ADAPTER Adapter,
   PVIDEO_MODE_INFORMATION *ModeInfo,
   DWORD *ModeInfoSize)
{
   PVIDEO_MODE_INFORMATION Info;
   const D3DKMT_DISPLAYMODE *Mode;
   ULONG Index;

   ASSERT(Adapter != NULL);
   ASSERT(Adapter->ModeCount != 0 && Adapter->ModeCount <= RCDD_DXGK_MAX_MODES);

   *ModeInfo = EngAllocMem(FL_ZERO_MEMORY, Adapter->ModeCount * sizeof(VIDEO_MODE_INFORMATION), ALLOC_TAG);
   *ModeInfoSize = sizeof(VIDEO_MODE_INFORMATION);
   if (*ModeInfo == NULL)
      return 0;

   for (Index = 0; Index < Adapter->ModeCount; Index++)
   {
      Mode = &Adapter->Modes[Index];
      Info = &(*ModeInfo)[Index];
      if (!RcddDxgkModeIsUsable(Mode))
         continue;
      Info->Length = sizeof(VIDEO_MODE_INFORMATION);
      Info->ModeIndex = Index;
      Info->VisScreenWidth = Mode->Width;
      Info->VisScreenHeight = Mode->Height;
      Info->ScreenStride = Mode->Width * 4;
      Info->NumberOfPlanes = 1;
      Info->BitsPerPlane = 32;
      Info->Frequency = Mode->IntegerRefreshRate;
      Info->NumberRedBits = 8;
      Info->NumberGreenBits = 8;
      Info->NumberBlueBits = 8;
      Info->RedMask = 0x00FF0000;
      Info->GreenMask = 0x0000FF00;
      Info->BlueMask = 0x000000FF;
      Info->AttributeFlags = VIDEO_MODE_GRAPHICS | VIDEO_MODE_COLOR;
      Info->VideoMemoryBitmapWidth = Mode->Width;
      Info->VideoMemoryBitmapHeight = Mode->Height;
   }
   return Adapter->ModeCount;
}

typedef struct _RCDD_DXGK_ENABLE_CALL
{
   PFN_RCDD_DXGK_ENABLE Enable;
   PRCDD_DXGK_ADAPTER Adapter;
   const D3DKMT_DISPLAYMODE *Mode;
   PVOID *Primary;
   ULONG_PTR *Size;
   PUINT Pitch;
   LONG Status;
} RCDD_DXGK_ENABLE_CALL;

static VOID
RcddDxgkEnableWorker(
   PVOID Parameter)
{
   RCDD_DXGK_ENABLE_CALL *Call = Parameter;

   Call->Status = Call->Enable(Call->Adapter->hAdapter, 0, &Call->Adapter->CddInterface, 0, Call->Mode, Call->Primary,
                               Call->Size, Call->Pitch);
}

BOOL
RcddDxgkEnableSource(
   PRCDD_PDEV ppdev)
{
   PRCDD_DXGK_ADAPTER Adapter = ppdev->DxgkAdapter;
   PFN_RCDD_DXGK_ENABLE Enable;
   const D3DKMT_DISPLAYMODE *Mode;
   D3DKMT_DISPLAYMODE EnableMode;
   RCDD_DXGK_ENABLE_CALL EnableCall;
   PVOID Primary = NULL;
   ULONG_PTR Size;
   UINT Pitch = 0;
   LONG Status;
   NTSTATUS MapStatus;
   PVOID Bits;
   SIZE_T ViewSize;

   ASSERT(Adapter != NULL);
   ASSERT(ppdev->ScreenPtr == NULL);
   if (ppdev->ModeIndex >= Adapter->ModeCount)
      return FALSE;
   Mode = &Adapter->Modes[ppdev->ModeIndex];
   if (!RcddDxgkModeIsUsable(Mode) || Mode->Width != ppdev->ScreenWidth || Mode->Height != ppdev->ScreenHeight)
      return FALSE;

   Size = (ULONG_PTR)Mode->Width * Mode->Height * 4;
   Enable = (PFN_RCDD_DXGK_ENABLE)Adapter->Interface.Functions[RCDD_DXGK_ENABLE];
   /* FIXME: the second and fourth arguments are passed as the first enable of a device passes them; their meaning is INFO_NEEDED */
   EnableMode = *Mode;
   RtlZeroMemory(&EnableMode.Flags, sizeof(EnableMode.Flags));
   EnableCall.Enable = Enable;
   EnableCall.Adapter = Adapter;
   EnableCall.Mode = &EnableMode;
   EnableCall.Primary = &Primary;
   EnableCall.Size = &Size;
   EnableCall.Pitch = &Pitch;
   EnableCall.Status = STATUS_UNSUCCESSFUL;
   if (!NT_SUCCESS(RcddDxgkRunInDisplayProcess(RcddDxgkEnableWorker, &EnableCall)))
      return FALSE;
   Status = EnableCall.Status;
   if (Status < 0 || Primary == NULL || Pitch < Mode->Width * 4 || Pitch > Mode->Width * 4 + 0x1000 ||
       Size < (ULONG_PTR)Pitch * Mode->Height)
   {
      DPRINT1("CDD: the graphics kernel gave no shadow surface for source 0 at %lux%lu: 0x%08lx pitch %u size 0x%Ix\n",
              Mode->Width, Mode->Height, Status, Pitch, Size);
      /* TODO: the source is not disabled in the graphics kernel */
      return FALSE;
   }

   ViewSize = Size;
   Bits = NULL;
   MapStatus = MmMapViewInSessionSpace(Primary, &Bits, &ViewSize);
   if (!NT_SUCCESS(MapStatus))
   {
      DPRINT1("CDD: the shadow surface of source 0 cannot be mapped: 0x%08lx\n", MapStatus);
      /* TODO: the source is not disabled in the graphics kernel */
      return FALSE;
   }
   ASSERT(Bits != NULL);
   ASSERT(ViewSize >= (SIZE_T)Pitch * Mode->Height);
   ppdev->ScreenPtr = Bits;
   ppdev->ScreenDelta = Pitch;
   ppdev->DxgkScreenBytes = Pitch * Mode->Height;
   return TRUE;
}

VOID
RcddDxgkDisableSource(
   PRCDD_PDEV ppdev)
{
   ASSERT(ppdev->DxgkAdapter != NULL);
   /* TODO: the source is not disabled in the graphics kernel */
   if (ppdev->ScreenPtr != NULL)
   {
      NTSTATUS Status = MmUnmapViewInSessionSpace(ppdev->ScreenPtr);

      ASSERT(NT_SUCCESS(Status));
      ppdev->ScreenPtr = NULL;
   }
   ppdev->DxgkScreenBytes = 0;
}

BOOL
RcddDxgkPresent(
   PRCDD_PDEV ppdev,
   const RECTL *prcl,
   ULONG Count)
{
   PRCDD_DXGK_ADAPTER Adapter = ppdev->DxgkAdapter;
   PFN_RCDD_DXGK_PRESENT_ON_SCREEN PresentOnScreen;
   RCDD_DXGK_CALL_CONTEXT Call;
   RCDD_PRESENT_ON_SCREEN Present;
   RECT Rect;
   ULONG Index;
   LONG Status;

   ASSERT(Adapter != NULL);
   if (ppdev->ScreenPtr == NULL)
      return FALSE;

   PresentOnScreen = (PFN_RCDD_DXGK_PRESENT_ON_SCREEN)Adapter->Interface.Functions[RCDD_DXGK_PRESENT_ON_SCREEN];
   if (!RcddDxgkEnterDisplayProcess(&Call))
      return FALSE;
   for (Index = 0; Index < (Count ? Count : 1); Index++)
   {
      if (Count == 0)
      {
         Rect.left = 0;
         Rect.top = 0;
         Rect.right = (LONG)ppdev->ScreenWidth;
         Rect.bottom = (LONG)ppdev->ScreenHeight;
      }
      else
      {
         Rect.left = max(prcl[Index].left, 0);
         Rect.top = max(prcl[Index].top, 0);
         Rect.right = min(prcl[Index].right, (LONG)ppdev->ScreenWidth);
         Rect.bottom = min(prcl[Index].bottom, (LONG)ppdev->ScreenHeight);
         if (Rect.left >= Rect.right || Rect.top >= Rect.bottom)
            continue;
      }

      RtlZeroMemory(&Present, sizeof(Present));
      /* FIXME: the first, +0x4C and +0x58 fields take the values seen on every present; their meaning is INFO_NEEDED */
      Present.Unknown0 = 1;
      Present.Unknown4C = 1;
      Present.Unknown58 = 3;
      Present.Context = Adapter->Context;
      Present.Source = ppdev->ScreenPtr;
      Present.Width = ppdev->ScreenWidth;
      Present.Height = ppdev->ScreenHeight;
      Present.Pitch = ppdev->ScreenDelta;
      Present.DirtyRect = &Rect;
      Status = PresentOnScreen(&Present);
      if (Status < 0)
      {
         DPRINT1("CDD: present on screen failed: 0x%08lx\n", Status);
         RcddDxgkLeaveDisplayProcess(&Call);
         return FALSE;
      }
   }
   RcddDxgkLeaveDisplayProcess(&Call);
   return TRUE;
}
