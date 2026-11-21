#ifndef _PRSHT_H
#define _PRSHT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201)
#endif

#ifndef SNDMSG
#ifdef __cplusplus
#define SNDMSG ::SendMessage
#else
#define SNDMSG SendMessage
#endif
#endif /* ifndef SNDMSG */

#define MAXPROPPAGES	100
#define PSP_DEFAULT	0
#define PSP_DLGINDIRECT	1
#define PSP_USEHICON	2
#define PSP_USEICONID	4
#define PSP_USETITLE	8
#define PSP_RTLREADING	16
#define PSP_HASHELP	32
#define PSP_USEREFPARENT	64
#define PSP_USECALLBACK	128
#define PSP_PREMATURE	1024
#if (_WIN32_IE >= 0x0400)
#define PSP_HIDEHEADER	2048
#define PSP_USEHEADERTITLE	4096
#define PSP_USEHEADERSUBTITLE	8192
#endif
#define PSPCB_RELEASE	1
#define PSPCB_CREATE	2
#define PSH_DEFAULT	0
#define PSH_PROPTITLE	1
#define PSH_USEHICON	2
#define PSH_USEICONID	4
#define PSH_PROPSHEETPAGE	8
#define PSH_WIZARDHASFINISH	16
#define PSH_WIZARD	32
#define PSH_USEPSTARTPAGE	64
#define PSH_NOAPPLYNOW	128
#define PSH_USECALLBACK	256
#define PSH_HASHELP	512
#define PSH_MODELESS	1024
#define PSH_RTLREADING	2048
#define PSH_WIZARDCONTEXTHELP	4096
#if (_WIN32_IE >= 0x0400)
#define PSH_WATERMARK	32768
#define PSH_USEHBMWATERMARK	65536
#define PSH_USEHPLWATERMARK	131072
#define PSH_STRETCHWATERMARK	262144
#define PSH_HEADER	524288
#define PSH_USEHBMHEADER	1048576
#define PSH_USEPAGELANG	2097152
#if (_WIN32_IE < 0x0500)
#define PSH_WIZARD97	0x00002000
#else
#define PSH_WIZARD97	0x01000000
#endif
#ifdef _WINE
#define PSH_WIZARD97_OLD 0x00002000
#define PSH_WIZARD97_NEW 0x01000000
#endif
#endif /* _WIN32_IE >= 0x0400 */
#if (_WIN32_IE >= 0x0500)
#define PSH_WIZARD_LITE	0x400000
#define PSH_NOCONTEXTHELP	0x2000000
#endif
#define PSCB_INITIALIZED	1
#define PSCB_PRECREATE	2
#if (_WIN32_IE >= 0x560)
#define PSCB_BUTTONPRESSED	3
#endif /* _WIN32_IE >= 0x560 */
#define PSM_GETTABCONTROL	1140
#define PSM_GETCURRENTPAGEHWND	1142
#if (_WIN32_IE >= 0x0500)
#define PSM_GETRESULT	1159
#define PropSheet_GetResult(hDlg)	SNDMSG(hDlg, PSM_GETRESULT, 0, 0)

#define PSM_HWNDTOINDEX	1153
#define PropSheet_HwndToIndex(hDlg, hwnd) \
        (int)SNDMSG(hDlg, PSM_HWNDTOINDEX, (WPARAM)(hwnd), 0)

#define PSM_IDTOINDEX	1157
#define PSM_INDEXTOHWND	1154
#define PSM_INDEXTOID	1158
#define PSM_INDEXTOPAGE	1156
#define PSM_INSERTPAGE	1143
#endif /* _WIN32_IE >= 0x0500 */
#define PSM_ISDIALOGMESSAGE	1141
#if (_WIN32_IE >= 0x0500)
#define PSM_PAGETOINDEX	1155
#endif /* _WIN32_IE >= 0x0500 */
#define PSM_PRESSBUTTON	1137
#if (_WIN32_IE >= 0x0500)
#define PSM_RECALCPAGESIZES	1160
#endif /* _WIN32_IE >= 0x0500 */
#define PSM_SETCURSELID	1138
#define PSM_SETFINISHTEXTW	1145
#define PSM_SETFINISHTEXTA	1139
#if (_WIN32_IE >= 0x0500)
#define PSM_SETHEADERTITLEA	1149
#define PSM_SETHEADERTITLEW	1150
#define PSM_SETHEADERSUBTITLEA	1151
#define PSM_SETHEADERSUBTITLEW	1152
#endif

#define PSN_FIRST                (0U - 200U)
#define PSN_SETACTIVE            (PSN_FIRST - 0)
#define PSN_KILLACTIVE           (PSN_FIRST - 1)
#define PSN_APPLY                (PSN_FIRST - 2)
#define PSN_RESET                (PSN_FIRST - 3)
#define PSN_HELP                 (PSN_FIRST - 5)
#define PSN_WIZBACK              (PSN_FIRST - 6)
#define PSN_WIZNEXT              (PSN_FIRST - 7)
#define PSN_WIZFINISH            (PSN_FIRST - 8)
#define PSN_QUERYCANCEL          (PSN_FIRST - 9)
#define PSN_GETOBJECT            (PSN_FIRST - 10)
#define PSN_TRANSLATEACCELERATOR (PSN_FIRST - 12)
#define PSN_QUERYINITIALFOCUS    (PSN_FIRST - 13)
#define PSN_LAST                 (PSN_FIRST - 99)

