/*
 * TAPI definitions
 *
 * Copyright (c) 1999 Andreas Mohr
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_TAPI_H
#define __WINE_TAPI_H

#include <windows.h>
#include <basetsd.h>
#include <oaidl.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

typedef HANDLE HCALL, *LPHCALL;
typedef HANDLE HLINE, *LPHLINE;
typedef HANDLE HLINEAPP, *LPHLINEAPP;
typedef HANDLE HPHONE, *LPHPHONE;
typedef HANDLE HPHONEAPP, *LPHPHONEAPP;

#define TAPIERR_REQUESTFAILED          __MSABI_LONG(-16)
#define LINEERR_ALLOCATED              0x80000001
#define LINEERR_BADDEVICEID            0x80000002
#define LINEERR_BEARERMODEUNAVAIL      0x80000003
#define LINEERR_CALLUNAVAIL            0x80000005
#define LINEERR_COMPLETIONOVERRUN      0x80000006
#define LINEERR_CONFERENCEFULL         0x80000007
#define LINEERR_DIALBILLING            0x80000008
#define LINEERR_DIALDIALTONE           0x80000009
#define LINEERR_DIALPROMPT             0x8000000A
#define LINEERR_DIALQUIET              0x8000000B
#define LINEERR_INCOMPATIBLEAPIVERSION 0x8000000C
#define LINEERR_INCOMPATIBLEEXTVERSION 0x8000000D
#define LINEERR_INIFILECORRUPT         0x8000000E
#define LINEERR_INUSE                  0x8000000F
#define LINEERR_INVALADDRESS           0x80000010
#define LINEERR_INVALADDRESSID         0x80000011
#define LINEERR_INVALADDRESSMODE       0x80000012
#define LINEERR_INVALADDRESSSTATE      0x80000013
#define LINEERR_INVALAPPHANDLE         0x80000014
#define LINEERR_INVALAPPNAME           0x80000015
#define LINEERR_INVALBEARERMODE        0x80000016
#define LINEERR_INVALCALLCOMPLMODE     0x80000017
#define LINEERR_INVALCALLHANDLE        0x80000018
#define LINEERR_INVALCALLPARAMS        0x80000019
#define LINEERR_INVALCALLPRIVILEGE     0x8000001A
#define LINEERR_INVALCALLSELECT        0x8000001B
#define LINEERR_INVALCALLSTATE         0x8000001C
#define LINEERR_INVALCALLSTATELIST     0x8000001D
#define LINEERR_INVALCARD              0x8000001E
#define LINEERR_INVALCOMPLETIONID      0x8000001F
#define LINEERR_INVALCONFCALLHANDLE    0x80000020
#define LINEERR_INVALCONSULTCALLHANDLE 0x80000021
#define LINEERR_INVALCOUNTRYCODE       0x80000022
#define LINEERR_INVALDEVICECLASS       0x80000023
#define LINEERR_INVALDEVICEHANDLE      0x80000024
#define LINEERR_INVALDIALPARAMS        0x80000025
#define LINEERR_INVALDIGITLIST         0x80000026
#define LINEERR_INVALDIGITMODE         0x80000027
#define LINEERR_INVALDIGITS            0x80000028
#define LINEERR_INVALEXTVERSION        0x80000029
#define LINEERR_INVALGROUPID           0x8000002A
#define LINEERR_INVALLINEHANDLE        0x8000002B
#define LINEERR_INVALLINESTATE         0x8000002C
#define LINEERR_INVALLOCATION          0x8000002D
#define LINEERR_INVALMEDIALIST         0x8000002E
#define LINEERR_INVALMEDIAMODE         0x8000002F
#define LINEERR_INVALMESSAGEID         0x80000030
#define LINEERR_INVALPARAM             0x80000032
#define LINEERR_INVALPARKID            0x80000033
#define LINEERR_INVALPARKMODE          0x80000034
#define LINEERR_INVALPOINTER           0x80000035
#define LINEERR_INVALPRIVSELECT        0x80000036
#define LINEERR_INVALRATE              0x80000037
#define LINEERR_INVALREQUESTMODE       0x80000038
#define LINEERR_INVALTERMINALID        0x80000039
#define LINEERR_INVALTERMINALMODE      0x8000003A
#define LINEERR_INVALTIMEOUT           0x8000003B
#define LINEERR_INVALTONE              0x8000003C
#define LINEERR_INVALTONELIST          0x8000003D
#define LINEERR_INVALTONEMODE          0x8000003E
#define LINEERR_INVALTRANSFERMODE      0x8000003F
#define LINEERR_LINEMAPPERFAILED       0x80000040
#define LINEERR_NOCONFERENCE           0x80000041
#define LINEERR_NODEVICE               0x80000042
#define LINEERR_NODRIVER               0x80000043
#define LINEERR_NOMEM                  0x80000044
#define LINEERR_NOREQUEST              0x80000045
#define LINEERR_NOTOWNER               0x80000046
#define LINEERR_NOTREGISTERED          0x80000047
#define LINEERR_OPERATIONFAILED        0x80000048
#define LINEERR_OPERATIONUNAVAIL       0x80000049
#define LINEERR_RATEUNAVAIL            0x8000004A
#define LINEERR_RESOURCEUNAVAIL        0x8000004B
#define LINEERR_REQUESTOVERRUN         0x8000004C
#define LINEERR_STRUCTURETOOSMALL      0x8000004D
#define LINEERR_TARGETNOTFOUND         0x8000004E
#define LINEERR_TARGETSELF             0x8000004F
#define LINEERR_UNINITIALIZED          0x80000050
#define LINEERR_USERUSERINFOTOOBIG     0x80000051
#define LINEERR_REINIT                 0x80000052
#define LINEERR_ADDRESSBLOCKED         0x80000053
#define LINEERR_BILLINGREJECTED        0x80000054
#define LINEERR_INVALFEATURE           0x80000055
#define LINEERR_NOMULTIPLEINSTANCE     0x80000056

#define STRINGFORMAT_ASCII             0x00000001
#define STRINGFORMAT_DBCS              0x00000002
#define STRINGFORMAT_UNICODE           0x00000003
#define STRINGFORMAT_BINARY            0x00000004

#define LINEADDRESSMODE_ADDRESSID      0x00000001
#define LINEADDRESSMODE_DIALABLEADDR   0x00000002

#define LINEBEARERMODE_VOICE           0x00000001
#define LINEBEARERMODE_SPEECH          0x00000002
#define LINEBEARERMODE_MULTIUSE        0x00000004
#define LINEBEARERMODE_DATA            0x00000008
#define LINEBEARERMODE_ALTSPEECHDATA   0x00000010
#define LINEBEARERMODE_NONCALLSIGNALING 0x00000020
#define LINEBEARERMODE_PASSTHROUGH     0x00000040

#define LINEMEDIAMODE_UNKNOWN          0x00000002
#define LINEMEDIAMODE_INTERACTIVEVOICE 0x00000004
#define LINEMEDIAMODE_AUTOMATEDVOICE   0x00000008
#define LINEMEDIAMODE_DATAMODEM        0x00000010

#define LINEDEVCAPFLAGS_CROSSADDRCONF  0x00000001
#define LINEDEVCAPFLAGS_HIGHLEVCOMP    0x00000002
#define LINEDEVCAPFLAGS_LOWLEVCOMP     0x00000004
#define LINEDEVCAPFLAGS_MEDIACONTROL   0x00000008
#define LINEDEVCAPFLAGS_MULTIPLEADDR   0x00000010
#define LINEDEVCAPFLAGS_CLOSEDROP      0x00000020
#define LINEDEVCAPFLAGS_DIALBILLING    0x00000040
#define LINEDEVCAPFLAGS_DIALQUIET      0x00000080
#define LINEDEVCAPFLAGS_DIALDIALTONE   0x00000100

#define LINEDEVSTATE_OTHER             0x00000001
#define LINEDEVSTATE_RINGING           0x00000002
#define LINEDEVSTATE_CONNECTED         0x00000004
#define LINEDEVSTATE_DISCONNECTED      0x00000008
#define LINEDEVSTATE_MSGWAITON         0x00000010
#define LINEDEVSTATE_MSGWAITOFF        0x00000020
#define LINEDEVSTATE_INSERVICE         0x00000040
#define LINEDEVSTATE_OUTOFSERVICE      0x00000080
#define LINEDEVSTATE_MAINTENANCE       0x00000100
#define LINEDEVSTATE_OPEN              0x00000200
#define LINEDEVSTATE_CLOSE             0x00000400
#define LINEDEVSTATE_NUMCALLS          0x00000800
#define LINEDEVSTATE_NUMCOMPLETIONS    0x00001000
#define LINEDEVSTATE_TERMINALS         0x00002000
#define LINEDEVSTATE_ROAMMODE          0x00004000
#define LINEDEVSTATE_BATTERY           0x00008000
#define LINEDEVSTATE_SIGNAL            0x00010000
#define LINEDEVSTATE_DEVSPECIFIC       0x00020000
#define LINEDEVSTATE_REINIT            0x00040000
#define LINEDEVSTATE_LOCK              0x00080000

#define LINEDEVSTATE_CAPSCHANGE        0x00100000
#define LINEDEVSTATE_CONFIGCHANGE      0x00200000
#define LINEDEVSTATE_TRANSLATECHANGE   0x00400000
#define LINEDEVSTATE_COMPLCANCEL       0x00800000
#define LINEDEVSTATE_REMOVED           0x01000000

#define LINELOCATIONOPTION_PULSEDIAL   0x00000001


#define LINEFEATURE_DEVSPECIFIC        0x00000001
#define LINEFEATURE_DEVSPECIFICFEAT    0x00000002
#define LINEFEATURE_FORWARD            0x00000004
#define LINEFEATURE_MAKECALL           0x00000008
#define LINEFEATURE_SETMEDIACONTROL    0x00000010
#define LINEFEATURE_SETTERMINAL        0x00000020
#define LINEFEATURE_SETDEVSTATUS       0x00000040
#define LINEFEATURE_FORWARDFWD         0x00000080
#define LINEFEATURE_FORWARDDND         0x00000100

/* tapi callback messages */

#define LINE_ADDRESSSTATE             __MSABI_LONG(0)
#define LINE_CALLINFO                 __MSABI_LONG(1)
#define LINE_CALLSTATE                __MSABI_LONG(2)
#define LINE_CLOSE                    __MSABI_LONG(3)
#define LINE_DEVSPECIFIC              __MSABI_LONG(4)
#define LINE_DEVSPECIFICFEATURE       __MSABI_LONG(5)
#define LINE_GATHERDIGITS             __MSABI_LONG(6)
#define LINE_GENERATE                 __MSABI_LONG(7)
#define LINE_LINEDEVSTATE             __MSABI_LONG(8)
#define LINE_MONITORDIGITS            __MSABI_LONG(9)
#define LINE_MONITORMEDIA             __MSABI_LONG(10)
#define LINE_MONITORTONE              __MSABI_LONG(11)
#define LINE_REPLY                    __MSABI_LONG(12)
#define LINE_REQUEST                  __MSABI_LONG(13)
#define PHONE_BUTTON                  __MSABI_LONG(14)
#define PHONE_CLOSE                   __MSABI_LONG(15)
#define PHONE_DEVSPECIFIC             __MSABI_LONG(16)
#define PHONE_REPLY                   __MSABI_LONG(17)
#define PHONE_STATE                   __MSABI_LONG(18)
#define LINE_CREATE                   __MSABI_LONG(19)
#define PHONE_CREATE                  __MSABI_LONG(20)

#define LINE_AGENTSPECIFIC            __MSABI_LONG(21)
#define LINE_AGENTSTATUS              __MSABI_LONG(22)
#define LINE_APPNEWCALL               __MSABI_LONG(23)
#define LINE_PROXYREQUEST             __MSABI_LONG(24)
#define LINE_REMOVE                   __MSABI_LONG(25)
#define PHONE_REMOVE                  __MSABI_LONG(26)
/* these are used as Param1 of line_callstate messages */
#define LINECALLSTATE_IDLE            0x00000001
#define LINECALLSTATE_OFFERING        0x00000002
#define LINECALLSTATE_ACCEPTED        0x00000004
#define LINECALLSTATE_DIALTONE        0x00000008
#define LINECALLSTATE_DIALING         0x00000010
#define LINECALLSTATE_RINGBACK        0x00000020
#define LINECALLSTATE_BUSY            0x00000040
#define LINECALLSTATE_SPECIALINFO     0x00000080
#define LINECALLSTATE_CONNECTED       0x00000100
#define LINECALLSTATE_PROCEEDING      0x00000200
#define LINECALLSTATE_ONHOLD          0x00000400
#define LINECALLSTATE_CONFERENCED     0x00000800
#define LINECALLSTATE_ONHOLDPENDCONF  0x00001000
#define LINECALLSTATE_ONHOLDPENDTRANSFER 0x00002000
#define LINECALLSTATE_DISCONNECTED    0x00004000
#define LINECALLSTATE_UNKNOWN         0x00008000
#define LINECONNECTEDMODE_ACTIVE            0x00000001
#define LINECONNECTEDMODE_INACTIVE          0x00000002
#define LINECONNECTEDMODE_ACTIVEHELD        0x00000004
#define LINECONNECTEDMODE_INACTIVEHELD      0x00000008
#define LINECONNECTEDMODE_CONFIRMED         0x00000010

