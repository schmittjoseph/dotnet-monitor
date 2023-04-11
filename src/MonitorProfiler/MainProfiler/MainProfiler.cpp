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
#include "../Snapshot/Snapshot.h"
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

    m_Level = 0;

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

STDMETHODIMP MainProfiler::LoadAsNotficationOnly(BOOL *pbNotificationOnly)
{
    ExpectedPtr(pbNotificationOnly);

    // JSFIX: This is called before init.
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

    // Decide what kind of profiler we will be.
    BOOL isMainProfiler = FALSE; // JSFIX
    if (SUCCEEDED(m_pEnvironmentHelper->GetIsMainProfiler(isMainProfiler))) {
        m_isMainProfiler = isMainProfiler;
    }

    // Logging is initialized and can now be used
    if (m_isMainProfiler) {
        m_pLogger->Log(LogLevel::Warning, _LS("Initializing as main profiler"));
        // Enable snapshotting
        m_pSnapshotter.reset(new (nothrow) Snapshot(m_pLogger, m_pCorProfilerInfo));
        IfNullRet(m_pSnapshotter);
    } else {
        m_pLogger->Log(LogLevel::Warning, _LS("Initializing as notify-only profiler"));
    }

#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    _threadDataManager = make_shared<ThreadDataManager>(m_pLogger);
    IfNullRet(_threadDataManager);
    _exceptionTracker.reset(new (nothrow) ExceptionTracker(m_pLogger, _threadDataManager, m_pCorProfilerInfo));
    IfNullRet(_exceptionTracker);
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS

    // Set product version environment variable to allow discovery of if the profiler
    // as been applied to a target process. Diagnostic tools must use the diagnostic
    // communication channel's GetProcessEnvironment command to get this value.
    IfFailLogRet(m_pEnvironmentHelper->SetProductVersion());

    DWORD eventsLow = COR_PRF_MONITOR::COR_PRF_MONITOR_NONE;
#ifdef DOTNETMONITOR_FEATURE_EXCEPTIONS
    ThreadDataManager::AddProfilerEventMask(eventsLow);
    _exceptionTracker->AddProfilerEventMask(eventsLow);
#endif // DOTNETMONITOR_FEATURE_EXCEPTIONS
    StackSampler::AddProfilerEventMask(eventsLow);
    _threadNameCache = make_shared<ThreadNameCache>();

    if (m_isMainProfiler) {
        m_pSnapshotter->AddProfilerEventMask(eventsLow);
    }

    IfFailLogRet(m_pCorProfilerInfo->SetEventMask2(
        eventsLow,
        COR_PRF_HIGH_MONITOR::COR_PRF_HIGH_MONITOR_NONE));

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

    m_pEnvironmentHelper = make_shared<EnvironmentHelper>(m_pEnvironment, m_pLogger);

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
    IfFailRet(m_pEnvironmentHelper->GetRuntimeInstanceId(instanceId));

#if TARGET_UNIX
    tstring separator = _T("/");
#else
    tstring separator = _T("\\");
#endif

    tstring sharedPath;
    IfFailRet(m_pEnvironmentHelper->GetSharedPath(sharedPath));

    _commandServer = std::unique_ptr<CommandServer>(new CommandServer(m_pLogger, m_pCorProfilerInfo));
    tstring socketPath = sharedPath + separator + instanceId + _T(".sock");

    IfFailRet(_commandServer->Start(to_string(socketPath), [this](const IpcMessage& message)-> HRESULT { return this->MessageCallback(message); }));

    return S_OK;
}

