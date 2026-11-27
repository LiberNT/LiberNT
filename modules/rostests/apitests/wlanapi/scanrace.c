/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Races scan ioctls against vwifi removal to reproduce the
 *              nwifi ioctl-vs-detach use-after-free
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <apitest.h>

#include <winsock2.h>
#include <wlanapi.h>
#include <setupapi.h>

#include "vwifi.h"

#define RACE_ITERATIONS 25

typedef struct _SCAN_THREAD
{
    HANDLE Client;
    GUID Interface;
    volatile LONG Stop;
    volatile LONG ScansIssued;
    volatile LONG ScanErrors;
} SCAN_THREAD, *PSCAN_THREAD;

static DWORD WINAPI
ScanThreadProc(PVOID Parameter)
{
    PSCAN_THREAD Context = Parameter;
    DWORD Error;

    while (!Context->Stop)
    {
        Error = WlanScan(Context->Client, &Context->Interface, NULL, NULL, NULL);
        if (Error == ERROR_SUCCESS)
        {
            InterlockedIncrement(&Context->ScansIssued);
        }
        else
        {
            InterlockedIncrement(&Context->ScanErrors);
            /* The interface is going away or already gone: a few errors are
             * expected during teardown, continuous ones mean we are done. */
            if (Context->ScanErrors > 20)
                break;
        }
        Sleep(0);
    }
    return 0;
}

START_TEST(WlanScanDetachRace)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    DWORD NegotiatedVersion;
    HANDLE Client;
    GUID Interface;
    DWORD Error;
    DWORD Iteration;
    DWORD CompletedIterations = 0;
    ULONG TotalScans = 0;

    Error = WlanOpenHandle(WLAN_API_VERSION_2_0, NULL, &NegotiatedVersion, &Client);
    if (Error == ERROR_SERVICE_NOT_ACTIVE)
    {
        skip("WlanSvc is not running\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "WlanOpenHandle failed: %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    /*
     * Each iteration installs the vwifi adapter, hammers it with scan ioctls
     * and removes the adapter while the ioctls are in flight. Without
     * reference counting in the nwifi ioctl path the detach frees the adapter
     * while an ioctl still uses it, crashing the kernel or hanging the scan;
     * either one ends this test process, so every finished iteration is
     * evidence the race is closed.
     */
    for (Iteration = 0; Iteration < RACE_ITERATIONS; Iteration++)
    {
        SCAN_THREAD Context;
        HANDLE Thread;

        if (!InstallVwifi(&Devices, &Device))
        {
            skip("Could not install the vwifi adapter\n");
            break;
        }
        if (!WaitInterface(Client, Devices, &Device, &Interface))
        {
            ok(FALSE, "Wireless interface never appeared\n");
            RemoveVwifi(Devices, &Device);
            break;
        }

        Context.Client = Client;
        Context.Interface = Interface;
        Context.Stop = 0;
        Context.ScansIssued = 0;
        Context.ScanErrors = 0;
        Thread = CreateThread(NULL, 0, ScanThreadProc, &Context, 0, NULL);
        ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());

        /* Vary the teardown delay so the detach lands at different points
         * of the scan pipeline. */
        Sleep((Iteration * 7) % 23);

        RemoveVwifi(Devices, &Device);

        if (Thread != NULL)
        {
            InterlockedExchange(&Context.Stop, 1);
            WaitForSingleObject(Thread, INFINITE);
            CloseHandle(Thread);
        }
        TotalScans += Context.ScansIssued;
        CompletedIterations++;
    }

    ok(CompletedIterations == RACE_ITERATIONS,
       "Completed %lu of %u race iterations\n", CompletedIterations, RACE_ITERATIONS);
    trace("Issued %lu scan ioctls across %lu iterations\n", TotalScans, CompletedIterations);

    WlanCloseHandle(Client, NULL);
}