/* these are Param2 values for state_disconnected line_callstate messages */
#define LINEDISCONNECTMODE_NORMAL           0x00000001
#define LINEDISCONNECTMODE_UNKNOWN          0x00000002
#define LINEDISCONNECTMODE_REJECT           0x00000004
#define LINEDISCONNECTMODE_PICKUP           0x00000008
#define LINEDISCONNECTMODE_FORWARDED        0x00000010
#define LINEDISCONNECTMODE_BUSY             0x00000020
#define LINEDISCONNECTMODE_NOANSWER         0x00000040
#define LINEDISCONNECTMODE_BADADDRESS       0x00000080
#define LINEDISCONNECTMODE_UNREACHABLE      0x00000100
#define LINEDISCONNECTMODE_CONGESTION       0x00000200
#define LINEDISCONNECTMODE_INCOMPATIBLE     0x00000400
#define LINEDISCONNECTMODE_UNAVAIL          0x00000800
#define LINEDISCONNECTMODE_NODIALTONE       0x00001000
#define LINEDISCONNECTMODE_NUMBERCHANGED    0x00002000
#define LINEDISCONNECTMODE_OUTOFORDER       0x00004000
#define LINEDISCONNECTMODE_TEMPFAILURE      0x00008000
#define LINEDISCONNECTMODE_QOSUNAVAIL       0x00010000
#define LINEDISCONNECTMODE_BLOCKED          0x00020000
#define LINEDISCONNECTMODE_DONOTDISTURB     0x00040000
#define LINEDISCONNECTMODE_CANCELLED        0x00080000

#define LINECALLSELECT_LINE                 0x00000001
#define LINECALLSELECT_ADDRESS              0x00000002
#define LINECALLSELECT_CALL                 0x00000004
#define LINECALLSELECT_DEVICEID             0x00000008

#define LINECALLPRIVILEGE_NONE              0x00000001
#define LINECALLPRIVILEGE_MONITOR           0x00000002
#define LINECALLPRIVILEGE_OWNER             0x00000004
#define LINECALLFEATURE_ACCEPT              0x00000001
#define LINECALLFEATURE_ADDTOCONF           0x00000002
#define LINECALLFEATURE_ANSWER              0x00000004
#define LINECALLFEATURE_BLINDTRANSFER       0x00000008
#define LINECALLFEATURE_COMPLETECALL        0x00000010
#define LINECALLFEATURE_COMPLETETRANSF      0x00000020
#define LINECALLFEATURE_DIAL                0x00000040
#define LINECALLFEATURE_DROP                0x00000080
#define LINECALLFEATURE_GATHERDIGITS        0x00000100
#define LINECALLFEATURE_GENERATEDIGITS      0x00000200
#define LINECALLFEATURE_GENERATETONE        0x00000400
#define LINECALLFEATURE_HOLD                0x00000800
#define LINECALLFEATURE_MONITORDIGITS       0x00001000
#define LINECALLFEATURE_MONITORMEDIA        0x00002000
#define LINECALLFEATURE_MONITORTONES        0x00004000
#define LINECALLFEATURE_PARK                0x00008000
#define LINECALLFEATURE_PREPAREADDCONF      0x00010000
#define LINECALLFEATURE_REDIRECT            0x00020000
#define LINECALLFEATURE_REMOVEFROMCONF      0x00040000
#define LINECALLFEATURE_SECURECALL          0x00080000
#define LINECALLFEATURE_SENDUSERUSER        0x00100000
#define LINECALLFEATURE_SETCALLPARAMS       0x00200000
#define LINECALLFEATURE_SETMEDIACONTROL     0x00400000
#define LINECALLFEATURE_SETTERMINAL         0x00800000
#define LINECALLFEATURE_SETUPCONF           0x01000000
#define LINECALLFEATURE_SETUPTRANSFER       0x02000000
#define LINECALLFEATURE_SWAPHOLD            0x04000000
#define LINECALLFEATURE_UNHOLD              0x08000000
#define LINECALLFEATURE_RELEASEUSERUSERINFO 0x10000000

#define LINEINITIALIZEEXOPTION_USEHIDDENWINDOW   0x00000001
#define LINEINITIALIZEEXOPTION_USEEVENT          0x00000002
#define LINEINITIALIZEEXOPTION_USECOMPLETIONPORT 0x00000003
#define LINEINITIALIZEEXOPTION_CALLHUBTRACKING   0x80000000

typedef struct lineaddresscaps_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwLineDeviceID;
    DWORD dwAddressSize;
    DWORD dwAddressOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
    DWORD dwAddressSharing;
    DWORD dwAddressStates;
    DWORD dwCallInfoStates;
    DWORD dwCallerIDFlags;
    DWORD dwCalledIDFlags;
    DWORD dwConnectedIDFlags;
    DWORD dwRedirectionIDFlags;
    DWORD dwRedirectingIDFlags;
    DWORD dwCallStates;
    DWORD dwDialToneModes;
    DWORD dwBusyModes;
    DWORD dwSpecialInfo;
    DWORD dwDisconnectModes;
    DWORD dwMaxNumActiveCalls;
    DWORD dwMaxNumOnHoldCalls;
    DWORD dwMaxNumOnHoldPendingCalls;
    DWORD dwMaxNumConference;
    DWORD dwMaxNumTransConf;
    DWORD dwAddrCapFlags;
    DWORD dwCallFeatures;
    DWORD dwRemoveFromConfCaps;
    DWORD dwRemoveFromConfState;
    DWORD dwTransferModes;
    DWORD dwParkModes;
    DWORD dwForwardModes;
    DWORD dwMaxForwardEntries;
    DWORD dwMaxSpecificEntries;
    DWORD dwMinFwdNumRings;
    DWORD dwMaxFwdNumRings;
    DWORD dwMaxCallCompletions;
    DWORD dwCallCompletionConds;
    DWORD dwCallCompletionModes;
    DWORD dwNumCompletionMessages;
    DWORD dwCompletionMsgTextEntrySize;
    DWORD dwCompletionMsgTextSize;
    DWORD dwCompletionMsgTextOffset;
    DWORD dwAddressFeatures;
} LINEADDRESSCAPS, *LPLINEADDRESSCAPS;

typedef struct lineaddressstatus_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwNumInUse;
    DWORD dwNumActiveCalls;
    DWORD dwNumOnHoldCalls;
    DWORD dwNumOnHoldPendCalls;
    DWORD dwAddressFeatures;
    DWORD dwNumRingsNoAnswer;
    DWORD dwForwardNumEntries;
    DWORD dwForwardSize;
    DWORD dwForwardOffset;
    DWORD dwTerminalModesSize;
    DWORD dwTerminalModesOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} LINEADDRESSSTATUS, *LPLINEADDRESSSTATUS;

typedef struct linedialparams_tag {
    DWORD dwDialPause;
    DWORD dwDialSpeed;
    DWORD dwDigitDuration;
    DWORD dwWaitForDialtone;
} LINEDIALPARAMS, *LPLINEDIALPARAMS;

typedef struct linecallinfo_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    HLINE hLine;
    DWORD dwLineDeviceID;
    DWORD dwAddressID;
    DWORD dwBearerMode;
    DWORD dwRate;
    DWORD dwMediaMode;
    DWORD dwAppSpecific;
    DWORD dwCallID;
    DWORD dwRelatedCallID;
    DWORD dwCallParamFlags;
    DWORD dwCallStates;
    DWORD dwMonitorDigitModes;
    DWORD dwMonitorMediaModes;
    LINEDIALPARAMS DialParams;
    DWORD dwOrigin;
    DWORD dwReason;
    DWORD dwCompletionID;
    DWORD dwNumOwners;
    DWORD dwNumMonitors;
    DWORD dwCountryCode;
    DWORD dwTrunk;
    DWORD dwCallerIDFlags;
    DWORD dwCallerIDSize;
    DWORD dwCallerIDOffset;
    DWORD dwCallerIDNameSize;
    DWORD dwCallerIDNameOffset;
    DWORD dwCalledIDFlags;
    DWORD dwCalledIDSize;
    DWORD dwCalledIDOffset;
    DWORD dwCalledIDNameSize;
    DWORD dwCalledIDNameOffset;
    DWORD dwConnectedIDFlags;
    DWORD dwConnectedIDSize;
    DWORD dwConnectedIDOffset;
    DWORD dwConnectedIDNameSize;
    DWORD dwConnectedIDNameOffset;
    DWORD dwRedirectionIDFlags;
    DWORD dwRedirectionIDSize;
    DWORD dwRedirectionIDOffset;
    DWORD dwRedirectionIDNameSize;
    DWORD dwRedirectionIDNameOffset;
    DWORD dwRedirectingIDFlags;
    DWORD dwRedirectingIDSize;
    DWORD dwRedirectingIDOffset;
    DWORD dwRedirectingIDNameSize;
    DWORD dwRedirectingIDNameOffset;
    DWORD dwAppNameSize;
    DWORD dwAppNameOffset;
    DWORD dwDisplayableAddressSize;
    DWORD dwDisplayableAddressOffset;
    DWORD dwCalledPartySize;
    DWORD dwCalledPartyOffset;
    DWORD dwCommentSize;
    DWORD dwCommentOffset;
    DWORD dwDisplaySize;
    DWORD dwDisplayOffset;
    DWORD dwUserUserInfoSize;
    DWORD dwUserUserInfoOffset;
    DWORD dwHighLevelCompSize;
    DWORD dwHighLevelCompOffset;
    DWORD dwLowLevelCompSize;
    DWORD dwLowLevelCompOffset;
    DWORD dwChargingInfoSize;
    DWORD dwChargingInfoOffset;
    DWORD dwTerminalModesSize;
    DWORD dwTerminalModesOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} LINECALLINFO, *LPLINECALLINFO;

typedef struct linecalllist_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwCallsNumEntries;
    DWORD dwCallsSize;
    DWORD dwCallsOffset;
} LINECALLLIST, *LPLINECALLLIST;

typedef struct linecallparams_tag {
    DWORD dwTotalSize;
    DWORD dwBearerMode;
    DWORD dwMinRate;
    DWORD dwMaxRate;
    DWORD dwMediaMode;
    DWORD dwCallParamFlags;
    DWORD dwAddressMode;
    DWORD dwAddressID;
    LINEDIALPARAMS DialParams;
    DWORD dwOrigAddressSize;
    DWORD dwOrigAddressOffset;
    DWORD dwDisplayableAddressSize;
    DWORD dwDisplayableAddressOffset;
    DWORD dwCalledPartySize;
    DWORD dwCalledPartyOffset;
    DWORD dwCommentSize;
    DWORD dwCommentOffset;
    DWORD dwUserUserInfoSize;
    DWORD dwUserUserInfoOffset;
    DWORD dwHighLevelCompSize;
    DWORD dwHighLevelCompOffset;
    DWORD dwLowLevelCompSize;
    DWORD dwLowLevelCompOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} LINECALLPARAMS, *LPLINECALLPARAMS;

typedef struct linecallstatus_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwCallState;
    DWORD dwCallStateMode;
    DWORD dwCallPrivilege;
    DWORD dwCallFeatures;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} LINECALLSTATUS, *LPLINECALLSTATUS;

typedef struct linecountrylist_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwNumCountries;
    DWORD dwCountryListSize;
    DWORD dwCountryListOffset;
} LINECOUNTRYLIST, *LPLINECOUNTRYLIST;

