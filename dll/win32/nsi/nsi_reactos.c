/*
 * ReactOS Network Store Interface backend
 *
 * This backend translates the NSI tables used by Wine's nsi.dll to the
 * native ReactOS TCP/IP query interface shared with iphlpapi.
 */

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winsock2.h"
#include "winternl.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "netioapi.h"
#include "iptypes.h"
#include "netiodef.h"
#include "wine/nsi.h"
#include "wine/debug.h"

#include <tdiinfo.h>
#include <tdilib.h>
#include <tcpioctl.h>
#include <tcpip_undoc.h>

#include "../iphlpapi/ifenum.h"
#include "../iphlpapi/ipstats.h"
#include "nsi_reactos.h"

typedef DWORD (*nsi_enumerate_func)(struct nsi_enumerate_all_ex *params);
typedef DWORD (*nsi_get_func)(struct nsi_get_all_parameters_ex *params);

struct nsi_table
{
    const NPI_MODULEID *module;
    UINT table;
    UINT sizes[4];
    nsi_enumerate_func enumerate;
    nsi_get_func get;
};

static BOOL module_equal(const NPI_MODULEID *left, const NPI_MODULEID *right)
{
    if (left->Type != right->Type) return FALSE;
    if (left->Type == MIT_GUID) return !memcmp(&left->Guid, &right->Guid, sizeof(left->Guid));
    if (left->Type == MIT_IF_LUID) return !memcmp(&left->IfLuid, &right->IfLuid, sizeof(left->IfLuid));
    return FALSE;
}

static NET_LUID make_luid(UINT index, UINT type)
{
    NET_LUID luid;

    luid.Value = 0;
    luid.Info.NetLuidIndex = index;
    luid.Info.IfType = type;
    return luid;
}

static UINT interface_type(UINT index)
{
    BYTE address[MAX_INTERFACE_PHYSADDR];
    DWORD length = sizeof(address), type = MIB_IF_TYPE_OTHER;

    getInterfacePhysicalByIndex(index, &length, address, &type);
    return type;
}

static NET_LUID interface_luid(UINT index)
{
    return make_luid(index, interface_type(index));
}

static GUID interface_guid(UINT index, const char *name)
{
    unsigned int data1, data2, data3, data4[8];
    const char *start;
    GUID guid;
    UINT i;

    memset(&guid, 0, sizeof(guid));
    guid.Data1 = index;
    memcpy(guid.Data4 + 2, "NetDev", 6);

    if (!name || !(start = strchr(name, '{'))) return guid;
    if (sscanf(start, "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &data1, &data2, &data3,
               &data4[0], &data4[1], &data4[2], &data4[3], &data4[4], &data4[5], &data4[6],
               &data4[7]) != 11)
        return guid;

    guid.Data1 = data1;
    guid.Data2 = (USHORT)data2;
    guid.Data3 = (USHORT)data3;
    for (i = 0; i < 8; i++) guid.Data4[i] = (BYTE)data4[i];
    return guid;
}

static void init_counted_string(IF_COUNTED_STRING *string, const char *source)
{
    int length;

    memset(string, 0, sizeof(*string));
    if (!source) return;
    length = MultiByteToWideChar(CP_ACP, 0, source, -1, string->String, ARRAY_SIZE(string->String));
    if (length > 0) string->Length = (length - 1) * sizeof(WCHAR);
}

static UINT oper_status(DWORD status)
{
    if (status == MIB_IF_OPER_STATUS_OPERATIONAL || status == MIB_IF_OPER_STATUS_CONNECTED) return NET_IF_OPER_STATUS_UP;
    if (status == MIB_IF_OPER_STATUS_CONNECTING) return NET_IF_OPER_STATUS_DORMANT;
    if (status == MIB_IF_OPER_STATUS_NON_OPERATIONAL) return NET_IF_OPER_STATUS_DOWN;
    return NET_IF_OPER_STATUS_UNKNOWN;
}

static BYTE prefix_length(DWORD mask)
{
    DWORD value = RtlUlongByteSwap(mask);
    BYTE length = 0;

    while (value & 0x80000000)
    {
        length++;
        value <<= 1;
    }
    return length;
}

static DWORD finish_enumeration(struct nsi_enumerate_all_ex *params, UINT_PTR total, BOOL want_data)
{
    if (!want_data || total <= params->count)
    {
        params->count = total;
        return ERROR_SUCCESS;
    }
    return ERROR_MORE_DATA;
}

static void fill_ndis_entry(UINT index, NET_LUID *key, struct nsi_ndis_ifinfo_rw *rw,
                            struct nsi_ndis_ifinfo_dynamic *dynamic, struct nsi_ndis_ifinfo_static *stat)
{
    MIB_IFROW row;
    const char *name;
    DWORD type = MIB_IF_TYPE_OTHER, physical_length = IF_MAX_PHYS_ADDRESS_LENGTH, description_length;
    BYTE physical[IF_MAX_PHYS_ADDRESS_LENGTH] = {0};
    char description[MAXLEN_IFDESCR + 1];

    memset(&row, 0, sizeof(row));
    row.dwIndex = index;
    getInterfaceEntryByIndex(index, &row);
    if (getInterfacePhysicalByIndex(index, &physical_length, physical, &type)) physical_length = 0;
    name = getInterfaceNameByIndex(index);
    if (key) *key = make_luid(index, type);
    if (rw)
    {
        memset(rw, 0, sizeof(*rw));
        rw->network_guid = interface_guid(index, name);
        rw->admin_status = row.dwAdminStatus == 2 ? NET_IF_ADMIN_STATUS_DOWN : NET_IF_ADMIN_STATUS_UP;
        init_counted_string(&rw->alias, name);
        rw->phys_addr.Length = physical_length;
        memcpy(rw->phys_addr.Address, physical, physical_length);
        init_counted_string(&rw->name2, name);
    }
    if (dynamic)
    {
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->oper_status = oper_status(row.dwOperStatus);
        dynamic->media_conn_state = dynamic->oper_status == NET_IF_OPER_STATUS_UP ? MediaConnectStateConnected : MediaConnectStateDisconnected;
        dynamic->mtu = row.dwMtu;
        dynamic->xmit_speed = row.dwSpeed;
        dynamic->rcv_speed = row.dwSpeed;
        dynamic->in_discards = row.dwInDiscards;
        dynamic->in_errors = row.dwInErrors;
        dynamic->in_octets = row.dwInOctets;
        dynamic->in_ucast_pkts = row.dwInUcastPkts;
        dynamic->in_mcast_pkts = row.dwInNUcastPkts;
        dynamic->out_octets = row.dwOutOctets;
        dynamic->out_ucast_pkts = row.dwOutUcastPkts;
        dynamic->out_mcast_pkts = row.dwOutNUcastPkts;
        dynamic->out_errors = row.dwOutErrors;
        dynamic->out_discards = row.dwOutDiscards;
        dynamic->in_ucast_octs = row.dwInOctets;
        dynamic->out_ucast_octs = row.dwOutOctets;
    }
    if (stat)
    {
        memset(stat, 0, sizeof(*stat));
        stat->if_index = index;
        if (row.dwDescrLen)
        {
            description_length = row.dwDescrLen < MAXLEN_IFDESCR ? row.dwDescrLen : MAXLEN_IFDESCR;
            memcpy(description, row.bDescr, description_length);
            description[description_length] = 0;
        }
        init_counted_string(&stat->descr, row.dwDescrLen ? description : name);
        stat->type = type;
        stat->access_type = type == MIB_IF_TYPE_LOOPBACK ? NET_IF_ACCESS_LOOPBACK : NET_IF_ACCESS_BROADCAST;
        stat->conn_type = NET_IF_CONNECTION_DEDICATED;
        stat->if_guid = interface_guid(index, name);
        stat->conn_present = type != MIB_IF_TYPE_LOOPBACK;
        stat->perm_phys_addr.Length = physical_length;
        memcpy(stat->perm_phys_addr.Address, physical, stat->perm_phys_addr.Length);
        stat->flags.hw = type != MIB_IF_TYPE_LOOPBACK;
    }
    if (name) consumeInterfaceName(name);
}

static DWORD enumerate_ndis(struct nsi_enumerate_all_ex *params)
{
    InterfaceIndexTable *indexes = getInterfaceIndexTable();
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR count = indexes ? indexes->numIndexes : 0, limit = count < params->count ? count : params->count;
    UINT_PTR i;

    for (i = 0; want_data && i < limit; i++)
        fill_ndis_entry(indexes->indexes[i], params->key_data ? (NET_LUID *)params->key_data + i : NULL,
                        params->rw_data ? (struct nsi_ndis_ifinfo_rw *)params->rw_data + i : NULL,
                        params->dynamic_data ? (struct nsi_ndis_ifinfo_dynamic *)params->dynamic_data + i : NULL,
                        params->static_data ? (struct nsi_ndis_ifinfo_static *)params->static_data + i : NULL);
    free(indexes);
    return finish_enumeration(params, count, want_data);
}

