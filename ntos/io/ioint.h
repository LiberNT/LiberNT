/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Declarations shared inside the I/O manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _IOINT_H
#define _IOINT_H

#define IOP_FIXED_STACK_SIZE 8

#define IOP_FILE_OBJECT_EXT_PRIORITY_HINT 0x0001
#define IOP_FILE_OBJECT_EXT_TOP_HINT      0x0002
#define IOP_FILE_OBJECT_EXT_FILTER        0x0004

#define IOP_DEVICE_LIST_MASK 0x00000003

#define IOP_IRP_QUOTA_CHARGED  0x01
#define IOP_IRP_LOOKASIDE_FREE 0x02

#define IOP_MINI_PACKET_SIGNATURE 0x55

typedef struct _IOP_RESERVE_IRP
{
    PIRP Irp;
    volatile LONG InUse;
    KEVENT Available;
    CCHAR StackSize;
} IOP_RESERVE_IRP, *PIOP_RESERVE_IRP;

typedef struct _IOP_FS_REGISTRATION_OPS
{
    LIST_ENTRY ListEntry;
    PDRIVER_OBJECT DriverObject;
    PDRIVER_FS_NOTIFICATION NotificationRoutine;
    BOOLEAN SynchronizeWithMounts;
} IOP_FS_REGISTRATION_OPS, *PIOP_FS_REGISTRATION_OPS;

typedef struct _IOP_DEVICE_ACTION_REQUEST
{
    LIST_ENTRY ListEntry;
    PDEVICE_OBJECT DeviceObject;
    DEVICE_ACTION Action;
    PKEVENT CompletionEvent;
    NTSTATUS *CompletionStatus;
} IOP_DEVICE_ACTION_REQUEST, *PIOP_DEVICE_ACTION_REQUEST;

typedef struct _IOP_PNP_NOTIFY_ENTRY
{
    LIST_ENTRY ListEntry;
    IO_NOTIFICATION_EVENT_CATEGORY EventCategory;
    ULONG EventCategoryFlags;
    GUID ClassGuid;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PDRIVER_OBJECT DriverObject;
    PDRIVER_NOTIFICATION_CALLBACK_ROUTINE CallbackRoutine;
    PVOID Context;
    volatile LONG ReferenceCount;
    BOOLEAN Unregistered;
} IOP_PNP_NOTIFY_ENTRY, *PIOP_PNP_NOTIFY_ENTRY;

typedef struct _IOP_PLUGPLAY_EVENT_BLOCK
{
    LIST_ENTRY ListEntry;
    ULONG Size;
    PLUGPLAY_EVENT_BLOCK Event;
} IOP_PLUGPLAY_EVENT_BLOCK, *PIOP_PLUGPLAY_EVENT_BLOCK;

typedef struct _IOP_DEVICE_INTERFACE
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING SymbolicLinkName;
    GUID ClassGuid;
    PDEVICE_OBJECT PhysicalDeviceObject;
    BOOLEAN Enabled;
} IOP_DEVICE_INTERFACE, *PIOP_DEVICE_INTERFACE;

typedef struct _IOP_DEVICE_REFERENCE
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING InstancePath;
    PDEVICE_OBJECT DeviceObject;
} IOP_DEVICE_REFERENCE, *PIOP_DEVICE_REFERENCE;

typedef struct _IOP_GROUP_TABLE
{
    PUNICODE_STRING Groups;
    ULONG Count;
} IOP_GROUP_TABLE, *PIOP_GROUP_TABLE;

/* ioinit.c */

VOID IopInitLookasideLists(VOID);
BOOLEAN IopCreateObjectTypes(VOID);
BOOLEAN IopCreateRootDirectories(VOID);
NTSTATUS IopCreateSecurityDescriptorPerType(_In_ SECURITY_DESCRIPTOR_TYPE Type,
                                            _Out_ PSECURITY_DESCRIPTOR *Descriptor);
NTSTATUS IopCreateDefaultDeviceSecurityDescriptor(_In_ DEVICE_TYPE DeviceType,
                                                  _In_ ULONG DeviceCharacteristics,
                                                  _In_ BOOLEAN HasName,
                                                  _Out_writes_bytes_(SIZE_OF_SUBSET_SD)
                                                      PVOID Buffer,
                                                  _Out_opt_ PBOOLEAN DefaultSdPresent,
                                                  _Out_ PSECURITY_DESCRIPTOR *Descriptor);