typedef struct linecountryentry_tag {
    DWORD dwCountryID;
    DWORD dwCountryCode;
    DWORD dwNextCountryID;
    DWORD dwCountryNameSize;
    DWORD dwCountryNameOffset;
    DWORD dwSameAreaRuleSize;
    DWORD dwSameAreaRuleOffset;
    DWORD dwLongDistanceRuleSize;
    DWORD dwLongDistanceRuleOffset;
    DWORD dwInternationalRuleSize;
    DWORD dwInternationalRuleOffset;
} LINECOUNTRYENTRY, *LPLINECOUNTRYENTRY;

typedef struct linedevcaps_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwProviderInfoSize;
    DWORD dwProviderInfoOffset;
    DWORD dwSwitchInfoSize;
    DWORD dwSwitchInfoOffset;
    DWORD dwPermanentLineID;
    DWORD dwLineNameSize;
    DWORD dwLineNameOffset;
    DWORD dwStringFormat;
    DWORD dwAddressModes;
    DWORD dwNumAddresses;
    DWORD dwBearerModes;
    DWORD dwMaxRate;
    DWORD dwMediaModes;
    DWORD dwGenerateToneModes;
    DWORD dwGenerateToneMaxNumFreq;
    DWORD dwGenerateDigitModes;
    DWORD dwMonitorToneMaxNumFreq;
    DWORD dwMonitorToneMaxNumEntries;
    DWORD dwMonitorDigitModes;
    DWORD dwGatherDigitsMinTimeout;
    DWORD dwGatherDigitsMaxTimeout;
    DWORD dwMedCtlDigitMaxListSize;
    DWORD dwMedCtlMediaMaxListSize;
    DWORD dwMedCtlToneMaxListSize;
    DWORD dwMedCtlCallStateMaxListSize;
    DWORD dwDevCapFlags;
    DWORD dwMaxNumActiveCalls;
    DWORD dwAnswerMode;
    DWORD dwRingModes;
    DWORD dwLineStates;
    DWORD dwUUIAcceptSize;
    DWORD dwUUIAnswerSize;
    DWORD dwUUIMakeCallSize;
    DWORD dwUUIDropSize;
    DWORD dwUUISendUserUserInfoSize;
    DWORD dwUUICallInfoSize;
    LINEDIALPARAMS MinDialParams;
    LINEDIALPARAMS MaxDialParams;
    LINEDIALPARAMS DefaultDialParams;
    DWORD dwNumTerminals;
    DWORD dwTerminalCapsSize;
    DWORD dwTerminalCapsOffset;
    DWORD dwTerminalTextEntrySize;
    DWORD dwTerminalTextSize;
    DWORD dwTerminalTextOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
    DWORD dwLineFeatures;
} LINEDEVCAPS, *LPLINEDEVCAPS;

typedef struct linedevstatus_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwNumOpens;
    DWORD dwOpenMediaModes;
    DWORD dwNumActiveCalls;
    DWORD dwNumOnHoldCalls;
    DWORD dwNumOnHoldPendCalls;
    DWORD dwLineFeatures;
    DWORD dwNumCallCompletions;
    DWORD dwRingMode;
    DWORD dwSignalLevel;
    DWORD dwBatteryLevel;
    DWORD dwRoamMode;
    DWORD dwDevStatusFlags;
    DWORD dwTerminalModesSize;
    DWORD dwTerminalModesOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} LINEDEVSTATUS, *LPLINEDEVSTATUS;

typedef struct lineextensionid_tag {
    DWORD dwExtensionID0;
    DWORD dwExtensionID1;
    DWORD dwExtensionID2;
    DWORD dwExtensionID3;
} LINEEXTENSIONID, *LPLINEEXTENSIONID;

typedef struct lineforward_tag {
    DWORD dwForwardMode;
    DWORD dwCallerAddressSize;
    DWORD dwCallerAddressOffset;
    DWORD dwDestCountryCode;
    DWORD dwDestAddressSize;
    DWORD dwDestAddressOffset;
} LINEFORWARD, *LPLINEFORWARD;

typedef struct lineforwardlist_tag {
    DWORD dwTotalSize;
    DWORD dwNumEntries;
    LINEFORWARD ForwardList[1];
} LINEFORWARDLIST, *LPLINEFORWARDLIST;

typedef struct linegeneratetone_tag {
    DWORD dwFrequency;
    DWORD dwCadenceOn;
    DWORD dwCadenceOff;
    DWORD dwVolume;
} LINEGENERATETONE, *LPLINEGENERATETONE;

typedef struct lineinitializeexparams_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwOptions;
    union {
    HANDLE hEvent;
    HANDLE hCompletionPort;
    } Handles;
    DWORD dwCompletionKey;
} LINEINITIALIZEEXPARAMS, *LPLINEINITIALIZEEXPARAMS;

typedef struct linemediacontrolcallstate_tag {
    DWORD dwCallStates;
    DWORD dwMediaControl;
} LINEMEDIACONTROLCALLSTATE, *LPLINEMEDIACONTROLCALLSTATE;

typedef struct linemediacontroldigit_tag {
    DWORD dwDigit;
    DWORD dwDigitModes;
    DWORD dwMediaControl;
} LINEMEDIACONTROLDIGIT, *LPLINEMEDIACONTROLDIGIT;

typedef struct linemediacontrolmedia_tag {
    DWORD dwMediaModes;
    DWORD dwDuration;
    DWORD dwMediaControl;
} LINEMEDIACONTROLMEDIA, *LPLINEMEDIACONTROLMEDIA;

typedef struct linemediacontroltone_tag {
    DWORD dwAppSpecific;
    DWORD dwDuration;
    DWORD dwFrequency1;
    DWORD dwFrequency2;
    DWORD dwFrequency3;
    DWORD dwMediaControl;
} LINEMEDIACONTROLTONE, *LPLINEMEDIACONTROLTONE;

typedef struct linemonitortone_tag {
    DWORD dwAppSpecific;
    DWORD dwDuration;
    DWORD dwFrequency1;
    DWORD dwFrequency2;
    DWORD dwFrequency3;
} LINEMONITORTONE, *LPLINEMONITORTONE;

typedef struct lineproviderlist_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwNumProviders;
    DWORD dwProviderListSize;
    DWORD dwProviderListOffset;
} LINEPROVIDERLIST, *LPLINEPROVIDERLIST;

typedef struct linetranslatecaps_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwNumLocations;
    DWORD dwLocationListSize;
    DWORD dwLocationListOffset;
    DWORD dwCurrentLocationID;
    DWORD dwNumCards;
    DWORD dwCardListSize;
    DWORD dwCardListOffset;
    DWORD dwCurrentPreferredCardID;
} LINETRANSLATECAPS, *LPLINETRANSLATECAPS;

typedef struct linelocationentry_tag {
    DWORD dwPermanentLocationID;
    DWORD dwLocationNameSize;
    DWORD dwLocationNameOffset;
    DWORD dwCountryCode;
    DWORD dwCityCodeSize;
    DWORD dwCityCodeOffset;
    DWORD dwPreferredCardID;
    DWORD dwLocalAccessCodeSize;
    DWORD dwLocalAccessCodeOffset;
    DWORD dwLongDistanceAccessCodeSize;
    DWORD dwLongDistanceAccessCodeOffset;
    DWORD dwTollPrefixListSize;
    DWORD dwTollPrefixListOffset;
    DWORD dwCountryID;
    DWORD dwOptions;
    DWORD dwCancelCallWaitingSize;
    DWORD dwCancelCallWaitingOffset;
} LINELOCATIONENTRY, *LPLINELOCATIONENTRY;

typedef struct linecardentry_tag {
    DWORD dwPermanentCardID;
    DWORD dwCardNameSize;
    DWORD dwCardNameOffset;
    DWORD dwCardNumberDigits;
    DWORD dwSameAreaRuleSize;
    DWORD dwSameAreaRuleOffset;
    DWORD dwLongDistanceRuleSize;
    DWORD dwLongDistanceRuleOffset;
    DWORD dwInternationalRuleSize;
    DWORD dwInternationalRuleOffset;
    DWORD dwOptions;
} LINECARDENTRY, *LPLINECARDENTRY;


typedef struct linetranslateoutput_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwDialableStringSize;
    DWORD dwDialableStringOffset;
    DWORD dwDisplayableStringSize;
    DWORD dwDisplayableStringOffset;
    DWORD dwCurrentCountry;
    DWORD dwDestCountry;
    DWORD dwTranslateResults;
} LINETRANSLATEOUTPUT, *LPLINETRANSLATEOUTPUT;

typedef struct linemessage_tag {
  DWORD     hDevice;
  DWORD     dwMessageID;
  DWORD_PTR dwCallbackInstance;
  DWORD_PTR dwParam1;
  DWORD_PTR dwParam2;
  DWORD_PTR dwParam3;
} LINEMESSAGE, *LPLINEMESSAGE;

typedef void (CALLBACK *LINECALLBACK)(DWORD, DWORD, DWORD_PTR, DWORD_PTR, DWORD_PTR, DWORD_PTR);

typedef struct _PHONEAPP {
  int dummy;
} PHONEAPP, *LPPHONEAPP;

typedef struct _PHONE {
    DWORD dwRingMode;
    DWORD dwVolume;
} PHONE, *LPPHONE;

typedef struct phonebuttoninfo_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwButtonMode;
    DWORD dwButtonFunction;
    DWORD dwButtonTextSize;
    DWORD dwButtonTextOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
    DWORD dwButtonState;
} PHONEBUTTONINFO, *LPPHONEBUTTONINFO;

typedef struct phonecaps_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwProviderInfoSize;
    DWORD dwProviderInfoOffset;
    DWORD dwPhoneInfoSize;
    DWORD dwPhoneInfoOffset;
    DWORD dwPermanentPhoneID;
    DWORD dwPhoneNameSize;
    DWORD dwPhoneNameOffset;
    DWORD dwStringFormat;
    DWORD dwPhoneStates;
    DWORD dwHookSwitchDevs;
    DWORD dwHandsetHookSwitchModes;
    DWORD dwSpeakerHookSwitchModes;
    DWORD dwHeadsetHookSwitchModes;
    DWORD dwVolumeFlags;
    DWORD dwGainFlags;
    DWORD dwDisplayNumRows;
    DWORD dwDisplayNumColumns;
    DWORD dwNumRingModes;
    DWORD dwNumButtonLamps;
    DWORD dwButtonModesSize;
    DWORD dwButtonModesOffset;
    DWORD dwButtonFunctionsSize;
    DWORD dwButtonFunctionsOffset;
    DWORD dwLampModesSize;
    DWORD dwLampModesOffset;
    DWORD dwNumSetData;
    DWORD dwSetDataSize;
    DWORD dwSetDataOffset;
    DWORD dwNumGetData;
    DWORD dwGetDataSize;
    DWORD dwGetDataOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} PHONECAPS, *LPPHONECAPS;

typedef struct phoneextensionid_tag {
    DWORD dwExtensionID0;
    DWORD dwExtensionID1;
    DWORD dwExtensionID2;
    DWORD dwExtensionID3;
} PHONEEXTENSIONID, *LPPHONEEXTENSIONID;

typedef struct phoneinitializeexparams_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwOptions;
    union {
    HANDLE hEvent;
    HANDLE hCompletionPort;
    } Handles;
    DWORD dwCompletionKey;
} PHONEINITIALIZEEXPARAMS, *LPPHONEINITIALIZEEXPARAMS;

typedef struct phonemessage_tag {
  DWORD     hDevice;
  DWORD     dwMessageID;
  DWORD_PTR dwCallbackInstance;
  DWORD_PTR dwParam1;
  DWORD_PTR dwParam2;
  DWORD_PTR dwParam3;
} PHONEMESSAGE, *LPPHONEMESSAGE;

typedef struct phonestatus_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwStatusFlags;
    DWORD dwNumOwners;
    DWORD dwRingMOde;
    DWORD dwRingVolume;
    DWORD dwHandsetHookSwitchMode;
    DWORD dwHandsetVolume;
    DWORD dwHandsetGain;
    DWORD dwSpeakerHookSwitchMode;
    DWORD dwSpeakerVolume;
    DWORD dwSpeakerGain;
    DWORD dwHeadsetHookSwitchMode;
    DWORD dwHeadsetVolume;
    DWORD dwHeadsetGain;
    DWORD dwDisplaySize;
    DWORD dwDisplayOffset;
    DWORD dwLampModesSize;
    DWORD dwLampModesOffset;
    DWORD dwOwnerNameSize;
    DWORD dwOwnerNameOffset;
    DWORD dwDevSpecificSize;
    DWORD dwDevSpecificOffset;
} PHONESTATUS, *LPPHONESTATUS;

