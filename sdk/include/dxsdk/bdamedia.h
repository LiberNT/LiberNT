/*
 * bdamedia.h
 *
 * This file is part of the ReactOS DXSDK package.
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

#ifndef _BDAMEDIA_
#define _BDAMEDIA_

#include <ksmedia.h>
#include <bdatypes.h>

typedef struct _KSP_BDA_NODE_PIN {
  KSPROPERTY Property;
  ULONG ulNodeType;
  ULONG ulInputPinId;
  ULONG ulOutputPinId;
} KSP_BDA_NODE_PIN, *PKSP_BDA_NODE_PIN;

typedef struct _KSM_BDA_PIN {
  KSMETHOD Method;
  __MINGW_EXTENSION union {
    ULONG PinId;
    ULONG PinType;
  };
  ULONG Reserved;
} KSM_BDA_PIN, *PKSM_BDA_PIN;

typedef struct _KSM_BDA_PIN_PAIR {
  KSMETHOD Method;
  __MINGW_EXTENSION union {
    ULONG InputPinId;
    ULONG InputPinType;
  };
  __MINGW_EXTENSION union {
    ULONG OutputPinId;
    ULONG OutputPinType;
  };
} KSM_BDA_PIN_PAIR, *PKSM_BDA_PIN_PAIR;


/* ------------------------------------------------------------
    BDA Topology Property Set {A14EE835-0A23-11d3-9CC7-00C04F7971E0}
*/

#define STATIC_KSPROPSETID_BdaTopology \
  0xa14ee835, 0x0a23, 0x11d3, 0x9c, 0xc7, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0

DEFINE_GUIDSTRUCT("A14EE835-0A23-11d3-9CC7-00C04F7971E0", KSPROPSETID_BdaTopology);

#define KSPROPSETID_BdaTopology DEFINE_GUIDNAMED(KSPROPSETID_BdaTopology)

typedef enum {
  KSPROPERTY_BDA_NODE_TYPES,
  KSPROPERTY_BDA_PIN_TYPES,
  KSPROPERTY_BDA_TEMPLATE_CONNECTIONS,
  KSPROPERTY_BDA_NODE_METHODS,
  KSPROPERTY_BDA_NODE_PROPERTIES,
  KSPROPERTY_BDA_NODE_EVENTS,
  KSPROPERTY_BDA_CONTROLLING_PIN_ID,
  KSPROPERTY_BDA_NODE_DESCRIPTORS
} KSPROPERTY_BDA_TOPOLOGY;

#define DEFINE_KSPROPERTY_ITEM_BDA_NODE_TYPES(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NODE_TYPES,                   \
                         (GetHandler),                                \
                         sizeof(KSPROPERTY),                          \
                         0,                                           \
                         FALSE,                                       \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_PIN_TYPES(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIN_TYPES,                   \
                         (GetHandler),                               \
                         sizeof(KSPROPERTY),                         \
                         0,                                          \
                         FALSE,                                      \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_TEMPLATE_CONNECTIONS(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_TEMPLATE_CONNECTIONS,                   \
                         (GetHandler),                                          \
                         sizeof(KSPROPERTY),                                    \
                         sizeof( BDA_TEMPLATE_CONNECTION),                      \
                         FALSE,                                                 \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_NODE_METHODS(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NODE_METHODS,                   \
                         (GetHandler),                                  \
                         sizeof(KSP_NODE),                              \
                         0,                                             \
                         FALSE,                                         \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_NODE_PROPERTIES(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NODE_PROPERTIES,                   \
                         (GetHandler),                                     \
                         sizeof(KSP_NODE),                                 \
                         0,                                                \
                         FALSE,                                            \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_NODE_EVENTS(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NODE_EVENTS,                   \
                         (GetHandler),                                 \
                         sizeof(KSP_NODE),                             \
                         0,                                            \
                         FALSE,                                        \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_CONTROLLING_PIN_ID(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CONTROLLING_PIN_ID,                   \
                         (GetHandler),                                        \
                         sizeof(KSP_BDA_NODE_PIN),                            \
                         sizeof( ULONG),                                      \
                         FALSE,                                               \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_NODE_DESCRIPTORS(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NODE_DESCRIPTORS,                   \
                         (GetHandler),                                      \
                         sizeof(KSPROPERTY),                                \
                         0,                                                 \
                         FALSE,                                             \
                         NULL, 0, NULL, NULL, 0)


/* ------------------------------------------------------------
    BDA Device Configuration Method Set {71985F45-1CA1-11d3-9CC8-00C04F7971E0}
*/

#define STATIC_KSMETHODSETID_BdaDeviceConfiguration \
  0x71985f45, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0

DEFINE_GUIDSTRUCT("71985F45-1CA1-11d3-9CC8-00C04F7971E0", KSMETHODSETID_BdaDeviceConfiguration);

#define KSMETHODSETID_BdaDeviceConfiguration DEFINE_GUIDNAMED(KSMETHODSETID_BdaDeviceConfiguration)

typedef enum {
  KSMETHOD_BDA_CREATE_PIN_FACTORY = 0,
  KSMETHOD_BDA_DELETE_PIN_FACTORY,
  KSMETHOD_BDA_CREATE_TOPOLOGY
} KSMETHOD_BDA_DEVICE_CONFIGURATION;

#define DEFINE_KSMETHOD_ITEM_BDA_CREATE_PIN_FACTORY(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CREATE_PIN_FACTORY,                            \
                       KSMETHOD_TYPE_READ,                                         \
                       (MethodHandler),                                            \
                       sizeof(KSM_BDA_PIN),                                        \
                       sizeof(ULONG),                                              \
                       SupportHandler)

#define DEFINE_KSMETHOD_ITEM_BDA_DELETE_PIN_FACTORY(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_DELETE_PIN_FACTORY,                            \
                       KSMETHOD_TYPE_NONE,                                         \
                       (MethodHandler),                                            \
                       sizeof(KSM_BDA_PIN),                                        \
                       0,                                                          \
                       SupportHandler)

#define DEFINE_KSMETHOD_ITEM_BDA_CREATE_TOPOLOGY(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CREATE_TOPOLOGY,                            \
                       KSMETHOD_TYPE_WRITE,                                     \
                       (MethodHandler),                                         \
                       sizeof(KSM_BDA_PIN_PAIR),                                \
                       0,                                                       \
                       SupportHandler)


/* ------------------------------------------------------------
  BDA Pin Control Property {0DED49D5-A8B7-4d5d-97A1-12B0C195874D}
*/

#define STATIC_KSPROPSETID_BdaPinControl \
  0xded49d5, 0xa8b7, 0x4d5d, 0x97, 0xa1, 0x12, 0xb0, 0xc1, 0x95, 0x87, 0x4d

DEFINE_GUIDSTRUCT("0DED49D5-A8B7-4d5d-97A1-12B0C195874D", KSPROPSETID_BdaPinControl);

#define KSPROPSETID_BdaPinControl DEFINE_GUIDNAMED(KSPROPSETID_BdaPinControl)

typedef enum {
  KSPROPERTY_BDA_PIN_ID = 0,
  KSPROPERTY_BDA_PIN_TYPE
} KSPROPERTY_BDA_PIN_CONTROL;

#define DEFINE_KSPROPERTY_ITEM_BDA_PIN_ID(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIN_ID,                   \
                         (GetHandler),                            \
                         sizeof( KSPROPERTY),                     \
                         sizeof( ULONG),                          \
                         FALSE,                                   \
                         NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_BDA_PIN_TYPE(GetHandler, SetHandler) \
  DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIN_TYPE,                   \
                         (GetHandler),                              \
                         sizeof( KSPROPERTY),                       \
                         sizeof( ULONG),                            \
                         FALSE,                                     \
                         NULL, 0, NULL, NULL, 0)


/* ------------------------------------------------------------
  BDA Change Sync Method Set {FD0A5AF3-B41D-11d2-9C95-00C04F7971E0}
*/

#define STATIC_KSMETHODSETID_BdaChangeSync \
  0xfd0a5af3, 0xb41d, 0x11d2, 0x9c, 0x95, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0

DEFINE_GUIDSTRUCT("FD0A5AF3-B41D-11d2-9C95-00C04F7971E0", KSMETHODSETID_BdaChangeSync);

#define KSMETHODSETID_BdaChangeSync DEFINE_GUIDNAMED(KSMETHODSETID_BdaChangeSync)

typedef enum {
  KSMETHOD_BDA_START_CHANGES = 0,
  KSMETHOD_BDA_CHECK_CHANGES,
  KSMETHOD_BDA_COMMIT_CHANGES,
  KSMETHOD_BDA_GET_CHANGE_STATE
} KSMETHOD_BDA_CHANGE_SYNC;

#define DEFINE_KSMETHOD_ITEM_BDA_START_CHANGES(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_START_CHANGES,                            \
                       KSMETHOD_TYPE_NONE,                                    \
                       (MethodHandler),                                       \
                       sizeof(KSMETHOD),                                      \
                       0,                                                     \
                       SupportHandler)

#define DEFINE_KSMETHOD_ITEM_BDA_CHECK_CHANGES(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CHECK_CHANGES,                            \
                       KSMETHOD_TYPE_NONE,                                    \
                       (MethodHandler),                                       \
                       sizeof(KSMETHOD),                                      \
                       0,                                                     \
                       SupportHandler)

#define DEFINE_KSMETHOD_ITEM_BDA_COMMIT_CHANGES(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_COMMIT_CHANGES,                            \
                       KSMETHOD_TYPE_NONE,                                     \
                       (MethodHandler),                                        \
                       sizeof(KSMETHOD),                                       \
                       0,                                                      \
                       SupportHandler)

#define DEFINE_KSMETHOD_ITEM_BDA_GET_CHANGE_STATE(MethodHandler, SupportHandler) \
  DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GET_CHANGE_STATE,                            \
                       KSMETHOD_TYPE_READ,                                       \
                       (MethodHandler),                                          \
                       sizeof(KSMETHOD),                                         \
                       0,                                                        \
                       SupportHandler)

#define STATIC_KSPROPSETID_BdaFrequencyFilter \
  0x71985f47, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0

DEFINE_GUIDSTRUCT("71985F47-1CA1-11d3-9CC8-00C04F7971E0", KSPROPSETID_BdaFrequencyFilter);

#define KSPROPSETID_BdaFrequencyFilter DEFINE_GUIDNAMED(KSPROPSETID_BdaFrequencyFilter)

typedef enum {
  KSPROPERTY_BDA_RF_TUNER_FREQUENCY = 0,
  KSPROPERTY_BDA_RF_TUNER_POLARITY,
  KSPROPERTY_BDA_RF_TUNER_RANGE,
  KSPROPERTY_BDA_RF_TUNER_TRANSPONDER,
  KSPROPERTY_BDA_RF_TUNER_BANDWIDTH,
  KSPROPERTY_BDA_RF_TUNER_FREQUENCY_MULTIPLIER,
  KSPROPERTY_BDA_RF_TUNER_CAPS,
  KSPROPERTY_BDA_RF_TUNER_SCAN_STATUS,
  KSPROPERTY_BDA_RF_TUNER_STANDARD,
  KSPROPERTY_BDA_RF_TUNER_STANDARD_MODE
} KSPROPERTY_BDA_FREQUENCY_FILTER;

#define STATIC_KSPROPSETID_BdaDigitalDemodulator \
  0xef30f379, 0x985b, 0x4d10, 0xb6, 0x40, 0xa7, 0x9d, 0x5e, 0x4, 0xe1, 0xe0

DEFINE_GUIDSTRUCT("EF30F379-985B-4d10-B640-A79D5E04E1E0", KSPROPSETID_BdaDigitalDemodulator);

#define KSPROPSETID_BdaDigitalDemodulator DEFINE_GUIDNAMED(KSPROPSETID_BdaDigitalDemodulator)

typedef enum {
  KSPROPERTY_BDA_MODULATION_TYPE = 0,
  KSPROPERTY_BDA_INNER_FEC_TYPE,
  KSPROPERTY_BDA_INNER_FEC_RATE,
  KSPROPERTY_BDA_OUTER_FEC_TYPE,
  KSPROPERTY_BDA_OUTER_FEC_RATE,
  KSPROPERTY_BDA_SYMBOL_RATE,
  KSPROPERTY_BDA_SPECTRAL_INVERSION,
  KSPROPERTY_BDA_GUARD_INTERVAL,
  KSPROPERTY_BDA_TRANSMISSION_MODE,
  KSPROPERTY_BDA_ROLL_OFF,
  KSPROPERTY_BDA_PILOT,
  KSPROPERTY_BDA_SIGNALTIMEOUTS
} KSPROPERTY_BDA_DIGITAL_DEMODULATOR;

#define STATIC_KSPROPSETID_BdaLNBInfo \
  0x992cf102, 0x49f9, 0x4719, 0xa6, 0x64, 0xc4, 0xf2, 0x3e, 0x24, 0x8, 0xf4

DEFINE_GUIDSTRUCT("992CF102-49F9-4719-A664-C4F23E2408F4", KSPROPSETID_BdaLNBInfo);

#define KSPROPSETID_BdaLNBInfo DEFINE_GUIDNAMED(KSPROPSETID_BdaLNBInfo)

typedef enum {
  KSPROPERTY_BDA_LNB_LOF_LOW_BAND = 0,
  KSPROPERTY_BDA_LNB_LOF_HIGH_BAND,
  KSPROPERTY_BDA_LNB_SWITCH_FREQUENCY
} KSPROPERTY_BDA_LNB_INFO;

#define STATIC_KSPROPSETID_BdaSignalStats \
  0x1347d106, 0xcf3a, 0x428a, 0xa5, 0xcb, 0xac, 0xd, 0x9a, 0x2a, 0x43, 0x38

DEFINE_GUIDSTRUCT("1347D106-CF3A-428a-A5CB-AC0D9A2A4338", KSPROPSETID_BdaSignalStats);

#define KSPROPSETID_BdaSignalStats DEFINE_GUIDNAMED(KSPROPSETID_BdaSignalStats)