VOID IopInitErrorLog(VOID);

#define SIZE_OF_SUBSET_SD (sizeof(SECURITY_DESCRIPTOR) + 0x200)

extern PLOADER_PARAMETER_BLOCK IopLoaderBlock;
extern ERESOURCE IopDatabaseResource;
extern ERESOURCE IopSecurityResource;
extern ERESOURCE IopDriverLoadResource;
extern ULONG IopDeviceObjectNumber;
extern PWSTR IopHardwareDatabaseKey;
typedef struct _FILE_IO_COMPLETION_INFORMATION
{
    PVOID KeyContext;
    PVOID ApcContext;
    IO_STATUS_BLOCK IoStatusBlock;
} FILE_IO_COMPLETION_INFORMATION, *PFILE_IO_COMPLETION_INFORMATION;

extern ULONG IoDeviceHandlerObjectSize;
extern POBJECT_TYPE IoControllerObjectType;
extern POBJECT_TYPE IoDeviceHandlerObjectType;
extern BOOLEAN PnPBootDriversLoaded;
extern BOOLEAN PnPBootDriversInitialized;
extern ANSI_STRING IoArcBootDeviceName;
extern ANSI_STRING IoArcHalDeviceName;
extern PCHAR IoLoaderArcBootDeviceName;
extern ULONG IoPageReadIrpAllocationFailure;
extern ULONG IoPageReadNonPagefileIrpAllocationFailure;
extern PWSTR ArcTypes[];
extern PWSTR IoDeviceInfoNames[];

/* devobj.c */

VOID IopEditDeviceList(_In_ PDRIVER_OBJECT DriverObject,
                       _In_ PDEVICE_OBJECT DeviceObject,
                       _In_ IOP_DEVICE_LIST_OPERATION Operation);
VOID IopDecrementDeviceObjectRef(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ BOOLEAN UnloadIfUnused);
VOID IopDecrementDeviceObjectHandleCount(_In_ PDEVICE_OBJECT DeviceObject);
PDEVICE_OBJECT IopGetLowestDevice(_In_ PDEVICE_OBJECT DeviceObject);
PDEVICE_OBJECT IopGetDevicePDO(_In_ PDEVICE_OBJECT DeviceObject);
NTSTATUS IopGetDeviceObjectPointer(_In_ PUNICODE_STRING ObjectName,
                                   _In_ ACCESS_MASK DesiredAccess,
                                   _Out_ PFILE_OBJECT *FileObject,
                                   _Out_ PDEVICE_OBJECT *DeviceObject,
                                   _In_ ULONG AttachFlag);
BOOLEAN IopCheckDeviceAndDriver(_In_ POPEN_PACKET OpenPacket,
                                _In_ PDEVICE_OBJECT DeviceObject);
NTSTATUS IopSetDeviceSecurityDescriptor(_In_ PDEVICE_OBJECT DeviceObject,
                                        _In_ PSECURITY_INFORMATION SecurityInformation,
                                        _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
                                        _In_ PULONG BufferLength,
                                        _In_ POOL_TYPE PoolType,
                                        _In_ PGENERIC_MAPPING GenericMapping);

/* devqueue.c */

VOID IopStartNextPacket(_In_ PDEVICE_OBJECT DeviceObject, _In_ BOOLEAN Cancelable);
VOID IopStartNextPacketByKey(_In_ PDEVICE_OBJECT DeviceObject,
                             _In_ BOOLEAN Cancelable,
                             _In_ ULONG Key);
VOID IopStartNextPacketByKeyEx(_In_ PDEVICE_OBJECT DeviceObject,
                               _In_ ULONG Key,
                               _In_ ULONG Flags);

/* iopacket.c */

extern GENERAL_LOOKASIDE IoSmallIrpLookaside;
extern GENERAL_LOOKASIDE IoLargeIrpLookaside;
extern GENERAL_LOOKASIDE IoCompletionPacketLookaside;

