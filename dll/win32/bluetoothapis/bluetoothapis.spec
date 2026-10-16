@ stub BluetoothAddressToString
@ stdcall BluetoothAuthenticateDeviceEx(ptr ptr ptr ptr long)
@ stub BluetoothDisconnectDevice
@ stdcall BluetoothEnableDiscovery(ptr long)
@ stdcall BluetoothEnableIncomingConnections(ptr long)
@ stub BluetoothEnumerateInstalledServices
@ stub BluetoothEnumerateInstalledServicesEx
@ stub BluetoothEnumerateLocalServices
@ stub BluetoothFindBrowseGroupClose
@ stub BluetoothFindClassIdClose
@ stdcall BluetoothFindDeviceClose(ptr)
@ stub BluetoothFindFirstBrowseGroup
@ stub BluetoothFindFirstClassId
@ stdcall BluetoothFindFirstDevice(ptr ptr)
@ stub BluetoothFindFirstProfileDescriptor
@ stub BluetoothFindFirstProtocolDescriptorStack
@ stub BluetoothFindFirstProtocolEntry
@ stdcall BluetoothFindFirstRadio(ptr ptr)
@ stub BluetoothFindFirstService
@ stub BluetoothFindFirstServiceEx
@ stub BluetoothFindNextBrowseGroup
@ stub BluetoothFindNextClassId
@ stdcall BluetoothFindNextDevice(ptr ptr)
@ stub BluetoothFindNextProfileDescriptor
@ stub BluetoothFindNextProtocolDescriptorStack
@ stub BluetoothFindNextProtocolEntry
@ stdcall BluetoothFindNextRadio(ptr ptr)
@ stub BluetoothFindNextService
@ stub BluetoothFindProfileDescriptorClose
@ stub BluetoothFindProtocolDescriptorStackClose
@ stub BluetoothFindProtocolEntryClose
@ stdcall BluetoothFindRadioClose(ptr)
@ stub BluetoothFindServiceClose
@ stdcall BluetoothGATTAbortReliableWrite(ptr int64 long)
@ stdcall BluetoothGATTBeginReliableWrite(ptr ptr long)
@ stdcall BluetoothGATTEndReliableWrite(ptr int64 long)
@ stdcall BluetoothGATTGetCharacteristicValue(ptr ptr long ptr ptr long)
@ stdcall BluetoothGATTGetCharacteristics(ptr ptr long ptr ptr long)
@ stdcall BluetoothGATTGetDescriptorValue(ptr ptr long ptr ptr long)
@ stdcall BluetoothGATTGetDescriptors(ptr ptr long ptr ptr long)
@ stdcall BluetoothGATTGetIncludedServices(ptr ptr long ptr ptr long)
@ stdcall BluetoothGATTGetServices(ptr long ptr ptr long)
@ stdcall BluetoothGATTRegisterEvent(ptr long ptr ptr ptr ptr long)
@ stdcall BluetoothGATTSetCharacteristicValue(ptr ptr ptr int64 long)
@ stdcall BluetoothGATTSetDescriptorValue(ptr ptr ptr long)
@ stdcall BluetoothGATTUnregisterEvent(ptr long)
@ stdcall BluetoothGetDeviceInfo(ptr long)
@ stub BluetoothGetLocalServiceInfo
@ stdcall BluetoothGetRadioInfo(ptr ptr)
@ stub BluetoothGetServicePnpInstance
@ stdcall BluetoothIsConnectable(ptr)
@ stdcall BluetoothIsDiscoverable(ptr)
@ stub BluetoothIsVersionAvailable
@ stub BluetoothRegisterForAuthentication
@ stdcall BluetoothRegisterForAuthenticationEx(ptr ptr ptr ptr)
@ stdcall BluetoothRemoveDevice(ptr)
@ stdcall BluetoothSdpEnumAttributes(ptr long ptr ptr)
@ stdcall BluetoothSdpGetAttributeValue(ptr long long ptr)
@ stdcall BluetoothSdpGetContainerElementData(ptr long ptr ptr)
@ stdcall BluetoothSdpGetElementData(ptr long ptr)
@ stub BluetoothSdpGetString
@ stub BluetoothSendAuthenticationResponse
@ stdcall BluetoothSendAuthenticationResponseEx(ptr ptr)
@ stdcall BluetoothSetLocalServiceInfo(ptr ptr long ptr)
@ stub BluetoothSetServiceState
@ stub BluetoothSetServiceStateEx
@ stdcall BluetoothUnregisterAuthentication(long)
@ stub BluetoothUpdateDeviceRecord
@ stub BthpCheckForUnsupportedGuid
@ stub BthpCleanupBRDeviceNode
@ stub BthpCleanupDeviceLocalServices
@ stub BthpCleanupDeviceRemoteServices
@ stub BthpCleanupLEDeviceNodes
@ stub BthpEnableAllServices
@ stub BthpEnableConnectableAndDiscoverable
@ stub BthpEnableRadioSoftware
@ stub BthpFindPnpInfo
@ stub BthpGATTCloseSession
@ stub BthpInnerRecord
@ stub BthpIsBluetoothServiceRunning
@ stub BthpIsConnectableByDefault
@ stub BthpIsDiscoverable
@ stub BthpIsDiscoverableByDefault
@ stub BthpIsRadioSoftwareEnabled
@ stub BthpIsTopOfServiceGroup
@ stub BthpMapStatusToErr
@ stub BthpNextRecord
@ stub BthpRegisterForAuthentication
@ stub BthpSetServiceState
@ stub BthpSetServiceStateEx
@ stub BthpTranspose16Bits
@ stub BthpTranspose32Bits
@ stub BthpTransposeAndExtendBytes
@ stdcall -private DllCanUnloadNow()
@ stub FindNextOpenVCOMPort
@ stub InstallIncomingComPort
@ stub ShouldForceAuthentication
