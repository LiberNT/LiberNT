
#define STANDALONE
#include <apitest.h>

extern void func_RegisterTouchWindow(void);
extern void func_AttachThreadInput(void);
extern void func_CalculatePopupWindowPosition(void);
extern void func_CallbackNesting(void);
extern void func_CharFuncs(void);
extern void func_CloseWindow(void);
extern void func_ComboBox(void);
extern void func_CopyImage(void);
extern void func_CopyImage_Icon(void);
extern void func_CursorPresentation(void);
extern void func_CreateDialog(void);
extern void func_CreateIconFromResourceEx(void);
extern void func_CoveredWindow(void);
extern void func_CreateWindowEx(void);
extern void func_DeferWindowPos(void);
extern void func_DefFrameProc(void);
extern void func_DeskWallpaper(void);
extern void func_DestroyCursorIcon(void);
extern void func_DestroyActiveWindow(void);
extern void func_DestroyWindow(void);
extern void func_DialogDpiChangeBehavior(void);
extern void func_DM_REPOSITION(void);
extern void func_Dpi(void);
extern void func_DrawIconEx(void);
extern void func_DrawText(void);
extern void func_desktop(void);
extern void func_EmptyClipboard(void);
extern void func_ClipboardAccess(void);
extern void func_ClipboardData(void);
extern void func_EnableScrollBar(void);
extern void func_EnumDisplaySettings(void);
extern void func_ForegroundRetake(void);
extern void func_ForegroundRight(void);
extern void func_ForegroundStartup(void);
extern void func_GetClassInfo(void);
extern void func_GetDCEx(void);
extern void func_GetProp(void);
extern void func_WindowRegionDC(void);
extern void func_GetIconInfo(void);
extern void func_GetKeyState(void);
extern void func_GetMessageTime(void);
extern void func_GetPeekMessage(void);
extern void func_GetPointerDevices(void);
extern void func_GetSetWindowInt(void);
extern void func_GetSystemMetrics(void);
extern void func_GetTitleBarInfoEx(void);
extern void func_GetUserObjectInformation(void);
extern void func_GetWindowPlacement(void);
extern void func_GhostWindow(void);
extern void func_GW_ENABLEDPOPUP(void);
extern void func_ImeWindow(void);
extern void func_InitializeLpkHooks(void);
extern void func_InputMessageSource(void);
extern void func_InvalidHandles(void);
extern void func_IsTopLevelWindow(void);
extern void func_IsServerSideWindow(void);
extern void func_JobUiWindowAccess(void);
extern void func_KbdLayout(void);
extern void func_LargeMessageBuffers(void);
extern void func_keybd_event(void);
extern void func_LoadImage(void);
extern void func_LoadImageGCC(void);
extern void func_LookupIconIdFromDirectoryEx(void);
extern void func_MenuUI(void);
extern void func_MenuItemData(void);
extern void func_MessageStateAnalyzer(void);
extern void func_MinimizedPosition(void);
extern void func_ModernUser32(void);
extern void func_NestedActivation(void);
extern void func_NestedWindowPos(void);
extern void func_NcPaintRgn(void);
extern void func_NextDlgItem(void);
extern void func_NonClientPaint(void);
extern void func_NonClientUpdate(void);
extern void func_OwnerDestroy(void);
extern void func_PersianLayout(void);
extern void func_PhysicalPoint(void);
extern void func_PhysicalToLogicalPoint(void);
extern void func_LogicalToPhysicalPoint(void);
extern void func_PrivateExtractIcons(void);
extern void func_RealGetWindowClass(void);
extern void func_RedrawWindow(void);
extern void func_RegisterHotKey(void);
extern void func_RegisterClassEx(void);
extern void func_ScrollBarRedraw(void);
extern void func_ScrollBarWndExtra(void);
extern void func_ScrollDC(void);
extern void func_ScrollWindowEx(void);
extern void func_UnhookWinEvent(void);
extern void func_SendMessageTimeout(void);
extern void func_SendMessageTimeoutHung(void);
extern void func_SetActiveWindow(void);
extern void func_SetCursorPos(void);
extern void func_SetFocus(void);
extern void func_SetParent(void);
extern void func_SetProp(void);
extern void func_SetScrollInfo(void);
extern void func_SetScrollRange(void);
extern void func_SetWindowPlacement(void);
extern void func_WindowDisplayAffinity(void);
extern void func_WindowRegionFrame(void);
extern void func_ShellWindowQueries(void);
extern void func_ShowWindow(void);
extern void func_SwitchToThisWindow(void);
extern void func_SystemParametersInfo(void);
extern void func_TopmostBand(void);
extern void func_SystemMenu(void);
extern void func_TrackMouseEvent(void);
extern void func_TrackPopupMenuEx(void);
extern void func_UncoveredWindow(void);
extern void func_UpdateRegion(void);
extern void func_UserHandleGrantAccess(void);
extern void func_VirtualKey(void);
extern void func_WindowBand(void);
extern void func_CallWindowProc(void);
extern void func_WndProc(void);
extern void func_wsprintfApi(void);