#define PSNRET_NOERROR	0
#define PSNRET_INVALID	1
#define PSNRET_INVALID_NOCHANGEPAGE	2
#define ID_PSRESTARTWINDOWS	2
#define ID_PSREBOOTSYSTEM	3
#define WIZ_CXDLG	276
#define WIZ_CYDLG	140
#define WIZ_CXBMP	80
#define WIZ_BODYX	92
#define WIZ_BODYCX	184
#define PROP_SM_CXDLG	212
#define PROP_SM_CYDLG	188
#define PROP_MED_CXDLG	227
#define PROP_MED_CYDLG	215
#define PROP_LG_CXDLG	252
#define PROP_LG_CYDLG	218
#define PSBTN_MAX	6
#define PSBTN_BACK	0
#define PSBTN_NEXT	1
#define PSBTN_FINISH	2
#define PSBTN_OK	3
#define PSBTN_APPLYNOW	4
#define PSBTN_CANCEL	5
#define PSBTN_HELP	6
#define PSWIZB_BACK	1
#define PSWIZB_NEXT	2
#define PSWIZB_FINISH	4
#define PSWIZB_DISABLEDFINISH	8
#define PSM_SETWIZBUTTONS	(WM_USER+112)
#define PSM_APPLY	(WM_USER+110)
#define PSM_UNCHANGED	(WM_USER+109)
#define PSM_QUERYSIBLINGS	(WM_USER+108)
#define PSM_CANCELTOCLOSE	(WM_USER+107)
#define PSM_REBOOTSYSTEM	(WM_USER+106)
#define PSM_RESTARTWINDOWS	(WM_USER+105)
#define PSM_CHANGED	(WM_USER+104)
#define PSM_ADDPAGE	(WM_USER+103)
#define PSM_REMOVEPAGE	(WM_USER+102)
#define PSM_SETCURSEL	(WM_USER+101)
#define PSM_SETTITLEA	(WM_USER+111)
#define PSM_SETTITLEW	(WM_USER+120)

#define PSPCB_ADDREF 0

#ifndef RC_INVOKED

#pragma pack(push,8)
typedef struct _PROPSHEETPAGEA {
	DWORD	dwSize;
	DWORD	dwFlags;
	HINSTANCE	hInstance;
	_ANONYMOUS_UNION union {
		LPCSTR	pszTemplate;
		LPCDLGTEMPLATE	pResource;
	} DUMMYUNIONNAME;
	_ANONYMOUS_UNION union {
		HICON hIcon;
		LPCSTR pszIcon;
	} DUMMYUNIONNAME2;
	LPCSTR	pszTitle;
	DLGPROC	pfnDlgProc;
	LPARAM	lParam;
	UINT(CALLBACK *pfnCallback)(HWND,UINT,struct _PROPSHEETPAGEA*);
	UINT *pcRefParent;
#if (_WIN32_IE >= 0x0400)
	LPCSTR pszHeaderTitle;
	LPCSTR pszHeaderSubTitle;
#endif
#if (_WIN32_IE >= 0x0501)
    HANDLE hActCtx;
#endif
#if (_WIN32_IE >= 0x0600)
    _ANONYMOUS_UNION union {
        HBITMAP hbmHeader;
        LPCSTR pszbmHeader;
    } DUMMYUNIONNAME3;
#endif
} PROPSHEETPAGEA,*LPPROPSHEETPAGEA,
  PROPSHEETPAGEA_LATEST, *LPPROPSHEETPAGEA_LATEST;
typedef const PROPSHEETPAGEA *LPCPROPSHEETPAGEA, *LPCPROPSHEETPAGEA_LATEST;
#define PROPSHEETPAGEA_V1_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEA, pcRefParent)
#define PROPSHEETPAGEA_V2_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEA, pszHeaderSubTitle)
#define PROPSHEETPAGEA_V3_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEA, hActCtx)
#define PROPSHEETPAGEA_V4_SIZE sizeof(PROPSHEETPAGEA)
typedef struct _PROPSHEETPAGEW {
	DWORD	dwSize;
	DWORD	dwFlags;
	HINSTANCE	hInstance;
	_ANONYMOUS_UNION union {
		LPCWSTR	pszTemplate;
		LPCDLGTEMPLATE	pResource;
	} DUMMYUNIONNAME;
	_ANONYMOUS_UNION union {
		HICON hIcon;
		LPCWSTR pszIcon;
	} DUMMYUNIONNAME2;
	LPCWSTR	pszTitle;
	DLGPROC	pfnDlgProc;
	LPARAM	lParam;
	UINT(CALLBACK *pfnCallback)(HWND,UINT,struct _PROPSHEETPAGEW*);
	UINT *pcRefParent;
#if (_WIN32_IE >= 0x0400)
	LPCWSTR pszHeaderTitle;
	LPCWSTR pszHeaderSubTitle;
#endif
#if (_WIN32_IE >= 0x0501)
    HANDLE hActCtx;
#endif
#if (_WIN32_IE >= 0x0600)
    _ANONYMOUS_UNION union {
        HBITMAP hbmHeader;
        LPCWSTR pszbmHeader;
    } DUMMYUNIONNAME3;
#endif
} PROPSHEETPAGEW,*LPPROPSHEETPAGEW,
  PROPSHEETPAGEW_LATEST, *LPPROPSHEETPAGEW_LATEST;
