// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "cor.h"
#include "corprof.h"
#include "com.h"
#include "../Logging/Logger.h"

#include <unordered_map>
#include <memory>

#include "InstrumentationRequest.h"
#include "AssemblyProbePrep.h"
#include <mutex>
#include <thread>
#include "../Utilities/PairHash.h"
#include "../Utilities/BlockingQueue.h"

// JSFIX: Not worker anymore
typedef enum WorkerMessage
{
    INSTALL_PROBES,
    UNINSTALL_PROBES
} WorkerMessage;

typedef struct _WORKER_PAYLOAD
{
    WorkerMessage message;
    std::vector<UNPROCESSED_INSTRUMENTATION_REQUEST> requests;
} WORKER_PAYLOAD;

class ProbeInstrumentation
{
    private:
        ICorProfilerInfo12* m_pCorProfilerInfo;
        std::shared_ptr<ILogger> m_pLogger;

        FunctionID m_probeFunctionId;
        std::unique_ptr<AssemblyProbePrep> m_pAssemblyProbePrep;

        /* Probe management */
        std::thread m_probeManagementThread;
        BlockingQueue<WORKER_PAYLOAD> m_probeManagementQueue;
        std::unordered_map<std::pair<ModuleID, mdMethodDef>, INSTRUMENTATION_REQUEST, PairHash<ModuleID, mdMethodDef>> m_activeInstrumentationRequests;
        std::mutex m_requestProcessingMutex;

    private:
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
