// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "cor.h"
#include "corprof.h"
#include "com.h"
#include "tstring.h"
#include "../Logging/Logger.h"

#include <unordered_map>

#include "AssemblyProbeCacheEntry.h"
#include "InstrumentationRequest.h"
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

        ICorProfilerInfo12* m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;

        std::thread _workerThread;
        BlockingQueue<WorkerMessage> _workerQueue;

        FunctionID m_enterProbeId;
        mdMethodDef m_enterProbeDef;

        ModuleID m_resolvedCorLibId;
        tstring m_resolvedCorLibName;

        std::vector<std::pair<FunctionID, std::vector<UINT32>>> m_RequestedFunctionIds;

        std::unordered_map<ModuleID, struct AssemblyProbeCacheEntry> m_AssemblyProbeCache;
        std::unordered_map<std::pair<ModuleID, mdMethodDef>, struct InstrumentationRequest, PairHash<ModuleID, mdMethodDef>> m_InstrumentationRequests;

        std::mutex m_RequestProcessingMutex;

        HRESULT GetTokenForType(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdToken tkResolutionScope,
            tstring name,
            mdToken* ptkType);

        HRESULT EmitProbeReference(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdMemberRef* ptkProbeMemberRef);

        HRESULT EmitNecessaryCorLibTypeTokens(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            struct CorLibTypeTokens* pCorLibTypeTokens);

        HRESULT PrepareAssemblyForProbes(
            ModuleID moduleId,
            mdMethodDef methodId,
            struct AssemblyProbeCacheEntry** ppAssemblyProbeInformation);

        HRESULT HydrateResolvedCorLib();
        HRESULT HydrateProbeMetadata();

        HRESULT GetTokenForCorLibAssemblyRef(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdAssemblyRef* ptkCorlibAssemblyRef);


        void WorkerThread();
        HRESULT Enable();
        HRESULT Disable();

    public:
        ProbeInstrumentation(
            const std::shared_ptr<ILogger>& logger,
            ICorProfilerInfo12* profilerInfo);

        HRESULT InitBackgroundService();
        void ShutdownBackgroundService();

        BOOL IsEnabled();
        BOOL IsAvailable();

        HRESULT RegisterFunctionProbe(FunctionID enterProbeId);
        HRESULT RequestFunctionProbeShutdown();
        HRESULT RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count, UINT32 boxingTokens[], ULONG boxingTokenCounts[]);

        void AddProfilerEventMask(DWORD& eventsLow);

        HRESULT STDMETHODCALLTYPE GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl);
};
