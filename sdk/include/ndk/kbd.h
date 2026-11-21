#ifndef __KBD_H
#define __KBD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual key flags */
#define KBDEXT     0x100  /* Extended key code */
#define KBDMULTIVK 0x200  /* Multi-key */
#define KBDSPECIAL 0x400  /* Special key */
#define KBDNUMPAD  0x800  /* Number-pad */

/* Modifier bits */
#define KBDSHIFT   0x001  /* Shift modifier */
#define KBDCTRL    0x002  /* Ctrl modifier */
#define KBDALT     0x004  /* Alt modifier */

/* Invalid shift */
#define SHFT_INVALID 0x0F

  typedef struct _VK_TO_BIT {
    BYTE Vk;
    BYTE ModBits;
  } VK_TO_BIT, *PVK_TO_BIT;

  typedef struct _MODIFIERS {
    PVK_TO_BIT pVkToBit;
    WORD wMaxModBits;
    BYTE ModNumber[];
  } MODIFIERS, *PMODIFIERS;

#define TYPEDEF_VK_TO_WCHARS(i) \
  typedef struct _VK_TO_WCHARS ## i { \
    BYTE VirtualKey; \
    BYTE Attributes; \
    WCHAR wch[i]; \
  } VK_TO_WCHARS ## i, *PVK_TO_WCHARS ## i;

  TYPEDEF_VK_TO_WCHARS(1)
  TYPEDEF_VK_TO_WCHARS(2)
  TYPEDEF_VK_TO_WCHARS(3)
  TYPEDEF_VK_TO_WCHARS(4)
  TYPEDEF_VK_TO_WCHARS(5)
  TYPEDEF_VK_TO_WCHARS(6)
  TYPEDEF_VK_TO_WCHARS(7)
  TYPEDEF_VK_TO_WCHARS(8)
  TYPEDEF_VK_TO_WCHARS(9)
  TYPEDEF_VK_TO_WCHARS(10)

  typedef struct _VK_TO_WCHAR_TABLE {
    PVK_TO_WCHARS1 pVkToWchars;
    BYTE nModifications;
    BYTE cbSize;
  } VK_TO_WCHAR_TABLE, *PVK_TO_WCHAR_TABLE;

  typedef struct _DEADKEY {
    DWORD dwBoth;
    WCHAR wchComposed;
    USHORT uFlags;
  } DEADKEY, *PDEADKEY;

  typedef WCHAR *DEADKEY_LPWSTR;

#define DKF_DEAD 1

  typedef struct _VSC_LPWSTR {
    BYTE vsc;
    LPWSTR pwsz;
  } VSC_LPWSTR, *PVSC_LPWSTR;

  typedef struct _VSC_VK {
    BYTE Vsc;
    USHORT Vk;
  } VSC_VK, *PVSC_VK;

#define TYPEDEF_LIGATURE(i) \
typedef struct _LIGATURE ## i { \
  BYTE VirtualKey; \
  WORD ModificationNumber; \
  WCHAR wch[i]; \
} LIGATURE ## i, *PLIGATURE ## i;

  TYPEDEF_LIGATURE(1)
  TYPEDEF_LIGATURE(2)
  TYPEDEF_LIGATURE(3)
  TYPEDEF_LIGATURE(4)
  TYPEDEF_LIGATURE(5)

#define KBD_VERSION 1
#define GET_KBD_VERSION(p) (HIWORD((p)->fLocalFlags))
#define KLLF_ALTGR     0x1
#define KLLF_SHIFTLOCK 0x2
#define KLLF_LRM_RLM   0x4

  typedef struct _KBDTABLES {
    PMODIFIERS pCharModifiers;
    PVK_TO_WCHAR_TABLE pVkToWcharTable;
    PDEADKEY pDeadKey;
    VSC_LPWSTR *pKeyNames;
    VSC_LPWSTR *pKeyNamesExt;
    LPWSTR *pKeyNamesDead;
    USHORT *pusVSCtoVK;
    BYTE bMaxVSCtoVK;
    PVSC_VK pVSCtoVK_E0;
    PVSC_VK pVSCtoVK_E1;
    DWORD fLocaleFlags;
    BYTE nLgMax;
    BYTE cbLgEntry;
    PLIGATURE1 pLigature;
    DWORD dwType;
    DWORD dwSubType;
  } KBDTABLES, *PKBDTABLES;

