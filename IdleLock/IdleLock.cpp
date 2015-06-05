// IdleLock.cpp
// Locks the workstation after a specified amount of idle time.
// The user selects the time by clicking the tray icon. 
// If "Only lock if screensaver is active" is selected,
// the screensaver timeout must be shorter than the "Turn off display"
// timeout. Otherwise, the system (Vista and later) will never report that
// the screensaver has been activated, and according to Stackoverflow.com
// it's not possible to reliably detect that the monitor has been turned off
// by the system.
//


#include "stdafx.h"
#include "AboutBox.h"
#include "IdleLock.h"

// Needs CommCtrl v6 for LoadIconMetric().
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include "Commctrl.h"

// -----------------------------------------------------------------------------

static const int CheckTimeoutInterval = 30000;  // How often we check to see if it's time to lock.
static const int DefaultTimeout = 20*60000;
static const wchar_t *AppRegKeyName = L"Software\\Wezeku\\IdleLock";
static const int TrayIconUId = 100;


class TWorkStationLocker
{
public:
    TWorkStationLocker() : idleTimeout() 
    {
        ReadSettings();
    }

    void LockIfIdleTimeout();

    void SetTimeout(int aIdleTimeout)
    {
        idleTimeout = aIdleTimeout;
        WriteSettings();
    }

    int GetTimeout()
    {
        return idleTimeout;
    }

    void RequireScreensaver(bool require)
    {
        requireScreenSaver = require;
        WriteSettings();
    }

    bool IsScreenSaverRequired()
    {
        return requireScreenSaver;
    }

    bool Enabled()
    {
        return enabled;
    }

    void Enable(bool aEnable)
    {
        enabled = aEnable;
        WriteSettings();
    }

protected:
    void  ReadSettings();
    void  WriteSettings();
    bool  ScreenSaverRunning();
          
    int   idleTimeout;
    bool  requireScreenSaver;
    bool  enabled;
    DWORD screenSaverActiveAt;  // The idleTime at which the screensaver was seen as active.
};


void TWorkStationLocker::LockIfIdleTimeout()
{
    if (!enabled)
        return;

    LASTINPUTINFO lastInputInfo;
    lastInputInfo.cbSize = sizeof lastInputInfo;

    if (!GetLastInputInfo(&lastInputInfo))
        throw "Error calling GetLastInputInfo.";

    // Get idle time, with a check for systemUpticks integer wraparound
    // (which occurs after about 48.8 days).
    DWORD systemUpticks = GetTickCount();
    DWORD idleTime = lastInputInfo.dwTime <= systemUpticks 
            ? systemUpticks - lastInputInfo.dwTime
            : (ULONG_MAX - lastInputInfo.dwTime) + 1 + systemUpticks;

    // Indicate that screensaver has been started.
    // If the monitor goes into power save mode, ScreenSaverRunning() will
    // return false, so we need to remember that the screensaver was actually 
    // activated at some point.
    if (screenSaverActiveAt == 0 && ScreenSaverRunning())
        screenSaverActiveAt = idleTime;
    // If there have been events since the last ativation of the screensaver,
    // we're no longer in screensaver/moniton power save mode, so clear
    // screensaver status.
    else if (idleTime < screenSaverActiveAt)
        screenSaverActiveAt = 0;

    // Lock if timeout, but never sooner than after 60 sec as a safeguard.
    if (idleTime > 60000 && idleTime >= (DWORD)idleTimeout
      && (!IsScreenSaverRequired() || screenSaverActiveAt != 0))
        LockWorkStation();  // If the wrkstn is already locked, nothing happens.
}


