#ifndef __NETCON_H__
#define __NETCON_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NCME_DEFAULT                = 0
}NETCONMGR_ENUM_FLAGS;

typedef enum
{
    NCCF_NONE                   = 0,
    NCCF_ALL_USERS              = 0x1,
    NCCF_ALLOW_DUPLICATION      = 0x2,
    NCCF_ALLOW_REMOVAL          = 0x4,
    NCCF_ALLOW_RENAME           = 0x8,
    NCCF_SHOW_ICON              = 0x10,
    NCCF_INCOMING_ONLY          = 0x20,
    NCCF_OUTGOING_ONLY          = 0x40,
    NCCF_BRANDED                = 0x80,
    NCCF_SHARED                 = 0x100,
    NCCF_BRIDGED                = 0x200,
    NCCF_FIREWALLED             = 0x400,
    NCCF_DEFAULT                = 0x800,
    NCCF_HOMENET_CAPABLE        = 0x1000,
    NCCF_SHARED_PRIVATE         = 0x2000,
    NCCF_QUARANTINED            = 0x4000,
    NCCF_RESERVED               = 0x8000,
    NCCF_BLUETOOTH_MASK         = 0xf0000,
    NCCF_LAN_MASK               = 0xf00000,
    NCCF_HOSTED_NETWORK = 0x10000,
    NCCF_VIRTUAL_STATION = 0x20000,
    NCCF_WIFI_DIRECT = 0x40000,
}NETCON_CHARACTERISTIC_FLAGS;


typedef enum
{
    NCS_DISCONNECTED            = 0,
    NCS_CONNECTING              = NCS_DISCONNECTED + 1,
    NCS_CONNECTED               = NCS_CONNECTING + 1,
    NCS_DISCONNECTING           = NCS_CONNECTED + 1,
    NCS_HARDWARE_NOT_PRESENT    = NCS_DISCONNECTING + 1,
    NCS_HARDWARE_DISABLED       = NCS_HARDWARE_NOT_PRESENT + 1,
    NCS_HARDWARE_MALFUNCTION    = NCS_HARDWARE_DISABLED + 1,
    NCS_MEDIA_DISCONNECTED      = NCS_HARDWARE_MALFUNCTION + 1,
    NCS_AUTHENTICATING          = NCS_MEDIA_DISCONNECTED + 1,
    NCS_AUTHENTICATION_SUCCEEDED= NCS_AUTHENTICATING + 1,
    NCS_AUTHENTICATION_FAILED   = NCS_AUTHENTICATION_SUCCEEDED + 1,
    NCS_INVALID_ADDRESS         = NCS_AUTHENTICATION_FAILED + 1,
    NCS_CREDENTIALS_REQUIRED    = NCS_INVALID_ADDRESS + 1,
    NCS_ACTION_REQUIRED = 0xD,
    NCS_ACTION_REQUIRED_RETRY = 0xE,
    NCS_CONNECT_FAILED = 0xF,
}NETCON_STATUS;

typedef enum
{
    NCT_DIRECT_CONNECT          = 0,
    NCT_INBOUND                 = NCT_DIRECT_CONNECT + 1,
    NCT_INTERNET                = NCT_INBOUND + 1,
    NCT_LAN                     = NCT_INTERNET + 1,
    NCT_PHONE                   = NCT_LAN + 1,
    NCT_TUNNEL                  = NCT_PHONE + 1,
    NCT_BRIDGE                  = NCT_TUNNEL + 1
}NETCON_TYPE;


typedef enum
{
    NCM_NONE                    = 0,
    NCM_DIRECT                  = NCM_NONE + 1,
    NCM_ISDN                    = NCM_DIRECT + 1,
    NCM_LAN                     = NCM_ISDN + 1,
    NCM_PHONE                   = NCM_LAN + 1,
    NCM_TUNNEL                  = NCM_PHONE + 1,
    NCM_PPPOE                   = NCM_TUNNEL + 1,
    NCM_BRIDGE                  = NCM_PPPOE + 1,
    NCM_SHAREDACCESSHOST_LAN    = NCM_BRIDGE + 1,
    NCM_SHAREDACCESSHOST_RAS    = NCM_SHAREDACCESSHOST_LAN + 1
}NETCON_MEDIATYPE;


