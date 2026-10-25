/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         HidParser description test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>
#include <hidpddi.h>

#define NDEBUG
#include <debug.h>

#include "HidP.h"

static UCHAR ExampleKeyboardDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop), */
    0x09, 0x06,       /* Usage (Keyboard), */
    0xA1, 0x01,       /* Collection (Application), */
    0x05, 0x07,       /*   Usage Page (Key Codes); */
    0x19, 0xE0,       /*   Usage Minimum (224), */
    0x29, 0xE7,       /*   Usage Maximum (231), */
    0x15, 0x00,       /*   Logical Minimum (0), */
    0x25, 0x01,       /*   Logical Maximum (1), */
    0x75, 0x01,       /*   Report Size (1), */
    0x95, 0x08,       /*   Report Count (8), */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute), ;Modifier byte */
    0x95, 0x01,       /*   Report Count (1), */
    0x75, 0x08,       /*   Report Size (8), */
    0x81, 0x01,       /*   Input (Constant), ;Reserved byte */
    0x95, 0x05,       /*   Report Count (5), */
    0x75, 0x01,       /*   Report Size (1), */
    0x05, 0x08,       /*   Usage Page (Page# for LEDs), */
    0x19, 0x01,       /*   Usage Minimum (1), */
    0x29, 0x05,       /*   Usage Maximum (5), */
    0x91, 0x02,       /*   Output (Data, Variable, Absolute), ;LED report */
    0x95, 0x01,       /*   Report Count (1), */
    0x75, 0x03,       /*   Report Size (3), */
    0x91, 0x01,       /*   Output (Constant), ;LED report padding */
    0x95, 0x06,       /*   Report Count (6), */
    0x75, 0x08,       /*   Report Size (8), */
    0x15, 0x00,       /*   Logical Minimum (0), */
    0x25, 0x65,       /*   Logical Maximum (101), */
    0x05, 0x07,       /*   Usage Page (Key Codes), */
    0x19, 0x00,       /*   Usage Minimum (0), */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data, Array), ;Key arrays (6 bytes) */
    0xC0              /* End Collection */
};

static UCHAR PowerProEliteDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop), */
    0x09, 0x04,       /* Usage (Joystick), */
    0xa1, 0x01,       /* Collection (Application), */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0x01,       /*     Report ID (1) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x01,       /*     Report Count (1), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x26, 0xff, 0x00, /*     Logical Maximum (255), */
    0x81, 0x03,       /*     Input (Constant, Variable, Absolute), */
    0x75, 0x01,       /*     Report Size (1), */
    0x95, 0x13,       /*     Report Count (19), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x25, 0x01,       /*     Logical Maximum (1), */
    0x35, 0x00,       /*     Physical Minimum (0), */
    0x45, 0x01,       /*     Physical Maximum (1), */
    0x05, 0x09,       /*     Usage Page (Button), */
    0x19, 0x01,       /*     Usage Minimum (1), */
    0x29, 0x13,       /*     Usage Maximum (19), */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute), */
    0x75, 0x01,       /*     Report Size (1), */
    0x95, 0x0d,       /*     Report Count (13), */
    0x06, 0x00, 0xff, /*     Usage Page (Vendor-defined FF00), */
    0x81, 0x03,       /*     Input (Constant, Variable, Absolute), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x26, 0xff, 0x00, /*     Logical Maximum (255), */
    0x05, 0x01,       /*     Usage Page (Generic Desktop), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xa1, 0x00,       /*     Collection (Physical), */
    0x75, 0x08,       /*       Report Size (8), */
    0x95, 0x04,       /*       Report Count (4), */
    0x35, 0x00,       /*       Physical Minimum (0), */
    0x46, 0xff, 0x00, /*       Physical Maximum (255), */
    0x09, 0x30,       /*       Usage (X), */
    0x09, 0x31,       /*       Usage (Y), */
    0x09, 0x32,       /*       Usage (Z), */
    0x09, 0x35,       /*       Usage (Rz), */
    0x81, 0x02,       /*       Input (Data, Variable, Absolute), */
    0xc0,             /*     End Collection */
    0x05, 0x01,       /*     Usage Page (Generic Desktop), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x27,       /*     Report Count (39), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */

    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0x02,       /*     Report ID (2) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0xee,       /*     Report ID (238) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0xef,       /*     Report ID (239) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xc0,             /* End Collection */
};
C_ASSERT(sizeof(PowerProEliteDescriptor) == 148);

