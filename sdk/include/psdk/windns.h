#ifndef _WINDNS_INCLUDED_
#define _WINDNS_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#define DNS_QUERY_STANDARD                  0x0
#define DNS_QUERY_ACCEPT_TRUNCATED_RESPONSE 0x1
#define DNS_QUERY_USE_TCP_ONLY              0x2
#define DNS_QUERY_NO_RECURSION              0x4
#define DNS_QUERY_BYPASS_CACHE              0x8
#define DNS_QUERY_NO_WIRE_QUERY             0x10
#define DNS_QUERY_NO_LOCAL_NAME             0x20
#define DNS_QUERY_NO_HOSTS_FILE             0x40
#define DNS_QUERY_NO_NETBT                  0x80
#define DNS_QUERY_TREAT_AS_FQDN             0x1000
#define DNS_QUERY_WIRE_ONLY                 0x100
#define DNS_QUERY_RETURN_MESSAGE            0x200
#define DNS_QUERY_MULTICAST_ONLY            0x400
#define DNS_QUERY_NO_MULTICAST              0x800
#define DNS_QUERY_DONT_RESET_TTL_VALUES     0x100000
#define DNS_QUERY_RESERVED                  0xf0000000

#define DNS_UPDATE_SECURITY_USE_DEFAULT    0x0
#define DNS_UPDATE_SECURITY_OFF            0x10
#define DNS_UPDATE_SECURITY_ON             0x20
#define DNS_UPDATE_SECURITY_ONLY           0x100
#define DNS_UPDATE_CACHE_SECURITY_CONTEXT  0x200
#define DNS_UPDATE_TEST_USE_LOCAL_SYS_ACCT 0x400
#define DNS_UPDATE_FORCE_SECURITY_NEGO     0x800
#define DNS_UPDATE_RESERVED                0xffff0000

#define DNS_CONFIG_FLAG_ALLOC TRUE

#ifndef RC_INVOKE

typedef DWORD IP4_ADDRESS;

typedef _Return_type_success_(return == 0) DWORD DNS_STATUS;

#define DNS_TYPE_ZERO    0x0000
#define DNS_TYPE_A       0x0001
#define DNS_TYPE_NS      0x0002
#define DNS_TYPE_MD      0x0003
#define DNS_TYPE_MF      0x0004
#define DNS_TYPE_CNAME   0x0005
#define DNS_TYPE_SOA     0x0006
#define DNS_TYPE_MB      0x0007
#define DNS_TYPE_MG      0x0008
#define DNS_TYPE_MR      0x0009
#define DNS_TYPE_NULL    0x000a
#define DNS_TYPE_WKS     0x000b
#define DNS_TYPE_PTR     0x000c
#define DNS_TYPE_HINFO   0x000d
#define DNS_TYPE_MINFO   0x000e
#define DNS_TYPE_MX      0x000f
#define DNS_TYPE_TEXT    0x0010
#define DNS_TYPE_RP      0x0011
#define DNS_TYPE_AFSDB   0x0012
#define DNS_TYPE_X25     0x0013
#define DNS_TYPE_ISDN    0x0014
#define DNS_TYPE_RT      0x0015
#define DNS_TYPE_NSAP    0x0016
#define DNS_TYPE_NSAPPTR 0x0017
#define DNS_TYPE_SIG     0x0018
#define DNS_TYPE_KEY     0x0019
#define DNS_TYPE_PX      0x001a
#define DNS_TYPE_GPOS    0x001b
#define DNS_TYPE_AAAA    0x001c
#define DNS_TYPE_LOC     0x001d
#define DNS_TYPE_NXT     0x001e
#define DNS_TYPE_EID     0x001f
#define DNS_TYPE_NIMLOC  0x0020
#define DNS_TYPE_SRV     0x0021
#define DNS_TYPE_ATMA    0x0022
#define DNS_TYPE_NAPTR   0x0023
#define DNS_TYPE_KX      0x0024
#define DNS_TYPE_CERT    0x0025
#define DNS_TYPE_A6      0x0026
#define DNS_TYPE_DNAME   0x0027
#define DNS_TYPE_SINK    0x0028
#define DNS_TYPE_OPT     0x0029
#define DNS_TYPE_UINFO   0x0064
#define DNS_TYPE_UID     0x0065
#define DNS_TYPE_GID     0x0066
#define DNS_TYPE_UNSPEC  0x0067
#define DNS_TYPE_ADDRS   0x00f8
#define DNS_TYPE_TKEY    0x00f9
#define DNS_TYPE_TSIG    0x00fa
#define DNS_TYPE_IXFR    0x00fb
#define DNS_TYPE_AXFR    0x00fc
#define DNS_TYPE_MAILB   0x00fd
#define DNS_TYPE_MAILA   0x00fe
#define DNS_TYPE_ALL     0x00ff
#define DNS_TYPE_ANY     0x00ff

#define DNS_TYPE_WINS   0xff01
#define DNS_TYPE_WINSR  0xff02
#define DNS_TYPE_NBSTAT (DNS_TYPE_WINSR)

#define DNSREC_QUESTION   0
#define DNSREC_ANSWER     1
#define DNSREC_AUTHORITY  2
#define DNSREC_ADDITIONAL 3

#define DNS_ATMA_MAX_ADDR_LENGTH 20

#define DNS_ADDR_MAX_SOCKADDR_LENGTH 32
#ifdef __REACTOS__
#define DNS_MAX_NAME_LENGTH 255
#define DNS_MAX_NAME_BUFFER_LENGTH 256
#endif

typedef enum _DNS_CHARSET {
  DnsCharSetUnknown,
  DnsCharSetUnicode,
  DnsCharSetUtf8,
  DnsCharSetAnsi
} DNS_CHARSET;

typedef enum {
  DnsConfigPrimaryDomainName_W,
  DnsConfigPrimaryDomainName_A,
  DnsConfigPrimaryDomainName_UTF8,
  DnsConfigAdapterDomainName_W,
  DnsConfigAdapterDomainName_A,
  DnsConfigAdapterDomainName_UTF8,
  DnsConfigDnsServerList,
  DnsConfigSearchList,
  DnsConfigAdapterInfo,
  DnsConfigPrimaryHostNameRegistrationEnabled,
  DnsConfigAdapterHostNameRegistrationEnabled,
  DnsConfigAddressRegistrationMaxCount,
  DnsConfigHostName_W,
  DnsConfigHostName_A,
  DnsConfigHostName_UTF8,
  DnsConfigFullHostName_W,
  DnsConfigFullHostName_A,
  DnsConfigFullHostName_UTF8,
#ifdef __REACTOS__
  DnsConfigDnsServersUnspec = 4144,
  DnsConfigDnsServersIpv4,
  DnsConfigDnsServersIpv6
#endif
} DNS_CONFIG_TYPE;

typedef enum {
  DnsFreeFlat = 0,
  DnsFreeRecordList,
  DnsFreeParsedMessageFields
} DNS_FREE_TYPE;

typedef enum _DNS_NAME_FORMAT {
  DnsNameDomain,
  DnsNameDomainLabel,
  DnsNameHostnameFull,
  DnsNameHostnameLabel,
  DnsNameWildcard,
  DnsNameSrvRecord
} DNS_NAME_FORMAT;

typedef enum {
  DnsSectionQuestion,
  DnsSectionAnswer,
  DnsSectionAuthority,
  DnsSectionAdditional
} DNS_SECTION;

typedef struct _IP4_ARRAY {
  DWORD AddrCount;
  IP4_ADDRESS AddrArray[1];
} IP4_ARRAY, *PIP4_ARRAY;

typedef struct {
  DWORD IP6Dword[4];
} IP6_ADDRESS, *PIP6_ADDRESS, DNS_IP6_ADDRESS, *PDNS_IP6_ADDRESS;

#pragma pack(push, 1)

typedef struct _DnsAddr
{
  CHAR MaxSa[DNS_ADDR_MAX_SOCKADDR_LENGTH];
#if defined(__midl) || defined(__WIDL__)
  DWORD DnsAddrUserDword[8];
#else
  union {
    DWORD DnsAddrUserDword[8];
  } Data;
#endif
} DNS_ADDR, *PDNS_ADDR;