void TWorkStationLocker::ReadSettings()
{
    HKEY hKey;
    DWORD regData;
    DWORD dataLen = sizeof regData;

    RegCreateKeyEx(HKEY_CURRENT_USER, AppRegKeyName, 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);

    idleTimeout = (RegQueryValueEx(hKey, L"LockTimeout", NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
        ? regData
        : DefaultTimeout;

    requireScreenSaver = (RegQueryValueEx(hKey, L"RequireScreenSaver", NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
        ? (regData != 0)
        : true;

    enabled = (RegQueryValueEx(hKey, L"Enabled", NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
        ? (regData != 0)
        : true;
}


void TWorkStationLocker::WriteSettings()
{
    HKEY hKey;
    DWORD regData;
    DWORD dataLen = sizeof regData;

    RegCreateKeyEx(HKEY_CURRENT_USER, AppRegKeyName, 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);

    regData = idleTimeout;
    RegSetValueEx(hKey, L"LockTimeout", 0, REG_DWORD, (BYTE *) &regData, dataLen);
    regData = requireScreenSaver;
    RegSetValueEx(hKey, L"RequireScreenSaver", 0, REG_DWORD, (BYTE *) &regData, dataLen);
    regData = enabled;
    RegSetValueEx(hKey, L"Enabled", 0, REG_DWORD, (BYTE *) &regData, dataLen);
}


bool TWorkStationLocker::ScreenSaverRunning()
{
    BOOL bSaver;
    SystemParametersInfo (SPI_GETSCREENSAVERRUNNING, 0, &bSaver, 0);
    return bSaver == TRUE;
}


// -----------------------------------------------------------------------------
// Win32 API bare metal stuff.
// -----------------------------------------------------------------------------

#define MAX_LOADSTRING 100
#define WM_USER_SHELLICON WM_USER + 1

NOTIFYICONDATA      nidApp;
TCHAR               szAppTitle[MAX_LOADSTRING];
TCHAR               szWindowClass[MAX_LOADSTRING];
HMENU               hPopMenu = NULL;
HINSTANCE           hInstance = NULL;
TWorkStationLocker *WorkStationLocker;
double              IconScaling;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
HMENU               CreateIdleLockMenu();
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
double              GetIconScaling(HWND hWnd);
void                UpdateTrayIcon(TWorkStationLocker &workStationLocker);
HICON               LoadTrayIcon(int resourceId);
BOOL                IsWinVistaOrLater();



int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR    lpCmdLine,
                       int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    MSG msg;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szAppTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_IDLELOCK, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}


ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IDLELOCK));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_IDLELOCK);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_IDLELOCK));

    return RegisterClassEx(&wcex);
}


// Saves instance handle and creates main window.
BOOL InitInstance(HINSTANCE aHInstance, int nCmdShow)
{
    hInstance = aHInstance;

    HWND hWnd;
    hWnd = CreateWindow(szWindowClass, szAppTitle, WS_OVERLAPPEDWINDOW,
       CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        return FALSE;
    }
    
    nidApp.cbSize           = sizeof NOTIFYICONDATA;
    nidApp.hIcon            = LoadTrayIcon(IDI_IDLELOCK); 
    nidApp.hWnd             = (HWND) hWnd;     // The window which will process this apps messages.
    nidApp.uID              = TrayIconUId;
    nidApp.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nidApp.uCallbackMessage = WM_USER_SHELLICON; 
    wcscpy_s(nidApp.szTip, szAppTitle);
    Shell_NotifyIcon(NIM_ADD, &nidApp); 

    IconScaling = GetIconScaling(hWnd);

    hPopMenu = CreateIdleLockMenu();
    WorkStationLocker = new TWorkStationLocker();
    UpdateTrayIcon(*WorkStationLocker);
    // Send us WM_TIMER messages every <CheckTimeoutInterval> seconds.
    SetTimer(hWnd, 1, CheckTimeoutInterval, NULL);
    
    return TRUE;
}


double GetIconScaling(HWND hWnd)
{
    RECT trayIconRect;
    NOTIFYICONIDENTIFIER niIdent;
    niIdent.cbSize = sizeof NOTIFYICONIDENTIFIER;
    niIdent.hWnd = hWnd;
    niIdent.uID = TrayIconUId;
    niIdent.guidItem = GUID_NULL;
    Shell_NotifyIconGetRect(&niIdent, &trayIconRect);  // Only Win7+.

    int iconRectWidth = trayIconRect.right - trayIconRect.left;
    // When DPI scaling is active, SM_CXSMICON is always 16 
    // (https://msdn.microsoft.com/en-us/library/ms701681%28v=vs.85%29.aspx).
    // If the icon is 16 in reality (96 dpi true), iconRectWidth is 24.
    return GetSystemMetrics(SM_CXSMICON) == 16 ?
        IconScaling = iconRectWidth / 24.
        : IconScaling = 1.;
}


void UpdateTrayIcon(TWorkStationLocker &workStationLocker)
{
    if (workStationLocker.Enabled())
        swprintf_s(nidApp.szTip, sizeof nidApp.szTip, L"%s - %d minutes", szAppTitle, workStationLocker.GetTimeout() / 60000);
    else
        swprintf_s(nidApp.szTip, sizeof nidApp.szTip, L"%s - Disabled", szAppTitle, workStationLocker.GetTimeout() / 60000);

    nidApp.hIcon = LoadTrayIcon(workStationLocker.Enabled() ? IDI_IDLELOCK : IDI_IDLELOCKOPEN);
    nidApp.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &nidApp);
}


