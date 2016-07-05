#pragma once

#include <fstream>
#include <iostream>

using namespace std;


class TLogger
{
public:
    TLogger() {}  // ctor for dummy logger.
    TLogger(const wchar_t *fName);
    ~TLogger();

    void Log(wchar_t *text);

private:
    wofstream &logFile = wofstream();
};