typedef struct _DnsAddrArray
{
  DWORD MaxCount;
  DWORD AddrCount;
  DWORD Tag;
  WORD  Family;
  WORD  WordReserved;
  DWORD Flags;
  DWORD MatchFlag;
  DWORD Reserved1;
  DWORD Reserved2;
#if defined(__midl) || defined(__WIDL__)
  [size_is(AddrCount)] DNS_ADDR AddrArray[];
#else
  DNS_ADDR AddrArray[1];
#endif
} DNS_ADDR_ARRAY, *PDNS_ADDR_ARRAY;

typedef struct _DNS_HEADER {
  WORD Xid;
  BYTE RecursionDesired:1;
  BYTE Truncation:1;
  BYTE Authoritative:1;
  BYTE Opcode:4;
  BYTE IsResponse:1;
  BYTE ResponseCode:4;
  BYTE CheckingDisabled:1;
  BYTE AuthenticatedData:1;
  BYTE Reserved:1;
  BYTE RecursionAvailable:1;
  WORD QuestionCount;
  WORD AnswerCount;
  WORD NameServerCount;
  WORD AdditionalCount;
} DNS_HEADER, *PDNS_HEADER;

typedef struct _DNS_WIRE_QUESTION {
  WORD QuestionType;
  WORD QuestionClass;
} DNS_WIRE_QUESTION, *PDNS_WIRE_QUESTION;

typedef struct _DNS_WIRE_RECORD {
  WORD RecordType;
  WORD RecordClass;
  DWORD TimeToLive;
  WORD DataLength;
} DNS_WIRE_RECORD, *PDNS_WIRE_RECORD;

#pragma pack(pop)

typedef struct _DNS_MESSAGE_BUFFER {
  DNS_HEADER MessageHead;
  CHAR MessageBody[1];
} DNS_MESSAGE_BUFFER, *PDNS_MESSAGE_BUFFER;

typedef struct _DnsRecordFlags {
  DWORD Section:2;
  DWORD Delete:1;
  DWORD CharSet:2;
  DWORD Unused:3;
  DWORD Reserved:24;
} DNS_RECORD_FLAGS;

typedef struct {
  IP4_ADDRESS IpAddress;
} DNS_A_DATA, *PDNS_A_DATA;

typedef struct {
  DNS_IP6_ADDRESS Ip6Address;
} DNS_AAAA_DATA, *PDNS_AAAA_DATA;

typedef struct {
  BYTE AddressType;
  BYTE Address[DNS_ATMA_MAX_ADDR_LENGTH];
} DNS_ATMA_DATA, *PDNS_ATMA_DATA;

typedef struct {
  WORD wFlags;
  BYTE chProtocol;
  BYTE chAlgorithm;
  BYTE Key[1];
} DNS_KEY_DATA, *PDNS_KEY_DATA;

typedef struct {
  WORD wVersion;
  WORD wSize;
  WORD wHorPrec;
  WORD wVerPrec;
  DWORD dwLatitude;
  DWORD dwLongitude;
  DWORD dwAltitude;
} DNS_LOC_DATA, *PDNS_LOC_DATA;

typedef struct {
  LPSTR pNameMailbox;
  LPSTR pNameErrorsMailbox;
} DNS_MINFO_DATAA, *PDNS_MINFO_DATAA;

typedef struct {
  LPWSTR pNameMailbox;
  LPWSTR pNameErrorsMailbox;
} DNS_MINFO_DATAW, *PDNS_MINFO_DATAW;

typedef struct {
  LPSTR pNameExchange;
  WORD wPreference;
  WORD Pad;
} DNS_MX_DATAA, *PDNS_MX_DATAA;

typedef struct {
  LPWSTR pNameExchange;
  WORD wPreference;
  WORD Pad;
} DNS_MX_DATAW, *PDNS_MX_DATAW;

typedef struct {
  DWORD dwByteCount;
  BYTE Data[1];
} DNS_NULL_DATA, *PDNS_NULL_DATA;

typedef struct {
  LPSTR pNameNext;
  WORD wNumTypes;
  WORD wTypes[1];
} DNS_NXT_DATAA, *PDNS_NXT_DATAA;

typedef struct {
  LPWSTR pNameNext;
  WORD wNumTypes;
  WORD wTypes[1];
} DNS_NXT_DATAW, *PDNS_NXT_DATAW;

typedef struct {
  LPSTR pNameHost;
} DNS_PTR_DATAA, *PDNS_PTR_DATAA;

typedef struct {
  LPWSTR pNameHost;
} DNS_PTR_DATAW, *PDNS_PTR_DATAW;

typedef struct {
  LPSTR pNameSigner;
  WORD wTypeCovered;
  BYTE chAlgorithm;
  BYTE chLabelCount;
  DWORD dwOriginalTtl;
  DWORD dwExpiration;
  DWORD dwTimeSigned;
  WORD wKeyTag;
  WORD Pad;
  BYTE Signature[1];
} DNS_SIG_DATAA, *PDNS_SIG_DATAA;

typedef struct {
  LPWSTR pNameSigner;
  WORD wTypeCovered;
  BYTE chAlgorithm;
  BYTE chLabelCount;
  DWORD dwOriginalTtl;
  DWORD dwExpiration;
  DWORD dwTimeSigned;
  WORD wKeyTag;
  WORD Pad;
  BYTE Signature[1];
} DNS_SIG_DATAW, *PDNS_SIG_DATAW;

typedef struct {
  LPSTR pNamePrimaryServer;
  LPSTR pNameAdministrator;
  DWORD dwSerialNo;
  DWORD dwRefresh;
  DWORD dwRetry;
  DWORD dwExpire;
  DWORD dwDefaultTtl;
} DNS_SOA_DATAA, *PDNS_SOA_DATAA;

typedef struct {
  LPWSTR pNamePrimaryServer;
  LPWSTR pNameAdministrator;
  DWORD dwSerialNo;
  DWORD dwRefresh;
  DWORD dwRetry;
  DWORD dwExpire;
  DWORD dwDefaultTtl;
} DNS_SOA_DATAW, *PDNS_SOA_DATAW;

typedef struct {
  LPSTR pNameTarget;
  WORD wPriority;
  WORD wWeight;
  WORD wPort;
  WORD Pad;
} DNS_SRV_DATAA, *PDNS_SRV_DATAA;

typedef struct {
  LPWSTR pNameTarget;
  WORD wPriority;
  WORD wWeight;
  WORD wPort;
  WORD Pad;
} DNS_SRV_DATAW, *PDNS_SRV_DATAW;

typedef struct {
  DWORD dwStringCount;
  LPSTR pStringArray[1];
} DNS_TXT_DATAA, *PDNS_TXT_DATAA;

typedef struct {
  DWORD dwStringCount;
  LPWSTR pStringArray[1];
} DNS_TXT_DATAW, *PDNS_TXT_DATAW;

typedef struct {
  LPSTR pNameAlgorithm;
  PBYTE pAlgorithmPacket;
  PBYTE pKey;
  PBYTE pOtherData;
  DWORD dwCreateTime;
  DWORD dwExpireTime;
  WORD wMode;
  WORD wError;
  WORD wKeyLength;
  WORD wOtherLength;
  UCHAR cAlgNameLength;
  BOOL bPacketPointers;
} DNS_TKEY_DATAA, *PDNS_TKEY_DATAA;

typedef struct {
  LPWSTR pNameAlgorithm;
  PBYTE pAlgorithmPacket;
  PBYTE pKey;
  PBYTE pOtherData;
  DWORD dwCreateTime;
  DWORD dwExpireTime;
  WORD wMode;
  WORD wError;
  WORD wKeyLength;
  WORD wOtherLength;
  UCHAR cAlgNameLength;
  BOOL bPacketPointers;
} DNS_TKEY_DATAW, *PDNS_TKEY_DATAW;

typedef struct {
  LPSTR pNameAlgorithm;
  PBYTE pAlgorithmPacket;
  PBYTE pSignature;
  PBYTE pOtherData;
  LONGLONG i64CreateTime;
  WORD wFudgeTime;
  WORD wOriginalXid;
  WORD wError;
  WORD wSigLength;
  WORD wOtherLength;
  UCHAR cAlgNameLength;
  BOOL bPacketPointers;
} DNS_TSIG_DATAA, *PDNS_TSIG_DATAA;

typedef struct {
  LPWSTR pNameAlgorithm;
  PBYTE pAlgorithmPacket;
  PBYTE pSignature;
  PBYTE pOtherData;
  LONGLONG i64CreateTime;
  WORD wFudgeTime;
  WORD wOriginalXid;
  WORD wError;
  WORD wSigLength;
  WORD wOtherLength;
  UCHAR cAlgNameLength;
  BOOL bPacketPointers;
} DNS_TSIG_DATAW, *PDNS_TSIG_DATAW;

