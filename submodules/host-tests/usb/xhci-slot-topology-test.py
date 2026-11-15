#!/usr/bin/env python3
# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     xHCI default-pipe topology lookup never takes the slot of a device that still has a USB address
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root) / 'drivers/usb/usbxhci'

source = (root / 'usbxhci.c').read_text()
function = re.search(r'(?ms)^static PXHCI_DEVICE_SLOT\nXHCI_FindSlotByTopology\(.*?^\}\n', source)
if not function:
    raise RuntimeError('XHCI_FindSlotByTopology not found in usbxhci.c')

hardware = (root / 'hardware.h').read_text()
constants = ''
for name in ('XHCI_MAX_SLOTS', 'XHCI_SLOT_ROUTE_MASK'):
    match = re.search(r'(?m)^#define ' + name + r'\s+(\S+)', hardware)
    if not match:
        raise RuntimeError(name + ' not found in hardware.h')
    constants += f'#define {name} {match.group(1)}\n'

fixture = constants + r'''
#include <stdio.h>
#include <string.h>
typedef unsigned char UCHAR, BOOLEAN;
typedef unsigned short USHORT;
typedef unsigned int ULONG;
#define TRUE 1
#define FALSE 0
#define _In_
typedef struct { ULONG RouteString; USHORT RootPortNumber; USHORT PortNumber; } USBPORT_ENDPOINT_PROPERTIES, *PUSBPORT_ENDPOINT_PROPERTIES;
typedef struct { BOOLEAN InUse; UCHAR UsbDeviceAddress; UCHAR RootPortNumber; ULONG RouteString; } XHCI_DEVICE_SLOT, *PXHCI_DEVICE_SLOT;
typedef struct { ULONG MaxSlots; XHCI_DEVICE_SLOT DeviceSlots[XHCI_MAX_SLOTS + 1]; } XHCI_EXTENSION, *PXHCI_EXTENSION;
''' + function.group(0) + r'''
static XHCI_EXTENSION Extension;
static int Failures;

static void Place(ULONG Slot, UCHAR Address, UCHAR RootPort, ULONG Route)
{
    Extension.DeviceSlots[Slot].InUse = TRUE;
    Extension.DeviceSlots[Slot].UsbDeviceAddress = Address;
    Extension.DeviceSlots[Slot].RootPortNumber = RootPort;
    Extension.DeviceSlots[Slot].RouteString = Route;
}

static void Expect(const char *Case, ULONG Expected)
{
    USBPORT_ENDPOINT_PROPERTIES Properties = { 0x4, 1, 4 };
    PXHCI_DEVICE_SLOT Slot = XHCI_FindSlotByTopology(&Extension, &Properties);
    ULONG Found = Slot ? (ULONG)(Slot - Extension.DeviceSlots) : 0;
    if (Found != Expected)
    {
        printf("%s: slot %u, expected %u\n", Case, Found, Expected);
        Failures++;
    }
}

int main(void)
{
    Extension.MaxSlots = 32;

    Place(16, 7, 1, 0x4);
    Expect("addressed device still owning hub port 4", 0);

    Place(17, 0, 1, 0x4);
    Expect("replacement device enumerating behind it", 17);

    memset(&Extension.DeviceSlots, 0, sizeof(Extension.DeviceSlots));
    Place(5, 0, 1, 0x4);
    Expect("slot reset to the default address", 5);

    memset(&Extension.DeviceSlots, 0, sizeof(Extension.DeviceSlots));
    Place(3, 0, 1, 0);
    Place(4, 0, 2, 0x4);
    Expect("other bus positions", 0);

    if (Failures)
        return 1;
    printf("xhci slot topology: 4 checks passed\n");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory)
    (path / 'topology.c').write_text(fixture)
    result = subprocess.run([args.cc, '-std=gnu11', '-Wall', '-o', str(path / 'topology'), str(path / 'topology.c')],
                            capture_output=True, text=True)
    if result.returncode:
        print(result.stderr)
        sys.exit(1)
    sys.exit(subprocess.run([str(path / 'topology')]).returncode)