/* Constants that help table decoding */
#define WCH_NONE  0xf000
#define WCH_DEAD  0xf001
#define WCH_LGTR  0xf002

/* VK_TO_WCHARS attributes */
#define CAPLOK       0x01
#define SGCAPS       0x02
#define CAPLOKALTGR  0x04
#define KANALOK      0x08
#define GRPSELTAP    0x80

#define VK_ABNT_C1  0xC1
#define VK_ABNT_C2  0xC2

/* Useful scancodes */
#define SCANCODE_LSHIFT  0x2A
#define SCANCODE_RSHIFT  0x36
#define SCANCODE_CTRL    0x1D
#define SCANCODE_ALT     0x38

#ifdef __cplusplus
};
#endif//__KBD_H

#define _KBD_ 0
#define ACUTE 0x301
#define AX_KBD_DESKTOP_TYPE 1
#define BREVE 0x306
#define CEDILLA 0x327
#define CIRCUMFLEX 0x302
#define DEC_KBD_ANSI_LAYOUT_TYPE 1
#define DEC_KBD_JIS_LAYOUT_TYPE 2
#define DIARESIS 0x308
#define DIARESIS_TONOS 0x385
#define DONTCARE_BIT 0x2000000
#define DOT_ABOVE 0x307
#define DOUBLE_ACUTE 0x30B
#define EXTENDED_BIT 0x1000000
#define FAKE_KEYSTROKE 0x2000000
#define FMR_KBD_JIS_TYPE 0
#define FMR_KBD_OASYS_TYPE 1
#define FMV_KBD_OASYS_TYPE 2
#define GRAVE 0x300
#define HACEK 0x30C
#define HOOK_ABOVE 0x309
#define KBD_LONG_POINTER
#define KBD_TYPE 4
#define KBDBASE 0
#define KBDBREAK ((USHORT)0x8000)
#define KBDGRPSELTAP 0x80
#define KBDINJECTEDVK ((USHORT)0x2000)
#define KBDKANA 8
#define KBDLOYA 0x20
#define KBDMAPPEDVK ((USHORT)0x4000)
#define KBDNLS_ALPHANUM 5
#define KBDNLS_CODEINPUT 0xA
#define KBDNLS_CONV_OR_NONCONV 0xF
#define KBDNLS_HELP_OR_END 0xB
#define KBDNLS_HIRAGANA 6
#define KBDNLS_HOME_OR_CLEAR 0xC
#define KBDNLS_INDEX_ALT 2
#define KBDNLS_INDEX_NORMAL 1
#define KBDNLS_KANAEVENT 0xE
#define KBDNLS_KANALOCK 4
#define KBDNLS_KATAKANA 7
#define KBDNLS_NOEVENT 1
#define KBDNLS_NULL 0
#define KBDNLS_NUMPAD 0xD
#define KBDNLS_ROMAN 9
#define KBDNLS_SBCSDBCS 8
#define KBDNLS_SEND_BASE_VK 2
#define KBDNLS_SEND_PARAM_VK 3
#define KBDNLS_TYPE_NORMAL 1
#define KBDNLS_TYPE_NULL 0
#define KBDNLS_TYPE_TOGGLE 2
#define KBDROYA 0x10
#define KBDTABLE_MULTI_MAX 8
#define KBDUNICODE ((USHORT)0x1000)
#define KEYBOARD_TYPE_GENERIC_101 4
#define KEYBOARD_TYPE_JAPAN 7
#define KEYBOARD_TYPE_KOREA 8
#define KEYBOARD_TYPE_UNKNOWN 0x51
#define KLLF_GLOBAL_ATTRS 2
#define KLLF_LAYOUT_ATTRS 7
#define MACRON 0x304
#define MICROSOFT_KBD_001_TYPE 4
#define MICROSOFT_KBD_002_TYPE 3
#define MICROSOFT_KBD_101_TYPE 0
#define MICROSOFT_KBD_101A_TYPE 0
#define MICROSOFT_KBD_101B_TYPE 4
#define MICROSOFT_KBD_101C_TYPE 5
#define MICROSOFT_KBD_103_TYPE 6
#define MICROSOFT_KBD_106_TYPE 2
#define MICROSOFT_KBD_AX_TYPE 1
#define MICROSOFT_KBD_FUNC 0xC
#define NEC_KBD_106_TYPE 5
#define NEC_KBD_H_MODE_TYPE 3
#define NEC_KBD_LAPTOP_TYPE 4
#define NEC_KBD_N_MODE_TYPE 2
#define NEC_KBD_NORMAL_TYPE 1
#define NLSKBD_INFO_ACCESSIBILITY_KEYMAP 2
#define NLSKBD_INFO_EMURATE_101_KEYBOARD 0x10
#define NLSKBD_INFO_EMURATE_106_KEYBOARD 0x20
#define NLSKBD_INFO_SEND_IME_NOTIFICATION 1
#define NLSKBD_OEM_AX 1
#define NLSKBD_OEM_DEC 0x18
#define NLSKBD_OEM_EPSON 4
#define NLSKBD_OEM_FUJITSU 5
#define NLSKBD_OEM_IBM 7
#define NLSKBD_OEM_MATSUSHITA 0xA
#define NLSKBD_OEM_MICROSOFT 0
#define NLSKBD_OEM_NEC 0xD
#define NLSKBD_OEM_TOSHIBA 0x12
#define OGONEK 0x328
#define OVERSCORE 0x305
#define RING 0x30A
#define SCANCODE_LWIN 0x5B
#define SCANCODE_NUMPAD_FIRST 0x47
#define SCANCODE_NUMPAD_LAST 0x52
#define SCANCODE_RWIN 0x5C
#define SCANCODE_SIMULATED 0x200
#define SCANCODE_THAI_LAYOUT_TOGGLE 0x29
#define T00 0xFF
#define T01 0x1B
#define T02 0x31
#define T03 0x32
#define T04 0x33
#define T05 0x34
#define T06 0x35
#define T07 0x36
#define T08 0x37
#define T09 0x38
#define T0A 0x39
#define T0B 0x30
#define T0C 0xBD
#define T0D 0xBB
#define T0E 8
#define T0F 9
#define T11 0x57
#define T12 0x45
#define T13 0x52
#define T14 0x54
#define T15 0x59
#define T16 0x55
#define T17 0x49
#define T18 0x4F
#define T19 0x50
#define T1A 0xDB
#define T1B 0xDD
#define T1C 0xD
#define T1D 0xA2
#define T1E 0x41
#define T1F 0x53
#define T20 0x44
#define T21 0x46
#define T22 0x47
#define T23 0x48
#define T24 0x4A
#define T25 0x4B
#define T26 0x4C
#define T27 0xBA
#define T28 0xDE
#define T29 0xC0
#define T2A 0xA0
#define T2B 0xDC
#define T2C 0x5A
#define T2D 0x58
#define T2E 0x43
#define T2F 0x56
#define T30 0x42
#define T31 0x4E
#define T32 0x4D
#define T33 0xBC
#define T34 0xBE
#define T35 0xBF
#define T36 0xA1
#define T37 0x6A
#define T38 0xA4
#define T39 0x20
#define T3A 0x14
#define T3B 0x70
#define T3C 0x71
#define T3D 0x72
#define T3E 0x73
#define T3F 0x74
#define T40 0x75
#define T41 0x76
#define T42 0x77
#define T43 0x78
#define T44 0x79
#define T45 0x90
#define T46 0x91
#define T47 0x24
#define T48 0x26
#define T49 0x21
#define T4A 0x6D
#define T4B 0x25
#define T4C 0xC
#define T4D 0x27
#define T4E 0x6B
#define T4F 0x23
#define T50 0x28
#define T51 0x22
#define T52 0x2D
#define T53 0x2E
#define T54 0x2C
#define T55 0xFF
#define T56 0xE2
#define T57 0x7A
#define T58 0x7B
#define T59 0xC
#define T5A 0xEE
#define T5B 0xF1
#define T5C 0xEA
#define T5D 0xF9
#define T5E 0xF5
#define T5F 0xF3
#define T60 0xFF
#define T61 0xFF
#define T62 0xFB
#define T63 0x2F
#define T64 0x7C
#define T65 0x7D
#define T66 0x7E
#define T67 0x7F
#define T68 0x80
#define T69 0x81
#define T6A 0x82
#define T6B 0x83
#define T6C 0x84
#define T6D 0x85
#define T6E 0x86
#define T6F 0xED
#define T70 0xFF
#define T71 0xE9
#define T72 0xFF
#define T73 0xC1
#define T74 0xFF
#define T75 0xFF
#define T76 0x87
#define T77 0xFF
#define T78 0xFF
#define T79 0xFF
#define T7A 0xFF
#define T7B 0xEB
#define T7C 9
#define T7D 0xFF
#define T7E 0xC2
#define T7F 0xEC
#define TILDE 0x303
#define TONOS 0x384
#define TOSHIBA_KBD_DESKTOP_TYPE 0xD
#define TOSHIBA_KBD_LAPTOP_TYPE 0xF
#define UMLAUT 0x308
#define VK_ 0
#define VK__none_ 0xFF
#define wszACUTE L"\x0301"
#define wszBREVE L"\x0306"
#define wszCEDILLA L"\x0327"
#define wszCIRCUMFLEX L"\x0302"
#define wszDIARESIS_TONOS L"\x0385"
#define wszDOT_ABOVE L"\x0307"
#define wszDOUBLE_ACUTE L"\x030B"
#define wszGRAVE 0
#define wszHACEK L"\x030C"
#define wszHOOK_ABOVE L"\x0309"
#define wszMACRON L"\x0304"
#define wszOGONEK L"\x0328"
#define wszOVERSCORE L"\x0305"
#define wszRING L"\x030A"
#define wszTILDE L"\x0303"
#define wszTONOS L"\x0384"
#define wszUMLAUT L"\x0308"
#define X1C 0xD
#define X1D 0xA3
#define X2E 0xAE
#define X32 0xAC
#define X35 0x6F
#define X37 0x2C
#define X38 0xA5
#define X46 3
#define X47 0x24
#define X48 0x26
#define X49 0x21
#define X4B 0x25
#define X4D 0x27
#define X4F 0x23
#define X50 0x28
#define X51 0x22
#define X52 0x2D
#define X53 0x2E
#define X5B 0x5B
#define X5C 0x5C
#define X5D 0x5D
#define X5E 0
#define X65 0xAA
#define X66 0xAB
#define X67 0xA8
#define X68 0xA9
#define X69 0xA7
#define X6A 0xA6
#define X6B 0xB6
#define X6C 0xB4
#define X6D 0xB5
#define Y1D 0x13