typedef struct {
  DWORD dwMappingFlag;
  DWORD dwLookupTimeout;
  DWORD dwCacheTimeout;
  DWORD cWinsServerCount;
  IP4_ADDRESS WinsServers[1];
} DNS_WINS_DATA, *PDNS_WINS_DATA;

typedef struct {
  DWORD dwMappingFlag;
  DWORD dwLookupTimeout;
  DWORD dwCacheTimeout;
  LPSTR pNameResultDomain;
} DNS_WINSR_DATAA, *PDNS_WINSR_DATAA;

typedef struct {
  DWORD dwMappingFlag;
  DWORD dwLookupTimeout;
  DWORD dwCacheTimeout;
  LPWSTR pNameResultDomain;
} DNS_WINSR_DATAW, *PDNS_WINSR_DATAW;

typedef struct {
  IP4_ADDRESS IpAddress;
  UCHAR chProtocol;
  BYTE BitMask[1];
} DNS_WKS_DATA, *PDNS_WKS_DATA;

typedef struct {
  WORD wKeyTag;
  BYTE chAlgorithm;
  BYTE chDigestType;
  WORD wDigestLength;
  WORD wPad;
  BYTE Digest[1];
} DNS_DS_DATA;

typedef struct {
  LPWSTR pNextDomainName;
  WORD wTypeBitMapsLength;
  WORD wPad;
  BYTE TypeBitMaps[1];
} DNS_NSEC_DATAW;

typedef struct {
  WORD wFlags;
  BYTE chProtocol;
  BYTE chAlgorithm;
  WORD wKeyLength;
  WORD wPad;
  BYTE Key[1];
} DNS_DNSKEY_DATA;

typedef struct {
  WORD wDataLength;
  WORD wPad;
  BYTE Data[1];
} DNS_OPT_DATA;

typedef struct {
  WORD wTypeCovered;
  BYTE chAlgorithm;
  BYTE chLabelCount;
  DWORD dwOriginalTtl;
  DWORD dwExpiration;
  DWORD dwTimeSigned;
  WORD wKeyTag;
  WORD Pad;
  LPWSTR pNameSigner;
  BYTE Signature[1];
} DNS_RRSIG_DATAW;

#if defined(__midl) || defined(__WIDL__)
typedef [switch_type(unsigned short )] union DNS_DATAW {
  [case(1)] DNS_A_DATA A;
  [case(6)] DNS_SOA_DATAW SOA;
  [case(12)] DNS_PTR_DATAW PTR;
  [case(2)] DNS_PTR_DATAW NS;
  [case(5)] DNS_PTR_DATAW CNAME;
  [case(7)] DNS_PTR_DATAW MB;
  [case(3)] DNS_PTR_DATAW MD;
  [case(4)] DNS_PTR_DATAW MF;
  [case(8)] DNS_PTR_DATAW MG;
  [case(9)] DNS_PTR_DATAW MR;
  [case(14)] DNS_MINFO_DATAW MINFO;
  [case(17)] DNS_MINFO_DATAW RP;
  [case(15)] DNS_MX_DATAW MX;
  [case(18)] DNS_MX_DATAW AFSDB;
  [case(21)] DNS_MX_DATAW RT;
  [case(28)] DNS_AAAA_DATA AAAA;
  [case(33)] DNS_SRV_DATAW SRV;
  [case(34)] DNS_ATMA_DATA ATMA;
  [case(43)] DNS_DS_DATA DS;
  [case(46)] DNS_RRSIG_DATAW RRSIG;
  [case(47)] DNS_NSEC_DATAW NSEC;
  [case(48)] DNS_DNSKEY_DATA DNSKEY;
  [case(41)] DNS_OPT_DATA OPT;
} DNS_DATAW;

typedef struct _DnsRecordW {
  [unique]struct _DnsRecordW *pNext;
  [unique][string] LPWSTR  pName;
  WORD wType;
  WORD wDataLength;
  DWORD Flags;
  DWORD dwTtl;
  DWORD dwReserved;
  [switch_is(wType)] DNS_DATAW Data;
} DNS_RECORDW, *PDNS_RECORDW;
#else
typedef struct _DnsRecordA {
  struct _DnsRecordA* pNext;
  LPSTR pName;
  WORD wType;
  WORD wDataLength;
  union {
    DWORD DW;
    DNS_RECORD_FLAGS S;
  } Flags;
  DWORD dwTtl;
  DWORD dwReserved;
  union {
    DNS_A_DATA A;
    DNS_SOA_DATAA SOA, Soa;
    DNS_PTR_DATAA PTR, Ptr, NS, Ns, CNAME, Cname, MB, Mb, MD, Md, MF, Mf, MG, Mg, MR, Mr;
    DNS_MINFO_DATAA MINFO, Minfo, RP, Rp;
    DNS_MX_DATAA MX, Mx, AFSDB, Afsdb, RT, Rt;
    DNS_TXT_DATAA HINFO, Hinfo, ISDN, Isdn, TXT, Txt, X25;
    DNS_NULL_DATA Null;
    DNS_WKS_DATA WKS, Wks;
    DNS_AAAA_DATA AAAA;
    DNS_KEY_DATA KEY, Key;
    DNS_SIG_DATAA SIG, Sig;
    DNS_ATMA_DATA ATMA, Atma;
    DNS_NXT_DATAA NXT, Nxt;
    DNS_SRV_DATAA SRV, Srv;
    DNS_TKEY_DATAA TKEY, Tkey;
    DNS_TSIG_DATAA TSIG, Tsig;
    DNS_WINS_DATA WINS, Wins;
    DNS_WINSR_DATAA WINSR, WinsR, NBSTAT, Nbstat;
  } Data;
} DNS_RECORDA, *PDNS_RECORDA;

typedef struct _DnsRecordW {
  struct _DnsRecordW* pNext;
  LPWSTR pName;
  WORD wType;
  WORD wDataLength;
  union {
    DWORD DW;
    DNS_RECORD_FLAGS S;
  } Flags;
  DWORD dwTtl;
  DWORD dwReserved;
  union {
    DNS_A_DATA A;
    DNS_SOA_DATAW SOA, Soa;
    DNS_PTR_DATAW PTR, Ptr, NS, Ns, CNAME, Cname, MB, Mb, MD, Md, MF, Mf, MG, Mg, MR, Mr;
    DNS_MINFO_DATAW MINFO, Minfo, RP, Rp;
    DNS_MX_DATAW MX, Mx, AFSDB, Afsdb, RT, Rt;
    DNS_TXT_DATAW HINFO, Hinfo, ISDN, Isdn, TXT, Txt, X25;
    DNS_NULL_DATA Null;
    DNS_WKS_DATA WKS, Wks;
    DNS_AAAA_DATA AAAA;
    DNS_KEY_DATA KEY, Key;
    DNS_SIG_DATAW SIG, Sig;
    DNS_ATMA_DATA ATMA, Atma;
    DNS_NXT_DATAW NXT, Nxt;
    DNS_SRV_DATAW SRV, Srv;
    DNS_TKEY_DATAW TKEY, Tkey;
    DNS_TSIG_DATAW TSIG, Tsig;
    DNS_WINS_DATA WINS, Wins;
    DNS_WINSR_DATAW WINSR, WinsR, NBSTAT, Nbstat;
  } Data;
} DNS_RECORDW, *PDNS_RECORDW;
#endif

#ifdef UNICODE
#define DNS_RECORD DNS_RECORDW
#define PDNS_RECORD PDNS_RECORDW
#else
#define DNS_RECORD DNS_RECORDA
#define PDNS_RECORD PDNS_RECORDA
#endif

typedef struct _DnsRRSet {
  PDNS_RECORD pFirstRR;
  PDNS_RECORD pLastRR;
} DNS_RRSET, *PDNS_RRSET;

#define DNS_RRSET_INIT(rrset)                            \
{                                                        \
    PDNS_RRSET  _prrset = &(rrset);                      \
    _prrset->pFirstRR = NULL;                            \
    _prrset->pLastRR = (PDNS_RECORD) &_prrset->pFirstRR; \
}

