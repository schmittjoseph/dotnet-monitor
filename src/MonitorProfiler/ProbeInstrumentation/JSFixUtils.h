#pragma once

// JSFIX: Remove file

#if TARGET_WINDOWS
#define _BREAK_() __debugbreak()
#else
#include <signal.h>
#define _BREAK_() raise(SIGTRAP)
#endif

#ifdef DEBUG
#define FEATURE_USAGE_GUARD() _BREAK_()
#define TEMPORARY_BREAK_ON_ERROR() _BREAK_()
#else
#define FEATURE_USAGE_GUARD() 
#define TEMPORARY_BREAK_ON_ERROR()
#endif

#define JSFIX_IfFailBreakAndRet_(EXPR) \
    do { \
        hr = (EXPR); \
        if(FAILED(hr)) { \
            TEMPORARY_BREAK_ON_ERROR(); \
            return (hr); \
        } \
    } while (0)

#define JSFIX_IfFailLogAndBreak_(pLogger, EXPR) \
    do { \
        hr = (EXPR); \
        if(FAILED(hr)) { \
            if (nullptr != pLogger) { \
                if (pLogger->IsEnabled(LogLevel::Error)) \
                { \
                    pLogger->Log(\
                        LogLevel::Error, \
                        _LS("IfFailLogRet(" #EXPR ") failed in function %s: 0x%08x"), \
                        __func__, \
                        hr); \
                } \
            } \
            TEMPORARY_BREAK_ON_ERROR(); \
            return (hr); \
        } \
    } while (0)

#undef IfFailRet
#define IfFailRet(EXPR) JSFIX_IfFailBreakAndRet_(EXPR)