typedef enum {
  KSPROPERTY_BDA_SIGNAL_STRENGTH = 0,
  KSPROPERTY_BDA_SIGNAL_QUALITY,
  KSPROPERTY_BDA_SIGNAL_PRESENT,
  KSPROPERTY_BDA_SIGNAL_LOCKED,
  KSPROPERTY_BDA_SAMPLE_TIME
} KSPROPERTY_BDA_SIGNAL_STATS;


typedef struct tagBDA_TRANSPORT_INFO {
  ULONG ulcbPhyiscalPacket;
  ULONG ulcbPhyiscalFrame;
  ULONG ulcbPhyiscalFrameAlignment;
  REFERENCE_TIME AvgTimePerFrame;
} BDA_TRANSPORT_INFO, *PBDA_TRANSPORT_INFO;

typedef struct tagKS_DATARANGE_BDA_TRANSPORT {
  KSDATARANGE DataRange;
  BDA_TRANSPORT_INFO BdaTransportInfo;
} KS_DATARANGE_BDA_TRANSPORT, *PKS_DATARANGE_BDA_TRANSPORT;


/* ------------------------------------------------------------
  BDA Stream Format GUIDs
*/

#define STATIC_KSDATAFORMAT_TYPE_BDA_ANTENNA\
  0x71985f41, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0

DEFINE_GUIDSTRUCT("71985F41-1CA1-11d3-9CC8-00C04F7971E0", KSDATAFORMAT_TYPE_BDA_ANTENNA);

#define KSDATAFORMAT_TYPE_BDA_ANTENNA DEFINE_GUIDNAMED(KSDATAFORMAT_TYPE_BDA_ANTENNA)

#define STATIC_KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT\
  0xf4aeb342, 0x0329, 0x4fdd, 0xa8, 0xfd, 0x4a, 0xff, 0x49, 0x26, 0xc9, 0x78

DEFINE_GUIDSTRUCT("F4AEB342-0329-4fdd-A8FD-4AFF4926C978", KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT);

#define KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT)

#define STATIC_KSDATAFORMAT_SPECIFIER_BDA_TRANSPORT\
  0x8deda6fd, 0xac5f, 0x4334, 0x8e, 0xcf, 0xa4, 0xba, 0x8f, 0xa7, 0xd0, 0xf0

DEFINE_GUIDSTRUCT("8DEDA6FD-AC5F-4334-8ECF-A4BA8FA7D0F0", KSDATAFORMAT_SPECIFIER_BDA_TRANSPORT);

#define KSDATAFORMAT_SPECIFIER_BDA_TRANSPORT DEFINE_GUIDNAMED(KSDATAFORMAT_SPECIFIER_BDA_TRANSPORT)

#define STATIC_KSDATAFORMAT_TYPE_BDA_IF_SIGNAL\
  0x61be0b47, 0xa5eb, 0x499b, 0x9a, 0x85, 0x5b, 0x16, 0xc0, 0x7f, 0x12, 0x58

DEFINE_GUIDSTRUCT("61BE0B47-A5EB-499b-9A85-5B16C07F1258", KSDATAFORMAT_TYPE_BDA_IF_SIGNAL);

#define KSDATAFORMAT_TYPE_BDA_IF_SIGNAL DEFINE_GUIDNAMED(KSDATAFORMAT_TYPE_BDA_IF_SIGNAL)

#define STATIC_KSDATAFORMAT_TYPE_MPEG2_SECTIONS\
  0x455f176c, 0x4b06, 0x47ce, 0x9a, 0xef, 0x8c, 0xae, 0xf7, 0x3d, 0xf7, 0xb5

DEFINE_GUIDSTRUCT("455F176C-4B06-47CE-9AEF-8CAEF73DF7B5", KSDATAFORMAT_TYPE_MPEG2_SECTIONS);

#define KSDATAFORMAT_TYPE_MPEG2_SECTIONS DEFINE_GUIDNAMED(KSDATAFORMAT_TYPE_MPEG2_SECTIONS)

#define STATIC_KSDATAFORMAT_SUBTYPE_ATSC_SI\
  0xb3c7397c, 0xd303, 0x414d, 0xb3, 0x3c, 0x4e, 0xd2, 0xc9, 0xd2, 0x97, 0x33

DEFINE_GUIDSTRUCT("B3C7397C-D303-414D-B33C-4ED2C9D29733", KSDATAFORMAT_SUBTYPE_ATSC_SI);

#define KSDATAFORMAT_SUBTYPE_ATSC_SI DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_ATSC_SI)

#define STATIC_KSDATAFORMAT_SUBTYPE_DVB_SI\
  0xe9dd31a3, 0x221d, 0x4adb, 0x85, 0x32, 0x9a, 0xf3, 0x9, 0xc1, 0xa4, 0x8

DEFINE_GUIDSTRUCT("e9dd31a3-221d-4adb-8532-9af309c1a408", KSDATAFORMAT_SUBTYPE_DVB_SI);

#define KSDATAFORMAT_SUBTYPE_DVB_SI DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_DVB_SI)

#define STATIC_KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_PSIP\
  0x762e3f66, 0x336f, 0x48d1, 0xbf, 0x83, 0x2b, 0x0, 0x35, 0x2c, 0x11, 0xf0

DEFINE_GUIDSTRUCT("762E3F66-336F-48d1-BF83-2B00352C11F0", KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_PSIP);

#define KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_PSIP DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_PSIP)

#define STATIC_KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_OOB_PSIP\
  0x951727db, 0xd2ce, 0x4528, 0x96, 0xf6, 0x33, 0x1, 0xfa, 0xbb, 0x2d, 0xe0

DEFINE_GUIDSTRUCT("951727DB-D2CE-4528-96F6-3301FABB2DE0", KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_OOB_PSIP);

#define KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_OOB_PSIP DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_BDA_OPENCABLE_OOB_PSIP)

#define STATIC_KSDATAFORMAT_SUBTYPE_ISDB_SI\
  0x4a2eeb99, 0x6458, 0x4538, 0xb1, 0x87, 0x04, 0x01, 0x7c, 0x41, 0x41, 0x3f

DEFINE_GUIDSTRUCT("4a2eeb99-6458-4538-b187-04017c41413f", KSDATAFORMAT_SUBTYPE_ISDB_SI);

#define KSDATAFORMAT_SUBTYPE_ISDB_SI DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_ISDB_SI)

#define STATIC_KSDATAFORMAT_SUBTYPE_PBDA_TRANSPORT_RAW\
  0x0d7aed42, 0xcb9a, 0x11db, 0x97, 0x05, 0x00, 0x50, 0x56, 0xc0, 0x00, 0x08

DEFINE_GUIDSTRUCT("0d7AED42-CB9A-11DB-9705-005056C00008", KSDATAFORMAT_SUBTYPE_PBDA_TRANSPORT_RAW);

#define KSDATAFORMAT_SUBTYPE_PBDA_TRANSPORT_RAW DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_PBDA_TRANSPORT_RAW)

#ifndef AudioType_Commentary
#define AudioType_Commentary 5
#endif
#ifndef AudioType_Dialogue
#define AudioType_Dialogue 4
#endif
#ifndef AudioType_Emergency
#define AudioType_Emergency 6
#endif
#ifndef AudioType_Hearing_Impaired
#define AudioType_Hearing_Impaired 3
#endif
#ifndef AudioType_Music_And_Effects
#define AudioType_Music_And_Effects 1
#endif
#ifndef AudioType_Reserved
#define AudioType_Reserved (-1)
#endif
#ifndef AudioType_Visually_Impaired
#define AudioType_Visually_Impaired 2
#endif
#ifndef AudioType_Voiceover
#define AudioType_Voiceover 7
#endif
#ifndef DTV_CardStatus_Error
#define DTV_CardStatus_Error 2
#endif
#ifndef DTV_CardStatus_FirmwareDownload
#define DTV_CardStatus_FirmwareDownload 3
#endif
#ifndef DTV_CardStatus_Removed
#define DTV_CardStatus_Removed 1
#endif
#ifndef DTV_Entitlement_NotEntitled
#define DTV_Entitlement_NotEntitled 1
#endif
#ifndef DTV_Entitlement_TechnicalFailure
#define DTV_Entitlement_TechnicalFailure 2
#endif
#ifndef DTV_MMIMessage_Close
#define DTV_MMIMessage_Close 1
#endif
#ifndef PARENTAL_CONTROL_ATTRIB_DIALOGUE
#define PARENTAL_CONTROL_ATTRIB_DIALOGUE 0x00000203
#endif
#ifndef PARENTAL_CONTROL_ATTRIB_FANTASY
#define PARENTAL_CONTROL_ATTRIB_FANTASY 0x00000204
#endif
#ifndef PARENTAL_CONTROL_ATTRIB_LANGUAGE
#define PARENTAL_CONTROL_ATTRIB_LANGUAGE 0x00000201
#endif
#ifndef PARENTAL_CONTROL_ATTRIB_SEXUAL
#define PARENTAL_CONTROL_ATTRIB_SEXUAL 0x00000202
#endif
#ifndef PARENTAL_CONTROL_ATTRIB_VIOLENCE
#define PARENTAL_CONTROL_ATTRIB_VIOLENCE 0x00000200
#endif
#ifndef PARENTAL_CONTROL_CONTENT_RATING
#define PARENTAL_CONTROL_CONTENT_RATING 0x00000100
#endif
#ifndef PARENTAL_CONTROL_TIME_RANGE
#define PARENTAL_CONTROL_TIME_RANGE 0x00000001
#endif
#ifndef PARENTAL_CONTROL_VALUE_UNDEFINED
#define PARENTAL_CONTROL_VALUE_UNDEFINED 0
#endif
#ifndef PBDA_PAIRING_PROTOCOL_VERSION
#define PBDA_PAIRING_PROTOCOL_VERSION 3
#endif
#ifndef REQUIRED_PARENTAL_CONTROL_TIME_RANGE
#define REQUIRED_PARENTAL_CONTROL_TIME_RANGE 0x00000002
#endif

typedef enum _BdaLockType {
    Bda_LockType_None = 0,
    Bda_LockType_PLL = 1,
    Bda_LockType_DecoderDemod = 2,
    Bda_LockType_Complete = 128
} BDA_LockType;

typedef enum _BdaSignalType {
    Bda_SignalType_Unknown = 0,
    Bda_SignalType_Analog = 1,
    Bda_SignalType_Digital = 2
} BDA_SignalType;

typedef enum BDA_DigitalSignalStandard {
    Bda_DigitalStandard_None = 0,
    Bda_DigitalStandard_DVB_T = 1,
    Bda_DigitalStandard_DVB_S = 2,
    Bda_DigitalStandard_DVB_C = 4,
    Bda_DigitalStandard_ATSC = 8,
    Bda_DigitalStandard_ISDB_T = 16,
    Bda_DigitalStandard_ISDB_S = 32,
    Bda_DigitalStandard_ISDB_C = 64
} BDA_DigitalSignalStandard;

typedef enum ChannelChangeSpanningEvent_State {
    ChannelChangeSpanningEvent_Start = 0,
    ChannelChangeSpanningEvent_End = 2
} ChannelChangeSpanningEvent_State;

typedef enum KSEVENT_BDA_EVENT_TYPE {
    KSEVENT_BDA_EVENT_PENDINGEVENT = 0
} KSEVENT_BDA_EVENT_TYPE;

typedef enum KSEVENT_BDA_TUNER {
    KSEVENT_BDA_TUNER_SCAN = 0
} KSEVENT_BDA_TUNER;

typedef enum KSMETHOD_BDA_CAS_SERVICE {
    KSMETHOD_BDA_CAS_CHECKENTITLEMENTTOKEN = 0,
    KSMETHOD_BDA_CAS_SETCAPTURETOKEN = 1,
    KSMETHOD_BDA_CAS_OPENBROADCASTMMI = 2,
    KSMETHOD_BDA_CAS_CLOSEMMIDIALOG = 3
} KSMETHOD_BDA_CAS_SERVICE;

typedef enum KSMETHOD_BDA_DEBUG_SERVICE {
    KSMETHOD_BDA_DEBUG_LEVEL = 0,
    KSMETHOD_BDA_DEBUG_DATA = 1
} KSMETHOD_BDA_DEBUG_SERVICE;

typedef enum KSMETHOD_BDA_DRM {
    KSMETHOD_BDA_DRM_CURRENT = 0,
    KSMETHOD_BDA_DRM_DRMSTATUS = 1
} KSMETHOD_BDA_DRM;

typedef enum KSMETHOD_BDA_EVENTING_SERVICE {
    KSMETHOD_BDA_EVENT_DATA = 0,
    KSMETHOD_BDA_EVENT_COMPLETE = 1
} KSMETHOD_BDA_EVENTING_SERVICE;

typedef enum KSMETHOD_BDA_GDDS_SERVICE {
    KSMETHOD_BDA_GDDS_DATATYPE = 0,
    KSMETHOD_BDA_GDDS_DATA = 1,
    KSMETHOD_BDA_GDDS_TUNEXMLFROMIDX = 2,
    KSMETHOD_BDA_GDDS_GETSERVICES = 3,
    KSMETHOD_BDA_GDDS_SERVICEFROMTUNEXML = 4,
    KSMETHOD_BDA_GDDS_DATAUPDATE = 5
} KSMETHOD_BDA_GDDS_SERVICE;

typedef enum KSMETHOD_BDA_GPNV_SERVICE {
    KSMETHOD_BDA_GPNV_GETVALUE = 0,
    KSMETHOD_BDA_GPNV_SETVALUE = 1,
    KSMETHOD_BDA_GPNV_NAMEFROMINDEX = 2,
    KSMETHOD_BDA_GPNV_GETVALUEUPDATENAME = 3
} KSMETHOD_BDA_GPNV_SERVICE;

typedef enum KSMETHOD_BDA_ISDB_CAS {
    KSMETHOD_BDA_ISDBCAS_SETREQUEST = 0,
    KSMETHOD_BDA_ISDBCAS_RESPONSEDATA = 1
} KSMETHOD_BDA_ISDB_CAS;

