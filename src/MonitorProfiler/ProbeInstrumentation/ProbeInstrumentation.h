// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "cor.h"
#include "corprof.h"
#include "com.h"
#include "tstring.h"
#include "../Logging/Logger.h"

#include <unordered_map>
#include <memory>

#include "InstrumentationRequest.h"
#include <mutex>
#include <thread>
#include "../Utilities/PairHash.h"
#include "../Utilities/BlockingQueue.h"
#include "../Utilities/NameCache.h"

class ProbeInstrumentation
{
    private:
        ICorProfilerInfo12* m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;

        std::thread m_probeManagementThread;
        BlockingQueue<WORKER_PAYLOAD> m_probeManagementQueue;

        std::unordered_map<std::pair<ModuleID, mdMethodDef>, INSTRUMENTATION_REQUEST, PairHash<ModuleID, mdMethodDef>> m_activeInstrumentationRequests;

        std::mutex m_requestProcessingMutex;

        /* Cache */
        FunctionID m_enterProbeId;
        mdMethodDef m_enterProbeDef;

        ModuleID m_resolvedCorLibId;
        tstring m_resolvedCorLibName;

        NameCache m_nameCache;


        std::unordered_map<ModuleID, std::shared_ptr<ASSEMBLY_PROBE_CACHE_ENTRY>> m_AssemblyProbeCache;

    private:
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
            COR_LIB_TYPE_TOKENS* pCorLibTypeTokens);

        HRESULT PrepareAssemblyForProbes(
            ModuleID moduleId,
            mdMethodDef methodId,
            std::shared_ptr<ASSEMBLY_PROBE_CACHE_ENTRY>& assemblyProbeInformation);

        HRESULT HydrateResolvedCorLib();
        HRESULT HydrateProbeMetadata();

        HRESULT GetTokenForCorLibAssemblyRef(
            IMetaDataImport* pMetadataImport,
            IMetaDataEmit* pMetadataEmit,
            mdAssemblyRef* ptkCorlibAssemblyRef);

        void WorkerThread();
        HRESULT Enable(std::vector<UNPROCESSED_INSTRUMENTATION_REQUEST>& requests);
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
