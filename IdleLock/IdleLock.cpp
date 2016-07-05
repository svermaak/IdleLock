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
#include "wtsapi32.h"
#include "AboutBox.h"
#include "IdleLock.h"

#include "Logger.h"
#include "WorkStationLocker.h"

// -----------------------------------------------------------------------------

static const int CheckTimeoutInterval = 30000;  // How often we check to see if it's time to lock.
static const int TrayIconUId = 100;

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
TWorkStationLocker *WorkStationLocker = NULL;
TLogger            *Logger = NULL;
double              IconScaling;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
HMENU               CreateIdleLockMenu();
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
double              GetIconScaling(HWND hWnd);
void                UpdateTrayIcon(TWorkStationLocker &workStationLocker);
HICON               LoadTrayIcon(HINSTANCE hInstance, int resourceId);



int APIENTRY _tWinMain(HINSTANCE hInstance,
                       HINSTANCE hPrevInstance,
                       LPTSTR    lpCmdLine,
                       int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    int argc;
    LPWSTR *argv = CommandLineToArgvW(lpCmdLine, &argc);
    if (argc == 2 && lstrcmpiW(argv[0], L"-logfile") == 0) {
        Logger = new TLogger(argv[1]);
    } else {
        Logger = new TLogger(); 
    }

    MSG msg;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szAppTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_IDLELOCK, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    {

        TWorkStationLocker wl(nidApp.hWnd, *Logger);
        WorkStationLocker = &wl;
        UpdateTrayIcon(*WorkStationLocker);

        // Main message loop:
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    delete Logger;

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
    nidApp.hIcon            = LoadTrayIcon(hInstance, IDI_IDLELOCK); 
    nidApp.hWnd             = (HWND) hWnd;     // The window which will process this apps messages.
    nidApp.uID              = TrayIconUId;
    nidApp.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nidApp.uCallbackMessage = WM_USER_SHELLICON; 
    wcscpy_s(nidApp.szTip, szAppTitle);
    Shell_NotifyIcon(NIM_ADD, &nidApp); 

    IconScaling = GetIconScaling(hWnd);

    hPopMenu = CreateIdleLockMenu();
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
        swprintf_s(nidApp.szTip, sizeof nidApp.szTip, L"%s - Disabled", szAppTitle);

    nidApp.hIcon = LoadTrayIcon(hInstance, workStationLocker.Enabled() ? IDI_IDLELOCK : IDI_IDLELOCKOPEN);
    nidApp.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &nidApp);
}


HICON LoadTrayIcon(HINSTANCE hInstance, int resourceId)
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

        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_UNLOCK) {
                WorkStationLocker->ReportUnlock();
            } else if (wParam == WTS_SESSION_LOCK) {
                WorkStationLocker->ReportLock();
            }
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
