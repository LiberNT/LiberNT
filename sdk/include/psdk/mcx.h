#ifndef _MCX_H
#define _MCX_H

#ifdef __cplusplus
extern "C" {
#endif
#define DIALOPTION_BILLING 64
#define DIALOPTION_QUIET 128
#define DIALOPTION_DIALTONE 256
#define MDMVOLFLAG_LOW	1
#define MDMVOLFLAG_MEDIUM	2
#define MDMVOLFLAG_HIGH	4
#define MDMVOL_LOW	0
#define MDMVOL_MEDIUM	1
#define MDMVOL_HIGH	2
#define MDMSPKRFLAG_OFF	1
#define MDMSPKRFLAG_DIAL	2
#define MDMSPKRFLAG_ON	4
#define MDMSPKRFLAG_CALLSETUP	8
#define MDMSPKR_OFF	0
#define MDMSPKR_DIAL 1
#define MDMSPKR_ON	2
#define MDMSPKR_CALLSETUP	3
#define MDM_COMPRESSION	1
#define MDM_ERROR_CONTROL	2
#define MDM_FORCED_EC	4
#define MDM_CELLULAR	8
#define MDM_FLOWCONTROL_HARD	16
#define MDM_FLOWCONTROL_SOFT	32
#define MDM_CCITT_OVERRIDE	64
#define MDM_SPEED_ADJUST	128
#define MDM_TONE_DIAL	256
#define MDM_BLIND_DIAL	512
#define MDM_V23_OVERRIDE	1024
typedef struct _MODEMDEVCAPS {
	DWORD dwActualSize;
	DWORD dwRequiredSize;
	DWORD dwDevSpecificOffset;
	DWORD dwDevSpecificSize;
	DWORD dwModemProviderVersion;
	DWORD dwModemManufacturerOffset;
	DWORD dwModemManufacturerSize;
	DWORD dwModemModelOffset;
	DWORD dwModemModelSize;
	DWORD dwModemVersionOffset;
	DWORD dwModemVersionSize;
	DWORD dwDialOptions;
	DWORD dwCallSetupFailTimer;
	DWORD dwInactivityTimeout;
	DWORD dwSpeakerVolume;
	DWORD dwSpeakerMode;
	DWORD dwModemOptions;
	DWORD dwMaxDTERate;
	DWORD dwMaxDCERate;
	BYTE abVariablePortion[1];
} MODEMDEVCAPS,*PMODEMDEVCAPS,*LPMODEMDEVCAPS;
typedef struct _MODEMSETTINGS {
	DWORD dwActualSize;
	DWORD dwRequiredSize;
	DWORD dwDevSpecificOffset;
	DWORD dwDevSpecificSize;
	DWORD dwCallSetupFailTimer;
	DWORD dwInactivityTimeout;
	DWORD dwSpeakerVolume;
	DWORD dwSpeakerMode;
	DWORD dwPreferredModemOptions;
	DWORD dwNegotiatedModemOptions;
	DWORD dwNegotiatedDCERate;
	BYTE abVariablePortion[1];
} MODEMSETTINGS,*PMODEMSETTINGS,*LPMODEMSETTINGS;
#ifdef __cplusplus
}
#endif
#ifndef MDM_ANALOG_RLP_OFF
#define MDM_ANALOG_RLP_OFF 0x1
#endif
#ifndef MDM_ANALOG_RLP_ON
#define MDM_ANALOG_RLP_ON 0x0
#endif
#ifndef MDM_ANALOG_V34
#define MDM_ANALOG_V34 0x2
#endif
#ifndef MDM_AUTO_ML_2
#define MDM_AUTO_ML_2 0x2
#endif
#ifndef MDM_AUTO_ML_DEFAULT
#define MDM_AUTO_ML_DEFAULT 0x0
#endif
#ifndef MDM_AUTO_ML_NONE
#define MDM_AUTO_ML_NONE 0x1
#endif
#ifndef MDM_AUTO_SPEED_DEFAULT
#define MDM_AUTO_SPEED_DEFAULT 0x0
#endif
#ifndef MDM_BEARERMODE_ANALOG
#define MDM_BEARERMODE_ANALOG 0x0
#endif
#ifndef MDM_BEARERMODE_GSM
#define MDM_BEARERMODE_GSM 0x2
#endif
#ifndef MDM_BEARERMODE_ISDN
#define MDM_BEARERMODE_ISDN 0x1
#endif
#ifndef MDM_DIAGNOSTICS
#define MDM_DIAGNOSTICS 0x00000800
#endif
#ifndef MDM_HDLCPPP_AUTH_CHAP
#define MDM_HDLCPPP_AUTH_CHAP 0x3
#endif
#ifndef MDM_HDLCPPP_AUTH_DEFAULT
#define MDM_HDLCPPP_AUTH_DEFAULT 0x0
#endif
#ifndef MDM_HDLCPPP_AUTH_MSCHAP
#define MDM_HDLCPPP_AUTH_MSCHAP 0x4
#endif
#ifndef MDM_HDLCPPP_AUTH_NONE
#define MDM_HDLCPPP_AUTH_NONE 0x1
#endif
#ifndef MDM_HDLCPPP_AUTH_PAP
#define MDM_HDLCPPP_AUTH_PAP 0x2
#endif
#ifndef MDM_HDLCPPP_ML_2
#define MDM_HDLCPPP_ML_2 0x2
#endif
#ifndef MDM_HDLCPPP_ML_DEFAULT
#define MDM_HDLCPPP_ML_DEFAULT 0x0
#endif
#ifndef MDM_HDLCPPP_ML_NONE
#define MDM_HDLCPPP_ML_NONE 0x1
#endif
#ifndef MDM_HDLCPPP_SPEED_56K
#define MDM_HDLCPPP_SPEED_56K 0x2
#endif
#ifndef MDM_HDLCPPP_SPEED_64K
#define MDM_HDLCPPP_SPEED_64K 0x1
#endif
#ifndef MDM_HDLCPPP_SPEED_DEFAULT
#define MDM_HDLCPPP_SPEED_DEFAULT 0x0
#endif
#ifndef MDM_MASK_AUTO_SPEED
#define MDM_MASK_AUTO_SPEED 0x7
#endif
#ifndef MDM_MASK_BEARERMODE
#define MDM_MASK_BEARERMODE 0x0000f000
#endif
#ifndef MDM_MASK_HDLCPPP_SPEED
#define MDM_MASK_HDLCPPP_SPEED 0x7
#endif
#ifndef MDM_MASK_PROTOCOLDATA
#define MDM_MASK_PROTOCOLDATA 0x0ff00000
#endif
#ifndef MDM_MASK_PROTOCOLID
#define MDM_MASK_PROTOCOLID 0x000f0000
#endif
#ifndef MDM_MASK_V110_SPEED
#define MDM_MASK_V110_SPEED 0xf
#endif
#ifndef MDM_MASK_V120_SPEED
#define MDM_MASK_V120_SPEED 0x7
#endif
#ifndef MDM_MASK_X75_DATA
#define MDM_MASK_X75_DATA 0x7
#endif
#ifndef MDM_PIAFS_INCOMING
#define MDM_PIAFS_INCOMING 0
#endif
#ifndef MDM_PIAFS_OUTGOING
#define MDM_PIAFS_OUTGOING 1
#endif
#ifndef MDM_PROTOCOLID_ANALOG
#define MDM_PROTOCOLID_ANALOG 0x7
#endif
#ifndef MDM_PROTOCOLID_AUTO
#define MDM_PROTOCOLID_AUTO 0x6
#endif
#ifndef MDM_PROTOCOLID_DEFAULT
#define MDM_PROTOCOLID_DEFAULT 0x0
#endif
#ifndef MDM_PROTOCOLID_GPRS
#define MDM_PROTOCOLID_GPRS 0x8
#endif
#ifndef MDM_PROTOCOLID_HDLCPPP
#define MDM_PROTOCOLID_HDLCPPP 0x1
#endif
#ifndef MDM_PROTOCOLID_PIAFS
#define MDM_PROTOCOLID_PIAFS 0x9
#endif
#ifndef MDM_PROTOCOLID_V110
#define MDM_PROTOCOLID_V110 0x4
#endif
#ifndef MDM_PROTOCOLID_V120
#define MDM_PROTOCOLID_V120 0x5
#endif
#ifndef MDM_PROTOCOLID_V128
#define MDM_PROTOCOLID_V128 0x2
#endif
#ifndef MDM_PROTOCOLID_X75
#define MDM_PROTOCOLID_X75 0x3
#endif
#ifndef MDM_SHIFT_AUTO_ML
#define MDM_SHIFT_AUTO_ML 0x6
#endif
#ifndef MDM_SHIFT_AUTO_SPEED
#define MDM_SHIFT_AUTO_SPEED 0x0
#endif
#ifndef MDM_SHIFT_BEARERMODE
#define MDM_SHIFT_BEARERMODE 12
#endif
#ifndef MDM_SHIFT_HDLCPPP_AUTH
#define MDM_SHIFT_HDLCPPP_AUTH 0x3
#endif
#ifndef MDM_SHIFT_HDLCPPP_ML
#define MDM_SHIFT_HDLCPPP_ML 0x6
#endif
#ifndef MDM_SHIFT_HDLCPPP_SPEED
#define MDM_SHIFT_HDLCPPP_SPEED 0x0
#endif
#ifndef MDM_SHIFT_PROTOCOLDATA
#define MDM_SHIFT_PROTOCOLDATA 20
#endif
#ifndef MDM_SHIFT_PROTOCOLID
#define MDM_SHIFT_PROTOCOLID 16
#endif
#ifndef MDM_SHIFT_V110_SPEED
#define MDM_SHIFT_V110_SPEED 0x0
#endif
#ifndef MDM_SHIFT_V120_ML
#define MDM_SHIFT_V120_ML 0x6
#endif
#ifndef MDM_SHIFT_V120_SPEED
#define MDM_SHIFT_V120_SPEED 0x0
#endif
#ifndef MDM_SHIFT_X75_DATA
#define MDM_SHIFT_X75_DATA 0x0
#endif
#ifndef MDM_V110_SPEED_12DOT0K
#define MDM_V110_SPEED_12DOT0K 0x5
#endif
#ifndef MDM_V110_SPEED_14DOT4K
#define MDM_V110_SPEED_14DOT4K 0x6
#endif
#ifndef MDM_V110_SPEED_19DOT2K
#define MDM_V110_SPEED_19DOT2K 0x7
#endif
#ifndef MDM_V110_SPEED_1DOT2K
#define MDM_V110_SPEED_1DOT2K 0x1
#endif
#ifndef MDM_V110_SPEED_28DOT8K
#define MDM_V110_SPEED_28DOT8K 0x8
#endif
#ifndef MDM_V110_SPEED_2DOT4K
#define MDM_V110_SPEED_2DOT4K 0x2
#endif
#ifndef MDM_V110_SPEED_38DOT4K
#define MDM_V110_SPEED_38DOT4K 0x9
#endif
#ifndef MDM_V110_SPEED_4DOT8K
#define MDM_V110_SPEED_4DOT8K 0x3
#endif
#ifndef MDM_V110_SPEED_57DOT6K
#define MDM_V110_SPEED_57DOT6K 0xA
#endif
#ifndef MDM_V110_SPEED_9DOT6K
#define MDM_V110_SPEED_9DOT6K 0x4
#endif
#ifndef MDM_V110_SPEED_DEFAULT
#define MDM_V110_SPEED_DEFAULT 0x0
#endif
#ifndef MDM_V120_ML_2
#define MDM_V120_ML_2 0x2
#endif
#ifndef MDM_V120_ML_DEFAULT
#define MDM_V120_ML_DEFAULT 0x0
#endif
#ifndef MDM_V120_ML_NONE
#define MDM_V120_ML_NONE 0x1
#endif
#ifndef MDM_V120_SPEED_56K
#define MDM_V120_SPEED_56K 0x2
#endif
#ifndef MDM_V120_SPEED_64K
#define MDM_V120_SPEED_64K 0x1
#endif
#ifndef MDM_V120_SPEED_DEFAULT
#define MDM_V120_SPEED_DEFAULT 0x0
#endif
#ifndef MDM_X75_DATA_128K
#define MDM_X75_DATA_128K 0x2
#endif
#ifndef MDM_X75_DATA_64K
#define MDM_X75_DATA_64K 0x1
#endif
#ifndef MDM_X75_DATA_BTX
#define MDM_X75_DATA_BTX 0x4
#endif
#ifndef MDM_X75_DATA_DEFAULT
#define MDM_X75_DATA_DEFAULT 0x0
#endif
#ifndef MDM_X75_DATA_T_70
#define MDM_X75_DATA_T_70 0x3
#endif

#endif /* _MCX_H */
