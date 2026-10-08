/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the adapter's OpenCL driver builds and runs a kernel on the GPU
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

#define ITEM_COUNT 1024

#define APPSMOKE_CL_DEVICE_TYPE_GPU 4
#define APPSMOKE_CL_PLATFORM_NAME 0x0902
#define APPSMOKE_CL_DEVICE_NAME 0x102B
#define APPSMOKE_CL_PROGRAM_BUILD_LOG 0x1183
#define APPSMOKE_CL_MEM_READ_WRITE 1
#define APPSMOKE_CL_MEM_COPY_HOST_PTR 32

typedef int (WINAPI *CL_GET_PLATFORM_IDS)(UINT, void **, UINT *);
typedef int (WINAPI *CL_GET_PLATFORM_INFO)(void *, UINT, SIZE_T, void *, SIZE_T *);
typedef int (WINAPI *CL_GET_DEVICE_IDS)(void *, ULONGLONG, UINT, void **, UINT *);
typedef int (WINAPI *CL_GET_DEVICE_INFO)(void *, UINT, SIZE_T, void *, SIZE_T *);
typedef void *(WINAPI *CL_CREATE_CONTEXT)(const INT_PTR *, UINT, void **, void *, void *, int *);
typedef void *(WINAPI *CL_CREATE_COMMAND_QUEUE)(void *, void *, ULONGLONG, int *);
typedef void *(WINAPI *CL_CREATE_PROGRAM)(void *, UINT, const char **, const SIZE_T *, int *);
typedef int (WINAPI *CL_BUILD_PROGRAM)(void *, UINT, void **, const char *, void *, void *);
typedef int (WINAPI *CL_GET_BUILD_INFO)(void *, void *, UINT, SIZE_T, void *, SIZE_T *);
typedef void *(WINAPI *CL_CREATE_KERNEL)(void *, const char *, int *);
typedef void *(WINAPI *CL_CREATE_BUFFER)(void *, ULONGLONG, SIZE_T, void *, int *);
typedef int (WINAPI *CL_SET_KERNEL_ARG)(void *, UINT, SIZE_T, const void *);
typedef int (WINAPI *CL_ENQUEUE_KERNEL)(void *, void *, UINT, const SIZE_T *, const SIZE_T *, const SIZE_T *,
                                        UINT, void **, void **);
typedef int (WINAPI *CL_ENQUEUE_READ)(void *, void *, UINT, SIZE_T, SIZE_T, void *, UINT, void **, void **);
typedef int (WINAPI *CL_OBJECT_CALL)(void *);

static const char KernelSource[] =
    "__kernel void scale(__global int *data)\n"
    "{\n"
    "    size_t i = get_global_id(0);\n"
    "    data[i] = data[i] * 3 + 1;\n"
    "}\n";