static UCHAR LargeReportCountDescriptor[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop), */
    0x09, 0x06,                   /* Usage (Keyboard), */
    0xa1, 0x01,                   /* Collection (Application), */
    0x75, 0x01,                   /*   Report Size (1), */
    0x97, 0xec, 0x04, 0x00, 0x00, /*   Report Count (1260), */
    0x81, 0x02,                   /*   Input (Data, Variable, Absolute), */
    0xc0                          /* End Collection */
};
C_ASSERT(sizeof(LargeReportCountDescriptor) == 16);

static UCHAR OverflowReportCountDescriptor[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop), */
    0x09, 0x06,                   /* Usage (Keyboard), */
    0xa1, 0x01,                   /* Collection (Application), */
    0x75, 0x01,                   /*   Report Size (1), */
    0x97, 0xff, 0xff, 0xff, 0xff, /*   Report Count (UINT32_MAX), */
    0x81, 0x02,                   /*   Input (Data, Variable, Absolute), */
    0xc0                          /* End Collection */
};
C_ASSERT(sizeof(OverflowReportCountDescriptor) == 16);

static UCHAR SignedAxisDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop), */
    0x09, 0x05,       /* Usage (Game Pad), */
    0xa1, 0x01,       /* Collection (Application), */
    0x85, 0x02,       /*   Report ID (2), */
    0x09, 0x30,       /*   Usage (X), */
    0x16, 0x00, 0x80, /*   Logical Minimum (-32768), */
    0x26, 0xff, 0x7f, /*   Logical Maximum (32767), */
    0x36, 0x00, 0x80, /*   Physical Minimum (-32768), */
    0x46, 0xff, 0x7f, /*   Physical Maximum (32767), */
    0x75, 0x10,       /*   Report Size (16), */
    0x95, 0x01,       /*   Report Count (1), */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute), */
    0xc0              /* End Collection */
};
C_ASSERT(sizeof(SignedAxisDescriptor) == 29);

#define LAYOUT_HEADER 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01
#define LAYOUT_USAGES 0x05, 0x07, 0x19, 0x00, 0x29, 0x01