typedef struct tagNETCON_PROPERTIES
{
    GUID guidId;
    LPWSTR pszwName;
    LPWSTR pszwDeviceName;
    NETCON_STATUS Status;
    NETCON_MEDIATYPE MediaType;
    DWORD dwCharacter;
    CLSID clsidThisObject;
    CLSID clsidUiObject;
}NETCON_PROPERTIES;


#undef  INTERFACE
#define INTERFACE   INetConnection
DECLARE_INTERFACE_(INetConnection, IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD_(HRESULT,Connect) (THIS) PURE;
    STDMETHOD_(HRESULT,Disconnect) (THIS) PURE;
    STDMETHOD_(HRESULT,Delete) (THIS) PURE;
    STDMETHOD_(HRESULT,Duplicate)(THIS_ LPCWSTR pszwDuplicateName, INetConnection **ppCon) PURE;
    STDMETHOD_(HRESULT,GetProperties) (THIS_ NETCON_PROPERTIES **ppProps) PURE;
    STDMETHOD_(HRESULT,GetUiObjectClassId) (THIS_ CLSID *pclsid) PURE;
    STDMETHOD_(HRESULT,Rename) (THIS_ LPCWSTR pszwNewName) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
/*** IUnknown methods ***/
#define INetConnection_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define INetConnection_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define INetConnection_Release(p)                 (p)->lpVtbl->Release(p)
#define INetConnection_Connect(p)                 (p)->lpVtbl->Connect(p)
#define INetConnection_Disconnect(p)              (p)->lpVtbl->Disconnect(p)
#define INetConnection_Delete(p)                  (p)->lpVtbl->Delete(p)
#define INetConnection_Duplicate(p,a,b)           (p)->lpVtbl->Duplicate(p,a,b)
#define INetConnection_GetProperties(p,a)         (p)->lpVtbl->GetProperties(p,a)
#define INetConnection_GetUiObjectClassId(p,a)    (p)->lpVtbl->GetUiObjectClassId(p,a)
#define INetConnection_Rename(p,a)                (p)->lpVtbl->Rename(p,a)
#endif

EXTERN_C const IID IID_INetConnection;


#undef  INTERFACE
#define INTERFACE   IEnumNetConnection
DECLARE_INTERFACE_(IEnumNetConnection, IUnknown)
{
    STDMETHOD(QueryInterface) (THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef) (THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD(Next)(THIS_ ULONG celt, INetConnection **rgelt, ULONG *pceltFetched) PURE;
    STDMETHOD(Skip) (THIS_ ULONG celt) PURE;
    STDMETHOD(Reset) (THIS) PURE;
    STDMETHOD(Clone) (THIS_ IEnumNetConnection **ppenum) PURE;
};
#undef INTERFACE

EXTERN_C const IID IID_IEnumNetConnection;

#if !defined(__cplusplus) || defined(CINTERFACE)
#define IEnumNetConnection_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define IEnumNetConnection_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define IEnumNetConnection_Release(p)                 (p)->lpVtbl->Release(p)
#define IEnumNetConnection_Next(p,a,b,c)              (p)->lpVtbl->Next(p,a,b,c)
#define IEnumNetConnection_Skip(p,a)                  (p)->lpVtbl->Skip(p,a)
#define IEnumNetConnection_Reset(p)                   (p)->lpVtbl->Reset(p)
#define IEnumNetConnection_Clone(p,a)                 (p)->lpVtbl->Clone(p,a)
#endif

#undef  INTERFACE
#define INTERFACE INetConnectionManager
DECLARE_INTERFACE_(INetConnectionManager, IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD_(HRESULT,EnumConnections) (THIS_ NETCONMGR_ENUM_FLAGS Flags, IEnumNetConnection **ppEnum) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
#define INetConnectionManager_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define INetConnectionManager_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define INetConnectionManager_Release(p)                 (p)->lpVtbl->Release(p)
#define INetConnectionManager_EnumConnections(p,a,b)     (p)->lpVtbl->EnumConnections(p,a,b)
#endif

EXTERN_C const CLSID CLSID_ConnectionManager;
EXTERN_C const IID IID_INetConnectionManager;

typedef enum
{
    NCUC_DEFAULT        = 0,
    NCUC_NO_UI          = 1,
    NCUC_ENABLE_DISABLE = 2
}NETCONUI_CONNECT_FLAGS;

#undef  INTERFACE
#define INTERFACE INetConnectionConnectUi
DECLARE_INTERFACE_(INetConnectionConnectUi, IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD_(HRESULT,SetConnection)(THIS_ INetConnection* pCon) PURE;
    STDMETHOD_(HRESULT,Connect) (THIS_ HWND hwndParent, DWORD dwFlags) PURE;
    STDMETHOD_(HRESULT,Disconnect) (THIS_ HWND hwndParent, DWORD dwFlags) PURE;
};

#if !defined(__cplusplus) || defined(CINTERFACE)
#define INetConnectionConnectUi_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define INetConnectionConnectUi_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define INetConnectionConnectUi_Release(p)                 (p)->lpVtbl->Release(p)
#define INetConnectionConnectUi_SetConnection(p,a)         (p)->lpVtbl->SetConnection(p,a)
#define INetConnectionConnectUi_Connect(p,a,b)             (p)->lpVtbl->Connect(p,a,b)
#define INetConnectionConnectUi_Disconnect(p,a,b)          (p)->lpVtbl->Disconnect(p,a,b)
#endif

EXTERN_C const IID IID_INetConnectionConnectUi;

#undef  INTERFACE
#define INTERFACE INetConnectionPropertyUi
DECLARE_INTERFACE_(INetConnectionPropertyUi, IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD_(HRESULT, SetConnection) (THIS_ INetConnection *pCon) PURE;
    STDMETHOD_(HRESULT, AddPages) (THIS_ HWND hwndParent, LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) PURE;


};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
#define INetConnectionPropertyUi_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define INetConnectionPropertyUi_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define INetConnectionPropertyUi_Release(p)                 (p)->lpVtbl->Release(p)
#define INetConnectionPropertyUi_SetConnection(p,a)         (p)->lpVtbl->SetConnection(p,a)
#define INetConnectionPropertyUi_AddPages(p,a,b,c)          (p)->lpVtbl->AddPages(p,a,b,c)
#endif

EXTERN_C const IID IID_INetConnectionPropertyUi;

#undef  INTERFACE
#define INTERFACE INetConnectionPropertyUi2
DECLARE_INTERFACE_(INetConnectionPropertyUi2, IUnknown)
{
    STDMETHOD_(HRESULT,QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG,AddRef)(THIS)  PURE;
    STDMETHOD_(ULONG,Release) (THIS) PURE;
    STDMETHOD_(HRESULT, SetConnection) (THIS_ INetConnection *pCon) PURE;
    STDMETHOD_(HRESULT, AddPages) (THIS_ HWND hwndParent, LPFNADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) PURE;
    STDMETHOD_(HRESULT, GetIcon) (THIS_ DWORD dwSize, HICON *phIcon) PURE;
};
#undef INTERFACE

#if !defined(__cplusplus) || defined(CINTERFACE)
#define INetConnectionPropertyUi2_QueryInterface(p,a,b)      (p)->lpVtbl->QueryInterface(p,a,b)
#define INetConnectionPropertyUi2_AddRef(p)                  (p)->lpVtbl->AddRef(p)
#define INetConnectionPropertyUi2_Release(p)                 (p)->lpVtbl->Release(p)
#define INetConnectionPropertyUi2_SetConnection(p,a)         (p)->lpVtbl->SetConnection(p,a)
#define INetConnectionPropertyUi2_AddPages(p,a,b,c)          (p)->lpVtbl->AddPages(p,a,b,c)
#define INetConnectionPropertyUi2_GetIcon(p,a,b)             (p)->lpVtbl->GetIcon(p,a,b)
#endif


EXTERN_C const IID IID_INetConnectionPropertyUi2;


VOID WINAPI NcFreeNetconProperties (NETCON_PROPERTIES* pProps);

#ifdef __cplusplus
}
#endif

#ifndef NETCON_MAX_NAME_LEN
#define NETCON_MAX_NAME_LEN 256
#endif

#define S_OBJECT_NO_LONGER_VALID ((HRESULT)2)

typedef struct IEnumNetSharingEveryConnection IEnumNetSharingEveryConnection;

struct IEnumNetSharingEveryConnectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(IEnumNetSharingEveryConnection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(IEnumNetSharingEveryConnection *);
    ULONG (WINAPI *Release)(IEnumNetSharingEveryConnection *);
    HRESULT (WINAPI *Next)(IEnumNetSharingEveryConnection *, ULONG, VARIANT *, ULONG *);
    HRESULT (WINAPI *Skip)(IEnumNetSharingEveryConnection *, ULONG);
    HRESULT (WINAPI *Reset)(IEnumNetSharingEveryConnection *);
    HRESULT (WINAPI *Clone)(IEnumNetSharingEveryConnection *, IEnumNetSharingEveryConnection **);
};

typedef struct IEnumNetSharingPortMapping IEnumNetSharingPortMapping;

struct IEnumNetSharingPortMappingVtbl
{
    HRESULT (WINAPI *QueryInterface)(IEnumNetSharingPortMapping *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(IEnumNetSharingPortMapping *);
    ULONG (WINAPI *Release)(IEnumNetSharingPortMapping *);
    HRESULT (WINAPI *Next)(IEnumNetSharingPortMapping *, ULONG, VARIANT *, ULONG *);
    HRESULT (WINAPI *Skip)(IEnumNetSharingPortMapping *, ULONG);
    HRESULT (WINAPI *Reset)(IEnumNetSharingPortMapping *);
    HRESULT (WINAPI *Clone)(IEnumNetSharingPortMapping *, IEnumNetSharingPortMapping **);
};

typedef struct IEnumNetSharingPrivateConnection IEnumNetSharingPrivateConnection;

struct IEnumNetSharingPrivateConnectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(IEnumNetSharingPrivateConnection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(IEnumNetSharingPrivateConnection *);
    ULONG (WINAPI *Release)(IEnumNetSharingPrivateConnection *);
    HRESULT (WINAPI *Next)(IEnumNetSharingPrivateConnection *, ULONG, VARIANT *, ULONG *);
    HRESULT (WINAPI *Skip)(IEnumNetSharingPrivateConnection *, ULONG);
    HRESULT (WINAPI *Reset)(IEnumNetSharingPrivateConnection *);
    HRESULT (WINAPI *Clone)(IEnumNetSharingPrivateConnection *, IEnumNetSharingPrivateConnection **);
};

typedef struct IEnumNetSharingPublicConnection IEnumNetSharingPublicConnection;

struct IEnumNetSharingPublicConnectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(IEnumNetSharingPublicConnection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(IEnumNetSharingPublicConnection *);
    ULONG (WINAPI *Release)(IEnumNetSharingPublicConnection *);
    HRESULT (WINAPI *Next)(IEnumNetSharingPublicConnection *, ULONG, VARIANT *, ULONG *);
    HRESULT (WINAPI *Skip)(IEnumNetSharingPublicConnection *, ULONG);
    HRESULT (WINAPI *Reset)(IEnumNetSharingPublicConnection *);
    HRESULT (WINAPI *Clone)(IEnumNetSharingPublicConnection *, IEnumNetSharingPublicConnection **);
};

typedef struct INetConnectionProps INetConnectionProps;

struct INetConnectionPropsVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetConnectionProps *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetConnectionProps *);
    ULONG (WINAPI *Release)(INetConnectionProps *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetConnectionProps *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetConnectionProps *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetConnectionProps *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetConnectionProps *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get_Guid)(INetConnectionProps *, BSTR *);
    HRESULT (WINAPI *get_Name)(INetConnectionProps *, BSTR *);
    HRESULT (WINAPI *get_DeviceName)(INetConnectionProps *, BSTR *);
    HRESULT (WINAPI *get_Status)(INetConnectionProps *, NETCON_STATUS *);
    HRESULT (WINAPI *get_MediaType)(INetConnectionProps *, NETCON_MEDIATYPE *);
    HRESULT (WINAPI *get_Characteristics)(INetConnectionProps *, DWORD *);
};

typedef struct INetSharingPortMapping INetSharingPortMapping;

typedef struct INetSharingPortMappingCollection INetSharingPortMappingCollection;

typedef struct INetSharingConfiguration INetSharingConfiguration;

typedef enum tagSHARINGCONNECTIONTYPE
{
    ICSSHARINGTYPE_PUBLIC = 0,
    ICSSHARINGTYPE_PRIVATE = 1
} SHARINGCONNECTIONTYPE;

typedef enum tagSHARINGCONNECTIONTYPE *LPSHARINGCONNECTIONTYPE;

typedef enum tagSHARINGCONNECTION_ENUM_FLAGS
{
    ICSSC_DEFAULT = 0,
    ICSSC_ENABLED = 1
} SHARINGCONNECTION_ENUM_FLAGS;

typedef enum tagICS_TARGETTYPE
{
    ICSTT_NAME = 0,
    ICSTT_IPADDRESS = 1
} ICS_TARGETTYPE;

struct INetSharingConfigurationVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingConfiguration *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingConfiguration *);
    ULONG (WINAPI *Release)(INetSharingConfiguration *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingConfiguration *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingConfiguration *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingConfiguration *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingConfiguration *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get_SharingEnabled)(INetSharingConfiguration *, VARIANT_BOOL *);
    HRESULT (WINAPI *get_SharingConnectionType)(INetSharingConfiguration *, SHARINGCONNECTIONTYPE *);
    HRESULT (WINAPI *DisableSharing)(INetSharingConfiguration *);
    HRESULT (WINAPI *EnableSharing)(INetSharingConfiguration *, SHARINGCONNECTIONTYPE);
    HRESULT (WINAPI *get_InternetFirewallEnabled)(INetSharingConfiguration *, VARIANT_BOOL *);
    HRESULT (WINAPI *DisableInternetFirewall)(INetSharingConfiguration *);
    HRESULT (WINAPI *EnableInternetFirewall)(INetSharingConfiguration *);
    HRESULT (WINAPI *get_EnumPortMappings)(INetSharingConfiguration *, SHARINGCONNECTION_ENUM_FLAGS, INetSharingPortMappingCollection **);
    HRESULT (WINAPI *AddPortMapping)(INetSharingConfiguration *, BSTR, UCHAR, USHORT, USHORT, DWORD, BSTR, ICS_TARGETTYPE, INetSharingPortMapping **);
    HRESULT (WINAPI *RemovePortMapping)(INetSharingConfiguration *, INetSharingPortMapping *);
};