typedef enum KSMETHOD_BDA_MUX_SERVICE {
    KSMETHOD_BDA_MUX_GETPIDLIST = 0,
    KSMETHOD_BDA_MUX_SETPIDLIST = 1
} KSMETHOD_BDA_MUX_SERVICE;

typedef enum KSMETHOD_BDA_SCAN_SERVICE {
    KSMETHOD_BDA_SCAN_CAPABILTIES = 0,
    KSMETHOD_BDA_SCANNING_STATE = 1,
    KSMETHOD_BDA_SCAN_FILTER = 2,
    KSMETHOD_BDA_SCAN_START = 3,
    KSMETHOD_BDA_SCAN_RESUME = 4,
    KSMETHOD_BDA_SCAN_STOP = 5
} KSMETHOD_BDA_SCAN_SERVICE;

typedef enum KSMETHOD_BDA_TS_SELECTOR {
    KSMETHOD_BDA_TS_SELECTOR_SETTSID = 0,
    KSMETHOD_BDA_TS_SELECTOR_GETTSINFORMATION = 1
} KSMETHOD_BDA_TS_SELECTOR;

typedef enum KSMETHOD_BDA_TUNER_SERVICE {
    KSMETHOD_BDA_TUNER_SETTUNER = 0,
    KSMETHOD_BDA_TUNER_GETTUNERSTATE = 1,
    KSMETHOD_BDA_TUNER_SIGNALNOISERATIO = 2
} KSMETHOD_BDA_TUNER_SERVICE;

typedef enum KSMETHOD_BDA_USERACTIVITY_SERVICE {
    KSMETHOD_BDA_USERACTIVITY_USEREASON = 0,
    KSMETHOD_BDA_USERACTIVITY_INTERVAL = 1,
    KSMETHOD_BDA_USERACTIVITY_DETECTED = 2
} KSMETHOD_BDA_USERACTIVITY_SERVICE;

typedef enum KSMETHOD_BDA_WMDRM {
    KSMETHOD_BDA_WMDRM_STATUS = 0,
    KSMETHOD_BDA_WMDRM_REVINFO = 1,
    KSMETHOD_BDA_WMDRM_CRL = 2,
    KSMETHOD_BDA_WMDRM_MESSAGE = 3,
    KSMETHOD_BDA_WMDRM_REISSUELICENSE = 4,
    KSMETHOD_BDA_WMDRM_RENEWLICENSE = 5,
    KSMETHOD_BDA_WMDRM_LICENSE = 6,
    KSMETHOD_BDA_WMDRM_KEYINFO = 7
} KSMETHOD_BDA_WMDRM;

typedef enum KSMETHOD_BDA_WMDRM_TUNER {
    KSMETHOD_BDA_WMDRMTUNER_CANCELCAPTURETOKEN = 0,
    KSMETHOD_BDA_WMDRMTUNER_SETPIDPROTECTION = 1,
    KSMETHOD_BDA_WMDRMTUNER_GETPIDPROTECTION = 2,
    KSMETHOD_BDA_WMDRMTUNER_SETSYNCVALUE = 3,
    KSMETHOD_BDA_WMDRMTUNER_STARTCODEPROFILE = 4,
    KSMETHOD_BDA_WMDRMTUNER_PURCHASE_ENTITLEMENT = 5
} KSMETHOD_BDA_WMDRM_TUNER;

typedef enum KSPROPERTY_BDA_AUTODEMODULATE {
    KSPROPERTY_BDA_AUTODEMODULATE_START = 0,
    KSPROPERTY_BDA_AUTODEMODULATE_STOP = 1
} KSPROPERTY_BDA_AUTODEMODULATE;

typedef enum KSPROPERTY_BDA_CA {
    KSPROPERTY_BDA_ECM_MAP_STATUS = 0,
    KSPROPERTY_BDA_CA_MODULE_STATUS = 1,
    KSPROPERTY_BDA_CA_SMART_CARD_STATUS = 2,
    KSPROPERTY_BDA_CA_MODULE_UI = 3,
    KSPROPERTY_BDA_CA_SET_PROGRAM_PIDS = 4,
    KSPROPERTY_BDA_CA_REMOVE_PROGRAM = 5
} KSPROPERTY_BDA_CA;

typedef enum KSPROPERTY_BDA_CA_EVENT {
    KSEVENT_BDA_PROGRAM_FLOW_STATUS_CHANGED = 0,
    KSEVENT_BDA_CA_MODULE_STATUS_CHANGED = 1,
    KSEVENT_BDA_CA_SMART_CARD_STATUS_CHANGED = 2,
    KSEVENT_BDA_CA_MODULE_UI_REQUESTED = 3
} KSPROPERTY_BDA_CA_EVENT;

typedef enum KSPROPERTY_BDA_DISEQC_COMMAND {
    KSPROPERTY_BDA_DISEQC_ENABLE = 0,
    KSPROPERTY_BDA_DISEQC_LNB_SOURCE = 1,
    KSPROPERTY_BDA_DISEQC_USETONEBURST = 2,
    KSPROPERTY_BDA_DISEQC_REPEATS = 3,
    KSPROPERTY_BDA_DISEQC_SEND = 4,
    KSPROPERTY_BDA_DISEQC_RESPONSE = 5
} KSPROPERTY_BDA_DISEQC_COMMAND;

typedef enum KSPROPERTY_BDA_DISEQC_EVENT {
    KSEVENT_BDA_DISEQC_DATA_RECEIVED = 0
} KSPROPERTY_BDA_DISEQC_EVENT;

typedef enum KSPROPERTY_BDA_ETHERNET_FILTER {
    KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_LIST_SIZE = 0,
    KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_LIST = 1,
    KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_MODE = 2
} KSPROPERTY_BDA_ETHERNET_FILTER;

typedef enum KSPROPERTY_BDA_IPv4_FILTER {
    KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_LIST_SIZE = 0,
    KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_LIST = 1,
    KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_MODE = 2
} KSPROPERTY_BDA_IPv4_FILTER;

typedef enum KSPROPERTY_BDA_IPv6_FILTER {
    KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_LIST_SIZE = 0,
    KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_LIST = 1,
    KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_MODE = 2
} KSPROPERTY_BDA_IPv6_FILTER;

typedef enum KSPROPERTY_BDA_NULL_TRANSFORM {
    KSPROPERTY_BDA_NULL_TRANSFORM_START = 0,
    KSPROPERTY_BDA_NULL_TRANSFORM_STOP = 1
} KSPROPERTY_BDA_NULL_TRANSFORM;

typedef enum KSPROPERTY_BDA_PIDFILTER {
    KSPROPERTY_BDA_PIDFILTER_MAP_PIDS = 0,
    KSPROPERTY_BDA_PIDFILTER_UNMAP_PIDS = 1,
    KSPROPERTY_BDA_PIDFILTER_LIST_PIDS = 2
} KSPROPERTY_BDA_PIDFILTER;

typedef enum KSPROPERTY_BDA_PIN_EVENT {
    KSEVENT_BDA_PIN_CONNECTED = 0,
    KSEVENT_BDA_PIN_DISCONNECTED = 1
} KSPROPERTY_BDA_PIN_EVENT;

typedef enum KSPROPERTY_BDA_VOID_TRANSFORM {
    KSPROPERTY_BDA_VOID_TRANSFORM_START = 0,
    KSPROPERTY_BDA_VOID_TRANSFORM_STOP = 1
} KSPROPERTY_BDA_VOID_TRANSFORM;

typedef enum KSPROPERTY_IDS_BDA_TABLE {
    KSPROPERTY_BDA_TABLE_SECTION = 0
} KSPROPERTY_IDS_BDA_TABLE;

typedef enum SignalAndServiceStatusSpanningEvent_State {
    SignalAndServiceStatusSpanningEvent_None = (-1),
    SignalAndServiceStatusSpanningEvent_Clear = 0,
    SignalAndServiceStatusSpanningEvent_NoTVSignal = 1,
    SignalAndServiceStatusSpanningEvent_ServiceOffAir = 2,
    SignalAndServiceStatusSpanningEvent_WeakTVSignal = 3,
    SignalAndServiceStatusSpanningEvent_NoSubscription = 4,
    SignalAndServiceStatusSpanningEvent_AllAVScrambled = 5
} SignalAndServiceStatusSpanningEvent_State;

typedef struct _ChannelChangeInfo {
  ChannelChangeSpanningEvent_State state;
  ULONGLONG TimeStamp;
} ChannelChangeInfo;

typedef struct _DualMonoInfo {
  LANGID LangID1;
  LANGID LangID2;
  LONG lISOLangCode1;
  LONG lISOLangCode2;
} DualMonoInfo;

typedef struct _LanguageInfo {
  LANGID LangID;
  LONG lISOLangCode;
} LanguageInfo;