HRESULT MainProfiler::MessageCallback(const IpcMessage& message)
{
    HRESULT hr = S_OK;
    m_pLogger->Log(LogLevel::Information, _LS("Message received from client: %d %d"), message.MessageType, message.Parameters);

    if (message.MessageType == MessageType::Callstack)
    {
        {
            tstring expected = _T("Benchmarks.Controllers.JsonController!JsonNk");
            tstring enterHook = _T("Mvc.Program!EnterHook");
            tstring leaveHook = _T("Mvc.Program!LeaveHook");

            std::lock_guard<std::mutex> lock(_mutex);

            FunctionID enterHookId;
            FunctionID leaveHookId;

            auto const& it = _functionNames.find(enterHook);
            if (it != _functionNames.end())
            {
                enterHookId = it->second;
            } else {
                m_pLogger->Log(LogLevel::Warning, _LS("Could not resolve managed enter hook"));
                return E_FAIL;
            }

            auto const& it2 = _functionNames.find(leaveHook);
            if (it2 != _functionNames.end())
            {
                leaveHookId = it2->second;
            } else {
                m_pLogger->Log(LogLevel::Warning, _LS("Could not resolve managed leave hook"));
                return E_FAIL;
            }

            auto const& it3 = _functionNames.find(expected);
            if (it3 != _functionNames.end())
            {
                m_pLogger->Log(LogLevel::Warning, _LS("Resolved user method to hook"));
                IfFailLogRet(m_pSnapshotter->Toggle(enterHookId, leaveHookId, it3->second));
            } else {
                m_pLogger->Log(LogLevel::Warning, _LS("Could not resolve FunctionID"));
            }
            // Make a string
            // Benchmarks.Controllers.JsonController!JsonNk

        }
        // IfFailLogRet(m_pSnapshotter->Disable());
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

bool hasEnding (std::wstring const &fullString, std::wstring const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}


HRESULT STDMETHODCALLTYPE MainProfiler::JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock)
{
    tstring name;

    name = GetFunctionIDName(functionId);

    if (0 != name.compare(0, 3, _T("Sys")) &&
        0 != name.compare(0, 1, _T(".")) &&
        0 != name.compare(0, 1, _T("<")) &&
        0 != name.compare(0, 3, _T("Mic"))) {
        // m_pLogger->Log(LogLevel::Debug, name);
    }

    // Stash a copy of the friendly name -> this info.

    std::lock_guard<std::mutex> lock(_mutex);

    // Make a string
    _functionNames[std::move(name)] = functionId;


    return S_OK;
}

#define SHORT_LENGTH    32
#define STRING_LENGTH  256
#define LONG_LENGTH   1024
tstring MainProfiler::GetClassIDName(ClassID classId)
{
    ModuleID modId;
    mdTypeDef classToken;
    ClassID parentClassID;
    ULONG32 nTypeArgs;
    ClassID typeArgs[SHORT_LENGTH];
    HRESULT hr = S_OK;

    if (classId == NULL)
    {
        printf("FAIL: Null ClassID passed in\n");
        return _T("");
    }

    hr = m_pCorProfilerInfo->GetClassIDInfo2(classId,
                                           &modId,
                                           &classToken,
                                           &parentClassID,
                                           SHORT_LENGTH,
                                           &nTypeArgs,
                                           typeArgs);
    if (CORPROF_E_CLASSID_IS_ARRAY == hr)
    {
        // We have a ClassID of an array.
        return _T("ArrayClass");
    }
    else if (CORPROF_E_CLASSID_IS_COMPOSITE == hr)
    {
        // We have a composite class
        return _T("CompositeClass");
    }
    else if (CORPROF_E_DATAINCOMPLETE == hr)
    {
        // type-loading is not yet complete. Cannot do anything about it.
        return _T("DataIncomplete");
    }
    else if (FAILED(hr))
    {
        printf("FAIL: GetClassIDInfo returned 0x%x for ClassID %x\n", hr, (unsigned int)classId);
        return _T("GetClassIDNameFailed");
    }

    ComPtr<IMetaDataImport> pMDImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(modId,
                                             (ofRead | ofWrite),
                                             IID_IMetaDataImport,
                                             (IUnknown **)&pMDImport );
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return _T("ClassIDLookupFailed");
    }

    WCHAR wName[LONG_LENGTH];
    DWORD dwTypeDefFlags = 0;
    hr = pMDImport->GetTypeDefProps(classToken,
                                         wName,
                                         LONG_LENGTH,
                                         NULL,
                                         &dwTypeDefFlags,
                                         NULL);
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return _T("ClassIDLookupFailed");
    }

    tstring name = wName;
    if (nTypeArgs > 0)
        name += _T("<");

    for(ULONG32 i = 0; i < nTypeArgs; i++)
    {

        tstring typeArgClassName;
        typeArgClassName.clear();
        name += GetClassIDName(typeArgs[i]);

        if ((i + 1) != nTypeArgs)
            name += _T(", ");
    }

    if (nTypeArgs > 0)
        name += _T(">");

    return name;
}

