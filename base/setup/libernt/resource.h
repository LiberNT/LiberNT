/*
 * PROJECT:     ReactOS GUI first stage setup application
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Resource header
 * COPYRIGHT:   Copyright 2008-2010 Matthias Kupfer <mkupfer@reactos.org>
 *              Copyright 2008-2009 Dmitry Chapyshev <dmitry@reactos.org>
 *              Copyright 2018-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

/* Bitmaps */
#define IDB_WATERMARK       100
#define IDB_LOGO            102
#define IDB_LOGOMASK        103

/* Icons */
#define IDI_MAIN            3000


#define IDC_STATIC          -1

/* Dialogs */
#define IDD_PROCESSPAGE      2060
#define IDC_ACTIVITY         2061
#define IDC_ITEM             2062
#define IDC_PROCESSPROGRESS  2063

#define IDD_FINISHPAGE       2070
#define IDC_FINISHTEXT       2072
#define IDC_RESTART_PROGRESS 2073
#define IDC_FINISHLOGO       2074

#define IDD_ACCOUNTPAGE      2110
#define IDC_COMPUTERNAME     2111
#define IDC_USERNAME         2112
#define IDC_PASSWORD1        2113
#define IDC_PASSWORD2        2114
#define IDC_TIMEZONELIST     2121
#define IDC_AUTODAYLIGHT     2122

#define IDD_SETUPFRAME       2200
#define IDC_FRAME_TITLE      2201
#define IDC_FRAME_SUBTITLE   2202
#define IDC_FRAME_PAGE       2203
#define IDC_FRAME_BACK       2204
#define IDC_FRAME_NEXT       2205

#define IDD_DISKPAGE         2210
#define IDC_DISKLIST         2211
#define IDC_DISKINFO         2212
#define IDC_DISKNEW          2213
#define IDC_DISKDELETE       2214
#define IDC_DISKFORMAT       2215
#define IDC_DISKEXTEND       2216
#define IDC_DISKSHRINK       2217
#define IDC_DISKERASE        2218
#define IDC_DISKREFRESH      2219
#define IDC_DISKNOTE         2220

#define IDD_NEWPARTITION     2230
#define IDC_NEWSIZE          2231
#define IDC_NEWSIZE_SPIN     2232
#define IDC_NEWSIZE_RANGE    2233

#define IDD_LANGUAGEPAGE     2250
#define IDC_LANGUAGELIST     2251
#define IDC_LAYOUTLIST       2252

#define IDD_APPSPAGE         2260
#define IDC_APPLIST          2261

#define IDD_READYPAGE        2240
#define IDC_READY_SUMMARY    2241
#define IDC_READY_WARNING    2242
#define IDC_CONFIRM_INSTALL  2243

#define IDD_RESIZE                  2140
#define IDC_RESIZE_LABEL_TOTAL      2141
#define IDC_RESIZE_TOTAL            2142
#define IDC_RESIZE_LABEL_AVAILABLE  2143
#define IDC_RESIZE_AVAILABLE        2144
#define IDC_RESIZE_LABEL_AMOUNT     2145
#define IDC_RESIZE_AMOUNT           2146
#define IDC_RESIZE_SPIN             2147
#define IDC_RESIZE_LABEL_AFTER      2148
#define IDC_RESIZE_AFTER            2149
#define IDC_RESIZE_ICON             2150
#define IDC_RESIZE_NOTE             2151



/* Strings */
#define IDS_SHRINK_CAPTION    5400
#define IDS_EXTEND_CAPTION    5401
#define IDS_SHRINK_TOTAL      5402
#define IDS_SHRINK_AVAILABLE  5403
#define IDS_SHRINK_AMOUNT     5404
#define IDS_SHRINK_AFTER      5405
#define IDS_SHRINK_BUTTON     5406
#define IDS_EXTEND_TOTAL      5407
#define IDS_EXTEND_AVAILABLE  5408
#define IDS_EXTEND_AMOUNT     5409
#define IDS_EXTEND_AFTER      5410
#define IDS_EXTEND_BUTTON     5411
#define IDS_RESIZE_PENDING    5412
#define IDS_RESIZE_FAILED     5413