VOID IopDisassociateThreadIrp(VOID);
VOID NTAPI IopFreeIrpKernelApc(_In_ PKAPC Apc);
VOID NTAPI IopAbortIrpKernelApc(_In_ PKAPC Apc);
VOID IopFreeReserveIrp(_In_ CCHAR PriorityBoost);
PIOP_IRP_EXTENSION IopGetIrpExtension(_In_ PIRP Irp, _In_ BOOLEAN Allocate);
VOID NTAPI IopCleanupIrp(_In_ PIRP Irp, _In_opt_ PFILE_OBJECT FileObject);

/* iocall.c */

NTSTATUS IopUnloadSafeCompletion(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ PIRP Irp,
                                 _In_ PVOID Context);
NTSTATUS IopSynchronousCompletion(_In_ PDEVICE_OBJECT DeviceObject,
                                  _In_ PIRP Irp,
                                  _In_ PVOID Context);

/* iocomplete.c */

VOID IopDeadIrp(VOID);
VOID IopCleanupAfterException(_In_ PFILE_OBJECT FileObject,
                              _In_ PIRP Irp,
                              _In_opt_ PKEVENT Event,
                              _In_opt_ PKEVENT LocalEvent);
VOID IopReportIncompleteSynchronousIrp(_In_ PIRP Irp);
NTSTATUS IopFinalizeAsynchronousIo(_In_ NTSTATUS SynchStatus,
                                   _In_ PKEVENT Event,
                                   _In_ PIRP Irp,
                                   _In_ KPROCESSOR_MODE PreviousMode,
                                   _In_ PIO_STATUS_BLOCK KernelIosb,
                                   _Out_ PIO_STATUS_BLOCK UserIosb);
NTSTATUS IopPerformSynchronousRequest(_In_ PDEVICE_OBJECT DeviceObject,
                                      _In_ PIRP Irp,
                                      _In_ PFILE_OBJECT FileObject,
                                      _In_ BOOLEAN Deferred,
                                      _In_ KPROCESSOR_MODE PreviousMode,
                                      _In_ BOOLEAN SynchIo,
                                      _In_ IOP_TRANSFER_TYPE TransferType);
VOID IopFreeMiniPacket(_In_ PIOP_MINI_COMPLETION_PACKET Packet);

/* iocreate.c */

NTSTATUS IopCreateFile(_Out_ PHANDLE FileHandle,
                       _In_ ACCESS_MASK DesiredAccess,
                       _In_ POBJECT_ATTRIBUTES ObjectAttributes,
                       _Out_ PIO_STATUS_BLOCK IoStatusBlock,
                       _In_opt_ PLARGE_INTEGER AllocationSize,
                       _In_ ULONG FileAttributes,
                       _In_ ULONG ShareAccess,
                       _In_ ULONG Disposition,
                       _In_ ULONG CreateOptions,
                       _In_opt_ PVOID EaBuffer,
                       _In_ ULONG EaLength,
                       _In_ CREATE_FILE_TYPE CreateFileType,
                       _In_opt_ PVOID ExtraCreateParameters,
                       _In_ ULONG Options,
                       _In_ ULONG InternalFlags,
                       _In_opt_ PIO_DRIVER_CREATE_CONTEXT DriverContext);
BOOLEAN IopCheckBackupRestorePrivilege(_In_ PACCESS_STATE AccessState,
                                       _Inout_ PULONG CreateOptions,
                                       _In_ KPROCESSOR_MODE PreviousMode,
                                       _In_ ULONG Disposition);
NTSTATUS IopOpenLinkOrRenameTarget(_Out_ PHANDLE Handle,
                                   _In_ PIRP Irp,
                                   _In_ PFILE_RENAME_INFORMATION RenameInfo,
                                   _In_ PFILE_OBJECT FileObject);

/* ioparse.c */

NTSTATUS IopCheckTopDeviceHint(_In_ PEXTENDED_DEVOBJ_EXTENSION DeviceExtension,
                               _Inout_ PDEVICE_OBJECT *DeviceObject,
                               _In_ POPEN_PACKET OpenPacket);

/* iofileinfo.c */

extern UCHAR IopQueryOperationLength[];
extern UCHAR IopSetOperationLength[];
extern ACCESS_MASK IopQueryOperationAccess[];
extern ACCESS_MASK IopSetOperationAccess[];
extern UCHAR IopQueryFsOperationLength[];
extern UCHAR IopSetFsOperationLength[];
extern ULONG IopQueryFsOperationAccess[];
extern ULONG IopSetFsOperationAccess[];