typedef const PROPSHEETPAGEW *LPCPROPSHEETPAGEW, *LPCPROPSHEETPAGEW_LATEST;
#define PROPSHEETPAGEW_V1_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEW, pcRefParent)
#define PROPSHEETPAGEW_V2_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEW, pszHeaderSubTitle)
#define PROPSHEETPAGEW_V3_SIZE CCSIZEOF_STRUCT(PROPSHEETPAGEW, hActCtx)
#define PROPSHEETPAGEW_V4_SIZE sizeof(PROPSHEETPAGEW)
typedef UINT(CALLBACK *LPFNPSPCALLBACKA)(HWND,UINT,LPPROPSHEETPAGEA);
typedef UINT(CALLBACK *LPFNPSPCALLBACKW)(HWND,UINT,LPPROPSHEETPAGEW);
typedef int(CALLBACK *PFNPROPSHEETCALLBACK)(HWND,UINT,LPARAM);
#ifndef _HPROPSHEETPAGE_DEFINED
#define _HPROPSHEETPAGE_DEFINED
#ifdef _WINE
struct _PSP;
typedef struct _PSP *HPROPSHEETPAGE;
#else
DECLARE_HANDLE(HPROPSHEETPAGE);
#endif
#endif /* _HPROPSHEETPAGE_DEFINED */
typedef struct _PROPSHEETHEADERA {
	DWORD	dwSize;
	DWORD	dwFlags;
	HWND	hwndParent;
	HINSTANCE	hInstance;
	_ANONYMOUS_UNION union {
		HICON	hIcon;
		LPCSTR	pszIcon;
	}DUMMYUNIONNAME;
	LPCSTR	pszCaption;
	UINT	nPages;
	_ANONYMOUS_UNION union {
		UINT	nStartPage;
		LPCSTR	pStartPage;
	}DUMMYUNIONNAME2;
	_ANONYMOUS_UNION union {
		LPCPROPSHEETPAGEA ppsp;
		HPROPSHEETPAGE *phpage;
	}DUMMYUNIONNAME3;
	PFNPROPSHEETCALLBACK pfnCallback;
#if (_WIN32_IE >= 0x0400)
	_ANONYMOUS_UNION union {
		HBITMAP hbmWatermark;
		LPCSTR pszbmWatermark;
	} DUMMYUNIONNAME4;
	HPALETTE hplWatermark;
	_ANONYMOUS_UNION union {
		HBITMAP hbmHeader;
		LPCSTR pszbmHeader;
	} DUMMYUNIONNAME5;
#endif
} PROPSHEETHEADERA,*LPPROPSHEETHEADERA;
typedef const PROPSHEETHEADERA *LPCPROPSHEETHEADERA;
#define PROPSHEETHEADERA_V1_SIZE CCSIZEOF_STRUCT(PROPSHEETHEADERA, pfnCallback)
#define PROPSHEETHEADERA_V2_SIZE sizeof(PROPSHEETHEADERA)
typedef struct _PROPSHEETHEADERW {
	DWORD	dwSize;
	DWORD	dwFlags;
	HWND	hwndParent;
	HINSTANCE	hInstance;
	_ANONYMOUS_UNION union {
		HICON	hIcon;
		LPCWSTR	pszIcon;
	}DUMMYUNIONNAME;
	LPCWSTR	pszCaption;
	UINT	nPages;
	_ANONYMOUS_UNION union {
		UINT	nStartPage;
		LPCWSTR	pStartPage;
	}DUMMYUNIONNAME2;
	_ANONYMOUS_UNION union {
		LPCPROPSHEETPAGEW ppsp;
		HPROPSHEETPAGE *phpage;
	}DUMMYUNIONNAME3;
	PFNPROPSHEETCALLBACK pfnCallback;
#if (_WIN32_IE >= 0x0400)
	_ANONYMOUS_UNION union {
		HBITMAP hbmWatermark;
		LPCWSTR pszbmWatermark;
	} DUMMYUNIONNAME4;
	HPALETTE hplWatermark;
	_ANONYMOUS_UNION union {
		HBITMAP hbmHeader;
		LPCWSTR pszbmHeader;
	} DUMMYUNIONNAME5;
#endif
} PROPSHEETHEADERW,*LPPROPSHEETHEADERW;
typedef const PROPSHEETHEADERW *LPCPROPSHEETHEADERW;
#define PROPSHEETHEADERW_V1_SIZE CCSIZEOF_STRUCT(PROPSHEETHEADERW, pfnCallback)
#define PROPSHEETHEADERW_V2_SIZE sizeof(PROPSHEETHEADERW)
typedef BOOL(CALLBACK *LPFNADDPROPSHEETPAGE)(HPROPSHEETPAGE,LPARAM);
typedef BOOL(CALLBACK *LPFNADDPROPSHEETPAGES)(LPVOID,LPFNADDPROPSHEETPAGE,LPARAM);
typedef struct _PSHNOTIFY {
	NMHDR hdr;
	LPARAM lParam;
} PSHNOTIFY,*LPPSHNOTIFY;