#define DNS_RRSET_ADD(rrset, pnewRR)   \
{                                      \
    PDNS_RRSET  _prrset = &(rrset);    \
    PDNS_RECORD _prrnew = (pnewRR);    \
    _prrset->pLastRR->pNext = _prrnew; \
    _prrset->pLastRR = _prrnew;        \
}

#define DNS_RRSET_TERMINATE(rrset)   \
{                                    \
    PDNS_RRSET  _prrset = &(rrset);  \
    _prrset->pLastRR->pNext = NULL;  \
}

#ifndef __WIDL__
// HACK

DNS_STATUS
WINAPI
DnsAcquireContextHandle_A(
  _In_ DWORD CredentialFlags,
  _In_opt_ PVOID Credentials,
  _Outptr_ PHANDLE pContext);

DNS_STATUS
WINAPI
DnsAcquireContextHandle_W(
  _In_ DWORD CredentialFlags,
  _In_opt_ PVOID Credentials,
  _Outptr_ PHANDLE pContext);

DNS_STATUS
WINAPI
DnsExtractRecordsFromMessage_W(
  _In_ PDNS_MESSAGE_BUFFER pDnsBuffer,
  _In_ WORD wMessageLength,
  _Outptr_ PDNS_RECORD *ppRecord);

DNS_STATUS
WINAPI
DnsExtractRecordsFromMessage_UTF8(
  _In_ PDNS_MESSAGE_BUFFER pDnsBuffer,
  _In_ WORD wMessageLength,
  _Outptr_ PDNS_RECORD *ppRecord);

DNS_STATUS
WINAPI
DnsModifyRecordsInSet_A(
  _In_opt_ PDNS_RECORD pAddRecords,
  _In_opt_ PDNS_RECORD pDeleteRecords,
  _In_ DWORD Options,
  _In_opt_ HANDLE hCredentials,
  _Inout_opt_ PIP4_ARRAY pExtraList,
  _Inout_opt_ PVOID pReserved);

DNS_STATUS
WINAPI
DnsModifyRecordsInSet_W(
  _In_opt_ PDNS_RECORD pAddRecords,
  _In_opt_ PDNS_RECORD pDeleteRecords,
  _In_ DWORD Options,
  _In_opt_ HANDLE hCredentials,
  _Inout_opt_ PIP4_ARRAY pExtraList,
  _Inout_opt_ PVOID pReserved);

DNS_STATUS
WINAPI
DnsModifyRecordsInSet_UTF8(
  _In_opt_ PDNS_RECORD pAddRecords,
  _In_opt_ PDNS_RECORD pDeleteRecords,
  _In_ DWORD Options,
  _In_opt_ HANDLE hCredentials,
  _Inout_opt_ PIP4_ARRAY pExtraList,
  _Inout_opt_ PVOID pReserved);

BOOL
WINAPI
DnsNameCompare_A(
  _In_ PCSTR pszName1,
  _In_ PCSTR pszName2);

BOOL
WINAPI
DnsNameCompare_W(
  _In_ PCWSTR pszwName1,
  _In_ PCWSTR pszwName2);

DNS_STATUS
WINAPI
DnsQuery_A(
  _In_ PCSTR pszName,
  _In_ WORD wType,
  _In_ DWORD Options,
  _Inout_opt_ PVOID pExtra,
  _Outptr_result_maybenull_ PDNS_RECORD *ppQueryResults,
  _Outptr_opt_result_maybenull_ PVOID *pReserved);

DNS_STATUS
WINAPI
DnsQuery_W(
  _In_ PCWSTR pszName,
  _In_ WORD wType,
  _In_ DWORD Options,
  _Inout_opt_ PVOID pExtra,
  _Outptr_result_maybenull_ PDNS_RECORD *ppQueryResults,
  _Outptr_opt_result_maybenull_ PVOID *pReserved);

DNS_STATUS
WINAPI
DnsQuery_UTF8(
  _In_ PCSTR pszName,
  _In_ WORD wType,
  _In_ DWORD Options,
  _Inout_opt_ PVOID pExtra,
  _Outptr_result_maybenull_ PDNS_RECORD *ppQueryResults,
  _Outptr_opt_result_maybenull_ PVOID *pReserved);

DNS_STATUS
WINAPI
DnsQueryConfig(
  _In_ DNS_CONFIG_TYPE Config,
  _In_ DWORD Flag,
  _In_opt_ PWSTR pwsAdapterName,
  _In_opt_ PVOID pReserved,
  _Out_writes_bytes_to_opt_(*pBufLen, *pBufLen) PVOID pBuffer,
  _Inout_ PDWORD pBufLen);

BOOL
WINAPI
DnsRecordCompare(
  _In_ PDNS_RECORD,
  _In_ PDNS_RECORD);

PDNS_RECORD
WINAPI
DnsRecordCopyEx(
  _In_ PDNS_RECORD pRecord,
  _In_ DNS_CHARSET CharSetIn,
  _In_ DNS_CHARSET CharSetOut);

void
WINAPI
DnsRecordListFree(
  _Inout_opt_ PDNS_RECORD,
  _In_ DNS_FREE_TYPE);

BOOL
WINAPI
DnsRecordSetCompare(
  _Inout_ PDNS_RECORD pRR1,
  _Inout_ PDNS_RECORD pRR2,
  _Outptr_opt_result_maybenull_ PDNS_RECORD *ppDiff1,
  _Outptr_opt_result_maybenull_ PDNS_RECORD *ppDiff2);

PDNS_RECORD
WINAPI
DnsRecordSetCopyEx(
  _In_ PDNS_RECORD pRecordSet,
  _In_ DNS_CHARSET CharSetIn,
  _In_ DNS_CHARSET CharSetOut);

PDNS_RECORD
WINAPI
DnsRecordSetDetach(
  _Inout_ PDNS_RECORD);

void
WINAPI
DnsReleaseContextHandle(
  _In_ HANDLE);

DNS_STATUS
WINAPI
DnsReplaceRecordSetA(
  _In_ PDNS_RECORD pReplaceSet,
  _In_ DWORD Options,
  _In_opt_ HANDLE hContext,
  _Inout_opt_ PIP4_ARRAY pExtraInfo,
  _Inout_opt_ PVOID pReserved);

DNS_STATUS
WINAPI
DnsReplaceRecordSetW(
  _In_ PDNS_RECORD pReplaceSet,
  _In_ DWORD Options,
  _In_opt_ HANDLE hContext,
  _Inout_opt_ PIP4_ARRAY pExtraInfo,
  _Inout_opt_ PVOID pReserved);

DNS_STATUS
WINAPI
DnsReplaceRecordSetUTF8(
  _In_ PDNS_RECORD pReplaceSet,
  _In_ DWORD Options,
  _In_opt_ HANDLE hContext,
  _Inout_opt_ PIP4_ARRAY pExtraInfo,
  _Inout_opt_ PVOID pReserved);

DNS_STATUS
WINAPI
DnsValidateName_A(
  _In_ LPCSTR,
  _In_ DNS_NAME_FORMAT);

DNS_STATUS
WINAPI
DnsValidateName_W(
  _In_ LPCWSTR,
  _In_ DNS_NAME_FORMAT);

DNS_STATUS
WINAPI
DnsValidateName_UTF8(
  _In_ LPCSTR,
  _In_ DNS_NAME_FORMAT);

BOOL
WINAPI
DnsWriteQuestionToBuffer_W(
  _Inout_ PDNS_MESSAGE_BUFFER pDnsBuffer,
  _Inout_ PDWORD pdwBufferSize,
  _In_ LPWSTR pszName,
  _In_ WORD wType,
  _In_ WORD Xid,
  _In_ BOOL fRecursionDesired);

BOOL
WINAPI
DnsWriteQuestionToBuffer_UTF8(
  _Inout_ PDNS_MESSAGE_BUFFER pDnsBuffer,
  _Inout_ PDWORD pdwBufferSize,
  _In_ LPSTR pszName,
  _In_ WORD wType,
  _In_ WORD Xid,
  _In_ BOOL fRecursionDesired);
#endif

#ifdef UNICODE