START_TEST(opencl)
{
    CL_GET_PLATFORM_IDS GetPlatformIds;
    CL_GET_PLATFORM_INFO GetPlatformInfo;
    CL_GET_DEVICE_IDS GetDeviceIds;
    CL_GET_DEVICE_INFO GetDeviceInfo;
    CL_CREATE_CONTEXT CreateContext;
    CL_CREATE_COMMAND_QUEUE CreateQueue;
    CL_CREATE_PROGRAM CreateProgram;
    CL_BUILD_PROGRAM BuildProgram;
    CL_GET_BUILD_INFO GetBuildInfo;
    CL_CREATE_KERNEL CreateKernel;
    CL_CREATE_BUFFER CreateBuffer;
    CL_SET_KERNEL_ARG SetKernelArg;
    CL_ENQUEUE_KERNEL EnqueueKernel;
    CL_ENQUEUE_READ EnqueueRead;
    CL_OBJECT_CALL Finish, ReleaseMem, ReleaseKernel, ReleaseProgram, ReleaseQueue, ReleaseContext;
    void *Platforms[8], *Platform = NULL, *Device = NULL, *Context = NULL, *Queue = NULL;
    void *Program = NULL, *Kernel = NULL, *Buffer = NULL;
    static int Data[ITEM_COUNT], Result[ITEM_COUNT];
    const char *Source = KernelSource;
    SIZE_T Global = ITEM_COUNT;
    WCHAR DriverName[MAX_PATH];
    BOOL HasDriver = AppSmokeOpenClDriverName(DriverName, ARRAYSIZE(DriverName));
    HMODULE Module = LoadLibraryW(L"OpenCL.dll");
    UINT Count = 0, DeviceCount, Index, Wrong = 0;
    char Name[128], Log[400];
    DWORD Start;
    int Status;

    if (!Module)
    {
        ok(!HasDriver, "OpenCL.dll does not load although the adapter has the OpenCL driver %ls: %lu\n",
           DriverName, GetLastError());
        skip("OpenCL.dll is unavailable\n");
        return;
    }

    GetPlatformIds = (CL_GET_PLATFORM_IDS)GetProcAddress(Module, "clGetPlatformIDs");
    GetPlatformInfo = (CL_GET_PLATFORM_INFO)GetProcAddress(Module, "clGetPlatformInfo");
    GetDeviceIds = (CL_GET_DEVICE_IDS)GetProcAddress(Module, "clGetDeviceIDs");
    GetDeviceInfo = (CL_GET_DEVICE_INFO)GetProcAddress(Module, "clGetDeviceInfo");
    CreateContext = (CL_CREATE_CONTEXT)GetProcAddress(Module, "clCreateContext");
    CreateQueue = (CL_CREATE_COMMAND_QUEUE)GetProcAddress(Module, "clCreateCommandQueue");
    CreateProgram = (CL_CREATE_PROGRAM)GetProcAddress(Module, "clCreateProgramWithSource");
    BuildProgram = (CL_BUILD_PROGRAM)GetProcAddress(Module, "clBuildProgram");
    GetBuildInfo = (CL_GET_BUILD_INFO)GetProcAddress(Module, "clGetProgramBuildInfo");
    CreateKernel = (CL_CREATE_KERNEL)GetProcAddress(Module, "clCreateKernel");
    CreateBuffer = (CL_CREATE_BUFFER)GetProcAddress(Module, "clCreateBuffer");
    SetKernelArg = (CL_SET_KERNEL_ARG)GetProcAddress(Module, "clSetKernelArg");
    EnqueueKernel = (CL_ENQUEUE_KERNEL)GetProcAddress(Module, "clEnqueueNDRangeKernel");
    EnqueueRead = (CL_ENQUEUE_READ)GetProcAddress(Module, "clEnqueueReadBuffer");
    Finish = (CL_OBJECT_CALL)GetProcAddress(Module, "clFinish");
    ReleaseMem = (CL_OBJECT_CALL)GetProcAddress(Module, "clReleaseMemObject");
    ReleaseKernel = (CL_OBJECT_CALL)GetProcAddress(Module, "clReleaseKernel");
    ReleaseProgram = (CL_OBJECT_CALL)GetProcAddress(Module, "clReleaseProgram");
    ReleaseQueue = (CL_OBJECT_CALL)GetProcAddress(Module, "clReleaseCommandQueue");
    ReleaseContext = (CL_OBJECT_CALL)GetProcAddress(Module, "clReleaseContext");
    if (!GetPlatformIds || !GetPlatformInfo || !GetDeviceIds || !GetDeviceInfo || !CreateContext || !CreateQueue ||
        !CreateProgram || !BuildProgram || !GetBuildInfo || !CreateKernel || !CreateBuffer || !SetKernelArg ||
        !EnqueueKernel || !EnqueueRead || !Finish || !ReleaseMem || !ReleaseKernel || !ReleaseProgram ||
        !ReleaseQueue || !ReleaseContext)
    {
        ok(0, "OpenCL.dll lacks a core entry point\n");
        return;
    }

    Start = GetTickCount();
    Status = GetPlatformIds(ARRAYSIZE(Platforms), Platforms, &Count);
    for (Index = 0; Status == 0 && Index < Count && Index < ARRAYSIZE(Platforms) && !Device; ++Index)
    {
        DeviceCount = 0;
        if (GetDeviceIds(Platforms[Index], APPSMOKE_CL_DEVICE_TYPE_GPU, 1, &Device, &DeviceCount) == 0 && DeviceCount)
            Platform = Platforms[Index];
        else
            Device = NULL;
    }
    if (!Device)
    {
        ok(!HasDriver, "No OpenCL GPU device although the adapter has the OpenCL driver %ls: status %d, %u platforms\n",
           DriverName, Status, Count);
        skip("No OpenCL GPU device\n");
        return;
    }

    Name[0] = 0;
    GetPlatformInfo(Platform, APPSMOKE_CL_PLATFORM_NAME, sizeof(Name) - 1, Name, NULL);
    trace("Platform %s, found in %lu ms\n", Name, GetTickCount() - Start);
    Name[0] = 0;
    GetDeviceInfo(Device, APPSMOKE_CL_DEVICE_NAME, sizeof(Name) - 1, Name, NULL);
    trace("Device %s\n", Name);

    Context = CreateContext(NULL, 1, &Device, NULL, NULL, &Status);
    ok(Context != NULL && Status == 0, "clCreateContext: %d\n", Status);
    if (Context)
    {
        Queue = CreateQueue(Context, Device, 0, &Status);
        ok(Queue != NULL && Status == 0, "clCreateCommandQueue: %d\n", Status);
        Program = CreateProgram(Context, 1, &Source, NULL, &Status);
        ok(Program != NULL && Status == 0, "clCreateProgramWithSource: %d\n", Status);
    }

    if (Queue && Program)
    {
        Start = GetTickCount();
        Status = BuildProgram(Program, 1, &Device, NULL, NULL, NULL);
        ok(Status == 0, "clBuildProgram: %d\n", Status);
        trace("Kernel build took %lu ms\n", GetTickCount() - Start);
        if (Status != 0)
        {
            memset(Log, 0, sizeof(Log));
            GetBuildInfo(Program, Device, APPSMOKE_CL_PROGRAM_BUILD_LOG, sizeof(Log) - 1, Log, NULL);
            trace("Build log: %s\n", Log);
        }
        else
        {
            Kernel = CreateKernel(Program, "scale", &Status);
            ok(Kernel != NULL && Status == 0, "clCreateKernel: %d\n", Status);
        }
    }

    if (Kernel)
    {
        for (Index = 0; Index < ITEM_COUNT; ++Index)
            Data[Index] = (int)Index;
        Buffer = CreateBuffer(Context, APPSMOKE_CL_MEM_READ_WRITE | APPSMOKE_CL_MEM_COPY_HOST_PTR,
                              sizeof(Data), Data, &Status);
        ok(Buffer != NULL && Status == 0, "clCreateBuffer: %d\n", Status);
    }

    if (Buffer)
    {
        Start = GetTickCount();
        Status = SetKernelArg(Kernel, 0, sizeof(Buffer), &Buffer);
        ok(Status == 0, "clSetKernelArg: %d\n", Status);
        Status = EnqueueKernel(Queue, Kernel, 1, NULL, &Global, NULL, 0, NULL, NULL);
        ok(Status == 0, "clEnqueueNDRangeKernel: %d\n", Status);
        Status = Finish(Queue);
        ok(Status == 0, "clFinish: %d\n", Status);
        Status = EnqueueRead(Queue, Buffer, 1, 0, sizeof(Result), Result, 0, NULL, NULL);
        ok(Status == 0, "clEnqueueReadBuffer: %d\n", Status);
        for (Index = 0; Status == 0 && Index < ITEM_COUNT; ++Index)
        {
            if (Result[Index] != (int)Index * 3 + 1)
                ++Wrong;
        }
        ok(Wrong == 0, "%u of %u kernel results are wrong\n", Wrong, ITEM_COUNT);
        trace("Kernel run and readback took %lu ms\n", GetTickCount() - Start);
    }

    if (Buffer)
        ReleaseMem(Buffer);
    if (Kernel)
        ReleaseKernel(Kernel);
    if (Program)
        ReleaseProgram(Program);
    if (Queue)
        ReleaseQueue(Queue);
    if (Context)
        ReleaseContext(Context);
}