struct _LIGATURE1
{
    BYTE VirtualKey;
    WORD ModificationNumber;
    WCHAR wch[1];
};

struct _LIGATURE2
{
    BYTE VirtualKey;
    WORD ModificationNumber;
    WCHAR wch[2];
};

struct _LIGATURE3
{
    BYTE VirtualKey;
    WORD ModificationNumber;
    WCHAR wch[3];
};

struct _LIGATURE4
{
    BYTE VirtualKey;
    WORD ModificationNumber;
    WCHAR wch[4];
};

struct _LIGATURE5
{
    BYTE VirtualKey;
    WORD ModificationNumber;
    WCHAR wch[5];
};

struct _VK_FUNCTION_PARAM
{
    BYTE NLSFEProcIndex;
    ULONG NLSFEProcParam;
};

struct _VK_TO_WCHARS1
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[1];
};

struct _VK_TO_WCHARS10
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[10];
};

struct _VK_TO_WCHARS2
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[2];
};

struct _VK_TO_WCHARS3
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[3];
};

struct _VK_TO_WCHARS4
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[4];
};

struct _VK_TO_WCHARS5
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[5];
};

struct _VK_TO_WCHARS6
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[6];
};

struct _VK_TO_WCHARS7
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[7];
};

struct _VK_TO_WCHARS8
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[8];
};