#define DNS_MINFO_DATA DNS_MINFO_DATAW
#define PDNS_MINFO_DATA PDNS_MINFO_DATAW
#define DNS_MX_DATA DNS_MX_DATAW
#define PDNS_MX_DATA PDNS_MX_DATAW
#define DNS_NXT_DATA DNS_NXT_DATAW
#define PDNS_NXT_DATA PDNS_NXT_DATAW
#define DNS_PTR_DATA DNS_PTR_DATAW
#define PDNS_PTR_DATA PDNS_PTR_DATAW
#define DNS_SIG_DATA DNS_SIG_DATAW
#define PDNS_SIG_DATA PDNS_SIG_DATAW
#define DNS_SOA_DATA DNS_SOA_DATAW
#define PDNS_SOA_DATA PDNS_SOA_DATAW
#define DNS_TXT_DATA DNS_TXT_DATAW
#define PDNS_TXT_DATA PDNS_TXT_DATAW
#define DNS_TKEY_DATA DNS_TKEY_DATAW
#define PDNS_TKEY_DATA PDNS_TKEY_DATAW
#define DNS_WINSR_DATA DNS_WINSR_DATAW
#define PDNS_WINSR_DATA PDNS_WINSR_DATAW
#define DnsAcquireContextHandle DnsAcquireContextHandle_W
#define DnsModifyRecordsInSet DnsModifyRecordsInSet_W
#define DnsNameCompare DnsNameCompare_W
#define DnsQuery DnsQuery_W
#define DnsReplaceRecordSet DnsReplaceRecordSetW
#define DnsValidateName DnsValidateName_W

#else /* UNICODE */

#define DNS_MINFO_DATA DNS_MINFO_DATAA
#define PDNS_MINFO_DATA PDNS_MINFO_DATAA
#define DNS_MX_DATA DNS_MX_DATAA
#define PDNS_MX_DATA PDNS_MX_DATAA
#define DNS_NXT_DATA DNS_NXT_DATAA
#define PDNS_NXT_DATA PDNS_NXT_DATAA
#define DNS_PTR_DATA DNS_PTR_DATAA
#define PDNS_PTR_DATA PDNS_PTR_DATAA
#define DNS_SIG_DATA DNS_SIG_DATAA
#define PDNS_SIG_DATA PDNS_SIG_DATAA
#define DNS_SOA_DATA DNS_SOA_DATAA
#define PDNS_SOA_DATA PDNS_SOA_DATAA
#define DNS_TXT_DATA DNS_TXT_DATAA
#define PDNS_TXT_DATA PDNS_TXT_DATAA
#define DNS_TKEY_DATA DNS_TKEY_DATAA
#define PDNS_TKEY_DATA PDNS_TKEY_DATAA
#define DNS_WINSR_DATA DNS_WINSR_DATAA
#define PDNS_WINSR_DATA PDNS_WINSR_DATAA
#define DnsAcquireContextHandle DnsAcquireContextHandle_A
#define DnsModifyRecordsInSet DnsModifyRecordsInSet_A
#define DnsNameCompare DnsNameCompare_A
#define DnsQuery DnsQuery_A
#define DnsReplaceRecordSet DnsReplaceRecordSetA
#define DnsValidateName DnsValidateName_A

#endif /* UNICODE */

#endif /* RC_INVOKED */

#ifdef __cplusplus
}
#endif

#ifndef DNS_APP_SETTINGS_EXCLUSIVE_SERVERS
#define DNS_APP_SETTINGS_EXCLUSIVE_SERVERS 0x1
#endif
#ifndef DNS_APP_SETTINGS_VERSION1
#define DNS_APP_SETTINGS_VERSION1 0x1
#endif
#ifndef DNS_CLASS_ALL
#define DNS_CLASS_ALL 0x00ff
#endif
#ifndef DNS_CLASS_ANY
#define DNS_CLASS_ANY 0x00ff
#endif
#ifndef DNS_CLASS_CHAOS
#define DNS_CLASS_CHAOS 0x0003
#endif
#ifndef DNS_CLASS_CSNET
#define DNS_CLASS_CSNET 0x0002
#endif
#ifndef DNS_CLASS_HESIOD
#define DNS_CLASS_HESIOD 0x0004
#endif
#ifndef DNS_CLASS_INTERNET
#define DNS_CLASS_INTERNET 0x0001
#endif
#ifndef DNS_CLASS_NONE
#define DNS_CLASS_NONE 0x00fe
#endif
#ifndef DNS_CLASS_UNICAST_RESPONSE
#define DNS_CLASS_UNICAST_RESPONSE 0x8000
#endif
#ifndef DNS_COMPRESSED_QUESTION_NAME
#define DNS_COMPRESSED_QUESTION_NAME 0xC00C
#endif
#ifndef DNS_CONNECTION_NAME_MAX_LENGTH
#define DNS_CONNECTION_NAME_MAX_LENGTH 64
#endif
#ifndef DNS_CONNECTION_POLICY_ENTRY_ONDEMAND
#define DNS_CONNECTION_POLICY_ENTRY_ONDEMAND 0x00000001
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_CURRENT_VERSION
#define DNS_CONNECTION_PROXY_INFO_CURRENT_VERSION 1
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_EXCEPTION_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_EXCEPTION_MAX_LENGTH 1024
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_EXTRA_INFO_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_EXTRA_INFO_MAX_LENGTH 1024
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_FLAG_BYPASSLOCAL
#define DNS_CONNECTION_PROXY_INFO_FLAG_BYPASSLOCAL 0x2
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_FLAG_DISABLED
#define DNS_CONNECTION_PROXY_INFO_FLAG_DISABLED 0x1
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_FRIENDLY_NAME_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_FRIENDLY_NAME_MAX_LENGTH 64
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_PASSWORD_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_PASSWORD_MAX_LENGTH 128
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_SERVER_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_SERVER_MAX_LENGTH 256
#endif
#ifndef DNS_CONNECTION_PROXY_INFO_USERNAME_MAX_LENGTH
#define DNS_CONNECTION_PROXY_INFO_USERNAME_MAX_LENGTH 128
#endif
#ifndef DNS_MAX_IP6_REVERSE_NAME_LENGTH
#define DNS_MAX_IP6_REVERSE_NAME_LENGTH 75
#endif
#ifndef DNS_MAX_TEXT_STRING_LENGTH
#define DNS_MAX_TEXT_STRING_LENGTH 255
#endif
#ifndef DNS_PORT_HOST_ORDER
#define DNS_PORT_HOST_ORDER 0x0035
#endif
#ifndef DNS_PORT_NET_ORDER
#define DNS_PORT_NET_ORDER 0x3500
#endif
#ifndef DNS_PROTOCOL_DOH
#define DNS_PROTOCOL_DOH 3
#endif
#ifndef DNS_PROTOCOL_DOT
#define DNS_PROTOCOL_DOT 4
#endif
#ifndef DNS_PROTOCOL_NO_WIRE
#define DNS_PROTOCOL_NO_WIRE 5
#endif
#ifndef DNS_PROTOCOL_TCP
#define DNS_PROTOCOL_TCP 2
#endif
#ifndef DNS_PROTOCOL_UDP
#define DNS_PROTOCOL_UDP 1
#endif
#ifndef DNS_PROTOCOL_UNSPECIFIED
#define DNS_PROTOCOL_UNSPECIFIED 0
#endif
#ifndef DNS_QUERY_DNSSEC_REQUIRED
#define DNS_QUERY_DNSSEC_REQUIRED 0x04000000
#endif
#ifndef DNS_QUERY_PARSE_ALL_RECORDS
#define DNS_QUERY_PARSE_ALL_RECORDS 0x0400000000000000
#endif
#ifndef DNS_QUERY_RAW_OPTION_BEST_EFFORT_PARSE
#define DNS_QUERY_RAW_OPTION_BEST_EFFORT_PARSE 0x0000000000000001
#endif
#ifndef DNS_QUERY_RAW_REQUEST_VERSION1
#define DNS_QUERY_RAW_REQUEST_VERSION1 0x1
#endif
#ifndef DNS_QUERY_RAW_RESULTS_VERSION1
#define DNS_QUERY_RAW_RESULTS_VERSION1 0x1
#endif
#ifndef DNS_QUERY_REQUEST_VERSION2
#define DNS_QUERY_REQUEST_VERSION2 0x2
#endif
#ifndef DNS_QUERY_REQUEST_VERSION3
#define DNS_QUERY_REQUEST_VERSION3 0x3
#endif
#ifndef DNS_RCLASS_ALL
#define DNS_RCLASS_ALL 0xff00
#endif
#ifndef DNS_RCLASS_ANY
#define DNS_RCLASS_ANY 0xff00
#endif
#ifndef DNS_RCLASS_CHAOS
#define DNS_RCLASS_CHAOS 0x0300
#endif
#ifndef DNS_RCLASS_CSNET
#define DNS_RCLASS_CSNET 0x0200
#endif
#ifndef DNS_RCLASS_HESIOD
#define DNS_RCLASS_HESIOD 0x0400
#endif
#ifndef DNS_RCLASS_INTERNET
#define DNS_RCLASS_INTERNET 0x0100
#endif
#ifndef DNS_RCLASS_MDNS_CACHE_FLUSH
#define DNS_RCLASS_MDNS_CACHE_FLUSH 0x0080
#endif
#ifndef DNS_RCLASS_NONE
#define DNS_RCLASS_NONE 0xfe00
#endif
#ifndef DNS_RCLASS_UNICAST_RESPONSE
#define DNS_RCLASS_UNICAST_RESPONSE 0x0080
#endif
#ifndef DNS_UPDATE_REMOTE_SERVER
#define DNS_UPDATE_REMOTE_SERVER 0x00004000
#endif
#ifndef DNS_UPDATE_SKIP_NO_UPDATE_ADAPTERS
#define DNS_UPDATE_SKIP_NO_UPDATE_ADAPTERS 0x00002000
#endif
#ifndef DNS_UPDATE_TRY_ALL_MASTER_SERVERS
#define DNS_UPDATE_TRY_ALL_MASTER_SERVERS 0x00001000
#endif
#ifndef DNS_VALSVR_ERROR_INVALID_NAME
#define DNS_VALSVR_ERROR_INVALID_NAME 0x02
#endif
#ifndef DNS_VALSVR_ERROR_NO_AUTH
#define DNS_VALSVR_ERROR_NO_AUTH 0x05
#endif
#ifndef DNS_VALSVR_ERROR_NO_RESPONSE
#define DNS_VALSVR_ERROR_NO_RESPONSE 0x04
#endif
#ifndef DNS_VALSVR_ERROR_NO_TCP
#define DNS_VALSVR_ERROR_NO_TCP 0x10
#endif
#ifndef DNS_VALSVR_ERROR_REFUSED
#define DNS_VALSVR_ERROR_REFUSED 0x06
#endif
#ifndef DNS_VALSVR_ERROR_UNKNOWN
#define DNS_VALSVR_ERROR_UNKNOWN 0xFF
#endif
#ifndef DNS_VALSVR_ERROR_UNREACHABLE
#define DNS_VALSVR_ERROR_UNREACHABLE 0x03
#endif
#ifndef DNS_WINS_FLAG_LOCAL
#define DNS_WINS_FLAG_LOCAL 0x00010000
#endif
#ifndef DNS_WINS_FLAG_SCOPE
#define DNS_WINS_FLAG_SCOPE 0x80000000
#endif
#ifndef INTERNET_DEFAULT_DOT_PORT
#define INTERNET_DEFAULT_DOT_PORT 853
#endif

