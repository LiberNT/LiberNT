/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Kernel-Mode Test Suite user-mode test list
 */

#include <kmt_test.h>

KMT_TESTFUNC Test_CcCopyRead;
KMT_TESTFUNC Test_CcCopyWrite;
KMT_TESTFUNC Test_CcMapData;
KMT_TESTFUNC Test_CcPinMappedData;
KMT_TESTFUNC Test_CcPinRead;
KMT_TESTFUNC Test_CcSetFileSizes;
KMT_TESTFUNC Test_Example;
KMT_TESTFUNC Test_FileAttributes;
KMT_TESTFUNC Test_FindFile;
KMT_TESTFUNC Test_PipeClientComputerName;
KMT_TESTFUNC Test_SecureMemoryCache;
KMT_TESTFUNC Test_NtFilterToken;
KMT_TESTFUNC Test_NtFilterTokenCapture;
KMT_TESTFUNC Test_SeLowBoxToken;
KMT_TESTFUNC Test_SeAuditAlarm;
KMT_TESTFUNC Test_SeSecurityDirectory;
KMT_TESTFUNC Test_SeSaclParts;
KMT_TESTFUNC Test_FltMgrIo;
KMT_TESTFUNC Test_FltMgrScan;
KMT_TESTFUNC Test_FltMgrTx;
KMT_TESTFUNC Test_CmFlushReaders;
KMT_TESTFUNC Test_NtfsTxfRecovery;
KMT_TESTFUNC Test_FltMgrLoad;
KMT_TESTFUNC Test_FltMgrReg;
KMT_TESTFUNC Test_HidPDescription;
KMT_TESTFUNC Test_IoAddDeviceUnload;
KMT_TESTFUNC Test_IoFilterRequirements;
KMT_TESTFUNC Test_IoCreateFile;
KMT_TESTFUNC Test_IoDeviceObject;
KMT_TESTFUNC Test_IoHandleAccess;
KMT_TESTFUNC Test_IoCustomTargetEvent;
KMT_TESTFUNC Test_IoDeviceTree;
KMT_TESTFUNC Test_IoDriverKey;
KMT_TESTFUNC Test_IoErrorLogRecord;
KMT_TESTFUNC Test_IoInterfaceOrder;
KMT_TESTFUNC Test_IoMessageRestart;
KMT_TESTFUNC Test_IoRemovalPolicy;
KMT_TESTFUNC Test_IoAffinityPolicy;
KMT_TESTFUNC Test_IoRemoveChildren;
KMT_TESTFUNC Test_IoReadWrite;
KMT_TESTFUNC Test_ExCpuSetInformation;
KMT_TESTFUNC Test_ExHandleInformation;
KMT_TESTFUNC Test_ExHandleListChurn;
KMT_TESTFUNC Test_PsThreadCpuSets;
KMT_TESTFUNC Test_KdBreakRequest;
KMT_TESTFUNC Test_KdDebugService;
KMT_TESTFUNC Test_KdLiveDump;
KMT_TESTFUNC Test_KdPullRemoteFile;
KMT_TESTFUNC Test_KeContextSegments;
KMT_TESTFUNC Test_KeNonCanonicalReturn;
KMT_TESTFUNC Test_KeSystemCallInt2E;
KMT_TESTFUNC Test_IoRemoveChild;
KMT_TESTFUNC Test_MmMapLockedPagesSpecifyCache;
KMT_TESTFUNC Test_NtCreateSection;
KMT_TESTFUNC Test_NtSystemDebugControl;
KMT_TESTFUNC Test_NtUserPhysicalPages;
KMT_TESTFUNC Test_NtProcessClone;
KMT_TESTFUNC Test_PoDisplayState;
KMT_TESTFUNC Test_PoHibernate;
KMT_TESTFUNC Test_PoSettingSession;
KMT_TESTFUNC Test_PoSleepInformation;
KMT_TESTFUNC Test_PoExecutionState;
KMT_TESTFUNC Test_PoShutdownPersist;
KMT_TESTFUNC Test_PoShutdownTime;
KMT_TESTFUNC Test_PoInitiatePowerAction;
KMT_TESTFUNC Test_BatteryClassNotify;
KMT_TESTFUNC Test_XpdmVersion;
KMT_TESTFUNC Test_PoIrp;
KMT_TESTFUNC Test_PoBatteryState;
KMT_TESTFUNC Test_PoFxState;
KMT_TESTFUNC Test_NdisSgDma;
KMT_TESTFUNC Test_IoDeviceNumaNode;
KMT_TESTFUNC Test_ExWddmPoFx;
KMT_TESTFUNC Test_RtlAvlTree;
KMT_TESTFUNC Test_RtlCaptureContext;
KMT_TESTFUNC Test_RtlException;
KMT_TESTFUNC Test_RtlGetVersion;
KMT_TESTFUNC Test_RtlIntSafe;
KMT_TESTFUNC Test_RtlMemory;
KMT_TESTFUNC Test_RtlRegistry;
KMT_TESTFUNC Test_RtlSehUnwind;
KMT_TESTFUNC Test_RtlSplayTree;
KMT_TESTFUNC Test_RtlStack;
KMT_TESTFUNC Test_RtlStrSafe;
KMT_TESTFUNC Test_RtlUnicodeString;
KMT_TESTFUNC Test_TcpIpIoctl;
KMT_TESTFUNC Test_TcpIpTdi;
KMT_TESTFUNC Test_TcpIpConnect;
KMT_TESTFUNC Test_TcpIpCancelReceive;
KMT_TESTFUNC Test_WfpListen;
KMT_TESTFUNC Test_WskName;
KMT_TESTFUNC Test_WfpTcp;
KMT_TESTFUNC Test_WfpUdp;
KMT_TESTFUNC Test_WfpUser;

