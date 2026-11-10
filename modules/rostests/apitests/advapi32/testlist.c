
#define STANDALONE
#include <apitest.h>

extern void func_AddMandatoryAce(void);
extern void func_ControlServiceEx(void);
extern void func_CopySid(void);
extern void func_CreateService(void);
extern void func_CryptCreateHash(void);
extern void func_DuplicateTokenEx(void);
extern void func_EnableTraceEx(void);
extern void func_EnumerateTraceGuidsEx(void);
extern void func_eventlog_supp(void);
extern void func_Hash(void);
extern void func_HKEY_CLASSES_ROOT(void);
extern void func_InitiateShutdown(void);
extern void func_ImpersonateRestricted(void);
extern void func_IntegrityLabels(void);
extern void func_LogonSession(void);
extern void func_LogonUserNetwork(void);
extern void func_IsTextUnicode(void);
extern void func_LockServiceDatabase(void);
extern void func_TokenDefaultDacl(void);
extern void func_LookupAccountName(void);
extern void func_LookupAccountSid(void);
extern void func_OpenTrace(void);
extern void func_QueryServiceConfig2(void);
extern void func_RegCreateKeyEx(void);
extern void func_RegCreateKeyTransacted(void);
extern void func_RegEnumKey(void);
extern void func_RegEnumValueW(void);
extern void func_RegFlushKey(void);
extern void func_RegOpenKeyExW(void);
extern void func_RegOpenKeyTransacted(void);
extern void func_RegQueryInfoKey(void);
extern void func_RegQueryValueExW(void);
extern void func_RtlEncryptMemory(void);
extern void func_SaferIdentifyLevel(void);
extern void func_ServiceArgs(void);
extern void func_ServiceCrash(void);
extern void func_ServiceEnv(void);
extern void func_TriggerInfo(void);
extern void func_ServiceNetwork(void);
extern void func_TokenBnoIsolation(void);
extern void func_TraceGuids(void);

const struct test winetest_testlist[] =
{
    { "AddMandatoryAce", func_AddMandatoryAce },
    { "ControlServiceEx", func_ControlServiceEx },
    { "CopySid", func_CopySid },
    { "CreateService", func_CreateService },
    { "CryptCreateHash", func_CryptCreateHash },
    { "DuplicateTokenEx", func_DuplicateTokenEx },
    { "EnableTraceEx", func_EnableTraceEx },
    { "EnumerateTraceGuidsEx", func_EnumerateTraceGuidsEx },
    { "eventlog_supp", func_eventlog_supp },
    { "Hash", func_Hash },
    { "HKEY_CLASSES_ROOT", func_HKEY_CLASSES_ROOT },
    { "InitiateShutdown", func_InitiateShutdown },
    { "ImpersonateRestricted", func_ImpersonateRestricted },
    { "IntegrityLabels", func_IntegrityLabels },
    { "LogonSession", func_LogonSession },
    { "LogonUserNetwork", func_LogonUserNetwork },
    { "IsTextUnicode" , func_IsTextUnicode },
    { "TokenDefaultDacl", func_TokenDefaultDacl },
    { "LockServiceDatabase" , func_LockServiceDatabase },
    { "LookupAccountName", func_LookupAccountName },
    { "LookupAccountSid", func_LookupAccountSid },
    { "OpenTrace", func_OpenTrace },
    { "QueryServiceConfig2", func_QueryServiceConfig2 },
    { "RegCreateKeyEx", func_RegCreateKeyEx },
    { "RegCreateKeyTransacted", func_RegCreateKeyTransacted },
    { "RegEnumKey", func_RegEnumKey },
    { "RegEnumValueW", func_RegEnumValueW },
    { "RegFlushKey", func_RegFlushKey },
    { "RegQueryInfoKey", func_RegQueryInfoKey },
    { "RegOpenKeyExW", func_RegOpenKeyExW },
    { "RegOpenKeyTransacted", func_RegOpenKeyTransacted },
    { "RegQueryValueExW", func_RegQueryValueExW },
    { "RtlEncryptMemory", func_RtlEncryptMemory },
    { "SaferIdentifyLevel", func_SaferIdentifyLevel },
    { "ServiceArgs", func_ServiceArgs },
    { "ServiceCrash", func_ServiceCrash },
    { "ServiceEnv", func_ServiceEnv },
    { "TriggerInfo", func_TriggerInfo },
    { "ServiceNetwork", func_ServiceNetwork },
    { "TokenBnoIsolation", func_TokenBnoIsolation },
    { "TraceGuids", func_TraceGuids },
    { 0, 0 }
};