NTSTATUS IopQueryDeviceInformation(_In_ PFILE_OBJECT FileObject,
                                   _In_ ULONG InformationClass,
                                   _In_ ULONG Length,
                                   _Out_opt_ PVOID Information,
                                   _Out_opt_ PULONG ReturnedLength,
                                   _In_ BOOLEAN File);
NTSTATUS IopGetBasicInformationFile(_In_ PFILE_OBJECT FileObject,
                                    _Out_ PFILE_BASIC_INFORMATION BasicInfo);
NTSTATUS IopQueryAttributesFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes,
                                _In_ FILE_INFORMATION_CLASS FileInformationClass,
                                _In_ ULONG FileInformationSize,
                                _Out_ PVOID FileInformation,
                                _In_ KPROCESSOR_MODE AccessMode);

/* iordwr.c */

NTSTATUS IopDeviceFsIoControl(_In_ HANDLE DeviceHandle,
                              _In_opt_ HANDLE Event,
                              _In_opt_ PIO_APC_ROUTINE UserApcRoutine,
                              _In_opt_ PVOID UserApcContext,
                              _Out_ PIO_STATUS_BLOCK IoStatusBlock,
                              _In_ ULONG IoControlCode,
                              _In_opt_ PVOID InputBuffer,
                              _In_ ULONG InputBufferLength,
                              _Out_opt_ PVOID OutputBuffer,
                              _In_ ULONG OutputBufferLength,
                              _In_ BOOLEAN IsDevIoCtl);

/* ioerrlog.c */

extern PVOID IopErrorLogObject;
extern KSPIN_LOCK IopLogListLock;
extern HANDLE IopLogPort;
extern BOOLEAN IopLogPortConnected;
extern BOOLEAN IopLogWorkerRunning;
extern ULONG IopTotalLogSize;
extern WORK_QUEUE_ITEM IopErrorLogWorkItem;
extern KDPC IopLogDpc;

PERROR_LOG_ENTRY IopGetErrorLogEntry(VOID);
VOID NTAPI IopLogDpcRoutine(_In_ PKDPC Dpc,
                            _In_opt_ PVOID DeferredContext,
                            _In_opt_ PVOID SystemArgument1,
                            _In_opt_ PVOID SystemArgument2);
VOID IopRestartLogWorker(VOID);
NTSTATUS IopConnectLogPort(VOID);

/* devtimer.c */

extern KSPIN_LOCK IopTimerLock;
extern LIST_ENTRY IopTimerQueueHead;
extern KDPC IopTimerDpc;
extern KTIMER IopTimer;
extern ULONG IopTimerCount;

VOID NTAPI IopTimerDispatch(_In_ PKDPC Dpc,
                            _In_opt_ PVOID DeferredContext,
                            _In_opt_ PVOID SystemArgument1,
                            _In_opt_ PVOID SystemArgument2);

/* workitem.c */

VOID NTAPI IopWorkItemCallback(_In_ PVOID Parameter);

/* ioevents.c */

NTSTATUS IopCreateEvent(_In_ PUNICODE_STRING EventName,
                        _Out_ PHANDLE EventHandle,
                        _In_ BOOLEAN ManualReset);
VOID IopSetEvent(_In_ PVOID EventPointer);

/* iointr.c */

NTSTATUS IopConnectInterruptExFullySpecific(_Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);
NTSTATUS IopConnectInterruptExLineBased(_Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);
NTSTATUS IopConnectInterruptExMessageBased(_Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);

/* iovolume.c */

extern LIST_ENTRY IopDiskFileSystemQueueHead;
extern LIST_ENTRY IopCdRomFileSystemQueueHead;
extern LIST_ENTRY IopTapeFileSystemQueueHead;
extern LIST_ENTRY IopNetworkFileSystemQueueHead;
extern LIST_ENTRY IopFsNotifyChangeQueueHead;
extern LIST_ENTRY IopFsRegistrationOps;
extern KSPIN_LOCK IopVpbSpinLock;

VOID IopNotifyAlreadyRegisteredFileSystems(_In_ PLIST_ENTRY ListHead,
                                           _In_ PDRIVER_FS_NOTIFICATION Routine,
                                           _In_ BOOLEAN SkipRaw);