struct _VK_TO_WCHARS9
{
    BYTE VirtualKey;
    BYTE Attributes;
    WCHAR wch[9];
};

struct _VK_VSC
{
    BYTE Vk;
    BYTE Vsc;
};

typedef struct _LIGATURE1 LIGATURE1;

typedef struct _LIGATURE2 LIGATURE2;

typedef struct _LIGATURE3 LIGATURE3;

typedef struct _LIGATURE4 LIGATURE4;

typedef struct _LIGATURE5 LIGATURE5;

typedef struct _LIGATURE2 *PLIGATURE2;

typedef struct _LIGATURE3 *PLIGATURE3;

typedef struct _LIGATURE4 *PLIGATURE4;

typedef struct _LIGATURE5 *PLIGATURE5;

typedef struct _VK_FUNCTION_PARAM *PVK_FPARAM;

typedef struct _VK_TO_WCHARS10 *PVK_TO_WCHARS10;

typedef struct _VK_TO_WCHARS2 *PVK_TO_WCHARS2;

typedef struct _VK_TO_WCHARS3 *PVK_TO_WCHARS3;

typedef struct _VK_TO_WCHARS4 *PVK_TO_WCHARS4;

typedef struct _VK_TO_WCHARS5 *PVK_TO_WCHARS5;