typedef enum _DNS_CONNECTION_PROXY_INFO_SWITCH {
    DNS_CONNECTION_PROXY_INFO_SWITCH_CONFIG = 0,
    DNS_CONNECTION_PROXY_INFO_SWITCH_SCRIPT = 1,
    DNS_CONNECTION_PROXY_INFO_SWITCH_WPAD = 2
} DNS_CONNECTION_PROXY_INFO_SWITCH;

typedef enum _DNS_CONNECTION_PROXY_TYPE {
    DNS_CONNECTION_PROXY_TYPE_NULL = 0,
    DNS_CONNECTION_PROXY_TYPE_HTTP = 1,
    DNS_CONNECTION_PROXY_TYPE_WAP = 2,
    DNS_CONNECTION_PROXY_TYPE_SOCKS4 = 4,
    DNS_CONNECTION_PROXY_TYPE_SOCKS5 = 5
} DNS_CONNECTION_PROXY_TYPE;

typedef enum DNS_CONNECTION_POLICY_TAG {
    TAG_DNS_CONNECTION_POLICY_TAG_DEFAULT = 0,
    TAG_DNS_CONNECTION_POLICY_TAG_CONNECTION_MANAGER = 1,
    TAG_DNS_CONNECTION_POLICY_TAG_WWWPT = 2
} DNS_CONNECTION_POLICY_TAG;

typedef enum DNS_PROXY_INFORMATION_TYPE {
    DNS_PROXY_INFORMATION_DIRECT = 0,
    DNS_PROXY_INFORMATION_DEFAULT_SETTINGS = 1,
    DNS_PROXY_INFORMATION_PROXY_NAME = 2,
    DNS_PROXY_INFORMATION_DOES_NOT_EXIST = 3
} DNS_PROXY_INFORMATION_TYPE;

typedef struct _DNS_APPLICATION_SETTINGS {
  ULONG Version;
  ULONG64 Flags;
} DNS_APPLICATION_SETTINGS;

typedef struct _DNS_CONNECTION_IFINDEX_ENTRY {
  PCWSTR pwszConnectionName;
  DWORD dwIfIndex;
} DNS_CONNECTION_IFINDEX_ENTRY;

typedef struct _DNS_CONNECTION_IFINDEX_LIST {
  DNS_CONNECTION_IFINDEX_ENTRY * pConnectionIfIndexEntries;
  DWORD nEntries;
} DNS_CONNECTION_IFINDEX_LIST;

typedef struct _DNS_CONNECTION_NAME {
  WCHAR wszName[65];
} DNS_CONNECTION_NAME;

typedef struct _DNS_CONNECTION_NAME_LIST {
  DWORD cNames;
  DNS_CONNECTION_NAME * pNames;
} DNS_CONNECTION_NAME_LIST;

typedef struct _DNS_CONNECTION_POLICY_ENTRY {
  PCWSTR pwszHost;
  PCWSTR pwszAppId;
  DWORD cbAppSid;
  PBYTE pbAppSid;
  DWORD nConnections;
  PCWSTR * ppwszConnections;
  DWORD dwPolicyEntryFlags;
} DNS_CONNECTION_POLICY_ENTRY, *PDNS_CONNECTION_POLICY_ENTRY;

typedef struct _DNS_CONNECTION_POLICY_ENTRY_LIST {
  DNS_CONNECTION_POLICY_ENTRY * pPolicyEntries;
  DWORD nEntries;
} DNS_CONNECTION_POLICY_ENTRY_LIST;

typedef struct _DNS_QUERY_RAW_CANCEL {
  CHAR reserved[32];
} DNS_QUERY_RAW_CANCEL;

typedef struct _DNS_SERVICE_INSTANCE {
  LPWSTR pszInstanceName;
  LPWSTR pszHostName;
  IP4_ADDRESS * ip4Address;
  IP6_ADDRESS * ip6Address;
  WORD wPort;
  WORD wPriority;
  WORD wWeight;
  DWORD dwPropertyCount;
  PWSTR * keys;
  PWSTR * values;
  DWORD dwInterfaceIndex;
} DNS_SERVICE_INSTANCE, *PDNS_SERVICE_INSTANCE;

typedef struct _MDNS_QUERY_HANDLE {
  WCHAR nameBuf[256];
  WORD wType;
  PVOID pSubscription;
  PVOID pWnfCallbackParams;
  ULONG stateNameData[2];
} MDNS_QUERY_HANDLE, *PMDNS_QUERY_HANDLE;

typedef struct DNS_PROXY_INFORMATION {
  ULONG version;
  DNS_PROXY_INFORMATION_TYPE proxyInformationType;
  PWSTR proxyName;
} DNS_PROXY_INFORMATION;