static void fill_ip_interface(UINT index, struct nsi_ip_interface_key *key, struct nsi_ip_interface_rw *rw,
                              struct nsi_ip_interface_dynamic *dynamic, struct nsi_ip_interface_static *stat)
{
    DWORD mtu = 0, status = 0;

    getInterfaceMtuByIndex(index, &mtu);
    getInterfaceStatusByIndex(index, &status);
    if (key) key->luid = interface_luid(index);
    if (rw)
    {
        memset(rw, 0, sizeof(*rw));
        rw->metric = 0;
        rw->base_reachable_time = 30000;
        rw->retransmit_time = 1000;
        rw->path_mtu_discovery_timeout = 600000;
        rw->dad_transmits = 1;
        rw->mtu = mtu;
    }
    if (dynamic)
    {
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->if_index = index;
        dynamic->reachable_time = 30000;
        dynamic->connected = oper_status(status) == NET_IF_OPER_STATUS_UP;
    }
    if (stat) memset(stat, 0, sizeof(*stat));
}

static DWORD enumerate_ip_interface(struct nsi_enumerate_all_ex *params)
{
    InterfaceIndexTable *indexes = getInterfaceIndexTable();
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR count = indexes ? indexes->numIndexes : 0, limit = count < params->count ? count : params->count;
    UINT_PTR i;

    for (i = 0; want_data && i < limit; i++)
        fill_ip_interface(indexes->indexes[i], params->key_data ? (struct nsi_ip_interface_key *)params->key_data + i : NULL,
                          params->rw_data ? (struct nsi_ip_interface_rw *)params->rw_data + i : NULL,
                          params->dynamic_data ? (struct nsi_ip_interface_dynamic *)params->dynamic_data + i : NULL,
                          params->static_data ? (struct nsi_ip_interface_static *)params->static_data + i : NULL);
    free(indexes);
    return finish_enumeration(params, count, want_data);
}

static void *ip6_query_table(ULONG table_id, ULONG *count)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\Ip6");
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK iosb;
    IP6_TABLE_HEADER *header = NULL;
    ULONG size = sizeof(IP6_TABLE_HEADER) + 4096;
    NTSTATUS status;
    HANDLE file;

    *count = 0;
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = NtOpenFile(&file, SYNCHRONIZE, &attr, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(status)) return NULL;

    for (;;)
    {
        if (!(header = HeapAlloc(GetProcessHeap(), 0, size))) break;
        *(ULONG *)header = table_id;
        status = NtDeviceIoControlFile(file, NULL, NULL, NULL, &iosb, IOCTL_IP6_QUERY_TABLE,
                                       header, sizeof(ULONG), header, size);
        if (status == STATUS_BUFFER_OVERFLOW && header->Needed > size)
        {
            size = header->Needed;
            HeapFree(GetProcessHeap(), 0, header);
            header = NULL;
            continue;
        }
        if (!NT_SUCCESS(status))
        {
            HeapFree(GetProcessHeap(), 0, header);
            header = NULL;
        }
        break;
    }

    NtClose(file);
    if (header) *count = header->Count;
    return header;
}

static void fill_ipv6_interface(const IP6_INTERFACE_ROW *row, struct nsi_ip_interface_key *key, struct nsi_ip_interface_rw *rw,
                                struct nsi_ip_interface_dynamic *dynamic, struct nsi_ip_interface_static *stat)
{
    if (key) key->luid = interface_luid(row->Index);
    if (rw)
    {
        memset(rw, 0, sizeof(*rw));
        rw->base_reachable_time = row->BaseReachableTime;
        rw->retransmit_time = row->RetransmitTime;
        rw->path_mtu_discovery_timeout = 600000;
        rw->dad_transmits = row->DadTransmits;
        rw->link_local_address_behavior = LinkLocalAlwaysOn;
        rw->link_local_address_timeout = ~0u;
        rw->mtu = row->Mtu;
        rw->site_prefix_len = 48;
    }
    if (dynamic)
    {
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->if_index = row->Index;
        dynamic->reachable_time = row->ReachableTime;
        dynamic->connected = row->Connected;
    }
    if (stat) memset(stat, 0, sizeof(*stat));
}

static DWORD enumerate_ipv6_interface(struct nsi_enumerate_all_ex *params)
{
    ULONG count;
    IP6_TABLE_HEADER *header = ip6_query_table(Ip6TableInterfaces, &count);
    const IP6_INTERFACE_ROW *rows = (const IP6_INTERFACE_ROW *)(header + 1);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, limit = count < params->count ? count : params->count;

    for (i = 0; want_data && i < limit; i++)
        fill_ipv6_interface(rows + i, params->key_data ? (struct nsi_ip_interface_key *)params->key_data + i : NULL,
                            params->rw_data ? (struct nsi_ip_interface_rw *)params->rw_data + i : NULL,
                            params->dynamic_data ? (struct nsi_ip_interface_dynamic *)params->dynamic_data + i : NULL,
                            params->static_data ? (struct nsi_ip_interface_static *)params->static_data + i : NULL);
    HeapFree(GetProcessHeap(), 0, header);
    return finish_enumeration(params, count, want_data);
}

static DWORD enumerate_ipv6_unicast(struct nsi_enumerate_all_ex *params)
{
    ULONG count;
    IP6_TABLE_HEADER *header = ip6_query_table(Ip6TableAddresses, &count);
    const IP6_ADDRESS_ROW *rows = (const IP6_ADDRESS_ROW *)(header + 1);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, limit = count < params->count ? count : params->count;

    for (i = 0; want_data && i < limit; i++)
    {
        const IP6_ADDRESS_ROW *row = rows + i;

        if (params->key_data)
        {
            struct nsi_ipv6_unicast_key *key = (struct nsi_ipv6_unicast_key *)params->key_data + i;
            memset(key, 0, sizeof(*key));
            key->luid = interface_luid(row->Index);
            memcpy(&key->addr, row->Address, sizeof(key->addr));
        }
        if (params->rw_data)
        {
            struct nsi_ip_unicast_rw *rw = (struct nsi_ip_unicast_rw *)params->rw_data + i;
            memset(rw, 0, sizeof(*rw));
            rw->preferred_lifetime = row->PreferredLifetime;
            rw->valid_lifetime = row->ValidLifetime;
            rw->prefix_origin = row->PrefixOrigin;
            rw->suffix_origin = row->SuffixOrigin;
            rw->on_link_prefix = row->PrefixLength;
        }
        if (params->dynamic_data)
        {
            struct nsi_ip_unicast_dynamic *dynamic = (struct nsi_ip_unicast_dynamic *)params->dynamic_data + i;
            memset(dynamic, 0, sizeof(*dynamic));
            dynamic->scope_id = IN6_IS_ADDR_LINKLOCAL((const IN6_ADDR *)row->Address) ? row->Index : 0;
            dynamic->dad_state = row->DadState;
        }
        if (params->static_data) memset((struct nsi_ip_unicast_static *)params->static_data + i, 0, sizeof(struct nsi_ip_unicast_static));
    }
    HeapFree(GetProcessHeap(), 0, header);
    return finish_enumeration(params, count, want_data);
}

static DWORD enumerate_ipv6_neighbour(struct nsi_enumerate_all_ex *params)
{
    ULONG count;
    IP6_TABLE_HEADER *header = ip6_query_table(Ip6TableNeighbors, &count);
    const IP6_NEIGHBOR_ROW *rows = (const IP6_NEIGHBOR_ROW *)(header + 1);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, limit = count < params->count ? count : params->count;

    for (i = 0; want_data && i < limit; i++)
    {
        const IP6_NEIGHBOR_ROW *row = rows + i;
        ULONG length = row->PhysicalAddressLength < IF_MAX_PHYS_ADDRESS_LENGTH ? row->PhysicalAddressLength : IF_MAX_PHYS_ADDRESS_LENGTH;

        if (params->key_data)
        {
            struct nsi_ipv6_neighbour_key *key = (struct nsi_ipv6_neighbour_key *)params->key_data + i;
            memset(key, 0, sizeof(*key));
            key->luid = key->luid2 = interface_luid(row->Index);
            memcpy(&key->addr, row->Address, sizeof(key->addr));
        }
        if (params->rw_data)
        {
            struct nsi_ip_neighbour_rw *rw = (struct nsi_ip_neighbour_rw *)params->rw_data + i;
            memset(rw, 0, sizeof(*rw));
            memcpy(rw->phys_addr, row->PhysicalAddress, length);
        }
        if (params->dynamic_data)
        {
            struct nsi_ip_neighbour_dynamic *dynamic = (struct nsi_ip_neighbour_dynamic *)params->dynamic_data + i;
            memset(dynamic, 0, sizeof(*dynamic));
            dynamic->state = row->State;
            dynamic->flags.is_router = row->IsRouter;
            dynamic->flags.is_unreachable = row->IsUnreachable;
            dynamic->phys_addr_len = length;
        }
    }
    HeapFree(GetProcessHeap(), 0, header);
    return finish_enumeration(params, count, want_data);
}

