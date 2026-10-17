#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the production display bring-up path (dxgkrnl bridge -> win32k) on the host.

The win32k device discovery, mode probing, desktop graphics start and the
dxgkrnl DEVICEMAP publication are extracted verbatim from the source tree and
driven against a simulated registry, object namespace and boot timeline.
Run from the source tree with a host C compiler supporting ASan and UBSan.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def skip_literal(source, index):
    quote = source[index]
    index += 1
    while source[index] != quote:
        index += 2 if source[index] == "\\" else 1
    return index + 1


def skip_trivia(source, index):
    if source.startswith("//", index):
        return source.index("\n", index)
    if source.startswith("/*", index):
        return source.index("*/", index) + 2
    if source[index] in "\"'":
        return skip_literal(source, index)
    return None


def match_close(source, index, opening, closing):
    depth = 0
    while True:
        skipped = skip_trivia(source, index)
        if skipped is not None:
            index = skipped
            continue
        if source[index] == opening:
            depth += 1
        elif source[index] == closing:
            depth -= 1
            if depth == 0:
                return index
        index += 1


def header_start(source, start):
    while start > 0:
        previous_end = start - 1
        previous_start = source.rfind("\n", 0, previous_end) + 1
        line = source[previous_start:previous_end].strip()
        if (not line or line.endswith("*/") or line.endswith("}") or
                line.endswith(";") or line.startswith("//") or line.startswith("#")):
            break
        start = previous_start
    return start


def function(source, name, path):
    position = 0
    while True:
        position = source.find("\n" + name + "(", position)
        if position < 0:
            raise SystemExit(f"{path}: definition of {name} not found")
        position += 1
        close = match_close(source, position + len(name), "(", ")")
        body = close + 1
        while source[body] in " \t\r\n":
            body += 1
        if source[body] == "{":
            end = match_close(source, body, "{", "}")
            return source[header_start(source, position):end + 1] + "\n"
        position = close


def block_after(source, marker, path):
    position = source.find(marker)
    if position < 0:
        raise SystemExit(f"{path}: marker {marker!r} not found")
    brace = source.rfind("{", 0, position)
    end = match_close(source, brace, "{", "}")
    return source[brace:end + 1] + "\n"


def text_between(source, first, last, path, include_last):
    start = source.find(first)
    if start < 0:
        raise SystemExit(f"{path}: marker {first!r} not found")
    end = source.find(last, start)
    if end < 0:
        raise SystemExit(f"{path}: marker {last!r} not found")
    if include_last:
        end += len(last)
    return source[start:end] + "\n"


EXTRACTS = {
    "w32registry.h": ("win32ss/user/ntuser/misc/registry.c",
                      ["RegOpenKey", "RegQueryValue", "RegReadDWORD"]),
    "w32display.h": ("win32ss/user/ntuser/display.c",
                     ["InitDisplayDriver", "InitVideo"]),
    "w32device.h": ("win32ss/gdi/eng/device.c",
                    ["EngpHasVgaDriver", "EngpLinkGraphicsDevice",
                     "EngpUnlinkGraphicsDevice", "EngpIsGraphicsDeviceRegistered",
                     "EngpFreeGraphicsDevice", "EngpUpdateGraphicsDeviceList",
                     "EngpGetRegistryHandleFromDeviceMap",
                     "EngpGetDisplayDriverParameters",
                     "EngpGetDisplayDriverAccelerationLevel",
                     "EngpRegisterGraphicsDevice", "EngpFindGraphicsDevice",
                     "EngDeviceIoControl"]),
    "w32ldev.h": ("win32ss/gdi/eng/ldevobj.c",
                  ["LDEVOBJ_ulGetDriverModes", "LDEVOBJ_bModeListedByEarlierDriver",
                   "LDEVOBJ_bBuildDevmodeList", "LDEVOBJ_bGetClosestMode",
                   "LDEVOBJ_bProbeAndCaptureDevmode"]),
    "w32mdev.h": ("win32ss/gdi/eng/mdevobj.c", ["MDEVOBJ_Create", "MDEVOBJ_vDestroy"]),
    "w32pdev.h": ("win32ss/gdi/eng/pdevobj.c", ["PDEVOBJ_lChangeDisplaySettings"]),
    "w32winsta.h": ("win32ss/user/ntuser/winsta.c", ["co_IntInitializeDesktopGraphics"]),
    "w32guicheck.h": ("win32ss/user/ntuser/guicheck.c",
                      ["co_AddGuiApp", "RemoveGuiApp", "co_IntGraphicsCheck"]),
    "w32windc.h": ("win32ss/user/ntuser/windc.c", ["DceCreateDisplayDC"]),
    "w32ntuser.h": ("win32ss/user/ntuser/ntuser.c", ["UserInitialize"]),
    "cddmodes.h": ("win32ss/drivers/displays/cdd/screen.c",
                   ["RcddGetAvailableModes", "RcddGetModes"]),
    "fbmodes.h": ("win32ss/drivers/displays/framebuf/screen.c",
                  ["GetAvailableModes", "DrvGetModes"]),
    "dxgdisplay.h": ("drivers/directx/dxgkrnl/display.c",
                     ["DxgkpRegWriteDword", "DxgkpRegWriteString",
                      "DxgkpRegWriteDisplayDrivers", "DxgkpSelectDisplayDriver",
                      "DxgkpSnapshotCommittedDisplayState", "DxgkpReadMaxObjectNumber",
                      "DxgkpRemoveDeviceMapEntry", "DxgkDisplayRegister"]),
    "dxgpost.h": ("drivers/directx/dxgkrnl/adapter.c",
                  ["DxgkpValidatePostDisplayInformation",
                   "DxgkpAcquirePostDisplayOwnership"]),
    "ntinbv.h": ("ntoskrnl/inbv/inbv.c",
                 ["InbvGetGopFrameBufferInfo", "InbvHasValidGopFrameBuffer"]),
    "softgpupost.h": ("sdk/lib/drivers/softgpu/platform_common.c",
                      ["SoftGpuValidatePostDisplayInfo", "SoftGpuAcquirePostDisplay"]),
}