#define ANALOG_AUXIN_NETWORK_TYPE 0
#define ANALOG_FM_NETWORK_TYPE 0
#define ANALOG_TV_NETWORK_TYPE 0
#define ATSC_TERRESTRIAL_TV_NETWORK_TYPE 0
#define AudioType_Standard 0
#define BDA_DEBUG_DATA_AVAILABLE 0
#define BDA_DEBUG_DATA_TYPE_STRING 0
#define BSKYB_TERRESTRIAL_TV_NETWORK_TYPE 0
#define DIGITAL_CABLE_NETWORK_TYPE 0
#define DIRECT_TV_SATELLITE_TV_NETWORK_TYPE 0
#define DTV_CardStatus_Inserted 0
#define DTV_Entitlement_CanDecrypt 0
#define DTV_MMIMessage_Open 0
#define DVB_CABLE_TV_NETWORK_TYPE 0
#define DVB_SATELLITE_TV_NETWORK_TYPE 0
#define DVB_TERRESTRIAL_TV_NETWORK_TYPE 0
#define ECHOSTAR_SATELLITE_TV_NETWORK_TYPE 0
#define EVENTID_ARIBcontentSpanningEvent 0
#define EVENTID_AudioDescriptorSpanningEvent 0
#define EVENTID_AudioTypeSpanningEvent 0
#define EVENTID_BDA_CASBroadcastMMI 0
#define EVENTID_BDA_CASCloseMMI 0
#define EVENTID_BDA_CASOpenMMI 0
#define EVENTID_BDA_CASReleaseTuner 0
#define EVENTID_BDA_CASRequestTuner 0
#define EVENTID_BDA_DiseqCResponseAvailable 0
#define EVENTID_BDA_EncoderSignalLock 0
#define EVENTID_BDA_FdcStatus 0
#define EVENTID_BDA_FdcTableSection 0
#define EVENTID_BDA_GPNVValueUpdate 0
#define EVENTID_BDA_GuideDataAvailable 0
#define EVENTID_BDA_GuideDataError 0
#define EVENTID_BDA_GuideServiceInformationUpdated 0
#define EVENTID_BDA_IsdbCASResponse 0
#define EVENTID_BDA_LbigsCloseConnectionHandle 0
#define EVENTID_BDA_LbigsOpenConnection 0
#define EVENTID_BDA_LbigsSendData 0
#define EVENTID_BDA_RatingPinReset 0
#define EVENTID_BDA_TransprtStreamSelectorInfo 0
#define EVENTID_BDA_TunerNoSignal 0
#define EVENTID_BDA_TunerSignalLock 0
#define EVENTID_BDA_UpdateDrmStatus 0
#define EVENTID_BDA_UpdateScanState 0
#define EVENTID_BDAConditionalAccessTAG 0
#define EVENTID_BDAEventingServicePendingEvent 0
#define EVENTID_CADenialCountChanged 0
#define EVENTID_CandidatePostTuneData 0
#define EVENTID_CardStatusChanged 0
#define EVENTID_CASFailureSpanningEvent 0
#define EVENTID_ChannelChangeSpanningEvent 0
#define EVENTID_ChannelInfoSpanningEvent 0
#define EVENTID_ChannelTypeSpanningEvent 0
#define EVENTID_CSDescriptorSpanningEvent 0
#define EVENTID_CtxADescriptorSpanningEvent 0
#define EVENTID_DFNWithNoActualAVData 0
#define EVENTID_DRMParingStatusChanged 0
#define EVENTID_DRMParingStepComplete 0
#define EVENTID_DualMonoSpanningEvent 0
#define EVENTID_DvbParentalRatingDescriptor 0
#define EVENTID_DVBScramblingControlSpanningEvent 0
#define EVENTID_EASMessageReceived 0
#define EVENTID_EmmMessageSpanningEvent 0
#define EVENTID_EntitlementChanged 0
#define EVENTID_LanguageSpanningEvent 0
#define EVENTID_MMIMessage 0
#define EVENTID_NewSignalAcquired 0
#define EVENTID_PBDAParentalControlEvent 0
#define EVENTID_PIDListSpanningEvent 0
#define EVENTID_PSITable 0
#define EVENTID_RRTSpanningEvent 0
#define EVENTID_ServiceTerminated 0
#define EVENTID_SignalAndServiceStatusEvent EVENTID_SignalAndServiceStatusSpanningEvent
#define EVENTID_SignalAndServiceStatusSpanningEvent 0
#define EVENTID_SignalStatusChanged 0
#define EVENTID_STBChannelNumber 0
#define EVENTID_StreamIDSpanningEvent 0
#define EVENTID_StreamTypeSpanningEvent 0
#define EVENTID_SubtitleSpanningEvent 0
#define EVENTID_TeletextSpanningEvent 0
#define EVENTID_TuneFailureEvent 0
#define EVENTID_TuneFailureSpanningEvent 0
#define EVENTID_TuningChanged 0
#define EVENTID_TuningChanging 0
#define EVENTTYPE_CASDescrambleFailureEvent 0
#define ISDB_CABLE_TV_NETWORK_TYPE 0
#define ISDB_S_NETWORK_TYPE 0
#define ISDB_SATELLITE_TV_NETWORK_TYPE 0
#define ISDB_T_NETWORK_TYPE 0
#define ISDB_TERRESTRIAL_TV_NETWORK_TYPE 0
#define KSCATEGORY_BDA_NETWORK_EPG 0
#define KSCATEGORY_BDA_NETWORK_PROVIDER 0
#define KSCATEGORY_BDA_NETWORK_TUNER 0
#define KSCATEGORY_BDA_RECEIVER_COMPONENT 0
#define KSCATEGORY_BDA_TRANSPORT_INFORMATION 0
#define KSCATEGORY_IP_SINK 0
#define KSDATAFORMAT_SPECIFIER_BDA_IP 0
#define KSDATAFORMAT_SUBTYPE_BDA_IP 0
#define KSDATAFORMAT_SUBTYPE_BDA_IP_CONTROL 0
#define KSDATAFORMAT_TYPE_BDA_IP 0
#define KSDATAFORMAT_TYPE_BDA_IP_CONTROL 0
#define KSDATAFORMAT_TYPE_MPE 0
#define KSEVENTSETID_BdaCAEvent 0
#define KSEVENTSETID_BdaDiseqCEvent 0
#define KSEVENTSETID_BdaEvent 0
#define KSEVENTSETID_BdaPinEvent 0
#define KSEVENTSETID_BdaTunerEvent 0
#define KSMETHODSETID_BdaConditionalAccessService 0
#define KSMETHODSETID_BdaDebug 0
#define KSMETHODSETID_BdaDrmService 0
#define KSMETHODSETID_BdaEventing 0
#define KSMETHODSETID_BdaGuideDataDeliveryService 0
#define KSMETHODSETID_BdaIsdbConditionalAccess 0
#define KSMETHODSETID_BdaMux 0
#define KSMETHODSETID_BdaNameValue 0
#define KSMETHODSETID_BdaNameValueA 0
#define KSMETHODSETID_BdaScanning 0
#define KSMETHODSETID_BdaTSSelector 0
#define KSMETHODSETID_BdaTuner 0
#define KSMETHODSETID_BdaUserActivity 0
#define KSMETHODSETID_BdaWmdrmSession 0
#define KSMETHODSETID_BdaWmdrmTuner 0
#define KSNODE_BDA_8PSK_DEMODULATOR 0
#define KSNODE_BDA_8VSB_DEMODULATOR 0
#define KSNODE_BDA_ANALOG_DEMODULATOR 0
#define KSNODE_BDA_COFDM_DEMODULATOR 0
#define KSNODE_BDA_COMMON_CA_POD 0
#define KSNODE_BDA_DRI_DRM 0
#define KSNODE_BDA_ISDB_S_DEMODULATOR 0
#define KSNODE_BDA_ISDB_T_DEMODULATOR 0
#define KSNODE_BDA_OPENCABLE_POD 0
#define KSNODE_BDA_PBDA_CAS 0
#define KSNODE_BDA_PBDA_DRM 0
#define KSNODE_BDA_PBDA_ISDBCAS 0
#define KSNODE_BDA_PBDA_MUX 0
#define KSNODE_BDA_PBDA_TUNER 0
#define KSNODE_BDA_PID_FILTER 0
#define KSNODE_BDA_QAM_DEMODULATOR 0
#define KSNODE_BDA_QPSK_DEMODULATOR 0
#define KSNODE_BDA_RF_TUNER 0
#define KSNODE_BDA_TS_SELECTOR 0
#define KSNODE_BDA_VIDEO_ENCODER 0
#define KSNODE_IP_SINK 0
#define KSPROPSETID_BdaAutodemodulate 0
#define KSPROPSETID_BdaCA 0
#define KSPROPSETID_BdaDiseqCommand 0
#define KSPROPSETID_BdaEthernetFilter 0
#define KSPROPSETID_BdaIPv4Filter 0
#define KSPROPSETID_BdaIPv6Filter 0
#define KSPROPSETID_BdaNullTransform 0
#define KSPROPSETID_BdaPIDFilter 0
#define KSPROPSETID_BdaTableSection 0
#define KSPROPSETID_BdaVoidTransform 0
#define MAX_COUNTRY_CODE_STRING 3
#define OCUR_PAIRING_PROTOCOL_VERSION 2
#define PBDA_ALWAYS_TUNE_IN_MUX 0
#define PINNAME_BDA_ANALOG_AUDIO 0
#define PINNAME_BDA_ANALOG_VIDEO 0
#define PINNAME_BDA_FM_RADIO 0
#define PINNAME_BDA_IF_PIN 0
#define PINNAME_BDA_OPENCABLE_PSIP_PIN 0
#define PINNAME_BDA_TRANSPORT 0
#define PINNAME_IPSINK_INPUT 0
#define PINNAME_MPE 0
#define STATIC_ANALOG_AUXIN_NETWORK_TYPE 0x742EF867, 0x9E1, 0x40A3, 0x82, 0xD3, 0x96, 0x69, 0xBA, 0x35, 0x32, 0x5F
#define STATIC_ANALOG_FM_NETWORK_TYPE 0x7728087b, 0x2bb9, 0x4e30, 0x80, 0x78, 0x44, 0x94, 0x76, 0xe5, 0x9d, 0xbb
#define STATIC_ANALOG_TV_NETWORK_TYPE 0xb820d87e, 0xe0e3, 0x478f, 0x8a, 0x38, 0x4e, 0x13, 0xf7, 0xb3, 0xdf, 0x42
#define STATIC_ATSC_TERRESTRIAL_TV_NETWORK_TYPE 0x0dad2fdd, 0x5fd7, 0x11d3, 0x8f, 0x50, 0x00, 0xc0, 0x4f, 0x79, 0x71, 0xe2
#define STATIC_BDA_DEBUG_DATA_AVAILABLE 0x69C24F54, 0x9983, 0x497e, 0xb4, 0x15, 0x28, 0x2b, 0xe4, 0xc5, 0x55, 0xfb
#define STATIC_BDA_DEBUG_DATA_TYPE_STRING 0xa806e767, 0xde5c, 0x430c, 0x80, 0xbf, 0xa2, 0x1e, 0xbe, 0x06, 0xc7, 0x48
#define STATIC_BSKYB_TERRESTRIAL_TV_NETWORK_TYPE 0x9E9E46C6, 0x3ABA, 0x4f08, 0xAD, 0x0E, 0xCC, 0x5A, 0xC8, 0x14, 0x8C, 0x2B
#define STATIC_DIGITAL_CABLE_NETWORK_TYPE 0x143827AB, 0xF77B, 0x498d, 0x81, 0xCA, 0x5A, 0x00, 0x7A, 0xEC, 0x28, 0xBF
#define STATIC_DIRECT_TV_SATELLITE_TV_NETWORK_TYPE 0x93b66fb5, 0x93d4, 0x4323, 0x92, 0x1c, 0xc1, 0xf5, 0x2d, 0xf6, 0x1d, 0x3f
#define STATIC_DVB_CABLE_TV_NETWORK_TYPE 0xdc0c0fe7, 0x485, 0x4266, 0xb9, 0x3f, 0x68, 0xfb, 0xf8, 0x0e, 0xd8, 0x34
#define STATIC_DVB_SATELLITE_TV_NETWORK_TYPE 0xfa4b375a, 0x45b4, 0x4d45, 0x84, 0x40, 0x26, 0x39, 0x57, 0xb1, 0x16, 0x23
#define STATIC_DVB_TERRESTRIAL_TV_NETWORK_TYPE 0x216c62df, 0x6d7f, 0x4e9a, 0x85, 0x71, 0x05, 0xf1, 0x4e, 0xdb, 0x76, 0x6a
#define STATIC_ECHOSTAR_SATELLITE_TV_NETWORK_TYPE 0xc4f6b31b, 0xc6bf, 0x4759, 0x88, 0x6f, 0xa7, 0x38, 0x6d, 0xca, 0x27, 0xa0
#define STATIC_EVENTID_ARIBcontentSpanningEvent 0x3a954083, 0x93d0, 0x463e, 0x90, 0xb2, 0x7, 0x42, 0xc4, 0x96, 0xed, 0xf0
#define STATIC_EVENTID_AudioDescriptorSpanningEvent 0x107bd41c, 0xa6da, 0x4691, 0x83, 0x69, 0x11, 0xb2, 0xcd, 0xaa, 0x28, 0x8e
#define STATIC_EVENTID_AudioTypeSpanningEvent 0x501cbfbe, 0xb849, 0x42ce, 0x9b, 0xe9, 0x3d, 0xb8, 0x69, 0xfb, 0x82, 0xb3
#define STATIC_EVENTID_BDA_CASBroadcastMMI 0x676876f0, 0x1132, 0x404c, 0xa7, 0xca, 0xe7, 0x20, 0x69, 0xa9, 0xd5, 0x4f
#define STATIC_EVENTID_BDA_CASCloseMMI 0x5d0f550f, 0xde2e, 0x479d, 0x83, 0x45, 0xec, 0x0e, 0x95, 0x57, 0xe8, 0xa2
#define STATIC_EVENTID_BDA_CASOpenMMI 0x85dac915, 0xe593, 0x410d, 0x84, 0x71, 0xd6, 0x81, 0x21, 0x05, 0xf2, 0x8e
#define STATIC_EVENTID_BDA_CASReleaseTuner 0x20c1a16b, 0x441f, 0x49a5, 0xbb, 0x5c, 0xe9, 0xa0, 0x44, 0x95, 0xc6, 0xc1
#define STATIC_EVENTID_BDA_CASRequestTuner 0xcf39a9d8, 0xf5d3, 0x4685, 0xbe, 0x57, 0xed, 0x81, 0xdb, 0xa4, 0x6b, 0x27
#define STATIC_EVENTID_BDA_DiseqCResponseAvailable 0xefa628f8, 0x1f2c, 0x4b67, 0x9e, 0xa5, 0xac, 0xf6, 0xfa, 0x9a, 0x1f, 0x36
#define STATIC_EVENTID_BDA_EncoderSignalLock 0x5ec90eb9, 0x39fa, 0x4cfc, 0xb9, 0x3f, 0x00, 0xbb, 0x11, 0x07, 0x7f, 0x5e
#define STATIC_EVENTID_BDA_FdcStatus 0x05f25366, 0xd0eb, 0x43d2, 0xbc, 0x3c, 0x68, 0x2b, 0x86, 0x3d, 0xf1, 0x42
#define STATIC_EVENTID_BDA_FdcTableSection 0x6a0cD757, 0x4ce3, 0x4e5b, 0x94, 0x44, 0x71, 0x87, 0xb8, 0x71, 0x52, 0xc5
#define STATIC_EVENTID_BDA_GPNVValueUpdate 0xff75c68c, 0xf416, 0x4e7e, 0xbf, 0x17, 0x6d, 0x55, 0xc5, 0xdf, 0x15, 0x75
#define STATIC_EVENTID_BDA_GuideDataAvailable 0x98db717a, 0x478a, 0x4cd4, 0x92, 0xd0, 0x95, 0xf6, 0x6b, 0x89, 0xe5, 0xb1
#define STATIC_EVENTID_BDA_GuideDataError 0xAC33C448, 0x6F73, 0x4fd7, 0xB3, 0x41, 0x59, 0x4C, 0x36, 0x0D, 0x8D, 0x74
#define STATIC_EVENTID_BDA_GuideServiceInformationUpdated 0xa1c3ea2b, 0x175f, 0x4458, 0xb7, 0x35, 0x50, 0x7d, 0x22, 0xdb, 0x23, 0xa6
#define STATIC_EVENTID_BDA_IsdbCASResponse 0xd4cb1966, 0x41bc, 0x4ced, 0x9a, 0x20, 0xfd, 0xce, 0xac, 0x78, 0xf7, 0x0d
#define STATIC_EVENTID_BDA_LbigsCloseConnectionHandle 0xC2F08B99, 0x65EF, 0x4314, 0x96, 0x71, 0xe9, 0x9d, 0x4c, 0xce, 0x0b, 0xae
#define STATIC_EVENTID_BDA_LbigsOpenConnection 0x356207B2, 0x6F31, 0x4eb0, 0xa2, 0x71, 0xb3, 0xfa, 0x6b, 0xb7, 0x68, 0x0f
#define STATIC_EVENTID_BDA_LbigsSendData 0x1123277B, 0xF1C6, 0x4154, 0x8b, 0x0d, 0x48, 0xe6, 0x15, 0x70, 0x59, 0xaa
#define STATIC_EVENTID_BDA_RatingPinReset 0xc6e048c0, 0xc574, 0x4c26, 0xbc, 0xda, 0x2f, 0x4d, 0x35, 0xeb, 0x5e, 0x85
#define STATIC_EVENTID_BDA_TransprtStreamSelectorInfo 0xc40f9f85, 0x9d0, 0x489c, 0x9e, 0x9c, 0x0a, 0xbb, 0xb5, 0x69, 0x51, 0xb0
#define STATIC_EVENTID_BDA_TunerNoSignal 0xe29b382b, 0x1edd, 0x4930, 0xbc, 0x46, 0x68, 0x2f, 0xd7, 0x2d, 0x2d, 0xfb
#define STATIC_EVENTID_BDA_TunerSignalLock 0x1872e740, 0xf573, 0x429b, 0xa0, 0x0e, 0xd9, 0xc1, 0xe4, 0x08, 0xaf, 0x09
#define STATIC_EVENTID_BDA_UpdateDrmStatus 0x65a6f681, 0x1462, 0x473b, 0x88, 0xce, 0xcb, 0x73, 0x14, 0x27, 0xbd, 0xb5
#define STATIC_EVENTID_BDA_UpdateScanState 0x55702B50, 0x7B49, 0x42B8, 0xA8, 0x2F, 0x4A, 0xFB, 0x69, 0x1B, 0x06, 0x28
#define STATIC_EVENTID_BDAConditionalAccessTAG 0xefc3a459, 0xae8b, 0x4b4a, 0x8f, 0xe9, 0x79, 0xa0, 0xd0, 0x97, 0xf3, 0xea
#define STATIC_EVENTID_BDAEventingServicePendingEvent 0x5ca51711, 0x5ddc, 0x41a6, 0x94, 0x30, 0xe4, 0x1b, 0x8b, 0x3b, 0xbc, 0x5b
#define STATIC_EVENTID_CADenialCountChanged 0x2a65c528, 0x2249, 0x4070, 0xac, 0x16, 0x0, 0x39, 0xc, 0xdf, 0xb2, 0xdd
#define STATIC_EVENTID_CandidatePostTuneData 0x9F02D3D0, 0x9F06, 0x4369, 0x9F, 0x1E, 0x3A, 0xD6, 0xCA, 0x19, 0x80, 0x7E
#define STATIC_EVENTID_CardStatusChanged 0xa265faea, 0xf874, 0x4b38, 0x9f, 0xf7, 0xc5, 0x3d, 0x2, 0x96, 0x99, 0x96
#define STATIC_EVENTID_CASFailureSpanningEvent 0xead831ae, 0x5529, 0x4d1f, 0xaf, 0xce, 0xd, 0x8c, 0xd1, 0x25, 0x7d, 0x30
#define STATIC_EVENTID_ChannelChangeSpanningEvent 0x9067C5E5, 0x4C5C, 0x4205, 0x86, 0xc8, 0x7a, 0xfe, 0x20, 0xfe, 0x1e, 0xfa
#define STATIC_EVENTID_ChannelInfoSpanningEvent 0x41f36d80, 0x4132, 0x4cc2, 0xb1, 0x21, 0x1, 0xa4, 0x32, 0x19, 0xd8, 0x1b
#define STATIC_EVENTID_ChannelTypeSpanningEvent 0x72ab1d51, 0x87d2, 0x489b, 0xba, 0x11, 0xe, 0x8, 0xdc, 0x21, 0x2, 0x43
#define STATIC_EVENTID_CSDescriptorSpanningEvent 0xefe779d9, 0x97f0, 0x4786, 0x80, 0xd, 0x95, 0xcf, 0x50, 0x5d, 0xdc, 0x66
#define STATIC_EVENTID_CtxADescriptorSpanningEvent 0x3ab4a2e6, 0x4247, 0x4b34, 0x89, 0x6c, 0x30, 0xaf, 0xa5, 0xd2, 0x1c, 0x24
#define STATIC_EVENTID_DFNWithNoActualAVData 0xf5689ffe, 0x55f9, 0x4bb3, 0x96, 0xbe, 0xae, 0x97, 0x1c, 0x63, 0xba, 0xe0
#define STATIC_EVENTID_DRMParingStatusChanged 0xF6
#define STATIC_EVENTID_DRMParingStepComplete 0x5b2ebf78, 0xb752, 0x4420, 0xb4, 0x1e, 0xa4, 0x72, 0xdc, 0x95, 0x82, 0x8e
#define STATIC_EVENTID_DualMonoSpanningEvent 0xa9a29b56, 0xa84b, 0x488c, 0x89, 0xd5, 0x0d, 0x4e, 0x76, 0x57, 0xc8, 0xce
#define STATIC_EVENTID_DvbParentalRatingDescriptor 0x2a67a58d, 0xeca5, 0x4eac, 0xab, 0xcb, 0xe7, 0x34, 0xd3, 0x77, 0x6d, 0x0a
#define STATIC_EVENTID_DVBScramblingControlSpanningEvent 0x4bd4e1c4, 0x90a1, 0x4109, 0x82, 0x36, 0x27, 0xf0, 0xe, 0x7d, 0xcc, 0x5b
#define STATIC_EVENTID_EASMessageReceived 0xd10df9d5, 0xc261, 0x4b85, 0x9e, 0x8a, 0x51, 0x7b, 0x32, 0x99, 0xca, 0xb2
#define STATIC_EVENTID_EmmMessageSpanningEvent 0x6bf00268, 0x4f7e, 0x4294, 0xaa, 0x87, 0xe9, 0xe9, 0x53, 0xe4, 0x3f, 0x14
#define STATIC_EVENTID_EntitlementChanged 0xD5
#define STATIC_EVENTID_LanguageSpanningEvent 0xe292666d, 0x9c02, 0x448d, 0xaa, 0x8d, 0x78, 0x1a, 0x93, 0xfd, 0xc3, 0x95
#define STATIC_EVENTID_MMIMessage 0xA4
#define STATIC_EVENTID_NewSignalAcquired 0xc87ec52d, 0xcd18, 0x404a, 0xa0, 0x76, 0xc0, 0x2a, 0x27, 0x3d, 0x3d, 0xe7
#define STATIC_EVENTID_PBDAParentalControlEvent 0xf947aa85, 0xfb52, 0x48e8, 0xb9, 0xc5, 0xe1, 0xe1, 0xf4, 0x11, 0xa5, 0x1a
#define STATIC_EVENTID_PIDListSpanningEvent 0x47fc8f65, 0xe2bb, 0x4634, 0x9c, 0xef, 0xfd, 0xbf, 0xe6, 0x26, 0x1d, 0x5c
#define STATIC_EVENTID_PSITable 0x1b9c3703, 0xd447, 0x4e16, 0x97, 0xbb, 0x1, 0x79, 0x9f, 0xc0, 0x31, 0xed
#define STATIC_EVENTID_RRTSpanningEvent 0xf6cfc8f4, 0xda93, 0x4f2f, 0xbf, 0xf8, 0xba, 0x1e, 0xe6, 0xfc, 0xa3, 0xa2
#define STATIC_EVENTID_ServiceTerminated 0xa1d591c, 0xe0d2, 0x4f8e, 0x89, 0x60, 0x23, 0x35, 0xbe, 0xf4, 0x5c, 0xcb
#define STATIC_EVENTID_SignalAndServiceStatusSpanningEvent 0x8068c5cb, 0x3c04, 0x492b, 0xb4, 0x7d, 0x3, 0x8, 0x82, 0xd, 0xce, 0x51
#define STATIC_EVENTID_SignalStatusChanged 0x6d9cfaf2, 0x702d, 0x4b01, 0x8d, 0xff, 0x68, 0x92, 0xad, 0x20, 0xd1, 0x91
#define STATIC_EVENTID_STBChannelNumber 0xAD
#define STATIC_EVENTID_StreamIDSpanningEvent 0xcaf1ab68, 0xe153, 0x4d41, 0xa6, 0xb3, 0xa7, 0xc9, 0x98, 0xdb, 0x75, 0xee
#define STATIC_EVENTID_StreamTypeSpanningEvent 0x60
#define STATIC_EVENTID_SubtitleSpanningEvent 0x5dcec048, 0xd0b9, 0x4163, 0x87, 0x2c, 0x4f, 0x32, 0x22, 0x3b, 0xe8, 0x8a
#define STATIC_EVENTID_TeletextSpanningEvent 0x9599d950, 0x5f33, 0x4617, 0xaf, 0x7c, 0x1e, 0x54, 0xb5, 0x10, 0xda, 0xa3
#define STATIC_EVENTID_TuneFailureEvent 0x69
#define STATIC_EVENTID_TuneFailureSpanningEvent 0x6f8aa455, 0x5ee1, 0x48ab, 0xa2, 0x7c, 0x4c, 0x8d, 0x70, 0xb9, 0xae, 0xba
#define STATIC_EVENTID_TuningChanged 0x9d7e6235, 0x4b7d, 0x425d, 0xa6, 0xd1, 0xd7, 0x17, 0xc3, 0x3b, 0x9c, 0x4c
#define STATIC_EVENTID_TuningChanging 0x83183c03, 0xc09e, 0x45c4, 0xa7, 0x19, 0x80, 0x7a, 0x94, 0x95, 0x2b, 0xf9
#define STATIC_EVENTTYPE_CASDescrambleFailureEvent 0xb2127d42, 0x7be5, 0x4f4b, 0x91, 0x30, 0x66, 0x79, 0x89, 0x9f, 0x4f, 0x4b
#define STATIC_ISDB_CABLE_TV_NETWORK_TYPE 0xc974ddb5, 0x41fe, 0x4b25, 0x97, 0x41, 0x92, 0xf0, 0x49, 0xf1, 0xd5, 0xd1
#define STATIC_ISDB_S_NETWORK_TYPE 0xa1e78202, 0x1459, 0x41b1, 0x9c, 0xa9, 0x2a, 0x92, 0x58, 0x7a, 0x42, 0xcc
#define STATIC_ISDB_SATELLITE_TV_NETWORK_TYPE 0xb0a4e6a0, 0x6a1a, 0x4b83, 0xbb, 0x5b, 0x90, 0x3e, 0x1d, 0x90, 0xe6, 0xb6
#define STATIC_ISDB_T_NETWORK_TYPE 0xfc3855a6, 0xc901, 0x4f2e, 0xab, 0xa8, 0x90, 0x81, 0x5a, 0xfc, 0x6c, 0x83
#define STATIC_ISDB_TERRESTRIAL_TV_NETWORK_TYPE 0x95037f6f, 0x3ac7, 0x4452, 0xb6, 0xc4, 0x45, 0xa9, 0xce, 0x92, 0x92, 0xa2
#define STATIC_KSCATEGORY_BDA_IP_SINK 0x71985f4aL, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x00, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSCATEGORY_BDA_NETWORK_EPG 0x71985f49, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSCATEGORY_BDA_NETWORK_PROVIDER 0x71985f4b, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSCATEGORY_BDA_NETWORK_TUNER 0x71985f48, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSCATEGORY_BDA_RECEIVER_COMPONENT 0xFD0A5AF4, 0xB41D, 0x11d2, 0x9c, 0x95, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSCATEGORY_BDA_TRANSPORT_INFORMATION 0xa2e3074f, 0x6c3d, 0x11d3, 0xb6, 0x53, 0x0, 0xc0, 0x4f, 0x79, 0x49, 0x8e
#define STATIC_KSDATAFORMAT_SPECIFIER_BDA_IP 0x6b891420, 0xdb09, 0x11d2, 0x8f, 0x32, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe2
#define STATIC_KSDATAFORMAT_SUBTYPE_BDA_IP 0x5a9a213c, 0xdb08, 0x11d2, 0x8f, 0x32, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe2
#define STATIC_KSDATAFORMAT_SUBTYPE_BDA_IP_CONTROL 0x499856e8, 0xe85b, 0x48ed, 0x9b, 0xea, 0x41, 0xd, 0xd, 0xd4, 0xef, 0x81
#define STATIC_KSDATAFORMAT_TYPE_BDA_IP 0xe25f7b8e, 0xcccc, 0x11d2, 0x8f, 0x25, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe2
#define STATIC_KSDATAFORMAT_TYPE_BDA_IP_CONTROL 0xdadd5799, 0x7d5b, 0x4b63, 0x80, 0xfb, 0xd1, 0x44, 0x2f, 0x26, 0xb6, 0x21
#define STATIC_KSDATAFORMAT_TYPE_MPE 0x455f176c, 0x4b06, 0x47ce, 0x9a, 0xef, 0x8c, 0xae, 0xf7, 0x3d, 0xf7, 0xb5
#define STATIC_KSEVENTSETID_BdaCAEvent 0x488c4ccc, 0xb768, 0x4129, 0x8e, 0xb1, 0xb0, 0xa, 0x7, 0x1f, 0x90, 0x68
#define STATIC_KSEVENTSETID_BdaDiseqCEvent 0x8b19bbf0, 0x4184, 0x43ac, 0xad, 0x3c, 0xc, 0x88, 0x9b, 0xe4, 0xc2, 0x12
#define STATIC_KSEVENTSETID_BdaEvent 0xae7e55b2, 0x96d7, 0x4e29, 0x90, 0x8f, 0x62, 0xf9, 0x5b, 0x2a, 0x16, 0x79
#define STATIC_KSEVENTSETID_BdaPinEvent 0x104781cd, 0x50bd, 0x40d5, 0x95, 0xfb, 0x08, 0x7e, 0xe, 0x86, 0xa5, 0x91
#define STATIC_KSEVENTSETID_BdaTunerEvent 0xaab59e17, 0x1c9, 0x4ebf, 0x93, 0xf2, 0xfc, 0x3b, 0x79, 0xb4, 0x6f, 0x91
#define STATIC_KSMETHODSETID_BdaConditionalAccessService 0x10ced3b4, 0x320b, 0x41bf, 0x98, 0x24, 0x1b, 0x2e, 0x68, 0xe7, 0x1e, 0xb9
#define STATIC_KSMETHODSETID_BdaDebug 0x0d4a90ec, 0xc69d, 0x4ee2, 0x8c, 0x5a, 0xfb, 0x1f, 0x63, 0xa5, 0x0d, 0xa1
#define STATIC_KSMETHODSETID_BdaDrmService 0xbff6b5bb, 0xb0ae, 0x484c, 0x9d, 0xca, 0x73, 0x52, 0x8f, 0xb0, 0xb4, 0x6e
#define STATIC_KSMETHODSETID_BdaEventing 0xf99492da, 0x6193, 0x4eb0, 0x86, 0x90, 0x66, 0x86, 0xcb, 0xff, 0x71, 0x3e
#define STATIC_KSMETHODSETID_BdaGuideDataDeliveryService 0x8d9d5562, 0x1589, 0x417d, 0x99, 0xce, 0xac, 0x53, 0x1d, 0xda, 0x19, 0xf9
#define STATIC_KSMETHODSETID_BdaIsdbConditionalAccess 0x5e68c627, 0x16c2, 0x4e6c, 0xb1, 0xe2, 0xd0, 0x1, 0x70, 0xcd, 0xaa, 0xf
#define STATIC_KSMETHODSETID_BdaMux 0x942aafec, 0x4c05, 0x4c74, 0xb8, 0xeb, 0x87, 0x6, 0xc2, 0xa4, 0x94, 0x3f
#define STATIC_KSMETHODSETID_BdaNameValue 0x36e07304, 0x9f0d, 0x4e88, 0x91, 0x18, 0xac, 0xb, 0xa3, 0x17, 0xb7, 0xf2
#define STATIC_KSMETHODSETID_BdaNameValueA 0xc24096d, 0x5ff5, 0x47de, 0xa8, 0x56, 0x6, 0x2e, 0x58, 0x7e, 0x37, 0x27
#define STATIC_KSMETHODSETID_BdaScanning 0x12eb49df, 0x6249, 0x47f3, 0xb1, 0x90, 0xe2, 0x1e, 0x6e, 0x2f, 0x8a, 0x9c
#define STATIC_KSMETHODSETID_BdaTSSelector 0x1dcfafe9, 0xb45e, 0x41b3, 0xbb, 0x2a, 0x56, 0x1e, 0xb1, 0x29, 0xae, 0x98
#define STATIC_KSMETHODSETID_BdaTuner 0xb774102f, 0xac07, 0x478a, 0x82, 0x28, 0x27, 0x42, 0xd9, 0x61, 0xfa, 0x7e
#define STATIC_KSMETHODSETID_BdaUserActivity 0xeda5c834, 0x4531, 0x483c, 0xbe, 0xa, 0x94, 0xe6, 0xc9, 0x6f, 0xf3, 0x96
#define STATIC_KSMETHODSETID_BdaWmdrmSession 0x4be6fa3d, 0x7cd, 0x4139, 0x8b, 0x80, 0x8c, 0x18, 0xba, 0x3a, 0xec, 0x88
#define STATIC_KSMETHODSETID_BdaWmdrmTuner 0x86d979cf, 0xa8a7, 0x4f94, 0xb5, 0xfb, 0x14, 0xc0, 0xac, 0xa6, 0x8f, 0xe6
#define STATIC_KSNODE_BDA_8PSK_DEMODULATOR 0xe957a0e7, 0xdd98, 0x4a3c, 0x81, 0x0b, 0x35, 0x25, 0x15, 0x7a, 0xb6, 0x2e
#define STATIC_KSNODE_BDA_8VSB_DEMODULATOR 0x71985f4f, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSNODE_BDA_ANALOG_DEMODULATOR 0x634db199, 0x27dd, 0x46b8, 0xac, 0xfb, 0xec, 0xc9, 0x8e, 0x61, 0xa2, 0xad
#define STATIC_KSNODE_BDA_COFDM_DEMODULATOR 0x2dac6e05, 0xedbe, 0x4b9c, 0xb3, 0x87, 0x1b, 0x6f, 0xad, 0x7d, 0x64, 0x95
#define STATIC_KSNODE_BDA_COMMON_CA_POD 0xd83ef8fc, 0xf3b8, 0x45ab, 0x8b, 0x71, 0xec, 0xf7, 0xc3, 0x39, 0xde, 0xb4
#define STATIC_KSNODE_BDA_DRI_DRM 0x4f95ad74, 0xcefb, 0x42d2, 0x94, 0xa9, 0x68, 0xc5, 0xb2, 0xc1, 0xaa, 0xbe
#define STATIC_KSNODE_BDA_IP_SINK 0x71985f4e, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSNODE_BDA_ISDB_S_DEMODULATOR 0xedde230a, 0x9086, 0x432d, 0xb8, 0xa5, 0x66, 0x70, 0x26, 0x38, 0x07, 0xe9
#define STATIC_KSNODE_BDA_ISDB_T_DEMODULATOR 0xfcea3ae3, 0x2cb2, 0x464d, 0x8f, 0x5d, 0x30, 0x5c, 0x0b, 0xb7, 0x78, 0xa2
#define STATIC_KSNODE_BDA_OPENCABLE_POD 0x345812a0, 0xfb7c, 0x4790, 0xaa, 0x7e, 0xb1, 0xdb, 0x88, 0xac, 0x19, 0xc9
#define STATIC_KSNODE_BDA_PBDA_CAS 0xc026869f, 0x7129, 0x4e71, 0x86, 0x96, 0xec, 0x8f, 0x75, 0x29, 0x9b, 0x77
#define STATIC_KSNODE_BDA_PBDA_DRM 0x9eeebd03, 0xeea1, 0x450f, 0x96, 0xae, 0x63, 0x3e, 0x6d, 0xe6, 0x3c, 0xce
#define STATIC_KSNODE_BDA_PBDA_ISDBCAS 0xf2cf2ab3, 0x5b9d, 0x40ae, 0xab, 0x7c, 0x4e, 0x7a, 0xd0, 0xbd, 0x1c, 0x52
#define STATIC_KSNODE_BDA_PBDA_MUX 0xf88c7787, 0x6678, 0x4f4b, 0xa1, 0x3e, 0xda, 0x9, 0x86, 0x1d, 0x68, 0x2b
#define STATIC_KSNODE_BDA_PBDA_TUNER 0xaa5e8286, 0x593c, 0x4979, 0x94, 0x94, 0x46, 0xa2, 0xa9, 0xdf, 0xe0, 0x76
#define STATIC_KSNODE_BDA_PID_FILTER 0xf5412789, 0xb0a0, 0x44e1, 0xae, 0x4f, 0xee, 0x99, 0x9b, 0x1b, 0x7f, 0xbe
#define STATIC_KSNODE_BDA_QAM_DEMODULATOR 0x71985f4d, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSNODE_BDA_QPSK_DEMODULATOR 0x6390c905, 0x27c1, 0x4d67, 0xbd, 0xb7, 0x77, 0xc5, 0xd, 0x7, 0x93, 0x0
#define STATIC_KSNODE_BDA_RF_TUNER 0x71985f4c, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSNODE_BDA_TS_SELECTOR 0x5eddf185, 0xfed1, 0x4f45, 0x96, 0x85, 0xbb, 0xb7, 0x3c, 0x32, 0x3c, 0xfc
#define STATIC_KSNODE_BDA_VIDEO_ENCODER 0xd98429e3, 0x65c9, 0x4ac4, 0x93, 0xaa, 0x76, 0x67, 0x82, 0x83, 0x3b, 0x7a
#define STATIC_KSPROPSETID_BdaAutodemodulate 0xddf15b12, 0xbd25, 0x11d2, 0x9c, 0xa0, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSPROPSETID_BdaCA 0xb0693766, 0x5278, 0x4ec6, 0xb9, 0xe1, 0x3c, 0xe4, 0x5, 0x60, 0xef, 0x5a
#define STATIC_KSPROPSETID_BdaDiseqCommand 0xf84e2ab0, 0x3c6b, 0x45e3, 0xa0, 0xfc, 0x86, 0x69, 0xd4, 0xb8, 0x1f, 0x11
#define STATIC_KSPROPSETID_BdaEthernetFilter 0x71985f43, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSPROPSETID_BdaIPv4Filter 0x71985f44, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSPROPSETID_BdaIPv6Filter 0xe1785a74, 0x2a23, 0x4fb3, 0x92, 0x45, 0xa8, 0xf8, 0x80, 0x17, 0xef, 0x33
#define STATIC_KSPROPSETID_BdaNullTransform 0xddf15b0d, 0xbd25, 0x11d2, 0x9c, 0xa0, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_KSPROPSETID_BdaPIDFilter 0xd0a67d65, 0x8df, 0x4fec, 0x85, 0x33, 0xe5, 0xb5, 0x50, 0x41, 0xb, 0x85
#define STATIC_KSPROPSETID_BdaTableSection 0x516b99c5, 0x971c, 0x4aaf, 0xb3, 0xf3, 0xd9, 0xfd, 0xa8, 0xa1, 0x5e, 0x16
#define STATIC_KSPROPSETID_BdaVoidTransform 0x71985f46, 0x1ca1, 0x11d3, 0x9c, 0xc8, 0x0, 0xc0, 0x4f, 0x79, 0x71, 0xe0
#define STATIC_PBDA_ALWAYS_TUNE_IN_MUX 0x1E1D7141, 0x583F, 0x4ac2, 0xB0, 0x19, 0x1F, 0x43, 0x0E, 0xDA, 0x0F, 0x4C
#define STATIC_PINNAME_BDA_ANALOG_AUDIO 0xd28a580a, 0x9b1f, 0x4b0c, 0x9c, 0x33, 0x9b, 0xf0, 0xa8, 0xea, 0x63, 0x6b
#define STATIC_PINNAME_BDA_ANALOG_VIDEO 0x5c0c8281, 0x5667, 0x486c, 0x84, 0x82, 0x63, 0xe3, 0x1f, 0x1, 0xa6, 0xe9
#define STATIC_PINNAME_BDA_FM_RADIO 0xd2855fed, 0xb2d3, 0x4eeb, 0x9b, 0xd0, 0x19, 0x34, 0x36, 0xa2, 0xf8, 0x90
#define STATIC_PINNAME_BDA_IF_PIN 0x1a9d4a42, 0xf3cd, 0x48a1, 0x9a, 0xea, 0x71, 0xde, 0x13, 0x3c, 0xbe, 0x14
#define STATIC_PINNAME_BDA_OPENCABLE_PSIP_PIN 0x297bb104, 0xe5c9, 0x4ace, 0xb1, 0x23, 0x95, 0xc3, 0xcb, 0xb2, 0x4d, 0x4f
#define STATIC_PINNAME_BDA_TRANSPORT 0x78216a81, 0xcfa8, 0x493e, 0x97, 0x11, 0x36, 0xa6, 0x1c, 0x8, 0xbd, 0x9d
#define STATIC_PINNAME_IPSINK_INPUT 0x3fdffa70L, 0xac9a, 0x11d2, 0x8f, 0x17, 0x00, 0xc0, 0x4f, 0x79, 0x71, 0xe2
#define STATIC_PINNAME_MPE 0xc1b06d73L, 0x1dbb, 0x11d3, 0x8f, 0x46, 0x00, 0xC0, 0x4f, 0x79, 0x71, 0xE2