#if (!(defined(UNICODE)))
#define DNS_IP4_REVERSE_DOMAIN_STRING DNS_IP4_REVERSE_DOMAIN_STRING_A
#endif
#define DNS_IP4_REVERSE_DOMAIN_STRING_A ("in-addr.arpa.")
#define DNS_IP4_REVERSE_DOMAIN_STRING_W (L"in-addr.arpa.")
#ifdef UNICODE
#define DNS_IP6_REVERSE_DOMAIN_STRING DNS_IP6_REVERSE_DOMAIN_STRING_W
#else
#if (!(defined(UNICODE)))
#define DNS_IP6_REVERSE_DOMAIN_STRING DNS_IP6_REVERSE_DOMAIN_STRING_A
#endif
#endif
#define DNS_IP6_REVERSE_DOMAIN_STRING_A ("ip6.arpa.")
#define DNS_IP6_REVERSE_DOMAIN_STRING_W (L"ip6.arpa.")
#define DNS_MAX_IP4_REVERSE_NAME_BUFFER_LENGTH DNS_MAX_IP4_REVERSE_NAME_LENGTH
#define DNS_MAX_IP4_REVERSE_NAME_LENGTH (IP4_ADDRESS_STRING_LENGTH + 15)
#define DNS_MAX_IP6_REVERSE_NAME_BUFFER_LENGTH 0x4B
#define DNS_MAX_REVERSE_NAME_BUFFER_LENGTH 0x4B
#define DNS_MAX_REVERSE_NAME_LENGTH 0x4B
#define DNS_OFFSET_TO_QUESTION_NAME 0xC
#define DNS_QUERY_CACHE_ONLY 0x10
#define DNS_VALSVR_ERROR_INVALID_ADDR 0x01
#define DnsFreeRecordListDeep 1
#define INTERNET_DEFAULT_DNS_PORT 0x35

struct _DNS_QUERY_RAW_RESULT
{
    ULONG version;
    DNS_STATUS queryStatus;
    ULONG64 queryOptions;
    ULONG64 queryRawOptions;
    ULONG64 responseFlags;
    ULONG queryRawResponseSize;
    BYTE *queryRawResponse;
    PDNS_RECORD queryRecords;
    ULONG protocol;
    union
    {
        CHAR maxSa[32];
    };
};

typedef void (WINAPI *DNS_PROXY_COMPLETION_ROUTINE)(void *, DNS_STATUS);

typedef struct _DNS_QUERY_RAW_RESULT DNS_QUERY_RAW_RESULT;

typedef void (WINAPI *DNS_QUERY_RAW_COMPLETION_ROUTINE)(void *, DNS_QUERY_RAW_RESULT *);

#if (!defined(__midl))
typedef void WINAPI DNS_SERVICE_REGISTER_COMPLETE(DWORD, PVOID, PDNS_SERVICE_INSTANCE);

typedef void WINAPI DNS_SERVICE_RESOLVE_COMPLETE(DWORD, PVOID, PDNS_SERVICE_INSTANCE);
#endif

typedef void WINAPI MDNS_QUERY_CALLBACK(PVOID, PMDNS_QUERY_HANDLE, PDNS_QUERY_RESULT);

#if (!defined(__midl))
typedef DNS_SERVICE_REGISTER_COMPLETE *PDNS_SERVICE_REGISTER_COMPLETE;

struct _DNS_SERVICE_REGISTER_REQUEST
{
    ULONG Version;
    ULONG InterfaceIndex;
    PDNS_SERVICE_INSTANCE pServiceInstance;
    PDNS_SERVICE_REGISTER_COMPLETE pRegisterCompletionCallback;
    PVOID pQueryContext;
    HANDLE hCredentials;
    BOOL unicastEnabled;
};

typedef struct _DNS_SERVICE_REGISTER_REQUEST DNS_SERVICE_REGISTER_REQUEST;

typedef struct _DNS_SERVICE_REGISTER_REQUEST *PDNS_SERVICE_REGISTER_REQUEST;

typedef DNS_SERVICE_RESOLVE_COMPLETE *PDNS_SERVICE_RESOLVE_COMPLETE;

struct _DNS_SERVICE_RESOLVE_REQUEST
{
    ULONG Version;
    ULONG InterfaceIndex;
    PWSTR QueryName;
    PDNS_SERVICE_RESOLVE_COMPLETE pResolveCompletionCallback;
    PVOID pQueryContext;
};

typedef struct _DNS_SERVICE_RESOLVE_REQUEST DNS_SERVICE_RESOLVE_REQUEST;

typedef struct _DNS_SERVICE_RESOLVE_REQUEST *PDNS_SERVICE_RESOLVE_REQUEST;
#endif

typedef MDNS_QUERY_CALLBACK *PMDNS_QUERY_CALLBACK;

struct _MDNS_QUERY_REQUEST
{
    ULONG Version;
    ULONG ulRefCount;
    PCWSTR Query;
    WORD QueryType;
    ULONG64 QueryOptions;
    ULONG InterfaceIndex;
    PMDNS_QUERY_CALLBACK pQueryCallback;
    PVOID pQueryContext;
    BOOL fAnswerReceived;
    ULONG ulResendCount;
};

typedef struct _MDNS_QUERY_REQUEST MDNS_QUERY_REQUEST;

typedef struct _MDNS_QUERY_REQUEST *PMDNS_QUERY_REQUEST;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
DNS_STATUS WINAPI DnsCancelQueryRaw(DNS_QUERY_RAW_CANCEL *cancelHandle);
DWORD DnsConnectionDeletePolicyEntries(DNS_CONNECTION_POLICY_TAG PolicyEntryTag);
DWORD DnsConnectionDeleteProxyInfo(PCWSTR pwszConnectionName, DNS_CONNECTION_PROXY_TYPE Type);
void DnsConnectionFreeNameList(DNS_CONNECTION_NAME_LIST *pNameList);
DWORD DnsConnectionGetNameList(DNS_CONNECTION_NAME_LIST *pNameList);
DWORD DnsConnectionSetPolicyEntries(DNS_CONNECTION_POLICY_TAG PolicyEntryTag, DNS_CONNECTION_POLICY_ENTRY_LIST *pPolicyEntryList);
DWORD DnsConnectionUpdateIfIndexTable(DNS_CONNECTION_IFINDEX_LIST *pConnectionIfIndexEntries);
void DnsFreeProxyName(PWSTR proxyName);
DWORD WINAPI DnsGetProxyInformation(PCWSTR hostName, DNS_PROXY_INFORMATION *proxyInformation, DNS_PROXY_INFORMATION *defaultProxyInformation, DNS_PROXY_COMPLETION_ROUTINE completionRoutine, void *completionContext);
DNS_STATUS WINAPI DnsIsFlatRecord(PDNS_RECORD pRecord, ULONG64 ullFlags, BOOL *pfFlat);
BOOL WINAPI DnsIsZtEnabled(void);
void DnsQueryRawResultFree(DNS_QUERY_RAW_RESULT *queryResults);
#if (!defined(__midl))
#if (!defined(__midl))
typedef struct _DNS_SERVICE_CANCEL *PDNS_SERVICE_CANCEL;
#endif

DNS_STATUS WINAPI DnsServiceBrowseCancel(PDNS_SERVICE_CANCEL pCancelHandle);
typedef DWORD *PIP4_ADDRESS;

PDNS_SERVICE_INSTANCE WINAPI DnsServiceConstructInstance(PCWSTR pServiceName, PCWSTR pHostName, PIP4_ADDRESS pIp4, PIP6_ADDRESS pIp6, WORD wPort, WORD wPriority, WORD wWeight, DWORD dwPropertiesCount, PCWSTR *keys, PCWSTR *values);
PDNS_SERVICE_INSTANCE WINAPI DnsServiceCopyInstance(PDNS_SERVICE_INSTANCE pOrig);
DWORD WINAPI DnsServiceDeRegister(PDNS_SERVICE_REGISTER_REQUEST pRequest, PDNS_SERVICE_CANCEL pCancel);
void DnsServiceFreeInstance(PDNS_SERVICE_INSTANCE pInstance);
DWORD WINAPI DnsServiceRegister(PDNS_SERVICE_REGISTER_REQUEST pRequest, PDNS_SERVICE_CANCEL pCancel);
DWORD WINAPI DnsServiceRegisterCancel(PDNS_SERVICE_CANCEL pCancelHandle);
DNS_STATUS WINAPI DnsServiceResolve(PDNS_SERVICE_RESOLVE_REQUEST pRequest, PDNS_SERVICE_CANCEL pCancel);
DNS_STATUS WINAPI DnsServiceResolveCancel(PDNS_SERVICE_CANCEL pCancelHandle);
#endif
DNS_STATUS WINAPI DnsStartMulticastQuery(PMDNS_QUERY_REQUEST pQueryRequest, PMDNS_QUERY_HANDLE pHandle);
DNS_STATUS WINAPI DnsStopMulticastQuery(PMDNS_QUERY_HANDLE pHandle);

#ifdef __cplusplus
}
#endif