NTSTATUS IopMountInitializeVpb(_In_ PDEVICE_OBJECT DeviceObject,
                               _In_ PDEVICE_OBJECT AttachedDevice,
                               _In_ BOOLEAN Raw);
BOOLEAN IopReferenceVerifyVpb(_In_ PDEVICE_OBJECT DeviceObject,
                              _Out_ PDEVICE_OBJECT *FileSystemDeviceObject,
                              _Out_ PVPB *Vpb);
NTSTATUS IopLoadFileSystemDriver(_In_ PDEVICE_OBJECT DeviceObject);

/* iorawfs.c */

extern PDEVICE_OBJECT RawDiskDeviceObject;
extern PDEVICE_OBJECT RawCdromDeviceObject;
extern PDEVICE_OBJECT RawTapeDeviceObject;

/* drvobj.c */

NTSTATUS IopDoLoadUnloadDriver(_In_opt_ PUNICODE_STRING DriverServiceName,
                               _Inout_ PDRIVER_OBJECT *DriverObject);
VOID IopUnloadDriver(_In_ PUNICODE_STRING DriverServiceName, _In_ BOOLEAN UnloadPnpDrivers);
NTSTATUS IopTryUnloadDriver(_In_ PDRIVER_OBJECT DriverObject, _In_ BOOLEAN Force);
NTSTATUS IopNormalizeImagePath(_Inout_ PUNICODE_STRING ImagePath,
                               _In_ PUNICODE_STRING ServiceName);
BOOLEAN IopCheckSafeBootDriver(_In_ HANDLE ServiceHandle, _In_ PUNICODE_STRING ServiceName);
NTSTATUS IopConfigureDeviceFromDriverDatabase(_In_ PDEVICE_NODE DeviceNode,
                                              _In_ HANDLE InstanceKey);

extern LIST_ENTRY DriverReinitListHead;
extern KSPIN_LOCK DriverReinitListLock;
extern PLIST_ENTRY DriverReinitTailEntry;
extern LIST_ENTRY DriverBootReinitListHead;
extern KSPIN_LOCK DriverBootReinitListLock;
extern PLIST_ENTRY DriverBootReinitTailEntry;

/* drvinit.c */

extern IOP_GROUP_TABLE IopGroupTable;
extern PULONG IopGroupIndex;
extern PUNICODE_STRING PiInitGroupOrderTable;
extern ULONG PiInitGroupOrderTableCount;

NTSTATUS LdrProcessDriverModule(_In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
                                _In_ PUNICODE_STRING FileName,
                                _Out_ PLDR_DATA_TABLE_ENTRY *ModuleObject);
NTSTATUS IopLoadServiceModule(_In_ HANDLE ServiceHandle,
                              _Out_ PDRIVER_OBJECT *DriverObject);

extern ULONG InitSafeBootMode;

/* ioshutdown.c */

extern LIST_ENTRY ShutdownListHead;
extern LIST_ENTRY LastChanceShutdownListHead;
extern KSPIN_LOCK ShutdownListLock;

VOID IoShutdownPnpDevices(VOID);

/* iodump.c */

extern ULONG IopNumTriageDumpDataBlocks;

/* iorsrc.c */

extern CONFIGURATION_INFORMATION IopConfigurationInformation;

NTSTATUS IopUpdateResourceMap(_In_ PDEVICE_NODE DeviceNode,
                              _In_ PWSTR Level1Key,
                              _In_ PWSTR Level2Key,
                              _In_opt_ PCM_RESOURCE_LIST ResourceList);
NTSTATUS IopUpdateResourceMapForPnPDevice(_In_ PDEVICE_NODE DeviceNode);
INTERFACE_TYPE IopDetermineDefaultInterfaceType(VOID);
PCWSTR IopGetInterfaceTypeString(_In_ INTERFACE_TYPE InterfaceType);

extern INTERFACE_TYPE PnpDefaultInterfaceType;

/* pnpnode.c */

extern ULONG IopNumberDeviceNodes;
extern LIST_ENTRY PpDeviceReferenceTable;
extern KGUARDED_MUTEX PpDeviceReferenceTableLock;
extern ERESOURCE PpRegistryDeviceResource;