struct _BDA_DEBUG_DATA
{
    int lResult;
    GUID uuidDebugDataType;
    ULONG ulDataSize;
    BYTE argbDebugData;
};

struct _BDA_EVENT_DATA
{
    int lResult;
    ULONG ulEventID;
    GUID uuidEventType;
    ULONG ulEventDataLength;
    BYTE argbEventData;
};

struct _ChannelInfo
{
    LONG lFrequency;
    union
    {
        struct
        {
            LONG lONID;
            LONG lTSID;
            LONG lSID;
        } DVB;
        struct
        {
            LONG lProgNumber;
        } DC;
        struct
        {
            LONG lProgNumber;
        } ATSC;
    };
};

struct _KSM_BDA_BUFFER
{
    KSM_NODE NodeMethod;
    ULONG ulBufferSize;
    BYTE argbBuffer;
};

struct _KSM_BDA_CAS_CAPTURETOKEN
{
    KSM_NODE NodeMethod;
    ULONG ulTokenLength;
    BYTE argbToken;
};

struct _KSM_BDA_CAS_ENTITLEMENTTOKEN
{
    KSM_NODE NodeMethod;
    ULONG ulDialogRequest;
    CHAR cLanguage[12];
    ULONG ulRequestType;
    ULONG ulEntitlementTokenLen;
    BYTE argbEntitlementToken;
};