struct _DNS_CONNECTION_PROXY_INFO
{
    DWORD Version;
    WCHAR *pwszFriendlyName;
    DWORD Flags;
    DNS_CONNECTION_PROXY_INFO_SWITCH Switch;
    union
    {
        struct _DNS_CONNECTION_PROXY_INFO_CONFIG
        {
            WCHAR *pwszServer;
            WCHAR *pwszUsername;
            WCHAR *pwszPassword;
            WCHAR *pwszException;
            WCHAR *pwszExtraInfo;
            WORD Port;
        } Config;
        struct _DNS_CONNECTION_PROXY_INFO_SCRIPT
        {
            WCHAR *pwszScript;
            WCHAR *pwszUsername;
            WCHAR *pwszPassword;
        } Script;
    };
};

typedef struct _DNS_CONNECTION_PROXY_ELEMENT DNS_CONNECTION_PROXY_ELEMENT;

struct _DNS_CONNECTION_PROXY_LIST
{
    DWORD cProxies;
    DNS_CONNECTION_PROXY_ELEMENT *pProxies;
};

typedef struct _DNS_CONNECTION_PROXY_INFO DNS_CONNECTION_PROXY_INFO;

struct _DNS_CONNECTION_PROXY_ELEMENT
{
    DNS_CONNECTION_PROXY_TYPE Type;
    DNS_CONNECTION_PROXY_INFO Info;
};

struct _DNS_CONNECTION_PROXY_INFO_EX
{
    DNS_CONNECTION_PROXY_INFO ProxyInfo;
    DWORD dwInterfaceIndex;
    WCHAR *pwszConnectionName;
    BOOL fDirectConfiguration;
    HANDLE hConnection;
};

typedef struct _DNS_CONNECTION_PROXY_INFO_EX DNS_CONNECTION_PROXY_INFO_EX;

typedef struct _DNS_CONNECTION_PROXY_LIST DNS_CONNECTION_PROXY_LIST;

typedef struct _DNS_CONNECTION_PROXY_INFO *PDNS_CONNECTION_PROXY_INFO;

typedef struct _DNS_CONNECTION_PROXY_INFO_EX *PDNS_CONNECTION_PROXY_INFO_EX;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void DnsConnectionFreeProxyInfo(DNS_CONNECTION_PROXY_INFO *pProxyInfo);
void DnsConnectionFreeProxyInfoEx(DNS_CONNECTION_PROXY_INFO_EX *pProxyInfoEx);
void DnsConnectionFreeProxyList(DNS_CONNECTION_PROXY_LIST *pProxyList);
DWORD DnsConnectionGetProxyInfo(PCWSTR pwszConnectionName, DNS_CONNECTION_PROXY_TYPE Type, DNS_CONNECTION_PROXY_INFO *pProxyInfo);
DWORD DnsConnectionGetProxyInfoForHostUrl(PCWSTR pwszHostUrl, BYTE *pSelectionContext, DWORD dwSelectionContextLength, DWORD dwExplicitInterfaceIndex, DNS_CONNECTION_PROXY_INFO_EX *pProxyInfoEx);
DWORD DnsConnectionGetProxyInfoForHostUrlEx(PCWSTR pwszHostUrl, BYTE *pSelectionContext, DWORD dwSelectionContextLength, DWORD dwExplicitInterfaceIndex, PCWSTR pwszConnectionName, DNS_CONNECTION_PROXY_INFO_EX *pProxyInfoEx);
DWORD DnsConnectionGetProxyList(PCWSTR pwszConnectionName, DNS_CONNECTION_PROXY_LIST *pProxyList);
DWORD DnsConnectionSetProxyInfo(PCWSTR pwszConnectionName, DNS_CONNECTION_PROXY_TYPE Type, const DNS_CONNECTION_PROXY_INFO *pProxyInfo);

#ifdef __cplusplus
}
#endif






struct _DNS_CUSTOM_SERVER
{
    DWORD dwServerType;
    ULONG64 ullFlags;
    union
    {
        PWSTR pwszTemplate;
        PWSTR pwszHostname;
    };
    union
    {
        CHAR MaxSa[32];
    };
};

typedef struct _DNS_CUSTOM_SERVER DNS_CUSTOM_SERVER;

struct _DNS_QUERY_RAW_REQUEST
{
    ULONG version;
    ULONG resultsVersion;
    ULONG dnsQueryRawSize;
    BYTE *dnsQueryRaw;
    PWSTR dnsQueryName;
    USHORT dnsQueryType;
    ULONG64 queryOptions;
    ULONG interfaceIndex;
    DNS_QUERY_RAW_COMPLETION_ROUTINE queryCompletionCallback;
    void *queryContext;
    ULONG64 queryRawOptions;
    ULONG customServersSize;
    DNS_CUSTOM_SERVER *customServers;
    ULONG protocol;
    union
    {
        CHAR maxSa[32];
    };
};

#if (!defined ( USE_PRIVATE_DNS_ADDR ) || defined (MIDL_PASS))
typedef void WINAPI DNS_QUERY_COMPLETION_ROUTINE(PVOID, PDNS_QUERY_RESULT);

typedef DNS_QUERY_COMPLETION_ROUTINE *PDNS_QUERY_COMPLETION_ROUTINE;

struct _DNS_QUERY_REQUEST3
{
    ULONG Version;
    PCWSTR QueryName;
    WORD QueryType;
    ULONG64 QueryOptions;
    PDNS_ADDR_ARRAY pDnsServerList;
    ULONG InterfaceIndex;
    PDNS_QUERY_COMPLETION_ROUTINE pQueryCompletionCallback;
    PVOID pQueryContext;
    BOOL IsNetworkQueryRequired;
    DWORD RequiredNetworkIndex;
    DWORD cCustomServers;
    DNS_CUSTOM_SERVER *pCustomServers;
};
#endif

typedef struct _DNS_QUERY_RAW_REQUEST DNS_QUERY_RAW_REQUEST;

#if (!defined ( USE_PRIVATE_DNS_ADDR ) || defined (MIDL_PASS))
typedef struct _DNS_QUERY_REQUEST3 DNS_QUERY_REQUEST3;

typedef struct _DNS_QUERY_REQUEST3 *PDNS_QUERY_REQUEST3;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void DnsFreeCustomServers(DWORD *pcServers, DNS_CUSTOM_SERVER **ppServers);
DWORD DnsGetApplicationSettings(DWORD *pcServers, DNS_CUSTOM_SERVER **ppDefaultServers, DNS_APPLICATION_SETTINGS *pSettings);
DNS_STATUS WINAPI DnsQueryRaw(DNS_QUERY_RAW_REQUEST *queryRequest, DNS_QUERY_RAW_CANCEL *cancelHandle);
DWORD DnsSetApplicationSettings(DWORD cServers, const DNS_CUSTOM_SERVER *pServers, const DNS_APPLICATION_SETTINGS *pSettings);

#ifdef __cplusplus
}
#endif

#define DNS_QUESTION_NAME_FROM_HEADER(_pHeader_) ((PCHAR) ((PDNS_HEADER) (_pHeader_) + 1))
#if (!(defined(UNICODE)))
#define DnsRecordCopy(pRR) DnsRecordCopyEx((pRR), DnsCharSetAnsi, DnsCharSetAnsi)
#define DnsRecordSetCopy(pRR) DnsRecordSetCopyEx((pRR), DnsCharSetAnsi, DnsCharSetAnsi)
#endif
#define INLINE_DWORD_FLIP(out, in) { DWORD _in = (in); (out) = ((_in << 8) & 0x00ff0000) | (_in << 24) | ((_in >> 8) & 0x0000ff00) | (_in >> 24); }
#define INLINE_HTONL(out, in) INLINE_DWORD_FLIP(out, in)
#define INLINE_NTOHL(out, in) INLINE_DWORD_FLIP(out, in)
#define INLINE_WRITE_FLIPPED_DWORD(pout, in) INLINE_DWORD_FLIP(* ((UNALIGNED DWORD *) (pout)), in)
#define INLINE_WRITE_FLIPPED_WORD(pout, in) INLINE_WORD_FLIP(* ((UNALIGNED WORD *) (pout)), in)
#define IS_DWORD_ALIGNED(p) (!((UINT_PTR) (p) & (UINT_PTR) 3))
#define IS_QWORD_ALIGNED(p) (!((UINT_PTR) (p) & (UINT_PTR) 7))
#define IS_WORD_ALIGNED(p) (!((UINT_PTR) (p) & (UINT_PTR) 1))

#endif /* _WINDNS_INCLUDED_ */
