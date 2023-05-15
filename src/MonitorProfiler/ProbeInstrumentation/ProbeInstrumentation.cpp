// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "corhlpr.h"

#include "ProbeInstrumentation.h"
#include "ProbeInjector.h"

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

ProbeInstrumentation::ProbeInstrumentation(const shared_ptr<ILogger>& logger, ICorProfilerInfo12* profilerInfo) :
    m_pCorProfilerInfo(profilerInfo),
    m_pLogger(logger),
    m_probeFunctionId(0),
    m_pAssemblyProbePrep(nullptr)
{
}

HRESULT ProbeInstrumentation::RegisterFunctionProbe(FunctionID enterProbeId)
{
    if (IsAvailable())
    {
        // Probes have already been pinned.
        return E_FAIL;
    }

    m_pLogger->Log(LogLevel::Information, _LS("Received probes."));

    // JSFIX: Validate the probe's signature before pinning it.
    m_probeFunctionId = enterProbeId;
    
    return S_OK;
}

HRESULT ProbeInstrumentation::InitBackgroundService()
{
    m_probeManagementThread = thread(&ProbeInstrumentation::WorkerThread, this);
    return S_OK;
}

void ProbeInstrumentation::WorkerThread()
{
    HRESULT hr = m_pCorProfilerInfo->InitializeCurrentThread();
    if (FAILED(hr))
    {
        m_pLogger->Log(LogLevel::Error, _LS("Unable to initialize thread: 0x%08x"), hr);
        return;
    }

    while (true)
    {
        WORKER_PAYLOAD payload;
        hr = m_probeManagementQueue.BlockingDequeue(payload);
        if (hr != S_OK)
        {
            break;
        }

        switch (payload.message)
        {
        case INSTALL_PROBES:
            hr = Enable(payload.requests);
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to install probes: 0x%08x"), hr);
            }
            break;

        case UNINSTALL_PROBES:
            hr = Disable();
            if (hr != S_OK)
            {
                m_pLogger->Log(LogLevel::Error, _LS("Failed to uninstall probes: 0x%08x"), hr);
            }
            break;

        default:
            m_pLogger->Log(LogLevel::Error, _LS("Unknown message"));
            break;
        }
    }
}

void ProbeInstrumentation::ShutdownBackgroundService()
{
    m_probeManagementQueue.Complete();
    m_probeManagementThread.join();
}

HRESULT ProbeInstrumentation::RequestFunctionProbeInstallation(UINT64 functionIds[], ULONG count, UINT32 boxingTokens[], ULONG boxingTokenCounts[])
{
    m_pLogger->Log(LogLevel::Information, _LS("Probe installation requested"));

    vector<UNPROCESSED_INSTRUMENTATION_REQUEST> requests;
    requests.reserve(count);

    ULONG offset = 0;
    for (ULONG i = 0; i < count; i++)
    {
        vector<UINT32> tokens;
        ULONG j;
        for (j = 0; j < boxingTokenCounts[i]; j++)
        {
            tokens.push_back(boxingTokens[offset+j]);
        }

        offset += j;

        UNPROCESSED_INSTRUMENTATION_REQUEST request;
        request.functionId = (FunctionID)functionIds[i];
        request.tkBoxingTypes = tokens;

        requests.push_back(request);
    }

    m_probeManagementQueue.Enqueue({WorkerMessage::INSTALL_PROBES, requests});

    return S_OK;
}

HRESULT ProbeInstrumentation::RequestFunctionProbeShutdown()
{
    m_pLogger->Log(LogLevel::Debug, _LS("Probe shutdown requested"));

    if (!IsAvailable())
    {
        return S_FALSE;
    }

    WORKER_PAYLOAD payload = {};
    payload.message = WorkerMessage::UNINSTALL_PROBES;
    m_probeManagementQueue.Enqueue(payload);

    return S_OK;
}

BOOL ProbeInstrumentation::IsAvailable()
{
    return m_probeFunctionId != 0;
}