VOID PpInitializeDeviceReferenceTable(VOID);
PIOP_DEVICE_REFERENCE PiAllocateGenericTableEntry(_In_ PUNICODE_STRING InstancePath);
VOID PiFreeGenericTableEntry(_In_ PIOP_DEVICE_REFERENCE Entry);
LONG PiCompareInstancePath(_In_ PCUNICODE_STRING First, _In_ PCUNICODE_STRING Second);
PCSTR PipGetDeviceNodeStateName(_In_ PNP_DEVNODE_STATE State);
PDEVICE_OBJECT IopGetDeviceObjectFromDeviceInstance(_In_ PUNICODE_STRING DeviceInstance);
NTSTATUS IopFindDeviceInstanceTraverse(_In_ PDEVICE_NODE DeviceNode, _In_ PVOID Context);
NTSTATUS PnpDeviceObjectToDeviceInstance(_In_ PDEVICE_OBJECT DeviceObject,
                                         _Out_ PHANDLE InstanceKey,
                                         _In_ ACCESS_MASK DesiredAccess);

/* pnpstate.c */

extern BOOLEAN IopDeviceActionInProgress;
extern WORK_QUEUE_ITEM IopDeviceActionWorkItem;

VOID PiEnumerationFinished(_In_opt_ PDEVICE_NODE DeviceNode);

/* pnpquery.c */

NTSTATUS NTAPI IopInvalidDeviceRequest(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);

/* pnpregutil.c */

NTSTATUS IopGetParentIdPrefix(_In_ PDEVICE_NODE DeviceNode, _Out_ PUNICODE_STRING Prefix);
NTSTATUS IopSetDeviceInstanceData(_In_ HANDLE InstanceKey, _In_ PDEVICE_NODE DeviceNode);
NTSTATUS PiGetDeviceRegistryProperty(_In_ PDEVICE_OBJECT DeviceObject,
                                     _In_ ULONG ValueType,
                                     _In_ PCWSTR ValueName,
                                     _In_opt_ PCWSTR KeyName,
                                     _Out_opt_ PVOID Buffer,
                                     _Inout_ PULONG BufferLength);
VOID PnpFreeUnicodeStringList(_In_opt_ PUNICODE_STRING UnicodeStringList,
                              _In_ ULONG StringCount);
NTSTATUS PnpBusTypeGuidGet(_In_ USHORT Index, _Out_ LPGUID BusTypeGuid);
USHORT IopGetBusTypeGuidIndex(_In_ LPGUID BusTypeGuid);

/* devinterface.c */

extern LIST_ENTRY IopDeviceInterfaceListHead;
extern KGUARDED_MUTEX IopDeviceInterfaceLock;

NTSTATUS IopOpenInterfaceClassKey(_In_ CONST GUID *ClassGuid,
                                  _In_ BOOLEAN Create,
                                  _Out_ PHANDLE KeyHandle);

/* devprop.c */

NTSTATUS IopInterfaceLinkToRegistryName(_In_ PUNICODE_STRING SymbolicLinkName,
                                        _Out_ LPGUID ClassGuid,
                                        _Out_ PUNICODE_STRING RegistryName);

/* pnpevent.c */

extern LIST_ENTRY IopPnpEventQueueHead;
extern KGUARDED_MUTEX IopPnpEventQueueLock;
extern KEVENT IopPnpEventQueueSemaphore;

/* pnpnotif.c */

extern LIST_ENTRY PiNotifyDeviceInterfaceListHead;
extern KGUARDED_MUTEX PiNotifyDeviceInterfaceLock;
extern LIST_ENTRY PiNotifyHwProfileListHead;
extern KGUARDED_MUTEX PiNotifyHwProfileLock;
extern KGUARDED_MUTEX PiNotifyTargetDeviceLock;

VOID NTAPI IopReportTargetDeviceChangeAsyncWorker(_In_ PVOID Context);
NTSTATUS PpSetCustomTargetEvent(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PKEVENT SyncEvent,
    _Out_opt_ PNTSTATUS SyncStatus,
    _In_opt_ PDEVICE_CHANGE_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _In_ PTARGET_DEVICE_CUSTOM_NOTIFICATION Notification);

/* pnpdetect.c */

NTSTATUS IopCreateArcName(_In_ PUNICODE_STRING ArcName,
                          _In_ PUNICODE_STRING DeviceName);
VOID IopMarkBootPartition(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

#endif /* _IOINT_H */
