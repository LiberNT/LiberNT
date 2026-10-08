# Workstation service client forwarders to netapi32

@ stdcall NetGetJoinInformation(wstr ptr ptr) netapi32.NetGetJoinInformation
@ stdcall NetGetJoinableOUs(wstr wstr wstr wstr ptr ptr) netapi32.NetGetJoinableOUs
@ stdcall NetWkstaGetInfo(wstr long ptr) netapi32.NetWkstaGetInfo
@ stdcall NetWkstaSetInfo(wstr long ptr ptr) netapi32.NetWkstaSetInfo
@ stdcall NetWkstaUserGetInfo(wstr long ptr) netapi32.NetWkstaUserGetInfo
@ stdcall NetWkstaUserSetInfo(wstr long ptr ptr) netapi32.NetWkstaUserSetInfo
@ stdcall NetWkstaUserEnum(wstr long ptr long ptr ptr ptr) netapi32.NetWkstaUserEnum
@ stdcall NetWkstaTransportEnum(wstr long ptr long ptr ptr ptr) netapi32.NetWkstaTransportEnum
@ stdcall NetWkstaTransportAdd(wstr long ptr ptr) netapi32.NetWkstaTransportAdd
@ stdcall NetWkstaTransportDel(wstr wstr long) netapi32.NetWkstaTransportDel
@ stdcall NetApiBufferFree(ptr) netapi32.NetApiBufferFree
@ stdcall NetAddAlternateComputerName(wstr wstr wstr wstr long) netapi32.NetAddAlternateComputerName
@ stdcall NetEnumerateComputerNames(wstr long long ptr ptr) netapi32.NetEnumerateComputerNames
@ stdcall NetJoinDomain(wstr wstr wstr wstr wstr long) netapi32.NetJoinDomain
@ stdcall NetRemoveAlternateComputerName(wstr wstr wstr wstr long) netapi32.NetRemoveAlternateComputerName
@ stdcall NetRenameMachineInDomain(wstr wstr wstr wstr long) netapi32.NetRenameMachineInDomain
@ stdcall NetSetPrimaryComputerName(wstr wstr wstr wstr long) netapi32.NetSetPrimaryComputerName
@ stdcall NetUnjoinDomain(wstr wstr wstr long) netapi32.NetUnjoinDomain
@ stdcall NetUseAdd(wstr long ptr ptr) netapi32.NetUseAdd
@ stdcall NetUseDel(wstr wstr long) netapi32.NetUseDel
@ stdcall NetUseEnum(wstr long ptr long ptr ptr ptr) netapi32.NetUseEnum
@ stdcall NetUseGetInfo(ptr ptr long ptr) netapi32.NetUseGetInfo
@ stdcall NetValidateName(wstr wstr wstr wstr long) netapi32.NetValidateName
@ stdcall -private DllInitialize(long long ptr) DllMain