#pragma pack(pop)

HPROPSHEETPAGE WINAPI CreatePropertySheetPageA(LPCPROPSHEETPAGEA);
HPROPSHEETPAGE WINAPI CreatePropertySheetPageW(LPCPROPSHEETPAGEW);
BOOL WINAPI DestroyPropertySheetPage(HPROPSHEETPAGE);
INT_PTR WINAPI PropertySheetA(LPCPROPSHEETHEADERA);
INT_PTR WINAPI PropertySheetW(LPCPROPSHEETHEADERW);
#define PropSheet_AddPage(d,p) SendMessage(d,PSM_ADDPAGE,0,(LPARAM)p)
#define PropSheet_Apply(d) SendMessage(d,PSM_APPLY,0,0)
#define PropSheet_CancelToClose(d) SendMessage(d,PSM_CANCELTOCLOSE,0,0)
#define PropSheet_Changed(d,w) SendMessage(d,PSM_CHANGED,(WPARAM)w,0)
#define PropSheet_GetCurrentPageHwnd(d) (HWND)SendMessage(d,PSM_GETCURRENTPAGEHWND,0,0)
#define PropSheet_GetTabControl(d) (HWND)SendMessage(d,PSM_GETTABCONTROL,0,0)
#define PropSheet_IsDialogMessage(d,m) (BOOL)SendMessage(d,PSM_ISDIALOGMESSAGE,0,(LPARAM)m)
#define PropSheet_PressButton(d,i) SendMessage(d,PSM_PRESSBUTTON,i,0)
#define PropSheet_QuerySiblings(d,w,l) SendMessage(d,PSM_QUERYSIBLINGS,w,l)
#define PropSheet_RebootSystem(d) SendMessage(d,PSM_REBOOTSYSTEM,0,0)
#define PropSheet_RemovePage(d,i,p) SendMessage(d,PSM_REMOVEPAGE,i,(LPARAM)p)
#define PropSheet_RestartWindows(d) SendMessage(d,PSM_RESTARTWINDOWS,0,0)
#define PropSheet_SetCurSel(d,p,i) SendMessage(d,PSM_SETCURSEL,i,(LPARAM)p)
#define PropSheet_SetCurSelByID(d,i) SendMessage(d,PSM_SETCURSELID,0,i)
#define PropSheet_SetFinishText(d,s) SendMessage(d,PSM_SETFINISHTEXT,0,(LPARAM)s)
#define PropSheet_SetTitle(d,w,s) SendMessage(d,PSM_SETTITLE,w,(LPARAM)s)
#define PropSheet_SetWizButtons(d,f) PostMessage(d,PSM_SETWIZBUTTONS,0,(LPARAM)f)
#define PropSheet_UnChanged(d,w) SendMessage(d,PSM_UNCHANGED,(WPARAM)w,0)
#define PropSheet_RecalcPageSizes(d) SendMessage(d,PSM_RECALCPAGESIZES,0,0)
#endif

#define WC_PROPSHEETA      "SysPropertySheet"
#if defined(__GNUC__)
# define WC_PROPSHEETW (const WCHAR []){ 'S','y','s', \
  'P','r','o','p','e','r','t','y','S','h','e','e','t',0 }
#elif defined(_MSC_VER)
# define WC_PROPSHEETW     L"SysPropertySheet"
#else
static const WCHAR WC_PROPSHEETW[] = { 'S','y','s',
  'P','r','o','p','e','r','t','y','S','h','e','e','t',0 };
#endif

