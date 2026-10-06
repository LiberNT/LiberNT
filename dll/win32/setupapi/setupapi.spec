@ stdcall CMP_GetBlockedDriverInfo(wstr ptr long ptr) cfgmgr32.CMP_GetBlockedDriverInfo
@ stdcall CMP_GetServerSideDeviceInstallFlags(ptr long ptr) cfgmgr32.CMP_GetServerSideDeviceInstallFlags
@ stdcall CMP_Init_Detection(long) cfgmgr32.CMP_Init_Detection
@ stdcall CMP_RegisterNotification(ptr ptr long ptr) cfgmgr32.CMP_RegisterNotification
@ stdcall CMP_Report_LogOn(long long) cfgmgr32.CMP_Report_LogOn
@ stdcall CMP_UnregisterNotification(ptr) cfgmgr32.CMP_UnregisterNotification
@ stdcall CMP_WaitNoPendingInstallEvents(long) cfgmgr32.CMP_WaitNoPendingInstallEvents
@ stdcall CMP_WaitServicesAvailable(ptr) cfgmgr32.CMP_WaitServicesAvailable
@ stdcall CM_Add_Empty_Log_Conf(ptr ptr long long) cfgmgr32.CM_Add_Empty_Log_Conf
@ stdcall CM_Add_Empty_Log_Conf_Ex(ptr ptr long long ptr) cfgmgr32.CM_Add_Empty_Log_Conf_Ex
@ stdcall CM_Add_IDA(ptr str long) cfgmgr32.CM_Add_IDA
@ stdcall CM_Add_IDW(ptr wstr long) cfgmgr32.CM_Add_IDW
@ stdcall CM_Add_ID_ExA(ptr str long ptr) cfgmgr32.CM_Add_ID_ExA
@ stdcall CM_Add_ID_ExW(ptr wstr long ptr) cfgmgr32.CM_Add_ID_ExW
@ stdcall CM_Add_Range(int64 int64 ptr long) cfgmgr32.CM_Add_Range
@ stdcall CM_Add_Res_Des(ptr ptr long ptr long long) cfgmgr32.CM_Add_Res_Des
@ stdcall CM_Add_Res_Des_Ex(ptr ptr long ptr long long ptr) cfgmgr32.CM_Add_Res_Des_Ex
@ stub -version=0x600+ CM_Apply_PowerScheme
@ stdcall CM_Connect_MachineA(str ptr) cfgmgr32.CM_Connect_MachineA
@ stdcall CM_Connect_MachineW(wstr ptr) cfgmgr32.CM_Connect_MachineW
@ stdcall CM_Create_DevNodeA(ptr str long long) cfgmgr32.CM_Create_DevNodeA
@ stdcall CM_Create_DevNodeW(ptr wstr long long) cfgmgr32.CM_Create_DevNodeW
@ stdcall CM_Create_DevNode_ExA(ptr str long long ptr) cfgmgr32.CM_Create_DevNode_ExA
@ stdcall CM_Create_DevNode_ExW(ptr wstr long long ptr) cfgmgr32.CM_Create_DevNode_ExW
@ stdcall CM_Create_Range_List(ptr long) cfgmgr32.CM_Create_Range_List
@ stdcall CM_Delete_Class_Key(ptr long) cfgmgr32.CM_Delete_Class_Key
@ stdcall CM_Delete_Class_Key_Ex(ptr long ptr) cfgmgr32.CM_Delete_Class_Key_Ex
@ stdcall CM_Delete_DevNode_Key(long long long) cfgmgr32.CM_Delete_DevNode_Key
@ stdcall CM_Delete_DevNode_Key_Ex(long long long ptr) cfgmgr32.CM_Delete_DevNode_Key_Ex
@ stub -version=0x600+ CM_Delete_Device_Interface_KeyA
@ stub -version=0x600+ CM_Delete_Device_Interface_KeyW
@ stub -version=0x600+ CM_Delete_Device_Interface_Key_ExA
@ stub -version=0x600+ CM_Delete_Device_Interface_Key_ExW
@ stub -version=0x600+ CM_Delete_PowerScheme
@ stdcall CM_Delete_Range(int64 int64 ptr long) cfgmgr32.CM_Delete_Range
@ stdcall CM_Detect_Resource_Conflict(long long ptr long ptr long) cfgmgr32.CM_Detect_Resource_Conflict
@ stdcall CM_Detect_Resource_Conflict_Ex(long long ptr long ptr long ptr) cfgmgr32.CM_Detect_Resource_Conflict_Ex
@ stdcall CM_Disable_DevNode(long long) cfgmgr32.CM_Disable_DevNode
@ stdcall CM_Disable_DevNode_Ex(long long ptr) cfgmgr32.CM_Disable_DevNode_Ex
@ stdcall CM_Disconnect_Machine(ptr) cfgmgr32.CM_Disconnect_Machine
@ stdcall CM_Dup_Range_List(ptr ptr long) cfgmgr32.CM_Dup_Range_List
@ stub -version=0x600+ CM_Duplicate_PowerScheme
@ stdcall CM_Enable_DevNode(long long) cfgmgr32.CM_Enable_DevNode
@ stdcall CM_Enable_DevNode_Ex(long long ptr) cfgmgr32.CM_Enable_DevNode_Ex
@ stdcall CM_Enumerate_Classes(long ptr long) cfgmgr32.CM_Enumerate_Classes
@ stdcall CM_Enumerate_Classes_Ex(long ptr long ptr) cfgmgr32.CM_Enumerate_Classes_Ex
@ stdcall CM_Enumerate_EnumeratorsA(long str ptr long) cfgmgr32.CM_Enumerate_EnumeratorsA
@ stdcall CM_Enumerate_EnumeratorsW(long wstr ptr long) cfgmgr32.CM_Enumerate_EnumeratorsW
@ stdcall CM_Enumerate_Enumerators_ExA(long str ptr long ptr) cfgmgr32.CM_Enumerate_Enumerators_ExA
@ stdcall CM_Enumerate_Enumerators_ExW(long wstr ptr long ptr) cfgmgr32.CM_Enumerate_Enumerators_ExW
@ stdcall CM_Find_Range(ptr int64 long int64 int64 ptr long) cfgmgr32.CM_Find_Range
@ stdcall CM_First_Range(ptr ptr ptr ptr long) cfgmgr32.CM_First_Range
@ stdcall CM_Free_Log_Conf(ptr long) cfgmgr32.CM_Free_Log_Conf
@ stdcall CM_Free_Log_Conf_Ex(ptr long ptr) cfgmgr32.CM_Free_Log_Conf_Ex
@ stdcall CM_Free_Log_Conf_Handle(ptr) cfgmgr32.CM_Free_Log_Conf_Handle
@ stdcall CM_Free_Range_List(ptr long) cfgmgr32.CM_Free_Range_List
@ stdcall CM_Free_Res_Des(ptr ptr long) cfgmgr32.CM_Free_Res_Des
@ stdcall CM_Free_Res_Des_Ex(ptr ptr long ptr) cfgmgr32.CM_Free_Res_Des_Ex
@ stdcall CM_Free_Res_Des_Handle(ptr) cfgmgr32.CM_Free_Res_Des_Handle
@ stdcall CM_Free_Resource_Conflict_Handle(ptr) cfgmgr32.CM_Free_Resource_Conflict_Handle
@ stdcall CM_Get_Child(ptr long long) cfgmgr32.CM_Get_Child
@ stdcall CM_Get_Child_Ex(ptr long long ptr) cfgmgr32.CM_Get_Child_Ex
@ stdcall CM_Get_Class_Key_NameA(ptr str ptr long) cfgmgr32.CM_Get_Class_Key_NameA
@ stdcall CM_Get_Class_Key_NameW(ptr wstr ptr long) cfgmgr32.CM_Get_Class_Key_NameW
@ stdcall CM_Get_Class_Key_Name_ExA(ptr str ptr long ptr) cfgmgr32.CM_Get_Class_Key_Name_ExA
@ stdcall CM_Get_Class_Key_Name_ExW(ptr wstr ptr long ptr) cfgmgr32.CM_Get_Class_Key_Name_ExW
@ stdcall CM_Get_Class_NameA(ptr str ptr long) cfgmgr32.CM_Get_Class_NameA
@ stdcall CM_Get_Class_NameW(ptr wstr ptr long) cfgmgr32.CM_Get_Class_NameW
@ stdcall CM_Get_Class_Name_ExA(ptr str ptr long ptr) cfgmgr32.CM_Get_Class_Name_ExA
@ stdcall CM_Get_Class_Name_ExW(ptr wstr ptr long ptr) cfgmgr32.CM_Get_Class_Name_ExW
@ stdcall CM_Get_Class_Registry_PropertyA(ptr long ptr ptr ptr long ptr) cfgmgr32.CM_Get_Class_Registry_PropertyA
@ stdcall CM_Get_Class_Registry_PropertyW(ptr long ptr ptr ptr long ptr) cfgmgr32.CM_Get_Class_Registry_PropertyW
@ stdcall CM_Get_Depth(ptr long long) cfgmgr32.CM_Get_Depth
@ stdcall CM_Get_Depth_Ex(ptr long long ptr) cfgmgr32.CM_Get_Depth_Ex
@ stdcall CM_Get_DevNode_Custom_PropertyA(long str ptr ptr ptr long) cfgmgr32.CM_Get_DevNode_Custom_PropertyA
@ stdcall CM_Get_DevNode_Custom_PropertyW(long wstr ptr ptr ptr long) cfgmgr32.CM_Get_DevNode_Custom_PropertyW
@ stdcall CM_Get_DevNode_Custom_Property_ExA(long str ptr ptr ptr long ptr) cfgmgr32.CM_Get_DevNode_Custom_Property_ExA
@ stdcall CM_Get_DevNode_Custom_Property_ExW(long wstr ptr ptr ptr long ptr) cfgmgr32.CM_Get_DevNode_Custom_Property_ExW
@ stdcall CM_Get_DevNode_Registry_PropertyA(long long ptr ptr ptr long) cfgmgr32.CM_Get_DevNode_Registry_PropertyA
@ stdcall CM_Get_DevNode_Registry_PropertyW(long long ptr ptr ptr long) cfgmgr32.CM_Get_DevNode_Registry_PropertyW
@ stdcall CM_Get_DevNode_Registry_Property_ExA(long long ptr ptr ptr long ptr) cfgmgr32.CM_Get_DevNode_Registry_Property_ExA
@ stdcall CM_Get_DevNode_Registry_Property_ExW(long long ptr ptr ptr long ptr) cfgmgr32.CM_Get_DevNode_Registry_Property_ExW
@ stdcall CM_Get_DevNode_Status(ptr ptr long long) cfgmgr32.CM_Get_DevNode_Status
@ stdcall CM_Get_DevNode_Status_Ex(ptr ptr long long ptr) cfgmgr32.CM_Get_DevNode_Status_Ex
@ stdcall CM_Get_Device_IDA(long str long long) cfgmgr32.CM_Get_Device_IDA
@ stdcall CM_Get_Device_IDW(long wstr long long) cfgmgr32.CM_Get_Device_IDW
@ stdcall CM_Get_Device_ID_ExA(long str long long ptr) cfgmgr32.CM_Get_Device_ID_ExA
@ stdcall CM_Get_Device_ID_ExW(long wstr long long ptr) cfgmgr32.CM_Get_Device_ID_ExW
@ stdcall CM_Get_Device_ID_ListA(str str long long) cfgmgr32.CM_Get_Device_ID_ListA
@ stdcall CM_Get_Device_ID_ListW(wstr wstr long long) cfgmgr32.CM_Get_Device_ID_ListW
@ stdcall CM_Get_Device_ID_List_ExA(str str long long ptr) cfgmgr32.CM_Get_Device_ID_List_ExA
@ stdcall CM_Get_Device_ID_List_ExW(wstr wstr long long ptr) cfgmgr32.CM_Get_Device_ID_List_ExW
@ stdcall CM_Get_Device_ID_List_SizeA(ptr str long) cfgmgr32.CM_Get_Device_ID_List_SizeA
@ stdcall CM_Get_Device_ID_List_SizeW(ptr wstr long) cfgmgr32.CM_Get_Device_ID_List_SizeW
@ stdcall CM_Get_Device_ID_List_Size_ExA(ptr str long ptr) cfgmgr32.CM_Get_Device_ID_List_Size_ExA
@ stdcall CM_Get_Device_ID_List_Size_ExW(ptr wstr long ptr) cfgmgr32.CM_Get_Device_ID_List_Size_ExW
@ stdcall CM_Get_Device_ID_Size(ptr long long) cfgmgr32.CM_Get_Device_ID_Size
@ stdcall CM_Get_Device_ID_Size_Ex(ptr long long ptr) cfgmgr32.CM_Get_Device_ID_Size_Ex
@ stdcall CM_Get_Device_Interface_AliasA(str ptr str ptr long) cfgmgr32.CM_Get_Device_Interface_AliasA
@ stdcall CM_Get_Device_Interface_AliasW(wstr ptr wstr ptr long) cfgmgr32.CM_Get_Device_Interface_AliasW
@ stdcall CM_Get_Device_Interface_Alias_ExA(str ptr str ptr long ptr) cfgmgr32.CM_Get_Device_Interface_Alias_ExA
@ stdcall CM_Get_Device_Interface_Alias_ExW(wstr ptr wstr ptr long ptr) cfgmgr32.CM_Get_Device_Interface_Alias_ExW
@ stdcall CM_Get_Device_Interface_ListA(ptr str str long long) cfgmgr32.CM_Get_Device_Interface_ListA
@ stdcall CM_Get_Device_Interface_ListW(ptr wstr wstr long long) cfgmgr32.CM_Get_Device_Interface_ListW
@ stdcall CM_Get_Device_Interface_List_ExA(ptr str str long long ptr) cfgmgr32.CM_Get_Device_Interface_List_ExA
@ stdcall CM_Get_Device_Interface_List_ExW(ptr wstr wstr long long ptr) cfgmgr32.CM_Get_Device_Interface_List_ExW
@ stdcall CM_Get_Device_Interface_List_SizeA(ptr ptr str long) cfgmgr32.CM_Get_Device_Interface_List_SizeA
@ stdcall CM_Get_Device_Interface_List_SizeW(ptr ptr wstr long) cfgmgr32.CM_Get_Device_Interface_List_SizeW
@ stdcall CM_Get_Device_Interface_List_Size_ExA(ptr ptr str long ptr) cfgmgr32.CM_Get_Device_Interface_List_Size_ExA
@ stdcall CM_Get_Device_Interface_List_Size_ExW(ptr ptr wstr long ptr) cfgmgr32.CM_Get_Device_Interface_List_Size_ExW
@ stdcall CM_Get_Device_Interface_PropertyW(wstr ptr ptr ptr ptr long) cfgmgr32.CM_Get_Device_Interface_PropertyW
@ stdcall CM_Get_First_Log_Conf(ptr long long) cfgmgr32.CM_Get_First_Log_Conf
@ stdcall CM_Get_First_Log_Conf_Ex(ptr long long long) cfgmgr32.CM_Get_First_Log_Conf_Ex
@ stdcall CM_Get_Global_State(ptr long) cfgmgr32.CM_Get_Global_State
@ stdcall CM_Get_Global_State_Ex(ptr long long) cfgmgr32.CM_Get_Global_State_Ex
@ stdcall CM_Get_HW_Prof_FlagsA(str long ptr long) cfgmgr32.CM_Get_HW_Prof_FlagsA
@ stdcall CM_Get_HW_Prof_FlagsW(wstr long ptr long) cfgmgr32.CM_Get_HW_Prof_FlagsW
@ stdcall CM_Get_HW_Prof_Flags_ExA(str long ptr long ptr) cfgmgr32.CM_Get_HW_Prof_Flags_ExA
@ stdcall CM_Get_HW_Prof_Flags_ExW(wstr long ptr long ptr) cfgmgr32.CM_Get_HW_Prof_Flags_ExW
@ stdcall CM_Get_Hardware_Profile_InfoA(long ptr long) cfgmgr32.CM_Get_Hardware_Profile_InfoA
@ stdcall CM_Get_Hardware_Profile_InfoW(long ptr long) cfgmgr32.CM_Get_Hardware_Profile_InfoW
@ stdcall CM_Get_Hardware_Profile_Info_ExA(long ptr long ptr) cfgmgr32.CM_Get_Hardware_Profile_Info_ExA
@ stdcall CM_Get_Hardware_Profile_Info_ExW(long ptr long ptr) cfgmgr32.CM_Get_Hardware_Profile_Info_ExW
@ stdcall CM_Get_Log_Conf_Priority(ptr ptr long) cfgmgr32.CM_Get_Log_Conf_Priority
@ stdcall CM_Get_Log_Conf_Priority_Ex(ptr ptr long ptr) cfgmgr32.CM_Get_Log_Conf_Priority_Ex
@ stdcall CM_Get_Next_Log_Conf(ptr ptr long) cfgmgr32.CM_Get_Next_Log_Conf
@ stdcall CM_Get_Next_Log_Conf_Ex(ptr ptr long ptr) cfgmgr32.CM_Get_Next_Log_Conf_Ex
@ stdcall CM_Get_Next_Res_Des(ptr ptr long ptr long) cfgmgr32.CM_Get_Next_Res_Des
@ stdcall CM_Get_Next_Res_Des_Ex(ptr ptr long ptr long ptr) cfgmgr32.CM_Get_Next_Res_Des_Ex
@ stdcall CM_Get_Parent(ptr long long) cfgmgr32.CM_Get_Parent
@ stdcall CM_Get_Parent_Ex(ptr long long ptr) cfgmgr32.CM_Get_Parent_Ex
@ stdcall CM_Get_Res_Des_Data(ptr ptr long long) cfgmgr32.CM_Get_Res_Des_Data
@ stdcall CM_Get_Res_Des_Data_Ex(ptr ptr long long ptr) cfgmgr32.CM_Get_Res_Des_Data_Ex
@ stdcall CM_Get_Res_Des_Data_Size(ptr ptr long) cfgmgr32.CM_Get_Res_Des_Data_Size
@ stdcall CM_Get_Res_Des_Data_Size_Ex(ptr ptr long ptr) cfgmgr32.CM_Get_Res_Des_Data_Size_Ex
@ stdcall CM_Get_Resource_Conflict_Count(ptr ptr) cfgmgr32.CM_Get_Resource_Conflict_Count
@ stdcall CM_Get_Resource_Conflict_DetailsA(ptr long ptr) cfgmgr32.CM_Get_Resource_Conflict_DetailsA
@ stdcall CM_Get_Resource_Conflict_DetailsW(ptr long ptr) cfgmgr32.CM_Get_Resource_Conflict_DetailsW
@ stdcall CM_Get_Sibling(ptr long long) cfgmgr32.CM_Get_Sibling
@ stdcall CM_Get_Sibling_Ex(ptr long long ptr) cfgmgr32.CM_Get_Sibling_Ex
@ stdcall CM_Get_Version() cfgmgr32.CM_Get_Version
@ stdcall CM_Get_Version_Ex(ptr) cfgmgr32.CM_Get_Version_Ex
@ stub -version=0x600+ CM_Import_PowerScheme
@ stub -version=0x600+ CM_Install_DevNodeW
@ stub -version=0x600+ CM_Install_DevNode_ExW
@ stdcall CM_Intersect_Range_List(ptr ptr ptr long) cfgmgr32.CM_Intersect_Range_List
@ stdcall CM_Invert_Range_List(ptr ptr int64 long) cfgmgr32.CM_Invert_Range_List
@ stdcall CM_Is_Dock_Station_Present(ptr) cfgmgr32.CM_Is_Dock_Station_Present
@ stdcall CM_Is_Dock_Station_Present_Ex(ptr ptr) cfgmgr32.CM_Is_Dock_Station_Present_Ex
@ stdcall CM_Is_Version_Available(long) cfgmgr32.CM_Is_Version_Available
@ stdcall CM_Is_Version_Available_Ex(long ptr) cfgmgr32.CM_Is_Version_Available_Ex
@ stdcall CM_Locate_DevNodeA(ptr str long) cfgmgr32.CM_Locate_DevNodeA
@ stdcall CM_Locate_DevNodeW(ptr wstr long) cfgmgr32.CM_Locate_DevNodeW
@ stdcall CM_Locate_DevNode_ExA(ptr str long ptr) cfgmgr32.CM_Locate_DevNode_ExA
@ stdcall CM_Locate_DevNode_ExW(ptr wstr long ptr) cfgmgr32.CM_Locate_DevNode_ExW
@ stdcall CM_Merge_Range_List(ptr ptr ptr long) cfgmgr32.CM_Merge_Range_List
@ stdcall CM_Modify_Res_Des(ptr ptr long ptr long long) cfgmgr32.CM_Modify_Res_Des
@ stdcall CM_Modify_Res_Des_Ex(ptr ptr long ptr long long ptr) cfgmgr32.CM_Modify_Res_Des_Ex
@ stdcall CM_Move_DevNode(long long long) cfgmgr32.CM_Move_DevNode
@ stdcall CM_Move_DevNode_Ex(long long long ptr) cfgmgr32.CM_Move_DevNode_Ex
@ stdcall CM_Next_Range(ptr ptr ptr long) cfgmgr32.CM_Next_Range
@ stdcall CM_Open_Class_KeyA(ptr str long long ptr long) cfgmgr32.CM_Open_Class_KeyA
@ stdcall CM_Open_Class_KeyW(ptr wstr long long ptr long) cfgmgr32.CM_Open_Class_KeyW
@ stdcall CM_Open_Class_Key_ExA(ptr str long long ptr long ptr) cfgmgr32.CM_Open_Class_Key_ExA
@ stdcall CM_Open_Class_Key_ExW(ptr wstr long long ptr long ptr) cfgmgr32.CM_Open_Class_Key_ExW
@ stdcall CM_Open_DevNode_Key(ptr long long long ptr long) cfgmgr32.CM_Open_DevNode_Key
@ stdcall CM_Open_DevNode_Key_Ex(ptr long long long ptr long ptr) cfgmgr32.CM_Open_DevNode_Key_Ex
@ stub -version=0x600+ CM_Open_Device_Interface_KeyA
@ stub -version=0x600+ CM_Open_Device_Interface_KeyW
@ stub -version=0x600+ CM_Open_Device_Interface_Key_ExA
@ stub -version=0x600+ CM_Open_Device_Interface_Key_ExW
@ stdcall CM_Query_And_Remove_SubTreeA(long ptr str long long) cfgmgr32.CM_Query_And_Remove_SubTreeA
@ stdcall CM_Query_And_Remove_SubTreeW(long ptr wstr long long) cfgmgr32.CM_Query_And_Remove_SubTreeW
@ stdcall CM_Query_And_Remove_SubTree_ExA(long ptr str long long ptr) cfgmgr32.CM_Query_And_Remove_SubTree_ExA
@ stdcall CM_Query_And_Remove_SubTree_ExW(long ptr wstr long long ptr) cfgmgr32.CM_Query_And_Remove_SubTree_ExW
@ stdcall CM_Query_Arbitrator_Free_Data(ptr long long long long) cfgmgr32.CM_Query_Arbitrator_Free_Data
@ stdcall CM_Query_Arbitrator_Free_Data_Ex(ptr long long long long ptr) cfgmgr32.CM_Query_Arbitrator_Free_Data_Ex
@ stdcall CM_Query_Arbitrator_Free_Size(ptr long long long) cfgmgr32.CM_Query_Arbitrator_Free_Size
@ stdcall CM_Query_Arbitrator_Free_Size_Ex(ptr long long long ptr) cfgmgr32.CM_Query_Arbitrator_Free_Size_Ex
@ stdcall CM_Query_Remove_SubTree(long long) cfgmgr32.CM_Query_Remove_SubTree
@ stdcall CM_Query_Remove_SubTree_Ex(long long ptr) cfgmgr32.CM_Query_Remove_SubTree_Ex
@ stdcall CM_Query_Resource_Conflict_List(ptr long long ptr long long ptr) cfgmgr32.CM_Query_Resource_Conflict_List
@ stdcall CM_Reenumerate_DevNode(long long) cfgmgr32.CM_Reenumerate_DevNode
@ stdcall CM_Reenumerate_DevNode_Ex(long long ptr) cfgmgr32.CM_Reenumerate_DevNode_Ex
@ stdcall CM_Register_Device_Driver(long long) cfgmgr32.CM_Register_Device_Driver
@ stdcall CM_Register_Device_Driver_Ex(long long ptr) cfgmgr32.CM_Register_Device_Driver_Ex
@ stdcall CM_Register_Device_InterfaceA(long ptr str str ptr long) cfgmgr32.CM_Register_Device_InterfaceA
@ stdcall CM_Register_Device_InterfaceW(long ptr wstr wstr ptr long) cfgmgr32.CM_Register_Device_InterfaceW
@ stdcall CM_Register_Device_Interface_ExA(long ptr str str ptr long ptr) cfgmgr32.CM_Register_Device_Interface_ExA
@ stdcall CM_Register_Device_Interface_ExW(long ptr wstr wstr ptr long ptr) cfgmgr32.CM_Register_Device_Interface_ExW
@ stdcall CM_Remove_SubTree(long long) cfgmgr32.CM_Remove_SubTree
@ stdcall CM_Remove_SubTree_Ex(long long ptr) cfgmgr32.CM_Remove_SubTree_Ex
@ stdcall CM_Request_Device_EjectA(long ptr str long long) cfgmgr32.CM_Request_Device_EjectA
@ stdcall CM_Request_Device_EjectW(long ptr wstr long long) cfgmgr32.CM_Request_Device_EjectW
@ stdcall CM_Request_Device_Eject_ExA(long ptr str long long ptr) cfgmgr32.CM_Request_Device_Eject_ExA
@ stdcall CM_Request_Device_Eject_ExW(long ptr wstr long long ptr) cfgmgr32.CM_Request_Device_Eject_ExW
@ stdcall CM_Request_Eject_PC() cfgmgr32.CM_Request_Eject_PC
@ stdcall CM_Request_Eject_PC_Ex(ptr) cfgmgr32.CM_Request_Eject_PC_Ex
@ stub -version=0x600+ CM_RestoreAll_DefaultPowerSchemes
@ stub -version=0x600+ CM_Restore_DefaultPowerScheme
@ stdcall CM_Run_Detection(long) cfgmgr32.CM_Run_Detection
@ stdcall CM_Run_Detection_Ex(long ptr) cfgmgr32.CM_Run_Detection_Ex
@ stub -version=0x600+ CM_Set_ActiveScheme
@ stdcall CM_Set_Class_Registry_PropertyA(ptr long ptr long long ptr) cfgmgr32.CM_Set_Class_Registry_PropertyA
@ stdcall CM_Set_Class_Registry_PropertyW(ptr long ptr long long ptr) cfgmgr32.CM_Set_Class_Registry_PropertyW
@ stdcall CM_Set_DevNode_Problem(long long long) cfgmgr32.CM_Set_DevNode_Problem
@ stdcall CM_Set_DevNode_Problem_Ex(long long long ptr) cfgmgr32.CM_Set_DevNode_Problem_Ex
@ stdcall CM_Set_DevNode_Registry_PropertyA(long long ptr long long) cfgmgr32.CM_Set_DevNode_Registry_PropertyA
@ stdcall CM_Set_DevNode_Registry_PropertyW(long long ptr long long) cfgmgr32.CM_Set_DevNode_Registry_PropertyW
@ stdcall CM_Set_DevNode_Registry_Property_ExA(long long ptr long long ptr) cfgmgr32.CM_Set_DevNode_Registry_Property_ExA
@ stdcall CM_Set_DevNode_Registry_Property_ExW(long long ptr long long ptr) cfgmgr32.CM_Set_DevNode_Registry_Property_ExW
@ stdcall CM_Set_HW_Prof(long long) cfgmgr32.CM_Set_HW_Prof
@ stdcall CM_Set_HW_Prof_Ex(long long ptr) cfgmgr32.CM_Set_HW_Prof_Ex
@ stdcall CM_Set_HW_Prof_FlagsA(str long long long) cfgmgr32.CM_Set_HW_Prof_FlagsA
@ stdcall CM_Set_HW_Prof_FlagsW(wstr long long long) cfgmgr32.CM_Set_HW_Prof_FlagsW
@ stdcall CM_Set_HW_Prof_Flags_ExA(str long long long ptr) cfgmgr32.CM_Set_HW_Prof_Flags_ExA
@ stdcall CM_Set_HW_Prof_Flags_ExW(wstr long long long ptr) cfgmgr32.CM_Set_HW_Prof_Flags_ExW
@ stdcall CM_Setup_DevNode(long long) cfgmgr32.CM_Setup_DevNode
@ stdcall CM_Setup_DevNode_Ex(long long ptr) cfgmgr32.CM_Setup_DevNode_Ex
@ stdcall CM_Test_Range_Available(int64 int64 ptr long) cfgmgr32.CM_Test_Range_Available
@ stdcall CM_Uninstall_DevNode(long long) cfgmgr32.CM_Uninstall_DevNode
@ stdcall CM_Uninstall_DevNode_Ex(long long ptr) cfgmgr32.CM_Uninstall_DevNode_Ex
@ stdcall CM_Unregister_Device_InterfaceA(str long) cfgmgr32.CM_Unregister_Device_InterfaceA
@ stdcall CM_Unregister_Device_InterfaceW(wstr long) cfgmgr32.CM_Unregister_Device_InterfaceW
@ stdcall CM_Unregister_Device_Interface_ExA(str long ptr) cfgmgr32.CM_Unregister_Device_Interface_ExA
@ stdcall CM_Unregister_Device_Interface_ExW(wstr long ptr) cfgmgr32.CM_Unregister_Device_Interface_ExW
@ stub -version=0x600+ CM_Write_UserPowerKey
@ stdcall DoesUserHavePrivilege(wstr)
@ stdcall -version=0x600+ DriverStoreAddDriverPackageA(str ptr ptr long ptr ptr)
@ stdcall -version=0x600+ DriverStoreAddDriverPackageW(wstr ptr ptr long ptr ptr)
@ stdcall -version=0x600+ DriverStoreDeleteDriverPackageA(str ptr ptr)
@ stdcall -version=0x600+ DriverStoreDeleteDriverPackageW(wstr ptr ptr)
@ stub -version=0x600+ DriverStoreEnumDriverPackageA
@ stub -version=0x600+ DriverStoreEnumDriverPackageW
@ stdcall -version=0x600+ DriverStoreFindDriverPackageA(str ptr ptr long ptr ptr ptr)
@ stdcall -version=0x600+ DriverStoreFindDriverPackageW(wstr ptr ptr long ptr ptr ptr)
@ stub ExtensionPropSheetPageProc
@ stdcall InstallCatalog(str str ptr)
@ stdcall InstallHinfSection(long long str long) InstallHinfSectionA
@ stdcall InstallHinfSectionA(long long str long)
@ stdcall InstallHinfSectionW(long long wstr long)
@ stdcall IsUserAdmin() pSetupIsUserAdmin
@ stdcall MyFree(ptr)
@ stdcall MyMalloc(long)
@ stdcall MyRealloc(ptr long)
@ stub -version=0x600+ PnpEnumDrpFile
@ stub -version=0x600+ PnpIsFileAclIntact
@ stub -version=0x600+ PnpIsFileContentIntact
@ stub -version=0x600+ PnpIsFilePnpDriver
@ stub -version=0x600+ PnpRepairWindowsProtectedDriver
@ stdcall SetupAddInstallSectionToDiskSpaceListA(long long long str ptr long)
@ stub SetupAddInstallSectionToDiskSpaceListW
@ stdcall -stub SetupAddSectionToDiskSpaceListA(long long long str long ptr long)
@ stub SetupAddSectionToDiskSpaceListW
@ stdcall SetupAddToDiskSpaceListA(long str int64 long ptr long)
@ stdcall SetupAddToDiskSpaceListW(long wstr int64 long ptr long)
@ stdcall SetupAddToSourceListA(long str)
@ stdcall SetupAddToSourceListW(long wstr)
@ stub SetupAdjustDiskSpaceListA
@ stub SetupAdjustDiskSpaceListW
@ stub SetupBackupErrorA
@ stub SetupBackupErrorW
@ stub SetupCancelTemporarySourceList
@ stdcall SetupCloseFileQueue(ptr)
@ stdcall SetupCloseInfFile(long)
@ stdcall SetupCloseLog()
@ stdcall SetupCommitFileQueue(long long ptr ptr) SetupCommitFileQueueA
@ stdcall SetupCommitFileQueueA(long long ptr ptr)
@ stdcall SetupCommitFileQueueW(long long ptr ptr)
@ stub -version=0x502+ SetupConfigureWmiFromInfSectionA
@ stub -version=0x502+ SetupConfigureWmiFromInfSectionW
@ stdcall SetupCopyErrorA(long str str str str str long long str long ptr)
@ stdcall SetupCopyErrorW(long wstr wstr wstr wstr wstr long long wstr long ptr)
@ stdcall SetupCopyOEMInfA(str str long long ptr long ptr ptr)
@ stdcall SetupCopyOEMInfW(wstr wstr long long ptr long ptr ptr)
@ stdcall SetupCreateDiskSpaceListA(ptr long long)
@ stdcall SetupCreateDiskSpaceListW(ptr long long)
@ stdcall SetupDecompressOrCopyFileA(str str ptr)
@ stdcall SetupDecompressOrCopyFileW(wstr wstr ptr)
@ stdcall SetupDefaultQueueCallback(ptr long long long) SetupDefaultQueueCallbackA
@ stdcall SetupDefaultQueueCallbackA(ptr long long long)
@ stdcall SetupDefaultQueueCallbackW(ptr long long long)
@ stdcall SetupDeleteErrorA(long str str long long)
@ stdcall SetupDeleteErrorW(long wstr wstr long long)
@ stdcall SetupDestroyDiskSpaceList(long)
@ stub -version=0x600+ SetupDiApplyPowerScheme
@ stub SetupDiAskForOEMDisk
@ stdcall SetupDiBuildClassInfoList(long ptr long ptr)
@ stdcall SetupDiBuildClassInfoListExA(long ptr long ptr str ptr)
@ stdcall SetupDiBuildClassInfoListExW(long ptr long ptr wstr ptr)
@ stdcall SetupDiBuildDriverInfoList(long ptr long)
@ stdcall SetupDiCallClassInstaller(long ptr ptr)
@ stub SetupDiCancelDriverInfoSearch
@ stdcall SetupDiChangeState(ptr ptr)
@ stdcall SetupDiClassGuidsFromNameA(str ptr long ptr)
@ stdcall SetupDiClassGuidsFromNameExA(str ptr long ptr str ptr)
@ stdcall SetupDiClassGuidsFromNameExW(wstr ptr long ptr wstr ptr)
@ stdcall SetupDiClassGuidsFromNameW(wstr ptr long ptr)
@ stdcall SetupDiClassNameFromGuidA(ptr str long ptr)
@ stdcall SetupDiClassNameFromGuidExA(ptr str long ptr wstr ptr)
@ stdcall SetupDiClassNameFromGuidExW(ptr wstr long ptr wstr ptr)
@ stdcall SetupDiClassNameFromGuidW(ptr wstr long ptr)
@ stdcall SetupDiCreateDevRegKeyA(ptr ptr long long long ptr str)
@ stdcall SetupDiCreateDevRegKeyW(ptr ptr long long long ptr wstr)
@ stdcall SetupDiCreateDeviceInfoA(ptr str ptr str ptr long ptr)
@ stdcall SetupDiCreateDeviceInfoList(ptr ptr)
@ stdcall SetupDiCreateDeviceInfoListExA(ptr long str ptr)
@ stdcall SetupDiCreateDeviceInfoListExW(ptr long wstr ptr)
@ stdcall SetupDiCreateDeviceInfoW(ptr wstr ptr wstr ptr long ptr)
@ stdcall SetupDiCreateDeviceInterfaceA(ptr ptr ptr str long ptr)
@ stdcall SetupDiCreateDeviceInterfaceRegKeyA(ptr ptr long long ptr ptr)
@ stdcall SetupDiCreateDeviceInterfaceRegKeyW(ptr ptr long long ptr ptr)
@ stdcall SetupDiCreateDeviceInterfaceW(ptr ptr ptr wstr long ptr)
@ stdcall SetupDiDeleteDevRegKey(ptr ptr long long long)
@ stdcall SetupDiDeleteDeviceInfo(long ptr)
@ stdcall SetupDiDeleteDeviceInterfaceData(ptr ptr)
@ stdcall SetupDiDeleteDeviceInterfaceRegKey(ptr ptr long)
@ stdcall SetupDiDestroyClassImageList(ptr)
@ stdcall SetupDiDestroyDeviceInfoList(long)
@ stdcall SetupDiDestroyDriverInfoList(long ptr long)
@ stdcall SetupDiDrawMiniIcon(ptr int128 long long)
@ stdcall SetupDiEnumDeviceInfo(long long ptr)
@ stdcall SetupDiEnumDeviceInterfaces(long ptr ptr long ptr)
@ stdcall SetupDiEnumDriverInfoA(long ptr long long ptr)
@ stdcall SetupDiEnumDriverInfoW(long ptr long long ptr)
@ stdcall -version=0x502+ SetupDiGetActualModelsSectionA(ptr ptr ptr long ptr ptr)
@ stdcall -version=0x502+ SetupDiGetActualModelsSectionW(ptr ptr ptr long ptr ptr)
@ stdcall SetupDiGetActualSectionToInstallA(long str str long ptr ptr)
@ stdcall SetupDiGetActualSectionToInstallExA(long str ptr str long ptr ptr ptr)
@ stdcall SetupDiGetActualSectionToInstallExW(long wstr ptr wstr long ptr ptr ptr)
@ stdcall SetupDiGetActualSectionToInstallW(long wstr wstr long ptr ptr)
@ stdcall SetupDiGetClassBitmapIndex(ptr ptr)
@ stdcall SetupDiGetClassDescriptionA(ptr str long ptr)
@ stdcall SetupDiGetClassDescriptionExA(ptr str long ptr str ptr)
@ stdcall SetupDiGetClassDescriptionExW(ptr wstr long ptr wstr ptr)
@ stdcall SetupDiGetClassDescriptionW(ptr wstr long ptr)
@ stdcall SetupDiGetClassDevPropertySheetsA(ptr ptr ptr long ptr long)
@ stdcall SetupDiGetClassDevPropertySheetsW(ptr ptr ptr long ptr long)
@ stdcall SetupDiGetClassDevsA(ptr ptr long long)
@ stdcall SetupDiGetClassDevsExA(ptr str ptr long ptr str ptr)
@ stdcall SetupDiGetClassDevsExW(ptr wstr ptr long ptr wstr ptr)
@ stdcall SetupDiGetClassDevsW(ptr ptr long long)
@ stdcall SetupDiGetClassImageIndex(ptr ptr ptr)
@ stdcall SetupDiGetClassImageList(ptr)
@ stdcall SetupDiGetClassImageListExA(ptr str ptr)
@ stdcall SetupDiGetClassImageListExW(ptr wstr ptr)
@ stdcall SetupDiGetClassInstallParamsA(ptr ptr ptr long ptr)
@ stdcall SetupDiGetClassInstallParamsW(ptr ptr ptr long ptr)
@ stub -version=0x600+ SetupDiGetClassPropertyExW
@ stub -version=0x600+ SetupDiGetClassPropertyKeys
@ stub -version=0x600+ SetupDiGetClassPropertyKeysExW
@ stub -version=0x600+ SetupDiGetClassPropertyW
@ stdcall SetupDiGetClassRegistryPropertyA(ptr long ptr ptr long ptr str ptr)
@ stdcall SetupDiGetClassRegistryPropertyW(ptr long ptr ptr long ptr wstr ptr)
@ stdcall SetupDiGetCustomDevicePropertyA(ptr ptr str long ptr ptr long ptr)
@ stdcall SetupDiGetCustomDevicePropertyW(ptr ptr wstr long ptr ptr long ptr)
@ stdcall SetupDiGetDeviceInfoListClass(ptr ptr)
@ stdcall SetupDiGetDeviceInfoListDetailA(ptr ptr)
@ stdcall SetupDiGetDeviceInfoListDetailW(ptr ptr)
@ stdcall SetupDiGetDeviceInstallParamsA(ptr ptr ptr)
@ stdcall SetupDiGetDeviceInstallParamsW(ptr ptr ptr)
@ stdcall SetupDiGetDeviceInstanceIdA(ptr ptr str long ptr)
@ stdcall SetupDiGetDeviceInstanceIdW(ptr ptr wstr long ptr)
@ stdcall SetupDiGetDeviceInterfaceAlias(ptr ptr ptr ptr)
@ stdcall SetupDiGetDeviceInterfaceDetailA(long ptr ptr long ptr ptr)
@ stdcall SetupDiGetDeviceInterfaceDetailW(long ptr ptr long ptr ptr)
@ stdcall SetupDiGetDeviceInterfacePropertyKeys(ptr ptr ptr long ptr long)
@ stdcall SetupDiGetDeviceInterfacePropertyW(ptr ptr ptr ptr ptr long ptr long)
@ stdcall SetupDiGetDevicePropertyKeys(ptr ptr ptr long ptr long)
@ stdcall SetupDiGetDevicePropertyW(ptr ptr ptr ptr ptr long ptr long)
@ stdcall SetupDiGetDeviceRegistryPropertyA(long ptr long ptr ptr long ptr)
@ stdcall SetupDiGetDeviceRegistryPropertyW(long ptr long ptr ptr long ptr)
@ stdcall SetupDiGetDriverInfoDetailA(ptr ptr ptr ptr long ptr)
@ stdcall SetupDiGetDriverInfoDetailW(ptr ptr ptr ptr long ptr)
@ stub SetupDiGetDriverInstallParamsA
@ stdcall SetupDiGetDriverInstallParamsW(ptr ptr ptr ptr)
@ stub SetupDiGetHwProfileFriendlyNameA
@ stub SetupDiGetHwProfileFriendlyNameExA
@ stub SetupDiGetHwProfileFriendlyNameExW
@ stub SetupDiGetHwProfileFriendlyNameW
@ stub SetupDiGetHwProfileList
@ stub SetupDiGetHwProfileListExA
@ stub SetupDiGetHwProfileListExW
@ stdcall SetupDiGetINFClassA(str ptr ptr long ptr)
@ stdcall SetupDiGetINFClassW(wstr ptr ptr long ptr)
@ stdcall SetupDiGetSelectedDevice(ptr ptr)
@ stdcall SetupDiGetSelectedDriverA(ptr ptr ptr)
@ stdcall SetupDiGetSelectedDriverW(ptr ptr ptr)
@ stub SetupDiGetWizardPage
@ stdcall SetupDiInstallClassA(long str long ptr)
@ stdcall SetupDiInstallClassExA(long str long ptr ptr ptr ptr)
@ stdcall SetupDiInstallClassExW(long wstr long ptr ptr ptr ptr)
@ stdcall SetupDiInstallClassW(long wstr long ptr)
@ stdcall SetupDiInstallDevice(ptr ptr)
@ stdcall SetupDiInstallDeviceInterfaces(ptr ptr)
@ stdcall SetupDiInstallDriverFiles(ptr ptr)
@ stdcall SetupDiLoadClassIcon(ptr ptr ptr)
@ stub -version=0x600+ SetupDiLoadDeviceIcon
@ stub SetupDiMoveDuplicateDevice
@ stdcall SetupDiOpenClassRegKey(ptr long)
@ stdcall SetupDiOpenClassRegKeyExA(ptr long long str ptr)
@ stdcall SetupDiOpenClassRegKeyExW(ptr long long wstr ptr)
@ stdcall SetupDiOpenDevRegKey(ptr ptr long long long long)
@ stdcall SetupDiOpenDeviceInfoA(ptr str long long ptr)
@ stdcall SetupDiOpenDeviceInfoW(ptr wstr long long ptr)
@ stdcall SetupDiOpenDeviceInterfaceA(ptr str long ptr)
@ stdcall SetupDiOpenDeviceInterfaceRegKey(ptr ptr long long)
@ stdcall SetupDiOpenDeviceInterfaceW(ptr wstr long ptr)
@ stdcall SetupDiRegisterCoDeviceInstallers(ptr ptr)
@ stdcall SetupDiRegisterDeviceInfo(ptr ptr long ptr ptr ptr)
@ stdcall SetupDiRemoveDevice(ptr ptr)
@ stdcall SetupDiRemoveDeviceInterface(ptr ptr)
@ stub -version=0x600+ SetupDiReportDeviceInstallError
@ stub -version=0x600+ SetupDiReportDriverNotFoundError
@ stub -version=0x600+ SetupDiReportDriverPackageImportationError
@ stub -version=0x600+ SetupDiReportGenericDriverInstalled
@ stub -version=0x600+ SetupDiReportPnPDeviceProblem
@ stdcall -version=0x502+ SetupDiRestartDevices(ptr ptr)
@ stdcall SetupDiSelectBestCompatDrv(ptr ptr)
@ stdcall SetupDiSelectDevice(ptr ptr)
@ stub SetupDiSelectOEMDrv
@ stdcall SetupDiSetClassInstallParamsA(ptr ptr ptr long)
@ stdcall SetupDiSetClassInstallParamsW(ptr ptr ptr long)
@ stub -version=0x600+ SetupDiSetClassPropertyExW
@ stub -version=0x600+ SetupDiSetClassPropertyW
@ stdcall SetupDiSetClassRegistryPropertyA(ptr long ptr long str ptr)
@ stdcall SetupDiSetClassRegistryPropertyW(ptr long ptr long wstr ptr)
@ stdcall SetupDiSetDeviceInstallParamsA(ptr ptr ptr)
@ stdcall SetupDiSetDeviceInstallParamsW(ptr ptr ptr)
@ stub SetupDiSetDeviceInterfaceDefault
@ stdcall SetupDiSetDeviceInterfacePropertyW(ptr ptr ptr long ptr long long)
@ stdcall SetupDiSetDevicePropertyW(ptr ptr ptr long ptr long long)
@ stdcall SetupDiSetDeviceRegistryPropertyA(ptr ptr long ptr long)
@ stdcall SetupDiSetDeviceRegistryPropertyW(ptr ptr long ptr long)
@ stdcall SetupDiSetDriverInstallParamsA(ptr ptr ptr ptr)
@ stdcall SetupDiSetDriverInstallParamsW(ptr ptr ptr ptr)
@ stdcall SetupDiSetSelectedDevice(ptr ptr)
@ stdcall SetupDiSetSelectedDriverA(ptr ptr ptr)
@ stdcall SetupDiSetSelectedDriverW(ptr ptr ptr)
@ stdcall SetupDiUnremoveDevice(ptr ptr)
@ stdcall SetupDuplicateDiskSpaceListA(ptr ptr long long)
@ stdcall SetupDuplicateDiskSpaceListW(ptr ptr long long)
@ stdcall SetupEnumInfSectionsA(long long ptr long ptr)
@ stdcall SetupEnumInfSectionsW(long long ptr long ptr)
@ stub -version=0x600+ SetupEnumPublishedInfA
@ stub -version=0x600+ SetupEnumPublishedInfW
@ stdcall SetupFindFirstLineA(long str str ptr)
@ stdcall SetupFindFirstLineW(long wstr wstr ptr)
@ stdcall SetupFindNextLine(ptr ptr)
@ stdcall SetupFindNextMatchLineA(ptr str ptr)
@ stdcall SetupFindNextMatchLineW(ptr wstr ptr)
@ stub SetupFreeSourceListA
@ stub SetupFreeSourceListW
@ stub SetupGetBackupInformationA
@ stub SetupGetBackupInformationW
@ stdcall SetupGetBinaryField(ptr long ptr long ptr)
@ stdcall SetupGetFieldCount(ptr)
@ stdcall SetupGetFileCompressionInfoA(str ptr ptr ptr ptr)
@ stdcall SetupGetFileCompressionInfoExA(str ptr long ptr ptr ptr ptr)
@ stdcall SetupGetFileCompressionInfoExW(wstr ptr long ptr ptr ptr ptr)
@ stdcall SetupGetFileCompressionInfoW(wstr ptr ptr ptr ptr)
@ stdcall SetupGetFileQueueCount(long long ptr)
@ stdcall SetupGetFileQueueFlags(long ptr)
@ stub -version=0x600+ SetupGetInfDriverStoreLocationA
@ stdcall SetupGetInfDriverStoreLocationW(wstr ptr wstr ptr long ptr)
@ stdcall SetupGetInfFileListA(str long str long ptr)
@ stdcall SetupGetInfFileListW(wstr long wstr long ptr)
@ stdcall SetupGetInfInformationA(ptr long ptr long ptr)
@ stdcall SetupGetInfInformationW(ptr long ptr long ptr)
@ stub -version=0x600+ SetupGetInfPublishedNameA
@ stub -version=0x600+ SetupGetInfPublishedNameW
@ stub SetupGetInfSections
@ stdcall SetupGetIntField(ptr long ptr)
@ stdcall SetupGetLineByIndexA(long str long ptr)
@ stdcall SetupGetLineByIndexW(long wstr long ptr)
@ stdcall SetupGetLineCountA(long str)
@ stdcall SetupGetLineCountW(long wstr)
@ stdcall SetupGetLineTextA(ptr long str str ptr long ptr)
@ stdcall SetupGetLineTextW(ptr long wstr wstr ptr long ptr)
@ stdcall SetupGetMultiSzFieldA(ptr long ptr long ptr)
@ stdcall SetupGetMultiSzFieldW(ptr long ptr long ptr)
@ stdcall SetupGetNonInteractiveMode()
@ stdcall SetupGetSourceFileLocationA(ptr ptr str ptr ptr long ptr)
@ stdcall SetupGetSourceFileLocationW(ptr ptr wstr ptr ptr long ptr)
@ stub SetupGetSourceFileSizeA
@ stub SetupGetSourceFileSizeW
@ stdcall SetupGetSourceInfoA(ptr long long ptr long ptr)
@ stdcall SetupGetSourceInfoW(ptr long long ptr long ptr)
@ stdcall SetupGetStringFieldA(ptr long ptr long ptr)
@ stdcall SetupGetStringFieldW(ptr long ptr long ptr)
@ stdcall SetupGetTargetPathA(ptr ptr str ptr long ptr)
@ stdcall SetupGetTargetPathW(ptr ptr wstr ptr long ptr)
@ stub -version=0x600+ SetupGetThreadLogToken
@ stdcall SetupInitDefaultQueueCallback(long)
@ stdcall SetupInitDefaultQueueCallbackEx(long long long long ptr)
@ stdcall SetupInitializeFileLogA(str long)
@ stdcall SetupInitializeFileLogW(wstr long)
@ stdcall SetupInstallFileA(ptr ptr str str str long ptr ptr)
@ stdcall SetupInstallFileExA(ptr ptr str str str long ptr ptr ptr)
@ stdcall SetupInstallFileExW(ptr ptr wstr wstr wstr long ptr ptr ptr)
@ stdcall SetupInstallFileW(ptr ptr wstr wstr wstr long ptr ptr)
@ stdcall SetupInstallFilesFromInfSectionA(long long long str str long)
@ stdcall SetupInstallFilesFromInfSectionW(long long long wstr wstr long)
@ stdcall SetupInstallFromInfSectionA(long long str long long str long ptr ptr long ptr)
@ stdcall SetupInstallFromInfSectionW(long long wstr long long wstr long ptr ptr long ptr)
@ stub -version=0x600+ SetupInstallLogCloseEventGroup
@ stub -version=0x600+ SetupInstallLogCreateEventGroup
@ stdcall SetupInstallServicesFromInfSectionA(long str long)
@ stdcall SetupInstallServicesFromInfSectionExA(long str long ptr ptr ptr ptr)
@ stdcall SetupInstallServicesFromInfSectionExW(long wstr long ptr ptr ptr ptr)
@ stdcall SetupInstallServicesFromInfSectionW(long wstr long)
@ stdcall SetupIterateCabinetA(str long ptr ptr)
@ stdcall SetupIterateCabinetW(wstr long ptr ptr)
@ stdcall SetupLogErrorA(str long)
@ stdcall SetupLogErrorW(wstr long)
@ stdcall SetupLogFileA(ptr str str str long str str str long)
@ stdcall SetupLogFileW(ptr wstr wstr wstr long wstr wstr wstr long)
@ stdcall SetupOpenAppendInfFileA(str long ptr)
@ stdcall SetupOpenAppendInfFileW(wstr long ptr)
@ stdcall SetupOpenFileQueue()
@ stdcall SetupOpenInfFileA(str str long ptr)
@ stdcall SetupOpenInfFileW(wstr wstr long ptr)
@ stdcall SetupOpenLog(long)
@ stdcall SetupOpenMasterInf()
@ stub SetupPrepareQueueForRestoreA
@ stub SetupPrepareQueueForRestoreW
@ stdcall SetupPromptForDiskA(ptr str str str str str long ptr long ptr)
@ stdcall SetupPromptForDiskW(ptr wstr wstr wstr wstr wstr long ptr long ptr)
@ stdcall SetupPromptReboot(ptr ptr long)
@ stdcall SetupQueryDrivesInDiskSpaceListA(ptr ptr long ptr)
@ stdcall SetupQueryDrivesInDiskSpaceListW(ptr ptr long ptr)
@ stub SetupQueryFileLogA
@ stub SetupQueryFileLogW
@ stdcall SetupQueryInfFileInformationA(ptr long str long ptr)
@ stdcall SetupQueryInfFileInformationW(ptr long wstr long ptr)
@ stdcall SetupQueryInfOriginalFileInformationA(ptr long ptr ptr)
@ stdcall SetupQueryInfOriginalFileInformationW(ptr long ptr ptr)
@ stdcall SetupQueryInfVersionInformationA(ptr long str ptr long ptr)
@ stdcall SetupQueryInfVersionInformationW(ptr long wstr ptr long ptr)
@ stub SetupQuerySourceListA
@ stub SetupQuerySourceListW
@ stdcall SetupQuerySpaceRequiredOnDriveA(long str ptr ptr long)
@ stdcall SetupQuerySpaceRequiredOnDriveW(long wstr ptr ptr long)
@ stdcall SetupQueueCopyA(long str str str str str str str long)
@ stdcall SetupQueueCopyIndirectA(ptr)
@ stdcall SetupQueueCopyIndirectW(ptr)
@ stdcall SetupQueueCopySectionA(long str long long str long)
@ stdcall SetupQueueCopySectionW(long wstr long long wstr long)
@ stdcall SetupQueueCopyW(long wstr wstr wstr wstr wstr wstr wstr long)
@ stdcall SetupQueueDefaultCopyA(long long str str str long)
@ stdcall SetupQueueDefaultCopyW(long long wstr wstr wstr long)
@ stdcall SetupQueueDeleteA(long str str)
@ stdcall SetupQueueDeleteSectionA(long long long str)
@ stdcall SetupQueueDeleteSectionW(long long long wstr)
@ stdcall SetupQueueDeleteW(long wstr wstr)
@ stdcall SetupQueueRenameA(long str str str str)
@ stdcall SetupQueueRenameSectionA(long long long str)
@ stdcall SetupQueueRenameSectionW(long long long wstr)
@ stdcall SetupQueueRenameW(long wstr wstr wstr wstr)
@ stub SetupRemoveFileLogEntryA
@ stub SetupRemoveFileLogEntryW
@ stub SetupRemoveFromDiskSpaceListA
@ stub SetupRemoveFromDiskSpaceListW
@ stdcall -stub SetupRemoveFromSourceListA(long str)
@ stdcall -stub SetupRemoveFromSourceListW(long wstr)
@ stub SetupRemoveInstallSectionFromDiskSpaceListA
@ stub SetupRemoveInstallSectionFromDiskSpaceListW
@ stub SetupRemoveSectionFromDiskSpaceListA
@ stub SetupRemoveSectionFromDiskSpaceListW
@ stdcall SetupRenameErrorA(long str str str long long)
@ stdcall SetupRenameErrorW(long wstr wstr wstr long long)
@ stdcall SetupScanFileQueue(long long long ptr ptr ptr) SetupScanFileQueueA
@ stdcall SetupScanFileQueueA(long long long ptr ptr ptr)
@ stdcall SetupScanFileQueueW(long long long ptr ptr ptr)
@ stdcall SetupSetDirectoryIdA(long long str)
@ stub SetupSetDirectoryIdExA
@ stub SetupSetDirectoryIdExW
@ stdcall SetupSetDirectoryIdW(long long wstr)
@ stdcall SetupSetFileQueueAlternatePlatformA(ptr ptr str)
@ stdcall SetupSetFileQueueAlternatePlatformW(ptr ptr wstr)
@ stdcall SetupSetFileQueueFlags(long long long)
@ stdcall SetupSetNonInteractiveMode(long)
@ stub SetupSetPlatformPathOverrideA
@ stub SetupSetPlatformPathOverrideW
@ stdcall SetupSetSourceListA(long ptr long)
@ stdcall SetupSetSourceListW(long ptr long)
@ stub -version=0x600+ SetupSetThreadLogToken
@ stdcall SetupTermDefaultQueueCallback(ptr)
@ stdcall SetupTerminateFileLog(long)
@ stub SetupUninstallNewlyCopiedInfs
@ stdcall SetupUninstallOEMInfA(str long ptr)
@ stdcall SetupUninstallOEMInfW(wstr long ptr)
@ stdcall SetupVerifyInfFileA(str ptr ptr)
@ stdcall SetupVerifyInfFileW(wstr ptr ptr)
@ stub -version=0x600+ SetupWriteTextLog
@ stub -version=0x600+ SetupWriteTextLogError
@ stub -version=0x600+ SetupWriteTextLogInfLine
@ stdcall UnicodeToMultiByte(wstr long) pSetupUnicodeToMultiByte
@ stub -version=0x600+ UpdateAllDeviceIndirectStrings
@ stub VerifyCatalogFile
@ stub -version=0x600+ pGetDriverPackageHash
@ stub -version=0x600+ pServerAddDriverPackage
@ stub -version=0x600+ pServerDeleteDriverPackage
@ stub pSetupAccessRunOnceNodeList
@ stub -version=0x501-0x502 pSetupAcquireSCMLock
@ stub pSetupAddMiniIconToList
@ stub pSetupAddTagToGroupOrderListEntry
@ stub -version=0x600+ pSetupAppendPath
@ stub -version=0x501-0x502 pSetupAppendStringToMultiSz
@ stdcall pSetupCaptureAndConvertAnsiArg(str ptr)
@ stdcall pSetupCenterWindowRelativeToParent(long)
@ stub -version=0x600+ pSetupCloseTextLogSection
@ stdcall pSetupConcatenatePaths(wstr wstr long ptr)
@ stub -version=0x600+ pSetupCreateTextLogSectionA
@ stub -version=0x600+ pSetupCreateTextLogSectionW
@ stub pSetupDestroyRunOnceNodeList
@ stub -version=0x600+ pSetupDiBuildInfoDataFromStrongName
@ stub -version=0x600+ pSetupDiCrimsonLogDeviceInstall
@ stub -version=0x501-0x502 pSetupDiGetDeviceInfoContext
@ stub -version=0x501-0x502 pSetupDiSetDeviceInfoContext
@ stub -version=0x600+ pSetupDiGetStrongNameForDriverNode
@ stub -version=0x600+ pSetupDiInvalidateHelperModules
@ stub -version=0x600+ pSetupDoLastKnownGoodBackup
@ stdcall pSetupDoesUserHavePrivilege(wstr) DoesUserHavePrivilege
@ stdcall pSetupDuplicateString(wstr)
@ stdcall pSetupEnablePrivilege(wstr long)
@ stdcall pSetupFree(ptr) MyFree
@ stub -version=0x501-0x502 pSetupFreeStringArray
@ stub pSetupGetCurrentDriverSigningPolicy
@ stub -version=0x600+ pSetupGetDriverDate
@ stub -version=0x600+ pSetupGetDriverVersion
@ stdcall pSetupGetField(ptr long)
@ stdcall pSetupGetFileTitle(wstr)
@ stdcall pSetupGetGlobalFlags()
@ stub -version=0x600+ pSetupGetIndirectStringsFromDriverInfo
@ stub pSetupGetInfSections
@ stdcall pSetupGetQueueFlags(ptr)
@ stub pSetupGetRealSystemTime
@ stdcall -version=0x501-0x502 pSetupGetVersionInfoFromImage(wstr ptr ptr)
@ stdcall pSetupGuidFromString(wstr ptr)
@ stub pSetupHandleFailedVerification
@ stub -version=0x501-0x502 pSetupInfCacheBuild
@ stub -version=0x600+ pSetupInfGetDigitalSignatureInfo
@ stub -version=0x501-0x502 pSetupInfIsFromOemLocation
@ stub -version=0x600+ pSetupInfSetDigitalSignatureInfo
@ stdcall pSetupInstallCatalog(wstr wstr ptr)
@ stub -version=0x501-0x502 pSetupInstallStopEx
@ stub -version=0x600+ pSetupIsBiDiLocalizedSystemEx
@ stdcall pSetupIsGuidNull(ptr)
@ stub -version=0x502+ pSetupIsLocalSystem
@ stdcall pSetupIsUserAdmin()
@ stub -version=0x600+ pSetupIsUserTrustedInstaller
@ stub -version=0x600+ pSetupLoadIndirectString
@ stub pSetupMakeSurePathExists
@ stdcall pSetupMalloc(long) MyMalloc
@ stdcall pSetupModifyGlobalFlags(long long)
@ stdcall pSetupMultiByteToUnicode(str long)
@ stdcall pSetupOpenAndMapFileForRead(wstr ptr ptr ptr ptr)
@ stub pSetupOutOfMemory
@ stub pSetupQueryMultiSzValueToArray
@ stdcall pSetupRealloc(ptr long) MyRealloc
@ stdcall pSetupRegistryDelnode(long long)
@ stub pSetupRetrieveServiceConfig
@ stub pSetupSetArrayToMultiSzValue
@ stdcall pSetupSetGlobalFlags(long)
@ stub pSetupSetNoDriverPrompts
@ stdcall pSetupSetQueueFlags(ptr long)
@ stub -version=0x501-0x502 pSetupSetSystemSourcePath
@ stub pSetupShouldDeviceBeExcluded
@ stdcall pSetupStringFromGuid(ptr wstr long)
@ stdcall pSetupStringTableAddString(ptr wstr long)
@ stdcall pSetupStringTableAddStringEx(ptr wstr long ptr long)
@ stdcall pSetupStringTableDestroy(ptr)
@ stdcall pSetupStringTableDuplicate(ptr)
@ stub pSetupStringTableEnum
@ stdcall pSetupStringTableGetExtraData(ptr long ptr long)
@ stdcall pSetupStringTableInitialize()
@ stdcall pSetupStringTableInitializeEx(long long)
@ stdcall pSetupStringTableLookUpString(ptr wstr long)
@ stdcall pSetupStringTableLookUpStringEx(ptr wstr long ptr ptr)
@ stdcall pSetupStringTableSetExtraData(ptr long ptr long)
@ stdcall pSetupStringTableStringFromId(ptr long)
@ stdcall pSetupStringTableStringFromIdEx(ptr long ptr ptr)
@ stdcall pSetupUnicodeToMultiByte(wstr long)
@ stdcall pSetupUnmapAndCloseFile(long long ptr)
@ stub pSetupVerifyCatalogFile
@ stub -version=0x501-0x502 pSetupVerifyFile
@ stub pSetupVerifyQueuedCatalogs
@ stub pSetupWriteLogEntry
@ stub pSetupWriteLogError
@ stub -version=0x600 pSpAddInboxDriverPackage # Vista only
@ stub -version=0x600 pSpDeleteInboxDriverPackage # Vista only
@ stub AcquireSCMLock
@ stub AddMiniIconToList
@ stub AddTagToGroupOrderListEntry
@ stub AppendStringToMultiSz
@ stdcall AssertFail(str long str)
@ stub CMP_WaitServices
@ stdcall CM_Get_DevNode_PropertyW(long ptr ptr ptr ptr long) cfgmgr32.CM_Get_DevNode_PropertyW
@ stdcall CM_Get_DevNode_Property_ExW(long ptr ptr ptr ptr long ptr) cfgmgr32.CM_Get_DevNode_Property_ExW
@ stub CM_Remove_Unmarked_Children
@ stub CM_Remove_Unmarked_Children_Ex
@ stub CM_Reset_Children_Marks
@ stub CM_Reset_Children_Marks_Ex
@ stdcall CaptureAndConvertAnsiArg(str ptr) pSetupCaptureAndConvertAnsiArg
@ stdcall CaptureStringArg(wstr ptr)
@ stub CenterWindowRelativeToParent
@ stub ConcatenatePaths
@ stdcall DelayedMove(wstr wstr)
@ stub DelimStringToMultiSz
@ stub DestroyTextFileReadBuffer
@ stdcall DuplicateString(wstr) pSetupDuplicateString
@ stdcall EnablePrivilege(wstr long) pSetupEnablePrivilege
@ stdcall FileExists(wstr ptr)
@ stub FreeStringArray
@ stub GetCurrentDriverSigningPolicy
@ stub GetNewInfName
@ stdcall GetSetFileTimestamp(wstr ptr ptr ptr long)
@ stub GetVersionInfoFromImage
@ stub InfIsFromOemLocation
@ stub InstallStop
@ stub InstallStopEx
@ stub LookUpStringInTable
@ stub MemoryInitialize
@ stdcall MultiByteToUnicode(str long) pSetupMultiByteToUnicode
@ stub MultiSzFromSearchControl
@ stub MyGetFileTitle
@ stdcall OpenAndMapFileForRead(wstr ptr ptr ptr ptr) pSetupOpenAndMapFileForRead
@ stub OutOfMemory
@ stub QueryMultiSzValueToArray
@ stdcall QueryRegistryValue(long wstr ptr ptr ptr)
@ stub ReadAsciiOrUnicodeTextFile
@ stdcall RegistryDelnode(long long) pSetupRegistryDelnode
@ stdcall RetreiveFileSecurity(wstr ptr)
@ stub RetrieveServiceConfig
@ stub SearchForInfFile
@ stub SetArrayToMultiSzValue
@ stub SetupDiDeleteDeviceRegKey
@ stub ShouldDeviceBeExcluded
@ stdcall StampFileSecurity(wstr ptr)
@ stdcall StringTableAddString(ptr wstr long) pSetupStringTableAddString
@ stdcall StringTableAddStringEx(ptr wstr long ptr long) pSetupStringTableAddStringEx
@ stdcall StringTableDestroy(ptr) pSetupStringTableDestroy
@ stdcall StringTableDuplicate(ptr) pSetupStringTableDuplicate
@ stub StringTableEnum
@ stdcall StringTableGetExtraData(ptr long ptr long) pSetupStringTableGetExtraData
@ stdcall StringTableInitialize() pSetupStringTableInitialize
@ stdcall StringTableInitializeEx(long long) pSetupStringTableInitializeEx
@ stdcall StringTableLookUpString(ptr wstr long) pSetupStringTableLookUpString
@ stdcall StringTableLookUpStringEx(ptr wstr long ptr long) pSetupStringTableLookUpStringEx
@ stdcall StringTableSetExtraData(ptr long ptr long) pSetupStringTableSetExtraData
@ stdcall StringTableStringFromId(ptr long) pSetupStringTableStringFromId
@ stdcall StringTableStringFromIdEx(ptr long ptr ptr) pSetupStringTableStringFromIdEx
@ stdcall StringTableTrim(ptr)
@ stdcall TakeOwnershipOfFile(wstr)
@ stdcall UnmapAndCloseFile(long long ptr) pSetupUnmapAndCloseFile
@ stub VerifyFile
@ stub pSetupDirectoryIdToPath
@ stub pSetupGetOsLoaderDriveAndPath
@ stub pSetupGetVersionDatum
@ stub pSetupSetSystemSourceFlags