const struct test winetest_testlist[] =
{
    { "RegisterTouchWindow", func_RegisterTouchWindow },
    { "AttachThreadInput", func_AttachThreadInput },
    { "CalculatePopupWindowPosition", func_CalculatePopupWindowPosition },
    { "CallbackNesting", func_CallbackNesting },
    { "CharFuncs", func_CharFuncs },
    { "CloseWindow", func_CloseWindow },
    { "ComboBox", func_ComboBox },
    { "CopyImage", func_CopyImage },
    { "CopyImage_Icon", func_CopyImage_Icon },
    { "CursorPresentation", func_CursorPresentation },
    { "CreateDialog", func_CreateDialog },
    { "CreateIconFromResourceEx", func_CreateIconFromResourceEx },
    { "CoveredWindow", func_CoveredWindow },
    { "CreateWindowEx", func_CreateWindowEx },
    { "DeferWindowPos", func_DeferWindowPos },
    { "DefFrameProc", func_DefFrameProc },
    { "DeskWallpaper", func_DeskWallpaper },
    { "DestroyCursorIcon", func_DestroyCursorIcon },
    { "DestroyActiveWindow", func_DestroyActiveWindow },
    { "DestroyWindow", func_DestroyWindow },
    { "DialogDpiChangeBehavior", func_DialogDpiChangeBehavior },
    { "DM_REPOSITION", func_DM_REPOSITION },
    { "Dpi", func_Dpi },
    { "DrawIconEx", func_DrawIconEx },
    { "DrawText", func_DrawText },
    { "desktop", func_desktop },
    { "EmptyClipboard", func_EmptyClipboard },
    { "ClipboardAccess", func_ClipboardAccess },
    { "ClipboardData", func_ClipboardData },
    { "EnableScrollBar", func_EnableScrollBar },
    { "EnumDisplaySettings", func_EnumDisplaySettings },
    { "ForegroundRetake", func_ForegroundRetake },
    { "ForegroundRight", func_ForegroundRight },
    { "ForegroundStartup", func_ForegroundStartup },
    { "GetClassInfo", func_GetClassInfo },
    { "GetDCEx", func_GetDCEx },
    { "GetProp", func_GetProp },
    { "WindowRegionDC", func_WindowRegionDC },
    { "GetIconInfo", func_GetIconInfo },
    { "GetKeyState", func_GetKeyState },
    { "GetMessageTime", func_GetMessageTime },
    { "GetPeekMessage", func_GetPeekMessage },
    { "GetPointerDevices", func_GetPointerDevices },
    { "GetSetWindowInt", func_GetSetWindowInt },
    { "GetSystemMetrics", func_GetSystemMetrics },
    { "GetTitleBarInfoEx", func_GetTitleBarInfoEx },
    { "GetUserObjectInformation", func_GetUserObjectInformation },
    { "GetWindowPlacement", func_GetWindowPlacement },
    { "GhostWindow", func_GhostWindow },
    { "GW_ENABLEDPOPUP", func_GW_ENABLEDPOPUP },
    { "ImeWindow", func_ImeWindow },
    { "InitializeLpkHooks", func_InitializeLpkHooks },
    { "InputMessageSource", func_InputMessageSource },
    { "InvalidHandles", func_InvalidHandles },
    { "IsTopLevelWindow", func_IsTopLevelWindow },
    { "IsServerSideWindow", func_IsServerSideWindow },
    { "JobUiWindowAccess", func_JobUiWindowAccess },
    { "KbdLayout", func_KbdLayout },
    { "LargeMessageBuffers", func_LargeMessageBuffers },
    { "keybd_event", func_keybd_event },
    { "LoadImage", func_LoadImage },
    { "LoadImageGCC", func_LoadImageGCC },
    { "LookupIconIdFromDirectoryEx", func_LookupIconIdFromDirectoryEx },
    { "MenuUI", func_MenuUI },
    { "MenuItemData", func_MenuItemData },
    { "MessageStateAnalyzer", func_MessageStateAnalyzer },
    { "MinimizedPosition", func_MinimizedPosition },
    { "ModernUser32", func_ModernUser32 },
    { "NestedActivation", func_NestedActivation },
    { "NestedWindowPos", func_NestedWindowPos },
    { "NcPaintRgn", func_NcPaintRgn },
    { "NextDlgItem", func_NextDlgItem },
    { "NonClientPaint", func_NonClientPaint },
    { "NonClientUpdate", func_NonClientUpdate },
    { "OwnerDestroy", func_OwnerDestroy },
    { "PersianLayout", func_PersianLayout },
    { "PhysicalPoint", func_PhysicalPoint },
    { "PhysicalToLogicalPoint", func_PhysicalToLogicalPoint },
    { "LogicalToPhysicalPoint", func_LogicalToPhysicalPoint },
    { "PrivateExtractIcons", func_PrivateExtractIcons },
    { "RealGetWindowClass", func_RealGetWindowClass },
    { "RedrawWindow", func_RedrawWindow },
    { "RegisterHotKey", func_RegisterHotKey },
    { "RegisterClassEx", func_RegisterClassEx },
    { "ScrollBarRedraw", func_ScrollBarRedraw },
    { "ScrollBarWndExtra", func_ScrollBarWndExtra },
    { "ScrollDC", func_ScrollDC },
    { "ScrollWindowEx", func_ScrollWindowEx },
    { "UnhookWinEvent", func_UnhookWinEvent },
    { "SendMessageTimeout", func_SendMessageTimeout },
    { "SendMessageTimeoutHung", func_SendMessageTimeoutHung },
    { "SetActiveWindow", func_SetActiveWindow },
    { "SetCursorPos", func_SetCursorPos },
    { "SetFocus", func_SetFocus },
    { "SetParent", func_SetParent },
    { "SetProp", func_SetProp },
    { "SetScrollInfo", func_SetScrollInfo },
    { "SetScrollRange", func_SetScrollRange },
    { "SetWindowPlacement", func_SetWindowPlacement },
    { "WindowDisplayAffinity", func_WindowDisplayAffinity },
    { "WindowRegionFrame", func_WindowRegionFrame },
    { "ShellWindowQueries", func_ShellWindowQueries },
    { "ShowWindow", func_ShowWindow },
    { "SwitchToThisWindow", func_SwitchToThisWindow },
    { "SystemMenu", func_SystemMenu },
    { "SystemParametersInfo", func_SystemParametersInfo },
    { "TopmostBand", func_TopmostBand },
    { "TrackMouseEvent", func_TrackMouseEvent },
    { "TrackPopupMenuEx", func_TrackPopupMenuEx },
    { "UncoveredWindow", func_UncoveredWindow },
    { "UpdateRegion", func_UpdateRegion },
    { "UserHandleGrantAccess", func_UserHandleGrantAccess },
    { "VirtualKey", func_VirtualKey },
    { "WindowBand", func_WindowBand },
    { "CallWindowProc", func_CallWindowProc },
    { "WndProc", func_WndProc },
    { "wsprintfApi", func_wsprintfApi },
    { 0, 0 }
};
