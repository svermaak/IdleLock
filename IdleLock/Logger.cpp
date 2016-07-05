#include "stdafx.h"
#include <time.h>

#include "Logger.h"


TLogger::TLogger(const wchar_t *fName)
{
    logFile.open(fName, ios::app);
    Log(L"Logging started.");
}


TLogger::~TLogger()
{
    if (logFile.is_open())
        Log(L"Closing log.");
        logFile.close();
}


void TLogger::Log(wchar_t *text)
{
    if (!logFile.is_open())
        return;

    wchar_t buf[1000];
    time_t timer;
    tm tmStruct;

    time(&timer);
    localtime_s(&tmStruct, &timer);

    wcsftime(buf, 500, L"%Y-%m-%d %H:%M:%S  ", &tmStruct);
    wcscat_s(buf, text);

    logFile << buf << endl;
    logFile.flush();
}
