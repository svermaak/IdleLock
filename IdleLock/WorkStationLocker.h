#pragma once


#include "stdafx.h"
#include "wtsapi32.h"


class TWorkStationLocker
{
public:
    static const int DefaultTimeout = 20 * 60000;

    TWorkStationLocker(HWND hWnd);
    ~TWorkStationLocker();

    void LockIfIdleTimeout();

    void ReportLock()
    {
        isLocked = true;
    }

    void ReportUnlock()
    {
        isLocked = false;
        unlockedTick = GetTickCount();
        screenSaverActiveAt = 0L;
    }

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

    HWND  hMsgTargetWnd;
    int   idleTimeout = DefaultTimeout;
    bool  requireScreenSaver;
    bool  enabled;
    bool  isLocked = false;
    DWORD screenSaverActiveAt = 0;  // The idleTime at which the screensaver was seen as active.
    DWORD unlockedTick = 0;  // The tick count at which the computer was unlocked.
};