def generate(root, work):
    for header, (relative, names) in EXTRACTS.items():
        source = (root / relative).read_text(errors="surrogateescape")
        (work / header).write_text("".join(function(source, name, relative) for name in names))

    device = (root / "win32ss/gdi/eng/device.c").read_text()
    (work / "w32deviceglobals.h").write_text(text_between(
        device, "static PGRAPHICS_DEVICE gpPrimaryGraphicsDevice;",
        "static ULONG giDevNum = 1;", "device.c", True))

    guicheck = (root / "win32ss/user/ntuser/guicheck.c").read_text()
    (work / "w32guiglobals.h").write_text(text_between(
        guicheck, "static LONG NrGuiAppsRunning", ";", "guicheck.c", True))

    relative = "drivers/directx/dxgkrnl/display.c"
    display = (root / relative).read_text()
    cases = text_between(display, "        case IOCTL_VIDEO_INIT_WIN32K_CALLBACKS:",
                         "        case IOCTL_VIDEO_SET_CURRENT_MODE:", relative, False)
    (work / "dxgdispatch.h").write_text(
        "static NTSTATUS NTAPI\nDxgkpDisplayDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)\n{\n"
        "    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);\n"
        "    NTSTATUS Status = STATUS_NOT_SUPPORTED;\n"
        "    ULONG BytesReturned = 0;\n"
        "    UNREFERENCED_PARAMETER(DeviceObject);\n"
        "    switch (Stack->Parameters.DeviceIoControl.IoControlCode)\n    {\n"
        + cases +
        "        default:\n            Status = STATUS_NOT_SUPPORTED;\n            break;\n    }\n"
        "    Irp->IoStatus.Status = Status;\n"
        "    Irp->IoStatus.Information = BytesReturned;\n"
        "    IoCompleteRequest(Irp, IO_NO_INCREMENT);\n"
        "    return Status;\n}\n")

    relative = "drivers/directx/dxgkrnl/dxgkrnl.c"
    driver = (root / relative).read_text()
    (work / "dxgdriverentry.h").write_text(
        "static VOID\nDxgkDriverEntryDeviceMap(VOID)\n"
        + block_after(driver, "UNICODE_STRING VideoMapName;", relative))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, help="LiberNT source directory")
    parser.add_argument("--keep", type=Path, help="Directory that keeps the generated sources")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = args.source_root or here.parents[3]

    with tempfile.TemporaryDirectory(prefix="display-bringup-") as directory:
        work = args.keep or Path(directory)
        work.mkdir(parents=True, exist_ok=True)
        generate(root, work)
        executable = work / "display-bringup-test"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + [
            "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-Wno-everything",
            "-I", str(work), "-I", str(root / "sdk/include/reactos/drivers/directx"),
            "-I", str(root / "sdk/include/reactos"),
            str(here / "display_bringup_host.c"), "-o", str(executable),
        ], check=True)
        sys.exit(subprocess.run([str(executable)]).returncode)


if __name__ == "__main__":
    main()