HRESULT ProbeInstrumentation::Enable(vector<UNPROCESSED_INSTRUMENTATION_REQUEST>& requests)
{
    HRESULT hr;

    lock_guard<mutex> lock(m_requestProcessingMutex);

    if (!IsAvailable() ||
        IsEnabled())
    {
        return E_FAIL;
    }

    if (!m_pAssemblyProbePrep)
    {
        m_pAssemblyProbePrep.reset(new (nothrow) AssemblyProbePrep(m_pCorProfilerInfo, m_probeFunctionId));
        IfNullRet(m_pAssemblyProbePrep);
    }

    unordered_map<pair<ModuleID, mdMethodDef>, INSTRUMENTATION_REQUEST, PairHash<ModuleID, mdMethodDef>> newRequests;

    vector<ModuleID> requestedModuleIds;
    vector<mdMethodDef> requestedMethodDefs;

    requestedModuleIds.reserve(requests.size());
    requestedMethodDefs.reserve(requests.size());

    for (auto const& req : requests)
    {
        INSTRUMENTATION_REQUEST request;
        request.uniquifier = (UINT64)req.functionId;
        request.tkBoxingTypes = req.tkBoxingTypes;

        IfFailLogRet(m_pCorProfilerInfo->GetFunctionInfo2(
            req.functionId,
            NULL,
            nullptr,
            &request.moduleId,
            &request.methodDef,
            0,
            nullptr,
            nullptr));

        mdMemberRef tkProbeFunction = mdMemberRefNil;
        IfFailLogRet(m_pAssemblyProbePrep->PrepareAssemblyForProbes(request.moduleId));

        requestedModuleIds.push_back(request.moduleId);
        requestedMethodDefs.push_back(request.methodDef);

        if (!m_pAssemblyProbePrep->TryGetAssemblyPrepData(request.moduleId, request.pAssemblyData))
        {
            return E_UNEXPECTED;
        }

        newRequests.insert({{request.moduleId, request.methodDef}, request});
    }

    IfFailLogRet(m_pCorProfilerInfo->RequestReJITWithInliners(
        COR_PRF_REJIT_BLOCK_INLINING | COR_PRF_REJIT_INLINING_CALLBACKS,
        (ULONG)requestedModuleIds.size(),
        requestedModuleIds.data(),
        requestedMethodDefs.data()));

    m_activeInstrumentationRequests = newRequests;

    return S_OK;
}

HRESULT ProbeInstrumentation::Disable()
{
    HRESULT hr;

    lock_guard<mutex> lock(m_requestProcessingMutex);

    if (!IsEnabled())
    {
        return S_FALSE;
    }

    vector<ModuleID> moduleIds;
    vector<mdMethodDef> methodDefs;

    moduleIds.reserve(m_activeInstrumentationRequests.size());
    methodDefs.reserve(m_activeInstrumentationRequests.size());

    for (auto const& requestData: m_activeInstrumentationRequests)
    {
        auto const& methodInfo = requestData.first;
        moduleIds.push_back(methodInfo.first);
        methodDefs.push_back(methodInfo.second);
    }

    IfFailLogRet(m_pCorProfilerInfo->RequestRevert(
        (ULONG)moduleIds.size(),
        moduleIds.data(),
        methodDefs.data(),
        nullptr));

    m_activeInstrumentationRequests.clear();

    return S_OK;
}

BOOL ProbeInstrumentation::IsEnabled()
{
    return !m_activeInstrumentationRequests.empty();
}

void ProbeInstrumentation::AddProfilerEventMask(DWORD& eventsLow)
{
    eventsLow |= COR_PRF_MONITOR::COR_PRF_ENABLE_REJIT;
}

HRESULT STDMETHODCALLTYPE ProbeInstrumentation::GetReJITParameters(ModuleID moduleId, mdMethodDef methodDef, ICorProfilerFunctionControl* pFunctionControl)
{
    HRESULT hr;

    INSTRUMENTATION_REQUEST* pRequest;
    {
        lock_guard<mutex> lock(m_requestProcessingMutex);
        auto const& it = m_activeInstrumentationRequests.find({moduleId, methodDef});
        if (it == m_activeInstrumentationRequests.end())
        {
            return E_FAIL;
        }
        pRequest = &it->second;
    }

    hr = ProbeInjector::InstallProbe(
        m_pCorProfilerInfo,
        pFunctionControl,
        pRequest);

    if (FAILED(hr))
    {
        m_pLogger->Log(LogLevel::Error, _LS("Failed to install probes, reverting (hr: 0x%08x)"), hr);
        RequestFunctionProbeShutdown();
        return hr;
    }

    return S_OK;
}