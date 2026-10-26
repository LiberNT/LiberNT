#pragma once

#include <ndk/kbd.h>
 
typedef struct tagKBDNLSLAYER
{
    USHORT OEMIdentifier;
    USHORT LayoutInformation;
    UINT NumOfVkToF;
    struct _VK_TO_FUNCTION_TABLE *pVkToF;
    INT NumOfMouseVKey;
    PUSHORT pusMouseVKey;
} KBDNLSLAYER, *PKBDNLSLAYER;
 
typedef struct tagKBDFILE
{
    HEAD head;
    struct tagKBDFILE *pkfNext;
    WCHAR awchKF[20];
    HANDLE hBase;
    struct _KBDTABLES *pKbdTbl;
    ULONG Size;
    PKBDNLSLAYER pKbdNlsTbl;
} KBDFILE, *PKBDFILE;

typedef struct tagKL
{
    HEAD head;
    struct tagKL *pklNext;
    struct tagKL *pklPrev;
    DWORD dwKL_Flags;
    HKL hkl;
    PKBDFILE spkf;
    DWORD dwFontSigs;
    UINT iBaseCharset;
    USHORT CodePage;
    WCHAR wchDiacritic;
    PIMEINFOEX piiex;
} KL, *PKL;

typedef struct _ATTACHINFO
{
  struct _ATTACHINFO *paiNext;
  PTHREADINFO pti1;
  PTHREADINFO pti2;
} ATTACHINFO, *PATTACHINFO;

extern PATTACHINFO gpai;

/* Key States */
#define KS_DOWN_BIT      0x80
#define KS_LOCK_BIT      0x01
/* Scan Codes */
#define SC_KEY_UP        0x8000
/* lParam bits */
#define LP_DO_NOT_CARE_BIT (1<<25) // For GetKeyNameText

/* General */
CODE_SEG("INIT") NTSTATUS NTAPI InitInputImpl(VOID);
VOID NTAPI RawInputThreadMain(VOID);
BOOL FASTCALL IntBlockInput(PTHREADINFO W32Thread, BOOL BlockIt);
NTSTATUS FASTCALL UserAttachThreadInput(PTHREADINFO,PTHREADINFO,BOOL);
BOOL FASTCALL IsRemoveAttachThread(PTHREADINFO);
VOID FASTCALL DoTheScreenSaver(VOID);
#define ThreadHasInputAccess(W32Thread) (TRUE)

/* Keyboard */
CODE_SEG("INIT") NTSTATUS NTAPI InitKeyboardImpl(VOID);
VOID NTAPI UserInitKeyboard(HANDLE hKeyboardDevice);
VOID NTAPI UserGetKeyboardTypematic(PULONG Delay, PULONG Rate);
VOID NTAPI UserSetKeyboardTypematic(VOID);
PKL W32kGetDefaultKeyLayout(VOID);
VOID NTAPI UserProcessKeyboardInput(PKEYBOARD_INPUT_DATA pKeyInput);
BOOL NTAPI UserSendKeyboardInput(KEYBDINPUT *pKbdInput, BOOL bInjected);
PKL NTAPI UserHklToKbl(HKL hKl);
BOOL NTAPI UserSetDefaultInputLang(HKL hKl);
extern DWORD gdwLanguageToggleKey;
extern DWORD gdwLayoutToggleKey;
extern BOOL gbEnableHexNumpad;

/* Mouse */
WORD FASTCALL UserGetMouseButtonsState(VOID);
VOID NTAPI UserProcessMouseInput(PMOUSE_INPUT_DATA pMouseInputData);
BOOL NTAPI UserSendMouseInput(MOUSEINPUT *pMouseInput, BOOL bInjected);
VOID NTAPI UserAccessKeyboardInput(KEYBDINPUT *pKbdInput);
VOID NTAPI UserAccessMouseButtonUp(VOID);
VOID NTAPI UserAccessSettingsChanged(VOID);
VOID NTAPI UserRecordMousePointerInput(const POINT *ppt, DWORD dwEventFlags, DWORD dwTime);

/* Raw input */
VOID NTAPI RawInputProcessKeyboard(WORD wScanCode, WORD wVk, DWORD dwFlags, UINT uMsg, ULONG_PTR dwExtraInfo, BOOL bInjected, DWORD dwTime);
VOID NTAPI RawInputProcessMouseData(PMOUSE_INPUT_DATA pMouseInputData);
VOID NTAPI RawInputProcessMouseInput(const MOUSEINPUT *pMouseInput);
VOID NTAPI RawInputSetMouseAttributes(HANDLE hMouseDevice);
VOID NTAPI RawInputUpdateDevices(VOID);
BOOL FASTCALL RawInputIsNoLegacy(PPROCESSINFO ppi, DWORD dwType);
VOID FASTCALL RawInputSetThreadData(PTHREADINFO pti, PUSER_MESSAGE Message, BOOL bRemove);
VOID FASTCALL RawInputCleanupThread(PTHREADINFO pti);
VOID FASTCALL RawInputCleanupProcess(PPROCESSINFO ppi);

typedef struct _RAWINPUT_HID_TARGET
{
    HANDLE hDevice;
    UNICODE_STRING Path;
    ULONG ReportLength;
} RAWINPUT_HID_TARGET, *PRAWINPUT_HID_TARGET;

