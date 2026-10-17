/*
 * PROJECT:     LiberNT host-side display tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Drive the production display bring-up path on a simulated boot
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define VOID void
#define CONST const
#define TRUE 1
#define FALSE 0
#define NTAPI
#define APIENTRY
#define FASTCALL
#define FORCEINLINE static inline
#define CODE_SEG(x)
#define PAGED_CODE()
#define IN
#define OUT
#define OPTIONAL
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define _Inout_opt_
#define _Success_(x)
#define _Requires_lock_held_(x)
#define _In_reads_bytes_opt_(x)
#define _Out_writes_bytes_opt_(x)
#define _PRAGMA_WARNING_SUPPRESS(x)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define UNIMPLEMENTED ((void)0)
#define ASSERT(x) HostAssert(!!(x), #x, __FILE__, __LINE__)
#define NT_ASSERT(x) ASSERT(x)

typedef uint8_t UCHAR, BYTE, BOOLEAN, *PUCHAR, *PBYTE, *LPBYTE, *PBOOLEAN;
typedef char CHAR, *PCHAR, KPROCESSOR_MODE;
typedef uint16_t USHORT, WORD;
typedef int16_t SHORT;
typedef uint32_t ULONG, DWORD, UINT, ACCESS_MASK, *PULONG, *PDWORD, *LPDWORD;
typedef int32_t LONG, NTSTATUS, *PLONG;
typedef int INT, BOOL;
typedef uint64_t ULONGLONG, *PULONGLONG;
typedef int64_t LONGLONG;
typedef uintptr_t ULONG_PTR, SIZE_T;
typedef intptr_t LONG_PTR;
typedef void *PVOID, *LPVOID, *HANDLE, **PHANDLE;
typedef HANDLE HKEY, *PHKEY, HDC, HBITMAP, HBRUSH, HFONT, HGDIOBJ, HSEMAPHORE, HDEV;
typedef wchar_t WCHAR, *PWCHAR, *PWSTR, *LPWSTR, *PWCH;
typedef const wchar_t *PCWSTR, *LPCWSTR;

typedef union _LARGE_INTEGER
{
    struct
    {
        ULONG LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER, PHYSICAL_ADDRESS;

typedef struct _UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

#define RTL_CONSTANT_STRING(s) { sizeof(s) - sizeof((s)[0]), sizeof(s), (PWSTR)(s) }
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define ARRAYSIZE(a) RTL_NUMBER_OF(a)
#define _countof(a) RTL_NUMBER_OF(a)
#define FIELD_OFFSET(t, f) ((LONG)offsetof(t, f))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define MAXLONG 0x7fffffffL
#define MAXULONG 0xffffffffUL
#define MAXULONG_PTR UINTPTR_MAX
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAX_PATH 260

#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
#define STATUS_PENDING                  ((NTSTATUS)0x00000103L)
#define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x80000005L)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED          ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_TYPE_MISMATCH     ((NTSTATUS)0xC0000024L)
#define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION    ((NTSTATUS)0xC0000035L)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009AL)
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BBL)
#define STATUS_DEVICE_DOES_NOT_EXIST    ((NTSTATUS)0xC00000C0L)
#define STATUS_INVALID_DEVICE_REQUEST   ((NTSTATUS)0xC0000010L)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182L)
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)

#define ERROR_SUCCESS 0
#define ERROR_INVALID_FUNCTION 1
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_DEV_NOT_EXIST 55
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MORE_DATA 234
#define ERROR_IO_PENDING 997

#define REG_NONE 0
#define REG_SZ 1
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_OPTION_VOLATILE 1
#define KEY_READ 0x20019
#define KEY_ALL_ACCESS 0xF003F
#define OBJ_CASE_INSENSITIVE 0x40
#define OBJ_KERNEL_HANDLE 0x200
#define RTL_REGISTRY_HANDLE 0x40000000
#define RTL_REGISTRY_DEVICEMAP 4
#define RTL_QUERY_REGISTRY_DIRECT 0x20

typedef struct _OBJECT_ATTRIBUTES
{
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define InitializeObjectAttributes(p, n, a, r, s) \
    do { (p)->Length = sizeof(OBJECT_ATTRIBUTES); (p)->RootDirectory = (r); \
         (p)->ObjectName = (n); (p)->Attributes = (a); (p)->SecurityDescriptor = (s); \
         (p)->SecurityQualityOfService = NULL; } while (0)

typedef enum _KEY_VALUE_INFORMATION_CLASS
{
    KeyValueBasicInformation,
    KeyValueFullInformation,
    KeyValuePartialInformation
} KEY_VALUE_INFORMATION_CLASS;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION
{
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

typedef NTSTATUS (*PRTL_QUERY_REGISTRY_ROUTINE)(PWSTR, ULONG, PVOID, ULONG, PVOID, PVOID);
typedef struct _RTL_QUERY_REGISTRY_TABLE
{
    PRTL_QUERY_REGISTRY_ROUTINE QueryRoutine;
    ULONG Flags;
    PWSTR Name;
    PVOID EntryContext;
    ULONG DefaultType;
    PVOID DefaultData;
    ULONG DefaultLength;
} RTL_QUERY_REGISTRY_TABLE, *PRTL_QUERY_REGISTRY_TABLE;

typedef enum _POOL_TYPE { NonPagedPool, PagedPool } POOL_TYPE;
typedef enum _KWAIT_REASON { Executive } KWAIT_REASON;
typedef enum _EVENT_TYPE { NotificationEvent, SynchronizationEvent } EVENT_TYPE;
typedef enum _MEMORY_CACHING_TYPE { MmNonCached, MmCached, MmWriteCombined } MEMORY_CACHING_TYPE;
typedef enum _DEVICE_REGISTRY_PROPERTY { DevicePropertyDeviceDescription } DEVICE_REGISTRY_PROPERTY;
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define FILE_DEVICE_VIDEO 0x23
#define FILE_DEVICE_SECURE_OPEN 0x100
#define FILE_READ_DATA 1
#define FILE_WRITE_DATA 2
#define DO_BUFFERED_IO 0x4
#define DO_DEVICE_INITIALIZING 0x80

typedef struct _KEVENT { LONG Signaled; } KEVENT, *PKEVENT;
typedef struct _KMUTEX { LONG Owned; } KMUTEX, *PKMUTEX;
typedef struct _EX_RUNDOWN_REF { LONG Count; } EX_RUNDOWN_REF;
typedef struct _DRIVER_OBJECT { LONG Unused; } DRIVER_OBJECT, *PDRIVER_OBJECT;
typedef struct _FILE_OBJECT { struct _DEVICE_OBJECT *DeviceObject; } FILE_OBJECT, *PFILE_OBJECT;
typedef struct _DEVICE_OBJECT
{
    PDRIVER_OBJECT DriverObject;
    ULONG Flags;
    PVOID DeviceExtension;
    WCHAR Name[64];
} DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _IO_STATUS_BLOCK
{
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _IO_STACK_LOCATION
{
    struct
    {
        struct
        {
            ULONG OutputBufferLength;
            ULONG InputBufferLength;
            ULONG IoControlCode;
        } DeviceIoControl;
    } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;

typedef struct _IRP
{
    struct
    {
        PVOID SystemBuffer;
    } AssociatedIrp;
    IO_STATUS_BLOCK IoStatus;
    KPROCESSOR_MODE RequestorMode;
    PVOID UserBuffer;
    ULONG UserBufferLength;
    PIO_STATUS_BLOCK UserIosb;
    IO_STACK_LOCATION Stack;
} IRP, *PIRP;
#define IoGetCurrentIrpStackLocation(i) (&(i)->Stack)

#define CTL_CODE(t, f, m, a) (((t) << 16) | ((a) << 14) | ((f) << 2) | (m))
#define IOCTL_VIDEO_QUERY_AVAIL_MODES       CTL_CODE(FILE_DEVICE_VIDEO, 0x100, 0, 0)
#define IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES   CTL_CODE(FILE_DEVICE_VIDEO, 0x101, 0, 0)
#define IOCTL_VIDEO_QUERY_CURRENT_MODE      CTL_CODE(FILE_DEVICE_VIDEO, 0x102, 0, 0)
#define IOCTL_VIDEO_SET_CURRENT_MODE        CTL_CODE(FILE_DEVICE_VIDEO, 0x103, 0, 0)
#define IOCTL_VIDEO_ENUM_MONITOR_PDO        CTL_CODE(FILE_DEVICE_VIDEO, 0x104, 0, 0)
#define IOCTL_VIDEO_INIT_WIN32K_CALLBACKS   CTL_CODE(FILE_DEVICE_VIDEO, 0x105, 0, 0)

#define VIDEO_MODE_COLOR 0x0001
#define VIDEO_MODE_GRAPHICS 0x0002
#define VIDEO_MODE_INTERLACED 0x0010
#define DXGK_DISP_DRIVERSPEC_SYSMEM_FB 0x0001

typedef struct _VIDEO_NUM_MODES
{
    ULONG NumModes;
    ULONG ModeInformationLength;
} VIDEO_NUM_MODES, *PVIDEO_NUM_MODES;

typedef struct _VIDEO_MODE_INFORMATION
{
    ULONG Length;
    ULONG ModeIndex;
    ULONG VisScreenWidth;
    ULONG VisScreenHeight;
    ULONG ScreenStride;
    ULONG NumberOfPlanes;
    ULONG BitsPerPlane;
    ULONG Frequency;
    ULONG XMillimeter;
    ULONG YMillimeter;
    ULONG NumberRedBits;
    ULONG NumberGreenBits;
    ULONG NumberBlueBits;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG AttributeFlags;
    ULONG VideoMemoryBitmapWidth;
    ULONG VideoMemoryBitmapHeight;
    ULONG DriverSpecificAttributeFlags;
} VIDEO_MODE_INFORMATION, *PVIDEO_MODE_INFORMATION;

typedef VOID (NTAPI *PVIDEO_WIN32K_CALLOUT)(PVOID Params);
typedef struct _VIDEO_WIN32K_CALLBACKS
{
    PVOID PhysDisp;
    PVIDEO_WIN32K_CALLOUT Callout;
    ULONG bACPI;
    HANDLE pPhysDeviceObject;
    ULONG DualviewFlags;
} VIDEO_WIN32K_CALLBACKS, *PVIDEO_WIN32K_CALLBACKS;

typedef struct _VIDEO_MONITOR_DEVICE
{
    ULONG flag;
    PDEVICE_OBJECT pdo;
    ULONG HwID;
} VIDEO_MONITOR_DEVICE, *PVIDEO_MONITOR_DEVICE;

typedef struct _POINTL
{
    LONG x;
    LONG y;
} POINTL, POINT;

#define CCHDEVICENAME 32
typedef struct _DEVMODEW
{
    WCHAR dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    POINTL dmPosition;
    DWORD dmDisplayOrientation;
    DWORD dmDisplayFixedOutput;
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    DWORD dmDisplayFlags;
    DWORD dmDisplayFrequency;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEW, *PDEVMODEW, *LPDEVMODEW;

#define DM_SPECVERSION 0x0401
#define DM_POSITION 0x00000020
#define DM_DISPLAYORIENTATION 0x00000080
#define DM_LOGPIXELS 0x00020000
#define DM_BITSPERPEL 0x00040000
#define DM_PELSWIDTH 0x00080000
#define DM_PELSHEIGHT 0x00100000
#define DM_DISPLAYFLAGS 0x00200000
#define DM_DISPLAYFREQUENCY 0x00400000
#define DM_PANNINGWIDTH 0x08000000
#define DM_PANNINGHEIGHT 0x10000000
#define DM_DISPLAYFIXEDOUTPUT 0x20000000
#define DM_INTERLACED 0x00000002

#define DISP_CHANGE_SUCCESSFUL 0
#define DISP_CHANGE_FAILED (-1)
#define DISP_CHANGE_BADMODE (-2)
#define DISP_CHANGE_BADPARAM (-5)

#define DISPLAY_DEVICE_PRIMARY_DEVICE 0x00000004
#define DISPLAY_DEVICE_VGA_COMPATIBLE 0x00000010

typedef struct _DEVMODEINFO
{
    struct _DEVMODEINFO *pdmiNext;
    ULONG cbdevmode;
    DEVMODEW adevmode[1];
} DEVMODEINFO, *PDEVMODEINFO;

typedef struct _DEVMODEENTRY
{
    DWORD dwFlags;
    PDEVMODEW pdm;
} DEVMODEENTRY, *PDEVMODEENTRY;

typedef struct _GRAPHICS_DEVICE
{
    WCHAR szNtDeviceName[CCHDEVICENAME];
    WCHAR szWinDeviceName[CCHDEVICENAME];
    struct _GRAPHICS_DEVICE *pNextGraphicsDevice;
    struct _GRAPHICS_DEVICE *pVgaDevice;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PVOID PhysDeviceHandle;
    DWORD StateFlags;
    PWSTR pDiplayDrivers;
    PWSTR pwszDescription;
    PDEVMODEINFO pdevmodeInfo;
    PDEVMODEENTRY pDevModeList;
    ULONG cDevModes;
    PVIDEO_MONITOR_DEVICE pvMonDev;
    DWORD dwMonCnt;
} GRAPHICS_DEVICE, *PGRAPHICS_DEVICE;

typedef struct _GDIINFO
{
    ULONG ulHorzRes;
    ULONG ulVertRes;
} GDIINFO;

typedef struct _PDEVOBJ
{
    PGRAPHICS_DEVICE pGraphicsDevice;
    GDIINFO gdiinfo;
    ULONG BitsPerPel;
    ULONG Frequency;
    LONG cRefs;
} PDEVOBJ, *PPDEVOBJ;

typedef struct _MDEVDISPLAY
{
    PPDEVOBJ ppdev;
} MDEVDISPLAY, *PMDEVDISPLAY;

typedef struct _MDEVOBJ
{
    ULONG cDev;
    PPDEVOBJ ppdevGlobal;
    MDEVDISPLAY dev[0];
} MDEVOBJ, *PMDEVOBJ;

typedef ULONG (APIENTRY *PFN_DrvGetModes)(HANDLE, ULONG, DEVMODEW *);
typedef struct _LDEVOBJ
{
    struct
    {
        PFN_DrvGetModes GetModes;
    } pfn;
} LDEVOBJ, *PLDEVOBJ;
#define LDEV_DEVICE_DISPLAY 1
#define LDEV_DEVICE_META 3

#define GDITAG_GDEVICE 1
#define GDITAG_DRVSUP 2
#define GDITAG_DEVMODE 3
#define GDITAG_MDEV 4
#define TAG_TEMP 5
#define USERTAG_DCE 6
#define TAG_DXGK_DISPLAY 7

typedef struct _TEXTMETRICW
{
    LONG tmHeight;
} TEXTMETRICW;

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define PLANES 14
#define BITSPIXEL 12
#define LOGPIXELSY 90
#define RASTERCAPS 38
#define RC_PALETTE 0x0100
#define PUSIF_PALETTEDISPLAY 0x01
#define SYSTEM_FONT 13
#define GDI_OBJ_HMGR_PUBLIC 0

typedef struct _SERVERINFO
{
    INT aiSysMet[100];
    DWORD Planes;
    DWORD BitsPixel;
    DWORD BitCount;
    DWORD dmLogPixels;
    DWORD PUSIFlags;
    INT cxSysFontChar;
    DWORD cySysFontChar;
    TEXTMETRICW tmSysFont;
    POINT ptCursor;
    HBRUSH hbrGray;
} SERVERINFO, *PSERVERINFO;

typedef struct _DESKTOP
{
    LONG Unused;
} DESKTOP, *PDESKTOP;

#define W32PF_CREATEDWINORDC 0x00000001
#define W32PF_MANUALGUICHECK 0x00000002
typedef struct _PROCESSINFO
{
    DWORD W32PF_flags;
} PROCESSINFO, *PPROCESSINFO;

#define VIDEO_DRIVER_INIT_FAILURE 0xB4
#define USER_VERSION MAKELONG(0x0000, 0x0005)

typedef enum _D3DDDIFORMAT
{
    D3DDDIFMT_UNKNOWN = 0,
    D3DDDIFMT_A8R8G8B8 = 21,
    D3DDDIFMT_X8R8G8B8 = 22
} D3DDDIFORMAT;
#define D3DDDI_ID_UNINITIALIZED ((UINT)(~0))

typedef struct _DXGK_DISPLAY_INFORMATION
{
    UINT Width;
    UINT Height;
    UINT Pitch;
    D3DDDIFORMAT ColorFormat;
    PHYSICAL_ADDRESS PhysicAddress;
    UINT TargetId;
    UINT AcpiId;
} DXGK_DISPLAY_INFORMATION, *PDXGK_DISPLAY_INFORMATION;

typedef enum _DXGK_FRAMEBUFFER_STATE
{
    FrameBufferStateUnknown = 0,
    FrameBufferStateInitializedByDriver,
    FrameBufferStateInitializedByFirmware
} DXGK_FRAMEBUFFER_STATE;

typedef struct _DXGK_DISPLAY_OWNERSHIP_FLAGS
{
    DXGK_FRAMEBUFFER_STATE FrameBufferState;
} DXGK_DISPLAY_OWNERSHIP_FLAGS, *PDXGK_DISPLAY_OWNERSHIP_FLAGS;

typedef struct _DXGK_INTERFACE
{
    ULONG Size;
    HANDLE DeviceHandle;
    NTSTATUS (APIENTRY *DxgkCbAcquirePostDisplayOwnership)(HANDLE, PDXGK_DISPLAY_INFORMATION);
} DXGK_INTERFACE, *PDXGK_INTERFACE;

typedef struct _DXGKRNL_MINIPORT_CONTEXT
{
    BOOLEAN IsBasicDisplayFallback;
    BOOLEAN IsDisplayOnlyDriver;
} DXGKRNL_MINIPORT_CONTEXT, *PDXGKRNL_MINIPORT_CONTEXT;

typedef struct _DXGKRNL_ADAPTER
{
    PDXGKRNL_MINIPORT_CONTEXT MiniportContext;
    PDEVICE_OBJECT PhysicalDeviceObject;
    WCHAR DisplayDeviceName[64];
    ULONG CommittedWidth;
    ULONG CommittedHeight;
    BOOLEAN VidPnCommitted;
    BOOLEAN HeadlessDesktop;
    KMUTEX VidPnMutex;
    ULONG PostDisplayWidth;
    ULONG PostDisplayHeight;
    ULONG PostDisplayPitch;
    PHYSICAL_ADDRESS PostDisplayPhysicalAddress;
    PVOID PostDisplayVirtualAddress;
    SIZE_T PostDisplayMappingSize;
    LONG PostDisplayAcquireCount;
    EX_RUNDOWN_REF ReverseCallbackRundownRef;
} DXGKRNL_ADAPTER, *PDXGKRNL_ADAPTER;

typedef struct _RECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
#define UNICODE_NULL ((WCHAR)0)

#include <loader_framebuffer.h>
#include <softgpu_2d_shared.h>

static void HostAssert(int Value, const char *Text, const char *File, int Line);
static void HostLog(const char *Who, const char *Format, ...);
__attribute__((noreturn)) static VOID KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4);

#define TRACE(...) ((void)0)
#define WARN(...) HostLog("win32k", __VA_ARGS__)
#define ERR(...) HostLog("win32k", __VA_ARGS__)
#define DXGKRNL_TRACE(...) ((void)0)
#define DXGKRNL_INFO(...) ((void)0)
#define DXGKRNL_WARN(...) HostLog("dxgkrnl", __VA_ARGS__)
#define DXGKRNL_ERR(...) HostLog("dxgkrnl", __VA_ARGS__)

#define _SEH2_TRY if (1)
#define _SEH2_EXCEPT(x) else
#define _SEH2_END
#define EXCEPTION_EXECUTE_HANDLER 1

#define RtlCopyMemory(d, s, n) memcpy((d), (s), (n))
#define RtlZeroMemory(d, n) memset((d), 0, (n))
#define _wcsicmp wcscasecmp
#define InterlockedIncrement(p) __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedCompareExchange(p, x, c) __sync_val_compare_and_swap((p), (c), (x))
#define InterlockedExchangePointer(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)


static jmp_buf g_BugCheckJump;
static BOOLEAN g_BugCheckArmed;
static ULONG g_BugCheckCode;
static ULONG_PTR g_BugCheckParameters[4];
static char g_Log[64][160];
static ULONG g_LogCount;

static void
HostAssert(int Value, const char *Text, const char *File, int Line)
{
    if (!Value)
        HostLog("assert", "%s at %s:%d", Text, File, Line);
}

static void
HostLog(const char *Who, const char *Format, ...)
{
    char Line[160];
    size_t Length;

    snprintf(Line, sizeof(Line), "%s: %s", Who, Format);
    Length = strlen(Line);
    while (Length && (Line[Length - 1] == '\n' || Line[Length - 1] == ' '))
        Line[--Length] = 0;
    if (g_LogCount < RTL_NUMBER_OF(g_Log))
        strcpy(g_Log[g_LogCount++], Line);
}

__attribute__((noreturn)) static VOID
KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4)
{
    g_BugCheckCode = Code;
    g_BugCheckParameters[0] = P1;
    g_BugCheckParameters[1] = P2;
    g_BugCheckParameters[2] = P3;
    g_BugCheckParameters[3] = P4;
    if (!g_BugCheckArmed)
    {
        fprintf(stderr, "unexpected bugcheck 0x%X outside a guarded step\n", Code);
        abort();
    }
    longjmp(g_BugCheckJump, 1);
}

static void
HostFixFormat(PWSTR Out, size_t Count, PCWSTR Format)
{
    size_t i = 0;

    while (*Format && i + 1 < Count)
    {
        if (Format[0] == L'%' && Format[1] == L'l' &&
            (Format[2] == L'u' || Format[2] == L'd' || Format[2] == L'x' || Format[2] == L'X'))
        {
            Out[i++] = L'%';
            Format += 2;
            continue;
        }
        Out[i++] = *Format++;
    }
    Out[i] = 0;
}

static NTSTATUS
HostVswprintf(PWSTR Destination, size_t Count, PCWSTR Format, va_list Arguments)
{
    WCHAR Fixed[256];
    int Written;

    HostFixFormat(Fixed, RTL_NUMBER_OF(Fixed), Format);
    Written = vswprintf(Destination, Count, Fixed, Arguments);
    return (Written < 0) ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

static NTSTATUS
HostSwprintf(PWSTR Destination, size_t Count, PCWSTR Format, ...)
{
    va_list Arguments;
    NTSTATUS Status;

    va_start(Arguments, Format);
    Status = HostVswprintf(Destination, Count, Format, Arguments);
    va_end(Arguments);
    return Status;
}

#define _swprintf(d, ...) HostSwprintf((d), RTL_NUMBER_OF(d), __VA_ARGS__)

static NTSTATUS
RtlStringCchPrintfW(PWSTR Destination, size_t Count, PCWSTR Format, ...)
{
    va_list Arguments;
    NTSTATUS Status;

    va_start(Arguments, Format);
    Status = HostVswprintf(Destination, Count, Format, Arguments);
    va_end(Arguments);
    return Status;
}

static NTSTATUS
RtlStringCbPrintfW(PWSTR Destination, size_t Bytes, PCWSTR Format, ...)
{
    va_list Arguments;
    NTSTATUS Status;

    va_start(Arguments, Format);
    Status = HostVswprintf(Destination, Bytes / sizeof(WCHAR), Format, Arguments);
    va_end(Arguments);
    return Status;
}

static NTSTATUS
RtlStringCchCopyW(PWSTR Destination, size_t Count, PCWSTR Source)
{
    if (wcslen(Source) + 1 > Count)
        return STATUS_BUFFER_OVERFLOW;
    wcscpy(Destination, Source);
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlStringCbCopyW(PWSTR Destination, size_t Bytes, PCWSTR Source)
{
    return RtlStringCchCopyW(Destination, Bytes / sizeof(WCHAR), Source);
}

static NTSTATUS
RtlStringCbCopyNW(PWSTR Destination, size_t Bytes, PCWSTR Source, size_t SourceBytes)
{
    size_t Count = SourceBytes / sizeof(WCHAR);
    size_t i;

    if (Count + 1 > Bytes / sizeof(WCHAR))
        return STATUS_BUFFER_OVERFLOW;
    for (i = 0; i < Count && Source[i]; i++)
        Destination[i] = Source[i];
    Destination[i] = 0;
    return STATUS_SUCCESS;
}

static VOID
RtlInitUnicodeString(PUNICODE_STRING String, PCWSTR Source)
{
    String->Buffer = (PWSTR)Source;
    String->Length = Source ? (USHORT)(wcslen(Source) * sizeof(WCHAR)) : 0;
    String->MaximumLength = Source ? (USHORT)(String->Length + sizeof(WCHAR)) : 0;
}

static BOOLEAN
RtlEqualUnicodeString(const UNICODE_STRING *A, const UNICODE_STRING *B, BOOLEAN CaseInsensitive)
{
    size_t i;

    if (A->Length != B->Length)
        return FALSE;
    for (i = 0; i < A->Length / sizeof(WCHAR); i++)
    {
        WCHAR a = A->Buffer[i], b = B->Buffer[i];
        if (CaseInsensitive)
        {
            a = towupper(a);
            b = towupper(b);
        }
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

static void
HostCopyName(PWSTR Out, size_t Count, const UNICODE_STRING *Name)
{
    size_t Length = min(Name->Length / sizeof(WCHAR), Count - 1);

    memcpy(Out, Name->Buffer, Length * sizeof(WCHAR));
    Out[Length] = 0;
}

static PVOID ExAllocatePoolWithTag(POOL_TYPE Type, SIZE_T Size, ULONG Tag) { return malloc(Size ? Size : 1); }
static PVOID ExAllocatePoolZero(POOL_TYPE Type, SIZE_T Size, ULONG Tag) { return calloc(1, Size ? Size : 1); }
static VOID ExFreePoolWithTag(PVOID P, ULONG Tag) { free(P); }
static VOID ExFreePool(PVOID P) { free(P); }
static PVOID EngAllocMem(ULONG Flags, ULONG Size, ULONG Tag) { return calloc(1, Size ? Size : 1); }
static VOID EngFreeMem(PVOID P) { free(P); }
static NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON Reason, KPROCESSOR_MODE Mode, BOOLEAN Alertable, PLARGE_INTEGER Timeout) { return STATUS_SUCCESS; }
static LONG KeReleaseMutex(PKMUTEX Mutex, BOOLEAN Wait) { return 0; }
static VOID KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State) { Event->Signaled = State; }
static VOID KeEnterCriticalRegion(VOID) { }
static VOID KeLeaveCriticalRegion(VOID) { }
static VOID ExReleaseRundownProtection(EX_RUNDOWN_REF *Ref) { }
static VOID EngAcquireSemaphore(HSEMAPHORE S) { }
static VOID EngAcquireSemaphoreShared(HSEMAPHORE S) { }
static VOID EngReleaseSemaphore(HSEMAPHORE S) { }


typedef struct _HOST_VALUE
{
    WCHAR Name[64];
    ULONG Type;
    ULONG Size;
    UCHAR Data[512];
} HOST_VALUE;

typedef struct _HOST_KEY
{
    WCHAR Path[192];
    BOOLEAN Exists;
    HOST_VALUE Values[32];
    ULONG ValueCount;
} HOST_KEY;

static HOST_KEY g_Keys[32];
static ULONG g_KeyCount;
static BOOLEAN g_InDxgkRegistration;
static ULONG g_DxgkMutations;
static ULONG g_HookAtMutation;
static VOID (*g_MutationHook)(VOID);

static HOST_KEY *
HostFindKey(PCWSTR Path)
{
    ULONG i;

    for (i = 0; i < g_KeyCount; i++)
    {
        if (g_Keys[i].Exists && wcscasecmp(g_Keys[i].Path, Path) == 0)
            return &g_Keys[i];
    }
    return NULL;
}

static HOST_KEY *
HostCreateKey(PCWSTR Path)
{
    HOST_KEY *Key = HostFindKey(Path);

    if (Key)
        return Key;
    Key = &g_Keys[g_KeyCount++];
    memset(Key, 0, sizeof(*Key));
    wcscpy(Key->Path, Path);
    Key->Exists = TRUE;
    return Key;
}

static BOOLEAN
HostParentExists(PCWSTR Path)
{
    WCHAR Parent[192];
    PWCHAR Slash;

    wcscpy(Parent, Path);
    Slash = wcsrchr(Parent, L'\\');
    if (!Slash)
        return FALSE;
    *Slash = 0;
    return HostFindKey(Parent) != NULL;
}

static VOID
HostMutation(VOID)
{
    if (!g_InDxgkRegistration)
        return;
    g_DxgkMutations++;
    if (g_MutationHook && g_DxgkMutations == g_HookAtMutation)
    {
        VOID (*Hook)(VOID) = g_MutationHook;
        BOOLEAN Saved = g_InDxgkRegistration;

        g_MutationHook = NULL;
        g_InDxgkRegistration = FALSE;
        Hook();
        g_InDxgkRegistration = Saved;
    }
}

static HOST_VALUE *
HostFindValue(HOST_KEY *Key, PCWSTR Name)
{
    ULONG i;

    for (i = 0; i < Key->ValueCount; i++)
    {
        if (wcscasecmp(Key->Values[i].Name, Name) == 0)
            return &Key->Values[i];
    }
    return NULL;
}

static NTSTATUS
HostSetValue(HOST_KEY *Key, PCWSTR Name, ULONG Type, const void *Data, ULONG Size)
{
    HOST_VALUE *Value = HostFindValue(Key, Name);

    if (Size > sizeof(Value->Data))
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!Value)
    {
        Value = &Key->Values[Key->ValueCount++];
        memset(Value, 0, sizeof(*Value));
        wcscpy(Value->Name, Name);
    }
    Value->Type = Type;
    Value->Size = Size;
    memcpy(Value->Data, Data, Size);
    HostMutation();
    return STATUS_SUCCESS;
}

static NTSTATUS
ZwCreateKey(PHANDLE KeyHandle, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes,
            ULONG TitleIndex, PUNICODE_STRING Class, ULONG Options, PULONG Disposition)
{
    WCHAR Path[192];
    HOST_KEY *Key;

    HostCopyName(Path, RTL_NUMBER_OF(Path), Attributes->ObjectName);
    Key = HostFindKey(Path);
    if (!Key)
    {
        if (!HostParentExists(Path))
            return STATUS_OBJECT_NAME_NOT_FOUND;
        Key = HostCreateKey(Path);
        HostMutation();
    }
    *KeyHandle = Key;
    return STATUS_SUCCESS;
}

static NTSTATUS
ZwOpenKey(PHANDLE KeyHandle, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes)
{
    WCHAR Path[192];
    HOST_KEY *Key;

    HostCopyName(Path, RTL_NUMBER_OF(Path), Attributes->ObjectName);
    Key = HostFindKey(Path);
    if (!Key)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    *KeyHandle = Key;
    return STATUS_SUCCESS;
}

static NTSTATUS
ZwClose(HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static NTSTATUS
ZwSetValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG TitleIndex,
              ULONG Type, PVOID Data, ULONG DataSize)
{
    WCHAR Name[64];

    HostCopyName(Name, RTL_NUMBER_OF(Name), ValueName);
    return HostSetValue((HOST_KEY *)KeyHandle, Name, Type, Data, DataSize);
}

static NTSTATUS
ZwQueryValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName, KEY_VALUE_INFORMATION_CLASS Class,
                PVOID Information, ULONG Length, PULONG ResultLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION Partial = Information;
    HOST_KEY *Key = KeyHandle;
    HOST_VALUE *Value;
    WCHAR Name[64];
    ULONG Needed;

    if (!Key->Exists)
        return STATUS_INVALID_HANDLE;
    HostCopyName(Name, RTL_NUMBER_OF(Name), ValueName);
    Value = HostFindValue(Key, Name);
    if (!Value)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    Needed = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + Value->Size;
    *ResultLength = Needed;
    if (Length < FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data))
        return STATUS_BUFFER_TOO_SMALL;
    Partial->TitleIndex = 0;
    Partial->Type = Value->Type;
    Partial->DataLength = Value->Size;
    if (Length < Needed)
        return STATUS_BUFFER_OVERFLOW;
    memcpy(Partial->Data, Value->Data, Value->Size);
    return STATUS_SUCCESS;
}

static HOST_KEY *
HostRelativeKey(ULONG RelativeTo, PCWSTR Path, BOOLEAN Create)
{
    WCHAR Full[192];

    if (RelativeTo == RTL_REGISTRY_HANDLE)
        return (HOST_KEY *)Path;
    swprintf(Full, RTL_NUMBER_OF(Full), L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\%ls", Path);
    if (!Create)
        return HostFindKey(Full);
    if (!HostFindKey(Full))
        HostMutation();
    return HostCreateKey(Full);
}

static NTSTATUS
RtlWriteRegistryValue(ULONG RelativeTo, PCWSTR Path, PCWSTR ValueName, ULONG Type, PVOID Data, ULONG Size)
{
    return HostSetValue(HostRelativeKey(RelativeTo, Path, TRUE), ValueName, Type, Data, Size);
}

static NTSTATUS
RtlDeleteRegistryValue(ULONG RelativeTo, PCWSTR Path, PCWSTR ValueName)
{
    HOST_KEY *Key = HostRelativeKey(RelativeTo, Path, FALSE);
    HOST_VALUE *Value;

    if (!Key || !(Value = HostFindValue(Key, ValueName)))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    *Value = Key->Values[--Key->ValueCount];
    HostMutation();
    return STATUS_SUCCESS;
}

static NTSTATUS
RtlQueryRegistryValues(ULONG RelativeTo, PCWSTR Path, PRTL_QUERY_REGISTRY_TABLE Table,
                       PVOID Context, PVOID Environment)
{
    HOST_KEY *Key = HostRelativeKey(RelativeTo, Path, FALSE);

    if (!Key)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    for (; Table->Name || Table->QueryRoutine; Table++)
    {
        HOST_VALUE *Value = HostFindValue(Key, Table->Name);

        if ((Table->Flags & RTL_QUERY_REGISTRY_DIRECT) && Value && Value->Type == REG_DWORD)
            memcpy(Table->EntryContext, Value->Data, sizeof(ULONG));
    }
    return STATUS_SUCCESS;
}


static PDEVICE_OBJECT g_Devices[16];
static ULONG g_DeviceCount;
static DRIVER_OBJECT g_DxgkDriverObject;
static DEVICE_OBJECT g_DxgkControlDevice = { &g_DxgkDriverObject };
static PDEVICE_OBJECT GDxgControlDeviceObject = &g_DxgkControlDevice;
static ULONG g_DeviceOpens;
static PFILE_OBJECT g_FileObjects[64];
static ULONG g_FileObjectCount;
static ULONG g_OpenFileObjects;

static VOID
ObDereferenceObject(PVOID Object)
{
    ULONG i;

    for (i = 0; i < g_FileObjectCount; i++)
    {
        if (g_FileObjects[i] == Object)
        {
            g_FileObjects[i] = NULL;
            g_OpenFileObjects--;
            free(Object);
            return;
        }
    }
}

static NTSTATUS
IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG ExtensionSize, PUNICODE_STRING Name,
               ULONG Type, ULONG Characteristics, BOOLEAN Exclusive, PDEVICE_OBJECT *Device)
{
    WCHAR Path[64];
    PDEVICE_OBJECT Object;
    ULONG i;

    HostCopyName(Path, RTL_NUMBER_OF(Path), Name);
    for (i = 0; i < g_DeviceCount; i++)
    {
        if (g_Devices[i] && wcscasecmp(g_Devices[i]->Name, Path) == 0)
            return STATUS_OBJECT_NAME_COLLISION;
    }
    Object = calloc(1, sizeof(*Object) + ExtensionSize);
    Object->DriverObject = DriverObject;
    Object->Flags = DO_DEVICE_INITIALIZING;
    Object->DeviceExtension = Object + 1;
    wcscpy(Object->Name, Path);
    g_Devices[g_DeviceCount++] = Object;
    *Device = Object;
    return STATUS_SUCCESS;
}

static VOID
IoDeleteDevice(PDEVICE_OBJECT Device)
{
    ULONG i;

    for (i = 0; i < g_DeviceCount; i++)
    {
        if (g_Devices[i] == Device)
            g_Devices[i] = NULL;
    }
}

static NTSTATUS
IoGetDeviceObjectPointer(PUNICODE_STRING Name, ACCESS_MASK Access, PFILE_OBJECT *File, PDEVICE_OBJECT *Device)
{
    WCHAR Path[64];
    ULONG i;

    HostCopyName(Path, RTL_NUMBER_OF(Path), Name);
    for (i = 0; i < g_DeviceCount; i++)
    {
        if (g_Devices[i] && wcscasecmp(g_Devices[i]->Name, Path) == 0)
        {
            PFILE_OBJECT Object = calloc(1, sizeof(*Object));
            Object->DeviceObject = g_Devices[i];
            *File = Object;
            *Device = g_Devices[i];
            g_DeviceOpens++;
            g_FileObjects[g_FileObjectCount++] = Object;
            g_OpenFileObjects++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static WCHAR g_AdapterDescription[64] = L"LiberNT Basic Display Adapter";

static NTSTATUS
IoGetDeviceProperty(PDEVICE_OBJECT Device, DEVICE_REGISTRY_PROPERTY Property, ULONG Length, PVOID Buffer, PULONG Result)
{
    ULONG Size = (ULONG)((wcslen(g_AdapterDescription) + 1) * sizeof(WCHAR));

    *Result = Size;
    if (Length < Size)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(Buffer, g_AdapterDescription, Size);
    return STATUS_SUCCESS;
}

static PDEVICE_OBJECT g_DisplayDeviceObject;
static PDXGKRNL_ADAPTER g_DisplayAdapter;
static ULONG g_DisplayDeviceNumber;
static BOOLEAN g_DisplayInitialModePending;
static PVOID g_DisplayWin32kPhysDisp;
static PVIDEO_WIN32K_CALLOUT g_DisplayWin32kCallout;

static VOID IoCompleteRequest(PIRP Irp, ULONG Boost);
static NTSTATUS NTAPI DxgkpDisplayDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);

static PIRP
IoBuildDeviceIoControlRequest(ULONG Code, PDEVICE_OBJECT Device, PVOID In, ULONG InLength,
                              PVOID Out, ULONG OutLength, BOOLEAN Internal, PKEVENT Event,
                              PIO_STATUS_BLOCK Iosb)
{
    PIRP Irp = calloc(1, sizeof(*Irp));
    ULONG Size = InLength > OutLength ? InLength : OutLength;

    Irp->AssociatedIrp.SystemBuffer = Size ? calloc(1, Size) : NULL;
    if (In && InLength)
        memcpy(Irp->AssociatedIrp.SystemBuffer, In, InLength);
    Irp->RequestorMode = KernelMode;
    Irp->UserBuffer = Out;
    Irp->UserBufferLength = OutLength;
    Irp->UserIosb = Iosb;
    Irp->Stack.Parameters.DeviceIoControl.IoControlCode = Code;
    Irp->Stack.Parameters.DeviceIoControl.InputBufferLength = InLength;
    Irp->Stack.Parameters.DeviceIoControl.OutputBufferLength = OutLength;
    return Irp;
}

static VOID
IoCompleteRequest(PIRP Irp, ULONG Boost)
{
    if (NT_SUCCESS(Irp->IoStatus.Status) && Irp->UserBuffer && Irp->IoStatus.Information)
        memcpy(Irp->UserBuffer, Irp->AssociatedIrp.SystemBuffer,
               min(Irp->IoStatus.Information, Irp->UserBufferLength));
    *Irp->UserIosb = Irp->IoStatus;
    free(Irp->AssociatedIrp.SystemBuffer);
    free(Irp);
}

static NTSTATUS
IoCallDriver(PDEVICE_OBJECT Device, PIRP Irp)
{
    if (Device != g_DisplayDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    return DxgkpDisplayDispatch(Device, Irp);
}


static LONGLONG g_NowMs;
static LONGLONG g_StartAtMs = -1;
static VOID (*g_StartEvent)(VOID);

static NTSTATUS
KeDelayExecutionThread(KPROCESSOR_MODE Mode, BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
    g_NowMs += -Interval->QuadPart / 10000;
    if (g_StartEvent && g_StartAtMs >= 0 && g_NowMs >= g_StartAtMs)
    {
        VOID (*Event)(VOID) = g_StartEvent;

        g_StartEvent = NULL;
        Event();
    }
    return STATUS_SUCCESS;
}


BOOL gbBaseVideo = FALSE;
static HDC ScreenDeviceContext;
static HDC hSystemBM;
static PMDEVOBJ gpmdev;
static SERVERINFO g_ServerInfo;
static PSERVERINFO gpsi = &g_ServerInfo;
static PROCESSINFO g_ProcessInfo;
static DESKTOP g_Desktop;
static ULONG g_DesktopShown;
static ULONG g_PdevCount;
static PDEVOBJ g_LastPdev;
static BOOLEAN g_FailPdevEnable;
static ULONG g_DcCount;

static VOID NTAPI VideoPortCallout(PVOID Params) { }
static NTSTATUS EngpUpdateMonitorDevices(PGRAPHICS_DEVICE Device) { return STATUS_SUCCESS; }
static HDC IntGdiCreateDC(PUNICODE_STRING Driver, PUNICODE_STRING Device, PVOID A, PVOID B, BOOL Display) { return (HDC)(ULONG_PTR)(0x1000 + ++g_DcCount); }
static BOOL IntPaintDesktop(HDC Dc) { return TRUE; }
static VOID GreSetDCOwner(HDC Dc, ULONG Owner) { }
static VOID IntDestroyPrimarySurface(VOID) { }
static BOOL IntCreatePrimarySurface(VOID) { return TRUE; }
static HDC NtGdiCreateCompatibleDC(HDC Dc) { return (HDC)(ULONG_PTR)0x2000; }
static HGDIOBJ NtGdiGetStockObject(INT Index) { return (HGDIOBJ)(ULONG_PTR)0x3000; }
static HFONT NtGdiSelectFont(HDC Dc, HGDIOBJ Font) { return NULL; }
static VOID InitMetrics(VOID) { }
static VOID UserUpdateMonitorSize(HDEV Device) { }
static VOID UserAttachMonitor(HDEV Device) { }
static INT NtGdiGetDeviceCaps(HDC Dc, INT Index) { return Index == BITSPIXEL ? 32 : (Index == PLANES ? 1 : (Index == LOGPIXELSY ? 96 : 0)); }
static INT IntGetCharDimensions(HDC Dc, TEXTMETRICW *Metrics, DWORD *Height) { *Height = 16; Metrics->tmHeight = 16; return 8; }
static VOID co_IntLoadDefaultCursors(VOID) { }
static VOID co_IntSetWndIcons(VOID) { }
static VOID MenuInit(VOID) { }
static PDESKTOP IntGetActiveDesktop(VOID) { return &g_Desktop; }
static VOID co_IntShowDesktop(PDESKTOP Desktop, ULONG Width, ULONG Height, BOOL Redraw) { g_DesktopShown++; }
static VOID IntHideDesktop(PDESKTOP Desktop) { }
static VOID GreDeleteObject(HGDIOBJ Object) { }
#define GDI_OBJ_HMGR_POWNED 1
static VOID InitSysParams(VOID) { }
static NTSTATUS UserCreateWinstaDirectory(VOID) { return STATUS_SUCCESS; }
static BOOL NtUserUpdatePerUserSystemParameters(DWORD Reserved, BOOL Enable) { return TRUE; }
static HBITMAP GreCreateBitmap(INT Width, INT Height, UINT Planes, UINT Bits, LPBYTE Data) { return (HBITMAP)(ULONG_PTR)0x4000; }
static HBRUSH IntGdiCreatePatternBrush(HBITMAP Bitmap) { return (HBRUSH)(ULONG_PTR)0x5000; }
static PVOID PsGetCurrentThreadWin32Thread(VOID) { return &g_ProcessInfo; }
static PPROCESSINFO PsGetCurrentProcessWin32Process(VOID) { return &g_ProcessInfo; }
static WORD UserGetSystemDpi(VOID) { return 96; }

static PPDEVOBJ
PDEVOBJ_Create(PGRAPHICS_DEVICE Device, PDEVMODEW Mode, ULONG Acceleration, ULONG Type)
{
    PPDEVOBJ Pdev;

    if (g_FailPdevEnable || !Mode)
        return NULL;
    Pdev = calloc(1, sizeof(*Pdev));
    Pdev->pGraphicsDevice = Device;
    Pdev->gdiinfo.ulHorzRes = Mode->dmPelsWidth;
    Pdev->gdiinfo.ulVertRes = Mode->dmPelsHeight;
    Pdev->BitsPerPel = Mode->dmBitsPerPel;
    Pdev->Frequency = Mode->dmDisplayFrequency;
    Pdev->cRefs = 1;
    g_LastPdev = *Pdev;
    g_PdevCount++;
    return Pdev;
}

static VOID PDEVOBJ_vReference(PPDEVOBJ Pdev) { Pdev->cRefs++; }
static VOID PDEVOBJ_vRelease(PPDEVOBJ Pdev) { if (--Pdev->cRefs == 0) free(Pdev); }
static BOOL PDEVOBJ_bDynamicModeChange(PPDEVOBJ Old, PPDEVOBJ New) { return TRUE; }
static BOOL PDEVOBJ_bDisableDisplay(PPDEVOBJ Pdev) { return TRUE; }
static BOOL MDEVOBJ_bDisable(PMDEVOBJ Mdev) { return TRUE; }
static VOID MDEVOBJ_vEnable(PMDEVOBJ Mdev) { }

static ULONG APIENTRY RcddGetModes(HANDLE, ULONG, DEVMODEW *);
static ULONG APIENTRY DrvGetModes(HANDLE, ULONG, DEVMODEW *);
static LDEVOBJ g_CddLdev;
static LDEVOBJ g_FramebufLdev;

static PLDEVOBJ
LDEVOBJ_pLoadDriver(LPWSTR Name, ULONG Type)
{
    g_CddLdev.pfn.GetModes = RcddGetModes;
    g_FramebufLdev.pfn.GetModes = DrvGetModes;
    if (wcscasecmp(Name, L"cdd") == 0)
        return &g_CddLdev;
    if (wcscasecmp(Name, L"framebuf") == 0)
        return &g_FramebufLdev;
    return NULL;
}

static VOID LDEVOBJ_vDereference(PLDEVOBJ Ldev) { }


static KMUTEX g_PostDisplayOwnershipMutex;
static LONG g_BootLogDisplay;
static PDXGKRNL_ADAPTER g_PostDisplayOwner;
static BOOLEAN g_CommitFails;
static ULONG g_InbvOwnershipLost;
static DXGKRNL_MINIPORT_CONTEXT g_BasicDisplayContext = { TRUE, FALSE };
static DEVICE_OBJECT g_BasicDisplayPdo;
static DXGKRNL_ADAPTER g_BasicDisplay;

static PDXGKRNL_ADAPTER DxgkpHandleToAdapter(HANDLE Handle) { return (PDXGKRNL_ADAPTER)Handle; }
static ULONGLONG DxgkpTraceNow100ns(VOID) { return 0; }
static ULONGLONG DxgkpTraceElapsedUs(ULONGLONG Start) { return 0; }
static PDXGKRNL_ADAPTER DxgkpReferencePostDisplayOwner(PDEVICE_OBJECT *Device) { *Device = NULL; return g_PostDisplayOwner; }
static NTSTATUS DxgkpRetainAndStopBasicDisplayFallback(PDXGKRNL_ADAPTER Claimant, PDXGKRNL_ADAPTER Owner, PDEVICE_OBJECT *Device, PDXGK_DISPLAY_INFORMATION Released, PBOOLEAN ByDriver) { *ByDriver = FALSE; return STATUS_SUCCESS; }
static VOID DxgkpReleasePostDisplayMapping(PDXGKRNL_ADAPTER Adapter) { Adapter->PostDisplayVirtualAddress = NULL; }
static PVOID MmMapIoSpace(PHYSICAL_ADDRESS Address, SIZE_T Size, MEMORY_CACHING_TYPE Type) { return (PVOID)(ULONG_PTR)0x7000000; }
static VOID InbvNotifyDisplayOwnershipLost(PVOID Callback) { g_InbvOwnershipLost++; }
static VOID DxgkpSetPostDisplayOwner(PDXGKRNL_ADAPTER Adapter) { g_PostDisplayOwner = Adapter; }
static VOID DxgkVidPnDestroyDisplayModeCache(PDXGKRNL_ADAPTER Adapter) { }
static NTSTATUS DxgkVidPnQueryVideoModes(PDXGKRNL_ADAPTER Adapter, PVIDEO_MODE_INFORMATION Modes, ULONG Count, PULONG Returned, BOOLEAN Current) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS DxgkpEnsureSharedDisplaySurfaces(PDXGKRNL_ADAPTER Adapter, ULONG Source) { return STATUS_SUCCESS; }

static NTSTATUS
DxgkDisplayCommitVidPn(PDXGKRNL_ADAPTER Adapter)
{
    if (g_CommitFails)
        return STATUS_UNSUCCESSFUL;
    Adapter->CommittedWidth = Adapter->PostDisplayWidth ? Adapter->PostDisplayWidth : 1024;
    Adapter->CommittedHeight = Adapter->PostDisplayHeight ? Adapter->PostDisplayHeight : 768;
    Adapter->VidPnCommitted = TRUE;
    return STATUS_SUCCESS;
}

static LOADER_PARAMETER_FRAMEBUFFER InbvGopFramebuffer;
static BOOLEAN InbvGopInfoValid = FALSE;


PGRAPHICS_DEVICE NTAPI EngpFindGraphicsDevice(PUNICODE_STRING pustrDevice, ULONG iDevNum);
PGRAPHICS_DEVICE NTAPI InitDisplayDriver(PWSTR pwszDeviceName, PWSTR pwszRegKey);
DWORD APIENTRY EngDeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD);
NTSTATUS EngpUpdateGraphicsDeviceList(VOID);
PMDEVOBJ MDEVOBJ_Create(PUNICODE_STRING pustrDeviceName, PDEVMODEW pdm);
VOID MDEVOBJ_vDestroy(PMDEVOBJ pmdev);
BOOL LDEVOBJ_bProbeAndCaptureDevmode(PGRAPHICS_DEVICE, PDEVMODEW, PDEVMODEW *, BOOL);
DWORD EngpGetDisplayDriverAccelerationLevel(PGRAPHICS_DEVICE pGraphicsDevice);
BOOL FASTCALL co_IntInitializeDesktopGraphics(VOID);
BOOL FASTCALL co_IntGraphicsCheck(BOOL Create);
NTSTATUS NTAPI InitVideo(VOID);
static VOID FASTCALL IntEndDesktopGraphics(VOID) { }

#include "w32deviceglobals.h"
#include "w32guiglobals.h"
#include "w32registry.h"
#include "w32device.h"
#include "w32display.h"
#include "w32ldev.h"
#include "w32mdev.h"
#include "w32pdev.h"
#include "w32winsta.h"
#include "w32guicheck.h"
#include "w32windc.h"
#include "w32ntuser.h"
#define DEVICE_NAME L"cdd"
#define ALLOC_TAG 0x20444443
#include "cddmodes.h"
#undef DEVICE_NAME
#undef ALLOC_TAG
#define DEVICE_NAME L"framebuf"
#define ALLOC_TAG 0x46554246
#include "fbmodes.h"
#undef DEVICE_NAME
#undef ALLOC_TAG
#include "ntinbv.h"
#include "dxgdisplay.h"
#include "dxgdispatch.h"
#include "dxgdriverentry.h"
#include "dxgpost.h"
#include "softgpupost.h"

static NTSTATUS APIENTRY
DxgkCbAcquirePostDisplayOwnership(HANDLE DeviceHandle, PDXGK_DISPLAY_INFORMATION DisplayInformation)
{
    return DxgkpAcquirePostDisplayOwnership(DeviceHandle, DisplayInformation, NULL);
}


typedef struct _GOP
{
    BOOLEAN Present;
    ULONG Width;
    ULONG Height;
    ULONG PixelsPerScanLine;
    ULONGLONG Base;
    ULONG Size;
} GOP;

static GOP g_Gop;
static NTSTATUS g_BasicDisplayStatus = STATUS_PENDING;
static NTSTATUS g_PostStatus;
static DXGK_DISPLAY_INFORMATION g_PostInfo;
static ULONGLONG g_PostVisible;
static NTSTATUS g_InitializeStatus = STATUS_PENDING;
static HDC g_FirstDc;
static BOOLEAN g_FirstDcRan;

static VOID
HostReset(VOID)
{
    memset(g_Keys, 0, sizeof(g_Keys));
    g_KeyCount = 0;
    HostCreateKey(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP");
    HostCreateKey(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\dxgkrnl");
    g_BasicDisplay.MiniportContext = &g_BasicDisplayContext;
    g_BasicDisplay.PhysicalDeviceObject = &g_BasicDisplayPdo;
}

static VOID
BootLoader(VOID)
{
    memset(&InbvGopFramebuffer, 0, sizeof(InbvGopFramebuffer));
    InbvGopInfoValid = g_Gop.Present;
    if (!g_Gop.Present)
        return;
    InbvGopFramebuffer.FrameBufferBase.QuadPart = (LONGLONG)g_Gop.Base;
    InbvGopFramebuffer.FrameBufferSize = g_Gop.Size;
    InbvGopFramebuffer.HorizontalResolution = g_Gop.Width;
    InbvGopFramebuffer.VerticalResolution = g_Gop.Height;
    InbvGopFramebuffer.PixelsPerScanLine = g_Gop.PixelsPerScanLine;
    InbvGopFramebuffer.PixelFormat = 32;
    InbvGopFramebuffer.RedMask = 0x00FF0000;
    InbvGopFramebuffer.GreenMask = 0x0000FF00;
    InbvGopFramebuffer.BlueMask = 0x000000FF;
    InbvGopFramebuffer.Reserved = 0xFF000000;
}

static VOID
DxgkrnlLoad(VOID)
{
    DxgkDriverEntryDeviceMap();
}

static VOID
BasicDisplayStart(VOID)
{
    DXGK_INTERFACE Interface = { sizeof(DXGK_INTERFACE), &g_BasicDisplay, DxgkCbAcquirePostDisplayOwnership };

    g_PostStatus = SoftGpuAcquirePostDisplay(&Interface, &g_PostInfo, &g_PostVisible);
    if (!NT_SUCCESS(g_PostStatus))
    {
        g_BasicDisplayStatus = g_PostStatus;
        return;
    }
    g_InDxgkRegistration = TRUE;
    g_BasicDisplayStatus = DxgkDisplayRegister(&g_BasicDisplay);
    g_InDxgkRegistration = FALSE;
}

static VOID
DxgkrnlLoadAndStart(VOID)
{
    DxgkrnlLoad();
    BasicDisplayStart();
}

static BOOLEAN
Guarded(VOID (*Step)(VOID))
{
    g_BugCheckArmed = TRUE;
    if (setjmp(g_BugCheckJump))
    {
        g_BugCheckArmed = FALSE;
        return FALSE;
    }
    Step();
    g_BugCheckArmed = FALSE;
    return TRUE;
}

static VOID CsrssUserInitialize(VOID) { g_InitializeStatus = UserInitialize(); }
static VOID FirstGetDC(VOID) { g_FirstDcRan = TRUE; g_FirstDc = DceCreateDisplayDC(); }

static ULONG
CountGraphicsDevices(PCWSTR NtName)
{
    PGRAPHICS_DEVICE Device;
    ULONG Count = 0;

    for (Device = gpGraphicsDeviceFirst; Device; Device = Device->pNextGraphicsDevice)
    {
        if (wcscasecmp(Device->szNtDeviceName, NtName) == 0)
            Count++;
    }
    return Count;
}


typedef enum _VERDICT { VerdictPass, VerdictFail, VerdictInfo } VERDICT;

static void
DumpLog(VOID)
{
    ULONG i;

    for (i = 0; i < g_LogCount; i++)
        printf("      | %s\n", g_Log[i]);
}

static BOOLEAN
LogContains(const char *Text)
{
    ULONG i;

    for (i = 0; i < g_LogCount; i++)
    {
        if (strstr(g_Log[i], Text))
            return TRUE;
    }
    return FALSE;
}

static VERDICT
Report(VERDICT Verdict, const char *Name, const char *Format, ...)
{
    static const char *Labels[] = { "PASS", "FAIL", "INFO" };
    va_list Arguments;

    printf("%s %s: ", Labels[Verdict], Name);
    va_start(Arguments, Format);
    vprintf(Format, Arguments);
    va_end(Arguments);
    printf("\n");
    if (Verdict == VerdictFail)
        DumpLog();
    return Verdict;
}

static void
DescribeOutcome(char *Out, size_t Size)
{
    if (g_BugCheckCode)
    {
        snprintf(Out, Size, "bugcheck 0x%X (0x%llx,0x%llx,0x%llx,0x%llx) after %s",
                 g_BugCheckCode,
                 (unsigned long long)g_BugCheckParameters[0],
                 (unsigned long long)g_BugCheckParameters[1],
                 (unsigned long long)g_BugCheckParameters[2],
                 (unsigned long long)g_BugCheckParameters[3],
                 g_FirstDcRan ? "first GetDC (DceCreateDisplayDC)" : "UserInitialize");
    }
    else
    {
        snprintf(Out, Size, "desktop %lux%lux%lu@%lu, %u graphics device(s) for \\Device\\Video0",
                 (unsigned long)g_LastPdev.gdiinfo.ulHorzRes,
                 (unsigned long)g_LastPdev.gdiinfo.ulVertRes,
                 (unsigned long)g_LastPdev.BitsPerPel,
                 (unsigned long)g_LastPdev.Frequency,
                 (unsigned)CountGraphicsDevices(L"\\Device\\Video0"));
    }
}

static BOOLEAN
DesktopIs(ULONG Width, ULONG Height)
{
    return !g_BugCheckCode && g_FirstDc && g_DesktopShown == 1 &&
           g_LastPdev.gdiinfo.ulHorzRes == Width && g_LastPdev.gdiinfo.ulVertRes == Height &&
           g_LastPdev.BitsPerPel == 32;
}


static const GOP g_Gen2Default = { TRUE, 1024, 768, 1024, 0xF8000000ULL, 8 * 1024 * 1024 };

static VERDICT
CaseModes(const char *Name, GOP Gop)
{
    char Outcome[200];
    ULONG Width = Gop.Present ? Gop.Width : 1024;
    ULONG Height = Gop.Present ? Gop.Height : 768;

    g_Gop = Gop;
    HostReset();
    BootLoader();
    DxgkrnlLoadAndStart();
    if (!NT_SUCCESS(g_BasicDisplayStatus))
        return Report(VerdictFail, Name, "BasicDisplay start failed 0x%08X (POST 0x%08X)",
                      (unsigned)g_BasicDisplayStatus, (unsigned)g_PostStatus);
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    if (!DesktopIs(Width, Height))
        return Report(VerdictFail, Name, "expected desktop %lux%lux32, got %s",
                      (unsigned long)Width, (unsigned long)Height, Outcome);
    return Report(VerdictPass, Name, "POST %ux%u pitch %u -> %s",
                  g_PostInfo.Width, g_PostInfo.Height, g_PostInfo.Pitch, Outcome);
}

static VERDICT
CaseStartDuringInitVideo(VOID)
{
    const char *Name = "adapter-registers-300ms-into-InitVideo-with-early-key";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    DxgkrnlLoad();
    g_StartAtMs = 300;
    g_StartEvent = BasicDisplayStart;
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    if (!DesktopIs(1024, 768) || g_NowMs != 300)
        return Report(VerdictFail, Name, "InitVideo returned 0x%08X after %lld ms -> %s",
                      (unsigned)g_InitializeStatus, (long long)g_NowMs, Outcome);
    return Report(VerdictPass, Name, "InitVideo waited %lld ms -> %s", (long long)g_NowMs, Outcome);
}

static VERDICT
CaseStartAfterInitVideo(VOID)
{
    const char *Name = "adapter-registers-after-InitVideo-gave-up";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    DxgkrnlLoad();
    if (!Guarded(CsrssUserInitialize))
    {
        DescribeOutcome(Outcome, sizeof(Outcome));
        return Report(VerdictFail, Name, "%s", Outcome);
    }
    BasicDisplayStart();
    Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    if (!DesktopIs(1024, 768))
        return Report(VerdictFail, Name,
                      "dxgkrnl loaded (empty DEVICEMAP\\VIDEO), InitVideo returned 0x%08X after %lld ms, "
                      "BasicDisplay registered 0x%08X before the first GetDC -> %s",
                      (unsigned)g_InitializeStatus, (long long)g_NowMs,
                      (unsigned)g_BasicDisplayStatus, Outcome);
    return Report(VerdictPass, Name, "InitVideo gave up after %lld ms -> %s", (long long)g_NowMs, Outcome);
}

static VERDICT
CaseStartAfterInitVideoWithoutKey(VOID)
{
    const char *Name = "adapter-starts-500ms-after-InitVideo-no-early-key";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    g_StartAtMs = 500;
    g_StartEvent = DxgkrnlLoadAndStart;
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    if (!DesktopIs(1024, 768))
        return Report(VerdictFail, Name, "%s", Outcome);
    return Report(VerdictPass, Name, "InitVideo waited %lld ms -> %s", (long long)g_NowMs, Outcome);
}

static ULONG
CountRegistrationMutations(VOID)
{
    pid_t Child;
    int Status;

    Child = fork();
    if (Child == 0)
    {
        g_Gop = g_Gen2Default;
        HostReset();
        BootLoader();
        DxgkrnlLoad();
        g_DxgkMutations = 0;
        BasicDisplayStart();
        _exit((int)g_DxgkMutations);
    }
    waitpid(Child, &Status, 0);
    return WIFEXITED(Status) ? (ULONG)WEXITSTATUS(Status) : 0;
}

static VERDICT
CaseInitVideoAtStep(ULONG Step, ULONG Total)
{
    char Name[64];
    char Outcome[200];
    BOOLEAN InitializeRan;

    snprintf(Name, sizeof(Name), "InitVideo-after-dxgkrnl-write-%u-of-%u", (unsigned)Step, (unsigned)Total);
    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    DxgkrnlLoad();
    g_DxgkMutations = 0;
    g_HookAtMutation = Step;
    g_MutationHook = CsrssUserInitialize;
    g_BugCheckArmed = TRUE;
    if (setjmp(g_BugCheckJump))
    {
        g_BugCheckArmed = FALSE;
        DescribeOutcome(Outcome, sizeof(Outcome));
        return Report(VerdictFail, Name, "%s", Outcome);
    }
    BasicDisplayStart();
    g_BugCheckArmed = FALSE;
    InitializeRan = g_InitializeStatus != STATUS_PENDING;
    Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    if (!InitializeRan || !DesktopIs(1024, 768))
        return Report(VerdictFail, Name, "%s", Outcome);
    if (LogContains("Failed to open registry key") || LogContains("Didn't find 'InstalledDisplayDrivers'"))
        return Report(VerdictFail, Name, "win32k saw \\Device\\Video0 published before its driver key was complete; %s", Outcome);
    return Report(VerdictPass, Name, "%s", Outcome);
}

static VERDICT
CaseDeviceListAfterFailedStart(VOID)
{
    const char *Name = "device-list-after-late-registration";
    BOOL Started;
    ULONG Before, After;

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    DxgkrnlLoad();
    if (!Guarded(CsrssUserInitialize))
        return Report(VerdictFail, Name, "UserInitialize bugchecked");
    BasicDisplayStart();
    Started = co_IntInitializeDesktopGraphics();
    Before = CountGraphicsDevices(L"\\Device\\Video0");
    EngpUpdateGraphicsDeviceList();
    EngpUpdateGraphicsDeviceList();
    After = CountGraphicsDevices(L"\\Device\\Video0");
    if (After != 1)
        return Report(VerdictFail, Name,
                      "desktop start %s; \\Device\\Video0 is linked %u time(s), %u after two "
                      "EnumDisplayDevices refreshes (first is named %ls), %u opens of \\Device\\Video0",
                      Started ? "succeeded" : "failed", (unsigned)Before, (unsigned)After,
                      gpGraphicsDeviceFirst ? gpGraphicsDeviceFirst->szWinDeviceName : L"-",
                      (unsigned)g_DeviceOpens);
    return Report(VerdictPass, Name, "one device after refreshes");
}

static ULONG
CountAllGraphicsDevices(VOID)
{
    PGRAPHICS_DEVICE Device;
    ULONG Count = 0;

    for (Device = gpGraphicsDeviceFirst; Device; Device = Device->pNextGraphicsDevice)
        Count += Device->pVgaDevice ? 2 : 1;
    return Count;
}

static BOOLEAN
PrimaryIsLinked(VOID)
{
    PGRAPHICS_DEVICE Device;

    if (!gpPrimaryGraphicsDevice)
        return TRUE;
    for (Device = gpGraphicsDeviceFirst; Device; Device = Device->pNextGraphicsDevice)
    {
        if (Device == gpPrimaryGraphicsDevice)
            return TRUE;
    }
    return FALSE;
}

static VERDICT
CaseEnableFailsReleasesDevice(VOID)
{
    const char *Name = "display-enable-fails-base-video-teardown";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    DxgkrnlLoadAndStart();
    g_FailPdevEnable = TRUE;
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    EngpUpdateGraphicsDeviceList();
    if (g_BugCheckCode != VIDEO_DRIVER_INIT_FAILURE || !PrimaryIsLinked() ||
        CountGraphicsDevices(L"\\Device\\Video0") != 1 ||
        g_OpenFileObjects != CountAllGraphicsDevices())
    {
        return Report(VerdictFail, Name,
                      "%s; primary %s, \\Device\\Video0 linked %u time(s), %u open file object(s) for %u device(s)",
                      Outcome, PrimaryIsLinked() ? "linked" : "dangling",
                      (unsigned)CountGraphicsDevices(L"\\Device\\Video0"),
                      (unsigned)g_OpenFileObjects, (unsigned)CountAllGraphicsDevices());
    }
    return Report(VerdictPass, Name, "%s; device list and references consistent", Outcome);
}

static VERDICT
CaseCommitFails(VOID)
{
    const char *Name = "BasicDisplay-registration-rolls-back";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    g_CommitFails = TRUE;
    DxgkrnlLoadAndStart();
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    return Report(VerdictInfo, Name, "DxgkDisplayRegister 0x%08X, DEVICEMAP\\VIDEO kept by DriverEntry -> %s",
                  (unsigned)g_BasicDisplayStatus, Outcome);
}

static VERDICT
CaseNoDisplayDriver(VOID)
{
    const char *Name = "no-display-driver-at-all";
    char Outcome[200];

    g_Gop = g_Gen2Default;
    HostReset();
    BootLoader();
    if (Guarded(CsrssUserInitialize))
        Guarded(FirstGetDC);
    DescribeOutcome(Outcome, sizeof(Outcome));
    return Report(VerdictInfo, Name, "InitVideo polled %lld ms -> %s", (long long)g_NowMs, Outcome);
}

typedef struct _CASE
{
    const char *Name;
    VERDICT (*Run)(ULONG Argument, ULONG Total);
    ULONG Argument;
} CASE;

static GOP g_Sweep[] =
{
    { TRUE, 1024, 768, 1024, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1024, 768, 1024, 0xF8000000ULL, 1024 * 768 * 4 },
    { TRUE, 1152, 864, 1152, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1280, 720, 1280, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1280, 1024, 1280, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1600, 900, 1600, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1920, 1080, 1920, 0xF8000000ULL, 8 * 1024 * 1024 },
    { TRUE, 1920, 1080, 2048, 0xF8000000ULL, 16 * 1024 * 1024 },
    { TRUE, 1024, 768, 1024, 0x1000000000ULL, 8 * 1024 * 1024 },
    { FALSE },
};

static VERDICT RunSweep(ULONG Index, ULONG Total)
{
    char Name[96];
    GOP Gop = g_Sweep[Index];

    if (Gop.Present)
        snprintf(Name, sizeof(Name), "modes-gop-%ux%u-scan%u-fb%uK-at-0x%llx",
                 (unsigned)Gop.Width, (unsigned)Gop.Height, (unsigned)Gop.PixelsPerScanLine,
                 (unsigned)(Gop.Size / 1024), (unsigned long long)Gop.Base);
    else
        snprintf(Name, sizeof(Name), "modes-no-gop-headless");
    return CaseModes(Name, Gop);
}

static VERDICT RunStartDuring(ULONG A, ULONG T) { return CaseStartDuringInitVideo(); }
static VERDICT RunStartAfter(ULONG A, ULONG T) { return CaseStartAfterInitVideo(); }
static VERDICT RunStartAfterNoKey(ULONG A, ULONG T) { return CaseStartAfterInitVideoWithoutKey(); }
static VERDICT RunStep(ULONG A, ULONG T) { return CaseInitVideoAtStep(A, T); }
static VERDICT RunDeviceList(ULONG A, ULONG T) { return CaseDeviceListAfterFailedStart(); }
static VERDICT RunCommitFails(ULONG A, ULONG T) { return CaseCommitFails(); }
static VERDICT RunEnableFails(ULONG A, ULONG T) { return CaseEnableFailsReleasesDevice(); }
static VERDICT RunNoDriver(ULONG A, ULONG T) { return CaseNoDisplayDriver(); }

static int
RunIsolated(VERDICT (*Run)(ULONG, ULONG), ULONG Argument, ULONG Total)
{
    pid_t Child;
    int Status;

    fflush(stdout);
    Child = fork();
    if (Child == 0)
    {
        VERDICT Verdict = Run(Argument, Total);
        fflush(stdout);
        _exit((int)Verdict);
    }
    waitpid(Child, &Status, 0);
    if (!WIFEXITED(Status))
    {
        printf("FAIL case crashed (signal %d)\n", WIFSIGNALED(Status) ? WTERMSIG(Status) : 0);
        return VerdictFail;
    }
    return WEXITSTATUS(Status);
}

int
main(int argc, char **argv)
{
    ULONG Counts[3] = { 0 };
    ULONG Total, i;
    int Verdict;

    printf("== Hyper-V Gen2 firmware framebuffer -> BasicDisplay -> win32k modes\n");
    for (i = 0; i < RTL_NUMBER_OF(g_Sweep); i++)
        Counts[RunIsolated(RunSweep, i, 0)]++;

    printf("== Boot ordering between BasicDisplay start and csrss InitVideo\n");
    Counts[RunIsolated(RunStartAfterNoKey, 0, 0)]++;
    Counts[RunIsolated(RunStartDuring, 0, 0)]++;
    Counts[RunIsolated(RunStartAfter, 0, 0)]++;
    Total = CountRegistrationMutations();
    for (i = 1; i <= Total; i++)
        Counts[RunIsolated(RunStep, i, Total)]++;
    Counts[RunIsolated(RunDeviceList, 0, 0)]++;
    Counts[RunIsolated(RunEnableFails, 0, 0)]++;

    printf("== Signature reference\n");
    Counts[RunIsolated(RunCommitFails, 0, 0)]++;
    Counts[RunIsolated(RunNoDriver, 0, 0)]++;

    printf("== %u passed, %u failed, %u informational\n",
           (unsigned)Counts[VerdictPass], (unsigned)Counts[VerdictFail], (unsigned)Counts[VerdictInfo]);
    return Counts[VerdictFail] ? 1 : 0;
}
