# Server service client forwarders to netapi32

@ stdcall I_NetServerSetServiceBits(wstr wstr long long) netapi32.I_NetServerSetServiceBits
@ stdcall NetConnectionEnum(wstr wstr long ptr long ptr ptr ptr) netapi32.NetConnectionEnum
@ stdcall NetFileClose(wstr long) netapi32.NetFileClose
@ stdcall NetFileEnum(wstr wstr wstr long ptr long ptr ptr ptr) netapi32.NetFileEnum
@ stdcall NetFileGetInfo(wstr long long ptr) netapi32.NetFileGetInfo
@ stdcall NetRemoteTOD(wstr ptr) netapi32.NetRemoteTOD
@ stdcall NetServerDiskEnum(wstr long ptr long ptr ptr ptr) netapi32.NetServerDiskEnum
@ stdcall NetServerGetInfo(wstr long ptr) netapi32.NetServerGetInfo
@ stdcall NetServerSetInfo(wstr long ptr ptr) netapi32.NetServerSetInfo
@ stdcall NetServerTransportAdd(wstr long ptr) netapi32.NetServerTransportAdd
@ stdcall NetServerTransportAddEx(wstr long ptr) netapi32.NetServerTransportAddEx
@ stdcall NetServerTransportDel(wstr long ptr) netapi32.NetServerTransportDel
@ stdcall NetServerTransportEnum(wstr long ptr long ptr ptr ptr) netapi32.NetServerTransportEnum
@ stdcall NetSessionDel(wstr wstr wstr) netapi32.NetSessionDel
@ stdcall NetSessionEnum(wstr wstr wstr long ptr long ptr ptr ptr) netapi32.NetSessionEnum
@ stdcall NetSessionGetInfo(wstr wstr wstr long ptr) netapi32.NetSessionGetInfo
@ stdcall NetShareAdd(wstr long ptr ptr) netapi32.NetShareAdd
@ stdcall NetShareCheck(wstr wstr ptr) netapi32.NetShareCheck
@ stdcall NetShareDel(wstr wstr long) netapi32.NetShareDel
@ stdcall NetShareDelSticky(wstr wstr long) netapi32.NetShareDelSticky
@ stdcall NetShareEnum(wstr long ptr long ptr ptr ptr) netapi32.NetShareEnum
@ stdcall NetShareEnumSticky(wstr long ptr long ptr ptr ptr) netapi32.NetShareEnumSticky
@ stdcall NetShareGetInfo(wstr wstr long ptr) netapi32.NetShareGetInfo
@ stdcall NetShareSetInfo(wstr wstr long ptr ptr) netapi32.NetShareSetInfo
