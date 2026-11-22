/*
 * Copyright (C) 2006 Dmitry Timoshkov
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

#ifndef __WINE_NTDSAPI_H
#define __WINE_NTDSAPI_H

/* FIXME: #include <schedule.h> */

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI DsClientMakeSpnForTargetServerA(LPCSTR, LPCSTR, DWORD*, LPSTR);
DWORD WINAPI DsClientMakeSpnForTargetServerW(LPCWSTR, LPCWSTR, DWORD*, LPWSTR);
#define DsClientMakeSpnForTargetServer WINELIB_NAME_AW(DsClientMakeSpnForTargetServer)

DWORD WINAPI DsMakeSpnA(LPCSTR, LPCSTR, LPCSTR, USHORT, LPCSTR, DWORD*, LPSTR);
DWORD WINAPI DsMakeSpnW(LPCWSTR, LPCWSTR, LPCWSTR, USHORT, LPCWSTR, DWORD*, LPWSTR);
#define DsMakeSpn WINELIB_NAME_AW(DsMakeSpn)

typedef enum
{
    DS_NAME_NO_FLAGS              = 0x0,
    DS_NAME_FLAG_SYNTACTICAL_ONLY = 0x1,
    DS_NAME_FLAG_EVAL_AT_DC       = 0x2,
    DS_NAME_FLAG_GCVERIFY         = 0x4,
    DS_NAME_FLAG_TRUST_REFERRAL   = 0x8
} DS_NAME_FLAGS;

typedef enum
{
    DS_UNKNOWN_NAME            = 0,
    DS_FQDN_1779_NAME          = 1,
    DS_NT4_ACCOUNT_NAME        = 2,
    DS_DISPLAY_NAME            = 3,
    DS_UNIQUE_ID_NAME          = 6,
    DS_CANONICAL_NAME          = 7,
    DS_USER_PRINCIPAL_NAME     = 8,
    DS_CANONICAL_NAME_EX       = 9,
    DS_SERVICE_PRINCIPAL_NAME  = 10,
    DS_SID_OR_SID_HISTORY_NAME = 11,
    DS_DNS_DOMAIN_NAME         = 12
} DS_NAME_FORMAT;

typedef enum
{
    DS_SPN_DNS_HOST    = 0,
    DS_SPN_DN_HOST     = 1,
    DS_SPN_NB_HOST     = 2,
    DS_SPN_DOMAIN      = 3,
    DS_SPN_NB_DOMAIN   = 4,
    DS_SPN_SERVICE     = 5
} DS_SPN_NAME_TYPE;

typedef enum
{
    DS_SPN_ADD_SPN_OP     = 0,
    DS_SPN_REPLACE_SPN_OP = 1,
    DS_SPN_DELETE_SPN_OP  = 2
} DS_SPN_WRITE_OP;

typedef struct
{
    DWORD status;
    LPSTR pDomain;
    LPSTR pName;
} DS_NAME_RESULT_ITEMA, *PDS_NAME_RESULT_ITEMA;

typedef struct
{
    DWORD  status;
    LPWSTR pDomain;
    LPWSTR pName;
} DS_NAME_RESULT_ITEMW, *PDS_NAME_RESULT_ITEMW;

DECL_WINELIB_TYPE_AW(DS_NAME_RESULT_ITEM)
DECL_WINELIB_TYPE_AW(PDS_NAME_RESULT_ITEM)

typedef struct
{
    DWORD                 cItems;
    PDS_NAME_RESULT_ITEMA rItems;
} DS_NAME_RESULTA, *PDS_NAME_RESULTA;

typedef struct
{
    DWORD                 cItems;
    PDS_NAME_RESULT_ITEMW rItems;
} DS_NAME_RESULTW, *PDS_NAME_RESULTW;

DECL_WINELIB_TYPE_AW(DS_NAME_RESULT)
DECL_WINELIB_TYPE_AW(PDS_NAME_RESULT)

DWORD WINAPI DsCrackNamesA(HANDLE handle, DS_NAME_FLAGS flags, DS_NAME_FORMAT offered, DS_NAME_FORMAT desired, DWORD num, const CHAR **names, PDS_NAME_RESULTA *result);
DWORD WINAPI DsCrackNamesW(HANDLE handle, DS_NAME_FLAGS flags, DS_NAME_FORMAT offered, DS_NAME_FORMAT desired, DWORD num, const WCHAR **names, PDS_NAME_RESULTW *result);
#define DsCrackNames WINELIB_NAME_AW(DsCrackNames)
DWORD WINAPI DsServerRegisterSpnA(DS_SPN_WRITE_OP operation, LPCSTR ServiceClass, LPCSTR UserObjectDN);
DWORD WINAPI DsServerRegisterSpnW(DS_SPN_WRITE_OP operation, LPCWSTR ServiceClass, LPCWSTR UserObjectDN);
#define DsServerRegisterSpn WINELIB_NAME_AW(DsServerRegisterSpn)

#ifdef __cplusplus
}
#endif

