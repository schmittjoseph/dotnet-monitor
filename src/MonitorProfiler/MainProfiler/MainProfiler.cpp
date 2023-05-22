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

shared_ptr<MainProfiler> MainProfiler::s_profiler;

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

    MainProfiler::s_profiler = shared_ptr<MainProfiler>(this);

    IfFailRet(InitializeCommon());

    return S_OK;
}

STDMETHODIMP MainProfiler::Shutdown()
{
    m_pLogger.reset();
    m_pEnvironment.reset();

    m_pProbeInstrumentation->ShutdownBackgroundService();
    m_pProbeInstrumentation.reset();

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

    m_pProbeInstrumentation.reset(new (nothrow) ProbeInstrumentation(m_pLogger, m_pCorProfilerInfo));
    IfNullRet(m_pProbeInstrumentation);

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
    
    m_pProbeInstrumentation->AddProfilerEventMask(eventsLow);

    _threadNameCache = make_shared<ThreadNameCache>();

    IfFailRet(m_pCorProfilerInfo->SetEventMask2(
        eventsLow,
        COR_PRF_HIGH_MONITOR::COR_PRF_HIGH_MONITOR_NONE));

    IfFailLogRet(m_pProbeInstrumentation->InitBackgroundService());

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
    m_pLogger->Log(LogLevel::Information, _LS("Message received from client: %d %d"), message.MessageType, message.Parameters);

    if (message.MessageType == MessageType::Callstack)
    {
        //Currently we do not have any options for this message
        return ProcessCallstackMessage();
    }

    return S_OK;
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
    return m_pProbeInstrumentation->GetReJITParameters(moduleId, methodId, pFunctionControl);
}

HRESULT STDMETHODCALLTYPE MainProfiler::RequestFunctionProbeUninstallation()
{
    return m_pProbeInstrumentation->RequestFunctionProbeUninstallation();
}

HRESULT STDMETHODCALLTYPE MainProfiler::RegisterFunctionProbe(FunctionID enterProbeId)
{
    return m_pProbeInstrumentation->RegisterFunctionProbe(enterProbeId);
}

HRESULT STDMETHODCALLTYPE MainProfiler::RequestFunctionProbeInstallation(ULONG64 functionIds[], ULONG32 count, ULONG32 argumentBoxingTypes[], ULONG32 argumentCounts[])
{
    return m_pProbeInstrumentation->RequestFunctionProbeInstallation(functionIds, count, argumentBoxingTypes, argumentCounts);
}

#ifndef DLLEXPORT
#define DLLEXPORT
#endif

STDAPI DLLEXPORT RegisterFunctionProbe(ULONG64 enterProbeId)
{
    return MainProfiler::s_profiler->RegisterFunctionProbe((FunctionID)enterProbeId);
}

STDAPI DLLEXPORT RequestFunctionProbeInstallation(ULONG64 functionIds[], ULONG32 count, ULONG32 argumentBoxingTypes[], ULONG32 argumentCounts[])
{
    return MainProfiler::s_profiler->RequestFunctionProbeInstallation(functionIds, count, argumentBoxingTypes, argumentCounts);
}

STDAPI DLLEXPORT RequestFunctionProbeUninstallation()
{
    return MainProfiler::s_profiler->RequestFunctionProbeUninstallation();
}