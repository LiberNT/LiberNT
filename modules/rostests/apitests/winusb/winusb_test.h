/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared helpers for the WinUSB tests on the QEMU usb-winusb device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <apitest.h>

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>
#include <initguid.h>

DEFINE_GUID(GUID_WINUSB_TEST, 0x5d4a3d8e, 0x91f2, 0x4c6b, 0x8e, 0x1a, 0x7b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60);
DEFINE_GUID(GUID_WINUSB_TEST2, 0x5d4a3d8e, 0x91f2, 0x4c6b, 0x8e, 0x1a, 0x7b, 0x2c, 0x3d, 0x4e, 0x5f, 0x61);

#define TEST_VID 0x46F4
#define TEST_PID 0x0010
#define TEST_PID_COMPOSITE 0x0011

#define PIPE_LOOP_OUT 0x01
#define PIPE_LOOP_IN 0x81
#define PIPE_INTR_IN 0x82
#define PIPE_HELD_IN 0x83
#define PIPE_LOOP2_OUT 0x04
#define PIPE_LOOP2_IN 0x84

#define REQ_ECHO 0x01
#define REQ_STORE 0x02
#define REQ_LOAD 0x03
#define REQ_RELEASE 0x10
#define REQ_NOTIFY 0x11
#define REQ_HALT 0x12
#define REQ_STATUS 0x13
#define REQ_RESET 0x14
#define REQ_IFACE 0x21
#define REQ_ENDPOINT 0x22
#define REQ_STALL 0x7F

#define TEST_EPS 16
#define TEST_TIMEOUT_ERROR 0xFFFFFFFF

#pragma pack(push, 1)
typedef struct _DEVICE_STATUS
{
    ULONG Magic;
    ULONG OutTransfers[TEST_EPS];
    ULONG OutBytes[TEST_EPS];
    ULONG OutZlps[TEST_EPS];
    ULONG InTransfers[TEST_EPS];
    ULONG InQueuedBytes[TEST_EPS];
    ULONG InQueuedPackets[TEST_EPS];
    ULONG InCancelled[TEST_EPS];
    ULONG HaltMask;
    ULONG ClearHalts;
    ULONG SetInterfaces;
    ULONG Alt0;
    ULONG Released;
    ULONG Resets;
    ULONG InPendingMask;
    ULONG InLastSize[TEST_EPS];
    ULONG InPrevSize[TEST_EPS];
    ULONG OutLastSize[TEST_EPS];
} DEVICE_STATUS, *PDEVICE_STATUS;
#pragma pack(pop)

typedef struct _TEST_DEVICE
{
    HANDLE File;
    WINUSB_INTERFACE_HANDLE Interface;
    WCHAR Path[512];
} TEST_DEVICE, *PTEST_DEVICE;

BOOL FindInterfacePath(const GUID *Guid, ULONG Index, PWSTR Path, ULONG PathChars);
BOOL OpenTestDevice(const GUID *Guid, PTEST_DEVICE Device);
VOID CloseTestDevice(PTEST_DEVICE Device);
BOOL VendorOut(WINUSB_INTERFACE_HANDLE Interface, UCHAR Request, USHORT Value, USHORT Index, PVOID Data,
               USHORT Length);
BOOL VendorIn(WINUSB_INTERFACE_HANDLE Interface, UCHAR Request, USHORT Value, USHORT Index, PVOID Data,
              USHORT Length, PULONG Transferred);
BOOL GetDeviceStatus(WINUSB_INTERFACE_HANDLE Interface, PDEVICE_STATUS Status);
BOOL ResetDevice(WINUSB_INTERFACE_HANDLE Interface);
DWORD ReadWait(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, PVOID Buffer, ULONG Length,
               PULONG Transferred, DWORD TimeoutMs);
DWORD WriteWait(PTEST_DEVICE Device, WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, PVOID Buffer, ULONG Length,
                PULONG Transferred, DWORD TimeoutMs);
BOOL SetPolicyBool(WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, ULONG Policy, UCHAR Value);
BOOL SetPolicyUlong(WINUSB_INTERFACE_HANDLE Interface, UCHAR Pipe, ULONG Policy, ULONG Value);
VOID FillPattern(PUCHAR Buffer, ULONG Length, ULONG Seed);
BOOL CheckPattern(const UCHAR *Buffer, ULONG Length, ULONG Seed);
ULONGLONG NowMs(VOID);
