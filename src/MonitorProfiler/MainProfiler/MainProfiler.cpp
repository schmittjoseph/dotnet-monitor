// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "MainProfiler.h"
#include "../Environment/EnvironmentHelper.h"
#include "../Environment/ProfilerEnvironment.h"
#include "../Logging/AggregateLogger.h"
#include "../Logging/DebugLogger.h"
#include "../Logging/StdErrLogger.h"
#include "../Stacks/StacksEventProvider.h"
#include "../Stacks/StackSampler.h"
#include "../ProbeInstrumentation/ProbeInstrumentation.h"
#include "../Utilities/ThreadUtilities.h"
#include "corhlpr.h"
#include "macros.h"
#include <memory>

using namespace std;

#define IfFailLogRet(EXPR) IfFailLogRet_(m_pLogger, EXPR)

#ifndef DLLEXPORT
#define DLLEXPORT
#endif

typedef INT32 (STDMETHODCALLTYPE *ManagedMessageCallback)(INT32, INT32, const BYTE*, UINT64);
std::mutex g_messageCallbackMutex;
ManagedMessageCallback g_pManagedMessageCallback = nullptr;

GUID MainProfiler::GetClsid()
{
    // {6A494330-5848-4A23-9D87-0E57BBF6DE79}
    return { 0x6A494330, 0x5848, 0x4A23,{ 0x9D, 0x87, 0x0E, 0x57, 0xBB, 0xF6, 0xDE, 0x79 } };
}

STDMETHODIMP MainProfiler::Initialize(IUnknown *pICorProfilerInfoUnk)
{
    ExpectedPtr(pICorProfilerInfoUnk);

    HRESULT hr = S_OK;

    // These should always be initialized first
    IfFailRet(ProfilerBase::Initialize(pICorProfilerInfoUnk));

    IfFailRet(InitializeCommon());

    return S_OK;
}

STDMETHODIMP MainProfiler::Shutdown()
{
    m_pLogger.reset();
    m_pEnvironment.reset();

    if (m_pProbeInstrumentation)
    {
        m_pProbeInstrumentation->ShutdownBackgroundService();
        m_pProbeInstrumentation.reset();
    }

    _commandServer->Shutdown();
    _commandServer.reset();

    return ProfilerBase::Shutdown();
}

STDMETHODIMP MainProfiler::ThreadCreated(ThreadID threadId)
{
    HRESULT hr = S_OK;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    IfFailLogRet(_threadDataManager->ThreadCreated(threadId));
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    return S_OK;
}

STDMETHODIMP MainProfiler::ThreadDestroyed(ThreadID threadId)
{
    HRESULT hr = S_OK;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    IfFailLogRet(_threadDataManager->ThreadDestroyed(threadId));
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    _threadNameCache->Remove(threadId);

    return S_OK;
}

STDMETHODIMP MainProfiler::ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[])
{
    if (name != nullptr && cchName > 0)
    {
        _threadNameCache->Set(threadId, tstring(name, cchName));
    }

    return S_OK;
}

STDMETHODIMP MainProfiler::ExceptionThrown(ObjectID thrownObjectId)
{
    HRESULT hr = S_OK;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadID threadId;
    IfFailLogRet(m_pCorProfilerInfo->GetCurrentThreadID(&threadId));

    IfFailLogRet(_exceptionTracker->ExceptionThrown(threadId, thrownObjectId));
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    return S_OK;
}

STDMETHODIMP MainProfiler::ExceptionSearchCatcherFound(FunctionID functionId)
{
    HRESULT hr = S_OK;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadID threadId;
    IfFailLogRet(m_pCorProfilerInfo->GetCurrentThreadID(&threadId));

    IfFailLogRet(_exceptionTracker->ExceptionSearchCatcherFound(threadId, functionId));
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    return S_OK;
}

STDMETHODIMP MainProfiler::ExceptionUnwindFunctionEnter(FunctionID functionId)
{
    HRESULT hr = S_OK;

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadID threadId;
    IfFailLogRet(m_pCorProfilerInfo->GetCurrentThreadID(&threadId));

    IfFailLogRet(_exceptionTracker->ExceptionUnwindFunctionEnter(threadId, functionId));
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    return S_OK;
}