/* tests with a leading '-' will not be listed */
const KMT_TEST TestList[] =
{
    { "CcCopyRead",                    Test_CcCopyRead },
    { "CcCopyWrite",                   Test_CcCopyWrite },
    { "CcMapData",                     Test_CcMapData },
    { "CcPinMappedData",               Test_CcPinMappedData },
    { "CcPinRead",                     Test_CcPinRead },
    { "CcSetFileSizes",                Test_CcSetFileSizes },
    { "-Example",                     Test_Example },
    { "FileAttributes",               Test_FileAttributes },
    { "FindFile",                     Test_FindFile },
    { "PipeClientComputerName",       Test_PipeClientComputerName },
    { "SecureMemoryCache",            Test_SecureMemoryCache },
    { "NtFilterToken",                Test_NtFilterToken },
    { "NtFilterTokenCapture",         Test_NtFilterTokenCapture },
    { "SeLowBoxToken",                Test_SeLowBoxToken },
    { "SeAuditAlarm",                 Test_SeAuditAlarm },
    { "SeSecurityDirectory",          Test_SeSecurityDirectory },
    { "SeSaclParts",                  Test_SeSaclParts },
    { "FltMgrIo",                     Test_FltMgrIo },
    { "FltMgrScan",                   Test_FltMgrScan },
    { "FltMgrTx",                     Test_FltMgrTx },
    { "CmFlushReaders",               Test_CmFlushReaders },
    { "NtfsTxfRecovery",              Test_NtfsTxfRecovery },
    { "-FltMgrLoad",                  Test_FltMgrLoad }, // TODO: WIP/untested/crashes.
    { "-FltMgrReg",                   Test_FltMgrReg }, // TODO: WIP/untested/crashes.
    { "HidPDescription",              Test_HidPDescription },
    { "IoAddDeviceUnload",            Test_IoAddDeviceUnload },
    { "IoFilterRequirements",         Test_IoFilterRequirements },
    { "IoCreateFile",                 Test_IoCreateFile },
    { "IoDeviceObject",               Test_IoDeviceObject },
    { "IoHandleAccess",               Test_IoHandleAccess },
    { "-IoCustomTargetEvent",         Test_IoCustomTargetEvent },
    { "-IoDeviceTree",                Test_IoDeviceTree },
    { "-IoDriverKey",                 Test_IoDriverKey },
    { "IoErrorLogRecord",             Test_IoErrorLogRecord },
    { "-IoInterfaceOrder",            Test_IoInterfaceOrder },
    { "-IoMessageRestart",            Test_IoMessageRestart },
    { "-IoRemovalPolicy",             Test_IoRemovalPolicy },
    { "-IoAffinityPolicy",            Test_IoAffinityPolicy },
    { "-IoRemoveChildren",            Test_IoRemoveChildren },
    { "IoReadWrite",                  Test_IoReadWrite },
    { "ExCpuSetInformation",          Test_ExCpuSetInformation },
    { "ExHandleInformation",          Test_ExHandleInformation },
    { "ExHandleListChurn",            Test_ExHandleListChurn },
    { "PsThreadCpuSets",              Test_PsThreadCpuSets },
    { "-KdBreakRequest",              Test_KdBreakRequest },
    { "KdDebugService",               Test_KdDebugService },
    { "KdLiveDump",                   Test_KdLiveDump },
    { "KdPullRemoteFile",             Test_KdPullRemoteFile },
    { "IoRemoveChild",                Test_IoRemoveChild },
    { "MmMapLockedPagesSpecifyCache", Test_MmMapLockedPagesSpecifyCache },
    { "NtCreateSection",              Test_NtCreateSection },
    { "NdisSgDma",                    Test_NdisSgDma },
    { "NtSystemDebugControl",         Test_NtSystemDebugControl },
    { "NtUserPhysicalPages",          Test_NtUserPhysicalPages },
    { "NtProcessClone",               Test_NtProcessClone },
    { "PoDisplayState",               Test_PoDisplayState },
    { "PoHibernate",                  Test_PoHibernate },
    { "PoSettingSession",             Test_PoSettingSession },
    { "PoSleepInformation",           Test_PoSleepInformation },
    { "PoExecutionState",             Test_PoExecutionState },
    { "-PoShutdownPersist",           Test_PoShutdownPersist },
    { "-PoShutdownTime",              Test_PoShutdownTime },
    { "-PoInitiatePowerAction",       Test_PoInitiatePowerAction },
    { "ExWddmPoFx",                    Test_ExWddmPoFx },
    { "PoBatteryState",                Test_PoBatteryState },
    { "PoFxState",                     Test_PoFxState },
    { "IoDeviceNumaNode",              Test_IoDeviceNumaNode },
    { "BatteryClassNotify",           Test_BatteryClassNotify },
    { "XpdmVersion",                  Test_XpdmVersion },
    { "PoIrp",                        Test_PoIrp },
    { "RtlAvlTree",                   Test_RtlAvlTree },
    { "RtlException",                 Test_RtlException },
    { "RtlGetVersion",                Test_RtlGetVersion },
    { "RtlIntSafe",                   Test_RtlIntSafe },
    { "RtlMemory",                    Test_RtlMemory },
    { "RtlRegistry",                  Test_RtlRegistry },
    { "RtlSehUnwind",                 Test_RtlSehUnwind },
    { "RtlSplayTree",                 Test_RtlSplayTree },
    { "RtlStack",                     Test_RtlStack },
    { "RtlStrSafe",                   Test_RtlStrSafe },
    { "RtlUnicodeString",             Test_RtlUnicodeString },
    { "TcpIpTdi",                     Test_TcpIpTdi },
    { "TcpIpConnect",                 Test_TcpIpConnect },
    { "TcpIpCancelReceive",           Test_TcpIpCancelReceive },
    { "WfpListen",                    Test_WfpListen },
    { "WskName",                      Test_WskName },
    { "WfpTcp",                       Test_WfpTcp },
    { "WfpUdp",                       Test_WfpUdp },
    { "WfpUser",                      Test_WfpUser },
#ifdef _M_AMD64
    { "KeContextSegments",            Test_KeContextSegments },
    { "KeNonCanonicalReturn",         Test_KeNonCanonicalReturn },
    { "KeSystemCallInt2E",            Test_KeSystemCallInt2E },
    { "RtlCaptureContextUM",          Test_RtlCaptureContext },
#endif
    { NULL,                           NULL },
};
