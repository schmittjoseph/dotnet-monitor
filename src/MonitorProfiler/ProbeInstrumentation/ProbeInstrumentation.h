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
#include <unordered_map>

#include "CorLibTypeTokens.h"
#include "InstrumentationRequest.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include "../Utilities/PairHash.h"
#include "../Utilities/BlockingQueue.h"

class ProbeInstrumentation
{
    private:
        enum WorkerMessage
        {
            INSTALL_PROBES,
            UNINSTALL_PROBES
        };

        ComPtr<ICorProfilerInfo12> m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;

        std::thread _workerThread;
        BlockingQueue<WorkerMessage> _workerQueue;

        
        FunctionID m_enterProbeId;
        FunctionID m_leaveHookId;
        mdMethodDef m_enterProbeDef;

        ModuleID m_resolvedCorLibId;
        tstring m_resolvedCorLibName;

        std::vector<FunctionID> m_RequestedFunctionIds;

        std::unordered_map<ModuleID, struct CorLibTypeTokens> m_ModuleTokens;
        std::unordered_map<std::pair<ModuleID, mdMethodDef>, struct InstrumentationRequest, PairHash<ModuleID, mdMethodDef>> m_InstrumentationRequests;

        bool _isRejitting;
        bool _isEnabled;

        HRESULT GetTokenForType(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            mdToken tkResolutionScope,
            tstring name,
            mdToken* ptkType);

        HRESULT EmitNecessaryCorLibTypeTokens(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            struct CorLibTypeTokens * pCorLibTypeTokens);

        HRESULT PrepareAssemblyForProbes(
            ModuleID moduleId,
            mdMethodDef methodId);

        HRESULT HydrateResolvedCorLib();
        HRESULT HydrateProbeMetadata();

        HRESULT GetTokenForCorLibAssemblyRef(
            ComPtr<IMetaDataImport> pMetadataImport,
            ComPtr<IMetaDataEmit> pMetadataEmit,
            mdAssemblyRef* ptkCorlibAssemblyRef);


        void WorkerThread();


    public:
        ProbeInstrumentation(
            const std::shared_ptr<ILogger>& logger,
            ICorProfilerInfo12* profilerInfo);

        HRESULT InitBackgroundService();
        void ShutdownBackgroundService();

        HRESULT Enable();
        HRESULT Disable();
        BOOL IsEnabled();
        BOOL IsAvailable();

        HRESULT RegisterFunctionProbe(FunctionID enterProbeId);
        HRESULT RequestFunctionProbeShutdown();
        HRESULT RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count);

        void AddProfilerEventMask(DWORD& eventsLow);

        HRESULT STDMETHODCALLTYPE GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl);
};
