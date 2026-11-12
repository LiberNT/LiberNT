/*
 * Copyright (C) 2022 Mohamad Al-Jaf
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

#ifndef _WINUSB_H_
#define _WINUSB_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __REACTOS__
#include <winusbio.h>
#endif

typedef PVOID WINUSB_INTERFACE_HANDLE, *PWINUSB_INTERFACE_HANDLE;

BOOL WINAPI WinUsb_Free(WINUSB_INTERFACE_HANDLE);

#ifdef __REACTOS__
typedef PVOID WINUSB_ISOCH_BUFFER_HANDLE, *PWINUSB_ISOCH_BUFFER_HANDLE;

#pragma pack(push, 1)
typedef struct _WINUSB_SETUP_PACKET
{
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;
#pragma pack(pop)

#ifndef __USB_TIME_SYNC_DEFINED
#define __USB_TIME_SYNC_DEFINED
#pragma pack(push, 1)
typedef struct _USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION
{
    HANDLE TimeTrackingHandle;
    BOOLEAN IsStartupDelayTolerable;
} USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION
{
    HANDLE TimeTrackingHandle;
} USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION
{
    HANDLE TimeTrackingHandle;
    ULONG InputFrameNumber;
    ULONG InputMicroFrameNumber;
    LARGE_INTEGER QueryPerformanceCounterAtInputFrameOrMicroFrame;
    LARGE_INTEGER QueryPerformanceCounterFrequency;
    ULONG PredictedAccuracyInMicroSeconds;
    ULONG CurrentGenerationID;
    LARGE_INTEGER CurrentQueryPerformanceCounter;
    ULONG CurrentHardwareFrameNumber;
    ULONG CurrentHardwareMicroFrameNumber;
    ULONG CurrentUSBFrameNumber;
} USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION, *PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION;
#pragma pack(pop)
#endif

BOOL __stdcall WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle);
BOOL __stdcall WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AssociatedInterfaceIndex,
                                             PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle);
BOOL __stdcall WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR DescriptorType, UCHAR Index,
                                    USHORT LanguageID, PUCHAR Buffer, ULONG BufferLength,
                                    PULONG LengthTransferred);
BOOL __stdcall WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber,
                                             PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor);
BOOL __stdcall WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG InformationType,
                                             PULONG BufferLength, PVOID Buffer);
BOOL __stdcall WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR SettingNumber);
BOOL __stdcall WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle, PUCHAR SettingNumber);
BOOL __stdcall WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateInterfaceNumber,
                                UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION PipeInformation);
BOOL __stdcall WinUsb_QueryPipeEx(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR AlternateSettingNumber,
                                  UCHAR PipeIndex, PWINUSB_PIPE_INFORMATION_EX PipeInformationEx);
BOOL __stdcall WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType,
                                    ULONG ValueLength, PVOID Value);
BOOL __stdcall WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, ULONG PolicyType,
                                    PULONG ValueLength, PVOID Value);
BOOL __stdcall WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer,
                               ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer,
                                ULONG BufferLength, PULONG LengthTransferred, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket,
                                      PUCHAR Buffer, ULONG BufferLength, PULONG LengthTransferred,
                                      LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL __stdcall WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL __stdcall WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID);
BOOL __stdcall WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType, ULONG ValueLength,
                                     PVOID Value);
BOOL __stdcall WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle, ULONG PolicyType,
                                     PULONG ValueLength, PVOID Value);
BOOL __stdcall WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle, LPOVERLAPPED lpOverlapped,
                                          LPDWORD lpNumberOfBytesTransferred, BOOL bWait);
PUSB_INTERFACE_DESCRIPTOR __stdcall WinUsb_ParseConfigurationDescriptor(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor, PVOID StartPosition, LONG InterfaceNumber,
    LONG AlternateSetting, LONG InterfaceClass, LONG InterfaceSubClass, LONG InterfaceProtocol);
PUSB_COMMON_DESCRIPTOR __stdcall WinUsb_ParseDescriptors(PVOID DescriptorBuffer, ULONG TotalLength,
                                                          PVOID StartPosition, LONG DescriptorType);
BOOL __stdcall WinUsb_GetCurrentFrameNumber(WINUSB_INTERFACE_HANDLE InterfaceHandle, PULONG CurrentFrameNumber,
                                            LARGE_INTEGER *TimeStamp);
BOOL __stdcall WinUsb_GetAdjustedFrameNumber(PULONG CurrentFrameNumber, LARGE_INTEGER TimeStamp);
BOOL __stdcall WinUsb_RegisterIsochBuffer(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID, PUCHAR Buffer,
                                          ULONG BufferLength, PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle);
BOOL __stdcall WinUsb_UnregisterIsochBuffer(WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle);
BOOL __stdcall WinUsb_WriteIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                     PULONG FrameNumber, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_ReadIsochPipe(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                    PULONG FrameNumber, ULONG NumberOfPackets,
                                    PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_WriteIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                         BOOL ContinueStream, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_ReadIsochPipeAsap(WINUSB_ISOCH_BUFFER_HANDLE BufferHandle, ULONG Offset, ULONG Length,
                                        BOOL ContinueStream, ULONG NumberOfPackets,
                                        PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors, LPOVERLAPPED Overlapped);
BOOL __stdcall WinUsb_StartTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                               PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION StartTrackingInfo);
BOOL __stdcall WinUsb_GetCurrentFrameNumberAndQpc(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                                  PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameQpcInfo);
BOOL __stdcall WinUsb_StopTrackingForTimeSync(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                              PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopTrackingInfo);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WINUSB_H_ */
