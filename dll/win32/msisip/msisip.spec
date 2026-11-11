5 stdcall MsiSIPIsMyTypeOfFile(wstr ptr)
6 stdcall MsiSIPGetSignedDataMsg(ptr ptr long ptr ptr)
7 stub MsiSIPPutSignedDataMsg
8 stub MsiSIPRemoveSignedDataMsg
9 stdcall MsiSIPCreateIndirectData(ptr ptr ptr)
10 stdcall MsiSIPVerifyIndirectData(ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