typedef struct _VK_TO_WCHARS6 *PVK_TO_WCHARS6;

typedef struct _VK_TO_WCHARS7 *PVK_TO_WCHARS7;

typedef struct _VK_TO_WCHARS8 *PVK_TO_WCHARS8;

typedef struct _VK_TO_WCHARS9 *PVK_TO_WCHARS9;

typedef struct _VK_VSC *PVK_VSC;

struct tagKBD_TYPE_INFO
{
    DWORD dwVersion;
    DWORD dwType;
    DWORD dwSubType;
};

typedef struct tagKBD_TYPE_INFO KBD_TYPE_INFO;

typedef struct tagKBD_TYPE_INFO *PKBD_TYPE_INFO;

struct tagKbdLayer
{
    PMODIFIERS pCharModifiers;
    PVK_TO_WCHAR_TABLE pVkToWcharTable;
    PDEADKEY pDeadKey;
    PVSC_LPWSTR pKeyNames;
    PVSC_LPWSTR pKeyNamesExt;
    WCHAR **pKeyNamesDead;
    USHORT *pusVSCtoVK;
    BYTE bMaxVSCtoVK;
    PVSC_VK pVSCtoVK_E0;
    PVSC_VK pVSCtoVK_E1;
    DWORD fLocaleFlags;
    BYTE nLgMax;
    BYTE cbLgEntry;
    PLIGATURE1 pLigature;
    DWORD dwType;
    DWORD dwSubType;
};

struct tagKBDTABLE_DESC
{
    WCHAR wszDllName[32];
    DWORD dwType;
    DWORD dwSubType;
};

typedef struct tagKBDTABLE_DESC KBDTABLE_DESC;

typedef struct tagKBDTABLE_DESC *PKBDTABLE_DESC;

struct tagKBDTABLE_MULTI
{
    UINT nTables;
    KBDTABLE_DESC aKbdTables[8];
};