static DWORD enumerate_ipv6_forward(struct nsi_enumerate_all_ex *params)
{
    ULONG count;
    IP6_TABLE_HEADER *header = ip6_query_table(Ip6TableRoutes, &count);
    const IP6_ROUTE_ROW *rows = (const IP6_ROUTE_ROW *)(header + 1);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, limit = count < params->count ? count : params->count;

    for (i = 0; want_data && i < limit; i++)
    {
        const IP6_ROUTE_ROW *row = rows + i;

        if (params->key_data)
        {
            struct nsi_ipv6_forward_key *key = (struct nsi_ipv6_forward_key *)params->key_data + i;
            memset(key, 0, sizeof(*key));
            memcpy(&key->prefix, row->Prefix, sizeof(key->prefix));
            key->prefix_len = (BYTE)row->PrefixLength;
            key->luid = key->luid2 = interface_luid(row->Index);
            memcpy(&key->next_hop, row->NextHop, sizeof(key->next_hop));
        }
        if (params->rw_data)
        {
            struct nsi_ip_forward_rw *rw = (struct nsi_ip_forward_rw *)params->rw_data + i;
            memset(rw, 0, sizeof(*rw));
            rw->valid_lifetime = row->ValidLifetime;
            rw->preferred_lifetime = row->PreferredLifetime;
            rw->metric = row->Metric;
            rw->protocol = row->Protocol;
            rw->loopback = row->Loopback;
            rw->autoconf = row->Autoconfigure;
            rw->publish = row->Publish;
            rw->immortal = row->Immortal;
        }
        if (params->dynamic_data)
        {
            struct nsi_ipv6_forward_dynamic *dynamic = (struct nsi_ipv6_forward_dynamic *)params->dynamic_data + i;
            memset(dynamic, 0, sizeof(*dynamic));
            memcpy(&dynamic->addr2, row->Prefix, sizeof(dynamic->addr2));
        }
        if (params->static_data)
        {
            struct nsi_ip_forward_static *stat = (struct nsi_ip_forward_static *)params->static_data + i;
            memset(stat, 0, sizeof(*stat));
            stat->origin = row->Origin;
            stat->if_index = row->Index;
        }
    }
    HeapFree(GetProcessHeap(), 0, header);
    return finish_enumeration(params, count, want_data);
}

static BOOL ipv6_statistics(IP6_STATISTICS *stats)
{
    ULONG count;
    IP6_TABLE_HEADER *header = ip6_query_table(Ip6TableStatistics, &count);

    if (!header) return FALSE;
    if (count != 1 || header->Needed < sizeof(*header) + sizeof(*stats))
    {
        HeapFree(GetProcessHeap(), 0, header);
        return FALSE;
    }
    memcpy(stats, header + 1, sizeof(*stats));
    HeapFree(GetProcessHeap(), 0, header);
    return TRUE;
}

static DWORD get_ipv6_compartment(struct nsi_get_all_parameters_ex *params)
{
    const UINT *key = params->key;
    IP6_STATISTICS stats;

    if (!key || *key != NET_IF_COMPARTMENT_ID_PRIMARY) return ERROR_FILE_NOT_FOUND;
    if (!ipv6_statistics(&stats)) return ERROR_NOT_SUPPORTED;
    if (params->rw_data)
    {
        struct nsi_ip_cmpt_rw *rw = params->rw_data;
        memset(rw, 0, sizeof(*rw));
        rw->not_forwarding = TRUE;
        rw->default_ttl = stats.DefaultHopLimit;
    }
    if (params->dynamic_data)
    {
        struct nsi_ip_cmpt_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->num_ifs = stats.NumInterfaces;
        dynamic->num_routes = stats.NumRoutes;
        dynamic->num_addrs = stats.NumAddresses;
    }
    return ERROR_SUCCESS;
}

static DWORD get_ipv6_icmp(struct nsi_get_all_parameters_ex *params)
{
    struct nsi_ip_icmpstats_dynamic *dynamic = params->dynamic_data;
    IP6_STATISTICS stats;

    if (!ipv6_statistics(&stats)) return ERROR_NOT_SUPPORTED;
    if (!dynamic) return ERROR_SUCCESS;
    memset(dynamic, 0, sizeof(*dynamic));
    dynamic->in_msgs = stats.IcmpInMessages;
    dynamic->in_errors = stats.IcmpInErrors;
    dynamic->out_msgs = stats.IcmpOutMessages;
    dynamic->out_errors = stats.IcmpOutErrors;
    return ERROR_SUCCESS;
}

static DWORD get_ipv6_stats(struct nsi_get_all_parameters_ex *params)
{
    IP6_STATISTICS stats;

    if (!ipv6_statistics(&stats)) return ERROR_NOT_SUPPORTED;
    if (params->dynamic_data)
    {
        struct nsi_ip_ipstats_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->in_recv = stats.InReceives;
        dynamic->fwd_dgrams = stats.ForwardedDatagrams;
        dynamic->in_delivers = stats.InDelivers;
        dynamic->out_reqs = stats.OutRequests;
        dynamic->in_hdr_errs = stats.InHeaderErrors;
        dynamic->in_addr_errs = stats.InAddressErrors;
        dynamic->in_unk_protos = stats.InUnknownProtocols;
        dynamic->reasm_reqds = stats.ReassemblyRequired;
        dynamic->reasm_oks = stats.ReassemblyOk;
        dynamic->reasm_fails = stats.ReassemblyFailures;
        dynamic->in_discards = stats.InDiscards;
        dynamic->out_no_routes = stats.OutNoRoutes;
        dynamic->out_discards = stats.OutDiscards;
        dynamic->frag_oks = stats.FragmentOk;
        dynamic->frag_fails = stats.FragmentFailures;
        dynamic->frag_creates = stats.FragmentsCreated;
    }
    if (params->static_data)
    {
        struct nsi_ip_ipstats_static *stat = params->static_data;
        memset(stat, 0, sizeof(*stat));
        stat->reasm_timeout = stats.ReassemblyTimeout;
    }
    return ERROR_SUCCESS;
}

static void fill_ipv6_sockaddr(SOCKADDR_INET *address, const UCHAR *bytes, ULONG scope_id, USHORT port)
{
    address->Ipv6.sin6_family = AF_INET6;
    memcpy(&address->Ipv6.sin6_addr, bytes, sizeof(address->Ipv6.sin6_addr));
    address->Ipv6.sin6_scope_id = scope_id;
    address->Ipv6.sin6_port = port;
}

static DWORD enumerate_ipv4_unicast(struct nsi_enumerate_all_ex *params)
{
    InterfaceIndexTable *indexes = getInterfaceIndexTable();
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, count = 0, written = 0;

    for (i = 0; indexes && i < indexes->numIndexes; i++)
    {
        DWORD address = getInterfaceIPAddrByIndex(indexes->indexes[i]);
        DWORD mask = getInterfaceMaskByIndex(indexes->indexes[i]);

        if (address == INADDR_ANY || address == INADDR_NONE) continue;
        if (want_data && written < params->count)
        {
            if (params->key_data)
            {
                struct nsi_ipv4_unicast_key *key = (struct nsi_ipv4_unicast_key *)params->key_data + written;
                memset(key, 0, sizeof(*key));
                key->luid = interface_luid(indexes->indexes[i]);
                key->addr.S_un.S_addr = address;
            }
            if (params->rw_data)
            {
                struct nsi_ip_unicast_rw *rw = (struct nsi_ip_unicast_rw *)params->rw_data + written;
                memset(rw, 0, sizeof(*rw));
                rw->preferred_lifetime = ~0u;
                rw->valid_lifetime = ~0u;
                rw->prefix_origin = IpPrefixOriginOther;
                rw->suffix_origin = IpSuffixOriginOther;
                rw->on_link_prefix = prefix_length(mask);
            }
            if (params->dynamic_data)
            {
                struct nsi_ip_unicast_dynamic *dynamic = (struct nsi_ip_unicast_dynamic *)params->dynamic_data + written;
                memset(dynamic, 0, sizeof(*dynamic));
                dynamic->dad_state = IpDadStatePreferred;
            }
            if (params->static_data) memset((struct nsi_ip_unicast_static *)params->static_data + written, 0, sizeof(struct nsi_ip_unicast_static));
            written++;
        }
        count++;
    }
    free(indexes);
    return finish_enumeration(params, count, want_data);
}

