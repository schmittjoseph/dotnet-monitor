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
    public:
        static std::shared_ptr<Snapshot> s_snapshotter;

    private:
        ComPtr<ICorProfilerInfo12> m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;
        BOOL m_enabled;

    public:
        Snapshot(
            const std::shared_ptr<ILogger>& logger,
            ICorProfilerInfo12* profilerInfo);

        HRESULT Enable(FunctionID funcId);
        HRESULT Disable(FunctionID funcId);
        HRESULT Toggle(FunctionID funcId);

        void AddProfilerEventMask(DWORD& eventsLow);
        HRESULT STDMETHODCALLTYPE EnterCallback(FunctionID functionId);
        HRESULT STDMETHODCALLTYPE LeaveCallback(FunctionID functionId);

        HRESULT STDMETHODCALLTYPE ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl);

        mdMethodDef GetMethodDefForFunction(FunctionID functionId);
        ModuleID GetModuleIDForFunction(FunctionID functionId);
};
