// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#if TARGET_WINDOWS
#include <processenv.h>
#else
#include <mutex>
#include <cstdlib>
#endif

class EnvironmentBlockUtilities
{
    private:
        static std::mutex _getEnvMutex;

    public:
        static HRESULT IsStartupSwitchSet(const char* name, BOOL& isSet)
        {
 #if TARGET_WINDOWS
            DWORD retValue = GetEnvironmentVariableW(name.c_str(), buffer, bufferSize);
            if (retValue == 0)
            {
                DWORD dwLastError = GetLastError();
                if (dwLastError == ERROR_ENVVAR_NOT_FOUND)
                {
                    if (buffer != NULL && bufferSize != 0)
                    {
                        *buffer = 0;
                    }
                    return S_FALSE;
                }
                else
                {
                    return HRESULT_FROM_WIN32(dwLastError);
                }
            }
            else if (retValue > bufferSize)
            {
                return E_INVALIDARG;
            }
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