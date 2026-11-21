/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unit tests for device tree parent lookup and bus address translation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <libernt/fdtlib.h>

#include "fdtbuild.h"

static
VOID
BuildTree(VOID)
{
    memset(&Builder, 0, sizeof(Builder));
    FdtBeginNode("");
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);

    FdtBeginNode("soc");
    FdtStrings("compatible", LIST("simple-bus"));
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtCells("ranges", 6, 0x7e000000, 0x3f000000, 0x01000000, 0x40000000, 0x40000000, 0x1000);
    FdtBeginNode("serial@7e201000");
    FdtCells("reg", 2, 0x7e201000, 0x200);
    FdtEndNode();
    FdtBeginNode("bus@7e300000");
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtCells("ranges", 3, 0, 0x7e300000, 0x1000);
    FdtBeginNode("device@100");
    FdtCells("reg", 2, 0x100, 0x10);
    FdtEndNode();
    FdtEndNode();
    FdtBeginNode("identity");
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtProperty("ranges", NULL, 0);
    FdtEndNode();
    FdtBeginNode("closed");
    FdtCells("#address-cells", 1, 1);
    FdtCells("#size-cells", 1, 1);
    FdtEndNode();
    FdtEndNode();

    FdtEndNode();
    FdtFinish();
}

START_TEST(FdtTranslate)
{
    ULONG Root, Soc, Serial, Bus, Device, Identity, Closed, Parent;
    ULONGLONG Address;
    FDT Fdt;

    BuildTree();
    ok(FdtOpen(Builder.Blob, Builder.BlobSize, &Fdt), "FdtOpen failed\n");
    Root = FdtRootNode(&Fdt);
    ok(FdtFindNode(&Fdt, "/soc", &Soc, NULL), "no /soc\n");
    ok(FdtFindNode(&Fdt, "/soc/serial@7e201000", &Serial, NULL), "no serial\n");
    ok(FdtFindNode(&Fdt, "/soc/bus@7e300000", &Bus, NULL), "no bus\n");
    ok(FdtFindNode(&Fdt, "/soc/bus@7e300000/device@100", &Device, NULL), "no device\n");
    ok(FdtFindNode(&Fdt, "/soc/identity", &Identity, NULL), "no identity bus\n");
    ok(FdtFindNode(&Fdt, "/soc/closed", &Closed, NULL), "no closed bus\n");

    ok(!FdtParentNode(&Fdt, Root, &Parent), "the root has a parent\n");
    Parent = FDT_NO_NODE;
    ok(FdtParentNode(&Fdt, Serial, &Parent) && (Parent == Soc), "serial parent %lx, expected %lx\n", Parent, Soc);
    Parent = FDT_NO_NODE;
    ok(FdtParentNode(&Fdt, Device, &Parent) && (Parent == Bus), "device parent %lx, expected %lx\n", Parent, Bus);
    Parent = FDT_NO_NODE;
    ok(FdtParentNode(&Fdt, Soc, &Parent) && (Parent == Root), "soc parent %lx, expected %lx\n", Parent, Root);

    Address = 0x7e201000;
    ok(FdtTranslateAddress(&Fdt, Soc, &Address) && (Address == 0x3f201000), "serial at %I64x\n", Address);
    Address = 0x40000000;
    ok(FdtTranslateAddress(&Fdt, Soc, &Address) && (Address == 0x40000000), "second range gave %I64x\n", Address);
    Address = 0x100;
    ok(FdtTranslateAddress(&Fdt, Bus, &Address) && (Address == 0x3f300100), "nested device at %I64x\n", Address);
    Address = 0x7e005000;
    ok(FdtTranslateAddress(&Fdt, Identity, &Address) && (Address == 0x3f005000), "identity bus gave %I64x\n", Address);
    Address = 0x5000;
    ok(!FdtTranslateAddress(&Fdt, Identity, &Address), "an identity bus let an address outside its parent's ranges through\n");
    Address = 0x9000000;
    ok(FdtTranslateAddress(&Fdt, Root, &Address) && (Address == 0x9000000), "root gave %I64x\n", Address);

    Address = 0x6000;
    ok(!FdtTranslateAddress(&Fdt, Closed, &Address), "a bus without ranges was crossed\n");
    Address = 0x10000000;
    ok(!FdtTranslateAddress(&Fdt, Soc, &Address), "an address outside every range was translated to %I64x\n", Address);
    Address = 0x1000;
    ok(!FdtTranslateAddress(&Fdt, Bus, &Address), "an address past the nested range was translated to %I64x\n", Address);
}