typedef void (CALLBACK *PHONECALLBACK)(DWORD, DWORD, DWORD_PTR, DWORD_PTR, DWORD_PTR, DWORD_PTR);

typedef struct varstring_tag {
    DWORD dwTotalSize;
    DWORD dwNeededSize;
    DWORD dwUsedSize;
    DWORD dwStringFormat;
    DWORD dwStringSize;
    DWORD dwStringOffset;
} VARSTRING, *LPVARSTRING;

/* line functions */
DWORD WINAPI lineAccept(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineAddProviderA(LPCSTR,HWND,LPDWORD);
DWORD WINAPI lineAddProviderW(LPCWSTR,HWND,LPDWORD);
#define      lineAddProvider WINELIB_NAME_AW(lineAddProvider)
DWORD WINAPI lineAddToConference(HCALL,HCALL);
DWORD WINAPI lineAnswer(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineBlindTransfer(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineClose(HLINE);
DWORD WINAPI lineCompleteCall(HCALL,LPDWORD,DWORD,DWORD);
DWORD WINAPI lineCompleteTransfer(HCALL,HCALL,LPHCALL,DWORD);
DWORD WINAPI lineConfigDialog(DWORD,HWND,LPCSTR);
DWORD WINAPI lineConfigDialogEdit(DWORD,HWND,LPCSTR,LPVOID const,DWORD,LPVARSTRING);
DWORD WINAPI lineConfigProvider(HWND,DWORD);
DWORD WINAPI lineDeallocateCall(HCALL);
DWORD WINAPI lineDevSpecific(HLINE,DWORD,HCALL,LPVOID,DWORD);
DWORD WINAPI lineDevSpecificFeature(HLINE,DWORD,LPVOID,DWORD);
DWORD WINAPI lineDial(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineDrop(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineForward(HLINE,DWORD,DWORD,LPLINEFORWARDLIST,DWORD,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineGatherDigits(HCALL,DWORD,LPSTR,DWORD,LPCSTR,DWORD,DWORD);
DWORD WINAPI lineGenerateDigits(HCALL,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineGenerateTone(HCALL,DWORD,DWORD,DWORD,LPLINEGENERATETONE);
DWORD WINAPI lineGetAddressCaps(HLINEAPP,DWORD,DWORD,DWORD,DWORD,LPLINEADDRESSCAPS);
DWORD WINAPI lineGetAddressID(HLINE,LPDWORD,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineGetAddressStatus(HLINE,DWORD,LPLINEADDRESSSTATUS);
DWORD WINAPI lineGetAppPriority(LPCSTR,DWORD,LPLINEEXTENSIONID const,DWORD,LPVARSTRING,LPDWORD);
DWORD WINAPI lineGetCallInfo(HCALL,LPLINECALLINFO);
DWORD WINAPI lineGetCallStatus(HCALL,LPLINECALLSTATUS);
DWORD WINAPI lineGetConfRelatedCalls(HCALL,LPLINECALLLIST);
DWORD WINAPI lineGetCountry(DWORD,DWORD,LPLINECOUNTRYLIST);
DWORD WINAPI lineGetDevCapsA(HLINEAPP,DWORD,DWORD,DWORD,LPLINEDEVCAPS);
DWORD WINAPI lineGetDevCapsW(HLINEAPP,DWORD,DWORD,DWORD,LPLINEDEVCAPS);
#define      lineGetDevCaps WINELIB_NAME_AW(lineGetDevCaps)
DWORD WINAPI lineGetDevConfig(DWORD,LPVARSTRING,LPCSTR);
DWORD WINAPI lineGetIDA(HLINE,DWORD,HCALL,DWORD,LPVARSTRING,LPCSTR);
DWORD WINAPI lineGetIDW(HLINE,DWORD,HCALL,DWORD,LPVARSTRING,LPCWSTR);
#define      lineGetID WINELIB_NAME_AW(lineGetID)
DWORD WINAPI lineGetIcon(DWORD,LPCSTR,HICON *);
DWORD WINAPI lineGetLineDevStatus(HLINE,LPLINEDEVSTATUS);
DWORD WINAPI lineGetNewCalls(HLINE,DWORD,DWORD,LPLINECALLLIST);
DWORD WINAPI lineGetNumRings(HLINE,DWORD,LPDWORD);
DWORD WINAPI lineGetProviderListA(DWORD,LPLINEPROVIDERLIST);
DWORD WINAPI lineGetProviderListW(DWORD,LPLINEPROVIDERLIST);
#define      lineGetProviderList WINELIB_NAME_AW(lineGetProviderList)
DWORD WINAPI lineGetRequest(HLINEAPP,DWORD,LPVOID);
DWORD WINAPI lineGetStatusMessages(HLINE,LPDWORD,LPDWORD);
DWORD WINAPI lineGetTranslateCaps(HLINEAPP,DWORD,LPLINETRANSLATECAPS);
DWORD WINAPI lineHandoff(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineHold(HCALL);
DWORD WINAPI lineInitialize(LPHLINEAPP,HINSTANCE,LINECALLBACK,LPCSTR,LPDWORD);
LONG  WINAPI lineInitializeExA(LPHLINEAPP,HINSTANCE,LINECALLBACK,LPCSTR,LPDWORD,LPDWORD,LPLINEINITIALIZEEXPARAMS);
LONG  WINAPI lineInitializeExW(LPHLINEAPP,HINSTANCE,LINECALLBACK,LPCWSTR,LPDWORD,LPDWORD,LPLINEINITIALIZEEXPARAMS);
#define      lineInitializeEx WINELIB_NAME_AW(lineInitializeEx)
DWORD WINAPI lineMakeCallA(HLINE,LPHCALL,LPCSTR,DWORD,LPLINECALLPARAMS);
DWORD WINAPI lineMakeCallW(HLINE,LPHCALL,LPCWSTR,DWORD,LPLINECALLPARAMS);
#define      lineMakeCall WINELIB_NAME_AW(lineMakeCall)
DWORD WINAPI lineMonitorDigits(HCALL,DWORD);
DWORD WINAPI lineMonitorMedia(HCALL,DWORD);
DWORD WINAPI lineMonitorTones(HCALL,LPLINEMONITORTONE,DWORD);
DWORD WINAPI lineNegotiateAPIVersion(HLINEAPP,DWORD,DWORD,DWORD,LPDWORD,LPLINEEXTENSIONID);
DWORD WINAPI lineNegotiateExtVersion(HLINEAPP,DWORD,DWORD,DWORD,DWORD,LPDWORD);
DWORD WINAPI lineOpen(HLINEAPP,DWORD,LPHLINE,DWORD,DWORD,DWORD,DWORD,DWORD,LPLINECALLPARAMS);
DWORD WINAPI linePark(HCALL,DWORD,LPCSTR,LPVARSTRING);
DWORD WINAPI linePickup(HLINE,DWORD,LPHCALL,LPCSTR,LPCSTR);
DWORD WINAPI linePrepareAddToConference(HCALL,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineRedirect(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineRegisterRequestRecipient(HLINEAPP,DWORD,DWORD,DWORD);
DWORD WINAPI lineReleaseUserUserInfo(HCALL);
DWORD WINAPI lineRemoveFromConference(HCALL);
DWORD WINAPI lineRemoveProvider(DWORD,HWND);
DWORD WINAPI lineSecureCall(HCALL);
DWORD WINAPI lineSendUserUserInfo(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineSetAppPriority(LPCSTR,DWORD,LPLINEEXTENSIONID const,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineSetAppSpecific(HCALL,DWORD);
DWORD WINAPI lineSetCallParams(HCALL,DWORD,DWORD,DWORD,LPLINEDIALPARAMS);
DWORD WINAPI lineSetCallPrivilege(HCALL,DWORD);
DWORD WINAPI lineSetCurrentLocation(HLINEAPP,DWORD);
DWORD WINAPI lineSetDevConfig(DWORD,LPVOID,DWORD,LPCSTR);
DWORD WINAPI lineSetMediaControl(HLINE,DWORD,HCALL,DWORD,LPLINEMEDIACONTROLDIGIT,DWORD,LPLINEMEDIACONTROLMEDIA,DWORD,LPLINEMEDIACONTROLTONE,DWORD,LPLINEMEDIACONTROLCALLSTATE,DWORD);
DWORD WINAPI lineSetMediaMode(HCALL,DWORD);
DWORD WINAPI lineSetNumRings(HLINE,DWORD,DWORD);
DWORD WINAPI lineSetStatusMessages(HLINE,DWORD,DWORD);
DWORD WINAPI lineSetTerminal(HLINE,DWORD,HCALL,DWORD,DWORD,DWORD,DWORD);
DWORD WINAPI lineSetTollList(HLINEAPP,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineSetupConference(HCALL,HLINE,LPHCALL,LPHCALL,DWORD,LPLINECALLPARAMS);
DWORD WINAPI lineSetupTransfer(HCALL,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineShutdown(HLINEAPP);
DWORD WINAPI lineSwapHold(HCALL,HCALL);
DWORD WINAPI lineTranslateAddress(HLINEAPP,DWORD,DWORD,LPCSTR,DWORD,DWORD,LPLINETRANSLATEOUTPUT);
DWORD WINAPI lineTranslateDialog(HLINEAPP,DWORD,DWORD,HWND,LPCSTR);
DWORD WINAPI lineUncompleteCall(HLINE,DWORD);
DWORD WINAPI lineUnHold(HCALL);
DWORD WINAPI lineUnpark(HLINE,DWORD,LPHCALL,LPCSTR);

DWORD WINAPI lineBlindTransferA(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineConfigDialogA(DWORD,HWND,LPCSTR);
DWORD WINAPI lineConfigDialogEditA(DWORD,HWND,LPCSTR,LPVOID const,DWORD,LPVARSTRING);
DWORD WINAPI lineDialA(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineForwardA(HLINE,DWORD,DWORD,LPLINEFORWARDLIST,DWORD,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineGatherDigitsA(HCALL,DWORD,LPSTR,DWORD,LPCSTR,DWORD,DWORD);
DWORD WINAPI lineGenerateDigitsA(HCALL,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineGetAddressCapsA(HLINEAPP,DWORD,DWORD,DWORD,DWORD,LPLINEADDRESSCAPS);
DWORD WINAPI lineGetAddressIDA(HLINE,LPDWORD,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineGetAddressStatusA(HLINE,DWORD,LPLINEADDRESSSTATUS);
DWORD WINAPI lineGetAppPriorityA(LPCSTR,DWORD,LPLINEEXTENSIONID const,DWORD,LPVARSTRING,LPDWORD);
DWORD WINAPI lineGetCallInfoA(HCALL,LPLINECALLINFO);
DWORD WINAPI lineGetCountryA(DWORD,DWORD,LPLINECOUNTRYLIST);
DWORD WINAPI lineGetDevConfigA(DWORD,LPVARSTRING,LPCSTR);
DWORD WINAPI lineGetIconA(DWORD,LPCSTR,HICON *);
DWORD WINAPI lineGetLineDevStatusA(HLINE,LPLINEDEVSTATUS);
DWORD WINAPI lineGetRequestA(HLINEAPP,DWORD,LPVOID);
DWORD WINAPI lineGetTranslateCapsA(HLINEAPP,DWORD,LPLINETRANSLATECAPS);
DWORD WINAPI lineHandoffA(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineOpenA(HLINEAPP,DWORD,LPHLINE,DWORD,DWORD,DWORD,DWORD,DWORD,LPLINECALLPARAMS);
DWORD WINAPI lineParkA(HCALL,DWORD,LPCSTR,LPVARSTRING);
DWORD WINAPI linePickupA(HLINE,DWORD,LPHCALL,LPCSTR,LPCSTR);
DWORD WINAPI linePrepareAddToConferenceA(HCALL,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineRedirectA(HCALL,LPCSTR,DWORD);
DWORD WINAPI lineSetAppPriorityA(LPCSTR,DWORD,LPLINEEXTENSIONID const,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineSetDevConfigA(DWORD,LPVOID,DWORD,LPCSTR);
DWORD WINAPI lineSetTollListA(HLINEAPP,DWORD,LPCSTR,DWORD);
DWORD WINAPI lineSetupConferenceA(HCALL,HLINE,LPHCALL,LPHCALL,DWORD,LPLINECALLPARAMS);
DWORD WINAPI lineSetupTransferA(HCALL,LPHCALL,LPLINECALLPARAMS);
DWORD WINAPI lineTranslateAddressA(HLINEAPP,DWORD,DWORD,LPCSTR,DWORD,DWORD,LPLINETRANSLATEOUTPUT);
DWORD WINAPI lineTranslateDialogA(HLINEAPP,DWORD,DWORD,HWND,LPCSTR);
DWORD WINAPI lineUnhold(HCALL);
DWORD WINAPI lineUnparkA(HLINE,DWORD,LPHCALL,LPCSTR);

/* phone functions */
DWORD WINAPI phoneClose(HPHONE);
DWORD WINAPI phoneConfigDialog(DWORD,HWND,LPCSTR);
DWORD WINAPI phoneDevSpecific(HPHONE,LPVOID,DWORD);
DWORD WINAPI phoneGetButtonInfo(HPHONE,DWORD,LPPHONEBUTTONINFO);
DWORD WINAPI phoneGetData(HPHONE,DWORD,LPVOID,DWORD);
DWORD WINAPI phoneGetDevCaps(HPHONEAPP,DWORD,DWORD,DWORD,LPPHONECAPS);
DWORD WINAPI phoneGetDisplay(HPHONE,LPVARSTRING);
DWORD WINAPI phoneGetGain(HPHONE,DWORD,LPDWORD);
DWORD WINAPI phoneGetHookSwitch(HPHONE,LPDWORD);
DWORD WINAPI phoneGetID(HPHONE,LPVARSTRING,LPCSTR);
DWORD WINAPI phoneGetIcon(DWORD,LPCSTR,HICON *);
DWORD WINAPI phoneGetLamp(HPHONE,DWORD,LPDWORD);
DWORD WINAPI phoneGetRing(HPHONE,LPDWORD,LPDWORD);
DWORD WINAPI phoneGetStatus(HPHONE,LPPHONESTATUS);
DWORD WINAPI phoneGetStatusMessages(HPHONE,LPDWORD,LPDWORD,LPDWORD);
DWORD WINAPI phoneGetVolume(HPHONE,DWORD,LPDWORD);
DWORD WINAPI phoneInitialize(LPHPHONEAPP,HINSTANCE,PHONECALLBACK,LPCSTR,LPDWORD);
DWORD WINAPI phoneNegotiateAPIVersion(HPHONEAPP,DWORD,DWORD,DWORD,LPDWORD,LPPHONEEXTENSIONID);
DWORD WINAPI phoneNegotiateExtVersion(HPHONEAPP,DWORD,DWORD,DWORD,DWORD,LPDWORD);
DWORD WINAPI phoneOpen(HPHONEAPP,DWORD,LPHPHONE,DWORD,DWORD,DWORD,DWORD);
DWORD WINAPI phoneSetButtonInfo(HPHONE,DWORD,LPPHONEBUTTONINFO);
DWORD WINAPI phoneSetData(HPHONE,DWORD,LPVOID,DWORD);
DWORD WINAPI phoneSetDisplay(HPHONE,DWORD,DWORD,LPCSTR,DWORD);
DWORD WINAPI phoneSetGain(HPHONE,DWORD,DWORD);
DWORD WINAPI phoneSetHookSwitch(HPHONE,DWORD,DWORD);
DWORD WINAPI phoneSetLamp(HPHONE,DWORD,DWORD);
DWORD WINAPI phoneSetRing(HPHONE,DWORD,DWORD);
DWORD WINAPI phoneSetStatusMessages(HPHONE,DWORD,DWORD,DWORD);
DWORD WINAPI phoneSetVolume(HPHONE,DWORD,DWORD);
DWORD WINAPI phoneShutdown(HPHONEAPP);

DWORD WINAPI phoneConfigDialogA(DWORD,HWND,LPCSTR);
DWORD WINAPI phoneGetButtonInfoA(HPHONE,DWORD,LPPHONEBUTTONINFO);
DWORD WINAPI phoneGetDevCapsA(HPHONEAPP,DWORD,DWORD,DWORD,LPPHONECAPS);
DWORD WINAPI phoneGetIDA(HPHONE,LPVARSTRING,LPCSTR);
DWORD WINAPI phoneGetIconA(DWORD,LPCSTR,HICON *);
DWORD WINAPI phoneGetStatusA(HPHONE,LPPHONESTATUS);
DWORD WINAPI phoneSetButtonInfoA(HPHONE,DWORD,LPPHONEBUTTONINFO);

/* "assisted" functions */
DWORD WINAPI tapiGetLocationInfo(LPSTR,LPSTR);
DWORD WINAPI tapiRequestMakeCall(LPCSTR,LPCSTR,LPCSTR,LPCSTR);

DWORD WINAPI tapiGetLocationInfoA(LPSTR,LPSTR);
DWORD WINAPI tapiRequestMakeCallA(LPCSTR,LPCSTR,LPCSTR,LPCSTR);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#ifndef INITIALIZE_NEGOTIATION
#define INITIALIZE_NEGOTIATION 0xFFFFFFFFUL
#endif
#ifndef LINEADDRESSSTATE_CAPSCHANGE
#define LINEADDRESSSTATE_CAPSCHANGE 0x00000100
#endif
#ifndef LINEAGENTFEATURE_AGENTSPECIFIC
#define LINEAGENTFEATURE_AGENTSPECIFIC 0x00000008
#endif
#ifndef LINEAGENTFEATURE_GETAGENTACTIVITYLIST
#define LINEAGENTFEATURE_GETAGENTACTIVITYLIST 0x00000010
#endif
#ifndef LINEAGENTFEATURE_GETAGENTGROUP
#define LINEAGENTFEATURE_GETAGENTGROUP 0x00000020
#endif
#ifndef LINEAGENTFEATURE_SETAGENTACTIVITY
#define LINEAGENTFEATURE_SETAGENTACTIVITY 0x00000004
#endif
#ifndef LINEAGENTFEATURE_SETAGENTGROUP
#define LINEAGENTFEATURE_SETAGENTGROUP 0x00000001
#endif
#ifndef LINEAGENTFEATURE_SETAGENTSTATE
#define LINEAGENTFEATURE_SETAGENTSTATE 0x00000002
#endif
#ifndef LINEAGENTSESSIONSTATE_BUSYONCALL
#define LINEAGENTSESSIONSTATE_BUSYONCALL 0x00000004
#endif
#ifndef LINEAGENTSESSIONSTATE_BUSYWRAPUP
#define LINEAGENTSESSIONSTATE_BUSYWRAPUP 0x00000008
#endif
#ifndef LINEAGENTSESSIONSTATE_ENDED
#define LINEAGENTSESSIONSTATE_ENDED 0x00000010
#endif
#ifndef LINEAGENTSESSIONSTATE_NOTREADY
#define LINEAGENTSESSIONSTATE_NOTREADY 0x00000001
#endif
#ifndef LINEAGENTSESSIONSTATE_READY
#define LINEAGENTSESSIONSTATE_READY 0x00000002
#endif
#ifndef LINEAGENTSESSIONSTATE_RELEASED
#define LINEAGENTSESSIONSTATE_RELEASED 0x00000020
#endif
#ifndef LINEAGENTSESSIONSTATUS_NEWSESSION
#define LINEAGENTSESSIONSTATUS_NEWSESSION 0x00000001
#endif
#ifndef LINEAGENTSESSIONSTATUS_STATE
#define LINEAGENTSESSIONSTATUS_STATE 0x00000002
#endif
#ifndef LINEAGENTSESSIONSTATUS_UPDATEINFO
#define LINEAGENTSESSIONSTATUS_UPDATEINFO 0x00000004
#endif
#ifndef LINEAGENTSTATEEX_BUSYACD
#define LINEAGENTSTATEEX_BUSYACD 0x00000004
#endif
#ifndef LINEAGENTSTATEEX_BUSYINCOMING
#define LINEAGENTSTATEEX_BUSYINCOMING 0x00000008
#endif
#ifndef LINEAGENTSTATEEX_BUSYOUTGOING
#define LINEAGENTSTATEEX_BUSYOUTGOING 0x00000010
#endif
#ifndef LINEAGENTSTATEEX_NOTREADY
#define LINEAGENTSTATEEX_NOTREADY 0x00000001
#endif
#ifndef LINEAGENTSTATEEX_READY
#define LINEAGENTSTATEEX_READY 0x00000002
#endif
#ifndef LINEAGENTSTATEEX_RELEASED
#define LINEAGENTSTATEEX_RELEASED 0x00000040
#endif
#ifndef LINEAGENTSTATEEX_UNKNOWN
#define LINEAGENTSTATEEX_UNKNOWN 0x00000020
#endif
#ifndef LINEAGENTSTATE_BUSYACD
#define LINEAGENTSTATE_BUSYACD 0x00000008
#endif
#ifndef LINEAGENTSTATE_BUSYINCOMING
#define LINEAGENTSTATE_BUSYINCOMING 0x00000010
#endif
#ifndef LINEAGENTSTATE_BUSYOTHER
#define LINEAGENTSTATE_BUSYOTHER 0x00000040
#endif
#ifndef LINEAGENTSTATE_BUSYOUTBOUND
#define LINEAGENTSTATE_BUSYOUTBOUND 0x00000020
#endif
#ifndef LINEAGENTSTATE_LOGGEDOFF
#define LINEAGENTSTATE_LOGGEDOFF 0x00000001
#endif
#ifndef LINEAGENTSTATE_NOTREADY
#define LINEAGENTSTATE_NOTREADY 0x00000002
#endif
#ifndef LINEAGENTSTATE_READY
#define LINEAGENTSTATE_READY 0x00000004
#endif
#ifndef LINEAGENTSTATE_UNAVAIL
#define LINEAGENTSTATE_UNAVAIL 0x00000200
#endif
#ifndef LINEAGENTSTATE_UNKNOWN
#define LINEAGENTSTATE_UNKNOWN 0x00000100
#endif
#ifndef LINEAGENTSTATE_WORKINGAFTERCALL
#define LINEAGENTSTATE_WORKINGAFTERCALL 0x00000080
#endif
#ifndef LINEAGENTSTATUSEX_NEWAGENT
#define LINEAGENTSTATUSEX_NEWAGENT 0x00000001
#endif
#ifndef LINEAGENTSTATUSEX_STATE
#define LINEAGENTSTATUSEX_STATE 0x00000002
#endif
#ifndef LINEAGENTSTATUSEX_UPDATEINFO
#define LINEAGENTSTATUSEX_UPDATEINFO 0x00000004
#endif
#ifndef LINEAGENTSTATUS_ACTIVITY
#define LINEAGENTSTATUS_ACTIVITY 0x00000008
#endif
#ifndef LINEAGENTSTATUS_ACTIVITYLIST
#define LINEAGENTSTATUS_ACTIVITYLIST 0x00000010
#endif
#ifndef LINEAGENTSTATUS_CAPSCHANGE
#define LINEAGENTSTATUS_CAPSCHANGE 0x00000040
#endif
#ifndef LINEAGENTSTATUS_GROUP
#define LINEAGENTSTATUS_GROUP 0x00000001
#endif
#ifndef LINEAGENTSTATUS_GROUPLIST
#define LINEAGENTSTATUS_GROUPLIST 0x00000020
#endif
#ifndef LINEAGENTSTATUS_NEXTSTATE
#define LINEAGENTSTATUS_NEXTSTATE 0x00000004
#endif
#ifndef LINEAGENTSTATUS_STATE
#define LINEAGENTSTATUS_STATE 0x00000002
#endif
#ifndef LINEAGENTSTATUS_VALIDNEXTSTATES
#define LINEAGENTSTATUS_VALIDNEXTSTATES 0x00000100
#endif
#ifndef LINEAGENTSTATUS_VALIDSTATES
#define LINEAGENTSTATUS_VALIDSTATES 0x00000080
#endif
#ifndef LINECALLFEATURE2_COMPLCALLBACK
#define LINECALLFEATURE2_COMPLCALLBACK 0x00000008
#endif
#ifndef LINECALLFEATURE2_COMPLCAMPON
#define LINECALLFEATURE2_COMPLCAMPON 0x00000004
#endif
#ifndef LINECALLFEATURE2_COMPLINTRUDE
#define LINECALLFEATURE2_COMPLINTRUDE 0x00000010
#endif
#ifndef LINECALLFEATURE2_COMPLMESSAGE
#define LINECALLFEATURE2_COMPLMESSAGE 0x00000020
#endif
#ifndef LINECALLFEATURE2_NOHOLDCONFERENCE
#define LINECALLFEATURE2_NOHOLDCONFERENCE 0x00000001
#endif
#ifndef LINECALLFEATURE2_ONESTEPTRANSFER
#define LINECALLFEATURE2_ONESTEPTRANSFER 0x00000002
#endif
#ifndef LINECALLFEATURE2_PARKDIRECT
#define LINECALLFEATURE2_PARKDIRECT 0x00000100
#endif
#ifndef LINECALLFEATURE2_PARKNONDIRECT
#define LINECALLFEATURE2_PARKNONDIRECT 0x00000200
#endif
#ifndef LINECALLFEATURE2_TRANSFERCONF
#define LINECALLFEATURE2_TRANSFERCONF 0x00000080
#endif
#ifndef LINECALLFEATURE2_TRANSFERNORM
#define LINECALLFEATURE2_TRANSFERNORM 0x00000040
#endif
#ifndef LINECALLTREATMENT_BUSY
#define LINECALLTREATMENT_BUSY 0x00000003
#endif
#ifndef LINECALLTREATMENT_MUSIC
#define LINECALLTREATMENT_MUSIC 0x00000004
#endif
#ifndef LINECALLTREATMENT_RINGBACK
#define LINECALLTREATMENT_RINGBACK 0x00000002
#endif
#ifndef LINECALLTREATMENT_SILENCE
#define LINECALLTREATMENT_SILENCE 0x00000001
#endif
#ifndef LINECARDOPTION_HIDDEN
#define LINECARDOPTION_HIDDEN 0x00000002
#endif
#ifndef LINECARDOPTION_PREDEFINED
#define LINECARDOPTION_PREDEFINED 0x00000001
#endif
#ifndef LINEDEVCAPFLAGS_LOCAL
#define LINEDEVCAPFLAGS_LOCAL 0x00002000
#endif
#ifndef LINEDISCONNECTMODE_DESTINATIONBARRED
#define LINEDISCONNECTMODE_DESTINATIONBARRED 0x00100000
#endif
#ifndef LINEDISCONNECTMODE_FDNRESTRICT
#define LINEDISCONNECTMODE_FDNRESTRICT 0x00200000
#endif
#ifndef LINEERR_DIALVOICEDETECT
#define LINEERR_DIALVOICEDETECT 0x8000005C
#endif
#ifndef LINEERR_DISCONNECTED
#define LINEERR_DISCONNECTED 0X80000060
#endif
#ifndef LINEERR_INVALAGENTACTIVITY
#define LINEERR_INVALAGENTACTIVITY 0x8000005B
#endif
#ifndef LINEERR_INVALAGENTGROUP
#define LINEERR_INVALAGENTGROUP 0x80000058
#endif
#ifndef LINEERR_INVALAGENTID
#define LINEERR_INVALAGENTID 0x80000057
#endif
#ifndef LINEERR_INVALAGENTSESSIONSTATE
#define LINEERR_INVALAGENTSESSIONSTATE 0x8000005F
#endif
#ifndef LINEERR_INVALAGENTSTATE
#define LINEERR_INVALAGENTSTATE 0x8000005A
#endif
#ifndef LINEERR_INVALPASSWORD
#define LINEERR_INVALPASSWORD 0x80000059
#endif
#ifndef LINEERR_SERVICE_NOT_RUNNING
#define LINEERR_SERVICE_NOT_RUNNING 0x80000061
#endif
#ifndef LINEERR_USERCANCELLED
#define LINEERR_USERCANCELLED 0x8000005D
#endif
#ifndef LINEFORWARDMODE_UNAVAIL
#define LINEFORWARDMODE_UNAVAIL 0x00020000
#endif
#ifndef LINEFORWARDMODE_UNKNOWN
#define LINEFORWARDMODE_UNKNOWN 0x00010000
#endif
#ifndef LINEGROUPSTATUS_GROUPREMOVED
#define LINEGROUPSTATUS_GROUPREMOVED 0x00000002
#endif
#ifndef LINEGROUPSTATUS_NEWGROUP
#define LINEGROUPSTATUS_NEWGROUP 0x00000001
#endif
#ifndef LINEMAPPER
#define LINEMAPPER 0xFFFFFFFF
#endif
#ifndef LINEOFFERINGMODE_ACTIVE
#define LINEOFFERINGMODE_ACTIVE 0x00000001
#endif
#ifndef LINEOFFERINGMODE_INACTIVE
#define LINEOFFERINGMODE_INACTIVE 0x00000002
#endif
#ifndef LINEOPENOPTION_PROXY
#define LINEOPENOPTION_PROXY 0x40000000
#endif
#ifndef LINEOPENOPTION_SINGLEADDRESS
#define LINEOPENOPTION_SINGLEADDRESS 0x80000000
#endif
#ifndef LINEPROXYREQUEST_AGENTSPECIFIC
#define LINEPROXYREQUEST_AGENTSPECIFIC 0x00000006
#endif
#ifndef LINEPROXYREQUEST_CREATEAGENT
#define LINEPROXYREQUEST_CREATEAGENT 0x00000009
#endif
#ifndef LINEPROXYREQUEST_CREATEAGENTSESSION
#define LINEPROXYREQUEST_CREATEAGENTSESSION 0x0000000C
#endif
#ifndef LINEPROXYREQUEST_GETAGENTACTIVITYLIST
#define LINEPROXYREQUEST_GETAGENTACTIVITYLIST 0x00000007
#endif
#ifndef LINEPROXYREQUEST_GETAGENTCAPS
#define LINEPROXYREQUEST_GETAGENTCAPS 0x00000004
#endif
#ifndef LINEPROXYREQUEST_GETAGENTGROUPLIST
#define LINEPROXYREQUEST_GETAGENTGROUPLIST 0x00000008
#endif
#ifndef LINEPROXYREQUEST_GETAGENTINFO
#define LINEPROXYREQUEST_GETAGENTINFO 0x0000000B
#endif
#ifndef LINEPROXYREQUEST_GETAGENTSESSIONINFO
#define LINEPROXYREQUEST_GETAGENTSESSIONINFO 0x0000000F
#endif
#ifndef LINEPROXYREQUEST_GETAGENTSESSIONLIST
#define LINEPROXYREQUEST_GETAGENTSESSIONLIST 0x0000000D
#endif
#ifndef LINEPROXYREQUEST_GETAGENTSTATUS
#define LINEPROXYREQUEST_GETAGENTSTATUS 0x00000005
#endif
#ifndef LINEPROXYREQUEST_GETGROUPLIST
#define LINEPROXYREQUEST_GETGROUPLIST 0x00000013
#endif
#ifndef LINEPROXYREQUEST_GETQUEUEINFO
#define LINEPROXYREQUEST_GETQUEUEINFO 0x00000012
#endif
#ifndef LINEPROXYREQUEST_GETQUEUELIST
#define LINEPROXYREQUEST_GETQUEUELIST 0x00000010
#endif
#ifndef LINEPROXYREQUEST_SETAGENTACTIVITY
#define LINEPROXYREQUEST_SETAGENTACTIVITY 0x00000003
#endif
#ifndef LINEPROXYREQUEST_SETAGENTGROUP
#define LINEPROXYREQUEST_SETAGENTGROUP 0x00000001
#endif
#ifndef LINEPROXYREQUEST_SETAGENTMEASUREMENTPERIOD
#define LINEPROXYREQUEST_SETAGENTMEASUREMENTPERIOD 0x0000000A
#endif
#ifndef LINEPROXYREQUEST_SETAGENTSESSIONSTATE
#define LINEPROXYREQUEST_SETAGENTSESSIONSTATE 0x0000000E
#endif
#ifndef LINEPROXYREQUEST_SETAGENTSTATE
#define LINEPROXYREQUEST_SETAGENTSTATE 0x00000002
#endif
#ifndef LINEPROXYREQUEST_SETAGENTSTATEEX
#define LINEPROXYREQUEST_SETAGENTSTATEEX 0x00000014
#endif
#ifndef LINEPROXYREQUEST_SETQUEUEMEASUREMENTPERIOD
#define LINEPROXYREQUEST_SETQUEUEMEASUREMENTPERIOD 0x00000011
#endif
#ifndef LINEPROXYSTATUS_ALLOPENFORACD
#define LINEPROXYSTATUS_ALLOPENFORACD 0x00000004
#endif
#ifndef LINEPROXYSTATUS_CLOSE
#define LINEPROXYSTATUS_CLOSE 0x00000002
#endif
#ifndef LINEPROXYSTATUS_OPEN
#define LINEPROXYSTATUS_OPEN 0x00000001
#endif
#ifndef LINEQUEUESTATUS_NEWQUEUE
#define LINEQUEUESTATUS_NEWQUEUE 0x00000002
#endif
#ifndef LINEQUEUESTATUS_QUEUEREMOVED
#define LINEQUEUESTATUS_QUEUEREMOVED 0x00000004
#endif
#ifndef LINEQUEUESTATUS_UPDATEINFO
#define LINEQUEUESTATUS_UPDATEINFO 0x00000001
#endif
#ifndef LINETRANSLATEOPTION_CANCELCALLWAITING
#define LINETRANSLATEOPTION_CANCELCALLWAITING 0x00000002
#endif
#ifndef LINETRANSLATEOPTION_FORCELD
#define LINETRANSLATEOPTION_FORCELD 0x00000008
#endif
#ifndef LINETRANSLATEOPTION_FORCELOCAL
#define LINETRANSLATEOPTION_FORCELOCAL 0x00000004
#endif
#ifndef PHONEBUTTONFUNCTION_ABBREVDIAL
#define PHONEBUTTONFUNCTION_ABBREVDIAL 0x0000000B
#endif
#ifndef PHONEBUTTONFUNCTION_BRIDGEDAPP
#define PHONEBUTTONFUNCTION_BRIDGEDAPP 0x0000001C
#endif
#ifndef PHONEBUTTONFUNCTION_BUSY
#define PHONEBUTTONFUNCTION_BUSY 0x0000001D
#endif
#ifndef PHONEBUTTONFUNCTION_CALLAPP
#define PHONEBUTTONFUNCTION_CALLAPP 0x0000001E
#endif
#ifndef PHONEBUTTONFUNCTION_CALLID
#define PHONEBUTTONFUNCTION_CALLID 0x00000022
#endif
#ifndef PHONEBUTTONFUNCTION_CAMPON
#define PHONEBUTTONFUNCTION_CAMPON 0x0000002B
#endif
#ifndef PHONEBUTTONFUNCTION_CONFERENCE
#define PHONEBUTTONFUNCTION_CONFERENCE 0x00000001
#endif
#ifndef PHONEBUTTONFUNCTION_CONNECT
#define PHONEBUTTONFUNCTION_CONNECT 0x00000007
#endif
#ifndef PHONEBUTTONFUNCTION_COVER
#define PHONEBUTTONFUNCTION_COVER 0x00000021
#endif
#ifndef PHONEBUTTONFUNCTION_DATAOFF
#define PHONEBUTTONFUNCTION_DATAOFF 0x00000019
#endif
#ifndef PHONEBUTTONFUNCTION_DATAON
#define PHONEBUTTONFUNCTION_DATAON 0x00000018
#endif
#ifndef PHONEBUTTONFUNCTION_DATETIME
#define PHONEBUTTONFUNCTION_DATETIME 0x0000001F
#endif
#ifndef PHONEBUTTONFUNCTION_DIRECTORY
#define PHONEBUTTONFUNCTION_DIRECTORY 0x00000020
#endif
#ifndef PHONEBUTTONFUNCTION_DISCONNECT
#define PHONEBUTTONFUNCTION_DISCONNECT 0x00000006
#endif
#ifndef PHONEBUTTONFUNCTION_DONOTDISTURB
#define PHONEBUTTONFUNCTION_DONOTDISTURB 0x0000001A
#endif
#ifndef PHONEBUTTONFUNCTION_DROP
#define PHONEBUTTONFUNCTION_DROP 0x00000003
#endif
#ifndef PHONEBUTTONFUNCTION_FLASH
#define PHONEBUTTONFUNCTION_FLASH 0x00000017
#endif
#ifndef PHONEBUTTONFUNCTION_FORWARD
#define PHONEBUTTONFUNCTION_FORWARD 0x0000000C
#endif
#ifndef PHONEBUTTONFUNCTION_HOLD
#define PHONEBUTTONFUNCTION_HOLD 0x00000004
#endif
#ifndef PHONEBUTTONFUNCTION_INTERCOM
#define PHONEBUTTONFUNCTION_INTERCOM 0x0000001B
#endif
#ifndef PHONEBUTTONFUNCTION_LASTNUM
#define PHONEBUTTONFUNCTION_LASTNUM 0x00000023
#endif
#ifndef PHONEBUTTONFUNCTION_MSGINDICATOR
#define PHONEBUTTONFUNCTION_MSGINDICATOR 0x00000026
#endif
#ifndef PHONEBUTTONFUNCTION_MSGWAITOFF
#define PHONEBUTTONFUNCTION_MSGWAITOFF 0x00000009
#endif
#ifndef PHONEBUTTONFUNCTION_MSGWAITON
#define PHONEBUTTONFUNCTION_MSGWAITON 0x00000008
#endif
#ifndef PHONEBUTTONFUNCTION_MUTE
#define PHONEBUTTONFUNCTION_MUTE 0x00000012
#endif
#ifndef PHONEBUTTONFUNCTION_NIGHTSRV
#define PHONEBUTTONFUNCTION_NIGHTSRV 0x00000024
#endif
#ifndef PHONEBUTTONFUNCTION_NONE
#define PHONEBUTTONFUNCTION_NONE 0x0000002E
#endif
#ifndef PHONEBUTTONFUNCTION_PARK
#define PHONEBUTTONFUNCTION_PARK 0x0000000F
#endif
#ifndef PHONEBUTTONFUNCTION_PICKUP
#define PHONEBUTTONFUNCTION_PICKUP 0x0000000D
#endif
#ifndef PHONEBUTTONFUNCTION_QUEUECALL
#define PHONEBUTTONFUNCTION_QUEUECALL 0x0000002D
#endif
#ifndef PHONEBUTTONFUNCTION_RECALL
#define PHONEBUTTONFUNCTION_RECALL 0x00000005
#endif
#ifndef PHONEBUTTONFUNCTION_REDIRECT
#define PHONEBUTTONFUNCTION_REDIRECT 0x00000011
#endif
#ifndef PHONEBUTTONFUNCTION_REJECT
#define PHONEBUTTONFUNCTION_REJECT 0x00000010
#endif
#ifndef PHONEBUTTONFUNCTION_REPDIAL
#define PHONEBUTTONFUNCTION_REPDIAL 0x00000027
#endif
#ifndef PHONEBUTTONFUNCTION_RINGAGAIN
#define PHONEBUTTONFUNCTION_RINGAGAIN 0x0000000E
#endif
#ifndef PHONEBUTTONFUNCTION_SAVEREPEAT
#define PHONEBUTTONFUNCTION_SAVEREPEAT 0x0000002C
#endif
#ifndef PHONEBUTTONFUNCTION_SELECTRING
#define PHONEBUTTONFUNCTION_SELECTRING 0x0000000A
#endif
#ifndef PHONEBUTTONFUNCTION_SENDCALLS
#define PHONEBUTTONFUNCTION_SENDCALLS 0x00000025
#endif
#ifndef PHONEBUTTONFUNCTION_SETREPDIAL
#define PHONEBUTTONFUNCTION_SETREPDIAL 0x00000028
#endif
#ifndef PHONEBUTTONFUNCTION_SPEAKEROFF
#define PHONEBUTTONFUNCTION_SPEAKEROFF 0x00000016
#endif
#ifndef PHONEBUTTONFUNCTION_SPEAKERON
#define PHONEBUTTONFUNCTION_SPEAKERON 0x00000015
#endif
#ifndef PHONEBUTTONFUNCTION_STATIONSPEED
#define PHONEBUTTONFUNCTION_STATIONSPEED 0x0000002A
#endif
#ifndef PHONEBUTTONFUNCTION_SYSTEMSPEED
#define PHONEBUTTONFUNCTION_SYSTEMSPEED 0x00000029
#endif
#ifndef PHONEBUTTONFUNCTION_TRANSFER
#define PHONEBUTTONFUNCTION_TRANSFER 0x00000002
#endif
#ifndef PHONEBUTTONFUNCTION_UNKNOWN
#define PHONEBUTTONFUNCTION_UNKNOWN 0x00000000
#endif
#ifndef PHONEBUTTONFUNCTION_VOLUMEDOWN
#define PHONEBUTTONFUNCTION_VOLUMEDOWN 0x00000014
#endif
#ifndef PHONEBUTTONFUNCTION_VOLUMEUP
#define PHONEBUTTONFUNCTION_VOLUMEUP 0x00000013
#endif
#ifndef PHONEBUTTONMODE_CALL
#define PHONEBUTTONMODE_CALL 0x00000002
#endif
#ifndef PHONEBUTTONMODE_DISPLAY
#define PHONEBUTTONMODE_DISPLAY 0x00000020
#endif
#ifndef PHONEBUTTONMODE_DUMMY
#define PHONEBUTTONMODE_DUMMY 0x00000001
#endif
#ifndef PHONEBUTTONMODE_FEATURE
#define PHONEBUTTONMODE_FEATURE 0x00000004
#endif
#ifndef PHONEBUTTONMODE_KEYPAD
#define PHONEBUTTONMODE_KEYPAD 0x00000008
#endif
#ifndef PHONEBUTTONMODE_LOCAL
#define PHONEBUTTONMODE_LOCAL 0x00000010
#endif
#ifndef PHONEBUTTONSTATE_DOWN
#define PHONEBUTTONSTATE_DOWN 0x00000002
#endif
#ifndef PHONEBUTTONSTATE_UNAVAIL
#define PHONEBUTTONSTATE_UNAVAIL 0x00000008
#endif
#ifndef PHONEBUTTONSTATE_UNKNOWN
#define PHONEBUTTONSTATE_UNKNOWN 0x00000004
#endif
#ifndef PHONEBUTTONSTATE_UP
#define PHONEBUTTONSTATE_UP 0x00000001
#endif
#ifndef PHONEERR_ALLOCATED
#define PHONEERR_ALLOCATED 0x90000001
#endif
#ifndef PHONEERR_BADDEVICEID
#define PHONEERR_BADDEVICEID 0x90000002
#endif
#ifndef PHONEERR_DISCONNECTED
#define PHONEERR_DISCONNECTED 0x90000024
#endif
#ifndef PHONEERR_INCOMPATIBLEAPIVERSION
#define PHONEERR_INCOMPATIBLEAPIVERSION 0x90000003
#endif
#ifndef PHONEERR_INCOMPATIBLEEXTVERSION
#define PHONEERR_INCOMPATIBLEEXTVERSION 0x90000004
#endif
#ifndef PHONEERR_INIFILECORRUPT
#define PHONEERR_INIFILECORRUPT 0x90000005
#endif
#ifndef PHONEERR_INUSE
#define PHONEERR_INUSE 0x90000006
#endif
#ifndef PHONEERR_INVALAPPHANDLE
#define PHONEERR_INVALAPPHANDLE 0x90000007
#endif
#ifndef PHONEERR_INVALAPPNAME
#define PHONEERR_INVALAPPNAME 0x90000008
#endif
#ifndef PHONEERR_INVALBUTTONLAMPID
#define PHONEERR_INVALBUTTONLAMPID 0x90000009
#endif
#ifndef PHONEERR_INVALBUTTONMODE
#define PHONEERR_INVALBUTTONMODE 0x9000000A
#endif
#ifndef PHONEERR_INVALBUTTONSTATE
#define PHONEERR_INVALBUTTONSTATE 0x9000000B
#endif
#ifndef PHONEERR_INVALDATAID
#define PHONEERR_INVALDATAID 0x9000000C
#endif
#ifndef PHONEERR_INVALDEVICECLASS
#define PHONEERR_INVALDEVICECLASS 0x9000000D
#endif
#ifndef PHONEERR_INVALEXTVERSION
#define PHONEERR_INVALEXTVERSION 0x9000000E
#endif
#ifndef PHONEERR_INVALHOOKSWITCHDEV
#define PHONEERR_INVALHOOKSWITCHDEV 0x9000000F
#endif
#ifndef PHONEERR_INVALHOOKSWITCHMODE
#define PHONEERR_INVALHOOKSWITCHMODE 0x90000010
#endif
#ifndef PHONEERR_INVALLAMPMODE
#define PHONEERR_INVALLAMPMODE 0x90000011
#endif
#ifndef PHONEERR_INVALPARAM
#define PHONEERR_INVALPARAM 0x90000012
#endif
#ifndef PHONEERR_INVALPHONEHANDLE
#define PHONEERR_INVALPHONEHANDLE 0x90000013
#endif
#ifndef PHONEERR_INVALPHONESTATE
#define PHONEERR_INVALPHONESTATE 0x90000014
#endif
#ifndef PHONEERR_INVALPOINTER
#define PHONEERR_INVALPOINTER 0x90000015
#endif
#ifndef PHONEERR_INVALPRIVILEGE
#define PHONEERR_INVALPRIVILEGE 0x90000016
#endif
#ifndef PHONEERR_INVALRINGMODE
#define PHONEERR_INVALRINGMODE 0x90000017
#endif
#ifndef PHONEERR_NODEVICE
#define PHONEERR_NODEVICE 0x90000018
#endif
#ifndef PHONEERR_NODRIVER
#define PHONEERR_NODRIVER 0x90000019
#endif
#ifndef PHONEERR_NOMEM
#define PHONEERR_NOMEM 0x9000001A
#endif
#ifndef PHONEERR_NOTOWNER
#define PHONEERR_NOTOWNER 0x9000001B
#endif
#ifndef PHONEERR_OPERATIONFAILED
#define PHONEERR_OPERATIONFAILED 0x9000001C
#endif
#ifndef PHONEERR_OPERATIONUNAVAIL
#define PHONEERR_OPERATIONUNAVAIL 0x9000001D
#endif
#ifndef PHONEERR_REINIT
#define PHONEERR_REINIT 0x90000023
#endif
#ifndef PHONEERR_REQUESTOVERRUN
#define PHONEERR_REQUESTOVERRUN 0x90000020
#endif
#ifndef PHONEERR_RESOURCEUNAVAIL
#define PHONEERR_RESOURCEUNAVAIL 0x9000001F
#endif
#ifndef PHONEERR_SERVICE_NOT_RUNNING
#define PHONEERR_SERVICE_NOT_RUNNING 0x90000025
#endif
#ifndef PHONEERR_STRUCTURETOOSMALL
#define PHONEERR_STRUCTURETOOSMALL 0x90000021
#endif
#ifndef PHONEERR_UNINITIALIZED
#define PHONEERR_UNINITIALIZED 0x90000022
#endif
#ifndef PHONEFEATURE_GETBUTTONINFO
#define PHONEFEATURE_GETBUTTONINFO 0x00000001
#endif
#ifndef PHONEFEATURE_GETDATA
#define PHONEFEATURE_GETDATA 0x00000002
#endif
#ifndef PHONEFEATURE_GETDISPLAY
#define PHONEFEATURE_GETDISPLAY 0x00000004
#endif
#ifndef PHONEFEATURE_GETGAINHANDSET
#define PHONEFEATURE_GETGAINHANDSET 0x00000008
#endif
#ifndef PHONEFEATURE_GETGAINHEADSET
#define PHONEFEATURE_GETGAINHEADSET 0x00000020
#endif
#ifndef PHONEFEATURE_GETGAINSPEAKER
#define PHONEFEATURE_GETGAINSPEAKER 0x00000010
#endif
#ifndef PHONEFEATURE_GETHOOKSWITCHHANDSET
#define PHONEFEATURE_GETHOOKSWITCHHANDSET 0x00000040
#endif
#ifndef PHONEFEATURE_GETHOOKSWITCHHEADSET
#define PHONEFEATURE_GETHOOKSWITCHHEADSET 0x00000100
#endif
#ifndef PHONEFEATURE_GETHOOKSWITCHSPEAKER
#define PHONEFEATURE_GETHOOKSWITCHSPEAKER 0x00000080
#endif
#ifndef PHONEFEATURE_GETLAMP
#define PHONEFEATURE_GETLAMP 0x00000200
#endif
#ifndef PHONEFEATURE_GETRING
#define PHONEFEATURE_GETRING 0x00000400
#endif
#ifndef PHONEFEATURE_GETVOLUMEHANDSET
#define PHONEFEATURE_GETVOLUMEHANDSET 0x00000800
#endif
#ifndef PHONEFEATURE_GETVOLUMEHEADSET
#define PHONEFEATURE_GETVOLUMEHEADSET 0x00002000
#endif
#ifndef PHONEFEATURE_GETVOLUMESPEAKER
#define PHONEFEATURE_GETVOLUMESPEAKER 0x00001000
#endif
#ifndef PHONEFEATURE_SETBUTTONINFO
#define PHONEFEATURE_SETBUTTONINFO 0x00004000
#endif
#ifndef PHONEFEATURE_SETDATA
#define PHONEFEATURE_SETDATA 0x00008000
#endif
#ifndef PHONEFEATURE_SETDISPLAY
#define PHONEFEATURE_SETDISPLAY 0x00010000
#endif
#ifndef PHONEFEATURE_SETGAINHANDSET
#define PHONEFEATURE_SETGAINHANDSET 0x00020000
#endif
#ifndef PHONEFEATURE_SETGAINHEADSET
#define PHONEFEATURE_SETGAINHEADSET 0x00080000
#endif
#ifndef PHONEFEATURE_SETGAINSPEAKER
#define PHONEFEATURE_SETGAINSPEAKER 0x00040000
#endif
#ifndef PHONEFEATURE_SETHOOKSWITCHHANDSET
#define PHONEFEATURE_SETHOOKSWITCHHANDSET 0x00100000
#endif
#ifndef PHONEFEATURE_SETHOOKSWITCHHEADSET
#define PHONEFEATURE_SETHOOKSWITCHHEADSET 0x00400000
#endif
#ifndef PHONEFEATURE_SETHOOKSWITCHSPEAKER
#define PHONEFEATURE_SETHOOKSWITCHSPEAKER 0x00200000
#endif
#ifndef PHONEFEATURE_SETLAMP
#define PHONEFEATURE_SETLAMP 0x00800000
#endif
#ifndef PHONEFEATURE_SETRING
#define PHONEFEATURE_SETRING 0x01000000
#endif
#ifndef PHONEFEATURE_SETVOLUMEHANDSET
#define PHONEFEATURE_SETVOLUMEHANDSET 0x02000000
#endif
#ifndef PHONEFEATURE_SETVOLUMEHEADSET
#define PHONEFEATURE_SETVOLUMEHEADSET 0x08000000
#endif
#ifndef PHONEFEATURE_SETVOLUMESPEAKER
#define PHONEFEATURE_SETVOLUMESPEAKER 0x04000000
#endif
#ifndef PHONEHOOKSWITCHDEV_HANDSET
#define PHONEHOOKSWITCHDEV_HANDSET 0x00000001
#endif
#ifndef PHONEHOOKSWITCHDEV_HEADSET
#define PHONEHOOKSWITCHDEV_HEADSET 0x00000004
#endif
#ifndef PHONEHOOKSWITCHDEV_SPEAKER
#define PHONEHOOKSWITCHDEV_SPEAKER 0x00000002
#endif
#ifndef PHONEHOOKSWITCHMODE_MIC
#define PHONEHOOKSWITCHMODE_MIC 0x00000002
#endif
#ifndef PHONEHOOKSWITCHMODE_MICSPEAKER
#define PHONEHOOKSWITCHMODE_MICSPEAKER 0x00000008
#endif
#ifndef PHONEHOOKSWITCHMODE_ONHOOK
#define PHONEHOOKSWITCHMODE_ONHOOK 0x00000001
#endif
#ifndef PHONEHOOKSWITCHMODE_SPEAKER
#define PHONEHOOKSWITCHMODE_SPEAKER 0x00000004
#endif
#ifndef PHONEHOOKSWITCHMODE_UNKNOWN
#define PHONEHOOKSWITCHMODE_UNKNOWN 0x00000010
#endif
#ifndef PHONEINITIALIZEEXOPTION_USECOMPLETIONPORT
#define PHONEINITIALIZEEXOPTION_USECOMPLETIONPORT 0x00000003
#endif
#ifndef PHONEINITIALIZEEXOPTION_USEEVENT
#define PHONEINITIALIZEEXOPTION_USEEVENT 0x00000002
#endif
#ifndef PHONEINITIALIZEEXOPTION_USEHIDDENWINDOW
#define PHONEINITIALIZEEXOPTION_USEHIDDENWINDOW 0x00000001
#endif
#ifndef PHONELAMPMODE_BROKENFLUTTER
#define PHONELAMPMODE_BROKENFLUTTER 0x00000040
#endif
#ifndef PHONELAMPMODE_DUMMY
#define PHONELAMPMODE_DUMMY 0x00000001
#endif
#ifndef PHONELAMPMODE_FLASH
#define PHONELAMPMODE_FLASH 0x00000010
#endif
#ifndef PHONELAMPMODE_FLUTTER
#define PHONELAMPMODE_FLUTTER 0x00000020
#endif
#ifndef PHONELAMPMODE_OFF
#define PHONELAMPMODE_OFF 0x00000002
#endif
#ifndef PHONELAMPMODE_STEADY
#define PHONELAMPMODE_STEADY 0x00000004
#endif
#ifndef PHONELAMPMODE_UNKNOWN
#define PHONELAMPMODE_UNKNOWN 0x00000080
#endif
#ifndef PHONELAMPMODE_WINK
#define PHONELAMPMODE_WINK 0x00000008
#endif
#ifndef PHONEPRIVILEGE_MONITOR
#define PHONEPRIVILEGE_MONITOR 0x00000001
#endif
#ifndef PHONEPRIVILEGE_OWNER
#define PHONEPRIVILEGE_OWNER 0x00000002
#endif
#ifndef PHONESTATE_CAPSCHANGE
#define PHONESTATE_CAPSCHANGE 0x00400000
#endif
#ifndef PHONESTATE_CONNECTED
#define PHONESTATE_CONNECTED 0x00000002
#endif
#ifndef PHONESTATE_DEVSPECIFIC
#define PHONESTATE_DEVSPECIFIC 0x00100000
#endif
#ifndef PHONESTATE_DISCONNECTED
#define PHONESTATE_DISCONNECTED 0x00000004
#endif
#ifndef PHONESTATE_DISPLAY
#define PHONESTATE_DISPLAY 0x00000020
#endif
#ifndef PHONESTATE_HANDSETGAIN
#define PHONESTATE_HANDSETGAIN 0x00000800
#endif
#ifndef PHONESTATE_HANDSETHOOKSWITCH
#define PHONESTATE_HANDSETHOOKSWITCH 0x00000200
#endif
#ifndef PHONESTATE_HANDSETVOLUME
#define PHONESTATE_HANDSETVOLUME 0x00000400
#endif
#ifndef PHONESTATE_HEADSETGAIN
#define PHONESTATE_HEADSETGAIN 0x00020000
#endif
#ifndef PHONESTATE_HEADSETHOOKSWITCH
#define PHONESTATE_HEADSETHOOKSWITCH 0x00008000
#endif
#ifndef PHONESTATE_HEADSETVOLUME
#define PHONESTATE_HEADSETVOLUME 0x00010000
#endif
#ifndef PHONESTATE_LAMP
#define PHONESTATE_LAMP 0x00000040
#endif
#ifndef PHONESTATE_MONITORS
#define PHONESTATE_MONITORS 0x00000010
#endif
#ifndef PHONESTATE_OTHER
#define PHONESTATE_OTHER 0x00000001
#endif
#ifndef PHONESTATE_OWNER
#define PHONESTATE_OWNER 0x00000008
#endif
#ifndef PHONESTATE_REINIT
#define PHONESTATE_REINIT 0x00200000
#endif
#ifndef PHONESTATE_REMOVED
#define PHONESTATE_REMOVED 0x00800000
#endif
#ifndef PHONESTATE_RESUME
#define PHONESTATE_RESUME 0x00080000
#endif
#ifndef PHONESTATE_RINGMODE
#define PHONESTATE_RINGMODE 0x00000080
#endif
#ifndef PHONESTATE_RINGVOLUME
#define PHONESTATE_RINGVOLUME 0x00000100
#endif
#ifndef PHONESTATE_SPEAKERGAIN
#define PHONESTATE_SPEAKERGAIN 0x00004000
#endif
#ifndef PHONESTATE_SPEAKERHOOKSWITCH
#define PHONESTATE_SPEAKERHOOKSWITCH 0x00001000
#endif
#ifndef PHONESTATE_SPEAKERVOLUME
#define PHONESTATE_SPEAKERVOLUME 0x00002000
#endif
#ifndef PHONESTATE_SUSPEND
#define PHONESTATE_SUSPEND 0x00040000
#endif
#ifndef PHONESTATUSFLAGS_CONNECTED
#define PHONESTATUSFLAGS_CONNECTED 0x00000001
#endif
#ifndef PHONESTATUSFLAGS_SUSPENDED
#define PHONESTATUSFLAGS_SUSPENDED 0x00000002
#endif
#ifndef TAPIERR_CONNECTED
#define TAPIERR_CONNECTED 0L
#endif
#ifndef TAPIERR_DESTBUSY
#define TAPIERR_DESTBUSY (-11L)
#endif
#ifndef TAPIERR_DESTNOANSWER
#define TAPIERR_DESTNOANSWER (-12L)
#endif
#ifndef TAPIERR_DESTUNAVAIL
#define TAPIERR_DESTUNAVAIL (-13L)
#endif
#ifndef TAPIERR_DEVICECLASSUNAVAIL
#define TAPIERR_DEVICECLASSUNAVAIL (-8L)
#endif
#ifndef TAPIERR_DEVICEIDUNAVAIL
#define TAPIERR_DEVICEIDUNAVAIL (-9L)
#endif
#ifndef TAPIERR_DEVICEINUSE
#define TAPIERR_DEVICEINUSE (-10L)
#endif
#ifndef TAPIERR_DROPPED
#define TAPIERR_DROPPED (-1L)
#endif
#ifndef TAPIERR_INVALDESTADDRESS
#define TAPIERR_INVALDESTADDRESS (-4L)
#endif
#ifndef TAPIERR_INVALDEVICECLASS
#define TAPIERR_INVALDEVICECLASS (-6L)
#endif
#ifndef TAPIERR_INVALDEVICEID
#define TAPIERR_INVALDEVICEID (-7L)
#endif
#ifndef TAPIERR_INVALPOINTER
#define TAPIERR_INVALPOINTER (-18L)
#endif
#ifndef TAPIERR_INVALWINDOWHANDLE
#define TAPIERR_INVALWINDOWHANDLE (-5L)
#endif
#ifndef TAPIERR_MMCWRITELOCKED
#define TAPIERR_MMCWRITELOCKED (-20L)
#endif
#ifndef TAPIERR_NOREQUESTRECIPIENT
#define TAPIERR_NOREQUESTRECIPIENT (-2L)
#endif
#ifndef TAPIERR_NOTADMIN
#define TAPIERR_NOTADMIN (-19L)
#endif
#ifndef TAPIERR_PROVIDERALREADYINSTALLED
#define TAPIERR_PROVIDERALREADYINSTALLED (-21L)
#endif
#ifndef TAPIERR_REQUESTCANCELLED
#define TAPIERR_REQUESTCANCELLED (-17L)
#endif
#ifndef TAPIERR_REQUESTQUEUEFULL
#define TAPIERR_REQUESTQUEUEFULL (-3L)
#endif
#ifndef TAPIERR_SCP_ALREADY_EXISTS
#define TAPIERR_SCP_ALREADY_EXISTS (-22L)
#endif
#ifndef TAPIERR_SCP_DOES_NOT_EXIST
#define TAPIERR_SCP_DOES_NOT_EXIST (-23L)
#endif
#ifndef TAPIERR_UNKNOWNREQUESTID
#define TAPIERR_UNKNOWNREQUESTID (-15L)
#endif
#ifndef TAPIERR_UNKNOWNWINHANDLE
#define TAPIERR_UNKNOWNWINHANDLE (-14L)
#endif
#ifndef TAPIMAXAPPNAMESIZE
#define TAPIMAXAPPNAMESIZE 40L
#endif
#ifndef TAPIMAXCALLEDPARTYSIZE
#define TAPIMAXCALLEDPARTYSIZE 40L
#endif
#ifndef TAPIMAXCOMMENTSIZE
#define TAPIMAXCOMMENTSIZE 80L
#endif
#ifndef TAPIMAXDESTADDRESSSIZE
#define TAPIMAXDESTADDRESSSIZE 80L
#endif
#ifndef TAPIMAXDEVICECLASSSIZE
#define TAPIMAXDEVICECLASSSIZE 40L
#endif
#ifndef TAPIMAXDEVICEIDSIZE
#define TAPIMAXDEVICEIDSIZE 40L
#endif

#endif /* __WINE_TAPI_H */