static DWORD enumerate_ipv4_neighbour(struct nsi_enumerate_all_ex *params)
{
    MIB_IPNETTABLE *table = getArpTable();
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR count = table ? table->dwNumEntries : 0, limit = count < params->count ? count : params->count;
    UINT_PTR i;

    for (i = 0; want_data && i < limit; i++)
    {
        MIB_IPNETROW *row = table->table + i;

        if (params->key_data)
        {
            struct nsi_ipv4_neighbour_key *key = (struct nsi_ipv4_neighbour_key *)params->key_data + i;
            memset(key, 0, sizeof(*key));
            key->luid = key->luid2 = interface_luid(row->dwIndex);
            key->addr.S_un.S_addr = row->dwAddr;
        }
        if (params->rw_data)
        {
            struct nsi_ip_neighbour_rw *rw = (struct nsi_ip_neighbour_rw *)params->rw_data + i;
            memset(rw, 0, sizeof(*rw));
            memcpy(rw->phys_addr, row->bPhysAddr, row->dwPhysAddrLen);
        }
        if (params->dynamic_data)
        {
            struct nsi_ip_neighbour_dynamic *dynamic = (struct nsi_ip_neighbour_dynamic *)params->dynamic_data + i;
            memset(dynamic, 0, sizeof(*dynamic));
            dynamic->state = row->dwType == MIB_IPNET_TYPE_STATIC ? NlnsPermanent : NlnsReachable;
            dynamic->phys_addr_len = row->dwPhysAddrLen;
        }
    }
    HeapFree(GetProcessHeap(), 0, table);
    return finish_enumeration(params, count, want_data);
}

static void fill_ipv4_forward(struct nsi_enumerate_all_ex *params, UINT_PTR i, DWORD dest, DWORD mask,
                              DWORD next_hop, DWORD if_index, DWORD metric)
{
    if (params->key_data)
    {
        struct nsi_ipv4_forward_key *key = (struct nsi_ipv4_forward_key *)params->key_data + i;
        memset(key, 0, sizeof(*key));
        key->prefix.S_un.S_addr = dest;
        key->prefix_len = prefix_length(mask);
        key->luid = key->luid2 = interface_luid(if_index);
        key->next_hop.S_un.S_addr = next_hop;
    }
    if (params->rw_data)
    {
        struct nsi_ip_forward_rw *rw = (struct nsi_ip_forward_rw *)params->rw_data + i;
        memset(rw, 0, sizeof(*rw));
        rw->valid_lifetime = ~0u;
        rw->preferred_lifetime = ~0u;
        rw->metric = metric;
        rw->protocol = MIB_IPPROTO_NETMGMT;
        rw->immortal = TRUE;
    }
    if (params->dynamic_data)
    {
        struct nsi_ipv4_forward_dynamic *dynamic = (struct nsi_ipv4_forward_dynamic *)params->dynamic_data + i;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->addr2.S_un.S_addr = dest;
    }
    if (params->static_data)
    {
        struct nsi_ip_forward_static *stat = (struct nsi_ip_forward_static *)params->static_data + i;
        memset(stat, 0, sizeof(*stat));
        stat->origin = NlroManual;
        stat->if_index = if_index;
    }
}

static BOOL ipv4_host_route_listed(const RouteTable *table, DWORD address)
{
    UINT_PTR i;

    for (i = 0; table && i < table->numRoutes; i++)
        if (table->routes[i].dest == address && table->routes[i].mask == INADDR_NONE) return TRUE;
    return FALSE;
}

static DWORD ipv4_interface_route_metric(const RouteTable *table, DWORD if_index)
{
    UINT_PTR i;

    for (i = 0; table && i < table->numRoutes; i++)
        if (table->routes[i].ifIndex == if_index && table->routes[i].mask) return table->routes[i].metric;
    return 0;
}

static DWORD enumerate_ipv4_forward(struct nsi_enumerate_all_ex *params)
{
    RouteTable *table = getRouteTable();
    InterfaceIndexTable *indexes = getInterfaceIndexTable();
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR count = 0, i;

    for (i = 0; table && i < table->numRoutes; i++)
    {
        RouteEntry *row = table->routes + i;
        DWORD next_hop = row->gateway;

        if (next_hop == getInterfaceIPAddrByIndex(row->ifIndex)) next_hop = INADDR_ANY;
        if (want_data && count < params->count)
            fill_ipv4_forward(params, count, row->dest, row->mask, next_hop, row->ifIndex, row->metric);
        count++;
    }

    for (i = 0; indexes && i < indexes->numIndexes; i++)
    {
        DWORD address = getInterfaceIPAddrByIndex(indexes->indexes[i]);

        if (address == INADDR_ANY || address == INADDR_NONE || ipv4_host_route_listed(table, address)) continue;
        if (want_data && count < params->count)
            fill_ipv4_forward(params, count, address, INADDR_NONE, INADDR_ANY, indexes->indexes[i],
                              ipv4_interface_route_metric(table, indexes->indexes[i]));
        count++;
    }

    free(indexes);
    HeapFree(GetProcessHeap(), 0, table);
    return finish_enumeration(params, count, want_data);
}

static DWORD get_ipv4_compartment(struct nsi_get_all_parameters_ex *params)
{
    const UINT *key = params->key;
    MIB_IPSTATS stats;
    HANDLE file;
    DWORD error;

    if (!key || *key != NET_IF_COMPARTMENT_ID_PRIMARY) return ERROR_FILE_NOT_FOUND;
    if (!NT_SUCCESS(openTcpFile(&file, 0))) return ERROR_NOT_SUPPORTED;
    memset(&stats, 0, sizeof(stats));
    error = getIPStats(file, &stats);
    closeTcpFile(file);
    if (error) return error;
    if (params->rw_data)
    {
        struct nsi_ip_cmpt_rw *rw = params->rw_data;
        memset(rw, 0, sizeof(*rw));
        rw->not_forwarding = stats.dwForwarding == MIB_IP_NOT_FORWARDING;
        rw->default_ttl = stats.dwDefaultTTL;
    }
    if (params->dynamic_data)
    {
        struct nsi_ip_cmpt_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->num_ifs = stats.dwNumIf;
        dynamic->num_routes = stats.dwNumRoutes;
        dynamic->num_addrs = stats.dwNumAddr;
    }
    return ERROR_SUCCESS;
}

static DWORD get_ipv4_icmp(struct nsi_get_all_parameters_ex *params)
{
    MIB_ICMP stats;
    struct nsi_ip_icmpstats_dynamic *dynamic = params->dynamic_data;
    DWORD error = getICMPStats(&stats);

    if (error) return error;
    if (!dynamic) return ERROR_SUCCESS;
    memset(dynamic, 0, sizeof(*dynamic));
    dynamic->in_msgs = stats.stats.icmpInStats.dwMsgs;
    dynamic->in_errors = stats.stats.icmpInStats.dwErrors;
    dynamic->in_type_counts[ICMP4_ECHO_REPLY] = stats.stats.icmpInStats.dwEchoReps;
    dynamic->in_type_counts[ICMP4_DST_UNREACH] = stats.stats.icmpInStats.dwDestUnreachs;
    dynamic->in_type_counts[ICMP4_SOURCE_QUENCH] = stats.stats.icmpInStats.dwSrcQuenchs;
    dynamic->in_type_counts[ICMP4_REDIRECT] = stats.stats.icmpInStats.dwRedirects;
    dynamic->in_type_counts[ICMP4_ECHO_REQUEST] = stats.stats.icmpInStats.dwEchos;
    dynamic->in_type_counts[ICMP4_TIME_EXCEEDED] = stats.stats.icmpInStats.dwTimeExcds;
    dynamic->in_type_counts[ICMP4_PARAM_PROB] = stats.stats.icmpInStats.dwParmProbs;
    dynamic->out_msgs = stats.stats.icmpOutStats.dwMsgs;
    dynamic->out_errors = stats.stats.icmpOutStats.dwErrors;
    dynamic->out_type_counts[ICMP4_ECHO_REPLY] = stats.stats.icmpOutStats.dwEchoReps;
    dynamic->out_type_counts[ICMP4_DST_UNREACH] = stats.stats.icmpOutStats.dwDestUnreachs;
    dynamic->out_type_counts[ICMP4_SOURCE_QUENCH] = stats.stats.icmpOutStats.dwSrcQuenchs;
    dynamic->out_type_counts[ICMP4_REDIRECT] = stats.stats.icmpOutStats.dwRedirects;
    dynamic->out_type_counts[ICMP4_ECHO_REQUEST] = stats.stats.icmpOutStats.dwEchos;
    dynamic->out_type_counts[ICMP4_TIME_EXCEEDED] = stats.stats.icmpOutStats.dwTimeExcds;
    dynamic->out_type_counts[ICMP4_PARAM_PROB] = stats.stats.icmpOutStats.dwParmProbs;
    return ERROR_SUCCESS;
}