#ifdef UNICODE
#define LPFNPSPCALLBACK	LPFNPSPCALLBACKW
#define PROPSHEETPAGE	PROPSHEETPAGEW
#define LPPROPSHEETPAGE	LPPROPSHEETPAGEW
#define LPCPROPSHEETPAGE	LPCPROPSHEETPAGEW
#define PROPSHEETHEADER	PROPSHEETHEADERW
#define LPPROPSHEETHEADER	LPPROPSHEETHEADERW
#define LPCPROPSHEETHEADER	LPCPROPSHEETHEADERW
#define PSM_SETTITLE PSM_SETTITLEW
#define PSM_SETFINISHTEXT PSM_SETFINISHTEXTW
#define PSM_SETHEADERTITLE PSM_SETHEADERTITLEW
#define PSM_SETHEADERSUBTITLE PSM_SETHEADERSUBTITLEW
#define CreatePropertySheetPage CreatePropertySheetPageW
#define PropertySheet PropertySheetW
#else
#define LPFNPSPCALLBACK	LPFNPSPCALLBACKA
#define PROPSHEETPAGE	PROPSHEETPAGEA
#define LPPROPSHEETPAGE	LPPROPSHEETPAGEA
#define LPCPROPSHEETPAGE	LPCPROPSHEETPAGEA
#define PROPSHEETHEADER	PROPSHEETHEADERA
#define LPPROPSHEETHEADER	LPPROPSHEETHEADERA
#define LPCPROPSHEETHEADER	LPCPROPSHEETHEADERA
#define PSM_SETTITLE PSM_SETTITLEA
#define PSM_SETFINISHTEXT PSM_SETFINISHTEXTA
#define PSM_SETHEADERTITLE PSM_SETHEADERTITLEA
#define PSM_SETHEADERSUBTITLE PSM_SETHEADERSUBTITLEA
#define CreatePropertySheetPage CreatePropertySheetPageA
#define PropertySheet PropertySheetA
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef __cplusplus
}
#endif
#ifndef PSH_AEROWIZARD
#define PSH_AEROWIZARD 0x00004000
#endif
#ifndef PSH_HEADERBITMAP
#define PSH_HEADERBITMAP 0x08000000
#endif
#ifndef PSH_NOMARGIN
#define PSH_NOMARGIN 0x10000000
#endif
#ifndef PSH_RESIZABLE
#define PSH_RESIZABLE 0x04000000
#endif
#ifndef PSNRET_MESSAGEHANDLED
#define PSNRET_MESSAGEHANDLED 3
#endif
#ifndef PSP_USEFUSIONCONTEXT
#define PSP_USEFUSIONCONTEXT 0x00004000
#endif
#ifndef PSWIZBF_ELEVATIONREQUIRED
#define PSWIZBF_ELEVATIONREQUIRED 0x00000001
#endif
#ifndef PSWIZB_CANCEL
#define PSWIZB_CANCEL 0x00000010
#endif
#ifndef PSWIZB_RESTORE
#define PSWIZB_RESTORE 1
#endif
#ifndef PSWIZB_SHOW
#define PSWIZB_SHOW 0
#endif

typedef struct _PROPSHEETPAGEA * LPPROPSHEETPAGEA_V4;
typedef struct _PROPSHEETPAGEW * LPPROPSHEETPAGEW_V4;
typedef LPCDLGTEMPLATE PROPSHEETPAGE_RESOURCE;
typedef struct _PROPSHEETPAGEA PROPSHEETPAGEA_V4;
typedef const PROPSHEETPAGEA_V4 * LPCPROPSHEETPAGEA_V4;
typedef struct _PROPSHEETPAGEW PROPSHEETPAGEW_V4;
typedef const PROPSHEETPAGEW_V4 * LPCPROPSHEETPAGEW_V4;