struct _KSM_BDA_DEBUG_LEVEL
{
    KSMETHOD Method;
    UCHAR ucDebugLevel;
    ULONG ulDebugStringSize;
    BYTE argbDebugString;
};

struct _KSM_BDA_GDDS_SERVICEFROMTUNEXML
{
    KSMETHOD Method;
    ULONG ulTuneXmlLength;
    BYTE argbTuneXml;
};

struct _KSM_BDA_GPNV_GETVALUE
{
    KSMETHOD Method;
    ULONG ulNameLength;
    CHAR cLanguage[12];
    BYTE argbData;
};

struct _KSM_BDA_GPNV_SETVALUE
{
    KSMETHOD Method;
    ULONG ulDialogRequest;
    CHAR cLanguage[12];
    ULONG ulNameLength;
    ULONG ulValueLength;
    BYTE argbName;
};

struct _KSM_BDA_ISDBCAS_REQUEST
{
    KSM_NODE NodeMethod;
    ULONG ulRequestID;
    ULONG ulIsdbCommandSize;
    BYTE argbIsdbCommandData;
};

struct _KSM_BDA_SCAN_FILTER
{
    KSMETHOD Method;
    ULONG ulScanModulationTypeSize;
    ULONG64 AnalogVideoStandards;
    BYTE argbScanModulationTypes;
};

struct _KSM_BDA_TUNER_TUNEREQUEST
{
    KSMETHOD Method;
    ULONG ulTuneLength;
    BYTE argbTuneData;
};

struct _KSM_BDA_WMDRM_RENEWLICENSE
{
    KSM_NODE NodeMethod;
    ULONG ulXMRLicenseLength;
    ULONG ulEntitlementTokenLength;
    BYTE argbDataBuffer;
};

struct _KSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT
{
    KSM_NODE NodeMethod;
    ULONG ulDialogRequest;
    CHAR cLanguage[12];
    ULONG ulPurchaseTokenLength;
    BYTE argbDataBuffer;
};

struct _SpanningEventEmmMessage
{
    BYTE bCAbroadcasterGroupId;
    BYTE bMessageControl;
    WORD wServiceId;
    WORD wTableIdExtension;
    BYTE bDeletionStatus;
    BYTE bDisplayingDuration1;
    BYTE bDisplayingDuration2;
    BYTE bDisplayingDuration3;
    BYTE bDisplayingCycle;
    BYTE bFormatVersion;
    BYTE bDisplayPosition;
    WORD wMessageLength;
    WCHAR szMessageArea;
};

typedef struct _BDA_DEBUG_DATA BDA_DEBUG_DATA;

typedef struct _BDA_EVENT_DATA BDA_EVENT_DATA;

typedef struct _KSM_BDA_BUFFER KSM_BDA_BUFFER;

typedef struct _KSM_BDA_CAS_CAPTURETOKEN KSM_BDA_CAS_CAPTURETOKEN;

typedef struct _KSM_BDA_CAS_ENTITLEMENTTOKEN KSM_BDA_CAS_ENTITLEMENTTOKEN;

typedef struct _KSM_BDA_DEBUG_LEVEL KSM_BDA_DEBUG_LEVEL;

typedef struct _KSM_BDA_GDDS_SERVICEFROMTUNEXML KSM_BDA_GDDS_SERVICEFROMTUNEXML;

typedef struct _KSM_BDA_GPNV_GETVALUE KSM_BDA_GPNV_GETVALUE;

typedef struct _KSM_BDA_GPNV_SETVALUE KSM_BDA_GPNV_SETVALUE;