#ifndef ACTRL_DS_CONTROL_ACCESS
#define ACTRL_DS_CONTROL_ACCESS 0x00000100
#endif
#ifndef ACTRL_DS_CREATE_CHILD
#define ACTRL_DS_CREATE_CHILD 0x00000001
#endif
#ifndef ACTRL_DS_DELETE_CHILD
#define ACTRL_DS_DELETE_CHILD 0x00000002
#endif
#ifndef ACTRL_DS_DELETE_TREE
#define ACTRL_DS_DELETE_TREE 0x00000040
#endif
#ifndef ACTRL_DS_LIST
#define ACTRL_DS_LIST 0x00000004
#endif
#ifndef ACTRL_DS_LIST_OBJECT
#define ACTRL_DS_LIST_OBJECT 0x00000080
#endif
#ifndef ACTRL_DS_OPEN
#define ACTRL_DS_OPEN 0x00000000
#endif
#ifndef ACTRL_DS_READ_PROP
#define ACTRL_DS_READ_PROP 0x00000010
#endif
#ifndef ACTRL_DS_SELF
#define ACTRL_DS_SELF 0x00000008
#endif
#ifndef ACTRL_DS_WRITE_PROP
#define ACTRL_DS_WRITE_PROP 0x00000020
#endif
#ifndef ADAM_REPL_AUTHENTICATION_MODE_MUTUAL_AUTH_REQUIRED
#if (!defined(MIDL_PASS))
#define ADAM_REPL_AUTHENTICATION_MODE_MUTUAL_AUTH_REQUIRED 2
#endif
#endif
#ifndef ADAM_REPL_AUTHENTICATION_MODE_NEGOTIATE
#if (!defined(MIDL_PASS))
#define ADAM_REPL_AUTHENTICATION_MODE_NEGOTIATE 1
#endif
#endif
#ifndef DS_BEHAVIOR_WIN2003
#define DS_BEHAVIOR_WIN2003 2
#endif
#ifndef DS_BEHAVIOR_WIN2003_WITH_MIXED_DOMAINS
#define DS_BEHAVIOR_WIN2003_WITH_MIXED_DOMAINS 1
#endif
#ifndef DS_BEHAVIOR_WIN2008
#define DS_BEHAVIOR_WIN2008 3
#endif
#ifndef DS_BEHAVIOR_WIN2008R2
#define DS_BEHAVIOR_WIN2008R2 4
#endif
#ifndef DS_BEHAVIOR_WIN2012
#define DS_BEHAVIOR_WIN2012 5
#endif
#ifndef DS_BEHAVIOR_WIN2012R2
#define DS_BEHAVIOR_WIN2012R2 6
#endif
#ifndef DS_BEHAVIOR_WIN2016
#define DS_BEHAVIOR_WIN2016 7
#endif
#ifndef DS_BEHAVIOR_WIN2025
#define DS_BEHAVIOR_WIN2025 10
#endif
#ifndef DS_INSTANCETYPE_IS_NC_HEAD
#define DS_INSTANCETYPE_IS_NC_HEAD 0x00000001
#endif
#ifndef DS_INSTANCETYPE_NC_COMING
#define DS_INSTANCETYPE_NC_COMING 0x00000010
#endif
#ifndef DS_INSTANCETYPE_NC_GOING
#define DS_INSTANCETYPE_NC_GOING 0x00000020
#endif
#ifndef DS_INSTANCETYPE_NC_IS_WRITEABLE
#define DS_INSTANCETYPE_NC_IS_WRITEABLE 0x00000004
#endif
#ifndef DS_LIST_ACCOUNT_OBJECT_FOR_SERVER
#define DS_LIST_ACCOUNT_OBJECT_FOR_SERVER 2
#endif
#ifndef DS_LIST_DNS_HOST_NAME_FOR_SERVER
#define DS_LIST_DNS_HOST_NAME_FOR_SERVER 1
#endif
#ifndef DS_REPADD_ASYNCHRONOUS_OPERATION
#define DS_REPADD_ASYNCHRONOUS_OPERATION 0x00000001
#endif
#ifndef DS_REPADD_ASYNCHRONOUS_REPLICA
#define DS_REPADD_ASYNCHRONOUS_REPLICA 0x00000020
#endif
#ifndef DS_REPADD_CRITICAL
#define DS_REPADD_CRITICAL 0x00000800
#endif
#ifndef DS_REPADD_DISABLE_NOTIFICATION
#define DS_REPADD_DISABLE_NOTIFICATION 0x00000040
#endif
#ifndef DS_REPADD_DISABLE_PERIODIC
#define DS_REPADD_DISABLE_PERIODIC 0x00000080
#endif
#ifndef DS_REPADD_INITIAL
#define DS_REPADD_INITIAL 0x00000004
#endif
#ifndef DS_REPADD_INTERSITE_MESSAGING
#define DS_REPADD_INTERSITE_MESSAGING 0x00000010
#endif
#ifndef DS_REPADD_NEVER_NOTIFY
#define DS_REPADD_NEVER_NOTIFY 0x00000200
#endif
#ifndef DS_REPADD_NONGC_RO_REPLICA
#define DS_REPADD_NONGC_RO_REPLICA 0x01000000
#endif
#ifndef DS_REPADD_PERIODIC
#define DS_REPADD_PERIODIC 0x00000008
#endif
#ifndef DS_REPADD_SELECT_SECRETS
#define DS_REPADD_SELECT_SECRETS 0x00001000
#endif
#ifndef DS_REPADD_TWO_WAY
#define DS_REPADD_TWO_WAY 0x00000400
#endif
#ifndef DS_REPADD_USE_COMPRESSION
#define DS_REPADD_USE_COMPRESSION 0x00000100
#endif
#ifndef DS_REPADD_WRITEABLE
#define DS_REPADD_WRITEABLE 0x00000002
#endif
#ifndef DS_REPDEL_ASYNCHRONOUS_OPERATION
#define DS_REPDEL_ASYNCHRONOUS_OPERATION 0x00000001
#endif
#ifndef DS_REPDEL_IGNORE_ERRORS
#define DS_REPDEL_IGNORE_ERRORS 0x00000008
#endif
#ifndef DS_REPDEL_INTERSITE_MESSAGING
#define DS_REPDEL_INTERSITE_MESSAGING 0x00000004
#endif
#ifndef DS_REPDEL_LOCAL_ONLY
#define DS_REPDEL_LOCAL_ONLY 0x00000010
#endif
#ifndef DS_REPDEL_NO_SOURCE
#define DS_REPDEL_NO_SOURCE 0x00000020
#endif
#ifndef DS_REPDEL_REF_OK
#define DS_REPDEL_REF_OK 0x00000040
#endif
#ifndef DS_REPDEL_WRITEABLE
#define DS_REPDEL_WRITEABLE 0x00000002
#endif
#ifndef DS_REPL_INFO_FLAG_IMPROVE_LINKED_ATTRS
#define DS_REPL_INFO_FLAG_IMPROVE_LINKED_ATTRS 0x00000001
#endif
#ifndef DS_REPL_NBR_COMPRESS_CHANGES
#define DS_REPL_NBR_COMPRESS_CHANGES 0x10000000
#endif
#ifndef DS_REPL_NBR_DISABLE_SCHEDULED_SYNC
#define DS_REPL_NBR_DISABLE_SCHEDULED_SYNC 0x08000000
#endif
#ifndef DS_REPL_NBR_DO_SCHEDULED_SYNCS
#define DS_REPL_NBR_DO_SCHEDULED_SYNCS 0x00000040
#endif
#ifndef DS_REPL_NBR_FULL_SYNC_IN_PROGRESS
#define DS_REPL_NBR_FULL_SYNC_IN_PROGRESS 0x00010000
#endif
#ifndef DS_REPL_NBR_FULL_SYNC_NEXT_PACKET
#define DS_REPL_NBR_FULL_SYNC_NEXT_PACKET 0x00020000
#endif
#ifndef DS_REPL_NBR_GCSPN
#define DS_REPL_NBR_GCSPN 0x00100000
#endif
#ifndef DS_REPL_NBR_IGNORE_CHANGE_NOTIFICATIONS
#define DS_REPL_NBR_IGNORE_CHANGE_NOTIFICATIONS 0x04000000
#endif
#ifndef DS_REPL_NBR_NEVER_SYNCED
#define DS_REPL_NBR_NEVER_SYNCED 0x00200000
#endif
#ifndef DS_REPL_NBR_NONGC_RO_REPLICA
#define DS_REPL_NBR_NONGC_RO_REPLICA 0x00000400
#endif
#ifndef DS_REPL_NBR_NO_CHANGE_NOTIFICATIONS
#define DS_REPL_NBR_NO_CHANGE_NOTIFICATIONS 0x20000000
#endif
#ifndef DS_REPL_NBR_PARTIAL_ATTRIBUTE_SET
#define DS_REPL_NBR_PARTIAL_ATTRIBUTE_SET 0x40000000
#endif
#ifndef DS_REPL_NBR_PREEMPTED
#define DS_REPL_NBR_PREEMPTED 0x01000000
#endif
#ifndef DS_REPL_NBR_RETURN_OBJECT_PARENTS
#define DS_REPL_NBR_RETURN_OBJECT_PARENTS 0x00000800
#endif
#ifndef DS_REPL_NBR_SELECT_SECRETS
#define DS_REPL_NBR_SELECT_SECRETS 0x00001000
#endif
#ifndef DS_REPL_NBR_SYNC_ON_STARTUP
#define DS_REPL_NBR_SYNC_ON_STARTUP 0x00000020
#endif
#ifndef DS_REPL_NBR_TWO_WAY_SYNC
#define DS_REPL_NBR_TWO_WAY_SYNC 0x00000200
#endif
#ifndef DS_REPL_NBR_USE_ASYNC_INTERSITE_TRANSPORT
#define DS_REPL_NBR_USE_ASYNC_INTERSITE_TRANSPORT 0x00000080
#endif
#ifndef DS_REPL_NBR_WRITEABLE
#define DS_REPL_NBR_WRITEABLE 0x00000010
#endif
#ifndef DS_REPMOD_ASYNCHRONOUS_OPERATION
#define DS_REPMOD_ASYNCHRONOUS_OPERATION 0x00000001
#endif
#ifndef DS_REPMOD_UPDATE_FLAGS
#define DS_REPMOD_UPDATE_FLAGS 0x00000001
#endif
#ifndef DS_REPMOD_UPDATE_INSTANCE
#define DS_REPMOD_UPDATE_INSTANCE 0x00000002
#endif
#ifndef DS_REPMOD_UPDATE_RESULT
#define DS_REPMOD_UPDATE_RESULT 0x00000008
#endif
#ifndef DS_REPMOD_UPDATE_SCHEDULE
#define DS_REPMOD_UPDATE_SCHEDULE 0x00000004
#endif
#ifndef DS_REPMOD_UPDATE_TRANSPORT
#define DS_REPMOD_UPDATE_TRANSPORT 0x00000010
#endif
#ifndef DS_REPMOD_WRITEABLE
#define DS_REPMOD_WRITEABLE 0x00000002
#endif
#ifndef DS_REPSYNCALL_ABORT_IF_SERVER_UNAVAILABLE
#define DS_REPSYNCALL_ABORT_IF_SERVER_UNAVAILABLE 0x00000001
#endif
#ifndef DS_REPSYNCALL_CROSS_SITE_BOUNDARIES
#define DS_REPSYNCALL_CROSS_SITE_BOUNDARIES 0x00000040
#endif
#ifndef DS_REPSYNCALL_DO_NOT_SYNC
#define DS_REPSYNCALL_DO_NOT_SYNC 0x00000008
#endif
#ifndef DS_REPSYNCALL_ID_SERVERS_BY_DN
#define DS_REPSYNCALL_ID_SERVERS_BY_DN 0x00000004
#endif
#ifndef DS_REPSYNCALL_PUSH_CHANGES_OUTWARD
#define DS_REPSYNCALL_PUSH_CHANGES_OUTWARD 0x00000020
#endif
#ifndef DS_REPSYNCALL_SKIP_INITIAL_CHECK
#define DS_REPSYNCALL_SKIP_INITIAL_CHECK 0x00000010
#endif
#ifndef DS_REPSYNCALL_SYNC_ADJACENT_SERVERS_ONLY
#define DS_REPSYNCALL_SYNC_ADJACENT_SERVERS_ONLY 0x00000002
#endif
#ifndef DS_REPSYNC_ABANDONED
#define DS_REPSYNC_ABANDONED 0x00008000
#endif
#ifndef DS_REPSYNC_ADD_REFERENCE
#define DS_REPSYNC_ADD_REFERENCE 0x00000200
#endif
#ifndef DS_REPSYNC_ASYNCHRONOUS_OPERATION
#define DS_REPSYNC_ASYNCHRONOUS_OPERATION 0x00000001
#endif
#ifndef DS_REPSYNC_ASYNCHRONOUS_REPLICA
#define DS_REPSYNC_ASYNCHRONOUS_REPLICA 0x00100000
#endif
#ifndef DS_REPSYNC_CRITICAL
#define DS_REPSYNC_CRITICAL 0x00200000
#endif
#ifndef DS_REPSYNC_FORCE
#define DS_REPSYNC_FORCE 0x00000100
#endif
#ifndef DS_REPSYNC_FULL
#define DS_REPSYNC_FULL 0x00000020
#endif
#ifndef DS_REPSYNC_FULL_IN_PROGRESS
#define DS_REPSYNC_FULL_IN_PROGRESS 0x00400000
#endif
#ifndef DS_REPSYNC_INITIAL
#define DS_REPSYNC_INITIAL 0x00002000
#endif
#ifndef DS_REPSYNC_INITIAL_IN_PROGRESS
#define DS_REPSYNC_INITIAL_IN_PROGRESS 0x00010000
#endif
#ifndef DS_REPSYNC_INTERSITE_MESSAGING
#define DS_REPSYNC_INTERSITE_MESSAGING 0x00000008
#endif
#ifndef DS_REPSYNC_NEVER_COMPLETED
#define DS_REPSYNC_NEVER_COMPLETED 0x00000400
#endif
#ifndef DS_REPSYNC_NEVER_NOTIFY
#define DS_REPSYNC_NEVER_NOTIFY 0x00001000
#endif
#ifndef DS_REPSYNC_NONGC_RO_REPLICA
#define DS_REPSYNC_NONGC_RO_REPLICA 0x01000000
#endif
#ifndef DS_REPSYNC_NOTIFICATION
#define DS_REPSYNC_NOTIFICATION 0x00080000
#endif
#ifndef DS_REPSYNC_NO_DISCARD
#define DS_REPSYNC_NO_DISCARD 0x00000080
#endif
#ifndef DS_REPSYNC_PARTIAL_ATTRIBUTE_SET
#define DS_REPSYNC_PARTIAL_ATTRIBUTE_SET 0x00020000
#endif
#ifndef DS_REPSYNC_PERIODIC
#define DS_REPSYNC_PERIODIC 0x00000004
#endif
#ifndef DS_REPSYNC_PREEMPTED
#define DS_REPSYNC_PREEMPTED 0x00800000
#endif
#ifndef DS_REPSYNC_REQUEUE
#define DS_REPSYNC_REQUEUE 0x00040000
#endif
#ifndef DS_REPSYNC_SELECT_SECRETS
#define DS_REPSYNC_SELECT_SECRETS 0x00008000
#endif
#ifndef DS_REPSYNC_TWO_WAY
#define DS_REPSYNC_TWO_WAY 0x00000800
#endif
#ifndef DS_REPSYNC_URGENT
#define DS_REPSYNC_URGENT 0x00000040
#endif
#ifndef DS_REPSYNC_USE_COMPRESSION
#define DS_REPSYNC_USE_COMPRESSION 0x00004000
#endif
#ifndef DS_REPSYNC_WRITEABLE
#define DS_REPSYNC_WRITEABLE 0x00000002
#endif
#ifndef DS_REPUPD_ADD_REFERENCE
#define DS_REPUPD_ADD_REFERENCE 0x00000004
#endif
#ifndef DS_REPUPD_ASYNCHRONOUS_OPERATION
#define DS_REPUPD_ASYNCHRONOUS_OPERATION 0x00000001
#endif
#ifndef DS_REPUPD_DELETE_REFERENCE
#define DS_REPUPD_DELETE_REFERENCE 0x00000008
#endif
#ifndef DS_REPUPD_REFERENCE_GCSPN
#define DS_REPUPD_REFERENCE_GCSPN 0x00000010
#endif
#ifndef DS_REPUPD_WRITEABLE
#define DS_REPUPD_WRITEABLE 0x00000002
#endif
#ifndef DS_ROLE_DOMAIN_OWNER
#define DS_ROLE_DOMAIN_OWNER 1
#endif
#ifndef DS_ROLE_INFRASTRUCTURE_OWNER
#define DS_ROLE_INFRASTRUCTURE_OWNER 4
#endif
#ifndef DS_ROLE_PDC_OWNER
#define DS_ROLE_PDC_OWNER 2
#endif
#ifndef DS_ROLE_RID_OWNER
#define DS_ROLE_RID_OWNER 3
#endif
#ifndef DS_SCHEMA_GUID_ATTR
#define DS_SCHEMA_GUID_ATTR 1
#endif
#ifndef DS_SCHEMA_GUID_ATTR_SET
#define DS_SCHEMA_GUID_ATTR_SET 2
#endif
#ifndef DS_SCHEMA_GUID_CLASS
#define DS_SCHEMA_GUID_CLASS 3
#endif
#ifndef DS_SCHEMA_GUID_CONTROL_RIGHT
#define DS_SCHEMA_GUID_CONTROL_RIGHT 4
#endif
#ifndef FLAG_DISABLABLE_OPTIONAL_FEATURE
#define FLAG_DISABLABLE_OPTIONAL_FEATURE 0x00000004
#endif
#ifndef FLAG_DOMAIN_OPTIONAL_FEATURE
#define FLAG_DOMAIN_OPTIONAL_FEATURE 0x00000002
#endif
#ifndef FLAG_FOREST_OPTIONAL_FEATURE
#define FLAG_FOREST_OPTIONAL_FEATURE 0x00000001
#endif
#ifndef FLAG_SERVER_OPTIONAL_FEATURE
#define FLAG_SERVER_OPTIONAL_FEATURE 0x00000008
#endif
#ifndef FRSCONN_MAX_PRIORITY
#define FRSCONN_MAX_PRIORITY 0x8
#endif
#ifndef FRSCONN_PRIORITY_MASK
#define FRSCONN_PRIORITY_MASK 0x70000000
#endif
#ifndef NTDSAPI_BIND_FIND_BINDING
#define NTDSAPI_BIND_FIND_BINDING 0x00000002
#endif
#ifndef NTDSAPI_BIND_FORCE_KERBEROS
#define NTDSAPI_BIND_FORCE_KERBEROS 0x00000004
#endif
#ifndef NTDSCONN_KCC_NO_REASON
#define NTDSCONN_KCC_NO_REASON 0
#endif
#ifndef NTDSCONN_OPT_IGNORE_SCHEDULE_MASK
#define NTDSCONN_OPT_IGNORE_SCHEDULE_MASK 0x80000000
#endif
#ifndef NTDSSETTINGS_DEFAULT_SERVER_REDUNDANCY
#define NTDSSETTINGS_DEFAULT_SERVER_REDUNDANCY 2
#endif