#define _PROPSHEETPAGEA_V4 _PROPSHEETPAGEA
#define _PROPSHEETPAGEW_V4 _PROPSHEETPAGEW
#define LPCPROPSHEETPAGE_LATEST LPCPROPSHEETPAGEA_LATEST
#define LPCPROPSHEETPAGE_V1 LPCPROPSHEETPAGEA_V1
#define LPCPROPSHEETPAGE_V2 LPCPROPSHEETPAGEA_V2
#define LPCPROPSHEETPAGE_V3 LPCPROPSHEETPAGEA_V3
#define LPCPROPSHEETPAGE_V4 LPCPROPSHEETPAGEA_V4
#define LPPROPSHEETPAGE_LATEST LPPROPSHEETPAGEA_LATEST
#define LPPROPSHEETPAGE_V1 LPPROPSHEETPAGEA_V1
#define LPPROPSHEETPAGE_V2 LPPROPSHEETPAGEA_V2
#define LPPROPSHEETPAGE_V3 LPPROPSHEETPAGEA_V3
#define LPPROPSHEETPAGE_V4 LPPROPSHEETPAGEA_V4
#define PropSheet_EnableWizButtonshDlg, dwState, dwMask) PSTMSG(hDlg, PSM_ENABLEWIZBUTTONS, (WPARAM) (dwState), (LPARAM) (dwMask))
#define PropSheet_IdToIndexhDlg, id) (int) SNDMSG(hDlg, PSM_IDTOINDEX, 0, (LPARAM) (id))
#define PropSheet_IndexToHwndhDlg, i) (HWND) SNDMSG(hDlg, PSM_INDEXTOHWND, (WPARAM) (i), 0)
#define PropSheet_IndexToIdhDlg, i) SNDMSG(hDlg, PSM_INDEXTOID, (WPARAM) (i), 0)
#define PropSheet_IndexToPagehDlg, i) (HPROPSHEETPAGE) SNDMSG(hDlg, PSM_INDEXTOPAGE, (WPARAM) (i), 0)
#define PropSheet_InsertPagehDlg, index, hpage) SNDMSG(hDlg, PSM_INSERTPAGE, (WPARAM) (index), (LPARAM) (hpage))
#define PropSheet_PageToIndexhDlg, hpage) (int) SNDMSG(hDlg, PSM_PAGETOINDEX, 0, (LPARAM) (hpage))
#define PropSheet_SetButtonTexthDlg, dwButton, lpszText) SNDMSG(hDlg, PSM_SETBUTTONTEXT, (WPARAM) (dwButton), (LPARAM) (lpszText))
#define PropSheet_SetHeaderSubTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERSUBTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetHeaderTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetNextTexthDlg, lpszText) SNDMSG(hDlg, PSM_SETNEXTTEXT, 0, (LPARAM) (lpszText))
#define PropSheet_ShowWizButtonshDlg, dwFlag, dwButton) PSTMSG(hDlg, PSM_SHOWWIZBUTTONS, (WPARAM) (dwFlag), (LPARAM) (dwButton))
#define PROPSHEETHEADER_V1_SIZE PROPSHEETHEADERA_V1_SIZE
#ifdef _WIN64
#define PROPSHEETHEADER_V2_SIZE 0x60
#else
#define PROPSHEETHEADER_V2_SIZE 0x34
#endif
#define PROPSHEETHEADERA_V1_FIELDS 0
#define PROPSHEETHEADERW_V1_FIELDS DWORD dwSize; DWORD dwFlags; HWND hwndParent; HINSTANCE hInstance; union { HICON hIcon; LPCWSTR pszIcon; } DUMMYUNIONNAME; LPCWSTR pszCaption; UINT nPages; union { UINT nStartPage; LPCWSTR pStartPage; } DUMMYUNIONNAME2; union { LPCPROPSHEETPAGEW ppsp; HPROPSHEETPAGE * phpage; } DUMMYUNIONNAME3; PFNPROPSHEETCALLBACK pfnCallback;
#define PROPSHEETPAGE_LATEST PROPSHEETPAGEA_LATEST
#define PROPSHEETPAGE_V1 0
#define PROPSHEETPAGE_V1_SIZE PROPSHEETPAGEA_V1_SIZE
#define PROPSHEETPAGE_V2 PROPSHEETPAGEA_V2
#ifdef _WIN64
#define PROPSHEETPAGE_V2_SIZE 0x58
#else
#define PROPSHEETPAGE_V2_SIZE 0x30
#endif
#define PROPSHEETPAGE_V3 PROPSHEETPAGEA_V3
#define PROPSHEETPAGE_V4 PROPSHEETPAGEA_V4
#define PROPSHEETPAGEA_V1_FIELDS 0
#define PROPSHEETPAGEW_V1_FIELDS DWORD dwSize; DWORD dwFlags; HINSTANCE hInstance; union { LPCWSTR pszTemplate; PROPSHEETPAGE_RESOURCE pResource; } DUMMYUNIONNAME; union { HICON hIcon; LPCWSTR pszIcon; } DUMMYUNIONNAME2; LPCWSTR pszTitle; DLGPROC pfnDlgProc; LPARAM lParam; LPFNPSPCALLBACKW pfnCallback; UINT * pcRefParent;
#define PSM_ENABLEWIZBUTTONS 0x48B
#define PSM_SETBUTTONTEXT 0x48C
#define PSM_SETBUTTONTEXTW 0x48C
#define PSM_SETNEXTTEXT 0x489
#define PSM_SETNEXTTEXTW 0x489
#define PSM_SHOWWIZBUTTONS 0x48A
#define PSTMSG PostMessage
#define PSWIZF_SETCOLOR ((UINT)0xFFFFFFFF)

