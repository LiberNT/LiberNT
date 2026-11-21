/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Standard event levels of the Windows event metadata
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WINEVENT_LEVEL_LOG_ALWAYS   0x0
#define WINEVENT_LEVEL_CRITICAL     0x1
#define WINEVENT_LEVEL_ERROR        0x2
#define WINEVENT_LEVEL_WARNING      0x3
#define WINEVENT_LEVEL_INFO         0x4
#define WINEVENT_LEVEL_VERBOSE      0x5

#ifndef MSG_category_Devices
#define MSG_category_Devices 0x00000001L

#define WINEVENT_OPCODE_START 1
#define WINEVENT_OPCODE_STOP 2

#endif
#ifndef MSG_category_Disk
#define MSG_category_Disk 0x00000002L
#endif
#ifndef MSG_category_Network
#define MSG_category_Network 0x00000007L
#endif
#ifndef MSG_category_Printers
#define MSG_category_Printers 0x00000003L
#endif
#ifndef MSG_category_Services
#define MSG_category_Services 0x00000004L
#endif
#ifndef MSG_category_Shell
#define MSG_category_Shell 0x00000005L
#endif
#ifndef MSG_category_SystemEvent
#define MSG_category_SystemEvent 0x00000006L
#endif
#ifndef MSG_channel_Application
#define MSG_channel_Application 0x00000100L
#endif
#ifndef MSG_channel_ProviderMetadata
#define MSG_channel_ProviderMetadata 0x90000003L
#endif
#ifndef MSG_channel_Security
#define MSG_channel_Security 0x00000101L
#endif
#ifndef MSG_channel_System
#define MSG_channel_System 0x00000102L
#endif
#ifndef MSG_channel_TraceClassic
#define MSG_channel_TraceClassic 0x90000001L
#endif
#ifndef MSG_channel_TraceLogging
#define MSG_channel_TraceLogging 0x90000002L
#endif
#ifndef MSG_keyword_AnyKeyword
#define MSG_keyword_AnyKeyword 0x10000000L
#endif
#ifndef MSG_keyword_AuditFailure
#define MSG_keyword_AuditFailure 0x10000035L
#endif
#ifndef MSG_keyword_AuditSuccess
#define MSG_keyword_AuditSuccess 0x10000036L
#endif
#ifndef MSG_keyword_Classic
#define MSG_keyword_Classic 0x10000038L
#endif
#ifndef MSG_keyword_CorrelationHint
#define MSG_keyword_CorrelationHint 0x10000037L
#endif
#ifndef MSG_keyword_ResponseTime
#define MSG_keyword_ResponseTime 0x10000031L
#endif
#ifndef MSG_keyword_SQM
#define MSG_keyword_SQM 0x10000034L
#endif
#ifndef MSG_keyword_WDIDiag
#define MSG_keyword_WDIDiag 0x10000033L
#endif
#ifndef MSG_level_Critical
#define MSG_level_Critical 0x50000001L
#endif
#ifndef MSG_level_Error
#define MSG_level_Error 0x50000002L
#endif
#ifndef MSG_level_Informational
#define MSG_level_Informational 0x50000004L
#endif
#ifndef MSG_level_LogAlways
#define MSG_level_LogAlways 0x50000000L
#endif
#ifndef MSG_level_Verbose
#define MSG_level_Verbose 0x50000005L
#endif
#ifndef MSG_level_Warning
#define MSG_level_Warning 0x50000003L
#endif
#ifndef MSG_opcode_DCStart
#define MSG_opcode_DCStart 0x30030000L
#endif
#ifndef MSG_opcode_DCStop
#define MSG_opcode_DCStop 0x30040000L
#endif
#ifndef MSG_opcode_Extension
#define MSG_opcode_Extension 0x30050000L
#endif
#ifndef MSG_opcode_Info
#define MSG_opcode_Info 0x30000000L
#endif
#ifndef MSG_opcode_Receive
#define MSG_opcode_Receive 0x30F00000L
#endif
#ifndef MSG_opcode_Reply
#define MSG_opcode_Reply 0x30060000L
#endif
#ifndef MSG_opcode_Resume
#define MSG_opcode_Resume 0x30070000L
#endif
#ifndef MSG_opcode_Send
#define MSG_opcode_Send 0x30090000L
#endif
#ifndef MSG_opcode_Start
#define MSG_opcode_Start 0x30010000L
#endif
#ifndef MSG_opcode_Stop
#define MSG_opcode_Stop 0x30020000L
#endif
#ifndef MSG_opcode_Suspend
#define MSG_opcode_Suspend 0x30080000L
#endif
#ifndef MSG_task_None
#define MSG_task_None 0x70000000L
#endif
#ifndef WINEVENT_CHANNEL_CLASSIC_TRACE
#define WINEVENT_CHANNEL_CLASSIC_TRACE 0x0
#endif
#ifndef WINEVENT_CHANNEL_GLOBAL_APPLICATION
#define WINEVENT_CHANNEL_GLOBAL_APPLICATION 0x9
#endif
#ifndef WINEVENT_CHANNEL_GLOBAL_SECURITY
#define WINEVENT_CHANNEL_GLOBAL_SECURITY 0xa
#endif
#ifndef WINEVENT_CHANNEL_GLOBAL_SYSTEM
#define WINEVENT_CHANNEL_GLOBAL_SYSTEM 0x8
#endif
#ifndef WINEVENT_CHANNEL_PROVIDERMETADATA
#define WINEVENT_CHANNEL_PROVIDERMETADATA 0xc
#endif
#ifndef WINEVENT_CHANNEL_TRACELOGGING
#define WINEVENT_CHANNEL_TRACELOGGING 0xb
#endif
#ifndef WINEVENT_KEYWORD_AUDIT_FAILURE
#define WINEVENT_KEYWORD_AUDIT_FAILURE 0x10000000000000
#endif
#ifndef WINEVENT_KEYWORD_AUDIT_SUCCESS
#define WINEVENT_KEYWORD_AUDIT_SUCCESS 0x20000000000000
#endif
#ifndef WINEVENT_KEYWORD_CORRELATION_HINT
#define WINEVENT_KEYWORD_CORRELATION_HINT 0x40000000000000
#endif
#ifndef WINEVENT_KEYWORD_EVENTLOG_CLASSIC
#define WINEVENT_KEYWORD_EVENTLOG_CLASSIC 0x80000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_49
#define WINEVENT_KEYWORD_RESERVED_49 0x2000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_56
#define WINEVENT_KEYWORD_RESERVED_56 0x100000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_57
#define WINEVENT_KEYWORD_RESERVED_57 0x200000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_58
#define WINEVENT_KEYWORD_RESERVED_58 0x400000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_59
#define WINEVENT_KEYWORD_RESERVED_59 0x800000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_60
#define WINEVENT_KEYWORD_RESERVED_60 0x1000000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_61
#define WINEVENT_KEYWORD_RESERVED_61 0x2000000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_62
#define WINEVENT_KEYWORD_RESERVED_62 0x4000000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESERVED_63
#define WINEVENT_KEYWORD_RESERVED_63 0x8000000000000000
#endif
#ifndef WINEVENT_KEYWORD_RESPONSE_TIME
#define WINEVENT_KEYWORD_RESPONSE_TIME 0x1000000000000
#endif
#ifndef WINEVENT_KEYWORD_SQM
#define WINEVENT_KEYWORD_SQM 0x8000000000000
#endif
#ifndef WINEVENT_KEYWORD_WDI_DIAG
#define WINEVENT_KEYWORD_WDI_DIAG 0x4000000000000
#endif
#ifndef WINEVENT_LEVEL_RESERVED_10
#define WINEVENT_LEVEL_RESERVED_10 0xa
#endif
#ifndef WINEVENT_LEVEL_RESERVED_11
#define WINEVENT_LEVEL_RESERVED_11 0xb
#endif
#ifndef WINEVENT_LEVEL_RESERVED_12
#define WINEVENT_LEVEL_RESERVED_12 0xc
#endif
#ifndef WINEVENT_LEVEL_RESERVED_13
#define WINEVENT_LEVEL_RESERVED_13 0xd
#endif
#ifndef WINEVENT_LEVEL_RESERVED_14
#define WINEVENT_LEVEL_RESERVED_14 0xe
#endif
#ifndef WINEVENT_LEVEL_RESERVED_15
#define WINEVENT_LEVEL_RESERVED_15 0xf
#endif
#ifndef WINEVENT_LEVEL_RESERVED_6
#define WINEVENT_LEVEL_RESERVED_6 0x6
#endif
#ifndef WINEVENT_LEVEL_RESERVED_7
#define WINEVENT_LEVEL_RESERVED_7 0x7
#endif
#ifndef WINEVENT_LEVEL_RESERVED_8
#define WINEVENT_LEVEL_RESERVED_8 0x8
#endif
#ifndef WINEVENT_LEVEL_RESERVED_9
#define WINEVENT_LEVEL_RESERVED_9 0x9
#endif
#ifndef WINEVENT_OPCODE_DC_START
#define WINEVENT_OPCODE_DC_START 0x3
#endif
#ifndef WINEVENT_OPCODE_DC_STOP
#define WINEVENT_OPCODE_DC_STOP 0x4
#endif
#ifndef WINEVENT_OPCODE_EXTENSION
#define WINEVENT_OPCODE_EXTENSION 0x5
#endif
#ifndef WINEVENT_OPCODE_INFO
#define WINEVENT_OPCODE_INFO 0x0
#endif
#ifndef WINEVENT_OPCODE_RECEIVE
#define WINEVENT_OPCODE_RECEIVE 0xf0
#endif
#ifndef WINEVENT_OPCODE_REPLY
#define WINEVENT_OPCODE_REPLY 0x6
#endif
#ifndef WINEVENT_OPCODE_RESERVED_241
#define WINEVENT_OPCODE_RESERVED_241 0xf1
#endif
#ifndef WINEVENT_OPCODE_RESERVED_242
#define WINEVENT_OPCODE_RESERVED_242 0xf2
#endif
#ifndef WINEVENT_OPCODE_RESERVED_243
#define WINEVENT_OPCODE_RESERVED_243 0xf3
#endif
#ifndef WINEVENT_OPCODE_RESERVED_244
#define WINEVENT_OPCODE_RESERVED_244 0xf4
#endif
#ifndef WINEVENT_OPCODE_RESERVED_245
#define WINEVENT_OPCODE_RESERVED_245 0xf5
#endif
#ifndef WINEVENT_OPCODE_RESERVED_246
#define WINEVENT_OPCODE_RESERVED_246 0xf6
#endif
#ifndef WINEVENT_OPCODE_RESERVED_247
#define WINEVENT_OPCODE_RESERVED_247 0xf7
#endif
#ifndef WINEVENT_OPCODE_RESERVED_248
#define WINEVENT_OPCODE_RESERVED_248 0xf8
#endif
#ifndef WINEVENT_OPCODE_RESERVED_249
#define WINEVENT_OPCODE_RESERVED_249 0xf9
#endif
#ifndef WINEVENT_OPCODE_RESERVED_250
#define WINEVENT_OPCODE_RESERVED_250 0xfa
#endif
#ifndef WINEVENT_OPCODE_RESERVED_251
#define WINEVENT_OPCODE_RESERVED_251 0xfb
#endif
#ifndef WINEVENT_OPCODE_RESERVED_252
#define WINEVENT_OPCODE_RESERVED_252 0xfc
#endif
#ifndef WINEVENT_OPCODE_RESERVED_253
#define WINEVENT_OPCODE_RESERVED_253 0xfd
#endif
#ifndef WINEVENT_OPCODE_RESERVED_254
#define WINEVENT_OPCODE_RESERVED_254 0xfe
#endif
#ifndef WINEVENT_OPCODE_RESERVED_255
#define WINEVENT_OPCODE_RESERVED_255 0xff
#endif
#ifndef WINEVENT_OPCODE_RESUME
#define WINEVENT_OPCODE_RESUME 0x7
#endif
#ifndef WINEVENT_OPCODE_SEND
#define WINEVENT_OPCODE_SEND 0x9
#endif
#ifndef WINEVENT_OPCODE_SUSPEND
#define WINEVENT_OPCODE_SUSPEND 0x8
#endif
#ifndef WINEVENT_TASK_NONE
#define WINEVENT_TASK_NONE 0x0
#endif
#ifndef WINEVT_KEYWORD_ANY
#define WINEVT_KEYWORD_ANY 0x0
#endif