static DWORD get_ipv4_stats(struct nsi_get_all_parameters_ex *params)
{
    MIB_IPSTATS stats;
    HANDLE file;
    DWORD error;

    if (!NT_SUCCESS(openTcpFile(&file, 0))) return ERROR_NOT_SUPPORTED;
    memset(&stats, 0, sizeof(stats));
    error = getIPStats(file, &stats);
    closeTcpFile(file);
    if (error) return error;
    if (params->dynamic_data)
    {
        struct nsi_ip_ipstats_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->in_recv = stats.dwInReceives;
        dynamic->fwd_dgrams = stats.dwForwDatagrams;
        dynamic->in_delivers = stats.dwInDelivers;
        dynamic->out_reqs = stats.dwOutRequests;
        dynamic->in_hdr_errs = stats.dwInHdrErrors;
        dynamic->in_addr_errs = stats.dwInAddrErrors;
        dynamic->in_unk_protos = stats.dwInUnknownProtos;
        dynamic->reasm_reqds = stats.dwReasmReqds;
        dynamic->reasm_oks = stats.dwReasmOks;
        dynamic->reasm_fails = stats.dwReasmFails;
        dynamic->in_discards = stats.dwInDiscards;
        dynamic->out_no_routes = stats.dwOutNoRoutes;
        dynamic->out_discards = stats.dwOutDiscards;
        dynamic->routing_discards = stats.dwRoutingDiscards;
        dynamic->frag_oks = stats.dwFragOks;
        dynamic->frag_fails = stats.dwFragFails;
        dynamic->frag_creates = stats.dwFragCreates;
    }
    if (params->static_data)
    {
        struct nsi_ip_ipstats_static *stat = params->static_data;
        memset(stat, 0, sizeof(*stat));
        stat->reasm_timeout = stats.dwReasmTimeout;
    }
    return ERROR_SUCCESS;
}

static DWORD get_tcp_stats(struct nsi_get_all_parameters_ex *params)
{
    const USHORT *family = params->key;
    MIB_TCPSTATS stats;
    HANDLE file;
    DWORD error;

    if (!family || (*family != AF_INET && *family != AF_INET6)) return ERROR_NOT_SUPPORTED;
    memset(&stats, 0, sizeof(stats));
    if (!NT_SUCCESS(openTcpFile(&file, 0))) return ERROR_NOT_SUPPORTED;
    error = getTCPStats(file, &stats);
    closeTcpFile(file);
    if (error) return error;
    if (*family == AF_INET6)
    {
        IP6_STATISTICS stats6;

        if (!ipv6_statistics(&stats6)) return ERROR_NOT_SUPPORTED;
        stats.dwActiveOpens = stats6.TcpActiveOpens;
        stats.dwPassiveOpens = stats6.TcpPassiveOpens;
        stats.dwAttemptFails = stats6.TcpAttemptFailures;
        stats.dwEstabResets = stats6.TcpEstablishedResets;
        stats.dwCurrEstab = stats6.TcpCurrentEstablished;
        stats.dwInSegs = (DWORD)stats6.TcpInSegments;
        stats.dwOutSegs = (DWORD)stats6.TcpOutSegments;
        stats.dwRetransSegs = stats6.TcpRetransmittedSegments;
        stats.dwOutRsts = stats6.TcpOutResets;
        stats.dwInErrs = stats6.TcpInErrors;
        stats.dwNumConns = stats6.TcpNumConnections;
    }
    if (params->dynamic_data)
    {
        struct nsi_tcp_stats_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->active_opens = stats.dwActiveOpens;
        dynamic->passive_opens = stats.dwPassiveOpens;
        dynamic->attempt_fails = stats.dwAttemptFails;
        dynamic->est_rsts = stats.dwEstabResets;
        dynamic->cur_est = stats.dwCurrEstab;
        dynamic->in_segs = stats.dwInSegs;
        dynamic->out_segs = stats.dwOutSegs;
        dynamic->retrans_segs = stats.dwRetransSegs;
        dynamic->out_rsts = stats.dwOutRsts;
        dynamic->in_errs = stats.dwInErrs;
        dynamic->num_conns = stats.dwNumConns;
    }
    if (params->static_data)
    {
        struct nsi_tcp_stats_static *stat = params->static_data;
        memset(stat, 0, sizeof(*stat));
        stat->rto_algo = stats.dwRtoAlgorithm;
        stat->rto_min = stats.dwRtoMin;
        stat->rto_max = stats.dwRtoMax;
        stat->max_conns = stats.dwMaxConn;
    }
    return ERROR_SUCCESS;
}

static DWORD enumerate_tcp(struct nsi_enumerate_all_ex *params)
{
    MIB_TCPTABLE_OWNER_MODULE *table = getTcpTable(ClassModule);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR i, count = 0, written = 0;
    ULONG count6;
    IP6_TABLE_HEADER *header6 = ip6_query_table(Ip6TableEndpoints, &count6);
    const IP6_ENDPOINT_ROW *rows6 = (const IP6_ENDPOINT_ROW *)(header6 + 1);

    for (i = 0; table && i < table->dwNumEntries; i++)
    {
        MIB_TCPROW_OWNER_MODULE *row = table->table + i;

        if (params->table == NSI_TCP_ESTAB_TABLE && row->dwState != MIB_TCP_STATE_ESTAB) continue;
        if (params->table == NSI_TCP_LISTEN_TABLE && row->dwState != MIB_TCP_STATE_LISTEN) continue;
        if (want_data && written < params->count)
        {
            if (params->key_data)
            {
                struct nsi_tcp_conn_key *key = (struct nsi_tcp_conn_key *)params->key_data + written;
                memset(key, 0, sizeof(*key));
                key->local.Ipv4.sin_family = AF_INET;
                key->local.Ipv4.sin_addr.S_un.S_addr = row->dwLocalAddr;
                key->local.Ipv4.sin_port = row->dwLocalPort;
                key->remote.Ipv4.sin_family = AF_INET;
                key->remote.Ipv4.sin_addr.S_un.S_addr = row->dwRemoteAddr;
                key->remote.Ipv4.sin_port = row->dwRemotePort;
            }
            if (params->dynamic_data)
            {
                struct nsi_tcp_conn_dynamic *dynamic = (struct nsi_tcp_conn_dynamic *)params->dynamic_data + written;
                memset(dynamic, 0, sizeof(*dynamic));
                dynamic->state = row->dwState;
            }
            if (params->static_data)
            {
                struct nsi_tcp_conn_static *stat = (struct nsi_tcp_conn_static *)params->static_data + written;
                memset(stat, 0, sizeof(*stat));
                stat->pid = row->dwOwningPid;
                stat->create_time = row->liCreateTimestamp.QuadPart;
                stat->mod_info = row->OwningModuleInfo[0];
            }
            written++;
        }
        count++;
    }
    for (i = 0; i < count6; i++)
    {
        const IP6_ENDPOINT_ROW *row = rows6 + i;

        if (row->Protocol != IPPROTO_TCP) continue;
        if (params->table == NSI_TCP_ESTAB_TABLE && row->State != MIB_TCP_STATE_ESTAB) continue;
        if (params->table == NSI_TCP_LISTEN_TABLE && row->State != MIB_TCP_STATE_LISTEN) continue;
        if (want_data && written < params->count)
        {
            if (params->key_data)
            {
                struct nsi_tcp_conn_key *key = (struct nsi_tcp_conn_key *)params->key_data + written;
                memset(key, 0, sizeof(*key));
                fill_ipv6_sockaddr(&key->local, row->LocalAddress, row->LocalScopeId, row->LocalPort);
                fill_ipv6_sockaddr(&key->remote, row->RemoteAddress, row->RemoteScopeId, row->RemotePort);
            }
            if (params->dynamic_data)
            {
                struct nsi_tcp_conn_dynamic *dynamic = (struct nsi_tcp_conn_dynamic *)params->dynamic_data + written;
                memset(dynamic, 0, sizeof(*dynamic));
                dynamic->state = row->State;
            }
            if (params->static_data)
            {
                struct nsi_tcp_conn_static *stat = (struct nsi_tcp_conn_static *)params->static_data + written;
                memset(stat, 0, sizeof(*stat));
                stat->pid = row->ProcessId;
                stat->create_time = row->CreateTime.QuadPart;
                stat->mod_info = row->ModuleInfo;
            }
            written++;
        }
        count++;
    }
    HeapFree(GetProcessHeap(), 0, header6);
    HeapFree(GetProcessHeap(), 0, table);
    return finish_enumeration(params, count, want_data);
}

