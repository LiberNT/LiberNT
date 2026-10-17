#pragma once

#define IFENT_SOFTWARE_LOOPBACK 24 /* This is an SNMP constant from rfc1213 */

NTSTATUS GetInterfaceIPv4Address( PIP_INTERFACE Interface,
				  ULONG Type,
				  PULONG Address );
UINT CountInterfaces(VOID);
UINT CountInterfaceAddresses( PIP_INTERFACE Interface );
NTSTATUS GetInterfaceSpeed( PIP_INTERFACE Interface, PUINT Speed );
NTSTATUS GetInterfaceName( PIP_INTERFACE Interface, PCHAR NameBuffer,
			   UINT NameMaxLen );
VOID GetInterfaceConnectionStatus( PIP_INTERFACE Interface, PULONG OperStatus );
BOOLEAN IsInterfaceOperational(PIP_INTERFACE Interface);
PIP_INTERFACE FindOnLinkInterface(PIP_ADDRESS Address);
PIP_INTERFACE GetDefaultInterface(VOID);
PIP_INTERFACE GetMulticastInterface(ULONG Selector);
BOOLEAN HasPrefix(PIP_ADDRESS Address, PIP_ADDRESS Prefix, UINT Length);
NTSTATUS IPAddInterfaceAddress( PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Address,
                                IPv4_RAW_ADDRESS Netmask, PULONG Context, PULONG Instance );
NTSTATUS IPDeleteInterfaceAddress( ULONG Context );
VOID IPDeleteInterfaceAddresses( PIP_INTERFACE Interface );
BOOLEAN IsInterfaceAddress( PIP_INTERFACE Interface, PIP_ADDRESS Address );
BOOLEAN IsInterfaceBroadcast( PIP_INTERFACE Interface, PIP_ADDRESS Address );
BOOLEAN IsInterfaceOnLink( PIP_INTERFACE Interface, PIP_ADDRESS Address );
BOOLEAN GetInterfaceSourceAddress( PIP_INTERFACE Interface, PIP_ADDRESS Destination, PIP_ADDRESS NextHop,
                                   PIP_ADDRESS Source );
UINT CopyInterfaceAddresses( PIP_INTERFACE Interface, PIPADDR_ENTRY Entries, UINT Count );
