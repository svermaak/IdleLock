#include "stdafx.h"
#include "IdleLock.h"

#include "AboutBox.h"


BOOL CALLBACK AboutDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
void CenterDialog(HWND hWnd);


void ShowAboutBox(HINSTANCE hInst, HWND hWnd) {
    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutDlgProc);
}


BOOL CALLBACK AboutDlgProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message) {
        case WM_INITDIALOG:
            CenterDialog(hWnd);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    EndDialog(hWnd, IDOK);
                    break;
                case IDCANCEL:
                    EndDialog(hWnd, IDCANCEL);
                    break;
            }
            break;
        default:
            return FALSE;
    }
    return TRUE;
}


void CenterDialog(HWND hWnd) 
{
    RECT dlgRc;
    RECT desktopRc;
    HWND desktop = GetDesktopWindow();
    GetWindowRect(desktop, &desktopRc);
    GetWindowRect(hWnd, &dlgRc);

    int x = (desktopRc.right - dlgRc.right) / 2;
    int y = (desktopRc.bottom - dlgRc.bottom) / 2;

    SetWindowPos(hWnd,
                 HWND_TOP,
                 (desktopRc.right - dlgRc.right) / 2,
                 (desktopRc.bottom - dlgRc.bottom) / 2,
                 0, 0, SWP_NOSIZE);
}