static const struct
{
    const char *Name;
    UCHAR Descriptor[40];
    UCHAR Length;
    NTSTATUS Status;
    USHORT Input, Output, Feature;
    USHORT InputButtons, InputValues, OutputButtons, OutputValues, FeatureButtons, FeatureValues;
} ReportLayouts[] =
{
    { "data without usage", { LAYOUT_HEADER, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xc0 }, 13, STATUS_COULD_NOT_INTERPRET },
    { "constant without usage", { LAYOUT_HEADER, 0x75, 0x01, 0x95, 0x08, 0x81, 0x01, 0xc0 }, 13, STATUS_SUCCESS, 2 },
    { "output without usage", { LAYOUT_HEADER, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0xc0 }, 13, STATUS_COULD_NOT_INTERPRET },
    { "feature without usage", { LAYOUT_HEADER, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x02, 0xc0 }, 13, STATUS_COULD_NOT_INTERPRET },
    { "8 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xc0 }, 19, STATUS_SUCCESS, 2, 0, 0, 1 },
    { "255 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0xff, 0x81, 0x02, 0xc0 }, 19, STATUS_COULD_NOT_INTERPRET },
    { "256 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0x00, 0x01, 0x81, 0x02, 0xc0 }, 20, STATUS_SUCCESS, 33, 0, 0, 1 },
    { "1023 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0xff, 0x03, 0x81, 0x02, 0xc0 }, 20, STATUS_COULD_NOT_INTERPRET },
    { "1024 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0x00, 0x04, 0x81, 0x02, 0xc0 }, 20, STATUS_SUCCESS, 129, 0, 0, 1 },
    { "4096 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0x00, 0x10, 0x81, 0x02, 0xc0 }, 20, STATUS_SUCCESS, 513, 0, 0, 1 },
    { "65535 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0xff, 0xff, 0x81, 0x02, 0xc0 }, 20, STATUS_COULD_NOT_INTERPRET },
    { "255 bytes", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x08, 0x95, 0xff, 0x81, 0x02, 0xc0 }, 19, STATUS_SUCCESS, 256, 0, 0, 0, 1 },
    { "two 4 bit items", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, LAYOUT_USAGES, 0x95, 0x04, 0x81, 0x02, 0xc0 }, 29, STATUS_SUCCESS, 2, 0, 0, 2 },
    { "4 bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 0xc0 }, 19, STATUS_COULD_NOT_INTERPRET },
    { "4 bits and constant padding", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 0x95, 0x04, 0x81, 0x01, 0xc0 }, 23, STATUS_SUCCESS, 2, 0, 0, 1 },
    { "7 input bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x07, 0x81, 0x02, LAYOUT_USAGES, 0x95, 0x08, 0x91, 0x02, 0xc0 }, 29, STATUS_COULD_NOT_INTERPRET },
    { "7 output bits", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, LAYOUT_USAGES, 0x95, 0x07, 0x91, 0x02, 0xc0 }, 29, STATUS_COULD_NOT_INTERPRET },
    { "4 bits in two reports", { LAYOUT_HEADER, 0x85, 0x01, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x04, 0x81, 0x02, 0x85, 0x02, LAYOUT_USAGES, 0x95, 0x04, 0x81, 0x02, 0xc0 }, 33, STATUS_COULD_NOT_INTERPRET },
    { "2 byte report count", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x96, 0x08, 0x00, 0x81, 0x02, 0xc0 }, 20, STATUS_SUCCESS, 2, 0, 0, 1 },
    { "4 byte report count", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x97, 0x08, 0x00, 0x00, 0x00, 0x81, 0x02, 0xc0 }, 22, STATUS_SUCCESS },
    { "4 byte report count after 1 byte", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x08, 0x97, 0xec, 0x04, 0x00, 0x00, 0x81, 0x02, 0xc0 }, 24, STATUS_SUCCESS, 2, 0, 0, 1 },
    { "4 byte report size", { LAYOUT_HEADER, LAYOUT_USAGES, 0x77, 0x01, 0x00, 0x00, 0x00, 0x95, 0x08, 0x81, 0x02, 0xc0 }, 22, STATUS_SUCCESS, 0, 0, 0, 0, 1 },
    { "array", { LAYOUT_HEADER, LAYOUT_USAGES, 0x75, 0x01, 0x95, 0x08, 0x81, 0x00, 0xc0 }, 19, STATUS_SUCCESS, 2, 0, 0, 1 },
};

static
VOID
TestReportLayouts(VOID)
{
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_CAPS Caps;
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(ReportLayouts); i++)
    {
        RtlZeroMemory(&DeviceDescription, sizeof(DeviceDescription));
        Status = HidP_GetCollectionDescription((PHIDP_REPORT_DESCRIPTOR)ReportLayouts[i].Descriptor, ReportLayouts[i].Length, NonPagedPool, &DeviceDescription);
        ok(Status == ReportLayouts[i].Status, "%s: Status = 0x%lx, expected 0x%lx\n", ReportLayouts[i].Name, Status, ReportLayouts[i].Status);
        if (!NT_SUCCESS(Status))
            continue;
        if (DeviceDescription.CollectionDescLength >= 1)
        {
            ok(DeviceDescription.CollectionDesc[0].InputLength == ReportLayouts[i].Input, "%s: InputLength = %u\n", ReportLayouts[i].Name, DeviceDescription.CollectionDesc[0].InputLength);
            ok(DeviceDescription.CollectionDesc[0].OutputLength == ReportLayouts[i].Output, "%s: OutputLength = %u\n", ReportLayouts[i].Name, DeviceDescription.CollectionDesc[0].OutputLength);
            ok(DeviceDescription.CollectionDesc[0].FeatureLength == ReportLayouts[i].Feature, "%s: FeatureLength = %u\n", ReportLayouts[i].Name, DeviceDescription.CollectionDesc[0].FeatureLength);
            Status = HidP_GetCaps(DeviceDescription.CollectionDesc[0].PreparsedData, &Caps);
            ok(Status == HIDP_STATUS_SUCCESS, "%s: HidP_GetCaps = 0x%lx\n", ReportLayouts[i].Name, Status);
            if (Status == HIDP_STATUS_SUCCESS)
            {
                ok(Caps.NumberInputButtonCaps == ReportLayouts[i].InputButtons, "%s: NumberInputButtonCaps = %u\n", ReportLayouts[i].Name, Caps.NumberInputButtonCaps);
                ok(Caps.NumberInputValueCaps == ReportLayouts[i].InputValues, "%s: NumberInputValueCaps = %u\n", ReportLayouts[i].Name, Caps.NumberInputValueCaps);
                ok(Caps.NumberOutputButtonCaps == ReportLayouts[i].OutputButtons, "%s: NumberOutputButtonCaps = %u\n", ReportLayouts[i].Name, Caps.NumberOutputButtonCaps);
                ok(Caps.NumberOutputValueCaps == ReportLayouts[i].OutputValues, "%s: NumberOutputValueCaps = %u\n", ReportLayouts[i].Name, Caps.NumberOutputValueCaps);
                ok(Caps.NumberFeatureButtonCaps == ReportLayouts[i].FeatureButtons, "%s: NumberFeatureButtonCaps = %u\n", ReportLayouts[i].Name, Caps.NumberFeatureButtonCaps);
                ok(Caps.NumberFeatureValueCaps == ReportLayouts[i].FeatureValues, "%s: NumberFeatureValueCaps = %u\n", ReportLayouts[i].Name, Caps.NumberFeatureValueCaps);
            }
        }
        else
        {
            ok(0, "%s: no collection\n", ReportLayouts[i].Name);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }
}

static
VOID
TestGetCollectionDescription(VOID)
{
    NTSTATUS Status;
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_VALUE_CAPS ValueCaps;
    USHORT Count;

    /* Empty report descriptor */
    RtlFillMemory(&DeviceDescription, sizeof(DeviceDescription), 0x55);
    Status = HidP_GetCollectionDescription(NULL,
                                           0,
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_NO_DATA_DETECTED);
    ok_eq_pointer(DeviceDescription.CollectionDesc, NULL);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 0);
    ok_eq_pointer(DeviceDescription.ReportIDs, NULL);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 0);
    if (NT_SUCCESS(Status)) HidP_FreeCollectionDescription(&DeviceDescription);

    /* Signed bounds must be extended to LONG before being exposed in value caps. */
    Status = HidP_GetCollectionDescription(SignedAxisDescriptor,
                                           sizeof(SignedAxisDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    if (!skip(NT_SUCCESS(Status) && DeviceDescription.CollectionDescLength >= 1,
              "Signed-axis descriptor parsing failure\n"))
    {
        Count = 1;
        RtlZeroMemory(&ValueCaps, sizeof(ValueCaps));
        Status = HidP_GetValueCaps(HidP_Input,
                                   &ValueCaps,
                                   &Count,
                                   DeviceDescription.CollectionDesc[0].PreparsedData);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(Count, 1);
        if (Status == HIDP_STATUS_SUCCESS && Count == 1)
        {
            ok_eq_long(ValueCaps.LogicalMin, -32768);
            ok_eq_long(ValueCaps.LogicalMax, 32767);
            ok_eq_long(ValueCaps.PhysicalMin, -32768);
            ok_eq_long(ValueCaps.PhysicalMax, 32767);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    /* Sample keyboard report descriptor from the HID spec */
    Status = HidP_GetCollectionDescription(ExampleKeyboardDescriptor,
                                           sizeof(ExampleKeyboardDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 1);
    if (!skip(NT_SUCCESS(Status), "Parsing failure\n"))
    {
        if (!skip(DeviceDescription.CollectionDescLength >= 1, "No collection\n"))
        {
            ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage, HID_USAGE_PAGE_GENERIC);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage, HID_USAGE_GENERIC_KEYBOARD);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 9);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].OutputLength, 2);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].FeatureLength, 0);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].PreparsedDataLength, 476);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 1, "No report IDs\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].InputLength, 8);
            ok_eq_uint(DeviceDescription.ReportIDs[0].OutputLength, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].FeatureLength, 0);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    /* Regression test for CORE-11538 */
    Status = HidP_GetCollectionDescription(PowerProEliteDescriptor,
                                           sizeof(PowerProEliteDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 4);
    if (!skip(NT_SUCCESS(Status), "Parsing failure\n"))
    {
        if (!skip(DeviceDescription.CollectionDescLength >= 1, "No collection\n"))
        {
            ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage, HID_USAGE_PAGE_GENERIC);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage, HID_USAGE_GENERIC_JOYSTICK);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 49);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].OutputLength, 49);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].FeatureLength, 49);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].PreparsedDataLength, 1388);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 1, "No first report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].InputLength, 49);
            ok_eq_uint(DeviceDescription.ReportIDs[0].OutputLength, 49);
            ok_eq_uint(DeviceDescription.ReportIDs[0].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 2, "No second report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[1].ReportID, 2);
            ok_eq_uint(DeviceDescription.ReportIDs[1].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[1].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[1].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[1].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 3, "No third report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[2].ReportID, 238);
            ok_eq_uint(DeviceDescription.ReportIDs[2].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[2].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[2].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[2].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 4, "No fourth report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[3].ReportID, 239);
            ok_eq_uint(DeviceDescription.ReportIDs[3].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[3].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[3].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[3].FeatureLength, 49);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    /* A data item without a usage */
    RtlFillMemory(&DeviceDescription, sizeof(DeviceDescription), 0x55);
    Status = HidP_GetCollectionDescription(LargeReportCountDescriptor, sizeof(LargeReportCountDescriptor), NonPagedPool, &DeviceDescription);
    ok_eq_hex(Status, STATUS_COULD_NOT_INTERPRET);
    ok_eq_pointer(DeviceDescription.CollectionDesc, NULL);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 0);
    ok_eq_pointer(DeviceDescription.ReportIDs, NULL);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 0);
    if (NT_SUCCESS(Status)) HidP_FreeCollectionDescription(&DeviceDescription);

    /* Reject a report count whose private report-item array would overflow */
    RtlFillMemory(&DeviceDescription, sizeof(DeviceDescription), 0x55);
    Status = HidP_GetCollectionDescription(OverflowReportCountDescriptor, sizeof(OverflowReportCountDescriptor), NonPagedPool, &DeviceDescription);
    ok_eq_hex(Status, STATUS_COULD_NOT_INTERPRET);
    ok_eq_pointer(DeviceDescription.CollectionDesc, NULL);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 0);
    ok_eq_pointer(DeviceDescription.ReportIDs, NULL);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 0);
    if (NT_SUCCESS(Status)) HidP_FreeCollectionDescription(&DeviceDescription);

    TestReportLayouts();
}

NTSTATUS
TestHidPDescription(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    PAGED_CODE();

    NT_VERIFY(ControlCode == IOCTL_TEST_DESCRIPTION);

    TestGetCollectionDescription();

    return STATUS_SUCCESS;
}
