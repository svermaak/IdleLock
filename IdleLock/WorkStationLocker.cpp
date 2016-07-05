#include "stdafx.h"
#include "WorkStationLocker.h"


static const wchar_t *AppRegKeyName = L"Software\\Wezeku\\IdleLock";


TWorkStationLocker::TWorkStationLocker(HWND hWnd, TLogger &logger) : hMsgTargetWnd(hWnd), Logger(logger)
{
    ReadSettings();
    WTSRegisterSessionNotification(hMsgTargetWnd, NOTIFY_FOR_THIS_SESSION);
}


TWorkStationLocker::~TWorkStationLocker()
{
    WTSUnRegisterSessionNotification(hMsgTargetWnd);
}


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
    DWORD lastUserActionTick = lastInputInfo.dwTime < 0 && unlockedTick >= 0
        ? lastInputInfo.dwTime  // if wraparound
        : max(lastInputInfo.dwTime, unlockedTick);


    DWORD systemUpticks = GetTickCount();
    DWORD idleTime = lastUserActionTick <= systemUpticks
        ? systemUpticks - lastUserActionTick
        : (ULONG_MAX - lastUserActionTick) + 1 + systemUpticks;

    // Indicate that screensaver has been started.
    // If the monitor goes into power save mode, ScreenSaverRunning() will
    // return false, so we need to remember that the screensaver was actually 
    // activated at some point.
    if (screenSaverActiveAt == 0 && ScreenSaverRunning()) {
        Logger.Log(L"Screensaver start detected.");
        screenSaverActiveAt = idleTime;
    }
    // If there have been events since the last ativation of the screensaver,
    // we're no longer in screensaver/moniton power save mode, so clear
    // screensaver status.
    else if (idleTime < screenSaverActiveAt)
        screenSaverActiveAt = 0;

    // Lock if timeout, but never sooner than after 60 sec as a safeguard.
    if (idleTime > 60000 && idleTime >= (DWORD)idleTimeout
        && (!IsScreenSaverRequired() || screenSaverActiveAt != 0)
        && !isLocked) {  // If the wrkstn is already locked, Win7 sometimes cancels the screensaver.
        LockWorkStation();
    }
}


void TWorkStationLocker::ReadSettings()
{
    HKEY hKey;
    DWORD regData;
    DWORD dataLen = sizeof regData;

    RegCreateKeyEx(HKEY_CURRENT_USER, AppRegKeyName, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);

    idleTimeout = (RegQueryValueEx(hKey, L"LockTimeout", NULL, NULL, (LPBYTE)&regData, &dataLen) == ERROR_SUCCESS)
        ? regData
        : DefaultTimeout;

    requireScreenSaver = (RegQueryValueEx(hKey, L"RequireScreenSaver", NULL, NULL, (LPBYTE)&regData, &dataLen) == ERROR_SUCCESS)
        ? (regData != 0)
        : true;

    enabled = (RegQueryValueEx(hKey, L"Enabled", NULL, NULL, (LPBYTE)&regData, &dataLen) == ERROR_SUCCESS)
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
    RegSetValueEx(hKey, L"LockTimeout", 0, REG_DWORD, (BYTE *)&regData, dataLen);
    regData = requireScreenSaver;
    RegSetValueEx(hKey, L"RequireScreenSaver", 0, REG_DWORD, (BYTE *)&regData, dataLen);
    regData = enabled;
    RegSetValueEx(hKey, L"Enabled", 0, REG_DWORD, (BYTE *)&regData, dataLen);
}


bool TWorkStationLocker::ScreenSaverRunning()
{
    BOOL bSaver;
    SystemParametersInfo(SPI_GETSCREENSAVERRUNNING, 0, &bSaver, 0);
    return bSaver == TRUE;
}

