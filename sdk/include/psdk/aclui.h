/*
 * aclui.h
 *
 * Access Control List Editor definitions
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef __ACLUI_H
#define __ACLUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <unknwn.h>
#include <accctrl.h>
#include <commctrl.h>

DEFINE_GUID(IID_ISecurityInformation, 0x965fc360, 0x16ff, 0x11d0, 0x0091, 0xcb,0x00,0xaa,0x00,0xbb,0xb7,0x23);
DEFINE_GUID(IID_ISecurityInformation2, 0xc3ccfdb4, 0x6f88, 0x11d2, 0x00a3, 0xce,0x00,0xc0,0x4f,0xb1,0x78,0x2a);
DEFINE_GUID(IID_IEffectivePermission, 0x3853dc76, 0x9f35, 0x407c, 0x0088, 0xa1,0xd1,0x93,0x44,0x36,0x5f,0xbc);
DEFINE_GUID(IID_ISecurityObjectTypeInfo, 0xfc3066eb, 0x79ef, 0x444b, 0x0091, 0x11,0xd1,0x8a,0x75,0xeb,0xf2,0xfa);

typedef interface ISecurityInformation *LPSECURITYINFO;
typedef interface ISecurityInformation2 *LPSECURITYINFO2;
typedef interface IEffectivePermission *LPEFFECTIVEPERMISSION;
typedef interface ISecurityObjectTypeInfo *LPSecurityObjectTypeInfo;

#undef INTERFACE
EXTERN_C const IID IID_ISecurityInformation;
#define INTERFACE ISecurityInformation
DECLARE_INTERFACE_(ISecurityInformation,IUnknown)
{
        /* IUnknown */
        STDMETHOD(QueryInterface)(THIS_ REFIID,PVOID*) PURE;
	STDMETHOD_(ULONG,AddRef)(THIS) PURE;
	STDMETHOD_(ULONG,Release)(THIS) PURE;

	/* ISecurityInformation */
	STDMETHOD(GetObjectInformation)(THIS_ PSI_OBJECT_INFO) PURE;
	STDMETHOD(GetSecurity)(THIS_ SECURITY_INFORMATION,PSECURITY_DESCRIPTOR*,BOOL) PURE;
	STDMETHOD(SetSecurity)(THIS_ SECURITY_INFORMATION,PSECURITY_DESCRIPTOR) PURE;
	STDMETHOD(GetAccessRights)(THIS_ GUID*,DWORD,PSI_ACCESS*,ULONG*,ULONG*) PURE;
	STDMETHOD(MapGeneric)(THIS_ GUID*,UCHAR*,PSI_ACCESS*) PURE;
	STDMETHOD(GetInheritTypes)(THIS_ PSI_INHERIT_TYPE*,ULONG*) PURE;
	STDMETHOD(PropertySheetPageCallback)(THIS_ HWND,UINT,SI_PAGE_TYPE) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE ISecurityInformation2
DECLARE_INTERFACE_(ISecurityInformation2,IUnknown)
{
        /* IUnknown */
        STDMETHOD(QueryInterface)(THIS_ REFIID,PVOID*) PURE;
	STDMETHOD_(ULONG,AddRef)(THIS) PURE;
	STDMETHOD_(ULONG,Release)(THIS) PURE;

	/* ISecurityInformation2 */
	STDMETHOD(IsDaclCanonical)(THIS_ PACL) PURE;
	STDMETHOD(LookupSids)(THIS_ ULONG,PSID*,LPDATAOBJECT*) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE IEffectivePermission
DECLARE_INTERFACE_(IEffectivePermission,IUnknown)
{
        /* IUnknown */
        STDMETHOD(QueryInterface)(THIS_ REFIID,PVOID*) PURE;
	STDMETHOD_(ULONG,AddRef)(THIS) PURE;
	STDMETHOD_(ULONG,Release)(THIS) PURE;

	/* IEffectivePermission */
	STDMETHOD(GetEffectivePermission)(THIS_ GUID*,PSID,LPCWSTR,PSECURITY_DESCRIPTOR,POBJECT_TYPE_LIST*,ULONG*,PACCESS_MASK*,ULONG*) PURE;
};
#undef INTERFACE

#undef INTERFACE
#define INTERFACE ISecurityObjectTypeInfo
DECLARE_INTERFACE_(ISecurityObjectTypeInfo,IUnknown)
{
        /* IUnknown */
        STDMETHOD(QueryInterface)(THIS_ REFIID,PVOID*) PURE;
	STDMETHOD_(ULONG,AddRef)(THIS) PURE;
	STDMETHOD_(ULONG,Release)(THIS) PURE;

	/* ISecurityObjectTypeInfo */
	STDMETHOD(GetInheritSource)(THIS_ SECURITY_INFORMATION,PACL,PINHERITED_FROM*) PURE;
};
#undef INTERFACE

HPROPSHEETPAGE WINAPI
CreateSecurityPage(LPSECURITYINFO psi);

BOOL WINAPI
EditSecurity(HWND hwndOwner, LPSECURITYINFO psi);

#ifdef __cplusplus
}
#endif
#ifndef DOBJ_COND_NTACLS
#define DOBJ_COND_NTACLS 0x00000008L
#endif
#ifndef DOBJ_RES_CONT
#define DOBJ_RES_CONT 0x00000001L
#endif
#ifndef DOBJ_RES_ROOT
#define DOBJ_RES_ROOT 0x00000002L
#endif
#ifndef DOBJ_RIBBON_LAUNCH
#define DOBJ_RIBBON_LAUNCH 0x00000010L
#endif
#ifndef DOBJ_VOL_NTACLS
#define DOBJ_VOL_NTACLS 0x00000004L
#endif
#ifndef SECURITY_OBJECT_ID_CENTRAL_ACCESS_RULE
#define SECURITY_OBJECT_ID_CENTRAL_ACCESS_RULE 4
#endif
#ifndef SECURITY_OBJECT_ID_CENTRAL_POLICY
#define SECURITY_OBJECT_ID_CENTRAL_POLICY 3
#endif
#ifndef SECURITY_OBJECT_ID_SHARE
#define SECURITY_OBJECT_ID_SHARE 2
#endif
#ifndef SI_AUDITS_ELEVATION_REQUIRED
#define SI_AUDITS_ELEVATION_REQUIRED 0x02000000L
#endif
#ifndef SI_DISABLE_DENY_ACE
#define SI_DISABLE_DENY_ACE 0x80000000L
#endif
#ifndef SI_ENABLE_CENTRAL_POLICY
#define SI_ENABLE_CENTRAL_POLICY 0x40000000L
#endif
#ifndef SI_ENABLE_EDIT_ATTRIBUTE_CONDITION
#define SI_ENABLE_EDIT_ATTRIBUTE_CONDITION 0x20000000L
#endif
#ifndef SI_OWNER_ELEVATION_REQUIRED
#define SI_OWNER_ELEVATION_REQUIRED 0x04000000L
#endif
#ifndef SI_PERMS_ELEVATION_REQUIRED
#define SI_PERMS_ELEVATION_REQUIRED 0x01000000L
#endif
#ifndef SI_SCOPE_ELEVATION_REQUIRED
#define SI_SCOPE_ELEVATION_REQUIRED 0x08000000L
#endif
#ifndef SI_VIEW_ONLY
#define SI_VIEW_ONLY 0x00400000L
#endif