#define IDS_CAPTION          5000
#define IDS_DRIVETITLE       5007
#define IDS_PROCESSTITLE     5011
#define IDS_PROCESSSUBTITLE  5012
#define IDS_ACCOUNTTITLE     5013
#define IDS_ABORTSETUP       5020
#define IDS_ABORTSETUP2      5021
#define IDS_NO_TXTSETUP_SIF  5022
#define IDS_FINISH_NO_REBOOT 5023
#define IDS_ABORT_NO_REBOOT  5024
#define IDS_INSTALLBTN       5030
#define IDS_RESTARTBTN       5031
#define IDS_POSTPONEBTN      5032


/* Progress Strings */
#define IDS_FORMATTING_PROGRESS1    5200
#define IDS_FORMATTING_PROGRESS2    5201
#define IDS_CHECKING_PROGRESS1      5202
#define IDS_CHECKING_PROGRESS2      5203
#define IDS_COPYING                 5204
#define IDS_MOVING                  5205
#define IDS_RENAMING                5206
#define IDS_DELETING                5207
#define IDS_CONFIG_SYSTEM_PARTITION 5210
#define IDS_PREPARE_PARTITIONS      5211
#define IDS_PREPARE_FILES           5212
#define IDS_COPYING_FILES           5213
#define IDS_CREATE_REGISTRY         5214
// #define IDS_INSTALL_FINALIZE        5216
#define IDS_INSTALL_BOOTLOADER      5217
#define IDS_CONFIGURE_SYSTEM        5218

#define IDS_REG_DONE                    5220
#define IDS_REG_REGHIVEUPDATE           5221
#define IDS_REG_IMPORTFILE              5222
#define IDS_REG_DISPLAYSETTINGSUPDATE   5223
#define IDS_REG_LOCALESETTINGSUPDATE    5224
#define IDS_REG_ADDKBLAYOUTS            5225
#define IDS_REG_KEYBOARDSETTINGSUPDATE  5226
#define IDS_REG_CODEPAGEINFOUPDATE      5227

#define IDS_CONFIG_PHASE_FIRST          5230
#define IDS_CONFIG_FOLDERS              5230
#define IDS_CONFIG_DEVICES              5231
#define IDS_CONFIG_NETWORK              5232
#define IDS_CONFIG_SETTINGS             5233
#define IDS_CONFIG_COMPONENTS           5234
#define IDS_CONFIG_SHORTCUTS            5235
#define IDS_CONFIG_SECURITY             5236
#define IDS_CONFIG_ACCOUNT              5237
#define IDS_CONFIG_FINISH               5238
#define IDS_CONFIG_PHASE_LAST           5238


/* Error Strings */

// ERROR_CREATE_PARTITION // TODO?
#define IDS_ERROR_CREATE_PARTITION              5305

#define IDS_WARN_DELETE_PARTITION_TITLE         5306
#define IDS_WARN_DELETE_PARTITION               5307
#define IDS_WARN_DELETE_MBR_EXTENDED_PARTITION  5308

// ERROR_WRITE_PTABLE

// ERROR_SYSTEM_PARTITION_NOT_FOUND
// ERROR_SYSTEM_PARTITION // TODO

// ERROR_FORMAT_UNRECOGNIZED_VOLUME // TODO

// ERROR_COULD_NOT_FORMAT // TODO?

// ERROR_FORMATTING_PARTITION

// ERROR_COULD_NOT_CHECK // TODO?

// ERROR_CHECKING_PARTITION // TODO

// ERROR_WRITE_BOOT
// ERROR_INSTALL_BOOTCODE
// STATUS_NOT_SUPPORTED for bootloader
// ERROR_BOOTLDR_FAILED // TODO

#define IDS_ERROR_COMPUTERNAME      5321
#define IDS_ERROR_USERNAME          5322
#define IDS_ERROR_USEREXISTS        5323
#define IDS_ERROR_PASSWORDMATCH     5324

