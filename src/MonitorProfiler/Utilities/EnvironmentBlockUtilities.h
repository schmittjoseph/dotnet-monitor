// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#if TARGET_WINDOWS
#include <processenv.h>
#include "tstring.h"
#include <iostream>
#else
#include <mutex>
#include <cstdlib>
#endif

class EnvironmentBlockUtilities
{
    private:
#if !TARGET_WINDOWS
        static std::mutex _getEnvMutex;
#endif

    public:
        static HRESULT IsStartupSwitchSet(const char* name, BOOL& isSet)
        {
 #if TARGET_WINDOWS
            const WCHAR EnabledValue = L'1';

            isSet = FALSE;
            tstring tName = to_tstring(name);

            const DWORD bufferSize = 2;
            WCHAR buffer[bufferSize];

            DWORD retValue = GetEnvironmentVariableW(tName.c_str(), buffer, bufferSize);
            if (retValue == 0)
            {
                DWORD dwLastError = GetLastError();
                if (dwLastError == ERROR_ENVVAR_NOT_FOUND)
                {
                    return S_OK;
                }
                else
                {
                    return HRESULT_FROM_WIN32(dwLastError);
                }
            }
            else if (retValue > bufferSize)
            {
                return S_OK;
            }

            isSet = (buffer[0] == EnabledValue);
#else
            const char EnabledValue = '1';

            // After C++ 11, getenv is thread-safe so long as the environment is not modified after the program has started (stenv, )
            // ref: IEEE Std 1003.1
            // "The return value from getenv() may point to static data which may be overwritten by subsequent calls to getenv() [...]"
            std::lock_guard<std::mutex> lock(_getEnvMutex);

            char *ret = std::getenv(name);
            isSet = (ret != nullptr && ret[0] == EnabledValue && ret[1] == '\0');
#endif

            return S_OK;
        }
};