typedef enum _DS_REPL_INFO_TYPE {
    DS_REPL_INFO_NEIGHBORS = 0,
    DS_REPL_INFO_CURSORS_FOR_NC = 1,
    DS_REPL_INFO_METADATA_FOR_OBJ = 2,
    DS_REPL_INFO_KCC_DSA_CONNECT_FAILURES = 3,
    DS_REPL_INFO_KCC_DSA_LINK_FAILURES = 4,
    DS_REPL_INFO_PENDING_OPS = 5,
    DS_REPL_INFO_METADATA_FOR_ATTR_VALUE = 6,
    DS_REPL_INFO_CURSORS_2_FOR_NC = 7,
    DS_REPL_INFO_CURSORS_3_FOR_NC = 8,
    DS_REPL_INFO_METADATA_2_FOR_OBJ = 9,
    DS_REPL_INFO_METADATA_2_FOR_ATTR_VALUE = 10,
    DS_REPL_INFO_METADATA_EXT_FOR_ATTR_VALUE = 11,
    DS_REPL_INFO_TYPE_MAX = 12
} DS_REPL_INFO_TYPE;

typedef enum _DS_REPL_OP_TYPE {
    DS_REPL_OP_TYPE_SYNC = 0,
    DS_REPL_OP_TYPE_ADD = 1,
    DS_REPL_OP_TYPE_DELETE = 2,
    DS_REPL_OP_TYPE_MODIFY = 3,
    DS_REPL_OP_TYPE_UPDATE_REFS = 4
} DS_REPL_OP_TYPE;

typedef enum DS_KCC_TASKID {
    DS_KCC_TASKID_UPDATE_TOPOLOGY = 0
} DS_KCC_TASKID;

typedef enum DS_NAME_ERROR {
    DS_NAME_NO_ERROR = 0,
    DS_NAME_ERROR_RESOLVING = 1,
    DS_NAME_ERROR_NOT_FOUND = 2,
    DS_NAME_ERROR_NOT_UNIQUE = 3,
    DS_NAME_ERROR_NO_MAPPING = 4,
    DS_NAME_ERROR_DOMAIN_ONLY = 5,
    DS_NAME_ERROR_NO_SYNTACTICAL_MAPPING = 6,
    DS_NAME_ERROR_TRUST_REFERRAL = 7
} DS_NAME_ERROR;

typedef enum DS_REPSYNCALL_ERROR {
    DS_REPSYNCALL_WIN32_ERROR_CONTACTING_SERVER = 0,
    DS_REPSYNCALL_WIN32_ERROR_REPLICATING = 1,
    DS_REPSYNCALL_SERVER_UNREACHABLE = 2
} DS_REPSYNCALL_ERROR;

typedef enum DS_REPSYNCALL_EVENT {
    DS_REPSYNCALL_EVENT_ERROR = 0,
    DS_REPSYNCALL_EVENT_SYNC_STARTED = 1,
    DS_REPSYNCALL_EVENT_SYNC_COMPLETED = 2,
    DS_REPSYNCALL_EVENT_FINISHED = 3
} DS_REPSYNCALL_EVENT;

typedef struct _DS_REPL_ATTR_META_DATA {
  LPWSTR pszAttributeName;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
} DS_REPL_ATTR_META_DATA;

typedef struct _DS_REPL_ATTR_META_DATA_2 {
  LPWSTR pszAttributeName;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  LPWSTR pszLastOriginatingDsaDN;
} DS_REPL_ATTR_META_DATA_2;

typedef struct _DS_REPL_ATTR_META_DATA_BLOB {
  DWORD oszAttributeName;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  DWORD oszLastOriginatingDsaDN;
} DS_REPL_ATTR_META_DATA_BLOB;

typedef struct _DS_REPL_CURSOR {
  UUID uuidSourceDsaInvocationID;
  USN usnAttributeFilter;
} DS_REPL_CURSOR;

typedef struct _DS_REPL_CURSOR_2 {
  UUID uuidSourceDsaInvocationID;
  USN usnAttributeFilter;
  FILETIME ftimeLastSyncSuccess;
} DS_REPL_CURSOR_2;

typedef struct _DS_REPL_CURSOR_3W {
  UUID uuidSourceDsaInvocationID;
  USN usnAttributeFilter;
  FILETIME ftimeLastSyncSuccess;
  LPWSTR pszSourceDsaDN;
} DS_REPL_CURSOR_3W;

typedef struct _DS_REPL_CURSOR_BLOB {
  UUID uuidSourceDsaInvocationID;
  USN usnAttributeFilter;
  FILETIME ftimeLastSyncSuccess;
  DWORD oszSourceDsaDN;
} DS_REPL_CURSOR_BLOB;

typedef struct _DS_REPL_CURSORS {
  DWORD cNumCursors;
  DWORD dwReserved;
  DS_REPL_CURSOR rgCursor[1];
} DS_REPL_CURSORS;

typedef struct _DS_REPL_CURSORS_2 {
  DWORD cNumCursors;
  DWORD dwEnumerationContext;
  DS_REPL_CURSOR_2 rgCursor[1];
} DS_REPL_CURSORS_2;

typedef struct _DS_REPL_CURSORS_3W {
  DWORD cNumCursors;
  DWORD dwEnumerationContext;
  DS_REPL_CURSOR_3W rgCursor[1];
} DS_REPL_CURSORS_3W;

typedef struct _DS_REPL_KCC_DSA_FAILUREW {
  LPWSTR pszDsaDN;
  UUID uuidDsaObjGuid;
  FILETIME ftimeFirstFailure;
  DWORD cNumFailures;
  DWORD dwLastResult;
} DS_REPL_KCC_DSA_FAILUREW;

typedef struct _DS_REPL_KCC_DSA_FAILURESW {
  DWORD cNumEntries;
  DWORD dwReserved;
  DS_REPL_KCC_DSA_FAILUREW rgDsaFailure[1];
} DS_REPL_KCC_DSA_FAILURESW;

typedef struct _DS_REPL_KCC_DSA_FAILUREW_BLOB {
  DWORD oszDsaDN;
  UUID uuidDsaObjGuid;
  FILETIME ftimeFirstFailure;
  DWORD cNumFailures;
  DWORD dwLastResult;
} DS_REPL_KCC_DSA_FAILUREW_BLOB;

typedef struct _DS_REPL_NEIGHBORW {
  LPWSTR pszNamingContext;
  LPWSTR pszSourceDsaDN;
  LPWSTR pszSourceDsaAddress;
  LPWSTR pszAsyncIntersiteTransportDN;
  DWORD dwReplicaFlags;
  DWORD dwReserved;
  UUID uuidNamingContextObjGuid;
  UUID uuidSourceDsaObjGuid;
  UUID uuidSourceDsaInvocationID;
  UUID uuidAsyncIntersiteTransportObjGuid;
  USN usnLastObjChangeSynced;
  USN usnAttributeFilter;
  FILETIME ftimeLastSyncSuccess;
  FILETIME ftimeLastSyncAttempt;
  DWORD dwLastSyncResult;
  DWORD cNumConsecutiveSyncFailures;
} DS_REPL_NEIGHBORW;

