/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Enhanced Host Controller Interface
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/hidparse/hidparse.c
 * PURPOSE:     HID Parser
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "hidparse.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegPath)
{
    DPRINT("********* HID PARSE *********\n");
    return STATUS_SUCCESS;
}