struct _PROPSHEETHEADERA_V1
{
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    union
    {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszCaption;
    UINT nPages;
    union
    {
        UINT nStartPage;
        LPCSTR pStartPage;
    };
    union
    {
        LPCPROPSHEETPAGEA ppsp;
        HPROPSHEETPAGE *phpage;
    };
    PFNPROPSHEETCALLBACK pfnCallback;
};

struct _PROPSHEETHEADERA_V2
{
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    union
    {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszCaption;
    UINT nPages;
    union
    {
        UINT nStartPage;
        LPCSTR pStartPage;
    };
    union
    {
        LPCPROPSHEETPAGEA ppsp;
        HPROPSHEETPAGE *phpage;
    };
    PFNPROPSHEETCALLBACK pfnCallback;
    union
    {
        HBITMAP hbmWatermark;
        LPCSTR pszbmWatermark;
    };
    HPALETTE hplWatermark;
    union
    {
        HBITMAP hbmHeader;
        LPCSTR pszbmHeader;
    };
};

struct _PROPSHEETHEADERW_V1
{
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    union
    {
        HICON hIcon;
        LPCWSTR pszIcon;
    };
    LPCWSTR pszCaption;
    UINT nPages;
    union
    {
        UINT nStartPage;
        LPCWSTR pStartPage;
    };
    union
    {
        LPCPROPSHEETPAGEW ppsp;
        HPROPSHEETPAGE *phpage;
    };
    PFNPROPSHEETCALLBACK pfnCallback;
};

struct _PROPSHEETHEADERW_V2
{
    DWORD dwSize;
    DWORD dwFlags;
    HWND hwndParent;
    HINSTANCE hInstance;
    union
    {
        HICON hIcon;
        LPCWSTR pszIcon;
    };
    LPCWSTR pszCaption;
    UINT nPages;
    union
    {
        UINT nStartPage;
        LPCWSTR pStartPage;
    };
    union
    {
        LPCPROPSHEETPAGEW ppsp;
        HPROPSHEETPAGE *phpage;
    };
    PFNPROPSHEETCALLBACK pfnCallback;
    union
    {
        HBITMAP hbmWatermark;
        LPCWSTR pszbmWatermark;
    };
    HPALETTE hplWatermark;
    union
    {
        HBITMAP hbmHeader;
        LPCWSTR pszbmHeader;
    };
};

struct _PROPSHEETPAGEA_V1
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKA pfnCallback;
    UINT *pcRefParent;
};

struct _PROPSHEETPAGEA_V2
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKA pfnCallback;
    UINT *pcRefParent;
    LPCSTR pszHeaderTitle;
    LPCSTR pszHeaderSubTitle;
};

struct _PROPSHEETPAGEA_V3
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCSTR pszIcon;
    };
    LPCSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKA pfnCallback;
    UINT *pcRefParent;
    LPCSTR pszHeaderTitle;
    LPCSTR pszHeaderSubTitle;
    HANDLE hActCtx;
};

struct _PROPSHEETPAGEW_V1
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCWSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCWSTR pszIcon;
    };
    LPCWSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKW pfnCallback;
    UINT *pcRefParent;
};

struct _PROPSHEETPAGEW_V2
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCWSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCWSTR pszIcon;
    };
    LPCWSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKW pfnCallback;
    UINT *pcRefParent;
    LPCWSTR pszHeaderTitle;
    LPCWSTR pszHeaderSubTitle;
};

struct _PROPSHEETPAGEW_V3
{
    DWORD dwSize;
    DWORD dwFlags;
    HINSTANCE hInstance;
    union
    {
        LPCWSTR pszTemplate;
        PROPSHEETPAGE_RESOURCE pResource;
    };
    union
    {
        HICON hIcon;
        LPCWSTR pszIcon;
    };
    LPCWSTR pszTitle;
    DLGPROC pfnDlgProc;
    LPARAM lParam;
    LPFNPSPCALLBACKW pfnCallback;
    UINT *pcRefParent;
    LPCWSTR pszHeaderTitle;
    LPCWSTR pszHeaderSubTitle;
    HANDLE hActCtx;
};

typedef struct _PROPSHEETHEADERA_V1 *LPPROPSHEETHEADERA_V1;

typedef struct _PROPSHEETHEADERA_V2 *LPPROPSHEETHEADERA_V2;

typedef struct _PROPSHEETHEADERW_V1 *LPPROPSHEETHEADERW_V1;

typedef struct _PROPSHEETHEADERW_V2 *LPPROPSHEETHEADERW_V2;

typedef struct _PROPSHEETPAGEA_V1 *LPPROPSHEETPAGEA_V1;

typedef struct _PROPSHEETPAGEA_V2 *LPPROPSHEETPAGEA_V2;

typedef struct _PROPSHEETPAGEA_V3 *LPPROPSHEETPAGEA_V3;

typedef struct _PROPSHEETPAGEW_V1 *LPPROPSHEETPAGEW_V1;

typedef struct _PROPSHEETPAGEW_V2 *LPPROPSHEETPAGEW_V2;

typedef struct _PROPSHEETPAGEW_V3 *LPPROPSHEETPAGEW_V3;

typedef struct _PROPSHEETHEADERA_V1 PROPSHEETHEADERA_V1;

typedef const PROPSHEETHEADERA_V1 *LPCPROPSHEETHEADERA_V1;

typedef struct _PROPSHEETHEADERA_V2 PROPSHEETHEADERA_V2;

typedef const PROPSHEETHEADERA_V2 *LPCPROPSHEETHEADERA_V2;

typedef struct _PROPSHEETHEADERW_V1 PROPSHEETHEADERW_V1;

typedef const PROPSHEETHEADERW_V1 *LPCPROPSHEETHEADERW_V1;

typedef struct _PROPSHEETHEADERW_V2 PROPSHEETHEADERW_V2;

typedef const PROPSHEETHEADERW_V2 *LPCPROPSHEETHEADERW_V2;

typedef struct _PROPSHEETPAGEA_V1 PROPSHEETPAGEA_V1;

typedef const PROPSHEETPAGEA_V1 *LPCPROPSHEETPAGEA_V1;