typedef enum _SI_PAGE_ACTIVATED {
    SI_SHOW_DEFAULT = 0,
    SI_SHOW_PERM_ACTIVATED = 1,
    SI_SHOW_AUDIT_ACTIVATED = 2,
    SI_SHOW_OWNER_ACTIVATED = 3,
    SI_SHOW_EFFECTIVE_ACTIVATED = 4,
    SI_SHOW_SHARE_ACTIVATED = 5,
    SI_SHOW_CENTRAL_POLICY_ACTIVATED = 6
} SI_PAGE_ACTIVATED;

typedef struct _EFFPERM_RESULT_LIST {
  BOOLEAN fEvaluated;
  ULONG cObjectTypeListLength;
  OBJECT_TYPE_LIST * pObjectTypeList;
  ACCESS_MASK * pGrantedAccessList;
} EFFPERM_RESULT_LIST, *PEFFPERM_RESULT_LIST;

typedef struct _SECURITY_OBJECT {
  PWSTR pwszName;
  PVOID pData;
  DWORD cbData;
  PVOID pData2;
  DWORD cbData2;
  DWORD Id;
  BOOLEAN fWellKnown;
} SECURITY_OBJECT, *PSECURITY_OBJECT;

typedef struct _SID_INFO {
  PSID pSid;
  PWSTR pwzCommonName;
  PWSTR pwzClass;
  PWSTR pwzUPN;
} SID_INFO, *PSID_INFO;

typedef struct _SID_INFO_LIST {
  ULONG cItems;
  SID_INFO aSidInfo[1];
} SID_INFO_LIST, *PSID_INFO_LIST;

#define ACLUIAPI DECLSPEC_IMPORT WINAPI
#define CFSTR_ACLUI_SID_INFO_LIST TEXT("CFSTR_ACLUI_SID_INFO_LIST")
#define PSPCB_SI_INITDIALOG 0x401
#define SECURITY_OBJECT_ID_OBJECT_SD 1

typedef struct IEffectivePermission2Vtbl IEffectivePermission2Vtbl;

typedef struct IEffectivePermission2 IEffectivePermission2;

struct IEffectivePermission2
{
    struct IEffectivePermission2Vtbl *lpVtbl;
};

typedef struct IEffectivePermissionVtbl IEffectivePermissionVtbl;

typedef struct ISecurityInformation2Vtbl ISecurityInformation2Vtbl;

typedef struct ISecurityInformation3Vtbl ISecurityInformation3Vtbl;

typedef struct ISecurityInformation3 ISecurityInformation3;

struct ISecurityInformation3
{
    struct ISecurityInformation3Vtbl *lpVtbl;
};

typedef struct ISecurityInformation4Vtbl ISecurityInformation4Vtbl;

typedef struct ISecurityInformation4 ISecurityInformation4;

struct ISecurityInformation4
{
    struct ISecurityInformation4Vtbl *lpVtbl;
};

typedef struct ISecurityInformationVtbl ISecurityInformationVtbl;

typedef struct ISecurityObjectTypeInfoVtbl ISecurityObjectTypeInfoVtbl;

typedef IEffectivePermission2 *LPEFFECTIVEPERMISSION2;

typedef ISecurityInformation3 *LPSECURITYINFO3;

typedef ISecurityInformation4 *LPSECURITYINFO4;

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
HRESULT EditSecurityAdvanced(HWND hwndOwner, LPSECURITYINFO psi, SI_PAGE_TYPE uSIPage);

#ifdef __cplusplus
}
#endif

#define COMBINE_PAGE_ACTIVATION(X, Y) (UINT) (((Y) << 16) | X)
#define GET_ACTIVATION_TYPE(Y) (UINT) (((Y) >> 16) & 0x0000ffff)
#define GET_PAGE_TYPE(X) (UINT) ((X) & 0x0000ffff)

#endif /* __ACLUI_H */

/* EOF */