VOID NTAPI RawInputProcessHidReport(HANDLE hDevice, const UCHAR *Report, ULONG Length);
ULONG NTAPI RawInputGetHidTargets(PRAWINPUT_HID_TARGET Targets, ULONG MaxTargets);

/* HID consumer controls */
#define HIDINPUT_MAX_DEVICES 8
#define HIDINPUT_MAX_RAW_DEVICES 16
struct _HID_COLLECTION_INFORMATION;
struct _HIDP_PREPARSED_DATA;
struct _HIDP_CAPS;
NTSTATUS NTAPI HidInputQueryCollection(PUNICODE_STRING Path, struct _HID_COLLECTION_INFORMATION *Information,
                                       struct _HIDP_PREPARSED_DATA **PreparsedData, struct _HIDP_CAPS *Caps);
VOID NTAPI HidInputRawInputChanged(VOID);
VOID NTAPI HidInputWaitRawInput(VOID);
NTSTATUS NTAPI InitHidInputImpl(PDRIVER_OBJECT DriverObject);
VOID NTAPI HidInputStart(VOID);
VOID NTAPI HidInputStop(VOID);
ULONG NTAPI HidInputPrepareWait(PVOID *WaitObjects, PULONG WaitCount);
BOOLEAN NTAPI HidInputSignaled(PVOID Object);
BOOLEAN NTAPI HidInputProcess(VOID);

/* Touch screens */
#define TOUCHINPUT_MAX_DEVICES 4
typedef struct _TOUCH_CONTACT
{
    ULONG ContactId;
    ULONG X;
    ULONG Y;
    BOOLEAN InContact;
    BOOLEAN Confidence;
} TOUCH_CONTACT, *PTOUCH_CONTACT;
struct _POINTER_TOUCH_SOURCE;
NTSTATUS NTAPI InitTouchInputImpl(PDRIVER_OBJECT DriverObject);
VOID NTAPI TouchInputStart(VOID);
VOID NTAPI TouchInputStop(VOID);
ULONG NTAPI TouchInputPrepareWait(PVOID *WaitObjects, PULONG WaitCount);
BOOLEAN NTAPI TouchInputSignaled(PVOID Object);
BOOLEAN NTAPI TouchInputProcess(VOID);
struct _POINTER_TOUCH_SOURCE * NTAPI PointerCreateTouchSource(ULONG Capacity);
VOID NTAPI PointerDestroyTouchSource(struct _POINTER_TOUCH_SOURCE *Source);
VOID NTAPI PointerProcessTouchFrame(struct _POINTER_TOUCH_SOURCE *Source, const TOUCH_CONTACT *Contacts, ULONG Count);
VOID FASTCALL co_PointerRouteMessage(PTHREADINFO pti, MSG *Msg, LONG_PTR ExtraInfo);
VOID FASTCALL PointerDefWindowProc(PWND Wnd, UINT Msg, WPARAM wParam);
BOOL FASTCALL PointerGetTouchInfoList(UINT32 PointerId, ULONG PointerType, SIZE_T EntrySize, PULONG EntriesCount,
                                      PULONG PointerCount, PVOID PointerInfo);
BOOL NTAPI UserSendTouchMouseInput(MOUSEINPUT *pmi);

/* IMM */
UINT FASTCALL IntImmProcessKey(
    _In_ PUSER_MESSAGE_QUEUE MessageQueue,
    _In_ PWND pWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);
VOID FASTCALL IntFreeImeHotKeys(VOID);

extern DWORD gSystemFS;
extern UINT gSystemCPCharSet; 
extern HANDLE ghKeyboardDevice;
extern PTHREADINFO ptiRawInput;
extern BYTE gafAsyncKeyState[256 * 2 / 8]; // 2 bits per key

#define GET_KS_BYTE(vk) ((vk) * 2 / 8)
#define GET_KS_DOWN_BIT(vk) (1 << (((vk) % 4)*2))
#define GET_KS_LOCK_BIT(vk) (1 << (((vk) % 4)*2 + 1))
#define IS_KEY_DOWN(ks, vk) (((ks)[GET_KS_BYTE(vk)] & GET_KS_DOWN_BIT(vk)) ? TRUE : FALSE)
#define IS_KEY_LOCKED(ks, vk) (((ks)[GET_KS_BYTE(vk)] & GET_KS_LOCK_BIT(vk)) ? TRUE : FALSE)
#define SET_KEY_DOWN(ks, vk, down) (ks)[GET_KS_BYTE(vk)] = ((down) ? \
                                                            ((ks)[GET_KS_BYTE(vk)] | GET_KS_DOWN_BIT(vk)) : \
                                                            ((ks)[GET_KS_BYTE(vk)] & ~GET_KS_DOWN_BIT(vk)))
#define SET_KEY_LOCKED(ks, vk, down) (ks)[GET_KS_BYTE(vk)] = ((down) ? \
                                                              ((ks)[GET_KS_BYTE(vk)] | GET_KS_LOCK_BIT(vk)) : \
                                                              ((ks)[GET_KS_BYTE(vk)] & ~GET_KS_LOCK_BIT(vk)))

extern PKL gspklBaseLayout;
extern KEYBOARD_ATTRIBUTES gKeyboardInfo;