static DWORD get_udp_stats(struct nsi_get_all_parameters_ex *params)
{
    const USHORT *family = params->key;
    MIB_UDPSTATS stats;
    HANDLE file;
    DWORD error;

    if (!family || (*family != AF_INET && *family != AF_INET6)) return ERROR_NOT_SUPPORTED;
    memset(&stats, 0, sizeof(stats));
    if (*family == AF_INET6)
    {
        IP6_STATISTICS stats6;

        if (!ipv6_statistics(&stats6)) return ERROR_NOT_SUPPORTED;
        stats.dwInDatagrams = (DWORD)stats6.UdpInDatagrams;
        stats.dwNoPorts = stats6.UdpNoPorts;
        stats.dwInErrors = stats6.UdpInErrors;
        stats.dwOutDatagrams = (DWORD)stats6.UdpOutDatagrams;
        stats.dwNumAddrs = stats6.UdpNumAddresses;
    }
    else
    {
        if (!NT_SUCCESS(openTcpFile(&file, 0))) return ERROR_NOT_SUPPORTED;
        error = getUDPStats(file, &stats);
        closeTcpFile(file);
        if (error) return error;
    }
    if (params->dynamic_data)
    {
        struct nsi_udp_stats_dynamic *dynamic = params->dynamic_data;
        memset(dynamic, 0, sizeof(*dynamic));
        dynamic->in_dgrams = stats.dwInDatagrams;
        dynamic->no_ports = stats.dwNoPorts;
        dynamic->in_errs = stats.dwInErrors;
        dynamic->out_dgrams = stats.dwOutDatagrams;
        dynamic->num_addrs = stats.dwNumAddrs;
    }
    return ERROR_SUCCESS;
}

static DWORD enumerate_udp(struct nsi_enumerate_all_ex *params)
{
    MIB_UDPTABLE_OWNER_MODULE *table = getUdpTable(ClassModule);
    BOOL want_data = params->key_size || params->rw_size || params->dynamic_size || params->static_size;
    UINT_PTR count = table ? table->dwNumEntries : 0, limit = count < params->count ? count : params->count;
    UINT_PTR i;
    ULONG count6;
    IP6_TABLE_HEADER *header6 = ip6_query_table(Ip6TableEndpoints, &count6);
    const IP6_ENDPOINT_ROW *rows6 = (const IP6_ENDPOINT_ROW *)(header6 + 1);

    for (i = 0; want_data && i < limit; i++)
    {
        MIB_UDPROW_OWNER_MODULE *row = table->table + i;

        if (params->key_data)
        {
            struct nsi_udp_endpoint_key *key = (struct nsi_udp_endpoint_key *)params->key_data + i;
            memset(key, 0, sizeof(*key));
            key->local.Ipv4.sin_family = AF_INET;
            key->local.Ipv4.sin_addr.S_un.S_addr = row->dwLocalAddr;
            key->local.Ipv4.sin_port = row->dwLocalPort;
        }
        if (params->static_data)
        {
            struct nsi_udp_endpoint_static *stat = (struct nsi_udp_endpoint_static *)params->static_data + i;
            memset(stat, 0, sizeof(*stat));
            stat->pid = row->dwOwningPid;
            stat->create_time = row->liCreateTimestamp.QuadPart;
            stat->flags = row->dwFlags;
            stat->mod_info = row->OwningModuleInfo[0];
        }
    }
    for (i = 0; i < count6; i++)
    {
        const IP6_ENDPOINT_ROW *row = rows6 + i;

        if (row->Protocol != IPPROTO_UDP) continue;
        if (want_data && count < params->count)
        {
            if (params->key_data)
            {
                struct nsi_udp_endpoint_key *key = (struct nsi_udp_endpoint_key *)params->key_data + count;
                memset(key, 0, sizeof(*key));
                fill_ipv6_sockaddr(&key->local, row->LocalAddress, row->LocalScopeId, row->LocalPort);
            }
            if (params->static_data)
            {
                struct nsi_udp_endpoint_static *stat = (struct nsi_udp_endpoint_static *)params->static_data + count;
                memset(stat, 0, sizeof(*stat));
                stat->pid = row->ProcessId;
                stat->create_time = row->CreateTime.QuadPart;
                stat->mod_info = row->ModuleInfo;
            }
        }
        count++;
    }
    HeapFree(GetProcessHeap(), 0, header6);
    HeapFree(GetProcessHeap(), 0, table);
    return finish_enumeration(params, count, want_data);
}

static const struct nsi_table tables[] =
{
    {&NPI_MS_NDIS_MODULEID, NSI_NDIS_IFINFO_TABLE, {sizeof(NET_LUID), sizeof(struct nsi_ndis_ifinfo_rw), sizeof(struct nsi_ndis_ifinfo_dynamic), sizeof(struct nsi_ndis_ifinfo_static)}, enumerate_ndis},
    {&NPI_MS_NDIS_MODULEID, NSI_NDIS_INDEX_LUID_TABLE, {sizeof(UINT), 0, 0, sizeof(NET_LUID)}},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_COMPARTMENT_TABLE, {sizeof(UINT), sizeof(struct nsi_ip_cmpt_rw), sizeof(struct nsi_ip_cmpt_dynamic), 0}, NULL, get_ipv4_compartment},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_ICMPSTATS_TABLE, {0, 0, sizeof(struct nsi_ip_icmpstats_dynamic), 0}, NULL, get_ipv4_icmp},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_IPSTATS_TABLE, {0, 0, sizeof(struct nsi_ip_ipstats_dynamic), sizeof(struct nsi_ip_ipstats_static)}, NULL, get_ipv4_stats},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_INTERFACE_TABLE, {sizeof(struct nsi_ip_interface_key), sizeof(struct nsi_ip_interface_rw), sizeof(struct nsi_ip_interface_dynamic), sizeof(struct nsi_ip_interface_static)}, enumerate_ip_interface},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_UNICAST_TABLE, {sizeof(struct nsi_ipv4_unicast_key), sizeof(struct nsi_ip_unicast_rw), sizeof(struct nsi_ip_unicast_dynamic), sizeof(struct nsi_ip_unicast_static)}, enumerate_ipv4_unicast},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_NEIGHBOUR_TABLE, {sizeof(struct nsi_ipv4_neighbour_key), sizeof(struct nsi_ip_neighbour_rw), sizeof(struct nsi_ip_neighbour_dynamic), 0}, enumerate_ipv4_neighbour},
    {&NPI_MS_IPV4_MODULEID, NSI_IP_FORWARD_TABLE, {sizeof(struct nsi_ipv4_forward_key), sizeof(struct nsi_ip_forward_rw), sizeof(struct nsi_ipv4_forward_dynamic), sizeof(struct nsi_ip_forward_static)}, enumerate_ipv4_forward},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_COMPARTMENT_TABLE, {sizeof(UINT), sizeof(struct nsi_ip_cmpt_rw), sizeof(struct nsi_ip_cmpt_dynamic), 0}, NULL, get_ipv6_compartment},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_ICMPSTATS_TABLE, {0, 0, sizeof(struct nsi_ip_icmpstats_dynamic), 0}, NULL, get_ipv6_icmp},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_IPSTATS_TABLE, {0, 0, sizeof(struct nsi_ip_ipstats_dynamic), sizeof(struct nsi_ip_ipstats_static)}, NULL, get_ipv6_stats},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_INTERFACE_TABLE, {sizeof(struct nsi_ip_interface_key), sizeof(struct nsi_ip_interface_rw), sizeof(struct nsi_ip_interface_dynamic), sizeof(struct nsi_ip_interface_static)}, enumerate_ipv6_interface},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_UNICAST_TABLE, {sizeof(struct nsi_ipv6_unicast_key), sizeof(struct nsi_ip_unicast_rw), sizeof(struct nsi_ip_unicast_dynamic), sizeof(struct nsi_ip_unicast_static)}, enumerate_ipv6_unicast},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_NEIGHBOUR_TABLE, {sizeof(struct nsi_ipv6_neighbour_key), sizeof(struct nsi_ip_neighbour_rw), sizeof(struct nsi_ip_neighbour_dynamic), 0}, enumerate_ipv6_neighbour},
    {&NPI_MS_IPV6_MODULEID, NSI_IP_FORWARD_TABLE, {sizeof(struct nsi_ipv6_forward_key), sizeof(struct nsi_ip_forward_rw), sizeof(struct nsi_ipv6_forward_dynamic), sizeof(struct nsi_ip_forward_static)}, enumerate_ipv6_forward},
    {&NPI_MS_TCP_MODULEID, NSI_TCP_STATS_TABLE, {sizeof(USHORT), 0, sizeof(struct nsi_tcp_stats_dynamic), sizeof(struct nsi_tcp_stats_static)}, NULL, get_tcp_stats},
    {&NPI_MS_TCP_MODULEID, NSI_TCP_ALL_TABLE, {sizeof(struct nsi_tcp_conn_key), 0, sizeof(struct nsi_tcp_conn_dynamic), sizeof(struct nsi_tcp_conn_static)}, enumerate_tcp},
    {&NPI_MS_TCP_MODULEID, NSI_TCP_ESTAB_TABLE, {sizeof(struct nsi_tcp_conn_key), 0, sizeof(struct nsi_tcp_conn_dynamic), sizeof(struct nsi_tcp_conn_static)}, enumerate_tcp},
    {&NPI_MS_TCP_MODULEID, NSI_TCP_LISTEN_TABLE, {sizeof(struct nsi_tcp_conn_key), 0, sizeof(struct nsi_tcp_conn_dynamic), sizeof(struct nsi_tcp_conn_static)}, enumerate_tcp},
    {&NPI_MS_UDP_MODULEID, NSI_UDP_STATS_TABLE, {sizeof(USHORT), 0, sizeof(struct nsi_udp_stats_dynamic), 0}, NULL, get_udp_stats},
    {&NPI_MS_UDP_MODULEID, NSI_UDP_ENDPOINT_TABLE, {sizeof(struct nsi_udp_endpoint_key), 0, 0, sizeof(struct nsi_udp_endpoint_static)}, enumerate_udp},
};