typedef struct tagKBDTABLE_MULTI KBDTABLE_MULTI;

typedef struct tagKBDTABLE_MULTI *PKBDTABLE_MULTI;

typedef struct _VK_FUNCTION_PARAM VK_FPARAM;

struct _VK_TO_FUNCTION_TABLE
{
    BYTE Vk;
    BYTE NLSFEProcType;
    BYTE NLSFEProcCurrent;
    BYTE NLSFEProcSwitch;
    VK_FPARAM NLSFEProc[8];
    VK_FPARAM NLSFEProcAlt[8];
};

typedef struct _VK_TO_FUNCTION_TABLE *PVK_F;

struct tagKbdNlsLayer
{
    USHORT OEMIdentifier;
    USHORT LayoutInformation;
    UINT NumOfVkToF;
    PVK_F pVkToF;
    INT NumOfMouseVKey;
    USHORT *pusMouseVKey;
};

typedef struct tagKbdNlsLayer KBDNLSTABLES;

typedef struct tagKbdNlsLayer *PKBDNLSTABLES;

typedef struct _VK_TO_FUNCTION_TABLE VK_F;

typedef struct _VK_TO_WCHARS1 VK_TO_WCHARS1;

typedef struct _VK_TO_WCHARS10 VK_TO_WCHARS10;

typedef struct _VK_TO_WCHARS2 VK_TO_WCHARS2;

typedef struct _VK_TO_WCHARS3 VK_TO_WCHARS3;

typedef struct _VK_TO_WCHARS4 VK_TO_WCHARS4;

typedef struct _VK_TO_WCHARS5 VK_TO_WCHARS5;

typedef struct _VK_TO_WCHARS6 VK_TO_WCHARS6;

typedef struct _VK_TO_WCHARS7 VK_TO_WCHARS7;

typedef struct _VK_TO_WCHARS8 VK_TO_WCHARS8;

typedef struct _VK_TO_WCHARS9 VK_TO_WCHARS9;

typedef struct _VK_VSC VK_VSC;

#define X5F 0x5F

#define _EQ(v4) (VK_ ## v4)
#define _NE(v1, v2, v3, v4, v5, v6) (VK_ ## v4)
#define DEADTRANS(ch, accent, comp, flags) { MAKELONG(ch, accent), comp, flags }
#define FUJITSU_KBD_CONSOLE(Id) (JAPANESE_KEYBOARD(Id) && (Id) . Subtype == ((NLSKBD_OEM_FUJITSU << 4) | FMV_KBD_OASYS_TYPE))
#define FUJITSU_KBD_REMOTE(Id) (JAPANESE_KEYBOARD(Id) && (Id) . SubType == 0x00020002)
#define IDS_FROM_SCANCODE(prefix, base) (0xc000 + ((0x ## prefix) >= 0xE0 ? 0x100 : 0) + (0x ## base))
#define JAPANESE_KBD_LAYOUT(hkl) (KBD_LAYOUT_LANG(hkl) == LANG_JAPANESE)
#define JAPANESE_KEYBOARD(Id) ((Id) . Type == KEYBOARD_TYPE_JAPAN)
#define KBD_LAYOUT_LANG(hkl) (PRIMARYLANGID(HandleToUlong(hkl)))
#define KLL_ATTR_FROM_KLF(x) ((x) >> 15)
#define KLL_GLOBAL_ATTR_FROM_KLF(x) (KLL_ATTR_FROM_KLF(x) & KLLF_GLOBAL_ATTRS)
#define KLL_LAYOUT_ATTR_FROM_KLF(x) (KLL_ATTR_FROM_KLF(x) & KLLF_LAYOUT_ATTRS)
#define KOREAN_KBD_LAYOUT(hkl) (KBD_LAYOUT_LANG(hkl) == LANG_KOREAN)
#define KOREAN_KEYBOARD(Id) ((Id) . Type == KEYBOARD_TYPE_KOREA)

#endif /* __KBD_H */