typedef struct _DS_REPL_NEIGHBORSW {
  DWORD cNumNeighbors;
  DWORD dwReserved;
  DS_REPL_NEIGHBORW rgNeighbor[1];
} DS_REPL_NEIGHBORSW;

typedef struct _DS_REPL_NEIGHBORW_BLOB {
  DWORD oszNamingContext;
  DWORD oszSourceDsaDN;
  DWORD oszSourceDsaAddress;
  DWORD oszAsyncIntersiteTransportDN;
  DWORD dwReplicaFlags;
  DWORD dwReserved;
  UUID uuidNamingContextObjGuid;
  UUID uuidSourceDsaObjGuid;
  UUID uuidSourceDsaInvocationID;
  UUID uuidAsyncIntersiteTransportObjGuid;
  USN usnLastObjChangeSynced;
  USN usnAttributeFilter;
  FILETIME ftimeLastSyncSuccess;
  FILETIME ftimeLastSyncAttempt;
  DWORD dwLastSyncResult;
  DWORD cNumConsecutiveSyncFailures;
} DS_REPL_NEIGHBORW_BLOB;

typedef struct _DS_REPL_OBJ_META_DATA {
  DWORD cNumEntries;
  DWORD dwReserved;
  DS_REPL_ATTR_META_DATA rgMetaData[1];
} DS_REPL_OBJ_META_DATA;

typedef struct _DS_REPL_OBJ_META_DATA_2 {
  DWORD cNumEntries;
  DWORD dwReserved;
  DS_REPL_ATTR_META_DATA_2 rgMetaData[1];
} DS_REPL_OBJ_META_DATA_2;

typedef struct _DS_REPL_OPW {
  FILETIME ftimeEnqueued;
  ULONG ulSerialNumber;
  ULONG ulPriority;
  DS_REPL_OP_TYPE OpType;
  ULONG ulOptions;
  LPWSTR pszNamingContext;
  LPWSTR pszDsaDN;
  LPWSTR pszDsaAddress;
  UUID uuidNamingContextObjGuid;
  UUID uuidDsaObjGuid;
} DS_REPL_OPW;

typedef struct _DS_REPL_OPW_BLOB {
  FILETIME ftimeEnqueued;
  ULONG ulSerialNumber;
  ULONG ulPriority;
  DS_REPL_OP_TYPE OpType;
  ULONG ulOptions;
  DWORD oszNamingContext;
  DWORD oszDsaDN;
  DWORD oszDsaAddress;
  UUID uuidNamingContextObjGuid;
  UUID uuidDsaObjGuid;
} DS_REPL_OPW_BLOB;

typedef struct _DS_REPL_PENDING_OPSW {
  FILETIME ftimeCurrentOpStarted;
  DWORD cNumPendingOps;
  DS_REPL_OPW rgPendingOp[1];
} DS_REPL_PENDING_OPSW;

typedef struct _DS_REPL_QUEUE_STATISTICSW {
  FILETIME ftimeCurrentOpStarted;
  DWORD cNumPendingOps;
  FILETIME ftimeOldestSync;
  FILETIME ftimeOldestAdd;
  FILETIME ftimeOldestMod;
  FILETIME ftimeOldestDel;
  FILETIME ftimeOldestUpdRefs;
} DS_REPL_QUEUE_STATISTICSW, DS_REPL_QUEUE_STATISTICSW_BLOB;

typedef struct _DS_REPL_VALUE_META_DATA {
  LPWSTR pszAttributeName;
  LPWSTR pszObjectDn;
  DWORD cbData;
  BYTE * pbData;
  FILETIME ftimeDeleted;
  FILETIME ftimeCreated;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
} DS_REPL_VALUE_META_DATA;

typedef struct _DS_REPL_ATTR_VALUE_META_DATA {
  DWORD cNumEntries;
  DWORD dwEnumerationContext;
  DS_REPL_VALUE_META_DATA rgMetaData[1];
} DS_REPL_ATTR_VALUE_META_DATA;

typedef struct _DS_REPL_VALUE_META_DATA_2 {
  LPWSTR pszAttributeName;
  LPWSTR pszObjectDn;
  DWORD cbData;
  BYTE * pbData;
  FILETIME ftimeDeleted;
  FILETIME ftimeCreated;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  LPWSTR pszLastOriginatingDsaDN;
} DS_REPL_VALUE_META_DATA_2;

typedef struct _DS_REPL_ATTR_VALUE_META_DATA_2 {
  DWORD cNumEntries;
  DWORD dwEnumerationContext;
  DS_REPL_VALUE_META_DATA_2 rgMetaData[1];
} DS_REPL_ATTR_VALUE_META_DATA_2;

typedef struct _DS_REPL_VALUE_META_DATA_BLOB {
  DWORD oszAttributeName;
  DWORD oszObjectDn;
  DWORD cbData;
  DWORD obData;
  FILETIME ftimeDeleted;
  FILETIME ftimeCreated;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  DWORD oszLastOriginatingDsaDN;
} DS_REPL_VALUE_META_DATA_BLOB;

typedef struct _DS_REPL_VALUE_META_DATA_BLOB_EXT {
  DWORD oszAttributeName;
  DWORD oszObjectDn;
  DWORD cbData;
  DWORD obData;
  FILETIME ftimeDeleted;
  FILETIME ftimeCreated;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  DWORD oszLastOriginatingDsaDN;
  DWORD dwUserIdentifier;
  DWORD dwPriorLinkState;
  DWORD dwCurrentLinkState;
} DS_REPL_VALUE_META_DATA_BLOB_EXT;

typedef struct _DS_REPL_VALUE_META_DATA_EXT {
  LPWSTR pszAttributeName;
  LPWSTR pszObjectDn;
  DWORD cbData;
  BYTE * pbData;
  FILETIME ftimeDeleted;
  FILETIME ftimeCreated;
  DWORD dwVersion;
  FILETIME ftimeLastOriginatingChange;
  UUID uuidLastOriginatingDsaInvocationID;
  USN usnOriginatingChange;
  USN usnLocalChange;
  LPWSTR pszLastOriginatingDsaDN;
  DWORD dwUserIdentifier;
  DWORD dwPriorLinkState;
  DWORD dwCurrentLinkState;
} DS_REPL_VALUE_META_DATA_EXT;

typedef struct _DS_REPL_ATTR_VALUE_META_DATA_EXT {
  DWORD cNumEntries;
  DWORD dwEnumerationContext;
  DS_REPL_VALUE_META_DATA_EXT rgMetaData[1];
} DS_REPL_ATTR_VALUE_META_DATA_EXT;

typedef struct DS_DOMAIN_CONTROLLER_INFO_1A {
  LPSTR NetbiosName;
  LPSTR DnsHostName;
  LPSTR SiteName;
  LPSTR ComputerObjectName;
  LPSTR ServerObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
} DS_DOMAIN_CONTROLLER_INFO_1A, *PDS_DOMAIN_CONTROLLER_INFO_1A;

typedef struct DS_DOMAIN_CONTROLLER_INFO_1W {
  LPWSTR NetbiosName;
  LPWSTR DnsHostName;
  LPWSTR SiteName;
  LPWSTR ComputerObjectName;
  LPWSTR ServerObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
} DS_DOMAIN_CONTROLLER_INFO_1W, *PDS_DOMAIN_CONTROLLER_INFO_1W;

typedef struct DS_DOMAIN_CONTROLLER_INFO_2A {
  LPSTR NetbiosName;
  LPSTR DnsHostName;
  LPSTR SiteName;
  LPSTR SiteObjectName;
  LPSTR ComputerObjectName;
  LPSTR ServerObjectName;
  LPSTR NtdsDsaObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
  BOOL fIsGc;
  GUID SiteObjectGuid;
  GUID ComputerObjectGuid;
  GUID ServerObjectGuid;
  GUID NtdsDsaObjectGuid;
} DS_DOMAIN_CONTROLLER_INFO_2A, *PDS_DOMAIN_CONTROLLER_INFO_2A;

typedef struct DS_DOMAIN_CONTROLLER_INFO_2W {
  LPWSTR NetbiosName;
  LPWSTR DnsHostName;
  LPWSTR SiteName;
  LPWSTR SiteObjectName;
  LPWSTR ComputerObjectName;
  LPWSTR ServerObjectName;
  LPWSTR NtdsDsaObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
  BOOL fIsGc;
  GUID SiteObjectGuid;
  GUID ComputerObjectGuid;
  GUID ServerObjectGuid;
  GUID NtdsDsaObjectGuid;
} DS_DOMAIN_CONTROLLER_INFO_2W, *PDS_DOMAIN_CONTROLLER_INFO_2W;

typedef struct DS_DOMAIN_CONTROLLER_INFO_3A {
  LPSTR NetbiosName;
  LPSTR DnsHostName;
  LPSTR SiteName;
  LPSTR SiteObjectName;
  LPSTR ComputerObjectName;
  LPSTR ServerObjectName;
  LPSTR NtdsDsaObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
  BOOL fIsGc;
  BOOL fIsRodc;
  GUID SiteObjectGuid;
  GUID ComputerObjectGuid;
  GUID ServerObjectGuid;
  GUID NtdsDsaObjectGuid;
} DS_DOMAIN_CONTROLLER_INFO_3A, *PDS_DOMAIN_CONTROLLER_INFO_3A;

typedef struct DS_DOMAIN_CONTROLLER_INFO_3W {
  LPWSTR NetbiosName;
  LPWSTR DnsHostName;
  LPWSTR SiteName;
  LPWSTR SiteObjectName;
  LPWSTR ComputerObjectName;
  LPWSTR ServerObjectName;
  LPWSTR NtdsDsaObjectName;
  BOOL fIsPdc;
  BOOL fDsEnabled;
  BOOL fIsGc;
  BOOL fIsRodc;
  GUID SiteObjectGuid;
  GUID ComputerObjectGuid;
  GUID ServerObjectGuid;
  GUID NtdsDsaObjectGuid;
} DS_DOMAIN_CONTROLLER_INFO_3W, *PDS_DOMAIN_CONTROLLER_INFO_3W;

typedef struct DS_REPSYNCALL_ERRINFOA {
  LPSTR pszSvrId;
  DS_REPSYNCALL_ERROR error;
  DWORD dwWin32Err;
  LPSTR pszSrcId;
} DS_REPSYNCALL_ERRINFOA, *PDS_REPSYNCALL_ERRINFOA;

typedef struct DS_REPSYNCALL_ERRINFOW {
  LPWSTR pszSvrId;
  DS_REPSYNCALL_ERROR error;
  DWORD dwWin32Err;
  LPWSTR pszSrcId;
} DS_REPSYNCALL_ERRINFOW, *PDS_REPSYNCALL_ERRINFOW;

