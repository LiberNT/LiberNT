/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PS/2 port test environment
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */
#ifndef I8042_TEST_SHIM_H
#define I8042_TEST_SHIM_H
#define _I8042PRT_PCH_
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define IN
#define OUT
#define OPTIONAL
#define NTAPI
#define TRUE 1
#define FALSE 0
#define ASSERT(x) assert(x)
#define UNREFERENCED_PARAMETER(x) (void)(x)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xc0000001u)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xc00000b5u)
#define NT_SUCCESS(x) ((x) >= 0)
#define KBD_OBF 0x01
#define KBD_IBF 0x02
#define MOU_OBF 0x20
#define KBD_PERR 0x80
#define KBD_ACK 0xfa
#define KBD_RESEND 0xfe
#define MOUSE_DISAB 0xa7
typedef void VOID;
typedef void *PVOID;
typedef uint8_t UCHAR, *PUCHAR, BOOLEAN, KIRQL;
typedef uint32_t ULONG;
typedef int32_t NTSTATUS;
typedef void *PKINTERRUPT;
typedef struct { struct { NTSTATUS Status; } IoStatus; } IRP, *PIRP;
typedef void *PDEVICE_OBJECT;
#define IO_NO_INCREMENT 0
VOID IoCompleteRequest(PIRP Irp, int Increment);
VOID IoStartNextPacket(PDEVICE_OBJECT Device, BOOLEAN Cancelable);
typedef struct { ULONG PollingIterations, ResendIterations; } I8042_SETTINGS;
typedef struct { PUCHAR DataPort, ControlPort; I8042_SETTINGS Settings; PKINTERRUPT HighestDIRQLInterrupt; PIRP CurrentIrp; PDEVICE_OBJECT CurrentIrpDevice; } PORT_DEVICE_EXTENSION, *PPORT_DEVICE_EXTENSION;
UCHAR TestReadPort(PUCHAR Port);
VOID TestWritePort(PUCHAR Port, UCHAR Value);
VOID KeStallExecutionProcessor(ULONG Time);
KIRQL KeAcquireInterruptSpinLock(PKINTERRUPT Interrupt);
VOID KeReleaseInterruptSpinLock(PKINTERRUPT Interrupt, KIRQL OldIrql);
#define READ_PORT_UCHAR(p) TestReadPort(p)
#define WRITE_PORT_UCHAR(p, v) TestWritePort(p, v)
NTSTATUS i8042ReadData(PPORT_DEVICE_EXTENSION DeviceExtension, UCHAR StatusFlags, PUCHAR Data);
NTSTATUS i8042ReadStatus(PPORT_DEVICE_EXTENSION DeviceExtension, PUCHAR Status);
NTSTATUS i8042ReadDataWait(PPORT_DEVICE_EXTENSION DeviceExtension, PUCHAR Data);
BOOLEAN i8042Write(PPORT_DEVICE_EXTENSION DeviceExtension, PUCHAR Port, UCHAR Data);
BOOLEAN i8042QuiesceMouse(PPORT_DEVICE_EXTENSION DeviceExtension);
VOID i8042FinishPacket(PPORT_DEVICE_EXTENSION DeviceExtension, NTSTATUS Result);
VOID i8042FailPacket(PDEVICE_OBJECT DeviceObject, PIRP Irp, NTSTATUS Result);
#define i8042ReadKeyboardData(d, p) i8042ReadData(d, KBD_OBF, p)
#endif