static const struct nsi_table *find_table(const NPI_MODULEID *module, UINT table)
{
    UINT i;

    if (!module) return NULL;
    for (i = 0; i < ARRAY_SIZE(tables); i++)
        if (tables[i].table == table && module_equal(tables[i].module, module)) return tables + i;
    return NULL;
}

static DWORD validate_sizes(const struct nsi_table *table, const UINT sizes[4])
{
    UINT i;

    for (i = 0; i < 4; i++)
        if (sizes[i] && sizes[i] != table->sizes[i]) return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

DWORD nsi_reactos_enumerate_all(struct nsi_enumerate_all_ex *params)
{
    const struct nsi_table *table;
    UINT sizes[4];
    DWORD error;

    if (!params || !params->module) return ERROR_INVALID_PARAMETER;
    table = find_table(params->module, params->table);
    if (!table || !table->enumerate) return ERROR_INVALID_PARAMETER;
    sizes[0] = params->key_size;
    sizes[1] = params->rw_size;
    sizes[2] = params->dynamic_size;
    sizes[3] = params->static_size;
    if ((error = validate_sizes(table, sizes))) return error;
    if ((params->key_size && !params->key_data) || (params->rw_size && !params->rw_data) ||
        (params->dynamic_size && !params->dynamic_data) || (params->static_size && !params->static_data)) return ERROR_INVALID_PARAMETER;
    return table->enumerate(params);
}

static DWORD get_from_enumeration(const struct nsi_table *table, struct nsi_get_all_parameters_ex *params)
{
    struct nsi_enumerate_all_ex enumerate;
    BYTE *data[4] = {NULL};
    UINT_PTR count = 0, i;
    DWORD error, not_found;

    not_found = (module_equal(params->module, &NPI_MS_IPV4_MODULEID) ||
                 module_equal(params->module, &NPI_MS_IPV6_MODULEID)) ? ERROR_NOT_FOUND : ERROR_FILE_NOT_FOUND;
    memset(&enumerate, 0, sizeof(enumerate));
    enumerate.module = params->module;
    enumerate.table = params->table;
    if ((error = table->enumerate(&enumerate))) return error;
    count = enumerate.count;
    if (!count) return not_found;
    data[0] = HeapAlloc(GetProcessHeap(), 0, table->sizes[0] * count);
    if (params->rw_size) data[1] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, table->sizes[1] * count);
    if (params->dynamic_size) data[2] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, table->sizes[2] * count);
    if (params->static_size) data[3] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, table->sizes[3] * count);
    if (!data[0] || (params->rw_size && !data[1]) || (params->dynamic_size && !data[2]) || (params->static_size && !data[3]))
    {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    enumerate.key_data = data[0];
    enumerate.key_size = table->sizes[0];
    enumerate.rw_data = data[1];
    enumerate.rw_size = params->rw_size;
    enumerate.dynamic_data = data[2];
    enumerate.dynamic_size = params->dynamic_size;
    enumerate.static_data = data[3];
    enumerate.static_size = params->static_size;
    enumerate.count = count;
    if ((error = table->enumerate(&enumerate))) goto done;
    error = not_found;
    for (i = 0; i < enumerate.count; i++)
    {
        if (memcmp(data[0] + i * table->sizes[0], params->key, table->sizes[0])) continue;
        if (params->rw_size) memcpy(params->rw_data, data[1] + i * table->sizes[1], table->sizes[1]);
        if (params->dynamic_size) memcpy(params->dynamic_data, data[2] + i * table->sizes[2], table->sizes[2]);
        if (params->static_size) memcpy(params->static_data, data[3] + i * table->sizes[3], table->sizes[3]);
        error = ERROR_SUCCESS;
        break;
    }
done:
    for (i = 0; i < ARRAY_SIZE(data); i++) HeapFree(GetProcessHeap(), 0, data[i]);
    return error;
}

DWORD nsi_reactos_get_all(struct nsi_get_all_parameters_ex *params)
{
    const struct nsi_table *table;
    UINT sizes[4];
    DWORD error;

    if (!params || !params->module) return ERROR_INVALID_PARAMETER;
    table = find_table(params->module, params->table);
    if (!table) return ERROR_INVALID_PARAMETER;
    sizes[0] = params->key_size;
    sizes[1] = params->rw_size;
    sizes[2] = params->dynamic_size;
    sizes[3] = params->static_size;
    if ((error = validate_sizes(table, sizes))) return error;
    if (params->key_size != table->sizes[0] || (params->key_size && !params->key)) return ERROR_INVALID_PARAMETER;
    if ((params->rw_size && !params->rw_data) || (params->dynamic_size && !params->dynamic_data) ||
        (params->static_size && !params->static_data)) return ERROR_INVALID_PARAMETER;
    if (table->get) return table->get(params);
    if (!table->enumerate) return ERROR_INVALID_PARAMETER;
    return get_from_enumeration(table, params);
}

DWORD nsi_reactos_get_parameter(struct nsi_get_parameter_ex *params)
{
    const struct nsi_table *table;
    struct nsi_get_all_parameters_ex get;
    BYTE *block;
    UINT size;
    DWORD error;

    if (!params || !params->module || params->param_type > NSI_PARAM_TYPE_STATIC) return ERROR_INVALID_PARAMETER;
    table = find_table(params->module, params->table);
    if (!table || params->key_size != table->sizes[0] || (params->key_size && !params->key)) return ERROR_INVALID_PARAMETER;
    size = table->sizes[params->param_type + 1];
    if (!size || !params->data || params->data_offset > size || params->data_size > size - params->data_offset) return ERROR_INVALID_PARAMETER;
    if (module_equal(params->module, &NPI_MS_NDIS_MODULEID) && params->table == NSI_NDIS_INDEX_LUID_TABLE)
    {
        UINT index = *(const UINT *)params->key;
        InterfaceIndexTable *indexes = getInterfaceIndexTable();
        UINT i;

        error = ERROR_FILE_NOT_FOUND;
        for (i = 0; indexes && i < indexes->numIndexes; i++)
            if (indexes->indexes[i] == index)
            {
                NET_LUID luid = interface_luid(index);
                memcpy(params->data, (BYTE *)&luid + params->data_offset, params->data_size);
                error = ERROR_SUCCESS;
                break;
            }
        free(indexes);
        return error;
    }
    if (!(block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size))) return ERROR_OUTOFMEMORY;
    memset(&get, 0, sizeof(get));
    get.module = params->module;
    get.table = params->table;
    get.first_arg = params->first_arg;
    get.key = params->key;
    get.key_size = params->key_size;
    if (params->param_type == NSI_PARAM_TYPE_RW)
    {
        get.rw_data = block;
        get.rw_size = size;
    }
    else if (params->param_type == NSI_PARAM_TYPE_DYNAMIC)
    {
        get.dynamic_data = block;
        get.dynamic_size = size;
    }
    else
    {
        get.static_data = block;
        get.static_size = size;
    }
    error = nsi_reactos_get_all(&get);
    if (!error) memcpy(params->data, block + params->data_offset, params->data_size);
    HeapFree(GetProcessHeap(), 0, block);
    return error;
}

struct nsi_notification
{
    struct nsi_notification *next;
    NPI_MODULEID module;
    UINT table;
    OVERLAPPED *overlapped;
    ULONG hash;
};

