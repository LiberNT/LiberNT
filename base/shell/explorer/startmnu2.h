#pragma once

typedef struct _SM2_FLYOUT_PALETTE
{
    COLORREF PanelBg;
    COLORREF PanelText;
    COLORREF DimText;
    COLORREF HotFill;
    COLORREF HotBorder;
    COLORREF Border;
    COLORREF AccentBg;
    COLORREF AccentText;
} SM2_FLYOUT_PALETTE;

enum SM2SHUTCMD
{
    SM2F_SHUTDOWN = 1,
    SM2F_RESTART,
    SM2F_LOGOFF,
    SM2F_LOCK,
    SM2F_SLEEP,
    SM2F_HIBERNATE
};

VOID StartMenu2_GetFlyoutPalette(OUT SM2_FLYOUT_PALETTE *pPal);
VOID StartMenu2_DoPowerCommand(IN HWND hwndOwner, IN INT nCmd);
BOOL StartMenu2_IsPowerCommandAllowed(IN INT nCmd);

HRESULT StartMenu2_Create(IN ITrayWindow *Tray, IN HWND hwndTray);
HRESULT StartMenu2_Popup(IN const RECT *prcStartBtn, IN UINT uPosition);
VOID StartMenu2_Hide(VOID);
BOOL StartMenu2_IsVisible(VOID);
VOID StartMenu2_RecordUsage(IN HWND hWnd);
VOID StartMenu2_Destroy(VOID);