typedef struct _PROPSHEETPAGEA_V2 PROPSHEETPAGEA_V2;

typedef const PROPSHEETPAGEA_V2 *LPCPROPSHEETPAGEA_V2;

typedef struct _PROPSHEETPAGEA_V3 PROPSHEETPAGEA_V3;

typedef const PROPSHEETPAGEA_V3 *LPCPROPSHEETPAGEA_V3;

typedef struct _PROPSHEETPAGEW_V1 PROPSHEETPAGEW_V1;

typedef const PROPSHEETPAGEW_V1 *LPCPROPSHEETPAGEW_V1;

typedef struct _PROPSHEETPAGEW_V2 PROPSHEETPAGEW_V2;

typedef const PROPSHEETPAGEW_V2 *LPCPROPSHEETPAGEW_V2;

typedef struct _PROPSHEETPAGEW_V3 PROPSHEETPAGEW_V3;

typedef const PROPSHEETPAGEW_V3 *LPCPROPSHEETPAGEW_V3;

#define PropSheet_EnableWizButtonshDlg, dwState, dwMask) PSTMSG(hDlg, PSM_ENABLEWIZBUTTONS, (WPARAM) (dwState), (LPARAM) (dwMask))
#define PropSheet_IdToIndexhDlg, id) (int) SNDMSG(hDlg, PSM_IDTOINDEX, 0, (LPARAM) (id))
#define PropSheet_IndexToHwndhDlg, i) (HWND) SNDMSG(hDlg, PSM_INDEXTOHWND, (WPARAM) (i), 0)
#define PropSheet_IndexToIdhDlg, i) SNDMSG(hDlg, PSM_INDEXTOID, (WPARAM) (i), 0)
#define PropSheet_IndexToPagehDlg, i) (HPROPSHEETPAGE) SNDMSG(hDlg, PSM_INDEXTOPAGE, (WPARAM) (i), 0)
#define PropSheet_InsertPagehDlg, index, hpage) SNDMSG(hDlg, PSM_INSERTPAGE, (WPARAM) (index), (LPARAM) (hpage))
#define PropSheet_PageToIndexhDlg, hpage) (int) SNDMSG(hDlg, PSM_PAGETOINDEX, 0, (LPARAM) (hpage))
#define PropSheet_SetButtonTexthDlg, dwButton, lpszText) SNDMSG(hDlg, PSM_SETBUTTONTEXT, (WPARAM) (dwButton), (LPARAM) (lpszText))
#define PropSheet_SetHeaderSubTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERSUBTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetHeaderTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetNextTexthDlg, lpszText) SNDMSG(hDlg, PSM_SETNEXTTEXT, 0, (LPARAM) (lpszText))
#define PropSheet_ShowWizButtonshDlg, dwFlag, dwButton) PSTMSG(hDlg, PSM_SHOWWIZBUTTONS, (WPARAM) (dwFlag), (LPARAM) (dwButton))

#define PropSheet_EnableWizButtonshDlg, dwState, dwMask) PSTMSG(hDlg, PSM_ENABLEWIZBUTTONS, (WPARAM) (dwState), (LPARAM) (dwMask))
#define PropSheet_IdToIndexhDlg, id) (int) SNDMSG(hDlg, PSM_IDTOINDEX, 0, (LPARAM) (id))
#define PropSheet_IndexToHwndhDlg, i) (HWND) SNDMSG(hDlg, PSM_INDEXTOHWND, (WPARAM) (i), 0)
#define PropSheet_IndexToIdhDlg, i) SNDMSG(hDlg, PSM_INDEXTOID, (WPARAM) (i), 0)
#define PropSheet_IndexToPagehDlg, i) (HPROPSHEETPAGE) SNDMSG(hDlg, PSM_INDEXTOPAGE, (WPARAM) (i), 0)
#define PropSheet_InsertPagehDlg, index, hpage) SNDMSG(hDlg, PSM_INSERTPAGE, (WPARAM) (index), (LPARAM) (hpage))
#define PropSheet_PageToIndexhDlg, hpage) (int) SNDMSG(hDlg, PSM_PAGETOINDEX, 0, (LPARAM) (hpage))
#define PropSheet_SetButtonTexthDlg, dwButton, lpszText) SNDMSG(hDlg, PSM_SETBUTTONTEXT, (WPARAM) (dwButton), (LPARAM) (lpszText))
#define PropSheet_SetHeaderSubTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERSUBTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetHeaderTitlehDlg, index, lpszText) SNDMSG(hDlg, PSM_SETHEADERTITLE, (WPARAM) (index), (LPARAM) (lpszText))
#define PropSheet_SetNextTexthDlg, lpszText) SNDMSG(hDlg, PSM_SETNEXTTEXT, 0, (LPARAM) (lpszText))
#define PropSheet_ShowWizButtonshDlg, dwFlag, dwButton) PSTMSG(hDlg, PSM_SHOWWIZBUTTONS, (WPARAM) (dwFlag), (LPARAM) (dwButton))

#endif