typedef struct _KSM_BDA_ISDBCAS_REQUEST KSM_BDA_ISDBCAS_REQUEST;

typedef struct _KSM_BDA_SCAN_FILTER KSM_BDA_SCAN_FILTER;

typedef struct _KSM_BDA_TUNER_TUNEREQUEST KSM_BDA_TUNER_TUNEREQUEST;

typedef struct _KSM_BDA_WMDRM_RENEWLICENSE KSM_BDA_WMDRM_RENEWLICENSE;

typedef struct _KSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT KSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT;

typedef struct _BDA_EVENT_DATA *PBDA_EVENT_DATA;

typedef struct _KSM_BDA_BUFFER *PKSM_BDA_BUFFER;

typedef struct _KSM_BDA_CAS_CAPTURETOKEN *PKSM_BDA_CAS_CAPTURETOKEN;

typedef struct _KSM_BDA_CAS_ENTITLEMENTTOKEN *PKSM_BDA_CAS_ENTITLEMENTTOKEN;

typedef struct _KSM_BDA_DEBUG_LEVEL *PKSM_BDA_DEBUG_LEVEL;

typedef struct _KSM_BDA_GDDS_SERVICEFROMTUNEXML *PKSM_BDA_GDDS_SERVICEFROMTUNEXML;

typedef struct _KSM_BDA_GPNV_GETVALUE *PKSM_BDA_GPNV_GETVALUE;

typedef struct _KSM_BDA_GPNV_SETVALUE *PKSM_BDA_GPNV_SETVALUE;

typedef struct _KSM_BDA_ISDBCAS_REQUEST *PKSM_BDA_ISDBCAS_REQUEST;

typedef struct _KSM_BDA_SCAN_FILTER *PKSM_BDA_SCAN_FILTER;

typedef struct _KSM_BDA_TUNER_TUNEREQUEST *PKSM_BDA_TUNER_TUNEREQUEST;

typedef struct _KSM_BDA_WMDRM_RENEWLICENSE *PKSM_BDA_WMDRM_RENEWLICENSE;

typedef struct _KSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT *PKSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT;

typedef struct _SpanningEventEmmMessage SpanningEventEmmMessage;



typedef enum
{
    ChannelTypeNone = 0,
    ChannelTypeOther = 1,
    ChannelTypeVideo = 2,
    ChannelTypeAudio = 4,
    ChannelTypeText = 8,
    ChannelTypeSubtitles = 0x10,
    ChannelTypeCaptions = 0x20,
    ChannelTypeSuperimpose = 0x40,
    ChannelTypeData = 0x80,
} ChannelType;

struct _ChannelTypeInfo
{
    ChannelType channelType;
    ULONGLONG timeStamp;
};

struct _DVBScramblingControlSpanningEvent
{
    ULONG ulPID;
    BOOL fScrambled;
};

struct _KSM_BDA_CAS_CLOSEMMIDIALOG
{
    KSM_NODE NodeMethod;
    ULONG ulDialogRequest;
    CHAR cLanguage[12];
    ULONG ulDialogNumber;
    ULONG ulReason;
};

struct _KSM_BDA_CAS_OPENBROADCASTMMI
{
    KSM_NODE NodeMethod;
    ULONG ulDialogRequest;
    CHAR cLanguage[12];
    ULONG ulEventId;
};

struct _KSM_BDA_DRM_SETDRM
{
    KSM_NODE NodeMethod;
    GUID NewDRMuuid;
};

struct _KSM_BDA_EVENT_COMPLETE
{
    KSMETHOD Method;
    ULONG ulEventID;
    ULONG ulEventResult;
};

struct _KSM_BDA_GDDS_TUNEXMLFROMIDX
{
    KSMETHOD Method;
    ULONG64 ulIdx;
};

struct _KSM_BDA_GPNV_NAMEINDEX
{
    KSMETHOD Method;
    ULONG ulValueNameIndex;
};

struct _KSM_BDA_SCAN_CAPABILTIES
{
    KSMETHOD Method;
    GUID uuidBroadcastStandard;
};

struct _KSM_BDA_SCAN_START
{
    KSMETHOD Method;
    ULONG LowerFrequency;
    ULONG HigherFrequency;
};

struct _KSM_BDA_TS_SELECTOR_SETTSID
{
    KSM_NODE NodeMethod;
    USHORT usTSID;
};

struct _KSM_BDA_USERACTIVITY_USEREASON
{
    KSMETHOD Method;
    ULONG ulUseReason;
};

struct _KSM_BDA_WMDRMTUNER_GETPIDPROTECTION
{
    KSM_NODE NodeMethod;
    ULONG ulPID;
};

struct _KSM_BDA_WMDRMTUNER_SETPIDPROTECTION
{
    KSM_NODE NodeMethod;
    ULONG ulPID;
    GUID uuidKeyID;
};

struct _KSM_BDA_WMDRMTUNER_SYNCVALUE
{
    KSM_NODE NodeMethod;
    ULONG ulSyncValue;
};

struct _PIDListSpanningEvent
{
    WORD wPIDCount;
    ULONG pulPIDs[1];
};

struct _SpanningEventDescriptor
{
    WORD wDataLen;
    WORD wProgNumber;
    WORD wSID;
    BYTE bDescriptor[1];
};

typedef struct _ChannelTypeInfo ChannelTypeInfo;

struct DvbParentalRatingParam
{
    CHAR szCountryCode[4];
    BYTE bRating;
};

struct DvbParentalRatingDescriptor
{
    ULONG ulNumParams;
    DvbParentalRatingParam pParams[1];
};

typedef struct _DVBScramblingControlSpanningEvent DVBScramblingControlSpanningEvent;

typedef struct tagKS_DATARANGE_BDA_ANTENNA KS_DATARANGE_BDA_ANTENNA;

struct KSEVENTDATA_BDA_RF_TUNER_SCAN_S
{
    KSEVENTDATA EventData;
    ULONG StartFrequency;
    ULONG EndFrequency;
    BDA_LockType LockRequested;
};

typedef struct _KSM_BDA_CAS_CLOSEMMIDIALOG KSM_BDA_CAS_CLOSEMMIDIALOG;

typedef struct _KSM_BDA_CAS_OPENBROADCASTMMI KSM_BDA_CAS_OPENBROADCASTMMI;

typedef struct _KSM_BDA_DRM_SETDRM KSM_BDA_DRM_SETDRM;

typedef struct _KSM_BDA_EVENT_COMPLETE KSM_BDA_EVENT_COMPLETE;

typedef struct _KSM_BDA_GDDS_TUNEXMLFROMIDX KSM_BDA_GDDS_TUNEXMLFROMIDX;

typedef struct _KSM_BDA_GPNV_NAMEINDEX KSM_BDA_GPNV_NAMEINDEX;

typedef struct _KSM_BDA_SCAN_CAPABILTIES KSM_BDA_SCAN_CAPABILTIES;

typedef struct _KSM_BDA_SCAN_START KSM_BDA_SCAN_START;

typedef struct _KSM_BDA_TS_SELECTOR_SETTSID KSM_BDA_TS_SELECTOR_SETTSID;

typedef struct _KSM_BDA_USERACTIVITY_USEREASON KSM_BDA_USERACTIVITY_USEREASON;

struct KSM_BDA_WMDRM_LICENSE
{
    KSM_NODE NodeMethod;
    GUID uuidKeyID;
};

typedef struct _KSM_BDA_WMDRMTUNER_GETPIDPROTECTION KSM_BDA_WMDRMTUNER_GETPIDPROTECTION;

typedef struct _KSM_BDA_WMDRMTUNER_SETPIDPROTECTION KSM_BDA_WMDRMTUNER_SETPIDPROTECTION;

typedef struct _KSM_BDA_WMDRMTUNER_SYNCVALUE KSM_BDA_WMDRMTUNER_SYNCVALUE;

struct KSP_NODE_ESPID
{
    KSP_NODE Property;
    ULONG EsPid;
};

struct KSPROPERTY_BDA_RF_TUNER_CAPS_S
{
    KSP_NODE Property;
    ULONG Mode;
    ULONG AnalogStandardsSupported;
    ULONG DigitalStandardsSupported;
    ULONG MinFrequency;
    ULONG MaxFrequency;
    ULONG SettlingTime;
    ULONG AnalogSensingRange;
    ULONG DigitalSensingRange;
    ULONG MilliSecondsPerMHz;
};

struct KSPROPERTY_BDA_RF_TUNER_SCAN_STATUS_S
{
    KSP_NODE Property;
    ULONG CurrentFrequency;
    ULONG FrequencyRangeMin;
    ULONG FrequencyRangeMax;
    ULONG MilliSecondsLeft;
};

struct KSPROPERTY_BDA_RF_TUNER_STANDARD_MODE_S
{
    KSP_NODE Property;
    BOOL AutoDetect;
};

struct KSPROPERTY_BDA_RF_TUNER_STANDARD_S
{
    KSP_NODE Property;
    BDA_SignalType SignalType;
    ULONG SignalStandard;
};

typedef struct RATING_ATTRIBUTE *LPRATING_ATTRIBUTE;

typedef struct RATING_INFO *LPRATING_INFO;

typedef struct RATING_SYSTEM *LPRATING_SYSTEM;

typedef struct _PBDAParentalControl PBDAParentalControl;

typedef struct _PIDListSpanningEvent PIDListSpanningEvent;

typedef struct tagKS_DATARANGE_BDA_ANTENNA *PKS_DATARANGE_BDA_ANTENNA;

typedef struct KSEVENTDATA_BDA_RF_TUNER_SCAN_S *PKSEVENTDATA_BDA_RF_TUNER_SCAN_S;

typedef struct _KSM_BDA_CAS_CLOSEMMIDIALOG *PKSM_BDA_CAS_CLOSEMMIDIALOG;

typedef struct _KSM_BDA_CAS_OPENBROADCASTMMI *PKSM_BDA_CAS_OPENBROADCASTMMI;

typedef struct _KSM_BDA_DRM_SETDRM *PKSM_BDA_DRM_SETDRM;

typedef struct _KSM_BDA_EVENT_COMPLETE *PKSM_BDA_EVENT_COMPLETE;

typedef struct _KSM_BDA_GDDS_TUNEXMLFROMIDX *PKSM_BDA_GDDS_TUNEXMLFROMIDX;

typedef struct _KSM_BDA_GPNV_NAMEINDEX *PKSM_BDA_GPNV_NAMEINDEX;

typedef struct _KSM_BDA_SCAN_CAPABILTIES *PKSM_BDA_SCAN_CAPABILTIES;

typedef struct _KSM_BDA_SCAN_START *PKSM_BDA_SCAN_START;

typedef struct _KSM_BDA_TS_SELECTOR_SETTSID *PKSM_BDA_TS_SELECTOR_SETTSID;

typedef struct _KSM_BDA_USERACTIVITY_USEREASON *PKSM_BDA_USERACTIVITY_USEREASON;

typedef struct KSM_BDA_WMDRM_LICENSE *PKSM_BDA_WMDRM_LICENSE;

typedef struct _KSM_BDA_WMDRMTUNER_GETPIDPROTECTION *PKSM_BDA_WMDRMTUNER_GETPIDPROTECTION;

typedef struct _KSM_BDA_WMDRMTUNER_SETPIDPROTECTION *PKSM_BDA_WMDRMTUNER_SETPIDPROTECTION;

typedef struct _KSM_BDA_WMDRMTUNER_SYNCVALUE *PKSM_BDA_WMDRMTUNER_SYNCVALUE;

typedef struct KSP_NODE_ESPID *PKSP_NODE_ESPID;

typedef struct KSPROPERTY_BDA_RF_TUNER_CAPS_S *PKSPROPERTY_BDA_RF_TUNER_CAPS_S;

typedef struct KSPROPERTY_BDA_RF_TUNER_SCAN_STATUS_S *PKSPROPERTY_BDA_RF_TUNER_SCAN_STATUS_S;

typedef struct KSPROPERTY_BDA_RF_TUNER_STANDARD_MODE_S *PKSPROPERTY_BDA_RF_TUNER_STANDARD_MODE_S;

typedef struct KSPROPERTY_BDA_RF_TUNER_STANDARD_S *PKSPROPERTY_BDA_RF_TUNER_STANDARD_S;

