/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pool tags of the kernel components
 * COPYRIGHT:   Copyright 2005 Steven Edwards <sedwards@reactos.com>
 *              Copyright 2006 Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2015 Thomas Faber <thomas.faber@reactos.org>
 *              Copyright 2021 George Bișoc <george.bisoc@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define TAG_BCB                     'cBcC'

#define TAG_INIT                    'tinI'

#define TAG_PROFILE                 'forP'

#define TAG_DEBUG_EVENT 'EgbD'

#define TAG_KDBG        'GBDK'

#define TAG_KNMI                    'IMNK'

#define TAG_KERNEL                  '  eK'

#define TAG_FLOATING_POINT_FX       'xFpF'

#define TAG_FLOATING_POINT_CONTEXT  'oCpF'

#define TAG_RANGE   'ARSF'

#define TAG_FLOCK   'KCLF'

#define TAG_FSTUB   'BtsF'

#define TAG_IO_TIMER            'MTOI'

#define TAG_DRIVER_EXTENSION    'EVRD'

#define TAG_FILE_NAME           'MANF'

#define TAG_FILE_SYSTEM         'SYSF'

#define TAG_IO                  '  oI'

#define TAG_IO_FULL_DRIVER_PATH '2NoI'

#define TAG_ERROR_LOG           'rEoI'

#define TAG_EA                  'aEoI'

#define TAG_IO_NAME             'mNoI'

#define TAG_IOWI                'IWOI'

#define TAG_IRP                 ' prI'

#define TAG_IRP_EXTENSION       'xprI'

#define TAG_IOBUF               'UBOI'

#define TAG_IO_INTERRUPT        'nioI'

#define TAG_MDL                 ' LDM'

#define TAG_IO_DEVNODE          'donD'

#define TAG_PNP_NOTIFY          'NPnP'

#define TAG_PNP_ROOT            'RPnP'
#define TAG_PNP_SWDEVICE        'SPnP'

#define TAG_PNP_GUIDS           'GPnP'

#define TAG_IO_RESOURCE         'CRSR'

#define TAG_IO_TIMER            'MTOI'

#define TAG_VPB                 ' BPV'

#define TAG_RTLREGISTRY         'vrqR'

#define TAG_PNP_DEVACTION       'aDpP'

#define TAG_DEV2DOS             ' d2D'

#define TAG_MODULE_OBJECT       'omlk'

#define TAG_LDR_WSTR            'swlk'

#define TAG_LDR_IMPORTS         'milk'

#define TAG_NONE                'enoN'

#define OB_NAME_TAG             'mNbO'

#define OB_DIR_TAG              'iDbO'

#define TAG_WAIT                'tiaW'

#define TAG_SEC_QUERY           'qSbO'

#define TAG_OBJECT_TYPE         'TjbO'

#define TAG_SYMLINK_TARGET      'TMYS'

#define TAG_OB_HANDLE           'dHbO'

#define TAG_PS_IMPERSONATION    'mIsP'

#define TAG_EJOB                'BOJE'

#define TAG_TERMINATE_APC       'CPAT'

#define TAG_PS_APC              'pasP'

#define TAG_SHIM                'MIHS'

#define TAG_QUOTA_BLOCK         'bQsP'

#define TAG_THREAD_NAME         'mNhT'

#define TAG_PS_TLS              'slTP'

#define TAG_PS_PROCESS_PARAMETERS 'pPsP'
#define TAG_PS_HANDLE_LIST      'lHsP'

#define TAG_PS_API_SET          'sApP'

#define TAG_WOW64_PROCESS       '46WP'

#define TAG_PS_ENERGY           'nEsP'

#define TAG_PS_PROPERTY         'rPsP'

#define TAG_PS_WS_WATCH         'wWsP'

#define TAG_KE_XSTATE           'sXeK'

#define TAG_ATMT    'TotA'

#define TAG_RTHL    'LHtR'

#define TAG_USTR    'RTSU'

#define TAG_ASTR    'RTSA'

#define TAG_OSTR    'RTSO'

#define TAG_SE                  '  eS'

#define TAG_ACL                 'cAeS'

#define TAG_SID                 'iSeS'

#define TAG_SD                  'dSeS'

#define TAG_QOS                 'sQeS'

#define TAG_SEPA                'aPeS'

#define TAG_PRIVILEGE_SET       'rPeS'

#define TAG_SE_TOKEN_LOCK       'lTeS'

#define TAG_SE_BNO                      'nBeS'

#define TAG_DACL                'lcaD'

#if (NTDDI_VERSION >= NTDDI_WIN11_GE)
#define TAG_IO_DRIVER_NAME      '1NoI'
#else
#define TAG_IO_DRIVER_NAME      TAG_IO
#endif