typedef struct DS_REPSYNCALL_SYNCA {
  LPSTR pszSrcId;
  LPSTR pszDstId;
  LPSTR pszNC;
  GUID * pguidSrc;
  GUID * pguidDst;
} DS_REPSYNCALL_SYNCA, *PDS_REPSYNCALL_SYNCA;

typedef struct DS_REPSYNCALL_SYNCW {
  LPWSTR pszSrcId;
  LPWSTR pszDstId;
  LPWSTR pszNC;
  GUID * pguidSrc;
  GUID * pguidDst;
} DS_REPSYNCALL_SYNCW, *PDS_REPSYNCALL_SYNCW;

typedef struct DS_REPSYNCALL_UPDATEA {
  DS_REPSYNCALL_EVENT event;
  DS_REPSYNCALL_ERRINFOA * pErrInfo;
  DS_REPSYNCALL_SYNCA * pSync;
} DS_REPSYNCALL_UPDATEA, *PDS_REPSYNCALL_UPDATEA;

typedef struct DS_REPSYNCALL_UPDATEW {
  DS_REPSYNCALL_EVENT event;
  DS_REPSYNCALL_ERRINFOW * pErrInfo;
  DS_REPSYNCALL_SYNCW * pSync;
} DS_REPSYNCALL_UPDATEW, *PDS_REPSYNCALL_UPDATEW;

typedef struct DS_SCHEMA_GUID_MAPA {
  GUID guid;
  DWORD guidType;
  LPSTR pName;
} DS_SCHEMA_GUID_MAPA, *PDS_SCHEMA_GUID_MAPA;

typedef struct DS_SCHEMA_GUID_MAPW {
  GUID guid;
  DWORD guidType;
  LPWSTR pName;
} DS_SCHEMA_GUID_MAPW, *PDS_SCHEMA_GUID_MAPW;

typedef struct DS_SITE_COST_INFO {
  DWORD errorCode;
  DWORD cost;
} DS_SITE_COST_INFO, *PDS_SITE_COST_INFO;

