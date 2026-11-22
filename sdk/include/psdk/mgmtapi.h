/*
 * Copyright 2024 Alex Henrie
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

#ifndef _INC_MGMTAPI
#define _INC_MGMTAPI

#include <snmp.h>

typedef void *LPSNMP_MGR_SESSION;

#ifndef MGMCTL_SETAGENTPORT
#define MGMCTL_SETAGENTPORT 0x01
#endif
#ifndef SNMP_MGMTAPI_AGAIN
#define SNMP_MGMTAPI_AGAIN 45
#endif
#ifndef SNMP_MGMTAPI_INVALID_BUFFER
#define SNMP_MGMTAPI_INVALID_BUFFER 48
#endif
#ifndef SNMP_MGMTAPI_INVALID_CTL
#define SNMP_MGMTAPI_INVALID_CTL 46
#endif
#ifndef SNMP_MGMTAPI_INVALID_SESSION
#define SNMP_MGMTAPI_INVALID_SESSION 47
#endif
#ifndef SNMP_MGMTAPI_NOTRAPS
#define SNMP_MGMTAPI_NOTRAPS 44
#endif
#ifndef SNMP_MGMTAPI_SELECT_FDERRORS
#define SNMP_MGMTAPI_SELECT_FDERRORS 41
#endif
#ifndef SNMP_MGMTAPI_TIMEOUT
#define SNMP_MGMTAPI_TIMEOUT 40
#endif
#ifndef SNMP_MGMTAPI_TRAP_DUPINIT
#define SNMP_MGMTAPI_TRAP_DUPINIT 43
#endif
#ifndef SNMP_MGMTAPI_TRAP_ERRORS
#define SNMP_MGMTAPI_TRAP_ERRORS 42
#endif

#ifdef __cplusplus
extern "C" {
#endif

LPSNMP_MGR_SESSION WINAPI SnmpMgrOpen(LPSTR lpAgentAddress, LPSTR lpAgentCommunity, INT nTimeOut, INT nRetries);
BOOL WINAPI SnmpMgrTrapListen(HANDLE *phTrapAvailable);

/* TODO: LiberNT does not implement the functions below yet */
BOOL SNMP_FUNC_TYPE SnmpMgrClose(LPSNMP_MGR_SESSION session);
BOOL SNMP_FUNC_TYPE SnmpMgrCtl(LPSNMP_MGR_SESSION session, DWORD dwCtlCode, LPVOID lpvInBuffer, DWORD cbInBuffer, LPVOID lpvOUTBuffer, DWORD cbOUTBuffer, LPDWORD lpcbBytesReturned);
BOOL SNMP_FUNC_TYPE SnmpMgrGetTrap(AsnObjectIdentifier *enterprise, AsnNetworkAddress *IPAddress, AsnInteger32 *genericTrap, AsnInteger32 *specificTrap, AsnTimeticks *timeStamp, SnmpVarBindList *variableBindings);
BOOL SNMP_FUNC_TYPE SnmpMgrGetTrapEx(AsnObjectIdentifier *enterprise, AsnNetworkAddress *agentAddress, AsnNetworkAddress *sourceAddress, AsnInteger32 *genericTrap, AsnInteger32 *specificTrap, AsnOctetString *community, AsnTimeticks *timeStamp, SnmpVarBindList *variableBindings);
BOOL SNMP_FUNC_TYPE SnmpMgrOidToStr(AsnObjectIdentifier *oid, LPSTR *string);
SNMPAPI SNMP_FUNC_TYPE SnmpMgrRequest(LPSNMP_MGR_SESSION session, BYTE requestType, SnmpVarBindList *variableBindings, AsnInteger32 *errorStatus, AsnInteger32 *errorIndex);
BOOL SNMP_FUNC_TYPE SnmpMgrStrToOid(LPSTR string, AsnObjectIdentifier *oid);

#ifdef __cplusplus
}
#endif

#endif /* _INC_MGMTAPI */