static CRITICAL_SECTION notification_cs;
static struct nsi_notification *notifications;
static HANDLE notification_wake;
static HANDLE notification_stop;
static HANDLE notification_complete;
static HANDLE notification_thread;

static ULONG hash_data(ULONG hash, const void *data, SIZE_T size)
{
    const BYTE *bytes = data;

    while (size--)
    {
        hash ^= *bytes++;
        hash *= 16777619;
    }
    return hash;
}

static DWORD table_hash(const NPI_MODULEID *module, UINT table_id, ULONG *result)
{
    const struct nsi_table *table = find_table(module, table_id);
    struct nsi_enumerate_all_ex params;
    BYTE *key = NULL, *rw = NULL, *dynamic = NULL, *stat = NULL;
    UINT_PTR count;
    UINT dynamic_size = 0;
    DWORD error;
    ULONG hash = 2166136261u;

    if (!table || !table->enumerate) return ERROR_INVALID_PARAMETER;
    /* Interface connectivity lives in the dynamic block. Include it so a
       retained Network List Manager sees link changes, but do not hash the
       traffic counters or continuously changing ages of other tables. */
    if (table_id == NSI_IP_INTERFACE_TABLE &&
        (module_equal(module, &NPI_MS_IPV4_MODULEID) || module_equal(module, &NPI_MS_IPV6_MODULEID)))
        dynamic_size = table->sizes[2];
    memset(&params, 0, sizeof(params));
    params.module = module;
    params.table = table_id;
    if ((error = table->enumerate(&params))) return error;
    count = params.count;
    if (count && table->sizes[0]) key = HeapAlloc(GetProcessHeap(), 0, count * table->sizes[0]);
    if (count && table->sizes[1]) rw = HeapAlloc(GetProcessHeap(), 0, count * table->sizes[1]);
    if (count && dynamic_size) dynamic = HeapAlloc(GetProcessHeap(), 0, count * dynamic_size);
    if (count && table->sizes[3]) stat = HeapAlloc(GetProcessHeap(), 0, count * table->sizes[3]);
    if (count && ((!key && table->sizes[0]) || (!rw && table->sizes[1]) ||
                  (!dynamic && dynamic_size) || (!stat && table->sizes[3])))
    {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    params.key_data = key;
    params.key_size = table->sizes[0];
    params.rw_data = rw;
    params.rw_size = table->sizes[1];
    params.dynamic_data = dynamic;
    params.dynamic_size = dynamic_size;
    params.static_data = stat;
    params.static_size = table->sizes[3];
    params.count = count;
    if ((error = table->enumerate(&params))) goto done;
    hash = hash_data(hash, &count, sizeof(count));
    if (key) hash = hash_data(hash, key, count * table->sizes[0]);
    if (rw) hash = hash_data(hash, rw, count * table->sizes[1]);
    if (dynamic) hash = hash_data(hash, dynamic, count * dynamic_size);
    if (stat) hash = hash_data(hash, stat, count * table->sizes[3]);
    *result = hash;
done:
    HeapFree(GetProcessHeap(), 0, key);
    HeapFree(GetProcessHeap(), 0, rw);
    HeapFree(GetProcessHeap(), 0, dynamic);
    HeapFree(GetProcessHeap(), 0, stat);
    return error;
}

static void complete_notification(struct nsi_notification *notification, NTSTATUS status)
{
    notification->overlapped->InternalHigh = 0;
    InterlockedExchange((LONG *)&notification->overlapped->Internal, status);
    SetEvent(notification->overlapped->hEvent ? notification->overlapped->hEvent : notification_complete);
}

struct nsi_table_state
{
    NPI_MODULEID module;
    UINT table;
    DWORD error;
    ULONG hash;
};

static DWORD cached_table_hash(struct nsi_table_state *states, ULONG *count, ULONG capacity,
                               const NPI_MODULEID *module, UINT table, ULONG *hash)
{
    ULONG index;

    for (index = 0; index < *count; index++)
    {
        if (states[index].table == table && !memcmp(&states[index].module, module, sizeof(*module)))
        {
            *hash = states[index].hash;
            return states[index].error;
        }
    }
    if (*count == capacity) return table_hash(module, table, hash);
    states[*count].module = *module;
    states[*count].table = table;
    states[*count].error = table_hash(module, table, &states[*count].hash);
    *hash = states[*count].hash;
    return states[(*count)++].error;
}

static DWORD WINAPI notification_worker(void *arg)
{
    HANDLE handles[2] = {notification_stop, notification_wake};

    for (;;)
    {
        struct nsi_notification **cursor;
        struct nsi_table_state states[16];
        ULONG state_count = 0;

        if (WaitForMultipleObjects(ARRAY_SIZE(handles), handles, FALSE, 1000) == WAIT_OBJECT_0) break;
        EnterCriticalSection(&notification_cs);
        cursor = &notifications;
        while (*cursor)
        {
            struct nsi_notification *notification = *cursor;
            ULONG hash;

            if (!cached_table_hash(states, &state_count, ARRAY_SIZE(states), &notification->module,
                                   notification->table, &hash) && hash != notification->hash)
            {
                *cursor = notification->next;
                complete_notification(notification, STATUS_SUCCESS);
                HeapFree(GetProcessHeap(), 0, notification);
                continue;
            }
            cursor = &notification->next;
        }
        LeaveCriticalSection(&notification_cs);
    }
    return 0;
}

BOOL nsi_reactos_init(void)
{
    InitializeCriticalSection(&notification_cs);
    notification_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    notification_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    notification_complete = CreateEventW(NULL, FALSE, FALSE, NULL);
    return notification_wake && notification_stop && notification_complete;
}

void nsi_reactos_cleanup(void)
{
    struct nsi_notification *notification;

    if (notification_stop) SetEvent(notification_stop);
    if (notification_thread) WaitForSingleObject(notification_thread, 2000);
    while ((notification = notifications))
    {
        notifications = notification->next;
        complete_notification(notification, STATUS_CANCELLED);
        HeapFree(GetProcessHeap(), 0, notification);
    }
    CloseHandle(notification_thread);
    CloseHandle(notification_complete);
    CloseHandle(notification_stop);
    CloseHandle(notification_wake);
    DeleteCriticalSection(&notification_cs);
}

DWORD nsi_reactos_cancel_change_notification(OVERLAPPED *overlapped)
{
    struct nsi_notification **cursor;

    if (!overlapped) return ERROR_NOT_FOUND;
    EnterCriticalSection(&notification_cs);
    for (cursor = &notifications; *cursor; cursor = &(*cursor)->next)
    {
        struct nsi_notification *notification = *cursor;

        if (notification->overlapped != overlapped) continue;
        *cursor = notification->next;
        complete_notification(notification, STATUS_CANCELLED);
        HeapFree(GetProcessHeap(), 0, notification);
        LeaveCriticalSection(&notification_cs);
        return ERROR_SUCCESS;
    }
    LeaveCriticalSection(&notification_cs);
    return ERROR_NOT_FOUND;
}

DWORD nsi_reactos_request_change_notification(struct nsi_request_change_notification_ex *params)
{
    struct nsi_notification *notification;
    OVERLAPPED local;
    DWORD bytes, error;

    if (!params || !find_table(params->module, params->table) || !find_table(params->module, params->table)->enumerate) return ERROR_INVALID_PARAMETER;
    if (!params->ovr)
    {
        memset(&local, 0, sizeof(local));
        if (!(local.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL))) return GetLastError();
        params->ovr = &local;
        error = nsi_reactos_request_change_notification(params);
        if (error == ERROR_IO_PENDING) error = GetOverlappedResult(notification_complete, &local, &bytes, TRUE) ? ERROR_SUCCESS : GetLastError();
        CloseHandle(local.hEvent);
        params->ovr = NULL;
        return error;
    }
    if (!(notification = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*notification)))) return ERROR_OUTOFMEMORY;
    notification->module = *params->module;
    notification->table = params->table;
    notification->overlapped = params->ovr;
    if ((error = table_hash(params->module, params->table, &notification->hash)))
    {
        HeapFree(GetProcessHeap(), 0, notification);
        return error;
    }
    params->ovr->Internal = STATUS_PENDING;
    params->ovr->InternalHigh = 0;
    if (params->ovr->hEvent) ResetEvent(params->ovr->hEvent);
    EnterCriticalSection(&notification_cs);
    notification->next = notifications;
    notifications = notification;
    if (!notification_thread) notification_thread = CreateThread(NULL, 0, notification_worker, NULL, 0, NULL);
    LeaveCriticalSection(&notification_cs);
    if (!notification_thread)
    {
        nsi_reactos_cancel_change_notification(params->ovr);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SetEvent(notification_wake);
    if (params->handle) *params->handle = notification_complete;
    return ERROR_IO_PENDING;
}
