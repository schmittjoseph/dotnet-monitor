// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "cor.h"
#include "corprof.h"
#include "com.h"
#include "tstring.h"
#include "../Logging/Logger.h"

#include <unordered_map>
#include <list>
#include <forward_list>

class Snapshot
{
    private:
        std::shared_ptr<std::list<FunctionID>> m_pPartialStack;
        std::unordered_map<ThreadID, std::shared_ptr<std::forward_list<FunctionID>>> m_pShadowStacks;

        ComPtr<ICorProfilerInfo12> m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;
        BOOL m_enabled;



        const DWORD m_dwLowEventMask = COR_PRF_MONITOR::COR_PRF_ENABLE_FUNCTION_ARGS |
            COR_PRF_ENABLE_FUNCTION_RETVAL |
            COR_PRF_ENABLE_FRAME_INFO |
            COR_PRF_MONITOR_ENTERLEAVE;

        const DWORD m_dwHighEventMask = 0;
    public:
        Snapshot(
            const std::shared_ptr<ILogger>& logger,
            ICorProfilerInfo12* profilerInfo);

        HRESULT Enable();
        HRESULT Disable();
        HRESULT Toggle();

        void AddProfilerEventMask(DWORD& eventsLow);
        HRESULT STDMETHODCALLTYPE EnterCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo);
        HRESULT STDMETHODCALLTYPE LeaveCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo);
        HRESULT STDMETHODCALLTYPE TailcallCallback(FunctionIDOrClientID functionId, COR_PRF_ELT_INFO eltInfo);

        mdMethodDef GetMethodDefForFunction(FunctionID functionId);
        ModuleID GetModuleIDForFunction(FunctionID functionId);

    private:
        HRESULT UpdateEventMask(DWORD deltaLowMask, DWORD deltaHighMask, BOOL invert);
};