STDMETHODIMP MainProfiler::InitializeForAttach(IUnknown* pCorProfilerInfoUnk, void* pvClientData, UINT cbClientData)
{
    HRESULT hr = S_OK;

    // These should always be initialized first
    IfFailRet(ProfilerBase::Initialize(pCorProfilerInfoUnk));

    IfFailRet(InitializeCommon());

    return S_OK;
}

STDMETHODIMP MainProfiler::LoadAsNotificationOnly(BOOL *pbNotificationOnly)
{
    ExpectedPtr(pbNotificationOnly);

    // JSFIX: Split this into two profilers, one notification-only and one capable of mutating the target app.
    *pbNotificationOnly = FALSE;

    return S_OK;
}

HRESULT MainProfiler::InitializeCommon()
{
    HRESULT hr = S_OK;

    // These are created in dependency order!
    IfFailRet(InitializeEnvironment());
    IfFailRet(InitializeLogging());
    IfFailRet(InitializeEnvironmentHelper());

    // Logging is initialized and can now be used

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    _threadDataManager = make_shared<ThreadDataManager>(m_pLogger);
    IfNullRet(_threadDataManager);
    _exceptionTracker.reset(new (nothrow) ExceptionTracker(m_pLogger, _threadDataManager, m_pCorProfilerInfo));
    IfNullRet(_exceptionTracker);
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    // Set product version environment variable to allow discovery of if the profiler
    // as been applied to a target process. Diagnostic tools must use the diagnostic
    // communication channel's GetProcessEnvironment command to get this value.
    IfFailLogRet(_environmentHelper->SetProductVersion());

    DWORD eventsLow = COR_PRF_MONITOR::COR_PRF_MONITOR_NONE;
#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadDataManager::AddProfilerEventMask(eventsLow);
    _exceptionTracker->AddProfilerEventMask(eventsLow);
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS
    StackSampler::AddProfilerEventMask(eventsLow);
    
    _threadNameCache = make_shared<ThreadNameCache>();

    bool enableParameterCapturing;
    IfFailLogRet(_environmentHelper->GetIsParameterCapturingEnabled(enableParameterCapturing));
    if (enableParameterCapturing)
    {
        m_pProbeInstrumentation.reset(new (nothrow) ProbeInstrumentation(m_pLogger, m_pCorProfilerInfo));
        IfNullRet(m_pProbeInstrumentation);
        m_pProbeInstrumentation->AddProfilerEventMask(eventsLow);
    }
    else
    {
        ProbeInstrumentation::DisableIncomingRequests();
    }

    IfFailRet(m_pCorProfilerInfo->SetEventMask2(
        eventsLow,
        COR_PRF_HIGH_MONITOR::COR_PRF_HIGH_MONITOR_NONE));

    if (enableParameterCapturing)
    {
        IfFailLogRet(m_pProbeInstrumentation->InitBackgroundService());
    }

    //Initialize this last. The CommandServer creates secondary threads, which will be difficult to cleanup if profiler initialization fails.
    IfFailLogRet(InitializeCommandServer());

    return S_OK;
}

HRESULT MainProfiler::InitializeEnvironment()
{
    if (m_pEnvironment)
    {
        return E_UNEXPECTED;
    }
    m_pEnvironment = make_shared<ProfilerEnvironment>(m_pCorProfilerInfo);
    return S_OK;
}

HRESULT MainProfiler::InitializeEnvironmentHelper()
{
    IfNullRet(m_pEnvironment);

    _environmentHelper = make_shared<EnvironmentHelper>(m_pEnvironment, m_pLogger);

    return S_OK;
}

HRESULT MainProfiler::InitializeLogging()
{
    HRESULT hr = S_OK;

    // Create an aggregate logger to allow for multiple logging implementations
    unique_ptr<AggregateLogger> pAggregateLogger(new (nothrow) AggregateLogger());
    IfNullRet(pAggregateLogger);

    shared_ptr<StdErrLogger> pStdErrLogger = make_shared<StdErrLogger>(m_pEnvironment);
    IfNullRet(pStdErrLogger);
    pAggregateLogger->Add(pStdErrLogger);

#ifdef _DEBUG
#ifdef TARGET_WINDOWS
    // Add the debug output logger for when debugging on Windows
    shared_ptr<DebugLogger> pDebugLogger = make_shared<DebugLogger>(m_pEnvironment);
    IfNullRet(pDebugLogger);
    pAggregateLogger->Add(pDebugLogger);
#endif
#endif

    m_pLogger.reset(pAggregateLogger.release());

    return S_OK;
}

