/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for InitializeIpInterfaceEntry
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <limits.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

static void test_fill(BYTE Fill)
{
    MIB_IPINTERFACE_ROW Row, Expected;
    ULONG Index;

    memset(&Expected, 0xff, sizeof(Expected));
    Expected.Family = AF_UNSPEC;
    Expected.InterfaceLuid.Value = 0;
    Expected.InterfaceIndex = 0;
    Expected.BaseReachableTime = 0;
    Expected.RetransmitTime = 0;
    Expected.PathMtuDiscoveryTimeout = 0;
    Expected.NlMtu = 0;

    memset(&Row, Fill, sizeof(Row));
    InitializeIpInterfaceEntry(&Row);

    ok(Row.Family == AF_UNSPEC, "Family %u\n", Row.Family);
    ok(Row.InterfaceLuid.Value == 0, "InterfaceLuid %I64x\n", Row.InterfaceLuid.Value);
    ok(Row.InterfaceIndex == 0, "InterfaceIndex %lu\n", Row.InterfaceIndex);
    ok(Row.MaxReassemblySize == ULONG_MAX, "MaxReassemblySize %lu\n", Row.MaxReassemblySize);
    ok(Row.InterfaceIdentifier == MAXULONG64, "InterfaceIdentifier %I64x\n", Row.InterfaceIdentifier);
    ok(Row.MinRouterAdvertisementInterval == ULONG_MAX, "MinRouterAdvertisementInterval %lu\n",
       Row.MinRouterAdvertisementInterval);
    ok(Row.MaxRouterAdvertisementInterval == ULONG_MAX, "MaxRouterAdvertisementInterval %lu\n",
       Row.MaxRouterAdvertisementInterval);
    ok(Row.AdvertisingEnabled == 0xff, "AdvertisingEnabled %u\n", Row.AdvertisingEnabled);
    ok(Row.ForwardingEnabled == 0xff, "ForwardingEnabled %u\n", Row.ForwardingEnabled);
    ok(Row.WeakHostSend == 0xff, "WeakHostSend %u\n", Row.WeakHostSend);
    ok(Row.WeakHostReceive == 0xff, "WeakHostReceive %u\n", Row.WeakHostReceive);
    ok(Row.UseAutomaticMetric == 0xff, "UseAutomaticMetric %u\n", Row.UseAutomaticMetric);
    ok(Row.UseNeighborUnreachabilityDetection == 0xff, "UseNeighborUnreachabilityDetection %u\n",
       Row.UseNeighborUnreachabilityDetection);
    ok(Row.ManagedAddressConfigurationSupported == 0xff, "ManagedAddressConfigurationSupported %u\n",
       Row.ManagedAddressConfigurationSupported);
    ok(Row.OtherStatefulConfigurationSupported == 0xff, "OtherStatefulConfigurationSupported %u\n",
       Row.OtherStatefulConfigurationSupported);
    ok(Row.AdvertiseDefaultRoute == 0xff, "AdvertiseDefaultRoute %u\n", Row.AdvertiseDefaultRoute);
    ok(Row.RouterDiscoveryBehavior == RouterDiscoveryUnchanged, "RouterDiscoveryBehavior %d\n",
       Row.RouterDiscoveryBehavior);
    ok(Row.DadTransmits == ULONG_MAX, "DadTransmits %lu\n", Row.DadTransmits);
    ok(Row.BaseReachableTime == 0, "BaseReachableTime %lu\n", Row.BaseReachableTime);
    ok(Row.RetransmitTime == 0, "RetransmitTime %lu\n", Row.RetransmitTime);
    ok(Row.PathMtuDiscoveryTimeout == 0, "PathMtuDiscoveryTimeout %lu\n", Row.PathMtuDiscoveryTimeout);
    ok(Row.LinkLocalAddressBehavior == LinkLocalUnchanged, "LinkLocalAddressBehavior %d\n",
       Row.LinkLocalAddressBehavior);
    ok(Row.LinkLocalAddressTimeout == ULONG_MAX, "LinkLocalAddressTimeout %lu\n", Row.LinkLocalAddressTimeout);
    for (Index = 0; Index < ScopeLevelCount; Index++)
        ok(Row.ZoneIndices[Index] == ULONG_MAX, "ZoneIndices[%lu] %lu\n", Index, Row.ZoneIndices[Index]);
    ok(Row.SitePrefixLength == ULONG_MAX, "SitePrefixLength %lu\n", Row.SitePrefixLength);
    ok(Row.Metric == ULONG_MAX, "Metric %lu\n", Row.Metric);
    ok(Row.NlMtu == 0, "NlMtu %lu\n", Row.NlMtu);
    ok(Row.Connected == 0xff, "Connected %u\n", Row.Connected);
    ok(Row.SupportsWakeUpPatterns == 0xff, "SupportsWakeUpPatterns %u\n", Row.SupportsWakeUpPatterns);
    ok(Row.SupportsNeighborDiscovery == 0xff, "SupportsNeighborDiscovery %u\n", Row.SupportsNeighborDiscovery);
    ok(Row.SupportsRouterDiscovery == 0xff, "SupportsRouterDiscovery %u\n", Row.SupportsRouterDiscovery);
    ok(Row.ReachableTime == ULONG_MAX, "ReachableTime %lu\n", Row.ReachableTime);
    ok(*(BYTE *)&Row.TransmitOffload == 0xff, "TransmitOffload %02x\n", *(BYTE *)&Row.TransmitOffload);
    ok(*(BYTE *)&Row.ReceiveOffload == 0xff, "ReceiveOffload %02x\n", *(BYTE *)&Row.ReceiveOffload);
    ok(Row.DisableDefaultRoutes == 0xff, "DisableDefaultRoutes %u\n", Row.DisableDefaultRoutes);
    ok(memcmp(&Row, &Expected, sizeof(Row)) == 0, "row prefilled with %02x differs from the defaults\n", Fill);
}

START_TEST(InitializeIpInterfaceEntry)
{
    ok(sizeof(MIB_IPINTERFACE_ROW) == 168, "sizeof(MIB_IPINTERFACE_ROW) %lu\n", (ULONG)sizeof(MIB_IPINTERFACE_ROW));
    test_fill(0x00);
    test_fill(0xcc);
}