HICON LoadTrayIcon(int resourceId)
{
    LPCTSTR resId = (LPCTSTR) MAKEINTRESOURCE(resourceId);
    HICON icon;

    int cx = int(GetSystemMetrics(SM_CXSMICON) * IconScaling);
    int cy = int(GetSystemMetrics(SM_CYSMICON) * IconScaling);
    icon = (HICON) LoadImage(hInstance, resId, IMAGE_ICON, cx, cy, 0);

    return icon;
}


HMENU CreateIdleLockMenu()
{
    wchar_t menuItem[50];
    HMENU menu = CreatePopupMenu();

    AppendMenu(menu, 0, 0, L"Lock when the user has been inactive for:");

    // Add menu entries for 5..60 minutes lock timeout.
    for (int i = 5; i <= 60; i += 5) {
        wsprintf(menuItem, L"%d minutes", i);
        AppendMenu(menu, 0, IDM_TIMEOUT + i, menuItem);
    }

    AppendMenu(menu, MF_SEPARATOR, 0, L"-");				
    AppendMenu(menu, 0, IDM_REQUIRESCREENSAVER, L"&Only lock if screensaver is active");
    AppendMenu(menu, 0, IDM_DISABLE, L"&Disable");
    AppendMenu(menu, 0, IDM_ABOUT, L"&About");
    AppendMenu(menu, MF_SEPARATOR, 0, L"-");
    AppendMenu(menu, 0, IDM_EXIT, L"E&xit");

    return menu;
}

// WndProc for the main window.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;

    switch (message) {
        case WM_USER_SHELLICON: 
            // Systray msg.
            switch(LOWORD(lParam)) {   
                case WM_LBUTTONDOWN: 
                case WM_RBUTTONDOWN: 
                    POINT curPos;
                    GetCursorPos(&curPos);
                    SetForegroundWindow(hWnd);

                    int checkedItem = WorkStationLocker->GetTimeout() / 60000 + IDM_TIMEOUT;
                    CheckMenuItem(hPopMenu, checkedItem, MF_BYCOMMAND | MF_CHECKED);
                    CheckMenuItem(hPopMenu, IDM_REQUIRESCREENSAVER, MF_BYCOMMAND | (WorkStationLocker->IsScreenSaverRequired() ? MF_CHECKED : MF_UNCHECKED));
                    CheckMenuItem(hPopMenu, IDM_DISABLE, MF_BYCOMMAND | (WorkStationLocker->Enabled() ? MF_UNCHECKED : MF_CHECKED));

                    TrackPopupMenu(hPopMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, curPos.x, curPos.y, 0, hWnd, NULL);
                    return TRUE; 
                }
            break;

        case WM_COMMAND:
            wmId    = LOWORD(wParam);
            wmEvent = HIWORD(wParam);
            // Parse the menu selections:
            switch (wmId) {
                case IDM_REQUIRESCREENSAVER:
                    WorkStationLocker->RequireScreensaver(!WorkStationLocker->IsScreenSaverRequired());
                    break;

                case IDM_DISABLE:
                    WorkStationLocker->Enable(!WorkStationLocker->Enabled());
                    UpdateTrayIcon(*WorkStationLocker);
                    break;

                case IDM_ABOUT:
                    ShowAboutBox(hInstance, hWnd);
                    break;

                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;

                default:
                    if (wmId > IDM_TIMEOUT && wmId < IDM_TIMEOUT + 1000) {
                        // Uncheck old selection
                        int checkedItem = WorkStationLocker->GetTimeout() / 60000 + IDM_TIMEOUT;
                        CheckMenuItem(hPopMenu, checkedItem, MF_BYCOMMAND | MF_UNCHECKED);

                        int timeout = (wmId - IDM_TIMEOUT) * 60000;
                        WorkStationLocker->SetTimeout(timeout);
                        UpdateTrayIcon(*WorkStationLocker);
                    }

                    return DefWindowProc(hWnd, message, wParam, lParam);
                }
            break;

        case WM_TIMER:
            WorkStationLocker->LockIfIdleTimeout();
            break;

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nidApp); 
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


BOOL IsWinVersionOrLater(DWORD majorVersion, DWORD minorVersion)
{
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = majorVersion;
    osvi.dwMinorVersion = minorVersion;

    DWORDLONG dwlConditionMask = 0;
    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

    return VerifyVersionInfo(&osvi,
        VER_MAJORVERSION | VER_MINORVERSION,
        dwlConditionMask);
}


BOOL IsWinVistaOrLater()
{
    return IsWinVersionOrLater(6, 0);
}

BOOL IsWin7OrLater()
{
    return IsWinVersionOrLater(6, 1);
}