HRESULT MainProfiler::InitializeCommandServer()
{
    HRESULT hr = S_OK;

    tstring instanceId;
    IfFailRet(_environmentHelper->GetRuntimeInstanceId(instanceId));

#if TARGET_UNIX
    tstring separator = _T("/");
#else
    tstring separator = _T("\\");
#endif

    tstring sharedPath;
    IfFailRet(_environmentHelper->GetSharedPath(sharedPath));

    _commandServer = std::unique_ptr<CommandServer>(new CommandServer(m_pLogger, m_pCorProfilerInfo));
    tstring socketPath = sharedPath + separator + instanceId + _T(".sock");

    IfFailRet(_commandServer->Start(to_string(socketPath), [this](const IpcMessage& message)-> HRESULT { return this->MessageCallback(message); }));

    return S_OK;
}

HRESULT MainProfiler::MessageCallback(const IpcMessage& message)
{
    HRESULT hr;
    m_pLogger->Log(LogLevel::Debug, _LS("Message received from client (MessageType: %d, PayloadType: %d)"), message.MessageType, message.PayloadType);

    if (message.PayloadType == PayloadType::None)
    {
        if (message.MessageType == MessageType::Callstack)
        {
            //Currently we do not have any options for this message
            return ProcessCallstackMessage();
        }
    }
    else if (message.PayloadType == PayloadType::Utf8Json)
    {
        // Utf8 json payloads are handled exclusively by managed code.
        lock_guard<mutex> lock(g_messageCallbackMutex);
        if (g_pManagedMessageCallback == nullptr)
        {
            return E_FAIL;
        }

        IfFailRet(g_pManagedMessageCallback(
            static_cast<INT32>(message.PayloadType),
            static_cast<INT32>(message.MessageType),
            message.Payload.data(),
            message.Payload.size()));

        return S_OK;
    }

    return E_FAIL;
}

HRESULT MainProfiler::ProcessCallstackMessage()
{
    HRESULT hr;

    StackSampler stackSampler(m_pCorProfilerInfo);
    std::vector<std::unique_ptr<StackSamplerState>> stackStates;
    std::shared_ptr<NameCache> nameCache;

    IfFailLogRet(stackSampler.CreateCallstack(stackStates, nameCache, _threadNameCache));

    std::unique_ptr<StacksEventProvider> eventProvider;
    IfFailLogRet(StacksEventProvider::CreateProvider(m_pCorProfilerInfo, eventProvider));

    for (auto& entry : nameCache->GetFunctions())
    {
        IfFailLogRet(eventProvider->WriteFunctionData(entry.first, *entry.second.get()));
    }
    for (auto& entry : nameCache->GetClasses())
    {
        IfFailLogRet(eventProvider->WriteClassData(entry.first, *entry.second.get()));
    }
    for (auto& entry : nameCache->GetModules())
    {
        IfFailLogRet(eventProvider->WriteModuleData(entry.first, *entry.second.get()));
    }
    for (auto& entry : nameCache->GetTypeNames())
    {
        //first: (Module,TypeDef)
        IfFailLogRet(eventProvider->WriteTokenData(entry.first.first, entry.first.second, *entry.second.get()));
    }

    for (std::unique_ptr<StackSamplerState>& stackState : stackStates)
    {
        IfFailLogRet(eventProvider->WriteCallstack(stackState->GetStack()));
    }

    //HACK See https://github.com/dotnet/runtime/issues/76704
    // We sleep here for 200ms to ensure that our event is timestamped. Since we are on a dedicated message
    // thread we should not be interfering with the app itself.
    ThreadUtilities::Sleep(200);

    IfFailLogRet(eventProvider->WriteEndEvent());

    return S_OK;
}

HRESULT STDMETHODCALLTYPE MainProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    if (m_pProbeInstrumentation)
    {
        return m_pProbeInstrumentation->GetReJITParameters(moduleId, methodId, pFunctionControl);
    }
    
    return S_OK;
}

STDAPI DLLEXPORT RegisterMonitorMessageCallback(
    ManagedMessageCallback pCallback
    )
{
    if (g_pManagedMessageCallback != nullptr)
    {
        return E_FAIL;
    }

    lock_guard<mutex> lock(g_messageCallbackMutex);
    if (g_pManagedMessageCallback != nullptr)
    {
        return E_FAIL;
    }

    g_pManagedMessageCallback = pCallback;
    return S_OK;
}