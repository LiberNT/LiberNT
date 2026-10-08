# Net logon client forwarders to netapi32

@ stdcall DsAddressToSiteNamesA(str long ptr str) netapi32.DsAddressToSiteNamesA
@ stdcall DsAddressToSiteNamesExA(str long ptr str str) netapi32.DsAddressToSiteNamesExA
@ stdcall DsAddressToSiteNamesExW(wstr long ptr wstr wstr) netapi32.DsAddressToSiteNamesExW
@ stdcall DsAddressToSiteNamesW(wstr long ptr wstr) netapi32.DsAddressToSiteNamesW
@ stdcall DsDeregisterDnsHostRecordsA(str str ptr ptr str) netapi32.DsDeregisterDnsHostRecordsA
@ stdcall DsDeregisterDnsHostRecordsW(wstr wstr ptr ptr wstr) netapi32.DsDeregisterDnsHostRecordsW
@ stdcall DsGetDcNameA(str str ptr str long ptr) netapi32.DsGetDcNameA
@ stdcall DsGetDcNameW(wstr wstr ptr wstr long ptr) netapi32.DsGetDcNameW
@ stdcall DsGetDcNameWithAccountA(str str long str ptr str long ptr) netapi32.DsGetDcNameWithAccountA
@ stdcall DsGetDcNameWithAccountW(wstr wstr long wstr ptr wstr long ptr) netapi32.DsGetDcNameWithAccountW
@ stdcall DsGetDcOpenA(str long str ptr str long ptr) netapi32.DsGetDcOpenA
@ stdcall DsGetDcOpenW(wstr long wstr ptr wstr long ptr) netapi32.DsGetDcOpenW
@ stdcall DsGetDcSiteCoverageA(str ptr str) netapi32.DsGetDcSiteCoverageA
@ stdcall DsGetDcSiteCoverageW(wstr ptr wstr) netapi32.DsGetDcSiteCoverageW
@ stdcall DsGetForestTrustInformationW(wstr wstr long ptr) netapi32.DsGetForestTrustInformationW
@ stdcall DsGetSiteNameA(str ptr) netapi32.DsGetSiteNameA
@ stdcall DsGetSiteNameW(wstr ptr) netapi32.DsGetSiteNameW
@ stdcall DsMergeForestTrustInformationW(wstr ptr ptr ptr) netapi32.DsMergeForestTrustInformationW
@ stdcall DsValidateSubnetNameA(str) netapi32.DsValidateSubnetNameA
@ stdcall DsValidateSubnetNameW(wstr) netapi32.DsValidateSubnetNameW
@ stdcall NetEnumerateTrustedDomains(wstr ptr) netapi32.NetEnumerateTrustedDomains
@ stdcall NetGetAnyDCName(wstr wstr ptr) netapi32.NetGetAnyDCName
@ stdcall NetGetDCName(wstr wstr ptr) netapi32.NetGetDCName
@ stdcall NetLogonGetTimeServiceParentDomain(wstr ptr ptr) netapi32.NetLogonGetTimeServiceParentDomain
@ stdcall NetLogonSetServiceBits(wstr long long) netapi32.NetLogonSetServiceBits
