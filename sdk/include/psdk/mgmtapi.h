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

#endif /* _INC_MGMTAPI */
