# Security accounts manager client forwarders to netapi32

@ stdcall NetGetDisplayInformationIndex(wstr long wstr ptr) netapi32.NetGetDisplayInformationIndex
@ stdcall NetGroupAdd(wstr long ptr ptr) netapi32.NetGroupAdd
@ stdcall NetGroupAddUser(wstr wstr wstr) netapi32.NetGroupAddUser
@ stdcall NetGroupDel(wstr wstr) netapi32.NetGroupDel
@ stdcall NetGroupDelUser(wstr wstr wstr) netapi32.NetGroupDelUser
@ stdcall NetGroupEnum(wstr long ptr long ptr ptr ptr) netapi32.NetGroupEnum
@ stdcall NetGroupGetInfo(wstr wstr long ptr) netapi32.NetGroupGetInfo
@ stdcall NetGroupGetUsers(wstr wstr long ptr long ptr ptr ptr) netapi32.NetGroupGetUsers
@ stdcall NetGroupSetInfo(wstr wstr long ptr ptr) netapi32.NetGroupSetInfo
@ stdcall NetGroupSetUsers(wstr wstr long ptr long) netapi32.NetGroupSetUsers
@ stdcall NetLocalGroupAdd(wstr long ptr ptr) netapi32.NetLocalGroupAdd
@ stdcall NetLocalGroupAddMember(wstr wstr ptr) netapi32.NetLocalGroupAddMember
@ stdcall NetLocalGroupAddMembers(wstr wstr long ptr long) netapi32.NetLocalGroupAddMembers
@ stdcall NetLocalGroupDel(wstr wstr) netapi32.NetLocalGroupDel
@ stdcall NetLocalGroupDelMember(wstr wstr ptr) netapi32.NetLocalGroupDelMember
@ stdcall NetLocalGroupDelMembers(wstr wstr long ptr long) netapi32.NetLocalGroupDelMembers
@ stdcall NetLocalGroupEnum(wstr long ptr long ptr ptr ptr) netapi32.NetLocalGroupEnum
@ stdcall NetLocalGroupGetInfo(wstr wstr long ptr) netapi32.NetLocalGroupGetInfo
@ stdcall NetLocalGroupGetMembers(wstr wstr long ptr long ptr ptr ptr) netapi32.NetLocalGroupGetMembers
@ stdcall NetLocalGroupSetInfo(wstr wstr long ptr ptr) netapi32.NetLocalGroupSetInfo
@ stdcall NetLocalGroupSetMembers(wstr wstr long ptr long) netapi32.NetLocalGroupSetMembers
@ stdcall NetQueryDisplayInformation(wstr long long long long ptr ptr) netapi32.NetQueryDisplayInformation
@ stdcall NetUserAdd(wstr long ptr ptr) netapi32.NetUserAdd
@ stdcall NetUserChangePassword(wstr wstr wstr wstr) netapi32.NetUserChangePassword
@ stdcall NetUserDel(wstr wstr) netapi32.NetUserDel
@ stdcall NetUserEnum(wstr long long ptr long ptr ptr ptr) netapi32.NetUserEnum
@ stdcall NetUserGetGroups(wstr wstr long ptr long ptr ptr) netapi32.NetUserGetGroups
@ stdcall NetUserGetInfo(wstr wstr long ptr) netapi32.NetUserGetInfo
@ stdcall NetUserGetLocalGroups(wstr wstr long long ptr long ptr ptr) netapi32.NetUserGetLocalGroups
@ stdcall NetUserModalsGet(wstr long ptr) netapi32.NetUserModalsGet
@ stdcall NetUserModalsSet(wstr long ptr ptr) netapi32.NetUserModalsSet
@ stdcall NetUserSetGroups(wstr wstr long ptr long) netapi32.NetUserSetGroups
@ stdcall NetUserSetInfo(wstr wstr long ptr ptr) netapi32.NetUserSetInfo
@ stdcall NetValidatePasswordPolicy(wstr ptr long ptr ptr) netapi32.NetValidatePasswordPolicy
@ stdcall NetValidatePasswordPolicyFree(ptr) netapi32.NetValidatePasswordPolicyFree
