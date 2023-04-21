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

#include "CorLibTypeTokens.h"
#include <condition_variable>
#include <mutex>

class Snapshot
{
    private:
        ComPtr<ICorProfilerInfo12> m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;
        
        FunctionID m_enterHookId;
        FunctionID m_leaveHookId;
        ModuleID m_resolvedCorLibId;

        std::vector<ModuleID> m_EnabledModuleIds;
        std::vector<mdToken> m_EnabledMethodDefs;

        std::condition_variable _rejitFinished;
        std::mutex _rejitMutex;
        bool _rejitDidFinish;


        HRESULT Snapshot::GetTokenForType(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            mdToken tkResolutionScope,
            tstring name,
            mdToken* ptkType);

        HRESULT EmitNecessaryCorLibTypeTokens(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            struct CorLibTypeTokens * pCorLibTypeTokens);

        HRESULT ResolveAllProbes();
        HRESULT ResolveCorLib(ModuleID *pCorLibModuleId);
        HRESULT GetTokenForExistingCorLibAssemblyRef(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            mdAssemblyRef* pTkMscorlibAssemblyRef);

    public:
        Snapshot(
            const std::shared_ptr<ILogger>& logger,
            ICorProfilerInfo12* profilerInfo);

        HRESULT Enable(tstring methodName);
        HRESULT Disable();
        BOOL IsEnabled();

        HRESULT RegisterFunctionProbes(FunctionID enterProbeID, FunctionID leaveProbeID);

        void AddProfilerEventMask(DWORD& eventsLow);

        HRESULT STDMETHODCALLTYPE ReJITHandler(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl);

        mdMethodDef GetMethodDefForFunction(FunctionID functionId);
};