#define IDS_DISKSUBTITLE            5500
#define IDS_ACCOUNTZONESUBTITLE     5501
#define IDS_READYTITLE              5502
#define IDS_READYSUBTITLE           5503
#define IDS_DONETITLE               5504
#define IDS_ABORTTITLE              5505
#define IDS_DONE_REBOOT             5506
#define IDS_ABORT_REBOOT            5507
#define IDS_BACKBTN                 5508
#define IDS_NEXTBTN                 5509
#define IDS_CANCELBTN               5510
#define IDS_CLOSEBTN                5511

#define IDS_DISK_COL_NAME           5520
#define IDS_DISK_COL_TOTAL          5521
#define IDS_DISK_COL_FREE           5522
#define IDS_DISK_COL_TYPE           5523
#define IDS_DISK_UNALLOCATED        5524
#define IDS_DISK_PARTITION          5525
#define IDS_DISK_PARTITION_LABEL    5526
#define IDS_DISK_SUFFIX_NEW         5527
#define IDS_DISK_SUFFIX_FORMAT      5528
#define IDS_DISKINFO                5529
#define IDS_TYPE_SYSTEM             5530
#define IDS_TYPE_MSR                5531
#define IDS_TYPE_PRIMARY            5532
#define IDS_TYPE_RECOVERY           5533
#define IDS_TYPE_OEM                5534
#define IDS_TYPE_LOGICAL            5535
#define IDS_TYPE_EXTENDED           5536

#define IDS_DISK_SELECT             5540
#define IDS_DISK_NO_REGION          5541
#define IDS_DISK_NOT_BASIC          5542
#define IDS_DISK_NOT_FIRMWARE       5543
#define IDS_DISK_NEW_SYSTEM         5544
#define IDS_DISK_FORMAT_WARN        5545
#define IDS_DISK_FORMAT_NTFS        5546
#define IDS_DISK_FORMAT_EXISTING    5547
#define IDS_DISK_ERASE_WARN         5548
#define IDS_NEWPART_RANGE           5549

#define IDS_READY_TARGET            5560
#define IDS_READY_ERASE             5561
#define IDS_READY_DELETE            5562
#define IDS_READY_CREATE            5563
#define IDS_READY_FORMAT            5564
#define IDS_READY_NOCHANGES         5565
#define IDS_READY_COMPUTER          5566
#define IDS_READY_ACCOUNT           5567
#define IDS_READY_TIMEZONE          5568
#define IDS_READY_LANGUAGE          5569
#define IDS_READY_KEYBOARD          5570
#define IDS_LANGUAGETITLE           5571
#define IDS_LANGUAGESUBTITLE        5572
#define IDS_INSTALL_FAILED          5584
#define IDS_INSTALL_STEP_FAILED     5585
#define IDS_FORMAT_FAILED           5586
#define IDS_CHECK_FAILED            5587
#define IDS_SETUPLIB_MISSING        5588
#define IDS_OOBE_REGIONTITLE        5600
#define IDS_OOBE_REGIONSUBTITLE     5601
#define IDS_APPSTITLE               5602
#define IDS_APPSSUBTITLE            5603
#define IDS_APPS_COL_NAME           5604
#define IDS_APPS_COL_SOURCE         5605
#define IDS_OOBE_FINISHTITLE        5606
#define IDS_OOBE_FINISHSUBTITLE     5607
#define IDS_OOBE_APPLY_REGION       5608
#define IDS_OOBE_APPLY_TIMEZONE     5609
#define IDS_OOBE_APPLY_NAME         5610
#define IDS_OOBE_APPLY_ACCOUNT      5611
#define IDS_OOBE_DOWNLOADING        5612
#define IDS_OOBE_INSTALLING         5613
#define IDS_OOBE_APP_FAILED         5614
#define IDS_OOBE_APPS_FAILED        5615
#define IDS_OOBE_ACCOUNT_FAILED     5616