typedef struct INetSharingEveryConnectionCollection INetSharingEveryConnectionCollection;

struct INetSharingEveryConnectionCollectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingEveryConnectionCollection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingEveryConnectionCollection *);
    ULONG (WINAPI *Release)(INetSharingEveryConnectionCollection *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingEveryConnectionCollection *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingEveryConnectionCollection *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingEveryConnectionCollection *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingEveryConnectionCollection *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get__NewEnum)(INetSharingEveryConnectionCollection *, IUnknown **);
    HRESULT (WINAPI *get_Count)(INetSharingEveryConnectionCollection *, long *);
};

typedef struct INetSharingPublicConnectionCollection INetSharingPublicConnectionCollection;

typedef struct INetSharingPrivateConnectionCollection INetSharingPrivateConnectionCollection;

typedef struct INetSharingManager INetSharingManager;

struct INetSharingManagerVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingManager *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingManager *);
    ULONG (WINAPI *Release)(INetSharingManager *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingManager *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingManager *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingManager *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingManager *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get_SharingInstalled)(INetSharingManager *, VARIANT_BOOL *);
    HRESULT (WINAPI *get_EnumPublicConnections)(INetSharingManager *, SHARINGCONNECTION_ENUM_FLAGS, INetSharingPublicConnectionCollection **);
    HRESULT (WINAPI *get_EnumPrivateConnections)(INetSharingManager *, SHARINGCONNECTION_ENUM_FLAGS, INetSharingPrivateConnectionCollection **);
    HRESULT (WINAPI *get_INetSharingConfigurationForINetConnection)(INetSharingManager *, INetConnection *, INetSharingConfiguration **);
    HRESULT (WINAPI *get_EnumEveryConnection)(INetSharingManager *, INetSharingEveryConnectionCollection **);
    HRESULT (WINAPI *get_NetConnectionProps)(INetSharingManager *, INetConnection *, INetConnectionProps **);
};

struct INetSharingPortMappingCollectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingPortMappingCollection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingPortMappingCollection *);
    ULONG (WINAPI *Release)(INetSharingPortMappingCollection *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingPortMappingCollection *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingPortMappingCollection *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingPortMappingCollection *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingPortMappingCollection *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get__NewEnum)(INetSharingPortMappingCollection *, IUnknown **);
    HRESULT (WINAPI *get_Count)(INetSharingPortMappingCollection *, long *);
};

typedef struct INetSharingPortMappingProps INetSharingPortMappingProps;

struct INetSharingPortMappingPropsVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingPortMappingProps *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingPortMappingProps *);
    ULONG (WINAPI *Release)(INetSharingPortMappingProps *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingPortMappingProps *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingPortMappingProps *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingPortMappingProps *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingPortMappingProps *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get_Name)(INetSharingPortMappingProps *, BSTR *);
    HRESULT (WINAPI *get_IPProtocol)(INetSharingPortMappingProps *, UCHAR *);
    HRESULT (WINAPI *get_ExternalPort)(INetSharingPortMappingProps *, long *);
    HRESULT (WINAPI *get_InternalPort)(INetSharingPortMappingProps *, long *);
    HRESULT (WINAPI *get_Options)(INetSharingPortMappingProps *, long *);
    HRESULT (WINAPI *get_TargetName)(INetSharingPortMappingProps *, BSTR *);
    HRESULT (WINAPI *get_TargetIPAddress)(INetSharingPortMappingProps *, BSTR *);
    HRESULT (WINAPI *get_Enabled)(INetSharingPortMappingProps *, VARIANT_BOOL *);
};