#if (!defined(MIDL_PASS))
#define ADAM_REPL_AUTHENTICATION_MODE_NEGOTIATE_PASS_THROUGH 0
#define ADAM_SCP_FSMO_NAMING_STRING "naming"
#define ADAM_SCP_FSMO_NAMING_STRING_W L"naming"
#define ADAM_SCP_FSMO_SCHEMA_STRING "schema"
#define ADAM_SCP_FSMO_SCHEMA_STRING_W L"schema"
#define ADAM_SCP_FSMO_STRING "fsmo:"
#define ADAM_SCP_FSMO_STRING_W L"fsmo:"
#define ADAM_SCP_INSTANCE_NAME_STRING "instance:"
#define ADAM_SCP_INSTANCE_NAME_STRING_W L"instance:"
#define ADAM_SCP_PARTITION_STRING "partition:"
#define ADAM_SCP_PARTITION_STRING_W L"partition:"
#define ADAM_SCP_SITE_NAME_STRING "site:"
#define ADAM_SCP_SITE_NAME_STRING_W L"site:"
#endif
#define DS_BEHAVIOR_LONGHORN 3
#define DS_BEHAVIOR_WIN2000 0
#define DS_BEHAVIOR_WIN7 4
#define DS_BEHAVIOR_WIN8 5
#define DS_BEHAVIOR_WINBLUE 6
#define DS_BEHAVIOR_WINTHRESHOLD 7
#define DS_DEFAULT_LOCALE 0x409
#define DS_DEFAULT_LOCALE_COMPARE_FLAGS 0x31003
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_1 DS_DOMAIN_CONTROLLER_INFO_1W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_1 DS_DOMAIN_CONTROLLER_INFO_1W
#else
#define DS_DOMAIN_CONTROLLER_INFO_1 DS_DOMAIN_CONTROLLER_INFO_1A
#endif
#endif
#endif
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_2 DS_DOMAIN_CONTROLLER_INFO_2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_2 DS_DOMAIN_CONTROLLER_INFO_2W
#else
#define DS_DOMAIN_CONTROLLER_INFO_2 DS_DOMAIN_CONTROLLER_INFO_2A
#endif
#endif
#endif
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_3 DS_DOMAIN_CONTROLLER_INFO_3W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_DOMAIN_CONTROLLER_INFO_3 DS_DOMAIN_CONTROLLER_INFO_3W
#else
#define DS_DOMAIN_CONTROLLER_INFO_3 DS_DOMAIN_CONTROLLER_INFO_3A
#endif
#endif
#endif
#define DS_DOMAIN_SIMPLE_NAME 8
#define DS_ENTERPRISE_SIMPLE_NAME 8
#define DS_EXIST_ADVISORY_MODE (0x1)
#define DS_GENERIC_ALL 0xF01FF
#define DS_GENERIC_EXECUTE 0x20004
#define DS_GENERIC_READ 0x20094
#define DS_GENERIC_WRITE 0x20028
#define DS_KCC_FLAG_ASYNC_OP (1 << 0)
#define DS_KCC_FLAG_DAMPED 2
#define DS_LIST_DSA_OBJECT_FOR_SERVER 0
#define DS_NAME_LEGAL_FLAGS 1
#define DS_REPL_NBR_MODIFIABLE_MASK 0x3C000260
#define DS_REPMOD_UPDATE_ADDRESS 2
#ifdef UNICODE
#define DS_REPSYNCALL_ERRINFO DS_REPSYNCALL_ERRINFOW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_REPSYNCALL_ERRINFO DS_REPSYNCALL_ERRINFOW
#else
#define DS_REPSYNCALL_ERRINFO DS_REPSYNCALL_ERRINFOA
#endif
#endif
#endif
#define DS_REPSYNCALL_NO_OPTIONS 0x00000000
#ifdef UNICODE
#define DS_REPSYNCALL_SYNC DS_REPSYNCALL_SYNCW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_REPSYNCALL_SYNC DS_REPSYNCALL_SYNCW
#else
#define DS_REPSYNCALL_SYNC DS_REPSYNCALL_SYNCA
#endif
#endif
#endif
#ifdef UNICODE
#define DS_REPSYNCALL_UPDATE DS_REPSYNCALL_UPDATEW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_REPSYNCALL_UPDATE DS_REPSYNCALL_UPDATEW
#else
#define DS_REPSYNCALL_UPDATE DS_REPSYNCALL_UPDATEA
#endif
#endif
#endif
#define DS_ROLE_SCHEMA_OWNER 0
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DS_SCHEMA_GUID_MAP DS_SCHEMA_GUID_MAPW
#else
#define DS_SCHEMA_GUID_MAP DS_SCHEMA_GUID_MAPA
#endif
#endif
#define DS_SCHEMA_GUID_NOT_FOUND 0
#define DS_SYNCED_EVENT_NAME "NTDSInitialSyncsCompleted"
#define DS_SYNCED_EVENT_NAME_W L"NTDSInitialSyncsCompleted"
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsAddSidHistory DsAddSidHistoryW
#else
#define DsAddSidHistory DsAddSidHistoryA
#endif
#endif
#ifdef UNICODE
#define DsBind DsBindW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBind DsBindW
#else
#define DsBind DsBindA
#endif
#endif
#endif
#ifdef UNICODE
#define DsBindByInstance DsBindByInstanceW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBindByInstance DsBindByInstanceW
#else
#define DsBindByInstance DsBindByInstanceA
#endif
#endif
#endif
#ifdef UNICODE
#define DsBindToISTG DsBindToISTGW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBindToISTG DsBindToISTGW
#else
#define DsBindToISTG DsBindToISTGA
#endif
#endif
#endif
#ifdef UNICODE
#define DsBindWithCred DsBindWithCredW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBindWithCred DsBindWithCredW
#else
#define DsBindWithCred DsBindWithCredA
#endif
#endif
#endif
#ifdef UNICODE
#define DsBindWithSpn DsBindWithSpnW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBindWithSpn DsBindWithSpnW
#else
#define DsBindWithSpn DsBindWithSpnA
#endif
#endif
#endif
#ifdef UNICODE
#define DsBindWithSpnEx DsBindWithSpnExW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsBindWithSpnEx DsBindWithSpnExW
#else
#define DsBindWithSpnEx DsBindWithSpnExA
#endif
#endif
#endif
#ifdef UNICODE
#define DsFreeDomainControllerInfo DsFreeDomainControllerInfoW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsFreeDomainControllerInfo DsFreeDomainControllerInfoW
#else
#define DsFreeDomainControllerInfo DsFreeDomainControllerInfoA
#endif
#endif
#endif
#ifdef UNICODE
#define DsFreeNameResult DsFreeNameResultW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsFreeNameResult DsFreeNameResultW
#else
#define DsFreeNameResult DsFreeNameResultA
#endif
#endif
#endif
#define DsFreePasswordCredentialsA DsFreePasswordCredentials
#define DsFreePasswordCredentialsW DsFreePasswordCredentials
#ifdef UNICODE
#define DsFreeSchemaGuidMap DsFreeSchemaGuidMapW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsFreeSchemaGuidMap DsFreeSchemaGuidMapW
#else
#define DsFreeSchemaGuidMap DsFreeSchemaGuidMapA
#endif
#endif
#endif
#ifdef UNICODE
#define DsFreeSpnArray DsFreeSpnArrayW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsFreeSpnArray DsFreeSpnArrayW
#else
#define DsFreeSpnArray DsFreeSpnArrayA
#endif
#endif
#endif
#ifdef UNICODE
#define DsGetDomainControllerInfo DsGetDomainControllerInfoW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsGetDomainControllerInfo DsGetDomainControllerInfoW
#else
#define DsGetDomainControllerInfo DsGetDomainControllerInfoA
#endif
#endif
#endif
#ifdef UNICODE
#define DsGetSpn DsGetSpnW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsGetSpn DsGetSpnW
#else
#define DsGetSpn DsGetSpnA
#endif
#endif
#endif
#ifdef UNICODE
#define DsInheritSecurityIdentity DsInheritSecurityIdentityW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsInheritSecurityIdentity DsInheritSecurityIdentityW
#else
#define DsInheritSecurityIdentity DsInheritSecurityIdentityA
#endif
#endif
#endif
#ifdef UNICODE
#define DsListDomainsInSite DsListDomainsInSiteW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsListDomainsInSite DsListDomainsInSiteW
#else
#define DsListDomainsInSite DsListDomainsInSiteA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsListInfoForServer DsListInfoForServerW
#else
#define DsListInfoForServer DsListInfoForServerA
#endif
#ifdef UNICODE
#define DsListRoles DsListRolesW
#else
#define DsListRoles DsListRolesA
#endif
#endif
#ifdef UNICODE
#define DsListServersForDomainInSite DsListServersForDomainInSiteW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsListServersForDomainInSite DsListServersForDomainInSiteW
#else
#define DsListServersForDomainInSite DsListServersForDomainInSiteA
#endif
#endif
#endif
#ifdef UNICODE
#define DsListServersInSite DsListServersInSiteW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsListServersInSite DsListServersInSiteW
#else
#define DsListServersInSite DsListServersInSiteA
#endif
#endif
#endif
#ifdef UNICODE
#define DsListSites DsListSitesW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsListSites DsListSitesW
#else
#define DsListSites DsListSitesA
#endif
#endif
#endif
#ifdef UNICODE
#define DsMakePasswordCredentials DsMakePasswordCredentialsW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsMakePasswordCredentials DsMakePasswordCredentialsW
#else
#define DsMakePasswordCredentials DsMakePasswordCredentialsA
#endif
#endif
#endif
#ifdef UNICODE
#define DsMapSchemaGuids DsMapSchemaGuidsW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsMapSchemaGuids DsMapSchemaGuidsW
#else
#define DsMapSchemaGuids DsMapSchemaGuidsA
#endif
#endif
#endif
#ifdef UNICODE
#define DsQuerySitesByCost DsQuerySitesByCostW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsQuerySitesByCost DsQuerySitesByCostW
#else
#define DsQuerySitesByCost DsQuerySitesByCostA
#endif
#endif
#endif
#ifdef UNICODE
#define DsRemoveDsDomain DsRemoveDsDomainW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsRemoveDsDomain DsRemoveDsDomainW
#else
#define DsRemoveDsDomain DsRemoveDsDomainA
#endif
#endif
#endif
#ifdef UNICODE
#define DsRemoveDsServer DsRemoveDsServerW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsRemoveDsServer DsRemoveDsServerW
#else
#define DsRemoveDsServer DsRemoveDsServerA
#endif
#endif
#endif
#ifdef UNICODE
#define DsReplicaDel DsReplicaDelW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsReplicaDel DsReplicaDelW
#else
#define DsReplicaDel DsReplicaDelA
#endif
#endif
#endif
#ifdef UNICODE
#define DsReplicaSync DsReplicaSyncW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsReplicaSync DsReplicaSyncW
#else
#define DsReplicaSync DsReplicaSyncA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsReplicaSyncAll DsReplicaSyncAllW
#else
#define DsReplicaSyncAll DsReplicaSyncAllA
#endif
#endif
#ifdef UNICODE
#define DsReplicaUpdateRefs DsReplicaUpdateRefsW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsReplicaUpdateRefs DsReplicaUpdateRefsW
#else
#define DsReplicaUpdateRefs DsReplicaUpdateRefsA
#endif
#endif
#endif
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsReplicaVerifyObjects DsReplicaVerifyObjectsW
#else
#define DsReplicaVerifyObjects DsReplicaVerifyObjectsA
#endif
#endif
#ifdef UNICODE
#define DsUnBind DsUnBindW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsUnBind DsUnBindW
#else
#define DsUnBind DsUnBindA
#endif
#endif
#endif
#ifdef UNICODE
#define DsWriteAccountSpn DsWriteAccountSpnW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define DsWriteAccountSpn DsWriteAccountSpnW
#else
#define DsWriteAccountSpn DsWriteAccountSpnA
#endif
#endif
#endif
#define GUID_COMPUTRS_CONTAINER_A "aa312825768811d1aded00c04fd8d5cd"
#define GUID_COMPUTRS_CONTAINER_BYTE "\xaa\x31\x28\x25\x76\x88\x11\xd1\xad\xed\x00\xc0\x4f\xd8\xd5\xcd"
#define GUID_COMPUTRS_CONTAINER_W L"aa312825768811d1aded00c04fd8d5cd"
#if (!defined(MIDL_PASS))
#define GUID_DATABASE_32K_PAGES_OPTIONAL_FEATURE_A "c62a9852731e4f75ae2473ae2775aab8"
#define GUID_DATABASE_32K_PAGES_OPTIONAL_FEATURE_BYTE "\xc6\x2a\x98\x52\x73\x1e\x4f\x75\xae\x24\x73\xae\x27\x75\xaa\xb8"
#define GUID_DATABASE_32K_PAGES_OPTIONAL_FEATURE_W L"c62a9852731e4f75ae2473ae2775aab8"
#endif
#define GUID_DELETED_OBJECTS_CONTAINER_A "18e2ea80684f11d2b9aa00c04f79f805"
#define GUID_DELETED_OBJECTS_CONTAINER_BYTE "\x18\xe2\xea\x80\x68\x4f\x11\xd2\xb9\xaa\x00\xc0\x4f\x79\xf8\x05"
#define GUID_DELETED_OBJECTS_CONTAINER_W L"18e2ea80684f11d2b9aa00c04f79f805"
#define GUID_DOMAIN_CONTROLLERS_CONTAINER_A "a361b2ffffd211d1aa4b00c04fd7d83a"
#define GUID_DOMAIN_CONTROLLERS_CONTAINER_BYTE "\xa3\x61\xb2\xff\xff\xd2\x11\xd1\xaa\x4b\x00\xc0\x4f\xd7\xd8\x3a"
#define GUID_DOMAIN_CONTROLLERS_CONTAINER_W L"a361b2ffffd211d1aa4b00c04fd7d83a"
#define GUID_FOREIGNSECURITYPRINCIPALS_CONTAINER_A "22b70c67d56e4efb91e9300fca3dc1aa"
#define GUID_FOREIGNSECURITYPRINCIPALS_CONTAINER_BYTE "\x22\xb7\x0c\x67\xd5\x6e\x4e\xfb\x91\xe9\x30\x0f\xca\x3d\xc1\xaa"
#define GUID_FOREIGNSECURITYPRINCIPALS_CONTAINER_W L"22b70c67d56e4efb91e9300fca3dc1aa"
#define GUID_INFRASTRUCTURE_CONTAINER_A "2fbac1870ade11d297c400c04fd8d5cd"
#define GUID_INFRASTRUCTURE_CONTAINER_BYTE "\x2f\xba\xc1\x87\x0a\xde\x11\xd2\x97\xc4\x00\xc0\x4f\xd8\xd5\xcd"
#define GUID_INFRASTRUCTURE_CONTAINER_W L"2fbac1870ade11d297c400c04fd8d5cd"
#define GUID_KEYS_CONTAINER_W L"683A24E2E8164BD3AF86AC3C2CF3F981"
#define GUID_LOSTANDFOUND_CONTAINER_A "ab8153b7768811d1aded00c04fd8d5cd"
#define GUID_LOSTANDFOUND_CONTAINER_BYTE "\xab\x81\x53\xb7\x76\x88\x11\xd1\xad\xed\x00\xc0\x4f\xd8\xd5\xcd"
#define GUID_LOSTANDFOUND_CONTAINER_W L"ab8153b7768811d1aded00c04fd8d5cd"
#define GUID_MANAGED_SERVICE_ACCOUNTS_CONTAINER_W L"1EB93889E40C45DF9F0C64D23BBB6237"
#define GUID_MICROSOFT_PROGRAM_DATA_CONTAINER_A "f4be92a4c777485e878e9421d53087db"
#define GUID_MICROSOFT_PROGRAM_DATA_CONTAINER_BYTE "\xf4\xbe\x92\xa4\xc7\x77\x48\x5e\x87\x8e\x94\x21\xd5\x30\x87\xdb"
#define GUID_MICROSOFT_PROGRAM_DATA_CONTAINER_W L"f4be92a4c777485e878e9421d53087db"
#define GUID_NTDS_QUOTAS_CONTAINER_A "6227f0af1fc2410d8e3bb10615bb5b0f"
#define GUID_NTDS_QUOTAS_CONTAINER_BYTE "\x62\x27\xf0\xaf\x1f\xc2\x41\x0d\x8e\x3b\xb1\x06\x15\xbb\x5b\x0f"
#define GUID_NTDS_QUOTAS_CONTAINER_W L"6227f0af1fc2410d8e3bb10615bb5b0f"
#if (!defined(MIDL_PASS))
#define GUID_PRIVILEGED_ACCESS_MANAGEMENT_OPTIONAL_FEATURE_A "73e843ece8cc4046b4ab07ffe4ab5bcd"
#define GUID_PRIVILEGED_ACCESS_MANAGEMENT_OPTIONAL_FEATURE_BYTE "\x73\xe8\x43\xec\xe8\xcc\x40\x46\xb4\xab\x07\xff\xe4\xab\x5b\xcd"
#define GUID_PRIVILEGED_ACCESS_MANAGEMENT_OPTIONAL_FEATURE_W L"73e843ece8cc4046b4ab07ffe4ab5bcd"
#endif
#define GUID_PROGRAM_DATA_CONTAINER_A "09460c08ae1e4a4ea0f64aee7daa1e5a"
#define GUID_PROGRAM_DATA_CONTAINER_BYTE "\x09\x46\x0c\x08\xae\x1e\x4a\x4e\xa0\xf6\x4a\xee\x7d\xaa\x1e\x5a"
#define GUID_PROGRAM_DATA_CONTAINER_W L"09460c08ae1e4a4ea0f64aee7daa1e5a"
#if (!defined(MIDL_PASS))
#define GUID_RECYCLE_BIN_OPTIONAL_FEATURE_A "d8dc6d76d0ac5e44f3b9a7f9b6744f2a"
#define GUID_RECYCLE_BIN_OPTIONAL_FEATURE_BYTE "\xd8\xdc\x6d\x76\xd0\xac\x5e\x44\xf3\xb9\xa7\xf9\xb6\x74\x4f\x2a"
#define GUID_RECYCLE_BIN_OPTIONAL_FEATURE_W L"d8dc6d76d0ac5e44f3b9a7f9b6744f2a"
#endif
#define GUID_SYSTEMS_CONTAINER_A "ab1d30f3768811d1aded00c04fd8d5cd"
#define GUID_SYSTEMS_CONTAINER_BYTE "\xab\x1d\x30\xf3\x76\x88\x11\xd1\xad\xed\x00\xc0\x4f\xd8\xd5\xcd"
#define GUID_SYSTEMS_CONTAINER_W L"ab1d30f3768811d1aded00c04fd8d5cd"
#define GUID_USERS_CONTAINER_A "a9d1ca15768811d1aded00c04fd8d5cd"
#define GUID_USERS_CONTAINER_BYTE "\xa9\xd1\xca\x15\x76\x88\x11\xd1\xad\xed\x00\xc0\x4f\xd8\xd5\xcd"
#define GUID_USERS_CONTAINER_W L"a9d1ca15768811d1aded00c04fd8d5cd"
#define NTDSAPI DECLSPEC_IMPORT
#define NTDSAPI_BIND_ALLOW_DELEGATION (0x00000001)
#define NTDSAPI_POSTXP DECLSPEC_IMPORT
#define NTDSCONN_KCC_GC_TOPOLOGY 1
#define NTDSCONN_KCC_INTERSITE_GC_TOPOLOGY 0x20
#define NTDSCONN_KCC_INTERSITE_TOPOLOGY 0x40
#define NTDSCONN_KCC_MINIMIZE_HOPS_TOPOLOGY 4
#define NTDSCONN_KCC_OSCILLATING_CONNECTION_TOPOLOGY 0x10
#define NTDSCONN_KCC_REDUNDANT_SERVER_TOPOLOGY 0x200
#define NTDSCONN_KCC_RING_TOPOLOGY 2
#define NTDSCONN_KCC_SERVER_FAILOVER_TOPOLOGY 0x80
#define NTDSCONN_KCC_SITE_FAILOVER_TOPOLOGY 0x100
#define NTDSCONN_KCC_STALE_SERVERS_TOPOLOGY 8
#define NTDSCONN_OPT_DISABLE_INTERSITE_COMPRESSION 0x10
#define NTDSCONN_OPT_IS_GENERATED 1
#define NTDSCONN_OPT_OVERRIDE_NOTIFY_DEFAULT 4
#define NTDSCONN_OPT_RODC_TOPOLOGY 0x40
#define NTDSCONN_OPT_TWOWAY_SYNC 2
#define NTDSCONN_OPT_USE_NOTIFY 8
#define NTDSCONN_OPT_USER_OWNED_SCHEDULE 0x20
#define NTDSDSA_OPT_BLOCK_RPC 0x40
#define NTDSDSA_OPT_DISABLE_INBOUND_REPL 2
#define NTDSDSA_OPT_DISABLE_NTDSCONN_XLATE 8
#define NTDSDSA_OPT_DISABLE_OUTBOUND_REPL 4
#define NTDSDSA_OPT_DISABLE_SPN_REGISTRATION 0x10
#define NTDSDSA_OPT_GENERATE_OWN_TOPO 0x20
#define NTDSDSA_OPT_IS_GC 1
#define NTDSSETTINGS_OPT_FORCE_KCC_W2K_ELECTION 0x80
#define NTDSSETTINGS_OPT_FORCE_KCC_WHISTLER_BEHAVIOR 0x40
#define NTDSSETTINGS_OPT_IS_AUTO_TOPOLOGY_DISABLED 1
#define NTDSSETTINGS_OPT_IS_GROUP_CACHING_ENABLED 0x20
#define NTDSSETTINGS_OPT_IS_INTER_SITE_AUTO_TOPOLOGY_DISABLED 0x10
#define NTDSSETTINGS_OPT_IS_RAND_BH_SELECTION_DISABLED 0x100
#define NTDSSETTINGS_OPT_IS_REDUNDANT_SERVER_TOPOLOGY_ENABLED 0x400
#define NTDSSETTINGS_OPT_IS_SCHEDULE_HASHING_ENABLED 0x200
#define NTDSSETTINGS_OPT_IS_TOPL_CLEANUP_DISABLED 2
#define NTDSSETTINGS_OPT_IS_TOPL_DETECT_STALE_DISABLED 8
#define NTDSSETTINGS_OPT_IS_TOPL_MIN_HOPS_DISABLED 4
#define NTDSSETTINGS_OPT_W2K3_BRIDGES_REQUIRED 0x1000
#define NTDSSETTINGS_OPT_W2K3_IGNORE_SCHEDULES 0x800
#define NTDSSITECONN_OPT_DISABLE_COMPRESSION 4
#define NTDSSITECONN_OPT_TWOWAY_SYNC 2
#define NTDSSITECONN_OPT_USE_NOTIFY 1
#define NTDSSITELINK_OPT_DISABLE_COMPRESSION 4
#define NTDSSITELINK_OPT_TWOWAY_SYNC 2
#define NTDSSITELINK_OPT_USE_NOTIFY 1
#define NTDSTRANSPORT_OPT_BRIDGES_REQUIRED 2
#define NTDSTRANSPORT_OPT_IGNORE_SCHEDULES 1
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_1 PDS_DOMAIN_CONTROLLER_INFO_1W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_1 PDS_DOMAIN_CONTROLLER_INFO_1W
#else
#define PDS_DOMAIN_CONTROLLER_INFO_1 PDS_DOMAIN_CONTROLLER_INFO_1A
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_2 PDS_DOMAIN_CONTROLLER_INFO_2W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_2 PDS_DOMAIN_CONTROLLER_INFO_2W
#else
#define PDS_DOMAIN_CONTROLLER_INFO_2 PDS_DOMAIN_CONTROLLER_INFO_2A
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_3 PDS_DOMAIN_CONTROLLER_INFO_3W
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_DOMAIN_CONTROLLER_INFO_3 PDS_DOMAIN_CONTROLLER_INFO_3W
#else
#define PDS_DOMAIN_CONTROLLER_INFO_3 PDS_DOMAIN_CONTROLLER_INFO_3A
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_REPSYNCALL_ERRINFO PDS_REPSYNCALL_ERRINFOW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_REPSYNCALL_ERRINFO PDS_REPSYNCALL_ERRINFOW
#else
#define PDS_REPSYNCALL_ERRINFO PDS_REPSYNCALL_ERRINFOA
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_REPSYNCALL_SYNC PDS_REPSYNCALL_SYNCW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_REPSYNCALL_SYNC PDS_REPSYNCALL_SYNCW
#else
#define PDS_REPSYNCALL_SYNC PDS_REPSYNCALL_SYNCA
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_REPSYNCALL_UPDATE PDS_REPSYNCALL_UPDATEW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_REPSYNCALL_UPDATE PDS_REPSYNCALL_UPDATEW
#else
#define PDS_REPSYNCALL_UPDATE PDS_REPSYNCALL_UPDATEA
#endif
#endif
#endif
#ifdef UNICODE
#define PDS_SCHEMA_GUID_MAP PDS_SCHEMA_GUID_MAPW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PDS_SCHEMA_GUID_MAP PDS_SCHEMA_GUID_MAPW
#else
#define PDS_SCHEMA_GUID_MAP PDS_SCHEMA_GUID_MAPA
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI DsBindA(LPCSTR DomainControllerName, LPCSTR DnsDomainName, HANDLE *phDS);
DWORD WINAPI DsBindW(LPCWSTR DomainControllerName, LPCWSTR DnsDomainName, HANDLE *phDS);
DWORD WINAPI DsGetSpnA(DS_SPN_NAME_TYPE ServiceType, LPCSTR ServiceClass, LPCSTR ServiceName, USHORT InstancePort, USHORT cInstanceNames, LPCSTR *pInstanceNames, const USHORT *pInstancePorts, DWORD *pcSpn, LPSTR **prpszSpn);

