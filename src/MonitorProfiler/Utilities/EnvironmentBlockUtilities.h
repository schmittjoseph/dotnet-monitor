// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "StringUtilities.h"
#include <tstring.h>

#if TARGET_WINDOWS
#include <processenv.h>
#else
#include <mutex>
#endif

class EnvironmentBlockUtilities
{
    private:
#if !TARGET_WINDOWS
        static std::mutex _getEnvMutex;
#endif

    public:
        static HRESULT IsStartupSwitchSet(tstring name, BOOL& isSet)
        {
            #define ENABLED_ENV_VAR_VALUE _T("1")

            HRESULT hr;
            WCHAR buffer[sizeof(ENABLED_ENV_VAR_VALUE)/sizeof(WCHAR)];

            tstring enabledString = tstring(ENABLED_ENV_VAR_VALUE);
            isSet = FALSE;

            hr = GetStartupEnvironmentVariable(name, buffer, sizeof(ENABLED_ENV_VAR_VALUE)/sizeof(WCHAR));
            if (hr == S_FALSE)
            {
                return S_OK;
            }
            else if (hr != S_OK)
            {
                return hr;
            }

            tstring valueString = tstring(buffer);
            if (enabledString == valueString)
            {
                isSet = TRUE;
            }

            return S_OK;
        }

    private:
        static HRESULT GetStartupEnvironmentVariable(tstring name, WCHAR *buffer, DWORD bufferSize)
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
/*
            HRESULT hr;
            // ref: IEEE Std 1003.1
            // "The return value from getenv() may point to static data which may be overwritten by subsequent calls to getenv() [...]"
            std::lock_guard<std::mutex> lock(_getEnvMutex);

            char *ret = getenv(name.c_str());
            if (ret == NULL)
            {
                value.clear();
                return S_FALSE;
            }

            IfFailRet(StringUtilities::Copy(buffer,value.data(), value.size(), ret));
            */
#endif

            return S_OK;
        }
};