struct INetSharingPortMappingVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingPortMapping *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingPortMapping *);
    ULONG (WINAPI *Release)(INetSharingPortMapping *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingPortMapping *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingPortMapping *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingPortMapping *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingPortMapping *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *Disable)(INetSharingPortMapping *);
    HRESULT (WINAPI *Enable)(INetSharingPortMapping *);
    HRESULT (WINAPI *get_Properties)(INetSharingPortMapping *, INetSharingPortMappingProps **);
    HRESULT (WINAPI *Delete)(INetSharingPortMapping *);
};

struct INetSharingPrivateConnectionCollectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingPrivateConnectionCollection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingPrivateConnectionCollection *);
    ULONG (WINAPI *Release)(INetSharingPrivateConnectionCollection *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingPrivateConnectionCollection *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingPrivateConnectionCollection *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingPrivateConnectionCollection *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingPrivateConnectionCollection *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get__NewEnum)(INetSharingPrivateConnectionCollection *, IUnknown **);
    HRESULT (WINAPI *get_Count)(INetSharingPrivateConnectionCollection *, long *);
};

struct INetSharingPublicConnectionCollectionVtbl
{
    HRESULT (WINAPI *QueryInterface)(INetSharingPublicConnectionCollection *, const IID *const , void **);
    ULONG (WINAPI *AddRef)(INetSharingPublicConnectionCollection *);
    ULONG (WINAPI *Release)(INetSharingPublicConnectionCollection *);
    HRESULT (WINAPI *GetTypeInfoCount)(INetSharingPublicConnectionCollection *, UINT *);
    HRESULT (WINAPI *GetTypeInfo)(INetSharingPublicConnectionCollection *, UINT, LCID, ITypeInfo **);
    HRESULT (WINAPI *GetIDsOfNames)(INetSharingPublicConnectionCollection *, const IID *const , LPOLESTR *, UINT, LCID, DISPID *);
    HRESULT (WINAPI *Invoke)(INetSharingPublicConnectionCollection *, DISPID, const IID *const , LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);
    HRESULT (WINAPI *get__NewEnum)(INetSharingPublicConnectionCollection *, IUnknown **);
    HRESULT (WINAPI *get_Count)(INetSharingPublicConnectionCollection *, long *);
};

typedef struct NetSharingManager NetSharingManager;

#endif