/* TODO: LiberNT does not implement the functions below yet */
NTDSAPI DWORD WINAPI DsAddSidHistoryA(HANDLE hDS, DWORD Flags, LPCSTR SrcDomain, LPCSTR SrcPrincipal, LPCSTR SrcDomainController, RPC_AUTH_IDENTITY_HANDLE SrcDomainCreds, LPCSTR DstDomain, LPCSTR DstPrincipal);
NTDSAPI DWORD WINAPI DsAddSidHistoryW(HANDLE hDS, DWORD Flags, LPCWSTR SrcDomain, LPCWSTR SrcPrincipal, LPCWSTR SrcDomainController, RPC_AUTH_IDENTITY_HANDLE SrcDomainCreds, LPCWSTR DstDomain, LPCWSTR DstPrincipal);
DWORD WINAPI DsBindByInstanceA(LPCSTR ServerName, LPCSTR Annotation, GUID *InstanceGuid, LPCSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCSTR ServicePrincipalName, DWORD BindFlags, HANDLE *phDS);
DWORD WINAPI DsBindByInstanceW(LPCWSTR ServerName, LPCWSTR Annotation, GUID *InstanceGuid, LPCWSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCWSTR ServicePrincipalName, DWORD BindFlags, HANDLE *phDS);
DWORD WINAPI DsBindingSetTimeout(HANDLE hDS, ULONG cTimeoutSecs);
DWORD WINAPI DsBindToISTGA(LPCSTR SiteName, HANDLE *phDS);
DWORD WINAPI DsBindToISTGW(LPCWSTR SiteName, HANDLE *phDS);
NTDSAPI DWORD WINAPI DsBindWithCredA(LPCSTR DomainControllerName, LPCSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, HANDLE *phDS);
NTDSAPI DWORD WINAPI DsBindWithCredW(LPCWSTR DomainControllerName, LPCWSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, HANDLE *phDS);
NTDSAPI DWORD WINAPI DsBindWithSpnA(LPCSTR DomainControllerName, LPCSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCSTR ServicePrincipalName, HANDLE *phDS);
DWORD WINAPI DsBindWithSpnExA(LPCSTR DomainControllerName, LPCSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCSTR ServicePrincipalName, DWORD BindFlags, HANDLE *phDS);
DWORD WINAPI DsBindWithSpnExW(LPCWSTR DomainControllerName, LPCWSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCWSTR ServicePrincipalName, DWORD BindFlags, HANDLE *phDS);
NTDSAPI DWORD WINAPI DsBindWithSpnW(LPCWSTR DomainControllerName, LPCWSTR DnsDomainName, RPC_AUTH_IDENTITY_HANDLE AuthIdentity, LPCWSTR ServicePrincipalName, HANDLE *phDS);
NTDSAPI void WINAPI DsFreeDomainControllerInfoA(DWORD InfoLevel, DWORD cInfo, void *pInfo);
NTDSAPI void WINAPI DsFreeDomainControllerInfoW(DWORD InfoLevel, DWORD cInfo, void *pInfo);
NTDSAPI void WINAPI DsFreeNameResultA(DS_NAME_RESULTA *pResult);
NTDSAPI void WINAPI DsFreeNameResultW(DS_NAME_RESULTW *pResult);
NTDSAPI void WINAPI DsFreePasswordCredentials(RPC_AUTH_IDENTITY_HANDLE AuthIdentity);
NTDSAPI void WINAPI DsFreeSchemaGuidMapA(PDS_SCHEMA_GUID_MAPA pGuidMap);
NTDSAPI void WINAPI DsFreeSchemaGuidMapW(PDS_SCHEMA_GUID_MAPW pGuidMap);
NTDSAPI void WINAPI DsFreeSpnArrayA(DWORD cSpn, LPSTR *rpszSpn);
NTDSAPI void WINAPI DsFreeSpnArrayW(DWORD cSpn, LPWSTR *rpszSpn);
DWORD DsGetDomainControllerInfoA(HANDLE hDs, LPCSTR DomainName, DWORD InfoLevel, DWORD *pcOut, void **ppInfo);
DWORD DsGetDomainControllerInfoW(HANDLE hDs, LPCWSTR DomainName, DWORD InfoLevel, DWORD *pcOut, void **ppInfo);
NTDSAPI DWORD WINAPI DsGetSpnW(DS_SPN_NAME_TYPE ServiceType, LPCWSTR ServiceClass, LPCWSTR ServiceName, USHORT InstancePort, USHORT cInstanceNames, LPCWSTR *pInstanceNames, const USHORT *pInstancePorts, DWORD *pcSpn, LPWSTR **prpszSpn);
NTDSAPI DWORD WINAPI DsInheritSecurityIdentityA(HANDLE hDS, DWORD Flags, LPCSTR SrcPrincipal, LPCSTR DstPrincipal);
NTDSAPI DWORD WINAPI DsInheritSecurityIdentityW(HANDLE hDS, DWORD Flags, LPCWSTR SrcPrincipal, LPCWSTR DstPrincipal);
NTDSAPI DWORD WINAPI DsListDomainsInSiteA(HANDLE hDs, LPCSTR site, PDS_NAME_RESULTA *ppDomains);
NTDSAPI DWORD WINAPI DsListDomainsInSiteW(HANDLE hDs, LPCWSTR site, PDS_NAME_RESULTW *ppDomains);
NTDSAPI DWORD WINAPI DsListInfoForServerA(HANDLE hDs, LPCSTR server, PDS_NAME_RESULTA *ppInfo);
NTDSAPI DWORD WINAPI DsListInfoForServerW(HANDLE hDs, LPCWSTR server, PDS_NAME_RESULTW *ppInfo);
NTDSAPI DWORD WINAPI DsListRolesA(HANDLE hDs, PDS_NAME_RESULTA *ppRoles);
NTDSAPI DWORD WINAPI DsListRolesW(HANDLE hDs, PDS_NAME_RESULTW *ppRoles);
NTDSAPI DWORD WINAPI DsListServersForDomainInSiteA(HANDLE hDs, LPCSTR domain, LPCSTR site, PDS_NAME_RESULTA *ppServers);
NTDSAPI DWORD WINAPI DsListServersForDomainInSiteW(HANDLE hDs, LPCWSTR domain, LPCWSTR site, PDS_NAME_RESULTW *ppServers);
NTDSAPI DWORD WINAPI DsListServersInSiteA(HANDLE hDs, LPCSTR site, PDS_NAME_RESULTA *ppServers);
NTDSAPI DWORD WINAPI DsListServersInSiteW(HANDLE hDs, LPCWSTR site, PDS_NAME_RESULTW *ppServers);
NTDSAPI DWORD WINAPI DsListSitesA(HANDLE hDs, PDS_NAME_RESULTA *ppSites);
NTDSAPI DWORD WINAPI DsListSitesW(HANDLE hDs, PDS_NAME_RESULTW *ppSites);
NTDSAPI DWORD WINAPI DsMakePasswordCredentialsA(LPCSTR User, LPCSTR Domain, LPCSTR Password, RPC_AUTH_IDENTITY_HANDLE *pAuthIdentity);
NTDSAPI DWORD WINAPI DsMakePasswordCredentialsW(LPCWSTR User, LPCWSTR Domain, LPCWSTR Password, RPC_AUTH_IDENTITY_HANDLE *pAuthIdentity);
NTDSAPI DWORD WINAPI DsMapSchemaGuidsA(HANDLE hDs, DWORD cGuids, GUID *rGuids, DS_SCHEMA_GUID_MAPA **ppGuidMap);
NTDSAPI DWORD WINAPI DsMapSchemaGuidsW(HANDLE hDs, DWORD cGuids, GUID *rGuids, DS_SCHEMA_GUID_MAPW **ppGuidMap);
DWORD WINAPI DsQuerySitesByCostA(HANDLE hDS, LPSTR pszFromSite, LPSTR *rgszToSites, DWORD cToSites, DWORD dwFlags, PDS_SITE_COST_INFO *prgSiteInfo);
DWORD WINAPI DsQuerySitesByCostW(HANDLE hDS, LPWSTR pwszFromSite, LPWSTR *rgwszToSites, DWORD cToSites, DWORD dwFlags, PDS_SITE_COST_INFO *prgSiteInfo);
void WINAPI DsQuerySitesFree(PDS_SITE_COST_INFO rgSiteInfo);
NTDSAPI DWORD WINAPI DsRemoveDsDomainA(HANDLE hDs, LPSTR DomainDN);
NTDSAPI DWORD WINAPI DsRemoveDsDomainW(HANDLE hDs, LPWSTR DomainDN);
NTDSAPI DWORD WINAPI DsRemoveDsServerA(HANDLE hDs, LPSTR ServerDN, LPSTR DomainDN, BOOL *fLastDcInDomain, BOOL fCommit);
NTDSAPI DWORD WINAPI DsRemoveDsServerW(HANDLE hDs, LPWSTR ServerDN, LPWSTR DomainDN, BOOL *fLastDcInDomain, BOOL fCommit);
NTDSAPI DWORD WINAPI DsReplicaConsistencyCheck(HANDLE hDS, DS_KCC_TASKID TaskID, DWORD dwFlags);
NTDSAPI DWORD WINAPI DsReplicaDelA(HANDLE hDS, LPCSTR NameContext, LPCSTR DsaSrc, ULONG Options);
NTDSAPI DWORD WINAPI DsReplicaDelW(HANDLE hDS, LPCWSTR NameContext, LPCWSTR DsaSrc, ULONG Options);
NTDSAPI void WINAPI DsReplicaFreeInfo(DS_REPL_INFO_TYPE InfoType, void *pInfo);
NTDSAPI DWORD WINAPI DsReplicaGetInfo2W(HANDLE hDS, DS_REPL_INFO_TYPE InfoType, LPCWSTR pszObject, UUID *puuidForSourceDsaObjGuid, LPCWSTR pszAttributeName, LPCWSTR pszValue, DWORD dwFlags, DWORD dwEnumerationContext, void **ppInfo);
NTDSAPI DWORD WINAPI DsReplicaGetInfoW(HANDLE hDS, DS_REPL_INFO_TYPE InfoType, LPCWSTR pszObject, UUID *puuidForSourceDsaObjGuid, void **ppInfo);
NTDSAPI DWORD WINAPI DsReplicaSyncA(HANDLE hDS, LPCSTR NameContext, const UUID *pUuidDsaSrc, ULONG Options);
NTDSAPI DWORD WINAPI DsReplicaSyncAllA(HANDLE hDS, LPCSTR pszNameContext, ULONG ulFlags, BOOL (WINAPI *pFnCallBack)(LPVOID, PDS_REPSYNCALL_UPDATEA), LPVOID pCallbackData, PDS_REPSYNCALL_ERRINFOA **pErrors);
NTDSAPI DWORD WINAPI DsReplicaSyncAllW(HANDLE hDS, LPCWSTR pszNameContext, ULONG ulFlags, BOOL (WINAPI *pFnCallBack)(LPVOID, PDS_REPSYNCALL_UPDATEW), LPVOID pCallbackData, PDS_REPSYNCALL_ERRINFOW **pErrors);
NTDSAPI DWORD WINAPI DsReplicaSyncW(HANDLE hDS, LPCWSTR NameContext, const UUID *pUuidDsaSrc, ULONG Options);
NTDSAPI DWORD WINAPI DsReplicaUpdateRefsA(HANDLE hDS, LPCSTR NameContext, LPCSTR DsaDest, const UUID *pUuidDsaDest, ULONG Options);
NTDSAPI DWORD WINAPI DsReplicaUpdateRefsW(HANDLE hDS, LPCWSTR NameContext, LPCWSTR DsaDest, const UUID *pUuidDsaDest, ULONG Options);
NTDSAPI DWORD WINAPI DsReplicaVerifyObjectsA(HANDLE hDS, LPCSTR NameContext, const UUID *pUuidDsaSrc, ULONG ulOptions);
NTDSAPI DWORD WINAPI DsReplicaVerifyObjectsW(HANDLE hDS, LPCWSTR NameContext, const UUID *pUuidDsaSrc, ULONG ulOptions);
NTDSAPI DWORD WINAPI DsUnBindA(HANDLE *phDS);
NTDSAPI DWORD WINAPI DsUnBindW(HANDLE *phDS);
NTDSAPI DWORD WINAPI DsWriteAccountSpnA(HANDLE hDS, DS_SPN_WRITE_OP Operation, LPCSTR pszAccount, DWORD cSpn, LPCSTR *rpszSpn);
NTDSAPI DWORD WINAPI DsWriteAccountSpnW(HANDLE hDS, DS_SPN_WRITE_OP Operation, LPCWSTR pszAccount, DWORD cSpn, LPCWSTR *rpszSpn);

#ifdef __cplusplus
}
#endif

#ifdef UNICODE
#define DsReplicaAdd DsReplicaAddW
#else
#define DsReplicaAdd DsReplicaAddA
#endif

#ifdef UNICODE
#define DsReplicaModify DsReplicaModifyW
#else
#define DsReplicaModify DsReplicaModifyA
#endif

#define FRSCONN_GET_PRIORITY(_options_) (((((_options_) & FRSCONN_PRIORITY_MASK) >> 28) != 0) ? (((_options_) & FRSCONN_PRIORITY_MASK) >> 28) : FRSCONN_MAX_PRIORITY)
#define NTDSCONN_IGNORE_SCHEDULE(_options_) (((_options_) & NTDSCONN_OPT_IGNORE_SCHEDULE_MASK) >> 31)

/* TODO: DsReplicaAddA/W and DsReplicaModifyA/W, which need SCHEDULE from schedule.h */

#endif /* __WINE_NTDSAPI_H */