#pragma pack(push, 1)
struct RATING_ATTRIBUTE
{
    DWORD rating_attribute_id;
    DWORD rating_attribute_value;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct RATING_SYSTEM
{
    GUID rating_system_id;
    BYTE rating_system_is_age_type : 1;
    BYTE reserved : 7;
    BYTE country_code[3];
    DWORD rating_attribute_count;
    RATING_ATTRIBUTE *lpratingattrib;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct _PBDAParentalControl
{
    ULONG rating_system_count;
    RATING_SYSTEM *rating_systems;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct RATING_INFO
{
    DWORD rating_system_count;
    RATING_SYSTEM *lpratingsystem;
};
#pragma pack(pop)

typedef struct _SpanningEventDescriptor SpanningEventDescriptor;

struct tagKS_DATARANGE_BDA_ANTENNA
{
    KSDATARANGE DataRange;
};

#define DEFINE_KSEVENT_BDA_CA_MODULE_STATUS_CHANGED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_CA_MODULE_STATUS_CHANGED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_BDA_CA_MODULE_UI_REQUESTED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_CA_MODULE_UI_REQUESTED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_BDA_CA_SMART_CARD_STATUS_CHANGED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_CA_SMART_CARD_STATUS_CHANGED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_BDA_DISEQC_DATA_RECEIVED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_DISEQC_DATA_RECEIVED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_BDA_PROGRAM_FLOW_STATUS_CHANGED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_PROGRAM_FLOW_STATUS_CHANGED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_ITEM_BDA_PENDINGEVENT(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_EVENT_PENDINGEVENT, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_ITEM_BDA_PIN_CONNECTED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_PIN_CONNECTED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSEVENT_ITEM_BDA_PIN_DISCONNECTED(AddHandler, RemoveHandler, SupportHandler) DEFINE_KSEVENT_ITEM(KSEVENT_BDA_PIN_DISCONNECTED, sizeof (KSEVENTDATA), 0, (AddHandler), (RemoveHandler), (SupportHandler))
#define DEFINE_KSMETHOD_BDA_DRM_DRMSTATUS(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_DRM_DRMSTATUS, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_DRM_DRMSTATUS), SupportHandler)
#define DEFINE_KSMETHOD_BDA_DRM_SETDRM(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_DRM_CURRENT, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_DRM_SETDRM), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_CRL(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_CRL, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_BUFFER), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_GETLICENSE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_LICENSE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRM_LICENSE), sizeof (BDA_BUFFER), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_KEYINFO(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_KEYINFO, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_WMDRM_KEYINFOLIST), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_REISSUELICENSE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_REISSUELICENSE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRM_LICENSE), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_RENEWLICENSE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_RENEWLICENSE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRM_RENEWLICENSE), sizeof (BDA_WMDRM_RENEWLICENSE), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_REVINFO(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_REVINFO, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_BUFFER), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_STATUS(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_STATUS, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_WMDRM_STATUS), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRM_TRANSACTMESSAGE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRM_MESSAGE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_BUFFER), sizeof (BDA_BUFFER), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_CANCELCAPTURETOKEN(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_CANCELCAPTURETOKEN, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_BUFFER), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_GETPIDPROTECTION(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_GETPIDPROTECTION, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRMTUNER_GETPIDPROTECTION), sizeof (BDA_WMDRMTUNER_PIDPROTECTION), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_PURCHASE_ENTITLEMENT(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_PURCHASE_ENTITLEMENT, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRMTUNER_PURCHASEENTITLEMENT), sizeof (BDA_WMDRMTUNER_PURCHASEENTITLEMENT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_SETPIDPROTECTION(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_SETPIDPROTECTION, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRMTUNER_SETPIDPROTECTION), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_SETSYNCVALUE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_SETSYNCVALUE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_WMDRMTUNER_SYNCVALUE), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_BDA_WMDRMTUNER_STARTCODEPROFILE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_WMDRMTUNER_STARTCODEPROFILE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSP_NODE), sizeof (BDA_BUFFER), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_CAS_CHECKENTITLEMENTTOKEN(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CAS_CHECKENTITLEMENTTOKEN, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_CAS_ENTITLEMENTTOKEN), sizeof (BDA_CAS_CHECK_ENTITLEMENTTOKEN), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_CAS_CLOSEMMIDIALOG(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CAS_CLOSEMMIDIALOG, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_CAS_CLOSEMMIDIALOG), sizeof (BDA_CAS_CLOSE_MMIDIALOG), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_CAS_OPENBROADCASTMMI(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CAS_OPENBROADCASTMMI, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_CAS_OPENBROADCASTMMI), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_CAS_SETCAPTURETOKEN(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_CAS_SETCAPTURETOKEN, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_CAS_CAPTURETOKEN), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_DEBUG_DATA(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_DEBUG_DATA, KSMETHOD_TYPE_MODIFY, (MethodHandler), sizeof (KSMETHOD), 0, SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_DEBUG_LEVEL(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_DEBUG_LEVEL, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_DEBUG_LEVEL), sizeof (LONG), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_EVENT_COMPLETE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_EVENT_COMPLETE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_EVENT_COMPLETE), sizeof (LONG), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_EVENT_DATA(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_EVENT_DATA, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_EVENT_DATA), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_DATA(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_DATA, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_GDDS_DATA), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_DATATYPE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_DATATYPE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_GDDS_DATATYPE), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_DATAUPDATE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_DATAUPDATE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_GETSERVICES(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_GETSERVICES, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_BUFFER), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_SERVICEFROMTUNEXML(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_SERVICEFROMTUNEXML, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_GDDS_SERVICEFROMTUNEXML), sizeof (BDA_STRING), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GDDS_TUNEXMLFROMIDX(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GDDS_TUNEXMLFROMIDX, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_GDDS_TUNEXMLFROMIDX), sizeof (BDA_STRING), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GPNV_GETVALUE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GPNV_GETVALUE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_GPNV_GETVALUE), sizeof (BDA_STRING), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GPNV_GETVALUEUPDATENAME(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GPNV_GETVALUEUPDATENAME, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_STRING), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GPNV_NAMEFROMINDEX(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GPNV_NAMEFROMINDEX, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_GPNV_NAMEINDEX), sizeof (BDA_STRING), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_GPNV_SETVALUE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_GPNV_SETVALUE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_GPNV_SETVALUE), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_ISDBCAS_RESPONSEDATA(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_ISDBCAS_RESPONSEDATA, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_ISDBCAS_RESPONSEDATA), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_ISDBCAS_SETREQUEST(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_ISDBCAS_SETREQUEST, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_ISDBCAS_REQUEST), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_MUX_GETPIDLIST(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_MUX_GETPIDLIST, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_BUFFER), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_MUX_SETPIDLIST(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_MUX_SETPIDLIST, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_BUFFER), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCAN_CAPABILTIES(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCAN_CAPABILTIES, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_SCAN_CAPABILTIES), sizeof (BDA_SCAN_CAPABILTIES), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCAN_FILTER(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCAN_FILTER, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_SCAN_FILTER), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCAN_RESUME(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCAN_RESUME, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCAN_START(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCAN_START, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_SCAN_START), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCAN_STOP(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCAN_STOP, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_SCANNING_STATE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_SCANNING_STATE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_SCAN_STATE), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_TS_SELECTOR_GETTSINFORMATION(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_TS_SELECTOR_GETTSINFORMATION, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_NODE), sizeof (BDA_TS_SELECTORINFO), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_TS_SELECTOR_SETTSID(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_TS_SELECTOR_SETTSID, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_TS_SELECTOR_SETTSID), sizeof (ULONG), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_TUNER_GETTUNERSTATE(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_TUNER_GETTUNERSTATE, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_TUNER_TUNERSTATE), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_TUNER_SETTUNER(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_TUNER_SETTUNER, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_TUNER_TUNEREQUEST), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_TUNER_SIGNALNOISERATIO(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_TUNER_SIGNALNOISERATIO, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_TUNER_DIAGNOSTICS), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_USERACTIVITY_DETECTED(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_USERACTIVITY_DETECTED, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_USERACTIVITY_INTERVAL(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_USERACTIVITY_INTERVAL, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSMETHOD), sizeof (BDA_USERACTIVITY_INTERVAL), SupportHandler)
#define DEFINE_KSMETHOD_ITEM_BDA_USERACTIVITY_USEREASON(MethodHandler, SupportHandler) DEFINE_KSMETHOD_ITEM(KSMETHOD_BDA_USERACTIVITY_USEREASON, KSMETHOD_TYPE_MODIFY | KSMETHOD_TYPE_SOURCE, (MethodHandler), sizeof (KSM_BDA_USERACTIVITY_USEREASON), sizeof (PBDARESULT), SupportHandler)
#define DEFINE_KSPROPERTY_ITEM_BDA_AUTODEMODULATE_START(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_AUTODEMODULATE_START, FALSE, sizeof (KSP_NODE), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_AUTODEMODULATE_STOP(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_AUTODEMODULATE_STOP, FALSE, sizeof (KSP_NODE), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_CA_MODULE_STATUS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CA_MODULE_STATUS, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_CA_MODULE_UI(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CA_MODULE_UI, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_CA_MODULE_UI), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_CA_REMOVE_PROGRAM(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CA_REMOVE_PROGRAM, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_CA_SET_PROGRAM_PIDS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CA_SET_PROGRAM_PIDS, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_PROGRAM_PID_LIST), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_CA_SMART_CARD_STATUS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_CA_SMART_CARD_STATUS, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_ENABLE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_ENABLE, (GetHandler), sizeof (KSP_NODE), sizeof (BOOL), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_LNB_SOURCE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_LNB_SOURCE, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_REPEATS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_REPEATS, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_RESPONSE(GetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_RESPONSE, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_DISEQC_RESPONSE), NULL, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_SEND(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_SEND, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_DISEQC_SEND), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_DISEQC_USETONEBURST(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_DISEQC_USETONEBURST, (GetHandler), sizeof (KSP_NODE), sizeof (BOOL), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_ECM_MAP_STATUS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_ECM_MAP_STATUS, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_ETHERNET_FILTER_MULTICAST_LIST(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_LIST, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_ETHERNET_ADDRESS_LIST), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_ETHERNET_FILTER_MULTICAST_LIST_SIZE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_LIST_SIZE, (GetHandler), sizeof (KSPROPERTY), sizeof (ULONG), FALSE, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_ETHERNET_FILTER_MULTICAST_MODE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_ETHERNET_FILTER_MULTICAST_MODE, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_MULTICAST_MODE), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_GUARD_INTERVAL(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_GUARD_INTERVAL, (GetHandler), sizeof (KSP_NODE), sizeof (GuardInterval), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_INNER_FEC_RATE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_INNER_FEC_RATE, (GetHandler), sizeof (KSP_NODE), sizeof (BinaryConvolutionCodeRate), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_INNER_FEC_TYPE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_INNER_FEC_TYPE, (GetHandler), sizeof (KSP_NODE), sizeof (FECMethod), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv4_FILTER_MULTICAST_LIST(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_LIST, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_IPv4_ADDRESS_LIST), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv4_FILTER_MULTICAST_LIST_SIZE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_LIST_SIZE, (GetHandler), sizeof (KSPROPERTY), sizeof (ULONG), FALSE, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv4_FILTER_MULTICAST_MODE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv4_FILTER_MULTICAST_MODE, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_MULTICAST_MODE), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv6_FILTER_MULTICAST_LIST(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_LIST, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_IPv6_ADDRESS_LIST), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv6_FILTER_MULTICAST_LIST_SIZE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_LIST_SIZE, (GetHandler), sizeof (KSPROPERTY), sizeof (ULONG), FALSE, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_IPv6_FILTER_MULTICAST_MODE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_IPv6_FILTER_MULTICAST_MODE, (GetHandler), sizeof (KSPROPERTY), sizeof (BDA_MULTICAST_MODE), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_LNB_LOF_HIGH_BAND(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_LNB_LOF_HIGH_BAND, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_LNB_LOF_LOW_BAND(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_LNB_LOF_LOW_BAND, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_LNB_SWITCH_FREQUENCY(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_LNB_SWITCH_FREQUENCY, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_MODULATION_TYPE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_MODULATION_TYPE, (GetHandler), sizeof (KSP_NODE), sizeof (ModulationType), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_NULL_TRANSFORM_START(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NULL_TRANSFORM_START, FALSE, sizeof (KSPROPERTY), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_NULL_TRANSFORM_STOP(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_NULL_TRANSFORM_STOP, FALSE, sizeof (KSPROPERTY), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_OUTER_FEC_RATE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_OUTER_FEC_RATE, (GetHandler), sizeof (KSP_NODE), sizeof (BinaryConvolutionCodeRate), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_OUTER_FEC_TYPE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_OUTER_FEC_TYPE, (GetHandler), sizeof (KSP_NODE), sizeof (FECMethod), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_PIDFILTER_LIST_PIDS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIDFILTER_LIST_PIDS, (GetHandler), sizeof (KSP_NODE), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_PIDFILTER_MAP_PIDS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIDFILTER_MAP_PIDS, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_PID_MAP), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_PIDFILTER_UNMAP_PIDS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PIDFILTER_UNMAP_PIDS, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_PID_UNMAP), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_PILOT(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PILOT, (GetHandler), sizeof (KSP_NODE), sizeof (Pilot), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_PLP_NUMBER(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_PLP_NUMBER, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_BANDWIDTH(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_BANDWIDTH, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_FREQUENCY(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_FREQUENCY, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_FREQUENCY_MULTIPLIER(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_FREQUENCY_MULTIPLIER, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_POLARITY(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_POLARITY, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_RANGE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_RANGE, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_RF_TUNER_TRANSPONDER(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_RF_TUNER_TRANSPONDER, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_ROLL_OFF(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_ROLL_OFF, (GetHandler), sizeof (KSP_NODE), sizeof (RollOff), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SAMPLE_TIME(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SAMPLE_TIME, (GetHandler), sizeof (KSP_NODE), sizeof (LONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_LOCK_CAPS(GetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_LOCK_CAPS, (GetHandler), sizeof (KSP_NODE), sizeof (BOOL), NULL, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_LOCK_TYPE(GetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_LOCK_TYPE, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_LockType), NULL, NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_LOCKED(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_LOCKED, (GetHandler), sizeof (KSP_NODE), sizeof (BOOL), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_PRESENT(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_PRESENT, (GetHandler), sizeof (KSP_NODE), sizeof (BOOL), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_QUALITY(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_QUALITY, (GetHandler), sizeof (KSP_NODE), sizeof (LONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNAL_STRENGTH(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNAL_STRENGTH, (GetHandler), sizeof (KSP_NODE), sizeof (LONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SIGNALTIMEOUTS(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SIGNALTIMEOUTS, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_SIGNAL_TIMEOUTS), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SPECTRAL_INVERSION(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SPECTRAL_INVERSION, (GetHandler), sizeof (KSP_NODE), sizeof (SpectralInversion), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_SYMBOL_RATE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_SYMBOL_RATE, (GetHandler), sizeof (KSP_NODE), sizeof (ULONG), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_TABLE_SECTION(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_TABLE_SECTION, (GetHandler), sizeof (KSP_NODE), sizeof (BDA_TABLE_SECTION), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_TRANSMISSION_MODE(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_TRANSMISSION_MODE, (GetHandler), sizeof (KSP_NODE), sizeof (TransmissionMode), (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_VOID_TRANSFORM_START(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_VOID_TRANSFORM_START, FALSE, sizeof (KSPROPERTY), 0, (SetHandler), NULL, 0, NULL, NULL, 0)
#define DEFINE_KSPROPERTY_ITEM_BDA_VOID_TRANSFORM_STOP(GetHandler, SetHandler) DEFINE_KSPROPERTY_ITEM(KSPROPERTY_BDA_VOID_TRANSFORM_STOP, FALSE, sizeof (KSPROPERTY), 0, (SetHandler), NULL, 0, NULL, NULL, 0)

#endif /* _BDAMEDIA_ */
