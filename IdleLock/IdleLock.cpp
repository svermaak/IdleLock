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

#include "IdleLock.h"

// -----------------------------------------------------------------------------

static const int CheckTimeoutInterval = 30000;  // How often we check to see if it's time to lock.
static const int DefaultTimeout = 20*60000;
static const wchar_t *AppRegKeyName = _T("Software\\Wezeku\\IdleLock");
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

    idleTimeout = (RegQueryValueEx(hKey, _T("LockTimeout"), NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
        ? regData
        : DefaultTimeout;

    requireScreenSaver = (RegQueryValueEx(hKey, _T("RequireScreenSaver"), NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
        ? (regData != 0)
        : true;

    enabled = (RegQueryValueEx(hKey, _T("Enabled"), NULL, NULL, (LPBYTE) &regData, &dataLen) == ERROR_SUCCESS)
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
    RegSetValueEx(hKey, _T("LockTimeout"), 0, REG_DWORD, (BYTE *) &regData, dataLen);
    regData = requireScreenSaver;
    RegSetValueEx(hKey, _T("RequireScreenSaver"), 0, REG_DWORD, (BYTE *) &regData, dataLen);
    regData = enabled;
    RegSetValueEx(hKey, _T("Enabled"), 0, REG_DWORD, (BYTE *) &regData, dataLen);
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
#define	WM_USER_SHELLICON WM_USER + 1

NOTIFYICONDATA nidApp;
TCHAR szTitle[MAX_LOADSTRING];
TCHAR szWindowClass[MAX_LOADSTRING];
TCHAR szApplicationToolTip[MAX_LOADSTRING];
HMENU hPopMenu = NULL;
HINSTANCE hInstance = NULL;
TWorkStationLocker *WorkStationLocker;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
HMENU               CreateIdleLockMenu();
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void UpdateTrayIcon(TWorkStationLocker &workStationLocker);



int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR    lpCmdLine,
                       int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    MSG msg;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
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


//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE aHInstance, int nCmdShow)
{
    hInstance = aHInstance;

    HWND hWnd;
    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
       CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
    
    if (!hWnd) {
        return FALSE;
    }
    
    nidApp.cbSize           = sizeof(NOTIFYICONDATA);
    nidApp.hIcon            = LoadIcon(hInstance, (LPCTSTR)MAKEINTRESOURCE(IDI_IDLELOCK)); 
    nidApp.hWnd             = (HWND) hWnd;     // The window which will process this apps messages.
    nidApp.uID              = TrayIconUId;
    nidApp.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nidApp.uCallbackMessage = WM_USER_SHELLICON; 
    wcscpy_s(nidApp.szTip, _T("IdleLock"));
    Shell_NotifyIcon(NIM_ADD, &nidApp); 
    
    hPopMenu = CreateIdleLockMenu();
    WorkStationLocker = new TWorkStationLocker();
    UpdateTrayIcon(*WorkStationLocker);
    // Send us WM_TIMER messages every <CheckTimeoutInterval> seconds.
    SetTimer(hWnd, 1, CheckTimeoutInterval, NULL);
    
    return TRUE;
}


void UpdateTrayIcon(TWorkStationLocker &workStationLocker)
{
    int resId = workStationLocker.Enabled() ? IDI_IDLELOCK : IDI_IDLELOCKOPEN;
    nidApp.hIcon = LoadIcon(hInstance, (LPCTSTR)MAKEINTRESOURCE(resId));
    nidApp.uFlags = NIF_ICON;
    Shell_NotifyIcon(NIM_MODIFY, &nidApp);
}


HMENU CreateIdleLockMenu()
{
    wchar_t menuItem[50];
    HMENU menu = CreatePopupMenu();

    AppendMenu(menu, 0, 0, _T("Lock when the user has been inactive for:"));

    // Add menu entries for 5..60 minutes lock timeout.
    for (int i = 5; i <= 60; i += 5) {
        wsprintf(menuItem, _T("%d minutes"), i);
        AppendMenu(menu, 0, IDM_TIMEOUT + i, menuItem);
    }

    AppendMenu(menu, MF_SEPARATOR, 0, _T("-"));				
    AppendMenu(menu, 0, IDM_REQUIRESCREENSAVER, _T("&Only lock if screensaver is active"));
    AppendMenu(menu, 0, IDM_DISABLE, _T("&Disable"));
    AppendMenu(menu, 0, IDM_EXIT, _T("E&xit"));

    return menu;
}

//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
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

// -----------------------------------------------------------------------------