tstring MainProfiler::GetFunctionIDName(FunctionID funcId)
{
    // If the FunctionID is 0, we could be dealing with a native function.
    if (funcId == 0)
    {
        return _T("Unknown_Native_Function");
    }

    tstring name;

    ClassID classId = NULL;
    ModuleID moduleId = NULL;
    mdToken token = NULL;
    ULONG32 nTypeArgs = NULL;
    ClassID typeArgs[SHORT_LENGTH];
    COR_PRF_FRAME_INFO frameInfo = NULL;

    HRESULT hr = S_OK;
    hr = m_pCorProfilerInfo->GetFunctionInfo2(funcId,
                                            frameInfo,
                                            &classId,
                                            &moduleId,
                                            &token,
                                            SHORT_LENGTH,
                                            &nTypeArgs,
                                            typeArgs);
    if (FAILED(hr))
    {
        printf("FAIL: GetFunctionInfo2 call failed with hr=0x%x\n", hr);
        return _T("FuncNameLookupFailed");
    }

    ComPtr<IMetaDataImport> pIMDImport;
    hr = m_pCorProfilerInfo->GetModuleMetaData(moduleId,
                                             ofRead,
                                             IID_IMetaDataImport,
                                             (IUnknown **)&pIMDImport);
    if (FAILED(hr))
    {
        printf("FAIL: GetModuleMetaData call failed with hr=0x%x\n", hr);
        return _T("FuncNameLookupFailed");
    }

    WCHAR funcName[STRING_LENGTH];
    hr = pIMDImport->GetMethodProps(token,
                                    NULL,
                                    funcName,
                                    STRING_LENGTH,
                                    0,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
    if (FAILED(hr))
    {
        printf("FAIL: GetMethodProps call failed with hr=0x%x", hr);
        return _T("FuncNameLookupFailed");
    }


    if (classId != NULL) {
        name += GetClassIDName(classId);
        name += _T("!");
    }

    name += funcName;

    // Fill in the type parameters of the generic method
    if (nTypeArgs > 0)
        name += _T("<");

    for(ULONG32 i = 0; i < nTypeArgs; i++)
    {
        name += GetClassIDName(typeArgs[i]);

        if ((i + 1) != nTypeArgs)
            name += _T(", ");
    }

    if (nTypeArgs > 0)
        name += _T(">");

    return name;
}

HRESULT STDMETHODCALLTYPE MainProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl* pFunctionControl)
{
    return m_pSnapshotter->ReJITHandler(moduleId, methodId, pFunctionControl);
}

BSTR STDMETHODCALLTYPE MainProfiler::GetLogMessage(PINT32 level)
{
    m_Level++;
    if (m_Level > 5) {
        m_Level = 0;
    }
    *level = m_Level;

    return ::SysAllocString(L"Hello from the profiler!");
}

#ifndef DLLEXPORT
#define DLLEXPORT
#endif // DLLEXPORT

STDAPI_(BSTR) DLLEXPORT GetLogMessage(PINT32 level)
{
    return MainProfiler::s_profiler->GetLogMessage(level);
}